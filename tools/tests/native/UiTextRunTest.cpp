// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/TextRun.h"
#include "src/ui/retained/TextInput.h"
#include <RmlUi/Core/StringUtilities.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace openq4::ui;
static unsigned checks = 0, lookups = 0;
static void Check(bool condition, const char* why) {
	++checks;
	if (!condition) { std::fprintf(stderr,"FAIL: %s\n",why); std::exit(1); }
}
static Glyph Lookup(std::uint32_t codepoint) {
	++lookups;
	Glyph glyph;
	glyph.advance = codepoint == ' ' ? 3.f : codepoint == 0x301 ? 0.f : 7.25f + float(codepoint % 3) * .125f;
	glyph.left = -.75f; glyph.top = -9.5f; glyph.width = 6.25f; glyph.height = 11.75f;
	glyph.u0 = .1f; glyph.v0 = .2f; glyph.u1 = .3f; glyph.v1 = .4f;
	if (codepoint != ' ') glyph.material = codepoint < 128 ? "font/latin" : "font/fallback";
	return glyph;
}
static void GeometryCases() {
	std::string error;
	for (const std::string& text : {std::string{}, std::string("-123.45e+6"), std::string("A \xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80"), std::string("e\xcc\x81")}) {
		for (const float spacing : {0.f, .375f, -.25f}) {
			const auto run = MeasureTextRun(text,spacing,Lookup,error);
			Check(bool(run) && error.empty(),"valid scalar text publishes a complete run");
			float expected = 0;
			std::size_t index = 0;
			// Independent historical loop: compare float addition order and every
			// actual glyph quad origin, bearing, UV and byte boundary to the run.
			for (Rml::StringIteratorU8 it(text);it;) {
				const auto start = std::size_t(it.offset()); const auto cp = std::uint32_t(*it); ++it;
				const auto glyph = Lookup(cp); const auto& record = run->glyphs.at(index++);
				Check(record.byteStart==start && record.byteEnd==std::size_t(it.offset()) && record.codepoint==cp,"UTF-8 scalar byte endpoints match Rml iteration");
				Check(record.penX==expected,"glyph pen is the same float position used by historical rendering");
				Check(record.penX+record.glyph.left==expected+glyph.left && record.glyph.top==glyph.top && record.glyph.width==glyph.width && record.glyph.height==glyph.height,"quad bounds preserve glyph bearings and extents");
				Check(record.glyph.u0==glyph.u0 && record.glyph.v0==glyph.v0 && record.glyph.u1==glyph.u1 && record.glyph.v1==glyph.v1 && record.glyph.material==glyph.material,"glyph UV and fallback material identity are preserved");
				float caret = -999; Check(run->CaretPosition(start,caret) && caret==expected,"caret starts at the submitted glyph pen");
				expected += glyph.advance + spacing;
				Check(run->CaretPosition(record.byteEnd,caret) && caret==expected,"caret endpoint uses the same advance plus trailing spacing");
			}
			Check(run->glyphs.size()==index && run->carets.size()==index+1,"one scalar record and all scalar caret endpoints");
			Check(run->width==expected && run->roundedWidth==int(std::lround(expected)),"only the final width is rounded");
			std::size_t visits = 0;
			const auto walked=WalkTextRun(text,spacing,Lookup,[&](const TextRunGlyph& record){Check(record.penX==run->glyphs.at(visits++).penX,"streaming and retained glyph positions agree");});
			Check(walked==expected && visits==index,"streaming fallback and measured run preserve output width");
		}
	}
	const auto unicode=MeasureTextRun("A\xc3\xa9\xf0\x9f\x98\x80",0,Lookup,error);
	for (const auto byte : {2u,4u,5u,6u,8u}) { float sentinel=123; Check(!unicode->CaretPosition(byte,sentinel) && sentinel==123,"split or out-of-range scalar caret leaves output untouched"); }
}
static void HitCases() {
	std::string error;
	const auto uniform=[](std::uint32_t cp){Glyph g;g.advance=cp=='z'?0.f:10.f;return g;};
	const auto run=MeasureTextRun("12z3",0,uniform,error);
	Check(run->HitTestLtr(-10)==0 && run->HitTestLtr(0)==0,"hit clamps left to first scalar");
	Check(run->HitTestLtr(4.9f)==0 && run->HitTestLtr(5)==1,"midpoint tie advances");
	Check(run->HitTestLtr(15)==3 && run->HitTestLtr(20)==3,"duplicate-position carets select the last scalar boundary");
	Check(run->HitTestLtr(100)==4,"hit clamps right to last scalar");
	Check(!run->HitTestLtr(std::numeric_limits<float>::quiet_NaN()) && !run->HitTestLtr(std::numeric_limits<float>::infinity()),"nonfinite hits are unavailable");
	const auto negative=MeasureTextRun("12",-11,uniform,error);
	Check(bool(negative) && !negative->monotonicLtr && !negative->HitTestLtr(0),"nonmonotonic layout remains measurable but cannot claim numeric LTR hit testing");
	const auto empty=MeasureTextRun({},0,uniform,error);
	Check(empty->HitTestLtr(-1)==0 && empty->HitTestLtr(100)==0,"empty input has one legal caret");
}
static void ValidationCases() {
	std::string error;
	for(const auto& text : {std::string("\xc0\x80",2),std::string("\xed\xa0\x80",3),std::string("a\0b",3),std::string(TextInputMaxBytes+1,'x')}) {
		lookups=0; Check(!MeasureTextRun(text,0,Lookup,error) && !error.empty() && lookups==0,"strict invalid or oversized text never queries glyphs");
	}
	Check(!MeasureTextRun("1",std::numeric_limits<float>::infinity(),Lookup,error),"nonfinite spacing rejected");
	Check(!MeasureTextRun("1",0,{},error),"missing glyph callback rejected");
	for (bool badUv:{false,true}) {
		const auto invalid=[&](std::uint32_t){auto g=Lookup('1');if(badUv)g.u0=std::numeric_limits<float>::quiet_NaN();else g.advance=std::numeric_limits<float>::max();return g;};
		Check(!MeasureTextRun("12",0,invalid,error),"invalid glyph metrics or overflowing width never publish partial run");
	}
	lookups=0; const std::string large(TextInputMaxBytes+3,'x');
	const auto expected=WalkTextRun(large,.25f,Lookup);
	Check(lookups==large.size() && expected>0,"legacy streaming path has no new editing transport limit");
	for(const std::string& raw : {std::string("a\0b",3),std::string("\xc0\x80\xff",3)}) {
		float original=0;for(Rml::StringIteratorU8 it(raw);it;++it)original+=Lookup(std::uint32_t(*it)).advance+.25f;
		Check(WalkTextRun(raw,.25f,Lookup)==original,"raw malformed and NUL iteration retain prior Rml behavior");
	}
}
static void CacheCases() {
	std::string error; TextRunCache cache(2,100000);
	lookups=0; const auto a=cache.Get(1,"12",.25f,Lookup,error); const auto n=lookups;
	Check(cache.Get(1,"12",.25f,Lookup,error)==a && lookups==n,"identical measurement and drawing share immutable glyph run");
	const auto b=cache.Get(2,"12",.25f,Lookup,error);Check(b!=a && lookups==n+2,"face identities isolate measurements");
	Check(cache.Get(1,"12",.25f,Lookup,error)==a,"cache hit promotes retained entry");
	const auto c=cache.Get(1,"12",.5f,Lookup,error);Check(c!=a && cache.ResidentEntries()==2,"spacing participates in key and count bounds residency");
	Check(cache.Get(2,"12",.25f,Lookup,error)!=b,"least recently used entry is evicted");
	const auto entries=cache.ResidentEntries(), bytes=cache.ResidentBytes();
	Check(!cache.Get(0,"12",0,Lookup,error) && cache.ResidentEntries()==entries && cache.ResidentBytes()==bytes,"rejected cache requests preserve residency");
	cache.Clear();Check(cache.ResidentEntries()==0 && cache.ResidentBytes()==0,"font resource release invalidates cache");
	const auto changed=[](std::uint32_t){Glyph g;g.advance=50;return g;};
	const auto fresh=cache.Get(1,"12",.25f,changed,error);Check(fresh!=a && fresh->width==100.5f && a->width!=fresh->width,"same face address after clear remeasures while old immutable run stays valid data");
	TextRunCache tiny(10,1);const auto transient=tiny.Get(1,"1",0,Lookup,error);
	Check(bool(transient) && tiny.ResidentEntries()==0 && tiny.ResidentBytes()==0,"over-budget run returned transiently without violating residency cap");
	TextRunCache disabled(0,100000);Check(bool(disabled.Get(1,"1",0,Lookup,error)) && disabled.ResidentEntries()==0,"zero entry budget disables retention without hiding text");
	TextRunCache sizing;const auto first=sizing.Get(1,"1",0,Lookup,error);const auto cost=sizing.ResidentBytes();
	TextRunCache oneByBytes(10,cost);const auto x=oneByBytes.Get(1,"1",0,Lookup,error);oneByBytes.Get(2,"1",0,Lookup,error);
	Check(oneByBytes.ResidentEntries()==1 && oneByBytes.ResidentBytes()<=cost && bool(first) && bool(x),"byte budget evicts independently of count and external shared ownership");
}
int main(){GeometryCases();HitCases();ValidationCases();CacheCases();std::printf("Scalar text runs: %u checks passed (unshaped CPU metrics; no glyph-raster or IME qualification)\n",checks);}
