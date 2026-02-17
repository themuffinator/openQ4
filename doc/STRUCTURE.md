# OpenQ4 Repository Structure

## Before Sanitization (Current State - INCORRECT)

```
OpenQ4/  (GPLv3)
├── src/
│   ├── game/           ❌ SDK CODE (Quake 4 EULA) - LICENSE VIOLATION!
│   │   ├── ai/         ❌ 295 files, 6.56 MB
│   │   ├── physics/    ❌ Should NOT be here
│   │   └── ...         ❌ Should be in OpenQ4-GameLibs
│   ├── bse_api/        ✅ OK (API headers only)
│   ├── framework/      ✅ OK (Engine code)
│   └── renderer/       ✅ OK (Engine code)
└── ...
```

**Problem:** Mixing GPLv3 and SDK EULA code = License violation!

## After Sanitization (Correct State - TARGET)

```
OpenQ4/  (GPLv3 only)
├── src/
│   ├── game/           ✅ Empty (synced during build)
│   │   ├── .gitkeep    ✅ Placeholder
│   │   └── README.md   ✅ Explains the structure
│   ├── bse_api/        ✅ API headers only
│   ├── framework/      ✅ Engine code
│   └── renderer/       ✅ Engine code
└── ...
```

**Solution:** Clean separation of licenses!

## Complete Project Structure (3 Repositories)

```
Workspace/
├── OpenQ4/                    (GPLv3)
│   ├── src/
│   │   ├── game/              ← Empty (synced at build time)
│   │   ├── bse_api/           ← API only
│   │   ├── framework/         ← Engine
│   │   └── renderer/          ← Engine
│   └── tools/
│       └── sanitize-history.ps1
│
├── OpenQ4-GameLibs/           (Quake 4 SDK EULA)
│   ├── src/
│   │   └── game/              ← SDK game code lives here
│   │       ├── ai/            ← 295 files
│   │       ├── physics/
│   │       └── ...
│   └── doc/
│       └── legacy/EULA.Development Kit.rtf
│
└── OpenQ4-BSE/                (Closed-source)
    └── src/
        └── bse/               ← BSE implementation
```

## Build-Time Integration

```
┌─────────────────────────────────────────────────────┐
│  Build Process                                      │
├─────────────────────────────────────────────────────┤
│                                                     │
│  1. Sync Game Code                                 │
│     ../OpenQ4-GameLibs/src/game/                   │
│            ↓ copy                                  │
│     OpenQ4/src/game/  (temporary, not committed)   │
│                                                     │
│  2. Build BSE                                      │
│     ../OpenQ4-BSE/src/bse/                         │
│            ↓ compile                               │
│     OpenQ4/builddir/libbse-q4.dll                  │
│                                                     │
│  3. Build Game Modules                             │
│     OpenQ4/src/game/  (synced)                     │
│            ↓ compile                               │
│     OpenQ4/builddir/openbase/game_sp.dll           │
│     OpenQ4/builddir/openbase/game_mp.dll           │
│                                                     │
│  4. Build Engine                                   │
│     OpenQ4/src/framework/                          │
│            ↓ compile                               │
│     OpenQ4/builddir/OpenQ4.exe                     │
│                                                     │
└─────────────────────────────────────────────────────┘
```

## License Boundaries

```
┌───────────────────┬──────────────────┬─────────────────┐
│   OpenQ4          │  OpenQ4-GameLibs │  OpenQ4-BSE     │
├───────────────────┼──────────────────┼─────────────────┤
│  GPLv3            │  Quake 4 SDK     │  Closed Source  │
│                   │  EULA            │                 │
├───────────────────┼──────────────────┼─────────────────┤
│  ✅ Engine code   │  ✅ Game code    │  ✅ BSE impl.   │
│  ✅ Build tools   │  ✅ AI, physics  │  ✅ Effects     │
│  ✅ BSE API       │  ✅ Weapons      │  ❌ No public   │
│  ❌ Game code     │  ❌ Engine code  │     access      │
│  ❌ BSE impl.     │  ❌ BSE code     │                 │
└───────────────────┴──────────────────┴─────────────────┘
```

## Git Ignore Protection

```gitignore
# .gitignore

# SDK Game Code (synchronized from OpenQ4-GameLibs during build)
# Do NOT commit this directory
src/game/
!src/game/.gitkeep
!src/game/README.md

# BSE Implementation (built from OpenQ4-BSE during build)
src/bse/
!src/bse_api/
```

**Result:** Game code can exist during build but won't be committed to git!

## Sanitization Workflow

```
┌─────────────────────────────────────────────────────┐
│  Current State: OpenQ4 contains SDK code            │
│  Problem: License violation                         │
└─────────────────┬───────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────┐
│  Step 1: Analyze                                    │
│  $ .\tools\sanitize-history.ps1 -Mode analyze       │
│  Output: Shows 295 files, 6.56 MB to remove         │
└─────────────────┬───────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────┐
│  Step 2: Clean                                      │
│  $ .\tools\sanitize-history.ps1 -Mode clean         │
│  Action: Removes src/game/, creates commit          │
└─────────────────┬───────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────┐
│  Step 3: Verify                                     │
│  $ ls src/game/                                     │
│  Output: Only .gitkeep and README.md                │
└─────────────────┬───────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────┐
│  Final State: OpenQ4 is license-compliant           │
│  ✅ No SDK code in repository                       │
│  ✅ No SDK code in git history                      │
│  ✅ Build system syncs from OpenQ4-GameLibs         │
└─────────────────────────────────────────────────────┘
```

## Developer Workflow

### Modifying Engine Code (OpenQ4)
```bash
cd OpenQ4
# Edit engine code in src/framework/, src/renderer/, etc.
git add src/framework/some_file.cpp
git commit -m "Fix engine bug"
git push
```

### Modifying Game Code (OpenQ4-GameLibs)
```bash
cd OpenQ4-GameLibs
# Edit game code in src/game/
git add src/game/ai/AI.cpp
git commit -m "Fix AI behavior"
git push

# Then in OpenQ4, sync will happen automatically during build
cd ../OpenQ4
.\tools\build\meson_setup.ps1 compile -C builddir
```

### Modifying BSE (OpenQ4-BSE - if you have access)
```bash
cd OpenQ4-BSE
# Edit BSE implementation
git add src/bse/effects.cpp
git commit -m "Improve particle effects"
# (Push to private repository)

# Then in OpenQ4, BSE will build automatically
cd ../OpenQ4
.\tools\build\meson_setup.ps1 compile -C builddir
```

## Key Points

1. **Never commit to `src/game/` in OpenQ4** - it's synced from OpenQ4-GameLibs
2. **Never commit BSE implementation to OpenQ4** - only API headers belong here
3. **Each repository has one license** - no mixing
4. **Build system handles integration** - no manual copying needed
5. **`.gitignore` protects you** - prevents accidental commits

## Documentation Files

- **[git-history-sanitization-guide.md](git-history-sanitization-guide.md)** - Complete guide
- **[license-compliance-checklist.md](license-compliance-checklist.md)** - Step-by-step
- **[QUICKSTART-sanitization.md](QUICKSTART-sanitization.md)** - 5-minute guide
- **[SUMMARY-sanitization.md](SUMMARY-sanitization.md)** - Overview
- **[STRUCTURE.md](STRUCTURE.md)** - This file (visual diagrams)

---

**Remember:** The goal is to maintain clear license boundaries while keeping the build system seamless!
