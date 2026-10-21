"""Helper for writing looks.json: render each in-scope tileset's 8x8 tiles as a labelled sheet.

    python tilesheet.py --pokeyellow <clone> --out <dir>

Writes <dir>/<TILESET>.png: tiles scaled x4 in a 16-wide grid, row labels (high hex digit) on the left and
column labels (low hex digit) on top, so tile $3D is row 3, column D. Tiles that in-scope maps use as the
bottom-left (collision) tile of a walking square are outlined in red; other used tiles in blue.
Also writes <dir>/<TILESET>_squares.png: every distinct 16x16 walking square used in scope, x3,
sorted by bottom-left tile id, with that id printed above it.
"""
import argparse
import os

import pngio
import pret
from extract import IN_SCOPE

FONT = {
    "0": ["111", "101", "101", "101", "111"], "1": ["010", "110", "010", "010", "111"],
    "2": ["111", "001", "111", "100", "111"], "3": ["111", "001", "111", "001", "111"],
    "4": ["101", "101", "111", "001", "001"], "5": ["111", "100", "111", "001", "111"],
    "6": ["111", "100", "111", "101", "111"], "7": ["111", "001", "010", "010", "010"],
    "8": ["111", "101", "111", "101", "111"], "9": ["111", "101", "111", "001", "111"],
    "A": ["010", "101", "111", "101", "101"], "B": ["110", "101", "110", "101", "110"],
    "C": ["011", "100", "100", "100", "011"], "D": ["110", "101", "101", "101", "110"],
    "E": ["111", "100", "110", "100", "111"], "F": ["111", "100", "110", "100", "100"],
}
GREYS = [(255, 255, 255, 255), (170, 170, 170, 255), (85, 85, 85, 255), (0, 0, 0, 255)]


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [(40, 40, 60, 255)] * (w * h)

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y * self.w + x] = c

    def text(self, x, y, s, scale=2, c=(255, 255, 0, 255)):
        for i, ch in enumerate(s):
            for ry, row in enumerate(FONT[ch]):
                for rx, bit in enumerate(row):
                    if bit == "1":
                        for dy in range(scale):
                            for dx in range(scale):
                                self.put(x + (i * 4 + rx) * scale + dx, y + ry * scale + dy, c)

    def tile(self, x, y, tile, scale):
        for r in range(8):
            for q in range(8):
                for dy in range(scale):
                    for dx in range(scale):
                        self.put(x + q * scale + dx, y + r * scale + dy, GREYS[tile[r][q]])

    def box(self, x, y, w, h, c):
        for i in range(w):
            self.put(x + i, y, c)
            self.put(x + i, y + h - 1, c)
        for j in range(h):
            self.put(x, y + j, c)
            self.put(x + w - 1, y + j, c)

    def save(self, path):
        pngio.write_rgba(path, self.w, self.h, self.px)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pokeyellow", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    p = pret.Pret(a.pokeyellow)
    ts = p.tileset_table()
    used_bl, used_any, squares = {}, {}, {}
    for const in IN_SCOPE:
        t = p.maps[const]["tileset"]
        blocks = p.blockset(ts[t]["blockset"])
        data = p.blocks(const)
        for b in data:
            blk = blocks[b]
            for sy in range(2):
                for sx in range(2):
                    q = (blk[(sy * 2) * 4 + sx * 2], blk[(sy * 2) * 4 + sx * 2 + 1],
                         blk[(sy * 2 + 1) * 4 + sx * 2], blk[(sy * 2 + 1) * 4 + sx * 2 + 1])
                    used_bl.setdefault(t, set()).add(q[2])
                    used_any.setdefault(t, set()).update(q)
                    squares.setdefault(t, set()).add(q)
    for t in sorted(used_bl):
        tiles = p.tile_gfx(ts[t]["gfx"])
        s, cell = 4, 8 * 4 + 4
        rows = (len(tiles) + 15) // 16
        cv = Canvas(20 + 16 * cell, 20 + rows * cell)
        for i in range(16):
            cv.text(20 + i * cell + 12, 4, "%X" % i)
        for r in range(rows):
            cv.text(4, 20 + r * cell + 12, "%X" % r)
        for i, tile in enumerate(tiles):
            x, y = 20 + (i % 16) * cell + 2, 20 + (i // 16) * cell + 2
            cv.tile(x, y, tile, s)
            if i in used_bl[t]:
                cv.box(x - 2, y - 2, 36, 36, (255, 0, 0, 255))
            elif i in used_any[t]:
                cv.box(x - 2, y - 2, 36, 36, (0, 128, 255, 255))
        cv.save(os.path.join(a.out, "%s.png" % t))
        sq = sorted(squares[t], key=lambda q: (q[2], q))
        per, cw, ch = 12, 16 * 3 + 10, 16 * 3 + 16
        cv = Canvas(per * cw, ((len(sq) + per - 1) // per) * ch)
        for i, q in enumerate(sq):
            x, y = (i % per) * cw + 5, (i // per) * ch + 14
            cv.text(x, y - 12, "%02X" % q[2])
            for k, tid in enumerate(q):
                if tid < len(tiles):
                    cv.tile(x + (k % 2) * 24, y + (k // 2) * 24, tiles[tid], 3)
        cv.save(os.path.join(a.out, "%s_squares.png" % t))
        print(t, "bottom-left ids:", " ".join("%02X" % i for i in sorted(used_bl[t])))


if __name__ == "__main__":
    main()
