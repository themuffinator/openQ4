// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "DisplayPresentation.h"
#include <mutex>

namespace {
std::mutex presentationMutex;
renderDisplayPresentation_t presentation{};
// Exhaustion is unreachable during a process lifetime, but must not wrap into
// an old generation or produce a false newly-presented sequence.
bool Advance(uint64_t& value) {
	if (value == UINT64_MAX) { presentation.available = 0; presentation.outcome = RDP_UNAVAILABLE; return false; }
	++value; return true;
}
}
void R_GetDisplayPresentation(renderDisplayPresentation_t* output) {
	if (!output) return;
	const std::lock_guard<std::mutex> lock(presentationMutex);
	*output = presentation;
}
void R_DisplayPresentationBeginDevice() {
	const std::lock_guard<std::mutex> lock(presentationMutex);
	presentation.available = 0; presentation.outcome = RDP_PENDING; presentation.nativeError = 0;
	presentation.parametersValid = 0; presentation.samples = 0; presentation.swapInterval = 0; presentation.presentMode = -1;
	Advance(presentation.generation);
}
void R_DisplayPresentationReady() {
	const std::lock_guard<std::mutex> lock(presentationMutex);
	if (presentation.generation == UINT64_MAX) return;
	presentation.available = 1; presentation.outcome = RDP_PENDING; presentation.nativeError = 0;
}
void R_DisplayPresentationShutdown() {
	const std::lock_guard<std::mutex> lock(presentationMutex);
	presentation.available = 0;
	if (presentation.outcome <= RDP_PRESENTED) presentation.outcome = RDP_UNAVAILABLE;
}
void R_DisplayPresentationSubmitted() {
	const std::lock_guard<std::mutex> lock(presentationMutex);
	if (presentation.available) Advance(presentation.submittedSequence);
}
void R_DisplayPresentationPresented() {
	const std::lock_guard<std::mutex> lock(presentationMutex);
	if (presentation.available && Advance(presentation.presentedSequence)) {
		presentation.outcome = RDP_PRESENTED; presentation.nativeError = 0;
	}
}
void R_DisplayPresentationFailed(renderDisplayOutcome_t outcome, int32_t nativeError) {
	const std::lock_guard<std::mutex> lock(presentationMutex);
	if (Advance(presentation.failureSequence)) {
		presentation.outcome = outcome; presentation.nativeError = nativeError;
	}
}
void R_DisplayPresentationParameters(int32_t samples, int32_t swapInterval, int32_t presentMode, uint32_t valid) {
	const std::lock_guard<std::mutex> lock(presentationMutex);
	presentation.samples = samples; presentation.swapInterval = swapInterval;
	presentation.presentMode = presentMode; presentation.parametersValid = valid;
}
renderDisplayChangeScope_t::renderDisplayChangeScope_t(renderDisplayOutcome_t failure) : failure(failure) {
	R_DisplayPresentationBeginDevice();
	renderDisplayPresentation_t current; R_GetDisplayPresentation(&current); failures = current.failureSequence;
}
renderDisplayChangeScope_t::~renderDisplayChangeScope_t() {
	if (succeeded) return;
	renderDisplayPresentation_t current; R_GetDisplayPresentation(&current);
	if (current.failureSequence == failures) R_DisplayPresentationFailed(failure);
}
void renderDisplayChangeScope_t::Succeeded() {
	succeeded = true; R_DisplayPresentationReady();
}
