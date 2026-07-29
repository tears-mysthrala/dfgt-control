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
    '--config', 'Release',
    '--target',
    'DfgtDirectInputFfb',
    'DfgtFfbComSmoke',
    'DfgtProtocolTests')
Invoke-Checked dotnet @(
    'publish', (Join-Path $driver 'ui\DfgtControl.csproj'),
    '-c', 'Release',
    '-r', 'win-x64',
    '--self-contained', 'false',
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
Copy-Item (Join-Path $release64 'DfgtFfbBroker.exe') `
    (Join-Path $stage 'bin\x64')
Copy-Item (Join-Path $release64 'DfgtAgent.exe') `
    (Join-Path $stage 'bin\x64')
Copy-Item (Join-Path $uiPublish 'DfgtControl.exe') `
    (Join-Path $stage 'bin\x64')
Copy-Item (Join-Path $release86 'DfgtDirectInputFfb.dll') `
    (Join-Path $stage 'bin\x86')
$publicScripts = @(
    'Backup-LogitechLegacyStack.ps1',
    'Install-DfgtFfb.ps1',
    'Remove-LogitechLegacyStack.ps1',
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
    (Join-Path $root 'docs\ARCHITECTURE.md')
) -Destination (Join-Path $stage 'docs')

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
    createdAtUtc = [DateTime]::UtcNow.ToString('o')
    files = @($manifestFiles)
}
[IO.File]::WriteAllText(
    (Join-Path $stage 'release-manifest.json'),
    ($manifest | ConvertTo-Json -Depth 5),
    [Text.UTF8Encoding]::new($false))

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
