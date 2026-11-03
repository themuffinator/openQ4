// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// JsonCpp number decoding with openQ4's subnormal-numbers.patch. A number
// from_chars cannot take whole and in range falls back to
// `istringstream >> double`, which JsonCpp 1.9.8 imbues with the classic
// locale, so it decodes with the platform's own num_get and no test facet can
// reach it. Under libc++ over a gdtoa strtod (Apple, FreeBSD, Android), strtod
// reports ERANGE for every subnormal or underflowing result and libc++ sets
// failbit but still stores the value: only there do these numbers take the
// patched branch, which the macOS jobs run. The MSVC STL and libstdc++ decode
// subnormals without failing.
#include <json/json.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#define TEST_SSE 1
#endif
// Json::Reader is deprecated upstream, but the patch changes it too.
#if defined(_MSC_VER)
#pragma warning(disable: 4996)
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d (check %u): %s\n",__LINE__,checks,#x);std::exit(1);}}while(false)

static std::uint64_t Bits(double value) { std::uint64_t bits; std::memcpy(&bits,&value,sizeof(bits)); return bits; }

// Denormals-are-zero only, where the target exposes it: floating comparisons
// then see a subnormal as zero, so the patch must classify it from its bits.
// Not flush-to-zero, because gdtoa computes a subnormal result with a final
// floating multiply that FTZ would flush before any decoder could see it.
struct DenormalsAreZero {
#if TEST_SSE
	unsigned old=_mm_getcsr();DenormalsAreZero(){_mm_setcsr(old|0x40u);}~DenormalsAreZero(){_mm_setcsr(old);}
#else
	DenormalsAreZero(){}
#endif
};

static bool Parse(const std::string& text, bool legacy, Json::Value& root, std::string& errors) {
	if (legacy) {
		Json::Reader reader;
		const bool ok = reader.parse(text,root,false);
		errors = reader.getFormattedErrorMessages();
		return ok;
	}
	Json::CharReaderBuilder builder;
	std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
	return reader->parse(text.data(),text.data() + text.size(),&root,&errors);
}
static void Decodes(const std::string& token, std::uint64_t bits) {
	for (bool legacy : {false,true}) {
		Json::Value root; std::string errors;
		CHECK(Parse("[" + token + "]",legacy,root,errors));
		CHECK(root.isArray() && root.size() == 1 && root[0u].isDouble());
		CHECK(Bits(root[0u].asDouble()) == bits);
	}
}
static void Rejects(const std::string& token) {
	for (bool legacy : {false,true}) {
		Json::Value root; std::string errors;
		CHECK(!Parse("[" + token + "]",legacy,root,errors));
		CHECK(errors.find("'" + token + "' is not a number") != std::string::npos);
	}
}

// Every library must decode these exactly, subnormal or not.
static void Numbers() {
	Decodes("4.9406564584124654e-324",0x0000000000000001ULL);
	Decodes("-4.9406564584124654e-324",0x8000000000000001ULL);
	Decodes("9.8813129168249309e-324",0x0000000000000002ULL);
	Decodes("3e-324",0x0000000000000001ULL);
	Decodes("2.2250738585072009e-308",0x000fffffffffffffULL);
	Decodes("0." + std::string(323,'0') + "49406564584124654",0x0000000000000001ULL);
	Decodes("2.2250738585072014e-308",0x0010000000000000ULL);
	Decodes("1.5",0x3ff8000000000000ULL);
}

int main() {
	Numbers();
	{ DenormalsAreZero daz; Numbers(); }
	// Overflow decodes as infinity everywhere. Underflow to zero is an error
	// where num_get reports it (libc++, the MSVC STL); libstdc++'s num_get
	// does not, so there it decodes as a signed zero.
	Decodes("1e309",0x7ff0000000000000ULL);
	Decodes("-1e309",0xfff0000000000000ULL);
#if defined(__GLIBCXX__)
	Decodes("1e-400",0x0000000000000000ULL);
	Decodes("-1e-400",0x8000000000000000ULL);
	Decodes("2e-324",0x0000000000000000ULL);
#else
	Rejects("1e-400");
	Rejects("-1e-400");
	Rejects("2e-324");
#endif
	std::printf("UiJsonNumberTest: %u checks passed\n",checks);
}
