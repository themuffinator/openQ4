// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "Document.h"
#include <charconv>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace openq4::ui {
namespace {
size_t Components(PresentationType type) {
	switch (type) {
	case PresentationType::Number:
	case PresentationType::Boolean: return 1;
	case PresentationType::Vector2: return 2;
	case PresentationType::Vector3: return 3;
	case PresentationType::Vector4: return 4;
	default: return 0;
	}
}
bool Space(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}
bool AliasPart(std::string_view part) {
	if (part.empty() || part.size() > 128) return false;
	for (unsigned char c : part) {
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
	}
	return true;
}
bool Number(std::string_view token, double& value) {
	// Floating from_chars is independent of the process locale. Its grammar
	// deliberately excludes a leading '+', which the public numeric API accepts.
	if (!token.empty() && token.front() == '+') {
		token.remove_prefix(1);
		if (!token.empty() && (token.front() == '+' || token.front() == '-')) return false;
	}
	if (token.empty()) return false;
	const auto result = std::from_chars(token.data(),token.data()+token.size(),value,std::chars_format::general);
	return result.ec == std::errc{} && result.ptr == token.data()+token.size() &&
		std::isfinite(value) && std::abs(value) <= 1000000000000.0;
}
std::string NumberText(double value) {
	char buffer[64];
	const auto result = std::to_chars(buffer,buffer+sizeof(buffer),value,
		std::chars_format::general,std::numeric_limits<double>::max_digits10);
	return result.ec == std::errc{} ? std::string(buffer,result.ptr) : std::string{};
}
}

bool ValidPresentationValue(const PresentationValue& value) {
	const size_t count = Components(value.type);
	if (value.type == PresentationType::String) {
		if (!ValidStateValue(StateValue(value.text))) return false;
	} else if (count == 0 || !value.text.empty()) return false;
	for (size_t i = 0; i < value.data.size(); ++i) {
		if (!std::isfinite(value.data[i]) || std::abs(value.data[i]) > 1000000000000.0) return false;
		if (i >= count && value.data[i] != 0) return false;
	}
	return value.type != PresentationType::Boolean || value.data[0] == 0 || value.data[0] == 1;
}

std::string PresentationAliasKey(const std::string& alias) {
	const auto separator = alias.find("::");
	if (separator == std::string::npos) {
		if (!AliasPart(alias)) return {};
	} else {
		if (!AliasPart(std::string_view(alias).substr(0,separator)) ||
			!AliasPart(std::string_view(alias).substr(separator+2))) return {};
	}
	std::string key = alias;
	for (char& c : key) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c-'A'+'a');
	// Dictionary variables have their own state API and must never be allocated
	// as a side effect of looking up a presentation value.
	if (separator != std::string::npos && key.compare(0,separator,"gui") == 0) return {};
	return key;
}

bool ParsePresentationValue(PresentationType type, const std::string& text,
	PresentationValue& value, std::string& error) {
	PresentationValue candidate;
	candidate.type = type;
	if (type == PresentationType::String) {
		candidate.text = text;
	} else {
		const size_t count = Components(type);
		if (count == 0) { error = "Unknown presentation value type"; return false; }
		std::string_view source(text);
		while (!source.empty() && Space(source.front())) source.remove_prefix(1);
		while (!source.empty() && Space(source.back())) source.remove_suffix(1);
		if (type == PresentationType::Boolean && (source == "true" || source == "false")) {
			candidate.data[0] = source == "true" ? 1 : 0;
		} else {
			const bool commaSeparated = source.find(',') != std::string_view::npos;
			for (size_t i = 0; i < count; ++i) {
				const auto end = source.find_first_of(", \t\r\n\f\v");
				const auto token = source.substr(0,end);
				if (!Number(token,candidate.data[i])) {
					error = "Expected a finite presentation number within +/-1e12"; return false;
				}
				source.remove_prefix(token.size());
				while (!source.empty() && Space(source.front())) source.remove_prefix(1);
				if (i+1 < count && commaSeparated) {
					if (source.empty() || source.front() != ',') {
						error = "Expected a comma between presentation components"; return false;
					}
					source.remove_prefix(1);
					while (!source.empty() && Space(source.front())) source.remove_prefix(1);
				}
			}
			if (!source.empty()) { error = "Unexpected trailing presentation value data"; return false; }
		}
	}
	if (!ValidPresentationValue(candidate)) { error = "Invalid presentation value"; return false; }
	value = std::move(candidate);
	error.clear();
	return true;
}

std::string FormatPresentationValue(const PresentationValue& value) {
	if (!ValidPresentationValue(value)) return {};
	if (value.type == PresentationType::String) return value.text;
	if (value.type == PresentationType::Boolean) return value.data[0] == 0 ? "0" : "1";
	std::string result;
	for (size_t i = 0; i < Components(value.type); ++i) {
		if (i != 0) result += ' ';
		result += NumberText(value.data[i]);
	}
	return result;
}
}
