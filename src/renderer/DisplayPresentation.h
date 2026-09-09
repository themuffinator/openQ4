// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <stdint.h>

// Renderer-local POD ABI. The engine must pair this with its module-instance
// epoch: unloading a renderer destroys its local counters. Presentation means
// that the API accepted a swap/present, not physical scanout or visual quality.
enum renderDisplayOutcome_t : uint32_t {
	RDP_UNAVAILABLE = 0, RDP_PENDING, RDP_PRESENTED, RDP_INIT_FAILED,
	RDP_SCREEN_FAILED, RDP_CONTEXT_FAILED, RDP_WAIT_FAILED, RDP_ACQUIRE_FAILED,
	RDP_RECORD_FAILED, RDP_SUBMIT_FAILED, RDP_PRESENT_FAILED, RDP_RECREATE_FAILED
};
struct renderDisplayPresentation_t {
	uint64_t generation;
	uint64_t submittedSequence;
	uint64_t presentedSequence;
	uint64_t failureSequence;
	uint32_t available;
	uint32_t outcome;
	int32_t nativeError; // Vulkan VkResult; zero when the API supplies only bool.
	uint32_t parametersValid; // RDP_PARAMETER_* bits below.
	int32_t samples; // Zero disables multisampling; otherwise actual sample count.
	int32_t swapInterval; // Effective interval, not an unapplied CVar request.
	int32_t presentMode; // VkPresentModeKHR, or -1 for GL.
	uint32_t reserved;
};
enum renderDisplayParameter_t : uint32_t {
	RDP_PARAMETER_SAMPLES = 1, RDP_PARAMETER_SWAP_INTERVAL = 2, RDP_PARAMETER_PRESENT_MODE = 4
};

// Coherent snapshot even when the backend runs separately from the caller.
void R_GetDisplayPresentation(renderDisplayPresentation_t* output);

// Private renderer writers. Counters never reset during a loaded module's
// lifetime. BeginDevice starts a new context/swapchain/display-parameter epoch;
// Ready requires successful initialization, but proves no frame was presented.
void R_DisplayPresentationBeginDevice();
void R_DisplayPresentationReady();
void R_DisplayPresentationShutdown();
void R_DisplayPresentationSubmitted();
void R_DisplayPresentationPresented();
void R_DisplayPresentationFailed(renderDisplayOutcome_t outcome, int32_t nativeError = 0);
void R_DisplayPresentationParameters(int32_t samples, int32_t swapInterval, int32_t presentMode, uint32_t valid);

// Ensures every early init/reset return reports failure. More specific errors
// recorded inside the scope are retained. Success never increments frame counts.
class renderDisplayChangeScope_t {
public:
	explicit renderDisplayChangeScope_t(renderDisplayOutcome_t failure);
	~renderDisplayChangeScope_t();
	void Succeeded();
	renderDisplayChangeScope_t(const renderDisplayChangeScope_t&) = delete;
	renderDisplayChangeScope_t& operator=(const renderDisplayChangeScope_t&) = delete;
private:
	renderDisplayOutcome_t failure;
	uint64_t failures;
	bool succeeded = false;
};
