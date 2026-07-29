[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [string]$InstallRoot = (Join-Path $env:ProgramFiles 'DfgtControl'),
    [string]$BackupRoot = (Join-Path $env:ProgramData 'DfgtControl\backups'),

    [Parameter(Mandatory)]
    [string]$TargetUserSid,

    [Parameter(Mandatory)]
    [string]$TargetUserProfile
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run the installer from an elevated PowerShell process.'
}
if ($TargetUserSid -notmatch '^S-1-5-21-(?:[0-9]+-){3}[0-9]+$') {
    throw 'TargetUserSid is not an allowlisted interactive user SID.'
}
$resolvedUserProfile = [IO.Path]::GetFullPath($TargetUserProfile).TrimEnd('\')
$userHive = "Registry::HKEY_USERS\$TargetUserSid"
if (-not (Test-Path -LiteralPath $userHive)) {
    throw "Target user hive is not loaded: $TargetUserSid"
}
if (-not (Test-Path -LiteralPath $resolvedUserProfile -PathType Container)) {
    throw "Target user profile does not exist: $resolvedUserProfile"
}

$driverRoot = Split-Path -Parent $PSScriptRoot
function Resolve-Artifact {
    param([Parameter(Mandatory)][string[]]$Candidates)
    foreach ($candidate in $Candidates) {
        $path = Join-Path $driverRoot $candidate
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return $path
        }
    }
    return Join-Path $driverRoot $Candidates[0]
}

$provider64 = Resolve-Artifact @(
    'bin\x64\DfgtDirectInputFfb.dll',
    'build-x64\Release\DfgtDirectInputFfb.dll')
$provider32 = Resolve-Artifact @(
    'bin\x86\DfgtDirectInputFfb.dll',
    'build-x86\Release\DfgtDirectInputFfb.dll')
$broker = Resolve-Artifact @(
    'bin\x64\DfgtFfbBroker.exe',
    'build-x64\Release\DfgtFfbBroker.exe')
$agent = Resolve-Artifact @(
    'bin\x64\DfgtAgent.exe',
    'build-x64\Release\DfgtAgent.exe')
$ui = Resolve-Artifact @(
    'bin\x64\DfgtControl.exe',
    'ui\publish\DfgtControl.exe')
foreach ($artifact in @($provider64, $provider32, $broker, $agent, $ui)) {
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
        throw "Required release artifact is missing: $artifact"
    }
}

$clsid = '{806A045D-E7E8-491E-A607-31495A7BF4A3}'
$oemRoot = 'HKLM:\SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_046D&PID_C29A'
$ffRoot = Join-Path $oemRoot 'OEMForceFeedback'
$userOemRoot = "$userHive\SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_046D&PID_C29A"
$userFfRoot = Join-Path $userOemRoot 'OEMForceFeedback'
$class64 = "HKLM:\SOFTWARE\Classes\CLSID\$clsid"
$class32 = "HKLM:\SOFTWARE\Classes\WOW6432Node\CLSID\$clsid"
$runKey = "$userHive\Software\Microsoft\Windows\CurrentVersion\Run"
$legacyRunName = 'DfgtFfbBroker'
$runName = 'DfgtControlAgent'

function New-EffectAttributes {
    param(
        [uint32]$Id,
        [uint32]$Type,
        [uint32]$StaticParameters,
        [uint32]$DynamicParameters,
        [uint32]$Coordinates
    )
    $result = [byte[]]::new(20)
    $values = @($Id, $Type, $StaticParameters, $DynamicParameters, $Coordinates)
    for ($index = 0; $index -lt $values.Count; $index++) {
        [BitConverter]::GetBytes([uint32]$values[$index]).CopyTo($result, $index * 4)
    }
    return ,$result
}

function Set-DefaultRegistryValue {
    param([string]$Path, [string]$Value)
    Set-Item -LiteralPath $Path -Value $Value
}

# Implemented parameter contract:
# software effects: duration, gain, axes, direction, envelope, type params,
# start delay; conditions: the same except envelope.
$softwareParams = [uint32]0x000003E5
$customParams = [uint32]($softwareParams -bor 0x00000002)
$conditionParams = [uint32]0x00000365
$cartesian = [uint32]0x00000010
$effects = @(
    @{ Guid = '{13541C20-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Constant';       Id = 0;  Type = 0x00008601; Params = $softwareParams },
    @{ Guid = '{13541C21-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Ramp Force';     Id = 1;  Type = 0x00008602; Params = $softwareParams },
    @{ Guid = '{13541C22-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Square Wave';    Id = 2;  Type = 0x00008603; Params = $softwareParams },
    @{ Guid = '{13541C23-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Sine Wave';      Id = 3;  Type = 0x00008603; Params = $softwareParams },
    @{ Guid = '{13541C24-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Triangle Wave';  Id = 4;  Type = 0x00008603; Params = $softwareParams },
    @{ Guid = '{13541C25-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Sawtooth Up';    Id = 5;  Type = 0x00008603; Params = $softwareParams },
    @{ Guid = '{13541C26-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Sawtooth Down';  Id = 6;  Type = 0x00008603; Params = $softwareParams },
    @{ Guid = '{13541C27-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Spring';         Id = 7;  Type = 0x0000D804; Params = $conditionParams },
    @{ Guid = '{13541C28-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Damper';         Id = 8;  Type = 0x0000D804; Params = $conditionParams },
    @{ Guid = '{13541C29-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Inertia';        Id = 9;  Type = 0x0000D804; Params = $conditionParams },
    @{ Guid = '{13541C2A-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Friction';       Id = 10; Type = 0x0000D804; Params = $conditionParams },
    @{ Guid = '{13541C2B-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Custom Force';   Id = 0x100; Type = 0x00008605; Params = $customParams }
)

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $BackupRoot $timestamp
$bin64 = Join-Path $InstallRoot 'bin\x64'
$bin32 = Join-Path $InstallRoot 'bin\x86'
$installedProvider64 = Join-Path $bin64 'DfgtDirectInputFfb.dll'
$installedProvider32 = Join-Path $bin32 'DfgtDirectInputFfb.dll'
$installedBroker = Join-Path $bin64 'DfgtFfbBroker.exe'
$installedAgent = Join-Path $bin64 'DfgtAgent.exe'
$installedUi = Join-Path $bin64 'DfgtControl.exe'

if ($PSCmdlet.ShouldProcess($InstallRoot, 'Install DFGT user-mode FFB stack and registry')) {
    New-Item -ItemType Directory -Path $backup, $bin64, $bin32 -Force | Out-Null

    if (Test-Path -LiteralPath $oemRoot) {
        & reg.exe export 'HKLM\SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_046D&PID_C29A' (Join-Path $backup 'oem-before.reg') /y | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Could not back up the DFGT OEM registry branch.' }
    }
    if (Test-Path -LiteralPath $userOemRoot) {
        & reg.exe export "HKEY_USERS\$TargetUserSid\SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_046D&PID_C29A" (Join-Path $backup 'oem-user-before.reg') /y | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw 'Could not back up the per-user DFGT OEM registry branch.'
        }
    }

    Copy-Item -LiteralPath $provider64 -Destination $installedProvider64 -Force
    Copy-Item -LiteralPath $provider32 -Destination $installedProvider32 -Force
    Copy-Item -LiteralPath $broker -Destination $installedBroker -Force
    Copy-Item -LiteralPath $agent -Destination $installedAgent -Force
    Copy-Item -LiteralPath $ui -Destination $installedUi -Force

    foreach ($class in @(
        @{ Path = $class64; Dll = $installedProvider64 },
        @{ Path = $class32; Dll = $installedProvider32 }
    )) {
        $inProc = Join-Path $class.Path 'InProcServer32'
        New-Item -Path $inProc -Force | Out-Null
        Set-DefaultRegistryValue -Path $class.Path -Value 'DFGT Control DirectInput FFB Provider'
        Set-DefaultRegistryValue -Path $inProc -Value $class.Dll
        New-ItemProperty -LiteralPath $inProc -Name ThreadingModel -Value Both -PropertyType String -Force | Out-Null
    }

    foreach ($forceFeedbackRoot in @($ffRoot, $userFfRoot)) {
        $effectsRoot = Join-Path $forceFeedbackRoot 'Effects'
        New-Item -Path $forceFeedbackRoot -Force | Out-Null
        # DIFFDEVICEATTRIBUTES: reserved flags, 2 ms FFB sample period, 2 ms
        # minimum time resolution. This matches the provider's mixer timer.
        New-ItemProperty -LiteralPath $forceFeedbackRoot -Name Attributes `
            -Value ([byte[]](0,0,0,0,208,7,0,0,208,7,0,0)) `
            -PropertyType Binary -Force | Out-Null
        New-ItemProperty -LiteralPath $forceFeedbackRoot -Name CLSID `
            -Value $clsid -PropertyType String -Force | Out-Null
        if (Test-Path -LiteralPath $effectsRoot) {
            Remove-Item -LiteralPath $effectsRoot -Recurse -Force
        }
        New-Item -Path $effectsRoot -Force | Out-Null
        foreach ($effect in $effects) {
            $effectPath = Join-Path $effectsRoot $effect.Guid
            New-Item -Path $effectPath -Force | Out-Null
            Set-DefaultRegistryValue -Path $effectPath -Value $effect.Name
            $attributes = New-EffectAttributes -Id $effect.Id `
                -Type $effect.Type -StaticParameters $effect.Params `
                -DynamicParameters $effect.Params -Coordinates $cartesian
            New-ItemProperty -LiteralPath $effectPath -Name Attributes `
                -Value $attributes -PropertyType Binary -Force | Out-Null
        }
    }

    # The inbox HID descriptor exposes independent active-low pedal usages.
    # Logitech's per-user metadata labels the Z usage as "Combined pedals";
    # replace that stale presentation while preserving all button labels.
    foreach ($metadataRoot in @($oemRoot, $userOemRoot)) {
        $brakeAxis = Join-Path $metadataRoot 'Axes\1'
        $acceleratorAxis = Join-Path $metadataRoot 'Axes\2'
        if (Test-Path -LiteralPath $brakeAxis) {
            Set-DefaultRegistryValue -Path $brakeAxis -Value 'Brake'
        }
        if (Test-Path -LiteralPath $acceleratorAxis) {
            Set-DefaultRegistryValue -Path $acceleratorAxis `
                -Value 'Accelerator'
        }
    }

    New-Item -Path $runKey -Force | Out-Null
    Remove-ItemProperty -LiteralPath $runKey -Name $legacyRunName `
        -ErrorAction SilentlyContinue
    New-ItemProperty -LiteralPath $runKey -Name $runName `
        -Value "`"$installedAgent`"" -PropertyType String -Force | Out-Null

    $programs = Join-Path $resolvedUserProfile `
        'AppData\Roaming\Microsoft\Windows\Start Menu\Programs'
    New-Item -ItemType Directory -Path $programs -Force | Out-Null
    $shortcutPath = Join-Path $programs 'DFGT Control.lnk'
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $installedUi
    $shortcut.WorkingDirectory = $bin64
    $shortcut.Description = 'Configurar Logitech Driving Force GT'
    $shortcut.Save()

    $manifest = [ordered]@{
        installedAt = (Get-Date).ToString('o')
        installedBy = $identity.Name
        targetUserSid = $TargetUserSid
        targetUserProfile = $resolvedUserProfile
        installRoot = $InstallRoot
        backup = $backup
        providerClsid = $clsid
        registrationScopes = @('HKLM', 'HKCU')
        effects = @($effects | ForEach-Object { $_.Name })
        files = @(
            $installedProvider64,
            $installedProvider32,
            $installedBroker,
            $installedAgent,
            $installedUi
        ) | ForEach-Object {
            $hash = Get-FileHash -LiteralPath $_ -Algorithm SHA256
            [ordered]@{ path = $_; sha256 = $hash.Hash }
        }
    }
    $manifestJson = $manifest | ConvertTo-Json -Depth 6
    [IO.File]::WriteAllText(
        (Join-Path $InstallRoot 'install-manifest.json'),
        $manifestJson,
        [Text.UTF8Encoding]::new($false))
}

[pscustomobject]@{
    installRoot = $InstallRoot
    backup = $backup
    providerClsid = $clsid
    advertisedEffects = @($effects | ForEach-Object { $_.Name })
    requiresBrokerStart = $true
    changed = -not [bool]$WhatIfPreference
} | ConvertTo-Json -Depth 5
