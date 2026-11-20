# After the feature freeze

The feature freeze started on 19 November 2026. It ends when all the tests in
[test-plan.md](test-plan.md) are green. Do not start these items before then.
Bug fixes in the current features are permitted during the freeze.

Each item is one pull request, unless the item says something different.

## 1. Zoom LiveTrak L6max

Compared with
[zoom-l6-companion](https://github.com/philmillman/zoom-l6-companion) (MIT,
WebMIDI) on 20 November 2026. Today our profile
(`resources/devices/zoom-l6max.yaml`) controls only the EQ, scenes A-D, the
sound pads (by note), the 14 USB inputs and the SD card import.

1. **Full CC map.** Add these factory CC numbers to the profile. CH1-8 are in
   order. Confirm them on the unit or in ZOOM L6 Editor > MIDI CC# Mapping.

   | Control | CC |
   |---|---|
   | EQ High | 1-8 |
   | EQ Mid Freq | 11-18 |
   | EQ Mid | 21-28 |
   | EQ Low | 33-40 |
   | Sub Mix send | 41-48 |
   | AUX1 send | 49-56 |
   | AUX2 send | 57-64 |
   | EFX send | 65-72 |
   | Pan | 73-80 |
   | Level | 81-88 |
   | Mute | 93, 94, 95, 102, 103, 104, 105, 106 |
   | MONO x2 (CH5, CH6) | 109, 110 |
   | USB 1/2 (CH7), USB 3/4 (CH8) | 113, 114 |
   | EFX type: AI NR 0, Hall 21, Room 43, Spring 64, Delay 85, Echo 107 | 117 |
   | Compressor on/off | 119 |

2. **L6max mixer page.** A native JUCE page in the layout of the companion
   app: one card for each channel with three groups (EQ: High, Mid, Mid Freq,
   Low; Sends: AUX1, AUX2, EFX, Sub Mix; Main: Pan, Level, Mute and the MONO
   or USB button), and a Reset button. Effects, compressor and scenes go in a
   section below. A compact view puts each channel on one row of knobs.
3. **Effect and compressor controls** on that page.
4. **Mixer settings through the editor port (SysEx)**, later: effect
   parameters, pad setup, MIDI and USB settings, read the mixer state on
   connect. The protocol is not official; see `docs/PROTOCOL.md` in the
   companion app.

## 2. Roland D-05

Compared with the D-05 Parameter Guide on 20 November 2026. All sound
parameters, all preset names and the Internal banks are done.

1. Edit the patch name (Patch 00-11H) and the tone names (Common 00-09H).
2. Add the patch MIDI settings: Transmit CH, Separate Mode Receive CH, Transmit
   Program Change (Patch 25-27H).
3. Separate key modes play on two MIDI channels. The profile must not say one
   part only for these modes.
4. Edit the user reverbs (Reverb Data 17-32).
5. Save an edited sound to a file (.syx) and load it again. Write it to the
   synth memory. D-50 bulk dump and bulk load.

The sequencer, the arpeggiator and the System settings are not in the D-05
MIDI map. No app can control them.

## 3. Yamaha PSR-S970 control

Compared with the PSR-S970/S770 Data List (MIDI Data Format) on 20 November
2026. Voices, XG reset, part mixer, style files and song export are done.

1. **Remote control of the keyboard's style player** (most important): Section
   Control `F0 43 7E 00 ss dd F7`, Tempo Control `F0 43 7E 01 ...`, Chord
   Control `F0 43 7E 02 ...`, Style No. and Split Point
   `F0 43 73 01 51 ...`. Then the app can start sections and send chords while
   the keyboard plays its own styles.
2. **Sound editor** with XG Parameter Change: part filter, envelope, vibrato,
   and the effect types (Reverb, Chorus, DSP1-4).
3. **Organ Flutes** (`F0 43 73 01 06 0B ...`) and **Vocal Harmony** control.
4. Registration Memory, Multi Pads, and the score and lyrics display settings
   (`F0 43 73 01 50 12 ...`).

## 4. Yamaha models from old PSR to Genos

The style reader and writer already work for SFF1 and SFF2 and all the Yamaha
style file names. Device profiles are YAML files.

1. **Model picker.** Select the keyboard model (old PSR, PSR-S series, Tyros,
   Genos, Genos2). The default is the PSR-S970. Today the song export, Style
   from Audio and the style tools use the PSR-S970 only (`PsrExport.h`
   `kProfileId`).
2. Add a device profile for each model, made from its Data List (voice list,
   bank numbers, special voices such as Revo drums, Super Articulation 2 and
   VRM on Genos).
3. Save a style as SFF1 for older keyboards. Today new styles are always SFF2.
4. Edit One Touch Settings (OTS). Today the app keeps them but does not edit
   them.
5. Read Audio Styles (drum parts that are audio).

## 5. Music Finder, Playlist and Registration

1. **Desktop Music Finder** with no limit on the number of songs. Each entry
   has the song name, the style, the voices, the tempo and other keyboard
   settings.
2. **Add songs through the AI chat**, for example: "Add 'Song name' as a slow
   bossa at 92 BPM".
3. **Export** to:
   - Music Finder files for the PSR-S970.
   - Playlist and Registration files for the PSR-SX series and Genos.
4. **In-app Playlist for older PSRs.** When you select a song, the app sends
   the style, the voices, the tempo and the split point to the keyboard through
   MIDI.
5. **Import** the factory Music Finder files (`.mfd`) and Playlist and
   Registration files (`.upl`, `.rgt`).
