# Device profiles

A device profile tells the app about one MIDI device: its sounds, how it picks
banks, which controllers its mixer listens to and how to reset it. Each profile
is one YAML file.

Built-in profiles live in `resources/devices`. Put your own in the app's
Devices folder (Options > Show Device Profiles Folder) and restart the app. A
file with the same `id` as a built-in one replaces it.

A track uses a profile when its MIDI port name contains one of the profile's
`ports` entries. Ports with no match use General MIDI.

## Format

```yaml
profile: 1                 # format version, always 1
id: roland-d-05            # unique, same as the file name
name: Roland D-05
maker: Roland
dialect: none              # gm, gs, xg or none
ports: ["D-05"]            # parts of the MIDI port name, case ignored
drumChannels: []           # channels that play drums (often [10])
programBase: 1             # 1 if the manual numbers programs 1-128, 0 for 0-127
mixer:                     # controller number for each mixer knob, or none
  volume: 7
  pan: none
  reverb: none
  chorus: none
reset:                     # optional SysEx, sent by "Reset device"
  - "F0 41 10 42 12 40 00 7F 00 41 F7"
notes: Free text for the user.

sounds:
  - group: "Preset 1"
    items:                 # [name, bank MSB, bank LSB, program]
      - ["Fantasia", 87, 8, 1]
      - ["Metal Harp", 87, 8, 2]
```

Use `-` for a bank byte the device doesn't want sent.

## Writing one with an AI

Give the AI this page and the device manual's MIDI implementation chart and
sound list, and ask for a profile file. Things to check in the manual:

- Bank select: which MSB and LSB values pick each bank.
- Whether program numbers are printed 1-128 or 0-127.
- Whether the device receives CC 7, 10, 91 and 93. Some use other numbers
  (the XioSynth takes volume on CC 119) or none at all.
- The reset message, if it has one (GM On, GS Reset, XG System On).

The app reports the line number when a profile has a mistake.

## Bundled profiles

| File | Source |
| --- | --- |
| `yamaha-psr-s970.yaml` | PSR-S970 Data List, Voice List |
| `roland-d-05.yaml` | D-05 Parameter Guide, Sound List and MIDI Implementation |
| `novation-xiosynth.yaml` | XioSynth MIDI Implementation chart |
| `generic-gs.yaml`, `generic-xg.yaml` | GM sound set with GS and XG resets |
