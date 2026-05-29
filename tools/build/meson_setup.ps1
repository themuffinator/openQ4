$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-VsDevCmdPath {
    param([string]$TargetArch)

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

    $component = if ($TargetArch -eq "arm64") {
        "Microsoft.VisualStudio.Component.VC.Tools.ARM64"
    } else {
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
    }
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

function Get-OpenQ4VsProcessArch {
    $arch = [System.Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString().ToLowerInvariant()
    switch ($arch) {
        "x64" { return "x64" }
        "x86" { return "x86" }
        "arm64" { return "arm64" }
        default { return "x64" }
    }
}

function Get-OpenQ4VsTargetArch {
    if (-not [string]::IsNullOrWhiteSpace($env:OPENQ4_VS_TARGET_ARCH)) {
        return $env:OPENQ4_VS_TARGET_ARCH.Trim().ToLowerInvariant()
    }

    return Get-OpenQ4VsProcessArch
}

function Get-OpenQ4VsHostArch {
    if (-not [string]::IsNullOrWhiteSpace($env:OPENQ4_VS_HOST_ARCH)) {
        return $env:OPENQ4_VS_HOST_ARCH.Trim().ToLowerInvariant()
    }

    return Get-OpenQ4VsProcessArch
}

function Get-MesonCommand {
    $pythonCandidates = @("python", "python3")
    foreach ($candidate in $pythonCandidates) {
        $pythonCommand = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($null -eq $pythonCommand) {
            continue
        }

        & $pythonCommand.Source -c "import mesonbuild.mesonmain" *> $null
        if ($LASTEXITCODE -eq 0) {
            return [PSCustomObject]@{
                Executable = $pythonCommand.Source
                Arguments  = @("-m", "mesonbuild.mesonmain")
            }
        }
    }

    $mesonCommand = Get-Command "meson" -ErrorAction SilentlyContinue
    if ($null -ne $mesonCommand) {
        return [PSCustomObject]@{
            Executable = $mesonCommand.Source
            Arguments  = @()
        }
    }

    throw "Could not find Meson. Install it into the active Python environment or make 'meson' available on PATH."
}

function Invoke-Meson {
    param(
        [string[]]$MesonArgs,
        [string]$VsDevCmdPath,
        [pscustomobject]$MesonCommand,
        [string]$VsTargetArch,
        [string]$VsHostArch
    )

    $mesonInvocation = @($MesonCommand.Arguments) + $MesonArgs
    if ([string]::IsNullOrWhiteSpace($VsDevCmdPath)) {
        & $MesonCommand.Executable @mesonInvocation
        return
    }

    $mesonCommandLine = @($MesonCommand.Executable) + $MesonCommand.Arguments + $MesonArgs
    $mesonCmd = ($mesonCommandLine | ForEach-Object { Quote-CmdArg $_ }) -join " "
    $fullCmd = 'call "' + $VsDevCmdPath + '" -arch=' + $VsTargetArch + ' -host_arch=' + $VsHostArch + ' >nul && ' + $mesonCmd
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
    if (Test-Path $introOptionsPath) {
        $introOptions = Get-Content $introOptionsPath -Raw | ConvertFrom-Json
        foreach ($option in $introOptions) {
            if ($option.name -eq $OptionName) {
                return $option.value
            }
        }
    }

    $cmdLinePath = Join-Path $BuildDir "meson-private\cmd_line.txt"
    if (-not (Test-Path $cmdLinePath)) {
        return $null
    }

    $inOptionsSection = $false
    foreach ($rawLine in Get-Content $cmdLinePath) {
        $line = $rawLine.Trim()
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        if ($line.StartsWith("[") -and $line.EndsWith("]")) {
            $inOptionsSection = $line -eq "[options]"
            continue
        }

        if (-not $inOptionsSection) {
            continue
        }

        $delimiterIndex = $line.IndexOf("=")
        if ($delimiterIndex -lt 0) {
            continue
        }

        $name = $line.Substring(0, $delimiterIndex).Trim()
        if ($name -ne $OptionName) {
            continue
        }

        $value = $line.Substring($delimiterIndex + 1).Trim()
        switch ($value.ToLowerInvariant()) {
            "true" { return $true }
            "false" { return $false }
            default { return $value }
        }
    }

    return $null
}

function Test-GitHubActionsEnvironment {
    return $env:GITHUB_ACTIONS -eq "true"
}

function Test-WindowsHost {
    return [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT
}

function Get-RequiredWindowsCRTOptionValue {
    return "static_from_buildtype"
}

function Ensure-WindowsStaticCRTSetupArgs {
    param([string[]]$MesonArgs)

    if (-not (Test-WindowsHost)) {
        return @($MesonArgs)
    }

    $requiredValue = Get-RequiredWindowsCRTOptionValue
    $requiredArg = "-Db_vscrt=$requiredValue"
    $updatedArgs = @()
    $found = $false

    foreach ($arg in $MesonArgs) {
        if ($arg -like "-Db_vscrt=*") {
            $found = $true
            if ($arg -ne $requiredArg) {
                Write-Host "Overriding Meson b_vscrt option to '$requiredValue' to satisfy OpenQ4 Windows static CRT policy."
            }
            $updatedArgs += $requiredArg
            continue
        }

        $updatedArgs += $arg
    }

    if (-not $found) {
        $updatedArgs += $requiredArg
    }

    return @($updatedArgs)
}

function Test-WindowsStaticCRTReconfigureNeeded {
    param([string]$BuildDir)

    if (-not (Test-WindowsHost) -or -not (Test-MesonBuildDirectory $BuildDir)) {
        return $false
    }

    $configuredValue = Get-MesonBuildOptionValue -BuildDir $BuildDir -OptionName "b_vscrt"
    $requiredValue = Get-RequiredWindowsCRTOptionValue
    return [string]$configuredValue -ne $requiredValue
}

function Test-ObsoleteBSEBuildOptionPresent {
    param([string]$BuildDir)

    if ([string]::IsNullOrWhiteSpace($BuildDir) -or -not (Test-Path $BuildDir)) {
        return $false
    }

    return $null -ne (Get-MesonBuildOptionValue -BuildDir $BuildDir -OptionName "build_libbse")
}

function Format-MesonOptionValue {
    param([object]$Value)

    if ($Value -is [bool]) {
        return $Value.ToString().ToLowerInvariant()
    }

    return [string]$Value
}

function Get-SetupArgsForExistingBuildDir {
    param(
        [string]$BuildDir,
        [string]$RepoRoot
    )

    $setupArgs = @(
        "setup",
        $BuildDir,
        $RepoRoot,
        "--backend",
        "ninja"
    )

    $buildtype = Get-MesonBuildOptionValue -BuildDir $BuildDir -OptionName "buildtype"
    if (-not [string]::IsNullOrWhiteSpace([string]$buildtype)) {
        $setupArgs += "--buildtype=$(Format-MesonOptionValue -Value $buildtype)"
    }

    $wrapMode = Get-MesonBuildOptionValue -BuildDir $BuildDir -OptionName "wrap_mode"
    if (-not [string]::IsNullOrWhiteSpace([string]$wrapMode)) {
        $setupArgs += "--wrap-mode=$(Format-MesonOptionValue -Value $wrapMode)"
    }

    $optionNames = @(
        "platform_backend",
        "version_track",
        "version_iteration",
        "version_base_override",
        "openal_root_override",
        "use_pch",
        "build_engine",
        "build_games",
        "build_game_sp",
        "build_game_mp",
        "enforce_msvc_2026"
    )

    foreach ($optionName in $optionNames) {
        $value = Get-MesonBuildOptionValue -BuildDir $BuildDir -OptionName $optionName
        if ($null -eq $value) {
            continue
        }

        $formattedValue = Format-MesonOptionValue -Value $value
        if ([string]::IsNullOrWhiteSpace($formattedValue)) {
            continue
        }

        $setupArgs += "-D$optionName=$formattedValue"
    }

    return @(Ensure-WindowsStaticCRTSetupArgs -MesonArgs $setupArgs)
}

function Recreate-MesonBuildDirectory {
    param(
        [string]$BuildDir,
        [string[]]$SetupArgs,
        [string]$VsDevCmdPath,
        [pscustomobject]$MesonCommand
    )

    if (Test-Path $BuildDir) {
        Remove-Item -LiteralPath $BuildDir -Recurse -Force
    }

    Invoke-Meson -MesonArgs $SetupArgs -VsDevCmdPath $VsDevCmdPath -MesonCommand $MesonCommand -VsTargetArch $vsTargetArch -VsHostArch $vsHostArch
    return [int]$LASTEXITCODE
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

function Remove-BSEArtifacts {
    param([string]$DirectoryPath)

    if ([string]::IsNullOrWhiteSpace($DirectoryPath) -or -not (Test-Path $DirectoryPath)) {
        return
    }

    $patterns = @(
        "openQ4-BSE_*.dll",
        "openQ4-BSE_*.dylib",
        "openQ4-BSE_*.so",
        "openQ4-BSE_*.lib",
        "openQ4-BSE_*.pdb",
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

function Remove-NonRuntimeInstallArtifacts {
    param([string]$InstallRoot)

    if ([string]::IsNullOrWhiteSpace($InstallRoot) -or -not (Test-Path $InstallRoot)) {
        return
    }

    $rootPatterns = @(
        "*.pdb",
        "*.lib",
        "*.exp",
        "*.ilk",
        "*.map",
        "*.zip",
        "mgscope_sendinput.cfg",
        "scope_autotest*.cfg"
    )

    foreach ($pattern in $rootPatterns) {
        $matches = @(Get-ChildItem -Path $InstallRoot -Filter $pattern -File -ErrorAction SilentlyContinue)
        foreach ($match in $matches) {
            Write-Host "Removing non-runtime staged artifact '$($match.FullName)'"
            Remove-Item -LiteralPath $match.FullName -Force
        }
    }

    foreach ($dirName in @("crashes")) {
        $dirPath = Join-Path $InstallRoot $dirName
        if (Test-Path $dirPath) {
            Write-Host "Removing non-runtime staged directory '$dirPath'"
            Remove-Item -LiteralPath $dirPath -Recurse -Force
        }
    }

    $installGameDir = Join-Path $InstallRoot "baseoq4"
    if (-not (Test-Path $installGameDir)) {
        return
    }

    $gameDirPatterns = @(
        "*.pdb",
        "*.lib",
        "*.exp",
        "*.ilk",
        "*.map",
        "*.so",
        "*.dylib",
        "game-sp_x86.dll",
        "game-mp_x86.dll"
    )

    foreach ($pattern in $gameDirPatterns) {
        $matches = @(Get-ChildItem -Path $installGameDir -Filter $pattern -File -ErrorAction SilentlyContinue)
        foreach ($match in $matches) {
            Write-Host "Removing non-runtime staged artifact '$($match.FullName)'"
            Remove-Item -LiteralPath $match.FullName -Force
        }
    }
}

function Stop-OpenQ4RuntimeProcesses {
    [OutputType([bool])]
    param()

    $processNames = @(
        "openQ4-client_x64",
        "openQ4-client_x86",
        "openQ4-client_arm64",
        "openQ4-ded_x64",
        "openQ4-ded_x86",
        "openQ4-ded_arm64",
        "OpenQ4-client_x64",
        "OpenQ4-client_x86",
        "OpenQ4-client_arm64",
        "OpenQ4-ded_x64",
        "OpenQ4-ded_x86",
        "OpenQ4-ded_arm64"
    )

    $running = @(Get-Process -Name $processNames -ErrorAction SilentlyContinue)
    if ($running.Count -eq 0) {
        return $false
    }

    Write-Host "Stopping running openQ4 processes before install: $($running.ProcessName -join ', ')"

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
            throw "Failed to stop running openQ4 processes. Close them manually and retry install. Details: $($_.Exception.Message)"
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

$vsTargetArch = Get-OpenQ4VsTargetArch
$vsHostArch = Get-OpenQ4VsHostArch
$vsDevCmd = $null
if (Test-WindowsHost) {
    $vsDevCmd = Get-VsDevCmdPath -TargetArch $vsTargetArch
}
$mesonCommand = Get-MesonCommand

$effectiveArgs = @($args)
if ($effectiveArgs.Count -eq 0) {
    throw "No Meson arguments were provided to meson_setup.ps1."
}

$commandName = $effectiveArgs[0].ToLowerInvariant()
$gameLibsRepo = if ([string]::IsNullOrWhiteSpace($env:OPENQ4_GAMELIBS_REPO)) { "" } else { $env:OPENQ4_GAMELIBS_REPO }
$buildGameLibsScript = Join-Path $scriptDir "build_gamelibs.ps1"
$stageWindowsRuntimeScript = Join-Path $scriptDir "stage_windows_runtime.py"
$syncIconsScript = Join-Path $scriptDir "sync_icons.py"

if ($commandName -eq "setup") {
    $effectiveArgs = Ensure-WindowsStaticCRTSetupArgs -MesonArgs $effectiveArgs
}

if ($commandName -eq "setup" -and ($effectiveArgs -contains "--reconfigure")) {
    $reconfigureIndex = [Array]::IndexOf($effectiveArgs, "--reconfigure")
    if ($reconfigureIndex -ge 0 -and ($reconfigureIndex + 1) -lt $effectiveArgs.Length) {
        $candidateBuildDir = [System.IO.Path]::GetFullPath($effectiveArgs[$reconfigureIndex + 1])
        if (Test-ObsoleteBSEBuildOptionPresent -BuildDir $candidateBuildDir) {
            Write-Host "Meson build directory '$candidateBuildDir' still uses the removed build_libbse option. Recreating it..."
            $effectiveArgs = @($effectiveArgs | Where-Object { $_ -ne "--reconfigure" })
            if (Test-Path $candidateBuildDir) {
                Remove-Item -LiteralPath $candidateBuildDir -Recurse -Force
            }
        }
    }
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
        $setupArgs = if (Test-ObsoleteBSEBuildOptionPresent -BuildDir $buildInfo.BuildDir) {
            Write-Host "Meson build directory '$($buildInfo.BuildDir)' still uses the removed build_libbse option. Recreating it..."
            Get-SetupArgsForExistingBuildDir -BuildDir $buildInfo.BuildDir -RepoRoot $repoRoot
        } else {
            @(
                "setup",
                $buildInfo.BuildDir,
                $repoRoot,
                "--backend",
                "ninja",
                "--buildtype=debug",
                "--wrap-mode=forcefallback"
            )
        }
        $setupArgs = Ensure-WindowsStaticCRTSetupArgs -MesonArgs $setupArgs

        if (Test-ObsoleteBSEBuildOptionPresent -BuildDir $buildInfo.BuildDir) {
            $setupCode = Recreate-MesonBuildDirectory -BuildDir $buildInfo.BuildDir -SetupArgs $setupArgs -VsDevCmdPath $vsDevCmd -MesonCommand $mesonCommand
        } else {
            Invoke-Meson -MesonArgs $setupArgs -VsDevCmdPath $vsDevCmd -MesonCommand $mesonCommand -VsTargetArch $vsTargetArch -VsHostArch $vsHostArch
            $setupCode = [int]$LASTEXITCODE
        }
        $setupCode = [int]$LASTEXITCODE
        if ($setupCode -ne 0) {
            exit $setupCode
        }
    }
    elseif (Test-ObsoleteBSEBuildOptionPresent -BuildDir $buildInfo.BuildDir) {
        Write-Host "Meson build directory '$($buildInfo.BuildDir)' still uses the removed build_libbse option. Recreating it..."
        $setupArgs = Get-SetupArgsForExistingBuildDir -BuildDir $buildInfo.BuildDir -RepoRoot $repoRoot
        $setupCode = Recreate-MesonBuildDirectory -BuildDir $buildInfo.BuildDir -SetupArgs $setupArgs -VsDevCmdPath $vsDevCmd -MesonCommand $mesonCommand
        if ($setupCode -ne 0) {
            exit $setupCode
        }
    }

    $reconfigureReasons = @()
    $needsWindowsStaticCRTRefresh = Test-WindowsStaticCRTReconfigureNeeded -BuildDir $buildInfo.BuildDir
    if ($needsWindowsStaticCRTRefresh) {
        $configuredCRT = Get-MesonBuildOptionValue -BuildDir $buildInfo.BuildDir -OptionName "b_vscrt"
        if ([string]::IsNullOrWhiteSpace([string]$configuredCRT)) {
            $configuredCRT = "unset"
        }
        Write-Host "Meson build directory '$($buildInfo.BuildDir)' uses b_vscrt='$configuredCRT'. Reconfiguring for OpenQ4's required Windows static CRT policy..."
        $reconfigureReasons += "Windows static CRT policy"
    }

    $needsGameLibsRefresh = Test-GamelibsStageRefreshNeeded -BuildDir $buildInfo.BuildDir -RepoRoot $repoRoot -GameLibsRepo $gameLibsRepo
    if ($needsGameLibsRefresh) {
        Write-Host "OpenQ4-GameLibs sources changed since the last staged snapshot. Reconfiguring '$($buildInfo.BuildDir)'..."
        $reconfigureReasons += "staged OpenQ4-GameLibs refresh"
    }

    if ($reconfigureReasons.Count -gt 0) {
        $reconfigureArgs = @(
            "setup",
            "--reconfigure",
            $buildInfo.BuildDir,
            $repoRoot
        )
        $reconfigureArgs = Ensure-WindowsStaticCRTSetupArgs -MesonArgs $reconfigureArgs
        Invoke-Meson -MesonArgs $reconfigureArgs -VsDevCmdPath $vsDevCmd -MesonCommand $mesonCommand -VsTargetArch $vsTargetArch -VsHostArch $vsHostArch
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

Invoke-Meson -MesonArgs $effectiveArgs -VsDevCmdPath $vsDevCmd -MesonCommand $mesonCommand -VsTargetArch $vsTargetArch -VsHostArch $vsHostArch
$exitCode = [int]$LASTEXITCODE

if ($commandName -eq "install" -and $exitCode -ne 0 -and $env:OPENQ4_INSTALL_RETRY_ON_FAILURE -ne "0") {
    Write-Host "Meson install failed; retrying once after ensuring openQ4 processes are stopped..."
    Stop-OpenQ4RuntimeProcesses | Out-Null
    Start-Sleep -Milliseconds 500
    Invoke-Meson -MesonArgs $effectiveArgs -VsDevCmdPath $vsDevCmd -MesonCommand $mesonCommand -VsTargetArch $vsTargetArch -VsHostArch $vsHostArch
    $exitCode = [int]$LASTEXITCODE
}

if ($exitCode -eq 0 -and ($commandName -eq "compile" -or $commandName -eq "install")) {
    $includeInstallRoot = $commandName -eq "install"
    $buildInfo = Get-CompileBuildDirInfo -MesonArgs $effectiveArgs -DefaultBuildDir $defaultBuildDir
    Remove-BSEArtifacts -DirectoryPath $buildInfo.BuildDir
    $installRootPath = Join-Path $repoRoot ".install"
    Remove-NonRuntimeInstallArtifacts -InstallRoot $installRootPath
    if ($includeInstallRoot) {
        Remove-BSEArtifacts -DirectoryPath $installRootPath
    }

    if (-not (Test-Path $stageWindowsRuntimeScript)) {
        throw "Windows runtime staging script not found: '$stageWindowsRuntimeScript'."
    }

    $runtimeArgs = @(
        "--source-root", $repoRoot,
        "--build-dir", $buildInfo.BuildDir
    )
    if ($includeInstallRoot) {
        $runtimeArgs += @("--install-dir", $installRootPath)
    }

    & python $stageWindowsRuntimeScript @runtimeArgs
    $runtimeExit = [int]$LASTEXITCODE
    if ($runtimeExit -ne 0) {
        exit $runtimeExit
    }
}

exit $exitCode
