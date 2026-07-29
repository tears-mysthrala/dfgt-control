[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [string]$InstallRoot = (Join-Path $env:ProgramFiles 'DfgtControl'),
    [string]$BackupRoot = (Join-Path $env:ProgramData 'DfgtControl\backups'),

    [Parameter(Mandatory)]
    [string]$ExpectedVersion,

    [Parameter(Mandatory)]
    [string]$TargetUserSid,

    [Parameter(Mandatory)]
    [string]$TargetUserProfile
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
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
$releaseManifestPath = Join-Path $driverRoot 'release-manifest.json'
if (-not (Test-Path -LiteralPath $releaseManifestPath -PathType Leaf)) {
    throw "Release manifest is missing: $releaseManifestPath"
}
$releaseManifest = Get-Content -LiteralPath $releaseManifestPath -Raw |
    ConvertFrom-Json
if ($releaseManifest.schemaVersion -ne 1 -or
    [string]$releaseManifest.version -ne $ExpectedVersion -or
    [string]$releaseManifest.commitSha -notmatch '^[0-9a-f]{40}$') {
    throw 'Release manifest schema, version or commit is invalid.'
}
$manifestHashes = @{}
foreach ($entry in $releaseManifest.files) {
    $relative = [string]$entry.path
    if ([IO.Path]::IsPathRooted($relative) -or
        $relative.Contains('..') -or
        [string]$entry.sha256 -notmatch '^[0-9A-Fa-f]{64}$') {
        throw "Unsafe release-manifest entry: $relative"
    }
    $candidate = [IO.Path]::GetFullPath(
        (Join-Path $driverRoot $relative))
    if (-not $candidate.StartsWith(
            ([IO.Path]::GetFullPath($driverRoot).TrimEnd('\') + '\'),
            [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "Release artifact is missing or escapes its root: $relative"
    }
    $actualHash = (Get-FileHash -LiteralPath $candidate `
        -Algorithm SHA256).Hash
    if ($actualHash -ne [string]$entry.sha256) {
        throw "Release artifact hash mismatch: $relative"
    }
    $manifestHashes[$candidate.ToLowerInvariant()] = $actualHash
    if ([IO.Path]::GetExtension($candidate) -in @(
            '.exe', '.dll', '.ps1')) {
        $signature = Get-AuthenticodeSignature -LiteralPath $candidate
        if ($signature.Status -notin @('Valid', 'NotSigned')) {
            throw "Invalid Authenticode state for $relative`: $($signature.Status)"
        }
    }
}
$packageFiles = @(
    Get-ChildItem -LiteralPath $driverRoot -File -Recurse |
        Where-Object {
            $_.FullName -notmatch '\\(?:build-|ui\\(?:bin|obj|publish))' -and
            $_.FullName -ne $releaseManifestPath
        }
)
$unexpectedFiles = @(
    $packageFiles |
        Where-Object {
            -not $manifestHashes.ContainsKey($_.FullName.ToLowerInvariant())
        }
)
if ($unexpectedFiles) {
    throw "Release contains unmanifested files: $($unexpectedFiles.FullName -join ', ')"
}

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

function Assert-PeArchitecture {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][ValidateSet('x86', 'x64')][string]$Architecture
    )
    $stream = [IO.File]::OpenRead($Path)
    try {
        $reader = [IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) {
            throw "Artifact is not a PE image: $Path"
        }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadUInt32()
        if ($peOffset -gt $stream.Length - 6) {
            throw "Artifact has an invalid PE header: $Path"
        }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "Artifact has an invalid PE signature: $Path"
        }
        $machine = $reader.ReadUInt16()
        $expectedMachine = if ($Architecture -eq 'x64') {
            0x8664
        }
        else { 0x014c }
        if ($machine -ne $expectedMachine) {
            throw "Artifact architecture mismatch for $Path"
        }
    }
    finally {
        $stream.Dispose()
    }
}

$provider64 = Resolve-Artifact @(
    'bin\x64\DfgtDirectInputFfb.dll',
    'build-x64\Release\DfgtDirectInputFfb.dll')
$provider32 = Resolve-Artifact @(
    'bin\x86\DfgtDirectInputFfb.dll',
    'build-x86\Release\DfgtDirectInputFfb.dll')
$smoke64 = Resolve-Artifact @(
    'bin\x64\DfgtFfbComSmoke.exe',
    'build-x64\Release\DfgtFfbComSmoke.exe')
$smoke32 = Resolve-Artifact @(
    'bin\x86\DfgtFfbComSmoke.exe',
    'build-x86\Release\DfgtFfbComSmoke.exe')
$broker = Resolve-Artifact @(
    'bin\x64\DfgtFfbBroker.exe',
    'build-x64\Release\DfgtFfbBroker.exe')
$statusSmoke = Resolve-Artifact @(
    'bin\x64\DfgtBrokerStatusSmoke.exe',
    'build-x64\Release\DfgtBrokerStatusSmoke.exe')
$maintenance = Resolve-Artifact @(
    'bin\x64\DfgtMaintenance.exe',
    'build-x64\Release\DfgtMaintenance.exe')
$agent = Resolve-Artifact @(
    'bin\x64\DfgtAgent.exe',
    'build-x64\Release\DfgtAgent.exe')
$ui = Resolve-Artifact @(
    'bin\x64\DfgtControl.exe',
    'ui\publish\DfgtControl.exe')
foreach ($artifact in @(
    $provider64, $provider32, $smoke64, $smoke32,
    $broker, $statusSmoke, $maintenance, $agent, $ui)) {
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
        throw "Required release artifact is missing: $artifact"
    }
}
foreach ($artifact in @(
    $provider64, $smoke64, $broker, $statusSmoke,
    $maintenance, $agent, $ui)) {
    Assert-PeArchitecture -Path $artifact -Architecture x64
}
foreach ($artifact in @($provider32, $smoke32)) {
    Assert-PeArchitecture -Path $artifact -Architecture x86
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
$resolvedInstallRoot = [IO.Path]::GetFullPath($InstallRoot).TrimEnd('\')
if (-not [IO.Path]::GetFileName($resolvedInstallRoot) -or
    $resolvedInstallRoot -in @(
        [IO.Path]::GetPathRoot($resolvedInstallRoot).TrimEnd('\'),
        [IO.Path]::GetFullPath($env:ProgramFiles).TrimEnd('\'),
        [IO.Path]::GetFullPath($env:ProgramData).TrimEnd('\'),
        [IO.Path]::GetFullPath($env:WINDIR).TrimEnd('\'),
        [IO.Path]::GetFullPath(
            [Environment]::GetFolderPath('UserProfile')).TrimEnd('\'))) {
    throw "Refusing an unexpected install root: $resolvedInstallRoot"
}
$stagingRoot = "$resolvedInstallRoot.staging-$timestamp"
$previousRoot = "$resolvedInstallRoot.previous-$timestamp"
$bin64 = Join-Path $InstallRoot 'bin\x64'
$bin32 = Join-Path $InstallRoot 'bin\x86'
$installedProvider64 = Join-Path $bin64 'DfgtDirectInputFfb.dll'
$installedProvider32 = Join-Path $bin32 'DfgtDirectInputFfb.dll'
$installedSmoke64 = Join-Path $bin64 'DfgtFfbComSmoke.exe'
$installedSmoke32 = Join-Path $bin32 'DfgtFfbComSmoke.exe'
$installedBroker = Join-Path $bin64 'DfgtFfbBroker.exe'
$installedStatusSmoke = Join-Path $bin64 'DfgtBrokerStatusSmoke.exe'
$installedMaintenance = Join-Path $bin64 'DfgtMaintenance.exe'
$installedAgent = Join-Path $bin64 'DfgtAgent.exe'
$installedUi = Join-Path $bin64 'DfgtControl.exe'
$installedTransactionMarker = Join-Path $InstallRoot `
    '.dfgt-transaction.json'
$installedCompleteMarker = Join-Path $InstallRoot '.dfgt-complete.json'

if ($PSCmdlet.ShouldProcess($InstallRoot, 'Install DFGT user-mode FFB stack and registry')) {
    if (-not $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run the installer from an elevated PowerShell process.'
    }
    $installParent = Split-Path -Parent $resolvedInstallRoot
    $installLeaf = [IO.Path]::GetFileName($resolvedInstallRoot)
    $orphanPrevious = @(
        Get-ChildItem -LiteralPath $installParent -Directory `
            -ErrorAction SilentlyContinue |
            Where-Object Name -like "$installLeaf.previous-*"
    )
    $orphanStaging = @(
        Get-ChildItem -LiteralPath $installParent -Directory `
            -ErrorAction SilentlyContinue |
            Where-Object Name -like "$installLeaf.staging-*"
    )
    if (-not (Test-Path -LiteralPath $resolvedInstallRoot) -and
        $orphanPrevious.Count -eq 1 -and
        (Test-Path -LiteralPath (
            Join-Path $orphanPrevious[0].FullName 'install-manifest.json') `
            -PathType Leaf)) {
        Move-Item -LiteralPath $orphanPrevious[0].FullName `
            -Destination $resolvedInstallRoot
        foreach ($orphan in $orphanStaging) {
            if (Test-Path -LiteralPath (
                    Join-Path $orphan.FullName '.dfgt-transaction.json') `
                    -PathType Leaf) {
                Remove-Item -LiteralPath $orphan.FullName -Recurse -Force
            }
        }
        throw 'Recovered the previous installation after an interrupted transaction. Review it, then run the installer again.'
    }
    if ($orphanPrevious -or $orphanStaging) {
        throw 'An ambiguous interrupted installation exists; no files were changed. Review the staging/previous directories.'
    }
    $swapped = $false
    try {
    if ((Test-Path -LiteralPath $stagingRoot) -or
        (Test-Path -LiteralPath $previousRoot)) {
        throw 'Staging or rollback path already exists; refusing ambiguous upgrade.'
    }
    $stageBin64 = Join-Path $stagingRoot 'bin\x64'
    $stageBin32 = Join-Path $stagingRoot 'bin\x86'
    New-Item -ItemType Directory -Path $backup, $stageBin64, $stageBin32 `
        -Force | Out-Null
    $transaction = [ordered]@{
        schemaVersion = 1
        state = 'staging'
        installRoot = $resolvedInstallRoot
        stagingRoot = $stagingRoot
        previousRoot = $previousRoot
        expectedVersion = $ExpectedVersion
        sourceCommit = [string]$releaseManifest.commitSha
        startedAt = (Get-Date).ToString('o')
    }
    $transactionJson = $transaction | ConvertTo-Json -Depth 4
    [IO.File]::WriteAllText(
        (Join-Path $stagingRoot '.dfgt-transaction.json'),
        $transactionJson,
        [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText(
        (Join-Path $backup 'install-journal.json'),
        $transactionJson,
        [Text.UTF8Encoding]::new($false))

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

    Copy-Item -LiteralPath $provider64 `
        -Destination (Join-Path $stageBin64 'DfgtDirectInputFfb.dll')
    Copy-Item -LiteralPath $provider32 `
        -Destination (Join-Path $stageBin32 'DfgtDirectInputFfb.dll')
    Copy-Item -LiteralPath $smoke64 `
        -Destination (Join-Path $stageBin64 'DfgtFfbComSmoke.exe')
    Copy-Item -LiteralPath $smoke32 `
        -Destination (Join-Path $stageBin32 'DfgtFfbComSmoke.exe')
    Copy-Item -LiteralPath $broker `
        -Destination (Join-Path $stageBin64 'DfgtFfbBroker.exe')
    Copy-Item -LiteralPath $statusSmoke `
        -Destination (Join-Path $stageBin64 'DfgtBrokerStatusSmoke.exe')
    Copy-Item -LiteralPath $maintenance `
        -Destination (Join-Path $stageBin64 'DfgtMaintenance.exe')
    Copy-Item -LiteralPath $agent `
        -Destination (Join-Path $stageBin64 'DfgtAgent.exe')
    Copy-Item -LiteralPath $ui `
        -Destination (Join-Path $stageBin64 'DfgtControl.exe')
    foreach ($pair in @(
        @{ Source = $provider64; Destination = (Join-Path $stageBin64 'DfgtDirectInputFfb.dll') },
        @{ Source = $provider32; Destination = (Join-Path $stageBin32 'DfgtDirectInputFfb.dll') },
        @{ Source = $smoke64; Destination = (Join-Path $stageBin64 'DfgtFfbComSmoke.exe') },
        @{ Source = $smoke32; Destination = (Join-Path $stageBin32 'DfgtFfbComSmoke.exe') },
        @{ Source = $broker; Destination = (Join-Path $stageBin64 'DfgtFfbBroker.exe') },
        @{ Source = $statusSmoke; Destination = (Join-Path $stageBin64 'DfgtBrokerStatusSmoke.exe') },
        @{ Source = $maintenance; Destination = (Join-Path $stageBin64 'DfgtMaintenance.exe') },
        @{ Source = $agent; Destination = (Join-Path $stageBin64 'DfgtAgent.exe') },
        @{ Source = $ui; Destination = (Join-Path $stageBin64 'DfgtControl.exe') }
    )) {
        $expectedHash = $manifestHashes[
            ([IO.Path]::GetFullPath($pair.Source)).ToLowerInvariant()]
        $stagedHash = (Get-FileHash -LiteralPath $pair.Destination `
            -Algorithm SHA256).Hash
        if (-not $expectedHash -or $stagedHash -ne $expectedHash) {
            throw "Staged artifact hash mismatch: $($pair.Destination)"
        }
    }

    $isUpgrade = Test-Path -LiteralPath $resolvedInstallRoot -PathType Container
    if ($isUpgrade) {
        $oldManifest = Join-Path $resolvedInstallRoot 'install-manifest.json'
        if (-not (Test-Path -LiteralPath $oldManifest -PathType Leaf)) {
            throw 'Existing install has no ownership manifest; refusing upgrade.'
        }
        $oldOwnership = Get-Content -LiteralPath $oldManifest -Raw |
            ConvertFrom-Json
        if ([IO.Path]::GetFullPath(
                [string]$oldOwnership.installRoot).TrimEnd('\') -ne
            $resolvedInstallRoot) {
            throw 'Existing ownership manifest targets another install root.'
        }
        $oldOwnedFiles = @(
            foreach ($file in $oldOwnership.files) {
                $candidate = [IO.Path]::GetFullPath([string]$file.path)
                if (-not $candidate.StartsWith(
                        "$resolvedInstallRoot\",
                        [StringComparison]::OrdinalIgnoreCase)) {
                    throw "Existing manifest escapes install root: $candidate"
                }
                $candidate
            }
        )
        $unowned = @(
            Get-ChildItem -LiteralPath $resolvedInstallRoot -File -Recurse |
                Where-Object {
                    $_.FullName -ne $oldManifest -and
                    $_.FullName -notin $oldOwnedFiles
                }
        )
        if ($unowned) {
            throw "Upgrade would move unowned files: $($unowned.FullName -join ', ')"
        }
        $presentDfgt = @(
            Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
                Where-Object InstanceId -like '*VID_046D&PID_C29A*'
        )
        if ($presentDfgt) {
            throw 'Disconnect the DFGT before upgrade. No process has been stopped.'
        }
        $installedMaintenanceTool = Join-Path $resolvedInstallRoot `
            'bin\x64\DfgtMaintenance.exe'
        $installedAgentProcess = Get-CimInstance Win32_Process |
            Where-Object {
                $_.Name -eq 'DfgtAgent.exe' -and
                $_.ExecutablePath -and
                [IO.Path]::GetFullPath($_.ExecutablePath).StartsWith(
                    "$resolvedInstallRoot\",
                    [StringComparison]::OrdinalIgnoreCase)
        }
        if ($installedAgentProcess) {
            if (Test-Path -LiteralPath $installedMaintenanceTool `
                    -PathType Leaf) {
                & $installedMaintenanceTool | Out-Null
                if ($LASTEXITCODE -ne 0) {
                    throw 'PrepareForMaintenance was not confirmed. Close games and retry with the wheel disconnected.'
                }
            }
        }
        Get-CimInstance Win32_Process |
            Where-Object {
                $_.Name -in @(
                    'DfgtFfbBroker.exe',
                    'DfgtAgent.exe',
                    'DfgtControl.exe') -and
                $_.ExecutablePath -and
                [IO.Path]::GetFullPath($_.ExecutablePath).StartsWith(
                    "$resolvedInstallRoot\",
                    [StringComparison]::OrdinalIgnoreCase)
            } |
            ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
        Move-Item -LiteralPath $resolvedInstallRoot -Destination $previousRoot
    }
    Move-Item -LiteralPath $stagingRoot -Destination $resolvedInstallRoot
    $swapped = $true

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

    $complete = [ordered]@{
        schemaVersion = 1
        state = 'complete'
        version = $ExpectedVersion
        sourceCommit = [string]$releaseManifest.commitSha
        completedAt = (Get-Date).ToString('o')
    }
    [IO.File]::WriteAllText(
        $installedCompleteMarker,
        ($complete | ConvertTo-Json -Depth 3),
        [Text.UTF8Encoding]::new($false))
    $manifest = [ordered]@{
        version = $ExpectedVersion
        sourceCommit = [string]$releaseManifest.commitSha
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
            $installedSmoke64,
            $installedSmoke32,
            $installedBroker,
            $installedStatusSmoke,
            $installedMaintenance,
            $installedAgent,
            $installedUi,
            $installedTransactionMarker,
            $installedCompleteMarker
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
    if (Test-Path -LiteralPath $previousRoot) {
        Remove-Item -LiteralPath $previousRoot -Recurse -Force
    }
    }
    catch {
        $failure = $_
        if ($swapped -and
            (Test-Path -LiteralPath $resolvedInstallRoot)) {
            Remove-Item -LiteralPath $resolvedInstallRoot -Recurse -Force
        }
        if (Test-Path -LiteralPath $previousRoot) {
            Move-Item -LiteralPath $previousRoot `
                -Destination $resolvedInstallRoot
        }
        foreach ($registryBackup in @(
            (Join-Path $backup 'oem-before.reg'),
            (Join-Path $backup 'oem-user-before.reg'))) {
            if (Test-Path -LiteralPath $registryBackup -PathType Leaf) {
                & reg.exe import $registryBackup | Out-Null
            }
        }
        if (-not (Test-Path -LiteralPath $previousRoot) -and
            -not (Test-Path -LiteralPath $resolvedInstallRoot)) {
            foreach ($ownedRoot in @($ffRoot, $userFfRoot)) {
                if (Test-Path -LiteralPath $ownedRoot) {
                    $owner = (Get-ItemProperty -LiteralPath $ownedRoot `
                        -Name CLSID -ErrorAction SilentlyContinue).CLSID
                    if ($owner -eq $clsid) {
                        Remove-Item -LiteralPath $ownedRoot -Recurse -Force
                    }
                }
            }
            foreach ($class in @($class64, $class32)) {
                if (Test-Path -LiteralPath $class) {
                    Remove-Item -LiteralPath $class -Recurse -Force
                }
            }
        }
        throw $failure
    }
    finally {
        if (Test-Path -LiteralPath $stagingRoot) {
            Remove-Item -LiteralPath $stagingRoot -Recurse -Force
        }
    }
}

[pscustomobject]@{
    installRoot = $InstallRoot
    backup = $backup
    providerClsid = $clsid
    advertisedEffects = @($effects | ForEach-Object { $_.Name })
    requiresBrokerStart = $true
    changed = -not [bool]$WhatIfPreference
} | ConvertTo-Json -Depth 5
