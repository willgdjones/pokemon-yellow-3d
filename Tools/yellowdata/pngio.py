"""Minimal PNG reader and writer using only the standard library (no Pillow on the build machines).

read_gray(path) returns (width, height, rows) where rows[y][x] is the Game Boy colour index 0..3
(0 = white, 3 = black), which is how pokeyellow's 2-bit grayscale PNGs are meant to be read.
write_rgba(path, width, height, pixels) writes an 8-bit RGBA PNG from a flat list of (r, g, b, a).
read_rgba(path) reads the PNGs this module writes (colour type 6, 8-bit) back, for verify.py.
"""
import struct
import zlib


def _chunks(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos = 8
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        yield kind, body


def _unfilter(raw, height, stride, bpp):
    rows = []
    prev = bytearray(stride)
    pos = 0
    for _ in range(height):
        ftype = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ftype == 1:
                line[i] = (line[i] + a) & 255
            elif ftype == 2:
                line[i] = (line[i] + b) & 255
            elif ftype == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 255
            elif ftype != 0:
                raise ValueError("bad PNG filter %d" % ftype)
        rows.append(line)
        prev = line
    return rows


def _decode(path):
    data = open(path, "rb").read()
    idat = b""
    palette = None
    for kind, body in _chunks(data):
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if interlace:
                raise ValueError("interlaced PNG not supported: %s" % path)
        elif kind == b"PLTE":
            palette = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif kind == b"IDAT":
            idat += body
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bits = depth * channels
    stride = (width * bits + 7) // 8
    bpp = max(1, bits // 8)
    rows = _unfilter(zlib.decompress(idat), height, stride, bpp)
    return width, height, depth, ctype, channels, palette, rows


def _samples(line, width, depth, channels):
    if depth == 8:
        return [tuple(line[x * channels:(x + 1) * channels]) for x in range(width)]
    if depth == 16:
        return [tuple(line[(x * channels + c) * 2] for c in range(channels)) for x in range(width)]
    out = []
    per = 8 // depth
    mask = (1 << depth) - 1
    for x in range(width):
        byte = line[x // per]
        shift = 8 - depth * (x % per + 1)
        out.append(((byte >> shift) & mask,))
    return out


def read_gray(path):
    """Read a pokeyellow 2bpp-style grayscale PNG as Game Boy colour indices (0 white .. 3 black)."""
    width, height, depth, ctype, channels, palette, rows = _decode(path)
    out = []
    for line in rows:
        samples = _samples(line, width, depth, channels)
        row = []
        for s in samples:
            if ctype == 3:
                r, g, b = palette[s[0]]
                level = (r * 299 + g * 587 + b * 114) // 1000
            elif ctype in (2, 6):
                level = (s[0] * 299 + s[1] * 587 + s[2] * 114) // 1000
            else:
                level = s[0] * 255 // ((1 << depth) - 1) if depth < 8 else s[0]
            # 4 grey levels: 255 white=0, 170=1, 85=2, 0 black=3
            row.append(3 - min(3, (level + 42) // 85))
        out.append(row)
    return width, height, out


def read_rgba(path):
    width, height, depth, ctype, channels, palette, rows = _decode(path)
    if depth != 8 or ctype not in (2, 6):
        raise ValueError("read_rgba only reads 8-bit RGB/RGBA PNGs")
    pixels = []
    for line in rows:
        for s in _samples(line, width, depth, channels):
            pixels.append(tuple(s) + ((255,) if channels == 3 else ()))
    return width, height, pixels


def png_size(path):
    data = open(path, "rb").read(33)
    return struct.unpack(">II", data[16:24])


def write_rgba(path, width, height, pixels):
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(pixels[y * width + x])

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)
