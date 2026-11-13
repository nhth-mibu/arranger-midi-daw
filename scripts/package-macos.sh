#!/bin/bash
# Private-test packaging; no Developer ID certificate or notarization required.
set -euo pipefail

app=${1:?Usage: package-macos.sh app-path output-directory}
out=${2:?Usage: package-macos.sh app-path output-directory}
test -x "$app/Contents/MacOS/Arranger MIDI DAW"
mkdir -p "$out"
out=$(cd "$out" && pwd)
stage=$(mktemp -d "${TMPDIR:-/tmp}/arranger-dmg.XXXXXX")
# Retain failed packaging stages for diagnosis; remove only this temporary stage on success.
ditto "$app" "$stage/Arranger MIDI DAW.app"
staged_app="$stage/Arranger MIDI DAW.app"
xattr -cr "$staged_app"
codesign --force --sign - --timestamp=none "$staged_app"
codesign --verify --deep --strict --verbose=2 "$staged_app"
codesign --display --verbose=2 "$staged_app"
ln -s /Applications "$stage/Applications"
printf '%s\n' \
  'Arranger MIDI DAW - macOS ARM64 private-test build' \
  'Drag Arranger MIDI DAW.app to Applications.' \
  'Ad-hoc signed; not Developer ID signed or notarized by Apple.' \
  'macOS may block the downloaded app. Only open builds you trust.' \
  'After a blocked launch, review System Settings > Privacy & Security > Open Anyway.' \
  > "$stage/READ-ME.txt"
printf 'Commit: %s\n' "${GITHUB_SHA:-local}" >> "$stage/READ-ME.txt"

zip="$out/ArrangerMidiDaw-macOS.zip"
dmg="$out/ArrangerMidiDaw-macos-arm64.dmg"
test ! -e "$zip"
test ! -e "$dmg"
ditto -c -k --keepParent "$staged_app" "$zip"
hdiutil create -volname 'Arranger MIDI DAW' -srcfolder "$stage" \
  -format UDZO -fs HFS+ "$dmg"
codesign --force --sign - --timestamp=none "$dmg"
codesign --verify --strict --verbose=2 "$dmg"
hdiutil verify "$dmg"
mount=$(mktemp -d "${TMPDIR:-/tmp}/arranger-mount.XXXXXX")
trap 'hdiutil detach "$mount" >/dev/null 2>&1 || true' EXIT
hdiutil attach "$dmg" -readonly -nobrowse -noautoopen -mountpoint "$mount"
codesign --verify --deep --strict --verbose=2 "$mount/Arranger MIDI DAW.app"
test "$(readlink "$mount/Applications")" = /Applications
hdiutil detach "$mount"
rmdir "$mount"
trap - EXIT
(cd "$out" && shasum -a 256 ArrangerMidiDaw-macOS.zip > ArrangerMidiDaw-macOS.zip.sha256)
(cd "$out" && shasum -a 256 ArrangerMidiDaw-macos-arm64.dmg > ArrangerMidiDaw-macos-arm64.dmg.sha256)
# stage is an explicit mktemp-created directory, never a user-supplied target.
rm -r "$stage"
