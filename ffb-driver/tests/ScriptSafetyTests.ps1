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
if ($install -notmatch 'release-manifest\.json' -or
    $install -notmatch 'ExpectedVersion' -or
    $install -notmatch 'Assert-PeArchitecture' -or
    $install -notmatch 'Staged artifact hash mismatch') {
    throw 'Installer does not verify release provenance before and after staging.'
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
foreach ($parameter in @(
    'InstallRoot',
    'ExpectedVersion',
    'TargetUserSid',
    'IncludeCodeIntegrityEvents',
    'RunComSmoke',
    'RunBrokerHandshake')) {
    if ($verify -notmatch [regex]::Escape($parameter)) {
        throw "Verifier parameter is missing: $parameter"
    }
}
foreach ($field in @(
    'warnings', 'failures', 'notTested', 'evidence', 'remediation')) {
    if ($verify -notmatch [regex]::Escape($field)) {
        throw "Verifier evidence field is missing: $field"
    }
}
$statusSmokePath = Join-Path (Split-Path -Parent $ScriptsDirectory) `
    'tests\BrokerStatusSmoke.cpp'
$statusSmoke = Get-Content -LiteralPath $statusSmokePath -Raw
if ($statusSmoke -match 'AttachDevice|ApplyProfile|EmergencyStop|SetRange' -or
    $statusSmoke -notmatch 'Command::GetStatus') {
    throw 'Broker status smoke is not strictly read-only.'
}

Write-Output 'script safety invariants passed'
