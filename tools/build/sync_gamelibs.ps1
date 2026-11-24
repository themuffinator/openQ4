param(
    [string]$GameLibsRepo = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$openQ4Root = [System.IO.Path]::GetFullPath((Join-Path $scriptDir "..\.."))

if ([string]::IsNullOrWhiteSpace($GameLibsRepo)) {
    $GameLibsRepo = $openQ4Root
}

$gameLibsRoot = [System.IO.Path]::GetFullPath($GameLibsRepo)
if ($gameLibsRoot -ne $openQ4Root) {
    throw "Game sources are canonical in openQ4/src/game. External source syncing has been retired."
}

Write-Host "sync_gamelibs.ps1 is deprecated. No files were copied."
Write-Host "Canonical game sources: $gameLibsRoot\src\game"

$global:LASTEXITCODE = 0
exit 0
