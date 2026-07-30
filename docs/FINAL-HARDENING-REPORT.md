# Beta hardening report

## Decision

Recommended state: **private beta candidate**. Safe automated tests, static
analysis, real disconnected lifecycle operations and native device arrival
have passed on the project machine. Do not label this a public beta until
suspend/resume, representative x64/x86 games, UI emergency/rearm and schema-3
restore/fault injection have been executed as described in `TEST-MATRIX.md`.

## Changes tied to failures

| Failure | Correction | Evidence |
|---|---|---|
| Emergency could be overwritten by later effects | Broker-owned latched state, generation invalidation and explicit rearm | `safety-state-machine` |
| Failed stop could be reported as stopped | `StopUnconfirmed`, bounded retries and truthful agent/UI status | `safety-state-machine`, `emergency-io-gate` |
| Software zero cleared native conditions | Independent physical-slot mask and software-only stop command | `effect-slot-isolation` |
| Heartbeat could hide a frozen renderer | Renderer epoch lease plus client process handle | `renderer-lease-watchdog`, `client-process-watch` |
| HID/pipe calls could block indefinitely | Overlapped I/O, cancellation and deadlines | `bounded-pipe-io`, `broker-framing-integration` |
| Effect mutex crossed OS I/O | Immutable render snapshots and separate render serialization | x64/x86 contract suites and lock audit |
| DirectInput flags/timing were incomplete | Transactional DIEP updates, DIES_SOLO, saturated INFINITE deadlines | `directinput-contract`, `effect-timing` |
| IPC used ABI structs without session binding | Explicit LE frames, nonce, sequence, capabilities, build ID and peer validation | `protocol-vectors`, framing integration |
| UI inferred health from process names | Same-user agent pipe with PID/path validation and real broker safety state | native/UI builds |
| Profile failures could overwrite valid data | Shared C++/C# golden corpus, cross-process lock, revision/CAS, crash recovery and LKG backup | `profile-document-validation`, `profile-golden-csharp` |
| Elevated install targeted accidental HKCU | Mandatory target SID/profile and `HKEY_USERS\<SID>` | `script-safety-invariants` |
| Upgrade/uninstall could stop an attached broker | Physical disconnect gate before path-owned `Stop-Process` | `script-safety-invariants` |
| Upgrade had no rollback | Manifest-verified staging, journal/complete markers, interrupted recovery, maintenance handshake and registry/file rollback | parser/invariant test; VM injection pending |
| Restore/uninstall accepted unsafe rollback inputs | Schema-3 identity/hash allowlist and verified-INF-only `pnputil` restore | `script-safety-invariants`; VM restore pending |
| Release evidence was incomplete | Self-contained UI, `/sdl`, CodeQL workflow, SBOM, commit manifest, checksum and attestations | local package inspection |
| DFGT could remain in shared C294 compatibility mode | Revision-gated upstream EXT_CMD9 transition before broker startup | `dfgt-native-mode-switch`; physical C294-to-C29A arrival |

## Local commits not yet pushed

- `c6e6d82` preserve profiles across transient storage failures
- `6c04831` keep DirectInput state locks away from pipe I/O
- `f454447` allowlist verified legacy driver restoration
- `dc65630` bind elevated install to an explicit user SID
- `cb6503e` make release verification evidence explicit
- `47ef08e` harden timing and release provenance
- `4d88e44` require disconnected transactional upgrades
- `4281f2e` expose truthful safety controls to the native UI
- `949b4fd` publish beta validation and rollback evidence
- `34743d0` lock down agent recovery policy
- `745d08d` require neutral disconnect before removal
- `7253904` clear MSVC static analysis findings
- `85849da` record beta hardening release decision
- `4f0feb4` make packaged validation executable
- `8a09f56` verify release provenance and live safety evidence
- `b29da5b` make maintenance and profile writes transactional
- `b781027` enter native DFGT mode on reconnect

## Automated results

- Release CTest: 18/18 x64, 18/18 x86.
- WPF: Release self-contained build passed with zero warnings.
- MSVC `/analyze`: passed after moving 131 KB peer-validation buffers from
  stack to heap and correcting API annotations/status checks.
- COM exports: `DllGetClassObject` and `DllCanUnloadNow` present in x64/x86.
- Mitigations: ASLR, NX and Control Flow Guard present in x64/x86; x64 also
  reports high-entropy VA.
- The generated `.sha256` and `release-manifest.json` are authoritative for
  each package digest and embedded commit. The final candidate is regenerated
  only after the tracked evidence is committed.

No automated test opened a physical HID path or produced force.

## Physical evidence

Earlier interactive development verified the DFGT's constant, bidirectional,
periodic, spring, damper, friction, range and emergency behaviour with a person
present and limited magnitudes.

This hardening pass additionally verified startup while absent followed by
arrival in `C294` compatibility mode, transition to native `C29A`, inbox
Microsoft HID binding and a safe broker status. The wheel did not move during
the mode transition. The observer reported that firmware autocenter was
disabled when the native broker opened it, which is the expected neutralization
sequence. The resulting state reported no FFB client, zero active slots and a
confirmed stop.

Disconnected install, old-to-new upgrade, new-to-new upgrade, uninstall and
reinstall completed on the project machine. Manifest hashes, owned registry
roots, x64/x86 COM activation, HVCI/VBS, Secure Boot in an elevated run, and
broker handshake were verified.

Still required against the packaged build:

- suspend/resume with no game, then with a controlled low-force effect;
- UI emergency stop and explicit rearm;
- one x64 and one x86 DirectInput game;
- schema-3 legacy restore in a disposable VM or recoverable test host.

## Known limitations and residual risk

- Provider-to-broker, broker-to-HID and physical motor-decay p99 have not been
  measured end to end. Published jitter is scheduler-only.
- Installer rollback structure has automated invariants but not injected
  registry/filesystem failures in a disposable VM.
- The native UI emergency/rearm interaction still requires a live manual run
  against the final installed package.
- CodeQL/provenance changes are configured but remain unverified remotely
  until the branch is pushed once.
- Secure Boot and HVCI were not disabled or modified. An elevated disconnected
  verifier confirmed both; a connected verifier confirmed HVCI/VBS, inbox HID,
  COM and broker state. The final package should repeat that capture.

## Rollback

Disconnect the wheel. Run the manifest-owned uninstaller. If legacy Logitech
drivers must be restored, use only
`Restore-LogitechLegacyStack.ps1 -BackupDirectory <schema-3-snapshot> -Execute`.
The script verifies machine, SID, build, hardware ID and every package hash,
and never imports registry files. Full details are in
`MIGRATION-ROLLBACK.md`.
