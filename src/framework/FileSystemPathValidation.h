// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

// Engine-private portable qpath validation shared by VFS mutations and exact
// durable configuration writes. This is not part of idFileSystem's public ABI.
bool FS_ValidateRelativeWritePath(const char* relativePath, const char** reason);
