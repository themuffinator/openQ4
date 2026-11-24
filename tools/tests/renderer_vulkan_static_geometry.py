#!/usr/bin/env python3
"""Exercise the Vulkan executor's resident static geometry against a counted allocator double.

The production functions are compiled as they stand. The double provides the
vertex cache header, a first-fit virtual block and a clock, so the test can
recycle headers, fill the buffer and advance frames and time at will.
"""

from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#define ID_INLINE inline
#define VK_NULL_HANDLE 0
typedef unsigned char byte;
typedef uint64_t uint64;
typedef uint64_t VkDeviceSize;
typedef int VkResult;
enum { VK_SUCCESS = 0, VK_ERROR_OUT_OF_DEVICE_MEMORY = -2 };
enum { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO = 12, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT = 0x80 };
enum { VMA_MEMORY_USAGE_AUTO = 7, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT = 0x400,
       VMA_ALLOCATION_CREATE_MAPPED_BIT = 0x4 };
typedef uintptr_t VkBuffer;
typedef uintptr_t VmaAllocation;
typedef uintptr_t VmaVirtualBlock;
typedef uintptr_t VmaVirtualAllocation;
struct VkBufferCreateInfo { int sType; VkDeviceSize size; int usage; };
struct VmaAllocationCreateInfo { int usage; int flags; };
struct VmaAllocationInfo { void *pMappedData; };
struct VmaVirtualBlockCreateInfo { VkDeviceSize size; };
struct VmaVirtualAllocationCreateInfo { VkDeviceSize size; VkDeviceSize alignment; };
constexpr int VK_FRAMES_IN_FLIGHT = 2, TAG_USED = 1, TAG_TEMP = 3;

template<class T> T Min( T a, T b ) { return std::min( a, b ); }
template<class T> T Max( T a, T b ) { return std::max( a, b ); }
struct idMath { static int ClampInt( int lo, int hi, int v ) { return v < lo ? lo : ( v > hi ? hi : v ); } };

struct vertCache_t { int tag = TAG_USED; unsigned long long serial = 0; int size = 0; int offset = 0; void *virtMem = nullptr; };
struct srfTriangles_t { vertCache_t *ambientCache = nullptr; const srfTriangles_t *ambientSurface = nullptr;
                        bool gpuSkinningDeferred = false; bool candidate = false; };
bool R_GpuSkinning_IsCandidate( const srfTriangles_t *tri ) { return tri->candidate; }
bool vkGpuSkinningEverDeferred = false;

struct Common {
    int warnings = 0, prints = 0;
    void Warning( const char *, ... ) { ++warnings; }
    void Printf( const char *, ... ) { ++prints; }
} commonStore, *common = &commonStore;
struct CVarInt { int value; int GetInteger() const { return value; } };
CVarInt r_vkStaticGeometry{ 1 }, r_vkStaticGeometryMegs{ 8 };
int clockMsec = 1000;
int Sys_Milliseconds() { return clockMsec; }

// the device double
struct { int allocator = 1; } vkCtx;
struct Ring { VkBuffer buffer = 77; };
struct { Ring vertexRings[ VK_FRAMES_IN_FLIGHT ]; } vkExec;
std::vector<unsigned char> deviceBytes;
bool refuseBuffer = false;
int buffersCreated = 0, buffersDestroyed = 0, blocksDestroyed = 0;
VkDeviceSize flushedBegin = 0, flushedEnd = 0;
int flushes = 0;
std::map<VkDeviceSize, VkDeviceSize> liveRanges;	// offset -> size
VkDeviceSize blockSize = 0;

VkResult vmaCreateBuffer( int, const VkBufferCreateInfo *bci, const VmaAllocationCreateInfo *vaci,
        VkBuffer *buffer, VmaAllocation *allocation, VmaAllocationInfo *info ) {
    assert( bci->usage == VK_BUFFER_USAGE_VERTEX_BUFFER_BIT && ( vaci->flags & VMA_ALLOCATION_CREATE_MAPPED_BIT ) != 0 );
    if ( refuseBuffer ) { return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
    deviceBytes.assign( static_cast<size_t>( bci->size ), 0xCD );
    *buffer = 4242; *allocation = 4243; info->pMappedData = deviceBytes.data();
    ++buffersCreated;
    return VK_SUCCESS;
}
void vmaDestroyBuffer( int, VkBuffer, VmaAllocation ) { ++buffersDestroyed; }
VkResult vmaCreateVirtualBlock( const VmaVirtualBlockCreateInfo *info, VmaVirtualBlock *block ) {
    blockSize = info->size; liveRanges.clear(); *block = 4244; return VK_SUCCESS;
}
void vmaClearVirtualBlock( VmaVirtualBlock ) { liveRanges.clear(); }
void vmaDestroyVirtualBlock( VmaVirtualBlock ) { assert( liveRanges.empty() ); ++blocksDestroyed; }
VkResult vmaVirtualAllocate( VmaVirtualBlock, const VmaVirtualAllocationCreateInfo *info,
        VmaVirtualAllocation *allocation, VkDeviceSize *offset ) {
    VkDeviceSize cursor = 0;
    for ( const auto &range : liveRanges ) {
        const VkDeviceSize aligned = ( cursor + info->alignment - 1 ) & ~( info->alignment - 1 );
        if ( aligned + info->size <= range.first ) { cursor = aligned; goto found; }
        cursor = range.first + range.second;
    }
    cursor = ( cursor + info->alignment - 1 ) & ~( info->alignment - 1 );
    if ( cursor + info->size > blockSize ) { return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
found:
    liveRanges[ cursor ] = info->size;
    *offset = cursor; *allocation = cursor + 1;
    return VK_SUCCESS;
}
void vmaVirtualFree( VmaVirtualBlock, VmaVirtualAllocation allocation ) {
    assert( liveRanges.erase( allocation - 1 ) == 1 );
}
VkResult vmaFlushAllocation( int, VmaAllocation, VkDeviceSize offset, VkDeviceSize size ) {
    flushedBegin = offset; flushedEnd = offset + size; ++flushes; return VK_SUCCESS;
}
'''

CASES = r'''
static std::vector<unsigned char> Payload( int bytes, unsigned char seed ) {
    std::vector<unsigned char> data( bytes );
    for ( int i = 0; i < bytes; i++ ) { data[ i ] = static_cast<unsigned char>( seed + i * 7 ); }
    return data;
}

static int frameSlot = 0;
static void Frame( int msec = 16 ) {
    clockMsec += msec;
    VK_StaticGeo_BeginFrame( frameSlot );
    frameSlot = ( frameSlot + 1 ) % VK_FRAMES_IN_FLIGHT;
}

int main() {
    unsigned long long nextSerial = 1;
    std::vector<unsigned char> bytesA = Payload( 6400, 3 ), bytesB = Payload( 6400, 90 );
    vertCache_t header; header.serial = nextSerial++; header.size = 6400; header.virtMem = bytesA.data();
    srfTriangles_t tri; tri.ambientCache = &header;

    // Nothing is tracked, and no buffer exists, before the first frame begins.
    assert( VK_StaticGeo_VertexOffset( &tri, 6400 ) == -1 && buffersCreated == 0 );
    Frame();

    // First sight: streamed, tracked. Still streamed until the stream has
    // been the same allocation for the promotion delay.
    assert( VK_StaticGeo_VertexOffset( &tri, 6400 ) == -1 && vkStaticGeo.numEntries == 1 );
    Frame( VK_STATIC_GEO_PROMOTE_MSEC - 20 );
    assert( VK_StaticGeo_VertexOffset( &tri, 6400 ) == -1 && buffersCreated == 0 );
    Frame( 40 );
    const int resident = VK_StaticGeo_VertexOffset( &tri, 6400 );
    assert( resident >= 0 && ( resident & VK_STATIC_GEO_FLAG ) != 0 && buffersCreated == 1 );
    const int offsetA = resident & ~VK_STATIC_GEO_FLAG;
    assert( offsetA % 64 == 0 && !std::memcmp( deviceBytes.data() + offsetA, bytesA.data(), 6400 ) );
    assert( vkStaticGeo.residentCount == 1 && vkStaticGeo.residentBytes == 6400 && vkStaticGeo.promotions == 1 );
    // The copy is flushed before the submit that reads it, and only once.
    assert( VK_StaticGeo_Flush() && flushes == 1 && flushedBegin == (VkDeviceSize)offsetA && flushedEnd == (VkDeviceSize)offsetA + 6400 );
    assert( VK_StaticGeo_Flush() && flushes == 1 );
    // Later frames draw the same bytes without another copy, and a draw of a
    // shorter prefix (light tris share their surface's stream) is served too.
    Frame();
    assert( VK_StaticGeo_VertexOffset( &tri, 6400 ) == resident && VK_StaticGeo_VertexOffset( &tri, 640 ) == resident );
    assert( vkStaticGeo.promotions == 1 && VK_StaticGeo_Flush() && flushes == 1 );
    assert( VK_StaticGeo_VertexOffset( &tri, 6464 ) == -1 );
    // Either form decodes to the buffer and offset to bind.
    VkDeviceSize bindOffset = 0;
    assert( VK_Exec_VertexStream( 1, resident, bindOffset ) == vkStaticGeo.buffer && bindOffset == (VkDeviceSize)offsetA );
    assert( VK_Exec_VertexStream( 1, 4096, bindOffset ) == vkExec.vertexRings[ 1 ].buffer && bindOffset == 4096 );

    // The cache recycles the header for another allocation of the same size:
    // the old bytes must never be drawn for it, and the old range stays
    // reserved until every frame that could read it has finished: each frame
    // slot must open again (its fence is waited then) after the release.
    // Reopening one slot any number of times is not enough.
    header.serial = nextSerial++; header.virtMem = bytesB.data();
    const int retiringSlot = ( frameSlot + VK_FRAMES_IN_FLIGHT - 1 ) % VK_FRAMES_IN_FLIGHT;
    assert( VK_StaticGeo_VertexOffset( &tri, 6400 ) == -1 && vkStaticGeo.recycled == 1 );
    assert( vkStaticGeo.residentCount == 0 && vkStaticGeo.numRetired == 1 && liveRanges.count( offsetA ) == 1 );
    for ( int i = 0; i < 5; i++ ) {
        clockMsec += 1;
        VK_StaticGeo_BeginFrame( frameSlot );
        assert( liveRanges.count( offsetA ) == 1 && vkStaticGeo.numRetired == 1 );
    }
    VK_StaticGeo_BeginFrame( retiringSlot );
    assert( liveRanges.empty() && vkStaticGeo.numRetired == 0 );
    frameSlot = ( retiringSlot + 1 ) % VK_FRAMES_IN_FLIGHT;
    // The new allocation becomes resident with its own bytes after its own delay.
    assert( VK_StaticGeo_VertexOffset( &tri, 6400 ) == -1 );
    Frame( VK_STATIC_GEO_PROMOTE_MSEC );
    const int residentB = VK_StaticGeo_VertexOffset( &tri, 6400 );
    assert( residentB >= 0 && !std::memcmp( deviceBytes.data() + ( residentB & ~VK_STATIC_GEO_FLAG ), bytesB.data(), 6400 ) );
    assert( VK_StaticGeo_Flush() );

    // What an animated model rebuilds every tic never stays long enough.
    vertCache_t dynamicHeader; dynamicHeader.size = 640; dynamicHeader.virtMem = bytesA.data();
    srfTriangles_t dynamicTri; dynamicTri.ambientCache = &dynamicHeader;
    const uint64 promotionsBefore = vkStaticGeo.promotions;
    for ( int i = 0; i < 40; i++ ) {
        dynamicHeader.serial = nextSerial++;
        assert( VK_StaticGeo_VertexOffset( &dynamicTri, 640 ) == -1 );
        Frame( 16 );
        assert( VK_StaticGeo_VertexOffset( &dynamicTri, 640 ) == -1 );
        Frame( 16 );
    }
    assert( vkStaticGeo.promotions == promotionsBefore );

    // Frame temporaries, snapshot copies and unserialled headers are never tracked.
    const int tracked = vkStaticGeo.numEntries;
    vertCache_t temp; temp.tag = TAG_TEMP; temp.serial = nextSerial++; temp.size = 640; temp.virtMem = bytesA.data();
    srfTriangles_t tempTri; tempTri.ambientCache = &temp;
    vertCache_t unserialled; unserialled.size = 640; unserialled.virtMem = bytesA.data();
    srfTriangles_t unserialledTri; unserialledTri.ambientCache = &unserialled;
    for ( int i = 0; i < 4; i++ ) {
        assert( VK_StaticGeo_VertexOffset( &tempTri, 640 ) == -1 && VK_StaticGeo_VertexOffset( &unserialledTri, 640 ) == -1 );
        Frame( 200 );
    }
    assert( vkStaticGeo.numEntries == tracked );

    // A surface the compute pass may skin keeps to the rings, through its
    // light tris too; the repair that rewrites a block drops its copy.
    assert( VK_StaticGeo_VertexOffset( &tri, 6400 ) == residentB );
    vkGpuSkinningEverDeferred = true;
    tri.candidate = true;
    srfTriangles_t lightTri; lightTri.ambientCache = &header; lightTri.ambientSurface = &tri;
    assert( VK_StaticGeo_VertexOffset( &tri, 6400 ) == -1 && VK_StaticGeo_VertexOffset( &lightTri, 6400 ) == -1 );
    tri.candidate = false; tri.gpuSkinningDeferred = true;
    assert( VK_StaticGeo_VertexOffset( &lightTri, 6400 ) == -1 );
    tri.gpuSkinningDeferred = false;
    assert( VK_StaticGeo_VertexOffset( &lightTri, 6400 ) == residentB );
    VK_StaticGeo_Forget( &header );
    assert( vkStaticGeo.residentCount == 0 && VK_StaticGeo_Find( &header ) == NULL );
    assert( VK_StaticGeo_VertexOffset( &tri, 6400 ) == -1 );
    vkGpuSkinningEverDeferred = false;

    // r_vkStaticGeometry 0 streams everything; 2 notices a resident stream
    // that no longer equals its source and streams it again.
    Frame( VK_STATIC_GEO_PROMOTE_MSEC );
    const int residentC = VK_StaticGeo_VertexOffset( &tri, 6400 );
    assert( residentC >= 0 );
    r_vkStaticGeometry.value = 0;
    assert( VK_StaticGeo_VertexOffset( &tri, 6400 ) == -1 );
    r_vkStaticGeometry.value = 2;
    assert( VK_StaticGeo_VertexOffset( &tri, 6400 ) == residentC && vkStaticGeo.mismatches == 0 );
    bytesB[ 100 ] ^= 0x40;
    assert( VK_StaticGeo_VertexOffset( &tri, 6400 ) == -1 && vkStaticGeo.mismatches == 1 && common->warnings == 1 );
    assert( vkStaticGeo.residentCount == 0 );
    r_vkStaticGeometry.value = 1;
    for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) { Frame( 1 ); }
    assert( liveRanges.empty() && VK_StaticGeo_Flush() );

    // A full buffer declines new streams (they stay in the rings), backs off,
    // and idle resident streams then make room. Streams still being drawn stay.
    const int megs = 8;
    const int streamBytes = 1024 * 1024;
    std::vector<unsigned char> big = Payload( streamBytes, 11 );
    std::vector<vertCache_t> headers( megs + 2 );
    std::vector<srfTriangles_t> tris( megs + 2 );
    for ( size_t i = 0; i < headers.size(); i++ ) {
        headers[ i ].serial = nextSerial++; headers[ i ].size = streamBytes; headers[ i ].virtMem = big.data();
        tris[ i ].ambientCache = &headers[ i ];
    }
    for ( int i = 0; i < megs; i++ ) { assert( VK_StaticGeo_VertexOffset( &tris[ i ], streamBytes ) == -1 ); }
    Frame( VK_STATIC_GEO_PROMOTE_MSEC );
    for ( int i = 0; i < megs; i++ ) { assert( VK_StaticGeo_VertexOffset( &tris[ i ], streamBytes ) >= 0 ); }
    assert( vkStaticGeo.residentCount == megs && vkStaticGeo.residentBytes == (long long)megs * streamBytes );
    assert( VK_StaticGeo_VertexOffset( &tris[ megs ], streamBytes ) == -1 );
    Frame( VK_STATIC_GEO_PROMOTE_MSEC );
    assert( VK_StaticGeo_VertexOffset( &tris[ megs ], streamBytes ) == -1 && vkStaticGeo.declined == 1 );
    // backing off: no second attempt (and no second sweep) within the window
    assert( VK_StaticGeo_VertexOffset( &tris[ megs + 1 ], streamBytes ) == -1 );
    Frame( VK_STATIC_GEO_PROMOTE_MSEC );
    assert( VK_StaticGeo_VertexOffset( &tris[ megs + 1 ], streamBytes ) == -1 && vkStaticGeo.declined == 1 );
    // Keep two streams drawn; the other six go idle and are evicted by the sweep.
    for ( unsigned int i = 0; i <= VK_STATIC_GEO_IDLE_FRAMES + (unsigned)( VK_STATIC_GEO_MAX_ENTRIES / VK_STATIC_GEO_SWEEP_PER_FRAME ) + 8; i++ ) {
        Frame( 1 );
        assert( VK_StaticGeo_VertexOffset( &tris[ 0 ], streamBytes ) >= 0 && VK_StaticGeo_VertexOffset( &tris[ 1 ], streamBytes ) >= 0 );
        (void)VK_StaticGeo_VertexOffset( &tris[ megs ], streamBytes );
    }
    assert( vkStaticGeo.evictions == (uint64)megs - 2 );
    assert( VK_StaticGeo_VertexOffset( &tris[ megs ], streamBytes ) >= 0 && vkStaticGeo.residentCount == 3 );
    assert( VK_StaticGeo_Flush() );

    // A stream larger than the whole buffer is simply streamed.
    vertCache_t huge; huge.serial = nextSerial++; huge.size = megs * 1024 * 1024 + 64; huge.virtMem = big.data();
    srfTriangles_t hugeTri; hugeTri.ambientCache = &huge;
    assert( VK_StaticGeo_VertexOffset( &hugeTri, 64 ) == -1 );
    Frame( VK_STATIC_GEO_PROMOTE_MSEC );
    assert( VK_StaticGeo_VertexOffset( &hugeTri, 64 ) == -1 );

    // Shutdown releases the buffer and its ranges; the next device starts empty.
    VK_StaticGeo_Shutdown();
    assert( buffersDestroyed == 1 && blocksDestroyed == 1 && vkStaticGeo.buffer == 0 && !vkStaticGeo.tableReady );
    assert( VK_StaticGeo_VertexOffset( &tris[ 0 ], streamBytes ) == -1 );
    Frame();
    assert( vkStaticGeo.numEntries == 0 && vkStaticGeo.residentCount == 0 );

    // A device that cannot give the buffer leaves every stream in the rings,
    // says so once, and does not ask again.
    refuseBuffer = true;
    const int warningsBefore = common->warnings;
    assert( VK_StaticGeo_VertexOffset( &tris[ 0 ], streamBytes ) == -1 );
    Frame( VK_STATIC_GEO_PROMOTE_MSEC );
    assert( VK_StaticGeo_VertexOffset( &tris[ 0 ], streamBytes ) == -1 && vkStaticGeo.failed && common->warnings == warningsBefore + 1 );
    Frame( VK_STATIC_GEO_PROMOTE_MSEC );
    assert( VK_StaticGeo_VertexOffset( &tris[ 0 ], streamBytes ) == -1 && common->warnings == warningsBefore + 1 && buffersCreated == 1 );

    std::puts( "Vulkan static geometry: promotion delay, recycled headers, retirement, exclusions, verify mode, eviction and device refusal passed" );
    return 0;
}
'''


def main():
    source = (ROOT / 'src/renderer/Vulkan/vk_GuiExecutor.cpp').read_text(encoding='utf-8')
    start = source.index('static const int VK_STATIC_GEO_FLAG = ')
    end = source.index('static void VK_StaticGeo_PrintInfo( const bool reset ) {')
    production = source[start:end]
    for needed in ('static int VK_StaticGeo_VertexOffset(', 'static void VK_StaticGeo_BeginFrame(',
                   'static ID_INLINE VkBuffer VK_Exec_VertexStream(', 'static void VK_StaticGeo_Forget('):
        if needed not in production:
            raise AssertionError(f'missing {needed!r} in the resident static geometry section')

    # The wiring the functions rely on.
    upload = source[source.index('static bool VK_Exec_UploadTriGeometry('):]
    upload = upload[:upload.index('bool VK_Exec_BindTriGeometry(')]
    if 'vertexOffset = freshGui ? -1 : VK_StaticGeo_VertexOffset( tri, vertexBytes );' not in upload:
        raise AssertionError('world streams must be offered to the resident cache, 2D surfaces never')
    if upload.index('memo.vertKey == vertKey') > upload.index('VK_StaticGeo_VertexOffset('):
        raise AssertionError('the per-frame memo answers before the resident cache is asked')
    begin = source[source.index('bool VK_GuiExecutor_BeginFrame( void ) {'):]
    begin = begin[:begin.index('\n}\n')]
    if not ( begin.index('vkWaitForFences( vkCtx.device, 1,') < begin.index('vkExec.vertexRings[ slot ].cursor = 0;')
             < begin.index('VK_StaticGeo_BeginFrame( slot );') ):
        raise AssertionError('retired ranges are released only once the frame slot is open again, its fence waited')
    submit = source[source.index('static bool VK_GuiExecutor_SubmitFrame( bool present ) {'):]
    if submit.index('VK_StaticGeo_Flush()') > submit.index('vkQueueSubmit2('):
        raise AssertionError('resident copies are flushed before the submit that reads them')
    if 'VK_StaticGeo_Forget( tri->ambientCache );\n\tmemcpy( vertexCache.Position( tri->ambientCache )' \
            not in source.replace('\r\n', '\n'):
        raise AssertionError('the deferred-skinning repair drops the resident copy before rewriting the block')
    if source.count('VK_StaticGeo_Shutdown();') != 1:
        raise AssertionError('the resident buffer dies with the executor')
    cache = (ROOT / 'src/renderer/VertexCache.cpp').read_text(encoding='utf-8')
    if 'block->serial = ++vertexCacheAllocSerial;' not in cache or cache.count('block->serial = 0;') < 5:
        raise AssertionError('static allocations take a fresh serial; temporaries and freed headers carry none')

    compiler = next((path for name in ('clang++', 'g++', 'c++') if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='vulkan-static-geometry-', dir=ROOT / '.tmp') as folder:
        cpp, binary = Path(folder) / 'static_geometry.cpp', Path(folder) / 'static_geometry.exe'
        cpp.write_text(SUPPORT + production.replace('\r\n', '\n') + CASES, encoding='utf-8')
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wno-unused-function', str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
