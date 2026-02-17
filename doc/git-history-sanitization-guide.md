# Git History Sanitization Guide

## Overview

This guide explains how to sanitize the OpenQ4 git history to remove SDK and BSE code that should not be in this repository due to licensing differences.

## Background

The OpenQ4 project consists of three repositories with different licenses:

1. **OpenQ4** (GPLv3) - Main engine code
2. **OpenQ4-GameLibs** (Quake 4 SDK EULA) - SDK-derived game code
3. **OpenQ4-BSE** (Closed-source) - BSE implementation

### Licensing Structure

- **OpenQ4 Engine**: GPLv3 license
  - Should contain: Engine code, framework, renderer, tools
  - Should NOT contain: SDK game code, BSE implementation

- **OpenQ4-GameLibs**: Quake 4 SDK EULA
  - Should contain: All game code from `src/game/`
  - Synchronized into OpenQ4 during builds via `tools/build/sync_gamelibs.ps1`

- **OpenQ4-BSE**: Closed-source
  - Should contain: BSE implementation code
  - OpenQ4 only contains BSE API headers in `src/bse_api/`

## Problem Statement

The current OpenQ4 repository contains:
- SDK-derived game code in `src/game/` (~6.56 MB, 295 files)
- This code is subject to id Software's EULA, not GPLv3
- Having this code in the git history creates a license violation

## Solution

### Option 1: Fresh Start (Recommended for New Repositories)

Since OpenQ4 currently has only 3 commits, the cleanest approach is:

1. **Create the companion repositories first**
   ```bash
   # Move game code to OpenQ4-GameLibs
   mkdir -p ../OpenQ4-GameLibs/src
   cp -r src/game ../OpenQ4-GameLibs/src/
   ```

2. **Create a new clean OpenQ4 repository**
   ```bash
   # In a new directory
   git init OpenQ4-Clean
   cd OpenQ4-Clean
   
   # Copy only engine code (exclude src/game/)
   # See detailed file list below
   ```

3. **Remove game code from OpenQ4**
   ```bash
   git rm -r src/game
   git commit -m "Remove SDK game code (moved to OpenQ4-GameLibs)"
   ```

### Option 2: Filter Existing History (For Repositories with Many Commits)

If the repository had extensive history, use `git-filter-repo`:

```bash
# Install git-filter-repo
pip install git-filter-repo

# Remove src/game directory from all history
git filter-repo --path src/game --invert-paths

# Remove any BSE implementation files if present
git filter-repo --path src/bse --invert-paths

# Force push to update remote
git push origin --force --all
```

### Option 3: BFG Repo-Cleaner (Alternative)

```bash
# Install BFG Repo-Cleaner
# Download from https://rtyley.github.io/bfg-repo-cleaner/

# Remove folders from history
java -jar bfg.jar --delete-folders src/game
java -jar bfg.jar --delete-folders src/bse

# Clean up
git reflog expire --expire=now --all
git gc --prune=now --aggressive

# Force push
git push origin --force --all
```

## Files to Keep in OpenQ4

### Engine Code (Keep)
- `src/aas/` - AAS (Area Awareness System)
- `src/bse_api/` - BSE API headers only (NOT implementation)
- `src/cm/` - Collision model
- `src/framework/` - Core framework
- `src/idlib/` - ID library
- `src/MayaImport/` - Maya importer
- `src/renderer/` - Rendering engine
- `src/sound/` - Sound system
- `src/sys/` - System interface
- `src/tools/` - Development tools
- `src/ui/` - User interface
- `src/external/` - Third-party libraries

### Game Code (Remove - Move to OpenQ4-GameLibs)
- `src/game/` - All SDK-derived game code
  - This includes: AI, physics, entities, weapons, etc.
  - ~293 files, ~7.2MB of code

### BSE Implementation (Remove - Move to OpenQ4-BSE)
- Any BSE implementation files (not just API headers)
- Keep only `src/bse_api/BSEInterface.h` and `src/bse_api/BSE_API.h`

## Build System Integration

After sanitization, the build system should:

1. **Sync game code during build**
   - `tools/build/sync_gamelibs.ps1` synchronizes from `../OpenQ4-GameLibs`
   - Game code is copied to `src/game/` temporarily during build
   - These files should be in `.gitignore`

2. **Build BSE from companion repo**
   - BSE is built from `../OpenQ4-BSE` (default location)
   - Can be overridden with `OPENQ4_BSE_REPO=<path>`

3. **Git ignore synchronized files**
   ```gitignore
   # In .gitignore
   src/game/
   !src/game/.gitkeep
   ```

## Verification Steps

After sanitization:

1. **Verify no SDK code in history**
   ```bash
   git log --all --full-history -- src/game/
   # Should return empty
   ```

2. **Verify repository size reduction**
   ```bash
   du -sh .git
   # Should be significantly smaller
   ```

3. **Check for any remaining license issues**
   ```bash
   # Search for Raven Software copyright in remaining files
   git grep -i "raven software" -- . ':!doc/'
   ```

4. **Ensure build still works**
   ```bash
   # Build should sync from OpenQ4-GameLibs automatically
   powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir
   ```

## Timeline for Sanitization

For the current OpenQ4 repository (3 commits):
- **Estimated time**: 1-2 hours
- **Recommended approach**: Option 1 (Fresh Start)
- **Risk**: Low (minimal history to preserve)

## Post-Sanitization Checklist

- [ ] Verify `src/game/` is removed from all commits
- [ ] Verify `src/bse/` implementation is removed (if present)
- [ ] Verify only BSE API headers remain in `src/bse_api/`
- [ ] Update `.gitignore` to prevent future accidents
- [ ] Update documentation to reflect new structure
- [ ] Test build process with companion repositories
- [ ] Update README.md licensing section
- [ ] Notify contributors of the change
- [ ] Update CI/CD if needed

## License Compliance Statement

After sanitization, the OpenQ4 repository should contain only:
- GPLv3-licensed engine code
- BSE API headers (interface only, not implementation)
- Build scripts that reference external repositories

All SDK-licensed game code should reside in OpenQ4-GameLibs.
All closed-source BSE code should reside in OpenQ4-BSE.

## References

- [git-filter-repo documentation](https://github.com/newren/git-filter-repo)
- [BFG Repo-Cleaner](https://rtyley.github.io/bfg-repo-cleaner/)
- [GitHub: Removing sensitive data](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/removing-sensitive-data-from-a-repository)
- [GPLv3 License](https://www.gnu.org/licenses/gpl-3.0.en.html)
- [Quake 4 SDK EULA](https://github.com/themuffinator/OpenQ4-GameLibs/blob/main/doc/legacy/EULA.Development%20Kit.rtf)

## Support

For questions or issues with git history sanitization:
- Open an issue on GitHub
- Contact: themuffinator
- Repository: https://github.com/themuffinator/OpenQ4
