[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [string]$InstallRoot = (Join-Path $env:ProgramFiles 'DfgtControl'),
    [string[]]$RestoreRegistryBackup
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run the uninstaller from an elevated PowerShell process.'
}

$clsid = '{806A045D-E7E8-491E-A607-31495A7BF4A3}'
$ffRoot = 'HKLM:\SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_046D&PID_C29A\OEMForceFeedback'
$userFfRoot = 'HKCU:\SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_046D&PID_C29A\OEMForceFeedback'
$class64 = "HKLM:\SOFTWARE\Classes\CLSID\$clsid"
$class32 = "HKLM:\SOFTWARE\Classes\WOW6432Node\CLSID\$clsid"
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$runNames = @('DfgtFfbBroker', 'DfgtControlAgent')
$manifestPath = Join-Path $InstallRoot 'install-manifest.json'

if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Refusing recursive cleanup because the install manifest is missing: $manifestPath"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$resolvedInstallRoot = [IO.Path]::GetFullPath($InstallRoot).TrimEnd('\')
$manifestRoot = [IO.Path]::GetFullPath([string]$manifest.installRoot).TrimEnd('\')
if ($resolvedInstallRoot -ne $manifestRoot -or
    [IO.Path]::GetFileName($resolvedInstallRoot) -ne 'DfgtControl') {
    throw "Refusing cleanup of an unexpected install root: $resolvedInstallRoot"
}

$ownedFiles = @(
    foreach ($file in $manifest.files) {
        $resolvedFile = [IO.Path]::GetFullPath([string]$file.path)
        if (-not $resolvedFile.StartsWith(
                "$resolvedInstallRoot\",
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "Manifest file escapes the install root: $resolvedFile"
        }
        $resolvedFile
    }
)
$unexpectedFiles = @(
    Get-ChildItem -LiteralPath $resolvedInstallRoot -File -Recurse |
        Where-Object {
            $_.FullName -ne $manifestPath -and
            $_.FullName -notin $ownedFiles
        }
)
if ($unexpectedFiles) {
    throw "Refusing recursive cleanup because unowned files exist: $($unexpectedFiles.FullName -join ', ')"
}

if ($RestoreRegistryBackup) {
    $resolvedBackups = @(
        foreach ($registryBackup in $RestoreRegistryBackup) {
            $resolvedBackup = [IO.Path]::GetFullPath($registryBackup)
            if (-not (Test-Path -LiteralPath $resolvedBackup -PathType Leaf) -or
                [IO.Path]::GetExtension($resolvedBackup) -ne '.reg') {
                throw "Registry backup is missing or is not a .reg file: $resolvedBackup"
            }
            $resolvedBackup
        }
    )
}
else {
    $resolvedBackups = @()
}

function Remove-OwnedForceFeedbackRoot {
    param([Parameter(Mandatory)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }
    $currentClsid = (Get-ItemProperty -LiteralPath $Path -Name CLSID `
        -ErrorAction SilentlyContinue).CLSID
    if ($currentClsid -eq $clsid) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
    else {
        Write-Warning "OEMForceFeedback at $Path is owned by $currentClsid; it was not removed."
    }
}

if ($PSCmdlet.ShouldProcess($resolvedInstallRoot, 'Remove DFGT user-mode FFB stack')) {
    Get-CimInstance Win32_Process |
        Where-Object {
            $_.Name -in @('DfgtFfbBroker.exe', 'DfgtAgent.exe', 'DfgtControl.exe') -and
            $_.ExecutablePath -and
            [IO.Path]::GetFullPath($_.ExecutablePath).StartsWith(
                $resolvedInstallRoot + '\',
                [StringComparison]::OrdinalIgnoreCase)
        } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force }

    if (Test-Path -LiteralPath $runKey) {
        foreach ($runName in $runNames) {
            Remove-ItemProperty -LiteralPath $runKey -Name $runName `
                -ErrorAction SilentlyContinue
        }
    }
    $shortcutPath = Join-Path `
        ([Environment]::GetFolderPath('Programs')) 'DFGT Control.lnk'
    Remove-Item -LiteralPath $shortcutPath -Force -ErrorAction SilentlyContinue

    Remove-OwnedForceFeedbackRoot -Path $ffRoot
    Remove-OwnedForceFeedbackRoot -Path $userFfRoot
    foreach ($class in @($class64, $class32)) {
        if (Test-Path -LiteralPath $class) {
            Remove-Item -LiteralPath $class -Recurse -Force
        }
    }

    foreach ($resolvedBackup in $resolvedBackups) {
        & reg.exe import $resolvedBackup | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Registry backup import failed: $resolvedBackup"
        }
    }

    foreach ($ownedFile in $ownedFiles) {
        Remove-Item -LiteralPath $ownedFile -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $manifestPath -Force
    Get-ChildItem -LiteralPath $resolvedInstallRoot -Directory -Recurse |
        Sort-Object { $_.FullName.Length } -Descending |
        ForEach-Object {
            if (-not (Get-ChildItem -LiteralPath $_.FullName -Force)) {
                Remove-Item -LiteralPath $_.FullName -Force
            }
        }
    if (-not (Get-ChildItem -LiteralPath $resolvedInstallRoot -Force)) {
        Remove-Item -LiteralPath $resolvedInstallRoot -Force
    }
}

[pscustomobject]@{
    removed = -not [bool]$WhatIfPreference
    installRoot = $resolvedInstallRoot
    restoredRegistryBackups = $resolvedBackups.Count
} | ConvertTo-Json
