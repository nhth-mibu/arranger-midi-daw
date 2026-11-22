## Lazy Arranger Midi DAW v0.5.0

This release adds a style remote for the PSR-S970, more D-05 editing, a
Music Finder in the AI chat, more translated panels and wider sound editor
profiles.

### New

- **PSR-S970 style remote** (`Track > PSR-S970 Style Remote...`): start the
  keyboard's own style sections (Intro, Main A-D, Fill In, Break, Ending),
  send the tempo and send chords, also with a bass note. The keyboard plays the
  style. Turn on `Style Section` and `Chord` receive in its MIDI settings. The
  style number and split point controls are not verified yet.
- **D-05 names and patch MIDI settings**: the sound editor has a `Names` page
  for the patch name and the upper and lower tone names. Click `Send Names` to
  write them to the temporary patch. The `Patch` page has Transmit CH, Separate
  Receive CH and Transmit Program Change.
- **D-05 Separate modes**: a second track on the D-05 gets the next MIDI
  channel, for patches in a Separate key mode.
- **Save and load sounds as .syx**: `Save Dump...` writes what `Read from
  Synth` received to a `.syx` file. `Load Dump...` sends a `.syx` file to the
  synth's temporary patch.
- **Music Finder in the AI chat**: ask the AI to add songs with their style,
  tempo and voices, for example "Add 'Song name' as a slow bossa at 92 BPM".
  It keeps the books in the app's Music Finder folder and can write and read
  them as CSV for a spreadsheet.
- **More languages in the panels**: the Inspector, the Mixer, the Loops view
  and the status bar are now in Danish, French and Vietnamese too.
- **Sound editor profiles**: a SysEx parameter can use more than one value
  byte (14-bit, nibbles or signed values), and a profile can choose its
  checksum. The AI can use this when it writes a profile from a MIDI chart.

### Downloads

- **macOS Universal** - `ArrangerMidiDaw-macos-universal.dmg` (arm64 + x86_64).
- **Windows x64** - `ArrangerMidiDaw-windows-x64.zip`. Requires the Microsoft
  Visual C++ v14 x64 Redistributable.
- **Linux x64** - `ArrangerMidiDaw-linux-x64-ubuntu-26.04.tar.gz`, built for
  Ubuntu 26.04 x64. Extract with `tar -xzf` to keep executable permissions.

Each package has its SHA-256 checksum next to it.

These are development packages. The Windows package has no code signature. The
macOS package has an ad-hoc signature and no notarization, so Gatekeeper may ask
for confirmation on first launch. The packages do not include a General MIDI
SoundFont.

Full changelog: https://github.com/nhth-mibu/arranger-midi-daw/compare/v0.4.0...v0.5.0
