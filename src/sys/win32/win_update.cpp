/*
===========================================================================

openQ4 platform services for the in-game updater (Windows)
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

#ifndef ID_DEDICATED

#include "../sys_update.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <cwchar>

#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

namespace {

std::wstring Widen( const std::string &text ) {
	if ( text.empty() ) {
		return std::wstring();
	}
	const int length = MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>( text.size() ), nullptr, 0 );
	if ( length <= 0 ) {
		return std::wstring();
	}
	std::wstring wide( static_cast<size_t>( length ), L'\0' );
	MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>( text.size() ), wide.data(), length );
	return wide;
}

std::string Narrow( const wchar_t *text, size_t length ) {
	if ( length == 0 ) {
		return std::string();
	}
	const int bytes = WideCharToMultiByte( CP_UTF8, 0, text, static_cast<int>( length ), nullptr, 0, nullptr, nullptr );
	std::string narrow( static_cast<size_t>( bytes > 0 ? bytes : 0 ), '\0' );
	if ( bytes > 0 ) {
		WideCharToMultiByte( CP_UTF8, 0, text, static_cast<int>( length ), narrow.data(), bytes, nullptr, nullptr );
	}
	return narrow;
}

std::string LastError( const char *what ) {
	return std::string( what ) + " failed (error " + std::to_string( GetLastError() ) + ")";
}

class InternetHandle {
public:
	explicit InternetHandle( HINTERNET handle = nullptr ) : handle( handle ) {}
	~InternetHandle() { if ( handle != nullptr ) { WinHttpCloseHandle( handle ); } }
	InternetHandle( const InternetHandle & ) = delete;
	InternetHandle &operator=( const InternetHandle & ) = delete;
	operator HINTERNET() const { return handle; }
	HINTERNET handle;
};

// CommandLineToArgvW's quoting rules, inverted.
std::wstring QuoteArgument( const std::wstring &argument ) {
	if ( !argument.empty() && argument.find_first_of( L" \t\n\v\"" ) == std::wstring::npos ) {
		return argument;
	}
	std::wstring quoted = L"\"";
	for ( size_t i = 0; ; ++i ) {
		size_t backslashes = 0;
		while ( i < argument.size() && argument[ i ] == L'\\' ) {
			++i;
			++backslashes;
		}
		if ( i == argument.size() ) {
			quoted.append( backslashes * 2, L'\\' );
			break;
		}
		if ( argument[ i ] == L'"' ) {
			quoted.append( backslashes * 2 + 1, L'\\' );
		} else {
			quoted.append( backslashes, L'\\' );
		}
		quoted += argument[ i ];
	}
	quoted += L'"';
	return quoted;
}

} // namespace

bool Sys_UpdateHttpsAvailable( std::string &reason ) {
	reason.clear();
	return true;
}

sysHttpsResult_t Sys_UpdateHttpsGet( const std::string &url, std::uint64_t maxBytes,
		const sysHttpsDataFn &onData, const sysHttpsProgressFn &onProgress ) {
	sysHttpsResult_t result;
	const std::wstring wideUrl = Widen( url );

	URL_COMPONENTS parts = {};
	parts.dwStructSize = sizeof( parts );
	wchar_t host[ 256 ] = {};
	wchar_t path[ 2048 ] = {};
	parts.lpszHostName = host;
	parts.dwHostNameLength = ARRAYSIZE( host );
	parts.lpszUrlPath = path;
	parts.dwUrlPathLength = ARRAYSIZE( path );
	if ( wideUrl.empty() || !WinHttpCrackUrl( wideUrl.c_str(), 0, 0, &parts ) || parts.nScheme != INTERNET_SCHEME_HTTPS ) {
		result.error = "not an https URL";
		return result;
	}

	InternetHandle session( WinHttpOpen( L"openQ4-updater/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 ) );
	if ( session.handle == nullptr ) {
		// Windows before 8.1 has no automatic proxy; use the configured one.
		session.handle = WinHttpOpen( L"openQ4-updater/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
	}
	if ( session.handle == nullptr ) {
		result.error = LastError( "WinHttpOpen" );
		return result;
	}
	DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
	if ( !WinHttpSetOption( session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof( protocols ) ) ) {
		protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
		WinHttpSetOption( session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof( protocols ) );
	}
	WinHttpSetTimeouts( session, 15000, 15000, 30000, 30000 );

	InternetHandle connection( WinHttpConnect( session, host, parts.nPort, 0 ) );
	if ( connection.handle == nullptr ) {
		result.error = LastError( "WinHttpConnect" );
		return result;
	}
	InternetHandle request( WinHttpOpenRequest( connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE ) );
	if ( request.handle == nullptr ) {
		result.error = LastError( "WinHttpOpenRequest" );
		return result;
	}
	// Redirects stay on HTTPS (WinHTTP's default policy never follows one to
	// HTTP); state it rather than rely on the default.
	DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
	WinHttpSetOption( request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof( redirectPolicy ) );

	if ( !WinHttpSendRequest( request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0 ) ) {
		result.error = LastError( "sending the request" );
		return result;
	}
	if ( !WinHttpReceiveResponse( request, nullptr ) ) {
		result.error = LastError( "receiving the response" );
		return result;
	}

	DWORD status = 0;
	DWORD statusSize = sizeof( status );
	if ( !WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
			&status, &statusSize, WINHTTP_NO_HEADER_INDEX ) ) {
		result.error = LastError( "reading the status" );
		return result;
	}
	result.status = static_cast<int>( status );
	if ( status < 200 || status >= 300 ) {
		result.error = "the server answered HTTP " + std::to_string( status );
		return result;
	}

	std::uint64_t total = 0;
	wchar_t lengthText[ 32 ] = {};
	DWORD lengthSize = sizeof( lengthText );
	if ( WinHttpQueryHeaders( request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
			lengthText, &lengthSize, WINHTTP_NO_HEADER_INDEX ) ) {
		total = std::wcstoull( lengthText, nullptr, 10 );
	}
	if ( total > maxBytes ) {
		result.error = "the response is larger than expected";
		return result;
	}

	std::uint64_t received = 0;
	std::vector<char> buffer( 256 * 1024 );
	for ( ;; ) {
		DWORD read = 0;
		if ( !WinHttpReadData( request, buffer.data(), static_cast<DWORD>( buffer.size() ), &read ) ) {
			result.error = LastError( "reading the response" );
			return result;
		}
		if ( read == 0 ) {
			break;
		}
		received += read;
		if ( received > maxBytes ) {
			result.error = "the response is larger than expected";
			return result;
		}
		if ( ( onData && !onData( buffer.data(), read ) ) || ( onProgress && !onProgress( received, total ) ) ) {
			result.cancelled = true;
			result.error = "cancelled";
			return result;
		}
	}
	if ( total != 0 && received != total ) {
		result.error = "the download ended early";
		return result;
	}
	result.ok = true;
	return result;
}

std::filesystem::path Sys_UpdateExecutablePath( void ) {
	std::wstring path( MAX_PATH, L'\0' );
	for ( ;; ) {
		const DWORD length = GetModuleFileNameW( nullptr, path.data(), static_cast<DWORD>( path.size() ) );
		if ( length == 0 ) {
			return std::filesystem::path();
		}
		if ( length < path.size() ) {
			path.resize( length );
			return std::filesystem::path( path );
		}
		path.resize( path.size() * 2 );
	}
}

std::filesystem::path Sys_UpdateWorkingDirectory( void ) {
	std::error_code ec;
	return std::filesystem::current_path( ec );
}

void Sys_UpdateLaunchArguments( std::vector<std::string> &arguments, std::string &commandTail ) {
	arguments.clear();
	const wchar_t *line = GetCommandLineW();
	// Skip the program name the way the C runtime does: quoted up to the next
	// quote, otherwise up to the first blank.
	if ( *line == L'"' ) {
		++line;
		while ( *line != L'\0' && *line != L'"' ) {
			++line;
		}
		if ( *line == L'"' ) {
			++line;
		}
	} else {
		while ( *line != L'\0' && *line != L' ' && *line != L'\t' ) {
			++line;
		}
	}
	while ( *line == L' ' || *line == L'\t' ) {
		++line;
	}
	commandTail = Narrow( line, std::wcslen( line ) );
}

std::uint64_t Sys_UpdateProcessId( void ) {
	return GetCurrentProcessId();
}

bool Sys_UpdateStartDetached( const std::filesystem::path &executable,
		const std::vector<std::string> &arguments, std::string &error ) {
	std::wstring commandLine = QuoteArgument( executable.wstring() );
	for ( const std::string &argument : arguments ) {
		commandLine += L" " + QuoteArgument( Widen( argument ) );
	}
	STARTUPINFOW startup = {};
	startup.cb = sizeof( startup );
	PROCESS_INFORMATION process = {};
	const std::wstring workingDir = executable.parent_path().wstring();
	if ( !CreateProcessW( executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
			CREATE_NEW_PROCESS_GROUP, nullptr, workingDir.c_str(), &startup, &process ) ) {
		error = LastError( "starting the updater" );
		return false;
	}
	CloseHandle( process.hThread );
	CloseHandle( process.hProcess );
	return true;
}

bool Sys_UpdateExtractTar( const std::filesystem::path &, const std::filesystem::path &, std::string &error ) {
	error = "Windows update archives are zips";
	return false;
}

bool Sys_UpdateMakeExecutable( const std::filesystem::path & ) {
	return true;
}

const char *Sys_UpdateManagedBy( void ) {
	return nullptr;
}

#endif /* !ID_DEDICATED */
