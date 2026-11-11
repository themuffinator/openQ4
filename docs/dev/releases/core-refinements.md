# Core runtime refinements — pending release notes

> **Detailed change record.** Everything recorded here up to 11 November 2026 shipped in [0.14.1](v0.14.1.md), whose notes are the player-facing summary. Later additions are unreleased development notes. See [release status](../../user/release-status.md).

## Highlights

- **More reliable AI tactical searches.** Nearby areas are considered in the
  correct order, larger navigation meshes are handled safely, and repeated
  searches reuse more of their working memory.
- **More consistent scripted motion and transitions.** Changes to animation
  endpoints and timing take effect immediately. Instant transitions and samples
  before a motion's start no longer reuse values from an earlier motion.
- **Safer multiplayer message handling.** Truncated data and empty string
  buffers are handled consistently. Larger binary delta fields no longer
  depend on a small temporary buffer.
- Large lists require less repeated copying, and adding an item already in a
  list remains safe when the list grows or shifts its contents.
- Empty script commands no longer produce misleading unknown-command messages
  during startup.
- **Less memory over long map rotations.** Each multiplayer map load used to
  keep a fresh copy of the multiplayer menus that was never freed, so memory
  and loading time grew with every map. The menus are now reused. Interface
  windows also no longer reserve room for 4,096 script expressions each, which
  lowers memory on interface-heavy levels such as The Awakening's Trian Factory.
- **Autosaves no longer fail on an interface that was never drawn.** Saving
  could stop with an error when a level held a window that had not been drawn
  yet; such windows now start with valid positions.
- Material hit maps are freed by the engine that allocated them, which fixes
  negative renderer memory counters, and a material with a second
  `materialImage` line no longer leaks the first one.

## Upgrade Notes

- Replace the complete package so the engine, renderer modules and both game
  modules remain matched. Original Quake 4 assets are still required.
- These fixes do not introduce a new save format or network protocol version.
- The renderer interface is now version 25: renderer modules from an earlier
  build are refused.
- The separate runtime surface-size limitation affecting the large `hub2` map
  remains unresolved.
