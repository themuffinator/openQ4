// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <string>

// Resolves the one exact unified save root and creates its parent directory.
// Both outputs are unchanged on validation failure; acquisition/durable writes
// still check that the directory was actually created and is usable.
bool Common_SettingsPersistencePaths(std::string& journal, std::string& lock, std::string& error);

// Engine-private configuration commit, with no idCommon/Game API change.
// A true coordinatorOwnsLock asserts ownership of the exact
// fs_savepath/baseoq4/.settings-recovery.lock lease for the entire journal
// lifecycle. Only that caller may persist a coordinator-qualified pending batch.
// Ordinary callers acquire the lease themselves and refuse any recovery journal.
// Success means complete checked serialization and acknowledged durable replace;
// failure leaves archive dirty and may follow an already-visible rename.
bool Common_WriteSettingsConfiguration(bool coordinatorOwnsLock, std::string& error);
