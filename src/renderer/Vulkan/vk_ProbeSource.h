// Copyright (C) 2026 DarkMatter Productions
#ifndef __VK_PROBE_SOURCE_H__
#define __VK_PROBE_SOURCE_H__

#include "../ModernSpecularProbeAtlas.h"
#include "../PBREnvironment.h"
#include "VulkanDevice.h"

struct vkProbeSourceStamp_t {
	std::uint64_t storageGeneration;
	std::uint64_t uploadGeneration;
	unsigned int handle;
	unsigned int imageGeneration;
	int faceSize;
};

// Immutable, fully uploaded authored cubemaps only. Reads mip zero in native
// +X,-X,+Y,-Y,+Z,-Z order, decodes ordinary sRGB radiance once, preserves
// finite RGBA16F radiance, and clamps negative HDR to zero like the GL atlas.
// Output and stamp are cleared on every failure. A cache miss synchronously
// submits/waits the upload queue; this never submits or resets a scene frame.
modernSpecularProbeAtlasReject_t VK_ProbeSource_Read( const idImage *image,
	openq4PBR::Cube &cube, vkProbeSourceStamp_t &stamp );
bool VK_ProbeSource_Matches( const idImage *image, const vkProbeSourceStamp_t &stamp );

// The same read split across frames (REN-06): BeginRead validates the source
// and submits the copy without waiting; CollectRead returns
// REJECT_PREFILTER_PENDING until the upload fence has signalled (or waits when
// asked), then copies the encoded texels out and releases the buffer. Decode
// turns those bytes into radiance and is safe on any thread. Every failure and
// CancelRead release the readback buffer; nothing is published partially.
struct vkProbeSourceRead_t {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = NULL;
	void *mapped = NULL;
	std::uint64_t batch = 0;
	vkProbeSourceStamp_t stamp = {};
	bool hdr = false;
	bool active = false;
};
modernSpecularProbeAtlasReject_t VK_ProbeSource_BeginRead( const idImage *image, vkProbeSourceRead_t &read );
modernSpecularProbeAtlasReject_t VK_ProbeSource_CollectRead( const idImage *image, vkProbeSourceRead_t &read,
	std::vector<std::uint8_t> &encoded, bool wait );
void VK_ProbeSource_CancelRead( vkProbeSourceRead_t &read );
bool VK_ProbeSource_Decode( const std::vector<std::uint8_t> &encoded, bool hdr, int faceSize, openq4PBR::Cube &cube );
bool VK_ProbeSource_RunSelfTest( void );

#endif
