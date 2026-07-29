[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$ScriptsDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Read-ValidatedScript {
    param([Parameter(Mandatory)][string]$Name)
    $path = Join-Path $ScriptsDirectory $Name
    $tokens = $null
    $errors = $null
    [Management.Automation.Language.Parser]::ParseFile(
        $path, [ref]$tokens, [ref]$errors) | Out-Null
    if ($errors.Count) {
        throw "$Name has parser errors: $($errors.Message -join '; ')"
    }
    return Get-Content -LiteralPath $path -Raw
}

$install = Read-ValidatedScript 'Install-DfgtFfb.ps1'
$uninstall = Read-ValidatedScript 'Uninstall-DfgtFfb.ps1'
$restore = Read-ValidatedScript 'Restore-LogitechLegacyStack.ps1'
$backup = Read-ValidatedScript 'Backup-LogitechLegacyStack.ps1'
$verify = Read-ValidatedScript 'Verify-HvciStack.ps1'

foreach ($entry in @(
    @{ Name = 'install'; Text = $install },
    @{ Name = 'uninstall'; Text = $uninstall })) {
    $disconnect = $entry.Text.IndexOf(
        "Disconnect the DFGT",
        [StringComparison]::Ordinal)
    $stop = $entry.Text.IndexOf(
        'Stop-Process',
        [StringComparison]::Ordinal)
    if ($disconnect -lt 0 -or $stop -lt 0 -or $disconnect -gt $stop) {
        throw "$($entry.Name) can stop a process before its disconnect gate."
    }
}
if ($install -notmatch 'TargetUserSid' -or
    $install -notmatch 'Registry::HKEY_USERS') {
    throw 'Installer does not bind user changes to an explicit SID hive.'
}
if ($install -notmatch 'staging-' -or
    $install -notmatch 'previous-' -or
    $install -notmatch 'catch\s*\{') {
    throw 'Installer transaction/rollback structure is absent.'
}
if ($restore -match '(?i)reg\.exe\s+import' -or
    $restore -notmatch '(?i)pnputil\.exe\s+/add-driver' -or
    $restore -notmatch 'schemaVersion\s+-ne\s+3') {
    throw 'Legacy restore is not restricted to verified schema-3 INFs.'
}
if ($backup -notmatch 'sha256' -or
    $backup -notmatch 'machineGuid' -or
    $backup -notmatch 'accountSid') {
    throw 'Legacy snapshot omits required identity or hash evidence.'
}
if ($verify -notmatch "'incomplete'" -or
    $verify -notmatch 'notTested') {
    throw 'Verifier can no longer represent unknown evidence explicitly.'
}
if ($verify -notmatch 'DfgtFfbComSmoke\.exe' -or
    $verify -notmatch 'activation\.tested' -or
    $verify -notmatch 'activation\.passed') {
    throw 'Verifier does not execute the packaged no-force COM smoke tests.'
}

Write-Output 'script safety invariants passed'
