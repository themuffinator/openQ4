// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "../retained/Document.h"
#include <bit>
#include <charconv>
#include <cstdint>
#include <limits>

namespace openq4::ui {
// Settings snapshots keep exact typed values regardless of renderer FTZ/DAZ.
// This is a comparison policy, not validation: callers still enforce their
// schema, finite/range/string budgets and host permissions before publication.
inline bool SettingsValueEqual(const StateValue& a, const StateValue& b) noexcept {
	if (a.valueless_by_exception() || b.valueless_by_exception() || a.index() != b.index()) return false;
	if (const double* number = std::get_if<double>(&a)) {
		static_assert(sizeof(double) == sizeof(std::uint64_t) && std::numeric_limits<double>::is_iec559);
		// Volatile integer observations prevent an optimizer from folding the
		// representation test back into floating equality (which obeys DAZ).
		const volatile std::uint64_t left = std::bit_cast<std::uint64_t>(*number);
		const volatile std::uint64_t right = std::bit_cast<std::uint64_t>(std::get<double>(b));
		const std::uint64_t leftMagnitude = left & 0x7fffffffffffffffULL;
		const std::uint64_t rightMagnitude = right & 0x7fffffffffffffffULL;
		if (leftMagnitude >= 0x7ff0000000000000ULL || rightMagnitude >= 0x7ff0000000000000ULL) return false;
		return left == right || (leftMagnitude == 0 && rightMagnitude == 0);
	}
	return a == b;
}
inline bool SettingsValuesEqual(const StateValues& a, const StateValues& b) noexcept {
	if (a.size() != b.size()) return false;
	auto left = a.begin(), right = b.begin();
	for (; left != a.end(); ++left, ++right)
		if (left->first != right->first || !SettingsValueEqual(left->second, right->second)) return false;
	return true;
}
enum class SettingsNumberFormat { FixedShortest, GeneralRoundTrip };

// FixedShortest is the CVar-compatible decimal grammar. GeneralRoundTrip keeps
// schema-1 journal spelling (general/max_digits10); journal callers canonicalize
// signed zero separately because JSON's integer -0 loses its sign on parsing.
// Failure preserves output. No process floating environment is changed.
inline bool SettingsNumberText(double number, SettingsNumberFormat format, std::string& output) {
	if (format != SettingsNumberFormat::FixedShortest && format != SettingsNumberFormat::GeneralRoundTrip) return false;
	const volatile std::uint64_t observed = std::bit_cast<std::uint64_t>(number);
	const std::uint64_t bits = observed;
	if ((bits & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL) return false;
	std::uint64_t fraction = bits & 0x000fffffffffffffULL;
	if ((bits & 0x7ff0000000000000ULL) == 0 && fraction != 0) {
		// Some standard libraries format subnormal inputs as zero under DAZ.
		// Expand fraction * 2^-1074 using integers, then round the exact digits.
		// The fixed representation is bounded by 1077 bytes including its sign.
		unsigned places = 1074;
		while ((fraction & 1) == 0) { fraction >>= 1; --places; }
		std::string digits = std::to_string(fraction);
		for (unsigned power = 0; power < places; ++power) {
			unsigned carry = 0;
			for (size_t i = digits.size(); i-- > 0;) {
				const unsigned value = unsigned(digits[i] - '0') * 5 + carry;
				digits[i] = char('0' + value % 10); carry = value / 10;
			}
			if (carry) digits.insert(digits.begin(), char('0' + carry));
		}
		std::string candidate = bits >> 63 ? "-" : "";
		if (format == SettingsNumberFormat::FixedShortest) {
			candidate += "0." + std::string(places - digits.size(), '0') + digits;
		} else {
			int exponent = static_cast<int>(digits.size()) - static_cast<int>(places) - 1;
			constexpr size_t precision = std::numeric_limits<double>::max_digits10;
			bool increment = digits[precision] > '5';
			if (digits[precision] == '5') {
				increment = ((digits[precision-1] - '0') & 1) != 0;
				for (size_t i = precision+1; i < digits.size(); ++i) increment = increment || digits[i] != '0';
			}
			digits.resize(precision);
			if (increment) {
				size_t i = digits.size();
				while (i && digits[i-1] == '9') { digits[--i] = '0'; }
				if (i) ++digits[i-1];
				else { digits[0] = '1'; ++exponent; }
			}
			while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
			candidate += digits.front();
			if (digits.size() > 1) candidate += "." + digits.substr(1);
			candidate += "e-" + std::to_string(-exponent);
		}
		output = std::move(candidate); return true;
	}
	char buffer[768];
	const auto result = format == SettingsNumberFormat::FixedShortest ?
		std::to_chars(buffer,buffer+sizeof(buffer),number,std::chars_format::fixed) :
		std::to_chars(buffer,buffer+sizeof(buffer),number,std::chars_format::general,std::numeric_limits<double>::max_digits10);
	if (result.ec != std::errc()) return false;
	std::string candidate(buffer,result.ptr); output = std::move(candidate); return true;
}
} // namespace openq4::ui
