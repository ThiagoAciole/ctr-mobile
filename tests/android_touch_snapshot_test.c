// The runner inserts the production snapshot declarations and functions below the stubs.
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define __ANDROID__ 1
#define internal static
#define DRAW_NEXT_MENU_IN_HIERARCHY 1
typedef int SDL_SpinLock;
static void SDL_LockSpinlock(SDL_SpinLock *lock) {
    while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE)) { }
}
static void SDL_UnlockSpinlock(SDL_SpinLock *lock) {
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
}
#define SDL_snprintf snprintf
#define SDL_strcmp strcmp
static size_t SDL_strlcpy(char *dst, const char *src, size_t size) {
    if (size) snprintf(dst, size, "%s", src);
    return strlen(src);
}
static unsigned long long SDL_GetTicksNS(void) { return 123456789; }
static void SDL_Log(const char *format, ...) { (void)format; }
struct MenuRow { int id; };
struct RectMenu { int state; struct RectMenu *ptrNextBox_InHierarchy; struct MenuRow *rows; };
static struct { struct RectMenu *ptrActiveMenu; unsigned int frameCounter; } state;
#define sdata (&state)
static int screen = 3;
#define IN_MENU 1
static struct {
    int characterSelectMenuState;
    struct { struct { int state; } transition; } trackSelect;
    struct { int state; } cupSelectTransition;
} D230;
static unsigned short pad = 0xffff;
static int axisX, axisY;
static int Ctrds_TouchScreenState(void) { return screen; }
static int Ctrds_CopyAndroidRaceResultsSnapshot(char *out, int size) {
    snprintf(out, size, "RESULTS"); return 1;
}
static void Platform_InputApplyTouchButtons(int slot, unsigned short buttons) { (void)slot; pad = buttons; }
static void Platform_InputApplyTouchAxes(int slot, int x, int y) { (void)slot; axisX = x; axisY = y; }
static void Platform_InputReleaseTouch(void) { pad = 0xffff; axisX = axisY = 0; }
void Ctrds_QueueGameTap(float x, float y);
void Ctrds_QueueRootMenuChoice(int choice);
void Ctrds_QueueTouchMenuChoice(int choice);

static int s_platformBeginScene, s_pinnedVramDisplayFrames, s_pinnedVramDisplayCustomRect;
static int s_pinnedVramDisplayX, s_pinnedVramDisplayY, s_pinnedVramDisplayW, s_pinnedVramDisplayH;
static int s_androidStartupImageDismissed, swaps, notifications;
static struct { struct { int x, y, w, h; } disp; } activeDispEnv;
#define NATIVE_PERF_BUCKET_PLATFORM_END_SCENE 0
#define CTRDS_VRAM_PANEL_X 0
#define CTRDS_VRAM_PANEL_Y 216
#define CTRDS_PANEL_W 512
#define CTRDS_PANEL_H 216
#define NativePerf_BeginScope(...) ((void)0)
#define NativePerf_EndScope(...) ((void)0)
#define NativeRenderer_EndScene(...) ((void)0)
#define NativeRenderer_PresentVRAMRect(...) ((void)0)
#define NativeRenderer_PresentVRAMDisplay(...) ((void)0)
#define NativeRenderer_EndGpuFrame(...) ((void)0)
#define NativeRenderer_SwapWindow(...) (++swaps)
#define NativeRenderer_StoreFrameBuffer(...) ((void)0)
#define NativeCompanion_Init(...) ((void)0)
#define NativeCompanion_IsReady(...) 0
#define NativeCompanion_Present(...) ((void)0)
#define NativeRenderer_PresentTwo(...) ((void)0)
#define Ctrds_SecondScreen(...) 0
#define Platform_Android_OnTouchScreenChanged(...) (++notifications)
#define Platform_Android_OnMainMenuReady(...) ((void)0)

/* PRODUCTION_CODE */

static int done;
static void check_ghost_header(void) {
    char snapshot[640];
    int selected = 2, ghostCount = 3;
    /* GHOST_HEADER */
    assert(prefix > 0);
    Ctrds_BeginAndroidTouchFrame();
    screen = 21;
    s_touchMenuSelected = selected; s_touchMenuVisible = 1;
    strcpy(s_touchMenuSnapshot, snapshot);
    Ctrds_PublishAndroidTouchFrame();
    struct CtrdsAndroidTouchSnapshot copy;
    Ctrds_CopyAndroidTouchFrame(&copy);
    assert(strcmp(copy.menu, "2\n@TITLE:SELECIONE UM FANTASMA\n") == 0);
}

static void *read_frames(void *unused) {
    (void)unused;
    while (!__atomic_load_n(&done, __ATOMIC_ACQUIRE)) {
        struct CtrdsAndroidTouchSnapshot copy;
        Ctrds_CopyAndroidTouchFrame(&copy);
        assert(copy.screen == 6);
        unsigned int payload;
        assert(sscanf(copy.menu, "0\n%u", &payload) == 1);
        assert(payload == copy.frame);
        assert(copy.selection[0] == '\0');
    }
    return NULL;
}

int main(void) {
    struct CtrdsAndroidTouchSnapshot copy;
    screen = 10;
    D230.cupSelectTransition.state = 0;
    Ctrds_PublishAndroidTouchFrame();
    Ctrds_CopyAndroidTouchFrame(&copy);
    assert(!copy.inputReady);
    Ctrds_ApplyAndroidTouchPad(copy.generation, 0xfffe, 0, 0);
    assert(pad == 0xffff);
    D230.cupSelectTransition.state = IN_MENU;
    assert(Ctrds_PublishAndroidTouchFrame());
    Ctrds_CopyAndroidTouchFrame(&copy);
    assert(copy.inputReady);
    unsigned int animationGeneration = copy.generation;
    strcpy(s_androidSelectionText, "R\t50\t60\tCOPA");
    assert(Ctrds_PublishAndroidTouchFrame());
    Ctrds_CopyAndroidTouchFrame(&copy);
    assert(copy.generation == animationGeneration);
    assert(strcmp(copy.selection, "R\t50\t60\tCOPA") == 0);
    assert(!Ctrds_PublishAndroidTouchFrame());
    screen = 22;
    assert(Ctrds_PublishAndroidTouchFrame());
    Ctrds_CopyAndroidTouchFrame(&copy);
    assert(!copy.inputReady);
    memset(&s_androidTouchFrame, 0, sizeof(s_androidTouchFrame));
    s_androidTouchFrame.screen = screen = 3;
    struct MenuRow rowsA = {1}, rowsB = {2};
    struct RectMenu menu = {0, NULL, &rowsA};
    state.ptrActiveMenu = &menu;
    Ctrds_CopyAndroidTouchFrame(&copy);
    assert(copy.screen == 3 && copy.generation == 0);
    Ctrds_BeginAndroidTouchFrame();
    screen = 6; s_touchMenuVisible = 1;
    strcpy(s_touchMenuSnapshot, "FIRST");
    assert(Ctrds_PublishAndroidTouchFrame());
    Ctrds_CopyAndroidTouchFrame(&copy);
    unsigned int first = copy.generation;
    assert(strcmp(copy.menu, "0\nFIRST") == 0);
    strcpy(s_touchMenuSnapshot, "INCOMPLETE");
    Ctrds_CopyAndroidTouchFrame(&copy);
    assert(strcmp(copy.menu, "0\nFIRST") == 0);
    Ctrds_QueueAndroidTouch(first, 2, 1, 0, 0);
    assert(s_touchMenuChoice == 1);
    Ctrds_ApplyAndroidTouchPad(first, 0xfffe, 0, 32767);
    assert(pad == 0xfffe && axisY == 32767);
    assert(!Ctrds_PublishAndroidTouchFrame());
    assert(pad == 0xfffe); // Dynamic frames must not interrupt held racing input.
    struct RectMenu otherMenu = {0, NULL, &rowsB};
    state.ptrActiveMenu = &otherMenu; // Menu replacement before the next Publish.
    int choice;
    assert(!Ctrds_TakeTouchMenuChoice(&choice) && s_touchMenuChoice == -1);
    Ctrds_QueueAndroidTouch(first, 0, 0, .5f, .5f);
    Ctrds_QueueAndroidTouch(first, 1, 1, 0, 0);
    float x, y;
    assert(!Ctrds_TakeGameTap(&x, &y));
    assert(!Ctrds_TakeRootMenuChoice(&choice));
    state.ptrActiveMenu = &menu;
    Ctrds_QueueAndroidTouch(first, 2, 1, 0, 0);
    assert(Ctrds_TakeTouchMenuChoice(&choice) && choice == 1);
    menu.rows = &rowsB;
    assert(Ctrds_PublishAndroidTouchFrame());
    assert(s_touchMenuChoice == -1 && pad == 0xffff && axisY == 0);
    Ctrds_QueueAndroidTouch(first, 0, 0, .5f, .5f);
    Ctrds_QueueAndroidTouch(first, 1, 1, 0, 0);
    Ctrds_QueueAndroidTouch(first, 2, 1, 0, 0);
    Ctrds_ApplyAndroidTouchPad(first, 0xfffe, 0, 32767);
    assert(!s_gameTapPending && s_rootMenuChoice == -1 && s_touchMenuChoice == -1 && pad == 0xffff);
    Ctrds_BeginAndroidTouchFrame();
    screen = 10; strcpy(s_androidSelectionText, "CUP");
    Ctrds_PublishAndroidTouchFrame();
    Ctrds_CopyAndroidTouchFrame(&copy);
    assert(copy.menu[0] == '\0' && strcmp(copy.selection, "CUP") == 0);
    Ctrds_BeginAndroidTouchFrame();
    screen = 15; strcpy(s_androidSelectionText, "GARAGE");
    Ctrds_PublishAndroidTouchFrame();
    Ctrds_CopyAndroidTouchFrame(&copy);
    assert(strcmp(copy.selection, "GARAGE") == 0 && copy.character[0] == '\0');
    Ctrds_BeginAndroidTouchFrame();
    Ctrds_PublishAndroidTouchFrame();
    Ctrds_CopyAndroidTouchFrame(&copy);
    assert(copy.selection[0] == '\0'); // A disappeared producer must not leave last-frame artwork.
    screen = 19; strcpy(s_androidAdventurePauseSnapshot, "L\tHINTS\t0");
    Ctrds_PublishAndroidTouchFrame();
    Ctrds_CopyAndroidTouchFrame(&copy);
    unsigned int hintList = copy.generation;
    strcpy(s_androidAdventurePauseSnapshot, "D\tHINT\t0\nDETAILS");
    assert(Ctrds_PublishAndroidTouchFrame());
    Ctrds_CopyAndroidTouchFrame(&copy);
    assert(copy.generation == hintList + 1);
    screen = 6; s_touchMenuVisible = 1; strcpy(s_touchMenuSnapshot, "0");
    Ctrds_PublishAndroidTouchFrame();
    pthread_t reader;
    assert(pthread_create(&reader, NULL, read_frames, NULL) == 0);
    for (unsigned int i = 1; i <= 20000; i++) {
        Ctrds_BeginAndroidTouchFrame();
        state.frameCounter = i; s_touchMenuVisible = 1;
        snprintf(s_touchMenuSnapshot, sizeof(s_touchMenuSnapshot), "%u", i);
        Ctrds_PublishAndroidTouchFrame();
    }
    __atomic_store_n(&done, 1, __ATOMIC_RELEASE);
    assert(pthread_join(reader, NULL) == 0);
    unsigned int oldGeneration;
    Ctrds_CopyAndroidTouchFrame(&copy); oldGeneration = copy.generation;
    screen = 12; state.frameCounter++;
    s_platformBeginScene = 1; s_pinnedVramDisplayFrames = 1;
    Platform_EndScene();
    Ctrds_CopyAndroidTouchFrame(&copy);
    assert(swaps == 1 && notifications == 1 && copy.screen == 12);
    assert(copy.menu[0] == '\0' && copy.generation > oldGeneration);
    assert(s_pinnedVramDisplayFrames == 0);
    s_platformBeginScene = 1; s_pinnedVramDisplayFrames = 1; s_pinnedVramDisplayCustomRect = 1;
    Platform_EndScene();
    assert(swaps == 2 && notifications == 1 && !s_pinnedVramDisplayCustomRect);
    screen = 6; state.frameCounter++;
    s_touchMenuVisible = 1; strcpy(s_touchMenuSnapshot, "RESTORED");
    s_platformBeginScene = 1;
    Platform_EndScene();
    Ctrds_CopyAndroidTouchFrame(&copy);
    assert(swaps == 3 && notifications == 2 && strcmp(copy.menu, "0\nRESTORED") == 0);
    Platform_EndScene();
    assert(swaps == 3 && notifications == 2);
    check_ghost_header();
    puts("native snapshot: publication, generation, stale input, held input and 20000 concurrent frames OK");
    puts("native presentation: raw VRAM, custom VRAM, normal scene and inactive scene OK");
}
