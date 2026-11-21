## Lazy Arranger Midi DAW v0.3.3

This release fixes bugs with hanging notes, solo, clip launch and MIDI inputs.

### Fixes

- No more hanging notes on hardware synths and plugins. This includes notes
  after a style chord change, short notes and sustain pedal notes.
- `Stop` sends All Notes Off to each port and channel that the song uses.
  Push `Stop` again to also send sustain off and All Sound Off (panic).
- A key you hold sends its note-off to the same place as its note-on, also
  when you select a different track.
- Solo is exclusive: a click on `S` soloes one track only. Shift-click or
  Cmd-click adds a track. This works in the mixer, the track list, controller
  Solo buttons and the AI chat.
- Clip grid: when the song is stopped, a launched clip starts on the bar line.
  Parts now stay in time. `Play` in the Loops view uses the same quantize.
- MIDI inputs: a keyboard or controller that you connect turns on by itself.
  Inputs that you turn off stay off.

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

Full changelog: https://github.com/nhth-mibu/arranger-midi-daw/compare/v0.3.2...v0.3.3
