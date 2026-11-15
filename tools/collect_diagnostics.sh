#!/bin/sh
# Collect recent F-Zero Forever diagnostics into one archive for a bug report.
# Linux/macOS counterpart of Collect diagnostics.cmd; ships beside the game.
# Nothing is uploaded. The archive contains game memory, saves and local paths.
set -eu

ROOT="${1:-$(cd "$(dirname "$0")" && pwd)}"
cd "$ROOT"

items=""
add() { items="$items
$1"; }
if [ -d diagnostics ]; then
  # Newest three replay folders and the newest performance log.
  for d in $(ls -1td diagnostics/replay-*/ 2>/dev/null | head -n 3); do add "${d%/}"; done
  perf="$(ls -1t diagnostics/performance-*.jsonl 2>/dev/null | head -n 1 || true)"
  [ -n "$perf" ] && add "$perf"
fi
[ -f last_run_report.json ] && add last_run_report.json
[ -f saves/netplay/net_diag.jsonl ] && add saves/netplay/net_diag.jsonl
items="$(printf '%s\n' "$items" | sed '/^$/d')"
if [ -z "$items" ]; then
  echo "No diagnostics found. Play with Crash diagnostics and replay enabled, quit, then try again." >&2
  exit 1
fi

mkdir -p reports
stamp="$(date +%Y%m%d-%H%M%S)"
if command -v zip >/dev/null 2>&1; then
  archive="reports/FZero-diagnostics-$stamp.zip"
  printf '%s\n' "$items" | zip -q -r -@ "$archive"
else
  archive="reports/FZero-diagnostics-$stamp.tar.gz"
  printf '%s\n' "$items" | tar -czf "$archive" -T -
fi
echo "Created $ROOT/$archive"
echo "Send this archive with a description of the problem. It contains game memory, saves and local diagnostic paths. Nothing is uploaded automatically."
case "$(uname -s)" in
  Darwin) open -R "$archive" 2>/dev/null || true ;;
  *) command -v xdg-open >/dev/null 2>&1 && xdg-open reports >/dev/null 2>&1 & ;;
esac
