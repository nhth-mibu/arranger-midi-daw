#!/usr/bin/env bash
# UI smoke test: opens each example song in the app on a virtual screen (Xvfb),
# runs every menu command once in each view, then quits while the song plays.
# The app must not crash or hang. Linux only (it needs xvfb-run).
#
#   scripts/ui-smoke-test.sh "<build>/src/app/ArrangerMidiDaw_artefacts/Release/Arranger MIDI DAW"
#
# The songs are copied to a temporary folder first, because Save is one of the
# commands. Settings go to "Arranger MIDI DAW Smoke Test", not your own.
set -euo pipefail

app="${1:?give the path of the app}"
repo="$(cd "$(dirname "$0")/.." && pwd)"
command -v xvfb-run > /dev/null || { echo "xvfb-run is not installed: skipped"; exit 0; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cp -r "$repo/examples/." "$work/"

failed=0
while IFS= read -r -d '' song; do
    name="${song#"$work"/}"
    log="$work/log.txt"
    start=$(date +%s)
    if timeout 600 xvfb-run -a -s "-screen 0 1600x1000x24" "$app" --smoke-test "$song" > "$log" 2>&1 \
        && grep -q "^smoke test: ran " "$log"; then
        echo "ok    $name ($(grep '^smoke test: ran ' "$log" | sed 's/smoke test: //'), $(( $(date +%s) - start )) s)"
    else
        echo "FAIL  $name: the last lines were"
        tail -n 25 "$log" | sed 's/^/      /'
        failed=1
    fi
done < <(find "$work" -name "*.arranger" -print0 | sort -z)
exit $failed
