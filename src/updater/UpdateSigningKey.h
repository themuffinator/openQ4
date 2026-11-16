/*
===========================================================================

openQ4 update signing key
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

#ifndef __OPENQ4_UPDATE_SIGNING_KEY_H__
#define __OPENQ4_UPDATE_SIGNING_KEY_H__

// The Ed25519 public key that signs each release's openq4-update.json, as 64
// hex digits. `tools/build/update_manifest.py keygen` writes it here; the
// private half is the OPENQ4_UPDATE_SIGNING_KEY secret of the release
// workflow and is never committed.
//
// Empty means this build trusts no manifest signature: the update check still
// reports a newer release, but the game opens the release page instead of
// installing it in place.
#define OPENQ4_UPDATE_SIGNING_PUBLIC_KEY ""

#endif /* !__OPENQ4_UPDATE_SIGNING_KEY_H__ */
