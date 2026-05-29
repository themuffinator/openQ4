param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('setup', 'compile', 'install')]
    [string]$Action
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$workspaceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$mesonWrapper = Join-Path $workspaceRoot 'tools\build\meson_setup.ps1'
$buildDir = Join-Path $workspaceRoot 'builddir'

if (-not (Test-Path $mesonWrapper)) {
    throw "openQ4 Meson wrapper not found at '$mesonWrapper'."
}

function Invoke-openQ4Meson([string[]]$MesonArgs) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $mesonWrapper @MesonArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

function Test-MesonBuildDirectory([string]$Path) {
    $coreData = Join-Path $Path 'meson-private\coredata.dat'
    $ninjaFile = Join-Path $Path 'build.ninja'
    return (Test-Path $coreData) -and (Test-Path $ninjaFile)
}

switch ($Action) {
    'setup' {
        if (Test-MesonBuildDirectory $buildDir) {
            Invoke-openQ4Meson @(
                'setup',
                $buildDir,
                $workspaceRoot,
                '--reconfigure',
                '--backend',
                'ninja',
                '--buildtype',
                'debug',
                '--wrap-mode=forcefallback'
            )
        } else {
            Invoke-openQ4Meson @(
                'setup',
                '--wipe',
                $buildDir,
                $workspaceRoot,
                '--backend',
                'ninja',
                '--buildtype',
                'debug',
                '--wrap-mode=forcefallback'
            )
        }
    }
    'compile' {
        Invoke-openQ4Meson @(
            'compile',
            '-C',
            $buildDir
        )
    }
    'install' {
        Invoke-openQ4Meson @(
            'install',
            '-C',
            $buildDir,
            '--no-rebuild',
            '--skip-subprojects'
        )
    }
}
