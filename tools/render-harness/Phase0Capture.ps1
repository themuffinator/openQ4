<#
    OpenQ4 renderer parity harness runner (Phase 0)
    Runs deterministic map boot + auto-screenshot captures and snapshots logs.

    Example:
    pwsh .\tools\render-harness\Phase0Capture.ps1 -SuiteFile .\tools\render-harness\phase0-capture-suite.json
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$SuiteFile = (Join-Path (Get-Item $PSScriptRoot).FullName "phase0-capture-suite.json"),

    [Parameter(Mandatory = $false)]
    [string]$InstallDir = (Join-Path (Get-Item (Join-Path $PSScriptRoot "..\..") ).FullName ".install"),

    [Parameter(Mandatory = $false)]
    [string]$SavePath = (Join-Path (Get-Item (Join-Path $PSScriptRoot "..\..") ).FullName ".home"),

    [Parameter(Mandatory = $false)]
    [string]$Executable = "OpenQ4-client_x64.exe",

    [Parameter(Mandatory = $false)]
    [string]$BaseGamePath = "C:\Program Files (x86)\Steam\steamapps\common\Quake 4",

    [Parameter(Mandatory = $false)]
    [string]$OutputRoot = (Join-Path (Get-Item (Join-Path $PSScriptRoot "..\..") ).FullName ".tmp\rbdoom-phase0-runs")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-Int([object]$value, [int]$default) {
    if ($null -eq $value) { return $default }
    $parsed = 0
    if ([int]::TryParse([string]$value, [ref]$parsed)) { return $parsed }
    return $default
}

function Resolve-IntOrNull([object]$value) {
    if ($null -eq $value) { return $null }
    $parsed = 0
    if ([int]::TryParse([string]$value, [ref]$parsed)) { return $parsed }
    return $null
}

function Merge-Cvars([pscustomobject]$source, [hashtable]$into) {
    if ($null -eq $source -or $null -eq $into) { return }

    foreach ($prop in $source.PSObject.Properties) {
        if ($null -eq $prop.Value) {
            continue
        }
        $into[$prop.Name] = [string]$prop.Value
    }
}

function Add-CvarsToArgs([hashtable]$cvars, [array]$argBuffer) {
    if ($null -eq $cvars -or $cvars.Count -eq 0) {
        return $argBuffer
    }

    foreach ($name in $cvars.Keys | Sort-Object) {
        $argBuffer += @(
            "+set",
            [string]$name,
            [string]$cvars[[string]$name]
        )
    }

    return $argBuffer
}

function Test-HasProperty([object]$source, [string]$propertyName) {
    if ($null -eq $source -or [string]::IsNullOrWhiteSpace($propertyName)) {
        return $false
    }
    return $null -ne $source.PSObject.Properties[$propertyName]
}

function Get-Property([object]$source, [string]$propertyName, [object]$defaultValue = $null) {
    if (Test-HasProperty -source $source -propertyName $propertyName) {
        return $source.$propertyName
    }
    return $defaultValue
}

function Resolve-CaptureVariants([pscustomobject]$scene, [pscustomobject]$common) {
    if ((Test-HasProperty -source $scene -propertyName "capture_variants") -and $scene.capture_variants) {
        return ,@($scene.capture_variants)
    }
    if ((Test-HasProperty -source $common -propertyName "capture_variants") -and $common.capture_variants) {
        return ,@($common.capture_variants)
    }
    return ,@([pscustomobject]@{ name = "default"; r_useShadowMapping = $null; suffix = "default" })
}

function New-RunCfgName([string]$sceneName, [string]$variantSuffix) {
    $sceneSafe = if ([string]::IsNullOrWhiteSpace($sceneName)) { "scene" } else { $sceneName }
    $variantSafe = if ([string]::IsNullOrWhiteSpace($variantSuffix)) { "variant" } else { $variantSuffix }
    $raw = "__phase0_{0}_{1}.cfg" -f $sceneSafe, $variantSafe
    return ([regex]::Replace($raw, "[^A-Za-z0-9_.-]", "_"))
}

function Write-RunCfg([string]$cfgPath, [hashtable]$cvars) {
    if ([string]::IsNullOrWhiteSpace($cfgPath)) {
        return
    }

    $lines = New-Object System.Collections.Generic.List[string]
    if ($null -ne $cvars -and $cvars.Count -gt 0) {
        foreach ($name in $cvars.Keys | Sort-Object) {
            $value = [string]$cvars[[string]$name]
            $escaped = $value.Replace("\", "\\").Replace('"', '\"')
            $lines.Add(('set {0} "{1}"' -f [string]$name, $escaped))
        }
    }

    if ($lines.Count -eq 0) {
        $lines.Add("echo phase0 cfg: no cvars")
    }

    Set-Content -Path $cfgPath -Value $lines -Encoding ascii
}

function New-ArgList([pscustomobject]$scene, [pscustomobject]$common, [string]$runCfgName = $null, [int]$captureDelayMs = 4000, [psobject[]]$variantCommands = @()) {
    $args = @(
        "+set", "logFile", "2",
        "+set", "logFileName", "logs/openq4.log",
        "+set", "developer", "1",
        "+set", "com_skipLoadingContinue", "1",
        "+set", "r_fullscreen", "0",
        "+set", "g_autoScreenshot", "1",
        "+set", "g_autoScreenshotDelayMs", $captureDelayMs.ToString(),
        "+set", "g_autoScreenshotQuit", "1",
        "+set", "fs_basepath", $BaseGamePath,
        "+set", "fs_savepath", $SavePath,
        "+set", "fs_devpath", $InstallDir,
        "+set", "fs_game", "openq4",
        "+set", "si_gameType", ($(if ($scene.mode -eq "mp") { "DM" } else { "singleplayer" })),
        "+set", "si_pure", "0",
        "+set", "net_serverAllowServerMod", "1",
        "+set", "sv_cheats", "1"
    )

    if (-not [string]::IsNullOrWhiteSpace($runCfgName)) {
        $args += @(
            "+exec", $runCfgName
        )
    }

    $sceneExtraCommands = Get-Property -source $scene -propertyName "extra_commands" -defaultValue $null
    if ($sceneExtraCommands) {
        $sceneExtraCommands | ForEach-Object { $args += [string]$_ }
    }

    if ($variantCommands) {
        $variantCommands | ForEach-Object { $args += [string]$_ }
    }

    if ($scene.mode -eq "mp") {
        $args += @(
            "+spawnServer",
            $scene.map
        )
    } else {
        $args += @(
            "+map",
            $scene.map
        )
    }

    return ,$args
}

function Get-AutoScreenshotPath([string]$logPath) {
    if (-not (Test-Path $logPath)) {
        return $null
    }
    $raw = Get-Content -Raw -Path $logPath
    $match = [regex]::Match($raw, "AutoScreenshot: wrote (?<path>[^ ]+) at")
    if ($match.Success) {
        return $match.Groups["path"].Value
    }
    return $null
}

function Get-LogSignalCounts([string]$logPath) {
    if (-not (Test-Path $logPath)) {
        return [ordered]@{
            Warnings = -1
            FatalOrError = -1
        }
    }
    $rawLog = Get-Content -Raw -Path $logPath
    $warningCount = ([regex]::Matches($rawLog, "(?im)warning|warn")).Count
    $errorCount = ([regex]::Matches($rawLog, "(?im)error|assert|fatal|denied|assertion")).Count
    return [ordered]@{
        Warnings = $warningCount
        FatalOrError = $errorCount
    }
}

if (-not (Test-Path $SuiteFile)) {
    throw "Suite file not found: $SuiteFile"
}

$suite = Get-Content -Raw -Path $SuiteFile | ConvertFrom-Json
$engineExe = Join-Path $InstallDir $Executable
$saveGamePaths = @(
    (Join-Path $SavePath "openq4"),
    (Join-Path $SavePath "q4base")
)
$logCandidates = $saveGamePaths | ForEach-Object { Join-Path $_ "logs\openq4.log" }
$screenshotDirs = $saveGamePaths | ForEach-Object { Join-Path $_ "screenshots\phase0" }

if (-not (Test-Path $engineExe)) {
    throw "Executable not found: $engineExe"
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
New-Item -ItemType Directory -Force -Path $SavePath | Out-Null
foreach ($gamePath in $saveGamePaths) {
    New-Item -ItemType Directory -Force -Path $gamePath | Out-Null
}
foreach ($shotPath in $screenshotDirs) {
    New-Item -ItemType Directory -Force -Path $shotPath | Out-Null
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$runDir = Join-Path $OutputRoot $timestamp
New-Item -ItemType Directory -Force -Path $runDir | Out-Null

$sceneResults = New-Object System.Collections.Generic.List[object]
$sceneIndex = 0

foreach ($scene in $suite.scenes) {
    $sceneIndex++
    $sceneNameRaw = [string](Get-Property -source $scene -propertyName "name" -defaultValue "")
    $sceneName = if ([string]::IsNullOrWhiteSpace($sceneNameRaw)) { "scene_$sceneIndex" } else { $sceneNameRaw }
    if ([string]::IsNullOrWhiteSpace($sceneName)) {
        $sceneName = [guid]::NewGuid().ToString("N")
    }

    $variants = Resolve-CaptureVariants -scene $scene -common $suite.common
        $variantIndex = 0
        $variantCvars = @{}

        foreach ($variant in $variants) {
            $variantIndex++
            $variantNameRaw = [string](Get-Property -source $variant -propertyName "name" -defaultValue "")
            $variantSuffixRaw = [string](Get-Property -source $variant -propertyName "suffix" -defaultValue "")
            $variantName = if ([string]::IsNullOrWhiteSpace($variantNameRaw)) { "variant_$variantIndex" } else { $variantNameRaw }
            $variantSuffix = if ([string]::IsNullOrWhiteSpace($variantSuffixRaw)) { $variantName } else { $variantSuffixRaw }
            $shadowMapMode = Resolve-IntOrNull (Get-Property -source $variant -propertyName "r_useShadowMapping" -defaultValue $null)
            $variantCvars = @{}
            Merge-Cvars -source (Get-Property -source $scene -propertyName "cvars" -defaultValue $null) -into $variantCvars
            Merge-Cvars -source (Get-Property -source $variant -propertyName "cvars" -defaultValue $null) -into $variantCvars
            if ($shadowMapMode -ne $null) {
                $variantCvars["r_useShadowMapping"] = $shadowMapMode.ToString()
            }

            Write-Host "Running scene $sceneName ($($scene.mode)) -> $($scene.map) [variant=$variantName]"
        $sceneDir = Join-Path $runDir "${sceneName}_${variantSuffix}"
        New-Item -ItemType Directory -Force -Path $sceneDir | Out-Null

        foreach ($candidateLog in $logCandidates) {
            if (Test-Path $candidateLog) { Remove-Item -Force -Path $candidateLog }
        }
        foreach ($shotPath in $screenshotDirs) {
            if (Test-Path $shotPath) {
                Get-ChildItem -File -Path $shotPath -Filter "auto_*.tga" -ErrorAction SilentlyContinue | Remove-Item -Force
            }
        }

        $captureDelayMs = Resolve-Int (Get-Property -source $variant -propertyName "capture_delay_ms" -defaultValue $null) $(Resolve-Int (Get-Property -source $scene -propertyName "capture_delay_ms" -defaultValue $null) $(Resolve-Int (Get-Property -source $suite.common -propertyName "capture_delay_ms" -defaultValue $null) 4000))
        $timeoutMs = Resolve-Int (Get-Property -source $variant -propertyName "post_timeout_ms" -defaultValue $null) $(Resolve-Int (Get-Property -source $scene -propertyName "post_timeout_ms" -defaultValue $null) $(Resolve-Int (Get-Property -source $suite.common -propertyName "post_timeout_ms" -defaultValue $null) 120000))
        if ($timeoutMs -lt 15000) { $timeoutMs = 15000 }

        $runCfgName = New-RunCfgName -sceneName $sceneName -variantSuffix $variantSuffix
        $runCfgPath = Join-Path (Join-Path $SavePath "openq4") $runCfgName
        Write-RunCfg -cfgPath $runCfgPath -cvars $variantCvars

        $args = New-ArgList -scene $scene -common $suite.common -runCfgName $runCfgName -captureDelayMs $captureDelayMs -variantCommands (Get-Property -source $variant -propertyName "extra_commands" -defaultValue @())
        $startTime = Get-Date
        $proc = Start-Process -FilePath $engineExe -ArgumentList $args -WorkingDirectory $InstallDir -PassThru -NoNewWindow
        $timedOut = -not $proc.WaitForExit($timeoutMs)

        if ($timedOut) {
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
            Write-Warning "Scene timed out and was terminated: $sceneName/$variantName"
        }

        $status = if ($timedOut) { "timeout" } elseif ($proc.ExitCode -ne 0) { "nonzero_exit" } else { "ok" }
        Start-Sleep -Milliseconds 250

        $logSource = $logCandidates |
            Where-Object { Test-Path $_ } |
            Sort-Object { (Get-Item $_).LastWriteTimeUtc } -Descending |
            Select-Object -First 1

        if ($logSource) {
            Copy-Item -Path $logSource -Destination (Join-Path $sceneDir "openq4.log") -Force -ErrorAction SilentlyContinue
        }

        $autoShot = Get-AutoScreenshotPath -logPath (Join-Path $sceneDir "openq4.log")
        $autoShotFile = $null
        if ($autoShot) {
            $autoShotRelative = ($autoShot -replace "/", "\")
            $shotRoots = New-Object System.Collections.Generic.List[string]
            if ($logSource) {
                $shotRoots.Add((Split-Path -Parent (Split-Path -Parent $logSource)))
            }
            foreach ($path in $saveGamePaths) {
                if (-not [string]::IsNullOrWhiteSpace($path)) {
                    $shotRoots.Add($path)
                }
            }

            foreach ($root in ($shotRoots | Select-Object -Unique)) {
                $autoShotFull = Join-Path $root $autoShotRelative
                if (-not (Test-Path $autoShotFull)) {
                    continue
                }
                $autoShotFile = Join-Path $sceneDir "screenshot.tga"
                Copy-Item -Path $autoShotFull -Destination $autoShotFile -Force
                Copy-Item -Path $autoShotFull -Destination (Join-Path $sceneDir "${sceneName}_${variantSuffix}.tga") -Force
                break
            }
        }

        $signals = Get-LogSignalCounts -logPath (Join-Path $sceneDir "openq4.log")
        $sceneResults.Add([pscustomobject]@{
            scene             = $sceneName
            variant           = $variantName
            mode              = $scene.mode
            map               = $scene.map
            capture_suffix    = $variantSuffix
            r_useShadowMapping = $shadowMapMode
            r_useParallelShadowMaps = $variantCvars["r_useParallelShadowMaps"]
            r_shadowMapSplits = $variantCvars["r_shadowMapSplits"]
            r_shadowMapOccluderFacing = $variantCvars["r_shadowMapOccluderFacing"]
            r_shadowMapOccluderSource = $variantCvars["r_shadowMapOccluderSource"]
            r_shadowMapDebugFlow = $variantCvars["r_shadowMapDebugFlow"]
            r_shadowMapDebugLight = $variantCvars["r_shadowMapDebugLight"]
            r_shadowMapDebugLogInterval = $variantCvars["r_shadowMapDebugLogInterval"]
            r_shadowMapStrictMappedPath = $variantCvars["r_shadowMapStrictMappedPath"]
            r_usePBR          = $variantCvars["r_usePBR"]
            r_useIndirectLighting = $variantCvars["r_useIndirectLighting"]
            r_usePostLightingStack = $variantCvars["r_usePostLightingStack"]
            r_useSSAO         = $variantCvars["r_useSSAO"]
            r_useTAA          = $variantCvars["r_useTAA"]
            r_useTonemap      = $variantCvars["r_useTonemap"]
            r_useHiZ          = $variantCvars["r_useHiZ"]
            r_useSSR          = $variantCvars["r_useSSR"]
            r_ssrStrength     = $variantCvars["r_ssrStrength"]
            r_useMaskedOcclusionCulling = $variantCvars["r_useMaskedOcclusionCulling"]
            r_graphicsAPI     = $variantCvars["r_graphicsAPI"]
            r_requireVulkanBootstrap = $variantCvars["r_requireVulkanBootstrap"]
            r_postAA          = $variantCvars["r_postAA"]
            status            = $status
            exit_code         = $proc.ExitCode
            capture_delay_ms  = $captureDelayMs
            post_timeout_ms   = $timeoutMs
            warnings          = $signals.Warnings
            errors            = $signals.FatalOrError
            screenshot_source = $autoShot
            screenshot_file   = if ($autoShotFile) { Split-Path -Leaf $autoShotFile } else { "" }
            elapsed_ms        = [int]( (Get-Date) - $startTime ).TotalMilliseconds
        })
    }
}

$summaryCsv = Join-Path $runDir "summary.csv"
$summaryJson = Join-Path $runDir "summary.json"
$sceneResults | Export-Csv -NoTypeInformation -Path $summaryCsv
$sceneResults | ConvertTo-Json -Depth 3 | Set-Content -Path $summaryJson

Write-Host "Phase 0 capture complete. Results:"
Write-Host "  run dir   : $runDir"
Write-Host "  scenes    : $($sceneResults.Count)"
Write-Host "  summary   : $summaryCsv"
Write-Host "  summaryJs : $summaryJson"
