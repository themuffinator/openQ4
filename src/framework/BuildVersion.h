#ifndef __BUILD_VERSION_H__
#define __BUILD_VERSION_H__

#include "openq4_version_generated.h"

// Preserve the legacy BUILD_NUMBER symbol while sourcing it from the shared
// generated version metadata.
const int BUILD_NUMBER = OPENQ4_VERSION_RESOURCE_BUILD;

#endif
