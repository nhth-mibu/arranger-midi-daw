# Controller mixer pages

A controller page maps a hardware controller's knobs, faders and buttons to
the mixer and transport. Pages are YAML files in `resources/controllers`. They
connect by MIDI port name, so a supported controller works as soon as it is
plugged in. Use MIDI Learn only for layouts of your own (planned).

The 8 strips follow the first 8 tracks. Bank left and right move them along 8
tracks at a time. Knobs send absolute values, so a strip jumps to the knob's
position on the first touch.

## Novation Launchkey Mini MK3

The app puts the keyboard in DAW mode when it connects and takes it out again
on quit. Enable both of its MIDI inputs in Audio and MIDI settings.

| Control | Does |
| --- | --- |
| Knobs 1-8 | Volume, Pan, Sends A (reverb) or Sends B (chorus). Pick with Shift + the knob-mode pads. |
| Track left / right | Previous / next 8 tracks |
| Play, Record | Play, record |
| Session pads | Start clips in the Clip grid: 8 tracks wide, 2 scenes high. An empty pad stops its track. |
| `>` | Start the top pad row as a scene |
| Stop/Solo/Mute | Stop all clips |

Pad lights: the track colour shows a clip. Green that pulses shows a clip that
plays. Green that flashes shows a clip that waits to start.

A copper mark at the left of the Clip grid shows the two scenes that the pads
control. To move the pads, right-click a scene name and select
`Put the controller pads on this row`.

The pad lights and the `>` and Stop/Solo/Mute numbers are not yet tested on a
real Launchkey.

## Behringer X-Touch Mini

Use standard mode (MC MODE light off) with the factory presets.

The 16 Layer A buttons start clips: the top row is the third scene row, the
bottom row is the fourth. Together with the Launchkey pads, the two controllers
cover 8 tracks and 4 scenes. One button starts one clip. Empty buttons stop
their track.

| Control | Layer A | Layer B |
| --- | --- | --- |
| Knobs 1-8 | Volume | Pan |
| Knob press | Select track | Centre pan |
| Top buttons | Clips, third scene row | Mute |
| Bottom buttons | Clips, fourth scene row | Solo |
| Fader | Master level | Master level |

The Launchkey controls play, record and scene launch. The X-Touch keeps its
knobs and fader on the mixer.

## Novation XioSynth 49

The XioSynth sends whatever its current controller template says, so this page
needs a template set up on the synth, all on MIDI channel 16:

| Template control | Message | Does |
| --- | --- | --- |
| Knobs 1-8 | CC 21-28 | Volume |
| Second knob page | CC 41-48 | Pan |
| Buttons 1-8 | CC 31-38, value 127 | Mute |
| Three buttons | CC 51, 52, 53 | Play, stop, record |

Only channel 16 is treated as the controller. The keys still play your tracks.

## Format

```yaml
controller: 1
id: behringer-x-touch-mini
name: Behringer X-Touch Mini
ports: ["X-TOUCH MINI"]   # part of the MIDI port name
channel: 0                # 0 = the whole port is the controller, or 1-16
strips: 8
connect: ["9F 0C 7F"]     # optional messages sent when it is plugged in
disconnect: ["9F 0C 00"]  # and when the app quits
modes:                    # optional: the controller tells us what its knobs do
  - { name: volume, cc: 9, channel: 16, value: 2 }
controls:
  - { cc: 1, channel: 11, count: 8, action: volume }
  - { note: 8, channel: 11, count: 8, action: mute }
  - { cc: 9, channel: 11, action: masterVolume }
```

Actions: `volume`, `pan`, `reverb`, `chorus`, `mute`, `solo`, `arm`, `select`,
`panCentre`, `masterVolume`, `play`, `stop`, `record`, `loop`, `bankLeft`,
`bankRight`, `launchClip`, `launchScene`, `stopClips`. `count: 8` means eight consecutive numbers, one per strip. `row: 2` puts a `launchClip` binding on the third scene row.

## Where the numbers come from

- Launchkey Mini MK3: community notes at github.com/giezu/LaunchkeyMiniMK3
  (facts only; that project has no licence, so none of its code is used).
- X-Touch Mini: Behringer's factory presets for layers A and B. The Quick Start
  Guide doesn't list them, so these need a check on the real unit.
- XioSynth: our own template, as above.
