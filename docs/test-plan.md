# Test plan

Goal: make the app robust before any new feature (feature freeze, 19 November 2026).

## Where we are now

- 299 test cases in 41 files (Catch2). CI runs them on the Mac mini on every push.
- Good coverage: styles (.sty/.prs), MCP tools (65 of 73 have a test), notation, audio tracks, racks and buses, automation, theory, AI helpers.
- Weak or missing:
  - **No fuzzing.** File readers are tested only with good files and a few broken ones.
  - **No sanitizer builds** (AddressSanitizer, ThreadSanitizer, UndefinedBehaviorSanitizer).
  - **Engine and transport**: 4 tests. Real-time playback under load is not tested.
  - **Undo/redo**: 3 tests on the document. Most edit commands have no undo test.
  - **Plugin hosting**: the two test VST3 plugins are built, but no automated test loads them.
  - **UI**: only CI screenshots. Nothing checks that menus and commands work.
  - **No autosave or crash recovery** (found while writing this plan). Now added: unsaved work is copied every minute and offered at the next start after a crash.

## Progress

- **P1 done** (PR 94): kitchen-sink round-trip, undo/redo for every edit, ASan/UBSan and TSan builds. Found 1 bug (signed overflow in the Arrangement Doctor).
- **P2 done** (this PR): fuzzer for 9 readers and MCP, file edge cases, engine stress, no allocations on the audio thread. Found 1 bug (style reader read past an array on a bad note number). Not done here: SoundFont (.sf2) and AIFF fuzzing (third-party readers: FluidSynth and JUCE), and item 7 (needs the app; moved to the UI smoke test, item 16).

## Risk order

1. Lose or damage the user's work (save, load, undo).
2. Crash or hang (bad files, plugins, audio thread, shutdown).
3. Wrong sound or timing (render, playback, latency).
4. Wrong data to hardware (device profiles, SysEx, styles to the PSR).
5. Wrong results that are easy to see and fix (score layout, chord names, AI output).

## Part A: automated tests (Claude, in the cloud on Linux)

Each line is one PR. Do them in this order.

### P1: protect the user's work

| # | Area | Add |
|---|------|-----|
| 1 | Project files | Round-trip a "kitchen sink" song that uses every feature (all track types, clips, takes, crossfades, automation, buses, plugins, styles, score marks, chords, lyrics). Save, load, save again: the two files must be equal. Open old files from each released version (v0.1.0, v0.1.1, v0.2.1) kept as fixtures. |
| 2 | Undo/redo | For every edit command in SongEdit and every MCP edit tool: do, undo, redo. The song must match the "before" and "after" versions exactly. Then a random sequence of 1,000 edits, undo all, compare with the start. |
| 3 | Sanitizer CI job | Linux build with AddressSanitizer + UndefinedBehaviorSanitizer, and one with ThreadSanitizer. Run the full test suite in both. Fix what they find. |

### P2: no crashes on bad input

| # | Area | Add |
|---|------|-----|
| 4 | Fuzzing | Fuzz targets (our own mutation fuzzer, as Apple Clang has no libFuzzer) for: project file, MIDI file, MusicXML, .sty/.prs (SFF1/SFF2, CASM), device and controller profile YAML, synth editor YAML, SoundFont (.sf2), WAV/AIFF sample reader, L6max folder import, MCP JSON requests. Seed each with the files we have. Run 10 minutes per target in a nightly job; keep every crash as a regression test. |
| 5 | Edge cases | Empty song, one-tick notes, 1,000 tracks, 100,000 notes, tempo 20 and 400 bpm, 1/32 and 13/8 time, very long audio files, missing audio files, Unicode and spaces in paths, read-only folders, full disk (write fails cleanly, old file stays). |
| 6 | Engine stress | Play/stop/locate/loop 10,000 times while edits come in from another thread. Change the song during playback. Start and stop the audio device many times. Check no allocations and no locks on the audio thread (count with a custom allocator in tests). |
| 7 | Shutdown and async | Close a project while an AI tool, analysis, plugin scan or render is running. Open a second project during playback. Nothing crashes, nothing leaks (ASan). |

### P3: correct sound and timing

| # | Area | Add |
|---|------|-----|
| 8 | Render checks | Render reference songs offline (built-in SoundFont, sampler, drum machine, audio clips). Compare with stored "golden" renders: peak, RMS and onset times within a small tolerance. Checks that a code change did not change the sound by accident. |
| 9 | Timing | MIDI out timestamps match the song to the sample, with tempo changes, loops and delay compensation. Clip grid launches start on the bar. Metronome lines up with notes. |
| 10 | Plugin host | Load the two test VST3 plugins in a headless test: scan, insert, process, automate a parameter, save and restore plugin state, remove. Add a third test plugin that crashes, hangs or reports a wrong latency: the scan must survive it. |
| 11 | MIDI round-trips | Export and import MIDI and MusicXML for every demo song and the kitchen-sink song. Read MusicXML files from MuseScore, Sibelius and Finale (public samples). |

### P4: correct data and results

| # | Area | Add |
|---|------|-----|
| 12 | Device profiles | Every profile loads and passes the schema. Bank select, program change, drum kit and reset messages match the manual's MIDI chart (tables in tests, one per device). |
| 13 | Styles | Write then read every style feature byte for byte. PSR export: files the PSR-S970 can open (check the format rules we know). |
| 14 | MCP tools | Tests for the 8 tools without one: find_chords, get_beat, remove_track, set_track, style_from_audio, get/play/save_style_from_audio. Every tool with missing, wrong and extreme arguments gives an error, never a crash. |
| 15 | Score | Layout snapshots (positions of notes, beams, ties) for a set of hard bars. PDF export opens and has the right page count. |
| 16 | UI smoke test | Start the app headless, run every menu command once on the demo song, and check it does not crash. Screenshots of every view in both themes, compared with the last good ones. |

### How we run it

- Normal tests: every push (as now).
- Sanitizer builds and UI smoke test: every PR, on Linux in the cloud or the WSL runner.
- Fuzzing and render checks: nightly, plus before a release.
- Every bug found gets a test first, then the fix.

## Part B: manual checklist (Nhat only)

Short sessions are fine. Write down what fails; Claude makes a test for it if it can.

### Hardware (about 15 minutes per device)

- [ ] **PSR-S970**: play each part type through the app. Change voices (bank and program). Send a style made in the app to the keyboard and play it with chords. Check guitar parts sound right (NTR/RTR are approximate). Load a style exported by the app on the keyboard.
- [ ] **Roland D-05**: sound editor changes the sound you hear. Reset and drum kits work.
- [ ] **XioSynth 49**: sound editor, mixer page (knobs move the right faders), 8 voices.
- [ ] **Launchkey Mini MK3**: mixer page, transport buttons, chord pads, Clip grid pads light up.
- [ ] **X-Touch Mini**: mixer page, knob rings follow the app.
- [ ] **L6max**: record with several inputs, import an SD card recording, check Scene D (program change 3).
- [ ] Unplug and plug back each device while the app plays. The app must not crash and must find it again.

### Plugins (about 30 minutes)

- [ ] Scan your real plugin folder. Note any plugin that fails or is missing.
- [ ] Use 3 to 5 plugins you use most: instrument and effect, open their windows, automate one control, save, close, reopen.
- [ ] One heavy session: 20+ tracks with plugins. Note CPU and any clicks.

### Computers

- [ ] **macOS (Mac mini)**: install from the DMG, first start, open the demo song, play, save, quit, reopen.
- [ ] **Windows**: same.
- [ ] Linux (WSL or real): only if you will use it.

### Audio and feel

- [ ] Latency: play a note on the keyboard and listen through a plugin synth at buffer sizes 64, 128, 256. Note the smallest buffer with no clicks.
- [ ] Record audio and MIDI together for 10 minutes. Check they stay in time.
- [ ] Use the app for one real song from start to end. Write down anything that is slow, confusing or wrong.

## Done when

- All P1 and P2 items are in and green; sanitizer and fuzz jobs run clean for a week.
- Nhat's checklist has no open "crash" or "lost work" item.
- Then the freeze can end.
