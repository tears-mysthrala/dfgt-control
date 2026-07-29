# Hardening audit for public beta

## Baseline

| Field | Value |
|---|---|
| Starting commit | `426d7007a1d40e4d2de5c5873959620f9066ec1c` |
| Branch | `hardening/beta-safety` |
| Visual Studio Build Tools | 2022 17.14.37 (`17.14.37516.0`) |
| MSVC | 19.44, reported by the baseline MSBuild toolchain |
| Windows SDK | `10.0.26100.0` |
| CMake | `3.29.2` |
| .NET SDK | `9.0.316` |
| Build command | `.\tools\Build-Release.ps1 -Version 0.1.0-hardening-baseline` |
| x64 result | Build passed; `protocol-vectors` 1/1 passed |
| x86 result | Build passed; `protocol-vectors` 1/1 passed |
| Package SHA-256 | `52EFB56E701FADE981FB46EAA66B2CAA0A0B80CDEB86A35F02595718A0066130` |
| Physical tests in baseline | Not executed |

Phase 0 follow-up added `transport-fake-injections`; the x64 and x86 builds
both execute 2/2 CTest tests successfully without opening the physical DFGT.

Evidence labels used below:

- **Observed in code**: directly established from the stated commit.
- **Reproduced without hardware**: exercised by a unit/integration test using
  no DFGT HID writes.
- **Reproduced on physical DFGT**: explicitly exercised with a person present.
- **Inferred, pending validation**: plausible but not yet reproduced.

## Findings and work matrix

| ID | Severity | Evidence | Observed behaviour / risk | Files | Reproduction | Correction | Acceptance criterion | State | Physical DFGT |
|---|---|---|---|---|---|---|---|---|---|
| SAF-001 | P0 | Reproduced without hardware | Emergency stop previously allowed later FFB. | broker, protocol, agent/UI | `safety-state-machine` | Latched state and explicit rearm | Active sine cannot reactivate after emergency stop | Closed | No |
| SAF-002 | P0 | Reproduced without hardware | Agent previously discarded emergency-stop HRESULT. | agent/UI | `safety-state-machine`, native UI IPC | Propagate StopUnconfirmed and status | UI/agent never claims confirmation on failure | Closed | No |
| SAF-003 | P0 | Reproduced without hardware | Failed stop previously did not block new force. | broker | `safety-state-machine`, `emergency-io-gate` | Persistent `StopUnconfirmed` | Failed required stop blocks force | Closed | No |
| SLOT-001 | P0 | Reproduced without hardware | `Constant(0,0)` previously selected `StopAll`. | broker | `effect-slot-isolation` | Stop software slot only | Zero emits slot-local stop | Closed | No |
| SLOT-002 | P0 | Reproduced without hardware | Provider software stop previously aliased `StopAll`. | provider | `directinput-contract`, `effect-slot-isolation` | Separate IPC command | Conditions remain active | Closed | No |
| SLOT-003 | P0 | Reproduced without hardware | One boolean previously represented all slots. | broker | `effect-slot-isolation` | Explicit slot bitmask | Lease tracks any active slot | Closed | No |
| HID-001 | P0 | Reproduced without hardware | HID writes were synchronous under state lock. | broker | `bounded-pipe-io`, `emergency-io-gate` | Overlapped deadlines and separate I/O gate | Emergency remains bounded | Closed | No |
| HID-002 | P0 | Reproduced without hardware | Open could leave partial state. | broker | `transport-fake-injections` | Transactional open | Failed stage leaves closed transport | Closed | No |
| LEASE-001 | P0 | Reproduced without hardware | Heartbeat could mask a frozen renderer. | provider, broker | `renderer-lease-watchdog` | Renderer-owned epoch | Frozen renderer expires | Closed | No |
| LEASE-002 | P0 | Reproduced without hardware | Expiry did not invalidate the session. | broker | `renderer-lease-watchdog`, `safety-state-machine` | Session generation invalidation | Expired session cannot resume | Closed | No |
| PROC-001 | P0 | Reproduced without hardware | Broker did not watch client process. | broker | `client-process-watch` | Process handle wait | Death requests immediate stop | Closed | No |
| PIPE-001 | P0 | Reproduced without hardware | Pipe I/O was unbounded. | provider, agent | `bounded-pipe-io`, `broker-framing-integration` | Overlapped I/O and cancellation | Calls have deadlines | Closed | No |
| LOCK-001 | P0 | Reproduced without hardware | Provider held effect mutex over pipe I/O. | provider | contract suite plus lock audit | Immutable render snapshots and separate render mutex | No pipe I/O under state mutex | Closed | No |
| DI-001 | P0 | Reproduced without hardware | DirectInput flags/timing were incomplete. | provider | `directinput-contract`, `effect-timing` | Transactional DIEP/DIES handling | Partial/SOLO/INFINITE pass | Closed | No |
| IPC-001 | P1 | Reproduced without hardware | Native structs lacked handshake framing. | shared protocol | `protocol-vectors`, `broker-framing-integration` | Explicit LE frames, nonce/capabilities/build | Mismatch fails closed | Closed | No |
| PNP-001 | P0 | Reproduced without hardware | Agent retry/arrival handling was incomplete. | agent | `agent-recovery-policy`; physical transition pending | PnP notifications, polling fallback and capped backoff | Arrival after start attaches | Implemented; physical transition pending | No |
| PROF-001 | P1 | Reproduced without hardware | Validators were revisionless and divergent. | agent, UI | `profile-document-validation`, C# build | Revision/CAS, LKG, aligned constraints | Schema invariants agree | Closed | No |
| PROF-002 | P1 | Reproduced without hardware | UI inferred health from process name. | UI | Native agent IPC build | Same-user agent status bridge | UI displays broker safety | Closed | No |
| INST-001 | P0 | Reproduced without hardware | Elevated install wrote implicit HKCU. | installer | parser/WhatIf preflight | Explicit SID/profile and HKEY_USERS | Intended SID is explicit | Closed | No |
| INST-002 | P0 | Reproduced without hardware | Upgrade lacked neutralization and rollback. | installer | `script-safety-invariants`; VM injection pending | Disconnect gate, staging, ownership and rollback root | Failure restores prior version | Implemented; VM fault injection pending | No |
| REST-001 | P1 | Reproduced without hardware | Restore was absent and uninstall accepted `.reg`. | scripts | parser plus schema/hash path audit | Schema-3 allowlisted INF restore | Foreign/altered snapshots rejected | Implemented; VM test pending | No |
| VER-001 | P1 | Reproduced without hardware | Verifier omitted filters/COM/CI/safety unknowns. | verifier | parser and release packaging | Structured fail/incomplete/pass | Unknown is never pass | Closed | No |
| CI-001 | P0 | Reproduced without hardware | CI previously compiled but barely tested. | CMake, CI | local CTest 14/14 per architecture | Register safe tests; physical manual | CI executes non-physical suites | Closed | No |
| PERF-001 | P2 | Reproduced without hardware | `wait_for(2 ms)` had no jitter evidence. | provider | `timing-jitter` | High-resolution periodic timer | p50/p95/p99/max/missed published | Partially closed; end-to-end metrics pending | No |

## Safety release rule

No P0 row may be marked complete from code inspection alone. It requires a
named automated test, and any physical claim additionally requires a manual
result that records hardware, Windows build, HVCI state, architecture, maximum
magnitude and the person-present acknowledgement.
