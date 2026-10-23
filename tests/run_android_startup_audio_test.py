"""Check production loading-MP3 lifecycle and native startup gain without devices."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
from run_android_menu_pulse_test import block


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=shutil.which("gcc"))
    args = parser.parse_args()
    if not args.cc:
        parser.error("GCC required")
    root = Path(__file__).resolve().parents[1]
    activity = (root / "android/app/src/main/java/com/ctrnative/CTRNativeActivity.java").read_text(encoding="utf-8")
    production = "\n".join(block(activity, name) for name in (
        "private void startStartupMusic(", "private void stopStartupMusic(",
        "private void finishStartupOverlay(", "protected void onPause("))
    java = r'''
class Log {
    static void e(String tag, String text) {}
    static void e(String tag, String text, Throwable error) {}
}
class R { static class raw { static int loading=1; } }
class MediaPlayer {
    interface Listener { boolean onError(MediaPlayer player, int what, int extra); }
    static boolean fail;
    static int creates, releases;
    boolean playing, looping, released;
    Listener listener;
    static MediaPlayer create(Object context, int id) { creates++; return fail ? null : new MediaPlayer(); }
    void setLooping(boolean value) { looping=value; }
    void setOnErrorListener(Listener value) { listener=value; }
    boolean isPlaying() { assert !released; return playing; }
    void start() { assert !released; playing=true; }
    void pause() { assert !released; playing=false; }
    void release() { assert !released; released=true; playing=false; releases++; }
}
class Base { protected void onPause() {} }
public class StartupMusicTest extends Base {
    static String CTRDS_TAG="test";
    MediaPlayer startupMusic;
    Object startupOverlay=new Object(), startupProgressUpdater;
    class Handler { void removeCallbacks(Object callback) {} }
    Handler startupProgressHandler=new Handler();
    static int unmuted;
    static void nativeFinishStartupAudio() { assert MediaPlayer.releases>0; unmuted++; }
    void updateTouchControls() {}
    void removeStartupOverlay() { startupOverlay=null; }
    /* PRODUCTION */
    public static void main(String[] args) {
        StartupMusicTest view=new StartupMusicTest();
        view.startStartupMusic();
        MediaPlayer player=view.startupMusic;
        assert player.playing && player.looping;
        view.startStartupMusic();
        assert MediaPlayer.creates==1;
        view.onPause(); assert !player.playing;
        view.startStartupMusic(); assert player.playing && MediaPlayer.creates==1;
        view.finishStartupOverlay();
        assert view.startupMusic==null && player.released && unmuted==1;
        view.finishStartupOverlay(); view.startStartupMusic(); view.stopStartupMusic();
        assert MediaPlayer.creates==1 && MediaPlayer.releases==1 && unmuted==1;
        view.startupOverlay=new Object();
        MediaPlayer.fail=true; view.startStartupMusic(); assert view.startupMusic==null;
        MediaPlayer.fail=false; view.startStartupMusic();
        player=view.startupMusic;
        assert player.listener.onError(player, 1, 0);
        assert player.released && view.startupMusic==null;
    }
}
'''
    native = (root / "platform/native_audio.c").read_text(encoding="utf-8")
    gain = next(line for line in native.splitlines() if "SDL_GetAtomicInt(&s_startupAudioReady) ?" in line)
    c = r'''
#include <assert.h>
#include <stddef.h>
typedef int SDL_AtomicInt;
static SDL_AtomicInt s_startupAudioReady;
static struct { struct { void *stream; } output; } s_audio;
static float currentGain=1.0f;
static void SDL_SetAtomicInt(SDL_AtomicInt *p, int value) { *p=value; }
static int SDL_GetAtomicInt(SDL_AtomicInt *p) { return *p; }
static void SDL_SetAudioStreamGain(void *stream, float value) { assert(stream); currentGain=value; }
/* PRODUCTION */
static void open_stream(void) {
    s_audio.output.stream=&s_audio;
    /* GAIN */
}
int main(void) {
    open_stream(); assert(currentGain==0.0f);
    NativeAudio_EndStartupMute(); assert(currentGain==1.0f);
    s_audio.output.stream=NULL;
    NativeAudio_EndStartupMute();
    open_stream(); assert(currentGain==1.0f);
}
'''
    with tempfile.TemporaryDirectory(prefix="ctr-startup-audio-") as directory:
        temp = Path(directory)
        unit = temp / "StartupMusicTest.java"
        unit.write_text(java.replace("/* PRODUCTION */", production), encoding="utf-8")
        subprocess.run(["javac", "-d", str(temp), str(unit)], check=True)
        subprocess.run(["java", "-ea", "-cp", str(temp), "StartupMusicTest"], check=True)
        unit = temp / "gain.c"
        unit.write_text(c.replace("/* PRODUCTION */", block(native, "void NativeAudio_EndStartupMute("))
                        .replace("/* GAIN */", gain), encoding="utf-8")
        binary = temp / "gain.exe"
        subprocess.run([args.cc, "-std=c11", str(unit), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("Startup audio: loop, pause/resume, menu handoff, failures, release and native gain: PASS")


if __name__ == "__main__":
    main()
