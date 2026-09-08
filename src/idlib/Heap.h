
#ifndef __HEAP_H__
#define __HEAP_H__

#include <stddef.h>
#include <stdint.h>

// All legacy heap block sizes and their serialized/accounting boundaries are
// signed 32-bit. Public size_t callers must validate against this limit before
// narrowing, including game sources consumed against the engine idlib.
static const size_t ID_HEAP_MAX_SIZE = 0x7fffffffu;

/*
===============================================================================

	Memory Management

	This is a replacement for the compiler heap code (i.e. "C" malloc() and
	free() calls). On average 2.5-3.0 times faster than MSVC malloc()/free().
	Worst case performance is 1.65 times faster and best case > 70 times.
 
===============================================================================
*/

// RAVEN BEGIN
// jsinger: attempt to eliminate cross-DLL allocation issues
#ifdef RV_UNIFIED_ALLOCATOR
class Memory
{
public:
	static void *Allocate(size_t size)
	{
		if(mAllocator)
		{
			return mAllocator(size);
		}
		else
		{
#ifndef _LOAD_DLL
			// allocations from the exe are safe no matter what, but allocations from a DLL through
			// this path will be unsafe, so we warn about them.  Adding a breakpoint here will allow
			// allow locating of the specific offending allocation
			Error("Unprotected allocations are not allowed.  Make sure you've initialized Memory before allocating dynamic memory");
#endif
			return malloc(size);
		}
	}

	static void Free(void *ptr)
	{
		if(mDeallocator)
		{
			mDeallocator(ptr);
		}
		else
		{
#ifndef _LOAD_DLL
			// allocations from the exe are safe no matter what, but allocations from a DLL through
			// this path will be unsafe, so we warn about them.  Adding a breakpoint here will allow
			// allow locating of the specific offending allocation
			Error("Unprotected allocations are not allowed.  Make sure you've initialized Memory before allocating dynamic memory");
#endif
			return free(ptr);
		}
	}

	static size_t MSize(void *ptr)
	{
		if(mMSize)
		{
			return mMSize(ptr);
		}
		else
		{
#ifndef _LOAD_DLL
			// allocations from the exe are safe no matter what, but allocations from a DLL through
			// this path will be unsafe, so we warn about them.  Adding a breakpoint here will allow
			// allow locating of the specific offending allocation
			Error("Unprotected allocations are not allowed.  Make sure you've initialized Memory before allocating dynamic memory");
#endif
			return _msize(ptr);
		}
	}

	static void InitAllocator(void *(*allocator)(size_t size), void (*deallocator)(void *), size_t (*msize)(void *ptr))
	{
		// check for errors prior to initialization
		if(!sOK)
		{
			Error("Unprotected allocation in DLL detected prior to initialization of memory system");
		}

		mAllocator = allocator;
		mDeallocator = deallocator;
		mMSize = msize;
	}

	static void DeinitAllocator()
	{
		mAllocator = NULL;
		mDeallocator = NULL;
		mMSize = NULL;
	}

	static void Error(const char *errStr);

private:
	static void *(*mAllocator)(size_t size);
	static void (*mDeallocator)(void *ptr);
	static size_t (*mMSize)(void *ptr);
	static bool sOK;
};
#endif
// RAVEN END

typedef struct {
	int		num;
	int		minSize;
	int		maxSize;
	// A single allocation is bounded by Mem_ValidateAllocSize, but the running
	// total is not: a loaded Quake 4 map sits within a few hundred megabytes of
	// INT_MAX, so the accumulator has to be wider than the sizes it sums.
	int64_t	totalSize;
} memoryStats_t;

// RAVEN BEGIN
// amccarthy:  tags for memory allocation tracking.  When updating this list please update the
// list of discriptions in Heap.cpp as well.
typedef enum {
	MA_NONE = 0,	
	
	MA_OPNEW,
	MA_DEFAULT,
	MA_LEXER,
	MA_PARSER,
	MA_AAS,
	MA_CLASS,
	MA_SCRIPT,
	MA_CM,
	MA_CVAR,
	MA_DECL,
	MA_FILESYS,
	MA_IMAGES,
	MA_MATERIAL,
	MA_MODEL,
	MA_FONT,
	MA_RENDER,
	MA_VERTEX,
	MA_SOUND,
	MA_WINDOW,
	MA_EVENT,
	MA_MATH,
	MA_ANIM,
	MA_DYNAMICBLOCK,
	MA_STRING,
	MA_GUI,
	MA_EFFECT,
	MA_ENTITY,
	MA_PHYSICS,
	MA_AI,
	MA_NETWORK,

	MA_DO_NOT_USE,		// neither of the two remaining enumerated values should be used (no use of MA_DO_NOT_USE prevents the second dword in a memory block from getting the value 0xFFFFFFFF)
	MA_MAX				// <- this enumerated value is a count and cannot exceed 32 (5 bits are used to encode tag within memory block with rvHeap.cpp)
} Mem_Alloc_Types_t;

#if defined(_DEBUG) || defined(_RV_MEM_SYS_SUPPORT)
const char *GetMemAllocStats(int tag, int &num, int &size, int &peak);
#endif
// RAVEN END

/*
================================================
Cross-module allocation totals.

idlib is compiled once per binary: the engine, the renderer module and the game
module each link their own archive, deliberately, so that the type layouts
behind __DOOM_DLL__ and GAME_DLL cannot collide. Every copy therefore keeps its
own allocation counters, and Mem_GetStats can only ever answer for the binary it
was called from -- which on Android leaves the renderer's image allocations out
of the engine's total entirely.

Each copy exports MEM_MODULE_STATS_ENTRY_POINT, and whoever loads a module looks
it up and registers it here. Mem_GetProcessStats then sums the local counters
with every registered module's. The lookup is by name through the normal dynamic
symbol path, so a module built before this existed simply does not resolve and
is skipped rather than breaking the load.
================================================
*/
#define MEM_MODULE_STATS_ENTRY_POINT	"openQ4_Mem_GetModuleStats"

// 'used' is load-bearing, not decoration: nothing inside the binary calls this
// -- it is only ever reached by name from the loader -- so without it both
// -dead_strip and --gc-sections drop the symbol and every module silently
// reports nothing.
#if defined( _WIN32 )
	#define MEM_MODULE_STATS_EXPORT		__declspec( dllexport )
#else
	#define MEM_MODULE_STATS_EXPORT		__attribute__( ( visibility( "default" ), used ) )
#endif

typedef void ( *memModuleStats_t )( memoryStats_t *stats );

// Exported from every idlib copy. Reports that binary's counters only.
extern "C" MEM_MODULE_STATS_EXPORT void openQ4_Mem_GetModuleStats( memoryStats_t *stats );

void		Mem_Init( void );
void		Mem_Shutdown( void );
void		Mem_EnableLeakTest( const char *name );
void		Mem_ClearFrameStats( void );
void		Mem_GetFrameStats( memoryStats_t &allocs, memoryStats_t &frees );
void		Mem_GetStats( memoryStats_t &stats );
// Registers a just-loaded module's exported counters. Ignores NULL and
// duplicates, so a vid_restart that reloads the same module cannot double-count.
void		Mem_RegisterModuleStats( memModuleStats_t provider );
void		Mem_UnregisterModuleStats( memModuleStats_t provider );
// Local counters plus every registered module's.
void		Mem_GetProcessStats( memoryStats_t &stats );
void		Mem_Dump_f( const class idCmdArgs &args );
void		Mem_DumpCompressed_f( const class idCmdArgs &args );
void		Mem_AllocDefragBlock( void );

// RAVEN BEGIN
// amccarthy command for printing tagged mem_alloc info
void		Mem_ShowMemAlloc_f( const idCmdArgs &args );

// jnewquist: Add Mem_Size to query memory allocation size
int			Mem_Size( void *ptr );

// jnewquist: memory tag stack for new/delete
#if (defined(_DEBUG) || defined(_RV_MEM_SYS_SUPPORT)) && !defined(ENABLE_INTEL_SMP)
class MemScopedTag {
	byte mTag;
	MemScopedTag *mPrev;
	static MemScopedTag *mTop;
public:
	MemScopedTag( byte tag ) {
		mTag = tag;
		mPrev = mTop;
		mTop = this;
	}
	~MemScopedTag() {
		assert( mTop != NULL );
		mTop = mTop->mPrev;
	}
	void SetTag( byte tag ) {
		mTag = tag;
	}
	static byte GetTopTag( void ) {
		if ( mTop ) {
			return mTop->mTag;
		} else {
			return MA_OPNEW;
		}
	}
};
#define MemScopedTag_GetTopTag() MemScopedTag::GetTopTag()
#define MEM_SCOPED_TAG(var, tag) MemScopedTag var(tag)
#define MEM_SCOPED_TAG_SET(var, tag) var.SetTag(tag)
#else
#define MemScopedTag_GetTopTag() MA_OPNEW
#define MEM_SCOPED_TAG(var, tag)
#define MEM_SCOPED_TAG_SET(var, tag)
#endif

// RAVEN END

#ifndef ID_DEBUG_MEMORY

// RAVEN BEGIN
// amccarthy: added tags from memory allocation tracking.
void *		Mem_Alloc( const size_t size, byte tag = MA_DEFAULT );
void *		Mem_ClearedAlloc( const size_t size, byte tag = MA_DEFAULT );
void		Mem_Free( void *ptr );
char *		Mem_CopyString( const char *in );
void *		Mem_Alloc16( const size_t size, byte tag=MA_DEFAULT );
void		Mem_Free16( void *ptr );

// jscott: standardised stack allocation
inline void *Mem_StackAlloc( const int size ) { return( _alloca( size ) ); }
inline void *Mem_StackAlloc16( const int size ) { 
	byte *addr = ( byte * )_alloca( size + 15 );
	addr = ( byte * )( ( uintptr_t )( addr + 15 ) & ~(uintptr_t)15 );
	return( ( void * )addr ); 
}

// dluetscher: moved the inline new/delete operators to sys_local.cpp and Game_local.cpp so that
//			   Tools.dll will link.
#if defined(_XBOX) || defined(ID_REDIRECT_NEWDELETE) || defined(_RV_MEM_SYS_SUPPORT)

void *operator new( size_t s );
void operator delete( void *p );
void *operator new[]( size_t s );
void operator delete[]( void *p );

#endif
// RAVEN END

#else /* ID_DEBUG_MEMORY */

// RAVEN BEGIN
// amccarthy: added tags from memory allocation tracking.
void *		Mem_Alloc( const size_t size, const char *fileName, const int lineNumber, byte tag = MA_DEFAULT );
void *		Mem_ClearedAlloc( const size_t size, const char *fileName, const int lineNumber, byte tag = MA_DEFAULT );
void		Mem_Free( void *ptr, const char *fileName, const int lineNumber );
char *		Mem_CopyString( const char *in, const char *fileName, const int lineNumber );
void *		Mem_Alloc16( const size_t size, const char *fileName, const int lineNumber, byte tag = MA_DEFAULT);
void		Mem_Free16( void *ptr, const char *fileName, const int lineNumber );


// jscott: standardised stack allocation
inline void *Mem_StackAlloc( const int size ) { return( _alloca( size ) ); }
inline void *Mem_StackAlloc16( const int size ) { 
	byte *addr = ( byte * )_alloca( size + 15 );
	addr = ( byte * )( ( uintptr_t )( addr + 15 ) & ~(uintptr_t)15 );
	return( ( void * )addr ); 
}

// dluetscher: moved the inline new/delete operators to sys_local.cpp and Game_local.cpp so that
//			   the Tools.dll will link.
#if defined(_XBOX) || defined(ID_REDIRECT_NEWDELETE) || defined(_RV_MEM_SYS_SUPPORT)

void *operator new( size_t s, int t1, int t2, char *fileName, int lineNumber );
void operator delete( void *p, int t1, int t2, char *fileName, int lineNumber );
void *operator new[]( size_t s, int t1, int t2, char *fileName, int lineNumber );
void operator delete[]( void *p, int t1, int t2, char *fileName, int lineNumber );

void *operator new( size_t s );
void operator delete( void *p );
void *operator new[]( size_t s );
void operator delete[]( void *p );
// RAVEN END

#define ID_DEBUG_NEW						new( 0, 0, __FILE__, __LINE__ )
#undef new
#define new									ID_DEBUG_NEW

#endif

#define		Mem_Alloc( size, tag )				Mem_Alloc( size, __FILE__, __LINE__ )
#define		Mem_ClearedAlloc( size, tag )		Mem_ClearedAlloc( size, __FILE__, __LINE__ )
#define		Mem_Free( ptr )					Mem_Free( ptr, __FILE__, __LINE__ )
#define		Mem_CopyString( s )				Mem_CopyString( s, __FILE__, __LINE__ )
#define		Mem_Alloc16( size, tag )				Mem_Alloc16( size, __FILE__, __LINE__ )
#define		Mem_Free16( ptr )				Mem_Free16( ptr, __FILE__, __LINE__ )
// RAVEN END
#endif /* ID_DEBUG_MEMORY */


/*
================================================
idTempArray is an array that is automatically free'd when it goes out of scope.
There is no "cast" operator because these are very unsafe.

The template parameter MUST BE POD!

Compile time asserting POD-ness of the template parameter is complicated due
to our vector classes that need a default constructor but are otherwise
considered POD.
================================================
*/
template < class T >
class idTempArray {
public:
	idTempArray(idTempArray<T>& other);
	idTempArray(unsigned int num);

	~idTempArray();

	T& operator [](unsigned int i) { assert(i < num); return buffer[i]; }
	const T& operator [](unsigned int i) const { assert(i < num); return buffer[i]; }

	T* Ptr() { return buffer; }
	const T* Ptr() const { return buffer; }

	size_t Size() const { return num * sizeof(T); }
	unsigned int Num() const { return num; }

	void Zero() { memset(Ptr(), 0, Size()); }

private:
	T* buffer;		// Ensure this buffer comes first, so this == &this->buffer
	unsigned int	num;
};

/*
========================
idTempArray::idTempArray
========================
*/
template < class T >
ID_INLINE idTempArray<T>::idTempArray(idTempArray<T>& other) {
	this->num = other.num;
	this->buffer = other.buffer;
	other.num = 0;
	other.buffer = NULL;
}

/*
========================
idTempArray::idTempArray
========================
*/
template < class T >
ID_INLINE idTempArray<T>::idTempArray(unsigned int num) {
	this->num = num;
	buffer = (T*)Mem_Alloc(num * sizeof(T));
}

/*
========================
idTempArray::~idTempArray
========================
*/
template < class T >
ID_INLINE idTempArray<T>::~idTempArray() {
	Mem_Free(buffer);
}

/*
================================================
idTempArray16 is idTempArray with the 16 byte alignment that _alloca16
guaranteed.

It exists so that a scratch buffer whose size comes from asset data -- a vertex
count out of a model file, say -- can be moved off the stack without silently
dropping the alignment the code around it was written against. _alloca16 cannot
fail: it moves the stack pointer and returns, so a large enough count simply
walks off the end of the thread's stack and the first write dies on the guard
page. A model big enough to do that is a normal model on a thread with a small
stack, which is every thread on Android.

The template parameter MUST BE POD, as with idTempArray.
================================================
*/
template < class T >
class idTempArray16 {
public:
	idTempArray16( unsigned int num ) {
		this->num = num;
		buffer = ( T * )Mem_Alloc16( num * sizeof( T ) );
	}
	~idTempArray16() {
		Mem_Free16( buffer );
	}

	T &			operator []( unsigned int i ) { assert( i < num ); return buffer[i]; }
	const T &	operator []( unsigned int i ) const { assert( i < num ); return buffer[i]; }

	T *			Ptr() { return buffer; }
	const T *	Ptr() const { return buffer; }

	size_t		Size() const { return num * sizeof( T ); }
	unsigned int Num() const { return num; }

	void		Zero() { memset( Ptr(), 0, Size() ); }

private:
	T *				buffer;
	unsigned int	num;

	idTempArray16( const idTempArray16 & );
	idTempArray16 & operator=( const idTempArray16 & );
};

/*
================================================
A 16 byte aligned scratch buffer that stays on the stack while that is cheap and
moves to the heap once it would not fit.

Some of these sites run per frame, so allocating unconditionally on the heap
would trade a crash for a malloc in the frame loop; and some are sized from
asset data, so staying on the stack risks walking off the end of it. The size
decides. 256KB is 4096 idDrawVerts, comfortably above any normal mesh and well
below the ~1MB that overflows a thread stack on Android.

Declares 'name' as a 'type *'. As with _alloca16, it is valid to the end of the
enclosing scope; unlike _alloca16, an oversized count is not fatal.
================================================
*/
#define OPENQ4_STACK_SCRATCH_LIMIT		( 256 * 1024 )

#define OPENQ4_ALLOC16_SCRATCH( type, name, count )										\
	const unsigned int name##_scratchCount = ( unsigned int )( count );					\
	idTempArray16< type > name##_scratchHeap(											\
		( ( size_t )name##_scratchCount * sizeof( type ) >= OPENQ4_STACK_SCRATCH_LIMIT )	\
			? name##_scratchCount : 0 );												\
	type *name = ( name##_scratchHeap.Num() > 0 )										\
		? name##_scratchHeap.Ptr()														\
		: ( type * )_alloca16( ( size_t )name##_scratchCount * sizeof( type ) )


/*
===============================================================================

	Block based allocator for fixed size objects.

	All objects of the 'type' are properly constructed.
	However, the constructor is not called for re-used objects.

===============================================================================
*/

// RAVEN BEGIN
// jnewquist: Mark memory tags for idBlockAlloc
template<class type, int blockSize, byte memoryTag>
class idBlockAlloc {
public:
							idBlockAlloc( void );
							~idBlockAlloc( void );

	void					Shutdown( void );

	type *					Alloc( void );
	void					Free( type *element );

	int						GetTotalCount( void ) const { return total; }
	int						GetAllocCount( void ) const { return active; }
	int						GetFreeCount( void ) const { return total - active; }

// RAVEN BEGIN
// jscott: get the amount of memory used
	size_t					Allocated( void ) const { return( total * sizeof( type ) ); }
// RAVEN END

private:
	typedef struct element_s {
		struct element_s *	next;
		type				t;
	} element_t;
	typedef struct block_s {
		element_t			elements[blockSize];
		struct block_s *	next;
	} block_t;

	block_t *				blocks;
	element_t *				free;
	int						total;
	int						active;
};

template<class type, int blockSize, byte memoryTag>
idBlockAlloc<type,blockSize,memoryTag>::idBlockAlloc( void ) {
	blocks = NULL;
	free = NULL;
	total = active = 0;
}

template<class type, int blockSize, byte memoryTag>
idBlockAlloc<type,blockSize,memoryTag>::~idBlockAlloc( void ) {
	Shutdown();
}

template<class type, int blockSize, byte memoryTag>
type *idBlockAlloc<type,blockSize,memoryTag>::Alloc( void ) {
	if ( !free ) {
		MEM_SCOPED_TAG(tag, memoryTag);
		block_t *block = new block_t;
		block->next = blocks;
		blocks = block;
		for ( int i = 0; i < blockSize; i++ ) {
			block->elements[i].next = free;
			free = &block->elements[i];
		}
		total += blockSize;
	}
	active++;
	element_t *element = free;
	free = free->next;
	element->next = NULL;
	return &element->t;
}

template<class type, int blockSize, byte memoryTag>
void idBlockAlloc<type,blockSize,memoryTag>::Free( type *t ) {
	element_t *element = (element_t *)( ( (byte *) t ) - offsetof( element_t, t ) );
	element->next = free;
	free = element;
	active--;
}

template<class type, int blockSize, byte memoryTag>
void idBlockAlloc<type,blockSize,memoryTag>::Shutdown( void ) {
	while( blocks ) {
		block_t *block = blocks;
		blocks = blocks->next;
		delete block;
	}
	blocks = NULL;
	free = NULL;
	total = active = 0;
}
// RAVEN END

/*
==============================================================================

	Dynamic allocator, simple wrapper for normal allocations which can
	be interchanged with idDynamicBlockAlloc.

	No constructor is called for the 'type'.
	Allocated blocks are always 16 byte aligned.

==============================================================================
*/

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
class idDynamicAlloc {
public:
									idDynamicAlloc( void );
									~idDynamicAlloc( void );

	void							Init( void );
	void							Shutdown( void );
	void							SetFixedBlocks( int numBlocks ) {}
	void							SetLockMemory( bool lock ) {}
	void							FreeEmptyBaseBlocks( void ) {}

	type *							Alloc( const int num );
	type *							Resize( type *ptr, const int num );
	void							Free( type *ptr );
	const char *					CheckMemory( const type *ptr ) const;

	int								GetNumBaseBlocks( void ) const { return 0; }
	int								GetBaseBlockMemory( void ) const { return 0; }
	int								GetNumUsedBlocks( void ) const { return numUsedBlocks; }
	int								GetUsedBlockMemory( void ) const { return static_cast<int>( Min( usedBlockMemory, static_cast<size_t>( idMath::INT_MAX ) ) ); }
	int								GetNumFreeBlocks( void ) const { return 0; }
	int								GetFreeBlockMemory( void ) const { return 0; }
	int								GetNumEmptyBaseBlocks( void ) const { return 0; }
	size_t							GetBaseBlockMemorySize( void ) const { return 0; }
	size_t							GetUsedBlockMemorySize( void ) const { return usedBlockMemory; }
	size_t							GetFreeBlockMemorySize( void ) const { return 0; }

private:
	int								numUsedBlocks;			// number of used blocks
	size_t							usedBlockMemory;		// total memory in used blocks

	int								numAllocs;
	int								numResizes;
	int								numFrees;

	void							Clear( void );
};

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
idDynamicAlloc<type, baseBlockSize, minBlockSize, memoryTag>::idDynamicAlloc( void ) {
	Clear();
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
idDynamicAlloc<type, baseBlockSize, minBlockSize, memoryTag>::~idDynamicAlloc( void ) {
	Shutdown();
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
void idDynamicAlloc<type, baseBlockSize, minBlockSize, memoryTag>::Init( void ) {
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
void idDynamicAlloc<type, baseBlockSize, minBlockSize, memoryTag>::Shutdown( void ) {
	Clear();
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
type *idDynamicAlloc<type, baseBlockSize, minBlockSize, memoryTag>::Alloc( const int num ) {
	numAllocs++;
	if ( num <= 0 ) {
		return NULL;
	}
	const size_t elementCount = static_cast<size_t>( num );
	if ( elementCount > static_cast<size_t>( idMath::INT_MAX ) / sizeof( type ) ) {
		return NULL;
	}
	const int byteCount = static_cast<int>( elementCount * sizeof( type ) );
	type *result = (type *) ( (byte *) Mem_Alloc16( static_cast<size_t>( byteCount ), memoryTag ) );
	if ( result == NULL ) {
		return NULL;
	}
	numUsedBlocks++;
	usedBlockMemory += static_cast<size_t>( byteCount );
// RAVEN BEGIN
// jscott: to make it build
// mwhitlock: to make it build on Xenon
	return result;
// RAVEN BEGIN
}

#include "math/Simd.h"

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
type *idDynamicAlloc<type, baseBlockSize, minBlockSize, memoryTag>::Resize( type *ptr, const int num ) {

	numResizes++;

// RAVEN BEGIN
// jnewquist: provide a real implementation of resize
	if ( num <= 0 ) {
		Free( ptr );
		return NULL;
	}

	type *newptr = Alloc( num );
	if ( newptr == NULL ) {
		return NULL;
	}

	if ( ptr != NULL ) {
		const int oldSize = Mem_Size(ptr);
		const int newSize = static_cast<int>( num * sizeof( type ) );
		SIMDProcessor->Memcpy( newptr, ptr, (newSize<oldSize)?newSize:oldSize );
		Free(ptr);
	}

	return newptr;
// RAVEN END
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
void idDynamicAlloc<type, baseBlockSize, minBlockSize, memoryTag>::Free( type *ptr ) {
	numFrees++;
	if ( ptr == NULL ) {
		return;
	}
	Mem_Free16( ptr );
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
const char *idDynamicAlloc<type, baseBlockSize, minBlockSize, memoryTag>::CheckMemory( const type *ptr ) const {
	return NULL;
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
void idDynamicAlloc<type, baseBlockSize, minBlockSize, memoryTag>::Clear( void ) {
	numUsedBlocks = 0;
	usedBlockMemory = 0;
	numAllocs = 0;
	numResizes = 0;
	numFrees = 0;
}


/*
==============================================================================

	Fast dynamic block allocator.

	No constructor is called for the 'type'.
	Allocated blocks are always 16 byte aligned.

==============================================================================
*/

#include "containers/BTree.h"

// RAVEN BEGIN
// jnewquist: Fast sanity checking of idDynamicBlockAlloc
//#ifdef _DEBUG
//#define DYNAMIC_BLOCK_ALLOC_CHECK
#define DYNAMIC_BLOCK_ALLOC_FASTCHECK
#define DYNAMIC_BLOCK_ALLOC_CHECK_IS_FATAL
//#endif
// RAVEN END

template<class type>
class alignas(16) idDynamicBlock {
public:
	type *							GetMemory( void ) const { return (type *)( ( (byte *) this ) + sizeof( idDynamicBlock<type> ) ); }
	int								GetSize( void ) const { return abs( size ); }
	void							SetSize( int s, bool isBaseBlock ) { size = isBaseBlock ? -s : s; }
	bool							IsBaseBlock( void ) const { return ( size < 0 ); }

// RAVEN BEGIN
// jnewquist: Fast sanity checking of idDynamicBlockAlloc
#if defined(DYNAMIC_BLOCK_ALLOC_CHECK) || defined(DYNAMIC_BLOCK_ALLOC_FASTCHECK)
// RAVEN END
	int								identifier[3];
	void *							allocator;
#endif

	int								size;					// size in bytes of the block
	idDynamicBlock<type> *			prev;					// previous memory block
	idDynamicBlock<type> *			next;					// next memory block
	idBTreeNode<idDynamicBlock<type>,int> *node;			// node in the B-Tree with free blocks
#if defined( _WIN64 ) || defined( __LP64__ )
	byte						alignmentPadding[8];		// explicit tail padding required by alignas(16) on 64-bit targets
#endif
};

static_assert( alignof( idDynamicBlock<byte> ) == 16, "idDynamicBlock alignment is part of the allocator contract" );
#if defined( _WIN64 ) || defined( __LP64__ )
static_assert( sizeof( idDynamicBlock<byte> ) == 64, "64-bit idDynamicBlock layout changed" );
#else
static_assert( sizeof( idDynamicBlock<byte> ) == 32, "32-bit idDynamicBlock layout changed" );
#endif

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
class idDynamicBlockAlloc {
public:
									idDynamicBlockAlloc( void );
									~idDynamicBlockAlloc( void );

	void							Init( void );
	void							Shutdown( void );
	void							SetFixedBlocks( int numBlocks );
	void							SetLockMemory( bool lock );
	void							FreeEmptyBaseBlocks( void );

	type *							Alloc( const int num );
	type *							Resize( type *ptr, const int num );
	void							Free( type *ptr );
	const char *					CheckMemory( const type *ptr ) const;

	int								GetNumBaseBlocks( void ) const { return numBaseBlocks; }
	int								GetBaseBlockMemory( void ) const { return static_cast<int>( Min( baseBlockMemory, static_cast<size_t>( idMath::INT_MAX ) ) ); }
	int								GetNumUsedBlocks( void ) const { return numUsedBlocks; }
	int								GetUsedBlockMemory( void ) const { return static_cast<int>( Min( usedBlockMemory, static_cast<size_t>( idMath::INT_MAX ) ) ); }
	int								GetNumFreeBlocks( void ) const { return numFreeBlocks; }
	int								GetFreeBlockMemory( void ) const { return static_cast<int>( Min( freeBlockMemory, static_cast<size_t>( idMath::INT_MAX ) ) ); }
	int								GetNumEmptyBaseBlocks( void ) const;
	size_t							GetBaseBlockMemorySize( void ) const { return baseBlockMemory; }
	size_t							GetUsedBlockMemorySize( void ) const { return usedBlockMemory; }
	size_t							GetFreeBlockMemorySize( void ) const { return freeBlockMemory; }

private:
	idDynamicBlock<type> *			firstBlock;				// first block in list in order of increasing address
	idDynamicBlock<type> *			lastBlock;				// last block in list in order of increasing address
	idBTree<idDynamicBlock<type>,int,4>freeTree;			// B-Tree with free memory blocks
	bool							allowAllocs;			// allow base block allocations
	bool							lockMemory;				// lock memory so it cannot get swapped out

// RAVEN BEGIN
// jnewquist: Fast sanity checking of idDynamicBlockAlloc
#if defined(DYNAMIC_BLOCK_ALLOC_CHECK) || defined(DYNAMIC_BLOCK_ALLOC_FASTCHECK)
// RAVEN END
	int								blockId[3];
#endif

	int								numBaseBlocks;			// number of base blocks
	size_t							baseBlockMemory;		// total memory in base blocks
	int								numUsedBlocks;			// number of used blocks
	size_t							usedBlockMemory;		// total memory in used blocks
	int								numFreeBlocks;			// number of free blocks
	size_t							freeBlockMemory;		// total memory in free blocks

	int								numAllocs;
	int								numResizes;
	int								numFrees;

	void							Clear( void );
	static bool						GetAlignedBlockSize( const int num, int &alignedBytes );
	idDynamicBlock<type> *			AllocInternal( const int num );
	idDynamicBlock<type> *			ResizeInternal( idDynamicBlock<type> *block, const int num );
	void							FreeInternal( idDynamicBlock<type> *block );
	void							LinkFreeInternal( idDynamicBlock<type> *block );
	void							UnlinkFreeInternal( idDynamicBlock<type> *block );
	void							CheckMemory( void ) const;
// RAVEN BEGIN
// jnewquist: Fast sanity checking of idDynamicBlockAlloc
	const char *					CheckMemory( const idDynamicBlock<type> *block ) const;
// RAVEN END
};

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::idDynamicBlockAlloc( void ) {
	Clear();
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::~idDynamicBlockAlloc( void ) {
	Shutdown();
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
void idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::Init( void ) {
// RAVEN BEGIN
// jnewquist: Tag scope and callees to track allocations using "new".
	MEM_SCOPED_TAG(tag,memoryTag);
// RAVEN END
	freeTree.Init();
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
void idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::Shutdown( void ) {
	idDynamicBlock<type> *block;

	for ( block = firstBlock; block != NULL; block = block->next ) {
		if ( block->node == NULL ) {
			FreeInternal( block );
		}
	}

	for ( block = firstBlock; block != NULL; block = firstBlock ) {
		firstBlock = block->next;
		assert( block->IsBaseBlock() );
		if ( lockMemory ) {
			idLib::sys->UnlockMemory( block, block->GetSize() + (int)sizeof( idDynamicBlock<type> ) );
		}
		Mem_Free16( block );
	}

	freeTree.Shutdown();

	Clear();
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
void idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::SetFixedBlocks( int numBlocks ) {
	int i;
	idDynamicBlock<type> *block;

	for ( i = numBaseBlocks; i < numBlocks; i++ ) {
//RAVEN BEGIN
//amccarthy: Added allocation tag
		block = ( idDynamicBlock<type> * ) Mem_Alloc16( baseBlockSize, memoryTag );
//RAVEN END
		if ( block == NULL ) {
			break;
		}
		if ( lockMemory ) {
			idLib::sys->LockMemory( block, baseBlockSize );
		}
// RAVEN BEGIN
// jnewquist: Fast sanity checking of idDynamicBlockAlloc
#if defined(DYNAMIC_BLOCK_ALLOC_CHECK) || defined(DYNAMIC_BLOCK_ALLOC_FASTCHECK)
// RAVEN END
		memcpy( block->identifier, blockId, sizeof( block->identifier ) );
		block->allocator = (void*)this;
#endif
		block->SetSize( baseBlockSize - (int)sizeof( idDynamicBlock<type> ), true );
		block->next = NULL;
		block->prev = lastBlock;
		if ( lastBlock ) {
			lastBlock->next = block;
		} else {
			firstBlock = block;
		}
		lastBlock = block;
		block->node = NULL;

		FreeInternal( block );

		numBaseBlocks++;
		baseBlockMemory += baseBlockSize;
	}

	allowAllocs = false;
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
void idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::SetLockMemory( bool lock ) {
	lockMemory = lock;
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
void idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::FreeEmptyBaseBlocks( void ) {
	idDynamicBlock<type> *block, *next;

	for ( block = firstBlock; block != NULL; block = next ) {
		next = block->next;

		if ( block->IsBaseBlock() && block->node != NULL && ( next == NULL || next->IsBaseBlock() ) ) {
			UnlinkFreeInternal( block );
			if ( block->prev ) {
				block->prev->next = block->next;
			} else {
				firstBlock = block->next;
			}
			if ( block->next ) {
				block->next->prev = block->prev;
			} else {
				lastBlock = block->prev;
			}
			if ( lockMemory ) {
				idLib::sys->UnlockMemory( block, block->GetSize() + (int)sizeof( idDynamicBlock<type> ) );
			}
			numBaseBlocks--;
			baseBlockMemory -= block->GetSize() + (int)sizeof( idDynamicBlock<type> );
			Mem_Free16( block );
		}
	}

#ifdef DYNAMIC_BLOCK_ALLOC_CHECK
	CheckMemory();
#endif
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
int idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::GetNumEmptyBaseBlocks( void ) const {
	int numEmptyBaseBlocks;
	idDynamicBlock<type> *block;

	numEmptyBaseBlocks = 0;
	for ( block = firstBlock; block != NULL; block = block->next ) {
		if ( block->IsBaseBlock() && block->node != NULL && ( block->next == NULL || block->next->IsBaseBlock() ) ) {
			numEmptyBaseBlocks++;
		}
	}
	return numEmptyBaseBlocks;
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
type *idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::Alloc( const int num ) {
	idDynamicBlock<type> *block;

	numAllocs++;

	if ( num <= 0 ) {
		return NULL;
	}

	block = AllocInternal( num );
	if ( block == NULL ) {
		return NULL;
	}
	block = ResizeInternal( block, num );
	if ( block == NULL ) {
		return NULL;
	}

#ifdef DYNAMIC_BLOCK_ALLOC_CHECK
	CheckMemory();
#endif

	numUsedBlocks++;
	usedBlockMemory += block->GetSize();

	return block->GetMemory();
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
type *idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::Resize( type *ptr, const int num ) {

	numResizes++;

	if ( ptr == NULL ) {
		return Alloc( num );
	}

	if ( num <= 0 ) {
		Free( ptr );
		return NULL;
	}

	idDynamicBlock<type> *block = ( idDynamicBlock<type> * ) ( ( (byte *) ptr ) - (int)sizeof( idDynamicBlock<type> ) );

	const size_t oldBlockSize = static_cast<size_t>( block->GetSize() );
	usedBlockMemory -= oldBlockSize;

	block = ResizeInternal( block, num );
	if ( block == NULL ) {
		usedBlockMemory += oldBlockSize;
		return NULL;
	}

#ifdef DYNAMIC_BLOCK_ALLOC_CHECK
	CheckMemory();
#endif

	usedBlockMemory += block->GetSize();

	return block->GetMemory();
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
void idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::Free( type *ptr ) {

	numFrees++;

	if ( ptr == NULL ) {
		return;
	}

	idDynamicBlock<type> *block = ( idDynamicBlock<type> * ) ( ( (byte *) ptr ) - (int)sizeof( idDynamicBlock<type> ) );

	numUsedBlocks--;
	usedBlockMemory -= block->GetSize();

	FreeInternal( block );

#ifdef DYNAMIC_BLOCK_ALLOC_CHECK
	CheckMemory();
#endif
}

// RAVEN BEGIN
// jnewquist: Fast sanity checking of idDynamicBlockAlloc
template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
const char *idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::CheckMemory( const idDynamicBlock<type> *block ) const {
	if ( block->node != NULL ) {
		return "memory has been freed";
	}

#if defined(DYNAMIC_BLOCK_ALLOC_CHECK) || defined(DYNAMIC_BLOCK_ALLOC_FASTCHECK)
// RAVEN END
	if ( block->identifier[0] != 0x11111111 || block->identifier[1] != 0x22222222 || block->identifier[2] != 0x33333333 ) {
		return "memory has invalid identifier";
	}
// RAVEN BEGIN
// jsinger: attempt to eliminate cross-DLL allocation issues
#ifndef RV_UNIFIED_ALLOCATOR
	if ( block->allocator != (void*)this ) {
		return "memory was allocated with different allocator";
	}
#endif // RV_UNIFIED_ALLOCATOR
// RAVEN END
#endif

	/* base blocks can be larger than baseBlockSize which can cause this code to fail
	idDynamicBlock<type> *base;
	for ( base = firstBlock; base != NULL; base = base->next ) {
		if ( base->IsBaseBlock() ) {
			const uintptr_t blockAddress = reinterpret_cast<uintptr_t>( block );
			const uintptr_t baseAddress = reinterpret_cast<uintptr_t>( base );
			if ( blockAddress >= baseAddress && blockAddress - baseAddress < static_cast<uintptr_t>( baseBlockSize ) ) {
				break;
			}
		}
	}
	if ( base == NULL ) {
		return "no base block found for memory";
	}
	*/

	return NULL;
}

// RAVEN BEGIN
// jnewquist: Fast sanity checking of idDynamicBlockAlloc
template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
const char *idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::CheckMemory( const type *ptr ) const {
	idDynamicBlock<type> *block;

	if ( ptr == NULL ) {
		return NULL;
	}

	block = ( idDynamicBlock<type> * ) ( ( (byte *) ptr ) - (int)sizeof( idDynamicBlock<type> ) );
	return CheckMemory( block );
}
// RAVEN END

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
void idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::Clear( void ) {
	firstBlock = lastBlock = NULL;
	allowAllocs = true;
	lockMemory = false;
	numBaseBlocks = 0;
	baseBlockMemory = 0;
	numUsedBlocks = 0;
	usedBlockMemory = 0;
	numFreeBlocks = 0;
	freeBlockMemory = 0;
	numAllocs = 0;
	numResizes = 0;
	numFrees = 0;

// RAVEN BEGIN
// jnewquist: Fast sanity checking of idDynamicBlockAlloc
#if defined(DYNAMIC_BLOCK_ALLOC_CHECK) || defined(DYNAMIC_BLOCK_ALLOC_FASTCHECK)
// RAVEN END
	blockId[0] = 0x11111111;
	blockId[1] = 0x22222222;
	blockId[2] = 0x33333333;
#endif
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
bool idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::GetAlignedBlockSize( const int num, int &alignedBytes ) {
	const size_t blockHeaderSize = sizeof( idDynamicBlock<type> );
	const size_t maxPayload = static_cast<size_t>( idMath::INT_MAX ) - blockHeaderSize - 15;

	if ( num <= 0 ) {
		return false;
	}

	const size_t elementCount = static_cast<size_t>( num );
	if ( elementCount > maxPayload / sizeof( type ) ) {
		return false;
	}

	const size_t byteCount = elementCount * sizeof( type );
	const size_t alignedByteCount = ( byteCount + 15 ) & ~static_cast<size_t>( 15 );
	if ( alignedByteCount > static_cast<size_t>( idMath::INT_MAX ) - blockHeaderSize ) {
		return false;
	}

	alignedBytes = static_cast<int>( alignedByteCount );
	return true;
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
idDynamicBlock<type> *idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::AllocInternal( const int num ) {
	idDynamicBlock<type> *block;
	int alignedBytes;
	if ( !GetAlignedBlockSize( num, alignedBytes ) ) {
		return NULL;
	}

	block = freeTree.FindSmallestLargerEqual( alignedBytes );
	if ( block != NULL ) {
		UnlinkFreeInternal( block );
	} else if ( allowAllocs ) {
		int allocSize = Max( baseBlockSize, alignedBytes + (int)sizeof( idDynamicBlock<type> ) );
//RAVEN BEGIN
//amccarthy: Added allocation tag
		block = ( idDynamicBlock<type> * ) Mem_Alloc16( allocSize, memoryTag );
//RAVEN END
		if ( block == NULL ) {
			return NULL;
		}
		if ( lockMemory ) {
			idLib::sys->LockMemory( block, allocSize );
		}
// RAVEN BEGIN
// jnewquist: Fast sanity checking of idDynamicBlockAlloc
#if defined(DYNAMIC_BLOCK_ALLOC_CHECK) || defined(DYNAMIC_BLOCK_ALLOC_FASTCHECK)
// RAVEN END
		memcpy( block->identifier, blockId, sizeof( block->identifier ) );
		block->allocator = (void*)this;
#endif
		block->SetSize( allocSize - (int)sizeof( idDynamicBlock<type> ), true );
		block->next = NULL;
		block->prev = lastBlock;
		if ( lastBlock ) {
			lastBlock->next = block;
		} else {
			firstBlock = block;
		}
		lastBlock = block;
		block->node = NULL;

		numBaseBlocks++;
		baseBlockMemory += allocSize;
	}

	return block;
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
idDynamicBlock<type> *idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::ResizeInternal( idDynamicBlock<type> *block, const int num ) {
	int alignedBytes;
	if ( !GetAlignedBlockSize( num, alignedBytes ) ) {
		return NULL;
	}

// RAVEN BEGIN
// jnewquist: Fast sanity checking of idDynamicBlockAlloc
#if defined(DYNAMIC_BLOCK_ALLOC_CHECK) || defined(DYNAMIC_BLOCK_ALLOC_FASTCHECK)
#if defined(DYNAMIC_BLOCK_ALLOC_CHECK_IS_FATAL)
	const char *chkstr = CheckMemory( block );
	if ( chkstr ) {
		throw idException( chkstr );
	}
#endif
// jsinger: attempt to eliminate cross-DLL allocation issues
#ifdef RV_UNIFIED_ALLOCATOR
	assert( block->identifier[0] == 0x11111111 && block->identifier[1] == 0x22222222 && block->identifier[2] == 0x33333333); // && block->allocator == (void*)this );
#else
	assert( block->identifier[0] == 0x11111111 && block->identifier[1] == 0x22222222 && block->identifier[2] == 0x33333333 && block->allocator == (void*)this );
#endif
// RAVEN END
#endif

	// if the new size is larger
	if ( alignedBytes > block->GetSize() ) {

		idDynamicBlock<type> *nextBlock = block->next;

		// try to annexate the next block if it's free
		const int64_t combinedSize = nextBlock
			? static_cast<int64_t>( block->GetSize() ) + static_cast<int64_t>( sizeof( idDynamicBlock<type> ) ) + nextBlock->GetSize()
			: 0;
		if ( nextBlock && !nextBlock->IsBaseBlock() && nextBlock->node != NULL &&
				combinedSize >= alignedBytes && combinedSize <= idMath::INT_MAX ) {

			UnlinkFreeInternal( nextBlock );
			block->SetSize( static_cast<int>( combinedSize ), block->IsBaseBlock() );
			block->next = nextBlock->next;
			if ( nextBlock->next ) {
				nextBlock->next->prev = block;
			} else {
				lastBlock = block;
			}
		} else {
			// allocate a new block and copy
			idDynamicBlock<type> *oldBlock = block;
			block = AllocInternal( num );
			if ( block == NULL ) {
				return NULL;
			}
			memcpy( block->GetMemory(), oldBlock->GetMemory(), oldBlock->GetSize() );
			FreeInternal( oldBlock );
		}
	}

	// if the unused space at the end of this block is large enough to hold a block with at least one element
	if ( block->GetSize() - alignedBytes - (int)sizeof( idDynamicBlock<type> ) < Max( minBlockSize, (int)sizeof( type ) ) ) {
		return block;
	}

	idDynamicBlock<type> *newBlock;

	newBlock = ( idDynamicBlock<type> * ) ( ( (byte *) block ) + (int)sizeof( idDynamicBlock<type> ) + alignedBytes );
// RAVEN BEGIN
// jnewquist: Fast sanity checking of idDynamicBlockAlloc
#if defined(DYNAMIC_BLOCK_ALLOC_CHECK) || defined(DYNAMIC_BLOCK_ALLOC_FASTCHECK)
// RAVEN END
	memcpy( newBlock->identifier, blockId, sizeof( newBlock->identifier ) );
	newBlock->allocator = (void*)this;
#endif
	newBlock->SetSize( block->GetSize() - alignedBytes - (int)sizeof( idDynamicBlock<type> ), false );
	newBlock->next = block->next;
	newBlock->prev = block;
	if ( newBlock->next ) {
		newBlock->next->prev = newBlock;
	} else {
		lastBlock = newBlock;
	}
	newBlock->node = NULL;
	block->next = newBlock;
	block->SetSize( alignedBytes, block->IsBaseBlock() );

	FreeInternal( newBlock );

	return block;
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
void idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::FreeInternal( idDynamicBlock<type> *block ) {

	assert( block->node == NULL );

// RAVEN BEGIN
// jnewquist: Fast sanity checking of idDynamicBlockAlloc
#if defined(DYNAMIC_BLOCK_ALLOC_CHECK) || defined(DYNAMIC_BLOCK_ALLOC_FASTCHECK)
#if defined(DYNAMIC_BLOCK_ALLOC_CHECK_IS_FATAL)
	const char *chkstr = CheckMemory( block );
	if ( chkstr ) {
		throw idException( chkstr );
	}
#endif
// jsinger: attempt to eliminate cross-DLL allocation issues
#ifdef RV_UNIFIED_ALLOCATOR
	assert( block->identifier[0] == 0x11111111 && block->identifier[1] == 0x22222222 && block->identifier[2] == 0x33333333 );//&& block->allocator == (void*)this );
#else
	assert( block->identifier[0] == 0x11111111 && block->identifier[1] == 0x22222222 && block->identifier[2] == 0x33333333 && block->allocator == (void*)this );
#endif // RV_UNIFIED_ALLOCATOR
// RAVEN END
#endif

	// try to merge with a next free block
	idDynamicBlock<type> *nextBlock = block->next;
	if ( nextBlock && !nextBlock->IsBaseBlock() && nextBlock->node != NULL ) {
		UnlinkFreeInternal( nextBlock );
		block->SetSize( block->GetSize() + (int)sizeof( idDynamicBlock<type> ) + nextBlock->GetSize(), block->IsBaseBlock() );
		block->next = nextBlock->next;
		if ( nextBlock->next ) {
			nextBlock->next->prev = block;
		} else {
			lastBlock = block;
		}
	}

	// try to merge with a previous free block
	idDynamicBlock<type> *prevBlock = block->prev;
	if ( prevBlock && !block->IsBaseBlock() && prevBlock->node != NULL ) {
		UnlinkFreeInternal( prevBlock );
		prevBlock->SetSize( prevBlock->GetSize() + (int)sizeof( idDynamicBlock<type> ) + block->GetSize(), prevBlock->IsBaseBlock() );
		prevBlock->next = block->next;
		if ( block->next ) {
			block->next->prev = prevBlock;
		} else {
			lastBlock = prevBlock;
		}
		LinkFreeInternal( prevBlock );
	} else {
		LinkFreeInternal( block );
	}
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
ID_INLINE void idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::LinkFreeInternal( idDynamicBlock<type> *block ) {
	block->node = freeTree.Add( block, block->GetSize() );
	numFreeBlocks++;
	freeBlockMemory += block->GetSize();
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
ID_INLINE void idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::UnlinkFreeInternal( idDynamicBlock<type> *block ) {
	freeTree.Remove( block->node );
	block->node = NULL;
	numFreeBlocks--;
	freeBlockMemory -= block->GetSize();
}

template<class type, int baseBlockSize, int minBlockSize, byte memoryTag>
void idDynamicBlockAlloc<type, baseBlockSize, minBlockSize, memoryTag>::CheckMemory( void ) const {
	idDynamicBlock<type> *block;

	for ( block = firstBlock; block != NULL; block = block->next ) {

// RAVEN BEGIN
// jnewquist: Fast sanity checking of idDynamicBlockAlloc
#if defined(DYNAMIC_BLOCK_ALLOC_CHECK) || defined(DYNAMIC_BLOCK_ALLOC_FASTCHECK)
#if defined(DYNAMIC_BLOCK_ALLOC_CHECK_IS_FATAL)
		const char *chkstr = CheckMemory( block );
		if ( chkstr ) {
			throw idException( chkstr );
		}
#endif
// jsinger: attempt to eliminate cross-DLL allocation issues
#ifdef RV_UNIFIED_ALLOCATOR
		assert( block->identifier[0] == 0x11111111 && block->identifier[1] == 0x22222222 && block->identifier[2] == 0x33333333); // && block->allocator == (void*)this );
#else
		assert( block->identifier[0] == 0x11111111 && block->identifier[1] == 0x22222222 && block->identifier[2] == 0x33333333 && block->allocator == (void*)this );
#endif
// RAVEN END
#endif

		// make sure the block is properly linked
		if ( block->prev == NULL ) {
			assert( firstBlock == block );
		} else {
			assert( block->prev->next == block );
		}
		if ( block->next == NULL ) {
			assert( lastBlock == block );
		} else {
			assert( block->next->prev == block );
		}
	}
}

#endif /* !__HEAP_H__ */
