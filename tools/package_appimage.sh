#!/usr/bin/env bash
# Package a Linux build's game and helper executables as an AppImage.
#
#   package_appimage.sh --build DIR --assets DIR --notices DIR --output FILE [--exe NAME]
#
# Called by tools/make_release.py --platform linux. The AppImage carries the
# executables, launcher assets and the shared-library closure the loader can
# actually reach. Bundled content (mods/, assets/, docs) stays beside the
# AppImage in the release folder, where the game resolves it from its working
# directory, exactly like the Windows ZIP.
set -euo pipefail

APP_NAME="F-Zero Forever"
HELPERS="FZeroExportCourses FZeroInspectPacks"
PYINSTALLER_HELPER="FZeroConvertContent"
ROM_EXTS="sfc smc"

LINUXDEPLOY_URL=https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
LINUXDEPLOY_SHA=36a2d7e274d12e1050d0e9ecfe11d339ed54720b2bec464c286d53f8b07f5c62
APPIMAGETOOL_URL=https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
APPIMAGETOOL_SHA=a6d71e2b6cd66f8e8d16c37ad164658985e0cf5fcaa950c90a482890cb9d13e0

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="" ASSETS="" NOTICES="" OUTPUT="" EXE="$APP_NAME"
while [ $# -gt 0 ]; do
  case "$1" in
    --build) BUILD="$2"; shift 2;;
    --assets) ASSETS="$2"; shift 2;;
    --notices) NOTICES="$2"; shift 2;;
    --output) OUTPUT="$2"; shift 2;;
    --exe) EXE="$2"; shift 2;;
    -h|--help) sed -n '2,12p' "$0"; exit 0;;
    *) echo "package_appimage.sh: unknown arg: $1" >&2; exit 2;;
  esac
done
[ -n "$BUILD" ] && [ -n "$ASSETS" ] && [ -n "$NOTICES" ] && [ -n "$OUTPUT" ] || {
  echo "package_appimage.sh: --build, --assets, --notices and --output are required" >&2; exit 2; }
BUILD="$(cd "$BUILD" && pwd)"
BIN="$BUILD/$EXE"
file -b "$BIN" | grep -q "ELF.*executable" || { echo "no Linux ELF at $BIN" >&2; exit 1; }
for helper in $HELPERS $PYINSTALLER_HELPER; do
  [ -x "$BUILD/$helper" ] || { echo "missing helper executable $BUILD/$helper" >&2; exit 1; }
done
[ -f "$ASSETS/fonts/LatoLatin-Regular.ttf" ] || { echo "staged launcher assets missing at $ASSETS" >&2; exit 1; }

WORK="$(mktemp -d "$BUILD/appimage-work.XXXXXX")"
cleanup() { chmod -R u+w "$WORK" 2>/dev/null || true; rm -rf "$WORK"; }
trap cleanup EXIT

APPDIR="$WORK/AppDir"
mkdir -p "$APPDIR"
TOOLS_DIR="$BUILD/appimage-tools"
mkdir -p "$TOOLS_DIR"

fetch_tool() {
  local url="$1" sha="$2" dest="$3"
  if [ ! -f "$dest" ] || [ "$(sha256sum "$dest" | awk '{print $1}')" != "$sha" ]; then
    curl -fL --retry 3 "$url" -o "$dest.tmp"
    printf '%s  %s\n' "$sha" "$dest.tmp" | sha256sum -c - >/dev/null
    mv "$dest.tmp" "$dest"
  fi
  chmod 0755 "$dest"
}
LINUXDEPLOY_BIN="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
APPIMAGETOOL_BIN="$TOOLS_DIR/appimagetool-x86_64.AppImage"
fetch_tool "$LINUXDEPLOY_URL" "$LINUXDEPLOY_SHA" "$LINUXDEPLOY_BIN"
fetch_tool "$APPIMAGETOOL_URL" "$APPIMAGETOOL_SHA" "$APPIMAGETOOL_BIN"
LINUXDEPLOY="$LINUXDEPLOY_BIN --appimage-extract-and-run"
APPIMAGETOOL="$APPIMAGETOOL_BIN --appimage-extract-and-run"

ICON="$WORK/fzerosnesrecomp.png"
command -v convert >/dev/null 2>&1 || { echo "ImageMagick convert is required for the icon" >&2; exit 1; }
convert "$REPO/assets/img/boxart.tga" -resize 240x240 -background transparent \
  -gravity center -extent 256x256 "$ICON"

DESKTOP="$WORK/fzerosnesrecomp.desktop"
cat > "$DESKTOP" <<EOF
[Desktop Entry]
Type=Application
Name=F-Zero Forever
Exec="F-Zero Forever"
Icon=fzerosnesrecomp
Categories=Game;
Terminal=false
EOF

# The game binary is renamed to the product name inside the AppDir.
STAGE_BIN="$WORK/$APP_NAME"
cp "$BIN" "$STAGE_BIN"
deploy_args=(--appdir "$APPDIR" --executable "$STAGE_BIN" --desktop-file "$DESKTOP" --icon-file "$ICON")
for helper in $HELPERS; do
  deploy_args+=(--executable "$BUILD/$helper")
done
$LINUXDEPLOY "${deploy_args[@]}"
# PyInstaller appends its archive to the ELF; patchelf/strip would corrupt it,
# and it carries its own libraries, so it is copied verbatim.
install -m 0755 "$BUILD/$PYINSTALLER_HELPER" "$APPDIR/usr/bin/$PYINSTALLER_HELPER"

# Prune libraries the executables cannot actually reach.
#
# linuxdeploy copies the whole transitive closure it sees on the BUILD host,
# including libraries only pulled in by host-side dependencies we do NOT
# bundle (libfreetype / libharfbuzz drag in glib, pcre2, png16, brotli, bz2,
# graphite2). Those copies are unreachable through the binaries' own RUNPATH
# ($ORIGIN/../lib); only a global LD_LIBRARY_PATH would make the loader prefer
# them, and that must not be set: every child process inherits it, so a host
# zenity/kdialog spawned by the file picker would load OUR glib against the
# host's GTK and die (the 1.6.0 "ROM button does nothing" bug).
#
# Rule: bundle a library only if everything above it in the chain is bundled
# too. Resolve each closure with LD_LIBRARY_PATH unset -- precisely what the
# shipped AppRun gives the loader -- and delete anything the loader did not pick.
closure() {
  for exe in "$APPDIR/usr/bin/$APP_NAME" $(for h in $HELPERS; do echo "$APPDIR/usr/bin/$h"; done); do
    env -u LD_LIBRARY_PATH ldd "$exe"
  done | awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^\//) print $i }' \
       | while read -r p; do readlink -f "$p" 2>/dev/null || true; done | sort -u
}
if [ -d "$APPDIR/usr/lib" ]; then
  KEEP="$WORK/appdir-keep.txt"
  closure > "$KEEP"
  pruned=0
  for f in "$APPDIR"/usr/lib/*; do
    [ -e "$f" ] || continue
    if ! grep -qxF "$(readlink -f "$f")" "$KEEP"; then
      echo "  prune unreachable bundled lib: $(basename "$f")"
      rm -f "$f"
      pruned=$((pruned + 1))
    fi
  done
  echo "  pruned $pruned unreachable libraries from usr/lib"
fi
for exe in "$APP_NAME" $HELPERS; do
  if env -u LD_LIBRARY_PATH ldd "$APPDIR/usr/bin/$exe" | grep -q 'not found'; then
    echo "$exe has unresolved libraries without LD_LIBRARY_PATH" >&2
    env -u LD_LIBRARY_PATH ldd "$APPDIR/usr/bin/$exe" | grep 'not found' >&2
    exit 1
  fi
done

# The recomp-ui launcher loads fonts, images and shaders from the executable's
# own directory (inside the mount); the game reads content from its cwd.
mkdir -p "$APPDIR/usr/bin/assets"
cp -r "$ASSETS/." "$APPDIR/usr/bin/assets/"
python3 "$REPO/tools/stage_linux_notices.py" "$APPDIR"

# Every bundled library ships with its notice: Debian packages carry theirs
# in /usr/share/doc/<package>/copyright; source-built libraries (SDL3 in
# /usr/local) are covered by stage_linux_notices.py.
mkdir -p "$NOTICES"
cp "$APPDIR/usr/share/doc/fzerosnesrecomp/licenses/${SNESRECOMP_SDL_BACKEND:-SDL3}.txt" "$NOTICES/"
if [ -d "$APPDIR/usr/lib" ]; then
  for f in "$APPDIR"/usr/lib/*; do
    [ -e "$f" ] || continue
    lib="$(basename "$f")"
    case "$lib" in libSDL3.so*|libSDL2*.so*) continue;; esac
    origin="$(env -u LD_LIBRARY_PATH ldconfig -p | awk -v l="$lib" '$1 == l { print $NF; exit }')"
    pkg=""
    [ -n "$origin" ] && pkg="$(dpkg -S "$(readlink -f "$origin")" 2>/dev/null | head -n1 | cut -d: -f1)"
    [ -z "$pkg" ] && [ -n "$origin" ] && pkg="$(dpkg -S "$origin" 2>/dev/null | head -n1 | cut -d: -f1)"
    copyright="/usr/share/doc/$pkg/copyright"
    [ -n "$pkg" ] && [ -f "$copyright" ] || {
      echo "no license notice found for bundled library $lib (${origin:-not in ldconfig})" >&2; exit 1; }
    cp "$copyright" "$NOTICES/$pkg.txt"
    cp "$copyright" "$APPDIR/usr/share/doc/fzerosnesrecomp/licenses/$pkg.txt"
  done
fi

rm -f "$APPDIR/AppRun"
cat > "$APPDIR/AppRun" <<EOF
#!/bin/sh
HERE="\$(dirname "\$(readlink -f "\$0")")"

# Do NOT export LD_LIBRARY_PATH for the bundle. The binaries carry
# RUNPATH \$ORIGIN/../lib and find usr/lib on their own, and a global
# LD_LIBRARY_PATH is inherited by every child process -- including the host
# zenity/kdialog the launcher spawns for the ROM picker, which would then load
# this bundle's libraries against the host GTK/Qt stack and die on start
# (the 1.6.0 "Browse For ROM does nothing" bug).
#
# Record the host's own value instead, so the launcher can hand a correct
# LD_LIBRARY_PATH to anything it spawns even if a wrapper set one for us.
export RECOMP_HOST_LD_LIBRARY_PATH="\${LD_LIBRARY_PATH:-}"
export SDL_JOYSTICK_HIDAPI_STEAM=1
export SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD=1
SELF="\${APPIMAGE:-\$0}"
ROMDIR="\$(dirname "\$(readlink -f "\$SELF")")"
export FZERO_VIDEO_CONFIG="\$ROMDIR/fzero-video.ini"

ROM=""
for ext in $ROM_EXTS; do
  for file in "\$ROMDIR"/*."\$ext"; do [ -e "\$file" ] && ROM="\$file" && break 2; done
done
if [ -n "\$ROM" ]; then
  cached=""
  [ -f "\$ROMDIR/rom.cfg" ] && cached="\$(head -n1 "\$ROMDIR/rom.cfg" 2>/dev/null | tr -d '\\r\\n')"
  if [ -z "\$cached" ] || [ ! -f "\$cached" ]; then
    [ -w "\$ROMDIR" ] && printf '%s\\n' "\$ROM" > "\$ROMDIR/rom.cfg" 2>/dev/null || true
  fi
fi

cd "\$ROMDIR" 2>/dev/null || true
exec "\$HERE/usr/bin/$APP_NAME" "\$@"
EOF
chmod +x "$APPDIR/AppRun"

bash "$REPO/tools/test_appimage_layout.sh" "$APPDIR"

mkdir -p "$(dirname "$OUTPUT")"
rm -f "$OUTPUT"
ARCH=x86_64 $APPIMAGETOOL --no-appstream "$APPDIR" "$OUTPUT"
chmod 0755 "$OUTPUT"
echo "AppImage: $OUTPUT"
