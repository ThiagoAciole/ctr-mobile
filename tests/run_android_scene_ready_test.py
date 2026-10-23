"""Check the production scene-ready gate without renderer or Android dependencies."""
import argparse
from pathlib import Path
import re
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
    source = (root / "game/ctrds/ctrds.c").read_text(encoding="utf-8")
    header = (root / "include/namespace_Main.h").read_text(encoding="utf-8")
    start = source.index("int Ctrds_TouchScreenState(void)")
    end = source.index("\n\tactiveMenu = sdata->ptrActiveMenu;", start)
    gate = source[start:end] + "\n (void)activeMenu; return -1;\n}\n"
    constants = []
    for name in ("LOAD_IDLE", "MAIN_MENU", "RENDER_FLAG_CHECKERED_FLAG"):
        value = re.search(r"\b" + name + r"\s*=\s*(-?(?:0x[0-9a-fA-F]+|\d+))", header)
        assert value, f"Missing production constant {name}"
        constants.append(f"#define {name} {value.group(1)}")
    harness = r'''
#include <assert.h>
#include <stddef.h>
/* CONSTANTS */
struct RectMenu;
struct GameTracker { unsigned gameMode1, renderFlags; };
static struct GameTracker tracker;
static struct State { struct GameTracker *gGT; struct { int stage; } Loading; } state;
static struct State *sdata = &state;
static int canDraw, fullyOffScreen;
static int RaceFlag_GetCanDraw(void) { return canDraw; }
static int RaceFlag_IsFullyOffScreen(void) { return fullyOffScreen; }
/* GATE */
int main(void) {
    assert(Ctrds_TouchScreenState() == 3);
    sdata = NULL;
    assert(Ctrds_TouchScreenState() == 3);
    sdata = &state;
    state.gGT = &tracker;
    state.Loading.stage = LOAD_IDLE;
    assert(Ctrds_TouchScreenState() == -1);
    tracker.renderFlags = RENDER_FLAG_CHECKERED_FLAG;
    canDraw = 1;
    assert(Ctrds_TouchScreenState() == 22); // Covered gameplay must wait.
    fullyOffScreen = 1;
    assert(Ctrds_TouchScreenState() == -1); // Revealed scene may show its HUD.
    fullyOffScreen = 0;
    canDraw = 0;
    assert(Ctrds_TouchScreenState() == -1);
    canDraw = 1;
    tracker.renderFlags = 0;
    assert(Ctrds_TouchScreenState() == -1);
    tracker.renderFlags = RENDER_FLAG_CHECKERED_FLAG;
    tracker.gameMode1 = MAIN_MENU;
    assert(Ctrds_TouchScreenState() == -1); // Main menu retains its checker backdrop.
    for (int stage = 0; stage < 8; stage++) {
        state.Loading.stage = stage;
        for (int mainMenu = 0; mainMenu < 2; mainMenu++) {
            tracker.gameMode1 = mainMenu ? MAIN_MENU : 0;
            for (fullyOffScreen = 0; fullyOffScreen < 2; fullyOffScreen++) {
                canDraw = 0;
                assert(Ctrds_TouchScreenState() == 22); // Entering or leaving any loading stage.
                canDraw = 1;
                assert(Ctrds_TouchScreenState() == 22);
            }
        }
    }
}
'''
    code = harness.replace("/* CONSTANTS */", "\n".join(constants)).replace("/* GATE */", gate)
    with tempfile.TemporaryDirectory(prefix="ctr-scene-") as directory:
        unit = Path(directory) / "scene.c"
        unit.write_text(code, encoding="utf-8")
        binary = Path(directory) / "scene.exe"
        subprocess.run([args.cc, "-std=c11", "-D__ANDROID__", str(unit), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("Scene readiness: loading, gameplay wipe, main menu and null state: PASS")


if __name__ == "__main__":
    main()
