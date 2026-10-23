"""Copies the upscaled art that is safe to ship into the build and the APK.

Not every icon can be replaced. The overlay composites with straight alpha
blending, so anything the game draws additively or subtractively -- the kart
shadows, the powered-up glow -- would come out as an opaque blob and is left on
the PSX path. Anything drawn with partial UVs is out for the same reason the
font is: the overlay replaces a whole icon, and the font atlas is one icon
holding every glyph.

What is left is the art that is drawn as a plain whole-icon sprite, which is
also the art the player actually looks at: the driver portraits, the item in the
box, the position numeral, and the button prompts.
"""

import os
import shutil
import sys

PORTRAITS = list(range(32, 45)) + [53, 54, 55]
NUMERALS = [25, 26, 27, 28]
# 12 (doctor) and 50 (ukauka) are left out: both dump as two half-masks spliced
# together, so their icon rect spans more than one animation cell and a single
# still cannot stand in for them.
ITEMS = [2, 5, 6, 7, 8, 9, 11, 13, 14, 15, 16, 18, 20, 21, 23]
BUTTONS = [59, 60, 61, 62, 128, 129, 130, 131]

SHIP = PORTRAITS + NUMERALS + ITEMS + BUTTONS


def main():
    src = sys.argv[1]
    targets = sys.argv[2:]

    for target in targets:
        os.makedirs(target, exist_ok=True)

    copied = 0
    missing = []

    for index in SHIP:
        name = 'icon_%d.png' % index
        path = os.path.join(src, name)

        if not os.path.exists(path):
            missing.append(index)
            continue

        for target in targets:
            shutil.copy2(path, os.path.join(target, name))
        copied += 1

    print('%d of %d icons installed to %s' % (copied, len(SHIP), ', '.join(targets)))
    if missing:
        print('not in the dump: %s' % ' '.join(str(i) for i in missing))


if __name__ == '__main__':
    main()
