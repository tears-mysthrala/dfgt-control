# Migration from Logitech Gaming Software

Migration changes kernel driver packages and should be treated as a reversible
system operation.

## Before migration

- Confirm the device is exactly `VID_046D&PID_C29A`.
- Disconnect unrelated Logitech gaming devices.
- Create a Windows restore point or equivalent system backup.
- Run the project backup script from an elevated PowerShell.
- Keep keyboard and mouse access available.

```powershell
$backup = .\scripts\Backup-LogitechLegacyStack.ps1 |
  ConvertFrom-Json
.\scripts\Install-DfgtFfb.ps1 -Confirm:$false
```

Test input and force feedback before removing anything.

## Remove only the audited legacy stack

The removal script is deliberately narrow. It verifies the DFGT replacement
manifest before touching the known Logitech packages and preserves unrelated
Logitech devices.

Disconnect the wheel before removal. The script refuses to continue while a
DFGT is present and refuses to remove a package shared by another device.

```powershell
.\scripts\Remove-LogitechLegacyStack.ps1 `
  -BackupDirectory $backup.backupDirectory `
  -Execute `
  -Confirm:$false
```

Reboot, reconnect the wheel and verify that both nodes use Microsoft drivers:

```powershell
Get-PnpDevice -PresentOnly |
  Where-Object InstanceId -match 'VID_046D&PID_C29A'
```

## Enable Memory Integrity

Prefer the Windows Security user interface. The included script exists for
repeatable local testing, but changing security policy should be an explicit
administrator decision.

After reboot:

```powershell
.\scripts\Verify-HvciStack.ps1
```

## Rollback

Uninstall only removes objects owned by the DFGT Control manifest:

```powershell
.\scripts\Uninstall-DfgtFfb.ps1 -Confirm:$false
```

Registry backups and exported driver packages are stored under
`%PROGRAMDATA%\DfgtControl\backups`. Restore only the exact backup created for
the affected machine. Windows Update or a vendor installer may be safer than
restoring an old package on a different Windows build.
