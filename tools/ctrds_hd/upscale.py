"""Build 4x replacement art from the icons dumped out of VRAM.

Three things happen here, in order, and the order matters:

1. The black backing is flood-filled from the border. These icons are drawn with
   an opaque black box behind them, which is why they read as a grey rectangle
   over the map. Only black *connected to the edge* is removed, so Crash's pupils
   and Polar's nose -- black, but interior -- survive.

2. The colour under transparent pixels is bled outward from the nearest opaque
   neighbour. Without this the scaler pulls the black that is still sitting in
   those pixels into the artwork and every icon gets a dark halo.

3. Colour and alpha are scaled separately by xBR, then recombined. xBR has no
   notion of alpha, so the mask is scaled as a greyscale image through the same
   filter -- which keeps the edge of the mask and the edge of the art agreeing.
"""

import os
import subprocess
import tempfile
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import png

SCALE = 4

# How dark counts as backing. The backing is a true 0,0,0 but 5-bit colour and
# the odd dark outline pixel sit just above it.
BLACK = 24

TMP = tempfile.mkdtemp(prefix='ctrds-hd-')


def flood_transparent(img):
    """Clears black connected to the border, leaving interior black alone."""
    w, h = img.w, img.h
    seen = bytearray(w * h)
    stack = []

    def dark(x, y):
        r, g, b, a = img.get(x, y)
        return (a == 0) or (r <= BLACK and g <= BLACK and b <= BLACK)

    for x in range(w):
        for y in (0, h - 1):
            if dark(x, y):
                stack.append((x, y))
    for y in range(h):
        for x in (0, w - 1):
            if dark(x, y):
                stack.append((x, y))

    while stack:
        x, y = stack.pop()
        i = y * w + x
        if seen[i]:
            continue
        seen[i] = 1
        img.set(x, y, (0, 0, 0, 0))

        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if 0 <= nx < w and 0 <= ny < h and not seen[ny * w + nx] and dark(nx, ny):
                stack.append((nx, ny))

    return img


def bleed(img, passes=6):
    """Pushes opaque colour outward into the transparent region."""
    w, h = img.w, img.h

    for _ in range(passes):
        out = img.copy()
        changed = False

        for y in range(h):
            for x in range(w):
                if img.get(x, y)[3] != 0:
                    continue

                acc = [0, 0, 0]
                n = 0

                for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                    if 0 <= nx < w and 0 <= ny < h:
                        r, g, b, a = img.get(nx, ny)
                        # A bled pixel is still transparent, so it has to be
                        # allowed to seed the next ring outward.
                        if a != 0 or (r or g or b):
                            acc[0] += r
                            acc[1] += g
                            acc[2] += b
                            n += 1

                if n:
                    out.set(x, y, (acc[0] // n, acc[1] // n, acc[2] // n, 0))
                    changed = True

        img = out
        if not changed:
            break

    return img


def xbr(src, dst, n):
    subprocess.run(
        ['ffmpeg', '-y', '-loglevel', 'error', '-i', src,
         '-vf', 'xbr=n=%d' % n, '-frames:v', '1', dst],
        check=True)


def process(src, dst):
    img = png.read(src)

    img = flood_transparent(img)

    alpha = png.Image(img.w, img.h)
    for y in range(img.h):
        for x in range(img.w):
            a = img.get(x, y)[3]
            alpha.set(x, y, (a, a, a, 255))

    colour = bleed(img)
    for y in range(colour.h):
        for x in range(colour.w):
            r, g, b, _ = colour.get(x, y)
            colour.set(x, y, (r, g, b, 255))

    c_in = os.path.join(TMP, 'c_in.png')
    a_in = os.path.join(TMP, 'a_in.png')
    c_out = os.path.join(TMP, 'c_out.png')
    a_out = os.path.join(TMP, 'a_out.png')

    png.write(c_in, colour)
    png.write(a_in, alpha)

    xbr(c_in, c_out, SCALE)
    xbr(a_in, a_out, SCALE)

    big_c = png.read(c_out)
    big_a = png.read(a_out)

    out = png.Image(big_c.w, big_c.h)
    for y in range(big_c.h):
        for x in range(big_c.w):
            r, g, b, _ = big_c.get(x, y)
            out.set(x, y, (r, g, b, big_a.get(x, y)[0]))

    png.write(dst, out)
    return out.w, out.h


def main():
    src_dir, dst_dir = sys.argv[1], sys.argv[2]
    only = set(sys.argv[3:])

    os.makedirs(dst_dir, exist_ok=True)

    names = sorted(
        (f for f in os.listdir(src_dir) if f.endswith('.png')),
        key=lambda f: int(f[5:-4]))

    done = 0
    for name in names:
        index = name[5:-4]
        if only and index not in only:
            continue

        w, h = process(os.path.join(src_dir, name), os.path.join(dst_dir, name))
        done += 1
        print('%s -> %dx%d' % (name, w, h))

    print('%d icons upscaled %dx' % (done, SCALE))


if __name__ == '__main__':
    main()
