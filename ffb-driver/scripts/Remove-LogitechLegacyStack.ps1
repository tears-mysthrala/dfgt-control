[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [Parameter(Mandatory)]
    [string]$BackupDirectory,

    [string]$InstallRoot = (Join-Path $env:ProgramFiles 'DfgtControl'),

    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run the migration from an elevated PowerShell process.'
}
if (-not $Execute) {
    throw 'Safety stop: pass -Execute only after reviewing the backup.'
}

$presentDfgt = @(
    Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object InstanceId -like '*VID_046D&PID_C29A*'
)
if ($presentDfgt) {
    throw 'Disconnect the DFGT before package removal. This prevents active force and in-use driver deletion.'
}

$resolvedBackup = [IO.Path]::GetFullPath($BackupDirectory).TrimEnd('\')
$snapshotPath = Join-Path $resolvedBackup 'snapshot.json'
if (-not (Test-Path $snapshotPath -PathType Leaf)) {
    throw "Backup snapshot is missing: $snapshotPath"
}
$snapshot = Get-Content $snapshotPath -Raw | ConvertFrom-Json
if ($snapshot.schemaVersion -notin @(2, 3) -or
    $snapshot.dfgtHardwareId -ne 'VID_046D&PID_C29A') {
    throw 'Unsupported or mismatched backup snapshot.'
}
$packageNames = @($snapshot.dfgtPackages)
if (-not $packageNames -or
    $packageNames.Count -gt 2 -or
    $packageNames | Where-Object { $_ -notmatch '^oem[0-9]+\.inf$' }) {
    throw 'Snapshot package list is empty, malformed or unexpectedly broad.'
}

foreach ($packageName in $packageNames) {
    $evidence = @($snapshot.packageEvidence |
        Where-Object publishedName -eq $packageName)
    if ($evidence.Count -ne 1) {
        throw "Backup evidence for $packageName is missing or ambiguous."
    }
    $packageRoot = [IO.Path]::GetFullPath(
        (Join-Path $resolvedBackup "driver-packages\$packageName")
    ).TrimEnd('\')
    foreach ($file in @($evidence[0].files)) {
        $candidate = [IO.Path]::GetFullPath(
            (Join-Path $packageRoot ([string]$file.relativePath)))
        if (-not $candidate.StartsWith(
                "$packageRoot\",
                [StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-Path $candidate -PathType Leaf)) {
            throw "Exported package evidence is missing or escaped: $candidate"
        }
        if ((Get-Item $candidate).Length -ne [long]$file.length -or
            (Get-FileHash $candidate -Algorithm SHA256).Hash -ne
                [string]$file.sha256) {
            throw "Exported package evidence changed: $candidate"
        }
    }
    $otherDevices = @(
        Get-CimInstance Win32_PnPSignedDriver |
            Where-Object {
                $_.InfName -eq $packageName -and
                $_.DeviceID -notlike '*VID_046D&PID_C29A*'
            }
    )
    if ($otherDevices) {
        throw "$packageName is also bound to non-DFGT devices; refusing removal."
    }
}

$manifestPath = Join-Path $InstallRoot 'install-manifest.json'
if (-not (Test-Path $manifestPath -PathType Leaf)) {
    throw "Replacement install manifest is missing: $manifestPath"
}
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$resolvedInstall = [IO.Path]::GetFullPath($InstallRoot).TrimEnd('\')
if ([IO.Path]::GetFullPath([string]$manifest.installRoot).TrimEnd('\') -ne
    $resolvedInstall) {
    throw 'Replacement manifest does not match the requested install root.'
}
foreach ($file in $manifest.files) {
    if (-not (Test-Path $file.path -PathType Leaf) -or
        (Get-FileHash $file.path -Algorithm SHA256).Hash -ne $file.sha256) {
        throw "Installed replacement evidence failed: $($file.path)"
    }
}

if ($PSCmdlet.ShouldProcess(
        ($packageNames -join ', '),
        'Remove only backed-up DFGT Logitech packages')) {
    foreach ($packageName in $packageNames) {
        & pnputil.exe /delete-driver $packageName /force
        if ($LASTEXITCODE -ne 0) {
            throw "pnputil failed to remove $packageName (exit $LASTEXITCODE)."
        }
    }
    & pnputil.exe /scan-devices
    if ($LASTEXITCODE -ne 0) { throw 'PnP device rescan failed.' }

    & (Join-Path $PSScriptRoot 'Install-DfgtFfb.ps1') `
        -InstallRoot $resolvedInstall `
        -TargetUserSid ([string]$snapshot.accountSid) `
        -TargetUserProfile ([string]$snapshot.userProfile) `
        -Confirm:$false
    if ($LASTEXITCODE -ne 0) {
        throw 'Replacement registration repair failed.'
    }
}

[pscustomobject]@{
    changed = -not [bool]$WhatIfPreference
    removedPackages = $packageNames
    backupDirectory = $resolvedBackup
    installRoot = $resolvedInstall
    next = 'Reconnect the DFGT, verify Microsoft HID binding, then reboot before checking HVCI.'
} | ConvertTo-Json -Depth 4
