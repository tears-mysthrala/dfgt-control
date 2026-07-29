[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$expectedClsid = '{806A045D-E7E8-491E-A607-31495A7BF4A3}'
$legacyServices = @('WmHidLo', 'WmFilter', 'WmVirHid', 'WmBEnum', 'WmXlCore')
$manifestPath = Join-Path $env:ProgramFiles `
    'DfgtControl\install-manifest.json'
$manifest = if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
    Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
}
else {
    $null
}

$fileChecks = @(
    if ($manifest) {
        foreach ($file in $manifest.files) {
            $exists = Test-Path -LiteralPath $file.path -PathType Leaf
            [ordered]@{
                path = $file.path
                exists = $exists
                hashMatch = $exists -and
                    (Get-FileHash -LiteralPath $file.path `
                        -Algorithm SHA256).Hash -eq [string]$file.sha256
            }
        }
    }
)

$deviceGuard = Get-CimInstance `
    -Namespace root\Microsoft\Windows\DeviceGuard `
    -ClassName Win32_DeviceGuard
$hvciRegistry = (Get-ItemProperty `
    'HKLM:\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity' `
    -ErrorAction SilentlyContinue).Enabled
$secureBoot = try {
    [pscustomobject]@{
        querySucceeded = $true
        enabled = [bool](Confirm-SecureBootUEFI)
        error = $null
    }
}
catch {
    [pscustomobject]@{
        querySucceeded = $false
        enabled = $null
        error = $_.Exception.Message
    }
}
$serviceChecks = @(
    foreach ($serviceName in $legacyServices) {
        $service = Get-CimInstance Win32_SystemDriver `
            -Filter "Name = '$serviceName'" -ErrorAction SilentlyContinue
        [ordered]@{
            name = $serviceName
            present = [bool]$service
            state = if ($service) { $service.State } else { $null }
            running = [bool]$service -and $service.State -eq 'Running'
        }
    }
)
$ffRoots = @(
    'HKLM:\SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_046D&PID_C29A\OEMForceFeedback',
    'HKCU:\SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_046D&PID_C29A\OEMForceFeedback'
)
$providerChecks = @(
    foreach ($ffRoot in $ffRoots) {
        $actualClsid = (Get-ItemProperty -LiteralPath $ffRoot `
            -ErrorAction SilentlyContinue).CLSID
        [ordered]@{
            path = $ffRoot
            clsid = $actualClsid
            matches = $actualClsid -eq $expectedClsid
        }
    }
)
$deviceChecks = @(
    Get-CimInstance Win32_PnPSignedDriver |
        Where-Object { $_.DeviceID -like '*VID_046D&PID_C29A*' } |
        ForEach-Object {
            [ordered]@{
                name = $_.DeviceName
                infName = $_.InfName
                provider = $_.DriverProviderName
                microsoftInbox = $_.InfName -notmatch '^oem[0-9]+\.inf$' -and
                    $_.DriverProviderName -match 'Microsoft'
            }
        }
)
$comChecks = @(
    foreach ($item in @(
        @{
            architecture = 'x64'
            path = "HKLM:\SOFTWARE\Classes\CLSID\$expectedClsid\InProcServer32"
        },
        @{
            architecture = 'x86'
            path = "HKLM:\SOFTWARE\Classes\WOW6432Node\CLSID\$expectedClsid\InProcServer32"
        }
    )) {
        $registeredPath = if (Test-Path -LiteralPath $item.path) {
            (Get-Item -LiteralPath $item.path).GetValue('')
        }
        else { $null }
        $exists = $registeredPath -and
            (Test-Path -LiteralPath $registeredPath -PathType Leaf)
        $smokePath = if ($registeredPath) {
            Join-Path (Split-Path -Parent $registeredPath) `
                'DfgtFfbComSmoke.exe'
        }
        else { $null }
        $activation = if ($exists -and
            (Test-Path -LiteralPath $smokePath -PathType Leaf)) {
            & $smokePath $registeredPath 2>&1 | Out-String
            [ordered]@{
                tested = $true
                exitCode = $LASTEXITCODE
                passed = $LASTEXITCODE -eq 0
            }
        }
        else {
            [ordered]@{
                tested = $false
                exitCode = $null
                passed = $false
            }
        }
        [ordered]@{
            architecture = $item.architecture
            registryPath = $item.path
            serverPath = $registeredPath
            exists = [bool]$exists
            ownedByManifest = [bool]($exists -and $manifest -and
                @($manifest.files.path) -contains $registeredPath)
            activation = $activation
        }
    }
)
$filterChecks = @(
    Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object InstanceId -like '*VID_046D&PID_C29A*' |
        ForEach-Object {
            $upper = @(Get-PnpDeviceProperty -InstanceId $_.InstanceId `
                -KeyName 'DEVPKEY_Device_UpperFilters' `
                -ErrorAction SilentlyContinue).Data
            $lower = @(Get-PnpDeviceProperty -InstanceId $_.InstanceId `
                -KeyName 'DEVPKEY_Device_LowerFilters' `
                -ErrorAction SilentlyContinue).Data
            [ordered]@{
                instanceId = $_.InstanceId
                upperFilters = $upper
                lowerFilters = $lower
                legacyFilterPresent = [bool](
                    @($upper + $lower) |
                        Where-Object { $_ -in $legacyServices })
            }
        }
)
$codeIntegrityEvents = try {
    @(
        Get-WinEvent -FilterHashtable @{
            LogName = 'Microsoft-Windows-CodeIntegrity/Operational'
            StartTime = (Get-Date).AddDays(-7)
        } -MaxEvents 100 -ErrorAction Stop |
            Where-Object {
                $_.Message -match '(?i)DfgtControl|DfgtDirectInputFfb|DfgtFfbBroker|DfgtAgent'
            } |
            Select-Object TimeCreated, Id, LevelDisplayName, Message
    )
}
catch {
    @()
}

$hvciRunning = $deviceGuard.SecurityServicesRunning -contains 2
$requiredPassed = $hvciRegistry -eq 1 -and
    $hvciRunning -and
    $manifest -and
    -not ($fileChecks | Where-Object { -not $_.hashMatch }) -and
    $deviceChecks.Count -gt 0 -and
    -not ($deviceChecks | Where-Object { -not $_.microsoftInbox }) -and
    -not ($serviceChecks | Where-Object running) -and
    -not ($providerChecks | Where-Object { -not $_.matches }) -and
    -not ($comChecks | Where-Object {
        -not $_.exists -or -not $_.ownedByManifest -or
        -not $_.activation.tested -or -not $_.activation.passed }) -and
    -not ($filterChecks | Where-Object legacyFilterPresent)
$notTested = @('Broker safety state and neutral confirmation')
$verdict = if (-not $requiredPassed) { 'fail' }
    elseif ($notTested.Count -gt 0) { 'incomplete' }
    else { 'pass' }

[ordered]@{
    capturedAt = (Get-Date).ToString('o')
    verdict = $verdict
    passed = $verdict -eq 'pass'
    notTested = $notTested
    secureBoot = $secureBoot
    hvci = [ordered]@{
        registryEnabled = $hvciRegistry
        virtualizationBasedSecurityStatus = `
            $deviceGuard.VirtualizationBasedSecurityStatus
        securityServicesConfigured = `
            $deviceGuard.SecurityServicesConfigured
        securityServicesRunning = $deviceGuard.SecurityServicesRunning
        running = $hvciRunning
        codeIntegrityPolicyEnforcementStatus = `
            $deviceGuard.CodeIntegrityPolicyEnforcementStatus
    }
    devices = $deviceChecks
    legacyServices = $serviceChecks
    provider = $providerChecks
    com = $comChecks
    filters = $filterChecks
    codeIntegrityEvents = $codeIntegrityEvents
    safety = [ordered]@{
        status = 'notTested'
        reason = 'Verifier performs no force and no control-pipe session is bundled yet.'
    }
    manifest = $manifestPath
    files = $fileChecks
} | ConvertTo-Json -Depth 8
