[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$')]
    [string]$Version,

    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
$driver = Join-Path $root 'ffb-driver'
$artifacts = Join-Path $root 'artifacts'
$stage = Join-Path $artifacts "DfgtControl-v$Version-win"
$zip = Join-Path $artifacts "DfgtControl-v$Version-win.zip"
$checksum = "$zip.sha256"
$resolvedRoot = [IO.Path]::GetFullPath($root).TrimEnd('\')
$resolvedArtifacts = [IO.Path]::GetFullPath($artifacts).TrimEnd('\')
if (-not $resolvedArtifacts.StartsWith(
        $resolvedRoot + '\',
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Artifact directory escaped the repository root.'
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory)][string]$File,
        [Parameter(Mandatory)][string[]]$Arguments
    )
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$File failed with exit code $LASTEXITCODE"
    }
}

New-Item -ItemType Directory -Path $artifacts -Force | Out-Null

Invoke-Checked cmake @(
    '-S', $driver,
    '-B', (Join-Path $driver 'build-x64'),
    '-A', 'x64')
Invoke-Checked cmake @(
    '--build', (Join-Path $driver 'build-x64'),
    '--config', 'Release')
Invoke-Checked cmake @(
    '-S', $driver,
    '-B', (Join-Path $driver 'build-x86'),
    '-A', 'Win32')
Invoke-Checked cmake @(
    '--build', (Join-Path $driver 'build-x86'),
    '--config', 'Release')
Invoke-Checked dotnet @(
    'publish', (Join-Path $driver 'ui\DfgtControl.csproj'),
    '-c', 'Release',
    '-r', 'win-x64',
    '--self-contained', 'true',
    '-o', (Join-Path $driver 'ui\publish'))

if (-not $SkipTests) {
    Invoke-Checked ctest @(
        '--test-dir', (Join-Path $driver 'build-x64'),
        '-C', 'Release',
        '--output-on-failure')
    Invoke-Checked ctest @(
        '--test-dir', (Join-Path $driver 'build-x86'),
        '-C', 'Release',
        '--output-on-failure')
}

if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
if (Test-Path -LiteralPath $zip) {
    Remove-Item -LiteralPath $zip -Force
}
New-Item -ItemType Directory -Path `
    (Join-Path $stage 'bin\x64'), `
    (Join-Path $stage 'bin\x86'), `
    (Join-Path $stage 'scripts'), `
    (Join-Path $stage 'docs') -Force | Out-Null

$release64 = Join-Path $driver 'build-x64\Release'
$release86 = Join-Path $driver 'build-x86\Release'
$uiPublish = Join-Path $driver 'ui\publish'

Copy-Item (Join-Path $release64 'DfgtDirectInputFfb.dll') `
    (Join-Path $stage 'bin\x64')
Copy-Item (Join-Path $release64 'DfgtFfbComSmoke.exe') `
    (Join-Path $stage 'bin\x64')
Copy-Item (Join-Path $release64 'DfgtFfbBroker.exe') `
    (Join-Path $stage 'bin\x64')
Copy-Item (Join-Path $release64 'DfgtBrokerStatusSmoke.exe') `
    (Join-Path $stage 'bin\x64')
Copy-Item (Join-Path $release64 'DfgtMaintenance.exe') `
    (Join-Path $stage 'bin\x64')
Copy-Item (Join-Path $release64 'DfgtAgent.exe') `
    (Join-Path $stage 'bin\x64')
Copy-Item (Join-Path $uiPublish 'DfgtControl.exe') `
    (Join-Path $stage 'bin\x64')
Copy-Item (Join-Path $release86 'DfgtDirectInputFfb.dll') `
    (Join-Path $stage 'bin\x86')
Copy-Item (Join-Path $release86 'DfgtFfbComSmoke.exe') `
    (Join-Path $stage 'bin\x86')
$publicScripts = @(
    'Backup-LogitechLegacyStack.ps1',
    'Install-DfgtFfb.ps1',
    'Remove-LogitechLegacyStack.ps1',
    'Restore-LogitechLegacyStack.ps1',
    'Uninstall-DfgtFfb.ps1',
    'Verify-HvciStack.ps1'
)
foreach ($script in $publicScripts) {
    Copy-Item -LiteralPath (Join-Path $driver "scripts\$script") `
        -Destination (Join-Path $stage 'scripts')
}
Copy-Item -LiteralPath @(
    (Join-Path $root 'README.md'),
    (Join-Path $root 'LICENSE'),
    (Join-Path $root 'SECURITY.md')
) -Destination $stage
Copy-Item -LiteralPath @(
    (Join-Path $root 'docs\MIGRATION.md'),
    (Join-Path $root 'docs\ARCHITECTURE.md'),
    (Join-Path $root 'docs\IPC-PROTOCOL.md'),
    (Join-Path $root 'docs\SAFETY-STATE-MACHINE.md'),
    (Join-Path $root 'docs\TEST-MATRIX.md'),
    (Join-Path $root 'docs\MIGRATION-ROLLBACK.md'),
    (Join-Path $root 'docs\VALIDATION.md'),
    (Join-Path $root 'docs\FINAL-HARDENING-REPORT.md')
) -Destination (Join-Path $stage 'docs')

$commitSha = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or
    $commitSha -notmatch '^[0-9a-f]{40}$') {
    throw 'Could not resolve the source commit SHA.'
}
$commitTimestampText = (& git -C $root show -s --format=%cI HEAD).Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'Could not resolve the source commit timestamp.'
}
$commitTimestamp = [DateTimeOffset]::Parse($commitTimestampText).UtcDateTime
$sbom = [ordered]@{
    spdxVersion = 'SPDX-2.3'
    dataLicense = 'CC0-1.0'
    SPDXID = 'SPDXRef-DOCUMENT'
    name = "DfgtControl-$Version"
    documentNamespace = "https://github.com/tears-mysthrala/dfgt-control/sbom/$commitSha"
    creationInfo = [ordered]@{
        created = $commitTimestamp.ToString('yyyy-MM-ddTHH:mm:ssZ')
        creators = @('Tool: DfgtControl-Build-Release')
    }
    packages = @(
        [ordered]@{
            name = 'DfgtControl'
            SPDXID = 'SPDXRef-Package-DfgtControl'
            versionInfo = $Version
            downloadLocation = 'NOASSERTION'
            filesAnalyzed = $false
            licenseConcluded = 'NOASSERTION'
            licenseDeclared = 'MIT'
            copyrightText = 'NOASSERTION'
            externalRefs = @(
                [ordered]@{
                    referenceCategory = 'OTHER'
                    referenceType = 'vcs'
                    referenceLocator = "git+https://github.com/tears-mysthrala/dfgt-control.git@$commitSha"
                }
            )
        }
    )
    relationships = @(
        [ordered]@{
            spdxElementId = 'SPDXRef-DOCUMENT'
            relationshipType = 'DESCRIBES'
            relatedSpdxElement = 'SPDXRef-Package-DfgtControl'
        }
    )
}
[IO.File]::WriteAllText(
    (Join-Path $stage 'SBOM.spdx.json'),
    ($sbom | ConvertTo-Json -Depth 8),
    [Text.UTF8Encoding]::new($false))

$manifestFiles = Get-ChildItem -LiteralPath $stage -File -Recurse |
    Sort-Object FullName |
    ForEach-Object {
        [ordered]@{
            path = [IO.Path]::GetRelativePath($stage, $_.FullName)
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        }
    }
$manifest = [ordered]@{
    schemaVersion = 1
    version = $Version
    commitSha = $commitSha
    createdAtUtc = $commitTimestamp.ToString('o')
    files = @($manifestFiles)
}
[IO.File]::WriteAllText(
    (Join-Path $stage 'release-manifest.json'),
    ($manifest | ConvertTo-Json -Depth 5),
    [Text.UTF8Encoding]::new($false))

Get-ChildItem -LiteralPath $stage -File -Recurse |
    ForEach-Object { $_.LastWriteTimeUtc = $commitTimestamp }
Compress-Archive -LiteralPath $stage -DestinationPath $zip `
    -CompressionLevel Optimal
$hash = Get-FileHash -LiteralPath $zip -Algorithm SHA256
[IO.File]::WriteAllText(
    $checksum,
    "$($hash.Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($zip))`n",
    [Text.UTF8Encoding]::new($false))

[pscustomobject]@{
    version = $Version
    package = $zip
    checksum = $checksum
    sha256 = $hash.Hash
} | ConvertTo-Json
