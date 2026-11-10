// Copyright (C) 2026 DarkMatter Productions
//
// openQ4 OpenXR test runtime.
//
// A minimal OpenXR 1.0 runtime with XR_KHR_opengl_enable and
// XR_KHR_vulkan_enable2, loaded through the standard Khronos loader by
// pointing XR_RUNTIME_JSON at the manifest the build writes next to this
// library. It exists so the engine's VR path can be run, inspected and
// captured on a machine with no headset and without changing any system-wide
// OpenXR runtime registration.
//
// It simulates one stereo head-mounted display and two tracked controllers.
// Poses and input come from an optional script; submitted frames are validated
// the way a real compositor would and can be written out as images.
//
// Environment:
//   OPENQ4_XR_TEST_SCRIPT   path of a frame script (see ParseScript)
//   OPENQ4_XR_TEST_LOG      path of a JSON-lines event log
//   OPENQ4_XR_TEST_EYE_SIZE "WIDTHxHEIGHT" recommended per-eye size (default 1440x1600)
//   OPENQ4_XR_TEST_RATE     display refresh in Hz (default 90); 0 runs unpaced
//   OPENQ4_XR_TEST_NO_INPUT 1 keeps every controller action inactive
//   OPENQ4_XR_TEST_FAULT    "call:occurrence:value" (e.g. xrWaitSwapchainImage:2:timeout)
//   OPENQ4_XR_TEST_FAULT_AFTER optional existing-file marker before counting calls
//   OPENQ4_XR_TEST_FAULT_ONCE optional receipt prevents reinjection after loader restart
//   OPENQ4_XR_TEST_VK_DEVICE  index of the Vulkan physical device the "headset"
//                           renders with (default: the first discrete GPU)
//   OPENQ4_XR_TEST_SPACES   comma-separated floor-level reference spaces offered
//                           besides VIEW and LOCAL: stage, local_floor (which
//                           also offers XR_EXT_local_floor), or none
//                           (default: stage)
//   OPENQ4_XR_TEST_VK_FORMATS comma-separated VkFormat values the swapchains
//                           offer, in order (default 43,50,37,44: RGBA and
//                           BGRA, sRGB first)
//
// Script lines are "<frame> <command> [arguments]", where frame counts
// completed xrEndFrame calls and a command takes effect when that many frames
// have been submitted, or "when <file> <command> [arguments]", which takes
// effect after the first frame submitted once <file> exists (an application
// can create marker files, e.g. with a console command, to synchronise with
// the script without any input). Commands:
//   head <yawDeg> <pitchDeg> <rollDeg> [x y z]      head pose in the stage space (metres)
//   hand <left|right> <x> <y> <z> <yaw> <pitch> <roll>
//   button <left|right> <trigger|squeeze|a|b|x|y|menu|thumbclick> <value>
//   stick <left|right> <x> <y>
//   capture <path prefix>                           write every layer of the next submitted frame
//   burst <path prefix> <frames> [scale]            write the left eye of that many consecutive frames,
//                                                   shrunk by an integer scale (default 4), to catch a
//                                                   single-frame dropout that one capture can miss
//   recenter [delayMsec]                            the headset's own recentre: LOCAL (and LOCAL_FLOOR)
//                                                   move under the head, facing its heading, for frames
//                                                   displayed delayMsec (default 300) from now on, and the
//                                                   change is announced at once, as a runtime does
//   focus <0|1>                                     toggle between FOCUSED and VISIBLE
//   exit                                            ask the application to quit (STOPPING)

#if defined( _WIN32 )
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>	// openxr_platform.h's Win32 section names IUnknown
#define XR_USE_PLATFORM_WIN32
#endif

#if defined( __APPLE__ )
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

// Vulkan only through the application's loader: every entry point comes from
// the vkGetInstanceProcAddr it hands xrCreateVulkanInstanceKHR, so the runtime
// links no Vulkan library
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define XR_USE_GRAPHICS_API_OPENGL
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>
#include <openxr/openxr_reflection.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined( __linux__ )
#include <dlfcn.h>
#endif

#if defined( _WIN32 )
#define TR_EXPORT extern "C" __declspec( dllexport )
#else
#define TR_EXPORT extern "C" __attribute__( ( visibility( "default" ) ) )
#endif

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_PIXEL_PACK_BUFFER
#define GL_PIXEL_PACK_BUFFER 0x88EB
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif
#ifndef GL_PIXEL_PACK_BUFFER_BINDING
#define GL_PIXEL_PACK_BUFFER_BINDING 0x88ED
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER_BINDING
#define GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#endif
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif

namespace {

/*
===============================================================================

	Small helpers

===============================================================================
*/

const uint32_t MAGIC_INSTANCE = 0x4F515849;	// 'OQXI'
const uint32_t MAGIC_SESSION = 0x4F515853;
const uint32_t MAGIC_SPACE = 0x4F515850;
const uint32_t MAGIC_SWAPCHAIN = 0x4F515843;
const uint32_t MAGIC_ACTIONSET = 0x4F515841;
const uint32_t MAGIC_ACTION = 0x4F515831;

const XrSystemId TR_SYSTEM_ID = 1;
const uint32_t TR_SWAPCHAIN_LENGTH = 3;

typedef void ( APIENTRY *PFNTRGLBINDBUFFER )( GLenum target, GLuint buffer );

struct Vec3 {
	float x, y, z;
};

struct Quat {
	float x, y, z, w;
};

struct Pose {
	Quat q;
	Vec3 p;
};

Vec3 V( float x, float y, float z ) {
	Vec3 v = { x, y, z };
	return v;
}

Quat QIdentity() {
	Quat q = { 0.0f, 0.0f, 0.0f, 1.0f };
	return q;
}

Quat QMul( const Quat &a, const Quat &b ) {
	Quat r;
	r.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
	r.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
	r.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
	r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
	return r;
}

Quat QConj( const Quat &q ) {
	Quat c = { -q.x, -q.y, -q.z, q.w };
	return c;
}

Quat QNorm( const Quat &q ) {
	const float len = std::sqrt( q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w );
	if ( !( len > 1e-8f ) ) {
		return QIdentity();
	}
	Quat n = { q.x / len, q.y / len, q.z / len, q.w / len };
	return n;
}

Vec3 QRotate( const Quat &q, const Vec3 &v ) {
	const Vec3 u = V( q.x, q.y, q.z );
	const Vec3 t = V( 2.0f * ( u.y * v.z - u.z * v.y ), 2.0f * ( u.z * v.x - u.x * v.z ), 2.0f * ( u.x * v.y - u.y * v.x ) );
	return V( v.x + q.w * t.x + ( u.y * t.z - u.z * t.y ),
		v.y + q.w * t.y + ( u.z * t.x - u.x * t.z ),
		v.z + q.w * t.z + ( u.x * t.y - u.y * t.x ) );
}

Quat QAxisAngle( const Vec3 &axis, float degrees ) {
	const float half = degrees * 3.14159265358979f / 360.0f;
	const float s = std::sin( half );
	Quat q = { axis.x * s, axis.y * s, axis.z * s, std::cos( half ) };
	return q;
}

// OpenXR convention: yaw turns left about +Y, pitch looks up about +X, roll
// tilts the head to the right about -Z.
Quat QFromYawPitchRoll( float yaw, float pitch, float roll ) {
	const Quat qYaw = QAxisAngle( V( 0.0f, 1.0f, 0.0f ), yaw );
	const Quat qPitch = QAxisAngle( V( 1.0f, 0.0f, 0.0f ), pitch );
	const Quat qRoll = QAxisAngle( V( 0.0f, 0.0f, -1.0f ), roll );
	return QNorm( QMul( qYaw, QMul( qPitch, qRoll ) ) );
}

// a * b: b expressed in a's frame
Pose PMul( const Pose &a, const Pose &b ) {
	Pose r;
	r.q = QNorm( QMul( a.q, b.q ) );
	const Vec3 rp = QRotate( a.q, b.p );
	r.p = V( a.p.x + rp.x, a.p.y + rp.y, a.p.z + rp.z );
	return r;
}

Pose PInverse( const Pose &a ) {
	Pose r;
	r.q = QConj( a.q );
	const Vec3 np = QRotate( r.q, a.p );
	r.p = V( -np.x, -np.y, -np.z );
	return r;
}

Pose PIdentity() {
	Pose p = { QIdentity(), V( 0.0f, 0.0f, 0.0f ) };
	return p;
}

Pose FromXr( const XrPosef &xr ) {
	Pose p;
	p.q = QNorm( Quat{ xr.orientation.x, xr.orientation.y, xr.orientation.z, xr.orientation.w } );
	p.p = V( xr.position.x, xr.position.y, xr.position.z );
	return p;
}

XrPosef ToXr( const Pose &p ) {
	XrPosef xr;
	xr.orientation.x = p.q.x;
	xr.orientation.y = p.q.y;
	xr.orientation.z = p.q.z;
	xr.orientation.w = p.q.w;
	xr.position.x = p.p.x;
	xr.position.y = p.p.y;
	xr.position.z = p.p.z;
	return xr;
}

bool PoseValid( const XrPosef &pose ) {
	const float lengthSq = pose.orientation.x * pose.orientation.x + pose.orientation.y * pose.orientation.y
		+ pose.orientation.z * pose.orientation.z + pose.orientation.w * pose.orientation.w;
	if ( !std::isfinite( lengthSq ) || std::fabs( lengthSq - 1.0f ) > 0.01f ) {
		return false;
	}
	return std::isfinite( pose.position.x ) && std::isfinite( pose.position.y ) && std::isfinite( pose.position.z );
}

int64_t NowNanoseconds() {
	return std::chrono::duration_cast<std::chrono::nanoseconds>( std::chrono::steady_clock::now().time_since_epoch() ).count();
}

template<typename T>
XrResult TwoCall( uint32_t capacity, uint32_t *countOutput, T *output, const std::vector<T> &values ) {
	if ( countOutput == nullptr ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	*countOutput = static_cast<uint32_t>( values.size() );
	if ( capacity == 0 ) {
		return XR_SUCCESS;
	}
	if ( capacity < values.size() ) {
		return XR_ERROR_SIZE_INSUFFICIENT;
	}
	if ( output == nullptr ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	for ( size_t i = 0; i < values.size(); ++i ) {
		output[i] = values[i];
	}
	return XR_SUCCESS;
}

XrResult TwoCallString( uint32_t capacity, uint32_t *countOutput, char *buffer, const std::string &value ) {
	if ( countOutput == nullptr ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	*countOutput = static_cast<uint32_t>( value.size() + 1 );
	if ( capacity == 0 ) {
		return XR_SUCCESS;
	}
	if ( capacity < value.size() + 1 ) {
		return XR_ERROR_SIZE_INSUFFICIENT;
	}
	if ( buffer == nullptr ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	std::memcpy( buffer, value.c_str(), value.size() + 1 );
	return XR_SUCCESS;
}

std::string JsonEscape( const std::string &text ) {
	std::string out;
	for ( char c : text ) {
		switch ( c ) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if ( static_cast<unsigned char>( c ) < 0x20 ) {
					char hex[8];
					std::snprintf( hex, sizeof( hex ), "\\u%04x", c );
					out += hex;
				} else {
					out += c;
				}
				break;
		}
	}
	return out;
}

/*
===============================================================================

	Runtime state

	One instance and one session at a time, which is all an application uses.
	Every entry point takes the global lock.

===============================================================================
*/

struct TRHandle {
	uint32_t magic;
};

struct TRInstance;
struct TRSession;

struct TRSpace : TRHandle {
	TRSession *			session;
	bool				isAction;
	XrReferenceSpaceType referenceType;
	struct TRAction *	action;
	XrPath				subactionPath;
	Pose				offset;
};

struct TRSwapchain : TRHandle {
	TRSession *			session;
	XrSwapchainCreateInfo info;
	uint32_t			imageCount;
	std::vector<GLuint>	textures;			// OpenGL sessions
	std::vector<VkImage> vkImages;			// Vulkan sessions, with their memory
	std::vector<VkDeviceMemory> vkMemory;
	uint32_t			nextAcquire;
	std::deque<uint32_t> acquired;		// acquired, in order; front is the one waited/released next
	bool				waited;
	int					lastReleased;
};

struct TRAction : TRHandle {
	struct TRActionSet *set;
	std::string			name;
	XrActionType		type;
	std::vector<XrPath>	subactionPaths;
	// per interaction profile suggested binding paths
	std::map<XrPath, std::vector<XrPath>> suggested;
	// last synced state per subaction path (XR_NULL_PATH = combined)
	struct SyncedState {
		bool	active;
		float	value;
		float	x, y;
		bool	changed;
		XrTime	changeTime;
	};
	std::map<XrPath, SyncedState> state;
};

struct TRActionSet : TRHandle {
	TRInstance *		instance;
	std::string			name;
	bool				attached;
	std::vector<TRAction *> actions;
};

struct HandInput {
	Pose	grip;
	float	trigger;
	float	squeeze;
	bool	a, b, x, y, menu, thumbClick;
	float	stickX, stickY;
};

// Device entry points of a Vulkan session, from the application's loader.
struct VulkanFns {
	PFN_vkGetDeviceProcAddr					GetDeviceProcAddr = nullptr;
	PFN_vkGetPhysicalDeviceMemoryProperties	GetPhysicalDeviceMemoryProperties = nullptr;
	PFN_vkGetDeviceQueue					GetDeviceQueue = nullptr;
	PFN_vkCreateImage						CreateImage = nullptr;
	PFN_vkDestroyImage						DestroyImage = nullptr;
	PFN_vkGetImageMemoryRequirements		GetImageMemoryRequirements = nullptr;
	PFN_vkAllocateMemory					AllocateMemory = nullptr;
	PFN_vkFreeMemory						FreeMemory = nullptr;
	PFN_vkBindImageMemory					BindImageMemory = nullptr;
	PFN_vkCreateBuffer						CreateBuffer = nullptr;
	PFN_vkDestroyBuffer						DestroyBuffer = nullptr;
	PFN_vkGetBufferMemoryRequirements		GetBufferMemoryRequirements = nullptr;
	PFN_vkBindBufferMemory					BindBufferMemory = nullptr;
	PFN_vkMapMemory							MapMemory = nullptr;
	PFN_vkUnmapMemory						UnmapMemory = nullptr;
	PFN_vkCreateCommandPool					CreateCommandPool = nullptr;
	PFN_vkDestroyCommandPool				DestroyCommandPool = nullptr;
	PFN_vkAllocateCommandBuffers			AllocateCommandBuffers = nullptr;
	PFN_vkFreeCommandBuffers				FreeCommandBuffers = nullptr;
	PFN_vkBeginCommandBuffer				BeginCommandBuffer = nullptr;
	PFN_vkEndCommandBuffer					EndCommandBuffer = nullptr;
	PFN_vkCmdPipelineBarrier				CmdPipelineBarrier = nullptr;
	PFN_vkCmdCopyImageToBuffer				CmdCopyImageToBuffer = nullptr;
	PFN_vkQueueSubmit						QueueSubmit = nullptr;
	PFN_vkQueueWaitIdle						QueueWaitIdle = nullptr;
};

struct TRSession : TRHandle {
	TRInstance *		instance;
	// a Vulkan session: the application's device and the queue the binding names
	bool				vulkan;
	VulkanFns			vk;
	VkPhysicalDevice	vkPhysicalDevice;
	VkDevice			vkDevice;
	VkQueue				vkQueue;
	VkCommandPool		vkCommandPool;
	VkBuffer			vkReadback;			// host-visible capture buffer, grown on demand
	VkDeviceMemory		vkReadbackMemory;
	VkDeviceSize		vkReadbackSize;
	XrSessionState		state;
	bool				running;
	bool				exitRequested;
	bool				focused;
	XrViewConfigurationType viewConfiguration;
	std::vector<TRSpace *> spaces;
	std::vector<TRSwapchain *> swapchains;
	std::vector<TRActionSet *> attachedSets;
	XrPath				currentProfile;
	// frame loop
	int64_t				nextDisplayTime;
	int64_t				period;
	bool				frameWaited;
	bool				frameBegun;
	int64_t				waitedDisplayTime;
	uint64_t			framesSubmitted;
	std::string			lastLayout;			// layer types and flags of the last submitted frame
	uint64_t			framesWaited;
};

struct ScriptEvent {
	uint64_t				frame;
	std::string				whenPath;		// non-empty: fires once this file exists
	bool					fired;
	std::vector<std::string> args;
};

struct TRInstance : TRHandle {
	std::vector<std::string> enabledExtensions;
	std::string			applicationName;
	std::deque<XrEventDataBuffer> events;
	TRSession *			session;
	std::vector<TRActionSet *> actionSets;
	std::vector<std::string> paths;		// index + 1 == XrPath
	std::map<std::string, XrPath> pathIds;
	bool				graphicsRequirementsQueried;
	bool				profileChangedPending;
	// what xrCreateVulkanInstanceKHR and friends made for the application
	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
	VkInstance			vkInstance = VK_NULL_HANDLE;
	VkPhysicalDevice	vkPhysicalDevice = VK_NULL_HANDLE;
	VkDevice			vkDevice = VK_NULL_HANDLE;
};

struct RuntimeGlobals {
	std::recursive_mutex	lock;
	TRInstance *			instance = nullptr;
	FILE *					log = nullptr;
	std::vector<ScriptEvent> script;
	size_t					scriptCursor = 0;
	std::string				pendingCapture;
	std::string				burstPrefix;
	int						burstRemaining = 0;
	int						burstIndex = 0;
	int						burstScale = 4;
	int						eyeWidth = 1440;
	int						eyeHeight = 1600;
	double					refreshRate = 90.0;
	Pose					head = { QIdentity(), { 0.0f, 1.6f, 0.0f } };
	HandInput				hands[2];
	bool					initialized = false;
	std::map<const void *, uint32_t> liveHandles;
	bool					noInput = false;
	bool					sessionLost = false, instanceLost = false;
	std::string				faultCall, faultValue, faultAfter, faultOnce;
	int						faultOccurrence = 1, faultCalls = 0;
	bool					faultFired = false;
	int						trackingInvalidFrames = 0;
	// floor-level reference spaces offered (OPENQ4_XR_TEST_SPACES)
	bool					offerStage = true;
	bool					offerLocalFloor = false;
	// LOCAL's origin in the stage space: a seated origin at the default head
	// height until the "recenter" script command moves it. The move applies to
	// locates at or after localChangeTime, as the specification requires of
	// XrEventDataReferenceSpaceChangePending; locateTime is the time of the
	// locate call being answered.
	Pose					localOrigin = { QIdentity(), { 0.0f, 1.6f, 0.0f } };
	Pose					pendingLocalOrigin = { QIdentity(), { 0.0f, 1.6f, 0.0f } };
	XrTime					localChangeTime = 0;
	XrTime					locateTime = 0;
};

RuntimeGlobals &G() {
	static RuntimeGlobals globals;
	return globals;
}

void Log( const char *fmt, ... ) {
	RuntimeGlobals &g = G();
	if ( g.log == nullptr ) {
		return;
	}
	char buffer[4096];
	va_list args;
	va_start( args, fmt );
	std::vsnprintf( buffer, sizeof( buffer ), fmt, args );
	va_end( args );
	std::fprintf( g.log, "%s\n", buffer );
	std::fflush( g.log );
}

bool FileExists( const std::string &path );

bool Fault( const char *call ) {
	RuntimeGlobals &g = G();
	if ( g.sessionLost || g.instanceLost ) {
		Log( "{\"event\":\"call_after_loss\",\"call\":\"%s\"}", call );
	}
	if ( g.faultFired || g.faultCall != call || ( !g.faultAfter.empty() && !FileExists( g.faultAfter ) ) || FileExists( g.faultOnce ) ) {
		return false;
	}
	if ( ++g.faultCalls != g.faultOccurrence ) {
		return false;
	}
	g.faultFired = true;
	Log( "{\"event\":\"injected_fault\",\"call\":\"%s\",\"occurrence\":%d,\"value\":\"%s\"}", call, g.faultCalls, g.faultValue.c_str() );
	if ( !g.faultOnce.empty() ) {
		std::ofstream( g.faultOnce ) << call << ':' << g.faultValue;
	}
	g.instanceLost = g.faultValue == "instance" || g.faultValue == "runtime";
	g.sessionLost = g.instanceLost || g.faultValue == "session" || g.faultValue == "pending";
	return true;
}

XrResult FaultResult() {
	const std::string &value = G().faultValue;
	if ( value == "session" ) return XR_ERROR_SESSION_LOST;
	if ( value == "instance" ) return XR_ERROR_INSTANCE_LOST;
	if ( value == "runtime" ) return XR_ERROR_RUNTIME_FAILURE;
	if ( value == "pending" ) return XR_SESSION_LOSS_PENDING;
	if ( value == "timeout" ) return XR_TIMEOUT_EXPIRED;
	if ( value == "limit" ) return XR_ERROR_LIMIT_REACHED;
	return XR_ERROR_CALL_ORDER_INVALID;
}

void ResetHands() {
	RuntimeGlobals &g = G();
	for ( int i = 0; i < 2; ++i ) {
		HandInput &hand = g.hands[i];
		hand = HandInput();
		hand.grip.q = QIdentity();
		hand.grip.p = V( i == 0 ? -0.2f : 0.2f, 1.25f, -0.35f );
	}
}

void ParseScript( const char *path ) {
	RuntimeGlobals &g = G();
	std::ifstream file( path );
	if ( !file ) {
		Log( "{\"event\":\"script_error\",\"path\":\"%s\"}", JsonEscape( path ).c_str() );
		return;
	}
	std::string line;
	while ( std::getline( file, line ) ) {
		const size_t hash = line.find( '#' );
		if ( hash != std::string::npos ) {
			line.erase( hash );
		}
		std::istringstream stream( line );
		ScriptEvent event;
		event.frame = 0;
		event.fired = false;
		std::string first;
		if ( !( stream >> first ) ) {
			continue;
		}
		if ( first == "when" ) {
			if ( !( stream >> event.whenPath ) ) {
				continue;
			}
		} else {
			char *end = nullptr;
			event.frame = std::strtoull( first.c_str(), &end, 10 );
			if ( end == first.c_str() || *end != '\0' ) {
				continue;
			}
		}
		std::string token;
		while ( stream >> token ) {
			event.args.push_back( token );
		}
		if ( !event.args.empty() ) {
			g.script.push_back( event );
		}
	}
	// frame commands run in frame order; marker commands keep file order
	std::stable_sort( g.script.begin(), g.script.end(), []( const ScriptEvent &a, const ScriptEvent &b ) {
		if ( a.whenPath.empty() != b.whenPath.empty() ) {
			return a.whenPath.empty();
		}
		return a.whenPath.empty() && a.frame < b.frame;
	} );
	Log( "{\"event\":\"script\",\"path\":\"%s\",\"commands\":%u}", JsonEscape( path ).c_str(), static_cast<unsigned>( g.script.size() ) );
}

void InitGlobals() {
	RuntimeGlobals &g = G();
	if ( g.initialized ) {
		return;
	}
	g.initialized = true;
	ResetHands();
	if ( const char *logPath = std::getenv( "OPENQ4_XR_TEST_LOG" ) ) {
		g.log = std::fopen( logPath, "a" );
	}
	if ( const char *size = std::getenv( "OPENQ4_XR_TEST_EYE_SIZE" ) ) {
		int w = 0, h = 0;
		if ( std::sscanf( size, "%dx%d", &w, &h ) == 2 && w >= 64 && h >= 64 && w <= 4096 && h <= 4096 ) {
			g.eyeWidth = w;
			g.eyeHeight = h;
		}
	}
	if ( const char *rate = std::getenv( "OPENQ4_XR_TEST_RATE" ) ) {
		const double value = std::atof( rate );
		if ( value >= 0.0 && value <= 1000.0 ) {
			g.refreshRate = value;
		}
	}
	if ( const char *script = std::getenv( "OPENQ4_XR_TEST_SCRIPT" ) ) {
		ParseScript( script );
	}
	g.noInput = std::getenv( "OPENQ4_XR_TEST_NO_INPUT" ) != nullptr;
	if ( const char *fault = std::getenv( "OPENQ4_XR_TEST_FAULT" ) ) {
		std::istringstream input( fault );
		std::string occurrence;
		std::getline( input, g.faultCall, ':' );
		std::getline( input, occurrence, ':' );
		std::getline( input, g.faultValue );
		g.faultOccurrence = std::max( 1, std::atoi( occurrence.c_str() ) );
	}
	if ( const char *spaces = std::getenv( "OPENQ4_XR_TEST_SPACES" ) ) {
		const std::string list = std::string( "," ) + spaces + ",";
		g.offerStage = list.find( ",stage," ) != std::string::npos;
		g.offerLocalFloor = list.find( ",local_floor," ) != std::string::npos;
	}
	if ( const char *path = std::getenv( "OPENQ4_XR_TEST_FAULT_AFTER" ) ) g.faultAfter = path;
	if ( const char *path = std::getenv( "OPENQ4_XR_TEST_FAULT_ONCE" ) ) g.faultOnce = path;
	Log( "{\"event\":\"runtime_loaded\",\"eye_width\":%d,\"eye_height\":%d,\"rate\":%.1f}", g.eyeWidth, g.eyeHeight, g.refreshRate );
}

template<typename T>
T *Check( T *handle, uint32_t magic ) {
	const auto found = G().liveHandles.find( handle );
	if ( handle == nullptr || found == G().liveHandles.end() || found->second != magic ) {
		Log( "{\"event\":\"invalid_handle\",\"kind\":%u}", magic );
		return nullptr;
	}
	return handle;
}

TRInstance *GetInstance( XrInstance handle ) {
	TRInstance *instance = Check( reinterpret_cast<TRInstance *>( handle ), MAGIC_INSTANCE );
	return instance == G().instance ? instance : nullptr;
}

TRSession *GetSession( XrSession handle ) {
	TRSession *session = Check( reinterpret_cast<TRSession *>( handle ), MAGIC_SESSION );
	if ( session == nullptr || G().instance == nullptr || G().instance->session != session ) {
		return nullptr;
	}
	return session;
}

void QueueEvent( TRInstance *instance, const XrEventDataBuffer &event ) {
	instance->events.push_back( event );
}

void QueueStateChange( TRSession *session, XrSessionState state ) {
	session->state = state;
	XrEventDataBuffer buffer = {};
	XrEventDataSessionStateChanged *changed = reinterpret_cast<XrEventDataSessionStateChanged *>( &buffer );
	changed->type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
	changed->next = nullptr;
	changed->session = reinterpret_cast<XrSession>( session );
	changed->state = state;
	changed->time = NowNanoseconds();
	QueueEvent( session->instance, buffer );
	Log( "{\"event\":\"session_state\",\"state\":%d}", static_cast<int>( state ) );
}

const char *PathString( TRInstance *instance, XrPath path ) {
	if ( path == XR_NULL_PATH || path > instance->paths.size() ) {
		return "";
	}
	return instance->paths[static_cast<size_t>( path - 1 )].c_str();
}

XrPath InternPath( TRInstance *instance, const std::string &text ) {
	std::map<std::string, XrPath>::const_iterator it = instance->pathIds.find( text );
	if ( it != instance->pathIds.end() ) {
		return it->second;
	}
	instance->paths.push_back( text );
	const XrPath id = static_cast<XrPath>( instance->paths.size() );
	instance->pathIds[text] = id;
	return id;
}

bool ValidPathString( const char *text ) {
	if ( text == nullptr || text[0] != '/' ) {
		return false;
	}
	const size_t length = std::strlen( text );
	if ( length >= XR_MAX_PATH_LENGTH || text[length - 1] == '/' ) {
		return false;
	}
	for ( size_t i = 0; i < length; ++i ) {
		const char c = text[i];
		const bool ok = ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '-' || c == '_' || c == '.' || c == '/';
		if ( !ok ) {
			return false;
		}
		if ( c == '/' && i + 1 < length && text[i + 1] == '/' ) {
			return false;
		}
	}
	return true;
}

/*
===============================================================================

	Simulated devices

===============================================================================
*/

int HandIndexForUserPath( const std::string &path ) {
	if ( path.rfind( "/user/hand/left", 0 ) == 0 ) {
		return 0;
	}
	if ( path.rfind( "/user/hand/right", 0 ) == 0 ) {
		return 1;
	}
	return -1;
}

Pose StagePoseOfReference( XrReferenceSpaceType type ) {
	RuntimeGlobals &g = G();
	switch ( type ) {
		case XR_REFERENCE_SPACE_TYPE_VIEW:
			return g.head;
		case XR_REFERENCE_SPACE_TYPE_LOCAL:
		case XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT: {
			// a seated origin at the default head height, until a recentre moves
			// it; the move counts for locates from its change time on
			if ( g.localChangeTime != 0 && g.locateTime >= g.localChangeTime ) {
				g.localOrigin = g.pendingLocalOrigin;
				g.localChangeTime = 0;
				Log( "{\"event\":\"reference_space_changed\",\"locate_time\":%lld}", static_cast<long long>( g.locateTime ) );
			}
			Pose local = g.localOrigin;
			if ( type == XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT ) {
				// LOCAL's origin dropped to the floor
				local.p.y = 0.0f;
			}
			return local;
		}
		case XR_REFERENCE_SPACE_TYPE_STAGE:
		default:
			return PIdentity();
	}
}

// Pose of an aim or grip input relative to the stage. The aim pose points
// along the controller's -Z like the grip in this simulation.
Pose StagePoseOfHand( int hand ) {
	return G().hands[hand].grip;
}

bool StagePoseOfSpace( const TRSpace *space, Pose &out ) {
	if ( space->isAction ) {
		TRInstance *instance = space->session->instance;
		int hand = -1;
		if ( space->subactionPath != XR_NULL_PATH ) {
			hand = HandIndexForUserPath( PathString( instance, space->subactionPath ) );
		} else if ( space->action != nullptr && !space->action->subactionPaths.empty() ) {
			hand = HandIndexForUserPath( PathString( instance, space->action->subactionPaths[0] ) );
		}
		if ( hand < 0 ) {
			// a pose action bound without a subaction path follows its first binding
			for ( const auto &profile : space->action->suggested ) {
				for ( XrPath binding : profile.second ) {
					hand = HandIndexForUserPath( PathString( instance, binding ) );
					if ( hand >= 0 ) {
						break;
					}
				}
				if ( hand >= 0 ) {
					break;
				}
			}
		}
		if ( hand < 0 ) {
			return false;
		}
		out = PMul( StagePoseOfHand( hand ), space->offset );
		return true;
	}
	out = PMul( StagePoseOfReference( space->referenceType ), space->offset );
	return true;
}

// Typical consumer-headset eye frusta: wider towards the temple than the nose.
XrFovf EyeFov( int eye ) {
	XrFovf fov;
	if ( eye == 0 ) {
		fov.angleLeft = -0.94f;
		fov.angleRight = 0.78f;
	} else {
		fov.angleLeft = -0.78f;
		fov.angleRight = 0.94f;
	}
	fov.angleUp = 0.86f;
	fov.angleDown = -0.92f;
	return fov;
}

const float TR_IPD = 0.064f;

/*
===============================================================================

	Input evaluation

===============================================================================
*/

// value of one binding path for the scripted controllers
bool EvaluateBinding( TRInstance *instance, XrPath binding, float &value, float &x, float &y, bool &isVector ) {
	const std::string path = PathString( instance, binding );
	const int hand = HandIndexForUserPath( path );
	if ( hand < 0 ) {
		return false;
	}
	const HandInput &input = G().hands[hand];
	const size_t inputPos = path.find( "/input/" );
	if ( inputPos == std::string::npos ) {
		return false;
	}
	const std::string component = path.substr( inputPos + 7 );
	isVector = false;
	x = y = 0.0f;
	if ( component == "trigger/value" || component == "trigger/click" || component == "trigger" || component == "select/click" ) {
		value = input.trigger;
	} else if ( component == "squeeze/value" || component == "squeeze/click" || component == "squeeze" ) {
		value = input.squeeze;
	} else if ( component == "a/click" ) {
		value = input.a ? 1.0f : 0.0f;
	} else if ( component == "b/click" ) {
		value = input.b ? 1.0f : 0.0f;
	} else if ( component == "x/click" ) {
		value = input.x ? 1.0f : 0.0f;
	} else if ( component == "y/click" ) {
		value = input.y ? 1.0f : 0.0f;
	} else if ( component == "menu/click" || component == "system/click" ) {
		value = input.menu ? 1.0f : 0.0f;
	} else if ( component == "thumbstick/click" || component == "trackpad/click" ) {
		value = input.thumbClick ? 1.0f : 0.0f;
	} else if ( component == "thumbstick" || component == "trackpad" ) {
		isVector = true;
		x = input.stickX;
		y = input.stickY;
		value = std::sqrt( x * x + y * y );
	} else if ( component == "thumbstick/x" || component == "trackpad/x" ) {
		value = input.stickX;
	} else if ( component == "thumbstick/y" || component == "trackpad/y" ) {
		value = input.stickY;
	} else if ( component == "grip/pose" || component == "aim/pose" ) {
		value = 1.0f;
	} else {
		value = 0.0f;
	}
	return true;
}

XrPath ChooseProfile( TRInstance *instance ) {
	static const char *preference[] = {
		"/interaction_profiles/oculus/touch_controller",
		"/interaction_profiles/valve/index_controller",
		"/interaction_profiles/htc/vive_controller",
		"/interaction_profiles/khr/simple_controller",
	};
	for ( const char *name : preference ) {
		std::map<std::string, XrPath>::const_iterator it = instance->pathIds.find( name );
		if ( it == instance->pathIds.end() ) {
			continue;
		}
		for ( TRActionSet *set : instance->actionSets ) {
			for ( TRAction *action : set->actions ) {
				if ( action->suggested.count( it->second ) ) {
					return it->second;
				}
			}
		}
	}
	return XR_NULL_PATH;
}

/*
===============================================================================

	Capture

===============================================================================
*/

PFNTRGLBINDBUFFER GetBindBuffer() {
#if defined( _WIN32 )
	static PFNTRGLBINDBUFFER fn = reinterpret_cast<PFNTRGLBINDBUFFER>( wglGetProcAddress( "glBindBuffer" ) );
	return fn;
#else
	return nullptr;
#endif
}

bool WriteTga( const std::string &path, int width, int height, const std::vector<uint8_t> &rgba ) {
	FILE *file = std::fopen( path.c_str(), "wb" );
	if ( file == nullptr ) {
		return false;
	}
	uint8_t header[18] = {};
	header[2] = 2;
	header[12] = static_cast<uint8_t>( width & 0xFF );
	header[13] = static_cast<uint8_t>( ( width >> 8 ) & 0xFF );
	header[14] = static_cast<uint8_t>( height & 0xFF );
	header[15] = static_cast<uint8_t>( ( height >> 8 ) & 0xFF );
	header[16] = 32;
	header[17] = 8;	// bottom-up rows, 8 alpha bits: matches the GL readback order
	std::fwrite( header, 1, sizeof( header ), file );
	std::vector<uint8_t> bgra( rgba.size() );
	for ( size_t i = 0; i + 3 < rgba.size(); i += 4 ) {
		bgra[i + 0] = rgba[i + 2];
		bgra[i + 1] = rgba[i + 1];
		bgra[i + 2] = rgba[i + 0];
		bgra[i + 3] = rgba[i + 3];
	}
	std::fwrite( bgra.data(), 1, bgra.size(), file );
	std::fclose( file );
	return true;
}

/*
===============================================================================

	Vulkan (XR_KHR_vulkan_enable2)

	Swapchain images are device-local images on the application's device,
	handed over in COLOR_ATTACHMENT_OPTIMAL. Captures copy them back on the
	application's queue, which the binding names; the runtime touches it only
	inside the application's own OpenXR calls, on its thread.

===============================================================================
*/

bool LoadVulkanFns( PFN_vkGetInstanceProcAddr getInstanceProcAddr, VkInstance instance, VkDevice device, VulkanFns &fns ) {
	fns.GetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>( getInstanceProcAddr( instance, "vkGetDeviceProcAddr" ) );
	fns.GetPhysicalDeviceMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
		getInstanceProcAddr( instance, "vkGetPhysicalDeviceMemoryProperties" ) );
	if ( fns.GetDeviceProcAddr == nullptr || fns.GetPhysicalDeviceMemoryProperties == nullptr ) {
		return false;
	}
#define TR_VK_DEVICE( name ) \
	fns.name = reinterpret_cast<PFN_vk##name>( fns.GetDeviceProcAddr( device, "vk" #name ) ); \
	if ( fns.name == nullptr ) { return false; }
	TR_VK_DEVICE( GetDeviceQueue )
	TR_VK_DEVICE( CreateImage )
	TR_VK_DEVICE( DestroyImage )
	TR_VK_DEVICE( GetImageMemoryRequirements )
	TR_VK_DEVICE( AllocateMemory )
	TR_VK_DEVICE( FreeMemory )
	TR_VK_DEVICE( BindImageMemory )
	TR_VK_DEVICE( CreateBuffer )
	TR_VK_DEVICE( DestroyBuffer )
	TR_VK_DEVICE( GetBufferMemoryRequirements )
	TR_VK_DEVICE( BindBufferMemory )
	TR_VK_DEVICE( MapMemory )
	TR_VK_DEVICE( UnmapMemory )
	TR_VK_DEVICE( CreateCommandPool )
	TR_VK_DEVICE( DestroyCommandPool )
	TR_VK_DEVICE( AllocateCommandBuffers )
	TR_VK_DEVICE( FreeCommandBuffers )
	TR_VK_DEVICE( BeginCommandBuffer )
	TR_VK_DEVICE( EndCommandBuffer )
	TR_VK_DEVICE( CmdPipelineBarrier )
	TR_VK_DEVICE( CmdCopyImageToBuffer )
	TR_VK_DEVICE( QueueSubmit )
	TR_VK_DEVICE( QueueWaitIdle )
#undef TR_VK_DEVICE
	return true;
}

uint32_t VulkanMemoryType( const TRSession *session, uint32_t typeBits, VkMemoryPropertyFlags wanted ) {
	VkPhysicalDeviceMemoryProperties properties = {};
	session->vk.GetPhysicalDeviceMemoryProperties( session->vkPhysicalDevice, &properties );
	for ( uint32_t i = 0; i < properties.memoryTypeCount; ++i ) {
		if ( ( typeBits & ( 1u << i ) ) != 0 && ( properties.memoryTypes[i].propertyFlags & wanted ) == wanted ) {
			return i;
		}
	}
	return UINT32_MAX;
}

// Records one command buffer, runs it on the application's queue and waits:
// everything the application submitted earlier has finished with it.
template<typename Record>
bool VulkanSubmitOnce( const TRSession *session, Record record ) {
	VkCommandBufferAllocateInfo allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocateInfo.commandPool = session->vkCommandPool;
	allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocateInfo.commandBufferCount = 1;
	VkCommandBuffer cmd = VK_NULL_HANDLE;
	if ( session->vk.AllocateCommandBuffers( session->vkDevice, &allocateInfo, &cmd ) != VK_SUCCESS ) {
		return false;
	}
	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VkResult result = session->vk.BeginCommandBuffer( cmd, &beginInfo );
	if ( result == VK_SUCCESS ) {
		record( cmd );
		result = session->vk.EndCommandBuffer( cmd );
	}
	if ( result == VK_SUCCESS ) {
		VkSubmitInfo submit = {};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &cmd;
		result = session->vk.QueueSubmit( session->vkQueue, 1, &submit, VK_NULL_HANDLE );
		if ( result == VK_SUCCESS ) {
			result = session->vk.QueueWaitIdle( session->vkQueue );
		}
	}
	session->vk.FreeCommandBuffers( session->vkDevice, session->vkCommandPool, 1, &cmd );
	return result == VK_SUCCESS;
}

void VulkanImageBarrier( const TRSession *session, VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
		VkPipelineStageFlags srcStage, VkAccessFlags srcAccess, VkPipelineStageFlags dstStage, VkAccessFlags dstAccess ) {
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = srcAccess;
	barrier.dstAccessMask = dstAccess;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.layerCount = 1;
	session->vk.CmdPipelineBarrier( cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier );
}

bool VulkanFormatIsBGRA( int64_t format ) {
	return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_B8G8R8A8_UNORM;
}

// Copies one image back through the capture buffer. The application left it
// in COLOR_ATTACHMENT_OPTIMAL at release, and gets it back that way. Rows come
// out bottom-up and in RGBA order, as the GL readback gives them.
bool ReadVulkanSwapchainImage( const TRSwapchain *swapchain, int index, const XrRect2Di &rect, std::vector<uint8_t> &out, int &width, int &height ) {
	TRSession *session = swapchain->session;
	const int fullWidth = static_cast<int>( swapchain->info.width );
	const int fullHeight = static_cast<int>( swapchain->info.height );
	const VkDeviceSize bytes = static_cast<VkDeviceSize>( fullWidth ) * fullHeight * 4;
	if ( session->vkReadbackSize < bytes ) {
		if ( session->vkReadback != VK_NULL_HANDLE ) {
			session->vk.DestroyBuffer( session->vkDevice, session->vkReadback, nullptr );
			session->vk.FreeMemory( session->vkDevice, session->vkReadbackMemory, nullptr );
			session->vkReadback = VK_NULL_HANDLE;
			session->vkReadbackMemory = VK_NULL_HANDLE;
			session->vkReadbackSize = 0;
		}
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = bytes;
		bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if ( session->vk.CreateBuffer( session->vkDevice, &bufferInfo, nullptr, &session->vkReadback ) != VK_SUCCESS ) {
			session->vkReadback = VK_NULL_HANDLE;
			return false;
		}
		VkMemoryRequirements requirements = {};
		session->vk.GetBufferMemoryRequirements( session->vkDevice, session->vkReadback, &requirements );
		VkMemoryAllocateInfo allocateInfo = {};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = requirements.size;
		allocateInfo.memoryTypeIndex = VulkanMemoryType( session, requirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
		if ( allocateInfo.memoryTypeIndex == UINT32_MAX
				|| session->vk.AllocateMemory( session->vkDevice, &allocateInfo, nullptr, &session->vkReadbackMemory ) != VK_SUCCESS
				|| session->vk.BindBufferMemory( session->vkDevice, session->vkReadback, session->vkReadbackMemory, 0 ) != VK_SUCCESS ) {
			session->vk.DestroyBuffer( session->vkDevice, session->vkReadback, nullptr );
			if ( session->vkReadbackMemory != VK_NULL_HANDLE ) {
				session->vk.FreeMemory( session->vkDevice, session->vkReadbackMemory, nullptr );
			}
			session->vkReadback = VK_NULL_HANDLE;
			session->vkReadbackMemory = VK_NULL_HANDLE;
			return false;
		}
		session->vkReadbackSize = bytes;
	}

	const VkImage image = swapchain->vkImages[static_cast<size_t>( index )];
	const bool copied = VulkanSubmitOnce( session, [&]( VkCommandBuffer cmd ) {
		VulkanImageBarrier( session, cmd, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT );
		VkBufferImageCopy region = {};
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.layerCount = 1;
		region.imageExtent.width = static_cast<uint32_t>( fullWidth );
		region.imageExtent.height = static_cast<uint32_t>( fullHeight );
		region.imageExtent.depth = 1;
		session->vk.CmdCopyImageToBuffer( cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, session->vkReadback, 1, &region );
		VulkanImageBarrier( session, cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT );
	} );
	if ( !copied ) {
		return false;
	}
	void *mapped = nullptr;
	if ( session->vk.MapMemory( session->vkDevice, session->vkReadbackMemory, 0, bytes, 0, &mapped ) != VK_SUCCESS || mapped == nullptr ) {
		return false;
	}
	const uint8_t *full = static_cast<const uint8_t *>( mapped );
	const int x0 = std::max( 0, rect.offset.x );
	const int y0 = std::max( 0, rect.offset.y );
	width = std::min( rect.extent.width, fullWidth - x0 );
	height = std::min( rect.extent.height, fullHeight - y0 );
	if ( width <= 0 || height <= 0 ) {
		session->vk.UnmapMemory( session->vkDevice, session->vkReadbackMemory );
		return false;
	}
	const bool bgra = VulkanFormatIsBGRA( swapchain->info.format );
	out.resize( static_cast<size_t>( width ) * height * 4 );
	for ( int row = 0; row < height; ++row ) {
		// Vulkan rows run top-down; the output is bottom-up like GL's
		const uint8_t *src = &full[( static_cast<size_t>( y0 + height - 1 - row ) * fullWidth + x0 ) * 4];
		uint8_t *dst = &out[static_cast<size_t>( row ) * width * 4];
		for ( int x = 0; x < width; ++x ) {
			dst[x * 4 + 0] = src[x * 4 + ( bgra ? 2 : 0 )];
			dst[x * 4 + 1] = src[x * 4 + 1];
			dst[x * 4 + 2] = src[x * 4 + ( bgra ? 0 : 2 )];
			dst[x * 4 + 3] = src[x * 4 + 3];
		}
	}
	session->vk.UnmapMemory( session->vkDevice, session->vkReadbackMemory );
	return true;
}

// Reads one swapchain image back with the GL state the application had left
// intact: the 2D binding of the active unit, both pixel buffer bindings and
// the pack alignment are restored.
bool ReadSwapchainImage( const TRSwapchain *swapchain, int index, const XrRect2Di &rect, std::vector<uint8_t> &out, int &width, int &height ) {
	if ( index < 0 || index >= static_cast<int>( swapchain->imageCount ) ) {
		return false;
	}
	if ( swapchain->session->vulkan ) {
		return ReadVulkanSwapchainImage( swapchain, index, rect, out, width, height );
	}
	const int fullWidth = static_cast<int>( swapchain->info.width );
	const int fullHeight = static_cast<int>( swapchain->info.height );
	std::vector<uint8_t> full( static_cast<size_t>( fullWidth ) * fullHeight * 4 );

	GLint previousTexture = 0, previousPack = 0, previousPackBuffer = 0;
	glGetIntegerv( GL_TEXTURE_BINDING_2D, &previousTexture );
	glGetIntegerv( GL_PACK_ALIGNMENT, &previousPack );
	PFNTRGLBINDBUFFER bindBuffer = GetBindBuffer();
	if ( bindBuffer != nullptr ) {
		glGetIntegerv( GL_PIXEL_PACK_BUFFER_BINDING, &previousPackBuffer );
		bindBuffer( GL_PIXEL_PACK_BUFFER, 0 );
	}
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glBindTexture( GL_TEXTURE_2D, swapchain->textures[static_cast<size_t>( index )] );
	glGetTexImage( GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, full.data() );
	glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>( previousTexture ) );
	glPixelStorei( GL_PACK_ALIGNMENT, previousPack );
	if ( bindBuffer != nullptr ) {
		bindBuffer( GL_PIXEL_PACK_BUFFER, static_cast<GLuint>( previousPackBuffer ) );
	}

	const int x0 = std::max( 0, rect.offset.x );
	const int y0 = std::max( 0, rect.offset.y );
	width = std::min( rect.extent.width, fullWidth - x0 );
	height = std::min( rect.extent.height, fullHeight - y0 );
	if ( width <= 0 || height <= 0 ) {
		return false;
	}
	out.resize( static_cast<size_t>( width ) * height * 4 );
	for ( int row = 0; row < height; ++row ) {
		std::memcpy( &out[static_cast<size_t>( row ) * width * 4], &full[( static_cast<size_t>( y0 + row ) * fullWidth + x0 ) * 4], static_cast<size_t>( width ) * 4 );
	}
	return true;
}

void CaptureImage( const std::string &prefix, const char *label, const XrSwapchainSubImage &subImage ) {
	TRSwapchain *swapchain = Check( reinterpret_cast<TRSwapchain *>( subImage.swapchain ), MAGIC_SWAPCHAIN );
	if ( swapchain == nullptr ) {
		return;
	}
	std::vector<uint8_t> pixels;
	int width = 0, height = 0;
	if ( !ReadSwapchainImage( swapchain, swapchain->lastReleased, subImage.imageRect, pixels, width, height ) ) {
		Log( "{\"event\":\"capture_failed\",\"layer\":\"%s\"}", label );
		return;
	}
	// a cheap content summary so harnesses need not decode the image
	double sum = 0.0;
	uint64_t lit = 0;
	for ( size_t i = 0; i + 3 < pixels.size(); i += 4 ) {
		const int luma = ( pixels[i] * 3 + pixels[i + 1] * 6 + pixels[i + 2] ) / 10;
		sum += luma;
		if ( luma > 8 ) {
			++lit;
		}
	}
	const uint64_t count = static_cast<uint64_t>( width ) * height;
	const std::string path = prefix + "_" + label + ".tga";
	const bool written = WriteTga( path, width, height, pixels );
	Log( "{\"event\":\"capture\",\"layer\":\"%s\",\"path\":\"%s\",\"written\":%s,\"width\":%d,\"height\":%d,\"mean_luma\":%.3f,\"lit_fraction\":%.5f}",
		label, JsonEscape( path ).c_str(), written ? "true" : "false", width, height,
		count ? sum / static_cast<double>( count ) : 0.0, count ? static_cast<double>( lit ) / static_cast<double>( count ) : 0.0 );
}

// One frame of a burst: the image shrunk by box filtering, with the same
// content summary as a capture so a harness can spot an odd frame from the
// log alone.
void CaptureBurstImage( const std::string &path, int scale, const XrSwapchainSubImage &subImage, int index ) {
	TRSwapchain *swapchain = Check( reinterpret_cast<TRSwapchain *>( subImage.swapchain ), MAGIC_SWAPCHAIN );
	if ( swapchain == nullptr ) {
		return;
	}
	std::vector<uint8_t> pixels;
	int width = 0, height = 0;
	if ( !ReadSwapchainImage( swapchain, swapchain->lastReleased, subImage.imageRect, pixels, width, height ) ) {
		Log( "{\"event\":\"burst_failed\",\"index\":%d}", index );
		return;
	}
	scale = scale < 1 ? 1 : scale;
	const int outWidth = width / scale, outHeight = height / scale;
	std::vector<uint8_t> shrunk( static_cast<size_t>( outWidth ) * outHeight * 4 );
	double sum = 0.0;
	for ( int y = 0; y < outHeight; ++y ) {
		for ( int x = 0; x < outWidth; ++x ) {
			int acc[4] = { 0, 0, 0, 0 };
			for ( int sy = 0; sy < scale; ++sy ) {
				for ( int sx = 0; sx < scale; ++sx ) {
					const uint8_t *p = &pixels[( static_cast<size_t>( y * scale + sy ) * width + x * scale + sx ) * 4];
					for ( int c = 0; c < 4; ++c ) {
						acc[c] += p[c];
					}
				}
			}
			uint8_t *out = &shrunk[( static_cast<size_t>( y ) * outWidth + x ) * 4];
			for ( int c = 0; c < 4; ++c ) {
				out[c] = static_cast<uint8_t>( acc[c] / ( scale * scale ) );
			}
			sum += ( out[0] * 3 + out[1] * 6 + out[2] ) / 10;
		}
	}
	const bool written = WriteTga( path, outWidth, outHeight, shrunk );
	Log( "{\"event\":\"burst\",\"index\":%d,\"path\":\"%s\",\"written\":%s,\"mean_luma\":%.3f}",
		index, JsonEscape( path ).c_str(), written ? "true" : "false",
		outWidth * outHeight ? sum / static_cast<double>( outWidth * outHeight ) : 0.0 );
}

bool FileExists( const std::string &path ) {
	FILE *file = std::fopen( path.c_str(), "rb" );
	if ( file == nullptr ) {
		return false;
	}
	std::fclose( file );
	return true;
}

void RunScriptCommand( TRSession *session, const std::vector<std::string> &a ) {
	RuntimeGlobals &g = G();
	{
		const std::string &command = a[0];
		auto f = [&]( size_t i, float fallback ) {
			return i < a.size() ? static_cast<float>( std::atof( a[i].c_str() ) ) : fallback;
		};
		if ( command == "head" ) {
			g.head.q = QFromYawPitchRoll( f( 1, 0.0f ), f( 2, 0.0f ), f( 3, 0.0f ) );
			if ( a.size() >= 7 ) {
				g.head.p = V( f( 4, 0.0f ), f( 5, 1.6f ), f( 6, 0.0f ) );
			}
		} else if ( command == "hand" && a.size() >= 8 ) {
			const int hand = a[1] == "left" ? 0 : 1;
			g.hands[hand].grip.p = V( f( 2, 0.0f ), f( 3, 0.0f ), f( 4, 0.0f ) );
			g.hands[hand].grip.q = QFromYawPitchRoll( f( 5, 0.0f ), f( 6, 0.0f ), f( 7, 0.0f ) );
		} else if ( command == "button" && a.size() >= 4 ) {
			HandInput &hand = g.hands[a[1] == "left" ? 0 : 1];
			const float value = f( 3, 0.0f );
			const bool on = value >= 0.5f;
			const std::string &name = a[2];
			if ( name == "trigger" ) {
				hand.trigger = value;
			} else if ( name == "squeeze" ) {
				hand.squeeze = value;
			} else if ( name == "a" ) {
				hand.a = on;
			} else if ( name == "b" ) {
				hand.b = on;
			} else if ( name == "x" ) {
				hand.x = on;
			} else if ( name == "y" ) {
				hand.y = on;
			} else if ( name == "menu" ) {
				hand.menu = on;
			} else if ( name == "thumbclick" ) {
				hand.thumbClick = on;
			}
		} else if ( command == "stick" && a.size() >= 4 ) {
			HandInput &hand = g.hands[a[1] == "left" ? 0 : 1];
			hand.stickX = f( 2, 0.0f );
			hand.stickY = f( 3, 0.0f );
		} else if ( command == "capture" && a.size() >= 2 ) {
			g.pendingCapture = a[1];
		} else if ( command == "burst" && a.size() >= 3 ) {
			g.burstPrefix = a[1];
			g.burstRemaining = std::atoi( a[2].c_str() );
			g.burstIndex = 0;
			g.burstScale = a.size() >= 4 ? std::atoi( a[3].c_str() ) : 4;
		} else if ( command == "recenter" ) {
			// under the head, level, facing its heading
			const Vec3 forward = QRotate( g.head.q, V( 0.0f, 0.0f, -1.0f ) );
			const float yaw = std::atan2( -forward.x, -forward.z ) * 180.0f / 3.14159265358979f;
			Pose moved;
			moved.q = QAxisAngle( V( 0.0f, 1.0f, 0.0f ), yaw );
			moved.p = g.head.p;
			const Pose previous = g.localChangeTime != 0 ? g.pendingLocalOrigin : g.localOrigin;
			g.pendingLocalOrigin = moved;
			// midway between two display times, so no frame sits on the threshold
			const int64_t delay = static_cast<int64_t>( f( 1, 300.0f ) ) * 1000000;
			const int64_t periods = session->period > 0 ? ( delay + session->period - 1 ) / session->period : 0;
			g.localChangeTime = session->nextDisplayTime + periods * session->period + session->period / 2;
			const XrReferenceSpaceType types[2] = { XR_REFERENCE_SPACE_TYPE_LOCAL, XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT };
			for ( int i = 0; i < ( g.offerLocalFloor ? 2 : 1 ); ++i ) {
				XrEventDataBuffer buffer = {};
				XrEventDataReferenceSpaceChangePending *pending = reinterpret_cast<XrEventDataReferenceSpaceChangePending *>( &buffer );
				pending->type = XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING;
				pending->next = nullptr;
				pending->session = reinterpret_cast<XrSession>( session );
				pending->referenceSpaceType = types[i];
				pending->changeTime = g.localChangeTime;
				pending->poseValid = XR_TRUE;
				pending->poseInPreviousSpace = ToXr( PMul( PInverse( previous ), moved ) );
				QueueEvent( session->instance, buffer );
			}
			Log( "{\"event\":\"reference_space_change_pending\",\"change_time\":%lld,\"next_display_time\":%lld}",
				static_cast<long long>( g.localChangeTime ), static_cast<long long>( session->nextDisplayTime ) );
		} else if ( command == "focus" ) {
			const bool focus = f( 1, 1.0f ) >= 0.5f;
			if ( session->running && focus != session->focused ) {
				session->focused = focus;
				QueueStateChange( session, focus ? XR_SESSION_STATE_FOCUSED : XR_SESSION_STATE_VISIBLE );
			}
		} else if ( command == "exit" ) {
			if ( session->running && !session->exitRequested ) {
				session->exitRequested = true;
				QueueStateChange( session, XR_SESSION_STATE_STOPPING );
			}
		}
		Log( "{\"event\":\"script_command\",\"frame\":%llu,\"command\":\"%s\"}",
			static_cast<unsigned long long>( session->framesSubmitted ), JsonEscape( command ).c_str() );
	}
}

void AdvanceScript( TRSession *session ) {
	RuntimeGlobals &g = G();
	for ( ScriptEvent &event : g.script ) {
		if ( event.fired ) {
			continue;
		}
		if ( event.whenPath.empty() ) {
			if ( event.frame > session->framesSubmitted ) {
				continue;
			}
		} else if ( !FileExists( event.whenPath ) ) {
			continue;
		}
		event.fired = true;
		RunScriptCommand( session, event.args );
	}
}

/*
===============================================================================

	API: instance

===============================================================================
*/

// XR_MNDX_egl_enable, declared without EGL headers: XrGraphicsBindingEGLMNDX
// carries the getProcAddress function, then EGLDisplay, EGLConfig and
// EGLContext pointers.
#define TR_MNDX_EGL_ENABLE_EXTENSION_NAME "XR_MNDX_egl_enable"
const uint32_t TR_MNDX_EGL_ENABLE_SPEC_VERSION = 2;

struct TREGLBinding {
	XrStructureType	type;
	const void *	next;
	void *			getProcAddress;
	void *			display;
	void *			config;
	void *			context;
};

const char *const SUPPORTED_EXTENSIONS[] = {
	XR_KHR_OPENGL_ENABLE_EXTENSION_NAME,
	XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
	XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME,
	XR_EXT_LOCAL_FLOOR_EXTENSION_NAME,	// only while OPENQ4_XR_TEST_SPACES offers local_floor
#if defined( __linux__ )
	TR_MNDX_EGL_ENABLE_EXTENSION_NAME,
#endif
};

uint32_t ExtensionVersion( const char *name ) {
	if ( std::strcmp( name, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME ) == 0 ) {
		return XR_KHR_opengl_enable_SPEC_VERSION;
	}
	if ( std::strcmp( name, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME ) == 0 ) {
		return XR_KHR_vulkan_enable2_SPEC_VERSION;
	}
	if ( std::strcmp( name, XR_EXT_LOCAL_FLOOR_EXTENSION_NAME ) == 0 ) {
		return XR_EXT_local_floor_SPEC_VERSION;
	}
#if defined( __linux__ )
	if ( std::strcmp( name, TR_MNDX_EGL_ENABLE_EXTENSION_NAME ) == 0 ) {
		return TR_MNDX_EGL_ENABLE_SPEC_VERSION;
	}
#endif
	if ( std::strcmp( name, XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME ) == 0 ) {
		return XR_KHR_composition_layer_cylinder_SPEC_VERSION;
	}
	return 0;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrEnumerateInstanceExtensionProperties( const char *layerName, uint32_t capacity, uint32_t *countOutput, XrExtensionProperties *properties ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	InitGlobals();
	if ( layerName != nullptr && layerName[0] != '\0' ) {
		return XR_ERROR_API_LAYER_NOT_PRESENT;
	}
	std::vector<XrExtensionProperties> values;
	for ( const char *name : SUPPORTED_EXTENSIONS ) {
		if ( std::strcmp( name, XR_EXT_LOCAL_FLOOR_EXTENSION_NAME ) == 0 && !G().offerLocalFloor ) {
			continue;
		}
		XrExtensionProperties p = {};
		p.type = XR_TYPE_EXTENSION_PROPERTIES;
		std::snprintf( p.extensionName, sizeof( p.extensionName ), "%s", name );
		p.extensionVersion = ExtensionVersion( name );
		values.push_back( p );
	}
	if ( countOutput == nullptr ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	*countOutput = static_cast<uint32_t>( values.size() );
	if ( capacity == 0 ) {
		return XR_SUCCESS;
	}
	if ( capacity < values.size() ) {
		return XR_ERROR_SIZE_INSUFFICIENT;
	}
	for ( size_t i = 0; i < values.size(); ++i ) {
		if ( properties[i].type != XR_TYPE_EXTENSION_PROPERTIES ) {
			return XR_ERROR_VALIDATION_FAILURE;
		}
		std::memcpy( properties[i].extensionName, values[i].extensionName, sizeof( values[i].extensionName ) );
		properties[i].extensionVersion = values[i].extensionVersion;
	}
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrCreateInstance( const XrInstanceCreateInfo *createInfo, XrInstance *instance ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	InitGlobals();
	if ( createInfo == nullptr || instance == nullptr || createInfo->type != XR_TYPE_INSTANCE_CREATE_INFO ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( G().instance != nullptr ) {
		return XR_ERROR_LIMIT_REACHED;
	}
	const XrVersion requested = createInfo->applicationInfo.apiVersion;
	if ( XR_VERSION_MAJOR( requested ) != 1 || XR_VERSION_MINOR( requested ) > 0 ) {
		Log( "{\"event\":\"create_instance_rejected\",\"api_major\":%u,\"api_minor\":%u}",
			static_cast<unsigned>( XR_VERSION_MAJOR( requested ) ), static_cast<unsigned>( XR_VERSION_MINOR( requested ) ) );
		return XR_ERROR_API_VERSION_UNSUPPORTED;
	}
	std::unique_ptr<TRInstance> created( new TRInstance() );
	created->magic = MAGIC_INSTANCE;
	created->session = nullptr;
	created->graphicsRequirementsQueried = false;
	created->profileChangedPending = false;
	std::string extensions;
	for ( uint32_t i = 0; i < createInfo->enabledExtensionCount; ++i ) {
		const char *name = createInfo->enabledExtensionNames[i];
		if ( ExtensionVersion( name ) == 0 ) {
			Log( "{\"event\":\"create_instance_rejected\",\"extension\":\"%s\"}", JsonEscape( name ).c_str() );
			return XR_ERROR_EXTENSION_NOT_PRESENT;
		}
		created->enabledExtensions.push_back( name );
		extensions += ( i ? "," : "" ) + std::string( "\"" ) + JsonEscape( name ) + "\"";
	}
	created->applicationName = createInfo->applicationInfo.applicationName;
	G().instance = created.release();
	G().liveHandles[ G().instance ] = MAGIC_INSTANCE;
	*instance = reinterpret_cast<XrInstance>( G().instance );
	Log( "{\"event\":\"create_instance\",\"application\":\"%s\",\"engine\":\"%s\",\"extensions\":[%s]}",
		JsonEscape( createInfo->applicationInfo.applicationName ).c_str(), JsonEscape( createInfo->applicationInfo.engineName ).c_str(), extensions.c_str() );
	return XR_SUCCESS;
}

bool InstanceHasExtension( TRInstance *instance, const char *name ) {
	return std::find( instance->enabledExtensions.begin(), instance->enabledExtensions.end(), name ) != instance->enabledExtensions.end();
}

void DestroySessionLocked( TRSession *session );

XRAPI_ATTR XrResult XRAPI_CALL TR_xrDestroyInstance( XrInstance handle ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRInstance *instance = GetInstance( handle );
	if ( instance == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( instance->session != nullptr ) {
		DestroySessionLocked( instance->session );
	}
	for ( TRActionSet *set : instance->actionSets ) {
		for ( TRAction *action : set->actions ) {
			G().liveHandles.erase( action );
			action->magic = 0;
			delete action;
		}
		G().liveHandles.erase( set );
		set->magic = 0;
		delete set;
	}
	G().liveHandles.erase( instance );
	instance->magic = 0;
	delete instance;
	G().instance = nullptr;
	G().instanceLost = G().sessionLost = false;
	Log( "{\"event\":\"destroy_instance\",\"live_handles\":%u}", static_cast<unsigned>( G().liveHandles.size() ) );
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrGetInstanceProperties( XrInstance handle, XrInstanceProperties *properties ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( GetInstance( handle ) == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( properties == nullptr || properties->type != XR_TYPE_INSTANCE_PROPERTIES ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	properties->runtimeVersion = XR_MAKE_VERSION( 0, 1, 0 );
	std::snprintf( properties->runtimeName, sizeof( properties->runtimeName ), "openQ4 test runtime" );
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrPollEvent( XrInstance handle, XrEventDataBuffer *eventData ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRInstance *instance = GetInstance( handle );
	if ( instance == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( eventData == nullptr || eventData->type != XR_TYPE_EVENT_DATA_BUFFER ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( instance->profileChangedPending && instance->session != nullptr ) {
		instance->profileChangedPending = false;
		XrEventDataBuffer buffer = {};
		XrEventDataInteractionProfileChanged *changed = reinterpret_cast<XrEventDataInteractionProfileChanged *>( &buffer );
		changed->type = XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED;
		changed->session = reinterpret_cast<XrSession>( instance->session );
		QueueEvent( instance, buffer );
	}
	if ( instance->events.empty() ) {
		return XR_EVENT_UNAVAILABLE;
	}
	*eventData = instance->events.front();
	instance->events.pop_front();
	return XR_SUCCESS;
}

#define TR_RESULT_CASE( name, value ) case name: return #name;
const char *ResultName( XrResult result ) {
	switch ( result ) {
		XR_LIST_ENUM_XrResult( TR_RESULT_CASE )
		default: return nullptr;
	}
}
#undef TR_RESULT_CASE

#define TR_STRUCT_CASE( name, value ) case name: return #name;
const char *StructureName( XrStructureType type ) {
	switch ( type ) {
		XR_LIST_ENUM_XrStructureType( TR_STRUCT_CASE )
		default: return nullptr;
	}
}
#undef TR_STRUCT_CASE

XRAPI_ATTR XrResult XRAPI_CALL TR_xrResultToString( XrInstance handle, XrResult value, char buffer[XR_MAX_RESULT_STRING_SIZE] ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( GetInstance( handle ) == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	const char *name = ResultName( value );
	if ( name != nullptr ) {
		std::snprintf( buffer, XR_MAX_RESULT_STRING_SIZE, "%s", name );
	} else {
		std::snprintf( buffer, XR_MAX_RESULT_STRING_SIZE, "XR_%s_%d", value < 0 ? "UNKNOWN_FAILURE" : "UNKNOWN_SUCCESS", static_cast<int>( value ) );
	}
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrStructureTypeToString( XrInstance handle, XrStructureType value, char buffer[XR_MAX_STRUCTURE_NAME_SIZE] ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( GetInstance( handle ) == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	const char *name = StructureName( value );
	if ( name != nullptr ) {
		std::snprintf( buffer, XR_MAX_STRUCTURE_NAME_SIZE, "%s", name );
	} else {
		std::snprintf( buffer, XR_MAX_STRUCTURE_NAME_SIZE, "XR_UNKNOWN_STRUCTURE_TYPE_%d", static_cast<int>( value ) );
	}
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrGetSystem( XrInstance handle, const XrSystemGetInfo *getInfo, XrSystemId *systemId ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( GetInstance( handle ) == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( getInfo == nullptr || systemId == nullptr || getInfo->type != XR_TYPE_SYSTEM_GET_INFO ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( getInfo->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY ) {
		return XR_ERROR_FORM_FACTOR_UNSUPPORTED;
	}
	*systemId = TR_SYSTEM_ID;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrGetSystemProperties( XrInstance handle, XrSystemId systemId, XrSystemProperties *properties ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( GetInstance( handle ) == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( systemId != TR_SYSTEM_ID ) {
		return XR_ERROR_SYSTEM_INVALID;
	}
	if ( properties == nullptr || properties->type != XR_TYPE_SYSTEM_PROPERTIES ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	properties->systemId = TR_SYSTEM_ID;
	properties->vendorId = 0x4F51;
	std::snprintf( properties->systemName, sizeof( properties->systemName ), "openQ4 simulated headset" );
	properties->graphicsProperties.maxSwapchainImageWidth = 4096;
	properties->graphicsProperties.maxSwapchainImageHeight = 4096;
	properties->graphicsProperties.maxLayerCount = XR_MIN_COMPOSITION_LAYERS_SUPPORTED;
	properties->trackingProperties.orientationTracking = XR_TRUE;
	properties->trackingProperties.positionTracking = XR_TRUE;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrEnumerateEnvironmentBlendModes( XrInstance handle, XrSystemId systemId, XrViewConfigurationType viewConfigurationType,
		uint32_t capacity, uint32_t *countOutput, XrEnvironmentBlendMode *modes ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( GetInstance( handle ) == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( systemId != TR_SYSTEM_ID ) {
		return XR_ERROR_SYSTEM_INVALID;
	}
	if ( viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO ) {
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
	}
	return TwoCall( capacity, countOutput, modes, std::vector<XrEnvironmentBlendMode>{ XR_ENVIRONMENT_BLEND_MODE_OPAQUE } );
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrEnumerateViewConfigurations( XrInstance handle, XrSystemId systemId, uint32_t capacity, uint32_t *countOutput,
		XrViewConfigurationType *types ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( GetInstance( handle ) == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( systemId != TR_SYSTEM_ID ) {
		return XR_ERROR_SYSTEM_INVALID;
	}
	return TwoCall( capacity, countOutput, types, std::vector<XrViewConfigurationType>{ XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO } );
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrGetViewConfigurationProperties( XrInstance handle, XrSystemId systemId, XrViewConfigurationType type,
		XrViewConfigurationProperties *properties ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( GetInstance( handle ) == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( systemId != TR_SYSTEM_ID ) {
		return XR_ERROR_SYSTEM_INVALID;
	}
	if ( type != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO ) {
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
	}
	if ( properties == nullptr || properties->type != XR_TYPE_VIEW_CONFIGURATION_PROPERTIES ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	properties->viewConfigurationType = type;
	properties->fovMutable = XR_FALSE;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrEnumerateViewConfigurationViews( XrInstance handle, XrSystemId systemId, XrViewConfigurationType type,
		uint32_t capacity, uint32_t *countOutput, XrViewConfigurationView *views ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( GetInstance( handle ) == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( systemId != TR_SYSTEM_ID ) {
		return XR_ERROR_SYSTEM_INVALID;
	}
	if ( type != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO ) {
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
	}
	if ( countOutput == nullptr ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	*countOutput = 2;
	if ( capacity == 0 ) {
		return XR_SUCCESS;
	}
	if ( capacity < 2 || views == nullptr ) {
		return XR_ERROR_SIZE_INSUFFICIENT;
	}
	for ( int i = 0; i < 2; ++i ) {
		if ( views[i].type != XR_TYPE_VIEW_CONFIGURATION_VIEW ) {
			return XR_ERROR_VALIDATION_FAILURE;
		}
		views[i].recommendedImageRectWidth = static_cast<uint32_t>( G().eyeWidth );
		views[i].recommendedImageRectHeight = static_cast<uint32_t>( G().eyeHeight );
		views[i].maxImageRectWidth = 4096;
		views[i].maxImageRectHeight = 4096;
		views[i].recommendedSwapchainSampleCount = 1;
		views[i].maxSwapchainSampleCount = 1;
	}
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrGetOpenGLGraphicsRequirementsKHR( XrInstance handle, XrSystemId systemId, XrGraphicsRequirementsOpenGLKHR *requirements ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRInstance *instance = GetInstance( handle );
	if ( instance == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( !InstanceHasExtension( instance, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME ) ) {
		return XR_ERROR_FUNCTION_UNSUPPORTED;
	}
	if ( systemId != TR_SYSTEM_ID ) {
		return XR_ERROR_SYSTEM_INVALID;
	}
	if ( requirements == nullptr || requirements->type != XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	requirements->minApiVersionSupported = XR_MAKE_VERSION( 3, 0, 0 );
	requirements->maxApiVersionSupported = XR_MAKE_VERSION( 4, 6, 0 );
	instance->graphicsRequirementsQueried = true;
	return XR_SUCCESS;
}

/*
===============================================================================

	API: XR_KHR_vulkan_enable2

	The runtime creates the application's instance and device through the
	application's own loader, as real runtimes do, and names the GPU the
	simulated headset renders with.

===============================================================================
*/

TRInstance *GetVulkanInstance( XrInstance handle, XrSystemId systemId, XrResult &result ) {
	TRInstance *instance = GetInstance( handle );
	result = XR_SUCCESS;
	if ( instance == nullptr ) {
		result = XR_ERROR_HANDLE_INVALID;
	} else if ( !InstanceHasExtension( instance, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME ) ) {
		result = XR_ERROR_FUNCTION_UNSUPPORTED;
	} else if ( systemId != TR_SYSTEM_ID ) {
		result = XR_ERROR_SYSTEM_INVALID;
	}
	return result == XR_SUCCESS ? instance : nullptr;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrGetVulkanGraphicsRequirements2KHR( XrInstance handle, XrSystemId systemId, XrGraphicsRequirementsVulkanKHR *requirements ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	XrResult result = XR_SUCCESS;
	TRInstance *instance = GetVulkanInstance( handle, systemId, result );
	if ( instance == nullptr ) {
		return result;
	}
	if ( requirements == nullptr || requirements->type != XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	requirements->minApiVersionSupported = XR_MAKE_VERSION( 1, 0, 0 );
	requirements->maxApiVersionSupported = XR_MAKE_VERSION( 1, 3, 0 );
	instance->graphicsRequirementsQueried = true;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrCreateVulkanInstanceKHR( XrInstance handle, const XrVulkanInstanceCreateInfoKHR *createInfo,
		VkInstance *vulkanInstance, VkResult *vulkanResult ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( createInfo == nullptr || vulkanInstance == nullptr || vulkanResult == nullptr || createInfo->type != XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR
			|| createInfo->pfnGetInstanceProcAddr == nullptr || createInfo->vulkanCreateInfo == nullptr ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	XrResult result = XR_SUCCESS;
	TRInstance *instance = GetVulkanInstance( handle, createInfo->systemId, result );
	if ( instance == nullptr ) {
		return result;
	}
	if ( !instance->graphicsRequirementsQueried ) {
		return XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING;
	}
	PFN_vkCreateInstance createInstance = reinterpret_cast<PFN_vkCreateInstance>(
		createInfo->pfnGetInstanceProcAddr( VK_NULL_HANDLE, "vkCreateInstance" ) );
	if ( createInstance == nullptr ) {
		return XR_ERROR_RUNTIME_FAILURE;
	}
	*vulkanResult = createInstance( createInfo->vulkanCreateInfo, createInfo->vulkanAllocator, vulkanInstance );
	const VkApplicationInfo *application = createInfo->vulkanCreateInfo->pApplicationInfo;
	Log( "{\"event\":\"create_vulkan_instance\",\"result\":%d,\"api_major\":%u,\"api_minor\":%u,\"extensions\":%u,\"layers\":%u}",
		static_cast<int>( *vulkanResult ),
		application != nullptr ? VK_API_VERSION_MAJOR( application->apiVersion ) : 1u,
		application != nullptr ? VK_API_VERSION_MINOR( application->apiVersion ) : 0u,
		createInfo->vulkanCreateInfo->enabledExtensionCount, createInfo->vulkanCreateInfo->enabledLayerCount );
	if ( *vulkanResult == VK_SUCCESS ) {
		instance->vkGetInstanceProcAddr = createInfo->pfnGetInstanceProcAddr;
		instance->vkInstance = *vulkanInstance;
		instance->vkPhysicalDevice = VK_NULL_HANDLE;
		instance->vkDevice = VK_NULL_HANDLE;
	}
	return XR_SUCCESS;
}

// The GPU the simulated headset renders with: OPENQ4_XR_TEST_VK_DEVICE, or the
// first discrete GPU, or the first integrated one, or the first device.
XRAPI_ATTR XrResult XRAPI_CALL TR_xrGetVulkanGraphicsDevice2KHR( XrInstance handle, const XrVulkanGraphicsDeviceGetInfoKHR *getInfo,
		VkPhysicalDevice *vulkanPhysicalDevice ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( getInfo == nullptr || vulkanPhysicalDevice == nullptr || getInfo->type != XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	XrResult result = XR_SUCCESS;
	TRInstance *instance = GetVulkanInstance( handle, getInfo->systemId, result );
	if ( instance == nullptr ) {
		return result;
	}
	if ( getInfo->vulkanInstance == VK_NULL_HANDLE || getInfo->vulkanInstance != instance->vkInstance ) {
		Log( "{\"event\":\"vulkan_device_rejected\",\"reason\":\"instance not created by xrCreateVulkanInstanceKHR\"}" );
		return XR_ERROR_VALIDATION_FAILURE;
	}
	PFN_vkEnumeratePhysicalDevices enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
		instance->vkGetInstanceProcAddr( instance->vkInstance, "vkEnumeratePhysicalDevices" ) );
	PFN_vkGetPhysicalDeviceProperties properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
		instance->vkGetInstanceProcAddr( instance->vkInstance, "vkGetPhysicalDeviceProperties" ) );
	if ( enumerate == nullptr || properties == nullptr ) {
		return XR_ERROR_RUNTIME_FAILURE;
	}
	uint32_t count = 0;
	if ( enumerate( instance->vkInstance, &count, nullptr ) != VK_SUCCESS || count == 0 ) {
		return XR_ERROR_RUNTIME_FAILURE;
	}
	std::vector<VkPhysicalDevice> devices( count );
	if ( enumerate( instance->vkInstance, &count, devices.data() ) != VK_SUCCESS ) {
		return XR_ERROR_RUNTIME_FAILURE;
	}
	int chosen = -1;
	const char *forced = std::getenv( "OPENQ4_XR_TEST_VK_DEVICE" );
	if ( forced != nullptr && forced[0] != '\0' ) {
		chosen = std::atoi( forced );
		if ( chosen < 0 || chosen >= static_cast<int>( count ) ) {
			return XR_ERROR_RUNTIME_FAILURE;
		}
	}
	const VkPhysicalDeviceType preference[] = { VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU };
	for ( VkPhysicalDeviceType type : preference ) {
		for ( uint32_t i = 0; i < count && chosen < 0; ++i ) {
			VkPhysicalDeviceProperties p = {};
			properties( devices[i], &p );
			if ( p.deviceType == type ) {
				chosen = static_cast<int>( i );
			}
		}
	}
	if ( chosen < 0 ) {
		chosen = 0;
	}
	VkPhysicalDeviceProperties p = {};
	properties( devices[static_cast<size_t>( chosen )], &p );
	instance->vkPhysicalDevice = devices[static_cast<size_t>( chosen )];
	*vulkanPhysicalDevice = instance->vkPhysicalDevice;
	Log( "{\"event\":\"vulkan_device\",\"index\":%d,\"name\":\"%s\"}", chosen, JsonEscape( p.deviceName ).c_str() );
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrCreateVulkanDeviceKHR( XrInstance handle, const XrVulkanDeviceCreateInfoKHR *createInfo,
		VkDevice *vulkanDevice, VkResult *vulkanResult ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( createInfo == nullptr || vulkanDevice == nullptr || vulkanResult == nullptr || createInfo->type != XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR
			|| createInfo->pfnGetInstanceProcAddr == nullptr || createInfo->vulkanCreateInfo == nullptr ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	XrResult result = XR_SUCCESS;
	TRInstance *instance = GetVulkanInstance( handle, createInfo->systemId, result );
	if ( instance == nullptr ) {
		return result;
	}
	if ( createInfo->vulkanPhysicalDevice == VK_NULL_HANDLE || createInfo->vulkanPhysicalDevice != instance->vkPhysicalDevice ) {
		Log( "{\"event\":\"create_vulkan_device_rejected\",\"reason\":\"not the device xrGetVulkanGraphicsDevice2KHR named\"}" );
		return XR_ERROR_VALIDATION_FAILURE;
	}
	PFN_vkCreateDevice createDevice = reinterpret_cast<PFN_vkCreateDevice>(
		createInfo->pfnGetInstanceProcAddr( instance->vkInstance, "vkCreateDevice" ) );
	if ( createDevice == nullptr ) {
		return XR_ERROR_RUNTIME_FAILURE;
	}
	*vulkanResult = createDevice( createInfo->vulkanPhysicalDevice, createInfo->vulkanCreateInfo, createInfo->vulkanAllocator, vulkanDevice );
	Log( "{\"event\":\"create_vulkan_device\",\"result\":%d,\"extensions\":%u,\"queue_families\":%u}",
		static_cast<int>( *vulkanResult ), createInfo->vulkanCreateInfo->enabledExtensionCount,
		createInfo->vulkanCreateInfo->queueCreateInfoCount );
	if ( *vulkanResult == VK_SUCCESS ) {
		instance->vkDevice = *vulkanDevice;
	}
	return XR_SUCCESS;
}

/*
===============================================================================

	API: session

===============================================================================
*/

XRAPI_ATTR XrResult XRAPI_CALL TR_xrCreateSession( XrInstance handle, const XrSessionCreateInfo *createInfo, XrSession *session ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRInstance *instance = GetInstance( handle );
	if ( instance == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( createInfo == nullptr || session == nullptr || createInfo->type != XR_TYPE_SESSION_CREATE_INFO ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( createInfo->systemId != TR_SYSTEM_ID ) {
		return XR_ERROR_SYSTEM_INVALID;
	}
	if ( instance->session != nullptr ) {
		return XR_ERROR_LIMIT_REACHED;
	}
	if ( !instance->graphicsRequirementsQueried ) {
		Log( "{\"event\":\"create_session_rejected\",\"reason\":\"graphics requirements not queried\"}" );
		return XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING;
	}
	// The binding must be a GL binding; the test runtime renders nothing itself,
	// so only the presence of a current context matters.
	const XrBaseInStructure *binding = reinterpret_cast<const XrBaseInStructure *>( createInfo->next );
	const char *bindingName = "none";
	while ( binding != nullptr ) {
		if ( binding->type == XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR ) {
			bindingName = "opengl_win32";
			break;
		}
		if ( binding->type == XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR ) {
			bindingName = "opengl_xlib";
			break;
		}
		if ( binding->type == XR_TYPE_GRAPHICS_BINDING_OPENGL_XCB_KHR ) {
			bindingName = "opengl_xcb";
			break;
		}
		if ( binding->type == XR_TYPE_GRAPHICS_BINDING_OPENGL_WAYLAND_KHR ) {
			bindingName = "opengl_wayland";
			break;
		}
		if ( binding->type == XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR ) {
			bindingName = "vulkan";
			break;
		}
		if ( binding->type == XR_TYPE_GRAPHICS_BINDING_EGL_MNDX ) {
			bindingName = "opengl_egl";
			break;
		}
		binding = binding->next;
	}
	if ( binding == nullptr ) {
		Log( "{\"event\":\"create_session_rejected\",\"reason\":\"no OpenGL or Vulkan graphics binding\"}" );
		return XR_ERROR_GRAPHICS_DEVICE_INVALID;
	}
	if ( binding->type == XR_TYPE_GRAPHICS_BINDING_EGL_MNDX ) {
		// the binding names the context current on this thread, as the GL binding rule has it
		const TREGLBinding *egl = reinterpret_cast<const TREGLBinding *>( binding );
		bool current = false;
#if defined( __linux__ )
		void *library = dlopen( "libEGL.so.1", RTLD_NOW | RTLD_NOLOAD );
		typedef void *( *getCurrent_t )( void );
		getCurrent_t getCurrentContext = library != nullptr
			? reinterpret_cast<getCurrent_t>( dlsym( library, "eglGetCurrentContext" ) ) : nullptr;
		current = getCurrentContext != nullptr && egl->context != nullptr && getCurrentContext() == egl->context;
		if ( library != nullptr ) {
			dlclose( library );
		}
#endif
		if ( !InstanceHasExtension( instance, TR_MNDX_EGL_ENABLE_EXTENSION_NAME ) || !InstanceHasExtension( instance, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME )
				|| egl->getProcAddress == nullptr || egl->display == nullptr || egl->config == nullptr || !current ) {
			Log( "{\"event\":\"create_session_rejected\",\"reason\":\"binding is not the current EGL context\"}" );
			return XR_ERROR_GRAPHICS_DEVICE_INVALID;
		}
	}
	const XrGraphicsBindingVulkanKHR *vulkanBinding = nullptr;
	if ( binding->type == XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR ) {
		vulkanBinding = reinterpret_cast<const XrGraphicsBindingVulkanKHR *>( binding );
		// XR_KHR_vulkan_enable2: only the objects the runtime created may be bound
		if ( !InstanceHasExtension( instance, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME ) || vulkanBinding->instance == VK_NULL_HANDLE
				|| vulkanBinding->instance != instance->vkInstance || vulkanBinding->physicalDevice != instance->vkPhysicalDevice
				|| vulkanBinding->device == VK_NULL_HANDLE || vulkanBinding->device != instance->vkDevice ) {
			Log( "{\"event\":\"create_session_rejected\",\"reason\":\"Vulkan binding names objects the runtime did not create\"}" );
			return XR_ERROR_GRAPHICS_DEVICE_INVALID;
		}
	}
#if defined( _WIN32 )
	if ( binding->type == XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR ) {
		const XrGraphicsBindingOpenGLWin32KHR *win32 = reinterpret_cast<const XrGraphicsBindingOpenGLWin32KHR *>( binding );
		if ( win32->hDC == nullptr || win32->hGLRC == nullptr || wglGetCurrentContext() != win32->hGLRC ) {
			Log( "{\"event\":\"create_session_rejected\",\"reason\":\"binding is not the current WGL context\"}" );
			return XR_ERROR_GRAPHICS_DEVICE_INVALID;
		}
	}
#endif

	TRSession *created = new TRSession();
	created->magic = MAGIC_SESSION;
	G().liveHandles[ created ] = MAGIC_SESSION;
	created->instance = instance;
	created->vulkan = false;
	created->vkPhysicalDevice = VK_NULL_HANDLE;
	created->vkDevice = VK_NULL_HANDLE;
	created->vkQueue = VK_NULL_HANDLE;
	created->vkCommandPool = VK_NULL_HANDLE;
	created->vkReadback = VK_NULL_HANDLE;
	created->vkReadbackMemory = VK_NULL_HANDLE;
	created->vkReadbackSize = 0;
	if ( vulkanBinding != nullptr ) {
		created->vulkan = true;
		created->vkPhysicalDevice = vulkanBinding->physicalDevice;
		created->vkDevice = vulkanBinding->device;
		bool ready = LoadVulkanFns( instance->vkGetInstanceProcAddr, vulkanBinding->instance, vulkanBinding->device, created->vk );
		if ( ready ) {
			created->vk.GetDeviceQueue( created->vkDevice, vulkanBinding->queueFamilyIndex, vulkanBinding->queueIndex, &created->vkQueue );
			VkCommandPoolCreateInfo poolInfo = {};
			poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
			poolInfo.queueFamilyIndex = vulkanBinding->queueFamilyIndex;
			ready = created->vkQueue != VK_NULL_HANDLE
				&& created->vk.CreateCommandPool( created->vkDevice, &poolInfo, nullptr, &created->vkCommandPool ) == VK_SUCCESS;
		}
		if ( !ready ) {
			delete created;
			Log( "{\"event\":\"create_session_rejected\",\"reason\":\"Vulkan device unusable\"}" );
			return XR_ERROR_GRAPHICS_DEVICE_INVALID;
		}
	}
	created->state = XR_SESSION_STATE_UNKNOWN;
	created->running = false;
	created->exitRequested = false;
	created->focused = true;
	created->viewConfiguration = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	created->currentProfile = XR_NULL_PATH;
	created->period = G().refreshRate > 0.0 ? static_cast<int64_t>( 1e9 / G().refreshRate ) : 11111111;
	created->nextDisplayTime = NowNanoseconds() + created->period;
	created->frameWaited = false;
	created->frameBegun = false;
	created->waitedDisplayTime = 0;
	created->framesSubmitted = 0;
	created->framesWaited = 0;
	instance->session = created;
	*session = reinterpret_cast<XrSession>( created );
	Log( "{\"event\":\"create_session\",\"binding\":\"%s\"}", bindingName );
	QueueStateChange( created, XR_SESSION_STATE_IDLE );
	QueueStateChange( created, XR_SESSION_STATE_READY );
	return XR_SUCCESS;
}

void DestroySpaceLocked( TRSpace *space ) {
	G().liveHandles.erase( space );
	space->magic = 0;
	delete space;
}

void DestroySwapchainLocked( TRSwapchain *swapchain ) {
	Log( "{\"event\":\"destroy_swapchain\",\"cancelled_images\":%u,\"waited\":%s}",
		static_cast<unsigned>( swapchain->acquired.size() ), swapchain->waited ? "true" : "false" );
	if ( !swapchain->textures.empty() ) {
		glDeleteTextures( static_cast<GLsizei>( swapchain->textures.size() ), swapchain->textures.data() );
	}
	// OpenXR requires the application's commands on these images to have
	// completed; a Vulkan validation layer reports any that have not
	const TRSession *session = swapchain->session;
	for ( size_t i = 0; i < swapchain->vkImages.size(); ++i ) {
		if ( swapchain->vkImages[i] != VK_NULL_HANDLE ) {
			session->vk.DestroyImage( session->vkDevice, swapchain->vkImages[i], nullptr );
		}
	}
	for ( size_t i = 0; i < swapchain->vkMemory.size(); ++i ) {
		if ( swapchain->vkMemory[i] != VK_NULL_HANDLE ) {
			session->vk.FreeMemory( session->vkDevice, swapchain->vkMemory[i], nullptr );
		}
	}
	G().liveHandles.erase( swapchain );
	swapchain->magic = 0;
	delete swapchain;
}

void DestroySessionLocked( TRSession *session ) {
	Log( "{\"event\":\"session_ownership\",\"frames\":%llu,\"abandoned_frame\":%s,\"waited_frame\":%s}",
		static_cast<unsigned long long>( session->framesSubmitted ), session->frameBegun ? "true" : "false", session->frameWaited ? "true" : "false" );
	for ( TRSpace *space : session->spaces ) {
		DestroySpaceLocked( space );
	}
	for ( TRSwapchain *swapchain : session->swapchains ) {
		DestroySwapchainLocked( swapchain );
	}
	if ( session->vulkan ) {
		if ( session->vkReadback != VK_NULL_HANDLE ) {
			session->vk.DestroyBuffer( session->vkDevice, session->vkReadback, nullptr );
		}
		if ( session->vkReadbackMemory != VK_NULL_HANDLE ) {
			session->vk.FreeMemory( session->vkDevice, session->vkReadbackMemory, nullptr );
		}
		if ( session->vkCommandPool != VK_NULL_HANDLE ) {
			session->vk.DestroyCommandPool( session->vkDevice, session->vkCommandPool, nullptr );
		}
	}
	session->instance->session = nullptr;
	// drop queued events that name this session
	G().liveHandles.erase( session );
	session->magic = 0;
	delete session;
	G().sessionLost = false;
	Log( "{\"event\":\"destroy_session\"}" );
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrDestroySession( XrSession handle ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	TRInstance *instance = session->instance;
	DestroySessionLocked( session );
	std::deque<XrEventDataBuffer> kept;
	for ( const XrEventDataBuffer &event : instance->events ) {
		if ( event.type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED && event.type != XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED
				&& event.type != XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING ) {
			kept.push_back( event );
		}
	}
	instance->events.swap( kept );
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrBeginSession( XrSession handle, const XrSessionBeginInfo *beginInfo ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( beginInfo == nullptr || beginInfo->type != XR_TYPE_SESSION_BEGIN_INFO ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( beginInfo->primaryViewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO ) {
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
	}
	if ( session->running ) {
		return XR_ERROR_SESSION_RUNNING;
	}
	if ( session->state != XR_SESSION_STATE_READY ) {
		return XR_ERROR_SESSION_NOT_READY;
	}
	session->running = true;
	session->nextDisplayTime = NowNanoseconds() + session->period;
	QueueStateChange( session, XR_SESSION_STATE_SYNCHRONIZED );
	QueueStateChange( session, XR_SESSION_STATE_VISIBLE );
	if ( session->focused ) {
		QueueStateChange( session, XR_SESSION_STATE_FOCUSED );
	}
	Log( "{\"event\":\"begin_session\"}" );
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrEndSession( XrSession handle ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( !session->running ) {
		return XR_ERROR_SESSION_NOT_RUNNING;
	}
	if ( session->state != XR_SESSION_STATE_STOPPING ) {
		return XR_ERROR_SESSION_NOT_STOPPING;
	}
	session->running = false;
	session->frameWaited = false;
	session->frameBegun = false;
	QueueStateChange( session, XR_SESSION_STATE_IDLE );
	if ( session->exitRequested ) {
		QueueStateChange( session, XR_SESSION_STATE_EXITING );
	}
	Log( "{\"event\":\"end_session\",\"frames\":%llu}", static_cast<unsigned long long>( session->framesSubmitted ) );
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrRequestExitSession( XrSession handle ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( !session->running ) {
		return XR_ERROR_SESSION_NOT_RUNNING;
	}
	session->exitRequested = true;
	if ( session->state != XR_SESSION_STATE_STOPPING ) {
		QueueStateChange( session, XR_SESSION_STATE_STOPPING );
	}
	Log( "{\"event\":\"request_exit\"}" );
	return XR_SUCCESS;
}

/*
===============================================================================

	API: spaces

===============================================================================
*/

XRAPI_ATTR XrResult XRAPI_CALL TR_xrEnumerateReferenceSpaces( XrSession handle, uint32_t capacity, uint32_t *countOutput, XrReferenceSpaceType *spaces ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( GetSession( handle ) == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	std::vector<XrReferenceSpaceType> offered{ XR_REFERENCE_SPACE_TYPE_VIEW, XR_REFERENCE_SPACE_TYPE_LOCAL };
	if ( G().offerStage ) {
		offered.push_back( XR_REFERENCE_SPACE_TYPE_STAGE );
	}
	if ( G().offerLocalFloor && InstanceHasExtension( GetSession( handle )->instance, XR_EXT_LOCAL_FLOOR_EXTENSION_NAME ) ) {
		offered.push_back( XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT );
	}
	return TwoCall( capacity, countOutput, spaces, offered );
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrCreateReferenceSpace( XrSession handle, const XrReferenceSpaceCreateInfo *createInfo, XrSpace *space ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( createInfo == nullptr || space == nullptr || createInfo->type != XR_TYPE_REFERENCE_SPACE_CREATE_INFO ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	const bool offered = createInfo->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_VIEW
		|| createInfo->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL
		|| ( createInfo->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE && G().offerStage )
		|| ( createInfo->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT && G().offerLocalFloor
			&& InstanceHasExtension( session->instance, XR_EXT_LOCAL_FLOOR_EXTENSION_NAME ) );
	if ( !offered ) {
		return XR_ERROR_REFERENCE_SPACE_UNSUPPORTED;
	}
	Log( "{\"event\":\"create_reference_space\",\"type\":%d}", static_cast<int>( createInfo->referenceSpaceType ) );
	if ( !PoseValid( createInfo->poseInReferenceSpace ) ) {
		return XR_ERROR_POSE_INVALID;
	}
	TRSpace *created = new TRSpace();
	created->magic = MAGIC_SPACE;
	G().liveHandles[ created ] = MAGIC_SPACE;
	created->session = session;
	created->isAction = false;
	created->referenceType = createInfo->referenceSpaceType;
	created->action = nullptr;
	created->subactionPath = XR_NULL_PATH;
	created->offset = FromXr( createInfo->poseInReferenceSpace );
	session->spaces.push_back( created );
	*space = reinterpret_cast<XrSpace>( created );
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrGetReferenceSpaceBoundsRect( XrSession handle, XrReferenceSpaceType type, XrExtent2Df *bounds ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( GetSession( handle ) == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( bounds == nullptr ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( type == XR_REFERENCE_SPACE_TYPE_STAGE && G().offerStage ) {
		bounds->width = 2.0f;
		bounds->height = 2.0f;
		return XR_SUCCESS;
	}
	bounds->width = bounds->height = 0.0f;
	return XR_SPACE_BOUNDS_UNAVAILABLE;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrCreateActionSpace( XrSession handle, const XrActionSpaceCreateInfo *createInfo, XrSpace *space ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( Fault( "xrCreateActionSpace" ) ) return FaultResult();
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( createInfo == nullptr || space == nullptr || createInfo->type != XR_TYPE_ACTION_SPACE_CREATE_INFO ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	TRAction *action = Check( reinterpret_cast<TRAction *>( createInfo->action ), MAGIC_ACTION );
	if ( action == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( action->type != XR_ACTION_TYPE_POSE_INPUT ) {
		return XR_ERROR_ACTION_TYPE_MISMATCH;
	}
	if ( createInfo->subactionPath != XR_NULL_PATH
		&& std::find( action->subactionPaths.begin(), action->subactionPaths.end(), createInfo->subactionPath ) == action->subactionPaths.end() ) {
		return XR_ERROR_PATH_UNSUPPORTED;
	}
	if ( !PoseValid( createInfo->poseInActionSpace ) ) {
		return XR_ERROR_POSE_INVALID;
	}
	TRSpace *created = new TRSpace();
	created->magic = MAGIC_SPACE;
	G().liveHandles[ created ] = MAGIC_SPACE;
	created->session = session;
	created->isAction = true;
	created->referenceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	created->action = action;
	created->subactionPath = createInfo->subactionPath;
	created->offset = FromXr( createInfo->poseInActionSpace );
	session->spaces.push_back( created );
	*space = reinterpret_cast<XrSpace>( created );
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrLocateSpace( XrSpace spaceHandle, XrSpace baseHandle, XrTime time, XrSpaceLocation *location ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( Fault( "xrLocateSpace" ) ) return FaultResult();
	TRSpace *space = Check( reinterpret_cast<TRSpace *>( spaceHandle ), MAGIC_SPACE );
	TRSpace *base = Check( reinterpret_cast<TRSpace *>( baseHandle ), MAGIC_SPACE );
	if ( space == nullptr || base == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( location == nullptr || location->type != XR_TYPE_SPACE_LOCATION ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( time <= 0 ) {
		return XR_ERROR_TIME_INVALID;
	}
	Pose spacePose, basePose;
	G().locateTime = time;
	const bool spaceTracked = StagePoseOfSpace( space, spacePose );
	const bool baseTracked = StagePoseOfSpace( base, basePose );
	if ( !spaceTracked || !baseTracked ) {
		location->locationFlags = 0;
		location->pose = ToXr( PIdentity() );
		return XR_SUCCESS;
	}
	if ( space->isAction ) {
		// an action space is only located while its action is bound and active
		const TRAction::SyncedState *state = nullptr;
		std::map<XrPath, TRAction::SyncedState>::const_iterator it = space->action->state.find( space->subactionPath );
		if ( it != space->action->state.end() ) {
			state = &it->second;
		}
		if ( state == nullptr || !state->active ) {
			location->locationFlags = 0;
			location->pose = ToXr( PIdentity() );
			return XR_SUCCESS;
		}
	}
	location->pose = ToXr( PMul( PInverse( basePose ), spacePose ) );
	location->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT
		| XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrDestroySpace( XrSpace handle ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRSpace *space = Check( reinterpret_cast<TRSpace *>( handle ), MAGIC_SPACE );
	if ( space == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	std::vector<TRSpace *> &spaces = space->session->spaces;
	spaces.erase( std::remove( spaces.begin(), spaces.end(), space ), spaces.end() );
	DestroySpaceLocked( space );
	return XR_SUCCESS;
}

/*
===============================================================================

	API: swapchains

===============================================================================
*/

const std::vector<int64_t> &VulkanSwapchainFormats() {
	static std::vector<int64_t> formats;
	if ( formats.empty() ) {
		const char *list = std::getenv( "OPENQ4_XR_TEST_VK_FORMATS" );
		if ( list != nullptr && list[0] != '\0' ) {
			std::stringstream stream( list );
			std::string item;
			while ( std::getline( stream, item, ',' ) ) {
				const long long value = std::atoll( item.c_str() );
				if ( value > 0 ) {
					formats.push_back( value );
				}
			}
		}
		if ( formats.empty() ) {
			formats = { VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM };
		}
	}
	return formats;
}

// Device-local images on the application's device, in COLOR_ATTACHMENT_OPTIMAL
// as an application finds them after xrWaitSwapchainImage. They are always
// transfer sources so captures can read them back.
bool CreateVulkanSwapchainImages( TRSwapchain *swapchain ) {
	const TRSession *session = swapchain->session;
	const XrSwapchainCreateInfo &info = swapchain->info;
	VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	if ( info.usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT ) {
		usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}
	if ( info.usageFlags & XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT ) {
		usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}
	if ( info.usageFlags & XR_SWAPCHAIN_USAGE_SAMPLED_BIT ) {
		usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
	}
	if ( info.usageFlags & XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT ) {
		usage |= VK_IMAGE_USAGE_STORAGE_BIT;
	}
	swapchain->vkImages.assign( swapchain->imageCount, VK_NULL_HANDLE );
	swapchain->vkMemory.assign( swapchain->imageCount, VK_NULL_HANDLE );
	for ( uint32_t i = 0; i < swapchain->imageCount; ++i ) {
		VkImageCreateInfo imageInfo = {};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		if ( info.usageFlags & XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT ) {
			imageInfo.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
		}
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = static_cast<VkFormat>( info.format );
		imageInfo.extent.width = info.width;
		imageInfo.extent.height = info.height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = usage;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if ( session->vk.CreateImage( session->vkDevice, &imageInfo, nullptr, &swapchain->vkImages[i] ) != VK_SUCCESS ) {
			swapchain->vkImages[i] = VK_NULL_HANDLE;
			return false;
		}
		VkMemoryRequirements requirements = {};
		session->vk.GetImageMemoryRequirements( session->vkDevice, swapchain->vkImages[i], &requirements );
		VkMemoryAllocateInfo allocateInfo = {};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = requirements.size;
		allocateInfo.memoryTypeIndex = VulkanMemoryType( session, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
		if ( allocateInfo.memoryTypeIndex == UINT32_MAX
				|| session->vk.AllocateMemory( session->vkDevice, &allocateInfo, nullptr, &swapchain->vkMemory[i] ) != VK_SUCCESS ) {
			swapchain->vkMemory[i] = VK_NULL_HANDLE;
			return false;
		}
		if ( session->vk.BindImageMemory( session->vkDevice, swapchain->vkImages[i], swapchain->vkMemory[i], 0 ) != VK_SUCCESS ) {
			return false;
		}
	}
	return VulkanSubmitOnce( session, [&]( VkCommandBuffer cmd ) {
		for ( VkImage image : swapchain->vkImages ) {
			VulkanImageBarrier( session, cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT );
		}
	} );
}

const std::vector<int64_t> &SwapchainFormats( const TRSession *session ) {
	static const std::vector<int64_t> formats = { GL_SRGB8_ALPHA8, GL_RGBA8, GL_RGBA16F };
	return session->vulkan ? VulkanSwapchainFormats() : formats;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrEnumerateSwapchainFormats( XrSession handle, uint32_t capacity, uint32_t *countOutput, int64_t *formats ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	return TwoCall( capacity, countOutput, formats, SwapchainFormats( session ) );
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrCreateSwapchain( XrSession handle, const XrSwapchainCreateInfo *createInfo, XrSwapchain *swapchain ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( Fault( "xrCreateSwapchain" ) ) return FaultResult();
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( createInfo == nullptr || swapchain == nullptr || createInfo->type != XR_TYPE_SWAPCHAIN_CREATE_INFO ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	const std::vector<int64_t> &formats = SwapchainFormats( session );
	if ( std::find( formats.begin(), formats.end(), createInfo->format ) == formats.end() ) {
		return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
	}
	if ( createInfo->width == 0 || createInfo->height == 0 || createInfo->width > 4096 || createInfo->height > 4096
		|| createInfo->faceCount != 1 || createInfo->arraySize != 1 || createInfo->mipCount != 1 || createInfo->sampleCount != 1 ) {
		Log( "{\"event\":\"create_swapchain_rejected\",\"width\":%u,\"height\":%u,\"faces\":%u,\"array\":%u,\"mips\":%u,\"samples\":%u}",
			createInfo->width, createInfo->height, createInfo->faceCount, createInfo->arraySize, createInfo->mipCount, createInfo->sampleCount );
		return XR_ERROR_FEATURE_UNSUPPORTED;
	}
	if ( ( createInfo->usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT ) == 0 ) {
		return XR_ERROR_FEATURE_UNSUPPORTED;
	}

	TRSwapchain *created = new TRSwapchain();
	created->magic = MAGIC_SWAPCHAIN;
	G().liveHandles[ created ] = MAGIC_SWAPCHAIN;
	created->session = session;
	created->info = *createInfo;
	created->info.next = nullptr;
	created->nextAcquire = 0;
	created->waited = false;
	created->lastReleased = -1;
	created->imageCount = TR_SWAPCHAIN_LENGTH;
	if ( session->vulkan ) {
		if ( !CreateVulkanSwapchainImages( created ) ) {
			DestroySwapchainLocked( created );
			Log( "{\"event\":\"create_swapchain_rejected\",\"reason\":\"vulkan image creation failed\"}" );
			return XR_ERROR_RUNTIME_FAILURE;
		}
		session->swapchains.push_back( created );
		*swapchain = reinterpret_cast<XrSwapchain>( created );
		Log( "{\"event\":\"create_swapchain\",\"format\":%lld,\"width\":%u,\"height\":%u,\"usage\":%llu,\"api\":\"vulkan\"}",
			static_cast<long long>( createInfo->format ), createInfo->width, createInfo->height,
			static_cast<unsigned long long>( createInfo->usageFlags ) );
		return XR_SUCCESS;
	}
	created->textures.resize( TR_SWAPCHAIN_LENGTH );

	// The application's context is current on this thread (OpenGL binding
	// rule), so the images live in its share group. Leave its bindings alone.
	GLint previousTexture = 0, previousUnpackBuffer = 0, previousUnpack = 0;
	glGetIntegerv( GL_TEXTURE_BINDING_2D, &previousTexture );
	glGetIntegerv( GL_UNPACK_ALIGNMENT, &previousUnpack );
	PFNTRGLBINDBUFFER bindBuffer = GetBindBuffer();
	if ( bindBuffer != nullptr ) {
		glGetIntegerv( GL_PIXEL_UNPACK_BUFFER_BINDING, &previousUnpackBuffer );
		bindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
	}
	glGenTextures( static_cast<GLsizei>( created->textures.size() ), created->textures.data() );
	const GLenum type = createInfo->format == GL_RGBA16F ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
	for ( GLuint texture : created->textures ) {
		glBindTexture( GL_TEXTURE_2D, texture );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexImage2D( GL_TEXTURE_2D, 0, static_cast<GLint>( createInfo->format ), static_cast<GLsizei>( createInfo->width ),
			static_cast<GLsizei>( createInfo->height ), 0, GL_RGBA, type, nullptr );
	}
	glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>( previousTexture ) );
	glPixelStorei( GL_UNPACK_ALIGNMENT, previousUnpack );
	if ( bindBuffer != nullptr ) {
		bindBuffer( GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>( previousUnpackBuffer ) );
	}
	const GLenum error = glGetError();
	if ( error != GL_NO_ERROR ) {
		Log( "{\"event\":\"create_swapchain_gl_error\",\"error\":%u}", static_cast<unsigned>( error ) );
	}

	session->swapchains.push_back( created );
	*swapchain = reinterpret_cast<XrSwapchain>( created );
	Log( "{\"event\":\"create_swapchain\",\"format\":%lld,\"width\":%u,\"height\":%u,\"usage\":%llu}",
		static_cast<long long>( createInfo->format ), createInfo->width, createInfo->height,
		static_cast<unsigned long long>( createInfo->usageFlags ) );
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrDestroySwapchain( XrSwapchain handle ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRSwapchain *swapchain = Check( reinterpret_cast<TRSwapchain *>( handle ), MAGIC_SWAPCHAIN );
	if ( swapchain == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	std::vector<TRSwapchain *> &swapchains = swapchain->session->swapchains;
	swapchains.erase( std::remove( swapchains.begin(), swapchains.end(), swapchain ), swapchains.end() );
	DestroySwapchainLocked( swapchain );
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrEnumerateSwapchainImages( XrSwapchain handle, uint32_t capacity, uint32_t *countOutput, XrSwapchainImageBaseHeader *images ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRSwapchain *swapchain = Check( reinterpret_cast<TRSwapchain *>( handle ), MAGIC_SWAPCHAIN );
	if ( swapchain == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( countOutput == nullptr ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( Fault( capacity == 0 ? "imageCount" : "imageFill" ) ) {
		if ( G().faultValue == "zero" || G().faultValue == "large" || G().faultValue == "changed" ) {
			*countOutput = G().faultValue == "zero" ? 0 : G().faultValue == "large" ? UINT32_MAX : capacity + 1;
			return XR_SUCCESS;
		}
		return FaultResult();
	}
	*countOutput = swapchain->imageCount;
	if ( capacity == 0 ) {
		return XR_SUCCESS;
	}
	if ( capacity < swapchain->imageCount ) {
		return XR_ERROR_SIZE_INSUFFICIENT;
	}
	if ( swapchain->session->vulkan ) {
		XrSwapchainImageVulkanKHR *vk = reinterpret_cast<XrSwapchainImageVulkanKHR *>( images );
		for ( size_t i = 0; i < swapchain->vkImages.size(); ++i ) {
			if ( vk[i].type != XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR ) {
				return XR_ERROR_VALIDATION_FAILURE;
			}
			vk[i].image = swapchain->vkImages[i];
		}
		return XR_SUCCESS;
	}
	XrSwapchainImageOpenGLKHR *gl = reinterpret_cast<XrSwapchainImageOpenGLKHR *>( images );
	for ( size_t i = 0; i < swapchain->textures.size(); ++i ) {
		if ( gl[i].type != XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR ) {
			return XR_ERROR_VALIDATION_FAILURE;
		}
		gl[i].image = swapchain->textures[i];
	}
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrAcquireSwapchainImage( XrSwapchain handle, const XrSwapchainImageAcquireInfo *acquireInfo, uint32_t *index ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	const bool inject = Fault( "xrAcquireSwapchainImage" );
	if ( inject && G().faultValue != "large" && G().faultValue != "end" ) return FaultResult();
	TRSwapchain *swapchain = Check( reinterpret_cast<TRSwapchain *>( handle ), MAGIC_SWAPCHAIN );
	if ( swapchain == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( index == nullptr || ( acquireInfo != nullptr && acquireInfo->type != XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO ) ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( swapchain->acquired.size() >= swapchain->imageCount ) {
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	*index = swapchain->nextAcquire;
	swapchain->acquired.push_back( swapchain->nextAcquire );
	swapchain->nextAcquire = ( swapchain->nextAcquire + 1 ) % swapchain->imageCount;
	if ( inject ) *index = G().faultValue == "large" ? UINT32_MAX : swapchain->imageCount;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrWaitSwapchainImage( XrSwapchain handle, const XrSwapchainImageWaitInfo *waitInfo ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( Fault( "xrWaitSwapchainImage" ) ) return FaultResult();
	TRSwapchain *swapchain = Check( reinterpret_cast<TRSwapchain *>( handle ), MAGIC_SWAPCHAIN );
	if ( swapchain == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( waitInfo == nullptr || waitInfo->type != XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( swapchain->acquired.empty() || swapchain->waited ) {
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	swapchain->waited = true;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrReleaseSwapchainImage( XrSwapchain handle, const XrSwapchainImageReleaseInfo *releaseInfo ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( Fault( "xrReleaseSwapchainImage" ) ) return FaultResult();
	TRSwapchain *swapchain = Check( reinterpret_cast<TRSwapchain *>( handle ), MAGIC_SWAPCHAIN );
	if ( swapchain == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( releaseInfo != nullptr && releaseInfo->type != XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( swapchain->acquired.empty() || !swapchain->waited ) {
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	swapchain->lastReleased = static_cast<int>( swapchain->acquired.front() );
	swapchain->acquired.pop_front();
	swapchain->waited = false;
	return XR_SUCCESS;
}

/*
===============================================================================

	API: frame loop

===============================================================================
*/

XRAPI_ATTR XrResult XRAPI_CALL TR_xrWaitFrame( XrSession handle, const XrFrameWaitInfo *waitInfo, XrFrameState *frameState ) {
	int64_t sleepUntil = 0;
	{
		std::lock_guard<std::recursive_mutex> guard( G().lock );
		if ( Fault( "xrWaitFrame" ) ) return FaultResult();
		TRSession *session = GetSession( handle );
		if ( session == nullptr ) {
			return XR_ERROR_HANDLE_INVALID;
		}
		if ( frameState == nullptr || frameState->type != XR_TYPE_FRAME_STATE || ( waitInfo != nullptr && waitInfo->type != XR_TYPE_FRAME_WAIT_INFO ) ) {
			return XR_ERROR_VALIDATION_FAILURE;
		}
		if ( !session->running ) {
			return XR_ERROR_SESSION_NOT_RUNNING;
		}
		const int64_t now = NowNanoseconds();
		if ( G().refreshRate > 0.0 ) {
			// display times stay on the refresh grid; a late application skips slots
			while ( session->nextDisplayTime < now + session->period / 2 ) {
				session->nextDisplayTime += session->period;
			}
			sleepUntil = session->nextDisplayTime - session->period;
		} else {
			session->nextDisplayTime = std::max( session->nextDisplayTime + 1, now + session->period );
		}
		frameState->predictedDisplayTime = session->nextDisplayTime;
		frameState->predictedDisplayPeriod = session->period;
		frameState->shouldRender = ( session->state == XR_SESSION_STATE_VISIBLE || session->state == XR_SESSION_STATE_FOCUSED ) ? XR_TRUE : XR_FALSE;
		session->waitedDisplayTime = session->nextDisplayTime;
		session->nextDisplayTime += session->period;
		session->frameWaited = true;
		++session->framesWaited;
	}
	// pace outside the lock, like a compositor throttling the application
	const int64_t now = NowNanoseconds();
	if ( sleepUntil > now ) {
		std::this_thread::sleep_for( std::chrono::nanoseconds( sleepUntil - now ) );
	}
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrBeginFrame( XrSession handle, const XrFrameBeginInfo *beginInfo ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( Fault( "xrBeginFrame" ) ) return FaultResult();
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( beginInfo != nullptr && beginInfo->type != XR_TYPE_FRAME_BEGIN_INFO ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( !session->running ) {
		return XR_ERROR_SESSION_NOT_RUNNING;
	}
	if ( !session->frameWaited ) {
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	session->frameWaited = false;
	if ( session->frameBegun ) {
		// the previous frame was never ended: it is discarded
		return XR_FRAME_DISCARDED;
	}
	session->frameBegun = true;
	return XR_SUCCESS;
}

bool ValidateSubImage( const XrSwapchainSubImage &subImage, const char *what ) {
	TRSwapchain *swapchain = Check( reinterpret_cast<TRSwapchain *>( subImage.swapchain ), MAGIC_SWAPCHAIN );
	if ( swapchain == nullptr ) {
		Log( "{\"event\":\"end_frame_invalid\",\"what\":\"%s\",\"reason\":\"swapchain handle\"}", what );
		return false;
	}
	if ( swapchain->lastReleased < 0 ) {
		Log( "{\"event\":\"end_frame_invalid\",\"what\":\"%s\",\"reason\":\"no released image\"}", what );
		return false;
	}
	const XrRect2Di &rect = subImage.imageRect;
	if ( rect.offset.x < 0 || rect.offset.y < 0 || rect.extent.width <= 0 || rect.extent.height <= 0
		|| rect.offset.x + rect.extent.width > static_cast<int32_t>( swapchain->info.width )
		|| rect.offset.y + rect.extent.height > static_cast<int32_t>( swapchain->info.height ) ) {
		Log( "{\"event\":\"end_frame_invalid\",\"what\":\"%s\",\"reason\":\"image rect\"}", what );
		return false;
	}
	if ( subImage.imageArrayIndex != 0 ) {
		Log( "{\"event\":\"end_frame_invalid\",\"what\":\"%s\",\"reason\":\"array index\"}", what );
		return false;
	}
	return true;
}

bool FovValid( const XrFovf &fov ) {
	return std::isfinite( fov.angleLeft ) && std::isfinite( fov.angleRight ) && std::isfinite( fov.angleUp ) && std::isfinite( fov.angleDown )
		&& fov.angleLeft < fov.angleRight && fov.angleDown < fov.angleUp;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrEndFrame( XrSession handle, const XrFrameEndInfo *endInfo ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( Fault( "xrEndFrame" ) ) return FaultResult();
	RuntimeGlobals &g = G();
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( endInfo == nullptr || endInfo->type != XR_TYPE_FRAME_END_INFO ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( !session->running ) {
		return XR_ERROR_SESSION_NOT_RUNNING;
	}
	if ( !session->frameBegun ) {
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	session->frameBegun = false;
	if ( endInfo->displayTime != session->waitedDisplayTime ) {
		Log( "{\"event\":\"end_frame_invalid\",\"reason\":\"display time\"}" );
		return XR_ERROR_TIME_INVALID;
	}
	if ( endInfo->environmentBlendMode != XR_ENVIRONMENT_BLEND_MODE_OPAQUE ) {
		return XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED;
	}
	if ( endInfo->layerCount > XR_MIN_COMPOSITION_LAYERS_SUPPORTED ) {
		return XR_ERROR_LAYER_LIMIT_EXCEEDED;
	}

	std::string summary;
	std::string layout;
	const bool capture = !g.pendingCapture.empty();
	const std::string capturePrefix = g.pendingCapture;
	g.pendingCapture.clear();
	for ( uint32_t i = 0; i < endInfo->layerCount; ++i ) {
		const XrCompositionLayerBaseHeader *layer = endInfo->layers[i];
		if ( layer == nullptr ) {
			return XR_ERROR_LAYER_INVALID;
		}
		if ( Check( reinterpret_cast<TRSpace *>( layer->space ), MAGIC_SPACE ) == nullptr ) {
			Log( "{\"event\":\"end_frame_invalid\",\"reason\":\"layer space\"}" );
			return XR_ERROR_HANDLE_INVALID;
		}
		char item[512];
		if ( layer->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION ) {
			const XrCompositionLayerProjection *projection = reinterpret_cast<const XrCompositionLayerProjection *>( layer );
			if ( projection->viewCount != 2 || projection->views == nullptr ) {
				Log( "{\"event\":\"end_frame_invalid\",\"reason\":\"projection view count\"}" );
				return XR_ERROR_VALIDATION_FAILURE;
			}
			for ( uint32_t v = 0; v < 2; ++v ) {
				const XrCompositionLayerProjectionView &view = projection->views[v];
				if ( view.type != XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW ) {
					return XR_ERROR_VALIDATION_FAILURE;
				}
				if ( !PoseValid( view.pose ) ) {
					Log( "{\"event\":\"end_frame_invalid\",\"reason\":\"projection pose\"}" );
					return XR_ERROR_POSE_INVALID;
				}
				if ( !FovValid( view.fov ) ) {
					Log( "{\"event\":\"end_frame_invalid\",\"reason\":\"projection fov\"}" );
					return XR_ERROR_VALIDATION_FAILURE;
				}
				if ( !ValidateSubImage( view.subImage, v == 0 ? "projection_left" : "projection_right" ) ) {
					return XR_ERROR_SWAPCHAIN_RECT_INVALID;
				}
			}
			std::snprintf( item, sizeof( item ),
				"{\"type\":\"projection\",\"flags\":%llu,\"left\":{\"x\":%.4f,\"y\":%.4f,\"z\":%.4f,\"w\":%d,\"h\":%d},\"right\":{\"x\":%.4f,\"y\":%.4f,\"z\":%.4f,\"w\":%d,\"h\":%d}}",
				static_cast<unsigned long long>( projection->layerFlags ),
				projection->views[0].pose.position.x, projection->views[0].pose.position.y, projection->views[0].pose.position.z,
				projection->views[0].subImage.imageRect.extent.width, projection->views[0].subImage.imageRect.extent.height,
				projection->views[1].pose.position.x, projection->views[1].pose.position.y, projection->views[1].pose.position.z,
				projection->views[1].subImage.imageRect.extent.width, projection->views[1].subImage.imageRect.extent.height );
			if ( capture ) {
				CaptureImage( capturePrefix, "left", projection->views[0].subImage );
				CaptureImage( capturePrefix, "right", projection->views[1].subImage );
			}
			if ( g.burstRemaining > 0 ) {
				char suffix[32];
				std::snprintf( suffix, sizeof( suffix ), "_%03d.tga", g.burstIndex );
				CaptureBurstImage( g.burstPrefix + suffix, g.burstScale, projection->views[0].subImage, g.burstIndex );
				++g.burstIndex;
				--g.burstRemaining;
			}
		} else if ( layer->type == XR_TYPE_COMPOSITION_LAYER_QUAD ) {
			const XrCompositionLayerQuad *quad = reinterpret_cast<const XrCompositionLayerQuad *>( layer );
			if ( !PoseValid( quad->pose ) ) {
				Log( "{\"event\":\"end_frame_invalid\",\"reason\":\"quad pose\"}" );
				return XR_ERROR_POSE_INVALID;
			}
			if ( !( quad->size.width > 0.0f ) || !( quad->size.height > 0.0f ) ) {
				Log( "{\"event\":\"end_frame_invalid\",\"reason\":\"quad size\"}" );
				return XR_ERROR_VALIDATION_FAILURE;
			}
			if ( !ValidateSubImage( quad->subImage, "quad" ) ) {
				return XR_ERROR_SWAPCHAIN_RECT_INVALID;
			}
			std::snprintf( item, sizeof( item ),
				"{\"type\":\"quad\",\"flags\":%llu,\"x\":%.4f,\"y\":%.4f,\"z\":%.4f,\"width\":%.3f,\"height\":%.3f,\"w\":%d,\"h\":%d}",
				static_cast<unsigned long long>( quad->layerFlags ), quad->pose.position.x, quad->pose.position.y, quad->pose.position.z,
				quad->size.width, quad->size.height, quad->subImage.imageRect.extent.width, quad->subImage.imageRect.extent.height );
			if ( capture ) {
				char label[32];
				std::snprintf( label, sizeof( label ), "quad%u", i );
				CaptureImage( capturePrefix, label, quad->subImage );
			}
		} else if ( layer->type == XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR ) {
			const XrCompositionLayerCylinderKHR *cylinder = reinterpret_cast<const XrCompositionLayerCylinderKHR *>( layer );
			if ( !InstanceHasExtension( session->instance, XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME ) ) {
				return XR_ERROR_LAYER_INVALID;
			}
			if ( !PoseValid( cylinder->pose ) || !( cylinder->radius > 0.0f ) || !( cylinder->centralAngle > 0.0f ) || !( cylinder->aspectRatio > 0.0f ) ) {
				Log( "{\"event\":\"end_frame_invalid\",\"reason\":\"cylinder shape\"}" );
				return XR_ERROR_VALIDATION_FAILURE;
			}
			if ( !ValidateSubImage( cylinder->subImage, "cylinder" ) ) {
				return XR_ERROR_SWAPCHAIN_RECT_INVALID;
			}
			std::snprintf( item, sizeof( item ), "{\"type\":\"cylinder\",\"radius\":%.3f,\"angle\":%.3f}", cylinder->radius, cylinder->centralAngle );
			if ( capture ) {
				char label[32];
				std::snprintf( label, sizeof( label ), "cylinder%u", i );
				CaptureImage( capturePrefix, label, cylinder->subImage );
			}
		} else {
			Log( "{\"event\":\"end_frame_invalid\",\"reason\":\"layer type %d\"}", static_cast<int>( layer->type ) );
			return XR_ERROR_LAYER_INVALID;
		}
		summary += ( i ? "," : "" ) + std::string( item );
		layout += ( i ? "," : "" ) + std::to_string( static_cast<int>( layer->type ) ) + ":" + std::to_string( static_cast<unsigned long long>( layer->layerFlags ) );
	}

	++session->framesSubmitted;
	// log the first frames, then a sample; captures and every change of layer
	// layout (a menu opening or closing) are always reported
	const bool layoutChanged = layout != session->lastLayout;
	session->lastLayout = layout;
	if ( session->framesSubmitted <= 3 || session->framesSubmitted % 90 == 0 || capture || layoutChanged ) {
		Log( "{\"event\":\"end_frame\",\"frame\":%llu,\"layers\":[%s]}", static_cast<unsigned long long>( session->framesSubmitted ), summary.c_str() );
	}
	AdvanceScript( session );
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrLocateViews( XrSession handle, const XrViewLocateInfo *locateInfo, XrViewState *viewState,
		uint32_t capacity, uint32_t *countOutput, XrView *views ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( Fault( "xrLocateViews" ) ) {
		if ( G().faultValue != "tracking" ) return FaultResult();
		G().trackingInvalidFrames = 90;
	}
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( locateInfo == nullptr || viewState == nullptr || countOutput == nullptr || locateInfo->type != XR_TYPE_VIEW_LOCATE_INFO
		|| viewState->type != XR_TYPE_VIEW_STATE ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( locateInfo->viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO ) {
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
	}
	if ( locateInfo->displayTime <= 0 ) {
		return XR_ERROR_TIME_INVALID;
	}
	TRSpace *space = Check( reinterpret_cast<TRSpace *>( locateInfo->space ), MAGIC_SPACE );
	if ( space == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	*countOutput = 2;
	if ( capacity == 0 ) {
		return XR_SUCCESS;
	}
	if ( capacity < 2 || views == nullptr ) {
		return XR_ERROR_SIZE_INSUFFICIENT;
	}
	Pose basePose;
	G().locateTime = locateInfo->displayTime;
	if ( !StagePoseOfSpace( space, basePose ) ) {
		viewState->viewStateFlags = 0;
		return XR_SUCCESS;
	}
	const Pose baseInverse = PInverse( basePose );
	for ( int eye = 0; eye < 2; ++eye ) {
		if ( views[eye].type != XR_TYPE_VIEW ) {
			return XR_ERROR_VALIDATION_FAILURE;
		}
		Pose eyeInHead = PIdentity();
		eyeInHead.p = V( eye == 0 ? -TR_IPD * 0.5f : TR_IPD * 0.5f, 0.0f, 0.0f );
		const Pose eyeInStage = PMul( G().head, eyeInHead );
		views[eye].pose = ToXr( PMul( baseInverse, eyeInStage ) );
		views[eye].fov = EyeFov( eye );
	}
	viewState->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT
		| XR_VIEW_STATE_ORIENTATION_TRACKED_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT;
	if ( G().trackingInvalidFrames > 0 ) {
		--G().trackingInvalidFrames;
		viewState->viewStateFlags = 0;
	}
	return XR_SUCCESS;
}

/*
===============================================================================

	API: paths and actions

===============================================================================
*/

XRAPI_ATTR XrResult XRAPI_CALL TR_xrStringToPath( XrInstance handle, const char *pathString, XrPath *path ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRInstance *instance = GetInstance( handle );
	if ( instance == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( path == nullptr ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( !ValidPathString( pathString ) ) {
		return XR_ERROR_PATH_FORMAT_INVALID;
	}
	*path = InternPath( instance, pathString );
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrPathToString( XrInstance handle, XrPath path, uint32_t capacity, uint32_t *countOutput, char *buffer ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRInstance *instance = GetInstance( handle );
	if ( instance == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( path == XR_NULL_PATH || path > instance->paths.size() ) {
		return XR_ERROR_PATH_INVALID;
	}
	return TwoCallString( capacity, countOutput, buffer, instance->paths[static_cast<size_t>( path - 1 )] );
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrCreateActionSet( XrInstance handle, const XrActionSetCreateInfo *createInfo, XrActionSet *actionSet ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRInstance *instance = GetInstance( handle );
	if ( instance == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( createInfo == nullptr || actionSet == nullptr || createInfo->type != XR_TYPE_ACTION_SET_CREATE_INFO ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( createInfo->actionSetName[0] == '\0' || createInfo->localizedActionSetName[0] == '\0' ) {
		return XR_ERROR_NAME_INVALID;
	}
	for ( TRActionSet *set : instance->actionSets ) {
		if ( set->name == createInfo->actionSetName ) {
			return XR_ERROR_NAME_DUPLICATED;
		}
	}
	TRActionSet *created = new TRActionSet();
	created->magic = MAGIC_ACTIONSET;
	G().liveHandles[ created ] = MAGIC_ACTIONSET;
	created->instance = instance;
	created->name = createInfo->actionSetName;
	created->attached = false;
	instance->actionSets.push_back( created );
	*actionSet = reinterpret_cast<XrActionSet>( created );
	Log( "{\"event\":\"create_action_set\",\"name\":\"%s\",\"localized\":\"%s\"}",
		JsonEscape( createInfo->actionSetName ).c_str(), JsonEscape( createInfo->localizedActionSetName ).c_str() );
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrDestroyActionSet( XrActionSet handle ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRActionSet *set = Check( reinterpret_cast<TRActionSet *>( handle ), MAGIC_ACTIONSET );
	if ( set == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	std::vector<TRActionSet *> &sets = set->instance->actionSets;
	sets.erase( std::remove( sets.begin(), sets.end(), set ), sets.end() );
	for ( TRAction *action : set->actions ) {
		G().liveHandles.erase( action );
		action->magic = 0;
		delete action;
	}
	G().liveHandles.erase( set );
	set->magic = 0;
	delete set;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrCreateAction( XrActionSet handle, const XrActionCreateInfo *createInfo, XrAction *action ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRActionSet *set = Check( reinterpret_cast<TRActionSet *>( handle ), MAGIC_ACTIONSET );
	if ( set == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( createInfo == nullptr || action == nullptr || createInfo->type != XR_TYPE_ACTION_CREATE_INFO ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( set->attached ) {
		return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
	}
	if ( createInfo->actionName[0] == '\0' ) {
		return XR_ERROR_NAME_INVALID;
	}
	for ( TRAction *existing : set->actions ) {
		if ( existing->name == createInfo->actionName ) {
			return XR_ERROR_NAME_DUPLICATED;
		}
	}
	TRAction *created = new TRAction();
	created->magic = MAGIC_ACTION;
	G().liveHandles[ created ] = MAGIC_ACTION;
	created->set = set;
	created->name = createInfo->actionName;
	created->type = createInfo->actionType;
	for ( uint32_t i = 0; i < createInfo->countSubactionPaths; ++i ) {
		created->subactionPaths.push_back( createInfo->subactionPaths[i] );
	}
	set->actions.push_back( created );
	*action = reinterpret_cast<XrAction>( created );
	Log( "{\"event\":\"create_action\",\"name\":\"%s\",\"localized\":\"%s\"}",
		JsonEscape( createInfo->actionName ).c_str(), JsonEscape( createInfo->localizedActionName ).c_str() );
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrDestroyAction( XrAction handle ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRAction *action = Check( reinterpret_cast<TRAction *>( handle ), MAGIC_ACTION );
	if ( action == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	std::vector<TRAction *> &actions = action->set->actions;
	actions.erase( std::remove( actions.begin(), actions.end(), action ), actions.end() );
	G().liveHandles.erase( action );
	action->magic = 0;
	delete action;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrSuggestInteractionProfileBindings( XrInstance handle, const XrInteractionProfileSuggestedBinding *suggested ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRInstance *instance = GetInstance( handle );
	if ( instance == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( suggested == nullptr || suggested->type != XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING || suggested->countSuggestedBindings == 0 ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	const std::string profile = PathString( instance, suggested->interactionProfile );
	if ( profile.rfind( "/interaction_profiles/", 0 ) != 0 ) {
		return XR_ERROR_PATH_UNSUPPORTED;
	}
	std::map<TRAction *, std::vector<XrPath>> accepted;
	for ( uint32_t i = 0; i < suggested->countSuggestedBindings; ++i ) {
		TRAction *action = Check( reinterpret_cast<TRAction *>( suggested->suggestedBindings[i].action ), MAGIC_ACTION );
		if ( action == nullptr ) {
			return XR_ERROR_HANDLE_INVALID;
		}
		if ( action->set->attached ) {
			return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
		}
		const std::string binding = PathString( instance, suggested->suggestedBindings[i].binding );
		if ( HandIndexForUserPath( binding ) < 0 || binding.find( "/input/" ) == std::string::npos ) {
			if ( binding.find( "/output/haptic" ) == std::string::npos ) {
				Log( "{\"event\":\"binding_rejected\",\"profile\":\"%s\",\"binding\":\"%s\"}", JsonEscape( profile ).c_str(), JsonEscape( binding ).c_str() );
				return XR_ERROR_PATH_UNSUPPORTED;
			}
		}
		accepted[action].push_back( suggested->suggestedBindings[i].binding );
	}
	// a later suggestion for the same profile replaces the earlier one
	for ( TRActionSet *set : instance->actionSets ) {
		for ( TRAction *action : set->actions ) {
			action->suggested.erase( suggested->interactionProfile );
		}
	}
	for ( const auto &entry : accepted ) {
		entry.first->suggested[suggested->interactionProfile] = entry.second;
	}
	Log( "{\"event\":\"suggest_bindings\",\"profile\":\"%s\",\"count\":%u}", JsonEscape( profile ).c_str(), suggested->countSuggestedBindings );
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrAttachSessionActionSets( XrSession handle, const XrSessionActionSetsAttachInfo *attachInfo ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( Fault( "xrAttachSessionActionSets" ) ) return FaultResult();
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( attachInfo == nullptr || attachInfo->type != XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO || attachInfo->countActionSets == 0 ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( !session->attachedSets.empty() ) {
		return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
	}
	for ( uint32_t i = 0; i < attachInfo->countActionSets; ++i ) {
		TRActionSet *set = Check( reinterpret_cast<TRActionSet *>( attachInfo->actionSets[i] ), MAGIC_ACTIONSET );
		if ( set == nullptr ) {
			return XR_ERROR_HANDLE_INVALID;
		}
		set->attached = true;
		session->attachedSets.push_back( set );
	}
	session->currentProfile = ChooseProfile( session->instance );
	session->instance->profileChangedPending = true;
	Log( "{\"event\":\"attach_action_sets\",\"count\":%u,\"profile\":\"%s\"}", attachInfo->countActionSets,
		JsonEscape( PathString( session->instance, session->currentProfile ) ).c_str() );
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrGetCurrentInteractionProfile( XrSession handle, XrPath topLevelUserPath, XrInteractionProfileState *profile ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( profile == nullptr || profile->type != XR_TYPE_INTERACTION_PROFILE_STATE ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( session->attachedSets.empty() ) {
		return XR_ERROR_ACTIONSET_NOT_ATTACHED;
	}
	const std::string user = PathString( session->instance, topLevelUserPath );
	profile->interactionProfile = HandIndexForUserPath( user ) >= 0 ? session->currentProfile : XR_NULL_PATH;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrSyncActions( XrSession handle, const XrActionsSyncInfo *syncInfo ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( Fault( "xrSyncActions" ) ) return FaultResult();
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( syncInfo == nullptr || syncInfo->type != XR_TYPE_ACTIONS_SYNC_INFO ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	TRInstance *instance = session->instance;
	const bool focused = session->running && session->state == XR_SESSION_STATE_FOCUSED;
	const XrTime now = NowNanoseconds();
	for ( uint32_t i = 0; i < syncInfo->countActiveActionSets; ++i ) {
		TRActionSet *set = Check( reinterpret_cast<TRActionSet *>( syncInfo->activeActionSets[i].actionSet ), MAGIC_ACTIONSET );
		if ( set == nullptr ) {
			return XR_ERROR_HANDLE_INVALID;
		}
		if ( !set->attached ) {
			return XR_ERROR_ACTIONSET_NOT_ATTACHED;
		}
		for ( TRAction *action : set->actions ) {
			std::vector<XrPath> keys = action->subactionPaths;
			keys.push_back( XR_NULL_PATH );
			std::map<XrPath, std::vector<XrPath>>::const_iterator bound = action->suggested.find( session->currentProfile );
			for ( XrPath key : keys ) {
				TRAction::SyncedState next = {};
				next.active = false;
				if ( focused && !G().noInput && bound != action->suggested.end() ) {
					for ( XrPath binding : bound->second ) {
						const std::string bindingString = PathString( instance, binding );
						if ( key != XR_NULL_PATH && bindingString.rfind( PathString( instance, key ), 0 ) != 0 ) {
							continue;
						}
						float value = 0.0f, x = 0.0f, y = 0.0f;
						bool isVector = false;
						if ( !EvaluateBinding( instance, binding, value, x, y, isVector ) ) {
							continue;
						}
						if ( !next.active ) {
							next.active = true;
							next.value = value;
							next.x = x;
							next.y = y;
						} else if ( std::fabs( value ) > std::fabs( next.value ) ) {
							next.value = value;
							next.x = x;
							next.y = y;
						}
					}
				}
				TRAction::SyncedState &previous = action->state[key];
				const bool wasBool = previous.value >= 0.5f;
				const bool isBool = next.value >= 0.5f;
				next.changed = previous.active && next.active
					&& ( action->type == XR_ACTION_TYPE_BOOLEAN_INPUT ? wasBool != isBool
						: ( previous.value != next.value || previous.x != next.x || previous.y != next.y ) );
				next.changeTime = next.changed ? now : previous.changeTime;
				previous = next;
			}
		}
	}
	return XR_SUCCESS;
}

const TRAction::SyncedState *ActionState( TRSession *session, const XrActionStateGetInfo *getInfo, XrActionType expected, XrResult &result ) {
	result = XR_SUCCESS;
	if ( getInfo == nullptr || getInfo->type != XR_TYPE_ACTION_STATE_GET_INFO ) {
		result = XR_ERROR_VALIDATION_FAILURE;
		return nullptr;
	}
	TRAction *action = Check( reinterpret_cast<TRAction *>( getInfo->action ), MAGIC_ACTION );
	if ( action == nullptr ) {
		result = XR_ERROR_HANDLE_INVALID;
		return nullptr;
	}
	if ( action->type != expected ) {
		result = XR_ERROR_ACTION_TYPE_MISMATCH;
		return nullptr;
	}
	if ( !action->set->attached ) {
		result = XR_ERROR_ACTIONSET_NOT_ATTACHED;
		return nullptr;
	}
	if ( getInfo->subactionPath != XR_NULL_PATH
		&& std::find( action->subactionPaths.begin(), action->subactionPaths.end(), getInfo->subactionPath ) == action->subactionPaths.end() ) {
		result = XR_ERROR_PATH_UNSUPPORTED;
		return nullptr;
	}
	( void )session;
	std::map<XrPath, TRAction::SyncedState>::const_iterator it = action->state.find( getInfo->subactionPath );
	return it == action->state.end() ? nullptr : &it->second;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrGetActionStateBoolean( XrSession handle, const XrActionStateGetInfo *getInfo, XrActionStateBoolean *state ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( Fault( "xrGetActionStateBoolean" ) ) return FaultResult();
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( state == nullptr || state->type != XR_TYPE_ACTION_STATE_BOOLEAN ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	XrResult result;
	const TRAction::SyncedState *synced = ActionState( session, getInfo, XR_ACTION_TYPE_BOOLEAN_INPUT, result );
	if ( result != XR_SUCCESS ) {
		return result;
	}
	state->isActive = synced != nullptr && synced->active ? XR_TRUE : XR_FALSE;
	state->currentState = state->isActive && synced->value >= 0.5f ? XR_TRUE : XR_FALSE;
	state->changedSinceLastSync = state->isActive && synced->changed ? XR_TRUE : XR_FALSE;
	state->lastChangeTime = synced != nullptr ? synced->changeTime : 0;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrGetActionStateFloat( XrSession handle, const XrActionStateGetInfo *getInfo, XrActionStateFloat *state ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( Fault( "xrGetActionStateFloat" ) ) return FaultResult();
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( state == nullptr || state->type != XR_TYPE_ACTION_STATE_FLOAT ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	XrResult result;
	const TRAction::SyncedState *synced = ActionState( session, getInfo, XR_ACTION_TYPE_FLOAT_INPUT, result );
	if ( result != XR_SUCCESS ) {
		return result;
	}
	state->isActive = synced != nullptr && synced->active ? XR_TRUE : XR_FALSE;
	state->currentState = state->isActive ? synced->value : 0.0f;
	state->changedSinceLastSync = state->isActive && synced->changed ? XR_TRUE : XR_FALSE;
	state->lastChangeTime = synced != nullptr ? synced->changeTime : 0;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrGetActionStateVector2f( XrSession handle, const XrActionStateGetInfo *getInfo, XrActionStateVector2f *state ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( Fault( "xrGetActionStateVector2f" ) ) return FaultResult();
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( state == nullptr || state->type != XR_TYPE_ACTION_STATE_VECTOR2F ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	XrResult result;
	const TRAction::SyncedState *synced = ActionState( session, getInfo, XR_ACTION_TYPE_VECTOR2F_INPUT, result );
	if ( result != XR_SUCCESS ) {
		return result;
	}
	state->isActive = synced != nullptr && synced->active ? XR_TRUE : XR_FALSE;
	state->currentState.x = state->isActive ? synced->x : 0.0f;
	state->currentState.y = state->isActive ? synced->y : 0.0f;
	state->changedSinceLastSync = state->isActive && synced->changed ? XR_TRUE : XR_FALSE;
	state->lastChangeTime = synced != nullptr ? synced->changeTime : 0;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrGetActionStatePose( XrSession handle, const XrActionStateGetInfo *getInfo, XrActionStatePose *state ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( state == nullptr || state->type != XR_TYPE_ACTION_STATE_POSE ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	XrResult result;
	const TRAction::SyncedState *synced = ActionState( session, getInfo, XR_ACTION_TYPE_POSE_INPUT, result );
	if ( result != XR_SUCCESS ) {
		return result;
	}
	state->isActive = synced != nullptr && synced->active ? XR_TRUE : XR_FALSE;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrEnumerateBoundSourcesForAction( XrSession handle, const XrBoundSourcesForActionEnumerateInfo *enumerateInfo,
		uint32_t capacity, uint32_t *countOutput, XrPath *sources ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( enumerateInfo == nullptr || enumerateInfo->type != XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	TRAction *action = Check( reinterpret_cast<TRAction *>( enumerateInfo->action ), MAGIC_ACTION );
	if ( action == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	std::vector<XrPath> values;
	std::map<XrPath, std::vector<XrPath>>::const_iterator bound = action->suggested.find( session->currentProfile );
	if ( bound != action->suggested.end() ) {
		values = bound->second;
	}
	return TwoCall( capacity, countOutput, sources, values );
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrGetInputSourceLocalizedName( XrSession handle, const XrInputSourceLocalizedNameGetInfo *getInfo,
		uint32_t capacity, uint32_t *countOutput, char *buffer ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( getInfo == nullptr || getInfo->type != XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO || getInfo->whichComponents == 0 ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if ( getInfo->sourcePath == XR_NULL_PATH || getInfo->sourcePath > session->instance->paths.size() ) {
		return XR_ERROR_PATH_INVALID;
	}
	return TwoCallString( capacity, countOutput, buffer, PathString( session->instance, getInfo->sourcePath ) );
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrApplyHapticFeedback( XrSession handle, const XrHapticActionInfo *hapticActionInfo, const XrHapticBaseHeader *hapticFeedback ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	TRSession *session = GetSession( handle );
	if ( session == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( hapticActionInfo == nullptr || hapticFeedback == nullptr || hapticActionInfo->type != XR_TYPE_HAPTIC_ACTION_INFO ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	TRAction *action = Check( reinterpret_cast<TRAction *>( hapticActionInfo->action ), MAGIC_ACTION );
	if ( action == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( action->type != XR_ACTION_TYPE_VIBRATION_OUTPUT ) {
		return XR_ERROR_ACTION_TYPE_MISMATCH;
	}
	if ( hapticFeedback->type == XR_TYPE_HAPTIC_VIBRATION ) {
		const XrHapticVibration *vibration = reinterpret_cast<const XrHapticVibration *>( hapticFeedback );
		if ( !( vibration->amplitude >= 0.0f && vibration->amplitude <= 1.0f ) || vibration->duration < 0 ) {
			return XR_ERROR_VALIDATION_FAILURE;
		}
		const std::string hand = hapticActionInfo->subactionPath == XR_NULL_PATH ? std::string( "both" )
			: PathString( session->instance, hapticActionInfo->subactionPath );
		Log( "{\"event\":\"haptic\",\"action\":\"%s\",\"hand\":\"%s\",\"amplitude\":%.3f,\"duration_ns\":%lld}",
			JsonEscape( action->name ).c_str(), JsonEscape( hand ).c_str(), vibration->amplitude, static_cast<long long>( vibration->duration ) );
	}
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL TR_xrStopHapticFeedback( XrSession handle, const XrHapticActionInfo *hapticActionInfo ) {
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( GetSession( handle ) == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( hapticActionInfo == nullptr || hapticActionInfo->type != XR_TYPE_HAPTIC_ACTION_INFO ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	return XR_SUCCESS;
}

/*
===============================================================================

	Dispatch

===============================================================================
*/

struct FunctionEntry {
	const char *		name;
	PFN_xrVoidFunction	function;
	const char *		extension;		// required instance extension, or nullptr
};

#define TR_ENTRY( name ) { #name, reinterpret_cast<PFN_xrVoidFunction>( TR_##name ), nullptr }
#define TR_EXT_ENTRY( name, ext ) { #name, reinterpret_cast<PFN_xrVoidFunction>( TR_##name ), ext }

const FunctionEntry FUNCTIONS[] = {
	TR_ENTRY( xrEnumerateInstanceExtensionProperties ),
	TR_ENTRY( xrCreateInstance ),
	TR_ENTRY( xrDestroyInstance ),
	TR_ENTRY( xrGetInstanceProperties ),
	TR_ENTRY( xrPollEvent ),
	TR_ENTRY( xrResultToString ),
	TR_ENTRY( xrStructureTypeToString ),
	TR_ENTRY( xrGetSystem ),
	TR_ENTRY( xrGetSystemProperties ),
	TR_ENTRY( xrEnumerateEnvironmentBlendModes ),
	TR_ENTRY( xrCreateSession ),
	TR_ENTRY( xrDestroySession ),
	TR_ENTRY( xrEnumerateReferenceSpaces ),
	TR_ENTRY( xrCreateReferenceSpace ),
	TR_ENTRY( xrGetReferenceSpaceBoundsRect ),
	TR_ENTRY( xrCreateActionSpace ),
	TR_ENTRY( xrLocateSpace ),
	TR_ENTRY( xrDestroySpace ),
	TR_ENTRY( xrEnumerateViewConfigurations ),
	TR_ENTRY( xrGetViewConfigurationProperties ),
	TR_ENTRY( xrEnumerateViewConfigurationViews ),
	TR_ENTRY( xrEnumerateSwapchainFormats ),
	TR_ENTRY( xrCreateSwapchain ),
	TR_ENTRY( xrDestroySwapchain ),
	TR_ENTRY( xrEnumerateSwapchainImages ),
	TR_ENTRY( xrAcquireSwapchainImage ),
	TR_ENTRY( xrWaitSwapchainImage ),
	TR_ENTRY( xrReleaseSwapchainImage ),
	TR_ENTRY( xrBeginSession ),
	TR_ENTRY( xrEndSession ),
	TR_ENTRY( xrRequestExitSession ),
	TR_ENTRY( xrWaitFrame ),
	TR_ENTRY( xrBeginFrame ),
	TR_ENTRY( xrEndFrame ),
	TR_ENTRY( xrLocateViews ),
	TR_ENTRY( xrStringToPath ),
	TR_ENTRY( xrPathToString ),
	TR_ENTRY( xrCreateActionSet ),
	TR_ENTRY( xrDestroyActionSet ),
	TR_ENTRY( xrCreateAction ),
	TR_ENTRY( xrDestroyAction ),
	TR_ENTRY( xrSuggestInteractionProfileBindings ),
	TR_ENTRY( xrAttachSessionActionSets ),
	TR_ENTRY( xrGetCurrentInteractionProfile ),
	TR_ENTRY( xrGetActionStateBoolean ),
	TR_ENTRY( xrGetActionStateFloat ),
	TR_ENTRY( xrGetActionStateVector2f ),
	TR_ENTRY( xrGetActionStatePose ),
	TR_ENTRY( xrSyncActions ),
	TR_ENTRY( xrEnumerateBoundSourcesForAction ),
	TR_ENTRY( xrGetInputSourceLocalizedName ),
	TR_ENTRY( xrApplyHapticFeedback ),
	TR_ENTRY( xrStopHapticFeedback ),
	TR_EXT_ENTRY( xrGetOpenGLGraphicsRequirementsKHR, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME ),
	TR_EXT_ENTRY( xrGetVulkanGraphicsRequirements2KHR, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME ),
	TR_EXT_ENTRY( xrCreateVulkanInstanceKHR, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME ),
	TR_EXT_ENTRY( xrGetVulkanGraphicsDevice2KHR, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME ),
	TR_EXT_ENTRY( xrCreateVulkanDeviceKHR, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME ),
};

XRAPI_ATTR XrResult XRAPI_CALL TR_xrGetInstanceProcAddr( XrInstance instance, const char *name, PFN_xrVoidFunction *function ) {
	if ( function == nullptr || name == nullptr ) {
		return XR_ERROR_VALIDATION_FAILURE;
	}
	*function = nullptr;
	std::lock_guard<std::recursive_mutex> guard( G().lock );
	if ( instance == XR_NULL_HANDLE ) {
		// only the pre-instance entry points are available without an instance
		if ( std::strcmp( name, "xrEnumerateInstanceExtensionProperties" ) == 0 ) {
			*function = reinterpret_cast<PFN_xrVoidFunction>( TR_xrEnumerateInstanceExtensionProperties );
			return XR_SUCCESS;
		}
		if ( std::strcmp( name, "xrCreateInstance" ) == 0 ) {
			*function = reinterpret_cast<PFN_xrVoidFunction>( TR_xrCreateInstance );
			return XR_SUCCESS;
		}
		if ( std::strcmp( name, "xrEnumerateApiLayerProperties" ) == 0 ) {
			return XR_ERROR_FUNCTION_UNSUPPORTED;
		}
		if ( std::strcmp( name, "xrGetInstanceProcAddr" ) == 0 ) {
			*function = reinterpret_cast<PFN_xrVoidFunction>( TR_xrGetInstanceProcAddr );
			return XR_SUCCESS;
		}
		return XR_ERROR_HANDLE_INVALID;
	}
	TRInstance *owner = GetInstance( instance );
	if ( owner == nullptr ) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if ( std::strcmp( name, "xrGetInstanceProcAddr" ) == 0 ) {
		*function = reinterpret_cast<PFN_xrVoidFunction>( TR_xrGetInstanceProcAddr );
		return XR_SUCCESS;
	}
	for ( const FunctionEntry &entry : FUNCTIONS ) {
		if ( std::strcmp( entry.name, name ) == 0 ) {
			if ( entry.extension != nullptr && !InstanceHasExtension( owner, entry.extension ) ) {
				return XR_ERROR_FUNCTION_UNSUPPORTED;
			}
			*function = entry.function;
			return XR_SUCCESS;
		}
	}
	return XR_ERROR_FUNCTION_UNSUPPORTED;
}

} // anonymous namespace

TR_EXPORT XrResult XRAPI_CALL xrNegotiateLoaderRuntimeInterface( const XrNegotiateLoaderInfo *loaderInfo, XrNegotiateRuntimeRequest *runtimeRequest ) {
	if ( loaderInfo == nullptr || runtimeRequest == nullptr
		|| loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO || loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION
		|| loaderInfo->structSize != sizeof( XrNegotiateLoaderInfo )
		|| runtimeRequest->structType != XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST || runtimeRequest->structVersion != XR_RUNTIME_INFO_STRUCT_VERSION
		|| runtimeRequest->structSize != sizeof( XrNegotiateRuntimeRequest ) ) {
		return XR_ERROR_INITIALIZATION_FAILED;
	}
	if ( loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_RUNTIME_VERSION || loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_RUNTIME_VERSION ) {
		return XR_ERROR_INITIALIZATION_FAILED;
	}
	if ( XR_VERSION_MAJOR( loaderInfo->minApiVersion ) > 1 || XR_VERSION_MAJOR( loaderInfo->maxApiVersion ) < 1 ) {
		return XR_ERROR_INITIALIZATION_FAILED;
	}
	runtimeRequest->runtimeInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
	runtimeRequest->runtimeApiVersion = XR_API_VERSION_1_0;
	runtimeRequest->getInstanceProcAddr = TR_xrGetInstanceProcAddr;
	return XR_SUCCESS;
}
