[CmdletBinding()]
param(
    [string]$BackupRoot = (Join-Path $env:ProgramData 'DfgtControl\legacy-backups')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run the backup from an elevated PowerShell process.'
}

$dfgtDrivers = @(
    Get-CimInstance Win32_PnPSignedDriver |
        Where-Object {
            $_.DeviceID -like '*VID_046D&PID_C29A*' -and
            $_.InfName -match '^oem[0-9]+\.inf$'
        }
)
if (-not $dfgtDrivers) {
    throw 'No connected DFGT using a third-party OEM package was found.'
}
if ($dfgtDrivers | Where-Object {
        $_.DriverProviderName -notmatch 'Logitech'
    }) {
    throw 'A DFGT OEM package is not identified as Logitech; refusing backup.'
}
$packageNames = @($dfgtDrivers.InfName | Sort-Object -Unique)

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $BackupRoot $timestamp
$packages = Join-Path $backup 'driver-packages'
$registry = Join-Path $backup 'registry'
New-Item -ItemType Directory -Path $packages, $registry -Force | Out-Null

$packageEvidence = @()
foreach ($packageName in $packageNames) {
    $infPath = Join-Path $env:windir "INF\$packageName"
    if (-not (Test-Path -LiteralPath $infPath -PathType Leaf)) {
        throw "Bound package INF is missing: $infPath"
    }
    $infText = Get-Content -LiteralPath $infPath -Raw
    if ($infText -notmatch '(?i)VID_046D&PID_C29A') {
        throw "$packageName does not declare VID_046D&PID_C29A."
    }

    $destination = Join-Path $packages $packageName
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    & pnputil.exe /export-driver $packageName $destination | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Could not export driver package $packageName."
    }
    $resolvedDestination = [IO.Path]::GetFullPath($destination).TrimEnd('\')
    $exportedFiles = @(Get-ChildItem $destination -File -Recurse)
    if (-not ($exportedFiles | Where-Object Extension -eq '.inf')) {
        throw "Exported package $packageName contains no INF file."
    }
    $fileEvidence = @(
        foreach ($file in $exportedFiles) {
            $resolvedFile = [IO.Path]::GetFullPath($file.FullName)
            if (-not $resolvedFile.StartsWith(
                    "$resolvedDestination\",
                    [StringComparison]::OrdinalIgnoreCase)) {
                throw "Exported file escaped its package directory."
            }
            [ordered]@{
                relativePath = [IO.Path]::GetRelativePath(
                    $resolvedDestination,
                    $resolvedFile)
                length = $file.Length
                sha256 = (Get-FileHash $resolvedFile -Algorithm SHA256).Hash
            }
        }
    )
    $boundToPackage = @(
        Get-CimInstance Win32_PnPSignedDriver |
            Where-Object InfName -eq $packageName |
            Select-Object DeviceName, DeviceID, InfName, DriverVersion,
                DriverProviderName
    )
    $packageEvidence += [ordered]@{
        publishedName = $packageName
        infSha256 = (Get-FileHash $infPath -Algorithm SHA256).Hash
        devices = $boundToPackage
        files = $fileEvidence
    }
}

$registryExports = @(
    @{
        Key = 'HKLM\SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_046D&PID_C29A'
        File = 'dfgt-oem.reg'
    },
    @{
        Key = 'HKLM\SOFTWARE\Classes\CLSID\{8D533A4E-7A5F-11D3-8297-0050DA1A72D3}'
        File = 'logitech-ffb-com-x64.reg'
    },
    @{
        Key = 'HKLM\SOFTWARE\Classes\WOW6432Node\CLSID\{8D533A4E-7A5F-11D3-8297-0050DA1A72D3}'
        File = 'logitech-ffb-com-x86.reg'
    }
)
foreach ($item in $registryExports) {
    & reg.exe query $item.Key *> $null
    if ($LASTEXITCODE -ne 0) { continue }
    & reg.exe export $item.Key (Join-Path $registry $item.File) /y | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Could not export registry key $($item.Key)."
    }
}

$registryEvidence = @(
    Get-ChildItem -LiteralPath $registry -File -Filter '*.reg' |
        ForEach-Object {
            [ordered]@{
                file = $_.Name
                length = $_.Length
                sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash
            }
        }
)
$machineGuid = (Get-ItemProperty `
    'HKLM:\SOFTWARE\Microsoft\Cryptography' -Name MachineGuid).MachineGuid
$windowsBuild = [Environment]::OSVersion.Version.Build
$snapshot = [ordered]@{
    schemaVersion = 3
    capturedAt = (Get-Date).ToString('o')
    computer = $env:COMPUTERNAME
    account = $identity.Name
    accountSid = $identity.User.Value
    userProfile = [Environment]::GetFolderPath('UserProfile')
    machineGuid = $machineGuid
    windowsBuild = $windowsBuild
    backupDirectory = $backup
    dfgtHardwareId = 'VID_046D&PID_C29A'
    dfgtPackages = $packageNames
    packageEvidence = $packageEvidence
    registryEvidence = $registryEvidence
    dfgtDevices = @($dfgtDrivers | Select-Object DeviceName, DeviceID,
        InfName, DriverVersion, DriverProviderName)
}
[IO.File]::WriteAllText(
    (Join-Path $backup 'snapshot.json'),
    ($snapshot | ConvertTo-Json -Depth 8),
    [Text.UTF8Encoding]::new($false))

$snapshot | ConvertTo-Json -Depth 8
