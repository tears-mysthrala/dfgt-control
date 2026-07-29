# DFGT native HID input mapping

Validated on the connected Logitech Driving Force GT in native
`VID_046D&PID_C29A` mode on 2026-07-29. The decoded capture uses Windows
`HidP_*` parsing. Because `WmFilter` is currently loaded, its preparsed
descriptor cannot yet be treated as the unmodified hardware descriptor.

## Axes and POV

| HID usage | Physical control | Native range | Observation |
| --- | --- | ---: | --- |
| X | Steering wheel | 0..16383 | Left stop 0, centre about 8192, right stop 16383 |
| Y | Reserved/fixed | 128 | Did not move during either pedal sweep |
| Z | Combined pedals (filtered descriptor) | 0..255 | `WmFilter` exposes a combined axis |
| Hat switch | D-pad | null, 0, 9000, 18000, 27000 | Up, right, down, left |

Before removal, the Logitech-filtered decoded view combined the pedals. A
read-only raw capture with an ordered accelerator-then-brake sweep proved that
the device nevertheless reports two independent, active-low pedal bytes:

| Raw byte offset | Physical control | Released | Fully pressed |
| ---: | --- | ---: | ---: |
| 7 | Accelerator | `FF` | `00` |
| 8 | Brake | `FF` | `00` |

The accelerator sweep occupied about 1.86–5.65 s and changed byte 7 while byte
8 stayed at `FF`. The brake sweep occupied about 7.87–9.78 s and changed byte
8 while byte 7 stayed at `FF`. Therefore pedal combination belongs to the
current Logitech-filtered descriptor/path rather than the raw device report.
After removing `WmFilter`, the inbox HID descriptor changed from a 10-byte
input report with five value caps to a 9-byte input report with four value
caps. A WinMM capture then proved the independent public mapping:

| Windows axis | Physical control | Released | Fully pressed |
| --- | --- | ---: | ---: |
| Y | Accelerator | 65535 | 0 |
| Z | Brake | 65535 | approximately 0 |

During the ordered capture, only Y changed for the accelerator and only Z
changed for the brake. X remained centred. The old `Combined pedals` label on
Z was stale per-user Logitech registry metadata and is replaced by the
installer.

## Buttons

| HID button | Physical control |
| ---: | --- |
| 1 | Cross |
| 2 | Square |
| 3 | Circle |
| 4 | Triangle |
| 5 | R1 |
| 6 | L1 |
| 7 | R2 |
| 8 | L2 |
| 9 | Select |
| 10 | Start |
| 11 | R3 |
| 12 | L3 |
| 13 | Right shift paddle |
| 14 | Left shift paddle |
| 15 | Dial press |
| 16 | Plus |
| 17 | Dial clockwise pulse |
| 18 | Dial counter-clockwise pulse |
| 19 | Minus |
| 20 | Horn |
| 21 | PS |

## Evidence

- `hid-decoded-capture.json`: full steering sweep, pedal sweep, POV and buttons
  1..12.
- `hid-buttons-13-21.json`: physical mapping of both paddles, dial controls,
  horn and PS.
- `hid-pedal-baseline.json`: read-only raw input baseline showing both pedal
  bytes at rest.
- `hid-pedals-raw-summary.json`: ordered independent pedal sweep, timing,
  extrema and invariants.
- `post-migration-input-summary.json`: inbox-HID/WinMM independent pedal
  mapping after removal of the Logitech filters.
- Decoder implementation: `sdk-helper/DfgtHidInput.cs`.
