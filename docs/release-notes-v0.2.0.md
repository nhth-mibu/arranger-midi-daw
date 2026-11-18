## Lazy Arranger Midi DAW v0.2.0

This release adds smart arranging tools, chord input, Style from Audio, Kids
Mode and support for the Zoom LiveTrak L6max.

### Arranging help

- `AI > Arrangement Doctor...` checks the arrangement for a goal and proposes
  changes. Listen to each proposal with `Play A` and `Play B`, then select `Use`.
- `AI > Fit to My Synths...` fits the parts to the voices and ranges of your
  synths (PSR-S970, D-05, XioSynth).
- `Check the Transcription` asks about the bass notes and chords that the
  analysis is not sure about. Listen to the choices and select the one you hear.
  Open it from the analysis window, `Track` or `AI`.

### Chords

- A chord lane with suggestions for the next chord.
- Play a chord on the keyboard to write it.
- Chord pads on the Launchkey, with inversion, voicing and octave.
- Reharmonize a passage and compare the options with A/B.

### Styles

- `Style > Style from Audio...` makes a PSR style from a recording. It compares
  the style with the recording and refines it until they are alike.
- `From Link...` gets the recording from a YouTube or other link. It uses your
  own `yt-dlp` and `ffmpeg`.

### Kids Mode

- `Transport > Kids Mode` (Cmd+Alt+K): any key on any MIDI keyboard plays the
  song. The song pauses when the keys stop.

### Zoom LiveTrak L6max

- A device profile for the L6max.
- Select the audio input for each track.
- `File > Import L6max Recording...` imports a recording from the SD card.

### Other changes

- Import the audio from a video file.
- The panels sit flush, with no gaps.
- The XioSynth profile has 8 voices.
- Fixes to playback, AI and MIDI editing.

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

Full changelog: https://github.com/nhth-mibu/arranger-midi-daw/compare/v0.1.1...v0.2.0
