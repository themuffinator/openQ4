// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EditorState.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace oq4editor {

namespace {

std::string Lower( std::string text ) {
	for ( char &c : text ) { if ( c >= 'A' && c <= 'Z' ) { c = static_cast<char>( c - 'A' + 'a' ); } }
	return text;
}

bool ReadDouble( const std::string &text, double &value ) {
	char *end = nullptr;
	const double parsed = std::strtod( text.c_str(), &end );
	if ( end == text.c_str() || !std::isfinite( parsed ) ) { return false; }
	while ( *end == ' ' ) { ++end; }
	if ( *end ) { return false; }
	value = parsed;
	return true;
}

}

DockLayout::DockLayout() {
	docks[Browser] = lastDock[Browser] = Left;
	docks[Inspector] = lastDock[Inspector] = Right;
	docks[Tools] = lastDock[Tools] = Right;
	docks[Problems] = lastDock[Problems] = Bottom;
	sizes[Left] = 154; sizes[Right] = 188; sizes[Bottom] = 100; sizes[Hidden] = 0;
	active[Left] = Browser; active[Right] = Inspector; active[Bottom] = Problems; active[Hidden] = Browser;
}

const char *DockLayout::PanelName( Panel panel ) {
	static const char *names[] = { "browser", "inspector", "tools", "problems" };
	return panel >= 0 && panel < PanelCount ? names[panel] : "";
}

const char *DockLayout::DockName( Dock dock ) {
	static const char *names[] = { "left", "right", "bottom", "hidden" };
	return dock >= 0 && dock < DockCount ? names[dock] : "";
}

bool DockLayout::Allowed( Panel panel, Dock dock ) {
	if ( panel < 0 || panel >= PanelCount || dock < 0 || dock >= DockCount ) { return false; }
	// Browser and inspector content needs a full-height column.
	return dock != Bottom || panel == Problems;
}

void DockLayout::FixActive() {
	for ( int d = 0; d < Hidden; ++d ) {
		if ( docks[active[d]] == d ) { continue; }
		for ( int p = 0; p < PanelCount; ++p ) { if ( docks[p] == d ) { active[d] = static_cast<Panel>( p ); break; } }
	}
}

bool DockLayout::Move( Panel panel, Dock dock ) {
	if ( !Allowed( panel, dock ) ) { return false; }
	if ( dock != Hidden && docks[panel] != dock ) {
		int sharing = 0;
		for ( int p = 0; p < PanelCount; ++p ) { if ( docks[p] == dock ) { ++sharing; } }
		if ( sharing >= MaxPerDock ) { return false; }
	}
	if ( dock != Hidden ) { lastDock[panel] = dock; active[dock] = panel; }
	docks[panel] = dock;
	FixActive();
	return true;
}

void DockLayout::Activate( Panel panel ) {
	if ( panel >= 0 && panel < PanelCount && docks[panel] != Hidden ) { active[docks[panel]] = panel; }
}

void DockLayout::Toggle( Panel panel ) {
	if ( panel < 0 || panel >= PanelCount ) { return; }
	Move( panel, docks[panel] == Hidden ? lastDock[panel] : Hidden );
}

void DockLayout::Resize( Dock dock, float delta ) {
	if ( !std::isfinite( delta ) ) { return; }
	if ( dock == Left || dock == Right ) { sizes[dock] = std::clamp( sizes[dock] + delta, MinSide, MaxSide ); }
	else if ( dock == Bottom ) { sizes[dock] = std::clamp( sizes[dock] + delta, MinBottom, MaxBottom ); }
}

std::string DockLayout::Serialize() const {
	std::string text;
	for ( int p = 0; p < PanelCount; ++p ) {
		text += std::string( PanelName( static_cast<Panel>( p ) ) ) + "=" + DockName( docks[p] ) + ( docks[p] == Hidden ? std::string( "/" ) + DockName( lastDock[p] ) : "" ) + ";";
	}
	for ( int d = 0; d < Hidden; ++d ) {
		text += std::string( "size." ) + DockName( static_cast<Dock>( d ) ) + "=" + std::to_string( static_cast<int>( std::lround( sizes[d] ) ) ) + ";";
		text += std::string( "tab." ) + DockName( static_cast<Dock>( d ) ) + "=" + PanelName( active[d] ) + ";";
	}
	return text;
}

bool DockLayout::Parse( const std::string &text ) {
	DockLayout parsed;
	for ( int p = 0; p < PanelCount; ++p ) { parsed.docks[p] = Hidden; } // assigned below, then validated
	bool assigned[PanelCount] = {};
	std::stringstream stream( text );
	std::string item;
	bool any = false;
	const auto panelOf = []( const std::string &name, Panel &panel ) {
		for ( int p = 0; p < PanelCount; ++p ) { if ( name == PanelName( static_cast<Panel>( p ) ) ) { panel = static_cast<Panel>( p ); return true; } }
		return false;
	};
	const auto dockOf = []( const std::string &name, Dock &dock ) {
		for ( int d = 0; d < DockCount; ++d ) { if ( name == DockName( static_cast<Dock>( d ) ) ) { dock = static_cast<Dock>( d ); return true; } }
		return false;
	};
	while ( std::getline( stream, item, ';' ) ) {
		const auto equals = item.find( '=' );
		if ( equals == std::string::npos ) { continue; }
		const std::string key = Lower( item.substr( 0, equals ) ), value = Lower( item.substr( equals + 1 ) );
		Panel panel;
		Dock dock;
		if ( panelOf( key, panel ) ) {
			const auto slash = value.find( '/' );
			Dock remembered;
			if ( !dockOf( value.substr( 0, slash ), dock ) || !Allowed( panel, dock ) ) { return false; }
			parsed.docks[panel] = dock;
			assigned[panel] = true;
			if ( dock != Hidden ) { parsed.lastDock[panel] = dock; }
			else if ( slash != std::string::npos && dockOf( value.substr( slash + 1 ), remembered ) && remembered != Hidden && Allowed( panel, remembered ) ) {
				parsed.lastDock[panel] = remembered;
			}
			any = true;
		} else if ( key.compare( 0, 5, "size." ) == 0 && dockOf( key.substr( 5 ), dock ) && dock != Hidden ) {
			double size;
			if ( !ReadDouble( value, size ) ) { return false; }
			parsed.sizes[dock] = static_cast<float>( size );
			parsed.Resize( dock, 0 );
			any = true;
		} else if ( key.compare( 0, 4, "tab." ) == 0 && dockOf( key.substr( 4 ), dock ) && dock != Hidden && panelOf( value, panel ) ) {
			parsed.active[dock] = panel;
		}
	}
	if ( !any ) { return false; }
	const DockLayout defaults;
	int perDock[DockCount] = {};
	for ( int p = 0; p < PanelCount; ++p ) {
		if ( !assigned[p] ) { parsed.docks[p] = defaults.docks[p]; }
		++perDock[parsed.docks[p]];
	}
	for ( int d = 0; d < Hidden; ++d ) { if ( perDock[d] > MaxPerDock ) { return false; } }
	parsed.FixActive();
	*this = parsed;
	return true;
}

DockLayout::Result DockLayout::Compute( const Rect &area ) const {
	Result result;
	bool used[DockCount] = {};
	int count[DockCount] = {};
	for ( int p = 0; p < PanelCount; ++p ) { used[docks[p]] = true; ++count[docks[p]]; }
	const float gap = 8;
	const float left = used[Left] ? sizes[Left] : 0, right = used[Right] ? sizes[Right] : 0;
	Rect dockRect[DockCount];
	dockRect[Left] = { area.x, area.y, left, area.h };
	dockRect[Right] = { area.x + area.w - right, area.y, right, area.h };
	const float centerX = area.x + ( used[Left] ? left + gap : 0 );
	const float centerW = area.w - ( used[Left] ? left + gap : 0 ) - ( used[Right] ? right + gap : 0 );
	const float bottom = used[Bottom] ? sizes[Bottom] : 0;
	dockRect[Bottom] = { centerX, area.y + area.h - bottom, centerW, bottom };
	result.views = { centerX, area.y, centerW, area.h - ( used[Bottom] ? bottom + gap : 0 ) };
	result.leftSplitter = used[Left] ? Rect{ area.x + left, area.y, gap, area.h } : Rect();
	result.rightSplitter = used[Right] ? Rect{ area.x + area.w - right - gap, area.y, gap, area.h } : Rect();
	result.bottomSplitter = used[Bottom] ? Rect{ centerX, area.y + area.h - bottom - gap, centerW, gap } : Rect();
	int slot[DockCount] = {};
	for ( int p = 0; p < PanelCount; ++p ) {
		const Dock dock = docks[p];
		PanelPlacement &placement = result.panels[p];
		if ( dock == Hidden ) { continue; }
		const Rect &r = dockRect[dock];
		const float tabW = r.w / static_cast<float>( count[dock] );
		placement.tab = { r.x + tabW * slot[dock]++, r.y, tabW, TabHeight };
		placement.tabVisible = true;
		placement.visible = active[dock] == p;
		placement.rect = { r.x, r.y + TabHeight + 2, r.w, r.h - TabHeight - 2 };
	}
	return result;
}

std::string ViewState::Serialize() const {
	std::ostringstream out;
	out << "camera " << FormatNumber( target.x ) << ' ' << FormatNumber( target.y ) << ' ' << FormatNumber( target.z ) << ' '
		<< FormatNumber( yaw ) << ' ' << FormatNumber( pitch ) << ' ' << FormatNumber( distance ) << '\n';
	out << "ortho " << FormatNumber( orthoCenter.x ) << ' ' << FormatNumber( orthoCenter.y ) << ' ' << FormatNumber( orthoCenter.z ) << ' ' << FormatNumber( orthoScale ) << '\n';
	out << "mode " << mode << '\n' << "quad " << ( quad ? 1 : 0 ) << '\n';
	out << "grid " << FormatNumber( grid ) << '\n' << "texturelock " << ( textureLock ? 1 : 0 ) << '\n';
	for ( const auto &layer : hiddenLayers ) { out << "hidden " << layer << '\n'; }
	for ( const auto &layer : lockedLayers ) { out << "locked " << layer << '\n'; }
	for ( const auto &item : selection ) { out << "select " << item << '\n'; }
	return out.str();
}

bool ViewState::Parse( const std::string &text ) {
	ViewState parsed;
	std::istringstream in( text );
	std::string line;
	bool camera = false;
	while ( std::getline( in, line ) ) {
		if ( !line.empty() && line.back() == '\r' ) { line.pop_back(); }
		const auto space = line.find( ' ' );
		if ( space == std::string::npos ) { continue; }
		const std::string key = line.substr( 0, space ), value = line.substr( space + 1 );
		std::istringstream fields( value );
		if ( key == "camera" ) {
			if ( !( fields >> parsed.target.x >> parsed.target.y >> parsed.target.z >> parsed.yaw >> parsed.pitch >> parsed.distance ) ) { return false; }
			camera = true;
		} else if ( key == "ortho" ) {
			if ( !( fields >> parsed.orthoCenter.x >> parsed.orthoCenter.y >> parsed.orthoCenter.z >> parsed.orthoScale ) ) { return false; }
		} else if ( key == "mode" ) { fields >> parsed.mode; }
		else if ( key == "quad" ) { int q = 0; fields >> q; parsed.quad = q != 0; }
		else if ( key == "grid" ) { fields >> parsed.grid; }
		else if ( key == "texturelock" ) { int l = 1; fields >> l; parsed.textureLock = l != 0; }
		else if ( key == "hidden" && !value.empty() ) { parsed.hiddenLayers.insert( value ); }
		else if ( key == "locked" && !value.empty() ) { parsed.lockedLayers.insert( value ); }
		else if ( key == "select" && !value.empty() && parsed.selection.size() < 4096 ) { parsed.selection.push_back( value ); }
	}
	if ( !camera || !parsed.target.Finite() || !std::isfinite( parsed.yaw ) || !std::isfinite( parsed.pitch ) ||
		!( parsed.distance > 0 ) || !std::isfinite( parsed.distance ) || !( parsed.orthoScale > 0 ) || !std::isfinite( parsed.orthoScale ) ) { return false; }
	parsed.mode = std::clamp( parsed.mode, 0, 3 );
	parsed.pitch = std::clamp( parsed.pitch, -85.0, 85.0 );
	parsed.distance = std::clamp( parsed.distance, 32.0, 60000.0 );
	parsed.orthoScale = std::clamp( parsed.orthoScale, 1.0 / 64.0, 512.0 );
	if ( !( parsed.grid >= 0.125 && parsed.grid <= 1024 ) ) { parsed.grid = 8; }
	parsed.valid = true;
	*this = parsed;
	return true;
}

std::string ViewStatePath( const std::string &mapPath ) {
	return "editor_experimental/views/" + ( mapPath.empty() ? std::string( "unnamed.map" ) : mapPath ) + ".view";
}

const char *AssetIndex::KindName( Kind kind ) {
	static const char *names[] = { "entityDef", "material", "model", "sound", "map" };
	return kind >= 0 && kind < KindCount ? names[kind] : "";
}

void AssetIndex::Begin( const std::vector<std::pair<Kind, Source>> &sources ) {
	pending = sources;
	sourceIndex = itemIndex = 0;
	for ( auto &list : names ) { list.clear(); }
	for ( auto &set : lookup ) { set.clear(); }
	stats = Stats();
	stats.complete = pending.empty();
}

bool AssetIndex::Step( std::size_t budget ) {
	if ( stats.complete ) { return true; }
	++stats.steps;
	while ( budget-- > 0 && sourceIndex < pending.size() ) {
		std::string name;
		const Kind kind = pending[sourceIndex].first;
		if ( itemIndex >= MaxEntries * 4 ) { stats.truncated = true; ++sourceIndex; itemIndex = 0; continue; }
		if ( !pending[sourceIndex].second( itemIndex++, name ) ) { ++sourceIndex; itemIndex = 0; continue; }
		if ( name.empty() ) { continue; }
		if ( stats.entries >= MaxEntries || stats.bytes + name.size() > MaxBytes ) { stats.truncated = true; sourceIndex = pending.size(); break; }
		if ( !lookup[kind].insert( Lower( name ) ).second ) { continue; }
		names[kind].push_back( name );
		++stats.entries;
		stats.bytes += name.size() + sizeof( std::string ) * 2;
	}
	if ( sourceIndex >= pending.size() ) { stats.complete = true; pending.clear(); }
	return stats.complete;
}

void AssetIndex::Cancel() {
	pending.clear();
	sourceIndex = itemIndex = 0;
	stats.cancelled = true;
	stats.complete = true;
}

std::vector<std::string> AssetIndex::Search( Kind kind, const std::string &filter, std::size_t limit ) const {
	std::vector<std::string> result;
	if ( kind < 0 || kind >= KindCount ) { return result; }
	const std::string needle = Lower( filter );
	for ( const auto &name : names[kind] ) {
		if ( result.size() >= limit ) { break; }
		if ( needle.empty() || Lower( name ).find( needle ) != std::string::npos ) { result.push_back( name ); }
	}
	return result;
}

bool AssetIndex::Contains( Kind kind, const std::string &name ) const {
	return kind >= 0 && kind < KindCount && lookup[kind].count( Lower( name ) ) != 0;
}

}
