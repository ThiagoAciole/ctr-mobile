"""Check native caption placement and race-title wipe visibility on Android/desktop."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=shutil.which("gcc"))
    args = parser.parse_args()
    if not args.cc:
        parser.error("GCC is required; pass --cc /path/to/gcc")
    root = Path(__file__).resolve().parents[1]
    source = (root / "game/232/AH_WarpPad.c").read_text(encoding="utf-8")
    start = source.index("int captionY =")
    end = source.index("DecalFont_DrawLine(warppadLNG", start)
    production = source[start:end]
    harness = """#include <assert.h>
static struct { struct { struct { int y, h; } rect; } pushBuffer[1]; } state;
#define gGT (&state)
static int touch;
#ifdef __ANDROID__
static int Platform_InputTouchActive(void) { return touch; }
#endif
static int caption(void) {
PRODUCTION
return captionY;
}
int main(void) {
    for (int height = 216; height <= 240; height += 24)
    for (int origin = 0; origin <= 16; origin += 16)
    for (touch = 0; touch <= 1; touch++) {
        state.pushBuffer[0].rect.y = origin;
        state.pushBuffer[0].rect.h = height;
#ifdef __ANDROID__
        if (touch) {
            assert(caption() == origin + height / 4);
            assert(caption() + 24 < origin + height * 0.60);
            continue;
        }
#endif
        assert(caption() == origin + height - 30);
    }
}
""".replace("PRODUCTION", production)
    race_source = (root / "game/UI/UI_RaceFlow.c").read_text(encoding="utf-8")
    start = race_source.index("int canDrawRaceTitles =")
    end = race_source.index("if (canDrawRaceTitles)", start)
    race_harness = """#include <assert.h>
#define RENDER_FLAG_CHECKERED_FLAG 0x1000
static struct { int renderFlags; } state;
#define gGT (&state)
static int fullyOn, fullyOff, drawable;
static int RaceFlag_IsFullyOnScreen(void) { return fullyOn; }
#ifdef __ANDROID__
static int RaceFlag_IsFullyOffScreen(void) { return fullyOff; }
static int RaceFlag_GetCanDraw(void) { return drawable; }
#endif
static int titles(void) {
PRODUCTION
return canDrawRaceTitles;
}
int main(void) {
    for (fullyOn = 0; fullyOn <= 1; fullyOn++)
    for (fullyOff = 0; fullyOff <= 1; fullyOff++)
    for (drawable = 0; drawable <= 1; drawable++)
    for (int enabled = 0; enabled <= 1; enabled++) {
        state.renderFlags = enabled ? RENDER_FLAG_CHECKERED_FLAG : 0;
#ifdef __ANDROID__
        assert(titles() == (!fullyOn && !(enabled && drawable && !fullyOff)));
#else
        assert(titles() == !fullyOn);
#endif
    }
}
""".replace("PRODUCTION", race_source[start:end])
    with tempfile.TemporaryDirectory(prefix="ctr-caption-") as directory:
        temp = Path(directory)
        test = temp / "caption.c"
        test.write_text(harness, encoding="utf-8")
        for platform in ([], ["-D__ANDROID__"]):
            binary = temp / "caption.exe"
            subprocess.run([args.cc, "-std=c11", "-Wall", "-Wextra",
                            "-Werror", *platform, str(test), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
            test.write_text(race_harness, encoding="utf-8")
            subprocess.run([args.cc, "-std=c11", "-Wall", "-Wextra",
                            "-Werror", *platform, str(test), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
            test.write_text(harness, encoding="utf-8")
    print("Native caption placement and race-title wipe gates: Android/desktop passed")


if __name__ == "__main__":
    main()
