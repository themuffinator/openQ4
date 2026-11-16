/*
===========================================================================

openQ4 update core
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

#ifndef __OPENQ4_UPDATE_CORE_H__
#define __OPENQ4_UPDATE_CORE_H__

/*
===============================================================================

	What the game needs to find, verify and stage an update, with no engine
	globals so the native tests exercise the same code.

	Trust: a release publishes openq4-update.json and its Ed25519 signature,
	openq4-update.json.sig. The public key is compiled in (UpdateSigningKey.h).
	Every download URL is built from the compiled repository, the manifest's
	tag and an asset name checked against a strict pattern, and every
	downloaded byte is checked against a SHA-256 the signed manifest gives.
	Nothing a server sends chooses a host, a path outside the release, or a
	digest. Without a key, or without a valid signature, the manifest may only
	say that a newer release exists; the game then opens the release page.

===============================================================================
*/

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace openq4 {
namespace update {

static constexpr const char *MANIFEST_ASSET = "openq4-update.json";
static constexpr const char *SIGNATURE_ASSET = "openq4-update.json.sig";
static constexpr std::size_t MAX_MANIFEST_BYTES = 256 * 1024;
static constexpr std::size_t MAX_SIGNATURE_BYTES = 1024;
static constexpr std::uint64_t MAX_DOWNLOAD_BYTES = 4ull * 1024 * 1024 * 1024;
static constexpr int MANIFEST_SCHEMA = 1;

struct Version {
	int		major = 0;
	int		minor = 0;
	int		patch = 0;
};

// Strict "MAJOR.MINOR.PATCH", each part 0-99999 ("0.14.000" is 0.14.0).
bool		ParseVersion( std::string_view text, Version &version );
int			CompareVersions( const Version &a, const Version &b );
std::string	FormatVersion( const Version &version );

struct PackageEntry {
	std::string		archive;	// release asset name
	std::uint64_t	size = 0;
	std::string		sha256;		// lowercase hex
};

struct PackEntry {
	std::string		name;		// "pak1.pk4"
	std::string		md5;
	std::string		sha256;
	std::uint64_t	size = 0;
	bool			bulk = false;	// published as its own asset, left out of update archives
};

struct Manifest {
	Version								version;
	std::string							versionText;
	std::string							tag;		// "v0.15.0"
	std::map<std::string, PackageEntry>	packages;	// "windows-x64" -> its update archive
	std::vector<PackEntry>				packs;
};

// Parses and validates a manifest. It does not check the signature.
bool		ParseManifest( std::string_view json, Manifest &manifest, std::string &error );

// The signature asset is 128 lowercase or uppercase hex digits, optionally
// followed by whitespace.
bool		VerifyManifestSignature( std::string_view manifestBytes, std::string_view signatureText,
				const std::uint8_t publicKey[ 32 ] );

// True when publicKeyHex is 64 hex digits; fills publicKey.
bool		DecodePublicKey( std::string_view publicKeyHex, std::uint8_t publicKey[ 32 ] );

bool		IsSafeAssetName( std::string_view name );
bool		IsSafeTag( std::string_view tag );
bool		IsHexDigest( std::string_view text, std::size_t bytes );

// repository is the compiled "https://github.com/OWNER/REPO".
std::string	LatestManifestURL( std::string_view repository, std::string_view asset );
std::string	ReleaseAssetURL( std::string_view repository, std::string_view tag, std::string_view asset );
std::string	ReleasePageURL( std::string_view repository, std::string_view tag );

std::string	HexEncode( const std::uint8_t *bytes, std::size_t count );

std::filesystem::path	PathFromUtf8( std::string_view text );
std::string				PathToUtf8( const std::filesystem::path &path );

// Cancellation and progress, polled between chunks. Returning false cancels.
using ContinueFn = std::function<bool()>;

bool		SHA256File( const std::filesystem::path &path, std::string &hexDigest, std::string &error,
				const ContinueFn &keepGoing = ContinueFn() );

// Extracts a zip (stored or deflated members, no ZIP64, no encryption) into
// destination. Every member name must pass IsSafeRelativePath, and every
// member's CRC-32 must match. Fills files with the extracted relative paths.
bool		ExtractZip( const std::filesystem::path &archive, const std::filesystem::path &destination,
				std::vector<std::string> &files, std::string &error, const ContinueFn &keepGoing = ContinueFn() );

// Lists the regular files under root as '/' separated relative paths, sorted.
// Fails on a symbolic link or an unsafe name.
bool		ListTreeFiles( const std::filesystem::path &root, std::vector<std::string> &files, std::string &error );

/*
===============================================================================

	Staging a release

	Downloads the platform's update archive and every bulk pack the install
	lacks, checks each against the manifest's size and SHA-256 as it arrives,
	unpacks the archive into <install>/.update/stage/, adds the packs, and
	writes the plan last. Anything left from a failed or cancelled attempt is
	removed. The transport is the caller's, so tests stage from local files.

===============================================================================
*/

struct UpdatePlan;

// Streams one release asset to onData; false (with error) when it failed,
// and cancelled set when onData or the caller's cancellation stopped it.
using FetchFn = std::function<bool( const std::string &asset, std::uint64_t maxBytes,
	const std::function<bool( const void *data, std::size_t bytes )> &onData,
	bool &cancelled, std::string &error )>;
// Bytes staged so far and in all; return false to cancel.
using ProgressFn = std::function<bool( std::uint64_t done, std::uint64_t total )>;
// Unpacks a .tar.xz (Linux update archives).
using ExtractTarFn = std::function<bool( const std::filesystem::path &archive,
	const std::filesystem::path &destination, std::string &error )>;

struct StageRequest {
	Manifest					manifest;
	std::string					platformKey;		// "windows-x64"
	std::filesystem::path		install;			// the folder holding the game; .update/ goes inside
	std::vector<std::string>	requiredFiles;		// what the archive must carry: the program to restart, the helper
	std::vector<PackEntry>		packs;				// the bulk packs to download (SelectPacks)
};

struct StageResult {
	bool			ok = false;
	bool			cancelled = false;
	std::string		error;
};

// The bulk packs a release lists that the install must download. isCurrent
// says whether the install already has a pack's exact bytes.
std::vector<PackEntry>	SelectPacks( const Manifest &manifest, const std::function<bool( const PackEntry & )> &isCurrent );
// The archive plus the selected packs.
std::uint64_t			StageBytes( const StageRequest &request );

// plan arrives with its launch fields set; its files and removals are filled here.
StageResult	StageRelease( const StageRequest &request, UpdatePlan &plan, const FetchFn &fetch,
				const ProgressFn &progress, const ExtractTarFn &extractTar );

bool		ReadSmallFile( const std::filesystem::path &path, std::size_t maxBytes, std::string &contents );
// Writes through a temporary file and renames it into place.
bool		WriteFileAtomically( const std::filesystem::path &path, std::string_view contents, std::string &error );

} // namespace update
} // namespace openq4

#endif /* !__OPENQ4_UPDATE_CORE_H__ */
