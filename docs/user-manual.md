# User manual

This manual tells you how to use Arranger MIDI DAW. It includes only the
functions that the app has now. The manual gets a new section with each new
phase.

On Windows and Linux, use the `Ctrl` key where the manual says `Cmd`.

## Words in this manual

| Word | Meaning |
| --- | --- |
| Track | One instrument part. A track has an output, a sound and mixer controls. |
| Clip | A block of notes on a track. |
| Output | The place where a track plays: the built-in SoundFont player or a MIDI port. |
| MIDI port | A connection to a keyboard, synth or controller. |
| SoundFont | An SF2 file that contains instrument sounds. |
| Playhead | The vertical line that shows the song position. |
| Loop | The part of the song that plays again and again. |
| Device profile | A file that tells the app the sounds of one synth. |
| Kit | The set of samples on a `Drum sampler` track. |
| Pad | One sample in a kit. A pad plays when its note plays. |
| Pattern | A short clip that repeats. You make patterns in the Beat view. |
| Slot | One box in the Clip grid. A slot can contain one clip. |
| Scene | One row of slots in the Clip grid. |
| Style | An accompaniment for an arranger keyboard. Its parts follow the chord that you play. |
| Section | One part of a style in time, for example `Main A`. A section is a scene. |

## Quick start

1. Open the app.
2. Select `Options > Audio and MIDI Settings...`.
3. Select your audio output. Enable your MIDI keyboard as an input.
4. Select `File > Choose SoundFont...` and open an SF2 file.
5. Play your keyboard. You hear the sound of the selected track.
6. Push `R` to record. Play some notes.
7. Push `Space` to stop. The app makes a clip with your notes.
8. Push `Space` again to hear the clip.

## The main window

![The main window](images/phase1-timeline.png)

- **Top bar:** transport buttons, the position display, the tempo, the meter and the view tabs.
- **Left:** the track list.
- **Centre:** the timeline with the clips. The note editor opens below it.
- **Top, tabs:** `Timeline`, `Mixer`, `Beat` and `Clips` show the different views. `Score` is not ready.
- **Right:** the inspector. It shows the selected track, the song, the metronome and your devices.
- **Bottom bar:** the MIDI inputs. A dot comes on when a message arrives.

## Tracks

### Add a track

Click `+` above the track list, or push `Cmd+T`.

### Set the output and the sound

1. Click the track.
2. In the inspector, select the output: `Built-in SoundFont`, `Drum sampler` or a MIDI port.
3. Select the MIDI channel. Use channel 10 for drums on most devices.
4. Click the sound button and select a sound.

The sound list comes from the SoundFont or from the device profile of the
MIDI port. If the app has no profile for the port, it shows the General MIDI
sounds.

### Change a track

- To rename a track, double-click its name.
- To move, duplicate, colour or delete a track, right-click the track.
- `M` mutes the track. `S` plays only this track. `R` arms the track for recording.

## Play and record

| Action | Key |
| --- | --- |
| Play or stop | `Space` |
| Go to the start | `Home` or `Enter` |
| Record | `R` |
| Loop on or off | `L` |
| Metronome on or off | `C` |

- To move the playhead, click the top half of the ruler.
- To set the loop, drag in the bottom half of the ruler.
- To change the tempo, drag the tempo value or double-click it.
- To change the meter, click the meter value.

### Record

1. Arm a track with `R` in the track list. If no track is armed, the app records on the selected track.
2. Push `R` on your computer keyboard. Playback and recording start.
3. Play your part.
4. Push `Space` to stop.

The app makes a new clip. The clip starts and ends on bar lines. To quantise
each recording automatically, select `Edit > Quantise While Recording`.

## Clips

| To do this | Do this |
| --- | --- |
| Add an empty clip | Double-click an empty area of a track |
| Select a clip | Click it. Drag on an empty area to select more than one. |
| Move a clip | Drag it. You can drag it to a different track. |
| Change the length | Drag the right edge |
| Duplicate | `Cmd+D` |
| Split at the playhead | `Cmd+E` |
| Join clips | Right-click and select `Join` |
| Delete | `Delete` |
| Open the note editor | Double-click the clip |

Clips snap to bars or beats. Hold `Alt` to move a clip freely.

To zoom, hold `Cmd` and turn the mouse wheel. On a trackpad, pinch.

## Drum sampler

A `Drum sampler` track plays your WAV files. Each file is a pad.

### Make a kit from samples

1. Click the track.
2. In the inspector, set the output to `Drum sampler`.
3. Click `Load samples...`.
4. Select `Load samples (WAV)...` and select one or more WAV files.

The app gives each file a pad. The first pad plays on note C1 (36). The
subsequent pads use the subsequent notes.

To hear, change or remove a pad, click the kit button and select the pad.

A choke group stops a sound when a different sound starts. For example, put
the open hi-hat and the closed hi-hat in the same choke group.

The project file keeps the location of each WAV file, not the sound. Do not
move or delete the WAV files.

### Slice a loop

The slicer cuts a drum loop into pieces. Each piece becomes a pad.

1. Click a track.
2. Click the kit button (or the sound button) in the inspector.
3. Select `Slice a loop (WAV)...` and select a WAV file.
4. Select how to cut the loop:
   - `Slice at each hit`: the app finds each drum hit. Move the slider to get more or fewer slices.
   - `Slice in equal parts`: select the number of parts.
   - `Slice by hand`: double-click the waveform to add or remove a marker.
5. Click a slice to hear it.
6. Select the length of the loop in bars.
7. Click `Make a clip that plays the loop`.

The app makes a clip that plays the slices in sequence. The loop now follows
the tempo of the song. You can move the notes of the clip to change the
rhythm.

## Beat view

The Beat view is a step sequencer for the selected track. Each row is a drum
sound. Each column is a step.

1. Click a drum track.
2. Click the `Beat` tab, or push `Cmd+4`.
3. Click `New pattern on this track`.
4. Click a step to add a note. Click it again to remove the note.

| To do this | Do this |
| --- | --- |
| Add many steps | Drag along a row |
| Make a step soft, normal or loud | Right-click the step |
| Change the step length | Select a value in the steps box, for example `1/16 steps` |
| Change the pattern length | Select 1, 2, 4 or 8 bars |
| Remove all notes | Click `Clear` |

A pattern is a clip that repeats. In the Timeline, drag the right edge of the
clip to make the pattern play for more bars.

For a `Drum sampler` track, the rows are the pads of the kit. For other
tracks, the rows are the General MIDI drum sounds.

## Clip grid

The Clip grid lets you play clips as loops and try different combinations.
Each column is a track. Each row is a scene. Each box is a slot.

To open the Clip grid, click the `Clips` tab or push `Cmd+5`.

### Put clips in slots

| To do this | Do this |
| --- | --- |
| Make an empty clip | Double-click an empty slot. The note editor opens. |
| Copy a clip from the Timeline | Select the clip in the Timeline. Then right-click an empty slot and select `Copy from the Timeline`. |
| Edit the notes | Double-click the clip |
| Change the length | Right-click the clip and select `Length` |
| Rename, copy or delete | Right-click the clip |
| Rename a scene | Double-click the scene name |

To edit a slot clip as a drum pattern, open its notes and then click the
`Beat` tab.

### Play clips

- To start a clip, click the play button on the clip.
- To start all clips in a row, click the scene name.
- To stop one track, click `Stop` below its column, or click the stop button in an empty slot.
- To stop all clips, click `Stop all`.

A clip does not start immediately. It starts at the subsequent bar, thus all
clips stay in time. To change this, select a different value in `Clips start`.

A clip that waits flashes. A clip that plays shows a progress line.

A track plays one slot at a time. While a slot plays, the Timeline clips of
that track are silent. When the slot stops, the Timeline plays again.

### Record the Clip grid to the Timeline

1. Click `Record to Timeline`.
2. Start clips and scenes.
3. Click `Record to Timeline` again, or push `Space`.

The app writes all that you played to the Timeline as clips. The slots stop,
and the Timeline plays your recording.

### Record notes into a slot

1. Start the clip in the slot.
2. Push `R` and play your keyboard.
3. Push `R` again.

The app adds your notes to the clip in the slot.

## Styles

A style is an accompaniment for an arranger keyboard, for example the Yamaha
PSR-S970. A style has sections (`Intro A`, `Main A`, `Fill In AA`, `Ending A`)
and eight parts (two rhythm parts, bass, two chord parts, pad and two phrase
parts). The parts follow the chord that you play.

In the app, a style is a Clip grid. Each scene is a section. Each track is a
part. Thus you edit a style with the Clip grid, the note editor and the Beat
view.

### Open a style

1. Select `File > Open Style File...`.
2. Select a Yamaha style file (`.sty`).

The Clip grid shows the sections and the parts. The voice and the mixer
values of each part are on its track.

### Play a style

1. Click a section name to start the section.
2. Play a chord on your keyboard below F#3. The parts follow the chord.

The `Chord` button shows the chord. The chord stays when you release the
keys. You can also click the `Chord` button and select a chord.

To change to a different section, click its name. The section starts at the
subsequent bar.

To hear the style with the voices of your keyboard, set the output of each
track to the MIDI port of the keyboard. On the keyboard, set the style to off
and set Local Control to off.

### Make a style from your clips

1. Put your clips in the Clip grid. Write all parts in the chord C major 7.
2. Select `File > Make the Clip Grid a Style`.

The app gives each track a style part. Drum tracks become rhythm parts. The
app also gives each scene a section name that the keyboard knows.

### Save a style

Select `File > Save as Style File...`. Copy the `.sty` file to a USB memory
and load it on the keyboard.

The chord rules in the app follow the rules that Yamaha publishes. The
keyboard can play some notes differently. Always do a check of a new style on
the keyboard.

## Note editor

Double-click a clip to open the note editor. Push `Esc` or click `Close` to
close it.

| To do this | Do this |
| --- | --- |
| Add a note | Click an empty area |
| Select notes | Click a note, or drag on an empty area |
| Move a note | Drag it left, right, up or down |
| Change the length | Drag the right edge of the note |
| Delete a note | Double-click it, or select it and push `Delete` |
| Move by a semitone | `Up` or `Down` arrow |
| Move by an octave | `Shift` + `Up` or `Down` arrow |
| Change the velocity | Drag in the velocity lane at the bottom |
| Select all notes | `Cmd+A` |
| Quantise | `Q`, or click `Quantise` |
| Hear a pitch | Click a key of the piano at the left |

The grid box sets the step for new notes, for movement and for quantise.
Hold `Alt` to ignore the grid.

`Quantise` moves the selected notes to the grid. If no notes are selected, it
moves all notes in the clip.

## Mixer

Click the `Mixer` tab, or push `Cmd+2`. Push `Cmd+1` to go back to the timeline.

![The mixer](images/phase1-mixer.png)

Each track has a strip:

- **Rev** and **Cho** knobs: reverb and chorus level.
- **Pan** knob: left or right position.
- **M**, **S**, **R**: mute, solo, record arm.
- **Fader:** volume.
- **Meter:** the level of tracks that use the built-in SoundFont.

Drag a knob up or down to change it. Hold `Shift` for small steps.
Double-click a control to set it to its default value.

The `Master` strip sets the level of the built-in SoundFont player.

For MIDI ports, the strip sends MIDI controllers to your synth: volume (CC 7),
pan (CC 10), reverb (CC 91) and chorus (CC 93). A device profile can change
these numbers.

## Hardware controllers

The app has mixer pages for these controllers. The pads of the Launchkey
Mini MK3 also start clips in the Clip grid. Connect the controller and
enable its MIDI inputs. No other setup is necessary.

- Novation Launchkey Mini MK3
- Behringer X-Touch Mini
- Novation XioSynth 49 (you must make a template on the synth first)

[Controller mixer pages](controllers.md) shows the function of each knob and
button.

## Your synths

The app has device profiles for the Yamaha PSR-S970, the Roland D-05 and the
Novation XioSynth. It also has profiles for Roland GS and Yamaha XG devices.
The app selects the profile from the name of the MIDI port.

To add a synth, write a device profile. [Device profiles](device-profiles.md)
tells you how.

To set a synth to its default MIDI state, click the sound button and select
`Reset`. This item shows only if the profile has a reset message.

## Files

| Action | Menu | Key |
| --- | --- | --- |
| New project | `File > New` | `Cmd+N` |
| Open a project or a MIDI file | `File > Open...` | `Cmd+O` |
| Save | `File > Save` | `Cmd+S` |
| Save with a new name | `File > Save As...` | `Cmd+Shift+S` |
| Import a MIDI file | `File > Import MIDI File...` | `Cmd+Shift+I` |
| Export a MIDI file | `File > Export MIDI File...` | `Cmd+Shift+E` |

Project files have the extension `.arranger`.

When you import a MIDI file, each MIDI channel becomes one track. The app
keeps the tempo, the meter, the sounds and the mixer values from the file.

## Undo

Push `Cmd+Z` to undo the last change. Push `Cmd+Shift+Z` to redo it. The
`Edit` menu shows the name of the change.

## Look

Select `View > Dark Theme` or `View > Light Theme`.

## If something goes wrong

| Problem | What to do |
| --- | --- |
| No sound from a SoundFont track | Make sure that an audio device is open and a SoundFont is loaded. The inspector shows the two. |
| No sound from a synth | Make sure that the track output is the MIDI port of the synth and the channel is correct. |
| The keyboard does not record | Enable the keyboard as a MIDI input in `Audio and MIDI Settings...`. Look for the dot in the bottom bar. |
| A synth plays the wrong sound | Select the sound again. If the list is incorrect, check the device profile. |
| A controller does nothing | Enable all MIDI inputs of the controller. For the X-Touch Mini, make sure that the `MC MODE` light is off. |
