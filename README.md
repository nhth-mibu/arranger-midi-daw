# Arranger MIDI DAW

A personal but professional MIDI sequencer and DAW: light and fast, with real score notation.

**Status:** Phase 0 (skeleton). The app opens a window, plays a sample-accurate metronome through your audio device, shows incoming MIDI, and saves and loads projects.

![Phase 0](docs/images/phase0-dark.png)

## What it will do

- MIDI sequencing with tracks and a piano roll
- Score view with proper engraving (Bravura / SMuFL)
- Beat view (drum machine) and sample slicer
- Clip grid for launching loops
- Arranger style editor for the Yamaha PSR-S970 (.sty)
- Built-in SoundFont (SF2/SF3) player for GM/GS sounds
- Audio recording and editing
- VST3 and AU plugin hosting (last phase)
- Device profiles for Yamaha PSR-S970, Roland D-05, Novation Launchkey Mini MK3 and Novation XioSynth 49, open to any other synth

## Stack

C++20 with JUCE 8 and CMake. macOS first, also Windows and Ubuntu Linux, all built in CI. The core and engine stay free of JUCE so it can be swapped out later.

## Build

You need CMake 3.24+ and a C++20 compiler: Xcode on macOS, Visual Studio 2022 on Windows, GCC 11+ or Clang 14+ on Ubuntu. JUCE, Catch2 and nlohmann/json are downloaded by CMake.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release        # unit tests
```

The app is in `build/src/app/ArrangerMidiDaw_artefacts/Release/`. On Ubuntu, first install the JUCE packages listed in `.github/workflows/ci.yml`.

Useful options:

- `-DARRANGER_BUILD_APP=OFF` builds only the core, engine and tests, with no JUCE.
- `-DARRANGER_JUCE_DIR=/path/to/JUCE` uses a local JUCE checkout instead of downloading it.

The app also takes `--screenshot out.png [--theme light] [--position ticks] project.arranger` to render its window to an image without opening any device. CI uses this to show the app on all three systems.

## Code layout

| Folder | What | JUCE? |
| --- | --- | --- |
| `src/core` | Song model, timing, project file | No |
| `src/engine` | Real-time engine: transport, metronome, processor interface, lock-free queue | No |
| `src/platform` | Audio and MIDI devices behind our own `DeviceHost` interface; `juce/` has the JUCE version | Only `juce/` |
| `src/app` | Window, UI and theme | Yes |
| `tests` | Catch2 tests for core and engine | No |
| `resources` | Themes (JSON) and the Inter font | |

CI checks that `src/core` and `src/engine` never mention JUCE.

## Shortcuts

Space play/stop, Home or Enter back to start, C metronome, Cmd/Ctrl+N new, +O open, +S save, +Shift+S save as, +, audio and MIDI settings.

## Docs

- [Build plan](docs/build-plan.md)
- [Style guide](docs/style-guide.md)
