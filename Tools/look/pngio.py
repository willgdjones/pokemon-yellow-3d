"""PNG reading and writing with the standard library only (the engine's Python has no PIL). m4v look tools.

    img = read(path)            # Image: w, h, channels (3 or 4), rows of bytes in .data (bytearray, RGB(A) 8-bit)
    write(path, w, h, rgba)     # rgba: bytes/bytearray of w*h*4, 8-bit, written with filter 0 and zlib level 9

Only 8-bit, non-interlaced grey, grey+alpha, RGB, RGBA and palette PNGs are read; that covers every PNG the project
writes (Unreal screenshots, Scenario textures, these tools).
"""
import struct
import zlib


class Image:
    def __init__(self, w, h, channels, data):
        self.w, self.h, self.channels, self.data = w, h, channels, data

    def pixel(self, x, y):
        i = (y * self.w + x) * self.channels
        return tuple(self.data[i:i + self.channels])

    def rgba(self):
        """The pixels as RGBA bytes."""
        if self.channels == 4:
            return bytes(self.data)
        out = bytearray(self.w * self.h * 4)
        out[3::4] = b'\xff' * (self.w * self.h)
        for c in range(3):
            out[c::4] = self.data[c::3]
        return bytes(out)


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read(path):
    raw = open(path, 'rb').read()
    if raw[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path}: not a PNG')
    pos, idat, palette, trns = 8, [], None, None
    w = h = depth = ctype = interlace = None
    while pos < len(raw):
        length, kind = struct.unpack('>I4s', raw[pos:pos + 8])
        body = raw[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
        elif kind == b'PLTE':
            palette = body
        elif kind == b'tRNS':
            trns = body
        elif kind == b'IDAT':
            idat.append(body)
        elif kind == b'IEND':
            break
    if depth != 8 or interlace:
        raise ValueError(f'{path}: only 8-bit non-interlaced PNGs are read (depth {depth}, interlace {interlace})')
    src_ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    stride = w * src_ch
    flat = zlib.decompress(b''.join(idat))
    out = bytearray(h * stride)
    prev = bytearray(stride)
    for y in range(h):
        f = flat[y * (stride + 1)]
        line = bytearray(flat[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        bpp = src_ch
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                up_left = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + _paeth(left, prev[i], up_left)) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    if ctype == 3:
        rgb = bytearray(w * h * 3)
        for i, index in enumerate(out):
            rgb[i * 3:i * 3 + 3] = palette[index * 3:index * 3 + 3]
        return Image(w, h, 3, rgb)
    if ctype == 0:
        rgb = bytearray(w * h * 3)
        for c in range(3):
            rgb[c::3] = out
        return Image(w, h, 3, rgb)
    if ctype == 4:
        rgba = bytearray(w * h * 4)
        for c in range(3):
            rgba[c::4] = out[0::2]
        rgba[3::4] = out[1::2]
        return Image(w, h, 4, rgba)
    return Image(w, h, src_ch, out)


def _chunk(kind, body):
    return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)


def write(path, w, h, rgba, channels=4):
    stride = w * channels
    rows = bytearray()
    for y in range(h):
        rows.append(0)
        rows += rgba[y * stride:(y + 1) * stride]
    ctype = 6 if channels == 4 else 2
    png = (b'\x89PNG\r\n\x1a\n' + _chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, ctype, 0, 0, 0))
           + _chunk(b'IDAT', zlib.compress(bytes(rows), 9)) + _chunk(b'IEND', b''))
    with open(path, 'wb') as f:
        f.write(png)
