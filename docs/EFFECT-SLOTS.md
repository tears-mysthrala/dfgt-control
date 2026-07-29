# DFGT effect-slot policy

| DirectInput type | DFGT slot | Rendering | Competition policy |
|---|---:|---|---|
| Constant, ramp, periodic, custom | 1 | Software mix, signed 64-bit accumulation and final saturation | Multiple effects are mixed |
| Spring | 2 | Native condition | One playing effect; another start returns `DIERR_DEVICEFULL` |
| Damper | 3 | Native condition | One playing damper or inertia |
| Inertia | 3 | Mapped to native damper | Shares the damper/inertia exclusion slot |
| Friction | 4 | Native condition | One playing effect |

The exclusion rule is checked before changing playback state. `DIES_SOLO`
stops other effects first and can therefore transfer ownership of a native
condition slot deterministically. Iteration order in `unordered_map` never
selects a winner.

A zero software mix emits only `StopSlotOne`. It does not stop or reinstall
native conditions. Profile gain changes retain the raw parameters stored in
each `EffectState`; the next render derives output from those raw values.

The renderer tick is 2 ms. Periodic and custom sample periods below 2 ms are
rejected rather than accepted with uncontrolled aliasing. Custom effects are
limited to one channel and 4,096 samples.
