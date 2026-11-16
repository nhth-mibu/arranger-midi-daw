# Technical overview

[Back to the front page](../README.md)

## Stack

- **Language and framework:** C++20 with JUCE 8, built with CMake.
- **Platforms:** macOS first (Apple Silicon), also Windows 10/11 and Ubuntu LTS. CI builds all three on every push.
- **Audio and MIDI:** Core Audio and Core MIDI on macOS, ASIO/WASAPI and Windows MIDI Services on Windows, ALSA and JACK (PipeWire) on Ubuntu.
- **Notation:** an original engraving engine drawing with the Bravura (SMuFL) font. MusicXML and PDF export.
- **Built-in sound:** a GM/GS SoundFont player (FluidSynth) that loads SF2 and SF3 files.
- **Plugins:** VST3 and Audio Units, added in the last build phase. VST2 only with an existing licence.

## Architecture

Four views (Timeline, Score, Beat, Clip grid) edit one shared song model. One real-time engine plays it. The views never touch the audio thread; they send edits, and the engine picks up a fresh snapshot.

The code is split into layers so JUCE stays replaceable:

| Layer | What lives there | Depends on JUCE? |
| --- | --- | --- |
| core | Song model, timing, sequencing, clip launching, notation layout, device mapping, project file | No |
| engine | Real-time graph, processor interface, buffers, lock-free queues, built-in instruments | No |
| platform | Audio device, MIDI ports, plugin hosting, file dialogs, each behind our own interface | Yes, as one implementation |
| ui | Windows, piano roll, score, grids | Yes |

CI checks that `core` and `engine` build without JUCE.

## Devices

Each synth and controller is described by a device profile file (YAML, checked against a schema), not by code. A profile covers identity, MIDI dialect (for example Yamaha XG vs Roland GS banks, drum kits, reset and SysEx), sound lists by name, controls and parameters. New synths start from a generic GM, GS or XG profile. MIDI Learn, an in-app profile editor and a test mode make profiles easy to make and check, and the open format lets an AI draft a profile from a synth's MIDI implementation chart.

Starting profiles: Yamaha PSR-S970, Roland D-05, Novation Launchkey Mini MK3, Novation XioSynth 49, Behringer X-Touch Mini. The Launchkey, the XioSynth and the X-Touch Mini each get a ready-made mixer page out of the box.

## Arranger styles

The style editor builds and edits PSR-S970 accompaniment styles, converts MIDI into a style, previews it live on the keyboard over MIDI with chord following, and saves Yamaha `.sty` files. The chord-rule part of the `.sty` format is only partly documented, so the work starts by re-saving existing styles byte for byte.

## Audio analysis

Track > Analyse Audio takes a recording apart. Heavy models run outside the core, all on the user's computer:

| Part | Tool | License | Where |
| --- | --- | --- | --- |
| Stems | Demucs (htdemucs) | MIT | Python helper |
| Notes per stem | Basic Pitch (ONNX model) | Apache 2.0 | Python helper |
| Instrument names | PANNs (Cnn14) | MIT, weights CC BY 4.0 | Python helper |
| Drum hits | the app's transient detector | ours | core |
| Chords | chroma, chord templates and a Viterbi path | ours | core (`AudioAnalysis`) |
| Lyrics | whisper.cpp, word timestamps | MIT | linked into the app (`src/whisper`) |

The Python helper (`resources/analysis/arranger_analyse.py`) runs in the app's own environment (Python 3.10, made with uv; `src/app/PythonTools`). It prints `PROGRESS`, then `RESULT <json>` or `ERROR`. The core reads the JSON and puts stems, MIDI tracks, chord symbols and lyrics into the song as one undo step. The MCP tools `analyse_audio`, `get_analysis`, `get_lyrics` and `edit_lyrics` let the AI start an analysis and correct the lyrics. `scripts/install-analysis-tools.sh` sets up the tools without the app; CI uses it on the Mac runner.

Chordino and madmom were left out: Chordino is GPL, and the madmom models are for non-commercial use only.

## AI music tools

`core/MidiAi` turns a tool call (`ai_humanize`, `ai_drumify`, `ai_continue`, `ai_write_part`, `ai_melody`) into a JSON request for a helper program and puts the helper's notes back into the song as one undo step. It has no model code, so it is tested with canned answers. The helpers run outside the app: `resources/ai/arranger_magenta.js` runs Magenta.js (GrooVAE, MusicRNN, ImprovRNN) in Node.js, and `resources/ai/arranger_generate.py` runs the Anticipatory Music Transformer (Thickstun et al., Apache 2.0) with PyTorch in the app's Python environment (`PythonTools`). Its sampling loop is a copy of the project's with two changes: a part can be limited to some instruments, so "write a bass" only writes bass, and after eight notes at one instant it must move to the next 1/16 note, so a stuck time token cannot fill the range with one chord. `MidiAiService` installs the helpers and their models into `<app data>/AI` and runs them on a worker thread; MCP calls of these tools run their helper on the connection thread, so the window keeps working.

## Music theory

`core/Theory.h` holds the app's music theory, in plain C++ with no JUCE and no
Python. Many ideas come from [music21](https://github.com/cuthbertLab/music21)
(BSD licence); the code is our own.

- **Keys and modes:** major, minor and the church modes, with key signatures.
- **Spelling:** notes and chord symbols are spelled on the line of fifths around the key. Minor keys spell their raised 6th and 7th with sharps. The score spells a chromatic note in the direction it moves.
- **Key finding:** Krumhansl-Schmuckler with the Krumhansl-Kessler profiles, a certainty value, and key regions from windows of bars (for modulations).
- **Roman numerals:** chord to numeral and back, with figured-bass inversions (`V65`), secondary dominants (`V7/V`), borrowed chords (`bVII`) and the Neapolitan (`N6`).
- **Skills:** the AI’s recipes and data live in a separate layer (`Skills.h`). The built-in pack holds the pop, rock, jazz and twelve-bar blues progressions; users can add JSON packs in the app’s `Skills` folder without a rebuild. Skills contain no executable code.
- **Scales:** seven-note scales spelled one letter a note, pentatonic, blues, whole tone.
- **Voice leading:** parallel and hidden fifths and octaves, voice crossing and large leaps, after music21's `VoiceLeadingQuartet`.

The AI tools `analyze_key`, `analyze_harmony`, `get_scale`, `chords_from_roman` and
`check_voice_leading` use it.

### Skills

The assistant has two layers:

1. **Code tools** in `McpServer.cpp` for actions that need the song, the audio
   engine or a device. These are compiled, tested and released with the app.
2. **Skill packs** in JSON for recipes, genre knowledge and data. The built-in
   pack is in `Skills.cpp`; users add files to the app's `Skills` folder. A skill
   holds an id, name, description, when to use it, instructions, tags, examples
   and optional data. It never runs code.

`find_skills` searches skills; `get_skill` returns the full instructions and
data. The assistant then uses the normal code tools. Bad user files are skipped
and reported, so a broken pack does not stop the server.

## More detail

See the [build plan](build-plan.md) for phases, UI direction, risks and open questions.
