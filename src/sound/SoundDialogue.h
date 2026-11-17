// SPDX-License-Identifier: GPL-3.0-or-later
// Spoken-dialogue classification is independent of mixer priority and routing.
#ifndef OPENQ4_SOUND_DIALOGUE_H
#define OPENQ4_SOUND_DIALOGUE_H

#include <initializer_list>
#include <string>
#include <string_view>

inline bool SoundDialogueSamplePath( std::string_view path ) {
	// The loader keeps canonical sound/vo names, but tools and mods may refer
	// directly to localized assets. Match a complete directory, not "voicegun".
	constexpr std::string_view prefix = "sound/vo";
	if ( path.size() <= prefix.size() ) return false;
	for ( size_t i = 0; i < prefix.size(); ++i ) {
		char c = path[i];
		if ( c == '\\' ) c = '/';
		if ( c >= 'A' && c <= 'Z' ) c += 'a' - 'A';
		if ( c != prefix[i] ) return false;
	}
	if ( path[8] == '/' || path[8] == '\\' ) return true;
	if ( path[8] != '_' ) return false;
	const auto end = path.find_first_of( "/\\", 9 );
	return end != std::string_view::npos && end > 9 && end + 1 < path.size();
}

inline bool SoundIsDialogue( bool music, bool authoredSpeech, std::string_view samplePath ) {
	// SSF_VO also labels UI effects, and SND_CHANNEL_VOICE is used by weapons.
	// Neither on its own is evidence that the selected sample is spoken dialogue.
	return !music && ( authoredSpeech || SoundDialogueSamplePath( samplePath ) );
}

namespace sound_dialogue_detail {
inline bool IsDigit( char c ) { return c >= '0' && c <= '9'; }
inline bool IsAlnum( char c ) { return IsDigit( c ) || ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ); }
// A Raven line id such as 1_2_1_105_2 (four or more numbers joined by '_').
inline bool LineIdAt( std::string_view text, size_t at ) {
	if ( at > 0 && ( IsAlnum( text[at - 1] ) || text[at - 1] == '_' ) ) return false;
	int numbers = 0;
	for ( size_t i = at; i < text.size(); ) {
		if ( !IsDigit( text[i] ) ) break;
		while ( i < text.size() && IsDigit( text[i] ) ) ++i;
		++numbers;
		if ( i >= text.size() || text[i] != '_' ) break;
		++i;
	}
	return numbers >= 4;
}
}

// The spoken line a voice shader's description gives, if it gives one. Raven's
// voice shaders carry the writers' script line as their description, which is
// often the only text a radio or chatter line has. Stage directions in (), []
// or <> (the Awakening's "<speaker:>" tags) are dropped, a description that runs
// into another line's id ends there, and what is left must be words: a pure
// direction ("(pain grunt)"), a sound label ("laugh") or a note with a '#' in
// it gives nothing. description must already be UTF-8.
inline bool SoundScriptCaption( std::string_view description, std::string& caption ) {
	using namespace sound_dialogue_detail;
	caption.clear();
	std::string text;
	char close = 0;
	for ( size_t i = 0; i < description.size(); ++i ) {
		const char c = description[i];
		if ( close ) {
			if ( c == close ) { close = 0; text += ' '; }
			continue;
		}
		if ( c == '(' ) { close = ')'; continue; }
		if ( c == '[' ) { close = ']'; continue; }
		if ( c == '<' ) { close = '>'; continue; }
		if ( c == '#' ) return false;
		if ( IsDigit( c ) && LineIdAt( description, i ) ) break;
		text += ( c == '\t' || c == '\r' || c == '\n' ) ? ' ' : c;
	}
	// Collapse the gaps directions leave, and keep no space before the
	// punctuation that followed one.
	bool letters = false;
	for ( size_t i = 0; i < text.size(); ++i ) {
		const char c = text[i];
		if ( c == ' ' ) {
			if ( caption.empty() || caption.back() == ' ' ) continue;
			caption += ' ';
			continue;
		}
		if ( ( c == ',' || c == '.' || c == '!' || c == '?' || c == ';' || c == ':' ) && !caption.empty() &&
			caption.back() == ' ' && i > 0 && text[i - 1] == ' ' && i > 1 && text[i - 2] == ' ' ) {
			caption.pop_back();
		}
		letters = letters || IsAlnum( c ) || static_cast<unsigned char>( c ) >= 0x80;
		caption += c;
	}
	while ( !caption.empty() && caption.back() == ' ' ) caption.pop_back();
	// A stray mark left in front of the first word (".And so do you."), not an ellipsis.
	if ( caption.size() > 1 && ( caption[0] == '.' || caption[0] == ',' ) && IsAlnum( caption[1] ) ) caption.erase( 0, 1 );
	if ( !letters ) { caption.clear(); return false; }
	std::string word;
	for ( char c : caption ) {
		if ( c >= 'A' && c <= 'Z' ) c += 'a' - 'A';
		if ( c >= 'a' && c <= 'z' ) word += c;
		else if ( IsAlnum( c ) || static_cast<unsigned char>( c ) >= 0x80 || ( c == ' ' && !word.empty() ) ) { word.clear(); break; }
	}
	for ( const char* label : { "death", "pain", "laugh", "laughs", "laughter", "laughing", "scream", "screams",
		"screaming", "grunt", "grunts", "breathing", "cough", "coughs", "coughing", "sigh", "sighs", "snoring",
		"yell", "groan", "groans", "static" } ) {
		if ( word == label ) { caption.clear(); return false; }
	}
	return true;
}

// Why a playing sample shows a subtitle or not under the mapping policy: it has
// installed text; no lipSync declares it; the declaration has no transcript; its
// text key does not resolve; only its random-variant shader declares one; an
// authored subtitle/ declaration (a caption pack, not installed lip-sync data)
// supplies its text; or, with no lip-sync text of its own, a helmet-radio copy
// shares its dry line's installed text, or the installed shader's own script
// line supplies it (SoundScriptCaption).
enum class SoundTranscript { Text, Undeclared, NoText, Unresolved, Variant, Authored, Script, Helmet };

inline const char* SoundTranscriptName( SoundTranscript transcript ) {
	switch ( transcript ) {
		case SoundTranscript::Text: return "text";
		case SoundTranscript::Undeclared: return "undeclared";
		case SoundTranscript::NoText: return "notext";
		case SoundTranscript::Unresolved: return "unresolved";
		case SoundTranscript::Variant: return "variant";
		case SoundTranscript::Authored: return "authored";
		case SoundTranscript::Script: return "script";
		case SoundTranscript::Helmet: return "helmet";
	}
	return "unknown";
}

#endif
