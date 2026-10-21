"""events.json (from the hand-checked Tools/yellowdata/events.json) and pikachu.json + pikachu/<mood>.png."""
import os
import re
from collections import OrderedDict

import asmlib
import pngio

# Face names describe each base picture as drawn (looked at by eye). Key: PikaPicAnimScript number.
FACE_NAMES = {
    14: "angry", 6: "turned_away", 13: "sulking", 9: "back_turned", 5: "displeased", 12: "unsure",
    3: "grumpy", 1: "neutral", 8: "content", 4: "happy", 15: "cheerful", 17: "calm", 7: "excited",
    2: "delighted", 16: "loving", 10: "affectionate", 19: "adoring", 20: "overjoyed",
}

EVENT_NAMES = ["levelup", "used_item", "used_x_item", "gym_leader", "used_tm_hm", "walking",
               "deposited", "fainted", "poison_fainted", "careless_trainer", "traded"]


def build(x):
    build_pikachu(x)
    build_events(x)


def _lines(x, rel):
    return x.p.lines(rel)


def build_pikachu(x):
    # constants/pikachu_emotion_constants.asm: PIKAHAPPY_* = 1..11
    happy_consts = [a[0] for o, a in (asmlib.macro(l) for l in _lines(x, "constants/pikachu_emotion_constants.asm"))
                    if o == "const" and a and a[0].startswith("PIKAHAPPY_")]
    change, moods = [], []
    section = None
    for line in _lines(x, "engine/events/pikachu_happiness.asm"):
        if line.startswith("HappinessChangeTable"):
            section = "change"
            continue
        if line.startswith("PikachuMoods"):
            section = "moods"
            continue
        op, args = asmlib.macro(line)
        if op == "db" and section == "change":
            change.append([asmlib.num(a) for a in args])
        elif op == "db" and section == "moods":
            moods.append(asmlib.num(args[0]))
    src = "\n".join(_lines(x, "engine/movie/oak_speech/init_player_data.asm"))
    m = re.search(r"ld a, (\$?\w+)\s*\n\s*ld \[wPikachuHappiness\], a", src)
    start_happiness = asmlib.num(m.group(1))
    m = re.search(r"ld a, (\$?\w+)\s*\n\s*ld \[wPikachuMood\], a", src)
    start_mood = asmlib.num(m.group(1))
    # engine/pikachu/pikachu_pic_animation.asm: mood bands and the happiness x mood face table
    pic_src = _lines(x, "engine/pikachu/pikachu_pic_animation.asm")
    mood_bands, happy_rows = [], []
    section = None
    for line in pic_src:
        if line.startswith("PikachuMoodLookupTable"):
            section = "mood"
            continue
        if line.startswith("PikaPicAnimationScriptPointerLookupTable"):
            section = "face"
            continue
        if line and not line[0].isspace() and section and not line.startswith(("PikachuMood", "PikaPicAnimationScriptPointer")):
            section = None
        op, args = asmlib.macro(line)
        if section == "mood" and op == "db":
            mood_bands.append({"mood_at_most": asmlib.num(args[0]), "column": asmlib.num(args[1])})
        elif section == "face" and op == "db":
            happy_rows.append({"happiness_at_most": asmlib.num(args[0]), "faces": []})
        elif section == "face" and op == "dpikapic":
            happy_rows[-1]["faces"].append(int(args[0].replace("PikaPicAnimScript", "")))
    # base picture per anim script (data/pikachu/pikachu_pic_animation.asm, first pikapic_loadgfx Pic_*)
    pic_of = {}
    pending = []
    for line in _lines(x, "data/pikachu/pikachu_pic_animation.asm"):
        m = re.match(r"PikaPicAnimScript(\d+):", line)
        if m:
            pending.append(int(m.group(1)))
            continue
        op, args = asmlib.macro(line)
        if op == "pikapic_loadgfx" and pending:
            for n in pending:
                pic_of[n] = args[0]
            pending = []
    pal = x.cgb["PAL_PIKACHU_PORTRAIT"]["rgb"]
    d = os.path.join(x.out, "pikachu")
    os.makedirs(d, exist_ok=True)
    faces = OrderedDict()
    used = sorted({f for r in happy_rows for f in r["faces"]})
    for n in used:
        pic = pic_of[n]
        png = "gfx/pikachu/unknown_%s.png" % pic[len("Pic_"):]
        w, h, rows = pngio.read_gray(x.p.path(png))
        px = [pal[v] + (255,) for r in rows for v in r]
        name = FACE_NAMES[n]
        pngio.write_rgba(os.path.join(d, name + ".png"), w, h, px)
        faces[name] = {"anim_script": "PikaPicAnimScript%d" % n, "picture": png, "file": "pikachu/%s.png" % name}
    name_of = {n: FACE_NAMES[n] for n in used}
    table = []
    for r in happy_rows:
        row = {"happiness_at_most": r["happiness_at_most"], "by_mood": []}
        for band in mood_bands:
            row["by_mood"].append({"mood_at_most": band["mood_at_most"], "face": name_of[r["faces"][band["column"] - 1]]})
        table.append(row)
    deltas = OrderedDict()
    for k, const in enumerate(happy_consts):
        deltas[EVENT_NAMES[k]] = {
            "const": const,
            "delta_by_happiness": {"0-99": _signed(change[k][0]), "100-199": _signed(change[k][1]), "200-255": _signed(change[k][2])},
            "mood_target": moods[k] if moods[k] != 0x80 else None,
        }
    out = OrderedDict()
    out["start_happiness"] = start_happiness
    out["start_mood"] = start_mood
    out["happiness_range"] = [0, 255]
    out["mood_range"] = [0, 255]
    out["deltas"] = deltas
    out["moods"] = list(faces.keys())
    out["faces"] = faces
    out["face_table"] = table
    out["_note"] = ("happiness changes by deltas[event].delta_by_happiness[band] (engine/events/pikachu_happiness.asm "
                    "ModifyPikachuHappiness, clamped 0-255). mood_target: a rise moves mood up to it (unless an emotion "
                    "modifier is active), a fall moves it down to it; null = mood unchanged ($80 in PikachuMoods). The face "
                    "shown when talking to Pikachu is face_table: first row whose happiness_at_most >= happiness, then the "
                    "first by_mood entry whose mood_at_most >= mood (engine/pikachu/pikachu_pic_animation.asm). "
                    "Face names describe the pictures; the game only numbers them.")
    out["source"] = ["engine/events/pikachu_happiness.asm", "engine/movie/oak_speech/init_player_data.asm",
                     "engine/pikachu/pikachu_pic_animation.asm", "data/pikachu/pikachu_pic_animation.asm",
                     "constants/pikachu_emotion_constants.asm"]
    x.pikachu = out
    if not table or start_happiness is None:
        raise ValueError('pikachu tables did not parse')
    from extract import jdump
    jdump(os.path.join(x.out, "pikachu.json"), out)


def _signed(v):
    return v - 256 if v >= 128 else v


def build_events(x):
    from extract import jdump, load_json
    hand = load_json("events.json")
    out = []
    flags = set(re.findall(r"\bconst (EVENT_\w+)", open(x.p.path("constants/event_constants.asm"), encoding="utf-8").read()))
    for e in hand["events"]:
        for f in e.get("requires", []) + e.get("requires_not", []) + e.get("sets", []) + e.get("resets", []):
            if isinstance(f, str) and f not in flags:
                raise ValueError("event %s: %s is not in constants/event_constants.asm" % (e["id"], f))
        b = e.get("battle")
        if b and not (b["class"] in x.trainers and 1 <= b["trainer_no"] <= len(x.trainers[b["class"]]["parties"])):
            raise ValueError("event %s: no party %s %s" % (e["id"], b["class"], b["trainer_no"]))
        if e["map"] not in x.maps:
            raise ValueError("event %s: map %s not in scope" % (e["id"], e["map"]))
        md = x.maps[e["map"]]
        if e.get("npc"):
            ids = {n["id"] for n in md["npcs"]} | {n["id"] for n in md["items"]}
            if e["npc"] not in ids:
                raise ValueError("event %s: npc %s not on %s" % (e["id"], e["npc"], e["map"]))
        for t in e["texts"]:
            if x.text_label(t) is None:
                raise ValueError("event %s: text %s is not a text label" % (e["id"], t))
        for g in e.get("gives", []) + e.get("takes", []):
            if "item" in g and g["item"] not in x.items and g["item"] != "MONEY":
                raise ValueError("event %s: unknown item %s" % (e["id"], g["item"]))
            if "pokemon" in g and g["pokemon"] not in x.species:
                raise ValueError("event %s: unknown species %s" % (e["id"], g["pokemon"]))
        out.append(e)
    jdump(os.path.join(x.out, "events.json"), out)
    x.events = out
