# MIDI DAW build plan

## Summary

Build it in **C++ with JUCE**, macOS first with Windows and Ubuntu alongside, and ship a usable MIDI sequencer that plays your hardware and built-in sounds in about 3 months of focused part-time work, before touching notation. Notation comes after the drum machine and looper, and plugin hosting is the last phase.

The app is one project with four views over the same data: a **Timeline** (tracks and piano roll), a **Score** view, a **Beat** view (drum machine), and a **Clip grid** (loop launcher). Everything is MIDI first; audio recording is a later add-on.

Key calls:

- **Language:** C++ / JUCE, not Rust. Plugin hosting is the deciding factor (see Tech stack).
- **Plugins:** VST3 (plus AU on Mac), added as the last phase. Until then, sound comes from your hardware and built-in instruments. VST2 only if you already hold a VST2 SDK licence. CLAP as a bonus later.
- **Platform:** macOS first (Apple Silicon), with Windows 10/11 and Ubuntu LTS built from the same code from day one. Build all three in CI so nothing drifts.
- **Look and feel:** original design, Reaper-like speed (small binary, instant startup, no bloat), not a clone of any product.

## Tech stack

**Recommendation: C++20 with JUCE 8.** Rust is a great language for the audio engine, but it loses on the two things this app lives or dies on: hosting plugins and drawing a complex editor.

|  | C++ / JUCE | Rust |
| --- | --- | --- |
| VST3 hosting | Built in, battle-tested in shipping DAWs (Tracktion, many others) | No mature host library. You'd bind the VST3 SDK yourself and debug every plugin quirk |
| VST2 hosting | Supported if you have the old SDK | Same SDK problem, plus bindings |
| Plugin editor windows | Handled for you on macOS, Windows and Linux | Manual window embedding per OS |
| GUI for a DAW | Mature, fast custom drawing, used by many DAWs | Young toolkits (egui, iced, Slint); none proven for a full DAW |
| Notation drawing | 2D graphics + SMuFL fonts work fine | Same, but less ecosystem |
| Real-time safety | Discipline required | Compiler helps a lot |
| Licence | Free personal tier under the revenue cap, or AGPLv3 | Mostly MIT/Apache |

Rust only makes sense as a hybrid later (for example a Rust DSP core behind a C++ shell), and that adds build pain for little gain on a solo project.

The rest of the stack:

- **Build:** CMake for all three. Xcode on macOS, Visual Studio 2022 on Windows, GCC or Clang on Ubuntu. GitHub Actions builds every platform on each push.
- **Audio I/O:** JUCE device layer: Core Audio on macOS, ASIO and WASAPI on Windows, ALSA and JACK (PipeWire) on Ubuntu.
- **MIDI I/O:** JUCE MIDI: Core MIDI on macOS, Windows MIDI Services on Windows, ALSA on Ubuntu.
- **Notation font:** Bravura (SMuFL, open licence). Own layout engine, see Architecture.
- **Import/export:** Standard MIDI File (built into JUCE), MusicXML (own reader/writer), PDF from the score view.
- **Project file:** a single JSON or SQLite file. Readable, diffable, easy to version.
- **Tests:** Catch2 for engine logic, plus a headless render test that bounces a project and compares output.

## Architecture

One shared song model feeds every view, and one real-time engine plays it. The views never touch the audio thread directly; they send edits, the engine picks up a fresh snapshot.

(The online doc has a diagram here: 4 views, 1 model, 1 engine.)

**Keeping JUCE replaceable.** JUCE is the shell, not the foundation. The code is split into four layers, and only the outer two know JUCE exists:

| Layer | What lives there | Depends on JUCE? |
| --- | --- | --- |
| core | Song model, timing, sequencing, clip launching, notation layout, device mapping, project file | No. Plain C++20, builds and tests on its own |
| engine | Real-time graph, our own processor interface, buffers, lock-free queues, built-in instruments | No |
| platform | Audio device, MIDI ports, plugin hosting, file dialogs, each behind a small interface we define | Yes, as one implementation |
| ui | Windows, piano roll, score, grids. Score and piano roll draw through our own thin 2D canvas interface | Yes |

CI enforces the rule: the core and engine targets must build without JUCE. A custom core later (for example RtAudio and RtMidi for devices, the VST3 SDK directly for plugins) means rewriting the platform layer only. The cost is a bit more wrapper code up front.

**Song model.** Tracks, MIDI clips, notes, tempo and time signature maps, markers. Each note is stored once in ticks (960 per quarter). Piano roll, score, drum grid and clip grid are just different editors over the same notes. Full undo/redo from the start.

**Audio engine.** A fixed audio callback that never allocates, locks or waits. The UI builds an immutable "play graph" (tracks, plugins, routing) and swaps it in with an atomic pointer. Plugins render in a dependency-ordered graph; multi-core processing comes later, once single-core is solid. Plugin delay compensation from day one.

**MIDI engine.** Sample-accurate scheduling of note and CC events into each plugin's buffer. Handles input recording, MIDI thru, quantise on input, and sending to external hardware ports (the PSR-S970 and D-05 can be played as sound modules, not just controllers).

**Built-in SoundFont player.** A GM/GS player built on FluidSynth (LGPL, cross-platform) that loads your own SF2 files (and compressed SF3). Each track picks a SoundFont and a preset by name. It follows GS conventions in the spirit of a Sound Canvas: bank-select variation sounds, drum kits on channel 10, GS reset, and common NRPNs, so old GM/GS MIDI files play back as intended. It lives in the engine as a normal processor, so a plugin can replace it per track later. The old Roland VSC plugin isn't an option: it's discontinued and 32-bit Windows only, so it won't run on a modern Mac.

**Mixer view.** A console-style mixer with one channel strip per track, built in Phase 1 and grown later.

- **Phase 1:** fader, pan, mute, solo, record arm, track colour and name, level meters, and a master strip. For hardware tracks (PSR-S970, D-05, XioSynth) the fader and pan send MIDI volume and pan (CC7 and CC10), and two send knobs drive the synth's own reverb and chorus (CC91 and CC93). SoundFont tracks get real audio faders and meters.
- **Hands-on:** a mixer page on the Launchkey Mini MK3 maps its 8 knobs to 8 faders or pans, with bank left and right. The XioSynth 49 gets its own ready-made mixer page too: its knobs control faders or pans in banks, and the XY pad controls pan and reverb send of the selected track. It works out of the box through a XioSynth control template (fixed CCs) that ships with its profile. The Behringer X-Touch Mini gets one as well: 8 encoders with LED rings for volume (push to switch to pan), the top button row for mute, the bottom row for solo, the fader for master volume, and layer B for reverb and chorus sends. MIDI Learn is only needed for custom layouts.
- **Phase 6 (audio):** audio faders and meters on every track, group buses, and send/return effect channels.
- **Phase 7 (plugins):** insert slots for VST3 and AU effects on each strip.
- **Phase 8:** automation of faders, pans and sends.

**Plugin host.** Scans VST3 folders in a separate process so a crashing plugin can't kill the app. Plugins load in-process at first (simplest, fastest). Plugin state saves into the project. Out-of-process "sandbox" hosting is a later option for flaky plugins.

**Score view.** An original engraving engine on top of the same notes:

1. Interpret: turn raw MIDI timing into readable rhythms (quantise for display only, split and tie at bar lines, detect tuplets, choose spelling like F# vs Gb from the key).
2. Lay out: voices, stems, beams, rests, accidentals, then spacing and line breaks.
3. Draw with the Bravura font. Edits in the score write back to the notes.

Start with single-staff and grand-staff piano scores, then multi-staff, lyrics and chord symbols. Export to PDF and MusicXML.

**Beat view (drum machine).** A step sequencer that writes a normal MIDI clip, so drum patterns also show in piano roll and score (as drum notation). Includes a simple built-in sample player with 16 pads, per-step velocity, probability, swing and flam. Any VST drum plugin can replace the built-in sampler.

**Clip grid (looping deck).** Tracks are columns, scenes are rows. Each cell holds a clip that loops. Launches are quantised to the next bar or beat. Supports record-into-empty-slot, overdub, and follow actions. You can record a clip-grid performance into the Timeline, so jamming turns into an arrangement.

**Style editor (arranger styles).** My reading: create and edit accompaniment styles for the PSR-S970 and save them as Yamaha style files (.sty) it can load. A style is shown as a grid of sections (Intro A to D, Main A to D, Fill-ins, Breaks, Endings) by style parts (Rhythm 1 and 2, Bass, Chord 1 and 2, Pad, Phrase 1 and 2), and each cell is a normal MIDI clip you edit in the piano roll.

- **Convert from MIDI:** pick bars of a MIDI file or Timeline, mark them as sections, map tracks to style parts, and set the chord each part was written in. The app suggests sections from markers and repeated patterns.
- **Chord following:** each part gets Yamaha-style rules for how notes move when the chord changes (for example, bass follows the root, chords re-voice). Play chords on the PSR-S970 or XioSynth and the app plays the style back so you hear it before exporting.
- **Round trip:** open existing .sty files to edit or learn from, save new ones, and send them to the keyboard (USB stick or file transfer).
- **Preview on the PSR-S970 itself:** the editor sends each style part over MIDI to the keyboard (style channels 9 to 16, with bank and program for each voice), so you hear its own voices and drum kits while you edit. The app reads the chords you play with your left hand from the keyboard's MIDI out and follows them live. Setup: the keyboard's own accompaniment off and Local Control off, so it doesn't double the notes. The app's chord following imitates the keyboard's; the final check is loading the saved .sty on the keyboard, since its built-in engine has the last word.

**Device profiles (synth and controller mapping).** Every synth and controller is described by a profile file, not by code, so adding a new synth means adding a file. Profiles are YAML (readable, allows comments), checked against a published schema. Starting profiles:

| Device | Role | What the profile covers |
| --- | --- | --- |
| Yamaha PSR-S970 | Main keyboard and sound module | 61 keys, sliders and buttons it sends, voice list with bank select (MSB/LSB/program) so you pick its sounds by name per track |
| Novation XioSynth 49 | Second keyboard, synth and audio interface | 49 keys, knobs, XY touchpad, pitch and mod wheels; a ready-made mixer page and control template out of the box; its own synth usable as a sound module |
| Roland D-05 | Sound module | Patch list by name, parameter CCs, SysEx patch dump and restore saved with the project |
| Novation Launchkey Mini MK3 | Hands-on control | Uses its DAW mode: 16 pads launch clips and scenes or play drums, 8 knobs map to mixer or params, transport buttons, pad colours mirror clip state |
| Behringer X-Touch Mini | Mixer controller | Ready-made mixer page in its standard MIDI mode: 8 encoders with LED ring feedback, 16 buttons for mute and solo, fader for master, layers A and B, banks of 8 tracks |

Each profile has five parts:

- **Identity:** name, ports, how to recognise it (USB port name, SysEx identity reply).
- **Dialect:** how this device speaks MIDI. Yamaha and Roland differ, so this is per device, never a global setting. It covers bank select style (Yamaha XG-style banks vs Roland GS variations), how drum kits are chosen (Yamaha picks drum voices by bank; Roland picks a kit by program on channel 10), the reset message (XG System On vs GS Reset), and SysEx format (Roland data-set messages need a checksum; Yamaha uses its own header and layout).
- **Sounds:** voice and patch lists with names and categories, so a track shows "Strings > Orchestra" instead of raw bank and program numbers.
- **Controls:** what keys, knobs and pads send, and what the device accepts back (pad colours, LEDs).
- **Parameters:** named synth settings with ranges and the message that sets each (CC, NRPN or a SysEx template).

Making it easy to use:

- **MIDI Learn:** click a control in the app, move a knob, done. Saved as your own layer on top of the profile.
- **Base profiles:** Generic GM, GS and XG. A new synth starts from the closest one and only lists what differs.
- **In-app profile editor:** browse and audition sounds, rename, fix a mapping, no file editing needed.
- **Import:** read existing instrument definition files (Cakewalk .ins style) that already exist for many synths.
- **Test mode:** sends test notes, program changes and a reset, so you hear right away if a profile is correct.

**Prepared for AI** (confirmed: profiles an AI can write). Because the format is open, documented and schema-checked, an AI assistant can draft a profile from a synth's MIDI implementation chart (the PDF in the back of every manual). The app validates it and runs test mode before it's used, so a wrong guess can't do harm. Later, the same profile and command interface could let an assistant do mapping tasks inside the app.

## UI direction

Modern and good-looking, but drawn cheaply: everything is vector, nothing redraws unless it changed, and there are no heavy image assets.

**Look**

- Original design. Taste references: the warm, polished studio feel of LUNA for the app, and the clean classic engraving of Sibelius for the score. Warm charcoal dark theme by default (plus a light theme), soft top-lit panels, a copper accent, and one muted colour per track that follows it through every view.
- Clean sans-serif UI font. The score page looks like printed music: cream paper, serif title and tempo text, Bravura for notation, generous spacing. Everything on a 4/8 px grid so it feels calm and aligned.
- Real-looking knobs only where they help (synth parameters); elsewhere simple sliders and numbers.
- Short, purposeful motion only: clip launch countdowns, playhead, pad feedback. Nothing that waits on an animation.
- Themes are a file (colours, sizes, spacing), so the look can be tuned without code.

**Layout**

- One window. Timeline, Score, Beat and Clip grid are tabs or split panes over the same song. A side inspector shows whatever is selected.
- Keyboard first: every action has a shortcut, and a command palette finds any action by typing.
- Crisp on Retina and high-DPI screens because nothing is a bitmap.

**Speed targets** (goals to test against, not measured yet)

| Target | Goal |
| --- | --- |
| Cold start | Under 1 second |
| Scrolling and zoom, 100 tracks | Smooth at the display's refresh rate |
| Idle CPU, nothing playing | Close to 0% (no redraws when nothing moves) |
| App download | Under about 30 MB |

GPU-backed drawing where it pays off (piano roll, score, meters), with only the changed regions repainted. Before any real UI code, Phase 0 produces a style guide and a clickable mockup to agree on the look.

## Milestones

Each phase ends with something you can actually make music with. Durations assume one person working part-time; they're rough guesses, not estimates from data.

1. **Phase 0: Skeleton (2 to 3 weeks).** CMake project, JUCE app window, audio and MIDI device setup on macOS, CI building Windows and Ubuntu too, a metronome that stays in time. Project save/load. UI style guide and mockups.
2. **Phase 1: Usable sequencer, v0.1 (2 to 3 months).** Tracks, MIDI recording from the PSR-S970 or XioSynth, piano roll, quantise, loop, undo, Mixer view with faders. Sound comes from your hardware (PSR-S970, D-05, XioSynth, with voice and patch names) plus the built-in SoundFont player loading your SF2 files, so it also makes sound on a laptop alone. MIDI file import/export. **This is the first version you use daily.**
3. **Phase 2: Beat view, Clip grid and Slicer (2 to 3 months).** Step sequencer with built-in sample player. Sample slicer: load a loop, chop it by transients, beat grid or by hand, then play slices from pads, the Beat view or as a MIDI clip. Its transient detector is built as a shared module, so drum audio to MIDI in Phase 6 reuses it. Clip grid with quantised launch, scenes, overdub. Launchkey Mini MK3 profile with pad colours. Record a jam into the Timeline.
4. **Phase 3: Arranger style editor (2 to 3 months).** Build and edit PSR-S970 accompaniment styles, convert MIDI into a style, preview styles live on the PSR-S970 over MIDI with chord following, and save Yamaha .sty files the keyboard can load. Details under Architecture.
5. **Phase 4: Score view, read-only (2 to 3 months).** Display any track as clean notation: rhythm interpretation, spelling, beams, ties, grand staff. Print and PDF export.
6. **Phase 5: Score editing (2 to 3 months).** Enter and edit notes in the score with mouse and keyboard, step entry from a keyboard, dynamics, articulations, lyrics, chord symbols. MusicXML import/export.
7. **Phase 6: Audio tracks (2 to 3 months).** Record audio (for example the PSR-S970's audio out or the XioSynth's built-in interface), waveform clips, cut, fade, crossfade, gain, comping takes, and time-stretch to follow tempo. Audio clips also work in the Clip grid. Audio to MIDI: single-note lines (vocal, bass, lead) convert with a pitch tracker (pYIN, no AI needed); drum hits reuse the slicer's transient detector and sort hits into kick, snare and hats; chords and full mixes use an open AI model (for example Basic Pitch via ONNX Runtime) and give a draft to clean up in the piano roll.
8. **Phase 7: Plugin hosting (2 to 3 months).** VST3 and Audio Unit scanning, instruments and effects, editor windows, plugin state in the project, delay compensation, crash-safe scanning. CLAP if wanted.
9. **Phase 8: Polish and depth (ongoing).** Automation lanes, multi-core engine, plugin sandboxing, signed and notarised macOS installer, themes, keyboard-shortcut editor.

Plugins come last, but the engine is built for them from Phase 0: the built-in SoundFont player, drum sampler and slicer use the same processor interface a plugin will. Adding hosting later plugs into that slot instead of reworking the engine.

## Risks

| Risk | Why it matters | What we do |
| --- | --- | --- |
| Notation engraving | Turning played MIDI into readable notation is a research-grade problem; it's where most of the effort goes | Ship read-only first, keep a "display quantise" setting per track, accept imperfect output early |
| VST2 licensing | Steinberg stopped licensing the VST2 SDK in 2018; new developers can't legally get it | VST3 only unless you already hold a VST2 licence. Most current plugins ship VST3 |
| Real-time audio | One lock or allocation on the audio thread causes clicks and dropouts | Strict rules: lock-free queues, preallocated buffers, a debug mode that flags violations |
| Plugin crashes | A bad plugin can take the whole app down | Scan in a separate process; add sandboxed hosting later |
| Scope creep | A DAW, a notation app, a drum machine and a looper are each big products | Stick to the phase order; each phase must be usable before the next starts |
| JUCE licence | Free tier has a revenue cap; above it you pay or go AGPL | Fine for personal use; revisit only if you sell it |
| Controller details | Exact messages vary by model and firmware | Profiles are data files, so fixing one never needs a rebuild |
| Yamaha style format | A .sty is a MIDI file plus Yamaha's chord-rule data. The MIDI part is well understood; the chord-rule part is only partly documented, and newer variants mostly come from community reverse engineering | Start by reading and re-saving existing PSR-S970 styles byte for byte, test every export on the keyboard, and keep the style model our own so the file writer can change |

Linux note: many commercial plugins have no Linux version, so on Ubuntu expect a smaller plugin set (open-source and Linux-native VST3s). The app itself will run the same on all three.

## Open questions

- Intel Mac or Apple Silicon? The plan assumes Apple Silicon.
- Any VST2-only plugins you can't live without? This only matters at Phase 7 now.

---

Source: the [MIDI DAW build plan](https://claude.ai/artifact/NHoz54R43Q6G3U5adCbr9e) doc (Nov 12, 2026). The online doc is the live version.
