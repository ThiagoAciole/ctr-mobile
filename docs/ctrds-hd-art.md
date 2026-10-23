# CTR-DS replacement 2D art

The HUD's 2D art is 4-bit paletted sprites drawn as PSX quads that sample
emulated VRAM. This is a pipeline for taking that art out of the build,
upscaling it, and putting it back as real GL textures.

## Why not CTR-tools

CTR-tools parses the disc's own files and would give the art in its authored
form. Two things argue against it here: it needs a .NET toolchain the build does
not otherwise want, and its output still has to be matched back to the icon
indices the port draws with. Reading emulated VRAM instead gives exactly the
texels the game is drawing, already through the palette it drew them with, and
each one arrives labelled with the index the override path keys on. The dump is
about thirty lines and needs nothing that is not already linked in.

## Getting the originals out

Set `hd_dump` in `ctrds.cfg` next to the disc image:

| value | behaviour |
|---|---|
| `0` | off |
| `1` | sweep every ~5s, overwriting; describes whatever is on screen now |
| `2` | wait for a race, sweep once; the race HUD art, safe from being overwritten |

PNGs land in `<assets>/hd_dump/icon_<index>.png`.

Use `2` for HUD art. An icon slot holds different texels in different contexts —
a race HUD icon read at the title screen is whatever art happens to occupy that
VRAM address at the time, which is how a driver portrait comes out as a piece of
the track-select screen. Nothing in the icon says which context it belongs to.

`hd_log=1` reports each icon index and name as it is first drawn, which is how
you find out which index holds what.

## Building the replacements

`tools/ctrds_hd/upscale.py <dump-dir> <out-dir> [indices...]`

Three steps, and the order matters:

1. **The black backing is flood-filled from the border.** These sprites are
   drawn with an opaque black box behind them, which is what makes a portrait
   read as a grey rectangle when it sits over the map. Only black *connected to
   the edge* goes; Crash's pupils and Polar's nose are black too, and interior.
2. **Colour is bled outward under the new transparency.** Skipping this leaves
   black sitting in those pixels for the scaler to pull into the artwork, and
   every icon comes out with a dark halo.
3. **Colour and alpha are scaled separately by xBR (`ffmpeg -vf xbr=n=4`) and
   recombined.** xBR has no notion of alpha, so the mask goes through the same
   filter as a greyscale image, which keeps the edge of the mask and the edge of
   the art agreeing.

## Putting them back

`tools/ctrds_hd/install.py <upscaled-dir> <target>...`

A PNG at `hd/icon_<index>.png` replaces that icon. Two places are searched, in
order: `<assets>/hd/` (what a player drops next to their disc image) and then
`hd/` relative — which on Android falls through SDL to the asset manager, so
whatever is in `android/app/src/main/assets/hd/` ships in the APK and needs no
install step.

## What can be replaced, and what cannot

The overlay draws replaced icons as GL quads with straight alpha blending, after
the ordering table has been walked. That sets the limits:

- **Not** anything drawn additively or subtractively — the kart shadows
  (`shadow1`, `shadow2`) and the powered-up glow (`poweredup`) would come out as
  opaque blobs.
- **Not** anything drawn with partial UVs. The overlay swaps a whole icon, and
  `debugfont` is one icon holding every glyph.
- **Not** `doctor` or `ukauka`. Both dump as two half-masks spliced together, so
  their icon rect spans more than one animation cell and no single still can
  stand in for them.

What is left is what the player actually looks at: the driver portraits, the
item in the box, the position numeral, and the button prompts.

Replacement textures get mipmaps. A portrait used as a map marker is a 172px
texture in a 16px box, and without them the marker crawls as the map rotates.

## Note on the dump itself

Reading VRAM resolves pending GPU writes, so it happens once per sweep from
`Ctrds_HdDumpTick`, between frames. Doing it from the draw path — on first draw
of each icon, which is the obvious place — takes the renderer apart underneath
itself and kills the process with no message.
