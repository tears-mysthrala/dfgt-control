# Beta hardening report

## Decision

Recommended state: **private alpha**. The code, safe automated tests, static
analysis and release package are ready for the final machine-validation pass.
Do not label this a public beta until the disconnected upgrade/uninstall,
arrival/removal, suspend/resume and schema-3 restore matrix in
`TEST-MATRIX.md` has been executed against the release package.

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
| Profile failures could overwrite valid data | Error taxonomy, corruption-only quarantine, revision/CAS, LKG backup and aligned validation | `profile-document-validation` |
| Elevated install targeted accidental HKCU | Mandatory target SID/profile and `HKEY_USERS\<SID>` | `script-safety-invariants` |
| Upgrade/uninstall could stop an attached broker | Physical disconnect gate before path-owned `Stop-Process` | `script-safety-invariants` |
| Upgrade had no rollback | Staging/previous-root swap, ownership checks and registry/file rollback | parser/invariant test; VM injection pending |
| Restore/uninstall accepted unsafe rollback inputs | Schema-3 identity/hash allowlist and verified-INF-only `pnputil` restore | `script-safety-invariants`; VM restore pending |
| Release evidence was incomplete | Self-contained UI, `/sdl`, CodeQL workflow, SBOM, commit manifest, checksum and attestations | local package inspection |

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

## Automated results

- Release CTest: 16/16 x64, 16/16 x86.
- WPF: Release self-contained build passed with zero warnings.
- MSVC `/analyze`: passed after moving 131 KB peer-validation buffers from
  stack to heap and correcting API annotations/status checks.
- COM exports: `DllGetClassObject` and `DllCanUnloadNow` present in x64/x86.
- Mitigations: ASLR, NX and Control Flow Guard present in x64/x86; x64 also
  reports high-entropy VA.
- Package:
  `DfgtControl-v0.1.0-beta-local2-win.zip`
- The generated `.sha256` and `release-manifest.json` are authoritative for
  the final package digest and embedded commit.

No automated test opened a physical HID path or produced force.

## Physical evidence

Earlier interactive development verified the DFGT's constant,
bidirectional, periodic, spring, damper, friction, range and emergency
behaviour with a person present and limited magnitudes. No physical test was
run during this final hardening pass.

Still required against the packaged build:

- disconnected transactional upgrade;
- startup without wheel followed by plug-in;
- unplug/replug while idle;
- suspend/resume with no game, then with a controlled low-force effect;
- UI emergency stop and explicit rearm;
- one x64 and one x86 DirectInput game;
- disconnected uninstall and reinstall;
- schema-3 legacy restore in a disposable VM or recoverable test host.

## Known limitations and residual risk

- Provider-to-broker, broker-to-HID and physical motor-decay p99 have not been
  measured end to end. Published jitter is scheduler-only.
- Installer rollback structure has automated invariants but not injected
  registry/filesystem failures in a disposable VM.
- Native UI bridge was compiled and reviewed but the installed old agent does
  not expose it; live validation requires installing the packaged build.
- CodeQL/provenance changes are configured but remain unverified remotely
  until the branch is pushed once.
- Secure Boot and HVCI were not disabled or modified by the hardening work;
  the final verifier must capture their live state after installation.

## Rollback

Disconnect the wheel. Run the manifest-owned uninstaller. If legacy Logitech
drivers must be restored, use only
`Restore-LogitechLegacyStack.ps1 -BackupDirectory <schema-3-snapshot> -Execute`.
The script verifies machine, SID, build, hardware ID and every package hash,
and never imports registry files. Full details are in
`MIGRATION-ROLLBACK.md`.
