# DFGT Control

Modern, HVCI-compatible force feedback and native profile control for the
Logitech Driving Force GT on Windows 10 and Windows 11.

DFGT Control removes the need for Logitech's legacy kernel filters. It keeps the
wheel on Microsoft's signed HID stack and implements DirectInput force feedback
in user mode. No custom kernel driver, browser, telemetry or network service is
used.

> Experimental community project. Not affiliated with or endorsed by Logitech.
> Keep a backup and read the migration steps before removing legacy drivers.

## Features

- x86 and x64 DirectInput force feedback;
- constant, ramp, periodic, spring, damper, inertia, friction and custom effects;
- 40–900° operating range with a bounded dynamic boundary;
- native tray agent and WPF profile editor;
- automatic profiles selected by exact game executable path;
- force fail-safe when a game disconnects or crashes;
- compatible with Windows Memory Integrity/HVCI;
- backup-first migration and manifest-guarded uninstall.

## Supported hardware

The first release supports the Logitech Driving Force GT:

```text
USB VID_046D&PID_C29A
```

Other wheels are not supported yet, even if they use similar Logitech
protocols.

## Install

1. Download the ZIP and `.sha256` file from the latest GitHub release.
2. Verify the checksum:

   ```powershell
   Get-FileHash .\DfgtControl-v0.1.0-win.zip -Algorithm SHA256
   ```

3. Extract the ZIP.
4. Open an elevated PowerShell in the extracted directory.
5. Back up the existing Logitech stack:

   ```powershell
   $backup = .\scripts\Backup-LogitechLegacyStack.ps1 |
     ConvertFrom-Json
   ```

6. Install DFGT Control:

   ```powershell
   .\scripts\Install-DfgtFfb.ps1 -Confirm:$false
   ```

7. Follow [the migration guide](docs/MIGRATION.md) before removing Logitech
   packages or enabling Memory Integrity.

The initial releases are unsigned community builds. Windows may show an
unknown-publisher warning. Never disable Memory Integrity or Secure Boot to run
DFGT Control.

## Build from source

Requirements:

- Visual Studio 2022 Build Tools with C++ desktop workload;
- Windows SDK;
- CMake 3.24 or newer;
- .NET 9 SDK.

```powershell
git clone https://github.com/tears-mysthrala/dfgt-control.git
cd dfgt-control
.\tools\Build-Release.ps1 -Version 0.1.0-dev
```

Build output and a redistributable ZIP are written to `artifacts`.

## Safety model

The broker is the only process that writes HID reports. Force and range values
are bounded, IPC is local and restricted to the current Windows user, and loss
of the DirectInput client triggers `StopAll`. See
[the architecture and threat boundaries](docs/ARCHITECTURE.md).

Physical effect tests are never run automatically. They require a person at the
wheel and explicit execution.

## Project status

The stack has been validated on a physical DFGT with:

- Microsoft `HidUsb`/`input.inf` binding;
- HVCI and VBS running;
- x86 and x64 DirectInput clients;
- client-crash fail-safe;
- dynamic operating range;
- independent periodic-effect gain;
- native UI, agent recovery and installed runtime.

This is strong evidence for the tested machine, not a guarantee for every
Windows build or game. Compatibility reports are welcome.

## Contributing and security

Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request. Report
security issues using [SECURITY.md](SECURITY.md), not a public exploit issue.

Protocol references are documented in
[PROTOCOL-SOURCES.md](PROTOCOL-SOURCES.md). No Logitech binaries, firmware or
SDK files are redistributed.

Licensed under the [MIT License](LICENSE).
