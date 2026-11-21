# XioSynth template SysEx

Notes for the after-freeze item "Send a controller template to the XioSynth".
Read from Novation's template files (`XIOSYNTH_TEMPLATES_080807`, 2006-2007:
one `.syx` per DAW or plugin, plus `XIOSYNTH_ALL_FACTORY_TEMPLATES.syx` with
all 16 slots). Nhat uploaded them on 21 November 2026. They are not in the
repository.

Not tested on a real XioSynth yet. Fields marked "guess" need a check on the
unit.

## One template = one SysEx message, 1808 bytes

| Bytes | Content |
|---|---|
| 0-5 | `F0 00 20 29 02 05` (Novation ID, then XioSynth) |
| 6 | `7F` |
| 7-12 | Single template file: `00 00 11 02 00 01`. Bulk dump: `01 00 08 07 00 nn`, `nn` = slot 0-15. Guess: the single file goes to the current template, the bulk form writes slot `nn`. |
| 13-28 | Template name, 16 ASCII characters, padded with spaces |
| 29-184 | Template settings, 156 bytes. Mostly zero. Byte 33 is `30`, byte 36 `02`, byte 37 `04`, byte 48 `40` in every file; some templates set bytes 30-31 and 43 (Logic: `1C 1D`, `1C`). Meaning unknown. |
| 185-1804 | 54 control records, 30 bytes each |
| 1805-1807 | `12 34 F7`. The same in every file, so it is not a checksum. |

## Control record, 30 bytes

| Byte | Content | Seen values |
|---|---|---|
| 0-15 | Control name, ASCII, padded with spaces | "Level", "cc60", "Pitch Bend" |
| 16 | Message type | `00` off, `01` CC, `0A` pitch bend, `0B` aftertouch |
| 17 | Low value | `00`; `7F` on transport buttons |
| 18 | High value | `7F`; `00` on transport and toggle buttons |
| 19 | Guess: control group | `30` (records 0-21, 44-50), `38` (records 22-43 in the CC Learn template), `34` (sustain in some templates), `71` (records 51-53) |
| 20 | Guess: mode flags | `41`, `01`, `11` (transport buttons: momentary?), `40`, `00` |
| 21 | Guess: display | `40`; `41` on Pan |
| 22 | Always `00` | |
| 23 | CC number | `07`, `0A`, `3C`... |
| 24 | Guess: MIDI channel | `00` = template channel; `0F` = channel 16 |
| 25 | Guess: start value | `64` (100) on levels, `40` on pan, `7F` on transport |
| 26-29 | Always `00` | |

## Which record is which control

Not known yet. The "MIDI CC Learn" template (`UserTemplate.syx`) gives every
record its own CC (records 0-43: CC 60-104 without 64, but record 23 is
named cc84 and sends CC 24; 46-47: CC 105-106; 44 pitch bend, 45 mod wheel,
50 sustain). To make the map: send that
template, move each knob, button and pedal on the XioSynth, and note the CC
it sends. The CC gives the record number.

## Plan

Build the 1808 bytes from `resources/controllers/novation-xiosynth-49.yaml`:
header, name "Arranger", the 156 settings bytes copied from the CC Learn
template, then one record per control. Send it, then compare with a dump read
back from the unit.
