## Lazy Arranger Midi DAW v1.0.0-beta.1

This is the first beta of version 1. Version 1 is now feature complete. From
now on it gets bug fixes only.

### New

- **Yamaha Music Finder files**: ask the AI chat to import a Yamaha Music
  Finder `.mfd` file into a Music Finder book. The import keeps the song
  name, style number, time signature, tempo, genre and keywords.
- **Yamaha Playlists**: the AI chat can read a Playlist `.tsv` and write a
  Music Finder book as a Playlist. The Registration files must already be on
  the keyboard at `I:/Playlist/<song>.rgt`.
- **Select a PSR style from the AI chat**: the AI can select a style on the
  PSR-S970 by its style number, for example from a Music Finder entry.
- **D-05 user reverbs**: the D-05 sound editor has pages for the user reverb
  slots 17-32. `Read from Synth` reads them.
- **PSR-S970 XG part mixer**: the sound editor has a page for each of the 16 XG
  parts with Volume, Pan, Reverb Send and Chorus Send.
- **PSR-S900 profile**: the PSR-S900 has its own device profile with the XG
  part mixer. It has no voice list yet.

### Fixed

- Many crash and stability fixes found in a code audit, for example:
  - buttons that close their own window, track renames, menus and dialogs
    that finish after their window has closed;
  - closing the main window with other windows open;
  - a SoundFont that finishes loading after the app has started to close;
  - AI tool calls that finish after the app has started to close.
- Style chords: a style channel with a source chord other than C now plays in
  the correct octave.
- PSR song export: lyrics were written twice, the second time without the
  count-in offset.
- MusicXML import: ties in different voices or staves no longer join the
  wrong notes.
- Splitting a looping MIDI clip keeps the notes and controls in the correct
  part.
- Python AI tools stop quickly when you click Stop.
- AI API keys are now kept in their own private file (`ai-keys.json`), not in
  the settings file. The app moves existing keys there.
- Windows: the AI chat command line is safe for text with quotes, `%` and line
  breaks.
- Damaged or unusual style, SysEx, MIDI and JSON files no longer crash the app.

### Downloads

- **macOS Universal** - `ArrangerMidiDaw-macos-universal.dmg` (arm64 + x86_64).
- **Windows x64** - `ArrangerMidiDaw-windows-x64.zip`. Requires the Microsoft
  Visual C++ v14 x64 Redistributable.
- **Linux x64** - `ArrangerMidiDaw-linux-x64-ubuntu-26.04.tar.gz`, built for
  Ubuntu 26.04 x64. Extract with `tar -xzf` to keep executable permissions.

Each package has its SHA-256 checksum next to it.

This is a beta. The Windows package has no code signature. The macOS package
has an ad-hoc signature and no notarization, so Gatekeeper may ask for
confirmation on first launch. The packages do not include a General MIDI
SoundFont.

Full changelog: https://github.com/nhth-mibu/arranger-midi-daw/compare/v0.5.0...v1.0.0-beta.1
