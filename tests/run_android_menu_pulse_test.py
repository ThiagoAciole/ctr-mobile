"""Exercise production menu pulses independently of Android's event loop."""
from pathlib import Path
import subprocess
import tempfile


def block(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    root = Path(__file__).resolve().parents[1]
    source = (root / "android/app/src/main/java/com/ctrnative/CTRTouchOverlay.java").read_text(encoding="utf-8")
    signatures = [
        "private void queueTouchTapPulse(", "private void release(",
        "private void sendInput(", "private void claimRootMenuChoice(",
        "private void claimGameMenuChoice(", "public void releaseAllTouches(",
        "private void clearPointers(", "private boolean claim(",
    ]
    production = "\n".join(block(source, signature) for signature in signatures)
    production += "\n" + block(source, "private int rootMenuChoiceAt(").replace("rootMenuChoiceAt(", "actualRootMenuChoiceAt(")
    production += "\n" + block(source, "private final Runnable finishTouchTapPulse =") + ";"
    production += "\n" + block(source, "private final Runnable finishDemoTapPulse =") + ";"
    menu_touch = source[source.index("int pointerId = event.getPointerId(index);"):]
    back = block(menu_touch, "else if (isBackButton(x, y))")
    production += "\nvoid menuBack(int pointerId) " + back[back.index("{"):]
    harness = r'''
import java.util.ArrayList;
class SystemClock {
    static long now;
    static long uptimeMillis() { return now; }
}
class CTRNativeActivity {
    static int mask, stickY, choice, calls, exits;
    static long generation;
    static void nativeTouchInput(int value, int x, int y, long gen) {
        mask = value; stickY = y; generation = gen; calls++;
    }
    static void nativeTouchRootMenuChoice(int value, long gen) { choice = value; }
    static void nativeTouchMenuChoice(int value, long gen) { choice = value; }
    void showRootMenuOptions() {}
    void finishAndRemoveTask() { exits++; }
}
public class MenuPulseTest {
    static final int BTN_CROSS=1, BTN_TRIANGLE=2, BTN_SQUARE=4, BTN_START=8, BTN_CIRCLE=16, ALL_RELEASED=0xffff;
    static final int SCREEN_GHOST_SELECT=16, SCREEN_PAUSE_MENU=11, SCREEN_RACE=1, SCREEN_ADVENTURE_ARENA=18;
    static class Control {
        int button, pointerId=-1;
        Control(int value) { button=value; }
        boolean contains(float x, float y) { return x==button; }
    }
    ArrayList<Control> controls=new ArrayList<>();
    ArrayList<Integer> advancePointers=new ArrayList<>();
    int touchTapPulseButton, lastMask=ALL_RELEASED, backPointer=-1;
    int rootMenuSelection, gameMenuSelection, screenState, adventureHintPointer=-1;
    int[] gameMenuNativeChoices={3};
    String[] gameMenuRows={"CONTINUAR"};
    long touchTapPulseUntil, demoTapPulseUntil, snapshotGeneration=7;
    boolean sending, adventurePauseProgressOpen, adventureHintSwipe, controlsVisible=true;
    Runnable scheduled;
    long scheduledAt;
    void removeCallbacks(Runnable callback) { if (scheduled==callback) scheduled=null; }
    void postDelayed(Runnable callback, long delay) {
        scheduled=callback; scheduledAt=SystemClock.now+delay;
    }
    void invalidate() {}
    Object getContext() { return new CTRNativeActivity(); }
    int getWidth() { return 1000; }
    int getHeight() { return 500; }
    int rootMenuChoiceAt(float x, float y) { return (int)x; }
    void tick(long time) {
        SystemClock.now=time;
        if (scheduled!=null && scheduledAt<=time) {
            Runnable callback=scheduled; scheduled=null; callback.run();
        }
        sendInput();
    }
    /* PRODUCTION */
    static void pressed(int button) { assert (CTRNativeActivity.mask & button)==0; }
    static void released() { assert CTRNativeActivity.mask==ALL_RELEASED; }
    public static void main(String[] args) {
        MenuPulseTest view=new MenuPulseTest();
        assert view.actualRootMenuChoiceAt(800, 309)==3;
        assert view.actualRootMenuChoiceAt(800, 380)==-1;
        CTRNativeActivity.choice=-1;
        view.claimRootMenuChoice(11, 3, 0);
        assert CTRNativeActivity.exits==1 && CTRNativeActivity.choice==-1;
        released();
        assert view.scheduled==null && view.advancePointers.isEmpty();
        // Complete DOWN and UP before the first simulated native tick.
        view.claimRootMenuChoice(1, 0, 0);
        view.sendInput(); view.release(1); view.sendInput();
        view.tick(16); pressed(BTN_CROSS);
        assert CTRNativeActivity.generation==7;
        view.tick(139); pressed(BTN_CROSS);
        view.tick(140); released();
        assert view.scheduled==null;

        view.claimGameMenuChoice(2, 0);
        view.sendInput(); view.release(2); view.sendInput();
        view.tick(156); pressed(BTN_CROSS);
        assert CTRNativeActivity.choice==3;
        view.tick(280); released();

        // Back uses the same pulse after its pointer is already released.
        view.backPointer=3; view.queueTouchTapPulse(BTN_TRIANGLE);
        view.sendInput(); view.release(3); view.sendInput();
        view.tick(296); pressed(BTN_TRIANGLE);
        view.tick(420); released();

        // A replacement pulse cancels the previous expiration callback.
        view.queueTouchTapPulse(BTN_CROSS);
        view.tick(430);
        view.queueTouchTapPulse(BTN_TRIANGLE);
        view.tick(560); pressed(BTN_TRIANGLE);
        assert (CTRNativeActivity.mask & BTN_CROSS)!=0;
        view.tick(570); released();

        // Held race controls retain their normal immediate release semantics.
        Control race=new Control(BTN_SQUARE); view.controls.add(race);
        race.pointerId=4; view.sendInput(); pressed(BTN_SQUARE);
        assert CTRNativeActivity.stickY==32767;
        view.release(4); view.sendInput(); released();
        assert CTRNativeActivity.stickY==0;

        // A screen transition cancels queued pulses and every held pointer.
        race.pointerId=5; view.queueTouchTapPulse(BTN_CROSS); view.sendInput();
        view.releaseAllTouches(); released();
        assert view.scheduled==null && race.pointerId==-1;
        view.tick(1000); released();

        int calls=CTRNativeActivity.calls;
        view.claimRootMenuChoice(6, -1, 0);
        view.claimGameMenuChoice(6, -1);
        view.sendInput();
        assert view.scheduled==null && CTRNativeActivity.calls==calls;

        // Pause's top Back chooses Retomar (row 0), rather than Triangle cancel.
        view.screenState=SCREEN_PAUSE_MENU;
        view.gameMenuNativeChoices[0]=0;
        view.menuBack(7); view.sendInput(); view.release(7); view.sendInput();
        view.tick(1016); pressed(BTN_CROSS);
        assert CTRNativeActivity.choice==0 && (CTRNativeActivity.mask & BTN_TRIANGLE)!=0;
        view.tick(1140); released();

        Control pause=new Control(BTN_START);
        Control accel=new Control(BTN_CROSS), weapon=new Control(BTN_CIRCLE);
        view.controls.add(pause); view.controls.add(accel); view.controls.add(weapon);
        for (int state : new int[]{SCREEN_RACE, SCREEN_ADVENTURE_ARENA}) {
            view.screenState=state;
            for (int button : new int[]{BTN_START, BTN_CIRCLE}) {
                long down=SystemClock.now;
                assert view.claim(8, button, 0);
                view.sendInput(); view.release(8); view.sendInput();
                view.tick(down+16); pressed(button);
                view.tick(down+140); released();
            }
            assert view.claim(9, BTN_CROSS, 0);
            view.sendInput(); pressed(BTN_CROSS);
            view.tick(SystemClock.now+500); pressed(BTN_CROSS);
            assert view.scheduled==null; // Accelerate remains a held button.
            // A quick weapon tap must survive the native tick while accelerating.
            long fire=SystemClock.now;
            assert view.claim(10, BTN_CIRCLE, 0);
            view.sendInput(); view.release(10); view.sendInput();
            view.tick(fire+16); pressed(BTN_CIRCLE); pressed(BTN_CROSS);
            view.tick(fire+140); pressed(BTN_CROSS);
            assert (CTRNativeActivity.mask & BTN_CIRCLE)!=0;
            view.release(9); view.sendInput(); released();
        }
        System.out.println("Menu pulse: menus/back/pause/weapon, weapon while accelerating and transition reset PASS");
    }
}
'''
    with tempfile.TemporaryDirectory(prefix="ctr-menu-pulse-") as directory:
        temp = Path(directory)
        unit = temp / "MenuPulseTest.java"
        unit.write_text(harness.replace("/* PRODUCTION */", production), encoding="utf-8")
        subprocess.run(["javac", "-d", str(temp), str(unit)], check=True)
        subprocess.run(["java", "-ea", "-cp", str(temp), "MenuPulseTest"], check=True)


if __name__ == "__main__":
    main()
