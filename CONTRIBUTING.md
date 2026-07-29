# Contributing

Thank you for helping keep old hardware useful on current Windows releases.

## Before opening a change

1. Search existing issues.
2. Separate observed hardware behaviour from assumptions.
3. Never attach Logitech binaries, exported driver packages, private signing
   material, full registry exports or logs containing personal paths.
4. Keep safety changes fail-closed: invalid or lost input must stop force.

## Build

Requirements:

- Windows 10 or Windows 11;
- Visual Studio 2022 Build Tools with Desktop development with C++;
- Windows SDK 10.0.26100 or compatible;
- .NET 9 SDK;
- CMake 3.24 or newer.

Run:

```powershell
.\tools\Build-Release.ps1 -Version 0.1.0-dev
```

The command builds x64 and x86 DirectInput providers, the x64 broker and agent,
the WPF UI, tests, and a redistributable ZIP under `artifacts`.

## Pull requests

- Explain the physical behaviour being changed.
- Add or update protocol/unit tests.
- State whether a real DFGT was used.
- Do not claim physical validation when only a simulator or unit test ran.
- Changes to force rendering, watchdogs, range or installation require a
  rollback/failure-path test.

By contributing, you agree that your contribution is licensed under MIT.
