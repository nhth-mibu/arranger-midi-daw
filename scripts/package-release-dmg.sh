#!/bin/bash
set -euo pipefail
payload=${1:?Usage: package-release-dmg.sh payload-directory output-directory version-tag}
out=${2:?Missing output directory}
tag=${3:?Missing version tag}
[[ "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$ ]]
app='Arranger MIDI DAW.app'
test -x "$payload/$app/Contents/MacOS/Arranger MIDI DAW"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$payload/$app/Contents/Info.plist")" = "$(tag_version=${tag#v}; echo "${tag_version%%-*}")"
mkdir -p "$out"
out=$(cd "$out" && pwd)
dmg="$out/ArrangerMidiDaw-macos-universal.dmg"
test ! -e "$dmg"
stage=$(mktemp -d "${TMPDIR:-/tmp}/arranger-release-dmg.XXXXXX")
ditto "$payload" "$stage"
ln -s /Applications "$stage/Applications"
printf '%s\n' "Arranger MIDI DAW $tag - Apple Silicon and Intel" \
  'Drag Arranger MIDI DAW.app to Applications.' \
  'Ad-hoc signed; not Developer ID signed or notarized by Apple.' \
  > "$stage/INSTALL.txt"
codesign --verify --deep --strict --verbose=2 "$stage/$app"
hdiutil create -volname "Arranger MIDI DAW ${tag#v}" -srcfolder "$stage" -format UDZO -fs HFS+ "$dmg"
codesign --force --sign - --timestamp=none "$dmg"
codesign --verify --strict --verbose=2 "$dmg"
hdiutil verify "$dmg"
mount=$(mktemp -d "${TMPDIR:-/tmp}/arranger-release-mount.XXXXXX")
trap 'hdiutil detach "$mount" >/dev/null 2>&1 || true' EXIT
hdiutil attach "$dmg" -readonly -nobrowse -noautoopen -mountpoint "$mount"
test "$(readlink "$mount/Applications")" = /Applications
codesign --verify --deep --strict --verbose=2 "$mount/$app"
archs=$(/usr/bin/lipo -archs "$mount/$app/Contents/MacOS/Arranger MIDI DAW")
for arch in arm64 x86_64; do
  case " $archs " in *" $arch "*) ;; *) echo "Missing architecture: $arch" >&2; exit 1 ;; esac
done
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$mount/$app/Contents/Info.plist")" = "$(tag_version=${tag#v}; echo "${tag_version%%-*}")"
hdiutil detach "$mount"
rmdir "$mount"
trap - EXIT
(cd "$out" && shasum -a 256 ArrangerMidiDaw-macos-universal.dmg > ArrangerMidiDaw-macos-universal.dmg.sha256)
rm -r "$stage"
