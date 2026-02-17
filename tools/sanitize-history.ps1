#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Sanitizes OpenQ4 git history to remove SDK and BSE code.

.DESCRIPTION
    This script helps remove SDK-licensed game code and BSE implementation
    from the OpenQ4 repository to comply with licensing requirements.
    
    The script provides multiple approaches:
    1. Clean removal (recommended for new repos)
    2. History filtering (for repos with extensive history)

.PARAMETER Mode
    The sanitization mode to use:
    - 'clean': Remove game code and commit (recommended)
    - 'filter': Use git-filter-repo to rewrite history
    - 'analyze': Analyze what would be removed (dry-run)

.PARAMETER BackupBranch
    Name of backup branch to create before sanitization (default: backup-pre-sanitize)

.EXAMPLE
    .\tools\sanitize-history.ps1 -Mode analyze
    Analyzes what would be removed without making changes

.EXAMPLE
    .\tools\sanitize-history.ps1 -Mode clean
    Removes SDK game code from the repository

.EXAMPLE
    .\tools\sanitize-history.ps1 -Mode filter
    Rewrites git history to remove all traces of game code
#>

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet('analyze', 'clean', 'filter')]
    [string]$Mode = 'analyze',
    
    [Parameter(Mandatory=$false)]
    [string]$BackupBranch = 'backup-pre-sanitize',
    
    [Parameter(Mandatory=$false)]
    [switch]$SkipBackup
)

$ErrorActionPreference = 'Stop'

# Color output helpers
function Write-Info { param($msg) Write-Host "ℹ️  $msg" -ForegroundColor Cyan }
function Write-Success { param($msg) Write-Host "✅ $msg" -ForegroundColor Green }
function Write-Warning { param($msg) Write-Host "⚠️  $msg" -ForegroundColor Yellow }
function Write-Error { param($msg) Write-Host "❌ $msg" -ForegroundColor Red }

# Ensure we're in the repository root
$repoRoot = git rev-parse --show-toplevel 2>$null
if (-not $repoRoot) {
    Write-Error "Not in a git repository"
    exit 1
}

Set-Location $repoRoot

Write-Info "OpenQ4 Git History Sanitization Tool"
Write-Info "======================================"
Write-Host ""

# Check current state
Write-Info "Checking repository state..."
$gameCodeExists = Test-Path "src/game"
$bseImplExists = Test-Path "src/bse"  # Not just src/bse_api
$commitCount = (git rev-list --all --count)

Write-Host "  Repository commits: $commitCount"
Write-Host "  Game code present: $gameCodeExists"
Write-Host "  BSE impl present: $bseImplExists"
Write-Host ""

# Analyze mode
if ($Mode -eq 'analyze') {
    Write-Info "Analysis Mode - Checking what would be removed"
    Write-Host ""
    
    if ($gameCodeExists) {
        Write-Warning "SDK Game Code (src/game/)"
        $gameFileCount = (Get-ChildItem -Recurse -File src/game | Measure-Object).Count
        $gameSize = (Get-ChildItem -Recurse -File src/game | Measure-Object -Property Length -Sum).Sum
        $gameSizeMB = [math]::Round($gameSize / 1MB, 2)
        
        Write-Host "  Files: $gameFileCount"
        Write-Host "  Size: $gameSizeMB MB"
        Write-Host "  Reason: SDK code should be in OpenQ4-GameLibs (Quake 4 SDK EULA)"
        Write-Host ""
        
        # Show some example files
        Write-Host "  Example files:"
        Get-ChildItem -Recurse -File src/game -Include *.cpp,*.h | 
            Select-Object -First 10 | 
            ForEach-Object { Write-Host "    - $($_.FullName.Replace($repoRoot, '.'))" }
        Write-Host ""
    }
    
    if ($bseImplExists) {
        Write-Warning "BSE Implementation (src/bse/)"
        Write-Host "  Reason: BSE implementation should be in OpenQ4-BSE (closed-source)"
        Write-Host ""
    }
    
    # Check git history
    Write-Info "Checking git history for removed files..."
    $gameInHistory = git log --all --full-history --format="%H" -- "src/game/" | Measure-Object -Line
    if ($gameInHistory.Lines -gt 0) {
        Write-Warning "Found $($gameInHistory.Lines) commits touching src/game/ in history"
        Write-Host "  These would be rewritten with 'filter' mode"
        Write-Host ""
    }
    
    Write-Info "Analysis complete"
    Write-Host ""
    Write-Info "Next steps:"
    Write-Host "  1. Run with -Mode clean to remove files from current state"
    Write-Host "  2. Run with -Mode filter to rewrite git history (if needed)"
    Write-Host "  3. See doc/git-history-sanitization-guide.md for full documentation"
    
    exit 0
}

# Create backup unless skipped
if (-not $SkipBackup) {
    Write-Info "Creating backup branch: $BackupBranch"
    git branch -f $BackupBranch
    Write-Success "Backup created at branch '$BackupBranch'"
    Write-Host ""
}

# Clean mode - remove files from current state
if ($Mode -eq 'clean') {
    Write-Info "Clean Mode - Removing SDK/BSE code from current state"
    Write-Host ""
    
    if (-not $gameCodeExists -and -not $bseImplExists) {
        Write-Success "No SDK or BSE implementation code found - repository is already clean!"
        exit 0
    }
    
    Write-Warning "This will:"
    if ($gameCodeExists) { Write-Host "  - Remove src/game/ directory" }
    if ($bseImplExists) { Write-Host "  - Remove src/bse/ implementation directory" }
    Write-Host "  - Create a commit with these removals"
    Write-Host ""
    
    $response = Read-Host "Continue? (yes/no)"
    if ($response -ne 'yes') {
        Write-Info "Cancelled by user"
        exit 0
    }
    
    # Remove game code
    if ($gameCodeExists) {
        Write-Info "Removing src/game/..."
        git rm -r src/game
        
        # Create a .gitkeep to preserve the directory structure in .gitignore
        New-Item -ItemType Directory -Force -Path "src/game" | Out-Null
        New-Item -ItemType File -Force -Path "src/game/.gitkeep" | Out-Null
        git add src/game/.gitkeep
        
        Write-Success "Removed src/game/"
    }
    
    # Remove BSE implementation (if exists)
    if ($bseImplExists) {
        Write-Info "Removing src/bse/ implementation..."
        git rm -r src/bse
        Write-Success "Removed src/bse/"
    }
    
    # Commit the changes
    $commitMsg = @"
Remove SDK and BSE code to comply with licensing

- Removed src/game/ (SDK code - now in OpenQ4-GameLibs)
- Game code is synchronized during build from companion repository
- Only BSE API headers remain in src/bse_api/

See doc/git-history-sanitization-guide.md for details.
"@
    
    git commit -m $commitMsg
    Write-Success "Changes committed"
    Write-Host ""
    
    Write-Info "Next steps:"
    Write-Host "  1. Update .gitignore to prevent re-adding game code"
    Write-Host "  2. Ensure companion repositories are set up:"
    Write-Host "     - ../OpenQ4-GameLibs (for game code)"
    Write-Host "     - ../OpenQ4-BSE (for BSE implementation)"
    Write-Host "  3. Test build with: tools/build/meson_setup.ps1 compile -C builddir"
    Write-Host ""
    Write-Success "Clean mode completed successfully!"
    
    exit 0
}

# Filter mode - rewrite git history
if ($Mode -eq 'filter') {
    Write-Info "Filter Mode - Rewriting git history"
    Write-Host ""
    
    # Check if git-filter-repo is available
    $hasFilterRepo = $null -ne (Get-Command git-filter-repo -ErrorAction SilentlyContinue)
    
    if (-not $hasFilterRepo) {
        Write-Error "git-filter-repo not found"
        Write-Host ""
        Write-Info "To install git-filter-repo:"
        Write-Host "  pip install git-filter-repo"
        Write-Host ""
        Write-Host "Or download from: https://github.com/newren/git-filter-repo"
        exit 1
    }
    
    Write-Warning "⚠️  WARNING: This will rewrite git history!"
    Write-Host ""
    Write-Host "This operation will:"
    Write-Host "  - Permanently remove src/game/ from all commits"
    Write-Host "  - Permanently remove src/bse/ from all commits (if exists)"
    Write-Host "  - Require force-push to update remote repository"
    Write-Host "  - Break any existing clones/forks"
    Write-Host ""
    Write-Host "Backup branch created: $BackupBranch"
    Write-Host ""
    
    $response = Read-Host "Type 'rewrite-history' to continue"
    if ($response -ne 'rewrite-history') {
        Write-Info "Cancelled by user"
        exit 0
    }
    
    Write-Info "Rewriting history to remove src/game/..."
    git filter-repo --path src/game --invert-paths --force
    
    if ($bseImplExists) {
        Write-Info "Rewriting history to remove src/bse/..."
        git filter-repo --path src/bse --invert-paths --force
    }
    
    Write-Success "History rewritten"
    Write-Host ""
    
    Write-Info "Cleaning up..."
    git reflog expire --expire=now --all
    git gc --prune=now --aggressive
    Write-Success "Cleanup complete"
    Write-Host ""
    
    Write-Warning "To update the remote repository, run:"
    Write-Host "  git push origin --force --all"
    Write-Host ""
    Write-Warning "⚠️  This will affect all users of the repository!"
    Write-Host ""
    Write-Success "Filter mode completed successfully!"
    
    exit 0
}

Write-Error "Unknown mode: $Mode"
exit 1
