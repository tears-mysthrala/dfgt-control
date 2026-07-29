[CmdletBinding()]
param(
    [string]$InstallRoot = (Join-Path $env:ProgramFiles 'DfgtControl'),
    [string]$ExpectedVersion,
    [string]$TargetUserSid = (
        [Security.Principal.WindowsIdentity]::GetCurrent().User.Value),
    [switch]$IncludeCodeIntegrityEvents,
    [switch]$RunComSmoke,
    [switch]$RunBrokerHandshake
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$expectedClsid = '{806A045D-E7E8-491E-A607-31495A7BF4A3}'
$legacyServices = @('WmHidLo', 'WmFilter', 'WmVirHid', 'WmBEnum', 'WmXlCore')
$resolvedInstallRoot = [IO.Path]::GetFullPath($InstallRoot).TrimEnd('\')
$manifestPath = Join-Path $resolvedInstallRoot 'install-manifest.json'
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
    "Registry::HKEY_USERS\$TargetUserSid\SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_046D&PID_C29A\OEMForceFeedback"
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
        $threadingModel = if (Test-Path -LiteralPath $item.path) {
            (Get-ItemProperty -LiteralPath $item.path `
                -ErrorAction SilentlyContinue).ThreadingModel
        }
        else { $null }
        $smokePath = if ($registeredPath) {
            Join-Path (Split-Path -Parent $registeredPath) `
                'DfgtFfbComSmoke.exe'
        }
        else { $null }
        $activation = if ($RunComSmoke -and $exists -and
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
            threadingModel = $threadingModel
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
            $upperProperty = Get-PnpDeviceProperty -InstanceId $_.InstanceId `
                -KeyName 'DEVPKEY_Device_UpperFilters' `
                -ErrorAction SilentlyContinue
            $lowerProperty = Get-PnpDeviceProperty -InstanceId $_.InstanceId `
                -KeyName 'DEVPKEY_Device_LowerFilters' `
                -ErrorAction SilentlyContinue
            $upper = if ($upperProperty -and
                $upperProperty.PSObject.Properties['Data']) {
                @($upperProperty.Data)
            }
            else { @() }
            $lower = if ($lowerProperty -and
                $lowerProperty.PSObject.Properties['Data']) {
                @($lowerProperty.Data)
            }
            else { @() }
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
$codeIntegrityEvents = @(if (-not $IncludeCodeIntegrityEvents) {
    @()
}
else { try {
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
} })

$brokerHandshake = if ($RunBrokerHandshake) {
    $statusSmoke = Join-Path $resolvedInstallRoot `
        'bin\x64\DfgtBrokerStatusSmoke.exe'
    if (Test-Path -LiteralPath $statusSmoke -PathType Leaf) {
        $output = & $statusSmoke 2>&1 | Out-String
        $exitCode = $LASTEXITCODE
        $parsed = try { $output | ConvertFrom-Json } catch { $null }
        [ordered]@{
            tested = $true
            passed = $exitCode -eq 0 -and $null -ne $parsed
            exitCode = $exitCode
            status = $parsed
        }
    }
    else {
        [ordered]@{
            tested = $false
            passed = $false
            exitCode = $null
            status = $null
        }
    }
}
else {
    [ordered]@{
        tested = $false
        passed = $false
        exitCode = $null
        status = $null
    }
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
        $_.threadingModel -ne 'Both' }) -and
    -not ($filterChecks | Where-Object legacyFilterPresent)
$notTested = @(
    if (-not $RunComSmoke) {
        'DirectInput COM activation x64/x86'
    }
    elseif ($comChecks | Where-Object {
        -not $_.activation.tested -or -not $_.activation.passed }) {
        'DirectInput COM activation x64/x86 did not pass'
    }
    if (-not $RunBrokerHandshake) {
        'Broker handshake and safety state'
    }
    elseif (-not $brokerHandshake.tested -or
        -not $brokerHandshake.passed) {
        'Broker handshake and safety state did not pass'
    }
    if (-not $IncludeCodeIntegrityEvents) {
        'Recent Code Integrity events'
    }
)
$warnings = @(
    if (-not $secureBoot.querySucceeded) {
        "Secure Boot could not be queried: $($secureBoot.error)"
    }
    if ($codeIntegrityEvents.Count -gt 0) {
        'Relevant recent Code Integrity events were found; inspect evidence.'
    }
)
$failures = @(
    if (-not $manifest) { 'Install manifest is missing.' }
    elseif ($ExpectedVersion -and
        (-not $manifest.PSObject.Properties['version'] -or
         [string]$manifest.version -ne $ExpectedVersion)) {
        "Installed version does not match $ExpectedVersion."
    }
    if ($manifest -and (
        -not $manifest.PSObject.Properties['installRoot'] -or
        [IO.Path]::GetFullPath([string]$manifest.installRoot).TrimEnd('\') -ne
            $resolvedInstallRoot)) {
        'Install manifest targets another root.'
    }
    if (-not $hvciRunning) { 'HVCI is not reported running.' }
    if ($hvciRegistry -ne 1) { 'HVCI is not configured enabled.' }
    if ($fileChecks | Where-Object { -not $_.hashMatch }) {
        'One or more installed files failed manifest verification.'
    }
    if ($serviceChecks | Where-Object running) {
        'A legacy Logitech kernel service is still running.'
    }
    if ($filterChecks | Where-Object legacyFilterPresent) {
        'A legacy Logitech device filter remains attached.'
    }
    if ($providerChecks | Where-Object { -not $_.matches }) {
        'A force-feedback provider registration does not match.'
    }
    if ($comChecks | Where-Object {
        -not $_.exists -or -not $_.ownedByManifest -or
        $_.threadingModel -ne 'Both' }) {
        'COM registration, ownership or ThreadingModel is invalid.'
    }
)
$requiredPassed = $requiredPassed -and $failures.Count -eq 0
$verdict = if (-not $requiredPassed) { 'fail' }
    elseif ($notTested.Count -gt 0) { 'incomplete' }
    else { 'pass' }

[ordered]@{
    capturedAt = (Get-Date).ToString('o')
    verdict = $verdict
    passed = $verdict -eq 'pass'
    warnings = $warnings
    failures = $failures
    notTested = $notTested
    evidence = [ordered]@{
        installRoot = $resolvedInstallRoot
        expectedVersion = $ExpectedVersion
        targetUserSid = $TargetUserSid
    }
    remediation = @(
        if ($failures.Count -gt 0) {
            'Disconnect the wheel and correct the listed failures before re-running verification.'
        }
        if ($notTested.Count -gt 0) {
            'Re-run with the explicit IncludeCodeIntegrityEvents, RunComSmoke and RunBrokerHandshake switches as appropriate.'
        }
    )
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
    brokerHandshake = $brokerHandshake
    safety = [ordered]@{
        status = if ($brokerHandshake.passed) {
            $brokerHandshake.status
        }
        else { 'notTested' }
        reason = 'Status smoke sends only Ping and GetStatus; it never sends force or attach commands.'
    }
    manifest = $manifestPath
    files = $fileChecks
} | ConvertTo-Json -Depth 8
