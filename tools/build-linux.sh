#!/usr/bin/env bash
# Build (and optionally package) F-Zero Forever for Linux x86_64.
#
#   bash tools/build-linux.sh                     # development build + tests
#   bash tools/build-linux.sh --packs DIR ...     # + release .tar.gz per edition
#
# All options are passed to tools/build_release.py (run with --help for them).
# Releases are an AppImage plus the bundled content in one .tar.gz, matching
# the Windows ZIP layout.
#
# Debian/Ubuntu packages:
#   sudo apt install build-essential cmake ninja-build python3-venv file curl \
#     imagemagick libxml2-dev libpng-dev libgif-dev rapidjson-dev libgl-dev \
#     libarchive-dev
# SDL3: an installed SDL3 package is used when found; otherwise CMake builds the
# pinned SDL3 release from source (install SDL's X11/Wayland/audio development
# packages first so that build has every backend).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
case "${1:-}" in -h|--help) sed -n '2,18p' "$0"; echo; exec python3 "$REPO/tools/build_release.py" --help;; esac

missing=()
for tool in cmake gcc g++ python3 file curl convert; do
  command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
for header in libxml2/libxml/parser.h png.h gif_lib.h rapidjson/document.h GL/gl.h archive.h; do
  [ -f "/usr/include/$header" ] || [ -f "/usr/local/include/$header" ] || missing+=("$header")
done
if [ "${#missing[@]}" -gt 0 ]; then
  echo "build-linux.sh: missing build dependencies: ${missing[*]}" >&2
  sed -n '12,15p' "$0" >&2
  exit 1
fi

# The importer helper is frozen with PyInstaller from a private, pinned venv.
VENV="$REPO/build-tools-venv-linux"
if [ ! -x "$VENV/bin/python" ]; then
  python3 -m venv "$VENV"
fi
"$VENV/bin/python" -m pip install --quiet --disable-pip-version-check \
  -r "$REPO/tools/converter-requirements.txt"

export SNESRECOMP_SDL_BACKEND="${SNESRECOMP_SDL_BACKEND:-SDL3}"
exec "$VENV/bin/python" "$REPO/tools/build_release.py" "$@"
