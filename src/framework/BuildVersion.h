#ifndef __BUILD_VERSION_H__
#define __BUILD_VERSION_H__

// BUILD_NUMBER moves with every commit; include this header only where the
// build number is used, never from a widely shared header.
#include "openq4_build_id_generated.h"

// Preserve the legacy BUILD_NUMBER symbol while sourcing it from the shared
// generated version metadata.
const int BUILD_NUMBER = OPENQ4_VERSION_RESOURCE_BUILD;

#endif
