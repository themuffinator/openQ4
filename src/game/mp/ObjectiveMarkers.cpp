// Copyright (C) 2026 DarkMatter Productions. Quake 4 SDK EULA.
#include "../../idlib/precompiled.h"
#pragma hdrstop
#include "../Game_local.h"
#include "GameState.h"
#include "GameTypes.h"
#include "CTF.h"
#include "ObjectiveMarkerLayout.h"
#include "../gamesys/HudPrimitives.h"

idCVar hud_objectiveMarkers("hud_objectiveMarkers", "1", CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL,
	"show multiplayer objective locations, status and distance");

namespace {
constexpr int MaxVisible = 8;
constexpr int MaxObjectives = 64;
enum Kind { Base, Zone, Assault };
struct Marker {
	idEntity* entity = nullptr;
	Kind kind = Base;
	int team = TEAM_NONE;
	idVec3 origin;
	float distance = 0;
	objectiveMarker::Placement position;
};

bool IsZone(idEntity* entity) {
	const int zone = entity->spawnArgs.GetInt("controlZone");
	return zone >= 1 && zone <= 3 && entity->IsType(idTrigger_Multi::GetClassType());
}

// The existing DeadZone protocol publishes each authored entityAffect's shader
// state. Never assign the aggregate HUD's last owner to every zone on a custom
// multi-zone map. Without a per-zone visual target, only a single-zone map can
// safely use that aggregate state. Unknown remains a location-only marker.
int ZoneOwner(idEntity* zone, int zoneCount) {
	rvGameState* state = gameLocal.mpGame.GetGameState();
	if (!state || !state->IsType(riDZGameState::GetClassType())) return -1;
	auto* dz = static_cast<riDZGameState*>(state);
	idEntity* target = gameLocal.FindEntity(zone->spawnArgs.GetString("entityAffect"));
	if (target) {
		const float value = target->GetRenderEntity()->shaderParms[7];
		for (int owner=0;owner<=3;++owner) if (value == float(owner)) return owner;
	}
	if (zoneCount != 1) return -1;
	const dzState_t marine = dz->GetDZState(TEAM_MARINE), strogg = dz->GetDZState(TEAM_STROGG);
	if (marine == DZ_DEADLOCK || strogg == DZ_DEADLOCK) return 3;
	if (marine == DZ_TAKEN) return TEAM_MARINE;
	if (strogg == DZ_TAKEN) return TEAM_STROGG;
	return 2;
}

// Native triangles and strokes are fed through the retained HUD's vector
// backend, with the same geometry as the stock fallback. xScale compensates
// the engine's stretched 640x480 submission space, keeping 45-degree cuts.
class Painter {
public:
	Painter(float scaleX) : xScale(scaleX), white(declManager->FindMaterial("_white")) {}
	void Polygon(const idVec2 (&points)[4], const idVec4& color) {
		idVec2 quad[4];
		for (int i=0;i<4;++i) quad[i].Set(points[i].x*xScale,points[i].y);
		packet.Quad(quad,idVec4(0,0,1,1),color,white);
	}
	void Line(float x,float y,float endX,float endY,float width,const idVec4& color) {
		idVec2 d(endX-x,endY-y);
		if (d.Normalize() < .001f) return;
		const idVec2 n(-d.y*width*.5f,d.x*width*.5f);
		const idVec2 a(x,y),b(endX,endY);
		// Clockwise on the y-down canvas, like Rect: the stock GUI renderer
		// culls the opposite winding even though the retained backend does not.
		const idVec2 quad[4]={a-n,b-n,b+n,a+n}; Polygon(quad,color);
	}
	void Outline(const idVec2 (&points)[4],float width,const idVec4& color) {
		for (int i=0;i<4;++i) Line(points[i].x,points[i].y,points[(i+1)%4].x,points[(i+1)%4].y,width,color);
	}
	void Flag(float x,float y,float scale,flagState_t state,const idVec4& color) {
		const float top=state==FS_DROPPED?-3.f:-6.f;
		Line(x-4*scale,y+6*scale,x-4*scale,y-6*scale,1.2f*scale,color);
		const idVec2 flag[4]={idVec2(x-3*scale,y+top*scale),idVec2(x+6*scale,y+top*scale),
			idVec2(x+3*scale,y+(top+4)*scale),idVec2(x-3*scale,y+(top+4)*scale)};
		if (state==FS_AT_BASE) Polygon(flag,color);
		else Outline(flag,.9f*scale,color);
		Line(x-6*scale,y+6*scale,x+6*scale,y+6*scale,1.2f*scale,color);
		// The solid pennant is home; a hollow pennant has left the base. Its
		// small upward/downward cue distinguishes carried from dropped.
		if (state!=FS_AT_BASE) {
			const float tip=state==FS_DROPPED?5.f:1.f;
			Line(x+scale,y+3*scale,x+3*scale,y+tip*scale,scale,color);
			Line(x+3*scale,y+tip*scale,x+5*scale,y+3*scale,scale,color);
		}
	}
	void Zone(float x,float y,float scale,int owner,bool locked,const idVec4& color) {
		// Open corners stay legible over a filled or split control core.
		for (int sx=-1;sx<=1;sx+=2) for (int sy=-1;sy<=1;sy+=2) {
			Line(x+sx*5*scale,y+sy*2.5f*scale,x+sx*5*scale,y+sy*5*scale,1.2f*scale,color);
			Line(x+sx*5*scale,y+sy*5*scale,x+sx*2.5f*scale,y+sy*5*scale,1.2f*scale,color);
		}
		if (owner==3) {
			const idVec2 left[4]={idVec2(x-4*scale,y),idVec2(x-scale,y-3*scale),idVec2(x-scale,y+3*scale),idVec2(x-4*scale,y)};
			const idVec2 right[4]={idVec2(x+4*scale,y),idVec2(x+scale,y+3*scale),idVec2(x+scale,y-3*scale),idVec2(x+4*scale,y)};
			Polygon(left,idVec4(.67f,.89f,.33f,color.w)); Polygon(right,idVec4(1,.56f,0,color.w));
		} else if (owner==TEAM_MARINE || owner==TEAM_STROGG) {
			const idVec2 core[4]={idVec2(x,y-3*scale),idVec2(x+3*scale,y),idVec2(x,y+3*scale),idVec2(x-3*scale,y)};
			Polygon(core,color);
		}
		if (locked) {
			// A restrained padlock replaces the artifact-required sentence.
			const float lx=x+8*scale,ly=y+8*scale;
			const idVec2 back[4]={idVec2(lx-4*scale,ly-4*scale),idVec2(lx+4*scale,ly-4*scale),
				idVec2(lx+4*scale,ly+4*scale),idVec2(lx-4*scale,ly+4*scale)};
			Polygon(back,idVec4(0,0,0,.72f*color.w));
			const idVec4 lock(.87f,.92f,.77f,color.w);
			const idVec2 body[4]={idVec2(lx-2.5f*scale,ly),idVec2(lx+2.5f*scale,ly),
				idVec2(lx+2.5f*scale,ly+3*scale),idVec2(lx-2.5f*scale,ly+3*scale)};
			Outline(body,.9f*scale,lock);
			Line(lx-1.5f*scale,ly,lx-1.5f*scale,ly-2.5f*scale,.9f*scale,lock);
			Line(lx-1.5f*scale,ly-2.5f*scale,lx+1.5f*scale,ly-2.5f*scale,.9f*scale,lock);
			Line(lx+1.5f*scale,ly-2.5f*scale,lx+1.5f*scale,ly,.9f*scale,lock);
		}
	}
	void Text(const char* text,float x,float y,float scale,float width,const idVec4& color) {
		idList<uiHudPrimitive_t> glyphs;
		// Shape the localized distance on one line and centre its actual ink.
		uiManager->BuildHudText(text,.18f,512,15,glyphs);
		if (!glyphs.Num()) return;
		float left=512,right=0;
		for (int i=0;i<glyphs.Num();++i) for (int n=0;n<4;++n) {
			left=Min(left,glyphs[i].points[n].x); right=Max(right,glyphs[i].points[n].x);
		}
		const float fit=Min(1.f,width/Max(1.f,right-left+4));
		const float center=(left+right)*.5f;
		for (int shadow=1;shadow>=0;--shadow) for (int i=0;i<glyphs.Num();++i) {
			const auto& item=glyphs[i]; idVec2 quad[4];
			for (int n=0;n<4;++n) quad[n].Set((x+(item.points[n].x-center+shadow*.6f)*scale*fit)*xScale,
				y+(item.points[n].y+shadow*.6f)*scale*fit);
			packet.Quad(quad,item.uv,shadow?idVec4(0,0,0,color.w*.85f):color,item.material,true);
		}
	}
	void Draw() { packet.Draw("native/objective-markers"); }
private:
	float xScale;
	const idMaterial* white;
	rvHudPrimitives packet;
};
}

void rvObjectiveMarkers::Clear() {
	objectives.Clear(); nextScan=0;
}

void rvObjectiveMarkers::Discover() {
	objectives.Clear();
	const bool flags=gameLocal.IsFlagGameType();
	for (idEntity* e=gameLocal.spawnedEntities.Next();e;e=e->spawnNode.Next()) {
		const bool base=flags && e->IsType(rvItemCTFFlag::GetClassType()) && !e->spawnArgs.GetBool("dropped");
		const bool zone=gameLocal.gameType==GAME_DEADZONE && IsZone(e);
		const bool assault=flags && e->IsType(rvCTF_AssaultPoint::GetClassType());
		if ((base || zone || assault) && objectives.Num()<MaxObjectives) {
			idEntityPtr<idEntity> ref; ref=e; objectives.Append(ref);
		}
	}
	nextScan=gameLocal.time+1000;
}

void rvObjectiveMarkers::Draw(idPlayer* viewer,idUserInterface* hud,const renderView_t* view) {
	idPlayer* local=gameLocal.GetLocalPlayer();
	rvGameState* state=gameLocal.mpGame.GetGameState();
	if (!gameLocal.isMultiplayer || !hud_objectiveMarkers.GetBool() || !hud || !viewer || !view || !state ||
		(!gameLocal.IsFlagGameType() && gameLocal.gameType!=GAME_DEADZONE) ||
		!g_showHud.GetBool() || viewer->health<=0 || viewer->spectating || viewer->disableHud || viewer->GuiActive() ||
		viewer->IsZoomed() || viewer->GetInfluenceLevel()!=INFLUENCE_NONE || gameLocal.GetCamera() ||
		viewer->scoreBoardOpen || viewer->forceScoreBoard || viewer->objectiveSystemOpen ||
		(local && (local->scoreBoardOpen || local->forceScoreBoard || (local->spectating && local->spectator!=viewer->entityNumber))) ||
		gameLocal.mpGame.GetCurrentMenu()!=0 || gameLocal.mpGame.ArenaCampaignPresentationActive()) return;
	const mpGameState_t phase=state->GetMPGameState();
	if (phase!=WARMUP && phase!=COUNTDOWN && phase!=GAMEON && phase!=SUDDENDEATH) return;
	// Match PlayerView's actual stereo path, including lean correction and
	// physical crouch. Inactive VR and flat spectator/camera views still use
	// their normal render view, even when vr_enable remains archived as 1.
	idVec3 projectionOrigin=view->vieworg;
	idMat3 projectionAxis=view->viewaxis;
	vrFrameState_t vrFrame; float trackingYaw;
	const bool stereo=viewer->GetVRView(vrFrame,trackingYaw) && vrFrame.stereo;
	float hudDistance=0,hudWidth=0,hudOffset=0;
	if (stereo) {
		if (!vrFrame.head.valid) return;
		idVec3 eyeOrigin; idMat3 eyeAxis;
		viewer->GetPresentationViewPos(eyeOrigin,eyeAxis);
		eyeOrigin.z+=viewer->VRCrouchLift(vrFrame);
		vrPose_t head=vrFrame.head;
		head.origin+=VR_HeadOffsetCorrection(vrFrame);
		VR_PoseToWorld(head,eyeOrigin,trackingYaw,projectionOrigin,projectionAxis);
		// Keep these dimensions aligned with OpenXRSystem::EndFrame's HUD quad.
		hudDistance=idMath::ClampFloat(.5f,5.f,cvarSystem->GetCVarFloat("vr_hudDistance"));
		hudWidth=idMath::ClampFloat(.3f,4.f,cvarSystem->GetCVarFloat("vr_hudWidth"));
		hudOffset=vrFrame.aimMode==VR_AIM_HAND && !cvarSystem->GetCVarBool("vr_hudOnSight") ?
			cvarSystem->GetCVarFloat("vr_hudHeightOffset") : 0;
	}
	if (!nextScan || gameLocal.time>=nextScan || gameLocal.time<nextScan-1000) Discover();

	Marker markers[MaxObjectives]; int count=0,zoneCount=0;
	for (int i=0;i<objectives.Num();++i) {
		idEntity* e=objectives[i].GetEntity();
		if (!e || e->GetInstance()!=viewer->GetInstance() || !e->GetPhysics()) continue;
		Marker m; m.entity=e; m.origin=e->GetPhysics()->GetOrigin();
		if (IsZone(e)) {
			if (!(e->GetPhysics()->GetContents() & CONTENTS_TRIGGER)) continue;
			m.kind=Zone; m.team=e->spawnArgs.GetInt("controlZone")-1;
			m.origin=e->GetPhysics()->GetAbsBounds().GetCenter(); ++zoneCount;
		} else if (e->IsType(rvCTF_AssaultPoint::GetClassType())) {
			m.kind=Assault;
			auto* point=static_cast<rvCTF_AssaultPoint*>(e);
			if (state->IsType(rvCTFGameState::GetClassType()) && point->GetIndex()>=0 && point->GetIndex()<MAX_AP)
				m.team=static_cast<rvCTFGameState*>(state)->GetAPOwner(point->GetIndex());
		} else {
			m.team=e->spawnArgs.GetInt("team","-1");
			if (m.team<0 || m.team>TEAM_MAX) continue;
		}
		m.distance=(m.origin-projectionOrigin).Length();
		if (!std::isfinite(m.distance)) continue;
		m.origin.z+=m.kind==Zone?0:48;
		markers[count++]=m;
	}
	// Bases first, then nearby control points. Stable ties prevent flickering
	// on symmetrical maps; only bounded objective references are visited/frame.
	std::sort(markers,markers+count,[](const Marker& a,const Marker& b) {
		if ((a.kind==Base)!=(b.kind==Base)) return a.kind==Base;
		return a.distance==b.distance?a.entity->entityNumber<b.entity->entityNumber:a.distance<b.distance;
	});
	const float width=480.f*renderSystem->GetScreenWidth()/Max(1,renderSystem->GetScreenHeight());
	const float scale=idMath::ClampFloat(.75f,1.5f,cvarSystem->GetCVarFloat("ui_hudScale"));
	const float safe=idMath::ClampFloat(0,10,cvarSystem->GetCVarFloat("ui_hudSafeInset"))*.01f;
	const float marginX=Max(38.f*scale,width*safe),marginY=Max(80.f*scale,480.f*safe);
	const float tanX=tanf(DEG2RAD(view->fov_x)*.5f),tanY=tanf(DEG2RAD(view->fov_y)*.5f);
	Painter paint(640.f/width);
	int selected[MaxVisible],drawn=0;
	for (int i=0;i<count && drawn<MaxVisible;++i) {
		Marker& m=markers[i]; const idVec3 delta=m.origin-projectionOrigin;
		const float right=-(delta*projectionAxis[1]),up=delta*projectionAxis[2],forward=delta*projectionAxis[0];
		const bool projected=stereo ? objectiveMarker::ProjectOnHud(right,up,forward,hudDistance,hudWidth,hudOffset,
			width,480,marginX,marginY,m.position) :
			objectiveMarker::Project(right,up,forward,tanX,tanY,width,480,marginX,marginY,m.position);
		if (!projected) continue;
		bool hidden=false;
		for (int j=0;j<drawn;++j) {
			const auto& p=markers[selected[j]].position;
			if (objectiveMarker::Overlaps(m.position.x,m.position.y,18*scale,18*scale,p.x,p.y,18*scale,18*scale)) hidden=true;
		}
		if (!hidden) selected[drawn++]=i;
	}
	// Select every symbol before deciding which distances fit. A later symbol
	// must not cover a distance that an earlier draw has already submitted.
	bool distances[MaxVisible]={};
	for (int i=0;i<drawn;++i) {
		Marker& m=markers[selected[i]]; const auto& p=m.position;
		bool details=p.emphasis>.65f;
		for (int j=0;j<drawn;++j) if (i!=j) {
			const auto& other=markers[selected[j]].position;
			if (objectiveMarker::Overlaps(p.x,p.y+23*scale,30*scale,6*scale,other.x,other.y,18*scale,18*scale) ||
				(distances[j] && objectiveMarker::Overlaps(p.x,p.y+23*scale,30*scale,6*scale,other.x,other.y+23*scale,30*scale,6*scale)))
				details=false;
		}
		distances[i]=details;
		int owner=m.team; bool locked=false;
		flagState_t flag=FS_AT_BASE;
		if (m.kind==Base && state->IsType(rvCTFGameState::GetClassType())) {
			flag=static_cast<rvCTFGameState*>(state)->GetFlagState(m.team);
		} else if (m.kind==Zone) {
			owner=ZoneOwner(m.entity,zoneCount);
			locked=(m.team==viewer->team || m.team>=TEAM_MAX) &&
				m.entity->spawnArgs.GetBool("requiresDeadZonePowerup","1") && !viewer->PowerUpActive(POWERUP_DEADZONE);
		}
		const float alpha=p.emphasis*(p.edge?.78f:1.f)*idMath::ClampFloat(0,1,cvarSystem->GetCVarFloat("ui_hudOpacity"));
		idVec4 color=owner==TEAM_MARINE?idVec4(.67f,.89f,.33f,alpha):
			owner==TEAM_STROGG?idVec4(1,.56f,0,alpha):idVec4(.87f,.92f,.77f,alpha);
		const float x=p.x,y=p.y,r=11*scale;
		const idVec2 diamond[4]={idVec2(x,y-r-2*scale),idVec2(x+r+2*scale,y),idVec2(x,y+r+2*scale),idVec2(x-r-2*scale,y)};
		paint.Polygon(diamond,idVec4(0,0,0,.48f*alpha));
		paint.Outline(diamond,.8f*scale,idVec4(color.x,color.y,color.z,alpha*.65f));
		if (m.kind==Base) paint.Flag(x,y,scale,flag,color);
		else paint.Zone(x,y,scale,owner,locked,color);
		if (p.edge) {
			const idVec2 direction(p.directionX,p.directionY),normal(-p.directionY,p.directionX),center(x,y);
			const idVec2 tip=center+direction*(21*scale),tail=center+direction*(16*scale);
			paint.Line(tail.x+normal.x*3*scale,tail.y+normal.y*3*scale,tip.x,tip.y,1.4f*scale,color);
			paint.Line(tail.x-normal.x*3*scale,tail.y-normal.y*3*scale,tip.x,tip.y,1.4f*scale,color);
		}
		if (details) {
			const int metres=idMath::ClampInt(0,9999,idMath::Ftoi(m.distance*.0254f+.5f));
			const idStr distance=va(common->GetLocalizedString("#str_231325"),metres);
			paint.Text(distance.c_str(),x,y+16*scale,scale,56,idVec4(.9f,.93f,.85f,alpha*.85f));
		}
	}
	paint.Draw();
}
