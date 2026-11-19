param(
    [ValidateSet("x64", "x86", "arm64")]
    [string]$Architecture = "arm64",
    [string]$Version = "1.25.2",
    [string]$NuGetPackageVersion = "1.23.1",
    [string]$OutputRoot = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-RepoRoot {
    return [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
}

function Quote-CmdArg([string]$Value) {
    if ($Value -match '[\s"&<>|()]') {
        return '"' + ($Value -replace '"', '\"') + '"'
    }
    return $Value
}

function Get-ProcessArch {
    $arch = [System.Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString().ToLowerInvariant()
    switch ($arch) {
        "x64" { return "x64" }
        "x86" { return "x86" }
        "arm64" { return "arm64" }
        default { return "x64" }
    }
}

function Get-VsComponentId([string]$TargetArch) {
    switch ($TargetArch) {
        "arm64" { return "Microsoft.VisualStudio.Component.VC.Tools.ARM64" }
        default { return "Microsoft.VisualStudio.Component.VC.Tools.x86.x64" }
    }
}

function Get-VsDevCmdPath([string]$TargetArch) {
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

    $component = Get-VsComponentId -TargetArch $TargetArch
    $installPathRaw = & $vswhere -latest -prerelease -version "[18.0,19.0)" -products * -requires $component -property installationPath
    $installPath = if ($null -eq $installPathRaw) { "" } else { "$installPathRaw".Trim() }
    if ([string]::IsNullOrWhiteSpace($installPath)) {
        $installPathRaw = & $vswhere -latest -prerelease -products * -requires $component -property installationPath
        $installPath = if ($null -eq $installPathRaw) { "" } else { "$installPathRaw".Trim() }
    }
    if ([string]::IsNullOrWhiteSpace($installPath)) {
        throw "No Visual Studio installation with component '$component' was found."
    }

    $vsDevCmd = Join-Path $installPath "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path $vsDevCmd)) {
        throw "Could not locate VsDevCmd.bat at '$vsDevCmd'."
    }

    return $vsDevCmd
}

function Assert-WorkspacePath {
    param(
        [string]$Path,
        [string]$RepoRoot
    )

    $resolvedPath = [System.IO.Path]::GetFullPath($Path)
    $resolvedRoot = [System.IO.Path]::GetFullPath($RepoRoot)
    # CI runs this under Windows PowerShell 5.1, whose .NET Framework has no
    # Path.TrimEndingDirectorySeparator.
    $normalizedRoot = $resolvedRoot.TrimEnd([char[]]@('\', '/'))
    $rootPrefix = $normalizedRoot + [System.IO.Path]::DirectorySeparatorChar
    if (
        -not $resolvedPath.Equals($normalizedRoot, [System.StringComparison]::OrdinalIgnoreCase) -and
        -not $resolvedPath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)
    ) {
        throw "Refusing to operate on path outside the repository: '$resolvedPath'"
    }
}

function Remove-WorkspaceDirectory {
    param(
        [string]$Path,
        [string]$RepoRoot
    )

    if (-not (Test-Path $Path)) {
        return
    }

    Assert-WorkspacePath -Path $Path -RepoRoot $RepoRoot
    Remove-Item -LiteralPath $Path -Recurse -Force
}

# OpenAL Soft 1.25.2 passes &std::unique_ptr<...>::operator* to
# std::views::transform. The standard leaves forming a pointer to a standard
# library member function unspecified, and MSVC 19.52's STL rejects it
# (C3889 in alc/alc.cpp). A lambda does the same dereference. The repair is
# exact and idempotent, and fails if the pinned source ever reads differently.
function Repair-OpenAlSource {
    param(
        [string]$SourceDir,
        [string]$Version
    )

    if ($Version -ne "1.25.2") {
        return
    }

    $path = Join-Path $SourceDir "alc\alc.cpp"
    $original = "std::views::transform(&ContextBase::EffectSlotCluster::operator*)"
    $repaired = "std::views::transform([](ContextBase::EffectSlotCluster const &cluster) -> auto& { return *cluster; })"
    $text = [System.IO.File]::ReadAllText($path)
    if ($text.Contains($repaired)) {
        return
    }

    $first = $text.IndexOf($original, [System.StringComparison]::Ordinal)
    if ($first -lt 0 -or $text.IndexOf($original, $first + 1, [System.StringComparison]::Ordinal) -ge 0) {
        throw "OpenAL Soft $Version alc/alc.cpp does not contain the expected EffectSlotCluster transform exactly once."
    }

    [System.IO.File]::WriteAllText($path, $text.Replace($original, $repaired), (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "Repaired OpenAL Soft $Version alc/alc.cpp for MSVC (EffectSlotCluster transform)"
}

function Invoke-CmdChain {
    param([string[]]$Commands)

    $fullCmd = ($Commands | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }) -join " && "
    & $env:ComSpec /d /c $fullCmd
    if ($LASTEXITCODE -ne 0) {
        throw "Command chain failed with exit code $LASTEXITCODE."
    }
}

function Get-NuGetRid([string]$TargetArch) {
    switch ($TargetArch) {
        "x64" { return "win-x64" }
        "x86" { return "win-x86" }
        "arm64" { return "win-arm64" }
        default { return "" }
    }
}

function Try-PrepareOpenAlFromNuGet {
    param(
        [string]$TargetArch,
        [string]$PackageVersion,
        [string]$DownloadsDir,
        [string]$TmpRoot,
        [string]$OutputRoot,
        [string]$RepoRoot
    )

    $rid = Get-NuGetRid -TargetArch $TargetArch
    if ([string]::IsNullOrWhiteSpace($rid)) {
        return $false
    }

    $packageDownloadPath = Join-Path $DownloadsDir "openal.soft-$PackageVersion.nupkg"
    $packageArchivePath = Join-Path $DownloadsDir "openal.soft-$PackageVersion.zip"
    $packageRoot = Join-Path $TmpRoot "openal-soft-nuget-$PackageVersion"
    $packageUrl = "https://api.nuget.org/v3-flatcontainer/openal.soft/$PackageVersion/openal.soft.$PackageVersion.nupkg"

    if (-not (Test-Path $packageDownloadPath)) {
        try {
            Invoke-WebRequest -Uri $packageUrl -OutFile $packageDownloadPath
        } catch {
            Write-Warning "NuGet OpenAL package download failed for version ${PackageVersion}: $($_.Exception.Message)"
            return $false
        }
    }

    if (-not (Test-Path (Join-Path $packageRoot "OpenAL.Soft.nuspec"))) {
        Remove-WorkspaceDirectory -Path $packageRoot -RepoRoot $RepoRoot
        try {
            Copy-Item -LiteralPath $packageDownloadPath -Destination $packageArchivePath -Force
            Expand-Archive -LiteralPath $packageArchivePath -DestinationPath $packageRoot
        } catch {
            Write-Warning "NuGet OpenAL package extraction failed for version ${PackageVersion}: $($_.Exception.Message)"
            return $false
        }
    }

    $runtimeDir = Join-Path $packageRoot "runtimes\$rid\native\Release"
    $headerDir = Join-Path $packageRoot "sources\AL"
    $runtimePath = Join-Path $runtimeDir "OpenAL32.dll"
    $importLibPath = Join-Path $runtimeDir "OpenAL32.lib"

    if (-not (Test-Path $runtimePath) -or -not (Test-Path $importLibPath) -or -not (Test-Path $headerDir)) {
        Write-Warning "NuGet OpenAL package $PackageVersion does not contain a complete Release payload for $rid."
        return $false
    }

    Remove-WorkspaceDirectory -Path $OutputRoot -RepoRoot $RepoRoot
    $binDir = Join-Path $OutputRoot "bin"
    $libDir = Join-Path $OutputRoot "lib"
    $includeDir = Join-Path $OutputRoot "include\AL"
    New-Item -ItemType Directory -Path $binDir -Force | Out-Null
    New-Item -ItemType Directory -Path $libDir -Force | Out-Null
    New-Item -ItemType Directory -Path $includeDir -Force | Out-Null

    Copy-Item -LiteralPath $runtimePath -Destination (Join-Path $binDir "OpenAL32.dll") -Force
    Copy-Item -LiteralPath $importLibPath -Destination (Join-Path $libDir "OpenAL32.lib") -Force
    Copy-Item -Path (Join-Path $headerDir "*") -Destination $includeDir -Recurse -Force

    return $true
}

$repoRoot = Get-RepoRoot
$tmpRoot = Join-Path $repoRoot ".tmp"
$downloadsDir = Join-Path $tmpRoot "downloads"
$downloadPath = Join-Path $downloadsDir "openal-soft-$Version.zip"
$sourceRoot = Join-Path $tmpRoot "openal-soft-src-$Version"
$sourceDir = Join-Path $sourceRoot "openal-soft-$Version"
$buildDir = Join-Path $tmpRoot "openal-soft-build-$Architecture"

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $tmpRoot "openal-soft-windows-$Architecture"
}

$outputRootFull = [System.IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $downloadsDir -Force | Out-Null
New-Item -ItemType Directory -Path $tmpRoot -Force | Out-Null

Remove-WorkspaceDirectory -Path $buildDir -RepoRoot $repoRoot
Remove-WorkspaceDirectory -Path $outputRootFull -RepoRoot $repoRoot
New-Item -ItemType Directory -Path $outputRootFull -Force | Out-Null

$vsTargetArch = if ([string]::IsNullOrWhiteSpace($env:OPENQ4_VS_TARGET_ARCH)) { $Architecture } else { $env:OPENQ4_VS_TARGET_ARCH.Trim().ToLowerInvariant() }
$vsHostArch = if ([string]::IsNullOrWhiteSpace($env:OPENQ4_VS_HOST_ARCH)) { Get-ProcessArch } else { $env:OPENQ4_VS_HOST_ARCH.Trim().ToLowerInvariant() }
$preparationSource = "source-build"
try {
    if (-not (Test-Path $downloadPath)) {
        $sourceUrl = "https://github.com/kcat/openal-soft/archive/refs/tags/$Version.zip"
        Invoke-WebRequest -Uri $sourceUrl -OutFile $downloadPath
    }

    if (-not (Test-Path $sourceDir)) {
        Remove-WorkspaceDirectory -Path $sourceRoot -RepoRoot $repoRoot
        Expand-Archive -LiteralPath $downloadPath -DestinationPath $sourceRoot
    }

    if (-not (Test-Path (Join-Path $sourceDir "CMakeLists.txt"))) {
        throw "OpenAL Soft source directory is missing a CMakeLists.txt file: '$sourceDir'"
    }

    Repair-OpenAlSource -SourceDir $sourceDir -Version $Version

    $vsDevCmd = Get-VsDevCmdPath -TargetArch $vsTargetArch

    $cmakeConfigure = @(
        "cmake",
        "-S", $sourceDir,
        "-B", $buildDir,
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_INSTALL_PREFIX=$outputRootFull",
        "-DLIBTYPE=SHARED",
        "-DALSOFT_UTILS=OFF",
        "-DALSOFT_NO_CONFIG_UTIL=ON",
        "-DALSOFT_EXAMPLES=OFF",
        "-DALSOFT_TESTS=OFF",
        "-DALSOFT_INSTALL=ON",
        "-DALSOFT_INSTALL_CONFIG=OFF",
        "-DALSOFT_INSTALL_HRTF_DATA=OFF",
        "-DALSOFT_INSTALL_AMBDEC_PRESETS=OFF",
        "-DALSOFT_INSTALL_EXAMPLES=OFF",
        "-DALSOFT_INSTALL_UTILS=OFF",
        "-DALSOFT_UPDATE_BUILD_VERSION=OFF"
    )
    $cmakeBuild = @("cmake", "--build", $buildDir, "--config", "Release")
    $cmakeInstall = @("cmake", "--install", $buildDir, "--config", "Release")

    $commandChain = @(
        ('call "' + $vsDevCmd + '" -arch=' + $vsTargetArch + ' -host_arch=' + $vsHostArch + ' >nul')
        (($cmakeConfigure | ForEach-Object { Quote-CmdArg $_ }) -join " ")
        (($cmakeBuild | ForEach-Object { Quote-CmdArg $_ }) -join " ")
        (($cmakeInstall | ForEach-Object { Quote-CmdArg $_ }) -join " ")
    )

    Invoke-CmdChain -Commands $commandChain
} catch {
    if (-not (Try-PrepareOpenAlFromNuGet -TargetArch $Architecture -PackageVersion $NuGetPackageVersion -DownloadsDir $downloadsDir -TmpRoot $tmpRoot -OutputRoot $outputRootFull -RepoRoot $repoRoot)) {
        throw
    }
    $preparationSource = "nuget"
    Write-Warning "Source-based OpenAL preparation failed for Windows $Architecture; used NuGet package $NuGetPackageVersion instead. $($_.Exception.Message)"
}

$expectedRuntime = Join-Path $outputRootFull "bin\OpenAL32.dll"
$expectedImportLib = Join-Path $outputRootFull "lib\OpenAL32.lib"

if (-not (Test-Path $expectedRuntime)) {
    throw "OpenAL Soft runtime was not installed: '$expectedRuntime'"
}
if (-not (Test-Path $expectedImportLib)) {
    throw "OpenAL Soft import library was not installed: '$expectedImportLib'"
}

Write-Host "Prepared OpenAL Soft for Windows $Architecture"
Write-Host "Preparation source: $preparationSource"
if ($preparationSource -eq "source-build") {
    Write-Host "Source archive: $downloadPath"
    Write-Host "Source directory: $sourceDir"
    Write-Host "Build directory: $buildDir"
} else {
    Write-Host "NuGet package version: $NuGetPackageVersion"
}
Write-Host "Install root: $outputRootFull"
