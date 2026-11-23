/*
===========================================================================

openQ4 video capture: helpers shared by the encoders
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

#if !defined( ID_DEDICATED )

#include "VideoEncoder.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#if defined( _WIN32 )
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

#if defined( _WIN32 )
static std::wstring VideoEncoder_Wide( const std::string &utf8 ) {
	if ( utf8.empty() ) {
		return std::wstring();
	}
	const int length = MultiByteToWideChar( CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0 );
	std::wstring wide( length > 0 ? length : 0, L'\0' );
	if ( length > 0 ) {
		MultiByteToWideChar( CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &wide[0], length );
	}
	return wide;
}
#endif

FILE *VideoEncoder_OpenFile( const std::string &utf8Path, const char *mode ) {
#if defined( _WIN32 )
	return _wfopen( VideoEncoder_Wide( utf8Path ).c_str(), VideoEncoder_Wide( mode ).c_str() );
#else
	return fopen( utf8Path.c_str(), mode );
#endif
}

bool VideoEncoder_RemoveFile( const std::string &utf8Path ) {
#if defined( _WIN32 )
	return DeleteFileW( VideoEncoder_Wide( utf8Path ).c_str() ) != 0;
#else
	return unlink( utf8Path.c_str() ) == 0;
#endif
}

bool VideoEncoder_RenameFile( const std::string &from, const std::string &to ) {
#if defined( _WIN32 )
	return MoveFileExW( VideoEncoder_Wide( from ).c_str(), VideoEncoder_Wide( to ).c_str(), MOVEFILE_REPLACE_EXISTING ) != 0;
#else
	return rename( from.c_str(), to.c_str() ) == 0;
#endif
}

static bool VideoEncoder_IsProgram( const std::string &path ) {
	if ( path.empty() ) {
		return false;
	}
#if defined( _WIN32 )
	const DWORD attributes = GetFileAttributesW( VideoEncoder_Wide( path ).c_str() );
	return attributes != INVALID_FILE_ATTRIBUTES && ( attributes & FILE_ATTRIBUTE_DIRECTORY ) == 0;
#else
	struct stat info;
	return stat( path.c_str(), &info ) == 0 && S_ISREG( info.st_mode ) && access( path.c_str(), X_OK ) == 0;
#endif
}

bool VideoEncoder_FindFFmpeg( const char *configured, std::string &path ) {
#if defined( _WIN32 )
	const char *program = "ffmpeg.exe";
	const char separator = ';';
	const char slash = '\\';
#else
	const char *program = "ffmpeg";
	const char separator = ':';
	const char slash = '/';
#endif
	path.clear();
	if ( configured != NULL && configured[0] != '\0' ) {
		std::string candidate = configured;
		if ( VideoEncoder_IsProgram( candidate ) ) {
			path = candidate;
			return true;
		}
		// a folder holding the program
		if ( candidate.back() != '/' && candidate.back() != '\\' ) {
			candidate += slash;
		}
		candidate += program;
		if ( VideoEncoder_IsProgram( candidate ) ) {
			path = candidate;
			return true;
		}
		return false;
	}
	std::string exeDir = Sys_EXEPath();
	const size_t cut = exeDir.find_last_of( "/\\" );
	if ( cut != std::string::npos ) {
		exeDir.resize( cut + 1 );
		if ( VideoEncoder_IsProgram( exeDir + program ) ) {
			path = exeDir + program;
			return true;
		}
	}
	const char *environmentPath = getenv( "PATH" );
	if ( environmentPath == NULL ) {
		return false;
	}
	std::string remaining = environmentPath;
	size_t start = 0;
	while ( start <= remaining.size() ) {
		size_t end = remaining.find( separator, start );
		if ( end == std::string::npos ) {
			end = remaining.size();
		}
		std::string directory = remaining.substr( start, end - start );
		if ( directory.size() >= 2 && directory.front() == '"' && directory.back() == '"' ) {
			directory = directory.substr( 1, directory.size() - 2 );
		}
		if ( !directory.empty() ) {
			if ( directory.back() != '/' && directory.back() != '\\' ) {
				directory += slash;
			}
			if ( VideoEncoder_IsProgram( directory + program ) ) {
				path = directory + program;
				return true;
			}
		}
		start = end + 1;
	}
	return false;
}

int VideoEncoder_QualityToCRF( int quality ) {
	quality = quality < 1 ? 1 : ( quality > 100 ? 100 : quality );
	const int crf = (int)floor( 51.0 - quality * 0.42 + 0.5 );
	return crf < 0 ? 0 : ( crf > 51 ? 51 : crf );
}

int VideoEncoder_QualityToKbps( int quality, int width, int height, int fps, const std::string &codec ) {
	quality = quality < 1 ? 1 : ( quality > 100 ? 100 : quality );
	// bits per pixel per frame: 0.02 at the bottom, 0.18 at the default 80,
	// 0.27 at 100 -- game footage is all motion, so it needs more than film
	const double q = quality / 100.0;
	double bitsPerPixel = 0.02 + q * q * 0.25;
	if ( codec == "hevc" || codec == "av1" ) {
		bitsPerPixel *= 0.65;
	}
	double kbps = (double)width * height * fps * bitsPerPixel / 1000.0;
	if ( kbps < 500.0 ) {
		kbps = 500.0;
	}
	if ( kbps > 240000.0 ) {
		kbps = 240000.0;
	}
	return (int)kbps;
}

#endif
