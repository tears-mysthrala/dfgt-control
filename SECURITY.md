# Security policy

## Supported versions

Only the latest published release is supported. Until the project reaches
`1.0.0`, releases are experimental and may change their profile or IPC schema.

## Reporting a vulnerability

Do not open a public issue for a vulnerability that could leave force active,
cross a local-user boundary, execute code during installation, or weaken
Windows security settings. Use GitHub private vulnerability reporting for this
repository. Include:

- affected version and architecture;
- Windows version and whether Memory Integrity/HVCI is running;
- the smallest safe reproduction;
- logs with usernames, device instance IDs and paths removed.

If private reporting is unavailable, open a public issue containing no exploit
details and ask the maintainer to enable a private channel.

## Security boundaries

- DFGT Control does not install a kernel driver. The wheel remains bound to the
  Microsoft HID stack.
- The broker is the only component that writes HID output reports.
- Named pipes reject remote clients and grant access only to the current user,
  SYSTEM and administrators.
- Force values, ranges, profile counts, paths and message sizes are bounded.
- Loss of the DirectInput client triggers `StopAll`.
- No telemetry, network listener or automatic updater is included.

The migration scripts change driver packages and registry state and therefore
require an elevated PowerShell process. Review their source and retain the
generated backup before removing the Logitech package.
