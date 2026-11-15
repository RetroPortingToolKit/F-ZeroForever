# Building from source

Clone with submodules:

```bash
git clone --recurse-submodules https://github.com/retroportingtoolkit/FZeroSNESRecomp.git
cd FZeroSNESRecomp
bash tools/bootstrap.sh
```

Put your own USA ROM at `fzero.sfc`, then generate the recompiled source:

```bash
bash tools/regen.sh
```

BS F-Zero Deluxe is built into every release. Generate its native module once
from the upstream Deluxe 1.1 archive (also your own copy):

```bash
python tools/regen_bs_deluxe.py --archive "BS F-Zero Deluxe.zip"
```

Local ROMs, generated files, saves, captures, and builds are ignored by git.

## Platform build scripts

Each platform has one entry point. They check dependencies, create a private
Python environment for the importer helper (`tools/converter-requirements.txt`),
then run the shared `tools/build_release.py`: verify submodule pins and
generated sources, configure and build every target in Release, run CTest,
and freeze `FZeroConvertContent` with PyInstaller. The runnable build lands in
`build-release-<platform>`. Pass `--help` for every option.

| Platform | Command | Dependencies |
| --- | --- | --- |
| Windows x64 | `powershell -File tools\build-windows.ps1` | MSYS2 MinGW: `pacman -S mingw-w64-x86_64-{gcc,cmake,ninja,libxml2,libpng,giflib,rapidjson,libarchive,SDL3}`; Python 3.12 for Windows |
| Linux x86_64 | `bash tools/build-linux.sh` | `apt install build-essential cmake ninja-build python3-venv file curl imagemagick libxml2-dev libpng-dev libgif-dev rapidjson-dev libgl-dev libarchive-dev` |
| macOS | `bash tools/build-macos.sh` | Xcode Command Line Tools; `brew install cmake ninja python@3.12 libpng giflib rapidjson libarchive imagemagick` |

SDL3 is taken from the system when installed; otherwise CMake builds the
pinned SDL3 release from source. Override generated-source locations with
`--gen-dir`, `--deluxe-gen-dir` and `--deluxe-data`.

## Player releases

Add `--packs DIR` (the reviewed, exported course packs) to stage one release per
edition through `tools/make_release.py`. The with-msu edition also needs the
verified soundtracks (`--cgp-music`, `--astra-velocity-music`); select editions
with `--editions with-msu,without-msu`. Commit first: the manifest records the
source revision and refuses dependency checkouts that differ from the pins.

| Platform | Archive | Layout |
| --- | --- | --- |
| Windows | `F-Zero-Forever-<v>-windows-x64-<edition>.zip` | `F-Zero Forever.exe`, helpers, MinGW DLLs, content |
| Linux | `F-Zero-Forever-<v>-linux-x86_64-<edition>.tar.gz` | `F-Zero Forever.AppImage` (game, helpers, libraries), content |
| macOS | `F-Zero-Forever-<v>-macos-<arch>-<edition>.zip` | `F-Zero Forever` + `.command` launcher, helpers, `lib/*.dylib` (ad-hoc signed), content |

The bundled content (courses, prebuilt course cache, soundtracks, documents
and notices) is identical across platforms; each archive ships a `.sha256` and
a manifest of every file. Releases are packaged on the platform they target,
because the course cache is produced by that build's own `FZeroInspectPacks`.
The Linux AppImage runs `tools/test_appimage_layout.sh` before it is sealed.
macOS releases are not notarized; players remove the quarantine attribute.

Run the ROM-free regression suites with `ctest --test-dir build --output-on-failure`.
With your local ROM and the upstream patch ZIP, `tests/test_msu_integration.py`
checks stock/Deluxe playback using generated test tones, missing-track fallback
and invalid-patch rejection. On Windows, `tests/test_desktop_integration.py`
additionally checks real launcher persistence, imported shaders, controller
overlays and save/load. Both scripts accept `--help` and keep their test data
under ignored `captures/` directories; neither bundles music or a shader.

`python tests/test_rom_persistence.py --source build` checks ROM selection
through the actual Windows file dialog, then Play/close and repeated restarts.
It also checks cancellation, an invalid pick, and selecting a moved ROM. It
requires an interactive desktop and never seeds the ROM cache itself.

`python tests/test_launcher_options.py --source build --rom PATH
--output captures/launcher-options` exercises the real Skip Launcher control,
relaunch/recovery, and typed HD resolution persistence. On Linux, prefix it
with `xvfb-run -a`. `--appimage-layout` also checks settings beside an AppImage
instead of inside its executable directory, using the AppImage environment.

On Linux, `xvfb-run -a python3 tests/test_appimage_rom_persistence.py
--appimage PATH --output captures/rom-persistence-linux` exercises the packaged
AppImage through the real zenity picker, then Play/quit and repeated relaunches.
It requires `zenity` and `xdotool`; use a fresh output directory for each run.


See [Contributing](../CONTRIBUTING.md) for dependency pins, generated-code rules and validation expectations.
