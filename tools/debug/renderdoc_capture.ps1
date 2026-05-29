[CmdletBinding()]
param(
    [ValidateSet("SP", "MP")]
    [string]$Mode = "SP",

    [string]$Map = "",

    [int]$CaptureFrames = 1,

    [int]$WaitFrames = 240,

    [int]$PostAA = 1,

    [int]$MultiSamples = 4,

    [switch]$LaunchOnly,

    [switch]$AllowUnsupported,

    [string]$BasePath = "C:\Program Files (x86)\Steam\steamapps\common\Quake 4"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$workspaceRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDir "..\.."))
$installDir = Join-Path $workspaceRoot ".install"
$savePath = Join-Path $workspaceRoot ".home"
$exePath = Join-Path $installDir "openQ4-client_x64.exe"
$captureTemplate = Join-Path $workspaceRoot ".home\baseoq4\renderdoc\openq4"
$captureDir = Split-Path -Parent $captureTemplate
$renderDocDoc = Join-Path $workspaceRoot "docs-dev\renderdoc-workflow.md"

if (-not (Test-Path -LiteralPath $exePath)) {
    throw "openQ4 client executable not found: $exePath"
}

if (-not (Test-Path -LiteralPath $BasePath)) {
    throw "Quake 4 base path not found: $BasePath"
}

$renderDocCandidates = @(
    (Join-Path $env:ProgramFiles "RenderDoc\renderdoccmd.exe"),
    "renderdoccmd.exe"
)

$renderDocCmd = $null
foreach ($candidate in $renderDocCandidates) {
    try {
        if (Get-Command $candidate -ErrorAction Stop) {
            $renderDocCmd = $candidate
            break
        }
    } catch {
    }
}

if (-not $renderDocCmd) {
    throw "renderdoccmd.exe was not found. Install RenderDoc or add it to PATH."
}

if ([string]::IsNullOrWhiteSpace($Map)) {
    if ($Mode -eq "SP") {
        $Map = "game/convoy1"
    } else {
        $Map = "mp/q4dm2"
    }
}

if (-not $AllowUnsupported) {
    throw "RenderDoc capture is not currently supported with the shipping openQ4 OpenGL renderer. The renderer is still hard-wired to ARB2 / compatibility-profile features that RenderDoc drops during startup. See $renderDocDoc. Use -AllowUnsupported only if you intentionally want to reproduce this limitation while testing renderer modernization."
}

New-Item -ItemType Directory -Force -Path $captureDir | Out-Null

$gameArgs = @(
    "+set", "logFile", "2",
    "+set", "logFileName", "logs/openq4.log",
    "+set", "developer", "1",
    "+set", "r_fullscreen", "0",
    "+set", "r_postAA", $PostAA.ToString(),
    "+set", "r_multiSamples", $MultiSamples.ToString(),
    "+set", "r_shaderReport", "2",
    "+set", "fs_basepath", $BasePath,
    "+set", "fs_savepath", $savePath,
    "+set", "fs_devpath", $installDir,
    "+set", "fs_game", "baseoq4"
)

if ($Mode -eq "SP") {
    $gameArgs += @(
        "+set", "si_gameType", "singleplayer",
        "+map", $Map
    )
} else {
    $gameArgs += @(
        "+set", "net_serverDedicated", "0",
        "+seta", "si_pure", "0",
        "+set", "net_serverAllowServerMod", "1",
        "+set", "sv_cheats", "1",
        "+set", "si_gameType", "DM",
        "+spawnServer", $Map
    )
}

if ($LaunchOnly) {
    $gameArgs += @(
        "+renderDocStatus"
    )
} else {
    $gameArgs += @(
        "+wait", $WaitFrames.ToString(),
        "+renderDocCapture", ([Math]::Max(1, $CaptureFrames)).ToString(),
        "+wait", "90",
        "+quit"
    )
}

Write-Host ("RenderDoc mode: {0} ({1})" -f $Mode, ($(if ($LaunchOnly) { "launch-only" } else { "capture" })))
Write-Host "Map: $Map"
Write-Host "Capture template: $captureTemplate"

& $renderDocCmd `
    "capture" `
    "--opt-disallow-fullscreen" `
    "--opt-api-validation" `
    "-w" `
    "-d" $installDir `
    "-c" $captureTemplate `
    $exePath `
    @gameArgs

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$logCandidates = @(
    (Join-Path $workspaceRoot ".home\baseoq4\logs\openq4.log"),
    (Join-Path $workspaceRoot ".home\q4base\logs\openq4.log")
)
$rendererCompatibilityFailure = $false

foreach ($logPath in $logCandidates) {
    if (-not (Test-Path -LiteralPath $logPath)) {
        continue
    }

    $logText = Get-Content -Raw -LiteralPath $logPath
    if ($logText -match "GL_ARB_fragment_program not found" -or
        $logText -match "does not support the necessary features") {
        $rendererCompatibilityFailure = $true
        break
    }
}

if ($rendererCompatibilityFailure) {
    throw "RenderDoc did not launch successfully because the injected run dropped required openQ4 compatibility / ARB2 OpenGL features. RenderDoc capture is not yet supported with the current openQ4 renderer."
}

if ($LaunchOnly) {
    exit 0
}

$captures = Get-ChildItem -LiteralPath $captureDir -Filter "openq4*.rdc" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending

if (-not $captures) {
    throw "RenderDoc finished without producing an .rdc capture in $captureDir"
}

Write-Host ("Latest capture: {0}" -f $captures[0].FullName)

exit $LASTEXITCODE
