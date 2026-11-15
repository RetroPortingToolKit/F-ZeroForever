#!/usr/bin/env bash
# Build (and optionally package) F-Zero Forever for macOS (Apple silicon or Intel).
#
#   bash tools/build-macos.sh                     # development build + tests
#   bash tools/build-macos.sh --packs DIR ...     # + release .zip per edition
#
# All options are passed to tools/build_release.py (run with --help for them).
# Releases are a folder with the game, its helpers, lib/*.dylib and the bundled
# content, ad-hoc signed and zipped. They are not notarized.
#
# Requirements: Xcode Command Line Tools (xcode-select --install) and Homebrew:
#   brew install cmake ninja python@3.12 libpng giflib rapidjson libarchive imagemagick
# SDL3 is used from Homebrew when installed (brew install sdl3); otherwise CMake
# builds the pinned SDL3 release from source and links it statically.
# Universal build: --cmake-arg CMAKE_OSX_ARCHITECTURES="arm64;x86_64" (needs
# universal dependencies; Homebrew bottles are single-architecture).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
case "${1:-}" in -h|--help) sed -n '2,17p' "$0"; echo; exec python3 "$REPO/tools/build_release.py" --help;; esac

[ "$(uname -s)" = Darwin ] || { echo "build-macos.sh: run this on macOS" >&2; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "build-macos.sh: run xcode-select --install" >&2; exit 1; }
command -v brew >/dev/null 2>&1 || { echo "build-macos.sh: Homebrew is required (https://brew.sh)" >&2; exit 1; }

missing=()
for formula in cmake ninja python@3.12 libpng giflib rapidjson libarchive imagemagick; do
  brew list --versions "$formula" >/dev/null 2>&1 || missing+=("$formula")
done
if [ "${#missing[@]}" -gt 0 ]; then
  echo "build-macos.sh: missing Homebrew formulae: brew install ${missing[*]}" >&2
  exit 1
fi

BREW="$(brew --prefix)"
# Keg-only formulae are not linked into the Homebrew prefix.
PREFIXES="$BREW"
for formula in libarchive libxml2 sdl3; do
  if brew list --versions "$formula" >/dev/null 2>&1; then
    PREFIXES="$PREFIXES;$(brew --prefix "$formula")"
  fi
done
export CMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:+$CMAKE_PREFIX_PATH;}$PREFIXES"
export PATH="$BREW/bin:$PATH"

PYTHON="$(brew --prefix python@3.12)/bin/python3.12"
VENV="$REPO/build-tools-venv-macos"
if [ ! -x "$VENV/bin/python" ]; then
  "$PYTHON" -m venv "$VENV"
fi
"$VENV/bin/python" -m pip install --quiet --disable-pip-version-check \
  -r "$REPO/tools/converter-requirements.txt"

exec "$VENV/bin/python" "$REPO/tools/build_release.py" "$@"
