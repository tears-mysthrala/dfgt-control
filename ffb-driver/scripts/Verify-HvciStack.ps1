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

$hvciRunning = $deviceGuard.SecurityServicesRunning -contains 2
$passed = $hvciRegistry -eq 1 -and
    $hvciRunning -and
    $manifest -and
    -not ($fileChecks | Where-Object { -not $_.hashMatch }) -and
    $deviceChecks.Count -gt 0 -and
    -not ($deviceChecks | Where-Object { -not $_.microsoftInbox }) -and
    -not ($serviceChecks | Where-Object running) -and
    -not ($providerChecks | Where-Object { -not $_.matches })

[ordered]@{
    capturedAt = (Get-Date).ToString('o')
    passed = [bool]$passed
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
    manifest = $manifestPath
    files = $fileChecks
} | ConvertTo-Json -Depth 8
