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
| SAF-001 | P0 | Observed in code | Emergency stop executes `StopAll` but does not latch; later FFB is accepted. | broker, protocol, agent/UI | Safety-state unit test | Explicit latched state and explicit rearm | Active sine cannot reactivate after emergency stop | Open | No |
| SAF-002 | P0 | Observed in code | Agent discards emergency-stop HRESULT and always claims force stopped. | `agent/DfgtAgent.cpp` | Control fake returns failure | Propagate status and expose unconfirmed stop | UI/agent never claims confirmation on failure | Open | No |
| SAF-003 | P0 | Observed in code | A failed stop does not enter a persistent blocking state. | broker | Fake HID stop failure | `StopUnconfirmed`, bounded retries, reject force | Any failed required stop blocks new force | Open | No |
| SLOT-001 | P0 | Observed in code | `Constant(0,0)` selects `StopAll`, clearing native conditions. | broker | Spring + sine zero-crossing fake HID test | Stop software slot only | Zero crossing emits `StopSlotOne`, never `StopAll` | Open | No |
| SLOT-002 | P0 | Observed in code | Provider `StopSlotOne()` aliases `StopAll()`. | DirectInput provider | Transport spy | Separate IPC command | Software stop leaves all condition slots active | Open | No |
| SLOT-003 | P0 | Observed in code | One `forceActive` boolean represents all physical slots. | broker | Multi-slot state tests | Explicit slot bitmask/state | Lease remains active while any slot is active | Open | No |
| HID-001 | P0 | Observed in code | HID writes are synchronous and can block while state mutex is held. | broker | Fake blocked transport + emergency test | Deadline-bound overlapped transport; I/O outside lock | Emergency path is bounded and lock-independent | Open | No |
| HID-002 | P0 | Inferred, pending validation | Open failure may leave partially initialized transport state. | broker | Inject failure at each open stage | Transactional RAII open | Every failed stage leaves `IsOpen == false` | Open | No |
| LEASE-001 | P0 | Observed in code | Auxiliary heartbeat renews lease while renderer may be stuck. | provider, broker | Frozen render epoch + live heartbeat | Renderer-owned monotonic epoch | Frozen renderer expires lease | Open | No |
| LEASE-002 | P0 | Observed in code | Lease expiry stops force but does not invalidate the same session. | broker | Fake clock/session test | Invalidate session and require reopen/rearm | Expired session cannot send further force | Open | No |
| PROC-001 | P0 | Observed in code | Broker does not watch the client process handle. | broker | Kill non-HID client process | PID/session/nonce and process wait | Process death starts stop immediately | Open | No |
| PIPE-001 | P0 | Observed in code | Provider and control-client pipe I/O use unbounded synchronous reads/writes. | provider, agent | Stalled fake pipe | Overlapped I/O with deadlines and cancellation | No COM/control call blocks indefinitely | Open | No |
| LOCK-001 | P0 | Observed in code | Effect-state mutex is held across transport I/O. | provider | Blocked pipe during concurrent COM destruction | Snapshot under lock, I/O after unlock | No OS I/O under central effect mutex | Open | No |
| DI-001 | P0 | Observed in code | DirectInput update/flag semantics are partial and not fully tested. | provider | Golden COM tests | Transactional DIEP/DIES handling | Required partial-update/SOLO/INFINITE tests pass | Open | No |
| IPC-001 | P1 | Observed in code | Native C++ structs are the wire format and lack nonce/capabilities/build ID. | shared protocol | x86/x64 golden vectors | Explicit little-endian framing/handshake | Cross-ABI vectors and mismatch fail closed | Open | No |
| PNP-001 | P0 | Observed in code | Agent retries only after a status failure; absent-at-start attachment is incomplete. | agent | Start fake with no device, then arrival | PnP notifications plus bounded fallback | Arrival after start attaches automatically | Open | No |
| PROF-001 | P1 | Observed in code | C++ and C# profile validators are independent and revisionless. | agent, UI | Shared golden corpus | One schema spec, revision/CAS and LKG | Both implementations agree on every vector | Open | No |
| PROF-002 | P1 | Observed in code | UI infers health from process name, not broker safety status. | UI | Same-name dummy process | Agent/UI status IPC | UI displays real device and safety state | Open | No |
| INST-001 | P0 | Observed in code | Elevated installer writes HKCU without an explicit target SID. | installer | Different elevation account test harness | Split machine/user phases | Standard user + other admin configures intended SID | Open | No |
| INST-002 | P0 | Observed in code | Upgrade lacks maintenance handshake and transactional journal. | installer | Inject copy/registry failures | Staging, ownership journal, atomic commit/rollback | Every injected failure restores prior version | Open | No |
| REST-001 | P1 | Observed in code | No allowlisted legacy restore script exists. | scripts | Snapshot fixture tests | Project-snapshot-only restore | Foreign/altered snapshots rejected | Open | No |
| VER-001 | P1 | Observed in code | Verifier does not inspect filters, COM activation, CI events or safety state. | verifier | Sanitized fixture/live non-force checks | Structured evidence/warnings/failures/notTested | Unknown is never treated as pass | Open | No |
| CI-001 | P0 | Reproduced without hardware | CTest registers only protocol vectors; other smoke binaries are compiled but not run. | CMake, CI | Baseline CTest output | Register all safe tests; mark hardware tests manual | CI executes every non-physical suite | Open | No |
| PERF-001 | P2 | Observed in code | A requested 2 ms `wait_for` is treated as the renderer schedule without jitter evidence. | provider | Fake clock + benchmark | Deadline timer and metrics | p50/p95/p99/max and missed ticks reported | Open | No |

## Safety release rule

No P0 row may be marked complete from code inspection alone. It requires a
named automated test, and any physical claim additionally requires a manual
result that records hardware, Windows build, HVCI state, architecture, maximum
magnitude and the person-present acknowledgement.
