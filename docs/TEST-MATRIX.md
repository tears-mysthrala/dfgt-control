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
| `profile-golden-csharp` | Pass | Pass | C++ and C# consume the same validation corpus |
| `timing-jitter` | Pass | Pass | High-resolution 2 ms timer remains bounded |
| `agent-recovery-policy` | Pass | Pass | Reconnect/backoff and no automatic rearm |
| `dfgt-native-mode-switch` | Pass | Pass | C294 revision gate and upstream command vectors |
| `script-safety-invariants` | Pass | Pass | Disconnect-before-kill, SID and restore allowlist |

Last local run: Windows 11 build 26100, MSVC 19.44, SDK 10.0.26100.0,
CMake 3.29.2 and .NET SDK 9.0.316. Result: 18/18 x64 and 18/18 x86.

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

The beta-hardening run also exercised the following person-observed operations
on Windows 11 build 26100:

- startup with the wheel absent, followed by USB arrival;
- real `C294` compatibility identity, revision `0x1322`, transitioned to
  native `C29A` using the upstream mode-switch sequence;
- no wheel movement during the transition; opening the native device
  intentionally disabled firmware autocenter as part of neutralization;
- attached broker status: connected, no FFB client, zero active slots and
  stop confirmed;
- Microsoft `input.inf` on both nodes, `HidUsb` transport, no legacy filters,
  HVCI/VBS running, x64/x86 COM activation and broker handshake;
- disconnected install, old-to-new upgrade, new-to-new upgrade, uninstall and
  reinstall with manifest/hash/registry ownership checks.

Suspend/resume, representative games and schema-3 legacy restore remain manual
acceptance items. Record each future run with:

- Windows build and HVCI/Secure Boot state;
- x86/x64 game/provider path;
- maximum requested magnitude;
- person-present acknowledgement;
- exact result and unexpected behaviour.

## Compatibility matrix still required before public beta

| Area | Current evidence | Required evidence |
|---|---|---|
| Windows 11 24H2/26100 + HVCI | Connected verifier pass; HVCI/VBS running | Repeat against final release ZIP |
| Secure Boot | Elevated disconnected verifier reported enabled | Repeat after final package install |
| x64 DirectInput | Automated COM/contract tests | Two representative games |
| x86 DirectInput | Automated COM/contract tests | One 32-bit game |
| Suspend/resume | Code path and compile only | Manual no-force-first run |
| Upgrade/uninstall | Real disconnected install/upgrade/uninstall/reinstall passed | Disposable VM fault injection |
| Legacy restore | Hash/identity allowlist and parser | Disposable VM with project snapshot |
