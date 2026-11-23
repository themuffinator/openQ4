/*
===========================================================================

openQ4 GPL Source Code
Copyright (C) 2026 DarkMatter Productions

This file is part of the openQ4 GPL Source Code. It is free software: you can
redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

===========================================================================
*/

// The server list an HTTPS host publishes for the browser's fallback (roadmap
// MP-19), as tools/master/openq4_master.py --json writes it:
//
//   { "format": "openq4-server-list", "version": 1, "servers": [
//       { "address": "192.0.2.7:28004", ... }, { "address": "[2001:db8::7]:28004", ... } ] }
//
// Only the "format" and each server's "address" are read; other fields and
// unknown keys are skipped, so a later version can add them. Standard library
// only: tools/tests/native/ServerListParseTest.cpp compiles it alone.

#ifndef __SERVERLISTPARSE_H__
#define __SERVERLISTPARSE_H__

#include <cstddef>
#include <string>
#include <vector>

namespace serverListParse {

static constexpr std::size_t MAX_SERVERS = 4096;
static constexpr std::size_t MAX_ADDRESS_BYTES = 64;
static constexpr int MAX_DEPTH = 16;

class Reader {
public:
	explicit Reader( const std::string &text ) : text( text ) {}

	bool Parse( std::vector<std::string> &addresses, std::string &error ) {
		addresses.clear();
		SkipSpace();
		std::string format;
		if ( !Object( 0, &addresses, &format ) ) {
			error = failure.empty() ? "malformed JSON" : failure;
			return false;
		}
		SkipSpace();
		if ( at != text.size() ) {
			error = "text after the list";
			return false;
		}
		if ( format != "openq4-server-list" ) {
			error = "not an openQ4 server list";
			addresses.clear();
			return false;
		}
		return true;
	}

private:
	const std::string &text;
	std::size_t at = 0;
	std::string failure;

	void SkipSpace() {
		while ( at < text.size() && ( text[ at ] == ' ' || text[ at ] == '\t' || text[ at ] == '\r' || text[ at ] == '\n' ) ) {
			at++;
		}
	}

	bool Fail( const char *why ) {
		if ( failure.empty() ) {
			failure = why;
		}
		return false;
	}

	bool String( std::string *out ) {
		if ( at >= text.size() || text[ at ] != '"' ) {
			return Fail( "expected a string" );
		}
		at++;
		std::string value;
		while ( at < text.size() && text[ at ] != '"' ) {
			char c = text[ at++ ];
			if ( static_cast<unsigned char>( c ) < 0x20 ) {
				return Fail( "a control character in a string" );
			}
			if ( c == '\\' ) {
				if ( at >= text.size() ) {
					return Fail( "an unfinished escape" );
				}
				const char e = text[ at++ ];
				switch ( e ) {
					case '"': c = '"'; break;
					case '\\': c = '\\'; break;
					case '/': c = '/'; break;
					case 'b': c = '\b'; break;
					case 'f': c = '\f'; break;
					case 'n': c = '\n'; break;
					case 'r': c = '\r'; break;
					case 't': c = '\t'; break;
					case 'u':
						// an address is ASCII: a \u escape in a value this reader keeps is no address
						if ( at + 4 > text.size() ) {
							return Fail( "an unfinished escape" );
						}
						at += 4;
						c = '?';
						break;
					default:
						return Fail( "an unknown escape" );
				}
			}
			if ( out != nullptr && value.size() < 4096 ) {
				value += c;
			}
		}
		if ( at >= text.size() ) {
			return Fail( "an unfinished string" );
		}
		at++;
		if ( out != nullptr ) {
			*out = value;
		}
		return true;
	}

	bool Scalar() {
		const std::size_t start = at;
		while ( at < text.size() && ( ( text[ at ] >= '0' && text[ at ] <= '9' ) || text[ at ] == '-' || text[ at ] == '+' ||
				text[ at ] == '.' || text[ at ] == 'e' || text[ at ] == 'E' || ( text[ at ] >= 'a' && text[ at ] <= 'z' ) ) ) {
			at++;
		}
		const std::string word = text.substr( start, at - start );
		if ( word == "true" || word == "false" || word == "null" ) {
			return true;
		}
		if ( word.empty() || !( ( word[ 0 ] >= '0' && word[ 0 ] <= '9' ) || word[ 0 ] == '-' ) ) {
			return Fail( "expected a value" );
		}
		return true;
	}

	// A value; servers collects the "address" of each object in the top level's "servers" array.
	bool Value( int depth, std::vector<std::string> *servers, bool inServers ) {
		SkipSpace();
		if ( depth > MAX_DEPTH ) {
			return Fail( "nested too deep" );
		}
		if ( at >= text.size() ) {
			return Fail( "the text ends early" );
		}
		const char c = text[ at ];
		if ( c == '{' ) {
			return Object( depth, inServers ? servers : nullptr, nullptr, inServers );
		}
		if ( c == '[' ) {
			return Array( depth, inServers ? servers : nullptr );
		}
		if ( c == '"' ) {
			return String( nullptr );
		}
		return Scalar();
	}

	bool Array( int depth, std::vector<std::string> *servers ) {
		at++;
		SkipSpace();
		if ( at < text.size() && text[ at ] == ']' ) {
			at++;
			return true;
		}
		while ( true ) {
			if ( !Value( depth + 1, servers, servers != nullptr ) ) {
				return false;
			}
			SkipSpace();
			if ( at < text.size() && text[ at ] == ',' ) {
				at++;
				continue;
			}
			if ( at < text.size() && text[ at ] == ']' ) {
				at++;
				return true;
			}
			return Fail( "expected , or ] in an array" );
		}
	}

	// The top level (format and servers), or one server's object (its address).
	bool Object( int depth, std::vector<std::string> *servers, std::string *format, bool serverEntry = false ) {
		if ( at >= text.size() || text[ at ] != '{' ) {
			return Fail( "expected an object" );
		}
		at++;
		SkipSpace();
		if ( at < text.size() && text[ at ] == '}' ) {
			at++;
			return true;
		}
		while ( true ) {
			SkipSpace();
			std::string key;
			if ( !String( &key ) ) {
				return false;
			}
			SkipSpace();
			if ( at >= text.size() || text[ at ] != ':' ) {
				return Fail( "expected : in an object" );
			}
			at++;
			SkipSpace();
			if ( depth == 0 && key == "format" && format != nullptr && at < text.size() && text[ at ] == '"' ) {
				if ( !String( format ) ) {
					return false;
				}
			} else if ( depth == 0 && key == "servers" && at < text.size() && text[ at ] == '[' ) {
				if ( !Array( depth + 1, servers ) ) {
					return false;
				}
			} else if ( serverEntry && key == "address" && at < text.size() && text[ at ] == '"' ) {
				std::string address;
				if ( !String( &address ) ) {
					return false;
				}
				if ( servers != nullptr && !address.empty() && address.size() <= MAX_ADDRESS_BYTES && servers->size() < MAX_SERVERS ) {
					servers->push_back( address );
				}
			} else if ( !Value( depth + 1, nullptr, false ) ) {
				return false;
			}
			SkipSpace();
			if ( at < text.size() && text[ at ] == ',' ) {
				at++;
				continue;
			}
			if ( at < text.size() && text[ at ] == '}' ) {
				at++;
				return true;
			}
			return Fail( "expected , or } in an object" );
		}
	}
};

// The addresses in a published list. False, with the reason, for text that is
// not a well-formed openQ4 server list.
inline bool ParseServerList( const std::string &text, std::vector<std::string> &addresses, std::string &error ) {
	Reader reader( text );
	return reader.Parse( addresses, error );
}

} // namespace serverListParse

#endif /* !__SERVERLISTPARSE_H__ */
