## Lazy Arranger Midi DAW v0.3.0

This release adds Groove, Song Forms, autosave and Open Recent. It also adds a
large set of tests and fixes for the problems that the tests found.

### Groove

- `AI > Groove...` takes the feel of one part and puts it on other parts.
- Take the groove from a MIDI track, an audio track, a file, or from words
  (for example `lazy swing, behind the beat`).
- Set the amount of timing and accents, and for drums the kick, snare and
  hi-hats. Compare with `Play Before` and `Play After`, then `Apply`.
- Your grooves stay in a groove library as `.groove` files.

### Song Forms

- `AI > Song Forms...` makes other orders of the song sections: a short
  version, a long intro, a gig version or a break for a solo.
- The app finds the sections from the music if the song has none.
- Each form stays in the song. The Timeline changes only when you use a form.
- Better transitions between sections, also for audio tracks.

### Safer work

- Autosave: the app keeps a copy of unsaved work each minute. After a crash it
  asks `Recover unsaved work?`.
- `File > Open Recent`, and each file dialog opens in the last folder you used.

### Tests and fixes

- New tests for round-trips, undo/redo of every edit, timing, plugin hosting,
  MIDI and MusicXML files, device profiles and all AI tools.
- A fuzzer, stress tests and sanitizer builds. Fixes for the problems they
  found.
- The groove and song form tools are also in the AI chat.

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

Full changelog: https://github.com/nhth-mibu/arranger-midi-daw/compare/v0.2.1...v0.3.0
