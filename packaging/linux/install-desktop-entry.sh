#!/bin/sh
# Adds Arranger MIDI DAW to the application menu of the current user, with its icon.
# Run it from the extracted folder: ./install-desktop-entry.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
apps="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
icons="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor"
mkdir -p "$apps" "$icons/256x256/apps" "$icons/1024x1024/apps"
cp "$here/arranger-midi-daw-256.png" "$icons/256x256/apps/arranger-midi-daw.png"
cp "$here/arranger-midi-daw.png" "$icons/1024x1024/apps/arranger-midi-daw.png"
sed "s|^Exec=.*|Exec=\"$here/Arranger MIDI DAW\" %f|" "$here/arranger-midi-daw.desktop" > "$apps/arranger-midi-daw.desktop"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$apps" || true
echo "Added Arranger MIDI DAW to the application menu."
