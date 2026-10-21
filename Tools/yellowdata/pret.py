"""Load the pret/pokeyellow disassembly as Python data: constants, maps, tilesets and graphics.

Every function here reads source files directly (.asm, .blk, .bst, .png); nothing is built or invented.
"""
import os
import re
from collections import OrderedDict

import asmlib
import pngio

TILE = 8  # pixels per Game Boy tile; a walking square (the output "tile") is 2x2 of these


class Pret:
    def __init__(self, root):
        self.root = root
        self.c = asmlib.Consts()
        for rel in [
            "constants/hardware_constants.asm" if os.path.exists(os.path.join(root, "constants/hardware_constants.asm")) else None,
            "constants/tileset_constants.asm",
            "constants/music_constants.asm",
            "constants/pokemon_constants.asm",
            "constants/pokedex_constants.asm",
            "constants/move_constants.asm",
            "constants/type_constants.asm",
            "constants/pokemon_data_constants.asm",
            "constants/sprite_constants.asm",
            "constants/palette_constants.asm",
        ]:
            if rel:
                self.c.load(self.path(rel))
        self.map_rows = []
        self.c.load(self.path("constants/map_constants.asm"), collect=self.map_rows)
        self.items = []
        self.c.load(self.path("constants/item_constants.asm"), collect=self.items)
        self.trainer_rows = []
        self.c.load(self.path("constants/trainer_constants.asm"), collect=self.trainer_rows)
        self._maps_meta()

    def path(self, rel):
        return os.path.join(self.root, rel.replace("/", os.sep))

    def lines(self, rel):
        return asmlib.read_lines(self.path(rel))

    # ---------------------------------------------------------------- maps
    def _maps_meta(self):
        """Map constants with block sizes, indoor groups and header data."""
        self.maps = OrderedDict()
        rows = asmlib.read_lines(self.path("constants/map_constants.asm"))
        pending = []
        for line in rows:
            op, args = asmlib.macro(line)
            if op == "map_const":
                name = args[0]
                self.maps[name] = {
                    "const": name,
                    "index": self.c[name],
                    "bw": asmlib.num(args[1]),
                    "bh": asmlib.num(args[2]),
                    "group": None,
                }
                pending.append(name)
            elif op == "end_indoor_group":
                for n in pending:
                    self.maps[n]["group"] = args[0]
                pending = []
        self.first_indoor = self.c["FIRST_INDOOR_MAP"]
        self.num_city = self.c["NUM_CITY_MAPS"]
        self.label_of = {}
        for fn in sorted(os.listdir(self.path("data/maps/headers"))):
            lines = asmlib.read_lines(self.path("data/maps/headers/" + fn))
            conns = OrderedDict()
            header = None
            for line in lines:
                op, args = asmlib.macro(line)
                if op == "map_header":
                    header = args
                elif op == "connection":
                    conns[args[0]] = {"label": args[1], "map": args[2], "offset_blocks": asmlib.num(args[3], self.c)}
            if not header:
                continue
            label, const, tileset = header[0], header[1], header[2]
            m = self.maps[const]
            m.update({"label": label, "tileset": tileset, "connections": conns, "header_file": "data/maps/headers/" + fn})
            self.label_of[const] = label

    def map_objects(self, const):
        """Parse data/maps/objects/<Label>.asm."""
        label = self.label_of[const]
        rel = "data/maps/objects/%s.asm" % label
        # The retail build: IF DEF(_DEBUG) warps (Red's room) are not in the ROM.
        from textlib import drop_conditionals
        lines = drop_conditionals(self.lines(rel))
        out = {"file": rel, "object_consts": [], "warps": [], "bgs": [], "objects": [], "border": None}
        section = None
        for line in lines:
            op, args = asmlib.macro(line)
            if op == "const_export":
                out["object_consts"].append(args[0])
            elif op == "db" and out["border"] is None and section is None:
                out["border"] = asmlib.num(args[0])
            elif op == "def_warp_events":
                section = "warp"
            elif op == "warp_event":
                out["warps"].append({"x": asmlib.num(args[0]), "y": asmlib.num(args[1]), "to_map": args[2], "to_warp": asmlib.num(args[3])})
            elif op == "bg_event":
                out["bgs"].append({"x": asmlib.num(args[0]), "y": asmlib.num(args[1]), "text": args[2]})
            elif op == "object_event":
                o = {
                    "x": asmlib.num(args[0]),
                    "y": asmlib.num(args[1]),
                    "sprite": args[2],
                    "movement": args[3],
                    "range": args[4],
                    "text": args[5],
                }
                if len(args) == 8:
                    o["trainer_class"] = args[6]
                    o["trainer_no"] = asmlib.num(args[7])
                elif len(args) == 7:
                    if args[6] in ("0", "NO_ITEM"):
                        o["item_flag_zero"] = True  # ITEM | text with item 0: a person, not an item ball
                    else:
                        o["item"] = args[6]
                out["objects"].append(o)
        return out

    def blocks(self, const):
        m = self.maps[const]
        data = open(self.path("maps/%s.blk" % m["label"]), "rb").read()
        if len(data) != m["bw"] * m["bh"]:
            raise ValueError("%s.blk has %d bytes, expected %d" % (m["label"], len(data), m["bw"] * m["bh"]))
        return data

    # ---------------------------------------------------------------- tilesets
    def tileset_table(self):
        """data/tilesets/tileset_headers.asm + gfx/tilesets.asm + collision tables, keyed by tileset constant."""
        names = [n for n, v in sorted(((k, v) for k, v in self.c.order.items()), key=lambda kv: kv[1])]
        headers = []
        for line in self.lines("data/tilesets/tileset_headers.asm"):
            op, args = asmlib.macro(line)
            if op == "tileset":
                headers.append(args)
        consts = []
        for line in self.lines("constants/tileset_constants.asm"):
            op, args = asmlib.macro(line)
            if op == "const":
                consts.append(args[0])
        gfx_file, blk_file = {}, {}
        pending_gfx, pending_blk = [], []
        for line in self.lines("gfx/tilesets.asm"):
            for lab in re.findall(r"(\w+)_GFX::", line):
                pending_gfx.append(lab)
            for lab in re.findall(r"(\w+)_Block::", line):
                pending_blk.append(lab)
            m = re.search(r'INCBIN "gfx/tilesets/(\w+)\.2bpp"', line)
            if m:
                for lab in pending_gfx:
                    gfx_file[lab] = "gfx/tilesets/%s.png" % m.group(1)
                pending_gfx = []
            m = re.search(r'INCBIN "gfx/blocksets/(\w+)\.bst"', line)
            if m:
                for lab in pending_blk:
                    blk_file[lab] = "gfx/blocksets/%s.bst" % m.group(1)
                pending_blk = []
        coll = {}
        pending = []
        for line in self.lines("data/tilesets/collision_tile_ids.asm"):
            for lab in re.findall(r"^(\w+)_Coll::", line):
                pending.append(lab)
            op, args = asmlib.macro(line)
            if op == "coll_tiles":
                for lab in pending:
                    coll[lab] = [asmlib.num(a) for a in args]
                pending = []
        out = OrderedDict()
        for const, h in zip(consts, headers):
            lab = h[0]
            counters = [asmlib.num(x) for x in h[1:4] if asmlib.num(x) != -1]
            grass = asmlib.num(h[4])
            out[const] = {
                "label": lab,
                "gfx": gfx_file[lab],
                "blockset": blk_file[lab],
                "walkable": coll[lab],
                "counters": counters,
                "grass": grass if grass != -1 else None,
            }
        return out

    def tile_gfx(self, rel):
        """List of 8x8 tiles (each 8 rows of 8 colour indices) from a tileset PNG, row-major."""
        w, h, rows = pngio.read_gray(self.path(rel))
        tiles = []
        for ty in range(h // TILE):
            for tx in range(w // TILE):
                tiles.append([rows[ty * TILE + r][tx * TILE:(tx + 1) * TILE] for r in range(TILE)])
        return tiles

    def blockset(self, rel):
        data = open(self.path(rel), "rb").read()
        return [list(data[i:i + 16]) for i in range(0, len(data), 16)]

    # ---------------------------------------------------------------- palettes
    def cgb_palettes(self):
        """CGBBasePalettes in data/sgb/sgb_palettes.asm, keyed by PAL_* constant, as 8-bit RGB."""
        pal_names = []
        for line in self.lines("constants/palette_constants.asm"):
            op, args = asmlib.macro(line)
            if op == "const" and args[0].startswith("PAL_"):
                pal_names.append(args[0])
        out = OrderedDict()
        section = None
        i = 0
        for line in self.lines("data/sgb/sgb_palettes.asm"):
            if line.startswith("CGBBasePalettes"):
                section = "cgb"
                continue
            if section == "cgb" and line and not line[0].isspace():
                break
            op, args = asmlib.macro(line)
            if section == "cgb" and op == "RGB":
                vals = [asmlib.num(a) for a in args]
                cols = [tuple((v << 3) | (v >> 2) for v in vals[k:k + 3]) for k in range(0, 12, 3)]
                out[pal_names[i]] = {"rgb5": [vals[k:k + 3] for k in range(0, 12, 3)], "rgb": cols}
                i += 1
        return out
