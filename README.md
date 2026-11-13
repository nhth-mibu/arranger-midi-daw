# Arranger MIDI DAW

A light, fast MIDI sequencer and DAW for people who play keyboards. Sequence tracks, read and write real score notation, build beats, launch loops, edit arranger styles for your Yamaha, and play your hardware synths, all in one app.

**Status:** Phase 7. You can record and edit MIDI on tracks, edit notes in the piano roll, mix, build beats in a step sequencer, play your own samples, slice drum loops, and launch clips and scenes in a clip grid. You can open, edit and save Yamaha styles (PSR-S970 and others), make a style from a MIDI file, and play it with chords from your keyboard. The Score view shows any track as printed music: write and edit notes with the mouse, the computer keyboard or your MIDI keyboard, add chord symbols, lyrics, dynamics and articulations, print or export PDF, and exchange scores with Sibelius, Dorico or MuseScore through MusicXML. It plays through the built-in SoundFont player, the drum sampler or your hardware. Audio tracks play, record and edit WAV files: clips with waveforms, gain and fades, loops that follow the song tempo, and audio to MIDI for melody lines and drum hits. VST3 and Audio Unit plugins work as instruments and as effects on tracks and on the master, with a crash-safe scan and settings saved in the project.

**The app today**

![Timeline with clips and the note editor](docs/images/phase1-timeline.png)

![Score view: a piano part on a grand staff](docs/images/phase4-score.png)

![Lead sheet: chord symbol, dynamic and lyrics](docs/images/phase5-marks.png)

![Audio track with a waveform, and the drum hits found in it as MIDI](docs/images/phase6-audio.png)

![Plugins on a track: an instrument and an effect](docs/images/phase7-plugins.png)

![Clip grid with scenes](docs/images/phase2-clips.png)

![A Yamaha style in the Clip grid: sections are rows, parts are columns](docs/images/phase3-style.png)

![Chord rules of one style part](docs/images/phase3-chord-rules.png)

![Beat view: a step sequencer](docs/images/phase2-beat.png)

![Slicer: a drum loop cut at each hit](docs/images/phase2-slicer.png)

![Mixer with faders, pans, sends and meters](docs/images/phase1-mixer.png)

![Light theme](docs/images/phase1-light.png)

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

- [User manual](docs/user-manual.md): how to use the app, in simple English
- [Technical overview](docs/technical-overview.md): stack, architecture and devices
- [Build plan](docs/build-plan.md): phases, risks and open questions
- [Device profiles](docs/device-profiles.md): teach the app a new synth with one YAML file
- [Controller mixer pages](docs/controllers.md): what each knob and button does
- [Self-hosted CI](docs/ci-self-hosted.md): the build machines and the hand-off notes
- [Building and code layout](docs/building.md): how to build, run the tests, folders and shortcuts
- [Style guide](docs/style-guide.md): colours, type and layout
