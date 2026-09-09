// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

class idStr;

// Engine-private access to the registered reset value. Missing and private
// variables fail without changing output; this never resets a live CVar.
bool CVar_ReadDefault(const char* name, idStr& output);
