param(
    [ValidateSet("x64", "x86", "arm64")]
    [string]$Architecture = "arm64",
    [string]$Version = "1.25.1",
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
    if (-not $resolvedPath.StartsWith($resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
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

function Invoke-CmdChain {
    param([string[]]$Commands)

    $fullCmd = ($Commands | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }) -join " && "
    & $env:ComSpec /d /c $fullCmd
    if ($LASTEXITCODE -ne 0) {
        throw "Command chain failed with exit code $LASTEXITCODE."
    }
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

Remove-WorkspaceDirectory -Path $buildDir -RepoRoot $repoRoot
Remove-WorkspaceDirectory -Path $outputRootFull -RepoRoot $repoRoot
New-Item -ItemType Directory -Path $outputRootFull -Force | Out-Null

$vsTargetArch = if ([string]::IsNullOrWhiteSpace($env:OPENQ4_VS_TARGET_ARCH)) { $Architecture } else { $env:OPENQ4_VS_TARGET_ARCH.Trim().ToLowerInvariant() }
$vsHostArch = if ([string]::IsNullOrWhiteSpace($env:OPENQ4_VS_HOST_ARCH)) { Get-ProcessArch } else { $env:OPENQ4_VS_HOST_ARCH.Trim().ToLowerInvariant() }
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

Invoke-CmdChain @(
    'call "' + $vsDevCmd + '" -arch=' + $vsTargetArch + ' -host_arch=' + $vsHostArch + ' >nul',
    (($cmakeConfigure | ForEach-Object { Quote-CmdArg $_ }) -join " "),
    (($cmakeBuild | ForEach-Object { Quote-CmdArg $_ }) -join " "),
    (($cmakeInstall | ForEach-Object { Quote-CmdArg $_ }) -join " ")
)

$expectedRuntime = Join-Path $outputRootFull "bin\OpenAL32.dll"
$expectedImportLib = Join-Path $outputRootFull "lib\OpenAL32.lib"

if (-not (Test-Path $expectedRuntime)) {
    throw "OpenAL Soft runtime was not installed: '$expectedRuntime'"
}
if (-not (Test-Path $expectedImportLib)) {
    throw "OpenAL Soft import library was not installed: '$expectedImportLib'"
}

Write-Host "Prepared OpenAL Soft for Windows $Architecture"
Write-Host "Source archive: $downloadPath"
Write-Host "Source directory: $sourceDir"
Write-Host "Build directory: $buildDir"
Write-Host "Install root: $outputRootFull"
