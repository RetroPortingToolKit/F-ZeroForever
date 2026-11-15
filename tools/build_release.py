"""Configure, build, test and package F-Zero Forever on Windows, Linux or macOS.

One implementation behind the per-platform entry points:

  Windows  powershell -File tools/build-windows.ps1 [options]
  Linux    bash tools/build-linux.sh [options]
  macOS    bash tools/build-macos.sh [options]

Those wrappers set up the platform toolchain and a Python with PyInstaller and
Pillow (tools/converter-requirements.txt), then run this script. Steps:

  1. verify the snesrecomp / recomp-ui submodules match their committed pins
  2. verify generated sources: src/gen (tools/regen.sh) and the BS Deluxe
     native module (tools/regen_bs_deluxe.py) -- both need your own ROMs
  3. configure + build every target (Release), then run CTest
  4. build the FZeroConvertContent importer helper with PyInstaller
  5. with --packs: stage one player release per edition through
     tools/make_release.py (with-msu needs --cgp-music/--astra-velocity-music)

Without --packs it stops after step 4, leaving a runnable development build.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = {"win32": "windows", "darwin": "macos"}.get(sys.platform, "linux")
SUFFIX = ".exe" if PLATFORM == "windows" else ""


def run(command, **kwargs):
    print("+ " + " ".join(f'"{c}"' if " " in str(c) else str(c) for c in command), flush=True)
    subprocess.run([str(c) for c in command], check=True, **kwargs)


def fail(message):
    raise SystemExit(f"build_release: {message}")


def check_submodules():
    for module in ("snesrecomp", "recomp-ui"):
        recorded = subprocess.check_output(["git", "ls-tree", "HEAD", module], cwd=ROOT, text=True).split()
        try:
            actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT / module,
                                             text=True, stderr=subprocess.DEVNULL).strip()
        except (subprocess.CalledProcessError, FileNotFoundError):
            actual = "<missing>"
        if len(recorded) < 3 or recorded[2] != actual:
            fail(f"{module} is at {actual}, expected {recorded[2] if len(recorded) > 2 else '?'}; "
                 "run: git submodule update --init --recursive")


def check_generated(gen, deluxe_gen, deluxe_data):
    if not any(gen.glob("*.c")):
        fail(f"{gen} has no generated sources; stage your F-Zero (USA) ROM as fzero.sfc "
             "and run: bash tools/regen.sh")
    sys.path.insert(0, str(ROOT / "tools"))
    from import_bs_deluxe import validate_namespace
    try:
        validate_namespace(deluxe_gen)
    except ValueError:
        fail(f"{deluxe_gen} is not a current BS Deluxe native module; run: "
             "python tools/regen_bs_deluxe.py --archive <BS F-Zero Deluxe 1.1 zip>")
    if not deluxe_data.is_file():
        fail(f"missing embedded BS Deluxe payload {deluxe_data}; run tools/regen_bs_deluxe.py")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--build", type=Path, default=ROOT / f"build-release-{PLATFORM}",
                   help="Build directory (default: build-release-<platform>)")
    p.add_argument("--version", default=(ROOT / "VERSION").read_text().strip(),
                   help="Release version; must match VERSION (default: VERSION)")
    p.add_argument("--label", default="", help="Optional build label, such as rc.1")
    p.add_argument("--gen-dir", type=Path, default=ROOT / "src/gen")
    p.add_argument("--deluxe-gen-dir", type=Path, default=ROOT / "captures/bs-deluxe/gen")
    p.add_argument("--deluxe-data", type=Path, default=ROOT / "captures/bs-deluxe/mods/bs-deluxe.dat")
    p.add_argument("--generator", default=None, help="CMake generator (default: Ninja when available)")
    p.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    p.add_argument("--cmake-arg", action="append", default=[], help="Extra -D argument for CMake")
    p.add_argument("--skip-tests", action="store_true")
    p.add_argument("--packs", type=Path, help="Reviewed exported course packs; enables packaging")
    p.add_argument("--editions", default="with-msu,without-msu",
                   help="Comma-separated: with-msu (bundled soundtracks), without-msu")
    p.add_argument("--cgp-music", type=Path, default=ROOT / "music/cgp")
    p.add_argument("--astra-velocity-music", type=Path, default=ROOT / "music/astra-velocity")
    p.add_argument("--deluxe-mods", type=Path, default=ROOT / "captures/bs-deluxe/mods")
    p.add_argument("--output", type=Path, default=ROOT / f"release-{PLATFORM}",
                   help="Parent directory for fresh staging folders and archives")
    p.add_argument("--mingw", type=Path, default=Path(os.environ.get("MINGW_PREFIX") or "C:/msys64/mingw64"),
                   help="Windows only: MSYS2 MinGW prefix")
    a = p.parse_args()

    if a.version != (ROOT / "VERSION").read_text().strip():
        fail("--version must match VERSION; update VERSION and CHANGELOG.md first")
    editions = [e.strip() for e in a.editions.split(",") if e.strip()]
    if not set(editions) <= {"with-msu", "without-msu"} or not editions:
        fail("--editions accepts with-msu and/or without-msu")
    build = a.build.resolve()
    release_version = a.version + ("-" + a.label if a.label else "")

    check_submodules()
    check_generated(a.gen_dir.resolve(), a.deluxe_gen_dir.resolve(), a.deluxe_data.resolve())
    for module in ("PyInstaller", "PIL"):
        try:
            __import__(module)
        except ImportError:
            fail(f"{sys.executable} lacks {module}; install tools/converter-requirements.txt "
                 "(the platform wrapper does this in a virtual environment)")

    generator = a.generator or ("Ninja" if shutil.which("ninja") else None)
    configure = ["cmake", "-S", ROOT, "-B", build, "-DCMAKE_BUILD_TYPE=Release",
                 f"-DSNESRECOMP_BUILD_VERSION={release_version}",
                 f"-DFZERO_GEN_DIR={a.gen_dir.resolve().as_posix()}",
                 f"-DFZERO_DELUXE_GEN_DIR={a.deluxe_gen_dir.resolve().as_posix()}",
                 f"-DFZERO_DELUXE_DATA_FILE={a.deluxe_data.resolve().as_posix()}",
                 "-DFZERO_STAGE_RELEASE_CONTENT=OFF",
                 f"-DPython3_EXECUTABLE={Path(sys.executable).as_posix()}"]
    if generator:
        configure += ["-G", generator]
    if PLATFORM == "windows":
        mingw = a.mingw
        configure += [f"-DCMAKE_C_COMPILER={(mingw / 'bin/gcc.exe').as_posix()}",
                      f"-DCMAKE_CXX_COMPILER={(mingw / 'bin/g++.exe').as_posix()}"]
    configure += [f"-D{arg}" if not arg.startswith("-D") else arg for arg in a.cmake_arg]
    run(configure)
    run(["cmake", "--build", build, "--parallel", str(a.jobs)])
    if not a.skip_tests:
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        run(["ctest", "--test-dir", build, "--output-on-failure", "-j", str(a.jobs)], env=env)

    converter = build / "converter"
    env = dict(os.environ, SNESRECOMP_ROOT=str(ROOT / "snesrecomp"))
    run([sys.executable, "-X", "utf8", "-m", "PyInstaller", "--noconfirm", "--distpath", converter,
         "--workpath", converter / "work", ROOT / "tools/FZeroConvertContent.spec"], env=env)
    run([sys.executable, "-X", "utf8", ROOT / "tools/stage_content_importer.py",
         "--write-notices", converter / "licenses"])
    stage_args = [sys.executable, "-X", "utf8", ROOT / "tools/stage_content_importer.py",
                  "--build", build, "--converter", converter / ("FZeroConvertContent" + SUFFIX)]
    if PLATFORM == "windows":
        stage_args += ["--mingw", a.mingw]
    run(stage_args)
    print(f"Build ready: {build / ('F-Zero Forever' + SUFFIX)}", flush=True)

    if a.packs is None:
        print("No --packs given: skipping release packaging.")
        return
    a.output.mkdir(parents=True, exist_ok=True)
    for edition in editions:
        command = [sys.executable, "-X", "utf8", ROOT / "tools/make_release.py", "--platform", PLATFORM,
                   "--build", build, "--output", a.output.resolve(), "--profile", "player",
                   "--music", "bundled" if edition == "with-msu" else "external",
                   "--packs", a.packs.resolve(), "--deluxe-mods", a.deluxe_mods.resolve(),
                   "--cgp-music", a.cgp_music.resolve(),
                   "--astra-velocity-music", a.astra_velocity_music.resolve()]
        if a.label:
            command += ["--label", a.label]
        if PLATFORM == "windows":
            command += ["--mingw", a.mingw]
        run(command, cwd=ROOT)


if __name__ == "__main__":
    main()
