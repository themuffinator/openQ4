#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#define ID_INLINE inline
using byte = unsigned char;
using dword = std::uint32_t;
class idVec2;
class idVec3;
class idVec4;
// The legacy math headers do not build cleanly at /W4; the codec header does.
#ifdef _MSC_VER
#pragma warning( push, 1 )
#endif
#include "src/idlib/math/Math.h"

const float idMath::HALF_PI = 1.57079632679489661923f;
const float idMath::SQRT_1OVER2 = 0.70710678118654752440f;
const float idMath::M_DEG2RAD = 0.01745329251994329577f;

// Satisfies the unused spherical specialization in Interpolate.h.
struct idQuat {
	idQuat operator+( const idQuat & ) const { std::abort(); }
	idQuat operator-( const idQuat & ) const { std::abort(); }
	idQuat operator*( float ) const { std::abort(); }
	void Slerp( const idQuat &, const idQuat &, float ) { std::abort(); }
};
#include "src/idlib/math/Extrapolate.h"
#include "src/idlib/math/Interpolate.h"
#ifdef _MSC_VER
#pragma warning( pop )
#endif
#include "src/idlib/SaveCodec.h"

/*
Golden-byte tests for the portable savegame codecs (roadmap Q4-06). The
structures below mirror the production field names; the game modules
instantiate the same templates with the real types. Each case encodes known
values through a writer that emits the primitives exactly as idSaveGame does
(little-endian int32 and binary32, one-byte bool), compares the bytes with a
fixed hex string, decodes them and checks every field. The hex strings are the
format: they must match on every platform.
*/

static int failures = 0;

static void Check( bool condition, const char *label ) {
	if ( !condition ) {
		std::fprintf( stderr, "FAIL: %s\n", label );
		++failures;
	}
}

class Writer {
public:
	std::vector<unsigned char> bytes;
	void WriteInt( int value ) { Put( static_cast<std::uint32_t>( value ) ); }
	void WriteFloat( float value ) { std::uint32_t bits; std::memcpy( &bits, &value, 4 ); Put( bits ); }
	void WriteBool( bool value ) { bytes.push_back( value ? 1 : 0 ); }
	void WriteByte( unsigned char value ) { bytes.push_back( value ); }
private:
	void Put( std::uint32_t value ) {
		for ( int i = 0; i < 4; i++ ) {
			bytes.push_back( static_cast<unsigned char>( value >> ( 8 * i ) ) );
		}
	}
};

class Reader {
public:
	explicit Reader( const std::vector<unsigned char> &data ) : bytes( data ) {}
	void ReadInt( int &value ) { value = static_cast<int>( Get() ); }
	void ReadFloat( float &value ) { std::uint32_t bits = Get(); std::memcpy( &value, &bits, 4 ); }
	void ReadBool( bool &value ) {
		Need( 1 );
		const unsigned char raw = bytes[ offset++ ];
		if ( raw > 1 ) {
			Error( "bool byte %d", raw );
		}
		value = raw != 0;
	}
	void ReadByte( unsigned char &value ) { Need( 1 ); value = bytes[ offset++ ]; }
	[[noreturn]] void Error( const char *fmt, ... ) { throw std::runtime_error( fmt ); }
	bool Finished() const { return offset == bytes.size(); }
private:
	void Need( size_t count ) {
		if ( offset + count > bytes.size() ) {
			Error( "read past end" );
		}
	}
	std::uint32_t Get() {
		Need( 4 );
		std::uint32_t value = 0;
		for ( int i = 0; i < 4; i++ ) {
			value |= static_cast<std::uint32_t>( bytes[ offset++ ] ) << ( 8 * i );
		}
		return value;
	}
	const std::vector<unsigned char> &bytes;
	size_t offset = 0;
};

template< int N >
struct FloatVector {
	float v[ N ];	// trivial, like idVec3: Extrapolate.h clears it with memset
	float &operator[]( int i ) { return v[ i ]; }
	float operator[]( int i ) const { return v[ i ]; }
	FloatVector operator+( const FloatVector &o ) const { FloatVector r; for ( int i = 0; i < N; i++ ) r.v[ i ] = v[ i ] + o.v[ i ]; return r; }
	FloatVector operator-( const FloatVector &o ) const { FloatVector r; for ( int i = 0; i < N; i++ ) r.v[ i ] = v[ i ] - o.v[ i ]; return r; }
	FloatVector operator*( float s ) const { FloatVector r; for ( int i = 0; i < N; i++ ) r.v[ i ] = v[ i ] * s; return r; }
	friend FloatVector operator*( float s, const FloatVector &a ) { return a * s; }
	bool operator==( const FloatVector &o ) const { return std::memcmp( v, o.v, sizeof( v ) ) == 0; }
};
using Vec3 = FloatVector<3>;
using Vec4 = FloatVector<4>;
using Angles = FloatVector<3>;

struct Mat3 {
	Vec3 rows[ 3 ];
	Vec3 &operator[]( int i ) { return rows[ i ]; }
	const Vec3 &operator[]( int i ) const { return rows[ i ]; }
};
struct Bounds {
	Vec3 b[ 2 ];
	Vec3 &operator[]( int i ) { return b[ i ]; }
	const Vec3 &operator[]( int i ) const { return b[ i ]; }
};

// idTraceModel's field names and limits.
enum traceModel_t { TRM_INVALID, TRM_BOX, TRM_OCTAHEDRON, TRM_DODECAHEDRON, TRM_CYLINDER, TRM_CONE, TRM_BONE, TRM_POLYGON, TRM_POLYGONVOLUME, TRM_CUSTOM };
struct TraceModelEdge { int v[ 2 ] = {}; Vec3 normal; };
struct TraceModelPoly { Vec3 normal; float dist = 0; Bounds bounds; int numEdges = 0; int edges[ 16 ] = {}; };
struct TraceModel {
	traceModel_t type = TRM_INVALID;
	int numVerts = 0;
	Vec3 verts[ 32 ];
	int numEdges = 0;
	TraceModelEdge edges[ 33 ];
	int numPolys = 0;
	TraceModelPoly polys[ 16 ];
	Vec3 offset;
	Bounds bounds;
	bool isConvex = false;
};

enum jointHandle_t { INVALID_JOINT = -1 };
enum jointModTransform_t { JOINTMOD_NONE, JOINTMOD_LOCAL, JOINTMOD_LOCAL_OVERRIDE, JOINTMOD_WORLD, JOINTMOD_WORLD_OVERRIDE };
struct JointMod {
	jointHandle_t jointnum = INVALID_JOINT;
	Mat3 mat;
	Vec3 pos;
	jointModTransform_t transform_pos = JOINTMOD_NONE;
	jointModTransform_t transform_axis = JOINTMOD_NONE;
	idInterpolateAccelDecelLinear<Angles> angularVelocity;
	int lastTime = 0;
	jointHandle_t collapseJoint = INVALID_JOINT;
};

enum AFJointModType_t { AF_JOINTMOD_AXIS, AF_JOINTMOD_ORIGIN, AF_JOINTMOD_BOTH };
struct PoseJointMod { AFJointModType_t mod = AF_JOINTMOD_AXIS; Mat3 axis; Vec3 origin; };
struct JointQuat { Vec4 q; Vec3 t; float w = 0; };
struct JointMat {
	float mat[ 12 ] = {};
	const float *ToFloatPtr() const { return mat; }
	float *ToFloatPtr() { return mat; }
};
struct PathTarget { int node = 0; float initialDistance = 0, minDistance = 0, throttle = 0; bool exitVehicle = false; };

static std::string Hex( const std::vector<unsigned char> &bytes ) {
	static const char digits[] = "0123456789abcdef";
	std::string text;
	for ( unsigned char value : bytes ) {
		text += digits[ value >> 4 ];
		text += digits[ value & 15 ];
	}
	return text;
}

static std::vector<unsigned char> Bytes( const std::string &hex ) {
	std::vector<unsigned char> bytes;
	for ( size_t i = 0; i + 1 < hex.size(); i += 2 ) {
		bytes.push_back( static_cast<unsigned char>( std::stoi( hex.substr( i, 2 ), nullptr, 16 ) ) );
	}
	return bytes;
}

static void Golden( const char *label, const Writer &writer, const char *expected ) {
	const std::string actual = Hex( writer.bytes );
	if ( actual != expected ) {
		std::fprintf( stderr, "FAIL: %s golden bytes\n  expected %s\n  actual   %s\n", label, expected, actual.c_str() );
		++failures;
	}
}

template< class Decode >
static void Rejects( const char *label, const std::vector<unsigned char> &bytes, Decode decode ) {
	Reader reader( bytes );
	try {
		decode( reader );
	} catch ( const std::runtime_error & ) {
		return;
	}
	std::fprintf( stderr, "FAIL: %s was accepted\n", label );
	++failures;
}

static Vec3 V3( float x, float y, float z ) { Vec3 v; v[ 0 ] = x; v[ 1 ] = y; v[ 2 ] = z; return v; }

static void TestPrimitives() {
	Writer writer;
	writer.WriteInt( 0x01020304 );
	writer.WriteInt( -2 );
	writer.WriteFloat( 1.0f );
	writer.WriteFloat( -0.5f );
	writer.WriteBool( true );
	writer.WriteBool( false );
	Golden( "primitives", writer, "04030201feffffff0000803f000000bf0100" );
}

// The game's flag structs are one-bit bool fields. These two mirror the
// smallest and the 17-flag vehicle layout; the test compares this compiler's
// raw bytes with the codec's, which is what keeps converted saves unchanged.
struct Flags5 { bool a : 1; bool b : 1; bool c : 1; bool d : 1; bool e : 1; };
struct Flags17 {
	bool f0 : 1; bool f1 : 1; bool f2 : 1; bool f3 : 1; bool f4 : 1; bool f5 : 1; bool f6 : 1; bool f7 : 1;
	bool f8 : 1; bool f9 : 1; bool f10 : 1; bool f11 : 1; bool f12 : 1; bool f13 : 1; bool f14 : 1; bool f15 : 1;
	bool f16 : 1;
};

static std::vector<unsigned char> RawBytes( const void *data, size_t size ) {
	const unsigned char *bytes = static_cast<const unsigned char *>( data );
	return std::vector<unsigned char>( bytes, bytes + size );
}

static void TestFlags() {
	Writer writer;
	const bool flags[ 5 ] = { true, false, true, true, false };
	SaveCodec_WriteFlagBytes( writer, flags );
	Golden( "flags", writer, "0d" );
	Reader reader( writer.bytes );
	bool decoded[ 5 ];
	SaveCodec_ReadFlagBytes( reader, decoded );
	Check( std::memcmp( decoded, flags, sizeof( flags ) ) == 0 && reader.Finished(), "flags round trip" );

	// Old saves copied the unused bits as they were; readers ignore them.
	const std::vector<unsigned char> dirtyBytes = Bytes( "ed" );
	Reader dirty( dirtyBytes );
	SaveCodec_ReadFlagBytes( dirty, decoded );
	Check( std::memcmp( decoded, flags, sizeof( flags ) ) == 0, "flag bits beyond the list are ignored" );

	Flags5 raw5 = {};
	raw5.a = true; raw5.c = true; raw5.d = true;
	Check( sizeof( Flags5 ) == 1 && RawBytes( &raw5, sizeof( raw5 ) ) == writer.bytes, "5-flag struct layout matches the codec" );

	bool vehicle[ 17 ];
	for ( int i = 0; i < 17; i++ ) {
		vehicle[ i ] = ( i % 3 ) == 0 || i == 16;
	}
	Writer vehicleWriter;
	SaveCodec_WriteFlagBytes( vehicleWriter, vehicle );
	Golden( "17 flags", vehicleWriter, "4992" "01" );
	Flags17 raw17 = {};
	raw17.f0 = raw17.f3 = raw17.f6 = raw17.f9 = raw17.f12 = raw17.f15 = raw17.f16 = true;
	Check( sizeof( Flags17 ) == 3 && RawBytes( &raw17, sizeof( raw17 ) ) == vehicleWriter.bytes, "17-flag struct layout matches the codec" );
	Reader vehicleReader( vehicleWriter.bytes );
	bool vehicleDecoded[ 17 ];
	SaveCodec_ReadFlagBytes( vehicleReader, vehicleDecoded );
	Check( std::memcmp( vehicleDecoded, vehicle, sizeof( vehicle ) ) == 0 && vehicleReader.Finished(), "17 flags round trip" );
	Rejects( "truncated flags", Bytes( "4992" ), []( Reader &r ) { bool f[ 17 ]; SaveCodec_ReadFlagBytes( r, f ); } );
}

static TraceModel SampleTraceModel() {
	TraceModel trm;
	trm.type = TRM_POLYGON;
	trm.numVerts = 2;
	trm.verts[ 0 ] = V3( 1, 2, 3 );
	trm.verts[ 1 ] = V3( -1, 0, 0.5f );
	trm.numEdges = 1;
	trm.edges[ 1 ].v[ 0 ] = 0;
	trm.edges[ 1 ].v[ 1 ] = 1;
	trm.edges[ 1 ].normal = V3( 0, 0, 1 );
	trm.edges[ 0 ].v[ 0 ] = 77;	// unused slot, never written
	trm.numPolys = 1;
	trm.polys[ 0 ].normal = V3( 0, 1, 0 );
	trm.polys[ 0 ].dist = 4;
	trm.polys[ 0 ].bounds[ 0 ] = V3( -1, -2, -3 );
	trm.polys[ 0 ].bounds[ 1 ] = V3( 1, 2, 3 );
	trm.polys[ 0 ].numEdges = 2;
	trm.polys[ 0 ].edges[ 0 ] = 1;
	trm.polys[ 0 ].edges[ 1 ] = -1;
	trm.offset = V3( 0.25f, 0, 0 );
	trm.bounds[ 0 ] = V3( -8, -8, -8 );
	trm.bounds[ 1 ] = V3( 8, 8, 8 );
	trm.isConvex = true;
	return trm;
}

static void ReadTrm( Reader &reader, TraceModel &trm ) {
	SaveCodec_ReadTraceModel( reader, trm, TRM_CUSTOM + 1, 32, 32, 16, 16 );
}

static void TestTraceModel() {
	const TraceModel trm = SampleTraceModel();
	Writer writer;
	SaveCodec_WriteTraceModel( writer, trm );
	Golden( "trace model", writer,
		"07000000" "02000000" "0000803f0000004000004040" "000080bf000000000000003f"
		"01000000" "0000000001000000" "00000000000000000000803f"
		"01000000" "000000000000803f00000000" "00008040"
		"000080bf000000c0000040c0" "0000803f0000004000004040"
		"02000000" "01000000ffffffff"
		"0000803e0000000000000000" "000000c1000000c1000000c1" "000000410000004100000041" "01" );
	Reader reader( writer.bytes );
	TraceModel decoded;
	ReadTrm( reader, decoded );
	Check( reader.Finished(), "trace model consumes its bytes" );
	Check( decoded.type == trm.type && decoded.numVerts == 2 && decoded.verts[ 1 ] == trm.verts[ 1 ], "trace model vertices" );
	Check( decoded.numEdges == 1 && decoded.edges[ 1 ].v[ 1 ] == 1 && decoded.edges[ 1 ].normal == trm.edges[ 1 ].normal, "trace model edges" );
	Check( decoded.edges[ 0 ].v[ 0 ] == 0, "trace model unused edge stays untouched" );
	Check( decoded.numPolys == 1 && decoded.polys[ 0 ].dist == 4 && decoded.polys[ 0 ].edges[ 1 ] == -1 &&
		decoded.polys[ 0 ].bounds[ 1 ] == trm.polys[ 0 ].bounds[ 1 ], "trace model polygon" );
	Check( decoded.offset == trm.offset && decoded.bounds[ 0 ] == trm.bounds[ 0 ] && decoded.isConvex, "trace model tail" );

	std::vector<unsigned char> badType = writer.bytes;
	badType[ 0 ] = 10;
	Rejects( "trace model type past the enum", badType, []( Reader &r ) { TraceModel t; ReadTrm( r, t ); } );
	std::vector<unsigned char> badVerts = writer.bytes;
	badVerts[ 4 ] = 33;
	Rejects( "trace model with 33 vertices", badVerts, []( Reader &r ) { TraceModel t; ReadTrm( r, t ); } );
	std::vector<unsigned char> truncated( writer.bytes.begin(), writer.bytes.end() - 1 );
	Rejects( "truncated trace model", truncated, []( Reader &r ) { TraceModel t; ReadTrm( r, t ); } );
	std::vector<unsigned char> badBool = writer.bytes;
	badBool.back() = 2;
	Rejects( "trace model convex byte 2", badBool, []( Reader &r ) { TraceModel t; ReadTrm( r, t ); } );
	// A complete record over the reader's limits: only the count checks refuse it.
	Rejects( "trace model over a 1-vertex limit", writer.bytes, []( Reader &r ) {
		TraceModel t; SaveCodec_ReadTraceModel( r, t, TRM_CUSTOM + 1, 1, 32, 16, 16 ); } );
	Rejects( "trace model over a 1-edge polygon limit", writer.bytes, []( Reader &r ) {
		TraceModel t; SaveCodec_ReadTraceModel( r, t, TRM_CUSTOM + 1, 32, 32, 16, 1 ); } );
}

static void TestInterpolate() {
	idInterpolateAccelDecelLinear<float> lerp;
	lerp.Init( 100, 200, 300, 1000, 1.0f, 3.0f );
	Writer writer;
	SaveCodec_WriteInterpolate( writer, lerp );
	Golden( "interpolate", writer, "0000c842000048430000964300007a440000803f00004040" );
	Reader reader( writer.bytes );
	idInterpolateAccelDecelLinear<float> decoded;
	SaveCodec_ReadInterpolate( reader, decoded );
	Check( reader.Finished(), "interpolate consumes its bytes" );
	for ( float time : { 0.0f, 150.0f, 300.0f, 500.0f, 900.0f, 1100.0f, 2000.0f } ) {
		Check( decoded.GetCurrentValue( time ) == lerp.GetCurrentValue( time ), "interpolate rebuilt by Init matches" );
	}
	// Accel plus decel beyond the duration: Init scales them, and the stored
	// values must rebuild the same scaled phases.
	idInterpolateAccelDecelLinear<float> scaled;
	scaled.Init( 0, 800, 800, 1000, 0.0f, 10.0f );
	Writer scaledWriter;
	SaveCodec_WriteInterpolate( scaledWriter, scaled );
	Reader scaledReader( scaledWriter.bytes );
	idInterpolateAccelDecelLinear<float> scaledDecoded;
	SaveCodec_ReadInterpolate( scaledReader, scaledDecoded );
	for ( float time : { 0.0f, 250.0f, 500.0f, 750.0f, 1000.0f } ) {
		Check( scaledDecoded.GetCurrentValue( time ) == scaled.GetCurrentValue( time ), "scaled interpolate rebuilds" );
	}
}

static void TestJointMod() {
	JointMod mod;
	mod.jointnum = static_cast<jointHandle_t>( 5 );
	mod.mat[ 0 ] = V3( 1, 0, 0 );
	mod.mat[ 1 ] = V3( 0, 1, 0 );
	mod.mat[ 2 ] = V3( 0, 0, 1 );
	mod.pos = V3( 2, 4, 8 );
	mod.transform_pos = JOINTMOD_LOCAL;
	mod.transform_axis = JOINTMOD_WORLD_OVERRIDE;
	mod.angularVelocity.Init( 10, 0, 0, 100, V3( 0, 90, 0 ), V3( 0, 180, 0 ) );
	mod.lastTime = 1234;
	mod.collapseJoint = INVALID_JOINT;
	Writer writer;
	SaveCodec_WriteJointMod( writer, mod );
	Golden( "joint modifier", writer,
		"05000000"
		"0000803f0000000000000000" "000000000000803f00000000" "00000000000000000000803f"
		"000000400000804000000041"
		"01000000" "04000000"
		"000020410000000000000000" "0000c842" "00000000" "0000b442" "00000000" "00000000" "00003443" "00000000"
		"d2040000" "ffffffff" );
	Reader reader( writer.bytes );
	JointMod decoded;
	SaveCodec_ReadJointMod( reader, decoded, JOINTMOD_WORLD_OVERRIDE + 1 );
	Check( reader.Finished(), "joint modifier consumes its bytes" );
	Check( static_cast<int>( decoded.jointnum ) == 5 && decoded.mat[ 2 ] == mod.mat[ 2 ] && decoded.pos == mod.pos, "joint modifier transform" );
	Check( decoded.transform_pos == JOINTMOD_LOCAL && decoded.transform_axis == JOINTMOD_WORLD_OVERRIDE, "joint modifier modes" );
	Check( decoded.angularVelocity.GetCurrentValue( 60 ) == mod.angularVelocity.GetCurrentValue( 60 ), "joint modifier angular velocity" );
	Check( decoded.lastTime == 1234 && decoded.collapseJoint == INVALID_JOINT, "joint modifier tail" );
	std::vector<unsigned char> badMode = writer.bytes;
	badMode[ 52 ] = 5;
	Rejects( "joint modifier transform 5", badMode, []( Reader &r ) { JointMod m; SaveCodec_ReadJointMod( r, m, JOINTMOD_WORLD_OVERRIDE + 1 ); } );
}

static void TestPoseAndJoints() {
	PoseJointMod pose;
	pose.mod = AF_JOINTMOD_BOTH;
	pose.axis[ 0 ] = V3( 0, 1, 0 );
	pose.axis[ 1 ] = V3( -1, 0, 0 );
	pose.axis[ 2 ] = V3( 0, 0, 1 );
	pose.origin = V3( 3, 2, 1 );
	Writer writer;
	SaveCodec_WritePoseJointMod( writer, pose );
	Golden( "pose joint modifier", writer,
		"02000000" "000000000000803f00000000" "000080bf0000000000000000" "00000000000000000000803f" "00004040000000400000803f" );
	Reader reader( writer.bytes );
	PoseJointMod decodedPose;
	SaveCodec_ReadPoseJointMod( reader, decodedPose, AF_JOINTMOD_BOTH + 1 );
	Check( reader.Finished() && decodedPose.mod == AF_JOINTMOD_BOTH && decodedPose.axis[ 1 ] == pose.axis[ 1 ] &&
		decodedPose.origin == pose.origin, "pose joint modifier round trip" );
	std::vector<unsigned char> badMod = writer.bytes;
	badMod[ 0 ] = 3;
	Rejects( "pose modifier type 3", badMod, []( Reader &r ) { PoseJointMod p; SaveCodec_ReadPoseJointMod( r, p, AF_JOINTMOD_BOTH + 1 ); } );

	JointQuat quat;
	quat.q[ 0 ] = 0; quat.q[ 1 ] = 0; quat.q[ 2 ] = 0; quat.q[ 3 ] = 1;
	quat.t = V3( 1, 2, 3 );
	quat.w = 0.5f;
	Writer quatWriter;
	SaveCodec_WriteJointQuat( quatWriter, quat );
	Golden( "joint quaternion", quatWriter, "0000000000000000000000000000803f" "0000803f0000004000004040" "0000003f" );
	Reader quatReader( quatWriter.bytes );
	JointQuat decodedQuat;
	SaveCodec_ReadJointQuat( quatReader, decodedQuat );
	Check( quatReader.Finished() && decodedQuat.q == quat.q && decodedQuat.t == quat.t && decodedQuat.w == 0.5f, "joint quaternion round trip" );

	JointMat mat;
	for ( int i = 0; i < 12; i++ ) {
		mat.mat[ i ] = static_cast<float>( i );
	}
	Writer matWriter;
	SaveCodec_WriteJointMat( matWriter, mat );
	Golden( "joint matrix", matWriter,
		"000000000000803f0000004000004040000080400000a0400000c0400000e040000000410000104100002041" "00003041" );
	Reader matReader( matWriter.bytes );
	JointMat decodedMat;
	SaveCodec_ReadJointMat( matReader, decodedMat );
	Check( matReader.Finished() && std::memcmp( decodedMat.mat, mat.mat, sizeof( mat.mat ) ) == 0, "joint matrix round trip" );
}

static void TestPathTarget() {
	PathTarget target;
	target.node = 0x1234;
	target.initialDistance = 512;
	target.minDistance = 32;
	target.throttle = 0.75f;
	target.exitVehicle = true;
	Writer writer;
	SaveCodec_WritePathTarget( writer, target, [&writer]( int node ) { writer.WriteInt( node ); } );
	Golden( "path target", writer, "34120000" "00000044" "00000042" "0000403f" "01" );
	Reader reader( writer.bytes );
	PathTarget decoded;
	SaveCodec_ReadPathTarget( reader, decoded, [&reader]( int &node ) { reader.ReadInt( node ); } );
	Check( reader.Finished() && decoded.node == 0x1234 && decoded.initialDistance == 512 && decoded.minDistance == 32 &&
		decoded.throttle == 0.75f && decoded.exitVehicle, "path target round trip" );
}

static void TestArrays() {
	const int maps[ 3 ] = { 2, -1, 0 };
	const float floats[ 2 ] = { 0.5f, -2.0f };
	Angles angles;
	angles[ 0 ] = 10; angles[ 1 ] = -20; angles[ 2 ] = 30;
	Writer writer;
	SaveCodec_WriteInts( writer, maps, 3 );
	SaveCodec_WriteFloats( writer, floats, 2 );
	SaveCodec_WriteValue( writer, angles );
	Golden( "arrays", writer, "02000000ffffffff00000000" "0000003f000000c0" "000020410000a0c10000f041" );
}

int main() try {
#ifdef _MSC_VER
	// Mutation controls deliberately abort. Keep the debug CRT on stderr so
	// unattended runs cannot wait for a modal assertion/abort dialog.
	_CrtSetReportMode( _CRT_ASSERT, _CRTDBG_MODE_FILE );
	_CrtSetReportFile( _CRT_ASSERT, _CRTDBG_FILE_STDERR );
	_set_abort_behavior( 0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT );
	_set_error_mode( _OUT_TO_STDERR );
#endif
	TestPrimitives();
	TestFlags();
	TestTraceModel();
	TestInterpolate();
	TestJointMod();
	TestPoseAndJoints();
	TestPathTarget();
	TestArrays();
	if ( failures ) {
		std::fprintf( stderr, "save codec: %d failure(s)\n", failures );
		return 1;
	}
	std::puts( "save codec: golden bytes, round trips and rejections passed" );
	return 0;
} catch ( const std::exception &error ) {
	// A malformed codec mutation may throw before its golden-byte check.
	// Report failure without an unhandled-exception dialog on Windows CI.
	std::fprintf( stderr, "save codec: unexpected exception: %s\n", error.what() );
	return 1;
}
