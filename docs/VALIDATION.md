# Validation record

This file is the public, sanitised validation record for the DfgtControl beta
hardening work. It contains no device instance identifiers, user paths,
registry exports, crash dumps, or legacy driver binaries.

## Automated validation

The release build executes the same local checks for both x64 and x86:

- COM ABI loading and class-factory activation without creating force;
- DirectInput contract and error-path tests;
- broker protocol framing, authentication, timeout, and client-death tests;
- emergency latch, explicit rearm, zero-force and unconfirmed-stop tests;
- effect-slot, condition-slot, watchdog, owner-PID, and lock-boundary tests;
- profile schema and document validation;
- agent recovery-policy tests;
- PowerShell installer, restore, verifier, and uninstall safety invariants;
- high-resolution scheduler jitter measurement.

The WPF application is also published self-contained for `win-x64`. The C++
targets compile with `/W4 /WX /sdl`, Control Flow Guard, ASLR, and DEP. A
separate local build with MSVC `/analyze` is required before a release cut.

## Deliberately excluded from automation

Automated CI never sends force-feedback commands to real hardware. It also
does not install, remove, or restore drivers on the developer workstation.
Those operations require an explicit human-approved validation session with
the wheel disconnected where applicable.

## Compatibility and physical matrix

`Pass` means the named release artifact was exercised. `Pending` is not green
and blocks a public-beta recommendation.

| Target | Architecture | HVCI | Secure Boot | Game/hardware path | State |
|---|---|---|---|---|---|
| Windows 11 24H2 build 26100 | x64 | Enabled on project host; release recheck pending | Enabled on project host; release recheck pending | Automated COM and contract suite | Pass (software only) |
| Windows 11 24H2 build 26100 | x86 | Same host | Same host | Automated COM and contract suite | Pass (software only) |
| Windows 11 24H2 build 26100 | x64 | Capture with packaged verifier | Capture with packaged verifier | Representative 64-bit DirectInput game | Pending |
| Windows 11 24H2 build 26100 | x86 | Capture with packaged verifier | Capture with packaged verifier | Representative 32-bit DirectInput game | Pending |
| Windows 11 supported build other than 26100 | x64/x86 | Required | Required | Install, COM, agent and DirectInput smoke | Pending |
| Disposable Windows VM | x64/x86 | Required where supported | Record actual state | Upgrade fault injection, uninstall and schema-3 restore | Pending |
| Logitech Driving Force GT | n/a | Host state recorded above | Host state recorded above | Plug/unplug, suspend/resume, emergency/rearm and bounded low-force effects | Pending against release ZIP |

Earlier interactive development exercised constant, bidirectional, periodic,
spring, damper, friction, range and emergency behaviour with a person present.
That evidence informed implementation but does not replace the pending run
against the packaged release.

## Evidence states

The project uses three distinct evidence states:

- **pass**: the named check ran and its assertion passed;
- **fail**: the named check ran and failed;
- **not tested**: the check did not run; it must never be presented as green.

Current hardware and clean-VM evidence is recorded in
`FINAL-HARDENING-REPORT.md`. Until every P0 hardware and rollback gate has
current evidence, the release recommendation remains private alpha rather
than public beta.
