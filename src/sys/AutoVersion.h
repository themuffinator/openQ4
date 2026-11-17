#ifndef __AUTO_VERSION_HEADER
#define __AUTO_VERSION_HEADER

// These values move with every commit; include this header only where they
// are used (the .rc files and a few version strings), never from a widely
// shared header.
#include "openq4_build_id_generated.h"

// used by the .rc files
#define VERSION_MAJOR_RELEASE			OPENQ4_VERSION_MAJOR
#define VERSION_MINOR_RELEASE			OPENQ4_VERSION_MINOR
#define VERSION_INTERNAL_BUILD			OPENQ4_VERSION_PATCH
#define VERSION_BUILD_NUMBER			OPENQ4_VERSION_RESOURCE_BUILD

#define VERSION_STRING					OPENQ4_VERSION_RESOURCE_COMMAS_STRING
#define VERSION_STRING_DOTTED			OPENQ4_VERSION_RESOURCE_DOTTED
#define VERSION_PRODUCT_STRING			OPENQ4_VERSION

#endif // __AUTO_VERSION_HEADER
