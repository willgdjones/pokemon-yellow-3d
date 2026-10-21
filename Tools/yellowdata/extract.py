"""Extract Game Boy Pokemon Yellow, Pallet Town to Cerulean, from the pret/pokeyellow disassembly.

    python extract.py [--pokeyellow <clone>] [--out <YellowData dir>]

Defaults: the clone at .cache/pokeyellow (cloned if missing) and the output
YellowData/. Standard library only. See README.md for the output contract.
"""
import argparse
import json
import os
import re
import subprocess
import sys
from collections import OrderedDict, deque

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import asmlib  # noqa: E402
import pngio  # noqa: E402
import pret  # noqa: E402
import textlib  # noqa: E402

REPO_URL = "https://github.com/pret/pokeyellow"
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))  # the project folder
DEFAULT_CLONE = os.path.join(ROOT, ".cache", "pokeyellow")
DEFAULT_OUT = os.path.join(ROOT, "YellowData")

# The maps in scope, pokeyellow map constants (constants/map_constants.asm), in map-id order.
# The task's CERULEAN_TRADE_HOUSE is Red/Blue's name: in Yellow that house is CERULEAN_MELANIES_HOUSE.
IN_SCOPE = [
    "PALLET_TOWN", "REDS_HOUSE_1F", "REDS_HOUSE_2F", "BLUES_HOUSE", "OAKS_LAB",
    "ROUTE_1", "VIRIDIAN_CITY", "VIRIDIAN_POKECENTER", "VIRIDIAN_MART", "VIRIDIAN_SCHOOL_HOUSE",
    "VIRIDIAN_NICKNAME_HOUSE", "VIRIDIAN_GYM",
    "ROUTE_22", "ROUTE_22_GATE", "ROUTE_2", "ROUTE_2_GATE", "ROUTE_2_TRADE_HOUSE", "DIGLETTS_CAVE_ROUTE_2",
    "VIRIDIAN_FOREST_SOUTH_GATE", "VIRIDIAN_FOREST", "VIRIDIAN_FOREST_NORTH_GATE",
    "PEWTER_CITY", "MUSEUM_1F", "MUSEUM_2F", "PEWTER_GYM", "PEWTER_NIDORAN_HOUSE", "PEWTER_MART",
    "PEWTER_SPEECH_HOUSE", "PEWTER_POKECENTER",
    "ROUTE_3", "MT_MOON_POKECENTER", "MT_MOON_1F", "MT_MOON_B1F", "MT_MOON_B2F", "ROUTE_4",
    "CERULEAN_CITY", "CERULEAN_TRASHED_HOUSE", "CERULEAN_MELANIES_HOUSE", "CERULEAN_POKECENTER", "CERULEAN_GYM",
    "BIKE_SHOP", "CERULEAN_MART", "CERULEAN_BADGE_HOUSE",
    "ROUTE_24", "ROUTE_25", "BILLS_HOUSE",
]

INTERIOR_ORIGIN_Y = 1000
INTERIOR_GAP = 10
INTERIOR_ROW_WIDTH = 200
PX = 16  # pixels per walking square in every PNG
MINIMAP_RING = 6  # tiles past the edges in the minimap's pictures (the screen reaches 5 past Red)

# Engine tables used for collision and tile classes.
WATER_TILE = 0x14                         # data/tilesets/water_tilesets.asm WaterTile
SHORE_TILES = (0x48, 0x32)                # ShoreTiles
NO_SHORE_TILESETS = ("SHIP_PORT", "GYM", "DOJO")  # engine/items/item_effects.asm IsNextTileShoreOrWater
CUT_TREE_TILE = {"OVERWORLD": 0x3D, "GYM": 0x50}  # engine/overworld/cut.asm


def jdump(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=2, sort_keys=True, ensure_ascii=False)
        f.write("\n")


def load_json(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return json.load(f)


def humanize(label):
    s = re.sub(r"(?<=[a-z])(?=[A-Z0-9])|(?<=[0-9])(?=[A-Z][a-z])", " ", label)
    return s.replace("Pokecenter", "Pokemon Center").replace("Mt Moon", "Mt. Moon")


class Extractor:
    def __init__(self, clone, out):
        self.clone = clone
        self.out = out
        self.p = pret.Pret(clone)
        self.c = self.p.c
        self.ts = self.p.tileset_table()
        self.T = textlib.Texts(clone)
        self.looks_table = load_json("looks.json")
        self.handlers = load_json("text_handlers.json")
        self.text_ids = OrderedDict()  # id -> string, filled as maps and data ask for texts
        self.objects = {}
        for const, m in self.p.maps.items():
            if "label" in m and os.path.exists(self.p.path("data/maps/objects/%s.asm" % m["label"])):
                self.objects[const] = self.p.map_objects(const)
        self.cgb = self.p.cgb_palettes()
        self.gfx_cache = {}

    # ------------------------------------------------------------------ helpers
    def tiles_of(self, tileset):
        t = self.ts[tileset]
        if t["gfx"] not in self.gfx_cache:
            self.gfx_cache[t["gfx"]] = self.p.tile_gfx(t["gfx"])
        return self.gfx_cache[t["gfx"]]

    def square_tiles(self, const):
        """[y][x] -> (tl, tr, bl, br) 8x8 tile ids of each walking square."""
        m = self.p.maps[const]
        blocks = self.p.blockset(self.ts[m["tileset"]]["blockset"])
        data = self.p.blocks(const)
        W, H = m["bw"] * 2, m["bh"] * 2
        out = []
        for y in range(H):
            row = []
            for x in range(W):
                blk = blocks[data[(y // 2) * m["bw"] + x // 2]]
                r, c = 2 * (y % 2), 2 * (x % 2)
                row.append((blk[r * 4 + c], blk[r * 4 + c + 1], blk[(r + 1) * 4 + c], blk[(r + 1) * 4 + c + 1]))
            out.append(row)
        return out

    def add_text(self, tid, s):
        if s is None:
            raise ValueError("text %s did not decode" % tid)
        self.text_ids[tid] = s

    def text_label(self, label):
        """Register a text label; return its id, or None if the label isn't plain text."""
        if label in self.text_ids:
            return label
        k = self.T.kind(label)
        if k != "text":
            return None
        self.add_text(label, self.T.body(label))
        return label

    def far_target(self, label):
        rel, i = self.T.labels[label]
        for line in self.T.files[rel][i + 1:]:
            op, args = asmlib.macro(line)
            if op is None:
                continue
            return self.T.qualify(args[0], rel, i) if op == "text_far" else None
        return None

    def handler_texts(self, label):
        """All text ids a map text pointer shows, in source order (first = its main line)."""
        k = self.T.kind(label)
        if k == "text":
            return [self.text_label(label)], None
        if k and k.startswith("special:"):
            macro = k.split(":", 1)[1]
            if macro not in self.handlers:
                raise ValueError("no text_handlers.json entry for %s (%s)" % (macro, label))
            return [self.text_label(t) for t in self.handlers[macro]["texts"]], macro
        if label in self.handlers:
            return [self.text_label(t) for t in self.handlers[label]["texts"]], label
        if k == "asm":
            refs = self.T.refs(label)
            fars = {self.far_target(r) for r in refs if "TrainerHeader" not in r}
            out = []
            for r in refs:
                if "TrainerHeader" in r:
                    for t in self.trainer_header(r)["texts"]:
                        out.append(t)
                elif r not in fars:
                    out.append(self.text_label(r))
            return [t for t in out if t], None
        raise ValueError("text pointer %s is neither text nor a known script" % label)

    def trainer_header(self, label):
        rel, i = self.T.labels[label]
        for line in self.T.files[rel][i + 1:]:
            op, args = asmlib.macro(line)
            if op == "trainer":
                texts = [self.T.qualify(a, rel, i) for a in args[2:5]]
                return {
                    "event": args[0],
                    "sight": asmlib.num(args[1]),
                    "battle_text": self.text_label(texts[0]),
                    "end_text": self.text_label(texts[1]),
                    "after_text": self.text_label(texts[2]),
                    "texts": [self.text_label(t) for t in texts],
                }
            if op is not None:
                break
        raise ValueError("%s is not a trainer header" % label)

    def text_pointers(self, const):
        """TEXT_* constant -> {'label': primary label, 'alts': [labels in other tables]}."""
        label = self.p.maps[const]["label"]
        rel = "scripts/%s.asm" % label
        lines = self.T.files[rel]
        tables = OrderedDict()
        consts = {}
        cur = None
        counter = 1
        for line in lines:
            lab = asmlib.label_of(line)
            if lab and not lab.startswith("."):
                cur = lab if re.match(r"%s_TextPointers\d*$" % label, lab) else None
                if cur:
                    tables[cur] = []
                continue
            if not cur:
                continue
            op, args = asmlib.macro(line)
            if op == "def_text_pointers":
                counter = 1
            elif op == "const_def":
                counter = asmlib.num(args[0])
            elif op == "dw_const":
                tables[cur].append(args[0])
                consts[args[1]] = counter
                counter += 1
            elif op == "dw":
                tables[cur].append(args[0])
        primary = "%s_TextPointers" % label
        out = {}
        for tconst, value in consts.items():
            main = tables[primary][value - 1]
            alts = [t[value - 1] for n, t in tables.items() if n != primary and len(t) >= value and t[value - 1] != main]
            out[tconst] = {"label": main, "alts": alts}
        return out

    # ------------------------------------------------------------------ maps
    def lastmap_parent(self, const, warp_index):
        """Resolve a LAST_MAP warp: the outdoor map that warps into this map at (or next to) this warp."""
        target = self.objects[const]["warps"][warp_index]
        best = None
        for other, obj in self.objects.items():
            for w in obj["warps"]:
                if w["to_map"] != const:
                    continue
                arrive = self.objects[const]["warps"][w["to_warp"] - 1]
                d = abs(arrive["x"] - target["x"]) + abs(arrive["y"] - target["y"])
                if self.p.maps[other]["index"] < self.p.first_indoor and (best is None or d < best[0]):
                    best = (d, other)
        if best is None:
            # reached only through other interiors (Viridian Forest gates): use their outdoor parent
            for other, obj in self.objects.items():
                if any(w["to_map"] == const for w in obj["warps"]) and other != const:
                    for k, w in enumerate(obj["warps"]):
                        if w["to_map"] == "LAST_MAP":
                            return self.lastmap_parent(other, k)
            raise ValueError("cannot resolve LAST_MAP for %s warp %d" % (const, warp_index + 1))
        return best[1]

    def palette_for(self, const):
        """engine/gfx/palettes.asm SetPal_Overworld."""
        m = self.p.maps[const]
        if m["tileset"] == "CEMETERY":
            return "PAL_GRAYMON"
        if m["tileset"] == "CAVERN":
            return "PAL_CAVE"
        idx = m["index"]
        if idx >= self.p.first_indoor:
            parents = {self.lastmap_parent(const, k) for k, w in enumerate(self.objects[const]["warps"]) if w["to_map"] == "LAST_MAP"}
            if not parents:
                parents = {self.lastmap_parent(const, 0)}
            pals = {self.palette_for(p_) for p_ in parents}
            if len(pals) != 1:
                raise ValueError("%s has parents with different palettes: %s" % (const, parents))
            return pals.pop()
        if idx < self.p.num_city:
            return self._pal_by_value(self.c["PAL_ROUTE"] + idx + 1)  # "a town's palette ID is its map ID + 1"
        return "PAL_ROUTE"

    def _pal_by_value(self, v):
        for n in self.cgb:
            if self.c[n] == v:
                return n
        raise KeyError(v)

    def music(self):
        out = []
        for line in self.p.lines("data/maps/songs.asm"):
            op, args = asmlib.macro(line)
            if op == "db" and args and args[0].startswith("MUSIC_"):
                out.append(args[0][len("MUSIC_"):].lower())
        return out

    def wild_tables(self):
        pointers = []
        for line in self.p.lines("data/wild/grass_water.asm"):
            op, args = asmlib.macro(line)
            if op == "dw":
                pointers.append(args[0])
        tables = {}
        for path in asmlib.files(self.clone, "data/wild/maps"):
            cur, mode = None, None
            for line in asmlib.read_lines(path):
                lab = asmlib.label_of(line)
                if lab:
                    cur = lab
                    tables[cur] = {"grass_rate": 0, "water_rate": 0, "grass": [], "water": [],
                                   "_source": os.path.relpath(path, self.clone).replace(os.sep, "/")}
                    continue
                op, args = asmlib.macro(line)
                if op == "def_grass_wildmons":
                    mode = "grass"
                    tables[cur]["grass_rate"] = asmlib.num(args[0])
                elif op == "def_water_wildmons":
                    mode = "water"
                    tables[cur]["water_rate"] = asmlib.num(args[0])
                elif op == "db" and mode and cur:
                    tables[cur][mode].append([asmlib.num(args[0]), args[1]])
        return pointers, tables

    def build_maps(self):
        songs = self.music()
        wild_ptrs, wild = self.wild_tables()
        self.wild_ptrs, self.wild = wild_ptrs, wild
        toggles = {}
        cur = None
        for line in self.p.lines("data/maps/toggleable_objects.asm"):
            op, args = asmlib.macro(line)
            if op == "toggleable_objects_for":
                cur = args[0]
            elif op == "toggle_object_state":
                toggles.setdefault(cur, {})[args[0]] = args[1]
        hidden = self.hidden_events()
        doors = self.door_tiles()
        ledges = []
        for line in self.p.lines("data/tilesets/ledge_tiles.asm"):
            op, args = asmlib.macro(line)
            if op == "db" and len(args) == 4:
                ledges.append((args[0], asmlib.num(args[1]), asmlib.num(args[2])))
        water_tilesets = []
        for line in self.p.lines("data/tilesets/water_tilesets.asm"):
            op, args = asmlib.macro(line)
            if op == "db" and args[0] != "-1" and not args[0].startswith("$"):
                water_tilesets.append(args[0])
        pairs = []
        for line in self.p.lines("data/tilesets/pair_collision_tile_ids.asm"):
            if line.startswith("TilePairCollisionsWater"):
                break
            op, args = asmlib.macro(line)
            if op == "db" and len(args) == 3:
                pairs.append((args[0], asmlib.num(args[1]), asmlib.num(args[2])))
        names = self.map_names()
        self.maps = OrderedDict()
        self.map_texts = {}
        for const in IN_SCOPE:
            m = self.p.maps[const]
            tileset = m["tileset"]
            ts = self.ts[tileset]
            W, H = m["bw"] * 2, m["bh"] * 2
            sq = self.square_tiles(const)
            obj = self.objects[const]
            walk = set(ts["walkable"])
            warp_at = {(w["x"], w["y"]) for w in obj["warps"]}
            tiles, looks, unknown = [], [], 0
            gfx_name = os.path.splitext(os.path.basename(ts["gfx"]))[0]
            table = self.looks_table["gfx"][gfx_name]
            for y in range(H):
                trow, lrow = [], []
                for x in range(W):
                    bl = sq[y][x][2]
                    ch = None
                    if (x, y) in warp_at:
                        ch = "d"
                    if ch is None and tileset == "OVERWORLD":
                        for facing, stand, ledge in ledges:
                            if bl != ledge:
                                continue
                            nx, ny, sym = {"SPRITE_FACING_DOWN": (x, y - 1, "v"), "SPRITE_FACING_LEFT": (x + 1, y, "<"),
                                           "SPRITE_FACING_RIGHT": (x - 1, y, ">")}[facing]
                            if 0 <= nx < W and 0 <= ny < H and sq[ny][nx][2] == stand:
                                ch = sym
                                break
                    if ch is None and bl in ts["counters"]:
                        ch = "="
                    if ch is None and CUT_TREE_TILE.get(tileset) == bl:
                        ch = "x"
                    if ch is None and bl not in walk and tileset in water_tilesets and (
                            bl == WATER_TILE or (bl in SHORE_TILES and tileset not in NO_SHORE_TILESETS)):
                        ch = "w"
                    if ch is None:
                        if bl in walk:
                            ch = "g" if bl == ts["grass"] else ("c" if tileset == "CAVERN" else ".")
                        else:
                            ch = "#"
                    trow.append(ch)
                    look = self.pick_look(table, sq[y][x], ch in ".gcd")
                    if look == "?" and ch == "d":
                        look = "D"  # an exit drawn as wall (gate doorways): the warp is the door
                    if ch in "v<>":
                        look = "L"
                    if look == "?":
                        unknown += 1
                    lrow.append(look)
                tiles.append("".join(trow))
                looks.append("".join(lrow))
            # pair collisions (elevation steps in caves and the forest)
            blocked = []
            my_pairs = {(a, b) for t_, a, b in pairs if t_ == tileset}
            if my_pairs:
                for y in range(H):
                    for x in range(W):
                        for dx, dy, d in ((1, 0, "east"), (-1, 0, "west"), (0, 1, "south"), (0, -1, "north")):
                            nx, ny = x + dx, y + dy
                            if 0 <= nx < W and 0 <= ny < H:
                                a, b = sq[y][x][2], sq[ny][nx][2]
                                if (a, b) in my_pairs or (b, a) in my_pairs:
                                    blocked.append({"x": x, "y": y, "dir": d})
            wild_label = wild_ptrs[m["index"]]
            wt = wild.get(wild_label, {"grass_rate": 0})
            md = OrderedDict()
            md["id"] = const
            md["name"] = names.get(const, humanize(m["label"]))
            md["label"] = m["label"]
            md["width"], md["height"] = W, H
            md["outdoor"] = m["index"] < self.p.first_indoor
            md["cave"] = tileset == "CAVERN"
            md["tileset"] = tileset
            md["music"] = songs[m["index"]]
            md["palette"] = self.palette_for(const)
            md["encounter_rate"] = wt["grass_rate"]
            md["border_block"] = obj["border"]
            md["tiles"] = tiles
            md["looks"] = looks
            md["blocked_edges"] = blocked
            md["connections"] = OrderedDict()
            for d, cn in m["connections"].items():
                e = {"map": cn["map"], "offset": cn["offset_blocks"] * 2}
                if cn["map"] not in IN_SCOPE:
                    e["out_of_scope"] = True
                md["connections"][d] = e
            md["warps"] = []
            for k, w in enumerate(obj["warps"]):
                e = {"x": w["x"], "y": w["y"], "to_warp": w["to_warp"]}
                if w["to_map"] == "LAST_MAP":
                    e["to_map"] = self.lastmap_parent(const, k)
                    e["last_map"] = True
                else:
                    e["to_map"] = w["to_map"]
                if e["to_map"] not in IN_SCOPE:
                    e["out_of_scope"] = True
                if sq[w["y"]][w["x"]][2] in doors.get(tileset, ()):
                    e["door"] = True  # data/tilesets/door_tile_ids.asm: arriving here, the player steps one tile down
                md["warps"].append(e)
            tp = self.text_pointers(const)
            self.map_texts[const] = tp
            md["signs"] = []
            for b in obj["bgs"]:
                texts, _ = self.handler_texts(tp[b["text"]]["label"])
                if not texts:
                    raise ValueError("%s sign %s has no text" % (const, b["text"]))
                self.add_text(b["text"], self.text_ids[texts[0]])
                md["signs"].append({"x": b["x"], "y": b["y"], "text": b["text"], "texts": texts})
            md["npcs"], md["items"] = [], []
            tog = toggles.get(const, {})
            for n, o in enumerate(obj["objects"]):
                oid = obj["object_consts"][n]
                entry = self.object_entry(const, oid, o, tp)
                if oid in tog:
                    entry["toggle"] = oid
                    entry["hidden_at_start"] = tog[oid] == "OFF"
                if "item" in o:
                    md["items"].append(entry)
                else:
                    md["npcs"].append(entry)
            md["hidden"] = [h for h in hidden.get(const, []) if "item" in h]
            md["hidden_events"] = [h for h in hidden.get(const, []) if "item" not in h]
            md["looks_unknown"] = unknown
            md["source"] = {"header": m["header_file"], "objects": obj["file"], "blocks": "maps/%s.blk" % m["label"],
                            "script": "scripts/%s.asm" % m["label"]}
            self.maps[const] = md

    def door_tiles(self):
        """data/tilesets/door_tile_ids.asm: {tileset: {tile ids}} (IsPlayerStandingOnDoorTile)."""
        labels, ptrs, cur = {}, [], None
        for line in self.p.lines("data/tilesets/door_tile_ids.asm"):
            lab = asmlib.label_of(line)
            if lab:
                cur = lab
                labels.setdefault(cur, [])
                continue
            op, args = asmlib.macro(line)
            if op == "dbw" and len(args) == 2:
                ptrs.append((args[0], args[1]))
            elif op == "door_tiles" and cur:
                labels[cur] += [asmlib.num(a) for a in args]
        return {ts: set(labels.get(lab, [])) for ts, lab in ptrs}

    def looks_for_squares(self, tileset, squares):
        """Looks ([y][x]) for walking squares of any tileset; tilesets without a looks table (Route 23's PLATEAU) get
        water, open ground or rock ('x') by collision alone."""
        ts = self.ts[tileset]
        walk = set(ts["walkable"])
        gfx_name = os.path.splitext(os.path.basename(ts["gfx"]))[0]
        table = self.looks_table["gfx"].get(gfx_name)
        out = []
        for row in squares:
            lrow = []
            for quad in row:
                if table is None:
                    lrow.append("W" if quad[2] == WATER_TILE else ("p" if quad[2] in walk else "x"))
                elif CUT_TREE_TILE.get(tileset) == quad[2]:
                    lrow.append("Z")  # a Cut tree (Route 9's, east of Cerulean): the small tree, not a big one
                else:
                    look = self.pick_look(table, quad, quad[2] in walk)
                    lrow.append("T" if look == "?" and quad[2] not in walk else look)
            out.append("".join(lrow))
        return out

    def border_looks(self, const):
        """The map's border block (the Game Boy draws it past every edge without a connection) as 2x2 looks."""
        m = self.p.maps[const]
        blocks = self.p.blockset(self.ts[m["tileset"]]["blockset"])
        blk = blocks[self.objects[const]["border"]]
        squares = [[(blk[r * 4 + c], blk[r * 4 + c + 1], blk[(r + 1) * 4 + c], blk[(r + 1) * 4 + c + 1]) for c in (0, 2)] for r in (0, 2)]
        return self.looks_for_squares(m["tileset"], squares)

    def build_scenery(self, depth=96):
        """scenery.json: what Kanto shows past the maps built. Each out-of-range connection's map (Route 21's sea south
        of Pallet Town, Route 23, Route 9, Route 5...) placed at pokeyellow's offset and cut to `depth` tiles past the
        seam, and every outdoor map's border block. Scenery only: the game draws it and never lets Red walk there."""
        out = {"depth": depth, "maps": [], "borders": OrderedDict()}
        self.scenery_cuts = []  # (entry, its squares) for the minimap's pictures
        for const in IN_SCOPE:
            md = self.maps[const]
            if md["tileset"] == "FOREST":
                out["borders"][const] = self.border_looks(const)  # Viridian Forest: drawn as the outdoors, ringed by its border trees
            if not md["outdoor"]:
                continue
            out["borders"][const] = self.border_looks(const)
            ox, oy = md["origin"]["x"], md["origin"]["y"]
            for d, cn in md["connections"].items():
                if not cn.get("out_of_scope"):
                    continue
                other = cn["map"]
                pm = self.p.maps[other]
                ow, oh = pm["bw"] * 2, pm["bh"] * 2
                off = cn["offset"]
                at = {"north": (ox + off, oy - oh), "south": (ox + off, oy + md["height"]),
                      "west": (ox - ow, oy + off), "east": (ox + md["width"], oy + off)}[d]
                # The part within `depth` tiles of the seam.
                x0, y0, x1, y1 = 0, 0, ow, oh
                if d == "north":
                    y0 = max(0, oh - depth)
                elif d == "south":
                    y1 = min(oh, depth)
                elif d == "west":
                    x0 = max(0, ow - depth)
                else:
                    x1 = min(ow, depth)
                sq = self.square_tiles(other)
                cut = [row[x0:x1] for row in sq[y0:y1]]
                e = OrderedDict()
                e["id"] = other
                e["from"] = const
                e["dir"] = d
                e["tileset"] = pm["tileset"]
                e["origin"] = {"x": at[0] + x0, "y": at[1] + y0}
                e["width"], e["height"] = x1 - x0, y1 - y0
                e["looks"] = self.looks_for_squares(pm["tileset"], cut)
                e["border"] = self.border_looks(other) if other in self.objects else None
                out["maps"].append(e)
                self.scenery_cuts.append((e, cut))
        self.scenery = out

    def build_kanto(self):
        """kanto.json (m4s): the rest of Kanto, drawn far past the maps built. Every outdoor map out of the range, placed
        by the same breadth-first search over pokeyellow's connections from Pallet Town (origin 0,0), as looks; the
        Town Map as a land/sea mask (one cell per Town Map square, with the square's Kanto tile rectangle); and the
        landmarks the far view is built round (Mt. Moon, the League, Rock Tunnel, Bill's Sea Cottage...), each at the
        Kanto tile of the warp that enters it. Scenery only: the game draws it and never lets Red walk there."""
        outdoor = [c for c, m in self.p.maps.items() if m["index"] < self.p.first_indoor and "label" in m
                   and os.path.exists(self.p.path("maps/%s.blk" % m["label"]))]
        origin = {"PALLET_TOWN": (0, 0)}
        q = deque(["PALLET_TOWN"])
        problems = []
        while q:
            a = q.popleft()
            ma = self.p.maps[a]
            ax, ay = origin[a]
            aw, ah = ma["bw"] * 2, ma["bh"] * 2
            for d, cn in ma["connections"].items():
                b = cn["map"]
                if b not in outdoor:
                    continue
                mb = self.p.maps[b]
                off = cn["offset_blocks"] * 2
                pos = {"north": (ax + off, ay - mb["bh"] * 2), "south": (ax + off, ay + ah),
                       "west": (ax - mb["bw"] * 2, ay + off), "east": (ax + aw, ay + off)}[d]
                if b in origin:
                    if origin[b] != pos:
                        problems.append("%s->%s %s: %s, kept %s" % (a, b, d, pos, origin[b]))
                else:
                    origin[b] = pos
                    q.append(b)
        for c in IN_SCOPE:
            if self.maps[c]["outdoor"] and (self.maps[c]["origin"]["x"], self.maps[c]["origin"]["y"]) != origin.get(c):
                raise ValueError("kanto.json places %s at %s, the maps at %s" % (c, origin.get(c), self.maps[c]["origin"]))
        out = OrderedDict()
        out["_comment"] = ("Kanto past the maps built (extract.py build_kanto): each out-of-range outdoor map at its place, "
                           "the Town Map's land and sea, and the landmarks; drawn as far scenery, never walked.")
        out["placement_notes"] = problems
        maps = []
        for c in outdoor:
            if c in IN_SCOPE or c not in origin:
                continue
            m = self.p.maps[c]
            e = OrderedDict()
            e["id"] = c
            e["tileset"] = m["tileset"]
            e["origin"] = {"x": origin[c][0], "y": origin[c][1]}
            e["width"], e["height"] = m["bw"] * 2, m["bh"] * 2
            e["looks"] = self.looks_for_squares(m["tileset"], self.square_tiles(c))
            e["border"] = self.border_looks(c) if c in self.objects else None
            maps.append(e)
        out["maps"] = maps
        # The Town Map's squares (engine/items/town_map.asm): which are sea, read from the picture town_map() wrote
        # (more than half its pixels the sea's blue), and the Kanto tiles each square covers, fitted to the outdoor maps'
        # own Town Map squares (data/maps/town_map_entries.asm) and their places in Kanto.
        w, h, px = pngio.read_rgba(os.path.join(self.out, "townmap", "townmap.png"))
        # The bottom-right square is open sea on Yellow's map: its most common colour is the sea's.
        from collections import Counter
        sea_rgb = Counter(px[(h - 1 - j) * w + (w - 1 - i)][:3] for j in range(8) for i in range(8)).most_common(1)[0][0]
        rows = []
        for cy in range(h // 8):
            row = ""
            for cx in range(w // 8):
                n = sum(1 for j in range(8) for i in range(8) if self._near(px[(cy * 8 + j) * w + cx * 8 + i][:3], sea_rgb))
                row += "~" if n > 32 else "L"
            rows.append(row)
        ents = []
        for line in self.p.lines("data/maps/town_map_entries.asm"):
            op, args = asmlib.macro(line)
            if op == "outdoor_map":
                ents.append((asmlib.num(args[0]), asmlib.num(args[1])))
        cells = {}
        for c, (x, y) in origin.items():
            m = self.p.maps[c]
            tx, ty = ents[m["index"]]
            # TownMapCoordsToOAMCoords puts the marker sprite at OAM (x*8+24, y*8+24), on screen (x*8+16, y*8+8): the
            # picture square a map's marker sits on is its entry + (2, 1).
            cells.setdefault((tx + 2, ty + 1), []).append((x + m["bw"], y + m["bh"]))  # each map's centre in Kanto tiles
        # Least squares: tile = a + b * square, per axis.
        def fit(k):
            pts = [(sqr[k], sum(p[k] for p in cs) / len(cs)) for sqr, cs in cells.items()]
            n = len(pts)
            sx = sum(p[0] for p in pts)
            sy = sum(p[1] for p in pts)
            sxx = sum(p[0] * p[0] for p in pts)
            sxy = sum(p[0] * p[1] for p in pts)
            b = (n * sxy - sx * sy) / float(n * sxx - sx * sx)
            return round((sy - b * sx) / n, 2), round(b, 3)
        ax, bx = fit(0)
        ay, by = fit(1)
        out["townmap"] = OrderedDict([("rows", rows), ("square_tiles", {"x": bx, "y": by}),
                                      ("square0_centre", {"x": ax, "y": ay}),
                                      ("_comment", "the 20x18 squares of the Town Map picture; '~' sea, 'L' land. Kanto tile of a "
                                                   "square's centre = square0_centre + square * square_tiles.")])
        # Landmarks: the Kanto tile of the warp on an outdoor map into the place (or the map itself).
        marks = OrderedDict()
        want = {"MT_MOON": ("MT_MOON_1F",), "ROCK_TUNNEL": ("ROCK_TUNNEL_1F",), "VICTORY_ROAD": ("VICTORY_ROAD_1F",),
                "SEA_COTTAGE": ("BILLS_HOUSE",), "POKEMON_LEAGUE": ("INDIGO_PLATEAU_LOBBY",), "POWER_PLANT": ("POWER_PLANT",)}
        for name, targets in want.items():
            for c in outdoor:
                if c not in origin or c not in self.objects:
                    continue
                for wp in self.objects[c]["warps"]:
                    if wp["to_map"] in targets and name not in marks:
                        marks[name] = {"map": c, "x": origin[c][0] + wp["x"], "y": origin[c][1] + wp["y"]}
        out["landmarks"] = marks
        self.kanto = out

    @staticmethod
    def _near(a, b, tol=24):
        return all(abs(int(x) - int(y)) <= tol for x, y in zip(a, b))

    def pick_look(self, table, quad, walkable):
        pri = self.looks_table["priority"]
        classes = [table.get("%02X" % t) for t in quad]
        classes = [c for c in classes if c]
        if walkable:
            cand = [c for c in classes if c in self.looks_table["walkable_ok"]]
            if not cand and any(c in "DSPum" for c in classes):
                cand = classes
        else:
            cand = [c for c in classes if c not in self.looks_table["ground"]] or classes
        if not cand:
            return "?"
        return min(cand, key=pri.index)

    def object_entry(self, const, oid, o, tp):
        e = OrderedDict()
        e["id"] = oid
        e["x"], e["y"] = o["x"], o["y"]
        e["sprite"] = o["sprite"]
        e["movement"] = "walk" if o["movement"] == "WALK" else "stay"
        e["facing"] = o["range"].lower()
        e["text_id"] = o["text"]
        ptr = tp[o["text"]]
        texts, special = self.handler_texts(ptr["label"])
        for alt in ptr["alts"]:
            for t in self.handler_texts(alt)[0]:
                if t not in texts:
                    texts.append(t)
        if special:
            e["script"] = special
        if "trainer_class" in o:
            cls = o["trainer_class"][len("OPP_"):]
            tr = OrderedDict([("class", cls), ("party", o["trainer_no"])])
            hdr = [r for r in self.T.refs(ptr["label"]) if "TrainerHeader" in r] if self.T.kind(ptr["label"]) == "asm" else []
            if hdr:
                h = self.trainer_header(hdr[0])
                tr.update({"sight": h["sight"], "event": h["event"], "battle_text": h["battle_text"],
                           "end_text": h["end_text"], "after_text": h["after_text"]})
            else:
                tr["sight"] = 0
            e["trainer"] = tr
        if "item" in o:
            e["item"] = o["item"]
        self.add_text(o["text"], self.text_ids[texts[0]] if texts else "")
        e["text"] = o["text"]
        e["texts"] = texts
        e["text_handler"] = ptr["label"]
        return e

    def hidden_events(self):
        out = {}
        cur = None
        for line in self.p.lines("data/events/hidden_events.asm"):
            op, args = asmlib.macro(line)
            if op == "hidden_events_for":
                cur = args[0]
            elif op in ("hidden_event", "hidden_text_predef") and cur in IN_SCOPE:
                h = {"x": asmlib.num(args[0]), "y": asmlib.num(args[1])}
                if args[2] == "HiddenItems":
                    h["item"] = args[3]
                else:
                    h["kind"] = args[2]
                    h["arg"] = args[3]
                out.setdefault(cur, []).append(h)
        return out

    def map_names(self):
        """Town-map names for towns/routes (data/maps/names.asm via town_map_entries.asm)."""
        label_text = {}
        for line in self.p.lines("data/maps/names.asm"):
            m = re.match(r'(\w+):\s*db\s+"(.*?)@"', line)
            if m:
                label_text[m.group(1)] = m.group(2)
        out = {}
        rows = [r for r in self.p.map_rows]
        k = 0
        for line in self.p.lines("data/maps/town_map_entries.asm"):
            op, args = asmlib.macro(line)
            if op == "outdoor_map":
                const = [n for n, v, a in rows if v == k][0]
                out[const] = label_text[args[2]]
                k += 1
        return out

    # ------------------------------------------------------------------ world frame
    def place_world(self):
        origin = {"PALLET_TOWN": (0, 0)}
        q = deque(["PALLET_TOWN"])
        problems = []
        while q:
            a = q.popleft()
            ma = self.maps[a]
            ax, ay = origin[a]
            for d, cn in ma["connections"].items():
                b = cn["map"]
                if cn.get("out_of_scope"):
                    continue
                mb = self.maps[b]
                off = cn["offset"]
                if d == "north":
                    pos = (ax + off, ay - mb["height"])
                elif d == "south":
                    pos = (ax + off, ay + ma["height"])
                elif d == "west":
                    pos = (ax - mb["width"], ay + off)
                else:
                    pos = (ax + ma["width"], ay + off)
                if b in origin:
                    if origin[b] != pos:
                        problems.append("%s->%s %s puts %s at %s, already at %s" % (a, b, d, b, pos, origin[b]))
                else:
                    origin[b] = pos
                    q.append(b)
        if problems:
            raise ValueError("\n".join(problems))
        outdoor = [c for c in IN_SCOPE if self.maps[c]["outdoor"]]
        missing = [c for c in outdoor if c not in origin]
        if missing:
            raise ValueError("outdoor maps not reached by connections: %s" % missing)
        x = y = 0
        row_h = 0
        y = INTERIOR_ORIGIN_Y
        for c in IN_SCOPE:
            if self.maps[c]["outdoor"]:
                continue
            m = self.maps[c]
            if x > 0 and x + m["width"] > INTERIOR_ROW_WIDTH:
                x = 0
                y += row_h + INTERIOR_GAP
                row_h = 0
            origin[c] = (x, y)
            x += m["width"] + INTERIOR_GAP
            row_h = max(row_h, m["height"])
        for c in IN_SCOPE:
            self.maps[c]["origin"] = {"x": origin[c][0], "y": origin[c][1]}

    # ------------------------------------------------------------------ pictures
    def render_map(self, const):
        m = self.maps[const]
        tiles = self.tiles_of(m["tileset"])
        pal = self.cgb[m["palette"]]["rgb"]
        sq = self.square_tiles(const)
        W, H = m["width"] * PX, m["height"] * PX
        px = [None] * (W * H)
        for y in range(m["height"]):
            for x in range(m["width"]):
                for k, tid in enumerate(sq[y][x]):
                    t = tiles[tid] if tid < len(tiles) else [[0] * 8] * 8
                    ox, oy = x * PX + (k % 2) * 8, y * PX + (k // 2) * 8
                    for r in range(8):
                        base = (oy + r) * W + ox
                        for cidx, v in enumerate(t[r]):
                            px[base + cidx] = pal[v] + (255,)
        return W, H, px

    def border_squares(self, const):
        """The map's border block as 2x2 walking squares of 8x8 tile ids, [row][col]."""
        m = self.p.maps[const]
        blocks = self.p.blockset(self.ts[m["tileset"]]["blockset"])
        blk = blocks[self.objects[const]["border"]]
        return [[(blk[r * 4 + c], blk[r * 4 + c + 1], blk[(r + 1) * 4 + c], blk[(r + 1) * 4 + c + 1]) for c in (0, 2)] for r in (0, 2)]

    def render_cells(self, cells):
        """{(x, y): (tileset, square, palette)} -> (x0, y0, W, H, px): the squares drawn 16 px each, transparent where
        no cell is."""
        x0 = min(k[0] for k in cells)
        y0 = min(k[1] for k in cells)
        W = (max(k[0] for k in cells) + 1 - x0) * PX
        H = (max(k[1] for k in cells) + 1 - y0) * PX
        px = [(0, 0, 0, 0)] * (W * H)
        for (cx, cy), (tileset, quad, palname) in cells.items():
            tiles = self.tiles_of(tileset)
            pal = self.cgb[palname]["rgb"]
            for k, tid in enumerate(quad):
                t = tiles[tid] if tid < len(tiles) else [[0] * 8] * 8
                ox, oy = (cx - x0) * PX + (k % 2) * 8, (cy - y0) * PX + (k // 2) * 8
                for r in range(8):
                    base = (oy + r) * W + ox
                    for cidx, v in enumerate(t[r]):
                        px[base + cidx] = pal[v] + (255,)
        return x0, y0, W, H, px

    def pictures(self, ring=None):
        """minimap/<MAP>.png: each map alone, its own size (the QA harness's reference pictures). The minimap's own:
        - minimap/kanto_outdoor.png: every outdoor map at its origin, and past the edges what the Game Boy draws there
          (m4r): each out-of-range neighbour's own tiles (scenery.json: Route 9 east of Cerulean, Route 21's sea...)
          and every map's border block, `ring` tiles deep, so the screen never ends in a blank at an edge;
        - minimap/ringed/<MAP>.png for the rooms (and Viridian Forest): the room ringed by its border block, `ring`
          tiles deep (minimap/ringed.json gives the ring)."""
        ring = MINIMAP_RING if ring is None else ring
        mm = os.path.join(self.out, "minimap")
        os.makedirs(os.path.join(mm, "ringed"), exist_ok=True)
        for const in IN_SCOPE:
            W, H, px = self.render_map(const)
            pngio.write_rgba(os.path.join(mm, const + ".png"), W, H, px)
        outdoor = [c for c in IN_SCOPE if self.maps[c]["outdoor"]]
        cells = {}
        for c in outdoor:
            m = self.maps[c]
            sq = self.square_tiles(c)
            for y in range(m["height"]):
                for x in range(m["width"]):
                    cells[(m["origin"]["x"] + x, m["origin"]["y"] + y)] = (m["tileset"], sq[y][x], m["palette"])
        for e, cut in self.scenery_cuts:
            pal = self.maps[e["from"]]["palette"]
            for y, row in enumerate(cut):
                for x, quad in enumerate(row):
                    cells.setdefault((e["origin"]["x"] + x, e["origin"]["y"] + y), (e["tileset"], quad, pal))
        for c in outdoor:
            m = self.maps[c]
            bs = self.border_squares(c)
            for y in range(-ring, m["height"] + ring):
                for x in range(-ring, m["width"] + ring):
                    k = (m["origin"]["x"] + x, m["origin"]["y"] + y)
                    if k not in cells:
                        cells[k] = (m["tileset"], bs[y % 2][x % 2], m["palette"])
        x0, y0, KW, KH, kp = self.render_cells(cells)
        pngio.write_rgba(os.path.join(mm, "kanto_outdoor.png"), KW, KH, kp)
        jdump(os.path.join(mm, "kanto_outdoor.json"), {
            "px_per_tile": PX, "origin_px": {"x": -x0 * PX, "y": -y0 * PX},
            "size_px": {"w": KW, "h": KH}, "maps": outdoor, "ring": ring,
            "scenery": [e["id"] for e, _ in self.scenery_cuts]})
        for c in IN_SCOPE:
            m = self.maps[c]
            if m["outdoor"]:
                continue
            sq = self.square_tiles(c)
            bs = self.border_squares(c)
            cells = {}
            for y in range(-ring, m["height"] + ring):
                for x in range(-ring, m["width"] + ring):
                    inside = 0 <= x < m["width"] and 0 <= y < m["height"]
                    cells[(x, y)] = (m["tileset"], sq[y][x] if inside else bs[y % 2][x % 2], m["palette"])
            _, _, W, H, px = self.render_cells(cells)
            pngio.write_rgba(os.path.join(mm, "ringed", c + ".png"), W, H, px)
        jdump(os.path.join(mm, "ringed.json"), {"px_per_tile": PX, "ring": ring,
                                                "maps": [c for c in IN_SCOPE if not self.maps[c]["outdoor"]]})
        self.red_sprite()
        self.people_sprites()
        self.town_map()
        self.menu_pictures()

    def red_sprite(self):
        """gfx/sprites/red.png: frames down, up, left (standing) then down, up, left (walking); right = left mirrored.
        Colours: the overworld OBP0 (%11010000, home/palettes.asm GBPalNormal) over PAL_ROUTE, index 0 transparent."""
        w, h, rows = pngio.read_gray(self.p.path("gfx/sprites/red.png"))
        pal = self.cgb["PAL_ROUTE"]["rgb"]
        obp0 = [0, 0, 1, 3]

        def frame(i, mirror=False):
            f = [rows[i * 16 + r][:16] for r in range(16)]
            return [list(reversed(r)) for r in f] if mirror else f

        order = [(0, False), (3, False), (1, False), (4, False), (2, False), (5, False), (2, True), (5, True)]
        W, H = 32, 64
        px = [(0, 0, 0, 0)] * (W * H)
        for k, (fi, mir) in enumerate(order):
            f = frame(fi, mir)
            ox, oy = (k % 2) * 16, (k // 2) * 16
            for r in range(16):
                for c in range(16):
                    v = f[r][c]
                    if v:
                        px[(oy + r) * W + ox + c] = pal[obp0[v]] + (255,)
        os.makedirs(os.path.join(self.out, "sprites"), exist_ok=True)
        pngio.write_rgba(os.path.join(self.out, "sprites", "red_minimap.png"), W, H, px)

    def people_sprites(self):
        """The minimap's people: every overworld sprite (data/sprites/sprites.asm SpriteSheetPointerTable, the pictures
        named in gfx/sprites.asm) as one atlas per map palette, sprites/people_<PAL>.png, and sprites/people.json naming
        each SPRITE_* constant's row. A row is 8 frames of 16x16 in red_minimap's order: down, down step, up, up step,
        left, left step, right, right step (right = left mirrored).
        - 6-frame sheets (16x96): standing down/up/left, then walking down/up/left.
        - 3-frame sheets (16x48, and the nurse's 16x64 with her bow last): standing only, so the step repeats it.
        - 1-frame sheets (16x16: balls, fossils, the Pokedex): the same picture every way.
        Colours: on the Game Boy Color every object palette is set from the map's palette (engine/gfx/palettes.asm
        InitCGBPalettes, OBP0 over the same base palette as the background), so each map palette gets its own atlas;
        the overworld OBP0 (%11010000), index 0 transparent."""
        names = []
        for line in self.p.lines("constants/sprite_constants.asm"):
            op, args = asmlib.macro(line)
            if op == "const" and args and args[0].startswith("SPRITE_"):
                names.append(args[0])
        files = {}
        for line in self.p.lines("gfx/sprites.asm"):
            m = re.match(r'\s*(\w+)::\s*INCBIN\s+"gfx/sprites/(\w+)\.2bpp"', line)
            if m:
                files[m.group(1)] = m.group(2)
        table = []
        for line in self.p.lines("data/sprites/sprites.asm"):
            op, args = asmlib.macro(line)
            if op == "overworld_sprite":
                table.append(args[0])
        # SPRITE_NONE is $00; the table starts at SPRITE_RED.
        rows = OrderedDict()
        for k, label in enumerate(table):
            const = names[k + 1] if k + 1 < len(names) else None
            if not const or "UNUSED" in const or label not in files:
                continue
            rows[const] = files[label]
        obp0 = [0, 0, 1, 3]
        sheets = {}
        for const, f in rows.items():
            w, h, g = pngio.read_gray(self.p.path("gfx/sprites/%s.png" % f))
            n = h // 16

            def frame(i, mirror=False, g=g):
                fr = [g[i * 16 + r][:16] for r in range(16)]
                return [list(reversed(r)) for r in fr] if mirror else fr
            if n >= 6:
                order = [(0, False), (3, False), (1, False), (4, False), (2, False), (5, False), (2, True), (5, True)]
            elif n >= 3:
                order = [(0, False), (0, False), (1, False), (1, False), (2, False), (2, False), (2, True), (2, True)]
            else:
                order = [(0, False)] * 8
            sheets[const] = [frame(i, mir) for i, mir in order]
        palettes = sorted({m["palette"] for m in self.maps.values()})
        W, H = 8 * 16, len(sheets) * 16
        os.makedirs(os.path.join(self.out, "sprites"), exist_ok=True)
        for pname in palettes:
            pal = self.cgb[pname]["rgb"]
            px = [(0, 0, 0, 0)] * (W * H)
            for row, (const, frames) in enumerate(sheets.items()):
                for k, fr in enumerate(frames):
                    for r in range(16):
                        for c in range(16):
                            v = fr[r][c]
                            if v:
                                px[(row * 16 + r) * W + k * 16 + c] = pal[obp0[v]] + (255,)
            pngio.write_rgba(os.path.join(self.out, "sprites", "people_%s.png" % pname), W, H, px)
        jdump(os.path.join(self.out, "sprites", "people.json"), {
            "frame_px": 16, "frames": ["down", "down_step", "up", "up_step", "left", "left_step", "right", "right_step"],
            "palettes": palettes, "rows": OrderedDict((c, i) for i, c in enumerate(sheets)),
            "source": OrderedDict((c, "gfx/sprites/%s.png" % rows[c]) for c in sheets)})

    def menu_pictures(self):
        """The Game Boy's small pictures the menus show:
        - icons/<ICON>.png (16x16): the party screen's icon for each kind (data/icon_pointers.asm: a frame of a sprite
          or of gfx/icons), and species.json's menu_icon for each species (data/pokemon/menu_icons.asm);
        - trainercard/red.png: Red's picture (gfx/player/red.png) and trainercard/badge_<n>.png / face_<n>.png, the
          eight badges and gym leaders' faces of the badge case (gfx/trainer_card/badges.png, face then badge).
        Shades 0-3 as the Game Boy's (white is transparent on icons)."""
        shades = [(255, 255, 255), (170, 170, 170), (85, 85, 85), (16, 16, 24)]
        def frame16(path, frame, transparent=True):
            w, h, rows = pngio.read_gray(self.p.path(path))
            y0 = frame * 16 if frame * 16 + 16 <= h else 0
            px = []
            for r in range(16):
                for c in range(16):
                    # gfx/icons are half icons 8 wide: the Game Boy mirrors the left half for the right.
                    cc = c if w >= 16 else (c if c < 8 else 15 - c)
                    v = rows[y0 + r][cc] if y0 + r < h and cc < w else 0
                    px.append((0, 0, 0, 0) if (v == 0 and transparent) else shades[v] + (255,))
            return px
        icons = {
            "ICON_MON": ("gfx/sprites/monster.png", 3), "ICON_BALL": ("gfx/sprites/poke_ball.png", 0),
            "ICON_FAIRY": ("gfx/sprites/fairy.png", 3), "ICON_BIRD": ("gfx/sprites/bird.png", 3),
            "ICON_WATER": ("gfx/sprites/seel.png", 0), "ICON_BUG": ("gfx/icons/bug.png", 0),
            "ICON_GRASS": ("gfx/icons/plant.png", 0), "ICON_SNAKE": ("gfx/icons/snake.png", 0),
            "ICON_QUADRUPED": ("gfx/icons/quadruped.png", 0), "ICON_PIKACHU": ("gfx/sprites/pikachu.png", 0),
        }
        os.makedirs(os.path.join(self.out, "icons"), exist_ok=True)
        for name, (path, frame) in icons.items():
            pngio.write_rgba(os.path.join(self.out, "icons", name + ".png"), 16, 16, frame16(path, frame))
        # Red's picture and the badge case.
        os.makedirs(os.path.join(self.out, "trainercard"), exist_ok=True)
        w, h, rows = pngio.read_gray(self.p.path("gfx/player/red.png"))
        pngio.write_rgba(os.path.join(self.out, "trainercard", "red.png"), w, h,
                         [shades[rows[r][c]] + (255,) for r in range(h) for c in range(w)])
        for k in range(8):
            pngio.write_rgba(os.path.join(self.out, "trainercard", "face_%d.png" % k), 16, 16, frame16("gfx/trainer_card/badges.png", 2 * k, False))
            pngio.write_rgba(os.path.join(self.out, "trainercard", "badge_%d.png" % k), 16, 16, frame16("gfx/trainer_card/badges.png", 2 * k + 1, False))

    def menu_icons(self):
        """data/pokemon/menu_icons.asm: the party icon of each species, in Pokedex order."""
        out = []
        for line in self.p.lines("data/pokemon/menu_icons.asm"):
            op, args = asmlib.macro(line)
            if op == "nybble" and args:
                out.append(args[0])
        return out

    def town_map(self):
        """engine/items/town_map.asm LoadTownMap: town_map.rle expanded over the 20x18-tile screen, tiles from
        gfx/town_map/town_map.png placed at vChars2 $60, coloured with CGB PAL_TOWNMAP."""
        rle = open(self.p.path("gfx/town_map/town_map.rle"), "rb").read()
        w, h, rows = pngio.read_gray(self.p.path("gfx/town_map/town_map.png"))
        tiles = [[rows[(i // (w // 8)) * 8 + r][(i % (w // 8)) * 8:(i % (w // 8)) * 8 + 8] for r in range(8)]
                 for i in range((w // 8) * (h // 8))]
        screen = []
        for b in rle:
            if b == 0:
                break
            screen += [b >> 4] * (b & 0xF)
        pal = self.cgb["PAL_TOWNMAP"]["rgb"]
        W, H = 160, 144
        px = [pal[0] + (255,)] * (W * H)
        for k, t in enumerate(screen[:20 * 18]):
            ox, oy = (k % 20) * 8, (k // 20) * 8
            for r in range(8):
                for c in range(8):
                    px[(oy + r) * W + ox + c] = pal[tiles[t][r][c]] + (255,)
        d = os.path.join(self.out, "townmap")
        os.makedirs(d, exist_ok=True)
        pngio.write_rgba(os.path.join(d, "townmap.png"), W, H, px)
        # entries
        ext = []
        for line in self.p.lines("data/maps/town_map_entries.asm"):
            op, args = asmlib.macro(line)
            if op == "outdoor_map":
                ext.append((asmlib.num(args[0]), asmlib.num(args[1])))
            elif op == "indoor_map":
                ext.append((args[0], asmlib.num(args[1]), asmlib.num(args[2])))
        outdoor = [e for e in ext if len(e) == 2]
        indoor = {e[0]: (e[1], e[2]) for e in ext if len(e) == 3}
        out = OrderedDict()
        for const in IN_SCOPE:
            m = self.p.maps[const]
            if m["index"] < self.p.first_indoor:
                x, y = outdoor[m["index"]]
            else:
                x, y = indoor[m["group"]]
            out[const] = {"x": x, "y": y}
        self.townmap = out
        jdump(os.path.join(d, "townmap.json"), out)

    # ------------------------------------------------------------------ data tables
    def const_names(self, rel, op="const"):
        out = []
        for line in self.p.lines(rel):
            o, args = asmlib.macro(line)
            if o == op:
                out.append(args[0])
        return out

    def list_strings(self, rel, op):
        out = []
        for line in self.p.lines(rel):
            o, args = asmlib.macro(line)
            if o == op:
                s = textlib.strings_in(args)
                out.append(self.T.cm.decode(s[0], rel)[0] if s else "")
        return out

    def type_names(self):
        consts = self.const_names("constants/type_constants.asm")
        labels = {}
        for line in self.p.lines("data/types/names.asm"):
            m = re.match(r'\.(\w+):\s*db\s+"(.*?)@"', line)
            if m:
                labels[m.group(1)] = m.group(2)
        # data/types/names.asm uses the label order .Normal .. .Dragon matching the constants
        return {c: labels[l] for c, l in zip([c for c in consts], ["Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bird", "Bug", "Ghost", "Fire", "Water", "Grass", "Electric", "Psychic", "Ice", "Dragon"]) if l in labels}

    def build_tables(self):
        tnames = self.type_names()
        self.tnames = tnames
        # types
        chart = OrderedDict()
        mult = {"SUPER_EFFECTIVE": 2.0, "NOT_VERY_EFFECTIVE": 0.5, "NO_EFFECT": 0.0}
        for line in self.p.lines("data/types/type_matchups.asm"):
            op, args = asmlib.macro(line)
            if op == "db" and len(args) == 3:
                chart.setdefault(tnames[args[0]], OrderedDict())[tnames[args[1]]] = mult[args[2]]
        used = [tnames[c] for c in self.const_names("constants/type_constants.asm") if c in tnames and c != "BIRD"]
        jdump(os.path.join(self.out, "types.json"), {
            "types": used, "chart": chart,
            "_note": "chart[attacker][defender] = multiplier; pairs not listed are 1.0 (data/types/type_matchups.asm)"})
        # moves
        mnames = self.list_strings("data/moves/names.asm", "li")
        moves = OrderedDict()
        k = 0
        for line in self.p.lines("data/moves/moves.asm"):
            op, args = asmlib.macro(line)
            if op == "move":
                moves[args[0]] = {"id": k + 1, "name": mnames[k], "effect": args[1], "power": asmlib.num(args[2]),
                                  "type": tnames[args[3]], "accuracy": asmlib.num(args[4]), "pp": asmlib.num(args[5])}
                k += 1
        self.moves = moves
        jdump(os.path.join(self.out, "moves.json"), moves)
        self.build_items()
        self.build_species()
        self.build_trainers()
        self.build_exp()

    def build_items(self):
        names = self.list_strings("data/items/names.asm", "li")
        prices = []
        for line in self.p.lines("data/items/prices.asm"):
            op, args = asmlib.macro(line)
            if op == "bcd3":
                prices.append(asmlib.num(args[0]))
        keys = []
        for line in self.p.lines("data/items/key_items.asm"):
            op, args = asmlib.macro(line)
            if op == "dbit":
                keys.append(args[0] == "TRUE")
        tm_prices = []
        for line in self.p.lines("data/items/tm_prices.asm"):
            op, args = asmlib.macro(line)
            if op == "nybble":
                tm_prices.append(asmlib.num(args[0]) * 1000)
        handlers = []
        for line in self.p.lines("engine/items/item_effects.asm"):
            if line.startswith("ItemUsePtrTable"):
                handlers = []
                continue
            op, args = asmlib.macro(line)
            if op == "dw" and args[0].startswith(("ItemUse", "UnusableItem")) and len(handlers) < len(names):
                handlers.append(args[0])
        use = load_json("item_use.json")
        items = OrderedDict()
        item_consts = [(n, v) for n, v, a in self.p.items]
        tmhm = self.tmhm_moves()
        for n, v in item_consts:
            if n == "NO_ITEM" or n.startswith("FLOOR_"):
                continue
            if v <= len(names) and not n.startswith(("TM_", "HM_")):
                h = handlers[v - 1]
                e = {"id": v, "name": names[v - 1], "price": prices[v - 1], "key": keys[v - 1],
                     "field_use": use[h]["field"], "battle_use": use[h]["battle"], "handler": h}
            elif n.startswith("HM_"):
                num = v - self.c["HM01"] + 1
                e = {"id": v, "name": "HM%02d" % num, "price": 0, "key": True, "hm": num, "move": tmhm["HM%02d" % num],
                     "field_use": True, "battle_use": False, "handler": "ItemUseTMHM"}
            elif n.startswith("TM_"):
                num = v - self.c["TM01"] + 1
                e = {"id": v, "name": "TM%02d" % num, "price": tm_prices[num - 1], "key": False, "tm": num,
                     "move": tmhm["TM%02d" % num], "field_use": True, "battle_use": False, "handler": "ItemUseTMHM"}
            else:
                continue
            items[n] = e
        self.items = items
        jdump(os.path.join(self.out, "items.json"), items)
        # marts: the script_mart text each in-scope map's clerk uses (data/items/marts.asm)
        marts = OrderedDict()
        for const in IN_SCOPE:
            for tconst, ptr in self.text_pointers(const).items():
                for lab in [ptr["label"]] + ptr["alts"]:
                    if self.T.kind(lab) == "special:script_mart":
                        rel, i = self.T.labels[lab]
                        for line in self.T.files[rel][i + 1:]:
                            op, args = asmlib.macro(line)
                            if op == "script_mart":
                                marts[const] = args
                                break
        self.marts = marts
        jdump(os.path.join(self.out, "marts.json"), marts)

    def tmhm_moves(self):
        out = {}
        tm = hm = 0
        for line in self.p.lines("constants/item_constants.asm"):
            op, args = asmlib.macro(line)
            if op == "add_tm":
                tm += 1
                out["TM%02d" % tm] = args[0]
            elif op == "add_hm":
                hm += 1
                out["HM%02d" % hm] = args[0]
        return out

    def build_species(self):
        internal = [n for n, v in sorted(((n, v) for n, v in self.c.order.items()), key=lambda kv: kv[1])]
        mon_consts = []
        for line in self.p.lines("constants/pokemon_constants.asm"):
            op, args = asmlib.macro(line)
            if op == "const":
                mon_consts.append(args[0])
        mon_consts = mon_consts[1:]  # drop NO_MON; index 1..190 (internal order)
        names = [self.T.cm.decode(textlib.strings_in(a)[0], "names")[0]
                 for o, a in (asmlib.macro(l) for l in self.p.lines("data/pokemon/names.asm")) if o == "dname"]
        evos_labels = [a[0] for o, a in (asmlib.macro(l) for l in self.p.lines("data/pokemon/evos_moves.asm")) if o == "dw"]
        dex_labels = [a[0] for o, a in (asmlib.macro(l) for l in self.p.lines("data/pokemon/dex_entries.asm")) if o == "dw"]
        tmhm_index = {}
        k = 0
        for line in self.p.lines("constants/item_constants.asm"):
            op, args = asmlib.macro(line)
            if op in ("add_tm", "add_hm"):
                pass
        tmnum = {}
        tm = hm = 0
        for line in self.p.lines("constants/item_constants.asm"):
            op, args = asmlib.macro(line)
            if op == "add_hm":
                hm += 1
                tmnum[args[0]] = "HM%02d" % hm
            elif op == "add_tm":
                tm += 1
                tmnum.setdefault(args[0], "TM%02d" % tm)
        # evolutions and learnsets
        evo_lines = self.p.lines("data/pokemon/evos_moves.asm")
        evo_data = {}
        cur = None
        stage = None
        for line in evo_lines:
            lab = asmlib.label_of(line)
            if lab and lab.endswith("EvosMoves"):
                cur = lab
                evo_data[cur] = {"evolutions": [], "learnset": []}
                stage = "evo"
                continue
            op, args = asmlib.macro(line)
            if op != "db" or cur is None:
                continue
            if args == ["0"]:
                stage = "learn" if stage == "evo" else None
                continue
            if stage == "evo":
                if args[0] == "EVOLVE_LEVEL":
                    evo_data[cur]["evolutions"].append({"method": "level", "param": asmlib.num(args[1]), "to": args[2]})
                elif args[0] == "EVOLVE_ITEM":
                    evo_data[cur]["evolutions"].append({"method": "item", "param": args[1], "min_level": asmlib.num(args[2]), "to": args[3]})
                elif args[0] == "EVOLVE_TRADE":
                    evo_data[cur]["evolutions"].append({"method": "trade", "param": asmlib.num(args[1]), "to": args[2]})
            elif stage == "learn":
                evo_data[cur]["learnset"].append([asmlib.num(args[0]), args[1]])
        # dex entries
        dex_lines = self.p.lines("data/pokemon/dex_entries.asm")
        dex = {}
        for idx, line in enumerate(dex_lines):
            lab = asmlib.label_of(line)
            if not lab or not lab.endswith("DexEntry") or lab == "MissingNoDexEntry":
                continue
            vals = []
            for l2 in dex_lines[idx + 1:idx + 8]:
                op, args = asmlib.macro(l2)
                if op:
                    vals.append((op, args))
            cat = self.T.cm.decode(textlib.strings_in(vals[0][1])[0], "dex")[0]
            ft, inch = asmlib.num(vals[1][1][0]), asmlib.num(vals[1][1][1])
            lb10 = asmlib.num(vals[2][1][0])
            far = vals[3][1][0]
            dex[lab] = {"category": cat, "height_ft_in": [ft, inch], "weight_lb": lb10 / 10.0,
                        "height_m": round((ft * 12 + inch) * 0.0254, 1), "weight_kg": round(lb10 / 10.0 * 0.45359237, 1),
                        "text": self.T.body(far), "text_id": far}
        # base stats (dex order)
        species = OrderedDict()
        dexnum_to_const = {}
        for path in [l for l in self.p.lines("data/pokemon/base_stats.asm") if "INCLUDE" in l]:
            rel = re.search(r'"(.*)"', path).group(1)
            raw = self.p.lines(rel)
            joined, buf = [], ""
            for l in raw:
                if l.rstrip().endswith("\\"):
                    buf += l.rstrip()[:-1] + " "
                    continue
                joined.append(buf + l)
                buf = ""
            dbs = [asmlib.macro(l) for l in joined]
            dbs = [(o, a) for o, a in dbs if o]
            dexc = dbs[0][1][0]
            stats = [asmlib.num(x) for x in dbs[1][1]]
            types = dbs[2][1]
            catch = asmlib.num(dbs[3][1][0])
            bexp = asmlib.num(dbs[4][1][0])
            lvl1 = [m for m in dbs[7][1] if m != "NO_MOVE"]
            growth = dbs[8][1][0]
            tms = []
            for o, a in dbs:
                if o == "tmhm":
                    tms = [tmnum[x] for x in a]
            num = self.c[dexc]
            dexnum_to_const[num] = rel
            species[rel] = {"dex": num, "types": [], "base": dict(zip(["hp", "attack", "defense", "speed", "special"], stats)),
                            "catch_rate": catch, "base_exp": bexp, "growth": growth[len("GROWTH_"):].lower(),
                            "level1_moves": lvl1, "tm": sorted(set(tms)), "_types": types}
        out = OrderedDict()
        for const in mon_consts:
            i = self.c[const] - 1  # internal index (constants/pokemon_constants.asm), skips MissingNo slots
            if i >= len(names):
                continue
            dconst = "DEX_" + const
            if dconst not in self.c:
                continue
            num = self.c[dconst]
            rel = dexnum_to_const[num]
            s = species[rel]
            ev = evo_data[evos_labels[i]]
            types = []
            for t in s.pop("_types"):
                if self.tnames[t] not in types:
                    types.append(self.tnames[t])
            s["types"] = types
            s["name"] = names[i]
            s["internal_id"] = i + 1
            s["evolutions"] = ev["evolutions"]
            s["learnset"] = [[lv, mv] for lv, mv in ev["learnset"]]
            d = dex[dex_labels[i]]
            s["dex_entry"] = {"category": d["category"], "height_m": d["height_m"], "weight_kg": d["weight_kg"],
                              "height_ft_in": d["height_ft_in"], "weight_lb": d["weight_lb"], "text": d["text"]}
            out[const] = s
        if len(out) != 151:
            raise ValueError("expected 151 species, got %d" % len(out))
        icons = self.menu_icons()
        for sp in out.values():
            if 1 <= sp["dex"] <= len(icons):
                sp["menu_icon"] = icons[sp["dex"] - 1]  # extra: the party screen's icon (data/pokemon/menu_icons.asm)
        self.species = OrderedDict(sorted(out.items(), key=lambda kv: kv[1]["dex"]))
        jdump(os.path.join(self.out, "species.json"), self.species)

    def build_trainers(self):
        classes = [n for n, v, a in self.p.trainer_rows][1:]  # drop NOBODY
        names = self.list_strings("data/trainers/names.asm", "li")
        money = []
        for line in self.p.lines("data/trainers/pic_pointers_money.asm"):
            op, args = asmlib.macro(line)
            if op == "pic_money":
                money.append(asmlib.num(args[1]))
        parties = OrderedDict()
        order = []
        for line in self.p.lines("data/trainers/parties.asm"):
            op, args = asmlib.macro(line)
            if op == "dw" and args[0].endswith("Data"):
                order.append(args[0])
        cur = None
        for line in self.p.lines("data/trainers/parties.asm"):
            lab = asmlib.label_of(line)
            if lab and lab.endswith("Data") and lab in order:
                cur = lab
                parties[cur] = []
                continue
            op, args = asmlib.macro(line)
            if op == "db" and cur:
                if args[0] == "$FF":
                    vals = args[1:-1]
                    party = [{"level": asmlib.num(vals[k]), "species": vals[k + 1]} for k in range(0, len(vals), 2)]
                else:
                    lv = asmlib.num(args[0])
                    party = [{"level": lv, "species": sp} for sp in args[1:-1]]
                parties[cur].append(party)
        # Yellow's special moves (data/trainers/special_moves.asm)
        special = []
        cur = None
        for line in self.p.lines("data/trainers/special_moves.asm"):
            op, args = asmlib.macro(line)
            if op == "db":
                if args == ["0"]:
                    cur = None
                elif args == ["-1"]:
                    break
                elif cur is None:
                    cur = (args[0], asmlib.num(args[1]))
                else:
                    special.append((cur, asmlib.num(args[0]), asmlib.num(args[1]), args[2]))
        out = OrderedDict()
        for k, cls in enumerate(classes):
            ps = parties[order[k]]
            for (c, no), mon, slot, mv in special:
                if c == cls:
                    ps[no - 1][mon - 1].setdefault("moves", {})[str(slot)] = mv
            out[cls] = {"name": names[k], "base_money": money[k] // 100, "base_money_bcd": money[k], "parties": ps,
                        "party_index_base": 1}
        self.trainers = out
        jdump(os.path.join(self.out, "trainers.json"), out)

    def build_exp(self):
        rates = []
        for line in self.p.lines("data/growth_rates.asm"):
            op, args = asmlib.macro(line)
            if op == "growth_rate":
                rates.append([asmlib.num(a) for a in args])
        names = [n[len("GROWTH_"):].lower() for n in self.const_names("constants/pokemon_data_constants.asm") if n.startswith("GROWTH_")]
        out = OrderedDict()
        for name, (a, b, c2, d, e) in zip(names, rates):
            lv = []
            for n in range(1, 101):
                v = (a * n ** 3) // b + c2 * n * n + d * n - e
                lv.append(max(0, v))
            out[name] = lv
        jdump(os.path.join(self.out, "exp.json"), {
            "_note": "exp[rate][level-1] = total exp needed for that level: a/b*n^3 + c*n^2 + d*n - e (data/growth_rates.asm, "
                     "engine/pokemon/experience.asm). Negative results at level 1 are clamped to 0.",
            "rates": out})

    def build_wild(self):
        out = OrderedDict()
        for const in IN_SCOPE:
            lab = self.wild_ptrs[self.p.maps[const]["index"]]
            t = self.wild.get(lab)
            if not t or (not t["grass"] and not t["water"]):
                continue
            out[const] = {"grass_rate": t["grass_rate"], "water_rate": t["water_rate"], "grass": t["grass"],
                          "water": t["water"], "source": t["_source"]}
        chances = []
        for line in self.p.lines("data/wild/probabilities.asm"):
            op, args = asmlib.macro(line)
            if op == "wild_chance":
                chances.append(asmlib.num(args[0]))
        for v in out.values():
            v["slot_chances_256"] = chances
        jdump(os.path.join(self.out, "wild.json"), out)
        self.wildout = out

    def build_trades(self):
        tconsts = [n for n in self.const_names("constants/script_constants.asm") if n.startswith("TRADE_FOR_")]
        rows = []
        for line in self.p.lines("data/events/trades.asm"):
            op, args = asmlib.macro(line)
            if op == "npctrade":
                rows.append(args)
        out = []
        for const in IN_SCOPE:
            rel = "scripts/%s.asm" % self.p.maps[const]["label"]
            for i, line in enumerate(self.T.files[rel]):
                m = re.search(r"ld a, (TRADE_FOR_\w+)", line)
                if m:
                    r = rows[tconsts.index(m.group(1))]
                    out.append({"map": const, "trade": m.group(1), "give": r[0], "get": r[1], "dialog": r[2],
                                "nickname": self.T.cm.decode(textlib.strings_in([r[3]])[0], "trades")[0],
                                "text_handler": self.T.parent_of(rel, i), "source": "data/events/trades.asm"})
        jdump(os.path.join(self.out, "trades.json"), out)
        self.trades = out

    # ------------------------------------------------------------------ run
    def run(self, commit):
        self.build_maps()
        self.place_world()
        self.build_tables()
        self.build_wild()
        self.build_trades()
        import events_pikachu
        events_pikachu.build(self)
        self.build_scenery()
        self.pictures()
        self.build_kanto()
        # Texts of Yellow's out-of-range blockers the game shows at the range's edge (Route 23's badge guards).
        for label in ("_Route23YouDontHaveTheBadgeYetText",):
            if self.text_label(label) is None:
                raise ValueError("%s is not text" % label)
        jdump(os.path.join(self.out, "scenery.json"), self.scenery)
        jdump(os.path.join(self.out, "kanto.json"), self.kanto)
        for const, md in self.maps.items():
            jdump(os.path.join(self.out, "maps", const + ".json"), md)
        jdump(os.path.join(self.out, "text.json"), self.text_ids)
        jdump(os.path.join(self.out, "manifest.json"), {
            "version": 1, "source": {"repo": REPO_URL, "commit": commit}, "maps": IN_SCOPE,
            "generator": "Tools/yellowdata/extract.py"})


def ensure_clone(path):
    if not os.path.isdir(os.path.join(path, ".git")):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        r = subprocess.run(["git", "clone", "--depth", "1", REPO_URL, path])
        if r.returncode != 0:
            sys.exit("clone of %s failed (exit %d)" % (REPO_URL, r.returncode))
    return subprocess.check_output(["git", "-C", path, "rev-parse", "HEAD"], text=True).strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pokeyellow", default=DEFAULT_CLONE)
    ap.add_argument("--out", default=DEFAULT_OUT)
    a = ap.parse_args()
    commit = ensure_clone(a.pokeyellow)
    Extractor(a.pokeyellow, a.out).run(commit)
    print("wrote", a.out, "from pokeyellow", commit)


if __name__ == "__main__":
    main()
