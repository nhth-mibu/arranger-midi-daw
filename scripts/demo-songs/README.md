# Demo songs

These scripts make the demo songs in `examples/`. You need Python with `mido`,
`fluidsynth`, `ffmpeg` and a GM SoundFont (GeneralUser GS).

1. `python3 make_copper_lights.py copper-lights.mid` (or `make_northern_fire.py northern-fire.mid`) writes the MIDI file.
2. `python3 stems.py copper-lights.mid` writes one MIDI file per track in `stems/`.
3. Render each stem with `fluidsynth -ni -g 0.6 -r 44100 -F <stem>.wav GeneralUser-GS.sf2 <stem>.mid`,
   then encode it with `ffmpeg -i <stem>.wav -c:a libvorbis -q:a 2 <stem>.ogg`. Leave out the silent Vocal stem.
4. Open the MIDI file in the app and save it as a project (`--render x.wav --save-project in.arranger file.mid`).
5. `python3 build_projects.py in.arranger "Copper Lights" 126 "examples/Copper Lights"` writes the
   MIDI project and the audio project. The audio project needs the stems in `Stems/`.
