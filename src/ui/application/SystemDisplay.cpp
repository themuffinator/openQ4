// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../../idlib/precompiled.h"
#include "SystemDisplay.h"
#include "SystemSettingsHost.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <variant>
#if defined(USE_SDL3) && !defined(ID_DEDICATED)
#include <SDL3/SDL.h>
#endif

namespace openq4::ui {
#if defined(ID_DEDICATED)
namespace { bool Unsupported(std::string& error) { error="Display settings require a client renderer"; return false; } }
bool CaptureDisplayTopology(SystemDisplayTopology&,std::string& error) { return Unsupported(error); }
bool BuildDisplayRequest(const StateValues&,const rendererDisplayState_t&,const SystemDisplayTopology&,SystemDisplayPlan&,std::string& error,bool) { return Unsupported(error); }
bool BuildDisplayRestore(const rendererDisplayState_t&,const SystemDisplayTopology&,SystemDisplayPlan&,std::string& error) { return Unsupported(error); }
bool MatchesDisplay(const SystemDisplayPlan&,const rendererDisplayState_t&,std::string& error) { return Unsupported(error); }
bool CaptureDisplayRecovery(const SystemDisplayPlan&,const SystemDisplayTopology&,StateValues&,std::string& error) { return Unsupported(error); }
bool InspectDisplayRecovery(const StateValues&,SystemDisplayPlan&,SystemDisplayTopology&,std::string& error) { return Unsupported(error); }
bool ValidateDisplayRecoveryPair(const StateValues&,const StateValues&,const StateValues&,std::string& error) { return Unsupported(error); }
bool ValidateDisplayPreserveActualPair(const StateValues&,const StateValues&,const StateValues&,const StateValues&,std::string& error) { return Unsupported(error); }
bool ResolveDisplayRecovery(const StateValues&,const SystemDisplayTopology&,SystemDisplayPlan&,std::string& error) { return Unsupported(error); }
bool BuildSystemDisplayCatalog(const SystemDisplayCatalogInput&,const StateValues&,SystemDisplayCatalog&,std::string& error) { return Unsupported(error); }
bool SystemDisplaySelectionPatch(const SystemDisplayCatalog&,SystemDisplayList,int,StateValues&,std::string& error) { return Unsupported(error); }
bool SameSystemDisplay(const SystemDisplayDescriptor&,const SystemDisplayDescriptor&) { return false; }
#else
namespace {
constexpr int MaxDisplays = 32;
bool Fail(std::string& error, const char* reason) { error = reason; return false; }
bool Dimensions(int w, int h) { return w >= 320 && w <= 16384 && h >= 240 && h <= 16384; }
bool Mode(const SystemDisplayMode& mode) {
	return mode.width > 0 && mode.height > 0 && std::isfinite(mode.refresh) && mode.refresh >= 0 && mode.refresh <= 1000 && double(static_cast<float>(mode.refresh))==mode.refresh;
}
bool SameMode(const SystemDisplayMode& a, const SystemDisplayMode& b) {
	return a.width == b.width && a.height == b.height && a.refresh == b.refresh;
}
bool SameMonitor(const SystemDisplayDescriptor& a, const SystemDisplayDescriptor& b) {
	return a.name == b.name && a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height && SameMode(a.desktop,b.desktop);
}
bool Topology(const SystemDisplayTopology& topology, std::string& error) {
	if (topology.displays.empty() || topology.displays.size() > MaxDisplays) return Fail(error,"Display topology exceeds supported bounds");
	std::set<unsigned> ids;
	for (const auto& display : topology.displays) {
		if (!display.id || !ids.insert(display.id).second || display.name.empty() || display.name.size() > 512 ||
			!ValidStateValue(StateValue(display.name)) || display.width <= 0 || display.height <= 0 ||
			display.width > 16384 || display.height > 16384 || !Mode(display.desktop) || display.modes.size() > 4096)
			return Fail(error,"Display topology contains an invalid descriptor");
		for (const auto& mode : display.modes) if (!Mode(mode)) return Fail(error,"Display topology contains an invalid mode");
	}
	return ids.contains(topology.primary) || Fail(error,"Primary display identity is unavailable");
}
const SystemDisplayDescriptor* Display(const SystemDisplayTopology& topology, unsigned id) {
	for (const auto& display : topology.displays) if (display.id == id) return &display;
	return nullptr;
}
bool Ready(const rendererDisplayState_t& state, std::string& error) {
	const auto& w = state.window;
	if (!state.rendererReady || !state.windowValid || !state.moduleEpoch || !state.presentation.generation ||
		!state.presentation.available || (state.presentation.parametersValid & 3) != 3 ||
		!w.displayId || w.logicalWidth <= 0 || w.logicalHeight <= 0 || w.pixelWidth <= 0 || w.pixelHeight <= 0 ||
		w.minimized || !w.currentModeValid || w.modePixelWidth <= 0 || w.modePixelHeight <= 0 ||
		!std::isfinite(w.refreshRate) || w.refreshRate < 0 || w.refreshRate > 1000 ||
		!std::isfinite(w.displayScale) || w.displayScale <= 0 ||
		!std::isfinite(w.pixelDensityX) || w.pixelDensityX <= 0 || !std::isfinite(w.pixelDensityY) || w.pixelDensityY <= 0)
		return Fail(error,"An actual ready display with known parameters is required");
	return true;
}
bool Samples(int value) { return value == 0 || value == 2 || value == 4 || value == 8 || value == 16; }
// The shared exclusive rule (src/sys/DisplayModeRule.h) over the captured
// list, which keeps SDL's order, so a plan expects the mode the strict window
// service and legacy startup choose. Auto aims at the desktop rate.
bool SelectMode(const SystemDisplayDescriptor& display, int width, int height, int refresh, SystemDisplayMode& output) {
	const int chosen=SelectDisplayMode(int(display.modes.size()),[&](int i,DisplayModeCandidate& mode) {
		const auto& option=display.modes[size_t(i)];
		mode={option.width,option.height,static_cast<float>(option.refresh)}; return true;
	},width,height,refresh,static_cast<float>(display.desktop.refresh));
	if (chosen<0) return false;
	output=display.modes[size_t(chosen)]; return true;
}
bool Bounds(const std::vector<SystemDisplayDescriptor>& displays, int& x, int& y, int& width, int& height) {
	int64_t left=0, top=0, right=0, bottom=0;
	for (size_t i=0; i<displays.size(); ++i) {
		const auto& d=displays[i];
		left=i?(std::min)(left,int64_t(d.x)):d.x; top=i?(std::min)(top,int64_t(d.y)):d.y;
		right=i?(std::max)(right,int64_t(d.x)+d.width):int64_t(d.x)+d.width;
		bottom=i?(std::max)(bottom,int64_t(d.y)+d.height):int64_t(d.y)+d.height;
	}
	if (displays.empty() || right-left <= 0 || right-left > 16384 || bottom-top <= 0 || bottom-top > 16384) return false;
	x=int(left); y=int(top); width=int(right-left); height=int(bottom-top); return true;
}
int Coordinate(int64_t value) { return int(std::clamp(value,int64_t((std::numeric_limits<int>::min)()),int64_t((std::numeric_limits<int>::max)()))); }
void ExpectedMode(SystemDisplayPlan& plan, const SystemDisplayMode& mode) {
	plan.expected.currentModeValid=true; plan.expected.modePixelWidth=mode.width; plan.expected.modePixelHeight=mode.height;
	plan.expected.refreshRate=static_cast<float>(mode.refresh); plan.checkMode=plan.checkRefresh=true;
}
bool Plan(const SystemDisplayPlan& plan, std::string& error) {
	const auto& r=plan.request; const auto& p=r.parms; const auto& w=plan.expected;
	if (!r.displayId || !Dimensions(p.width,p.height) || p.displayHz < 0 || p.displayHz > 1000 ||
		!Samples(p.multiSamples) || r.swapInterval < -1 || r.swapInterval > 1 || p.stereo ||
		(p.hiddenWindow && (p.fullScreen || p.borderless)) || (p.fullScreen && p.borderless) ||
		(r.spanDisplays && !p.borderless && !(p.fullScreen && r.fullscreenDesktop)) ||
		plan.monitors.empty() || plan.monitors.size() > MaxDisplays || plan.monitors.front().id != r.displayId ||
		w.logicalWidth <= 0 || w.logicalHeight <= 0 || w.minimized ||
		(w.hidden != p.hiddenWindow) || w.maximized != r.maximized ||
		(plan.checkPixels && (w.pixelWidth <= 0 || w.pixelHeight <= 0)) ||
		(plan.checkMode && (!w.currentModeValid || w.modePixelWidth <= 0 || w.modePixelHeight <= 0)) ||
		(plan.checkRefresh && (!std::isfinite(w.refreshRate) || w.refreshRate < 0 || w.refreshRate > 1000)) ||
		(plan.checkPosition && !w.positionValid)) return Fail(error,"Display plan is malformed");
	const bool spanDesktop=p.fullScreen && r.fullscreenDesktop && r.spanDisplays;
	if (w.fullscreen != (p.fullScreen && !spanDesktop) ||
		w.fullscreenDesktop != (w.fullscreen && r.fullscreenDesktop) ||
		(!w.fullscreen && w.borderless != (p.borderless || spanDesktop)) || (!r.spanDisplays && w.displayId != r.displayId))
		return Fail(error,"Display plan policy is inconsistent");
	return true;
}
bool Candidate(const StateValues& values, std::string& error) {
	const auto& schema=SystemSettingsHost::Schema();
	if (values.size()!=schema.size()) return Fail(error,"Display request requires the complete settings catalog");
	for (const auto& [key,type]:schema) {
		const auto value=values.find(key);
		if (value==values.end() || value->second.index()!=type || !ValidStateValue(value->second))
			return Fail(error,"Display request catalog type mismatch");
	}
	for (const char* key:{"r_mode","r_screen","r_multiScreen","r_multiSamples","r_displayRefresh","r_swapInterval","r_windowWidth","r_windowHeight","r_customWidth","r_customHeight"}) {
		const double n=std::get<double>(values.at(key));
		if (std::floor(n)!=n || n<(std::numeric_limits<int>::min)() || n>(std::numeric_limits<int>::max)())
			return Fail(error,"Display request contains an invalid integer");
	}
	return true;
}
} // namespace

bool CaptureDisplayTopology(SystemDisplayTopology& output, std::string& error) {
#if defined(USE_SDL3) && !defined(ID_DEDICATED)
	SystemDisplayTopology topology; topology.primary=SDL_GetPrimaryDisplay();
	const char* driver=SDL_GetCurrentVideoDriver(); topology.absolutePlacement=driver && std::string(driver)!="wayland";
	int count=0; SDL_DisplayID* ids=SDL_GetDisplays(&count);
	if (!ids || count<=0 || count>MaxDisplays) { SDL_free(ids); return Fail(error,"Display enumeration is unavailable or exceeds the settings limit"); }
	const auto convert=[](const SDL_DisplayMode* mode,SystemDisplayMode& converted) {
		if (!mode || mode->w<=0 || mode->h<=0 || !std::isfinite(mode->pixel_density) || mode->pixel_density<=0) return false;
		const double w=std::floor(double(mode->w)*mode->pixel_density+.5), h=std::floor(double(mode->h)*mode->pixel_density+.5);
		if (!std::isfinite(w) || !std::isfinite(h) || w<1 || h<1 || w>(std::numeric_limits<int>::max)() || h>(std::numeric_limits<int>::max)()) return false;
		converted={int(w),int(h),mode->refresh_rate}; return Mode(converted);
	};
	bool okay=true;
	for (int i=0; i<count && okay; ++i) {
		SystemDisplayDescriptor display; display.id=ids[i]; const char* name=SDL_GetDisplayName(ids[i]); SDL_Rect bounds{};
		const SDL_DisplayMode* desktopMode=SDL_GetDesktopDisplayMode(ids[i]);
		okay=name && SDL_GetDisplayBounds(ids[i],&bounds) && convert(desktopMode,display.desktop);
		if (!okay) break;
		// An exclusive mode resizes its display's bounds while it is active:
		// Windows, X11 and macOS switch the display, and Wayland reports a
		// focused exclusive window's emulated mode. A descriptor names the
		// monitor, so it keeps the desktop size, which the bounds equal otherwise.
		display.name=name; display.x=bounds.x; display.y=bounds.y; display.width=desktopMode->w; display.height=desktopMode->h;
		int modeCount=0; SDL_DisplayMode** modes=SDL_GetFullscreenDisplayModes(ids[i],&modeCount);
		okay=modes && modeCount>=0 && modeCount<=4096;
		for (int j=0; okay && j<modeCount; ++j) {
			SystemDisplayMode mode;
			okay=modes[j] && (!modes[j]->displayID || modes[j]->displayID==ids[i]) && convert(modes[j],mode);
			if (okay) display.modes.push_back(mode);
		}
		SDL_free(modes); if (okay) topology.displays.push_back(std::move(display));
	}
	SDL_free(ids);
	if (!okay || !Topology(topology,error)) return Fail(error,"Display topology is unavailable or invalid");
	output=std::move(topology); error.clear(); return true;
#else
	(void)output; return Fail(error,"Display topology requires the SDL3 client host");
#endif
}

bool BuildDisplayRequest(const StateValues& values, const rendererDisplayState_t& captured,
	const SystemDisplayTopology& topology, SystemDisplayPlan& output, std::string& error, bool windowMultisampling) {
	if (!Candidate(values,error) || !Topology(topology,error) || !Ready(captured,error)) return false;
	const auto integer=[&](const char* key){return int(std::get<double>(values.at(key)));};
	const int index=integer("r_screen"), modeIndex=integer("r_mode");
	if (index < -1 || index>=int(topology.displays.size()) || integer("r_multiScreen")<0 || integer("r_multiScreen")>1)
		return Fail(error,"Selected display index or span policy is invalid");
	const auto* display=index>=0?&topology.displays[index]:Display(topology,captured.window.displayId);
	if (!display) display=Display(topology,topology.primary);
	if (!display) return Fail(error,"Selected display is unavailable");
	SystemDisplayPlan plan; auto& request=plan.request; auto& p=request.parms; auto& expected=plan.expected;
	request.displayId=display->id; request.displayIndex=int(display-&topology.displays.front());
	p.fullScreen=std::get<bool>(values.at("r_fullscreen")); p.borderless=!p.fullScreen && std::get<bool>(values.at("r_borderless"));
	request.fullscreenDesktop=p.fullScreen && std::get<bool>(values.at("r_fullscreenDesktop"));
	request.spanDisplays=integer("r_multiScreen")!=0 && (p.borderless || request.fullscreenDesktop);
	if (integer("r_multiScreen") && p.fullScreen && !request.fullscreenDesktop) return Fail(error,"Exclusive fullscreen cannot span displays");
	p.hiddenWindow=captured.window.hidden; p.multiSamples=windowMultisampling?integer("r_multiSamples"):0; p.displayHz=integer("r_displayRefresh");
	request.swapInterval=integer("r_swapInterval"); p.width=integer("r_windowWidth"); p.height=integer("r_windowHeight");
	request.maximized=false; expected.hidden=p.hiddenWindow; expected.displayId=display->id;
	plan.monitors.push_back(*display);
	if (request.spanDisplays) for (const auto& other:topology.displays) if (other.id!=display->id) plan.monitors.push_back(other);
	const bool borderless=p.borderless || (request.fullscreenDesktop && request.spanDisplays);
	expected.fullscreen=p.fullScreen && !borderless; expected.fullscreenDesktop=expected.fullscreen && request.fullscreenDesktop; expected.borderless=borderless;
	ExpectedMode(plan,display->desktop);
	if (p.fullScreen && !request.fullscreenDesktop) {
		// Share the canonical legacy-mode lookup with the host catalog.
		if (!SystemSettingsHost::ResolveModeDimensions(modeIndex,integer("r_customWidth"),integer("r_customHeight"),display->desktop.width,display->desktop.height,p.width,p.height))
			return Fail(error,"Exclusive mode index or pixel dimensions are invalid");
		SystemDisplayMode selected;
		if (!SelectMode(*display,p.width,p.height,p.displayHz,selected)) return Fail(error,"No exact exclusive pixel mode and refresh is available");
		expected.pixelWidth=p.width; expected.pixelHeight=p.height; plan.checkPixels=true; ExpectedMode(plan,selected);
		// SDL logical dimensions depend on mode density; the actual strict mode
		// and physical framebuffer are authoritative for exclusive requests.
		expected.logicalWidth=p.width; expected.logicalHeight=p.height;
	} else if (expected.fullscreenDesktop) {
		p.width=display->desktop.width; p.height=display->desktop.height;
		expected.pixelWidth=p.width; expected.pixelHeight=p.height; plan.checkPixels=true;
		expected.logicalWidth=display->width; expected.logicalHeight=display->height;
	} else if (borderless) {
		if (request.spanDisplays && !topology.absolutePlacement) return Fail(error,"Spanning requires absolute window placement");
		if (!Bounds(plan.monitors,expected.windowX,expected.windowY,p.width,p.height)) return Fail(error,"Spanned display bounds exceed supported dimensions");
		expected.logicalWidth=p.width; expected.logicalHeight=p.height; expected.positionValid=topology.absolutePlacement; plan.checkPosition=topology.absolutePlacement;
	} else {
		expected.logicalWidth=p.width; expected.logicalHeight=p.height;
		expected.positionValid=topology.absolutePlacement; plan.checkPosition=topology.absolutePlacement;
		// A bordered window keeps its place on its display. A fullscreen or
		// borderless window sits at the display's origin, which would put the
		// new window's title bar off the screen, so that one is centred.
		const bool placed=captured.window.positionValid && captured.window.displayId==display->id &&
			!captured.window.fullscreen && !captured.window.borderless;
		if (placed) { expected.windowX=captured.window.windowX; expected.windowY=captured.window.windowY; }
		else { expected.windowX=Coordinate(int64_t(display->x)+(int64_t(display->width)-p.width)/2); expected.windowY=Coordinate(int64_t(display->y)+(int64_t(display->height)-p.height)/2); }
	}
	if (plan.checkPosition) { request.restorePlacement=true; request.windowX=expected.windowX; request.windowY=expected.windowY; }
	if (!Plan(plan,error)) return false;
	output=std::move(plan); error.clear(); return true;
}

bool BuildDisplayRestore(const rendererDisplayState_t& captured, const SystemDisplayTopology& topology,
	SystemDisplayPlan& output, std::string& error) {
	if (!Ready(captured,error) || !Topology(topology,error)) return false;
	const auto& w=captured.window; const auto* display=Display(topology,w.displayId);
	if (!display) return Fail(error,"The captured display is no longer available");
	SystemDisplayPlan plan; plan.expected=w; auto& r=plan.request;
	r.displayId=w.displayId; r.displayIndex=int(display-&topology.displays.front());
	r.parms.fullScreen=w.fullscreen; r.fullscreenDesktop=w.fullscreenDesktop; r.parms.borderless=!w.fullscreen && w.borderless;
	r.parms.hiddenWindow=w.hidden; r.parms.width=w.fullscreen?w.pixelWidth:w.logicalWidth; r.parms.height=w.fullscreen?w.pixelHeight:w.logicalHeight;
	r.parms.multiSamples=captured.presentation.samples; r.swapInterval=captured.presentation.swapInterval;
	r.parms.displayHz=w.fullscreen&&!w.fullscreenDesktop?int(std::floor(w.refreshRate+.5f)):0;
	r.maximized=w.maximized; r.restorePlacement=w.positionValid && topology.absolutePlacement; r.windowX=w.windowX; r.windowY=w.windowY;
	plan.checkPixels=plan.checkMode=plan.checkRefresh=true; plan.checkPosition=r.restorePlacement;
	plan.monitors.push_back(*display);
	if (r.parms.borderless) {
		if (!w.positionValid || !topology.absolutePlacement) return Fail(error,"Cannot resolve borderless restore bounds without absolute placement");
		if (w.windowX!=display->x || w.windowY!=display->y || w.logicalWidth!=display->width || w.logicalHeight!=display->height) {
			int x,y,width,height;
			if (!Bounds(topology.displays,x,y,width,height) || w.windowX!=x || w.windowY!=y || w.logicalWidth!=width || w.logicalHeight!=height)
				return Fail(error,"The captured borderless rectangle is not a supported display or span");
			r.spanDisplays=true;
			for (const auto& other:topology.displays) if (other.id!=display->id) plan.monitors.push_back(other);
		}
	}
	if (w.fullscreen && !w.fullscreenDesktop) {
		const auto restores=[&](int refresh) {
			SystemDisplayMode selected;
			return SelectMode(*display,r.parms.width,r.parms.height,refresh,selected) && selected.refresh==double(w.refreshRate);
		};
		// Its whole hertz can name a neighbour (60.00 beside an Auto start's
		// 59.94); Auto then restores the rate Auto chose.
		if (!restores(r.parms.displayHz)) {
			if (!restores(0)) return Fail(error,"The exact captured exclusive refresh cannot be restored");
			r.parms.displayHz=0;
		}
	}
	if (!Plan(plan,error)) return false;
	output=std::move(plan); error.clear(); return true;
}

bool MatchesDisplay(const SystemDisplayPlan& plan, const rendererDisplayState_t& observed, std::string& error) {
	if (!Plan(plan,error) || !Ready(observed,error)) return false;
	const auto& expected=plan.expected; const auto& w=observed.window;
	const SystemDisplayDescriptor* monitor=nullptr;
	for (const auto& item:plan.monitors) if (item.id==w.displayId) monitor=&item;
	if (!monitor || (!plan.request.spanDisplays && w.displayId!=plan.request.displayId) ||
		w.fullscreen!=expected.fullscreen || w.fullscreenDesktop!=expected.fullscreenDesktop ||
		(!w.fullscreen && w.borderless!=expected.borderless) || w.hidden!=expected.hidden || w.maximized!=expected.maximized ||
		(!w.fullscreen && (w.logicalWidth!=expected.logicalWidth || w.logicalHeight!=expected.logicalHeight)) ||
		(plan.checkPixels && (w.pixelWidth!=expected.pixelWidth || w.pixelHeight!=expected.pixelHeight)) ||
		(plan.checkPosition && (!w.positionValid || w.windowX!=expected.windowX || w.windowY!=expected.windowY)) ||
		observed.presentation.samples!=plan.request.parms.multiSamples || observed.presentation.swapInterval!=plan.request.swapInterval)
		return Fail(error,"Actual display policy, dimensions, placement or renderer parameters differ from the request");
	const int modeWidth=plan.request.spanDisplays?monitor->desktop.width:expected.modePixelWidth;
	const int modeHeight=plan.request.spanDisplays?monitor->desktop.height:expected.modePixelHeight;
	const double refresh=plan.request.spanDisplays?monitor->desktop.refresh:expected.refreshRate;
	if ((plan.checkMode && (w.modePixelWidth!=modeWidth || w.modePixelHeight!=modeHeight)) ||
		(plan.checkRefresh && double(w.refreshRate)!=refresh)) return Fail(error,"Actual display mode or refresh differs from the request");
	error.clear(); return true;
}

namespace {
struct RecoveryReader {
	const StateValues& values;
	std::set<std::string> used;
	bool number(const std::string& key,double& result) {
		const auto entry=values.find(key);
		if (entry==values.end() || !used.insert(key).second || !std::holds_alternative<double>(entry->second)) return false;
		result=std::get<double>(entry->second); return std::isfinite(result);
	}
	bool integer(const std::string& key,int& result) {
		double n=0;
		if (!number(key,n) || std::floor(n)!=n || n<(std::numeric_limits<int>::min)() || n>(std::numeric_limits<int>::max)()) return false;
		result=int(n); return true;
	}
	bool boolean(const std::string& key,bool& result) {
		const auto entry=values.find(key);
		if (entry==values.end() || !used.insert(key).second || !std::holds_alternative<bool>(entry->second)) return false;
		result=std::get<bool>(entry->second); return true;
	}
	bool text(const std::string& key,std::string& result) {
		const auto entry=values.find(key);
		if (entry==values.end() || !used.insert(key).second || !std::holds_alternative<std::string>(entry->second)) return false;
		result=std::get<std::string>(entry->second); return !result.empty() && result.size()<=512 && ValidStateValue(entry->second);
	}
};
bool RecoveryPlan(const SystemDisplayPlan& plan,const SystemDisplayTopology& topology,std::string& error) {
	if (!Plan(plan,error) || !Topology(topology,error)) return false;
	const auto& r=plan.request; const auto& p=r.parms; const auto& e=plan.expected;
	if (!plan.checkMode || !plan.checkRefresh || (e.fullscreen && !plan.checkPixels) ||
		plan.checkPosition!=r.restorePlacement || (plan.checkPosition && (!topology.absolutePlacement || r.windowX!=e.windowX || r.windowY!=e.windowY)) ||
		(!e.fullscreen && (e.logicalWidth!=p.width || e.logicalHeight!=p.height)) ||
		(r.spanDisplays ? plan.monitors.size()!=topology.displays.size() : plan.monitors.size()!=1))
		return Fail(error,"Recovery display checks or topology are inconsistent");
	std::set<unsigned> used;
	for (const auto& recorded:plan.monitors) {
		int matches=0;
		for (const auto& live:topology.displays) if (SameMonitor(recorded,live)) ++matches;
		const auto* live=Display(topology,recorded.id);
		if (matches!=1 || !live || !SameMonitor(recorded,*live) || !used.insert(recorded.id).second)
			return Fail(error,"Recorded monitor identity is missing or ambiguous");
	}
	const auto& selected=plan.monitors.front();
	SystemDisplayMode mode=selected.desktop;
	if (p.fullScreen && !r.fullscreenDesktop) {
		if (!SelectMode(selected,p.width,p.height,p.displayHz,mode)) return Fail(error,"Recorded exclusive mode is no longer available");
	}
	if (e.modePixelWidth!=mode.width || e.modePixelHeight!=mode.height || double(e.refreshRate)!=mode.refresh ||
		(e.fullscreen && (e.pixelWidth!=p.width || e.pixelHeight!=p.height)) ||
		(e.fullscreenDesktop && (p.width!=selected.desktop.width || p.height!=selected.desktop.height)))
		return Fail(error,"Recorded display mode does not match available exact pixel modes");
	if (!e.fullscreen && e.borderless) {
		int x,y,width,height;
		if (!Bounds(plan.monitors,x,y,width,height) || width!=p.width || height!=p.height ||
			(plan.checkPosition && (e.windowX!=x || e.windowY!=y))) return Fail(error,"Recorded borderless bounds differ from the topology");
	}
	return true;
}
} // namespace

bool CaptureDisplayRecovery(const SystemDisplayPlan& plan,const SystemDisplayTopology& topology,
	StateValues& output,std::string& error) {
	if (!RecoveryPlan(plan,topology,error)) return false;
	StateValues saved; const auto& r=plan.request; const auto& p=r.parms; const auto& e=plan.expected;
	saved["version"]=1.0; saved["absolutePlacement"]=topology.absolutePlacement;
	const auto integer=[&](const char* key,int value){saved[key]=double(value);};
	integer("width",p.width); integer("height",p.height); integer("refresh",p.displayHz); integer("samples",p.multiSamples); integer("interval",r.swapInterval);
	integer("x",r.windowX); integer("y",r.windowY);
	saved["fullscreen"]=p.fullScreen; saved["desktop"]=r.fullscreenDesktop; saved["borderless"]=p.borderless;
	saved["hidden"]=p.hiddenWindow; saved["span"]=r.spanDisplays; saved["maximized"]=r.maximized; saved["placement"]=r.restorePlacement;
	integer("actual.logicalWidth",e.logicalWidth); integer("actual.logicalHeight",e.logicalHeight);
	integer("actual.pixelWidth",e.pixelWidth); integer("actual.pixelHeight",e.pixelHeight);
	integer("actual.modeWidth",e.modePixelWidth); integer("actual.modeHeight",e.modePixelHeight);
	integer("actual.x",e.windowX); integer("actual.y",e.windowY); saved["actual.refresh"]=double(e.refreshRate);
	saved["actual.fullscreen"]=e.fullscreen; saved["actual.desktop"]=e.fullscreenDesktop; saved["actual.borderless"]=e.borderless;
	saved["actual.hidden"]=e.hidden; saved["actual.maximized"]=e.maximized; saved["actual.positionValid"]=e.positionValid; saved["actual.modeValid"]=e.currentModeValid;
	saved["check.pixels"]=plan.checkPixels; saved["check.mode"]=plan.checkMode; saved["check.refresh"]=plan.checkRefresh; saved["check.position"]=plan.checkPosition;
	integer("monitor.count",int(plan.monitors.size()));
	for (size_t i=0;i<plan.monitors.size();++i) {
		const auto& d=plan.monitors[i]; const std::string prefix="monitor."+std::to_string(i)+".";
		saved[prefix+"name"]=d.name; saved[prefix+"x"]=double(d.x); saved[prefix+"y"]=double(d.y);
		saved[prefix+"width"]=double(d.width); saved[prefix+"height"]=double(d.height);
		saved[prefix+"modeWidth"]=double(d.desktop.width); saved[prefix+"modeHeight"]=double(d.desktop.height); saved[prefix+"refresh"]=d.desktop.refresh;
	}
	output=std::move(saved); error.clear(); return true;
}

bool InspectDisplayRecovery(const StateValues& saved,SystemDisplayPlan& output,
	SystemDisplayTopology& recordedTopology,std::string& error) {
	if (saved.size()>512) return Fail(error,"Recovery display payload exceeds supported bounds");
	SystemDisplayTopology topology; topology.primary=1;
	size_t bytes=0;
	for (const auto& [key,value]:saved) {
		bytes+=key.size()+(std::holds_alternative<std::string>(value)?std::get<std::string>(value).size():8);
		if (key.size()>64 || !ValidStateValue(value) || bytes>65536) return Fail(error,"Recovery display payload exceeds its validation bounds");
	}
	RecoveryReader read{saved,{}}; SystemDisplayPlan plan; auto& r=plan.request; auto& p=r.parms; auto& e=plan.expected;
	int version=0,count=0; bool absolute=false; double refresh=0;
	bool okay=read.integer("version",version) && version==1 && read.boolean("absolutePlacement",absolute) &&
		read.integer("width",p.width) && read.integer("height",p.height) && read.integer("refresh",p.displayHz) && read.integer("samples",p.multiSamples) && read.integer("interval",r.swapInterval) &&
		read.integer("x",r.windowX) && read.integer("y",r.windowY) && read.boolean("fullscreen",p.fullScreen) && read.boolean("desktop",r.fullscreenDesktop) &&
		read.boolean("borderless",p.borderless) && read.boolean("hidden",p.hiddenWindow) && read.boolean("span",r.spanDisplays) && read.boolean("maximized",r.maximized) && read.boolean("placement",r.restorePlacement) &&
		read.integer("actual.logicalWidth",e.logicalWidth) && read.integer("actual.logicalHeight",e.logicalHeight) && read.integer("actual.pixelWidth",e.pixelWidth) && read.integer("actual.pixelHeight",e.pixelHeight) &&
		read.integer("actual.modeWidth",e.modePixelWidth) && read.integer("actual.modeHeight",e.modePixelHeight) && read.integer("actual.x",e.windowX) && read.integer("actual.y",e.windowY) &&
		read.number("actual.refresh",refresh) && refresh>=0 && refresh<=1000 &&
		read.boolean("actual.fullscreen",e.fullscreen) && read.boolean("actual.desktop",e.fullscreenDesktop) && read.boolean("actual.borderless",e.borderless) &&
		read.boolean("actual.hidden",e.hidden) && read.boolean("actual.maximized",e.maximized) && read.boolean("actual.positionValid",e.positionValid) && read.boolean("actual.modeValid",e.currentModeValid) &&
		read.boolean("check.pixels",plan.checkPixels) && read.boolean("check.mode",plan.checkMode) && read.boolean("check.refresh",plan.checkRefresh) && read.boolean("check.position",plan.checkPosition) &&
		read.integer("monitor.count",count) && count>0 && count<=MaxDisplays;
	e.refreshRate=static_cast<float>(refresh);
	if (double(e.refreshRate)!=refresh) okay=false;
	for (int i=0;okay && i<count;++i) {
		SystemDisplayDescriptor descriptor; const std::string prefix="monitor."+std::to_string(i)+".";
		okay=read.text(prefix+"name",descriptor.name) && read.integer(prefix+"x",descriptor.x) && read.integer(prefix+"y",descriptor.y) &&
			read.integer(prefix+"width",descriptor.width) && read.integer(prefix+"height",descriptor.height) &&
			read.integer(prefix+"modeWidth",descriptor.desktop.width) && read.integer(prefix+"modeHeight",descriptor.desktop.height) && read.number(prefix+"refresh",descriptor.desktop.refresh);
		descriptor.id=unsigned(i+1);
		// The record establishes this one historically selected exclusive mode.
		// Its dimensions/refresh must still agree with the request in RecoveryPlan.
		if (i==0 && p.fullScreen && !r.fullscreenDesktop)
			descriptor.modes.push_back({e.modePixelWidth,e.modePixelHeight,refresh});
		plan.monitors.push_back(descriptor); topology.displays.push_back(std::move(descriptor));
	}
	if (!okay || read.used.size()!=saved.size()) return Fail(error,"Recovery display payload has invalid, missing or unknown fields");
	topology.absolutePlacement=absolute;
	r.displayId=plan.monitors.front().id; e.displayId=r.displayId;
	r.displayIndex=e.displayIndex=0;
	if (!RecoveryPlan(plan,topology,error)) return false;
	output=std::move(plan); recordedTopology=std::move(topology); error.clear(); return true;
}

bool ValidateDisplayPreserveActualPair(const StateValues& savedRestore,const StateValues& savedTarget,
 const StateValues& catalogBaseline,const StateValues& catalogTarget,std::string& error) {
 if (!Candidate(catalogBaseline,error) || !Candidate(catalogTarget,error)) return false;
 for (const auto& item:SystemSettingsHost::Catalog()) if ((item.effects & SystemSettingDisplayRestart) &&
  !SettingsValueEqual(catalogBaseline.at(item.key),catalogTarget.at(item.key)))
  return Fail(error,"Preserve-actual recovery contains a catalog display change");
 SystemDisplayPlan restore,target; SystemDisplayTopology oldTopology,newTopology;
 if (!InspectDisplayRecovery(savedRestore,restore,oldTopology,error) ||
  !InspectDisplayRecovery(savedTarget,target,newTopology,error)) return false;
 // The swap interval is an immediate setting, but a restart names it in its
 // request. The target may differ from the captured display in that one field,
 // and only as the catalog's own change says.
 StateValues expected=savedRestore;
 if (!SettingsValueEqual(catalogBaseline.at("r_swapInterval"),catalogTarget.at("r_swapInterval")))
  expected["interval"]=catalogTarget.at("r_swapInterval");
 if (!SettingsValuesEqual(expected,savedTarget)) return Fail(error,"Preserve-actual recovery changes the captured display");
 error.clear(); return true;
}

bool ValidateDisplayRecoveryPair(const StateValues& savedRestore,const StateValues& savedTarget,
	const StateValues& catalogTarget,std::string& error) {
	SystemDisplayPlan restore,target; SystemDisplayTopology restoreTopology,targetTopology;
	if (!Candidate(catalogTarget,error) ||
		!InspectDisplayRecovery(savedRestore,restore,restoreTopology,error) ||
		!InspectDisplayRecovery(savedTarget,target,targetTopology,error)) return false;
	if (restoreTopology.absolutePlacement!=targetTopology.absolutePlacement)
		return Fail(error,"Saved display records disagree about placement capability");
	SystemDisplayTopology topology; topology.absolutePlacement=restoreTopology.absolutePlacement;
	// A non-spanning record stores only its selected monitor. Their union is
	// sufficient for target reconstruction; a span must contain the complete
	// historical set. Shared descriptors receive one identity and the union of
	// actually recorded exclusive modes, never modes invented from today's SDL.
	for (const auto* recorded:{&restoreTopology,&targetTopology}) for (const auto& monitor:recorded->displays) {
		SystemDisplayDescriptor* found=nullptr;
		for (auto& existing:topology.displays) if (SameMonitor(existing,monitor)) { found=&existing; break; }
		if (!found) {
			if (topology.displays.size()>=MaxDisplays) return Fail(error,"Saved display pair exceeds supported topology bounds");
			topology.displays.push_back(monitor); found=&topology.displays.back(); found->id=unsigned(topology.displays.size());
		} else for (const auto& mode:monitor.modes) {
			bool present=false; for (const auto& existing:found->modes) present|=SameMode(existing,mode);
			if (!present) found->modes.push_back(mode);
		}
	}
	const auto remap=[&](SystemDisplayPlan& plan) {
		for (auto& monitor:plan.monitors) for (const auto& known:topology.displays) if (SameMonitor(monitor,known)) {
			monitor=known; break;
		}
		plan.request.displayId=plan.expected.displayId=plan.monitors.front().id;
		plan.request.displayIndex=plan.expected.displayIndex=int(plan.request.displayId-1);
	};
	remap(restore); remap(target); topology.primary=restore.request.displayId;
	if (!RecoveryPlan(restore,topology,error) || !RecoveryPlan(target,topology,error)) return false;
	rendererDisplayState_t observed{};
	observed.moduleEpoch=1; observed.rendererReady=observed.windowValid=true;
	observed.presentation.generation=1; observed.presentation.available=1;
	observed.presentation.parametersValid=RDP_PARAMETER_SAMPLES|RDP_PARAMETER_SWAP_INTERVAL;
	observed.presentation.samples=restore.request.parms.multiSamples;
	observed.presentation.swapInterval=restore.request.swapInterval;
	observed.window=restore.expected; observed.window.displayScale=1;
	observed.window.pixelDensityX=observed.window.pixelDensityY=1;
	StateValues candidate=catalogTarget;
	const double index=std::get<double>(candidate.at("r_screen"));
	if (index < -1 || index >= MaxDisplays) return Fail(error,"Saved display selection exceeds supported bounds");
	// Explicit selection follows the saved descriptor. Auto must still select
	// the historical current display; remapping Auto would conceal corruption.
	if (index>=0) candidate["r_screen"]=double(target.request.displayIndex);
	// A back end without window multisampling (Vulkan) saves a single-sample
	// window whatever r_multiSamples asks; any other saved count must still be
	// the catalog's.
	SystemDisplayPlan rebuilt;
	if (!BuildDisplayRequest(candidate,observed,topology,rebuilt,error,target.request.parms.multiSamples!=0)) return false;
	if (rebuilt.monitors.size()!=target.monitors.size() || rebuilt.monitors.front().id!=target.monitors.front().id)
		return Fail(error,"Saved target monitor set contradicts the settings catalog");
	for (const auto& monitor:rebuilt.monitors) {
		bool found=false; for (const auto& saved:target.monitors) found|=monitor.id==saved.id;
		if (!found) return Fail(error,"Saved target monitor set contradicts the settings catalog");
	}
	// Enumeration order inside a span is immaterial after membership is proven.
	rebuilt.monitors=target.monitors;
	StateValues rebuiltValues,targetValues;
	if (!CaptureDisplayRecovery(rebuilt,topology,rebuiltValues,error) || !CaptureDisplayRecovery(target,topology,targetValues,error)) return false;
	if (rebuiltValues!=targetValues) return Fail(error,"Saved target display policy contradicts the settings catalog");
	error.clear(); return true;
}

bool ResolveDisplayRecovery(const StateValues& saved,const SystemDisplayTopology& topology,
	SystemDisplayPlan& output,std::string& error) {
	SystemDisplayPlan plan; SystemDisplayTopology recorded;
	if (!Topology(topology,error) || !InspectDisplayRecovery(saved,plan,recorded,error)) return false;
	if (recorded.absolutePlacement!=topology.absolutePlacement)
		return Fail(error,"Recorded display placement capability is unavailable");
	for (auto& monitor:plan.monitors) {
		const SystemDisplayDescriptor* matched=nullptr; int matches=0;
		for (const auto& live:topology.displays) if (SameMonitor(monitor,live)) { matched=&live; ++matches; }
		if (matches!=1) return Fail(error,"Recorded display configuration is missing or ambiguous; automatic restoration is unresolved");
		monitor=*matched;
	}
	plan.request.displayId=plan.expected.displayId=plan.monitors.front().id;
	const auto* selected=Display(topology,plan.request.displayId);
	plan.request.displayIndex=plan.expected.displayIndex=int(selected-&topology.displays.front());
	if (!RecoveryPlan(plan,topology,error)) return false;
	output=std::move(plan); error.clear(); return true;
}

namespace {
using FormatArgument=std::variant<int,std::string>;
// Exactly the placeholders the arguments supply, in order; %% is a percent sign.
bool FormatChecked(const std::string& format,const std::vector<FormatArgument>& arguments,std::string& output) {
	std::string result; size_t next=0;
	for (size_t i=0; i<format.size(); ++i) {
		if (format[i]!='%') { result+=format[i]; continue; }
		if (++i>=format.size()) return false;
		if (format[i]=='%') { result+='%'; continue; }
		if (next>=arguments.size()) return false;
		const auto& argument=arguments[next++];
		if (format[i]=='d' && std::holds_alternative<int>(argument)) result+=std::to_string(std::get<int>(argument));
		else if (format[i]=='s' && std::holds_alternative<std::string>(argument)) result+=std::get<std::string>(argument);
		else return false;
	}
	if (next!=arguments.size()) return false;
	output=std::move(result); return true;
}
// A translation whose placeholders differ falls back to the English format.
std::string Format(const std::string& localized,const char* english,const std::vector<FormatArgument>& arguments) {
	std::string output;
	if (FormatChecked(localized,arguments,output) && ValidStateValue(StateValue(output))) return output;
	FormatChecked(english,arguments,output); return output;
}
std::string Label(const std::string& localized,const char* english) {
	return !localized.empty() && localized.find('%')==std::string::npos && ValidStateValue(StateValue(localized)) ? localized : english;
}
// A display name cut at a code point boundary to 48 code points, the last an ellipsis.
std::string DisplayName(const std::string& name) {
	size_t points=0, cut=0;
	for (size_t i=0; i<name.size(); ++i) {
		if ((static_cast<unsigned char>(name[i])&0xC0)==0x80) continue;
		if (points==47) cut=i;
		if (++points>48) return name.substr(0,cut)+"\xE2\x80\xA6";
	}
	return name;
}
// The stock menu's aspect suffix: the nearest common ratio within 4%, else the
// reduced ratio when it is small.
std::string Aspect(int width,int height) {
	static constexpr int Ratios[][2]={{5,4},{4,3},{3,2},{5,3},{16,10},{16,9},{21,9},{32,10},{32,9}};
	const float aspect=float(width)/float(height);
	int best=-1; float bestDelta=std::numeric_limits<float>::infinity();
	for (int i=0; i<int(sizeof(Ratios)/sizeof(Ratios[0])); ++i) {
		const float delta=std::fabs(aspect-float(Ratios[i][0])/float(Ratios[i][1]));
		if (delta<bestDelta) { bestDelta=delta; best=i; }
	}
	if (best>=0 && bestDelta<=float(Ratios[best][0])/float(Ratios[best][1])*0.04f)
		return std::to_string(Ratios[best][0])+":"+std::to_string(Ratios[best][1]);
	int a=width, b=height;
	while (b) { const int r=a%b; a=b; b=r; }
	return a>0 && width/a<=64 && height/a<=64 ? std::to_string(width/a)+":"+std::to_string(height/a) : std::string{};
}
bool Integer(const StateValues& values,const char* key,int& output) {
	const auto value=values.find(key);
	if (value==values.end() || !std::holds_alternative<double>(value->second)) return false;
	const double number=std::get<double>(value->second);
	if (!std::isfinite(number) || std::floor(number)!=number || number<-1e9 || number>1e9) return false;
	output=int(number); return true;
}
} // namespace

bool SameSystemDisplay(const SystemDisplayDescriptor& a, const SystemDisplayDescriptor& b) { return SameMonitor(a,b); }

bool BuildSystemDisplayCatalog(const SystemDisplayCatalogInput& input, const StateValues& draft,
	SystemDisplayCatalog& output, std::string& error) {
	int screen=0, mode=0, customWidth=0, customHeight=0, refresh=0;
	if (!Integer(draft,"r_screen",screen) || !Integer(draft,"r_mode",mode) || !Integer(draft,"r_customWidth",customWidth) ||
		!Integer(draft,"r_customHeight",customHeight) || !Integer(draft,"r_displayRefresh",refresh))
		return Fail(error,"The display lists need the display settings draft");
	if (!Topology(input.topology,error)) return false;
	const auto& text=input.text;
	const auto& displays=input.topology.displays;
	SystemDisplayCatalog catalog; catalog.available=true; catalog.count=int(displays.size()); catalog.deviceSelected=screen;
	// Devices: each connected display, then stale indices up to the draft's.
	const int devices=(std::min)(SystemDisplayDeviceSlots,(std::max)(catalog.count,screen+1));
	for (int i=0; i<devices; ++i)
		catalog.devices.push_back(i<catalog.count ? Format(text.display,"%d: %s",{i+1,DisplayName(displays[i].name)}) :
			Format(text.missing,"Display %d (not connected)",{i+1}));
	catalog.descriptors=displays;
	int x=0, y=0, width=0, height=0;
	catalog.spanAvailable=catalog.count>1 && input.topology.absolutePlacement && Bounds(displays,x,y,width,height);
	// Auto describes the window's display, else the primary; a pick of Auto
	// checks the draft's size and rate against it.
	const auto* automatic=Display(input.topology,input.currentDisplay) ? Display(input.topology,input.currentDisplay) : Display(input.topology,input.topology.primary);
	catalog.autoDisplay=automatic ? int(automatic-displays.data()) : -1;
	// The display the mode and refresh lists describe: the chosen index, or
	// Auto's. A stale index has none.
	const SystemDisplayDescriptor* display=screen>=0 ? (screen<catalog.count ? &displays[size_t(screen)] : nullptr) : automatic;
	const auto size=[&](int w,int h) { return Format(text.size,"%d \xC3\x97 %d",{w,h}); };
	const auto sized=[&](int w,int h) {
		const auto aspect=Aspect(w,h);
		return aspect.empty() ? size(w,h) : Format(text.qualified,"%s (%s)",{size(w,h),aspect});
	};
	// Modes: Desktop Native, the sizes the display offers for exclusive
	// fullscreen, then Custom; the draft selects as the stock menu does.
	const int cw=std::clamp(customWidth,320,16384), ch=std::clamp(customHeight,240,16384);
	int targetWidth=0, targetHeight=0; const bool preferCustom=mode==-1;
	if (mode==-1) { targetWidth=cw; targetHeight=ch; }
	else if (mode>=0 && !SystemSettingsHost::ResolveModeDimensions(mode,cw,ch,0,0,targetWidth,targetHeight)) targetWidth=targetHeight=0;
	std::vector<std::pair<int,int>> sizes;
	if (display) for (const auto& m:display->modes)
		if (Dimensions(m.width,m.height) && std::find(sizes.begin(),sizes.end(),std::pair{m.width,m.height})==sizes.end())
			sizes.emplace_back(m.width,m.height);
	std::sort(sizes.begin(),sizes.end());
	// On overflow keep the current size and the largest ones.
	const size_t room=size_t(SystemDisplayModeSlots-3);
	if (sizes.size()>room) {
		const std::pair<int,int> current{targetWidth,targetHeight};
		std::vector<std::pair<int,int>> kept(sizes.end()-std::ptrdiff_t(room),sizes.end());
		if (std::find(sizes.begin(),sizes.end(),current)!=sizes.end() && std::find(kept.begin(),kept.end(),current)==kept.end()) {
			kept.erase(kept.begin()); kept.insert(std::lower_bound(kept.begin(),kept.end(),current),current);
		}
		sizes=std::move(kept);
	}
	SystemDisplayModeSlot desktop; desktop.label=Label(text.desktop,"Desktop Native");
	if (display) {
		desktop.width=display->desktop.width; desktop.height=display->desktop.height;
		desktop.label=Format(text.qualified,"%s (%s)",{desktop.label,size(desktop.width,desktop.height)});
	}
	catalog.modes.push_back(desktop);
	for (const auto& [w,h]:sizes)
		catalog.modes.push_back({SystemDisplayModeSlot::Kind::Size,w,h,SystemSettingsHost::LegacyModeForSize(w,h),sized(w,h)});
	// Custom names the size it will write: the draft's, clamped to the range.
	catalog.modes.push_back({SystemDisplayModeSlot::Kind::Custom,cw,ch,-1,Format(text.qualified,"%s (%s)",{Label(text.custom,"Custom"),size(cw,ch)})});
	if (mode!=-2) {
		int selected=-1;
		for (int i=1; i+1<int(catalog.modes.size()); ++i)
			if (catalog.modes[size_t(i)].width==targetWidth && catalog.modes[size_t(i)].height==targetHeight) { selected=i; break; }
		if (selected<0 && preferCustom) selected=int(catalog.modes.size())-1;
		if (selected<0 && Dimensions(targetWidth,targetHeight)) {
			catalog.unlistedMode.push_back({SystemDisplayModeSlot::Kind::Unlisted,targetWidth,targetHeight,mode,sized(targetWidth,targetHeight)});
			selected=SystemDisplayModeSlots-1;
		}
		catalog.modeSelected=(std::max)(selected,0);
	}
	// Refresh rates for the size the draft requests, rounded to whole hertz as
	// the strict request matches them. The desktop rate is not added.
	const int rateWidth=mode==-2 ? desktop.width : targetWidth, rateHeight=mode==-2 ? desktop.height : targetHeight;
	std::vector<int> rates;
	if (display) for (const auto& m:display->modes) {
		const int rate=int(std::floor(m.refresh+.5));
		if (m.width==rateWidth && m.height==rateHeight && rate>0 && std::find(rates.begin(),rates.end(),rate)==rates.end()) rates.push_back(rate);
	}
	std::sort(rates.begin(),rates.end());
	const size_t rateRoom=size_t(SystemDisplayRefreshSlots-2);
	if (rates.size()>rateRoom) {
		std::vector<int> kept(rates.end()-std::ptrdiff_t(rateRoom),rates.end());
		if (std::find(rates.begin(),rates.end(),refresh)!=rates.end() && std::find(kept.begin(),kept.end(),refresh)==kept.end()) {
			kept.erase(kept.begin()); kept.insert(std::lower_bound(kept.begin(),kept.end(),refresh),refresh);
		}
		rates=std::move(kept);
	}
	catalog.refresh.push_back({0,Label(text.automatic,"Auto")});
	for (int rate:rates) catalog.refresh.push_back({rate,Format(text.rate,"%d Hz",{rate})});
	if (refresh!=0) {
		const auto found=std::find_if(catalog.refresh.begin(),catalog.refresh.end(),[&](const auto& slot) { return slot.rate==refresh; });
		if (found!=catalog.refresh.end()) catalog.refreshSelected=int(found-catalog.refresh.begin());
		else {
			catalog.unlistedRefresh.push_back({refresh,Format(text.rate,"%d Hz",{refresh})});
			catalog.refreshSelected=SystemDisplayRefreshSlots-1;
		}
	}
	output=std::move(catalog); error.clear(); return true;
}

bool SystemDisplaySelectionPatch(const SystemDisplayCatalog& catalog, SystemDisplayList list, int index,
	StateValues& patch, std::string& error) {
	if (!catalog.available) return Fail(error,"The display lists are unavailable");
	StateValues result;
	const int current=list==SystemDisplayList::Device ? catalog.deviceSelected :
		list==SystemDisplayList::Mode ? catalog.modeSelected : catalog.refreshSelected;
	if (index==current) { patch=std::move(result); error.clear(); return true; }
	// The draft's size and rate as the lists name them.
	const auto& draftSize=catalog.modeSelected>=0 && catalog.modeSelected<int(catalog.modes.size()) ? catalog.modes[size_t(catalog.modeSelected)] :
		catalog.unlistedMode.empty() ? catalog.modes.front() : catalog.unlistedMode.front();
	const int draftRate=catalog.refreshSelected>=0 && catalog.refreshSelected<int(catalog.refresh.size()) ? catalog.refresh[size_t(catalog.refreshSelected)].rate :
		catalog.unlistedRefresh.empty() ? 0 : catalog.unlistedRefresh.front().rate;
	// The display an r_screen value describes: its index, or Auto's. A stale index has none.
	const auto described=[&](int screen) -> const SystemDisplayDescriptor* {
		const int at=screen>=0 ? screen : catalog.autoDisplay;
		return at>=0 && at<catalog.count && size_t(at)<catalog.descriptors.size() ? &catalog.descriptors[size_t(at)] : nullptr;
	};
	// Whether a display offers a slot's size at a whole-hertz rate, or at all
	// for 0, as the strict request matches it. Desktop Native is its own desktop.
	const auto offers=[](const SystemDisplayDescriptor& display,const SystemDisplayModeSlot& slot,int refresh) {
		const bool desktop=slot.kind==SystemDisplayModeSlot::Kind::Desktop;
		SystemDisplayMode ignored;
		return SelectMode(display,desktop ? display.desktop.width : slot.width,desktop ? display.desktop.height : slot.height,refresh,ignored);
	};
	if (list==SystemDisplayList::Device) {
		if (index<-1 || index>=(std::min)(catalog.count,int(catalog.devices.size()))) return Fail(error,"That display is not connected");
		result["r_screen"]=double(index);
		// The new display keeps the draft's size and rate where it offers them. A
		// listed or legacy size it lacks becomes Desktop Native and a rate it lacks
		// becomes Auto, so the pick alone never makes Apply refuse the draft. A
		// typed Custom size stays the player's choice.
		if (const auto* display=described(index)) {
			const bool listed=draftSize.kind==SystemDisplayModeSlot::Kind::Size || draftSize.kind==SystemDisplayModeSlot::Kind::Unlisted;
			const bool keepSize=!listed || offers(*display,draftSize,0);
			if (!keepSize) result["r_mode"]=-2.0;
			const SystemDisplayModeSlot desktop;
			if (draftRate>0 && !offers(*display,keepSize ? draftSize : desktop,draftRate)) result["r_displayRefresh"]=0.0;
		}
	} else if (list==SystemDisplayList::Mode) {
		if (index<0 || index>=int(catalog.modes.size())) return Fail(error,"That display size is not in the list");
		const auto& slot=catalog.modes[size_t(index)];
		if (slot.kind==SystemDisplayModeSlot::Kind::Desktop) result["r_mode"]=-2.0;
		else if (slot.kind==SystemDisplayModeSlot::Kind::Custom) {
			result["r_mode"]=-1.0; result["r_customWidth"]=double(slot.width); result["r_customHeight"]=double(slot.height);
		} else {
			// A listed size writes both dimensions and its legacy mode, else Custom.
			result["r_customWidth"]=double(slot.width); result["r_customHeight"]=double(slot.height);
			result["r_mode"]=double(slot.legacyMode);
		}
		// A rate the picked size lacks on the listed display returns to Auto.
		if (const auto* display=described(catalog.deviceSelected); display && draftRate>0 && !offers(*display,slot,draftRate))
			result["r_displayRefresh"]=0.0;
	} else {
		if (index<0 || index>=int(catalog.refresh.size())) return Fail(error,"That refresh rate is not in the list");
		result["r_displayRefresh"]=double(catalog.refresh[size_t(index)].rate);
	}
	patch=std::move(result); error.clear(); return true;
}
#endif // !ID_DEDICATED
} // namespace openq4::ui
