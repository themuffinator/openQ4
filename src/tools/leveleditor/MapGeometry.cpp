// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// The engine build force-includes idlib, which replaces snprintf with a
// diagnostic macro; this portable file formats with the C library.
#ifdef snprintf
#undef snprintf
#endif

namespace oq4editor {

double Vec3::Length() const { return std::sqrt( Dot( *this ) ); }
bool Vec3::Finite() const { return std::isfinite( x ) && std::isfinite( y ) && std::isfinite( z ); }

Vec3 Affine::Linear( const Vec3 &p ) const {
	return Vec3( m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z,
		m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z,
		m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z );
}
Vec3 Affine::Apply( const Vec3 &p ) const { return Linear( p ) + t; }

double Affine::Determinant() const {
	return m[0][0] * ( m[1][1] * m[2][2] - m[1][2] * m[2][1] ) -
		m[0][1] * ( m[1][0] * m[2][2] - m[1][2] * m[2][0] ) +
		m[0][2] * ( m[1][0] * m[2][1] - m[1][1] * m[2][0] );
}

bool Affine::Invert( Affine &inverse ) const {
	const double det = Determinant();
	if ( !std::isfinite( det ) || std::fabs( det ) < 1e-12 ) { return false; }
	const double s = 1.0 / det;
	inverse.m[0][0] = ( m[1][1] * m[2][2] - m[1][2] * m[2][1] ) * s;
	inverse.m[0][1] = ( m[0][2] * m[2][1] - m[0][1] * m[2][2] ) * s;
	inverse.m[0][2] = ( m[0][1] * m[1][2] - m[0][2] * m[1][1] ) * s;
	inverse.m[1][0] = ( m[1][2] * m[2][0] - m[1][0] * m[2][2] ) * s;
	inverse.m[1][1] = ( m[0][0] * m[2][2] - m[0][2] * m[2][0] ) * s;
	inverse.m[1][2] = ( m[0][2] * m[1][0] - m[0][0] * m[1][2] ) * s;
	inverse.m[2][0] = ( m[1][0] * m[2][1] - m[1][1] * m[2][0] ) * s;
	inverse.m[2][1] = ( m[0][1] * m[2][0] - m[0][0] * m[2][1] ) * s;
	inverse.m[2][2] = ( m[0][0] * m[1][1] - m[0][1] * m[1][0] ) * s;
	inverse.t = -inverse.Linear( t );
	return true;
}

bool Affine::IsIdentity() const {
	for ( int r = 0; r < 3; ++r ) {
		for ( int c = 0; c < 3; ++c ) { if ( m[r][c] != ( r == c ? 1.0 : 0.0 ) ) { return false; } }
	}
	return t.x == 0 && t.y == 0 && t.z == 0;
}

bool Affine::IsRigid() const {
	for ( int r = 0; r < 3; ++r ) {
		for ( int c = 0; c < 3; ++c ) {
			double dot = 0;
			for ( int k = 0; k < 3; ++k ) { dot += m[k][r] * m[k][c]; }
			if ( std::fabs( dot - ( r == c ? 1.0 : 0.0 ) ) > 1e-9 ) { return false; }
		}
	}
	return Determinant() > 0;
}

Affine Affine::Translation( const Vec3 &delta ) { Affine a; a.t = delta; return a; }

Affine Affine::Rotation( int axis, double degrees, const Vec3 &pivot ) {
	Affine a;
	// Exact quarter turns keep axial planes exactly axial.
	double c, s;
	const double turns = degrees / 90.0;
	if ( std::fabs( turns - std::round( turns ) ) < 1e-12 ) {
		static const double cosQ[4] = { 1, 0, -1, 0 }, sinQ[4] = { 0, 1, 0, -1 };
		const int q = ( ( static_cast<int>( std::llround( turns ) ) % 4 ) + 4 ) % 4;
		c = cosQ[q]; s = sinQ[q];
	} else {
		const double radians = degrees * 3.14159265358979323846 / 180.0;
		c = std::cos( radians ); s = std::sin( radians );
	}
	const int u = ( axis + 1 ) % 3, v = ( axis + 2 ) % 3;
	a.m[u][u] = c; a.m[u][v] = -s; a.m[v][u] = s; a.m[v][v] = c;
	a.t = pivot - a.Linear( pivot );
	return a;
}

Affine Affine::Scale( const Vec3 &factors, const Vec3 &pivot ) {
	Affine a;
	for ( int i = 0; i < 3; ++i ) { a.m[i][i] = factors[i]; }
	a.t = pivot - a.Linear( pivot );
	return a;
}

Affine operator*( const Affine &a, const Affine &b ) {
	Affine r;
	for ( int i = 0; i < 3; ++i ) {
		for ( int j = 0; j < 3; ++j ) {
			r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
		}
	}
	r.t = a.Linear( b.t ) + a.t;
	return r;
}

std::string FormatNumber( double value ) {
	if ( !std::isfinite( value ) ) { return "0"; }
	if ( std::fabs( value ) < 5e-11 ) { return "0"; }
	const float target = static_cast<float>( value );
	char buffer[64];
	// Fixed notation: the map lexer does not read exponents. The engine stores
	// map numbers as floats, so the shortest spelling of the same float loses
	// nothing a reload would keep.
	for ( int decimals = 0; decimals <= 10; ++decimals ) {
		std::snprintf( buffer, sizeof( buffer ), "%.*f", decimals, value );
		if ( static_cast<float>( std::strtod( buffer, nullptr ) ) == target ) { break; }
	}
	std::string text = buffer;
	if ( text.find( '.' ) != std::string::npos ) {
		while ( !text.empty() && text.back() == '0' ) { text.pop_back(); }
		if ( !text.empty() && text.back() == '.' ) { text.pop_back(); }
	}
	if ( text == "-0" ) { text = "0"; }
	return text;
}

double SnapValue( double value, double grid ) {
	if ( !( grid > 0 ) || !std::isfinite( value ) ) { return value; }
	return std::round( value / grid ) * grid;
}

void TextureAxisBase( const Vec3 &normal, Vec3 &s, Vec3 &t ) {
	// Mirrors ComputeAxisBase in idlib/MapFile.cpp, including its cleaning
	// threshold and float precision, so locked textures round-trip.
	const float nx = std::fabs( normal.x ) < 1e-6 ? 0.0f : static_cast<float>( normal.x );
	const float ny = std::fabs( normal.y ) < 1e-6 ? 0.0f : static_cast<float>( normal.y );
	const float nz = std::fabs( normal.z ) < 1e-6 ? 0.0f : static_cast<float>( normal.z );
	const double rotY = -std::atan2( static_cast<double>( nz ), std::sqrt( static_cast<double>( ny ) * ny + static_cast<double>( nx ) * nx ) );
	const double rotZ = std::atan2( static_cast<double>( ny ), static_cast<double>( nx ) );
	s = Vec3( -std::sin( rotZ ), std::cos( rotZ ), 0 );
	t = Vec3( -std::sin( rotY ) * std::cos( rotZ ), -std::sin( rotY ) * std::sin( rotZ ), -std::cos( rotY ) );
}

namespace {

struct GeoToken {
	std::size_t begin = 0, end = 0;
	std::string text;
	bool quoted = false;
};

bool IsNumber( const std::string &text, double &value ) {
	if ( text.empty() ) { return false; }
	char *end = nullptr;
	value = std::strtod( text.c_str(), &end );
	return end && *end == '\0' && std::isfinite( value );
}

double Clean( double value ) {
	if ( std::fabs( value ) < 1e-12 ) { return 0; }
	const double rounded = std::round( value );
	if ( std::fabs( value - rounded ) < 1e-7 * ( std::max )( 1.0, std::fabs( value ) ) ) { return rounded; }
	return value;
}

}

class GeometryLexer {
public:
	explicit GeometryLexer( const std::string &text ) : source( text ) {}
	bool Next( GeoToken &token ) {
		while ( position < source.size() ) {
			const char c = source[position];
			if ( c == ' ' || c == '\t' || c == '\r' || c == '\n' ) { ++position; continue; }
			if ( source.compare( position, 2, "//" ) == 0 ) {
				position = source.find( '\n', position );
				if ( position == std::string::npos ) { position = source.size(); }
				continue;
			}
			if ( source.compare( position, 2, "/*" ) == 0 ) {
				const auto end = source.find( "*/", position + 2 );
				if ( end == std::string::npos ) { position = source.size(); return false; }
				position = end + 2;
				continue;
			}
			break;
		}
		if ( position >= source.size() ) { return false; }
		token = GeoToken();
		token.begin = position;
		const char first = source[position++];
		if ( first == '"' ) {
			const auto end = source.find( '"', position );
			if ( end == std::string::npos ) { return false; }
			token.quoted = true;
			token.text = source.substr( position, end - position );
			position = end + 1;
		} else if ( first == '{' || first == '}' || first == '(' || first == ')' ) {
			token.text.assign( 1, first );
		} else {
			while ( position < source.size() ) {
				const char c = source[position];
				if ( c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '{' || c == '}' || c == '(' || c == ')' || c == '"' ) { break; }
				++position;
			}
			token.text = source.substr( token.begin, position - token.begin );
		}
		token.end = position;
		return true;
	}
	bool Peek( GeoToken &token ) {
		const auto saved = position;
		const bool ok = Next( token );
		position = saved;
		return ok;
	}
	bool Expect( const char *text ) {
		GeoToken token;
		return Next( token ) && !token.quoted && token.text == text;
	}
	bool Number( NumberRef &number ) {
		GeoToken token;
		if ( !Next( token ) || token.quoted || !IsNumber( token.text, number.value ) ) { return false; }
		number.begin = token.begin; number.end = token.end; number.original = number.value;
		return true;
	}
	std::size_t position = 0;
private:
	const std::string &source;
};

bool Primitive::Parse( const std::string &text, std::string &error ) {
	*this = Primitive();
	source = text;
	GeometryLexer lexer( source );
	GeoToken token;
	if ( !lexer.Expect( "{" ) || !lexer.Next( token ) ) { error = "primitive does not start with '{'"; return false; }
	header = token.text;
	if ( token.quoted ) { error = "unexpected string in primitive header"; return false; }
	if ( header == "brushDef3" ) { return ParseBrush( lexer, error ); }
	if ( header == "patchDef2" || header == "patchDef3" ) { return ParsePatch( lexer, header == "patchDef3", error ); }
	// brushDef/brushDef2 and Quake 3 style brushes stay losslessly stored but
	// are not editable by the geometry tools.
	kind = Kind::Unsupported;
	return true;
}

bool Primitive::ParseBrush( GeometryLexer &lexer, std::string &error ) {
	if ( !lexer.Expect( "{" ) ) { error = "brushDef3 needs '{'"; return false; }
	GeoToken token;
	while ( lexer.Peek( token ) && token.quoted ) {
		// Editor-only brush epairs precede the first side.
		lexer.Next( token );
		if ( !lexer.Next( token ) || !token.quoted ) { error = "brush epair needs a quoted value"; return false; }
	}
	while ( true ) {
		if ( !lexer.Next( token ) ) { error = "unterminated brushDef3"; return false; }
		if ( !token.quoted && token.text == "}" ) { break; }
		if ( token.quoted || token.text != "(" ) { error = "brush side must start with a plane"; return false; }
		BrushSide side;
		for ( auto &number : side.plane ) { if ( !lexer.Number( number ) ) { error = "invalid brush plane"; return false; } }
		if ( !lexer.Expect( ")" ) || !lexer.Expect( "(" ) ) { error = "invalid brush plane"; return false; }
		for ( int row = 0; row < 2; ++row ) {
			if ( !lexer.Expect( "(" ) ) { error = "invalid texture matrix"; return false; }
			for ( int i = 0; i < 3; ++i ) { if ( !lexer.Number( side.texture[row * 3 + i] ) ) { error = "invalid texture matrix"; return false; } }
			if ( !lexer.Expect( ")" ) ) { error = "invalid texture matrix"; return false; }
		}
		if ( !lexer.Expect( ")" ) || !lexer.Next( token ) || token.text == ")" || token.text == "(" || ( !token.quoted && token.text == "}" ) ) {
			error = "brush side needs a material"; return false;
		}
		side.material = token.text; side.materialQuoted = token.quoted;
		side.materialBegin = token.begin; side.materialEnd = token.end;
		// Doom 3 era sides may carry trailing content/flag integers.
		double ignored;
		while ( lexer.Peek( token ) && !token.quoted && IsNumber( token.text, ignored ) ) { lexer.Next( token ); }
		sides.push_back( side );
		if ( sides.size() > 1024 ) { error = "brush has more than 1024 sides"; return false; }
	}
	if ( !lexer.Expect( "}" ) ) { error = "unterminated brush primitive"; return false; }
	kind = Kind::Brush;
	return true;
}

bool Primitive::ParsePatch( GeometryLexer &lexer, bool def3, std::string &error ) {
	GeoToken token;
	if ( !lexer.Expect( "{" ) || !lexer.Next( token ) || token.text == "(" ) { error = "patch needs a material"; return false; }
	patchMaterial = token.text; patchMaterialQuoted = token.quoted;
	patchMaterialBegin = token.begin; patchMaterialEnd = token.end;
	if ( !lexer.Expect( "(" ) ) { error = "patch needs dimensions"; return false; }
	patchInfo.resize( def3 ? 7 : 5 );
	for ( auto &number : patchInfo ) { if ( !lexer.Number( number ) ) { error = "invalid patch dimensions"; return false; } }
	if ( !lexer.Expect( ")" ) ) { error = "invalid patch dimensions"; return false; }
	const double width = patchInfo[0].value, height = patchInfo[1].value;
	if ( width < 1 || height < 1 || width > 1024 || height > 1024 || width != std::floor( width ) || height != std::floor( height ) ) {
		error = "patch dimensions must be integers from 1 to 1024"; return false;
	}
	patchWidth = static_cast<int>( width ); patchHeight = static_cast<int>( height );
	if ( !lexer.Expect( "(" ) ) { error = "patch needs a control net"; return false; }
	points.reserve( static_cast<std::size_t>( patchWidth ) * patchHeight );
	for ( int i = 0; i < patchWidth; ++i ) {
		if ( !lexer.Expect( "(" ) ) { error = "invalid patch row"; return false; }
		for ( int j = 0; j < patchHeight; ++j ) {
			PatchPoint point;
			if ( !lexer.Expect( "(" ) ) { error = "invalid patch point"; return false; }
			for ( auto &number : point.v ) { if ( !lexer.Number( number ) ) { error = "invalid patch point"; return false; } }
			if ( !lexer.Expect( ")" ) ) { error = "invalid patch point"; return false; }
			points.push_back( point );
		}
		if ( !lexer.Expect( ")" ) ) { error = "invalid patch row"; return false; }
	}
	if ( !lexer.Expect( ")" ) || !lexer.Expect( "}" ) || !lexer.Expect( "}" ) ) { error = "unterminated patch"; return false; }
	kind = Kind::Patch;
	return true;
}

bool Primitive::Modified() const {
	if ( materialChanged ) { return true; }
	for ( const auto &side : sides ) {
		for ( const auto &n : side.plane ) { if ( n.Changed() ) { return true; } }
		for ( const auto &n : side.texture ) { if ( n.Changed() ) { return true; } }
	}
	for ( const auto &point : points ) { for ( const auto &n : point.v ) { if ( n.Changed() ) { return true; } } }
	return false;
}

std::string Primitive::Serialize() const {
	if ( !Modified() ) { return source; }
	struct Edit { std::size_t begin, end; std::string text; };
	std::vector<Edit> edits;
	const auto number = [&edits]( const NumberRef &n ) { if ( n.Changed() ) { edits.push_back( { n.begin, n.end, FormatNumber( n.value ) } ); } };
	const auto material = [&edits, this]( std::size_t begin, std::size_t end, bool quoted ) {
		if ( materialChanged ) { edits.push_back( { begin, end, quoted ? "\"" + replacementMaterial + "\"" : replacementMaterial } ); }
	};
	for ( const auto &side : sides ) {
		for ( const auto &n : side.plane ) { number( n ); }
		for ( const auto &n : side.texture ) { number( n ); }
		material( side.materialBegin, side.materialEnd, side.materialQuoted );
	}
	if ( kind == Kind::Patch ) { material( patchMaterialBegin, patchMaterialEnd, patchMaterialQuoted ); }
	for ( const auto &point : points ) { for ( const auto &n : point.v ) { number( n ); } }
	std::sort( edits.begin(), edits.end(), []( const Edit &a, const Edit &b ) { return a.begin < b.begin; } );
	std::string text;
	text.reserve( source.size() + edits.size() * 4 );
	std::size_t cursor = 0;
	for ( const auto &edit : edits ) {
		text.append( source, cursor, edit.begin - cursor );
		text += edit.text;
		cursor = edit.end;
	}
	text.append( source, cursor, std::string::npos );
	return text;
}

std::string Primitive::Material() const {
	if ( materialChanged ) { return replacementMaterial; }
	if ( kind == Kind::Patch ) { return patchMaterial; }
	return sides.empty() ? std::string() : sides.front().material;
}

bool Primitive::SetMaterial( const std::string &material, std::string &error ) {
	if ( kind == Kind::Unsupported ) { error = "this primitive format is not editable"; return false; }
	if ( material.empty() || material.size() > 256 || material.find_first_of( "\"\r\n(){} \t" ) != std::string::npos ) {
		error = "materials are 1 to 256 bytes without quotes, braces, parentheses or spaces"; return false;
	}
	replacementMaterial = material;
	materialChanged = true;
	return true;
}

std::vector<Vec3> PlaneWinding( const std::vector<BrushSide> &sides, std::size_t index, const Vec3 &origin ) {
	const Vec3 normal = sides[index].Normal();
	const double length = normal.Length();
	if ( !( length > 0.5 ) ) { return {}; }
	const Vec3 n = normal * ( 1.0 / length );
	const double d = sides[index].D() / length;
	// Largest basis axis picks a stable in-plane frame, as idWinding does.
	Vec3 up = std::fabs( n.z ) > std::fabs( n.x ) && std::fabs( n.z ) > std::fabs( n.y ) ? Vec3( 1, 0, 0 ) : Vec3( 0, 0, 1 );
	up = up - n * up.Dot( n );
	up = up * ( 1.0 / up.Length() );
	const Vec3 right = up.Cross( n );
	const Vec3 center = n * -d;
	const double size = 1048576.0;
	std::vector<Vec3> winding = {
		center - right * size + up * size, center + right * size + up * size,
		center + right * size - up * size, center - right * size - up * size };
	for ( std::size_t j = 0; j < sides.size() && !winding.empty(); ++j ) {
		if ( j == index ) { continue; }
		const Vec3 clipNormal = sides[j].Normal();
		const double clipLength = clipNormal.Length();
		if ( !( clipLength > 0.5 ) ) { return {}; }
		const Vec3 cn = clipNormal * ( 1.0 / clipLength );
		const double cd = sides[j].D() / clipLength;
		std::vector<Vec3> clipped;
		for ( std::size_t i = 0; i < winding.size(); ++i ) {
			const Vec3 &a = winding[i], &b = winding[( i + 1 ) % winding.size()];
			const double da = cn.Dot( a ) + cd, db = cn.Dot( b ) + cd;
			const double epsilon = 0.01;
			if ( da <= epsilon ) { clipped.push_back( a ); }
			if ( ( da > epsilon && db < -epsilon ) || ( da < -epsilon && db > epsilon ) ) {
				const double f = da / ( da - db );
				clipped.push_back( a + ( b - a ) * f );
			}
		}
		winding.swap( clipped );
		if ( winding.size() < 3 ) { winding.clear(); }
	}
	for ( auto &point : winding ) { point = point + origin; }
	return winding;
}

std::vector<std::vector<Vec3>> Primitive::Polygons( const Vec3 &origin ) const {
	std::vector<std::vector<Vec3>> result;
	if ( kind == Kind::Brush ) {
		for ( std::size_t i = 0; i < sides.size(); ++i ) {
			auto winding = PlaneWinding( sides, i, origin );
			if ( winding.size() >= 3 ) { result.push_back( std::move( winding ) ); }
		}
	} else if ( kind == Kind::Patch ) {
		const auto at = [this]( int i, int j ) {
			const auto &p = points[static_cast<std::size_t>( i ) * patchHeight + j];
			return Vec3( p.v[0].value, p.v[1].value, p.v[2].value );
		};
		for ( int i = 0; i + 1 < patchWidth; ++i ) {
			for ( int j = 0; j + 1 < patchHeight; ++j ) {
				result.push_back( { at( i, j ), at( i + 1, j ), at( i + 1, j + 1 ), at( i, j + 1 ) } );
			}
		}
		if ( result.empty() && !points.empty() ) {
			std::vector<Vec3> line;
			for ( int i = 0; i < patchWidth; ++i ) { for ( int j = 0; j < patchHeight; ++j ) { line.push_back( at( i, j ) ); } }
			result.push_back( line );
		}
	}
	return result;
}

bool Primitive::Bounds( const Vec3 &origin, Vec3 &mins, Vec3 &maxs ) const {
	bool any = false;
	for ( const auto &polygon : Polygons( origin ) ) {
		for ( const auto &p : polygon ) {
			if ( !any ) { mins = maxs = p; any = true; continue; }
			for ( int i = 0; i < 3; ++i ) { mins[i] = ( std::min )( mins[i], p[i] ); maxs[i] = ( std::max )( maxs[i], p[i] ); }
		}
	}
	return any;
}

bool Primitive::Validate( std::string &problem ) const {
	if ( kind == Kind::Unsupported ) { problem.clear(); return true; }
	if ( kind == Kind::Patch ) {
		if ( patchWidth < 3 || patchHeight < 3 || patchWidth % 2 == 0 || patchHeight % 2 == 0 ) {
			problem = "patch control net must be odd and at least 3x3"; return false;
		}
		for ( const auto &point : points ) {
			for ( const auto &n : point.v ) { if ( !std::isfinite( n.value ) || std::fabs( n.value ) > 1e7 ) { problem = "patch has an invalid control point"; return false; } }
		}
		return true;
	}
	if ( sides.size() < 4 ) { problem = "brush has fewer than four sides"; return false; }
	for ( const auto &side : sides ) {
		const double length = side.Normal().Length();
		if ( !std::isfinite( length ) || std::fabs( length - 1.0 ) > 0.01 || !std::isfinite( side.D() ) ) {
			problem = "brush has a non-unit or invalid plane"; return false;
		}
	}
	int faces = 0;
	for ( std::size_t i = 0; i < sides.size(); ++i ) { if ( PlaneWinding( sides, i, Vec3() ).size() >= 3 ) { ++faces; } }
	if ( faces < 4 ) { problem = "brush encloses no volume"; return false; }
	return true;
}

namespace {

bool Solve3( const double a[3][3], const double b[3], double x[3] ) {
	const double det = a[0][0] * ( a[1][1] * a[2][2] - a[1][2] * a[2][1] ) -
		a[0][1] * ( a[1][0] * a[2][2] - a[1][2] * a[2][0] ) +
		a[0][2] * ( a[1][0] * a[2][1] - a[1][1] * a[2][0] );
	if ( !std::isfinite( det ) || std::fabs( det ) < 1e-12 ) { return false; }
	for ( int c = 0; c < 3; ++c ) {
		double m[3][3];
		for ( int r = 0; r < 3; ++r ) { for ( int k = 0; k < 3; ++k ) { m[r][k] = k == c ? b[r] : a[r][k]; } }
		x[c] = ( m[0][0] * ( m[1][1] * m[2][2] - m[1][2] * m[2][1] ) -
			m[0][1] * ( m[1][0] * m[2][2] - m[1][2] * m[2][0] ) +
			m[0][2] * ( m[1][0] * m[2][1] - m[1][1] * m[2][0] ) ) / det;
	}
	return true;
}

}

bool Primitive::Transform( const Affine &world, const Vec3 &oldOrigin, const Vec3 &newOrigin, bool textureLock, std::string &error ) {
	if ( kind == Kind::Unsupported ) { error = "primitive format '" + header + "' is not editable"; return false; }
	if ( !( world.Determinant() > 1e-9 ) ) { error = "transform must preserve orientation and volume"; return false; }
	Affine inverse;
	if ( !world.Invert( inverse ) ) { error = "transform is not invertible"; return false; }
	if ( kind == Kind::Patch ) {
		std::vector<PatchPoint> moved = points;
		for ( auto &point : moved ) {
			const Vec3 p = world.Apply( Vec3( point.v[0].value, point.v[1].value, point.v[2].value ) );
			if ( !p.Finite() || std::fabs( p.x ) > 1e7 || std::fabs( p.y ) > 1e7 || std::fabs( p.z ) > 1e7 ) {
				error = "transform moves a patch outside the map limits"; return false;
			}
			for ( int i = 0; i < 3; ++i ) { point.v[i].value = Clean( p[i] ); }
		}
		points.swap( moved );
		return true;
	}
	std::vector<BrushSide> moved = sides;
	for ( auto &side : moved ) {
		const Vec3 normal = side.Normal();
		const double length = normal.Length();
		if ( !( length > 0.5 ) ) { error = "brush has a degenerate plane"; return false; }
		const Vec3 n = normal * ( 1.0 / length );
		// World plane: n.p + dw = 0, with brushDef3 planes stored entity-local.
		const double dw = side.D() / length - n.Dot( oldOrigin );
		// Normals transform by the inverse transpose.
		Vec3 n2( inverse.m[0][0] * n.x + inverse.m[1][0] * n.y + inverse.m[2][0] * n.z,
			inverse.m[0][1] * n.x + inverse.m[1][1] * n.y + inverse.m[2][1] * n.z,
			inverse.m[0][2] * n.x + inverse.m[1][2] * n.y + inverse.m[2][2] * n.z );
		const double length2 = n2.Length();
		if ( !( length2 > 1e-12 ) ) { error = "transform collapses a brush plane"; return false; }
		n2 = n2 * ( 1.0 / length2 );
		for ( int i = 0; i < 3; ++i ) { n2[i] = Clean( n2[i] ); }
		n2 = n2 * ( 1.0 / n2.Length() );
		const Vec3 p0 = n * -dw;
		const Vec3 q0 = world.Apply( p0 );
		const double dw2 = -n2.Dot( q0 );
		const double d2 = Clean( dw2 + n2.Dot( newOrigin ) );
		if ( !std::isfinite( d2 ) || std::fabs( d2 ) > 1e7 ) { error = "transform moves a brush outside the map limits"; return false; }
		if ( textureLock ) {
			Vec3 s, t, s2, t2;
			TextureAxisBase( n, s, t );
			TextureAxisBase( n2, s2, t2 );
			const Vec3 samples[3] = { p0, p0 + s * 64.0, p0 + t * 64.0 };
			double a[3][3];
			for ( int i = 0; i < 3; ++i ) {
				const Vec3 q = world.Apply( samples[i] );
				a[i][0] = q.Dot( s2 ); a[i][1] = q.Dot( t2 ); a[i][2] = 1.0;
			}
			for ( int row = 0; row < 2; ++row ) {
				const double m0 = side.texture[row * 3].value, m1 = side.texture[row * 3 + 1].value, m2 = side.texture[row * 3 + 2].value;
				double b[3], x[3];
				for ( int i = 0; i < 3; ++i ) { b[i] = samples[i].Dot( s ) * m0 + samples[i].Dot( t ) * m1 + m2; }
				if ( !Solve3( a, b, x ) ) { error = "texture lock could not solve a face"; return false; }
				for ( int i = 0; i < 3; ++i ) {
					if ( !std::isfinite( x[i] ) ) { error = "texture lock produced an invalid matrix"; return false; }
					// Texture coordinates repeat: keep the shift within half a
					// repeat of the original so moves do not grow it without bound.
					double value = Clean( x[i] );
					if ( i == 2 ) { value = Clean( value - std::round( value - m2 ) ); }
					side.texture[row * 3 + i].value = value;
				}
			}
		}
		// Keep the source's spelling when a value round-trips unchanged.
		for ( int i = 0; i < 3; ++i ) {
			side.plane[i].value = static_cast<float>( n2[i] ) == static_cast<float>( side.plane[i].original ) ? side.plane[i].original : n2[i];
		}
		side.plane[3].value = static_cast<float>( d2 ) == static_cast<float>( side.plane[3].original ) ? side.plane[3].original : d2;
		for ( auto &n3 : side.texture ) {
			if ( static_cast<float>( n3.value ) == static_cast<float>( n3.original ) ) { n3.value = n3.original; }
		}
	}
	sides.swap( moved );
	return true;
}

std::string BoxBrushSource( const Vec3 &mins, const Vec3 &maxs, const std::string &material, const Vec3 &origin, const char *newline ) {
	const Vec3 lo = mins - origin, hi = maxs - origin;
	const struct { Vec3 n; double d; } planes[6] = {
		{ Vec3( 0, 0, -1 ), lo.z }, { Vec3( 0, 0, 1 ), -hi.z },
		{ Vec3( 0, -1, 0 ), lo.y }, { Vec3( 1, 0, 0 ), -hi.x },
		{ Vec3( 0, 1, 0 ), -hi.y }, { Vec3( -1, 0, 0 ), lo.x } };
	std::string text = std::string( "{" ) + newline + " brushDef3" + newline + " {" + newline;
	for ( const auto &plane : planes ) {
		text += "  ( " + FormatNumber( plane.n.x ) + " " + FormatNumber( plane.n.y ) + " " + FormatNumber( plane.n.z ) + " " +
			FormatNumber( plane.d ) + " ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) \"" + material + "\"" + newline;
	}
	text += std::string( " }" ) + newline + "}";
	return text;
}

std::string FlatPatchSource( const Vec3 &mins, const Vec3 &maxs, const std::string &material, const char *newline ) {
	std::string text = std::string( "{" ) + newline + " patchDef2" + newline + " {" + newline + "  \"" + material + "\"" + newline +
		"  ( 3 3 0 0 0 )" + newline + "  (" + newline;
	for ( int i = 0; i < 3; ++i ) {
		text += "   ( ";
		for ( int j = 0; j < 3; ++j ) {
			const double fx = i * 0.5, fy = j * 0.5;
			const Vec3 p( mins.x + ( maxs.x - mins.x ) * fx, mins.y + ( maxs.y - mins.y ) * fy, maxs.z );
			text += "( " + FormatNumber( p.x ) + " " + FormatNumber( p.y ) + " " + FormatNumber( p.z ) + " " +
				FormatNumber( fx ) + " " + FormatNumber( fy ) + " ) ";
		}
		text += std::string( ")" ) + newline;
	}
	text += std::string( "  )" ) + newline + " }" + newline + "}";
	return text;
}

}
