// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
// Internal parser bridge. JsonCpp never enters the public editor API.
#include "Document.h"
namespace Json { class Value; }
namespace openq4::ui::detail {
bool ParseDocumentSource(const std::string&, Json::Value&, std::vector<Diagnostic>&);
const Json::Value* ResolveDocumentSource(const Json::Value&, const std::string&);
}
