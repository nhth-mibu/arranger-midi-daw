## Lazy Arranger Midi DAW v0.4.0

This release adds the L6max mixer page, menu languages and XioSynth patch
names, and fixes bugs with hardware synths, the Clip grid and MusicXML export.

### New

- **L6max mixer page** (`Track > L6max Mixer...`): one card for each channel
  with EQ, sends, pan, level, mute and the MONO or USB button. Effect type,
  compressor and scenes A-D are below. `Compact` puts each channel on one row.
  `Reset` asks before it changes the mixer. The page shows the last values that
  the app sent, not the values on the mixer.
- **Languages**: `Options > Language` has Danish, French and Vietnamese for the
  menus, view tabs and header tooltips. Restart the app after you change it.
- **XioSynth patch names**: the 200 factory patches (Bank A and Bank B) have
  their names in the sound list.
- **Clip grid toggle**: click a playing clip, or push its Launchkey pad, to
  stop it. Click a waiting clip again to cancel the wait. The clip that plays
  continues.

### Fixes

- Hardware synths go silent when the audio device stops or restarts, for
  example when you change the audio input.
- A track that you move to a synth with one MIDI channel (Roland D-05,
  Novation XioSynth) gets that synth's receive channel. This works in the
  Inspector, the AI chat and Fit to My Synths.
- D-05 `Get Sound Names` sends the bank select as separate MIDI messages. It no
  longer stays at 0%.
- The View menu ticks show the current view and theme.
- MusicXML export of a song with no MIDI notes shows an error and keeps the old
  file.

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

Full changelog: https://github.com/nhth-mibu/arranger-midi-daw/compare/v0.3.4...v0.4.0
