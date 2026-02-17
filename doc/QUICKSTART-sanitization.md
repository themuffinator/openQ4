# Quick Start: Sanitize OpenQ4 Git History

⚡ **Fast track guide for removing SDK and BSE code from OpenQ4 repository**

## Current Situation

- OpenQ4 repository contains SDK game code (~295 files, ~6.56 MB)
- This code should be in OpenQ4-GameLibs (different license)
- Current repository has 3 commits - easy to fix!

## Quick Fix (5 Minutes)

### Step 1: Analyze
```powershell
cd OpenQ4
.\tools\sanitize-history.ps1 -Mode analyze
```

Review the output to confirm what will be removed.

### Step 2: Clean
```powershell
.\tools\sanitize-history.ps1 -Mode clean
```

Type `yes` when prompted. This will:
- Remove `src/game/` directory
- Keep only `.gitkeep` and `README.md` as placeholders
- Create a commit documenting the removal

### Step 3: Verify
```bash
# Should only show README.md and .gitkeep
ls src/game/

# Should show clean state
git status
```

### Step 4: Push
```bash
git push origin main
```

## Done! ✅

The repository is now license-compliant. Game code will be synced from OpenQ4-GameLibs during builds.

## What If I Need the Game Code?

The build system automatically handles this:

```powershell
# Build will sync game code from ../OpenQ4-GameLibs
.\tools\build\meson_setup.ps1 compile -C builddir
```

The synced files are in `.gitignore` and won't be committed.

## Full Documentation

For detailed information, see:
- [Git History Sanitization Guide](git-history-sanitization-guide.md)
- [License Compliance Checklist](license-compliance-checklist.md)

## Questions?

Open an issue on GitHub or review the [AGENTS.md](../AGENTS.md) file for project rules.
