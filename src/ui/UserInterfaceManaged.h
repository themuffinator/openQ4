// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "UserInterface.h"

// Engine-private ownership and scheduling contract. Game modules continue to
// use idUserInterface; neither legacy windows nor these manager operations
// cross that interface. Managed instances are dynamically allocated and become
// manager-owned immediately, including before a source has been loaded.
class idUserInterfaceManaged : public idUserInterface {
	friend class idUserInterfaceManagerLocal;
public:
	idUserInterfaceManaged();
	virtual ~idUserInterfaceManaged();

	virtual const char *GetSourceFile() const = 0;
	virtual ID_TIME_T GetTimeStamp() const = 0;
	virtual bool IsMenuGui() const = 0;
	virtual bool AlwaysThink() const = 0;
	virtual void RunTimeEvents( int time ) = 0;
	virtual size_t Size() = 0;
	virtual int NumTransitions() = 0;

	void ClearRefs() { refs = 0; }
	void AddRef() { refs++; }
	int GetRefs() const { return refs; }

protected:
	// Publish only after initialization has established valid metadata. Loaded
	// and demo registries are non-owning subsets of the allocation registry.
	void RegisterLoaded();
	void RegisterDemo();
	void RefreshThinking();

private:
	idUserInterfaceManaged( const idUserInterfaceManaged & ) = delete;
	idUserInterfaceManaged &operator=( const idUserInterfaceManaged & ) = delete;
	int refs;
	unsigned long long allocationId;
};
