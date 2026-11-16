## Lazy Arranger Midi DAW v0.1.1

This release adds version information and update checks. It also refreshes the
documentation.

### About and updates

- `Options > About Lazy Arranger Midi DAW` shows the installed version and the
  public release page.
- The app checks the public release page after it starts. When a newer version
  is available, the app shows `Download`, `Later` and `Skip This Version`.
- `Download` opens the public release page in your browser. `Later` asks again
  at the next start. `Skip This Version` hides that version until a newer one is
  available.
- `Options > Check for Updates...` checks immediately and reports the result.
- The update check reads only the public release page. It sends no song data.

### Documentation

- The user manual has an **About and updates** section.
- The callout boxes, arrows and text boxes in the manual screenshots now align.
- The feature sheet shows the current app. It covers AI assist, skills,
  speech input and output, synth panels, styles, YAML device profiles, mixing and
  scoring.

### Downloads

- **macOS Universal** - `ArrangerMidiDaw-macos-universal.dmg` (arm64 + x86_64).
- **Windows x64** - `ArrangerMidiDaw-windows-x64.zip`. Requires the Microsoft
  Visual C++ v14 x64 Redistributable.
- **Linux x64** - `ArrangerMidiDaw-linux-x64-ubuntu-26.04.tar.gz`, built for
  Ubuntu 26.04 x64. Extract with `tar -xzf` to keep executable permissions.
- **Arranger Demo Styles** - `ArrangerMidiDaw-Demo-Styles.zip`.

Each package has its SHA-256 checksum next to it.

These are development packages. The Windows package has no code signature. The
macOS package has an ad-hoc signature and no notarization, so Gatekeeper may ask
for confirmation on first launch. The built-in AI voices download on first use.
The packages do not include a General MIDI SoundFont.

The application source code stays private. Use the binaries only according to the
licences of their components.

Full changelog: https://github.com/nhth-mibu/arranger-midi-daw/compare/v0.1.0...v0.1.1
