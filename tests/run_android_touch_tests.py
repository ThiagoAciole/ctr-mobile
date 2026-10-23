"""Run host checks against the actual native publication code and Java packet parser."""
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
        parser.error("GCC with pthread support is required; pass --cc /path/to/gcc")
    root = Path(__file__).resolve().parents[1]
    source = (root / "game/ctrds/ctrds.c").read_text(encoding="utf-8")
    header = (root / "include/ctrds.h").read_text(encoding="utf-8")
    struct_start = header.index("struct CtrdsAndroidTouchSnapshot\n{")
    struct_end = header.index("};", struct_start) + 2
    start = source.index("internal SDL_SpinLock s_gameTapLock")
    end = source.index("// 0x20 is the elapsed-time value", start)
    queue_start = source.index("void Ctrds_QueueGameTap(")
    queue_end = source.index("void Ctrds_QueueAndroidAudioVolume(", queue_start)
    production = header[struct_start:struct_end] + "\n" + source[start:end] + source[queue_start:queue_end]
    platform = (root / "platform/native_platform.c").read_text(encoding="utf-8")
    start = platform.index("#ifdef __ANDROID__\ninternal void Platform_PublishAndroidTouchFrame(")
    end = platform.index("// NOTE(aalhendi): Frame timing", start)
    production += platform[start:end]
    harness = (root / "tests/android_touch_snapshot_test.c").read_text(encoding="utf-8")
    ghost_start = source.index("int prefix = SDL_snprintf(snapshot, sizeof(snapshot),")
    ghost_end = source.index(";", ghost_start) + 1
    harness = harness.replace("/* GHOST_HEADER */", source[ghost_start:ghost_end])
    with tempfile.TemporaryDirectory(prefix="ctr-touch-") as directory:
        temp = Path(directory)
        native = temp / "snapshot_test.c"
        native.write_text(harness.replace("/* PRODUCTION_CODE */", production), encoding="utf-8")
        executable = temp / "snapshot_test.exe"
        subprocess.run([args.cc, "-std=c11", "-O2", "-pthread", str(native), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        java_dir = root / "android/app/src/main/java/com/ctrnative"
        subprocess.run(["javac", "-d", str(temp), str(java_dir / "CTRTouchSnapshot.java"),
                        str(root / "tests/CTRTouchSnapshotTest.java")], check=True)
        subprocess.run(["java", "-ea", "-cp", str(temp), "com.ctrnative.CTRTouchSnapshotTest"], check=True)


if __name__ == "__main__":
    main()
