// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "retained/TextInputBroker.h"

class idUserInterface;

struct uiTextDeliveryResult_t {
	openq4::ui::TextDeliveryOutcome outcome = openq4::ui::TextDeliveryOutcome::Rejected;
	openq4::ui::TextBrokerContext context;
	std::string diagnostic;
};

// Engine-thread boundary only. The caller resolves the CURRENT Session owner
// afresh for each call and supplies checked native window/session lifetimes.
// Pointers are borrowed for this call only; registry membership precedes any
// dereference, and delivery additionally checks the exact allocation token.
// These APIs do not authorize native provenance, start editing, dispatch GUI
// actions, write accepted settings, pump/route native events, control devices,
// or establish replay authority. Existing read-only focus eligibility applies.
// authorizedContext is the exact context synchronized into the broker before
// it issued delivery; nativeWindow/nativeSession are checked afresh at dispatch.
// Complete the broker immediately with the observed result, even on rejection;
// never substitute the old context to hide a failed freshness/receipt check.
openq4::ui::TextBrokerContext UI_QueryTextContext(idUserInterface* current,
	std::uint64_t nativeWindow, std::uint64_t nativeSession);
uiTextDeliveryResult_t UI_DeliverTextInput(idUserInterface* current,
	std::uint64_t nativeWindow, std::uint64_t nativeSession,
	const openq4::ui::TextBrokerContext& authorizedContext,
	const openq4::ui::TextBrokerDelivery& delivery);

// Private checked process-lifetime allocator for retained backend/document
// identities. Zero is permanent exhaustion, never a valid text owner.
std::uint64_t UI_NextTextLifetime();
