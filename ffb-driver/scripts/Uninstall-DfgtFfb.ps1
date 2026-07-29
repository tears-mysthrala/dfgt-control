[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [string]$InstallRoot = (Join-Path $env:ProgramFiles 'DfgtControl')
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
$class64 = "HKLM:\SOFTWARE\Classes\CLSID\$clsid"
$class32 = "HKLM:\SOFTWARE\Classes\WOW6432Node\CLSID\$clsid"
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
if ([string]$manifest.targetUserSid -notmatch
        '^S-1-5-21-(?:[0-9]+-){3}[0-9]+$') {
    throw 'Install manifest has no valid target user SID.'
}
$targetUserSid = [string]$manifest.targetUserSid
$targetUserProfile = [IO.Path]::GetFullPath(
    [string]$manifest.targetUserProfile).TrimEnd('\')
$userHive = "Registry::HKEY_USERS\$targetUserSid"
if (-not (Test-Path -LiteralPath $userHive)) {
    throw "Target user hive is not loaded: $targetUserSid"
}
$userFfRoot = "$userHive\SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_046D&PID_C29A\OEMForceFeedback"
$runKey = "$userHive\Software\Microsoft\Windows\CurrentVersion\Run"

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
            $runValue = (Get-ItemProperty -LiteralPath $runKey `
                -Name $runName -ErrorAction SilentlyContinue).$runName
            if ($runValue) {
                $candidate = ([string]$runValue).Trim().Trim('"')
                if ([IO.Path]::GetFullPath($candidate).StartsWith(
                        "$resolvedInstallRoot\",
                        [StringComparison]::OrdinalIgnoreCase)) {
                    Remove-ItemProperty -LiteralPath $runKey -Name $runName
                }
            }
        }
    }
    $shortcutPath = Join-Path $targetUserProfile `
        'AppData\Roaming\Microsoft\Windows\Start Menu\Programs\DFGT Control.lnk'
    if (Test-Path -LiteralPath $shortcutPath -PathType Leaf) {
        $shell = New-Object -ComObject WScript.Shell
        $shortcut = $shell.CreateShortcut($shortcutPath)
        if ([IO.Path]::GetFullPath($shortcut.TargetPath) -eq
            [IO.Path]::GetFullPath(
                (Join-Path $resolvedInstallRoot 'bin\x64\DfgtControl.exe'))) {
            Remove-Item -LiteralPath $shortcutPath -Force
        }
    }

    Remove-OwnedForceFeedbackRoot -Path $ffRoot
    Remove-OwnedForceFeedbackRoot -Path $userFfRoot
    foreach ($class in @($class64, $class32)) {
        if (Test-Path -LiteralPath $class) {
            Remove-Item -LiteralPath $class -Recurse -Force
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
    restoredRegistryBackups = 0
    legacyRestore = 'Use Restore-LogitechLegacyStack.ps1 with a verified schema 3 project snapshot.'
} | ConvertTo-Json
