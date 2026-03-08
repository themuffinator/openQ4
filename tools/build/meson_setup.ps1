$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-VsDevCmdPath {
    if ($env:VSINSTALLDIR) {
        $candidate = Join-Path $env:VSINSTALLDIR "Common7\Tools\VsDevCmd.bat"
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "Could not locate vswhere.exe."
    }

    $component = "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
    # Prefer Visual Studio 2026+ (major 18) when installed.
    $installPathRaw = & $vswhere -latest -prerelease -version "[18.0,19.0)" -products * -requires $component -property installationPath
    $installPath = if ($null -eq $installPathRaw) { "" } else { "$installPathRaw".Trim() }
    if ([string]::IsNullOrWhiteSpace($installPath)) {
        $installPathRaw = & $vswhere -latest -prerelease -products * -requires $component -property installationPath
        $installPath = if ($null -eq $installPathRaw) { "" } else { "$installPathRaw".Trim() }
        if (-not [string]::IsNullOrWhiteSpace($installPath)) {
            Write-Host "Visual Studio 2026+ was not found. Falling back to latest available toolchain at '$installPath'."
        }
    }

    if ([string]::IsNullOrWhiteSpace($installPath)) {
        throw "No Visual Studio installation with C++ tools was found."
    }

    $vsDevCmd = Join-Path $installPath "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path $vsDevCmd)) {
        throw "Could not locate VsDevCmd.bat at '$vsDevCmd'."
    }

    return $vsDevCmd
}

function Quote-CmdArg([string]$Value) {
    if ($Value -match '[\s"&<>|()]') {
        return '"' + ($Value -replace '"', '\"') + '"'
    }
    return $Value
}

function Invoke-Meson {
    param(
        [string[]]$MesonArgs,
        [string]$VsDevCmdPath
    )

    if ([string]::IsNullOrWhiteSpace($VsDevCmdPath)) {
        & meson @MesonArgs
        return
    }

    $mesonCmd = "meson " + (($MesonArgs | ForEach-Object { Quote-CmdArg $_ }) -join " ")
    $fullCmd = 'call "' + $VsDevCmdPath + '" -arch=x64 -host_arch=x64 >nul && ' + $mesonCmd
    & $env:ComSpec /d /c $fullCmd
}

function Get-CompileBuildDirInfo {
    param(
        [string[]]$MesonArgs,
        [string]$DefaultBuildDir
    )

    $result = [PSCustomObject]@{
        BuildDir = $DefaultBuildDir
        HasExplicit = $false
    }

    for ($i = 0; $i -lt $MesonArgs.Length; $i++) {
        $arg = $MesonArgs[$i]
        if ($arg -eq "-C" -and ($i + 1) -lt $MesonArgs.Length) {
            $result.BuildDir = $MesonArgs[$i + 1]
            $result.HasExplicit = $true
            break
        }

        if ($arg.StartsWith("-C") -and $arg.Length -gt 2) {
            $result.BuildDir = $arg.Substring(2)
            $result.HasExplicit = $true
            break
        }
    }

    $result.BuildDir = [System.IO.Path]::GetFullPath($result.BuildDir)
    return $result
}

function Test-MesonBuildDirectory {
    param([string]$BuildDir)

    $coreData = Join-Path $BuildDir "meson-private\coredata.dat"
    $ninjaFile = Join-Path $BuildDir "build.ninja"
    return (Test-Path $coreData) -and (Test-Path $ninjaFile)
}

function Get-MesonBuildOptionValue {
    param(
        [string]$BuildDir,
        [string]$OptionName
    )

    $introOptionsPath = Join-Path $BuildDir "meson-info\intro-buildoptions.json"
    if (-not (Test-Path $introOptionsPath)) {
        return $null
    }

    $introOptions = Get-Content $introOptionsPath -Raw | ConvertFrom-Json
    foreach ($option in $introOptions) {
        if ($option.name -eq $OptionName) {
            return $option.value
        }
    }

    return $null
}

function Test-GitHubActionsEnvironment {
    return $env:GITHUB_ACTIONS -eq "true"
}

function Get-DesiredBuildLibBSEValue {
    if (Test-GitHubActionsEnvironment) {
        return $false
    }

    return $true
}

function Get-DesiredBuildLibBSEMesonValue {
    if (Get-DesiredBuildLibBSEValue) {
        return "true"
    }

    return "false"
}

function Set-BuildLibBSEMesonArg {
    param([string[]]$MesonArgs)

    $desiredArg = "-Dbuild_libbse=$(Get-DesiredBuildLibBSEMesonValue)"
    $result = @()
    foreach ($arg in $MesonArgs) {
        if ($arg -like "-Dbuild_libbse=*") {
            continue
        }

        $result += $arg
    }

    $result += $desiredArg
    return $result
}

function Get-LatestFileWriteTimeUtc {
    param([string]$DirectoryPath)

    if ([string]::IsNullOrWhiteSpace($DirectoryPath) -or -not (Test-Path $DirectoryPath)) {
        return $null
    }

    $latest = $null
    foreach ($file in Get-ChildItem -Path $DirectoryPath -Recurse -File -ErrorAction SilentlyContinue) {
        if ($null -eq $latest -or $file.LastWriteTimeUtc -gt $latest) {
            $latest = $file.LastWriteTimeUtc
        }
    }

    return $latest
}

function Get-OpenQ4GameLibsRepoPath {
    param(
        [string]$RepoRoot,
        [string]$ConfiguredRepo
    )

    if (-not [string]::IsNullOrWhiteSpace($ConfiguredRepo)) {
        return [System.IO.Path]::GetFullPath($ConfiguredRepo)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "..\OpenQ4-GameLibs"))
}

function Test-GamelibsStageRefreshNeeded {
    param(
        [string]$BuildDir,
        [string]$RepoRoot,
        [string]$GameLibsRepo
    )

    if (-not (Test-MesonBuildDirectory $BuildDir)) {
        return $false
    }

    $buildEngine = Get-MesonBuildOptionValue -BuildDir $BuildDir -OptionName "build_engine"
    $buildGames = Get-MesonBuildOptionValue -BuildDir $BuildDir -OptionName "build_games"
    if ($buildEngine -ne $true -and $buildGames -ne $true) {
        return $false
    }

    $resolvedGameLibsRepo = Get-OpenQ4GameLibsRepoPath -RepoRoot $RepoRoot -ConfiguredRepo $GameLibsRepo
    $sourceGameDir = Join-Path $resolvedGameLibsRepo "src\game"
    $stagedGameDir = Join-Path $RepoRoot ".tmp\gamelibs_stage\src\game"

    if (-not (Test-Path $sourceGameDir)) {
        return $false
    }

    if (-not (Test-Path $stagedGameDir)) {
        return $true
    }

    $sourceLatest = Get-LatestFileWriteTimeUtc -DirectoryPath $sourceGameDir
    $stageLatest = Get-LatestFileWriteTimeUtc -DirectoryPath $stagedGameDir

    if ($null -eq $sourceLatest) {
        return $false
    }

    if ($null -eq $stageLatest) {
        return $true
    }

    return $sourceLatest -gt $stageLatest
}

function Test-BSEConfigRefreshNeeded {
    param([string]$BuildDir)

    if (-not (Test-MesonBuildDirectory $BuildDir)) {
        return $false
    }

    $currentValue = Get-MesonBuildOptionValue -BuildDir $BuildDir -OptionName "build_libbse"
    if ($null -eq $currentValue) {
        return $false
    }

    return [bool]$currentValue -ne (Get-DesiredBuildLibBSEValue)
}

function Remove-BSEArtifacts {
    param([string]$DirectoryPath)

    if ([string]::IsNullOrWhiteSpace($DirectoryPath) -or -not (Test-Path $DirectoryPath)) {
        return
    }

    $patterns = @(
        "OpenQ4-BSE_*.dll",
        "OpenQ4-BSE_*.dylib",
        "OpenQ4-BSE_*.so",
        "OpenQ4-BSE_*.lib",
        "OpenQ4-BSE_*.pdb"
    )

    foreach ($pattern in $patterns) {
        $matches = @(Get-ChildItem -Path $DirectoryPath -Filter $pattern -File -ErrorAction SilentlyContinue)
        foreach ($match in $matches) {
            Write-Host "Removing stale BSE artifact '$($match.FullName)'"
            Remove-Item -LiteralPath $match.FullName -Force
        }
    }
}

function Sync-BSEArtifactsForCurrentConfig {
    param(
        [string]$BuildDir,
        [string]$RepoRoot,
        [bool]$IncludeInstallRoot
    )

    if (-not (Test-MesonBuildDirectory $BuildDir)) {
        return
    }

    $buildLibBSE = Get-MesonBuildOptionValue -BuildDir $BuildDir -OptionName "build_libbse"
    if ($buildLibBSE -ne $false) {
        return
    }

    Remove-BSEArtifacts -DirectoryPath $BuildDir
    if ($IncludeInstallRoot) {
        Remove-BSEArtifacts -DirectoryPath (Join-Path $RepoRoot ".install")
    }
}

function Stop-OpenQ4RuntimeProcesses {
    [OutputType([bool])]
    param()

    $processNames = @(
        "OpenQ4-client_x64",
        "OpenQ4-ded_x64"
    )

    $running = @(Get-Process -Name $processNames -ErrorAction SilentlyContinue)
    if ($running.Count -eq 0) {
        return $false
    }

    Write-Host "Stopping running OpenQ4 processes before install: $($running.ProcessName -join ', ')"

    foreach ($proc in $running) {
        try {
            if ($proc.MainWindowHandle -ne 0) {
                $null = $proc.CloseMainWindow()
            }
        } catch {
            # Ignore close-window failures and fall back to force stop below.
        }
    }

    Start-Sleep -Milliseconds 500

    $stillRunning = @(Get-Process -Name $processNames -ErrorAction SilentlyContinue)
    if ($stillRunning.Count -gt 0) {
        try {
            $stillRunning | Stop-Process -Force -ErrorAction Stop
        } catch {
            throw "Failed to stop running OpenQ4 processes. Close them manually and retry install. Details: $($_.Exception.Message)"
        }
    }

    return $true
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDir "..\.."))
$defaultBuildDir = Join-Path $repoRoot "builddir"

$rcWrapper = Join-Path $scriptDir "rc.cmd"
if (-not (Test-Path $rcWrapper)) {
    throw "WINDRES wrapper not found at '$rcWrapper'."
}

$env:WINDRES = $rcWrapper

$vsDevCmd = $null
if ($null -eq (Get-Command cl -ErrorAction SilentlyContinue)) {
    $vsDevCmd = Get-VsDevCmdPath
}

$effectiveArgs = @($args)
if ($effectiveArgs.Count -eq 0) {
    throw "No Meson arguments were provided to meson_setup.ps1."
}

$commandName = $effectiveArgs[0].ToLowerInvariant()
$gameLibsRepo = if ([string]::IsNullOrWhiteSpace($env:OPENQ4_GAMELIBS_REPO)) { "" } else { $env:OPENQ4_GAMELIBS_REPO }
$buildGameLibsScript = Join-Path $scriptDir "build_gamelibs.ps1"
$syncIconsScript = Join-Path $scriptDir "sync_icons.py"

if ($commandName -eq "setup") {
    $effectiveArgs = Set-BuildLibBSEMesonArg -MesonArgs $effectiveArgs
}

$buildGameLibs = $env:OPENQ4_BUILD_GAMELIBS -eq "1"
if ($commandName -eq "compile" -and $buildGameLibs -and $env:OPENQ4_SKIP_GAMELIBS_BUILD -ne "1") {
    if (-not (Test-Path $buildGameLibsScript)) {
        throw "GameLibs build script not found: '$buildGameLibsScript'."
    }

    $buildArgs = @()
    if (-not [string]::IsNullOrWhiteSpace($gameLibsRepo)) {
        $buildArgs += @("-GameLibsRepo", $gameLibsRepo)
    }

    & $buildGameLibsScript @buildArgs
    $buildExit = [int]$LASTEXITCODE
    if ($buildExit -ne 0) {
        exit $buildExit
    }
}

if ($effectiveArgs.Length -gt 0 -and ($effectiveArgs[0] -eq "compile" -or $effectiveArgs[0] -eq "install")) {
    $isCompile = $effectiveArgs[0] -eq "compile"
    $isInstall = $effectiveArgs[0] -eq "install"
    $buildInfo = Get-CompileBuildDirInfo -MesonArgs $effectiveArgs -DefaultBuildDir $defaultBuildDir

    if ($isCompile -and -not (Test-MesonBuildDirectory $buildInfo.BuildDir)) {
        Write-Host "Meson build directory '$($buildInfo.BuildDir)' is missing or invalid. Running meson setup..."
        $setupArgs = @(
            "setup",
            "--wipe",
            $buildInfo.BuildDir,
            $repoRoot,
            "--backend",
            "ninja",
            "--buildtype=debug",
            "--wrap-mode=forcefallback"
        )
        $setupArgs = Set-BuildLibBSEMesonArg -MesonArgs $setupArgs
        Invoke-Meson -MesonArgs $setupArgs -VsDevCmdPath $vsDevCmd
        $setupCode = [int]$LASTEXITCODE
        if ($setupCode -ne 0) {
            exit $setupCode
        }
    }

    $needsGameLibsRefresh = Test-GamelibsStageRefreshNeeded -BuildDir $buildInfo.BuildDir -RepoRoot $repoRoot -GameLibsRepo $gameLibsRepo
    $needsBSERefresh = Test-BSEConfigRefreshNeeded -BuildDir $buildInfo.BuildDir
    if ($needsGameLibsRefresh -or $needsBSERefresh) {
        if ($needsGameLibsRefresh) {
            Write-Host "OpenQ4-GameLibs sources changed since the last staged snapshot. Reconfiguring '$($buildInfo.BuildDir)'..."
        }
        if ($needsBSERefresh) {
            Write-Host "Applying local/CI BSE policy to '$($buildInfo.BuildDir)' (build_libbse=$(Get-DesiredBuildLibBSEMesonValue))..."
        }
        $reconfigureArgs = @(
            "setup",
            "--reconfigure",
            $buildInfo.BuildDir,
            $repoRoot
        )
        $reconfigureArgs = Set-BuildLibBSEMesonArg -MesonArgs $reconfigureArgs
        Invoke-Meson -MesonArgs $reconfigureArgs -VsDevCmdPath $vsDevCmd
        $reconfigureCode = [int]$LASTEXITCODE
        if ($reconfigureCode -ne 0) {
            exit $reconfigureCode
        }
    }

    if (-not $buildInfo.HasExplicit) {
        $remainingArgs = @()
        if ($effectiveArgs.Length -gt 1) {
            $remainingArgs = $effectiveArgs[1..($effectiveArgs.Length - 1)]
        }
        $effectiveArgs = @($effectiveArgs[0], "-C", $buildInfo.BuildDir) + $remainingArgs
    }

    if ($isInstall -and -not ($effectiveArgs -contains "--skip-subprojects")) {
        $effectiveArgs += "--skip-subprojects"
    }
}

if ($commandName -eq "install" -and $env:OPENQ4_INSTALL_CLOSE_RUNNING -ne "0") {
    Stop-OpenQ4RuntimeProcesses | Out-Null
}

if (@("setup", "compile", "install").Contains($commandName) -and $env:OPENQ4_SKIP_ICON_SYNC -ne "1") {
    if (-not (Test-Path $syncIconsScript)) {
        throw "Icon sync script not found: '$syncIconsScript'."
    }

    & python $syncIconsScript "--source-root" $repoRoot
    $syncIconExit = [int]$LASTEXITCODE
    if ($syncIconExit -ne 0) {
        exit $syncIconExit
    }
}

Invoke-Meson -MesonArgs $effectiveArgs -VsDevCmdPath $vsDevCmd
$exitCode = [int]$LASTEXITCODE

if ($commandName -eq "install" -and $exitCode -ne 0 -and $env:OPENQ4_INSTALL_RETRY_ON_FAILURE -ne "0") {
    Write-Host "Meson install failed; retrying once after ensuring OpenQ4 processes are stopped..."
    Stop-OpenQ4RuntimeProcesses | Out-Null
    Start-Sleep -Milliseconds 500
    Invoke-Meson -MesonArgs $effectiveArgs -VsDevCmdPath $vsDevCmd
    $exitCode = [int]$LASTEXITCODE
}

if ($exitCode -eq 0 -and ($commandName -eq "compile" -or $commandName -eq "install")) {
    $includeInstallRoot = $commandName -eq "install"
    $buildInfo = Get-CompileBuildDirInfo -MesonArgs $effectiveArgs -DefaultBuildDir $defaultBuildDir
    Sync-BSEArtifactsForCurrentConfig -BuildDir $buildInfo.BuildDir -RepoRoot $repoRoot -IncludeInstallRoot:$includeInstallRoot
}

exit $exitCode
