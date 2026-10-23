"""Check the production pause callback's first-frame publication and input cooldown."""
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
        parser.error("GCC required; pass --cc /path/to/gcc")
    root = Path(__file__).resolve().parents[1]
    source = (root / "game/MAIN/MainFreeze.c").read_text(encoding="utf-8")
    start = source.index("void MainFreeze_MenuPtrDefault(struct RectMenu *menu)")
    end = source.index("\n\tif (menu->rowSelected < 0)", start)
    callback = source[start:end] + "\n (void)levID; (void)stringID; inputCalls++;\n}\n"
    harness = r'''
#include <assert.h>
#include <stdint.h>
typedef uint16_t u16;
typedef uint32_t u32;
enum { RECTMENU_FUNC_STATE_INPUT, RECTMENU_FUNC_STATE_UPDATE };
enum { ADVENTURE_ARENA = 1, NEEDS_TO_CLOSE = 0x1000, RECTMENU_DRAW_STYLE_3P4P_LAYOUT = 8 };
struct RectMenu { int funcState, drawStyle, state; };
struct GameTracker { int gameMode1, cooldownfromPauseUntilUnpause, numPlyrCurrGame; };
static struct GameTracker tracker;
static struct { struct GameTracker *gGT; } state = { &tracker };
#define sdata (&state)
static int mode, loaded = 1, snapshotCalls, inputCalls;
#define Ctrds_TouchControlsMode() mode
static int LOAD_IsOpen_AdvHub(void) { return loaded; }
static void AH_Pause_Update(void) { snapshotCalls++; }
/* CALLBACK */
int main(void) {
    struct RectMenu menu = {RECTMENU_FUNC_STATE_UPDATE, 0, 0};
    tracker.gameMode1 = ADVENTURE_ARENA;
    tracker.cooldownfromPauseUntilUnpause = 5;
    MainFreeze_MenuPtrDefault(&menu);
#ifdef __ANDROID__
    assert(snapshotCalls == 1); // First paused frame has progress data.
#else
    assert(snapshotCalls == 0); // Native desktop timing is unchanged.
#endif
    snapshotCalls = 0;
    menu.funcState = RECTMENU_FUNC_STATE_INPUT;
    MainFreeze_MenuPtrDefault(&menu);
    assert(inputCalls == 0); // Opening press cannot immediately unpause.
    menu.funcState = RECTMENU_FUNC_STATE_UPDATE;
    mode = 2;
    MainFreeze_MenuPtrDefault(&menu);
    assert(snapshotCalls == 0); // Native-only UI keeps its original timing.
    mode = 0;
    tracker.cooldownfromPauseUntilUnpause = 0;
    loaded = 0;
    MainFreeze_MenuPtrDefault(&menu);
    assert(snapshotCalls == 0);
    loaded = 1;
    menu.state = NEEDS_TO_CLOSE;
    MainFreeze_MenuPtrDefault(&menu);
    assert(snapshotCalls == 0);
    menu.state = 0;
    tracker.gameMode1 = 0;
    MainFreeze_MenuPtrDefault(&menu);
    assert(snapshotCalls == 0); // Race pauses have no adventure progress.
    tracker.gameMode1 = ADVENTURE_ARENA;
    MainFreeze_MenuPtrDefault(&menu);
    assert(snapshotCalls == 1);
    menu.funcState = RECTMENU_FUNC_STATE_INPUT;
    MainFreeze_MenuPtrDefault(&menu);
    assert(inputCalls == 1); // Input is accepted once cooldown expires.
}
'''
    with tempfile.TemporaryDirectory(prefix="ctr-pause-") as directory:
        temp = Path(directory)
        unit = temp / "pause.c"
        unit.write_text(harness.replace("/* CALLBACK */", callback), encoding="utf-8")
        for defines in ([], ["-D__ANDROID__"]):
            binary = temp / "pause.exe"
            subprocess.run([args.cc, "-std=c11", "-Wall", *defines, str(unit), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
    print("Pause first-frame snapshot and cooldown: PASS (Android + desktop)")


if __name__ == "__main__":
    main()
