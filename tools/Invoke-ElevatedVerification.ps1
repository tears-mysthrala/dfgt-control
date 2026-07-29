[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$VerifierPath,
    [Parameter(Mandatory)][string]$InstallRoot,
    [Parameter(Mandatory)][string]$ExpectedVersion,
    [Parameter(Mandatory)][string]$TargetUserSid,
    [Parameter(Mandatory)][string]$ResultPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

try {
    $json = & $VerifierPath `
        -InstallRoot $InstallRoot `
        -ExpectedVersion $ExpectedVersion `
        -TargetUserSid $TargetUserSid `
        -IncludeCodeIntegrityEvents `
        -RunComSmoke `
        -RunBrokerHandshake
    $null = $json | ConvertFrom-Json
    $directory = Split-Path -Parent $ResultPath
    New-Item -ItemType Directory -Path $directory -Force |
        Out-Null
    [IO.File]::WriteAllText(
        $ResultPath,
        ($json -join [Environment]::NewLine),
        [Text.UTF8Encoding]::new($false))
}
catch {
    [IO.File]::WriteAllText(
        "$ResultPath.error.txt",
        $_.Exception.ToString(),
        [Text.UTF8Encoding]::new($false))
    exit 1
}
