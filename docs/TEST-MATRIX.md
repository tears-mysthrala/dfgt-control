# Test matrix

## Automated, no physical force

All CTest entries below are safe for unattended CI. The broker integration
test launches only with `--diagnostics-no-force` and never receives a HID path.
Physical tests are excluded from CTest and GitHub Actions.

| Test | x64 | x86 | Safety property |
|---|---:|---:|---|
| `com-abi-activation` | Pass | Pass | Real COM activation and exports |
| `directinput-contract` | Pass | Pass | DIEP partial updates, DIES_SOLO, INFINITE |
| `protocol-vectors` | Pass | Pass | Framing, sizes, nonce/capability rejection |
| `transport-fake-injections` | Pass | Pass | Transactional open and HID failures |
| `safety-state-machine` | Pass | Pass | Latched emergency and StopUnconfirmed |
| `effect-slot-isolation` | Pass | Pass | Software zero preserves condition slots |
| `renderer-lease-watchdog` | Pass | Pass | Frozen renderer epoch expires |
| `client-process-watch` | Pass | Pass | Process-handle death detection |
| `bounded-pipe-io` | Pass | Pass | Timeout and cancellation |
| `emergency-io-gate` | Pass | Pass | Emergency is not blocked by ordinary I/O |
| `effect-timing` | Pass | Pass | Saturating deadlines and INFINITE |
| `broker-framing-integration` | Pass | Pass | Partial client cannot retain the pipe |
| `profile-document-validation` | Pass | Pass | Unique IDs/paths/desktop and bounds |
| `timing-jitter` | Pass | Pass | High-resolution 2 ms timer remains bounded |
| `agent-recovery-policy` | Pass | Pass | Reconnect/backoff and no automatic rearm |

Last local run: Windows 11 build 26100, MSVC 19.44, SDK 10.0.26100.0,
CMake 3.29.2 and .NET SDK 9.0.316. Result: 15/15 x64 and 15/15 x86.

The local 1,000-sample timing run produced:

| Architecture | jitter p50 | p95 | p99 | max | missed ticks |
|---|---:|---:|---:|---:|---:|
| x64 | 69 us | 542 us | 665 us | 4,619 us | 2 |
| x86 | 64 us | 548 us | 669 us | 4,627 us | 2 |

These numbers describe scheduler wake-up jitter on one machine. They do not
measure provider-to-broker latency, USB completion or physical motor decay.

## Manual physical evidence

Hardware: Logitech Driving Force GT (`VID_046D&PID_C29A`). A person was
present for every force-producing test. Earlier development validation
confirmed constant, bidirectional, periodic, spring, damper, friction, range
and emergency-stop behaviours at deliberately limited magnitudes. It also
identified that the first range-boundary implementation behaved as constant
resistance rather than a hard lock; the implementation was corrected and
retested interactively.

No physical test was executed during the beta-hardening automated run. In
particular, unplug/replug, suspend/resume, upgrade, uninstall and legacy restore
remain manual acceptance items. Record each future run with:

- Windows build and HVCI/Secure Boot state;
- x86/x64 game/provider path;
- maximum requested magnitude;
- person-present acknowledgement;
- exact result and unexpected behaviour.

## Compatibility matrix still required before public beta

| Area | Current evidence | Required evidence |
|---|---|---|
| Windows 11 24H2/26100 + HVCI | Previously validated on project machine | Repeat against release ZIP |
| Secure Boot | Previously reported enabled | Capture verifier result |
| x64 DirectInput | Automated COM/contract tests | Two representative games |
| x86 DirectInput | Automated COM/contract tests | One 32-bit game |
| Suspend/resume | Code path and compile only | Manual no-force-first run |
| Upgrade/uninstall | Static/WhatIf and ownership checks | Disposable VM fault injection |
| Legacy restore | Hash/identity allowlist and parser | Disposable VM with project snapshot |
