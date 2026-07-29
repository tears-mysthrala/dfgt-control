# Public release policy

## Scope of the first release

The maintained product consists of:

- x86/x64 DirectInput force-feedback providers;
- x64 user-mode HID broker;
- x64 tray agent and WPF profile editor;
- backup, install, migration, verification and uninstall scripts;
- protocol tests and optional hardware diagnostics.

Historical web experiments, Logitech SDK experiments, raw HID captures and
machine-specific test output are not release artifacts.

## Release gates

1. CI builds x86 and x64 with warnings treated as errors.
2. Protocol tests pass.
3. The release package contains only allowlisted files.
4. SHA-256 checksums are generated from the final ZIP.
5. Source contains no personal paths, secrets or redistributed Logitech files.
6. Manual evidence confirms input, x86/x64 FFB, fail-safe, range restoration,
   HVCI and uninstall behaviour on a real DFGT.
7. Known limitations and unsigned-binary status are explicit in the release
   notes.

## Versioning

Use semantic versions. Before `1.0.0`, IPC/profile compatibility may change but
must be called out in release notes. A hardware-validated release is tagged
`vMAJOR.MINOR.PATCH`; unvalidated builds are CI artifacts, never releases.

## Signing

The initial community build may be unsigned. Releases must publish checksums
and reproducible build instructions. Authenticode signing is a future gate once
a protected code-signing identity and a sustainable release process exist.
