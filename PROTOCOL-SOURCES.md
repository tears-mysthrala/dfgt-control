# DFGT protocol sources

The protocol implementation is based on independently published interoperability
information and is validated against the connected hardware before use.

Primary references:

- Linux `hid-lg4ff`:
  <https://github.com/torvalds/linux/blob/master/drivers/hid/hid-lg4ff.c>
- `new-lg4ff`:
  <https://github.com/berarma/new-lg4ff/blob/master/hid-lg4ff.c>
- Microsoft DirectInput `DICONDITION` semantics:
  <https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416601(v=vs.85)>

Known DFGT facts used by the implementation:

- Native USB identity: `VID_046D&PID_C29A`.
- HID output report: report ID `0x00` plus a seven-byte payload.
- Range command: `F8 81 <degrees-low> <degrees-high> 00 00 00`.
- Stop all: `F3 00 00 00 00 00 00`.
- Stop slot one: `13 00 00 00 00 00 00`.
- Constant force in slot one: `11 08 <level> 80 00 00 00`.
- Spring, damper and friction use the wheel's native condition-effect slots.
- DirectInput condition coefficients are signed: negative coefficients produce
  restoring/resisting force for spring, damper and friction effects.

No Logitech binaries, SDK files, firmware or private signing material are
redistributed by this project.
