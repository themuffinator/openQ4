// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_EDITOR_MAP_GEOMETRY_H
#define OPENQ4_EDITOR_MAP_GEOMETRY_H

#include <cstddef>
#include <string>
#include <vector>

// Typed adapters over the lossless primitive source kept by MapDocument.
// Parsing records the byte span of every numeric token. Serializing rewrites
// only the numbers whose value changed, so an untouched or partially edited
// primitive keeps its comments, spacing, number spelling and material text.
// Coordinates follow idMapFile: brushDef3 planes are stored relative to the
// owning entity's "origin", while patch control points are stored in world
// space (the loader subtracts the origin). Transforms take that into account.
namespace oq4editor {

struct Vec3 {
	double x = 0, y = 0, z = 0;
	Vec3() = default;
	Vec3( double ax, double ay, double az ) : x( ax ), y( ay ), z( az ) {}
	double &operator[]( int i ) { return i == 0 ? x : i == 1 ? y : z; }
	double operator[]( int i ) const { return i == 0 ? x : i == 1 ? y : z; }
	Vec3 operator+( const Vec3 &o ) const { return Vec3( x + o.x, y + o.y, z + o.z ); }
	Vec3 operator-( const Vec3 &o ) const { return Vec3( x - o.x, y - o.y, z - o.z ); }
	Vec3 operator-() const { return Vec3( -x, -y, -z ); }
	Vec3 operator*( double s ) const { return Vec3( x * s, y * s, z * s ); }
	double Dot( const Vec3 &o ) const { return x * o.x + y * o.y + z * o.z; }
	Vec3 Cross( const Vec3 &o ) const { return Vec3( y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x ); }
	double Length() const;
	bool Finite() const;
};

// Row-major 3x3 matrix plus translation: p' = m * p + t.
struct Affine {
	double m[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
	Vec3 t;
	Vec3 Apply( const Vec3 &p ) const;
	Vec3 Linear( const Vec3 &p ) const;
	double Determinant() const;
	bool Invert( Affine &inverse ) const;
	bool IsIdentity() const;
	bool IsRigid() const; // orthonormal linear part with determinant +1
	static Affine Translation( const Vec3 &delta );
	// Rotation about a principal axis (0=X, 1=Y, 2=Z) through a pivot.
	static Affine Rotation( int axis, double degrees, const Vec3 &pivot );
	static Affine Scale( const Vec3 &factors, const Vec3 &pivot );
};
Affine operator*( const Affine &a, const Affine &b ); // apply b, then a

// Shortest fixed-point spelling that reads back as the same float.
std::string FormatNumber( double value );
double SnapValue( double value, double grid );

struct NumberRef {
	std::size_t begin = 0, end = 0; // span in the primitive source
	double value = 0, original = 0;
	bool Changed() const { return value != original; }
};

struct BrushSide {
	NumberRef plane[4];   // a b c d with a*x + b*y + c*z + d = 0, normal outward
	NumberRef texture[6]; // ( ( m00 m01 m02 ) ( m10 m11 m12 ) )
	std::string material;
	std::size_t materialBegin = 0, materialEnd = 0;
	bool materialQuoted = true;
	Vec3 Normal() const { return Vec3( plane[0].value, plane[1].value, plane[2].value ); }
	double D() const { return plane[3].value; }
};

struct PatchPoint { NumberRef v[5]; }; // x y z s t

class Primitive {
public:
	enum class Kind { Unsupported, Brush, Patch };
	bool Parse( const std::string &text, std::string &error );
	std::string Serialize() const;
	bool Modified() const;
	Kind GetKind() const { return kind; }
	const std::string &Source() const { return source; }
	const std::string &Header() const { return header; } // brushDef3, patchDef2, ...
	std::vector<BrushSide> &Sides() { return sides; }
	const std::vector<BrushSide> &Sides() const { return sides; }
	int PatchWidth() const { return patchWidth; }
	int PatchHeight() const { return patchHeight; }
	const std::vector<PatchPoint> &Points() const { return points; }
	// World-space transform of a primitive owned by an entity whose stored
	// origin moves from oldOrigin to newOrigin. Texture lock keeps brush face
	// texture coordinates attached to the moved points; patches always carry
	// their own coordinates. Fails without modification on invalid results.
	bool Transform( const Affine &world, const Vec3 &oldOrigin, const Vec3 &newOrigin, bool textureLock, std::string &error );
	bool SetMaterial( const std::string &material, std::string &error );
	std::string Material() const;
	// Brush face polygons / patch control-net quads in world space.
	std::vector<std::vector<Vec3>> Polygons( const Vec3 &origin ) const;
	bool Bounds( const Vec3 &origin, Vec3 &mins, Vec3 &maxs ) const;
	// Validation without a renderer: reports why dmap would reject the shape.
	bool Validate( std::string &problem ) const;

private:
	bool ParseBrush( class GeometryLexer &lexer, std::string &error );
	bool ParsePatch( class GeometryLexer &lexer, bool def3, std::string &error );
	std::string source, header;
	Kind kind = Kind::Unsupported;
	std::vector<BrushSide> sides;
	std::vector<PatchPoint> points;
	std::vector<NumberRef> patchInfo;
	int patchWidth = 0, patchHeight = 0;
	std::size_t patchMaterialBegin = 0, patchMaterialEnd = 0;
	bool patchMaterialQuoted = true;
	std::string patchMaterial, replacementMaterial;
	bool materialChanged = false;
};

// Quake 4 planar texture axes, matching idMapFile's ComputeAxisBase.
void TextureAxisBase( const Vec3 &normal, Vec3 &s, Vec3 &t );
std::vector<Vec3> PlaneWinding( const std::vector<BrushSide> &sides, std::size_t side, const Vec3 &origin );

// New primitive text in the Quake 4 brushDef3/patchDef2 layout used by
// stock maps. Coordinates are world space; origin is the owning entity's.
std::string BoxBrushSource( const Vec3 &mins, const Vec3 &maxs, const std::string &material, const Vec3 &origin, const char *newline );
std::string FlatPatchSource( const Vec3 &mins, const Vec3 &maxs, const std::string &material, const char *newline );

}
#endif
