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
enum { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO = 12, VK_BUFFER_USAGE_INDEX_BUFFER_BIT = 0x40, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT = 0x80 };
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

struct vertCache_t { int tag = TAG_USED; unsigned long long serial = 0; int size = 0; int offset = 0; void *virtMem = nullptr;
                     vertCache_t **user = nullptr; bool indexBuffer = false; };
typedef int glIndex_t;
struct shadowCache_t { float xyzw[ 4 ]; };
struct srfTriangles_t { vertCache_t *ambientCache = nullptr; const srfTriangles_t *ambientSurface = nullptr;
                        bool gpuSkinningDeferred = false; bool candidate = false;
                        vertCache_t *shadowCache = nullptr; shadowCache_t *shadowVertexes = nullptr; glIndex_t *indexes = nullptr;
                        vertCache_t *indexCache = nullptr; };
bool R_GpuSkinning_IsCandidate( const srfTriangles_t *tri ) { return tri->candidate; }
bool vkGpuSkinningEverDeferred = false;

struct Common {
    int warnings = 0, prints = 0;
    void Warning( const char *, ... ) { ++warnings; }
    void Printf( const char *, ... ) { ++prints; }
} commonStore, *common = &commonStore;
struct CVarInt { int value; int GetInteger() const { return value; } };
struct CVarBool { bool value; bool GetBool() const { return value; } };
CVarInt r_vkStaticGeometry{ 1 }, r_vkStaticGeometryMegs{ 8 };
CVarBool r_vkStaticShadowVolumes{ true }, r_vkStaticSurfaceIndexes{ true }, r_vkStaticInteractionIndexes{ true };
int clockMsec = 1000;
int Sys_Milliseconds() { return clockMsec; }

// the device double
struct { int allocator = 1; } vkCtx;
struct Ring { VkBuffer buffer = 77; };
struct IndexRing { VkBuffer buffer = 78; };
struct { Ring vertexRings[ VK_FRAMES_IN_FLIGHT ]; IndexRing indexRings[ VK_FRAMES_IN_FLIGHT ]; } vkExec;
std::vector<unsigned char> deviceBytes;
bool refuseBuffer = false;
int buffersCreated = 0, buffersDestroyed = 0, blocksDestroyed = 0;
VkDeviceSize flushedBegin = 0, flushedEnd = 0;
int flushes = 0;
std::map<VkDeviceSize, VkDeviceSize> liveRanges;	// offset -> size
VkDeviceSize blockSize = 0;

VkResult vmaCreateBuffer( int, const VkBufferCreateInfo *bci, const VmaAllocationCreateInfo *vaci,
        VkBuffer *buffer, VmaAllocation *allocation, VmaAllocationInfo *info ) {
    // vertex streams and the index lists that ride on a block share the buffer
    assert( bci->usage == ( VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT )
            && ( vaci->flags & VMA_ALLOCATION_CREATE_MAPPED_BIT ) != 0 );
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

# What the front end's rule for an interaction's lists reads (Interaction.cpp).
FRONT_END = r'''
struct surfaceInteraction_t { int firstLinkMsec = 0; };
struct { int frameCount = 0; } tr;
bool renderThreadOverlap = false;
bool R_RenderThread_OverlapRequested( void ) { return renderThreadOverlap; }
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

    // Stencil shadow volumes. A volume that owns its block (a prelight volume:
    // its projected vertices were copied into the block, whose owner is the
    // surface's own shadowCache field) keeps both streams resident: the block,
    // and the index list that shares its life.
    {
        std::vector<unsigned char> volumeVerts = Payload( 3200, 17 ), borrowedVerts = Payload( 1600, 29 );
        std::vector<glIndex_t> listA( 300 ), listB( 300 ), listC( 150 ), borrowedList( 60 );
        for ( size_t i = 0; i < listA.size(); i++ ) {
            listA[ i ] = static_cast<glIndex_t>( i * 3 );
            listB[ i ] = static_cast<glIndex_t>( i * 5 + 1 );
        }
        for ( size_t i = 0; i < listC.size(); i++ ) { listC[ i ] = static_cast<glIndex_t>( i + 9 ); }
        const size_t listBytes = listA.size() * sizeof( glIndex_t );
        shadowCache_t projected[ 1 ];
        vertCache_t volumeHeader; volumeHeader.serial = nextSerial++; volumeHeader.size = 3200; volumeHeader.virtMem = volumeVerts.data();
        srfTriangles_t volume; volume.shadowCache = &volumeHeader; volume.shadowVertexes = projected; volume.indexes = listA.data();
        volumeHeader.user = &volume.shadowCache;

        assert( VK_StaticGeo_VolumeVertexOffset( &volume, 3200 ) == -1 && VK_StaticGeo_VolumeIndexOffset( &volume, listBytes ) == -1 );
        Frame( VK_STATIC_GEO_PROMOTE_MSEC - 20 );
        assert( VK_StaticGeo_VolumeVertexOffset( &volume, 3200 ) == -1 && VK_StaticGeo_VolumeIndexOffset( &volume, listBytes ) == -1 );
        assert( vkStaticGeo.volumeVertexHitBytes == 0 && vkStaticGeo.volumeIndexHitBytes == 0 );
        Frame( 40 );
        const int volumeVertex = VK_StaticGeo_VolumeVertexOffset( &volume, 3200 );
        const int volumeIndex = VK_StaticGeo_VolumeIndexOffset( &volume, listBytes );
        assert( volumeVertex >= 0 && volumeIndex >= 0 && volumeVertex != volumeIndex );
        assert( !std::memcmp( deviceBytes.data() + ( volumeVertex & ~VK_STATIC_GEO_FLAG ), volumeVerts.data(), 3200 ) );
        assert( !std::memcmp( deviceBytes.data() + ( volumeIndex & ~VK_STATIC_GEO_FLAG ), listA.data(), listBytes ) );
        assert( ( volumeIndex & ~VK_STATIC_GEO_FLAG ) % 4 == 0 );
        assert( vkStaticGeo.volumeVertexHitBytes == 3200 && vkStaticGeo.volumeIndexHitBytes == listBytes );
        // the block and the list that rides on it are two entries of one table
        vkStaticGeoEntry_t *blockEntry = VK_StaticGeo_Find( &volumeHeader );
        vkStaticGeoEntry_t *listEntry = VK_StaticGeo_Find( VK_StaticGeo_IndexListKey( &volumeHeader ) );
        assert( blockEntry != NULL && listEntry != NULL && blockEntry != listEntry );
        assert( blockEntry->list == NULL && listEntry->list == listA.data() && listEntry->listBytes == (int)listBytes );
        // each keeps its own length: the block all of its bytes, the list only its own
        assert( blockEntry->bytes == 3200 && listEntry->bytes == (int)listBytes );
        // either form of an index offset decodes to the buffer and offset to bind
        VkDeviceSize volumeBind = 0;
        assert( VK_Exec_IndexStream( 0, volumeIndex, volumeBind ) == vkStaticGeo.buffer
                && volumeBind == (VkDeviceSize)( volumeIndex & ~VK_STATIC_GEO_FLAG ) );
        assert( VK_Exec_IndexStream( 1, 512, volumeBind ) == vkExec.indexRings[ 1 ].buffer && volumeBind == 512
                && vkExec.indexRings[ 1 ].buffer != vkExec.vertexRings[ 1 ].buffer );
        assert( VK_StaticGeo_Flush() );
        Frame();
        const uint64 volumePromotions = vkStaticGeo.promotions;
        assert( VK_StaticGeo_VolumeVertexOffset( &volume, 3200 ) == volumeVertex
                && VK_StaticGeo_VolumeIndexOffset( &volume, listBytes ) == volumeIndex && vkStaticGeo.promotions == volumePromotions );

        // The surface shows another list under the same block. It never does
        // (the list is allocated with the surface and freed with it), and if
        // it did the old list must not be drawn for the new one; the vertex
        // stream is untouched. The same list at another length is another list.
        uint64 recycledBefore = vkStaticGeo.recycled;
        volume.indexes = listB.data();
        assert( VK_StaticGeo_VolumeIndexOffset( &volume, listBytes ) == -1 && vkStaticGeo.recycled == recycledBefore + 1 );
        assert( VK_StaticGeo_VolumeVertexOffset( &volume, 3200 ) == volumeVertex );
        Frame( VK_STATIC_GEO_PROMOTE_MSEC );
        const int volumeIndexB = VK_StaticGeo_VolumeIndexOffset( &volume, listBytes );
        assert( volumeIndexB >= 0 && !std::memcmp( deviceBytes.data() + ( volumeIndexB & ~VK_STATIC_GEO_FLAG ), listB.data(), listBytes ) );
        assert( VK_StaticGeo_VolumeIndexOffset( &volume, listBytes / 2 ) == -1 && vkStaticGeo.recycled == recycledBefore + 2 );
        Frame( VK_STATIC_GEO_PROMOTE_MSEC );
        assert( VK_StaticGeo_VolumeIndexOffset( &volume, listBytes / 2 ) >= 0 );
        assert( VK_StaticGeo_VolumeIndexOffset( &volume, listBytes ) == -1 );
        Frame( VK_STATIC_GEO_PROMOTE_MSEC );
        assert( VK_StaticGeo_VolumeIndexOffset( &volume, listBytes ) >= 0 );

        // The cache recycles the header: both streams start again, and the new
        // allocation is never served from the old one's copies.
        recycledBefore = vkStaticGeo.recycled;
        std::vector<unsigned char> volumeVertsB = Payload( 3200, 101 );
        volumeHeader.serial = nextSerial++; volumeHeader.virtMem = volumeVertsB.data();
        assert( VK_StaticGeo_VolumeVertexOffset( &volume, 3200 ) == -1 && VK_StaticGeo_VolumeIndexOffset( &volume, listBytes ) == -1 );
        assert( vkStaticGeo.recycled == recycledBefore + 2 );
        Frame( VK_STATIC_GEO_PROMOTE_MSEC );
        const int volumeVertexB = VK_StaticGeo_VolumeVertexOffset( &volume, 3200 );
        assert( volumeVertexB >= 0 && !std::memcmp( deviceBytes.data() + ( volumeVertexB & ~VK_STATIC_GEO_FLAG ), volumeVertsB.data(), 3200 ) );
        assert( VK_StaticGeo_VolumeIndexOffset( &volume, listBytes ) >= 0 );

        // A volume that borrows the block of the surface it is cast from (no
        // projected vertices; the block belongs to that surface) shares one
        // resident vertex stream with every other light's volume of the
        // surface. Its index list is one of many over that block and stays
        // in the ring: nothing is tracked for it.
        vertCache_t borrowedHeader; borrowedHeader.serial = nextSerial++; borrowedHeader.size = 1600; borrowedHeader.virtMem = borrowedVerts.data();
        srfTriangles_t caster; caster.shadowCache = &borrowedHeader; borrowedHeader.user = &caster.shadowCache;
        srfTriangles_t lightA, lightB;
        lightA.shadowCache = lightB.shadowCache = &borrowedHeader;
        lightA.indexes = borrowedList.data(); lightB.indexes = listC.data();
        const int trackedBefore = vkStaticGeo.numEntries;
        assert( VK_StaticGeo_VolumeVertexOffset( &lightA, 1600 ) == -1 );
        Frame( VK_STATIC_GEO_PROMOTE_MSEC );
        const int shared = VK_StaticGeo_VolumeVertexOffset( &lightA, 1600 );
        assert( shared >= 0 && VK_StaticGeo_VolumeVertexOffset( &lightB, 1600 ) == shared );
        const uint64 indexHitsBefore = vkStaticGeo.volumeIndexHitBytes;
        for ( int i = 0; i < 3; i++ ) {
            assert( VK_StaticGeo_VolumeIndexOffset( &lightA, borrowedList.size() * sizeof( glIndex_t ) ) == -1
                    && VK_StaticGeo_VolumeIndexOffset( &lightB, listC.size() * sizeof( glIndex_t ) ) == -1 );
            Frame( VK_STATIC_GEO_PROMOTE_MSEC );
        }
        assert( vkStaticGeo.numEntries == trackedBefore + 1 && VK_StaticGeo_Find( VK_StaticGeo_IndexListKey( &borrowedHeader ) ) == NULL );
        assert( vkStaticGeo.volumeIndexHitBytes == indexHitsBefore );
        // Projected vertices do not make a copy of the surface the block's
        // owner, and owning a block without projected vertices is the cast-from
        // surface itself, which is not a volume.
        srfTriangles_t copy = volume;
        assert( VK_StaticGeo_VolumeIndexOffset( &copy, listBytes ) == -1 );
        caster.indexes = listC.data();
        assert( VK_StaticGeo_VolumeIndexOffset( &caster, listC.size() * sizeof( glIndex_t ) ) == -1 );
        assert( VK_StaticGeo_Find( VK_StaticGeo_IndexListKey( &borrowedHeader ) ) == NULL );

        // r_vkStaticShadowVolumes 0 copies every volume as before; r_vkStaticGeometry 0
        // does too; 2 notices a resident index list that no longer equals its source.
        r_vkStaticShadowVolumes.value = false;
        assert( VK_StaticGeo_VolumeVertexOffset( &volume, 3200 ) == -1 && VK_StaticGeo_VolumeIndexOffset( &volume, listBytes ) == -1 );
        r_vkStaticShadowVolumes.value = true;
        r_vkStaticGeometry.value = 0;
        assert( VK_StaticGeo_VolumeVertexOffset( &volume, 3200 ) == -1 && VK_StaticGeo_VolumeIndexOffset( &volume, listBytes ) == -1 );
        r_vkStaticGeometry.value = 2;
        const uint64 mismatchesBefore = vkStaticGeo.mismatches;
        const int volumeWarnings = common->warnings;
        assert( VK_StaticGeo_VolumeVertexOffset( &volume, 3200 ) == volumeVertexB && VK_StaticGeo_VolumeIndexOffset( &volume, listBytes ) >= 0 );
        assert( vkStaticGeo.mismatches == mismatchesBefore );
        listB[ 7 ] ^= 2;
        assert( VK_StaticGeo_VolumeIndexOffset( &volume, listBytes ) == -1 && vkStaticGeo.mismatches == mismatchesBefore + 1
                && common->warnings == volumeWarnings + 1 );
        assert( VK_StaticGeo_VolumeVertexOffset( &volume, 3200 ) == volumeVertexB );
        r_vkStaticGeometry.value = 1;
        assert( VK_StaticGeo_Flush() );
    }

    // A model surface's own index list. The surface owns its vertex block (the
    // block names the surface's ambientCache field) and its list alike, so the
    // list rides on the block: resident after its own delay, beside the vertex
    // stream and apart from it.
    {
        std::vector<unsigned char> surfaceVerts = Payload( 4800, 41 );
        std::vector<glIndex_t> own( 240 ), subset( 90 ), other( 240 );
        for ( size_t i = 0; i < own.size(); i++ ) {
            own[ i ] = static_cast<glIndex_t>( i * 2 + 5 );
            other[ i ] = static_cast<glIndex_t>( i * 7 + 3 );
        }
        for ( size_t i = 0; i < subset.size(); i++ ) { subset[ i ] = static_cast<glIndex_t>( i * 2 + 5 ); }
        const size_t ownBytes = own.size() * sizeof( glIndex_t ), subsetBytes = subset.size() * sizeof( glIndex_t );
        vertCache_t surfaceHeader; surfaceHeader.serial = nextSerial++; surfaceHeader.size = 4800; surfaceHeader.virtMem = surfaceVerts.data();
        srfTriangles_t surface; surface.ambientCache = &surfaceHeader; surface.indexes = own.data();
        surfaceHeader.user = &surface.ambientCache;

        assert( VK_StaticGeo_VertexOffset( &surface, 4800 ) == -1 && VK_StaticGeo_SurfaceIndexOffset( &surface, ownBytes ) == -1 );
        Frame( VK_STATIC_GEO_PROMOTE_MSEC - 20 );
        assert( VK_StaticGeo_SurfaceIndexOffset( &surface, ownBytes ) == -1 && vkStaticGeo.surfaceIndexHitBytes == 0 );
        Frame( 40 );
        const int surfaceVertex = VK_StaticGeo_VertexOffset( &surface, 4800 );
        const int surfaceIndex = VK_StaticGeo_SurfaceIndexOffset( &surface, ownBytes );
        assert( surfaceVertex >= 0 && surfaceIndex >= 0 && surfaceIndex != surfaceVertex );
        assert( !std::memcmp( deviceBytes.data() + ( surfaceIndex & ~VK_STATIC_GEO_FLAG ), own.data(), ownBytes ) );
        assert( !std::memcmp( deviceBytes.data() + ( surfaceVertex & ~VK_STATIC_GEO_FLAG ), surfaceVerts.data(), 4800 ) );
        assert( vkStaticGeo.surfaceIndexHitBytes == ownBytes );
        vkStaticGeoEntry_t *ownEntry = VK_StaticGeo_Find( VK_StaticGeo_IndexListKey( &surfaceHeader ) );
        assert( ownEntry != NULL && ownEntry != VK_StaticGeo_Find( &surfaceHeader )
                && ownEntry->list == own.data() && ownEntry->bytes == (int)ownBytes );
        assert( VK_StaticGeo_Flush() );
        Frame();
        const uint64 surfacePromotions = vkStaticGeo.promotions;
        assert( VK_StaticGeo_SurfaceIndexOffset( &surface, ownBytes ) == surfaceIndex && vkStaticGeo.promotions == surfacePromotions );

        // A light interaction's triangles share the block and draw a subset of
        // their own over it. They are never the owner, not even when they are
        // completely inside the light and point at the surface's own list (the
        // per-frame memo serves that case); nor is a frame's copy of the
        // surface, nor a surface without a list (a packed mesh).
        const int trackedBefore = vkStaticGeo.numEntries;
        const uint64 hitBytesBefore = vkStaticGeo.surfaceIndexHitBytes;
        srfTriangles_t lightTris; lightTris.ambientCache = &surfaceHeader; lightTris.ambientSurface = &surface; lightTris.indexes = subset.data();
        assert( VK_StaticGeo_SurfaceIndexOffset( &lightTris, subsetBytes ) == -1 );
        lightTris.indexes = own.data();
        assert( VK_StaticGeo_SurfaceIndexOffset( &lightTris, ownBytes ) == -1 );
        srfTriangles_t frameCopy = surface;
        assert( VK_StaticGeo_SurfaceIndexOffset( &frameCopy, ownBytes ) == -1 );
        const uint64 recycledUntouched = vkStaticGeo.recycled;
        surface.indexes = nullptr;
        assert( VK_StaticGeo_SurfaceIndexOffset( &surface, ownBytes ) == -1 );
        surface.indexes = own.data();
        assert( vkStaticGeo.numEntries == trackedBefore && vkStaticGeo.surfaceIndexHitBytes == hitBytesBefore );
        // none of those refusals touched the block's own entry: its vertex stream stays where it was
        assert( vkStaticGeo.recycled == recycledUntouched && VK_StaticGeo_VertexOffset( &surface, 4800 ) == surfaceVertex );
        assert( VK_StaticGeo_SurfaceIndexOffset( &surface, ownBytes ) == surfaceIndex );

        // The repair that rewrites a block in place drops the vertex stream's
        // copy and leaves the list's alone: the list did not change.
        VK_StaticGeo_Forget( &surfaceHeader );
        assert( VK_StaticGeo_Find( &surfaceHeader ) == NULL && VK_StaticGeo_SurfaceIndexOffset( &surface, ownBytes ) == surfaceIndex );

        // Another list under the same block, and a recycled header, start again.
        uint64 recycledBefore = vkStaticGeo.recycled;
        surface.indexes = other.data();
        assert( VK_StaticGeo_SurfaceIndexOffset( &surface, ownBytes ) == -1 && vkStaticGeo.recycled == recycledBefore + 1 );
        Frame( VK_STATIC_GEO_PROMOTE_MSEC );
        const int otherIndex = VK_StaticGeo_SurfaceIndexOffset( &surface, ownBytes );
        assert( otherIndex >= 0 && !std::memcmp( deviceBytes.data() + ( otherIndex & ~VK_STATIC_GEO_FLAG ), other.data(), ownBytes ) );
        surfaceHeader.serial = nextSerial++;
        assert( VK_StaticGeo_SurfaceIndexOffset( &surface, ownBytes ) == -1 && vkStaticGeo.recycled == recycledBefore + 2 );
        Frame( VK_STATIC_GEO_PROMOTE_MSEC );
        assert( VK_StaticGeo_SurfaceIndexOffset( &surface, ownBytes ) >= 0 );

        // r_vkStaticSurfaceIndexes 0 copies every list as before, leaving volumes
        // and vertex streams resident; r_vkStaticGeometry 0 copies everything;
        // 2 notices a resident list that no longer equals its source.
        r_vkStaticSurfaceIndexes.value = false;
        assert( VK_StaticGeo_SurfaceIndexOffset( &surface, ownBytes ) == -1 );
        r_vkStaticSurfaceIndexes.value = true;
        r_vkStaticGeometry.value = 0;
        assert( VK_StaticGeo_SurfaceIndexOffset( &surface, ownBytes ) == -1 );
        r_vkStaticGeometry.value = 2;
        const uint64 mismatchesBefore = vkStaticGeo.mismatches;
        const int surfaceWarnings = common->warnings;
        assert( VK_StaticGeo_SurfaceIndexOffset( &surface, ownBytes ) >= 0 && vkStaticGeo.mismatches == mismatchesBefore );
        other[ 11 ] ^= 4;
        // counted every time; said at the first, second, fourth, ... difference, and this is the third
        assert( VK_StaticGeo_SurfaceIndexOffset( &surface, ownBytes ) == -1 && vkStaticGeo.mismatches == mismatchesBefore + 1
                && vkStaticGeo.mismatches == 3 && common->warnings == surfaceWarnings );
        assert( VK_StaticGeo_Find( VK_StaticGeo_IndexListKey( &surfaceHeader ) )->offset < 0 );
        r_vkStaticGeometry.value = 1;
        assert( VK_StaticGeo_Flush() );
    }

    // An index list that belongs to a light interaction: the part of a surface
    // the light reaches, or a volume cast from an entity's surface. No vertex
    // block shares its life, so the front end gives a lasting one an index
    // block of its own, and that block's serial is the list's identity.
    {
        std::vector<glIndex_t> subset( 180 ), blockCopy, later( 180 );
        for ( size_t i = 0; i < subset.size(); i++ ) {
            subset[ i ] = static_cast<glIndex_t>( i * 3 + 2 );
            later[ i ] = static_cast<glIndex_t>( i * 11 + 4 );
        }
        blockCopy = subset;
        const size_t subsetBytes = subset.size() * sizeof( glIndex_t );
        std::vector<unsigned char> litVerts = Payload( 2560, 53 );
        vertCache_t litHeader; litHeader.serial = nextSerial++; litHeader.size = 2560; litHeader.virtMem = litVerts.data();
        srfTriangles_t lit; lit.ambientCache = &litHeader; litHeader.user = &lit.ambientCache;
        vertCache_t listHeader; listHeader.serial = nextSerial++; listHeader.size = (int)subsetBytes; listHeader.virtMem = blockCopy.data();
        listHeader.indexBuffer = true;
        srfTriangles_t lightTris; lightTris.ambientCache = &litHeader; lightTris.ambientSurface = &lit; lightTris.indexes = subset.data();

        // Without a block of its own the list stays in the ring, and nothing is tracked for it.
        const int trackedBefore = vkStaticGeo.numEntries;
        assert( VK_StaticGeo_InteractionIndexOffset( &lightTris, subsetBytes ) == -1 && vkStaticGeo.numEntries == trackedBefore );
        lightTris.indexCache = &listHeader; listHeader.user = &lightTris.indexCache;
        assert( VK_StaticGeo_InteractionIndexesWanted() );

        // With one it is resident after the block's own delay, whether or not
        // the vertices it indexes are.
        assert( VK_StaticGeo_InteractionIndexOffset( &lightTris, subsetBytes ) == -1 && vkStaticGeo.numEntries == trackedBefore + 1 );
        Frame( VK_STATIC_GEO_PROMOTE_MSEC - 20 );
        assert( VK_StaticGeo_InteractionIndexOffset( &lightTris, subsetBytes ) == -1 && vkStaticGeo.interactionIndexHitBytes == 0 );
        Frame( 40 );
        const int listOffset = VK_StaticGeo_InteractionIndexOffset( &lightTris, subsetBytes );
        assert( listOffset >= 0 && ( listOffset & VK_STATIC_GEO_FLAG ) != 0 && ( listOffset & ~VK_STATIC_GEO_FLAG ) % 4 == 0 );
        assert( !std::memcmp( deviceBytes.data() + ( listOffset & ~VK_STATIC_GEO_FLAG ), subset.data(), subsetBytes ) );
        assert( vkStaticGeo.interactionIndexHitBytes == subsetBytes );
        // it is the block's own entry: no list rides on an index block
        assert( VK_StaticGeo_Find( &listHeader ) != NULL && VK_StaticGeo_Find( &listHeader )->list == NULL
                && VK_StaticGeo_Find( VK_StaticGeo_IndexListKey( &listHeader ) ) == NULL );
        // the surface's own list is another matter and is still refused to the light's triangles
        assert( VK_StaticGeo_SurfaceIndexOffset( &lightTris, subsetBytes ) == -1 );
        assert( VK_StaticGeo_Flush() );
        Frame();
        const uint64 listPromotions = vkStaticGeo.promotions;
        assert( VK_StaticGeo_InteractionIndexOffset( &lightTris, subsetBytes ) == listOffset && vkStaticGeo.promotions == listPromotions );

        // Only the surface that owns the block is served, only for all of the
        // block, and only where the block is a CPU-backed index block that
        // holds a copy of a list the surface still has.
        const uint64 hitsBefore = vkStaticGeo.interactionIndexHitBytes, recycledBefore = vkStaticGeo.recycled;
        srfTriangles_t frameCopy = lightTris;
        assert( VK_StaticGeo_InteractionIndexOffset( &frameCopy, subsetBytes ) == -1 );
        assert( VK_StaticGeo_InteractionIndexOffset( &lightTris, subsetBytes / 2 ) == -1 );
        lightTris.indexes = nullptr;
        assert( VK_StaticGeo_InteractionIndexOffset( &lightTris, subsetBytes ) == -1 );
        lightTris.indexes = subset.data();
        listHeader.indexBuffer = false;
        assert( VK_StaticGeo_InteractionIndexOffset( &lightTris, subsetBytes ) == -1 );
        listHeader.indexBuffer = true; listHeader.virtMem = nullptr;
        assert( VK_StaticGeo_InteractionIndexOffset( &lightTris, subsetBytes ) == -1 );
        listHeader.virtMem = blockCopy.data(); listHeader.tag = TAG_TEMP;
        assert( VK_StaticGeo_InteractionIndexOffset( &lightTris, subsetBytes ) == -1 );
        listHeader.tag = TAG_USED;
        assert( vkStaticGeo.interactionIndexHitBytes == hitsBefore && vkStaticGeo.recycled == recycledBefore );
        assert( VK_StaticGeo_InteractionIndexOffset( &lightTris, subsetBytes ) == listOffset );

        // The interaction is rebuilt: its surface is freed, the block with it,
        // and the header comes back with another serial for another list.
        subset = later; blockCopy = later;
        listHeader.serial = nextSerial++; listHeader.virtMem = blockCopy.data();
        assert( VK_StaticGeo_InteractionIndexOffset( &lightTris, subsetBytes ) == -1 && vkStaticGeo.recycled == recycledBefore + 1 );
        Frame( VK_STATIC_GEO_PROMOTE_MSEC );
        const int laterOffset = VK_StaticGeo_InteractionIndexOffset( &lightTris, subsetBytes );
        assert( laterOffset >= 0 && !std::memcmp( deviceBytes.data() + ( laterOffset & ~VK_STATIC_GEO_FLAG ), later.data(), subsetBytes ) );

        // A stencil volume cast from an entity's surface borrows the surface's
        // vertex block; its list is refused as a volume's own and served as
        // the interaction's.
        std::vector<glIndex_t> castList( 96 ), castCopy;
        for ( size_t i = 0; i < castList.size(); i++ ) { castList[ i ] = static_cast<glIndex_t>( i * 5 ); }
        castCopy = castList;
        const size_t castBytes = castList.size() * sizeof( glIndex_t );
        vertCache_t castHeader; castHeader.serial = nextSerial++; castHeader.size = (int)castBytes; castHeader.virtMem = castCopy.data();
        castHeader.indexBuffer = true;
        srfTriangles_t cast; cast.shadowCache = &litHeader; cast.indexes = castList.data(); cast.indexCache = &castHeader;
        castHeader.user = &cast.indexCache;
        assert( VK_StaticGeo_VolumeIndexOffset( &cast, castBytes ) == -1 && VK_StaticGeo_InteractionIndexOffset( &cast, castBytes ) == -1 );
        Frame( VK_STATIC_GEO_PROMOTE_MSEC );
        const int castOffset = VK_StaticGeo_InteractionIndexOffset( &cast, castBytes );
        assert( VK_StaticGeo_VolumeIndexOffset( &cast, castBytes ) == -1 && castOffset >= 0 && castOffset != laterOffset );
        assert( !std::memcmp( deviceBytes.data() + ( castOffset & ~VK_STATIC_GEO_FLAG ), castList.data(), castBytes ) );

        // r_vkStaticInteractionIndexes 0 copies these lists as before and asks
        // the front end for no further blocks; r_vkStaticGeometry 0 does too.
        r_vkStaticInteractionIndexes.value = false;
        assert( VK_StaticGeo_InteractionIndexOffset( &lightTris, subsetBytes ) == -1 && !VK_StaticGeo_InteractionIndexesWanted() );
        r_vkStaticInteractionIndexes.value = true;
        r_vkStaticGeometry.value = 0;
        assert( VK_StaticGeo_InteractionIndexOffset( &lightTris, subsetBytes ) == -1 && !VK_StaticGeo_InteractionIndexesWanted() );
        // 2 notices a resident copy that no longer equals the block, and a
        // block that no longer equals the list drawn from; the second drops
        // the block's entry, so the list starts again.
        r_vkStaticGeometry.value = 2;
        uint64 mismatchesBefore = vkStaticGeo.mismatches;
        assert( VK_StaticGeo_InteractionIndexesWanted() );
        assert( VK_StaticGeo_InteractionIndexOffset( &lightTris, subsetBytes ) == laterOffset && vkStaticGeo.mismatches == mismatchesBefore );
        subset[ 5 ] ^= 8;
        assert( VK_StaticGeo_InteractionIndexOffset( &lightTris, subsetBytes ) == -1 && vkStaticGeo.mismatches == mismatchesBefore + 1 );
        assert( VK_StaticGeo_Find( &listHeader ) == NULL );
        subset[ 5 ] ^= 8;
        assert( VK_StaticGeo_InteractionIndexOffset( &lightTris, subsetBytes ) == -1 );
        Frame( VK_STATIC_GEO_PROMOTE_MSEC );
        assert( VK_StaticGeo_InteractionIndexOffset( &lightTris, subsetBytes ) >= 0 );
        mismatchesBefore = vkStaticGeo.mismatches;
        castCopy[ 9 ] ^= 1; castList[ 9 ] ^= 1;
        assert( VK_StaticGeo_InteractionIndexOffset( &cast, castBytes ) == -1 && vkStaticGeo.mismatches == mismatchesBefore + 1 );
        r_vkStaticGeometry.value = 1;
        assert( VK_StaticGeo_Flush() );

        // The front end's rule. A surface interaction is stamped when it is
        // first linked and is due its blocks once it has lasted; one rebuilt
        // every tic starts from an empty record each time and never is.
        surfaceInteraction_t lasting;
        const int clockBefore = clockMsec;
        tr.frameCount = 100; clockMsec = 5000;
        assert( !R_InteractionIndexBlockDue( &lasting ) && lasting.firstLinkMsec == 5000 );
        tr.frameCount++; clockMsec += INTERACTION_INDEX_BLOCK_MSEC - 1;
        assert( !R_InteractionIndexBlockDue( &lasting ) && lasting.firstLinkMsec == 5000 );
        // the time is read once a frame: every view of a frame answers alike
        clockMsec += 50;
        assert( !R_InteractionIndexBlockDue( &lasting ) );
        tr.frameCount++;
        assert( R_InteractionIndexBlockDue( &lasting ) && lasting.firstLinkMsec == 5000 );
        for ( int i = 0; i < 40; i++ ) {
            surfaceInteraction_t rebuilt;
            tr.frameCount++; clockMsec += 16;
            assert( !R_InteractionIndexBlockDue( &rebuilt ) && !R_InteractionIndexBlockDue( &rebuilt ) );
        }
        // Not while a frame snapshot would copy the block, nor while the
        // executor would not use it: no record is started either.
        surfaceInteraction_t unasked;
        renderThreadOverlap = true;
        assert( !R_InteractionIndexBlockDue( &unasked ) && !R_InteractionIndexBlockDue( &lasting ) && unasked.firstLinkMsec == 0 );
        renderThreadOverlap = false;
        r_vkStaticInteractionIndexes.value = false;
        assert( !R_InteractionIndexBlockDue( &unasked ) && !R_InteractionIndexBlockDue( &lasting ) && unasked.firstLinkMsec == 0 );
        r_vkStaticInteractionIndexes.value = true;
        assert( R_InteractionIndexBlockDue( &lasting ) );
        // a clock that reads zero still leaves a record
        tr.frameCount++; clockMsec = 0;
        assert( !R_InteractionIndexBlockDue( &unasked ) && unasked.firstLinkMsec == 1 );
        clockMsec = clockBefore;
    }

    // Shutdown releases the buffer and its ranges; the next device starts empty.
    VK_StaticGeo_Shutdown();
    assert( !VK_StaticGeo_InteractionIndexesWanted() );
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
    // and the front end is asked for no blocks it could not use
    assert( vkStaticGeo.tableReady && !VK_StaticGeo_InteractionIndexesWanted() );
    Frame( VK_STATIC_GEO_PROMOTE_MSEC );
    assert( VK_StaticGeo_VertexOffset( &tris[ 0 ], streamBytes ) == -1 && common->warnings == warningsBefore + 1 && buffersCreated == 1 );

    std::puts( "Vulkan static geometry: promotion delay, recycled headers, retirement, exclusions, verify mode, eviction, "
               "shadow volume streams and owned index lists, surfaces' own index lists, interactions' index lists and the "
               "front end's rule for them, and device refusal passed" );
    return 0;
}
'''


def main():
    source = (ROOT / 'src/renderer/Vulkan/vk_GuiExecutor.cpp').read_text(encoding='utf-8')
    start = source.index('static const int VK_STATIC_GEO_FLAG = ')
    end = source.index('static void VK_StaticGeo_PrintInfo( const bool reset ) {')
    production = source[start:end]
    for needed in ('static int VK_StaticGeo_VertexOffset(', 'static void VK_StaticGeo_BeginFrame(',
                   'static ID_INLINE VkBuffer VK_Exec_VertexStream(', 'static void VK_StaticGeo_Forget(',
                   'static int VK_StaticGeo_StreamOffset(', 'static int VK_StaticGeo_VolumeVertexOffset(',
                   'static int VK_StaticGeo_VolumeIndexOffset(', 'static ID_INLINE VkBuffer VK_Exec_IndexStream(',
                   'static int VK_StaticGeo_SurfaceIndexOffset(', 'static int VK_StaticGeo_InteractionIndexOffset(',
                   'bool VK_StaticGeo_InteractionIndexesWanted( void ) {'):
        if needed not in production:
            raise AssertionError(f'missing {needed!r} in the resident static geometry section')

    # The front end's half: the rule that gives a lasting interaction's lists
    # their blocks is compiled as it stands, and its two uses are pinned.
    interaction = (ROOT / 'src/renderer/Interaction.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')
    rule_start = interaction.index('#ifdef OPENQ4_RENDERER_VK_MODULE\nstatic const int INTERACTION_INDEX_BLOCK_MSEC = ')
    rule_end = interaction.index('#else\nstatic ID_INLINE bool R_InteractionIndexBlockDue( surfaceInteraction_t * ) {\n\treturn false;\n}\n#endif', rule_start)
    front_end_rule = interaction[rule_start + len('#ifdef OPENQ4_RENDERER_VK_MODULE\n'):rule_end]
    if 'static bool R_InteractionIndexBlockDue( surfaceInteraction_t *sint ) {' not in front_end_rule:
        raise AssertionError('the front end decides which interactions are due index blocks')
    shadow_site = ('\tif ( !shadowTris->indexCache && shadowTris->numIndexes > 0 && shadowTris->indexes != NULL\n'
                   '\t\t\t&& ( R_StaticIndexCacheAllowed( entityDef )\n'
                   '\t\t\t\t|| ( !shadowTris->shadowVertexes && R_InteractionIndexBlockDue( sint ) ) ) ) {\n'
                   '\t\tvertexCache.Alloc(\n\t\t\tshadowTris->indexes,\n'
                   '\t\t\tshadowTris->numIndexes * sizeof( shadowTris->indexes[0] ),\n\t\t\t&shadowTris->indexCache,\n\t\t\ttrue );')
    light_site = ('&& ( R_StaticIndexCacheAllowed( entityDef )\n'
                  '\t\t\t\t\t\t\t\t\t\t// a light that reaches the whole surface draws the surface\'s\n'
                  '\t\t\t\t\t\t\t\t\t\t// own list, which rides on the surface\'s vertex block\n'
                  '\t\t\t\t\t\t\t\t\t\t|| ( lightTris->indexes != ambientTris->indexes && R_InteractionIndexBlockDue( sint ) ) ) ) {\n'
                  '\t\t\t\t\t\t\t\tvertexCache.Alloc(\n\t\t\t\t\t\t\t\t\tlightTris->indexes,\n'
                  '\t\t\t\t\t\t\t\t\tlightTris->numIndexes * sizeof( lightTris->indexes[0] ),\n'
                  '\t\t\t\t\t\t\t\t\t&lightTris->indexCache,\n\t\t\t\t\t\t\t\t\ttrue );')
    if shadow_site not in interaction or light_site not in interaction or interaction.count('R_InteractionIndexBlockDue( sint )') != 2:
        raise AssertionError('a lasting interaction\'s own lists get blocks owned by the surfaces that hold them: a volume without '
                             'projected vertices, and light triangles that are not the surface\'s own list')
    # A packed surface's light triangles are a frame's copy and take a frame's block.
    if not ( interaction.index('lightTris = frameTris;') < interaction.index(light_site) ):
        raise AssertionError('the frame copy of a packed surface\'s light triangles is handled before the lasting case')
    # The record starts empty with the surfaces and dies with them.
    header = (ROOT / 'src/renderer/Interaction.h').read_text(encoding='utf-8')
    if 'int\t\t\t\t\t\tfirstLinkMsec;' not in header \
            or 'surfaces = (surfaceInteraction_t *)R_ClearedStaticAlloc( sizeof( *surfaces ) * numSurfaces );' not in interaction:
        raise AssertionError('a surface interaction\'s first-link record starts at zero with its surfaces')

    # Stencil shadow volumes: the per-frame memo answers first, the ring takes
    # only what has no resident copy, and both streams are bound through the
    # decoders, here and where a prepared volume is bound later.
    unix = source.replace('\r\n', '\n')
    shadow = unix[unix.index('bool VK_Exec_BindShadowGeometry('):]
    shadow = shadow[:shadow.index('bool VK_Exec_PrepareShadowGeometry(')]
    if not ( shadow.index('memo.vertKey == vertKey') < shadow.index('vertexOffset = VK_StaticGeo_VolumeVertexOffset( tri, vertexBytes );')
             < shadow.index('if ( vertexOffset < 0 ) {') < shadow.index('VK_Ring_Alloc( vkExec.vertexRings[ slot ], verts,') ):
        raise AssertionError('a volume\'s vertex stream goes to the ring only when the memo and the resident cache have none')
    if not ( shadow.index('memo.idxKey == idxKey') < shadow.index('indexOffset = VK_StaticGeo_VolumeIndexOffset( tri, indexBytes );')
             < shadow.index('if ( indexOffset < 0 ) {') < shadow.index('VK_Ring_Alloc( vkExec.indexRings[ slot ], tri->indexes,') ):
        raise AssertionError('a volume\'s index list goes to the ring only when the memo and the resident cache have none')
    volume_chain = ('indexOffset = VK_StaticGeo_VolumeIndexOffset( tri, indexBytes );\n'
                    '\t\t\tif ( indexOffset < 0 ) {\n'
                    '\t\t\t\t// a volume cast from an entity\'s surface: the interaction\'s list\n'
                    '\t\t\t\tindexOffset = VK_StaticGeo_InteractionIndexOffset( tri, indexBytes );\n'
                    '\t\t\t}\n'
                    '\t\t\tif ( indexOffset < 0 ) {\n'
                    '\t\t\t\tindexOffset = VK_Ring_Alloc( vkExec.indexRings[ slot ], tri->indexes,')
    if volume_chain not in shadow or shadow.count('VK_StaticGeo_InteractionIndexOffset(') != 1:
        raise AssertionError('a volume that does not own its vertices offers its list as the interaction\'s before the ring takes it')
    if 'VK_Exec_VertexStream( slot, vertexOffset, vertexBindOffset )' not in shadow \
            or 'VK_Exec_IndexStream( slot, indexOffset, indexBindOffset )' not in shadow \
            or 'vkExec.indexRings[ slot ].buffer, (VkDeviceSize)indexOffset' in shadow \
            or '&vkExec.vertexRings[ slot ].buffer, &vertexBindOffset' in shadow:
        raise AssertionError('a volume binds whichever buffer its offsets name')
    prepared = unix[unix.index('void VK_Exec_BindPreparedTriGeometry('):]
    prepared = prepared[:prepared.index('\n}\n')]
    if 'VK_Exec_IndexStream( slot, indexOffset, indexBindOffset )' not in prepared \
            or 'VK_Exec_VertexStream( slot, vertexOffset, bindOffset )' not in prepared:
        raise AssertionError('a prepared volume binds whichever buffer its offsets name')
    for path in sorted((ROOT / 'src/renderer/Vulkan').glob('*.cpp')):
        text = path.read_text(encoding='utf-8')
        if path.name != 'vk_GuiExecutor.cpp' and ('VK_STATIC_GEO_FLAG' in text or 'indexRings[' in text):
            raise AssertionError(f'{path.name} would have to decode resident offsets itself')

    # A surface's own index list: asked for only beside a resident vertex stream
    # (which rules out 2D surfaces and whatever the compute pass skins), after
    # the per-frame memo, and only for a plain list; the ring takes the rest.
    surface_upload = unix[unix.index('static bool VK_Exec_UploadTriGeometry('):]
    surface_upload = surface_upload[:surface_upload.index('bool VK_Exec_BindTriGeometry(')]
    lookup = ('if ( !freshGui && ( vertexOffset & VK_STATIC_GEO_FLAG ) != 0 ) {\n'
              '\t\t\t\t\tindexOffset = VK_StaticGeo_SurfaceIndexOffset( tri, indexBytes );\n\t\t\t\t}')
    if surface_upload.count('VK_StaticGeo_SurfaceIndexOffset(') != 1 or lookup not in surface_upload:
        raise AssertionError('a surface\'s index list is offered to the resident cache only beside its resident vertex stream')
    if not ( surface_upload.index('memo.idxKey == idxKey') < surface_upload.index('if ( indexSource != NULL ) {')
             < surface_upload.index(lookup) < surface_upload.index('residentIndexes = true;')
             < surface_upload.index('indexSource, indexBytes, 4 );') ):
        raise AssertionError('the memo answers first, then the resident cache, then the ring')
    if surface_upload.index('flattened.Ptr(), indexBytes, 4 );') > surface_upload.index(lookup) \
            or surface_upload.index(lookup) > surface_upload.index('cachedIndexes, indexBytes, 4 );'):
        raise AssertionError('flattened packed lists and cached lists are never offered to the resident cache')
    # A list the surface does not own is offered as an interaction's, after the
    # surface's own lookup and never for a 2D surface.
    interaction_lookup = ('if ( indexOffset < 0 && !freshGui ) {\n'
                          '\t\t\t\t\tindexOffset = VK_StaticGeo_InteractionIndexOffset( tri, indexBytes );\n\t\t\t\t}')
    if surface_upload.count('VK_StaticGeo_InteractionIndexOffset(') != 1 or interaction_lookup not in surface_upload \
            or not ( surface_upload.index(lookup) < surface_upload.index(interaction_lookup)
                     < surface_upload.index('residentIndexes = true;') ):
        raise AssertionError('an interaction\'s index list is offered to the resident cache after the surface\'s own, before the ring')
    if 'if ( !residentIndexes && r_vkResourceProfile.GetBool() ) {' not in surface_upload:
        raise AssertionError('the ring profile counts only what the ring took')
    # Every bind of an offset the upload reported decodes it. The two that
    # remain on the ring bind what they have just put there themselves.
    bind_tri = unix[unix.index('bool VK_Exec_BindTriGeometry('):]
    bind_tri = bind_tri[:bind_tri.index('\n}\n')]
    gui_bind = unix[unix.index('static void VK_ClassicGui_BindPreparedGeometry('):]
    gui_bind = gui_bind[:gui_bind.index('\n}\n')]
    if 'VK_Exec_IndexStream( slot, indexOffset, indexBindOffset )' not in bind_tri or 'indexRings' in bind_tri \
            or 'VK_Exec_IndexStream( slot, plan.indexOffset, indexOffset )' not in gui_bind or 'indexRings' in gui_bind \
            or unix.count('VK_Exec_IndexStream( slot, drawPlan.indexOffset, indexOffset )') != 1:
        raise AssertionError('a surface binds whichever buffer its index offset names')
    ring_binds = [line.strip() for line in unix.split('\n') if 'vkCmdBindIndexBuffer(' in line and 'indexRings' in line]
    following = unix.split('vkCmdBindIndexBuffer( vkExec.cmd,\n')
    if ring_binds != ['vkCmdBindIndexBuffer( cmd, vkExec.indexRings[ slot ].buffer, (VkDeviceSize)indexOffset, VK_INDEX_TYPE_UINT32 );'] \
            or len(following) != 2 or not following[1].lstrip().startswith('vkExec.indexRings[ vkExec.frameSlot ].buffer,'):
        raise AssertionError('only the raw shadow batch and the special-effects quad bind the index ring directly')
    # What an owned index list relies on: the block is allocated for the
    # surface's own field, and the surface frees it when it goes.
    light = (ROOT / 'src/renderer/tr_light.cpp').read_text(encoding='utf-8')
    if 'vertexCache.Alloc( tri->shadowVertexes, tri->numVerts * sizeof( *tri->shadowVertexes ), &tri->shadowCache );' not in light:
        raise AssertionError('a private shadow cache is owned by its surface\'s shadowCache field')
    hooks = (ROOT / 'src/renderer/tr_main.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')
    free_hook = hooks[hooks.index('static void R_GeoFreeVertexCaches( srfTriangles_t *tri ) {'):]
    free_hook = free_hook[:free_hook.index('\n}\n')]
    if 'if ( tri->shadowCache && ( tri->shadowVertexes != NULL || tri->verts != NULL ) ) {' not in free_hook \
            or 'R_GeoFreeTriSurfVertCache( tri->shadowCache );' not in free_hook:
        raise AssertionError('a surface frees the shadow cache it owns when it is freed')
    if 'R_GeoFreeTriSurfVertCache( tri->indexCache );' not in free_hook:
        raise AssertionError('a surface frees the index block it owns when it is freed')
    geometry = (ROOT / 'src/rendergeo/RenderGeometryTriSurf.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')
    really_free = geometry[geometry.index('void R_ReallyFreeStaticTriSurf( srfTriangles_t *tri ) {'):]
    if not ( really_free.index('R_FreeStaticTriSurfVertexCaches( tri );') < really_free.index('triIndexAllocator.Free( tri->indexes );') ):
        raise AssertionError('the block is released no later than the index list that rides on it')
    # The same for a model surface and the list it draws itself with. What
    # rebuilds a kept surface in place frees its vertex caches first.
    if 'vertexCache.Alloc( tri->verts, tri->numVerts * sizeof( tri->verts[0] ), &tri->ambientCache );' not in light:
        raise AssertionError('an ambient cache is owned by its surface\'s ambientCache field')
    if not ( free_hook.index('if ( tri->ambientSurface == NULL ) {') < free_hook.index('R_GeoFreeTriSurfVertCache( tri->ambientCache );')
             < free_hook.index('} else {') ):
        raise AssertionError('a model surface frees the ambient cache it owns when it is freed')
    for name in ('ModelOverlay.cpp', 'Model_beam.cpp', 'Model_md5.cpp'):
        if 'R_FreeStaticTriSurfVertexCaches(' not in (ROOT / 'src/renderer' / name).read_text(encoding='utf-8'):
            raise AssertionError(f'{name} rebuilds a kept surface in place and must free its vertex caches')

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
    if 'block->user = buffer;' not in cache or '\tblock->user = NULL;' not in cache.replace('\r\n', '\n'):
        raise AssertionError('a static block names the field that owns it until it is freed')

    compiler = next((path for name in ('clang++', 'g++', 'c++') if (path := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='vulkan-static-geometry-', dir=ROOT / '.tmp') as folder:
        cpp, binary = Path(folder) / 'static_geometry.cpp', Path(folder) / 'static_geometry.exe'
        cpp.write_text(SUPPORT + production.replace('\r\n', '\n') + FRONT_END + front_end_rule + CASES, encoding='utf-8')
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wno-unused-function', str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
