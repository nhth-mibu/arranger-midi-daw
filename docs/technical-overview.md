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

## AI music tools

`core/MidiAi` turns a tool call (`ai_humanize`, `ai_drumify`, `ai_continue`, `ai_write_part`, `ai_melody`) into a JSON request for a helper program and puts the helper's notes back into the song as one undo step. It has no model code, so it is tested with canned answers. The helpers run outside the app: `resources/ai/arranger_magenta.js` runs Magenta.js (GrooVAE, MusicRNN, ImprovRNN) in Node.js, and `resources/ai/arranger_generate.py` runs the Anticipatory Music Transformer (Thickstun et al., Apache 2.0) with PyTorch in the app's Python environment (`PythonTools`). Its sampling loop is a copy of the project's with one change: a part can be limited to some instruments, so "write a bass" only writes bass. `MidiAiService` installs the helpers and their models into `<app data>/AI` and runs them on a worker thread; MCP calls of these tools run their helper on the connection thread, so the window keeps working.

## More detail

See the [build plan](build-plan.md) for phases, UI direction, risks and open questions.
