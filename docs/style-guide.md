# Style guide

The look of Arranger MIDI DAW: a warm charcoal studio with a copper accent, modern but light. Taste references are LUNA (the app) and Sibelius (the score page), but the design is our own.

![Dark theme](images/phase1-timeline.png)

## Principles

- **Calm.** Dark warm greys, one accent colour, colour only where it means something (track colours, play, record, activity).
- **Fast to draw.** Everything is vector. No images, no shadows that need blurring, no animation that waits. Nothing repaints unless it changed, so an idle app uses close to 0% CPU.
- **One colour per track.** A track keeps its colour in every view: timeline, piano roll, score, beat grid and clip grid.
- **Keyboard first.** Every action has a command and a shortcut. A command palette comes later.
- **Themes are files.** Colours and sizes live in `resources/themes/*.json`. Change the file, not the code.

## Colours (dark theme)

| Token | Hex | Used for |
| --- | --- | --- |
| `background` | `#0b0d0f` | Window background, between cards |
| `panel` | `#121518` | Cards, header, status bar |
| `panelRaised` | `#1a1e22` | Buttons, fields, selected tab |
| `line` | `#23282d` | Dividers, grid lines |
| `lineStrong` | `#353c44` | Borders of buttons and fields |
| `text` | `#eee9e1` | Main text |
| `textMuted` | `#a69e93` | Secondary text |
| `textDim` | `#6e6961` | Section labels, hints |
| `accent` | `#d89454` | Copper accent: toggles, selection, the app badge |
| `playing` | `#96b57e` | Play button while playing |
| `recording` | `#dc7a6b` | Record button |
| `ok` / `warning` | `#91ad7c` / `#dca45f` | Device status dots |
| `lcd` / `lcdText` | `#07090a` / `#f1ece4` | Position, tempo and meter display |
| `timeline` / `lane` / `laneAlt` | `#0a0c0e` / `#101316` / `#13171a` | Timeline canvas and alternating track lanes |
| `laneSelected` | `#1b2025` | Selected track lane |
| `gridBar` / `gridBeat` | `#2b3138` / `#1d2227` | Bar and beat lines |
| `selection` | `#e4b57c` | Selection tint |
| `playhead` | `#e7bd86` | Playhead |

Track colours: coral `#cf7b6d`, amber `#cb9a5b`, teal `#6aa79a`, blue `#7693b5`, violet `#9a8bbb`, green `#8ba97b`, pink `#c0849f`. A project stores the colour **name**, so each theme can tune the shade.

The light theme (`light.json`) uses the same tokens on warm paper tones with a darker copper (`#b37632`).

![Light theme](images/phase1-light.png)

## Type

- **Inter** (SIL Open Font License, bundled in `resources/fonts`) for the whole UI. Regular for text, SemiBold for titles and values.
- Sizes: 16 for titles, 13 to 15 for body text, 11 for section labels (uppercase, letter-spaced), 20 for display values.
- The score page will use **Bravura** for notation and a serif for title and tempo text (Phase 4).

## Layout

- 4 px grid. Spacing is a multiple of 4; card gaps are 12.
- Corner radius 9 (7 for small fields, 12 for cards).
- Header 60 px: song, transport, display, view tabs, device status.
- Left: track list (248 px). Centre: timeline. Right: inspector (296 px). Bottom: status bar (28 px) with MIDI devices.
- Real-looking knobs only for synth parameters. Elsewhere: switches, sliders and numbers.

## Motion

Only what carries information: the playhead, the beat lights, MIDI activity dots, and later clip-launch countdowns and pad feedback. While playing, the UI refreshes at 60 Hz; when idle, 4 Hz.

## Mockups

The approved mockups (timeline, clips and beat, score, audio and slicer) are in the project files under `mockups/`. They are the target for Phases 1 to 6; the Phase 0 app shows only what works today.
