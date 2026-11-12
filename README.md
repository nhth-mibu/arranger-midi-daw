# Arranger MIDI DAW

A personal but professional MIDI sequencer and DAW: light and fast, with real score notation.

**Status:** planning. No app code yet.

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

## Docs

- [Build plan](docs/build-plan.md)
