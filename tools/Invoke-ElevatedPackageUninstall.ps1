[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackageRoot,
    [Parameter(Mandatory)][string]$InstallRoot,
    [Parameter(Mandatory)][string]$ResultPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$result = [ordered]@{
    capturedAt = (Get-Date).ToString('o')
    packageRoot = [IO.Path]::GetFullPath($PackageRoot)
    installRoot = [IO.Path]::GetFullPath($InstallRoot)
    elevated = $false
    succeeded = $false
    output = $null
    error = $null
}
try {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    $result.elevated = $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    $uninstaller = Join-Path $result.packageRoot `
        'scripts\Uninstall-DfgtFfb.ps1'
    $result.output = & $uninstaller `
        -InstallRoot $result.installRoot `
        -Confirm:$false | Out-String
    $result.succeeded = $true
}
catch {
    $result.error = $_.Exception.ToString()
}
finally {
    $directory = Split-Path -Parent $ResultPath
    New-Item -ItemType Directory -Path $directory -Force |
        Out-Null
    [IO.File]::WriteAllText(
        $ResultPath,
        ($result | ConvertTo-Json -Depth 8),
        [Text.UTF8Encoding]::new($false))
}
if (-not $result.succeeded) { exit 1 }
