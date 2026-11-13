# Map entity strings

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

Map entity strings let you change what spawns in a stock Quake 4 map (its
lights, spawn points, items, triggers, scripts and other entities) without
editing, recompiling or redistributing the map itself. Use them for mods,
server packages, compatibility fixes and alternate gameplay layouts.

## Quick start

Put a companion file next to the map's path, with the same name:

- `maps/mp/q4dm1.ent` replaces every entity in `maps/mp/q4dm1.map`.
- `maps/mp/q4dm1.entx` adds entities to it.

You can use either file or both. They are found through the normal game search
path, so they can be loose files or sit inside a PK4 (a Quake 4 package, which
is a renamed zip file). When several packages carry the same file, the usual
package priority decides which one is used.

To add a spawn point, a light or an item, use an `.entx`. To move, remove or
change existing entities, use an `.ent`.

## How the two files combine

`.ent` is a complete replacement for the map's entity list. It must contain worldspawn (the entity that holds map-wide settings) and every other entity that should still spawn.

`.entx` appends entities to whichever list is active: the map's own entities when there is no `.ent`, or the `.ent` replacement when both exist.

When a map loads, openQ4:

1. Loads the original `.map` or `.reg`.
2. Builds the map's collision from it.
3. Swaps in the `.ent` entity list, if there is one.
4. Adds the entities from the `.entx`, if there is one.
5. Spawns the result as normal.

Because the swap happens after the map's geometry and collision are built,
these files cannot add, remove or reshape brushes and patches. They change
entities only.

## File format

Both files are plain-text entity lists in the usual map format. An optional
`Version 1` header is accepted, `//` comments are allowed, and you should put
quotes around every key and value.

An `.entx` that adds a spawn point:

```text
Version 1

{
    "classname" "info_player_deathmatch"
    "name" "custom_spawn_north"
    "origin" "128 256 64"
    "angle" "90"
}
```

A complete `.ent` replacement:

```text
Version 1

{
    "classname" "worldspawn"
    "name" "world"
    "gravity" "1066"
}

{
    "classname" "info_player_start"
    "name" "replacement_player_start"
    "origin" "128 256 64"
    "angle" "90"
}
```

That replacement is deliberately tiny. A real one must keep every light,
script, trigger, mover, spawn point and other entity the map and game mode
need, so start from a copy of the map's full entity list.

## Rules and limits

Both files are checked in full before either is used. If one is malformed, the
map stops loading with a warning that names the problem, rather than spawning a
half-changed entity list.

- A `.ent` must not be empty, must begin with exactly one worldspawn, and must contain no other worldspawn.
- An `.entx` must not contain worldspawn.
- Every entity needs a `classname`.
- A key may appear only once per entity (ignoring upper and lower case).
- Entity names must be unique within a file, ignoring case. A name in an
  `.entx` must also not match an entity already in the active list.
- `spawn_entnum` is reserved for the game; you cannot set it.
- Only key/value entities are accepted. Brush and patch blocks are rejected.
- Each file is limited to 16 MiB, 4,096 entities, and 4,096 keys per entity.
- The final entity list must fit the game's entity table: at most 4,062
  entities on desktop builds. Leave room for entities created during play.

The files may start with a UTF-8 byte-order mark. openQ4 notices when you add,
remove or change either file, so reloading the map picks up your edits.

## Editing tools and the original map

The feature does not rewrite the original `.map`. Entities added this way are
never written out when a map is saved or exported, and the level editor refuses
to save a map while a complete `.ent` replacement is active. Map tools such as
dmap compile the original map source and ignore these files.

## Multiplayer

Ship the same entity-string files to the server and every client, ideally in
one PK4 for each map. They are map content, not a server setting, and the
server does not send them to clients, so every player needs the same files.
