# License Compliance Checklist for OpenQ4

This checklist helps ensure the OpenQ4 repository maintains proper license separation between GPLv3 engine code, SDK-licensed game code, and closed-source BSE code.

## Pre-Sanitization Verification

- [ ] **Understand the issue**
  - [ ] OpenQ4 is GPLv3 licensed
  - [ ] SDK game code is under Quake 4 SDK EULA (incompatible with GPLv3)
  - [ ] BSE is closed-source proprietary code
  - [ ] Mixing these in one repository creates license violations

- [ ] **Identify affected code**
  - [ ] Game code in `src/game/` (~295 files, ~6.56 MB)
  - [ ] Any BSE implementation in `src/bse/` (currently none)
  - [ ] Commits in git history containing these files

## Repository Setup

- [ ] **Set up companion repositories**
  - [ ] Clone/create OpenQ4-GameLibs at `../OpenQ4-GameLibs`
  - [ ] Clone/access OpenQ4-BSE at `../OpenQ4-BSE` (if available)
  - [ ] Verify both repositories are accessible

- [ ] **Backup current state**
  ```bash
  git branch backup-pre-sanitize
  git push origin backup-pre-sanitize
  ```

## Sanitization Process

### Option A: Clean Removal (Recommended)

For repositories with minimal history (like current state with 3 commits):

- [ ] **Run analysis**
  ```powershell
  .\tools\sanitize-history.ps1 -Mode analyze
  ```

- [ ] **Review what will be removed**
  - [ ] Verify the file list is correct
  - [ ] Check that only SDK/BSE code will be removed
  - [ ] Ensure engine code won't be affected

- [ ] **Perform clean removal**
  ```powershell
  .\tools\sanitize-history.ps1 -Mode clean
  ```

- [ ] **Verify removal**
  ```bash
  # Verify game code is removed
  ls src/game/
  # Should only show .gitkeep and README.md
  
  # Check git status
  git status
  # Should show clean working tree
  ```

### Option B: History Rewrite (For Extensive History)

For repositories with many commits containing SDK code:

- [ ] **Install git-filter-repo**
  ```bash
  pip install git-filter-repo
  ```

- [ ] **Run history filter**
  ```powershell
  .\tools\sanitize-history.ps1 -Mode filter
  ```

- [ ] **Verify history is clean**
  ```bash
  git log --all --full-history -- src/game/
  # Should return empty
  ```

- [ ] **Force push (CAUTION)**
  ```bash
  git push origin --force --all
  ```

## Post-Sanitization Verification

- [ ] **Check repository state**
  ```bash
  # Verify no game code in current state
  find src/game -type f -name "*.cpp" -o -name "*.h"
  # Should return empty or only README.md
  
  # Verify no game code in history
  git log --all --full-history -- src/game/ | wc -l
  # Should be 0
  
  # Check repository size
  du -sh .git
  # Should be reduced
  ```

- [ ] **Verify .gitignore**
  ```bash
  cat .gitignore | grep game
  # Should show src/game/ is ignored
  ```

- [ ] **Test build system**
  - [ ] Ensure OpenQ4-GameLibs is in `../OpenQ4-GameLibs`
  - [ ] Run build sync:
    ```powershell
    .\tools\build\sync_gamelibs.ps1
    ```
  - [ ] Verify files are synced to `src/game/`
  - [ ] Verify synced files show as ignored in git:
    ```bash
    git status
    # src/game/*.cpp files should not appear
    ```

- [ ] **Test full build**
  ```powershell
  powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir
  ```

## Documentation Updates

- [ ] **Update README.md** ✅ (Already done)
  - [x] Add repository structure section
  - [x] Explain licensing separation
  - [x] Add sanitization guide reference

- [ ] **Update AGENTS.md** (If needed)
  - [ ] Verify rules about game code location
  - [ ] Update any references to in-tree game code

- [ ] **Create LICENSE notes** (If needed)
  - [ ] Document which code is under which license
  - [ ] Reference companion repositories

## Communication

- [ ] **Notify contributors**
  - [ ] Explain the change in a GitHub issue/discussion
  - [ ] Update any active PRs with guidance
  - [ ] Document the change in release notes

- [ ] **Update CI/CD** (If applicable)
  - [ ] Update build scripts to sync from OpenQ4-GameLibs
  - [ ] Ensure automated builds still work
  - [ ] Update deployment scripts

## Final Verification

- [ ] **License audit**
  - [ ] Scan for any remaining Raven Software copyrights:
    ```bash
    git grep -i "raven software" -- . ':!doc/' ':!src/game/README.md'
    ```
  - [ ] Verify all remaining code is GPLv3 compatible
  - [ ] Check that BSE API headers have appropriate notices

- [ ] **Security check**
  - [ ] No proprietary/closed-source code in history
  - [ ] No SDK code in history
  - [ ] No leaked credentials or sensitive data

- [ ] **Functionality test**
  - [ ] Build succeeds
  - [ ] Game launches
  - [ ] Single-player works
  - [ ] Multiplayer works
  - [ ] No new errors or warnings

## Sign-off

- [ ] **Repository owner approval**
  - [ ] Changes reviewed
  - [ ] License compliance verified
  - [ ] Documentation complete
  - [ ] Ready for public use

## Ongoing Maintenance

- [ ] **Prevent future violations**
  - [x] `.gitignore` configured to exclude `src/game/`
  - [x] `src/game/README.md` explains the structure
  - [ ] Pre-commit hooks (optional)
  - [ ] Regular audits

- [ ] **Developer education**
  - [ ] Document the process in onboarding materials
  - [ ] Explain why the separation is necessary
  - [ ] Provide clear examples of what goes where

## Resources

- [Git History Sanitization Guide](git-history-sanitization-guide.md)
- [Sanitization Script](../tools/sanitize-history.ps1)
- [OpenQ4-GameLibs Repository](https://github.com/themuffinator/OpenQ4-GameLibs)
- [GPLv3 License](../LICENSE)
- [Quake 4 SDK EULA](https://github.com/themuffinator/OpenQ4-GameLibs/blob/main/doc/legacy/EULA.Development%20Kit.rtf)

## Notes

- **Timeframe**: For a repository with 3 commits, this process should take 1-2 hours
- **Backup**: Always maintain a backup branch before sanitization
- **Communication**: Inform all contributors before force-pushing history changes
- **Testing**: Thoroughly test the build system after sanitization

---

**Last Updated**: 2026-02-17  
**Script Version**: 1.0  
**Repository Commits**: 3
