# F-Zero Forever for Android

Native SDL runner and Android launcher, using the same structure as the Sonic
Android ports. The game consumer pins snesrecomp and recomp-ui as submodules;
there is no second engine copy in this project. Android 9 or newer, arm64 or
x86-64, OpenGL ES 2.0. No ROM is bundled.

Install the signed APK and choose your own unmodified F-Zero (USA) `.sfc` or
`.smc`. The importer verifies SHA-256 before atomically replacing the installed
copy and accepts a 512-byte copier header. No broad storage permission is used.

Home, Settings and Mods use native Android widgets with large touch targets.
Fit to screen and adaptive widescreen default on. The portable HD renderer is
available in Mods and defaults off to limit mobile CPU and battery cost.
Crash diagnostics and replay recording default on; the independent Mods switch
remembers an explicit opt-out. Performance diagnostics are separately opt-in.
The desktop GL4 HD backend, desktop shader presets and online lobby UI are not
exposed by this Android launcher.

Touch controls support steering while accelerating/boosting/leaning. Standard
SDL gamepads work alongside touch. Android Back or the upper-right menu pauses
the game and offers resume, quick save, quick load and return to launcher.
The game runs in a separate app process so returning to the launcher and
starting again cannot reuse stale native global state. Native saves and
settings stay in the app's private files directory across APK updates.
Uninstalling the app removes that data, as with other Android apps.

## Build

Use JDK 17, Android SDK 35, NDK 27.1.12297006 and CMake 3.22.1.
The Gradle wrapper is 8.7 with Android Gradle Plugin 8.6.1. Native libraries
use 16 KiB ELF alignment. Dependencies are fetched from upstream archives
and SHA-256-checked against `sources.json`; matching SDL Java glue is taken
directly from that pinned source tree.

Initialize the engine/UI submodules recursively. Generate the private stock
and namespaced BS Deluxe native sources through the normal host pipelines.
Set `FZERO_GEN_DIR`, `FZERO_DELUXE_GEN_DIR` and `FZERO_DELUXE_DATA_FILE` to those
verified inputs. `SNESRECOMP_ROOT` and `RECOMP_UI_ROOT` can select worktrees.

Stage content from the verified without-MSU desktop release:

```
python android/tools/stage_content.py path/to/without-msu.zip
python android/tools/build.py --debug
```

For a signed release, keep a private JSON file outside Git with `keystore`,
`alias`, `store_password` and `key_password`, then run:

```
python android/tools/build.py --signing path/to/private-signing.json
```

The stable signing key must be backed up privately to permit in-place updates.
Do not use the debug keystore for release distribution. The script requires
signing configuration before building a release. APKs are under
`android/app/build/outputs/apk/`. Compilation is limited to two jobs, with
one linker, and scripts impose no total-run timeout.

The standalone `android/tools/test_game_files.java` exercises ROM integrity,
header stripping, atomic replacement, archive containment, fresh defaults and
saved-choice preservation using the JDK and optional private test ROM.
