## Lazy Arranger Midi DAW v0.1.0

The first release under the name **Lazy Arranger Midi DAW**, with AI assist.
The source stays private; the binaries are public.

### AI assist

- The assistant has two layers: code tools for the song, and **skills** for
  recipes and data. Use `find_skills` and `get_skill`.
- The built-in **Common chord progressions** skill has 25 pop, rock, jazz and
  twelve-bar blues progressions. They are written as roman numerals and work in
  any key.
- More skills can be added as JSON packs in the app's `Skills` folder, without
  rebuilding the app. Skills contain text and data only.
- The AI chat now has a **microphone** button for voice input. The built-in
  Whisper writes the words into the message box. It needs a Whisper model from
  `Options > Audio Analysis Tools...`.
- The **speaking-person** button turns spoken replies on or off. Use the system
  voice, or install the offline Kitten or Kokoro voice from `Options > Speech...`.
  Each complete reply is spoken once; the switches are global.
- The assistant prompt now knows about the new skills and the speech controls.

### Mixer

- A fader that the AI, a MIDI controller or Undo moves now glides to its new
  value with an accent-coloured glow, so the change is visible.
- A fader you drag yourself follows the mouse directly.

### Styles and first run

- On first run, the app looks for `ArrangerMidiDaw-Demo-Styles.zip` next to the
  program and offers to unpack the 540 demo styles. It sets the style folder so
  the assistant can find them by name.
- The demo style pack is attached to this release.
- The “Genos factory” wording is gone.

### Timeline and SoundFont

- MIDI notes, note lines and the gaps between parts in the Timeline are half
  the previous thickness.
- The inspector and the mixer show SoundFont program names, such as
  `Acoustic Grand Piano` or `Standard Kit`.
- An exact loop end on an audio block boundary now repeats correctly.

### Downloads

- **macOS Universal** - `ArrangerMidiDaw-macos-universal.dmg` (arm64 + x86_64).
- **Windows x64** - `ArrangerMidiDaw-windows-x64.zip`. Requires the Microsoft
  Visual C++ v14 x64 Redistributable.
- **Linux x64** - `ArrangerMidiDaw-linux-x64-ubuntu-26.04.tar.gz`, built for
  Ubuntu 26.04 x64. Extract with `tar -xzf` to keep executable permissions.
- **Arranger Demo Styles** - `ArrangerMidiDaw-Demo-Styles.zip`.

SHA-256 checksums are attached next to each package.

These are development packages. Windows is unsigned. macOS is ad-hoc signed and
not notarized, so Gatekeeper may ask for confirmation on first launch. The
built-in AI voices download on first use. The packages do not include a General
MIDI SoundFont.

The application source code is private. Use the binaries only according to the
licences of their components.

Full changelog: https://github.com/nhth-mibu/arranger-midi-daw/compare/v0.0.16...v0.1.0
