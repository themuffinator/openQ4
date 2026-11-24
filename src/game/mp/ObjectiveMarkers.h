// Copyright (C) 2026 DarkMatter Productions. Quake 4 SDK EULA.
#pragma once

class rvObjectiveMarkers {
public:
	rvObjectiveMarkers() : nextScan(0) {}
	void Clear();
	void Draw(idPlayer* viewer, idUserInterface* hud, const renderView_t* view);
private:
	void Discover();
	idList<idEntityPtr<idEntity>> objectives;
	int nextScan;
};
