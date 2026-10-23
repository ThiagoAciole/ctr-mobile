"""Check physical/touch input arbitration and saved gamepad bindings on the host."""
from pathlib import Path
import os
import subprocess
import tempfile

from run_android_menu_pulse_test import block


def main():
    root = Path(__file__).resolve().parents[1]
    source = (root / 'platform/native_input.c').read_text(encoding='utf-8')
    mapping_struct = block(source, 'struct NativeInputControllerMapping\n') + ';'
    production = mapping_struct + '\n' + block(source, 'static void NativeInput_ApplyAndroidBindings(')
    production += '\n' + block(source, 'void Platform_InputUpdate(')
    harness = r'''
#include <assert.h>
#include <string.h>
#include <stdint.h>
typedef int s32;
typedef uint16_t u16;
static int s_androidBindings[16], s_androidMappingLock;
void SDL_LockSpinlock(int *lock) {}
void SDL_UnlockSpinlock(int *lock) {}
#define NATIVE_INPUT_MAX_CONTROLLERS 2
#define NATIVE_INPUT_PAD_ANALOG 0x73
#define NATIVE_INPUT_PAD_DIGITAL 0x41
struct PlatformInputPadSnapshot { int connected, status, id; unsigned char analog[4]; u16 buttons; };
struct { void *controller; struct PlatformInputPadSnapshot snapshot; } s_controllers[2];
int s_inputInitialized=1, s_installedSnapshotsActive, g_padCommEnable=1, s_touchActive=1;
u16 s_touchButtons=0xffff;
int s_touchAnalogLX=128, s_touchAnalogLY=128;
void NativeInput_WriteInstalledSnapshots(void) {}
void SDL_PumpEvents(void) {}
int NativeInput_KeyboardSuppressed(void) { return 0; }
u16 NativeInput_ReadKeyboard(void) { return 0xffff; }
void NativeInput_ResetSnapshot(int slot) { memset(&s_controllers[slot].snapshot, 0, sizeof(struct PlatformInputPadSnapshot)); }
void NativeInput_ApplyController(int slot) {
    struct PlatformInputPadSnapshot *s=&s_controllers[slot].snapshot;
    s->buttons=0xffff; s->analog[2]=128; s->analog[3]=128;
    if (s_controllers[slot].controller) { s->connected=1; s->id=0x73; s->analog[2]=210; }
}
void NativeInput_ApplyKeyboard(int slot, u16 buttons) {}
u16 NativeInput_GetSnapshotButtons(struct PlatformInputPadSnapshot *s) { return s->buttons; }
void NativeInput_SetSnapshotButtons(struct PlatformInputPadSnapshot *s, u16 b) { s->buttons=b; }
void NativeInput_WritePadBus(void) {}
/* PRODUCTION */
int main(void) {
    struct NativeInputControllerMapping mapping={0};
    mapping.gc_axis_left_x=0x4000; mapping.gc_circle=1;
    s_androidBindings[0]=10; s_androidBindings[12]=13;
    NativeInput_ApplyAndroidBindings(&mapping);
    assert(mapping.gc_cross==9 && mapping.gc_dpad_up==12);
    assert(mapping.gc_circle==1 && mapping.gc_axis_left_x==0x4000);
    s_controllers[0].controller=(void*)1;
    Platform_InputUpdate();
    assert(s_controllers[0].snapshot.analog[2]==210);
    assert(s_controllers[0].snapshot.id==0x73);
    s_touchAnalogLY=240; s_touchButtons=0xbfff;
    Platform_InputUpdate();
    assert(s_controllers[0].snapshot.analog[2]==210);
    assert(s_controllers[0].snapshot.analog[3]==240);
    assert(s_controllers[0].snapshot.buttons==0xbfff);
    s_controllers[0].controller=0; s_touchAnalogLX=128; s_touchAnalogLY=128;
    Platform_InputUpdate();
    assert(s_controllers[0].snapshot.connected && s_controllers[0].snapshot.id==0x41);
}
'''
    with tempfile.TemporaryDirectory(prefix='ctr-gamepad-') as directory:
        temp = Path(directory)
        cfile = temp / 'gamepad.c'
        cfile.write_text(harness.replace('/* PRODUCTION */', production), encoding='utf-8')
        compiler = Path('C:/msys64/ucrt64/bin/gcc.exe')
        env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ['PATH'])
        subprocess.run([str(compiler), str(cfile), '-o', str(temp / 'gamepad.exe')], check=True, env=env)
        subprocess.run([str(temp / 'gamepad.exe')], check=True)
        java = root / 'android/app/src/main/java/com/ctrnative'
        android_jar = Path(os.environ['LOCALAPPDATA']) / 'Android/Sdk/platforms/android-34/android.jar'
        subprocess.run(['javac', '-cp', str(android_jar), '-d', str(temp),
                        str(java / 'CTRGamepadMapping.java'),
                        str(root / 'tests/CTRGamepadMappingTest.java')], check=True)
        subprocess.run(['java', '-ea', '-cp', str(temp), 'com.ctrnative.CTRGamepadMappingTest'], check=True)
        overlay = (java / 'CTRTouchOverlay.java').read_text(encoding='utf-8')
        menu = r'''
public class RootGamepadTest {
    static final int SCREEN_ROOT_MENU=5;
    int screenState=5, rootMenuSelection, confirms, selection;
    boolean snapshotInputReady=true;
    int getWidth() { return 1000; }
    int getHeight() { return 500; }
    void invalidate() {}
    void sendInput() {}
    void claimRootMenuChoice(int pointer, float x, float y) {
        confirms++; selection=rootMenuSelection;
        assert x==800 && y>0 && y<500;
    }
    /* MENU */
    public static void main(String[] args) {
        RootGamepadTest view=new RootGamepadTest();
        view.handleRootGamepadAction(12); assert view.rootMenuSelection==3;
        view.handleRootGamepadAction(13); assert view.rootMenuSelection==0;
        view.handleRootGamepadAction(13); assert view.rootMenuSelection==1 && view.confirms==0;
        view.handleRootGamepadAction(0); assert view.confirms==1 && view.selection==1;
        view.snapshotInputReady=false;
        view.handleRootGamepadAction(0); assert view.confirms==1;
        view.snapshotInputReady=true; view.screenState=1;
        view.handleRootGamepadAction(13); assert view.rootMenuSelection==1;
        System.out.println("Root gamepad navigation and explicit confirmation: OK");
    }
}
'''
        root_test = temp / 'RootGamepadTest.java'
        root_test.write_text(menu.replace('/* MENU */', block(overlay, 'public void handleRootGamepadAction(')), encoding='utf-8')
        subprocess.run(['javac', '-d', str(temp), str(root_test)], check=True)
        subprocess.run(['java', '-ea', '-cp', str(temp), 'RootGamepadTest'], check=True)
        activity = (java / 'CTRNativeActivity.java').read_text(encoding='utf-8')
        start = activity.index('final boolean touchOverlayRequired =')
        end = activity.index('if (wanted) {', start)
        visibility = r'''
class CTRTouchOverlay {
    static final int SCREEN_DEMO=4, SCREEN_CUTSCENE=12, SCREEN_MASK_HINT=17,
        SCREEN_PAUSE_RESULTS=2, SCREEN_RACE_RESULTS_MENU=20, SCREEN_LOADING=22,
        SCREEN_RACE=1, SCREEN_ADVENTURE_ARENA=18;
}
public class GamepadVisibilityTest {
    static boolean[] check(int screenState, int mode, int pads) {
        /* VISIBILITY */
        return new boolean[]{wanted, showControls};
    }
    public static void main(String[] args) {
        for (int screen : new int[]{1,18}) for (int mode=0; mode<3; mode++) {
            assert !check(screen,mode,1)[1];
            assert check(screen,mode,0)[1] == (mode!=2);
        }
        assert check(5,2,1)[0];
        assert check(6,2,1)[0];
        assert !check(22,0,0)[1];
        assert !check(17,1,0)[1];
        System.out.println("Race/adventure gamepad hiding and disconnect fallback: OK");
    }
}
'''
        visibility_test = temp / 'GamepadVisibilityTest.java'
        visibility_test.write_text(visibility.replace('/* VISIBILITY */', activity[start:end]), encoding='utf-8')
        subprocess.run(['javac', '-d', str(temp), str(visibility_test)], check=True)
        subprocess.run(['java', '-ea', '-cp', str(temp), 'GamepadVisibilityTest'], check=True)
    print('Physical gamepad/touch arbitration: OK')


if __name__ == '__main__':
    main()
