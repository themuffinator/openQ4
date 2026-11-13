# Level-load cache (experimental)

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

The level-load cache is an experimental option that aims to make maps load
faster the second time. On the first visit, openQ4 notes which files the map
uses and saves ready-processed copies of its models, map geometry, collision
data and animations in your save folder. A later visit can read those files
ahead of time and skip work it has already done.

It is **off by default**: on the stock maps measured so far, it has not yet
proved reliably faster than normal loading. Try it if you are curious, and turn
it off again if loading is no quicker for you.

## Turning it on and off

Enter this in the console (it is a console variable, or cvar), then load a map:

```text
com_levelLoadModernization 1
```

- The first visit to each map loads from the game files as usual, and may take
  a little longer while the cache files are written.
- A later visit reuses them only when everything that affects loading still
  matches: the same map, game mode, installed packages and mods, and the same
  load-related settings. Otherwise openQ4 quietly builds fresh copies.

To go back to normal loading, enter `com_levelLoadModernization 0`. In that mode
no cache file is read or written, whatever the other settings below say.

## Safety and compatibility

Your installed Quake 4 and mod files always come first: openQ4 finds each
file through the normal game search rules before it uses a prepared copy, and
the normal loading code still checks and uses the result.

Every cache file is checked in full before it is used. A stale, truncated, corrupt, or mismatched cache is ignored, the bad file is removed where appropriate, and openQ4 loads the original instead.

Cache files are disposable. They do not replace any game assets and do not
affect joining pure servers (servers that require matching game files). Never
copy them into Quake 4's PK4 packages.

## Where the files are

The cache lives in openQ4's save folder (`fs_savepath`), inside the active game
folder, normally `<fs_savepath>/baseoq4/`:

```text
generated/manifests/       which files each map uses
generated/models/          processed models
generated/worlds/          processed map geometry
generated/collision/       processed collision data
generated/animations/      processed MD5 animations
```

So the animation cache, for example, is in `<fs_savepath>/baseoq4/generated/animations/`.

Most file names are codes rather than readable names; animation files keep the
animation's path and end in `.banim`. Animation cache files are portable: x64 and ARM64 builds use the same format. There is no need to copy cache files between installations; a different
set of packages or settings simply builds new ones.

## Settings

Defaults:

```text
com_levelLoadModernization 0
com_levelLoadCache 1
com_levelLoadCacheWrite 1
com_levelLoadPreload 1
com_levelLoadCacheReport 0
g_useGeneratedAnimCache 1
g_writeGeneratedAnimCache 1
```

`com_levelLoadModernization` is the master switch; the others only take effect
while it is `1`, so turning it on is all you need. Change them before loading a
map:

| Setting | What it does |
| --- | --- |
| `com_levelLoadModernization` | Master switch. `1` turns on every cache part that is set to `1` below; `0` is normal loading, even if older settings turned parts on. |
| `com_levelLoadCache` | `0` ignores the saved file lists and the model, map geometry and collision caches completely. |
| `com_levelLoadCacheWrite` | `0` stops new cache files being written; existing valid ones are still read. |
| `com_levelLoadPreload` | `0` stops openQ4 reading a map's files ahead of time, while still using valid model, geometry and collision caches. |
| `com_levelLoadCacheReport` | `1` writes per-map cache hits, misses, bytes and memory use to the log. |
| `g_useGeneratedAnimCache` | `0` ignores existing animation cache (`.banim`) files. |
| `g_writeGeneratedAnimCache` | `0` stops new animation cache files being written. |

Advanced limits on how much openQ4 reads ahead are also available:
`com_levelLoadPreloadMaxEntries`, `com_levelLoadPreloadMaxFileMB`,
`com_levelLoadPreloadMaxStagingMB`, `com_levelLoadPreloadMaxDecodeMB`,
`com_levelLoadPreloadReadChunkKB` and `com_levelLoadPreloadDecodeChunkKB`. The
defaults are deliberately modest and you should not need to change them;
raising them can increase memory use while a map loads.

## Troubleshooting

**Start the cache afresh.** Close openQ4 and delete one or more of the
`generated/` folders listed above. openQ4 rebuilds what it needs from the
installed game files.

**Never delete Quake 4's own files.** Only the `generated/` folders in your save
folder are cache. Leave the game's `.proc`, `.cm`, model, image, sound and
animation files alone.

**A map fails to load.** Set `com_levelLoadModernization 0` and try again. If
it still fails, the cache is not the cause. When you report the problem, include
your `fs_savepath` and a console log: start openQ4 with
`+set logFile 2 +set logFileName logs/openq4.log`, and the log is written to
`<fs_savepath>/baseoq4/logs/openq4.log`.

**Modders editing loose files.** openQ4 recognises a changed loose file by its
path, size and date. A tool that rewrites a file but keeps both its size and its
date can slip past that check, so delete the `generated/` folders or set
`com_levelLoadCache 0` while testing such edits. Files inside PK4s are tied to
the package's checksum and are not affected.

Developers: the cache's full design is in
[level-load cache modernization](../dev/loading-cache-modernization.md).
