[CmdletBinding()]
param(
    [string]$Map = "mp/q4dm1",

    [int]$Port = 28110,

    [int]$MaxFPS = 240,

    [ValidateRange(0, 1)]
    [int]$SwapInterval = 0,

    [string]$ClientName = "LoopbackClient",

    [int]$ServerWaitSeconds = 15,

    [int]$ClientSettleSeconds = 5,

    [string]$BasePath = "C:\Program Files (x86)\Steam\steamapps\common\Quake 4",

    [string]$SaveRoot = "",

    [switch]$ShowFPS,

    [ValidateRange(0, 2)]
    [int]$ShowFramePacing = 0,

    [switch]$Fullscreen
)

$ErrorActionPreference = "Stop"

function New-OpenQ4CommonArgs {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SavePath,

        [Parameter(Mandatory = $true)]
        [string]$LogFileName,

        [Parameter(Mandatory = $true)]
        [string]$InstallDir,

        [Parameter(Mandatory = $true)]
        [string]$BasePath,

        [Parameter(Mandatory = $true)]
        [string]$MaxFPS,

        [Parameter(Mandatory = $true)]
        [string]$SwapInterval,

        [Parameter(Mandatory = $true)]
        [string]$Fullscreen,

        [Parameter(Mandatory = $true)]
        [bool]$ShowFPS,

        [Parameter(Mandatory = $true)]
        [int]$ShowFramePacing
    )

    $args = @(
        "+set", "win_allowMultipleInstances", "1",
        "+set", "logFile", "2",
        "+set", "logFileName", $LogFileName,
        "+set", "developer", "1",
        "+set", "com_maxfps", $MaxFPS,
        "+set", "r_swapInterval", $SwapInterval,
        "+set", "r_fullscreen", $Fullscreen,
        "+set", "g_autoScreenshot", "0",
        "+set", "fs_basepath", $BasePath,
        "+set", "fs_savepath", $SavePath,
        "+set", "fs_devpath", $InstallDir,
        "+set", "fs_game", "baseoq4"
    )

    if ($ShowFPS) {
        $args += @("+set", "com_showFPS", "1")
    }

    if ($ShowFramePacing -gt 0) {
        $args += @("+set", "com_showFramePacing", $ShowFramePacing.ToString())
    }

    return $args
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$workspaceRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDir "..\.."))
$installDir = Join-Path $workspaceRoot ".install"
$exePath = Join-Path $installDir "openQ4-client_x64.exe"

if ([string]::IsNullOrWhiteSpace($SaveRoot)) {
    $SaveRoot = Join-Path $workspaceRoot ".tmp"
} elseif (-not [System.IO.Path]::IsPathRooted($SaveRoot)) {
    $SaveRoot = Join-Path $workspaceRoot $SaveRoot
}

if (-not (Test-Path -LiteralPath $exePath)) {
    throw "openQ4 client executable not found: $exePath"
}

if (-not (Test-Path -LiteralPath $installDir)) {
    throw "openQ4 install directory not found: $installDir"
}

if (-not (Test-Path -LiteralPath $BasePath)) {
    throw "Quake 4 base path not found: $BasePath"
}

if (-not (Test-Path -LiteralPath $SaveRoot)) {
    New-Item -ItemType Directory -Force -Path $SaveRoot | Out-Null
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$serverSavePath = Join-Path $SaveRoot ("listen_server_{0}" -f $stamp)
$clientSavePath = Join-Path $SaveRoot ("listen_client_{0}" -f $stamp)
New-Item -ItemType Directory -Force -Path $serverSavePath, $clientSavePath | Out-Null

$fullscreenValue = if ($Fullscreen) { "1" } else { "0" }
$maxFPSValue = $MaxFPS.ToString()
$swapIntervalValue = $SwapInterval.ToString()

$serverArgs = New-OpenQ4CommonArgs `
    -SavePath $serverSavePath `
    -LogFileName "logs/listen-server.log" `
    -InstallDir $installDir `
    -BasePath $BasePath `
    -MaxFPS $maxFPSValue `
    -SwapInterval $swapIntervalValue `
    -Fullscreen $fullscreenValue `
    -ShowFPS $ShowFPS.IsPresent `
    -ShowFramePacing $ShowFramePacing

$serverArgs += @(
    "+set", "net_serverDedicated", "0",
    "+set", "net_port", $Port.ToString(),
    "+seta", "si_pure", "0",
    "+set", "net_serverAllowServerMod", "1",
    "+set", "sv_cheats", "1",
    "+set", "si_gameType", "DM",
    "+spawnServer", $Map
)

$clientArgs = New-OpenQ4CommonArgs `
    -SavePath $clientSavePath `
    -LogFileName "logs/listen-client.log" `
    -InstallDir $installDir `
    -BasePath $BasePath `
    -MaxFPS $maxFPSValue `
    -SwapInterval $swapIntervalValue `
    -Fullscreen $fullscreenValue `
    -ShowFPS $ShowFPS.IsPresent `
    -ShowFramePacing $ShowFramePacing

$clientArgs += @(
    "+set", "ui_name", $ClientName,
    "+connect", ("127.0.0.1:{0}" -f $Port)
)

Write-Host ("Launching listen server on {0} ({1})" -f $Port, $Map)
Write-Host ("Server savepath: {0}" -f $serverSavePath)
Write-Host ("Client savepath: {0}" -f $clientSavePath)

$serverProcess = Start-Process -FilePath $exePath -WorkingDirectory $installDir -ArgumentList $serverArgs -PassThru

if ($ServerWaitSeconds -gt 0) {
    Start-Sleep -Seconds $ServerWaitSeconds
}

$clientProcess = Start-Process -FilePath $exePath -WorkingDirectory $installDir -ArgumentList $clientArgs -PassThru

if ($ClientSettleSeconds -gt 0) {
    Start-Sleep -Seconds $ClientSettleSeconds
}

$serverProcess.Refresh()
$clientProcess.Refresh()

$result = [pscustomobject]@{
    Timestamp       = $stamp
    Map             = $Map
    Port            = $Port
    MaxFPS          = $MaxFPS
    SwapInterval    = $SwapInterval
    Fullscreen      = $Fullscreen.IsPresent
    ShowFPS         = $ShowFPS.IsPresent
    ShowFramePacing = $ShowFramePacing
    ServerPID       = $serverProcess.Id
    ClientPID       = $clientProcess.Id
    ServerRunning   = -not $serverProcess.HasExited
    ClientRunning   = -not $clientProcess.HasExited
    ServerSavePath  = $serverSavePath
    ClientSavePath  = $clientSavePath
    ServerLog       = (Join-Path $serverSavePath "q4base\logs\listen-server.log")
    ClientLog       = (Join-Path $clientSavePath "q4base\logs\listen-client.log")
}

$result
