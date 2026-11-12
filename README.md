# Arranger MIDI DAW

A light, fast MIDI sequencer and DAW for people who play keyboards. Sequence tracks, read and write real score notation, build beats, launch loops, edit arranger styles for your Yamaha, and play your hardware synths, all in one app.

**Status:** Phase 0 (skeleton). The app opens a window, plays a sample-accurate metronome through your audio device, shows incoming MIDI, and saves and loads projects. Everything else is planned; the mockups below show where it's going.

**The app today (Phase 0)**

![Phase 0 app: header with transport and display, track list, timeline and inspector](docs/images/phase0-dark.png)

## Screens

> Design mockups for the phases ahead. The final look may change.

**Timeline and piano roll**

![Timeline mockup: tracks with coloured clips, a piano roll and a track inspector](docs/images/mockup-timeline.png)

**Clip grid and Beat view**

![Clip grid and drum step sequencer mockup](docs/images/mockup-clips-and-beat.png)

**Score**

![Score mockup: a grand-staff page with chord symbols](docs/images/mockup-score.png)

**Audio tracks and sample slicer**

![Audio tracks with waveforms and a sample slicer mapped to pads](docs/images/mockup-audio-and-slicer.png)

## Highlights

- Timeline, piano roll and score views over the same notes
- Drum machine, sample slicer and a clip grid for live looping
- Arranger style editor for the Yamaha PSR-S970, with MIDI to style conversion
- Ready-made support for PSR-S970, Roland D-05, Launchkey Mini MK3, XioSynth 49 and Behringer X-Touch Mini, open to any synth
- Built-in SoundFont player for your SF2 files
- macOS first, also Windows and Linux

## Learn more

- [Technical overview](docs/technical-overview.md): stack, architecture and devices
- [Build plan](docs/build-plan.md): phases, risks and open questions
- [Building and code layout](docs/building.md): how to build, run the tests, folders and shortcuts
- [Style guide](docs/style-guide.md): colours, type and layout
