# Building and code layout

## Build

You need CMake 3.24+ and a C++20 compiler: Xcode on macOS, Visual Studio 2022 on Windows, GCC 11+ or Clang 14+ on Ubuntu. JUCE, Catch2 and nlohmann/json are downloaded by CMake.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release        # unit tests
```

The app is in `build/src/app/ArrangerMidiDaw_artefacts/Release/`. On Ubuntu, first install the JUCE packages listed in [`.github/workflows/ci.yml`](../.github/workflows/ci.yml).

Useful options:

- `-DARRANGER_BUILD_APP=OFF` builds only the core, engine and tests, with no JUCE.
- `-DARRANGER_JUCE_DIR=/path/to/JUCE` uses a local JUCE checkout instead of downloading it.

Video audio import uses a separate FFmpeg executable on PATH or beside the app executable. Install FFmpeg on PATH before running CTest to exercise the video decoding integration tests. Those tests use small committed MP4, MKV and WebM fixtures. If FFmpeg is unavailable, decoding integration tests report a skip; the core tests still run. FFmpeg is not downloaded by CMake.

The app also takes `--screenshot out.png [--theme light] [--position ticks] [--track-height px] [--view score] [--track name] project.arranger` to render its window to an image without opening any device. `--export-pdf out.pdf [--track name] project.arranger` writes the score of a track as a PDF, and `--export-musicxml out.musicxml project.arranger` writes the score of all tracks. `--score-keys "n5cde"` types keys into the Score view before a screenshot, and `--score-marks` adds sample lyrics, a chord symbol and a dynamic. `--audio file.wav` puts a WAV file on a new audio track, and `--audio-to-midi drums` (or `melody`) converts it. `--automation volume` (or `pan`, `reverb`, `chorus`, or `plugin` for the first control of the first effect) with `--track name` shows an automation line with sample points. `--buses` adds a sample group bus and a reverb return. `--shortcuts` with `--screenshot` renders the Keyboard Shortcuts window. `--scan-plugins [folder]` scans for plugins and prints the list. `--render out.wav [--seconds n] project.arranger` plays a project to a WAV file with no audio device. Configure with `-DARRANGER_BUILD_TEST_PLUGINS=ON` to build two small VST3 plugins (an effect and an instrument) for trying the plugin host. CI uses this to show the app on all three systems.

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
