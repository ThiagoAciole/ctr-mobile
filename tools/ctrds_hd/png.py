"""Minimal PNG read/write.

Only what this pipeline produces and consumes: 8-bit, non-interlaced, colour
types 0 (grey), 2 (RGB) and 6 (RGBA). Images are held as a flat bytearray of
RGBA rows, which is the only shape the rest of the tooling needs.
"""

import struct
import zlib


class Image:
    def __init__(self, w, h, data=None):
        self.w = w
        self.h = h
        self.data = data if data is not None else bytearray(w * h * 4)

    def get(self, x, y):
        i = (y * self.w + x) * 4
        return self.data[i], self.data[i + 1], self.data[i + 2], self.data[i + 3]

    def set(self, x, y, px):
        i = (y * self.w + x) * 4
        self.data[i] = px[0]
        self.data[i + 1] = px[1]
        self.data[i + 2] = px[2]
        self.data[i + 3] = px[3]

    def copy(self):
        return Image(self.w, self.h, bytearray(self.data))


def _paeth(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read(path):
    with open(path, 'rb') as f:
        raw = f.read()

    assert raw[:8] == b'\x89PNG\r\n\x1a\n', path

    pos = 8
    idat = bytearray()
    w = h = depth = ctype = None
    palette = None
    trns = None

    while pos < len(raw):
        (length,) = struct.unpack('>I', raw[pos:pos + 4])
        kind = raw[pos + 4:pos + 8]
        body = raw[pos + 8:pos + 8 + length]
        pos += 12 + length

        if kind == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
            assert depth == 8, 'only 8-bit'
            assert interlace == 0, 'no interlace'
        elif kind == b'PLTE':
            palette = body
        elif kind == b'tRNS':
            trns = body
        elif kind == b'IDAT':
            idat += body
        elif kind == b'IEND':
            break

    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    stride = w * channels
    buf = zlib.decompress(bytes(idat))

    lines = bytearray(stride * h)
    prev = bytearray(stride)
    src = 0

    for y in range(h):
        ft = buf[src]
        src += 1
        line = bytearray(buf[src:src + stride])
        src += stride

        if ft == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                c = prev[i - channels] if i >= channels else 0
                line[i] = (line[i] + _paeth(a, prev[i], c)) & 0xFF

        lines[y * stride:(y + 1) * stride] = line
        prev = line

    out = bytearray(w * h * 4)

    for i in range(w * h):
        s = i * channels
        d = i * 4
        if ctype == 0:
            v = lines[s]
            out[d] = out[d + 1] = out[d + 2] = v
            out[d + 3] = 255
        elif ctype == 2:
            out[d:d + 3] = lines[s:s + 3]
            out[d + 3] = 255
        elif ctype == 3:
            idx = lines[s]
            out[d:d + 3] = palette[idx * 3:idx * 3 + 3]
            out[d + 3] = trns[idx] if (trns and idx < len(trns)) else 255
        elif ctype == 4:
            v = lines[s]
            out[d] = out[d + 1] = out[d + 2] = v
            out[d + 3] = lines[s + 1]
        else:
            out[d:d + 4] = lines[s:s + 4]

    return Image(w, h, out)


def write(path, img):
    stride = img.w * 4
    raw = bytearray()

    for y in range(img.h):
        raw.append(0)
        raw += img.data[y * stride:(y + 1) * stride]

    def chunk(kind, body):
        return (struct.pack('>I', len(body)) + kind + body +
                struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF))

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', img.w, img.h, 8, 6, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(bytes(raw), 9)))
        f.write(chunk(b'IEND', b''))
