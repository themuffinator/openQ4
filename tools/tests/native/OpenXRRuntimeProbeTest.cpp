// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
//
// Stage one of VR headset detection (src/sys/openxr/OpenXRRuntimeProbe.h):
// the runtime manifest's name and library, and the Linux loader's search
// order for the active runtime. Engine-free; the probe's file, registry and
// environment reads are exercised by tools/tests/openxr_vr_detection_smoke.py.

#include "../../../src/sys/openxr/OpenXRRuntimeProbe.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Expect( bool condition, const char *what ) {
	if ( !condition ) {
		std::fprintf( stderr, "FAIL: %s\n", what );
		++failures;
	}
}

void Manifests( void ) {
	using openq4::xr::ParseRuntimeManifest;
	std::string name, library;
	// SteamVR's manifest, as installed
	Expect( ParseRuntimeManifest( "{\n  \"file_format_version\" : \"1.0.0\",\n  \"runtime\" : {\n"
		"    \"VALVE_runtime_is_steamvr\" : true,\n    \"library_path\" : \"..\\\\bin\\\\win64\\\\vrclient_x64.dll\",\n"
		"    \"name\" : \"SteamVR\"\n  }\n}\n", name, library ), "a SteamVR manifest parses" );
	Expect( name == "SteamVR", "the runtime's name is read" );
	Expect( library == "..\\bin\\win64\\vrclient_x64.dll", "escaped backslashes are undone" );
	// a manifest without a name is named after its library's file
	Expect( ParseRuntimeManifest( "{\"runtime\":{\"library_path\":\"/usr/lib/libopenxr_monado.so\"}}", name, library ), "no name parses" );
	Expect( name == "libopenxr_monado.so", "a nameless runtime takes its library's file name" );
	// a name outside the runtime object is not the runtime's
	Expect( ParseRuntimeManifest( "{\"name\":\"outer\",\"runtime\":{\"library_path\":\"a/b.dll\",\"functions\":{\"x\":\"y\"}},"
		"\"extra\":{\"name\":\"later\"}}", name, library ), "nested objects parse" );
	Expect( name == "b.dll", "only the runtime object's own name counts" );
	// braces inside strings do not end the object
	Expect( ParseRuntimeManifest( "{\"runtime\":{\"name\":\"odd } name\",\"library_path\":\"r.so\"}}", name, library ), "braces in strings" );
	Expect( name == "odd } name", "a brace in a string stays in the name" );
	// not a runtime manifest
	Expect( !ParseRuntimeManifest( "{\"api_layer\":{\"name\":\"x\"}}", name, library ), "an API layer manifest is not a runtime" );
	Expect( !ParseRuntimeManifest( "{\"runtime\":{\"name\":\"x\"", name, library ), "a truncated manifest is refused" );
	Expect( openq4::xr::PathFileName( "C:\\a\\b.json" ) == "b.json" && openq4::xr::PathFileName( "/a/b.json" ) == "b.json",
		"file names follow either separator" );
}

void SearchOrder( void ) {
	using openq4::xr::ActiveRuntimeCandidates;
	// defaults: ~/.config, /etc/xdg, /etc; the architecture's name first
	const std::vector<std::string> plain = ActiveRuntimeCandidates( nullptr, "/home/p", nullptr, "x86_64" );
	const std::vector<std::string> expected = {
		"/home/p/.config/openxr/1/active_runtime.x86_64.json", "/etc/xdg/openxr/1/active_runtime.x86_64.json",
		"/etc/openxr/1/active_runtime.x86_64.json", "/home/p/.config/openxr/1/active_runtime.json",
		"/etc/xdg/openxr/1/active_runtime.json", "/etc/openxr/1/active_runtime.json" };
	Expect( plain == expected, "the loader's default directories, the architecture's name first" );
	// XDG_CONFIG_HOME replaces ~/.config; every XDG_CONFIG_DIRS entry is read, empty ones skipped
	const std::vector<std::string> set = ActiveRuntimeCandidates( "/x/conf/", "/home/p", "/a::/b/", "" );
	const std::vector<std::string> expectedSet = { "/x/conf/openxr/1/active_runtime.json", "/a/openxr/1/active_runtime.json",
		"/b/openxr/1/active_runtime.json", "/etc/openxr/1/active_runtime.json" };
	Expect( set == expectedSet, "XDG_CONFIG_HOME and XDG_CONFIG_DIRS replace the defaults" );
	// empty variables take the defaults; no home leaves only the system directories
	const std::vector<std::string> empty = ActiveRuntimeCandidates( "", nullptr, "", nullptr );
	const std::vector<std::string> expectedEmpty = { "/etc/xdg/openxr/1/active_runtime.json", "/etc/openxr/1/active_runtime.json" };
	Expect( empty == expectedEmpty, "empty variables take the defaults" );
}

} // namespace

int main( void ) {
	Manifests();
	SearchOrder();
	if ( failures != 0 ) {
		std::fprintf( stderr, "OpenXR runtime probe: %d failures\n", failures );
		return 1;
	}
	std::printf( "OpenXR runtime probe: manifests and search order passed\n" );
	return 0;
}
