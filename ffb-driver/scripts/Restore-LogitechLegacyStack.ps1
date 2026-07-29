[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [Parameter(Mandatory)]
    [string]$BackupDirectory,

    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run restore from an elevated PowerShell process.'
}
if (-not $Execute) {
    throw 'Safety stop: review the snapshot, then pass -Execute.'
}

$root = [IO.Path]::GetFullPath($BackupDirectory).TrimEnd('\')
$snapshotPath = Join-Path $root 'snapshot.json'
if (-not (Test-Path -LiteralPath $snapshotPath -PathType Leaf)) {
    throw "Project snapshot is missing: $snapshotPath"
}
$snapshot = Get-Content -LiteralPath $snapshotPath -Raw | ConvertFrom-Json
if ($snapshot.schemaVersion -ne 3 -or
    $snapshot.dfgtHardwareId -ne 'VID_046D&PID_C29A') {
    throw 'Only schema 3 DFGT project snapshots can be restored.'
}

$machineGuid = (Get-ItemProperty `
    'HKLM:\SOFTWARE\Microsoft\Cryptography' -Name MachineGuid).MachineGuid
if ($snapshot.computer -ne $env:COMPUTERNAME -or
    $snapshot.machineGuid -ne $machineGuid -or
    $snapshot.accountSid -ne $identity.User.Value) {
    throw 'Snapshot machine or user identity does not match this restore context.'
}
if ([int]$snapshot.windowsBuild -gt [Environment]::OSVersion.Version.Build) {
    throw 'Snapshot came from a newer Windows build; refusing downgrade restore.'
}

$presentDfgt = @(
    Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object InstanceId -like '*VID_046D&PID_C29A*'
)
if ($presentDfgt) {
    throw 'Disconnect the DFGT before restoring its legacy package.'
}

$packageNames = @($snapshot.dfgtPackages)
if ($packageNames.Count -lt 1 -or $packageNames.Count -gt 2 -or
    $packageNames | Where-Object { $_ -notmatch '^oem[0-9]+\.inf$' }) {
    throw 'Snapshot package allowlist is malformed or unexpectedly broad.'
}

$infFiles = @()
foreach ($packageName in $packageNames) {
    $evidence = @($snapshot.packageEvidence |
        Where-Object publishedName -eq $packageName)
    if ($evidence.Count -ne 1) {
        throw "Package evidence for $packageName is missing or ambiguous."
    }
    $packageRoot = [IO.Path]::GetFullPath(
        (Join-Path $root "driver-packages\$packageName")).TrimEnd('\')
    $packageInfs = @()
    foreach ($file in @($evidence[0].files)) {
        $candidate = [IO.Path]::GetFullPath(
            (Join-Path $packageRoot ([string]$file.relativePath)))
        if (-not $candidate.StartsWith(
                "$packageRoot\",
                [StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            throw "Snapshot file is absent or escapes its package: $candidate"
        }
        if ((Get-Item -LiteralPath $candidate).Length -ne [long]$file.length -or
            (Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash -ne
                [string]$file.sha256) {
            throw "Snapshot hash verification failed: $candidate"
        }
        if ([IO.Path]::GetExtension($candidate) -ieq '.inf') {
            $infText = Get-Content -LiteralPath $candidate -Raw
            if ($infText -notmatch '(?i)VID_046D&PID_C29A') {
                throw "INF does not declare the DFGT hardware ID: $candidate"
            }
            $packageInfs += $candidate
        }
    }
    if ($packageInfs.Count -ne 1) {
        throw "$packageName must contain exactly one verified INF."
    }
    $infFiles += $packageInfs[0]
}

if ($PSCmdlet.ShouldProcess(
        ($infFiles -join ', '),
        'Restore verified DFGT legacy driver packages')) {
    foreach ($inf in $infFiles) {
        & pnputil.exe /add-driver $inf /install
        if ($LASTEXITCODE -ne 0) {
            throw "pnputil failed for $inf (exit $LASTEXITCODE)."
        }
    }
    & pnputil.exe /scan-devices
    if ($LASTEXITCODE -ne 0) { throw 'PnP rescan failed.' }
}

[pscustomobject]@{
    restored = -not [bool]$WhatIfPreference
    backupDirectory = $root
    packages = $packageNames
    registryImported = $false
    next = 'Reconnect only the DFGT and verify binding before enabling force.'
} | ConvertTo-Json -Depth 4
