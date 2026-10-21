"""Check YellowData against the output contract and against the pokeyellow sources.

    python verify.py [--data <YellowData dir>] [--pokeyellow <clone>]

Exit code 0 only when every check passes. Prints a 20x18 ASCII dump of Pallet Town's tiles at the end.
Object counts and warps are compared with the retail build of each objects file: IF DEF(_DEBUG) blocks (such as the
four debug warps in RedsHouse2F.asm) are dropped first, as textlib does for text.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import asmlib  # noqa: E402
import pngio  # noqa: E402
from extract import DEFAULT_CLONE, DEFAULT_OUT, IN_SCOPE  # noqa: E402
from textlib import drop_conditionals  # noqa: E402

TILE_CHARS = set(".g c#wv<>d=x".replace(" ", ""))
LOOK_CHARS = set("psfGTFWyrLBRDnSCcuomkbtPhlVKHZ?")
MAP_FIELDS = ["id", "name", "width", "height", "outdoor", "cave", "origin", "tileset", "music", "palette",
              "encounter_rate", "connections", "tiles", "looks", "warps", "signs", "npcs", "items", "hidden"]
OPPOSITE = {"north": "south", "south": "north", "east": "west", "west": "east"}

failures = []
passes = 0


def check(ok, msg):
    global passes
    if ok:
        passes += 1
    else:
        failures.append(msg)
    return ok


def load(d, rel):
    with open(os.path.join(d, rel), encoding="utf-8") as f:
        return json.load(f)


def source_lines(clone, label):
    """The retail build's lines of data/maps/objects/<label>.asm: comments gone and the IF DEF(_DEBUG) (and other
    conditional) branches blanked, as the ROM is built (textlib.drop_conditionals, as for text)."""
    return drop_conditionals(asmlib.read_lines(os.path.join(clone, "data", "maps", "objects", label + ".asm")))


def source_counts(clone, label):
    text = "\n".join(source_lines(clone, label))
    return {k: len(re.findall(r"^\s*%s\b" % k, text, re.M)) for k in ("warp_event", "bg_event", "object_event")}


def source_warps(clone, label):
    """[(x, y, to_map, to_warp)] of the retail build, in order."""
    out = []
    for line in source_lines(clone, label):
        op, args = asmlib.macro(line)
        if op == "warp_event":
            out.append((asmlib.num(args[0]), asmlib.num(args[1]), args[2], asmlib.num(args[3])))
    return out


def source_hidden_items(clone, const):
    text = open(os.path.join(clone, "data", "events", "hidden_events.asm"), encoding="utf-8").read()
    m = re.search(r"hidden_events_for %s\n(.*?)db -1" % const, text, re.S)
    return len(re.findall(r"HiddenItems", m.group(1))) if m else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default=DEFAULT_OUT)
    ap.add_argument("--pokeyellow", default=DEFAULT_CLONE)
    a = ap.parse_args()
    d = a.data

    manifest = load(d, "manifest.json")
    check(manifest.get("version") == 1, "manifest version is 1")
    check(re.match(r"^[0-9a-f]{40}$", manifest["source"]["commit"] or "") is not None, "manifest has a source commit")
    check(manifest["maps"] == IN_SCOPE, "manifest lists exactly the in-scope maps")
    text = load(d, "text.json")
    species = load(d, "species.json")
    items = load(d, "items.json")
    trainers = load(d, "trainers.json")
    maps = {c: load(d, "maps/%s.json" % c) for c in manifest["maps"]}

    # --- schema, rows, tiles
    for c, m in maps.items():
        for f in MAP_FIELDS:
            check(f in m, "%s has field %s" % (c, f))
        check(m["id"] == c, "%s id matches file" % c)
        check(len(m["tiles"]) == m["height"] and len(m["looks"]) == m["height"], "%s has height rows" % c)
        for y, (tr, lr) in enumerate(zip(m["tiles"], m["looks"])):
            check(len(tr) == m["width"], "%s tiles row %d has width %d" % (c, y, m["width"]))
            check(len(lr) == m["width"], "%s looks row %d has width %d" % (c, y, m["width"]))
            check(set(tr) <= TILE_CHARS, "%s tiles row %d uses only legend chars: %s" % (c, y, set(tr) - TILE_CHARS))
            check(set(lr) <= LOOK_CHARS, "%s looks row %d uses only legend chars: %s" % (c, y, set(lr) - LOOK_CHARS))
        check(m["looks"] and all("?" not in r for r in m["looks"]), "%s has no unclassified looks" % c)
        for w in m["warps"]:
            check(m["tiles"][w["y"]][w["x"]] == "d", "%s warp at %d,%d is a d tile" % (c, w["x"], w["y"]))
        for fld in ("outdoor", "cave"):
            check(isinstance(m[fld], bool), "%s %s is bool" % (c, fld))
    check(maps["PALLET_TOWN"]["width"] == 20 and maps["PALLET_TOWN"]["height"] == 18, "PALLET_TOWN is 20x18")
    check(maps["ROUTE_1"]["width"] == 20 and maps["ROUTE_1"]["height"] == 36, "ROUTE_1 is 20x36")
    check(maps["PALLET_TOWN"]["origin"] == {"x": 0, "y": 0}, "PALLET_TOWN origin is 0,0")

    # --- warps
    for c, m in maps.items():
        for w in m["warps"]:
            if w.get("out_of_scope"):
                check(w["to_map"] not in maps, "%s warp to %s marked out of scope correctly" % (c, w["to_map"]))
                continue
            t = maps.get(w["to_map"])
            check(t is not None, "%s warp target %s exists" % (c, w["to_map"]))
            if t:
                check(1 <= w["to_warp"] <= len(t["warps"]), "%s warp -> %s #%d exists" % (c, w["to_map"], w["to_warp"]))

    # --- connections both ways, and no overlaps
    for c, m in maps.items():
        for dname, cn in m["connections"].items():
            if cn.get("out_of_scope"):
                check(cn["map"] not in maps, "%s %s connection out of scope correctly" % (c, dname))
                continue
            back = maps[cn["map"]]["connections"].get(OPPOSITE[dname])
            check(back is not None and back["map"] == c and back["offset"] == -cn["offset"],
                  "%s %s -> %s offset %d matches the way back" % (c, dname, cn["map"], cn["offset"]))
            o, t = m["origin"], maps[cn["map"]]["origin"]
            if dname in ("north", "south"):
                check(t["x"] - o["x"] == cn["offset"], "%s %s placement agrees with offset" % (c, dname))
            else:
                check(t["y"] - o["y"] == cn["offset"], "%s %s placement agrees with offset" % (c, dname))
    rects = [(c, m["origin"]["x"], m["origin"]["y"], m["width"], m["height"]) for c, m in maps.items()]
    for i in range(len(rects)):
        for j in range(i + 1, len(rects)):
            a1, b1 = rects[i], rects[j]
            overlap = a1[1] < b1[1] + b1[3] and b1[1] < a1[1] + a1[3] and a1[2] < b1[2] + b1[4] and b1[2] < a1[2] + a1[4]
            check(not overlap, "%s and %s do not overlap" % (a1[0], b1[0]))
    for c, m in maps.items():
        check(m["outdoor"] == (m["origin"]["y"] < 1000), "%s is %s the interior rows" % (c, "outside" if m["outdoor"] else "inside"))

    # --- object counts against the pokeyellow objects files
    for c, m in maps.items():
        src = source_counts(a.pokeyellow, m["label"])
        check(len(m["warps"]) == src["warp_event"], "%s warps %d == %d" % (c, len(m["warps"]), src["warp_event"]))
        check(len(m["signs"]) == src["bg_event"], "%s signs %d == %d" % (c, len(m["signs"]), src["bg_event"]))
        check(len(m["npcs"]) + len(m["items"]) == src["object_event"],
              "%s npcs+items %d == %d" % (c, len(m["npcs"]) + len(m["items"]), src["object_event"]))
        check(len(m["hidden"]) == source_hidden_items(a.pokeyellow, c), "%s hidden items match" % c)
        # Warp by warp: the same tiles and targets as the retail objects file (LAST_MAP resolved to a map).
        src_w = source_warps(a.pokeyellow, m["label"])
        for k, w in enumerate(m["warps"]):
            if k >= len(src_w):
                check(False, "%s warp %d at %d,%d -> %s is not in the retail build (debug-only or invented)"
                      % (c, k, w["x"], w["y"], w["to_map"]))
                continue
            x, y, to_map, to_warp = src_w[k]
            same = (w["x"], w["y"], w["to_warp"]) == (x, y, to_warp) and (
                w["to_map"] == to_map or (to_map == "LAST_MAP" and w.get("last_map")))
            check(same, "%s warp %d is %d,%d -> %s #%d; retail has %d,%d -> %s #%d"
                  % (c, k, w["x"], w["y"], w["to_map"], w["to_warp"], x, y, to_map, to_warp))

    # --- every text id resolves
    refs = []
    for c, m in maps.items():
        for s in m["signs"]:
            refs += [(c, s["text"])] + [(c, t) for t in s["texts"]]
        for n in m["npcs"] + m["items"]:
            refs += [(c, n["text"])] + [(c, t) for t in n["texts"]]
            tr = n.get("trainer")
            if tr:
                refs += [(c, tr[k]) for k in ("battle_text", "end_text", "after_text") if k in tr]
    events = load(d, "events.json")
    for e in events:
        refs += [(e["id"], t) for t in e["texts"]]
    for k, s in species.items():
        check(isinstance(s["dex_entry"]["text"], str) and s["dex_entry"]["text"], "%s has dex text" % k)
    bad = [(w, t) for w, t in refs if t not in text]
    check(not bad, "every text id resolves (%d refs, missing: %s)" % (len(refs), bad[:5]))
    check(all(isinstance(v, str) for v in text.values()), "every text value is a string")

    # --- tables
    check(len(species) == 151 and sorted(s["dex"] for s in species.values()) == list(range(1, 152)), "species has dex 1-151")
    for k, s in species.items():
        for f in ("dex", "name", "types", "base", "catch_rate", "base_exp", "growth", "learnset", "evolutions", "tm", "dex_entry"):
            check(f in s, "species %s has %s" % (k, f))
        for f in ("category", "height_m", "weight_kg", "text"):
            check(f in s["dex_entry"], "species %s dex_entry has %s" % (k, f))
    marts = load(d, "marts.json")
    vm = marts.get("VIRIDIAN_MART", [])
    check("POKE_BALL" in vm and items["POKE_BALL"]["price"] == 200 and items["POKE_BALL"]["name"] == "POKé BALL",
          "Viridian Mart sells POKé BALL at 200")
    check("POTION" in vm and items["POTION"]["price"] == 300, "Viridian Mart sells POTION at 300")
    for mc, stock in marts.items():
        check(all(i in items for i in stock), "%s stock items exist" % mc)
    wild = load(d, "wild.json")
    r1 = wild["ROUTE_1"]["grass"]
    src = open(os.path.join(a.pokeyellow, "data", "wild", "maps", "Route1.asm"), encoding="utf-8").read()
    src_slots = [[int(l), s] for l, s in re.findall(r"db\s+(\d+),\s*(\w+)", src.split("def_water_wildmons")[0])]
    check({s for _, s in r1} == {"PIDGEY", "RATTATA"}, "ROUTE_1 grass has only Pidgey and Rattata")
    check(r1 == src_slots and len(r1) == 10, "ROUTE_1 grass slots equal Yellow's data/wild/maps/Route1.asm")
    brock = trainers["BROCK"]["parties"][0]
    check([p["species"] for p in brock] == ["GEODUDE", "ONIX"], "Brock's party is Geodude and Onix")
    used = set()
    for mc, t in wild.items():
        used |= {s for _, s in t["grass"] + t.get("water", [])}
    for cls in trainers.values():
        for p in cls["parties"]:
            used |= {m["species"] for m in p}
    for tr in load(d, "trades.json"):
        used |= {tr["give"], tr["get"]}
    for e in events:
        used |= {g["pokemon"] for g in e.get("gives", []) if "pokemon" in g}
    missing = used - set(species)
    check(not missing, "every species used by wild, trainers, events and trades exists (missing %s)" % missing)
    for c, m in maps.items():
        for n in m["npcs"]:
            tr = n.get("trainer")
            if tr:
                cls = trainers.get(tr["class"])
                check(cls is not None and 1 <= tr["party"] <= len(cls["parties"]),
                      "%s %s trainer %s party %d exists" % (c, n["id"], tr["class"], tr["party"]))
        for it in m["items"] + m["hidden"]:
            check(it["item"] in items, "%s item %s exists" % (c, it["item"]))
    for k in ("types.json", "moves.json", "exp.json", "trades.json", "pikachu.json"):
        check(os.path.exists(os.path.join(d, k)), "%s exists" % k)

    # --- pictures
    for c, m in maps.items():
        w, h = pngio.png_size(os.path.join(d, "minimap", c + ".png"))
        check((w, h) == (m["width"] * 16, m["height"] * 16), "minimap %s is %dx%d" % (c, m["width"] * 16, m["height"] * 16))
    k = load(d, "minimap/kanto_outdoor.json")
    w, h = pngio.png_size(os.path.join(d, "minimap", "kanto_outdoor.png"))
    check(k["px_per_tile"] == 16 and (w, h) == (k["size_px"]["w"], k["size_px"]["h"]), "kanto_outdoor size matches json")
    for c, m in maps.items():
        if m["outdoor"]:
            x = k["origin_px"]["x"] + m["origin"]["x"] * 16
            y = k["origin_px"]["y"] + m["origin"]["y"] * 16
            check(0 <= x and 0 <= y and x + m["width"] * 16 <= w and y + m["height"] * 16 <= h, "%s fits in kanto_outdoor" % c)
    check(pngio.png_size(os.path.join(d, "sprites", "red_minimap.png")) == (32, 64), "red_minimap is 32x64 (2 frames x 4 facings)")
    rg = load(d, "minimap/ringed.json")
    for c, m in maps.items():
        if m["outdoor"]:
            continue
        r = rg["ring"]
        check(pngio.png_size(os.path.join(d, "minimap", "ringed", c + ".png")) == ((m["width"] + 2 * r) * 16, (m["height"] + 2 * r) * 16),
              "minimap/ringed/%s is the room plus %d tiles round it" % (c, r))
    sc = load(d, "scenery.json")
    for e in sc["maps"]:
        x = k["origin_px"]["x"] + e["origin"]["x"] * 16
        y = k["origin_px"]["y"] + e["origin"]["y"] * 16
        check(0 <= x and 0 <= y and x + e["width"] * 16 <= w and y + e["height"] * 16 <= h, "scenery %s fits in kanto_outdoor" % e["id"])
    pp = load(d, "sprites/people.json")
    for c, m in maps.items():
        check(m["palette"] in pp["palettes"], "%s palette %s has a people atlas" % (c, m["palette"]))
        for n in m["npcs"]:
            check(n["sprite"] in pp["rows"], "%s %s sprite %s is in the people atlas" % (c, n["id"], n["sprite"]))
    for s in ("SPRITE_RED", "SPRITE_PIKACHU", "SPRITE_POKE_BALL"):
        check(s in pp["rows"], "people atlas has %s" % s)
    for pal in pp["palettes"]:
        check(pngio.png_size(os.path.join(d, "sprites", "people_%s.png" % pal)) == (128, 16 * len(pp["rows"])),
              "people_%s is 128 x 16 per sprite" % pal)
    check(pngio.png_size(os.path.join(d, "townmap", "townmap.png")) == (160, 144), "townmap is 160x144")
    tm = load(d, "townmap/townmap.json")
    check(set(tm) == set(IN_SCOPE), "townmap.json covers every map")
    pk = load(d, "pikachu.json")
    for mood in pk["moods"]:
        check(os.path.exists(os.path.join(d, "pikachu", mood + ".png")), "pikachu/%s.png exists" % mood)

    print("checks passed: %d, failed: %d" % (passes, len(failures)))
    for f in failures[:50]:
        print("FAIL:", f)
    print()
    print("PALLET_TOWN tiles (20x18):")
    for row in maps["PALLET_TOWN"]["tiles"]:
        print("  " + row)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
