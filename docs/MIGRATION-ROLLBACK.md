# Migration and rollback contract

## Ownership and identity

The machine phase owns only the files listed in
`install-manifest.json`, the project CLSID and DFGT
`VID_046D&PID_C29A` force-feedback roots that point to that CLSID.
The elevated installer requires an explicit target user SID and profile. It
writes per-user state through `HKEY_USERS\<SID>` and the corresponding Start
Menu path; it never assumes the elevated token's HKCU is the intended user.

## Upgrade transaction

1. Resolve and validate every source artifact.
2. Create a unique sibling staging directory.
3. Copy the complete new version into staging.
4. Validate the existing ownership manifest and reject unowned files.
5. If this is an upgrade, require the physical DFGT to be disconnected.
6. Only after disconnection, stop processes whose executable path belongs to
   the existing install root.
7. Rename the old root to a unique rollback root and move staging into place.
8. Write COM/OEM/user registration and the new hash manifest.
9. Delete the rollback root only after the transaction succeeds.

On failure after the swap, the installer removes the incomplete new root,
renames the previous root back and restores only registry exports that it
created for the exact DFGT roots. A missing manifest, unexpected install root,
loaded wheel or unowned file fails closed before process termination.

This is a disconnected-upgrade policy, not a live maintenance handshake.
Disconnecting the USB device is the neutralization proof used before stopping
the old broker.

## Uninstall

Uninstall verifies the manifest root and rejects recursive cleanup when
unowned files exist. It stops only processes executing below that root. Run
entries and shortcuts are removed only when their target matches the owned
installation. Force-feedback roots are removed only when their CLSID is the
project CLSID. Arbitrary `.reg` import is not supported.

## Legacy restore

`Restore-LogitechLegacyStack.ps1` accepts only schema 3 snapshots produced by
`Backup-LogitechLegacyStack.ps1`. It verifies:

- DFGT hardware ID;
- machine GUID and computer name;
- target user SID;
- Windows build policy;
- package count and published-name shape;
- every exported file length and SHA-256;
- exactly one DFGT-declaring INF per package;
- physical DFGT disconnection.

It then invokes only `pnputil /add-driver <verified-inf> /install` followed by a
rescan. Registry exports are evidence only and are never imported by restore.

## Recovery decision

- If a new install fails before swap: remove staging; the system is unchanged.
- If upgrade fails after swap: restore the previous owned root and DFGT
  registry backup.
- If legacy removal fails: keep the schema 3 snapshot and do not broaden the
  package allowlist.
- If stop is unconfirmed: disconnect USB; do not rearm, upgrade or uninstall
  while force-capable transport remains attached.
