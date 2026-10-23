"""Exercise production Adventure transitions and RectMenu input with save I/O stubs."""
import argparse
from pathlib import Path
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
    menus = (root / "game/230/MM_MenuFlow.c").read_text(encoding="utf-8")
    rect = (root / "game/RECTMENU.c").read_text(encoding="utf-8")
    flags = (root / "include/namespace_RectMenu.h").read_text(encoding="utf-8")
    buttons = (root / "include/namespace_Gamepad.h").read_text(encoding="utf-8")
    # Exercise Android root, management, confirmation and deletion without touching saves.
    start = menus.index("void MM_MenuProc_NewLoad(struct RectMenu *menu)")
    root_branch = menus.index("\n\t\tif (s_androidAdventureMenuStage == MM_ANDROID_ADV_MENU_ROOT)", start)
    end = menus.index("\n\t#endif", root_branch)
    campaign = menus[start:end] + "\n#endif\n}\n"
    start = rect.index("int RECTMENU_ProcessInput(struct RectMenu *m)")
    end = rect.index("\n// NOTE", start)
    process = rect[start:end]
    button_enums = buttons[buttons.index("enum RawInput"):buttons.index("struct GamepadButtonMap")]
    menu_enums = flags[flags.index("enum MenuFlags"):flags.index("struct RectMenu\n")]
    harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define SDL_snprintf snprintf
typedef int16_t s16;
#define CTR_STATIC_ASSERT(x) _Static_assert(x, #x)
/* ENUMS */
struct RectMenu {
    int state, funcState, rowSelected;
    struct MenuRow *rows;
    struct RectMenu *ptrNextBox_InHierarchy, *ptrPrevBox_InHierarchy;
    void (*funcPtr)(struct RectMenu *);
};
enum { MM_EXIT_ROUTE_ADV_NEW = 1, MM_EXIT_ROUTE_ADV_LOAD, TITLE_MENU_STATE_EXITING };
static int exists, loadResult, deleteResult;
static struct { struct RectMenu menuAdventure; struct MenuRow rowsAdventure[3];
                int desiredMenuIndex, titleMenuState; } D230;
struct AdvProgress { char name[16]; int characterID; };
static struct {
    char prevNameEntered[16];
    struct { int completionPercent, numTrophies, numKeys, numRelics;
             struct { int total; } numCtrTokens; } currAdvProfile;
} tracker;
static struct { struct RectMenu menuSubmitName; struct { int iconID; } MetaDataCharacters[16]; } data;
#define SDL_clamp(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
static void GAMEPROG_AdvPercent(struct AdvProgress *p) { (void)p; }
enum NativeAdventureCampaignResult { NATIVE_ADVENTURE_CAMPAIGN_OK, NATIVE_ADVENTURE_CAMPAIGN_INVALID };
static int NativeAdventureCampaign_Exists(void) { return exists; }
static enum NativeAdventureCampaignResult NativeAdventureCampaign_Load(struct AdvProgress *p) { memset(p, 0, sizeof(*p)); return loadResult; }
static int deleteCount;
static enum NativeAdventureCampaignResult NativeAdventureCampaign_Delete(void) { deleteCount++; if (!deleteResult) exists = 0; return deleteResult; }
static struct { struct RectMenu *activeSubMenu; int AnyPlayerTap, buttonTapPerPlayer[4], buttonHeldPerPlayer[4], advRng;
    struct AdvProgress advProgress; typeof(tracker) *gGT; struct RectMenu *ptrDesiredMenu; } state;
#define sdata (&state)
static void RngDeadCoed(int *rng) { (void)rng; }
static void OtherFX_Play(int sound, int mode) { (void)sound; (void)mode; }
static void RECTMENU_ClearInput(void) { state.AnyPlayerTap = state.buttonTapPerPlayer[0] = 0; }
/* PRODUCTION */
static void choose(struct RectMenu *root, int row, int button) {
    D230.menuAdventure.rowSelected = row;
    state.buttonTapPerPlayer[0] = button;
    RECTMENU_ProcessInput(root);
}
int main(void) {
    state.gGT = &tracker;
    struct MenuRow rootRows[] = {{1, 0, 0, 0, 0}, {-1}};
    struct RectMenu rootMenu = {.rows=rootRows};
    struct RectMenu *menu = &D230.menuAdventure;
    D230.rowsAdventure[0] = (struct MenuRow){0x8d, 0, 1, 0, 0};
    D230.rowsAdventure[1] = (struct MenuRow){0x8e, 0, 1, 1, 1};
    D230.rowsAdventure[2].stringIndex = -1;
    menu->rows = D230.rowsAdventure;
    menu->funcPtr = MM_MenuProc_NewLoad;
    rootMenu.ptrNextBox_InHierarchy = menu;
    char snapshot[192];
    for (int repeat = 0; repeat < 20; repeat++) {
        exists = 1;
        menu->state = 0;
        rootMenu.state = ONLY_DRAW_TITLE | DRAW_NEXT_MENU_IN_HIERARCHY;
        state.activeSubMenu = menu;
        MM_AdvCampaign_SetAndroidStage(MM_ANDROID_ADV_MENU_ROOT);
        assert(MM_AdventureCampaign_CopyTouchMenuSnapshot(snapshot, sizeof(snapshot)) == 2);
        // Continue failures keep the menu usable; valid saves retain their route.
        loadResult = NATIVE_ADVENTURE_CAMPAIGN_INVALID;
        choose(&rootMenu, 0, BTN_CROSS_one);
        assert(s_androidAdventureMenuStage == MM_ANDROID_ADV_MENU_ERROR);
        choose(&rootMenu, 0, BTN_TRIANGLE);
        assert(s_androidAdventureMenuStage == MM_ANDROID_ADV_MENU_ROOT);
        assert(menu->rowSelected == 0);
        loadResult = 0;
        choose(&rootMenu, 0, BTN_CROSS_one);
        assert(D230.desiredMenuIndex == MM_EXIT_ROUTE_ADV_LOAD);
        menu->state = 0;
        choose(&rootMenu, 0, BTN_TRIANGLE);
        assert((rootMenu.state & (ONLY_DRAW_TITLE | DRAW_NEXT_MENU_IN_HIERARCHY)) == 0);
        rootMenu.state = ONLY_DRAW_TITLE | DRAW_NEXT_MENU_IN_HIERARCHY;
        MM_AdvCampaign_SetAndroidStage(MM_ANDROID_ADV_MENU_ROOT);
        // Open management, return to the list, and enter again.
        choose(&rootMenu, 1, BTN_CROSS_one);
        assert(s_androidAdventureMenuStage == MM_ANDROID_ADV_MENU_MANAGE);
        assert(menu->rowSelected == 0);
        choose(&rootMenu, 1, BTN_CROSS_one);
        assert(s_androidAdventureMenuStage == MM_ANDROID_ADV_MENU_ROOT);
        assert(menu->rowSelected == 0);
        choose(&rootMenu, 1, BTN_CROSS_one);
        assert(MM_AdventureCampaign_CopyTouchMenuSnapshot(snapshot, sizeof(snapshot)) == 2);
        assert(strcmp(snapshot, "EXCLUIR CAMPANHA\nVOLTAR") == 0);
        assert(menu->rows[2].stringIndex == RECTMENU_STRING_NONE);
        // Cancel using both No and gamepad Back, then reopen confirmation.
        for (int cancel = 0; cancel < 2; cancel++) {
            choose(&rootMenu, 0, BTN_CROSS_one);
            assert(s_androidAdventureMenuStage == MM_ANDROID_ADV_MENU_DELETE_CONFIRM);
            assert(menu->rowSelected == 1);
            assert(MM_AdventureCampaign_CopyTouchMenuSnapshot(snapshot, sizeof(snapshot)) == 2);
            assert(strcmp(snapshot, "@TITLE:EXCLUIR CAMPANHA?\nSIM\nNÃO") == 0);
            choose(&rootMenu, 1, cancel ? BTN_TRIANGLE : BTN_CROSS_one);
            assert(s_androidAdventureMenuStage == MM_ANDROID_ADV_MENU_MANAGE);
            assert(menu->rowSelected == 0 && exists);
        }
        // Failed deletion keeps the save and returns through management.
        choose(&rootMenu, 0, BTN_CROSS_one);
        deleteResult = NATIVE_ADVENTURE_CAMPAIGN_INVALID;
        choose(&rootMenu, 0, BTN_CROSS_one);
        assert(s_androidAdventureMenuStage == MM_ANDROID_ADV_MENU_ERROR && exists);
        choose(&rootMenu, 1, BTN_CROSS_one);
        assert(s_androidAdventureMenuStage == MM_ANDROID_ADV_MENU_MANAGE);
        assert(menu->rowSelected == 0);
        deleteResult = 0;
        choose(&rootMenu, 0, BTN_CROSS_one);
        choose(&rootMenu, 0, BTN_CROSS_one);
        assert(!exists && s_androidAdventureMenuStage == MM_ANDROID_ADV_MENU_ROOT);
        assert(menu->rowSelected == 0 && menu->rows == s_androidAdventureNewRows);
        assert(menu->rows[1].stringIndex == RECTMENU_STRING_NONE);
        assert(MM_AdventureCampaign_CopyTouchMenuSnapshot(snapshot, sizeof(snapshot)) == 1);
        assert(strcmp(snapshot, "NOVA CAMPANHA") == 0);
        choose(&rootMenu, 0, BTN_TRIANGLE);
        assert((rootMenu.state & (ONLY_DRAW_TITLE | DRAW_NEXT_MENU_IN_HIERARCHY)) == 0);
        rootMenu.state = ONLY_DRAW_TITLE | DRAW_NEXT_MENU_IN_HIERARCHY;
        choose(&rootMenu, 0, BTN_CROSS_one);
        assert(D230.desiredMenuIndex == MM_EXIT_ROUTE_ADV_NEW);
        assert(D230.titleMenuState == TITLE_MENU_STATE_EXITING);
    }
    assert(deleteCount == 40);
    // Lists that stay unchanged keep their directional selection behavior.
    struct RectMenu plain = {.rows=D230.rowsAdventure};
    state.activeSubMenu = &plain;
    state.buttonTapPerPlayer[0] = BTN_DOWN;
    RECTMENU_ProcessInput(&plain);
    assert(plain.rowSelected == 1);
    state.buttonTapPerPlayer[0] = BTN_UP;
    RECTMENU_ProcessInput(&plain);
    assert(plain.rowSelected == 0);
}
''' 
    code = harness.replace("/* ENUMS */", button_enums + menu_enums)
    stage_start = menus.index("enum\n{")
    stage_end = menus.index("int MM_AdventureCampaign_CopyTouchCardSnapshot(")
    snapshot_start = menus.index("int MM_AdventureCampaign_CopyTouchMenuSnapshot(")
    snapshot_end = menus.index("\n#endif", snapshot_start)
    code = code.replace("/* PRODUCTION */", menus[stage_start:stage_end] + menus[snapshot_start:snapshot_end] + campaign + process)
    with tempfile.TemporaryDirectory(prefix="ctr-campaign-") as directory:
        unit = Path(directory) / "campaign.c"
        unit.write_text(code, encoding="utf-8")
        binary = Path(directory) / "campaign.exe"
        subprocess.run([args.cc, "-std=gnu11", "-D__ANDROID__", str(unit), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("Campaign: 20 cycles of management without rename, cancel, delete failure/success, Back and New: PASS")


if __name__ == "__main__":
    main()
