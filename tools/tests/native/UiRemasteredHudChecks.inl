#include <chrono>
// Remastered HUD documents (ui-visual-design.md 14.4-14.12): each loads
// through the real compiler and runtime, lands its groups on the section 14.4
// anchors at several aspects, and answers the stock HUD's state and events.

static Viewport RemasteredViewport(int width, int height) {
	Viewport viewport; viewport.width = width; viewport.height = height;
	viewport.canvasHeight = 720; viewport.canvasMinWidth = 960; viewport.fitScale = 1;
	return viewport;
}

// The engine peer's layout values for a viewport and the window's safe-area
// insets it carries (RetainedHud.cpp, through the shared HudLayoutFor).
static StateValues RemasteredLayout(const Viewport& viewport, float scale = 1, float insetPercent = 0, int hudWidth = 1) {
	const auto layout = HudLayoutFor(float(viewport.width),float(viewport.height),viewport.DpRatio(),
		{viewport.safeLeft,viewport.safeTop,viewport.safeRight,viewport.safeBottom},hudWidth,insetPercent,scale);
	return {{"hud.width",double(layout.width)},{"hud.height",double(layout.height)},{"hud.left",double(layout.left)},
		{"hud.right",double(layout.right)},{"hud.top",double(layout.top)},{"hud.bottom",double(layout.bottom)},
		{"hud.scale",double(layout.scale)},{"hud.centerScale",double(layout.centerScale)}};
}
static StateValues RemasteredLayout(int width, int height, float scale = 1, float insetPercent = 0, int hudWidth = 1) {
	return RemasteredLayout(RemasteredViewport(width,height),scale,insetPercent,hudWidth);
}

// The HUD layout preference the documents read (ui_hudLayout: 1 Remastered, 2 Minimal).
static void RemasteredLayoutPreference(ScreenHost& host, double layout) { host.cvars["ui_hudLayout"] = layout; }

static bool Shown(const Runtime& runtime, const std::string& id) {
	const auto value = runtime.PresentedValue(id,"display");
	return value && value->text != "none";
}

// Section 13.3 and LAY-016 on the HUD, with the window's safe-area insets of
// a phone in portrait and in landscape. Every node placed on an edge anchor
// (its box follows hud.left, hud.top, hud.right or hud.bottom) keeps those
// edges inside the safe area, an edge midpoint (its margin follows them)
// centres on the safe area's height, and everything a readout on one draws
// (each text, vector and picture under it that shows) stays inside it. A
// draw belongs to the readout whose box holds all of it, since boxes run
// wider than their words and art. What a twin without insets draws past an
// edge of the view (the objectives bar, art past the stock canvas) may run
// under that edge's inset; what it keeps on the screen must stay in the safe
// area. The root, and every full-screen effect filling it, still fills the
// view, and a view that loses its insets lays out as the twin.
static void CheckRemasteredHudSafeArea(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	Check(document.Load(source,diagnostics),"a Remastered HUD loads for the safe-area probe");
	const auto& model = document.Model();
	std::string name = path;
	std::replace(name.begin(),name.end(),'\\','/');
	name = name.substr(name.rfind("guis/"));
	const std::function<bool(const Expression&)> edge = [&](const Expression& expression) {
		return expression.state == "hud.left" || expression.state == "hud.top" || expression.state == "hud.right" ||
			expression.state == "hud.bottom" || std::any_of(expression.args.begin(),expression.args.end(),edge);
	};
	std::map<std::string,std::set<std::string>> placed;   // each edge-placed node's anchored edges
	for (const auto& binding : model.bindings)
		if ((binding.property == "left" || binding.property == "top" || binding.property == "right" || binding.property == "bottom" ||
			binding.property == "margin-top") && std::any_of(binding.values.begin(),binding.values.end(),edge))
			placed[binding.node].insert(binding.property);
	struct Readout { std::string id; std::vector<std::string> ancestors, clips; };
	std::vector<Readout> readouts, fills;
	std::vector<std::string> ancestors, clips;
	const auto full = [](const Node& node) {
		const auto length = [&](const char* key, double value, const char* unit) {
			const auto found = node.properties.find(key);
			return found != node.properties.end() && found->second.data[0] == value && found->second.unit == unit;
		};
		return length("left",0,"dp") && length("top",0,"dp") && length("width",100,"%") && length("height",100,"%");
	};
	const std::function<void(const Node&,bool,bool)> collect = [&](const Node& node, bool on, bool filling) {
		on = on || placed.contains(node.id);
		filling = filling && (node.id == model.root.id || full(node));
		if (on && node.type != "group") readouts.push_back({node.id,ancestors,clips});
		if (filling && node.id != model.root.id) fills.push_back({node.id,ancestors,clips});
		// A clipping group or a mask bounds what shows of its subtree.
		const auto overflow = node.properties.find("overflow");
		const bool clipping = node.mask.has_value() || (overflow != node.properties.end() && overflow->second.text != "visible");
		ancestors.push_back(node.id);
		if (clipping) clips.push_back(node.id);
		for (const auto& child : node.children) collect(child,on,filling);
		if (clipping) clips.pop_back();
		ancestors.pop_back();
	};
	collect(model.root,false,true);
	const auto savedCVars = host.cvars;
	RemasteredLayoutPreference(host,1);
	// `runtime` takes the insets; `twin` is driven alike and never has them.
	Runtime runtime(host), twin(host); std::string error; Runtime::EventEffects effects;
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/hud/safe-area.q4ui",diagnostics) && twin.Initialize() &&
		twin.LoadDocument(source,"guis/hud/safe-area.q4ui",diagnostics),"the HUD loads into a runtime and its twin for the safe-area probe");
	StateValues values;
	for (const auto& [key,value] : StateValues{{"player_health",100.0},{"player_healthpct",1.0},{"player_armor",57.0},{"player_armorpct",.57},
		{"player_ammo",36.0},{"player_totalammo",50.0},{"player_clip_size",50.0},{"player_ammopct",.72},{"player_weapon",5.0},
		{"ammoIcon",std::string("item_nailgun")},{"weapicon",std::string("gun_nailgun")},{"weapon0",true},{"weapon5",true},
		{"weapon5_index",1.0},{"weapon5_ammo",86.0},{"weaponcount",2.0},{"player_idealweapon",5.0},{"weaponname",std::string("NAILGUN")},
		{"itemtext",std::string("Armor Shard")},{"itemicon",std::string("item_armorshard")},{"vehicle_health",1.0}})
		if (model.state.contains(key)) values[key] = value;
	const auto both = [&](const StateValues& state, double now) {
		return runtime.SetState(state,error,now) && twin.SetState(state,error,now);
	};
	const auto run = [&](const std::string& event, double now) {
		return runtime.RunEvent(event,now,effects,error) && twin.RunEvent(event,now,effects,error);
	};
	double now = 0;
	Check(both(values,now) && run("hudJoin",now),"the HUD joins for the safe-area probe");
	// Everything that comes up, at once, for each shape: notices, pickups, the
	// strip, the transmission display, boss bars, warnings and messages. The
	// interactive brackets follow their target in the world: no readout.
	const auto raises = [](const std::string& event) {
		if (event.find("Brackets") != std::string::npos) return false;
		for (const char* prefix : {"hide","fade","clear","oq4_close","leave","hudJoin","onInit"}) if (event.starts_with(prefix)) return false;
		for (const char* suffix : {"Done","done","Down","_off","Off","Out","Hide","Clear"}) if (event.ends_with(suffix)) return false;
		return true;
	};
	std::vector<std::string> raised;
	for (const auto& [event,program] : model.events) if (raises(event)) raised.push_back(event);
	const auto hidden = [](Runtime& view, const std::string& id) {
		const auto display = view.PresentedValue(id,"display");
		const auto opacity = view.PresentedValue(id,"opacity");
		return (display && display->text == "none") || (opacity && opacity->type == ValueType::Number && opacity->data[0] <= 0);
	};
	const auto concealed = [&](Runtime& view, const Readout& readout) {
		return hidden(view,readout.id) || std::any_of(readout.ancestors.begin(),readout.ancestors.end(),[&](const auto& id) { return hidden(view,id); });
	};
	// What shows of a readout: its box inside every clipping group over it.
	const auto shown = [&](Runtime& view, const Readout& readout, Bounds& box) {
		if (concealed(view,readout) || !view.GetBounds(readout.id,box)) return false;
		float left = box.x, top = box.y, right = box.x+box.width, bottom = box.y+box.height;
		for (const auto& clip : readout.clips) {
			Bounds frame;
			if (!view.GetBounds(clip,frame)) return false;
			left = std::max(left,frame.x); top = std::max(top,frame.y);
			right = std::min(right,frame.x+frame.width); bottom = std::min(bottom,frame.y+frame.height);
		}
		box = {left,top,right-left,bottom-top};
		return box.width >= 1 && box.height >= 1;
	};
	const auto snapshot = [&](Runtime& view) {
		std::map<std::string,Bounds> boxes;
		for (const auto& readout : readouts) {
			Bounds box;
			if (shown(view,readout,box)) boxes[readout.id] = box;
		}
		return boxes;
	};
	// Each readout's drawing: the lit extent of every draw its box holds whole
	// (the smallest box holding it wins).
	struct Extent { float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f; };
	const auto drawings = [&](Runtime& view) {
		std::vector<std::pair<const Readout*,Bounds>> regions;
		for (const auto& readout : readouts) {
			Bounds box;
			if (!concealed(view,readout) && view.GetBounds(readout.id,box) && box.width > 0 && box.height > 0) regions.push_back({&readout,box});
		}
		std::map<std::string,std::vector<Extent>> result;
		for (const auto& draw : host.draws) {
			Extent lit;
			for (const auto& vertex : draw.vertices) if (vertex.a > 0) {
				lit.x0 = std::min(lit.x0,vertex.x); lit.y0 = std::min(lit.y0,vertex.y); lit.x1 = std::max(lit.x1,vertex.x); lit.y1 = std::max(lit.y1,vertex.y);
			}
			if (lit.x1 < lit.x0) continue;
			const Readout* owner = nullptr; float area = 0;
			for (const auto& [readout,box] : regions)
				if (lit.x0 >= box.x-1 && lit.y0 >= box.y-1 && lit.x1 <= box.x+box.width+1 && lit.y1 <= box.y+box.height+1 &&
					(!owner || box.width*box.height < area)) owner = readout, area = box.width*box.height;
			if (owner) result[owner->id].push_back(lit);
		}
		return result;
	};
	for (const auto& shape : {SafeShape{1080,2400,0,96,0,48},SafeShape{2400,1080,96,0,24,40}}) {
		const Viewport plain = RemasteredViewport(shape.width,shape.height);
		Check(both(RemasteredLayout(plain),now += .1),"the HUD takes a view without insets");
		for (const auto& event : raised) Check(run(event,now += .001),"a HUD event runs for the safe-area probe");
		runtime.Frame(plain,now += .5); twin.Frame(plain,now);
		Viewport viewport = plain;
		viewport.safeLeft = shape.left; viewport.safeTop = shape.top; viewport.safeRight = shape.right; viewport.safeBottom = shape.bottom;
		const auto inset = RemasteredLayout(viewport);
		Check(runtime.SetState(inset,error,now += .1),error.c_str());
		host.draws.clear();
		runtime.Frame(viewport,now += .5);
		const auto drawn = drawings(runtime);
		host.draws.clear();
		twin.Frame(plain,now);
		const auto unset = drawings(twin);
		const float w = float(shape.width), h = float(shape.height), dp = viewport.DpRatio();
		Check(Near(dp,std::min((h-shape.top-shape.bottom)/720,(w-shape.left-shape.right)/960),.0001f),"the HUD canvas fits the safe area");
		Check(Near(float(std::get<double>(inset.at("hud.top"))*dp),shape.top,.01f) &&
			Near(float(std::get<double>(inset.at("hud.bottom"))*dp),shape.bottom,.01f) &&
			float(std::get<double>(inset.at("hud.left"))*dp) >= shape.left-.01f && float(std::get<double>(inset.at("hud.right"))*dp) >= shape.right-.01f,
			"each edge anchor keeps at least the window's inset");
		const auto fillsView = [&](const std::string& id) {
			Bounds box;
			const bool filled = runtime.GetBounds(id,box) && Near(box.x,0,1) && Near(box.y,0,1) && Near(box.width,w,1) && Near(box.height,h,1);
			if (!filled) std::fprintf(stderr,"%s %gx%g: %s at %.1f,%.1f %.1fx%.1f does not fill the view\n",name.c_str(),w,h,id.c_str(),
				box.x,box.y,box.width,box.height);
			return filled;
		};
		Check(fillsView(model.root.id) && std::all_of(fills.begin(),fills.end(),[&](const auto& fill) { return concealed(runtime,fill) || fillsView(fill.id); }),
			"the HUD root and the full-screen effects filling it stay full-bleed");
		const float left = shape.left-1, top = shape.top-1, right = w-shape.right+1, bottom = h-shape.bottom+1;
		size_t outside = 0, middles = 0;
		const float middle = shape.top+(h-shape.top-shape.bottom)/2;
		for (const auto& [id,edges] : placed) {
			Bounds box;
			if (!runtime.GetBounds(id,box) || (box.width <= 0 && box.height <= 0)) continue;
			middles += edges.contains("margin-top");
			if ((!edges.contains("left") || box.x >= left) && (!edges.contains("top") || box.y >= top) &&
				(!edges.contains("right") || box.x+box.width <= right) && (!edges.contains("bottom") || box.y+box.height <= bottom) &&
				(!edges.contains("margin-top") || Near(box.y+box.height/2,middle,1))) continue;
			std::fprintf(stderr,"%s %dx%d: %s at %.1f,%.1f %.1fx%.1f leaves the safe area %.0f,%.0f-%.0f,%.0f or its middle %.1f\n",
				name.c_str(),shape.width,shape.height,id.c_str(),box.x,box.y,box.width,box.height,left+1,top+1,right-1,bottom-1,middle);
			++outside;
		}
		const bool sides = model.state.contains("hud.top") && std::any_of(placed.begin(),placed.end(),[](const auto& entry) {
			return entry.first == "anchor-cl" || entry.first == "anchor-cr";
		});
		Check(!sides || middles > 0,"the edge midpoints centre on the safe area");
		size_t owned = 0;
		for (const auto& [id,extents] : drawn) {
			// The edges past which the twin draws this readout off the screen.
			bool offLeft = false, offTop = false, offRight = false, offBottom = false;
			if (const auto found = unset.find(id); found != unset.end()) for (const auto& lit : found->second) {
				offLeft = offLeft || lit.x0 < -1; offTop = offTop || lit.y0 < -1; offRight = offRight || lit.x1 > w+1; offBottom = offBottom || lit.y1 > h+1;
			}
			for (const auto& lit : extents) {
				++owned;
				if ((offLeft || lit.x0 >= left) && (offTop || lit.y0 >= top) && (offRight || lit.x1 <= right) && (offBottom || lit.y1 <= bottom)) continue;
				std::fprintf(stderr,"%s %dx%d: %s draws at %.1f,%.1f-%.1f,%.1f, outside the safe area %.0f,%.0f-%.0f,%.0f\n",name.c_str(),shape.width,
					shape.height,id.c_str(),lit.x0,lit.y0,lit.x1,lit.y1,left+1,top+1,right-1,bottom-1);
				++outside;
			}
		}
		Check(placed.empty() || owned > 0,"the safe-area probe sees readouts drawn on the edge anchors");
		Check(outside == 0,"every edge anchor and every readout on one stands in the safe area");
		// The insets go: every readout returns to where the twin has it.
		Check(runtime.SetState(RemasteredLayout(plain),error,now += .1),error.c_str());
		runtime.Frame(plain,now += .5); twin.Frame(plain,now);
		const auto after = snapshot(runtime), still = snapshot(twin);
		Check(after.size() == still.size() && std::all_of(still.begin(),still.end(),[&](const auto& entry) {
			const auto found = after.find(entry.first);
			const Bounds box = found != after.end() ? found->second : Bounds{-1,-1,-1,-1};
			const bool same = Near(box.x,entry.second.x,.01f) && Near(box.y,entry.second.y,.01f) && Near(box.width,entry.second.width,.01f) &&
				Near(box.height,entry.second.height,.01f);
			if (!same) std::fprintf(stderr,"%s %dx%d: %s stands at %.1f,%.1f %.1fx%.1f, not at %.1f,%.1f %.1fx%.1f\n",name.c_str(),shape.width,
				shape.height,entry.first.c_str(),box.x,box.y,box.width,box.height,entry.second.x,entry.second.y,entry.second.width,entry.second.height);
			return same;
		}),"a HUD that loses its insets lays every readout out as one that never had them");
	}
	std::printf("%s: %zu edge-placed nodes, %zu readouts, %zu full-screen layers, %zu events raised\n",name.c_str(),placed.size(),readouts.size(),
		fills.size(),raised.size());
	Check(host.errors == 0,"no HUD safe-area diagnostics");
	host.cvars = savedCVars;
}

// Grain must cover the full swept area, including the instant either side
// of its loop boundary. A sparse sheet that scrolls out of the mask passes
// a first-frame screenshot but leaves the gauges bare for most of the loop.
static void CheckStroggGaugeGrain(ScreenHost& host, const std::string& source) {
	const bool previousReducedMotion=host.reducedMotion;
	host.reducedMotion=false;
	Runtime runtime(host);
	std::vector<Diagnostic> diagnostics;
	std::string error;
	Runtime::EventEffects effects;
	Check(runtime.Initialize() && runtime.LoadDocument(source,"guis/hud/strogg-grain.q4ui",diagnostics),"Strogg grain document loads");
	StateValues values=RemasteredLayout(1280,720);
	values.insert({{"player_health",100.0},{"player_healthpct",1.0},{"player_armor",100.0},{"player_armorpct",1.0},
		{"player_ammo",50.0},{"player_totalammo",100.0},{"player_clip_size",50.0},{"player_ammopct",1.0}});
	Check(runtime.SetState(values,error,0) && runtime.RunEvent("hudJoin",0,effects,error),"Strogg grain starts with the HUD");
	for (double phase : {.1,.5,1.0,1.5,1.99,2.01}) {
		host.draws.clear();
		runtime.Frame(RemasteredViewport(1280,720),phase);
		for (const char* kind : {"ammo","health","armor"}) {
			const std::string prefix=std::string(kind)+"-static";
			Bounds mask;
			const auto translation=runtime.PresentedValue(prefix,"transform");
			Check(translation && runtime.GetBounds(prefix+"-mask",mask) && Near(mask.width,129*1.5f,.1f) && Near(mask.height,67*1.5f,.1f),
				"every grain mask retains the complete gauge rectangle");
			float left=1e9f,top=1e9f,right=-1e9f,bottom=-1e9f;
			for (int row=0;row<5;++row) for (int column=0;column<2;++column) {
				const std::string tile=prefix+"-r"+std::to_string(row)+"-c"+std::to_string(column);
				Bounds bounds;
				Check(runtime.GetBounds(tile,bounds) && Near(bounds.width,mask.width,.1f) && Near(bounds.height,mask.height,.1f) &&
					Near(bounds.x,mask.x+column*mask.width,.1f) && Near(bounds.y,mask.y+(row-4)*mask.height,.1f),
					"the grain images form a contiguous two-column five-row tile sheet");
				const auto image=runtime.PresentedValue(tile,"image"), blend=runtime.PresentedValue(tile,"image-blend"),
					tint=runtime.PresentedValue(tile,"image-color");
				Check(image && image->text=="gfx/guis/hud/s_static" && blend && blend->text=="additive" && tint &&
					Near(float(tint->data[0]),22/255.f,.001f) && Near(float(tint->data[1]),22/255.f,.001f) &&
					Near(float(tint->data[2]),22/255.f,.001f) && Near(float(tint->data[3]),1,.001f),
					"Strogg grain uses installed stock noise with additive #161616 tint");
				left=std::min(left,bounds.x+float(translation->data[0]));
				top=std::min(top,bounds.y+float(translation->data[1]));
				right=std::max(right,bounds.x+bounds.width+float(translation->data[0]));
				bottom=std::max(bottom,bounds.y+bounds.height+float(translation->data[1]));
			}
			Check(left<=mask.x+.1f && top<=mask.y+.1f && right>=mask.x+mask.width-.1f && bottom>=mask.y+mask.height-.1f,
				"animated grain covers the entire local mask at every sampled phase");
		}
	}
	Check(std::find(host.materials.begin(),host.materials.end(),"q4-add/gfx/guis/hud/s_static")!=host.materials.end(),
		"the grain resolves through the installed additive material");
	host.reducedMotion=true;
	runtime.SetReducedMotion(true,2.1);
	runtime.Frame(RemasteredViewport(1280,720),2.2);
	std::vector<Value> still;
	for (const char* kind : {"ammo","health","armor"}) still.push_back(*runtime.PresentedValue(std::string(kind)+"-static","transform"));
	runtime.Frame(RemasteredViewport(1280,720),3.2);
	int index=0;
	for (const char* kind : {"ammo","health","armor"}) {
		const auto transform=runtime.PresentedValue(std::string(kind)+"-static","transform");
		Check(transform && Near(float(transform->data[0]),float(still[index].data[0]),.001f) &&
			Near(float(transform->data[1]),float(still[index].data[1]),.001f),"reduced motion leaves the grain translation still");
		++index;
	}
	Check(host.errors==0,"Strogg grain phases produce no runtime diagnostics");
	runtime.Shutdown();
	host.reducedMotion=previousReducedMotion;
}

static void CheckHudInstruments(ScreenHost& host, const std::string& source, bool strogg) {
	Runtime runtime(host); std::vector<Diagnostic> diagnostics; std::string error; Runtime::EventEffects effects;
	Check(runtime.LoadDocument(source,"instruments.q4ui",diagnostics),"HUD instruments load");
	auto state=RemasteredLayout(1280,720);
	state.insert({{"player_health",100.0},{"player_healthpct",1.0}});
	Check(runtime.SetState(state,error,0) && runtime.RunEvent("hudJoin",0,effects,error),"instruments start");
	const float width=(strogg ? 129.f : 125.f)*1.5f, height=(strogg ? 67.f : 59.f)*1.5f;
	for (bool reduced : {false,true}) {
		const double epoch=reduced ? 10 : 0;
		runtime.SetReducedMotion(reduced,epoch);
		Check(runtime.RunEvent("hudJoin",epoch,effects,error),"instrument playback restarts");
		for (double time : {.1,1.25,2.63,2.64,4.99,5.01}) {
			host.draws.clear(); runtime.Frame(RemasteredViewport(1280,720),epoch+time);
			for (int wave=0;wave<(strogg ? 2 : 1);++wave) {
				const std::string id="health-ekg-normal-"+std::to_string(wave);
				const auto shift=runtime.PresentedValue(id,"image-offset-x");
				const double speed=wave==1 ? .38 : .2;
				// The standard HUD timeline inserts its authored start at 1 ms.
				Check(shift && Near(float(shift->data[0]),float(-std::fmod(time*speed,1.0)),.001f),
					"ECG keeps its scroll speed and full texture period, including reduced motion");
				Bounds first;
				Check(runtime.GetBounds(id,first) && Near(first.width,width,.02f) && Near(first.height,height,.02f),
					"one stationary ECG quad covers the mask throughout UV wrapping");
				const auto image=runtime.PresentedValue(id,"image"), blend=runtime.PresentedValue(id,"image-blend");
				const std::string expected=strogg ? (wave==1 ? "gfx/guis/hud/s_ekg2.tga" : "gfx/guis/hud/s_ekg.tga") : "gfx/guis/hud/ekg.tga";
				Check(image && image->text==expected && blend && blend->text=="additive","ECG uses installed bitmap light");
			}
		}
	}
	Check(runtime.SetState({{"player_health",20.0},{"player_healthpct",.2}},error,16) && runtime.RunEvent("updateHealth",16,effects,error),"low ECG state");
	runtime.Frame(RemasteredViewport(1280,720),16.1);
	const auto low=runtime.PresentedValue("health-ekg-low-0","image-color");
	Check(Shown(runtime,"health-ekg-low") && !Shown(runtime,"health-ekg-normal") && low && Near(float(low->data[0]),76/255.f,.001f),"low health keeps the red ECG tint");
	Check(runtime.SetState({{"player_health",100.0},{"player_healthpct",1.0}},error,17) && runtime.RunEvent("updateHealth",17,effects,error),"ECG recovers");
	Check(runtime.RunEvent("radioChatterUp",17,effects,error),"radio graph shows");
	Check(!runtime.GetState().contains("radio.peak.0"),"preset radio graph has no per-frame audio bindings");
	runtime.SetReducedMotion(false,18);
	Check(runtime.RunEvent("hudJoin",18,effects,error),"preset graph starts");
	const double waveTable[]={.95,.4,1,.5,.8,1,.6,1.4,.95,.6,.75,1.2,.7,1.3,.6,.8,.95,.6,1,.7,.95};
	for (int sample=0;sample<48;++sample) {
		const double phase=(sample+.5)/(21*.6);
		host.draws.clear(); runtime.Frame(RemasteredViewport(1280,720),18+phase);
		const auto shift=runtime.PresentedValue("radio-graph","image-offset-x");
		Check(shift && Near(float(shift->data[0]),float(std::fmod(phase*.6,1.0)),.001f),"legacy audiograph scrolls at .6 texture widths per second");
		const double tablePhase=std::fmod(phase*.6,1.0)*21;
		const int index=int(tablePhase); const double fraction=tablePhase-index;
		const auto scale=runtime.PresentedValue("radio-graph","image-scale-y");
		Check(scale && std::abs(scale->data[0]-(waveTable[index]*(1-fraction)+waveTable[(index+1)%21]*fraction))<.0001,
			"legacy waveform interpolates every stock table interval, including the duplicated end and wrap sample");
		Bounds wave; Check(runtime.GetBounds("radio-graph",wave) && Near(wave.width,41*1.5f,.01f) && Near(wave.height,25*1.5f,.01f),
			"the legacy waveform uses its complete source rectangle, not the narrower mask width");
	}
	Check(std::find(host.materials.begin(),host.materials.end(),"q4-add/_repeat/gfx/guis/hud/waveform.tga")!=host.materials.end(),
		"the audiograph repeats the installed legacy bitmap with additive light");
	runtime.SetReducedMotion(true,22);
	runtime.Frame(RemasteredViewport(1280,720),22.1);
	const auto still=*runtime.PresentedValue("radio-graph","image-offset-x");
	const auto stillScale=*runtime.PresentedValue("radio-graph","image-scale-y");
	runtime.Frame(RemasteredViewport(1280,720),23);
	Check(runtime.PresentedValue("radio-graph","image-offset-x")->data==still.data &&
		runtime.PresentedValue("radio-graph","image-scale-y")->data==stillScale.data,"reduced motion freezes both waveform UV channels");
	Check(host.errors==0,"HUD instruments have no runtime diagnostics");
}

static void CheckRemasteredHud(ScreenHost& host, const char* path, const std::string& id, bool strogg) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%s %u:%u %s\n",path,unsigned(d.line),unsigned(d.column),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == id,"Remastered HUD validates");
	Check(document.Model().canvasHeight == 720 && document.Model().canvasMinWidth == 960,"Remastered HUD uses the 4:3 center-screen canvas");
	RemasteredLayoutPreference(host,1);
	CheckHudInstruments(host,source,strogg);
	if (strogg) CheckStroggGaugeGrain(host,source);
	Runtime runtime(host);
	Check(runtime.Initialize(),"retained runtime");
	Check(runtime.LoadDocument(source,"guis/hud/remastered.q4ui",diagnostics),"Remastered HUD loads into a runtime");
	std::string error;
	StateValues values = RemasteredLayout(1280,720);
	values.insert({{"player_health",100.0},{"player_healthpct",1.0},{"player_armor",57.0},{"player_armorpct",.57},
		{"player_ammo",36.0},{"player_totalammo",50.0},{"player_clip_size",50.0},{"player_ammopct",.72},{"player_weapon",5.0},
		{"ammoIcon",std::string("item_nailgun")},{"weapicon",std::string("gun_nailgun")},{"mp",false}});
	Check(runtime.SetState(values,error,0),error.c_str());
	Runtime::EventEffects effects;
	Check(runtime.RunEvent("hudJoin",0,effects,error),"the join program runs");
	host.draws.clear();
	runtime.Frame(RemasteredViewport(1280,720),.1);
	Check(host.errors == 0,"Remastered HUD frames without runtime errors");
	Check(!host.draws.empty(),"Remastered HUD draws native geometry");
	Bounds ammo, health, armor;
	// The status bar: one 438 u group centred on the bottom edge (Strogg 449 u).
	Check(runtime.GetBounds("ammo-gauge",ammo) && runtime.GetBounds("health-gauge",health) && runtime.GetBounds("armor-gauge",armor),
		"the three gauges are laid out");
	const float first = strogg ? 95.5f : 101.f, second = strogg ? 275.5f : 278.f, third = strogg ? 415.5f : 414.f;
	Check(Near(ammo.x,160+first*1.5f) && Near(health.x,160+second*1.5f) && Near(armor.x,160+third*1.5f),
		"the status bar sits centred with the stock gaps at 16:9");
	Check(Near(ammo.y,(strogg ? 419 : 420)*1.5f),"gauges rest on the bottom edge");
	for (const auto& gauge : {ammo, health, armor})
		Check(Near(gauge.width,(strogg ? 129 : 125)*1.5f) && Near(gauge.height,(strogg ? 67 : 59)*1.5f),
			"each centered gauge keeps its legacy rectangle size");
	// Armor at zero hides its gauge without moving the others.
	values["player_armor"] = 0.0; values["player_armorpct"] = 0.0;
	Check(runtime.SetState(values,error,.2),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.25);
	Bounds moved;
	Check(!Shown(runtime,"armor-gauge"),"armor hides at zero");
	Check(runtime.GetBounds("health-gauge",moved) && Near(moved.x,health.x),"health keeps its place when armor goes");
	values["player_armor"] = 57.0; values["player_armorpct"] = .2;
	Check(runtime.SetState(values,error,.3),error.c_str());
	// The armor fill drains toward the numerals: at 20% only the trailing
	// part from 120 - 115 x 0.2 = 97 u shows, cut parallel to the sheared
	// sides. The texture's bevel modulates the tint at constant alpha. Its
	// silhouette layer is composed through the moving fill window; vertices
	// submitted into that intermediate layer are deliberately not pre-clipped.
	host.draws.clear();
	runtime.Frame(RemasteredViewport(1280,720),.32);
	{
		// The Strogg fill runs 119 u to 124 u: 100.2 u at 20%.
		const float lead = strogg ? 124 - 119*.2f : 97, fillR = strogg ? 1.f : .698f, fillG = strogg ? .564f : .811f;
		const float alpha = strogg ? .35f : .349f;
		Bounds gauge; runtime.GetBounds("armor-gauge",gauge);
		const auto window = runtime.PresentedValue("armor-fill-window","left");
		const auto width = runtime.PresentedValue("armor-fill-window","width");
		size_t count = 0;
		for (const auto& draw : host.draws) for (const auto& v : draw.vertices)
			if (std::abs(v.a-alpha) < .005f && v.r > 0 && std::abs(v.g/v.r-fillG/fillR) < .025f &&
				v.r >= fillR*alpha*.67f && v.r <= fillR*alpha*1.01f &&
				v.x > gauge.x-20 && v.x < gauge.x+gauge.width+20) ++count;
		std::fprintf(stderr,"armor window left %.2f, shaded fill vertices %zu, clip masks %llu fallbacks %llu\n",
			window ? window->data[0] : -1.f,count,
			(unsigned long long)runtime.Statistics().clipMasks,(unsigned long long)runtime.Statistics().clipMaskFallbacks);
		Check(window && Near(float(window->data[0]),lead*1.5f,.01f),"the fill window's leading edge follows the armor");
		Check(count > 0 && width && Near(float(width->data[0]),((strogg ? 129 : 125)-lead)*1.5f,.01f) &&
			runtime.Statistics().clipMasks > 0 && runtime.Statistics().clipMaskFallbacks == 0,
			"the shaded fill is composed through its trailing clip window without fallback");
	}
	values["player_armorpct"] = .57;
	Check(runtime.SetState(values,error,.35),error.c_str());

	// 21:9 (2520x1080): side screens capped at 21:9; the transmission display
	// takes the trailing edge, the status bar stays centred.
	Check(runtime.SetState(RemasteredLayout(2520,1080),error,.4),error.c_str());
	Check(runtime.RunEvent("radioChatterUp",.4,effects,error),"radio up");
	runtime.Frame(RemasteredViewport(2520,1080),.5);
	// The top-trailing anchor box (the stock canvas) ends at the view's edge.
	Bounds corner;
	const float px = 1080/720.f;   // pixels per dp at 1080p
	Check(runtime.GetBounds("anchor-tr",corner) && Near(corner.x+corner.width,2520,1.5f) && Near(corner.width,960*px,1.5f) && Near(corner.y,0),
		"the transmission display's anchor takes the trailing edge at 21:9");
	Check(Shown(runtime,"radio"),"the transmission display shows on radioChatterUp");
	Check(runtime.GetBounds("ammo-gauge",ammo) && Near(ammo.x,(2520-960*px)/2+first*1.5f*px,1.5f),"the status bar stays centred at 21:9");

	// 32:9 (3840x1080): edge groups stop at the 21:9 edges.
	Check(runtime.SetState(RemasteredLayout(3840,1080),error,.6),error.c_str());
	runtime.Frame(RemasteredViewport(3840,1080),.7);
	const float cap = 720*64/27.f*px;
	Check(runtime.GetBounds("anchor-tr",corner) && Near(corner.x+corner.width,(3840+cap)/2,2.f),
		"the trailing groups stop at the 21:9 edge on 32:9");

	// 9:16 portrait (720x1280): the center screen spans the width.
	Check(runtime.SetState(RemasteredLayout(720,1280),error,.8),error.c_str());
	runtime.Frame(RemasteredViewport(720,1280),.9);
	Check(runtime.GetBounds("ammo-gauge",ammo) && ammo.x > 0 && ammo.x+ammo.width < 720,"the status bar fits the portrait width");
	// The Strogg gauge rect (419 + 67 u) overhangs the canvas below its art.
	Check(runtime.GetBounds("armor-gauge",armor) && armor.y+armor.height <= 1280+(strogg ? 6*1.5f*.75f+1 : 1) && armor.y > 1000,
		"gauges ride the portrait bottom edge");

	// HUD scale grows each group about its anchor: the bottom edge holds.
	Check(runtime.SetState(RemasteredLayout(1280,720,1.5f),error,1.0),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),1.05);
	Bounds scaled;
	// Scaled about the bottom-centre anchor point (480, 720 dp of its box):
	// with the box centre at (480, 360) the compensating translation is
	// (0, (720 - 360)(1 - 1.5)).
	const auto bottom = runtime.PresentedValue("anchor-bc","transform");
	Check(bottom && Near(float(bottom->data[0]),0,.01f) && Near(float(bottom->data[1]),-180,.01f) &&
		Near(float(bottom->data[2]),1.5f,.001f) && Near(float(bottom->data[3]),1.5f,.001f),
		"HUD scale grows the status bar about the bottom edge");
	const auto corner2 = runtime.PresentedValue("anchor-tr","transform");
	Check(corner2 && Near(float(corner2->data[0]),-240,.01f) && Near(float(corner2->data[1]),180,.01f),
		"HUD scale grows trailing groups about their corner");
	(void)scaled;
	Check(runtime.SetState(RemasteredLayout(1280,720),error,1.1),error.c_str());

	// The weapon strip shows on a change, after the first weapon.
	Check(runtime.SetState({{"weapon0",true},{"weapon0_index",0.0},{"weapon5",true},{"weapon5_index",1.0},{"weapon5_ammo",86.0},
		{"weapon0_ammo",-1.0},{"weaponcount",2.0},{"player_idealweapon",5.0},{"player_lastweapon",0.0},{"weaponname",std::string("NAILGUN")}},error,1.2),error.c_str());
	Check(runtime.RunEvent("weaponChange",1.2,effects,error),"weapon change runs");
	runtime.Frame(RemasteredViewport(1280,720),1.3);
	Bounds slot0, slot5;
	Check(runtime.GetBounds("slot0",slot0) && runtime.GetBounds("slot5",slot5) && slot0.width > 0,"strip slots appear on a change");
	Check(Near(slot5.x-slot0.x,30*1.5f),"slots sit on a 30 u pitch");
	Check(Near((slot0.x+slot5.x+26*1.5f)/2,640,1.5f),"the strip is centred");
	runtime.Frame(RemasteredViewport(1280,720),2.4);
	Check(!Shown(runtime,"strip"),"the strip hides 1000 ms after the change");

	// Pickups: a repeat merges with a count; a different item adds a line.
	Check(runtime.SetState({{"itemtext",std::string("Armor Shard")},{"itemicon",std::string("item_armorshard")}},error,2.5),error.c_str());
	Check(runtime.RunEvent("itemPickup",2.5,effects,error) && runtime.RunEvent("itemPickup",2.55,effects,error),"pickups run");
	runtime.Frame(RemasteredViewport(1280,720),2.6);
	auto count = runtime.PresentedValue("pickup0-count","text");
	auto count1 = runtime.PresentedValue("pickup1-count","text");
	Check((count && count->text == "2") || (count1 && count1->text == "2"),"a repeated pickup merges into one line with a count");
	Check(runtime.SetState({{"itemtext",std::string("Large Health")},{"itemicon",std::string("item_health")}},error,2.7),error.c_str());
	Check(runtime.RunEvent("itemPickup",2.7,effects,error),"second pickup");
	runtime.Frame(RemasteredViewport(1280,720),2.8);
	Bounds p0, p1;
	Check(runtime.GetBounds("pickup0",p0) && runtime.GetBounds("pickup1",p1) && std::abs(p0.y-p1.y) > 30,"two pickup lines stack");
	runtime.Frame(RemasteredViewport(1280,720),6.5);
	Check(!Shown(runtime,"pickup0") && !Shown(runtime,"pickup1"),"pickup lines leave after 3250 ms");

	// Low health switches the health colours and pulses the rim.
	Check(runtime.SetState({{"player_health",20.0},{"player_healthpct",.2},{"player_healthDelta",80.0}},error,7),error.c_str());
	Check(runtime.RunEvent("updateHealth",7,effects,error),"update health");
	runtime.Frame(RemasteredViewport(1280,720),7.1);
	Check(Shown(runtime,"health-rim-on") && Shown(runtime,"health-fill-low") && !Shown(runtime,"health-fill-sp"),"low health shows the rim and red fill");

	// The hit indicator: 0.70 to nothing over 500 ms, rotated toward the source.
	Check(runtime.SetState({{"hitdir",90.0}},error,7.2),error.c_str());
	Check(runtime.RunEvent("playerHit",7.2,effects,error),"player hit");
	host.draws.clear();
	runtime.Frame(RemasteredViewport(1280,720),7.24);
	const auto fade = runtime.PresentedValue("hit-fade","opacity");
	const auto turn = runtime.PresentedValue("hit","transform");
	if (fade && turn) std::fprintf(stderr,"hit-fade opacity %.3f, hit rotation %.1f, draws %zu, composites %zu\n",fade->data[0],turn->data[4],host.draws.size(),host.compositeOpacities.size());
	Check(fade && fade->data[0] > .55 && fade->data[0] < .7,"the hit indicator flashes from 0.70");
	Check(turn && Near(float(turn->data[4]),-90,.01f),"the hit indicator turns toward the source");
	runtime.Frame(RemasteredViewport(1280,720),7.8);
	Check(runtime.PresentedValue("hit-fade","opacity")->data[0] < .01,"the hit indicator is gone after 500 ms");

	// The HUD settings (13.9): the damage direction, the pickup messages and
	// the weapon strip can be turned off, and the opacity fades the whole HUD.
	host.cvars["ui_hudDamageDirection"] = false;
	host.cvars["ui_hudPickups"] = false;
	host.cvars["ui_hudWeaponStrip"] = false;
	host.cvars["ui_hudOpacity"] = 0.5;
	Check(runtime.RunEvent("weaponChange",7.82,effects,error) && runtime.RunEvent("itemPickup",7.82,effects,error),"a change and a pickup run");
	runtime.Frame(RemasteredViewport(1280,720),7.84);
	const auto rootOpacity = runtime.PresentedValue("hud-root","opacity");
	Check(!Shown(runtime,"hit") && !Shown(runtime,"pickups") && !Shown(runtime,"strip") && rootOpacity &&
		Near(float(rootOpacity->data[0]),.5f,.001f),"the HUD settings hide their groups and fade the HUD");
	host.cvars["ui_hudDamageDirection"] = true;
	host.cvars["ui_hudPickups"] = true;
	host.cvars["ui_hudWeaponStrip"] = true;
	host.cvars["ui_hudOpacity"] = 1.0;
	runtime.Frame(RemasteredViewport(1280,720),7.86);
	Check(Shown(runtime,"hit") && Shown(runtime,"pickups") && Near(float(runtime.PresentedValue("hud-root","opacity")->data[0]),1),
		"the groups and the full opacity return");
	// Reduced flashing (13.10, 14.7) caps the dome and its chevron at 0.40.
	host.cvars["ui_reducedFlashing"] = true;
	Check(runtime.RunEvent("playerHit",7.865,effects,error),"a hit under reduced flashing runs");
	runtime.Frame(RemasteredViewport(1280,720),7.866);
	{
		const auto peak = runtime.PresentedValue("hit-fade","opacity");
		const auto disc = runtime.PresentedValue("hit-dome-disc","opacity");
		const auto mark = runtime.PresentedValue("hit-chevron","opacity");
		Check(peak && disc && mark && peak->data[0] * disc->data[0] <= .401 && peak->data[0] * disc->data[0] > .35 &&
			Near(float(disc->data[0]),float(mark->data[0])),"reduced flashing caps the hit indicator at 0.40");
	}
	host.cvars["ui_reducedFlashing"] = false;
	runtime.Frame(RemasteredViewport(1280,720),7.868);
	Check(Near(float(runtime.PresentedValue("hit-dome-disc","opacity")->data[0]),1),"the full indicator returns without reduced flashing");

	// A ride (the flatbed, the tram) replaces the three gauges with its silhouette gauge.
	const char* healthKey = strogg ? "tram_healthpct" : "vehicle_health";
	Check(runtime.SetState({{healthKey,0.5}},error,7.9),error.c_str());
	Check(runtime.RunEvent(strogg ? "enterTram" : "enterFlatbed",7.9,effects,error),"boarding a ride runs");
	runtime.Frame(RemasteredViewport(1280,720),7.95);
	Check(Shown(runtime,"ride") && !Shown(runtime,"health-gauge") && !Shown(runtime,"ammo-gauge"),"a ride's gauge replaces the gauges");
	Bounds ride;
	Check(runtime.GetBounds("ride",ride) && Near(ride.x+ride.width/2,640,1.5f),"the ride gauge centres on the bottom edge");
	const auto rideShift = runtime.PresentedValue("ride-win","transform");
	Check(rideShift && Near(float(rideShift->data[0]),(strogg ? 119-105*.5f : 120-115*.5f)*1.5f,.1f),"the silhouette fills to half from the trailing end");
	Check(runtime.RunEvent(strogg ? "leaveTram" : "leaveFlatbed",8,effects,error),"leaving the ride runs");
	runtime.Frame(RemasteredViewport(1280,720),8.05);
	Check(!Shown(runtime,"ride") && Shown(runtime,"health-gauge"),"leaving restores the gauges");

	// Minimal (section 13.9): the gauges stay where they are; the strip,
	// pickups, radio, boss bar, notices and damage direction go.
	RemasteredLayoutPreference(host,2);
	runtime.Frame(RemasteredViewport(1280,720),8.1);
	Bounds minimalHealth;
	Check(Shown(runtime,"health-gauge") && Shown(runtime,"ammo-gauge") && runtime.GetBounds("health-gauge",minimalHealth) &&
		Near(minimalHealth.x,health.x) && Near(minimalHealth.y,health.y),"Minimal keeps the gauges in place");
	Check(!Shown(runtime,"bc-full") && !Shown(runtime,"tr-full") && !Shown(runtime,"tc-full") && !Shown(runtime,"cc-full"),
		"Minimal leaves out the strip, pickups, radio, boss bar, notices and damage direction");
	RemasteredLayoutPreference(host,1);
	runtime.Frame(RemasteredViewport(1280,720),8.15);
	Check(Shown(runtime,"bc-full") && Shown(runtime,"tr-full") && Shown(runtime,"tc-full") && Shown(runtime,"cc-full"),
		"Remastered brings them back");
	// Competitive (13.9) is a multiplayer preset: there the status bar grows by
	// 15% about the bottom edge; single player keeps the Remastered size.
	RemasteredLayoutPreference(host,3);
	runtime.Frame(RemasteredViewport(1280,720),8.16);
	const auto single = runtime.PresentedValue("anchor-bc","transform");
	Check(single && Near(float(single->data[2]),1,.001f) && Shown(runtime,"bc-full"),"single player keeps the Remastered status bar");
	Check(runtime.SetState({{"mp",true}},error,8.17),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),8.18);
	const auto enlarged = runtime.PresentedValue("anchor-bc","transform");
	Check(enlarged && Near(float(enlarged->data[2]),1.15f,.001f) && Near(float(enlarged->data[1]),360*(1-1.15f),.05f),
		"Competitive grows the multiplayer status bar by 15% about the bottom edge");
	Check(runtime.SetState({{"mp",false}},error,8.19),error.c_str());
	RemasteredLayoutPreference(host,1);
	runtime.Frame(RemasteredViewport(1280,720),8.2);

	// Alarms (14.10): the carried flag beats 22 to 28 u in 150 ms and the exit
	// chevron rises; reduced motion holds the flag at 28 u and the chevron
	// still at 0.50.
	if (id.find(".awakening") == std::string::npos && !strogg) {
		Check(runtime.SetState({{"mp",true},{"powerup_flag_visible",1.0}},error,8.21),error.c_str());
		Check(runtime.RunEvent("oq4_carriedFlag",8.21,effects,error) && runtime.RunEvent("showExit",8.21,effects,error),
			"the carried flag and the exit run");
		runtime.Frame(RemasteredViewport(1280,720),8.36);
		const auto grown = runtime.PresentedValue("pu-flag-beat","transform"), risen = runtime.PresentedValue("exit-chevron","transform");
		Check(Shown(runtime,"pu-flag") && grown && Near(float(grown->data[2]),28/22.f,.02f),"the carried flag grows to 28 u in 150 ms");
		Check(Shown(runtime,"exit") && risen && float(risen->data[1]) < -.2f,"the exit chevron rises");
		host.reducedMotion = true;
		runtime.SetReducedMotion(true,8.37);
		runtime.Frame(RemasteredViewport(1280,720),8.5);
		const auto still = runtime.PresentedValue("pu-flag-beat","transform"), peak = runtime.PresentedValue("pu-flag-held","transform");
		const auto rest = runtime.PresentedValue("exit-chevron","transform"), faint = runtime.PresentedValue("exit-chevron","opacity");
		Check(still && Near(float(still->data[2]),1) && peak && Near(float(peak->data[2]),28/22.f,.001f),
			"reduced motion holds the carried flag at 28 u");
		Check(rest && Near(float(rest->data[1]),0) && faint && Near(float(faint->data[0]),.5f,.01f),
			"reduced motion leaves the exit chevron still at 0.50");
		host.reducedMotion = false;
		runtime.SetReducedMotion(false,8.55);
		Check(runtime.SetState({{"mp",false},{"powerup_flag_visible",0.0}},error,8.56),error.c_str());
		Check(runtime.RunEvent("oq4_carriedFlag",8.56,effects,error) && runtime.RunEvent("hideExit",8.56,effects,error),
			"the carried flag and the exit leave");
		runtime.Frame(RemasteredViewport(1280,720),8.6);
		Check(!Shown(runtime,"pu-flag") && !Shown(runtime,"exit"),"the carried flag and the exit go");
	}

	// The Awakening's m03 script also sends the ship panel to the player's HUD.
	if (id.find(".awakening") != std::string::npos) {
		Check(!Shown(runtime,"ship"),"the ship panel waits for the script");
		Check(runtime.RunEvent("ship_target_01_on",8.7,effects,error),"a marker reaches the player's HUD");
		runtime.Frame(RemasteredViewport(1280,720),8.75);
		Bounds cone;
		Check(Shown(runtime,"ship") && Shown(runtime,"ship-target1") && !runtime.GetBounds("ship-cone",cone),
			"on foot the panel appears without the gun's view cone");
	}
	// Independent readouts: reloading grows the clip and leaves reserve still.
	Check(runtime.SetState({{"mp",false}},error,9),error.c_str());
	Check(runtime.RunEvent("ammoClipGain",9,effects,error),"clip gain runs");
	runtime.Frame(RemasteredViewport(1280,720),9.02);
    const float reserveSize=72*(strogg?.26f:.32f);
    const auto clipPop=runtime.PresentedValue("ammo-clip","font-size"), reserveStill=runtime.PresentedValue("ammo-reserve","font-size");
    Check(clipPop && clipPop->data[0]>36*1.15 && reserveStill && Near(float(reserveStill->data[0]),reserveSize,.001f),
        "clip gain grows only the clip's original textscale");
    runtime.Frame(RemasteredViewport(1280,720),9.3);
    Check(Near(float(runtime.PresentedValue("ammo-clip","font-size")->data[0]),36,.001f),"clip gain settles after 250 ms");
    Check(runtime.RunEvent("ammoReserveGain",9.31,effects,error),"reserve gain runs");
    runtime.Frame(RemasteredViewport(1280,720),9.33);
    Check(runtime.PresentedValue("ammo-reserve","font-size")->data[0]>reserveSize*1.15 &&
        Near(float(runtime.PresentedValue("ammo-clip","font-size")->data[0]),36,.001f),"reserve gain leaves the clip still");
    runtime.Frame(RemasteredViewport(1280,720),9.6);
    Check(runtime.RunEvent("ammoPulse",9.61,effects,error),"legacy inventory pulse remains accepted");
    runtime.Frame(RemasteredViewport(1280,720),9.63);
    Check(Near(float(runtime.PresentedValue("ammo-reserve","font-size")->data[0]),reserveSize,.001f) &&
        Near(float(runtime.PresentedValue("ammo-clip","font-size")->data[0]),36,.001f),"unrelated pickup does not pop the active readout");
    host.legacyRequests.clear();
    Check(runtime.SetState({{"player_health",1234.0},{"player_armor",57.0},{"oq4.weaponImage",std::string("gfx/guis/hud/icons/gun_machinegun")}},error,9.7),error.c_str());
    runtime.Frame(RemasteredViewport(1280,720),9.7);
    bool legacyHealth=false;
    for (const auto& r:host.legacyRequests) if (r.text=="1234") {
        Check(r.clip,"legacy health windows clip their glyphs");
        legacyHealth=r.family==(strogg?"r_strogg":"chain") && Near(r.scale,.5f,.001f) &&
            Near(r.width,strogg?60.f:52.f,.1f) && Near(r.height,26.f,.1f) && r.align==2;
    }
    Check(legacyHealth,"the complete uncapped number reaches the original font, textscale, window and trailing alignment");
    host.legacyRequests.clear();
    runtime.Frame(RemasteredViewport(1280,720),9.7);
    Check(std::none_of(host.legacyRequests.begin(),host.legacyRequests.end(),[](const auto& r){return r.text=="1234";}),
        "unchanged legacy readouts reuse their glyph layout");
    Check(runtime.SetState({{"player_health",1235.0}},error,9.8),error.c_str());
    runtime.Frame(RemasteredViewport(1280,720),9.8);
    Check(std::any_of(host.legacyRequests.begin(),host.legacyRequests.end(),[](const auto& r){return r.text=="1235";}),
        "a changed readout lays out its new glyphs immediately");
    Check(runtime.PresentedValue("ammo-weapon-bitmap","image")->text=="gfx/guis/hud/icons/gun_machinegun",
        "the ammo backdrop keeps the original bitmap source");
	// Compare actual laid-out labels when the boss and every notice coexist.
	Check(runtime.SetState({{"oq4.boss",true},{"oq4.saved",true},{"oq4.missile",true},{"oq4.exit",true}},error,10),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),10.02);
	Bounds exitLabel,savedLabel,missileLabel;
	Check(runtime.GetBounds("exit-text",exitLabel) && runtime.GetBounds("saved-text",savedLabel) && runtime.GetBounds("missile-text",missileLabel),
		"simultaneous notices lay out");
	// GetBounds reports layout boxes before their parent's paint transform.
	exitLabel.y+=float(runtime.PresentedValue("exit","transform")->data[1]);
	savedLabel.y+=float(runtime.PresentedValue("saved","transform")->data[1]);
	missileLabel.y+=float(runtime.PresentedValue("missile","transform")->data[1]);
	Check(exitLabel.y>89.2f*1.5f && savedLabel.y>=exitLabel.y+exitLabel.height && missileLabel.y>=savedLabel.y+savedLabel.height,
		"boss, exit, save and missile notices occupy separate rows");
	Check(host.errors == 0,"no runtime diagnostics across the HUD exercise");
	runtime.Shutdown();
}

// The multiplayer peer (ui-visual-design.md 14.9): the stock mphud keys and
// events plus the game's Remastered fields drive the kill feed, standing,
// team panels, timer, center column, chat, vote and follow card.
static void CheckRemasteredMpHud(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%s %u:%u %s\n",path,unsigned(d.line),unsigned(d.column),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.hud.remastered.mp","Remastered MP HUD validates");
	RemasteredLayoutPreference(host,1);
	Runtime runtime(host);
	Check(runtime.Initialize(),"retained runtime");
	Check(runtime.LoadDocument(source,"guis/hud/remastered_mp.q4ui",diagnostics),"Remastered MP HUD loads into a runtime");
	std::string error;
	Runtime::EventEffects effects;
	StateValues values = RemasteredLayout(1280,720);
	values.insert({{"gametype",1.0},{"oq4_state",3.0},{"oq4_timed",true},{"oq4_clock_warn",0.0},{"oq4_clock",std::string("7:05")}});
	Check(runtime.SetState(values,error,0),error.c_str());
	Check(runtime.RunEvent("hudJoin",0,effects,error),"the join program runs");
	runtime.Frame(RemasteredViewport(1280,720),.1);
	Check(host.errors == 0,"the MP HUD frames without runtime errors");

	// Kill feed: trailing at 637 u in the top-trailing anchor (box left 320 dp at 16:9).
	Check(runtime.SetState({{"oq4_feed0_live",true},{"oq4_feed0_alpha",1.0},{"oq4_feed0_attacker",std::string("Vex")},
		{"oq4_feed0_victim",std::string("Kaz")},{"oq4_feed0_weapon",7.0},{"oq4_feed0_kind",0.0},{"oq4_feed0_hasAttacker",true}},error,.2),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.25);
	Bounds line;
	Check(Shown(runtime,"feed0") && runtime.GetBounds("feed0-c",line) && Near(line.x+line.width,320+637*1.5f,1.5f),
		"a kill feed line trails at 633 u on its plate");
	Check(Shown(runtime,"feed0-w7") && !Shown(runtime,"feed0-w6") && Shown(runtime,"feed0-a") && Shown(runtime,"feed0-plate") &&
		!Shown(runtime,"feed0-band"),"the line shows the attacker and the weapon on the dark plate");
	Check(Near(line.y,(24-11.5f)*1.5f,1.f),"the first line sits at the stock baseline");
	Check(runtime.SetState({{"oq4_feed0_kind",1.0},{"oq4_feed0_hasAttacker",false},{"oq4_feed0_you",true},{"oq4_feed0_youVictim",true}},error,.3),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.35);
	Check(Shown(runtime,"feed0-skull") && !Shown(runtime,"feed0-w7") && !Shown(runtime,"feed0-a"),"a suicide shows the skull and no attacker");
	Check(Shown(runtime,"feed0-band") && Shown(runtime,"feed0-marker") && !Shown(runtime,"feed0-plate"),"your line uses the band plate and the marker");
	const auto victimColour = runtime.PresentedValue("feed0-v","color");
	Check(victimColour && Near(float(victimColour->data[0]),1) && Near(float(victimColour->data[2]),1),"your name is white");
	Check(runtime.SetState({{"gametype",2.0}},error,.4),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.45);
	Check(runtime.GetBounds("feed0-c",line) && Near(line.y,(70-11.5f)*1.5f,1.f),"the Tourney feed sits under the strip");
	// Tourney's message bar, its timer and the ROUND and ARENA plate sit at the
	// top of the center screen under the strip; the standing's timer gives way.
	Check(runtime.SetState({{"oq4_tourney_round",std::string("ROUND 2")},{"oq4_mb_sub",std::string("ARENA 3")},
		{"oq4_tourney_label",std::string("Quarter Finals")},{"spectatetext0",std::string("Arena 3; old round wording")}},error,.42),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.46);
	Bounds tbar, tunder;
	const auto troundText = runtime.PresentedValue("tplace-round","text"), tarenaText = runtime.PresentedValue("tplace-arena","text");
	Check(Shown(runtime,"tourney-bar") && runtime.GetBounds("tourney-bar",tbar) && Near(tbar.x,160,1.f) && Shown(runtime,"tourney-under") &&
		runtime.GetBounds("tourney-under",tunder) && Near(tunder.x,160+6*1.5f,1.f) && Near(tunder.y,58*1.5f,1.f) && !Shown(runtime,"timer"),
		"Tourney's bar and timer sit in the center screen under the strip");
	Check(troundText && troundText->text == "ROUND 2" && tarenaText && tarenaText->text == "ARENA 3" && Shown(runtime,"ttimer-c"),
		"the plate names the round and the arena beside the timer");
	Check(runtime.PresentedValue("tourney-bar-text","text")->text=="Quarter Finals",
		"the Tourney bar uses the banner's round wording instead of the stock arena description");
	Check(runtime.SetState({{"oq4_state",4.0},{"oq4_countdown",false},{"oq4_clock_warn",0.0},
		{"spectatetext1",std::string("Join arena")},{"spectatetext2",std::string("Cycle arenas")}},error,.47),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.475);
	Check(Shown(runtime,"ttimer-sd") && Shown(runtime,"ttimer-inf") && !Shown(runtime,"ttimer-clock") && !Shown(runtime,"ttimer-urgent"),
		"arena sudden death shows infinity rather than an expired countdown");
	Check(Shown(runtime,"tourney-help1") && Shown(runtime,"tourney-help2"),"Tourney keeps join and arena-cycle instructions");
	Check(runtime.SetState({{"oq4_state",2.0},{"oq4_countdown",true},{"oq4_tourney_label",std::string("")},
		{"spectatetext0",std::string("STARTING GAME IN 4")}},error,.476),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.477);
	Check(!Shown(runtime,"tourney-bar") && Shown(runtime,"ttimer-starts"),"the Tourney countdown appears only in the timer");
	const std::string longHelp="Use the tournament menu to choose another arena and follow its players while you wait for the next round. "
		"This deliberately extended localized instruction must keep every word readable and move the following instruction down to a separate row.";
	Check(runtime.SetState({{"spectatetext1",longHelp},{"spectatetext2",longHelp}},error,.478),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.4785);
	Bounds helpBlock, helpFirst, helpSecond;
	Check(runtime.GetBounds("tourney-help",helpBlock) && runtime.GetBounds("tourney-help1",helpFirst) &&
		runtime.GetBounds("tourney-help2",helpSecond) && Near(helpBlock.x,160+8*1.5f,1.f) && Near(helpBlock.width,624*1.5f,1.f) &&
		helpBlock.y>=tunder.y+tunder.height && helpFirst.height>13*1.5f && helpSecond.height>13*1.5f &&
		helpSecond.y>=helpFirst.y+helpFirst.height-.5f && helpSecond.y+helpSecond.height<=helpBlock.y+helpBlock.height+.5f,
		"long Tourney instructions wrap across the center width in separate rows below the timer");
	const auto helpOverflow=runtime.PresentedValue("tourney-help2","text-overflow");
	Check(runtime.PresentedValue("tourney-help2","text")->text==longHelp && (!helpOverflow || helpOverflow->text!="ellipsis"),
		"Tourney instructions retain all localized words without ellipsis");
	Check(runtime.SetState({{"spectatetext1",std::string("")}},error,.479),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.4795);
	Check(!Shown(runtime,"tourney-help1") && runtime.GetBounds("tourney-help2",helpSecond) && Near(helpSecond.y,helpBlock.y,.5f),
		"an absent first instruction leaves no blank row before the second");
	Check(runtime.SetState({{"oq4_state",1.0},{"oq4_countdown",false},{"showready",true},{"oq4_mb_ready",std::string("READY 3/8")}},error,.48),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.485);
	Check(Shown(runtime,"ttimer-ready") && runtime.PresentedValue("ttimer-ready","text")->text=="READY 3/8","warm-up uses the compact ready count");
	Check(runtime.SetState({{"oq4_mb_ready",std::string("")}},error,.49),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.495);
	Check(!Shown(runtime,"ttimer-ready"),"a bot-only warm-up does not show READY 0/0");
	Check(runtime.SetState({{"oq4_state",3.0},{"showready",false}},error,.499),error.c_str());
	Check(runtime.SetState({{"gametype",1.0},{"oq4_feed0_live",false}},error,.5),error.c_str());

	// Standing: the leader and you, your gap, the limit.
	Check(runtime.SetState({{"player1_name",std::string("Sarge")},{"player1_score",std::string("24")},{"oq4_rank1",std::string("1ST")},
		{"player2_name",std::string("You")},{"player2_score",std::string("19")},{"oq4_rank2",std::string("3RD")},{"oq4_row2_you",true},
		{"oq4_limit",30.0},{"oq4_gap",-5.0},{"oq4_gap_show",true}},error,.6),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.65);
	Bounds row1, row2;
	Check(Shown(runtime,"standing") && runtime.GetBounds("standing1",row1) && runtime.GetBounds("standing2",row2) &&
		Near(row1.x,6*1.5f) && Near(row1.y,36*1.5f) && Near(row2.y-row1.y,21*1.5f),"the standing plates sit from y 36, 3 u apart");
	Check(Shown(runtime,"standing2-band") && Shown(runtime,"standing2-marker") && !Shown(runtime,"standing1-band"),"your row carries the band and marker");
	Check(Shown(runtime,"standing2-gap") && !Shown(runtime,"standing1-gap") && Shown(runtime,"standing1-limit"),"your gap and the limit show");
	const auto sign = runtime.PresentedValue("standing2-sign","text");
	Check(sign && sign->text == "#str_231224","a deficit reads with the minus sign key");
	const auto lead = runtime.PresentedValue("standing1-rank","color");
	Check(lead && Near(float(lead->data[0]),1) && Near(float(lead->data[2]),0x23/255.f,.01f),"the leader ordinal is #FFBE23");
	Bounds clock;
	Check(runtime.GetBounds("timer",clock) && Near(clock.y,82*1.5f),"the timer sits under the standing");

	// Team panels: your team first.
	Check(runtime.SetState({{"gametype",4.0},{"oq4_team_game",true},{"oq4_my_team",1.0},{"marine_score",3.0},{"strogg_score",2.0},
		{"oq4_flag0_state",1.0},{"oq4_flag0_carrier",std::string("Vex")},{"oq4_flag0_carrier_team",1.0}},error,.7),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.75);
	Bounds marine, strogg;
	Check(Shown(runtime,"teams") && !Shown(runtime,"standing") && runtime.GetBounds("team0",marine) && runtime.GetBounds("team1",strogg) &&
		Near(strogg.y,36*1.5f) && Near(marine.y,61*1.5f),"your team panel leads");
	Check(Shown(runtime,"team0-flag-pulse") && Shown(runtime,"team0-flag-bang") && Shown(runtime,"team0-carrier") &&
		Shown(runtime,"team1-flag-base"),"a taken flag pulses under ! with its carrier");
	Check(runtime.GetBounds("timer",clock) && Near(clock.y,90*1.5f),"the timer sits under the team panels");
	Check(runtime.SetState({{"gametype",10.0},{"showround",true},{"roundtext",std::string("ROUND 2")},
		{"roundtime",std::string("1:12")},{"showroundtime",false}},error,.76),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.77);
	Check(Shown(runtime,"round-c") && Shown(runtime,"round-clock") && runtime.PresentedValue("round-clock","text")->text=="1:12",
		"the entire timed round has a clock even before Classic's last-thirty-second flag");
	Check(runtime.SetState({{"roundtime",std::string("")}},error,.78),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.79);
	Check(!Shown(runtime,"round-clock"),"an untimed or inactive round has no stale countdown");
	Check(runtime.SetState({{"gametype",4.0},{"showround",false}},error,.795),error.c_str());

	// Timer: the last minute in #FFBE23, the last ten seconds pulsing #E46D56.
	Check(runtime.SetState({{"oq4_clock_warn",1.0},{"oq4_clock",std::string("0:42")}},error,.8),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.85);
	const auto minute = runtime.PresentedValue("timer-clock","color");
	Check(Shown(runtime,"timer-clock") && minute && Near(float(minute->data[2]),0x23/255.f,.01f),"the last minute reads #FFBE23");
	Check(runtime.SetState({{"oq4_clock_warn",2.0},{"oq4_clock",std::string("0:08")}},error,.9),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.95);
	Check(Shown(runtime,"timer-urgent") && !Shown(runtime,"timer-clock"),"the last ten seconds pulse");
	Check(runtime.SetState({{"oq4_state",1.0},{"showready",true},{"oq4_mb_ready",std::string("READY 3/8")}},error,1.0),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),1.05);
	Check(Shown(runtime,"timer-warm") && Shown(runtime,"timer-ready") && !Shown(runtime,"timer-clock"),"warm-up shows the ready count");
	Check(runtime.SetState({{"oq4_state",3.0}},error,1.1),error.c_str());

	// Center column: notices stack and leave after 2500 ms.
	Check(runtime.SetState({{"frag_notice_text",std::string("You fragged Vex")},{"main_notice_text",std::string("3rd place with 19")},
		{"oq4_rank_notice_text",std::string("3rd place with 19")},{"oq4_rank_notice_lead",std::string("3rd")},
		{"oq4_rank_notice_post",std::string(" place with 19")}},error,1.2),error.c_str());
	Check(runtime.RunEvent("frag_notice",1.2,effects,error) && runtime.RunEvent("main_notice",1.2,effects,error),"notices run");
	runtime.Frame(RemasteredViewport(1280,720),1.25);
	const auto sliding = runtime.PresentedValue("center-b1","transform");
	Check(sliding && float(sliding->data[1]) < -2 && float(sliding->data[1]) > -(17.3f*1.35f+3)*1.5f,
		"the lines under a new frag notice slide down into place");
	runtime.Frame(RemasteredViewport(1280,720),1.3);
	Bounds frag, main;
	Check(Shown(runtime,"frag") && Shown(runtime,"main") && runtime.GetBounds("frag",frag) && runtime.GetBounds("main",main) &&
		main.y >= frag.y+frag.height,"the main notice stacks under the frag notice");
	Check(Shown(runtime,"main-lead") && !Shown(runtime,"main-text"),"a rank notice sets its ordinal apart");
	Check(Near(frag.x+frag.width/2,640,1.5f),"notices are centred");
	runtime.Frame(RemasteredViewport(1280,720),3.8);
	Check(!Shown(runtime,"frag") && !Shown(runtime,"main"),"notices leave after 2500 ms");

	// Chat: a line shows on its plate.
	Check(runtime.SetState({{"oq4_chat0_live",true},{"oq4_chat0_alpha",1.0},{"oq4_chat0_name",std::string("Kaz")},
		{"oq4_chat0_text",std::string("rail on the bridge")},{"oq4_chat0_kind",1.0},{"oq4_chat0_team",0.0},{"oq4_chat_count",1.0}},error,3.9),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),3.95);
	Bounds chat;
	Check(Shown(runtime,"chat0") && Shown(runtime,"chat0-tag") && runtime.GetBounds("chat0",chat) && Near(chat.x,8*1.5f,1.f) &&
		Near(chat.y+chat.height,720-59*1.5f,2.f),"a team chat line sits on the leading edge clear of the status bar");
	Check(!Shown(runtime,"chatin"),"no input at rest");
	StateValues wrappedChat;
	for (int i=0;i<4;++i) {
		const auto prefix=std::string("oq4_chat")+std::to_string(i);
		wrappedChat[prefix+"_live"]=true; wrappedChat[prefix+"_alpha"]=1.0;
		wrappedChat[prefix+"_text"]=std::string("A long chat message with enough words to wrap onto several lines in the narrow side screen. Repeated detail keeps each message well beyond one line.");
	}
	Check(runtime.SetState(wrappedChat,error,3.96),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),3.97);
	Bounds restedHistory;
	Check(runtime.GetBounds("chatlog",restedHistory) && restedHistory.height<=8*13.3f*1.5f+1,
		"wrapped messages stay within eight lines while chat input is closed");
	Check(runtime.SetState({{"oq4_chat1_live",false},{"oq4_chat2_live",false},{"oq4_chat3_live",false},
		{"oq4_chat0_text",std::string("rail on the bridge")}},error,3.98),error.c_str());


	// Vote and follow card.
	Check(runtime.SetState({{"voteNotice",1.0},{"voteInfo_1",std::string("Map: q4dm7")},{"oq4_vote_yes",3.0},{"oq4_spectating",true},
		{"oq4_following",true},{"oq4_follow_name",std::string("Vex")}},error,4.1),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),4.15);
	Check(Shown(runtime,"vote") && Shown(runtime,"vote-l1") && !Shown(runtime,"vote-l2"),"the vote card lists its fields");
	Check(runtime.SetState({{"oq4_vote_yes_key",std::string("F1")},{"oq4_vote_no_key",std::string("F2")}},error,4.151),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),4.152);
	Check(Shown(runtime,"vote-yes-chip") && Shown(runtime,"vote-no-chip") && runtime.PresentedValue("vote-yes-key","text")->text=="F1",
		"desktop vote bindings use labelled keycaps");
	Check(runtime.SetState({{"oq4_vote_yes_key.glyph",2.0},{"oq4_vote_no_key.glyph",3.0}},error,4.153),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),4.154);
	Check(Shown(runtime,"vote-yes-cross") && Shown(runtime,"vote-no-circle") && !Shown(runtime,"vote-yes-chip") && !Shown(runtime,"vote-no-chip"),
		"vote prompts follow the active controller glyph family");
	Check(runtime.SetState({{"oq4_vote_voted",true}},error,4.155),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),4.156);
	Check(!Shown(runtime,"vote-yes-glyph") && !Shown(runtime,"vote-no-glyph") && Shown(runtime,"vote-yes-cells"),
		"after voting the tallies remain and both device prompts disappear");
	Check(runtime.SetState({{"oq4_vote_voted",false},{"oq4_vote_yes_key.glyph",0.0},{"oq4_vote_no_key.glyph",0.0}},error,4.157),error.c_str());
	Check(Shown(runtime,"follow") && Shown(runtime,"follow-name") && !Shown(runtime,"spec-free"),"the follow card names the followed player");
	Check(runtime.SetState({{"oq4_key_prev",std::string("MWHEELDOWN")}},error,4.16),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),4.17);
	const auto prev = runtime.PresentedValue("pr-prev-key","text");
	Check(Shown(runtime,"pr-next") && Shown(runtime,"pr-prev") && Shown(runtime,"pr-free") && prev && prev->text == "MWHEELDOWN",
		"following, the prompts read Next, Previous and Free camera");

	// Free camera: name tags over the players in view, centred on their points.
	Check(runtime.SetState({{"oq4_following",false},{"oq4_tags",2.0},{"oq4_tag0_x",0.5},{"oq4_tag0_y",0.4},
		{"oq4_tag0_name",std::string("Nyx")},{"oq4_tag0_team",1.0},{"oq4_tag1_x",0.25},{"oq4_tag1_y",0.5},
		{"oq4_tag1_name",std::string("Orin")},{"oq4_tag1_team",-1.0}},error,4.18),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),4.19);
	Bounds tag, pointer;
	const auto tagName = runtime.PresentedValue("tag0-name","text");
	Check(Shown(runtime,"tag0") && Shown(runtime,"tag1") && !Shown(runtime,"tag2") && tagName && tagName->text == "Nyx",
		"the free camera tags the players in view");
	// The holder moves by transform (bounds are its layout box): the plate's
	// centre and the pointer's tip land on the projected point.
	const auto place = runtime.PresentedValue("tag0","transform");
	Check(place && runtime.GetBounds("tag0-plate",tag) && runtime.GetBounds("tag0-point",pointer) && Near(tag.height,13*1.5f,1.f) &&
		Near(float(place->data[0])+tag.x+tag.width/2,640,1.5f) && Near(float(place->data[1])+pointer.y+pointer.height,.4f*720,1.5f),
		"a tag centres over its player with its pointer's tip on the point");
	Check(Shown(runtime,"tag0-team1") && !Shown(runtime,"tag0-team0") && !Shown(runtime,"tag1-team0") && !Shown(runtime,"tag1-team1"),
		"a team tag carries its team's block; deathmatch carries none");
	Check(Shown(runtime,"spec-free") && !Shown(runtime,"follow-name"),"the card reads Free camera");
	Check(runtime.SetState({{"oq4_following",true}},error,4.195),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),4.197);
	Check(!Shown(runtime,"tag0"),"following, the tags go");

	// Respawn: the attack control's Respawn in the lower third, fading in as it
	// appears; every server delay keeps that control beside the seconds left.
	Check(runtime.SetState({{"oq4_spectating",false},{"oq4_following",false},{"oq4_respawn",1.0},
		{"oq4_key_attack",std::string("MOUSE1")}},error,4.2),error.c_str());
	Check(runtime.RunEvent("oq4_respawn",4.2,effects,error),"the respawn prompt appears");
	runtime.Frame(RemasteredViewport(1280,720),4.25);
	Bounds respawn;
	const auto rising = runtime.PresentedValue("respawn-c","opacity");
	Check(rising && float(rising->data[0]) < .5f,"the respawn prompt fades in");
	runtime.Frame(RemasteredViewport(1280,720),4.5);
	const auto risen = runtime.PresentedValue("respawn-c","opacity");
	Check(risen && Near(float(risen->data[0]),1),"the respawn prompt settles opaque");
	Check(Shown(runtime,"respawn") && Shown(runtime,"respawn-ready") && !Shown(runtime,"respawn-held") && !Shown(runtime,"follow"),
		"dead, the attack control's Respawn shows instead of the follow card");
	const auto key = runtime.PresentedValue("respawn-key","text");
	Check(key && key->text == "MOUSE1","the prompt names the attack control");
	Check(runtime.GetBounds("respawn-c",respawn) && Near(respawn.x+respawn.width/2,640,1.5f) && Near(respawn.y,318*1.5f,1.f) &&
		respawn.y+respawn.height < 720-51*1.5f,"the prompt uses the atlas's lower-third position, clear of the status bar");
	Check(runtime.SetState({{"oq4_respawn",2.0},{"oq4_respawn_in",4.0}},error,4.6),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),4.65);
	const auto seconds = runtime.PresentedValue("respawn-in","text");
	Check(Shown(runtime,"respawn-held") && Shown(runtime,"respawn-ready") && Shown(runtime,"respawn-glyph") && seconds && seconds->text == "4",
		"a held respawn keeps its control and Respawn label beside the countdown");
	Check(runtime.SetState({{"oq4_respawn",0.0}},error,4.7),error.c_str());
	Check(runtime.RunEvent("oq4_respawn",4.7,effects,error),"the respawn prompt leaves");
	runtime.Frame(RemasteredViewport(1280,720),4.75);
	Check(!Shown(runtime,"respawn"),"alive again, the prompt goes");

	// Minimal: the standings, timer, notices, kill feed and aim text go;
	// the vote, chat and the respawn prompt stay.
	RemasteredLayoutPreference(host,2);
	Check(runtime.SetState({{"oq4_respawn",1.0}},error,4.8),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),4.85);
	Check(!Shown(runtime,"tl-full") && !Shown(runtime,"tc-full") && !Shown(runtime,"tr-full") && !Shown(runtime,"cc-full"),
		"Minimal leaves out the standings, timer, notices, kill feed and aim text");
	Check(Shown(runtime,"vote") && Shown(runtime,"respawn") && Shown(runtime,"chatbox"),"Minimal keeps the vote, chat and the respawn prompt");
	RemasteredLayoutPreference(host,1);
	Check(runtime.SetState({{"oq4_respawn",0.0}},error,4.9),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),4.95);
	Check(Shown(runtime,"tl-full") && Shown(runtime,"tr-full"),"Remastered brings them back");

	// The live managed card uses one summary; warm-up/pause still show all
	// populated lines, without losing the record kept for the scoreboard.
	Check(runtime.SetState({{"match_context_visible",1.0},{"match_context_phase",std::string("LIVE")},
		{"match_context_role",std::string("Player")},{"match_context_series",std::string("Series 1-0")},
		{"match_context_timeouts",std::string("Timeouts 1/2")},{"match_context_proposal",std::string("Map proposal")},
		{"match_context_compact",false},{"match_context_summary",std::string("Series 1-0")}},error,4.951),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),4.952);
	Bounds expandedContext, compactContext;
	Check(runtime.GetBounds("ctx",expandedContext) && Shown(runtime,"ctx-series") && Shown(runtime,"ctx-timeouts") && !Shown(runtime,"ctx-summary"),
		"the expanded context retains its full localized record");
	Check(runtime.SetState({{"match_context_compact",true}},error,4.96),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),4.961);
	Check(runtime.GetBounds("ctx",compactContext) && compactContext.height<expandedContext.height && Near(compactContext.y,expandedContext.y) &&
		Shown(runtime,"ctx-phase") && Shown(runtime,"ctx-summary") && !Shown(runtime,"ctx-role") && !Shown(runtime,"ctx-series") &&
		!Shown(runtime,"ctx-timeouts") && !Shown(runtime,"ctx-proposal"),"live play collapses the card to phase and one summary in its existing slot");
	Check(runtime.PresentedValue("ctx-series","text")->text=="Series 1-0","collapsing the HUD does not erase the full record");
	Check(runtime.SetState({{"match_context_compact",false}},error,4.97),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),4.971);
	Check(Shown(runtime,"ctx-series") && Shown(runtime,"ctx-proposal") && !Shown(runtime,"ctx-summary"),"a pause or warm-up expands the record again");
	Check(runtime.SetState({{"match_context_visible",0.0}},error,4.98),error.c_str());

	// The input (atlas mpChat) opens at the box's bottom and pushes the lines,
	// now history, up: the prompts, then the field with the channel tag and
	// the entry, the caret between the text before and after it.
	Check(runtime.SetState({{"oq4_chatting",true},{"oq4_chatin_before",std::string("rail on")},{"oq4_chatin_after",std::string(" the bridge")},
		{"oq4_chatin_left",110.0},{"oq4_chatin_channels",true}},error,5.0),error.c_str());
	Check(runtime.RunEvent("oq4_chatType",5.0,effects,error),"typing restarts the caret");
	host.draws.clear();
	runtime.Frame(RemasteredViewport(1280,720),5.005);
	Bounds field, prompts, history, before, at, after, view;
	Check(Shown(runtime,"chatin") && Shown(runtime,"chat0") && Shown(runtime,"chatin-all") && !Shown(runtime,"chatin-team") &&
		Shown(runtime,"chatin-pr-send") && Shown(runtime,"chatin-pr-channel") && Shown(runtime,"chatin-pr-cancel") && !Shown(runtime,"chatin-count"),
		"the input opens under the history with the ALL tag and Send, Channel and Cancel");
	const auto sendKey = runtime.PresentedValue("chatin-pr-send-key-name","text");
	Check(sendKey && sendKey->text == "#str_107019","Send names the Enter key");
	Check(runtime.GetBounds("chatin-field",field) && runtime.GetBounds("chat0",history) && runtime.GetBounds("chatin-prompts",prompts) &&
		history.y+history.height <= prompts.y && prompts.y+prompts.height <= field.y && Near(field.height,18*1.5f,.5f),
		"the history, the prompts and the field stack in that order");
	Check(Near(field.y+field.height,720-59*1.5f,2.f),"the field takes the box's bottom: the box never lowers");
	Check(runtime.GetBounds("chatin-before",before) && runtime.GetBounds("chatin-at",at) && runtime.GetBounds("chatin-after",after) &&
		runtime.GetBounds("chatin-view",view) && Near(at.x,before.x+before.width,.5f) && Near(after.x,at.x,.5f) && Near(before.x,view.x,.5f) &&
		before.width > 10 && before.width < view.width-10,
		"a short entry starts at its view, the caret between the text before and after it");
	Bounds shownShort;
	Check(runtime.GetBounds("chatin-shown",shownShort) && Near(shownShort.x+shownShort.width,at.x,.5f),"the shown text ends at the caret");
	const auto blink = runtime.PresentedValue("chatin-caret","opacity");
	Check(Shown(runtime,"chatin-caret") && !Shown(runtime,"chatin-caret-steady") && blink && blink->data[0] > .99,"the caret starts its blink on");
	runtime.Frame(RemasteredViewport(1280,720),5.75);
	const auto blinked = runtime.PresentedValue("chatin-caret","opacity");
	Check(blinked && blinked->data[0] < .01,"the caret is off in the second half of its second");
	// A long entry scrolls sideways: the caret stays 3 u inside the view, the
	// view clips the text before it, and the characters left show.
	const std::string longEntry = "the quick brown fox jumps over the lazy dog while the railgun charges on the bridge above the lava "
		"and the quad waits";
	Check(runtime.SetState({{"oq4_chatin_before",longEntry},{"oq4_chatin_after",std::string("")},{"oq4_chatin_left",8.0},
		{"oq4.chatClipped",true}},error,5.76),error.c_str());
	host.draws.clear();
	runtime.Frame(RemasteredViewport(1280,720),5.77);
	Bounds probe, shown;
	Check(runtime.GetBounds("chatin-probe-text",probe) && runtime.GetBounds("chatin-view",view) && probe.width > view.width*1.2f,
		"the long entry outgrows its view");
	Check(runtime.GetBounds("chatin-at",at) && Near(at.x,view.x+view.width-3*1.5f,1.f),
		"a long entry scrolls to keep the caret 3 u inside its view");
	Check(runtime.GetBounds("chatin-shown",shown) && Near(shown.x+shown.width,at.x,.5f),"the shown text ends at the caret");
	float inkLeft = 1e9f, inkRight = -1e9f; std::size_t ink = 0;
	for (const auto& draw : host.draws) for (const auto& v : draw.vertices)
		if (v.y > field.y && v.y < field.y+field.height && v.a > .99f && std::abs(v.b-0x8D/255.f) < .02f && v.r > .99f) {
			inkLeft = std::min(inkLeft,v.x); inkRight = std::max(inkRight,v.x); ++ink;
		}
	Check(ink > 0 && inkLeft >= view.x-1 && inkRight <= at.x+1.5f && inkRight > at.x-12,
		"the view clips the scrolled text before it and shows its end at the caret");
	const auto count = runtime.PresentedValue("chatin-count","text");
	Check(Shown(runtime,"chatin-count") && count && count->text == "8","the characters left show past 80% of the limit");
	Check(Shown(runtime,"chatin-more") && Shown(runtime,"chatin-fade"),"text before the view fades under the mark");
	// The team channel takes your team's text color; overstrike underlines.
	Check(runtime.SetState({{"oq4_chatin_team",true},{"oq4_my_team",1.0},{"oq4_chatin_overstrike",true},{"oq4.chatClipped",false},
		{"oq4_chatin_left",0.0}},error,5.78),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),5.79);
	Bounds caret;
	const auto spent = runtime.PresentedValue("chatin-count","color");
	Check(Shown(runtime,"chatin-team") && Shown(runtime,"chatin-team-s") && !Shown(runtime,"chatin-team-m") && !Shown(runtime,"chatin-all"),
		"the team channel shows the TEAM tag in your team's color");
	Check(runtime.GetBounds("chatin-caret",caret) && caret.width > caret.height && !Shown(runtime,"chatin-more"),
		"overstrike turns the caret into an underline");
	Check(spent && Near(float(spent->data[0]),0xE4/255.f,.01f),"no characters left reads #E46D56");
	// Reduced motion holds the caret steady.
	host.reducedMotion = true;
	runtime.SetReducedMotion(true,5.795);
	runtime.Frame(RemasteredViewport(1280,720),5.8);
	Check(Shown(runtime,"chatin-caret-steady") && !Shown(runtime,"chatin-caret"),"reduced motion holds the caret steady");
	host.reducedMotion = false;
	runtime.SetReducedMotion(false,5.81);
	Check(runtime.SetState({{"oq4_chatting",false},{"oq4_chatin_team",false},{"oq4_chatin_overstrike",false},{"oq4_chatin_left",128.0},
		{"oq4_chatin_before",std::string("")}},error,5.85),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),5.9);
	Check(!Shown(runtime,"chatin"),"closing the input removes it");

	// Alarms (14.10). A taken flag beats 28 to 34 u once a second.
	Check(runtime.SetState({{"gametype",4.0},{"oq4_team_game",true},{"oq4_my_team",1.0},{"oq4_flag0_state",1.0},
		{"oq4_flag2_state",0.0}},error,5.95),error.c_str());
	Check(runtime.RunEvent("oq4_flags",5.95,effects,error),"a flag change runs");
	runtime.Frame(RemasteredViewport(1280,720),6.075);
	const auto beat = runtime.PresentedValue("team0-flag-pulse","transform");
	Check(Shown(runtime,"team0-flag-held") && beat && float(beat->data[2]) > 1.05f && float(beat->data[2]) < 1.2144f,
		"a taken flag beats toward 34 u");
	// One flag: on its carrier's panel the neutral flag swings to the carrier's
	// team color with the beat, there in 250 ms.
	Check(runtime.SetState({{"gametype",5.0},{"oq4_flag0_state",0.0},{"oq4_flag2_state",1.0},{"oq4_flag2_carrier",std::string("Nyx")},
		{"oq4_flag2_carrier_team",1.0}},error,6.1),error.c_str());
	Check(runtime.RunEvent("oq4_flags",6.1,effects,error),"the one flag's change runs");
	runtime.Frame(RemasteredViewport(1280,720),6.35);
	const auto wash = runtime.PresentedValue("team1-neutral-wash","opacity");
	Check(Shown(runtime,"team1-neutral-held") && !Shown(runtime,"team1-neutral-wash-rest") && wash && float(wash->data[0]) > .9f,
		"the one flag swings to its carrier's team color");
	// Reduced motion holds the flag at 34 u in the carrier's color.
	host.reducedMotion = true;
	runtime.SetReducedMotion(true,6.36);
	runtime.Frame(RemasteredViewport(1280,720),6.4);
	const auto still = runtime.PresentedValue("team1-neutral-pulse","transform"), peak = runtime.PresentedValue("team1-neutral-held","transform");
	Check(still && Near(float(still->data[2]),1) && peak && Near(float(peak->data[2]),1.2143f,.001f) &&
		Shown(runtime,"team1-neutral-wash-rest"),"reduced motion holds the one flag at 34 u in its carrier's color");
	// Dead Zone under reduced motion: a control tick holds the team's color for
	// a second instead of the game's relaxing pulse.
	Check(runtime.SetState({{"gametype",8.0},{"oq4_flag2_state",0.0},{"marine_pulse_amount",0.6},{"strogg_pulse_amount",0.0}},error,6.45),
		error.c_str());
	Check(runtime.RunEvent("oq4_zonePulse1",6.45,effects,error),"a control tick runs");
	runtime.Frame(RemasteredViewport(1280,720),6.5);
	const auto held = runtime.PresentedValue("team1-score","color"), calm = runtime.PresentedValue("team0-score","color");
	Check(held && Near(float(held->data[1]),0x8E/255.f,.01f) && calm && Near(float(calm->data[2]),1),
		"reduced motion: a tick holds the team's color and a fading pulse stays white");
	runtime.Frame(RemasteredViewport(1280,720),7.5);
	const auto released = runtime.PresentedValue("team1-score","color");
	Check(released && Near(float(released->data[1]),1),"the held color goes after a second");
	host.reducedMotion = false;
	runtime.SetReducedMotion(false,7.55);
	runtime.Frame(RemasteredViewport(1280,720),7.6);
	const auto pulsing = runtime.PresentedValue("team0-score","color");
	Check(pulsing && float(pulsing->data[2]) < .9f,"with motion the score follows the game's pulse");

	// Voice (14.9): your transmission on the band plate with a microphone, a
	// talker with the speaker symbol, each trace in its team's text color.
	Check(runtime.SetState({{"gametype",4.0},{"oq4_my_team",1.0},{"oq4_audio_team",0.0},{"audio_name",std::string("Kaz")}},error,7.65),
		error.c_str());
	Check(runtime.RunEvent("show_transmit_self",7.65,effects,error) && runtime.RunEvent("show_transmit",7.65,effects,error),"the talkers run");
	runtime.Frame(RemasteredViewport(1280,720),7.7);
	const auto mine = runtime.PresentedValue("voice-self-graph","image-color"), theirs = runtime.PresentedValue("voice-other-graph","image-color");
	const auto talker = runtime.PresentedValue("voice-other-name","text");
	Check(Shown(runtime,"voice-self") && Shown(runtime,"voice-other") && talker && talker->text == "Kaz","both talker plates show");
	Check(mine && Near(float(mine->data[1]),0x8E/255.f,.01f) && theirs && Near(float(theirs->data[1]),0xE3/255.f,.01f),
		"each trace takes its talker's team text color");
	Check(runtime.RunEvent("hide_transmit_self",7.75,effects,error) && runtime.RunEvent("hide_transmit",7.75,effects,error),"the talkers stop");
	runtime.Frame(RemasteredViewport(1280,720),7.8);
	Check(!Shown(runtime,"voice-self") && !Shown(runtime,"voice-other"),"the talker plates go");
	Check(host.errors == 0,"no runtime diagnostics across the MP HUD exercise");
	runtime.Shutdown();
}

// Competitive (ui_hudLayout 3; 13.9, 14.9): the match bar at the top center,
// network quality beside it, the team status at the leading edge, the vote in
// the top-leading corner and the chat clear of the enlarged status bar.
static void CheckRemasteredCompetitiveHud(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	std::vector<Diagnostic> diagnostics;
	RemasteredLayoutPreference(host,3);
	Runtime runtime(host);
	Check(runtime.Initialize(),"retained runtime");
	Check(runtime.LoadDocument(source,"guis/hud/remastered_mp.q4ui",diagnostics),"Remastered MP HUD loads for Competitive");
	std::string error;
	Runtime::EventEffects effects;
	StateValues values = RemasteredLayout(1280,720);
	values.insert({{"gametype",1.0},{"oq4_state",3.0},{"oq4_timed",true},{"oq4_clock_warn",0.0},{"oq4_clock",std::string("7:05")},
		{"oq4_mb_sub",std::string("TO 30")},{"oq4_mb0_name",std::string("You")},{"oq4_mb0_rank",std::string("2ND")},{"oq4_mb0_score",19.0},
		{"oq4_mb0_you",true},{"oq4_mb1_name",std::string("Sarge")},{"oq4_mb1_rank",std::string("1ST")},{"oq4_mb1_score",24.0},
		{"oq4_mb1_lead",true},{"oq4_ping",40.0},{"oq4_loss",0.0},{"player1_name",std::string("Sarge")},{"oq4_rank1",std::string("1ST")}});
	Check(runtime.SetState(values,error,0),error.c_str());
	Check(runtime.RunEvent("hudJoin",0,effects,error),"the join program runs");
	runtime.Frame(RemasteredViewport(1280,720),.1);
	Check(host.errors == 0,"Competitive frames without runtime errors");

	// The bar: three plates from y 6, 26 u tall, 2 u apart, a 60 u timer
	// between 122 u sides (the top-center box starts at 160 dp at 16:9).
	Bounds side0, timer, side1;
	Check(Shown(runtime,"matchbar") && !Shown(runtime,"tl-rem") && !Shown(runtime,"tc-strip"),
		"Competitive trades the standing, timer and strip for the match bar");
	Check(runtime.GetBounds("mb0-plate",side0) && runtime.GetBounds("mbt-plate",timer) && runtime.GetBounds("mb1-plate",side1),
		"the bar's plates are laid out");
	Check(Near(side0.x,160+166*1.5f) && Near(side0.width,122*1.5f) && Near(side0.y,6*1.5f) && Near(side0.height,26*1.5f) &&
		Near(timer.x,160+290*1.5f) && Near(timer.width,60*1.5f) && Near(side1.x,160+352*1.5f),"the sides flank the 60 u timer 2 u apart");
	// Deathmatch: the rank ordinal on the band, the leader's in #FFBE23, the
	// name and your marker; the scores at the inner ends in Chain.
	Check(Shown(runtime,"mb0-band") && Shown(runtime,"mb0-rank") && Shown(runtime,"mb0-name") && !Shown(runtime,"mb0-block0") &&
		!Shown(runtime,"mb0-block1") && !Shown(runtime,"mb0-team"),"deathmatch sides hold the rank and the name");
	Check(Shown(runtime,"mb0-marker") && !Shown(runtime,"mb1-marker"),"you lead with the marker");
	const auto leader = runtime.PresentedValue("mb1-rank","color"), you = runtime.PresentedValue("mb0-rank","color");
	Check(leader && Near(float(leader->data[2]),0x23/255.f,.01f) && you && Near(float(you->data[2]),1),"the leader's ordinal is #FFBE23");
	const auto score0 = runtime.PresentedValue("mb0-score","text"), score1 = runtime.PresentedValue("mb1-score","text");
	Check(score0 && score0->text == "19" && score1 && score1->text == "24","each side shows its score");
	Bounds score;
	Check(runtime.GetBounds("mb0-score",score) && Near(score.x+score.width,160+(166+122-7)*1.5f,1.5f),"the score sits at the inner end");
	// The timer: the time over the limit.
	const auto clock = runtime.PresentedValue("mbt-clock","text"), sub = runtime.PresentedValue("mbt-sub","text");
	Check(Shown(runtime,"mbt-clock") && clock && clock->text == "7:05" && Shown(runtime,"mbt-sub") && sub && sub->text == "TO 30",
		"the timer reads the time over the limit");
	Check(runtime.SetState({{"oq4_clock_warn",2.0},{"oq4_clock",std::string("0:08")}},error,.2),error.c_str());
	Check(runtime.RunEvent("oq4_clockWarn",.2,effects,error),"the clock warning runs");
	runtime.Frame(RemasteredViewport(1280,720),.25);
	Check(Shown(runtime,"mbt-urgent") && !Shown(runtime,"mbt-clock"),"the last ten seconds pulse in the bar");
	Check(runtime.SetState({{"oq4_clock_warn",0.0},{"oq4_state",1.0},{"showready",true},{"oq4_mb_ready",std::string("READY 3/8")}},error,.3),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.35);
	const auto ready = runtime.PresentedValue("mbt-sub","text");
	Check(Shown(runtime,"mbt-warm") && ready && ready->text == "READY 3/8","warm-up reads WARM-UP over the ready count");
	Check(runtime.SetState({{"oq4_state",3.0},{"showready",false}},error,.4),error.c_str());

	// The kill feed keeps its place beside the bar at 16:9 and steps 38 u
	// under it on narrower views, where its longest line would meet the
	// network readout.
	Check(runtime.SetState({{"oq4_feed0_live",true},{"oq4_feed0_alpha",1.0},{"oq4_feed0_attacker",std::string("Vex")},
		{"oq4_feed0_victim",std::string("Kaz")},{"oq4_feed0_weapon",7.0},{"oq4_feed0_kind",0.0},{"oq4_feed0_hasAttacker",true}},error,.41),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.415);
	Bounds feedLine;
	Check(Shown(runtime,"feed0") && runtime.GetBounds("feed0-c",feedLine) && Near(feedLine.y,(24-11.5f)*1.5f,1.f),
		"the kill feed keeps its place beside the bar at 16:9");
	Check(runtime.SetState(RemasteredLayout(960,720),error,.42),error.c_str());
	runtime.Frame(RemasteredViewport(960,720),.425);
	Check(runtime.GetBounds("feed0-c",feedLine) && Near(feedLine.y,(24-11.5f+38)*1.5f,1.f),"at 4:3 the kill feed steps under the bar");
	Check(runtime.SetState(RemasteredLayout(1280,720),error,.43),error.c_str());
	Check(runtime.SetState({{"oq4_feed0_live",false}},error,.435),error.c_str());

	// Network quality follows the bar's trailing end at all times: four bars
	// by the ping, each 2% of loss removing one.
	runtime.Frame(RemasteredViewport(1280,720),.45);
	Bounds net;
	Check(Shown(runtime,"mbn") && runtime.GetBounds("mbn",net) && Near(net.x,160+480*1.5f,1.5f),"network quality follows the bar");
	const auto lit3 = runtime.PresentedValue("mbn-b3","background-color");
	Check(lit3 && Near(float(lit3->data[0]),0xB5/255.f,.01f) && Near(float(lit3->data[3]),.9f,.01f),"a 40 ms ping lights all four bars");
	Check(runtime.SetState({{"oq4_loss",5.0}},error,.5),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.55);
	const auto lossy1 = runtime.PresentedValue("mbn-b1","background-color"), lossy2 = runtime.PresentedValue("mbn-b2","background-color");
	Check(lossy1 && Near(float(lossy1->data[0]),0xE3/255.f,.01f) && lossy2 && Near(float(lossy2->data[3]),.2f,.01f),
		"5% loss leaves two amber bars");
	Check(runtime.SetState({{"oq4_loss",0.0}},error,.6),error.c_str());

	// Team modes: your team leads with its color and mark; flags hang under
	// the bar from the inner ends; your team's list sits at the leading edge.
	Check(runtime.SetState({{"gametype",4.0},{"oq4_team_game",true},{"oq4_my_team",1.0},{"marine_score",3.0},{"strogg_score",2.0},
		{"oq4_flag1_state",1.0},{"oq4_flag1_carrier",std::string("Vex")},{"oq4_flag0_state",2.0},{"oq4_ts1_count",2.0},
		{"oq4_ts1_0_name",std::string("You")},{"oq4_ts1_0_weapon",7.0},{"oq4_ts1_0_health",125.0},{"oq4_ts1_0_armor",50.0},
		{"oq4_ts1_0_you",true},{"oq4_ts1_1_name",std::string("Kaz")},{"oq4_ts1_1_dead",true},{"oq4_ts1_1_health",0.0}},error,.7),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.75);
	Check(Shown(runtime,"mb0-block1") && Shown(runtime,"mb0-logo1") && Shown(runtime,"mb1-block0") && Shown(runtime,"mb0-team") &&
		!Shown(runtime,"mb0-band") && !Shown(runtime,"mb0-name"),"team modes lead with your team's block, mark and name");
	const auto teamName = runtime.PresentedValue("mb0-team","text");
	const auto teamScore = runtime.PresentedValue("mb0-score","text");
	Check(teamName && teamName->text == "#str_200199" && teamScore && teamScore->text == "2","the Strogg side leads with the Strogg score");
	Bounds flag0, flag1;
	Check(Shown(runtime,"mbf0") && Shown(runtime,"mbf0-taken") && Shown(runtime,"mbf0-carrier") && Shown(runtime,"mbf0-1taken") &&
		Shown(runtime,"mbf1-dropped") && Shown(runtime,"mbf1-0dropped") && !Shown(runtime,"mbf1-taken"),
		"each side's flag hangs under the bar with its state and carrier");
	Check(runtime.GetBounds("mbf0",flag0) && runtime.GetBounds("mbf1",flag1) && Near(flag0.x+flag0.width,160+282*1.5f,1.5f) &&
		Near(flag1.x,160+358*1.5f,1.5f) && Near(flag0.y,34*1.5f,1.f),"the flags hang 6 u in from the inner ends");
	Bounds row0, row1, head;
	Check(Shown(runtime,"tsl") && !Shown(runtime,"tst") && Shown(runtime,"tsl0") && Shown(runtime,"tsl1") && !Shown(runtime,"tsl2"),
		"your team's list shows its players");
	Check(runtime.GetBounds("tsl0",row0) && runtime.GetBounds("tsl1",row1) && Near(row0.x,8*1.5f) && Near(row0.y,150*1.5f) &&
		Near(row0.width,138*1.5f) && Near(row1.y-row0.y,16.5f*1.5f),"the rows sit from y 150 on a 16.5 u pitch");
	Check(Shown(runtime,"tsl0-band") && Shown(runtime,"tsl0-marker") && Shown(runtime,"tsl0-w7") && !Shown(runtime,"tsl0-plate"),
		"your row uses the band plate, the marker and your weapon");
	const auto overcharged = runtime.PresentedValue("tsl0-hp","color");
	Check(overcharged && Near(float(overcharged->data[2]),0x23/255.f,.01f),"health above 100 reads #FFBE23");
	const auto deadName = runtime.PresentedValue("tsl1-name","opacity");
	Check(Shown(runtime,"tsl1-dead") && !Shown(runtime,"tsl1-hp") && deadName && Near(float(deadName->data[0]),.4f),
		"the dead keep their row at 0.40 under a cross");
	Check(runtime.GetBounds("tsl-head",head) && head.y+head.height <= row0.y+1,"the team name sits above the list");

	// The vote takes the top-leading corner, clear of the team status.
	Check(runtime.SetState({{"voteNotice",1.0},{"voteInfo_1",std::string("Map: q4dm7")},{"oq4_vote_yes",3.0}},error,.8),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.85);
	Bounds ballot;
	Check(Shown(runtime,"vote") && runtime.GetBounds("vote",ballot) && Near(ballot.y,36*1.5f,1.f) && ballot.y+ballot.height < head.y,
		"the vote sits in the top-leading corner clear of the team status");
	Check(runtime.SetState({{"voteNotice",0.0}},error,.9),error.c_str());

	// Chat clears the 15% larger status bar by 8 u (21:9, where the box spans
	// the room beside it).
	Check(runtime.SetState(RemasteredLayout(2520,1080),error,1.0),error.c_str());
	Check(runtime.SetState({{"oq4_chat0_live",true},{"oq4_chat0_alpha",1.0},{"oq4_chat0_name",std::string("Kaz")},
		{"oq4_chat0_text",std::string("rail on the bridge")},{"oq4_chat0_kind",0.0}},error,1.05),error.c_str());
	runtime.Frame(RemasteredViewport(2520,1080),1.1);
	Bounds chat;
	const float px = 1080/720.f;
	Check(Shown(runtime,"chat0") && runtime.GetBounds("chat0",chat) && Near(chat.y+chat.height,1080-(51*1.15f+8)*1.5f*px,3.f),
		"chat clears the enlarged status bar by 8 u");
	// Narrower views keep the box low, under eight team rows (which end at
	// 280 u): 200 u wide at 16:9, and at 4:3 as wide as the space beside the
	// widest enlarged strip allows, down to 120 u.
	Check(runtime.SetState(RemasteredLayout(1280,720),error,1.12),error.c_str());
	StateValues lines;
	for (int i = 0; i < 4; ++i) {
		const std::string key = "oq4_chat"+std::to_string(i);
		lines.insert({{key+"_live",true},{key+"_alpha",1.0},{key+"_name",std::string("Kaz")},{key+"_text",std::string("rail on the bridge")},
			{key+"_kind",0.0}});
	}
	Check(runtime.SetState(lines,error,1.13),error.c_str());
	for (const int width : {1280,960}) {
		Check(runtime.SetState(RemasteredLayout(width,720),error,width == 1280 ? 1.14 : 1.16),error.c_str());
		runtime.Frame(RemasteredViewport(width,720),width == 1280 ? 1.15 : 1.17);
		Bounds box; float top = 720;
		for (int i = 0; i < 4; ++i) {
			Bounds line;
			if (runtime.GetBounds("chat"+std::to_string(i),line)) top = std::min(top,line.y);
		}
		const float expected = width == 1280 ? 200*1.5f : 120*1.5f;
		Check(runtime.GetBounds("chatbox",box) && Near(box.width,expected,1.f) && Near(box.y+box.height,720-(51*1.15f+8)*1.5f,2.f),
			width == 1280 ? "at 16:9 the box stays 8 u above the enlarged status bar, 200 u wide" :
				"at 4:3 the box narrows beside the enlarged strip instead of rising");
		Check(top > 280*1.5f,"four chat lines stay under eight team rows");
	}
	Check(runtime.SetState(RemasteredLayout(1280,720),error,1.2),error.c_str());

	// The open input shows the history that fits under the team status: with
	// eight rows (to 280 u) six of eight messages at 16:9, the newest.
	StateValues history{{"oq4_chatting",true},{"oq4_chat_count",8.0},{"oq4_ts1_count",8.0}};
	for (int i = 0; i < 8; ++i) {
		const std::string key = "oq4_chat"+std::to_string(i);
		history.insert({{key+"_live",true},{key+"_alpha",1.0},{key+"_name",std::string("Kaz")},{key+"_text",std::string("on the bridge")},
			{key+"_kind",0.0}});
	}
	Check(runtime.SetState(history,error,1.21),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),1.22);
	Bounds oldest;
	Check(!Shown(runtime,"chat0") && !Shown(runtime,"chat1") && Shown(runtime,"chat2") && Shown(runtime,"chat7") && Shown(runtime,"chatin"),
		"the input's history keeps the newest messages that fit");
	Check(runtime.GetBounds("chat2",oldest) && oldest.y > 280*1.5f,"the history stays under eight team rows");
	// At 4:3 the box narrows to 120 u: the prompts wrap onto a second row and
	// wrapped history is clipped at the room's top, never reaching the rows.
	StateValues wrapped;
	for (int i = 0; i < 8; ++i)
		wrapped.insert({"oq4_chat"+std::to_string(i)+"_text",std::string("hold the lower ramp until the quad is up then push through")});
	wrapped.insert({"oq4_chatin_channels",true});
	Check(runtime.SetState(wrapped,error,1.221),error.c_str());
	Check(runtime.SetState(RemasteredLayout(960,720),error,1.222),error.c_str());
	runtime.Frame(RemasteredViewport(960,720),1.2225);
	// The engine peer measures the prompt row for the room (a layout readback).
	Bounds measured;
	Check(runtime.GetBounds("chatin-prompts",measured) &&
		runtime.SetState({{"oq4.chatPrompts",double(measured.height)}},error,1.2226),"the prompt row's height reads back");
	host.draws.clear();
	runtime.Frame(RemasteredViewport(960,720),1.223);
	Bounds log, promptRow, line7;
	Check(runtime.GetBounds("chatlog",log) && log.y >= 280*1.5f-.5f && runtime.GetBounds("chat7",line7) && line7.height > 20*1.5f,
		"wrapped history is clipped at the room's top under eight team rows");
	float inkTop = 1e9f;
	for (const auto& draw : host.draws) for (const auto& v : draw.vertices)
		if (v.x < 120*1.5f && v.y > 200*1.5f && v.y < log.y+log.height && std::abs(v.b-0x8D/255.f) < .02f && v.r > .99f && v.a > .99f)
			inkTop = std::min(inkTop,v.y);
	Check(inkTop >= log.y-.5f,"no history ink above the room");
	// The engine peer reads the first line whose top the room holds (a layout
	// readback); a line the room cuts fades, so the history shows whole messages.
	int lowest = 8, first = 8;
	for (int i = 0; i < 8; ++i) {
		Bounds b;
		if (!Shown(runtime,"chat"+std::to_string(i)) || !runtime.GetBounds("chat"+std::to_string(i),b) || b.height <= 0) continue;
		lowest = std::min(lowest,i);
		if (first == 8 && b.y >= log.y-.5f) first = i;
	}
	Check(first > lowest && first < 8,"the room cuts the oldest wrapped line it shows");
	Check(runtime.SetState({{"oq4.chatFirst",double(first)}},error,1.2232),error.c_str());
	runtime.Frame(RemasteredViewport(960,720),1.2234);
	const auto cut = runtime.PresentedValue("chat"+std::to_string(lowest),"opacity");
	const auto whole = runtime.PresentedValue("chat"+std::to_string(first),"opacity");
	Check(cut && cut->data[0] < .01 && whole && whole->data[0] > .99,"the cut line fades and the whole ones stay");
	Check(runtime.GetBounds("chatin-prompts",promptRow) && promptRow.height > 15*1.5f,"the prompts wrap in a 120 u box");
	Check(runtime.SetState(RemasteredLayout(1280,720),error,1.224),error.c_str());
	StateValues rest{{"oq4_chatting",false},{"oq4_chat_count",0.0},{"oq4_ts1_count",2.0}};
	for (int i = 0; i < 8; ++i) rest.insert({"oq4_chat"+std::to_string(i)+"_live",false});
	Check(runtime.SetState(rest,error,1.23),error.c_str());

	// Spectating: both lists flank the view, the followed row lit; held
	// statistics take the trailing list's place. Tags add a health line.
	Check(runtime.SetState({{"oq4_spectating",true},{"oq4_following",true},{"oq4_follow_name",std::string("Nyx")},{"oq4_my_team",1.0},
		{"oq4_ts1_0_you",false},{"oq4_ts1_0_followed",true},{"oq4_ts0_count",1.0},{"oq4_ts0_0_name",std::string("Orin")},
		{"oq4_ts0_0_health",80.0},{"oq4_ts0_0_weapon",1.0}},error,1.3),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),1.35);
	Bounds trailing;
	const auto leadingName = runtime.PresentedValue("tsl0-name","text");
	Check(Shown(runtime,"tsl") && Shown(runtime,"tst") && leadingName && leadingName->text == "Orin","spectators see both teams, the Marines leading");
	// The trailing anchor's box ends at the view's edge (box left 320 dp at 16:9).
	Check(runtime.GetBounds("tst0",trailing) && Near(trailing.x+trailing.width,320+(494+138)*1.5f,1.5f),"the Strogg list trails 8 u from the edge");
	Check(Shown(runtime,"tst0-lit1") && Shown(runtime,"tst0-marker") && !Shown(runtime,"tst0-band"),"the followed row is lit with the marker");
	Check(runtime.SetState({{"stat_visible",1.0}},error,1.4),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),1.45);
	Check(!Shown(runtime,"tst") && Shown(runtime,"tsl"),"held statistics take the trailing list's place");
	Check(runtime.SetState({{"stat_visible",0.0},{"oq4_following",false},{"oq4_tags",1.0},{"oq4_tag0_x",0.5},{"oq4_tag0_y",0.4},
		{"oq4_tag0_name",std::string("Nyx")},{"oq4_tag0_team",1.0},{"oq4_tag0_health",60.0}},error,1.5),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),1.55);
	Bounds track, fill;
	Check(Shown(runtime,"tag0") && Shown(runtime,"tag0-hp") && runtime.GetBounds("tag0-hp",track) && runtime.GetBounds("tag0-hp-fill",fill) &&
		track.width > 0 && Near(fill.width,track.width*.6f,1.f),"a Competitive tag's health line fills to the health");

	// Remastered drops the bar, the lists and the tags' health line.
	RemasteredLayoutPreference(host,1);
	runtime.Frame(RemasteredViewport(1280,720),1.6);
	Check(!Shown(runtime,"matchbar") && !Shown(runtime,"tsl") && !Shown(runtime,"tst") && !Shown(runtime,"tag0-hp") && Shown(runtime,"tl-rem"),
		"Remastered returns the standing and drops the Competitive groups");
	Check(host.errors == 0,"no runtime diagnostics across the Competitive exercise");
	runtime.Shutdown();
}

// The vehicle HUD (guis/vehicles/hud.gui, 14.12): the centred status bar,
// its warnings, the entry lines, the hit flash and the HUD prompt.
static void CheckRemasteredVehicleHud(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%s %u:%u %s\n",path,unsigned(d.line),unsigned(d.column),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.hud.remastered.vehicle","Remastered vehicle HUD validates");
	RemasteredLayoutPreference(host,1);
	Runtime runtime(host);
	Check(runtime.Initialize(),"retained runtime");
	Check(runtime.LoadDocument(source,"guis/hud/remastered_vehicle.q4ui",diagnostics),"Remastered vehicle HUD loads into a runtime");
	std::string error;
	Runtime::EventEffects effects;
	StateValues values = RemasteredLayout(1280,720);
	values.insert({{"vehicle_health",0.8},{"vehicle_shield",0.55},{"vehicle_weapon",3.0},{"vehicle_weaponammo",6.0},
		{"vehicle_weaponcharge",0.5},{"vehicle_locked",0.0},{"oq4_key_jump",std::string("SPACE")}});
	Check(runtime.SetState(values,error,0),error.c_str());
	Check(runtime.RunEvent("hudJoin",0,effects,error),"the join program runs");
	runtime.Frame(RemasteredViewport(1280,720),.1);
	Check(host.errors == 0,"the vehicle HUD frames without runtime errors");

	// The status bar spans 11-288 u in the stock and centres on 320 u: the
	// hull gauge sits at 241.5 u, the shield gauge at 334.5 u, both at y 420.
	Bounds hull, shield, panel;
	Check(runtime.GetBounds("vhull-gauge",hull) && Near(hull.x,160+241.5f*1.5f,1.f) && Near(hull.y,420*1.5f,1.f),
		"the hull gauge sits on the centred status bar");
	Check(runtime.GetBounds("vshield-gauge",shield) && Near(shield.x-hull.x,93*1.5f,1.f),"the shield gauge follows 93 u on");
	Check(Shown(runtime,"vweap3") && !Shown(runtime,"vweap2") && Shown(runtime,"vcount") && Shown(runtime,"vcharge"),
		"the rocket pods show their glyph, count and charge bar");
	const auto count = runtime.PresentedValue("vcount-text","text");
	Check(count && count->text == "6","the rocket count reads the ammunition");
	Check(Shown(runtime,"vhull-track") && !Shown(runtime,"vhull-track-warn") && !Shown(runtime,"vshield-label-warn"),
		"healthy gauges keep their own colours");
	Check(runtime.SetState({{"vehicle_weapon",0.0}},error,.2),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.25);
	Check(Shown(runtime,"vweap0") && !Shown(runtime,"vcount") && Shown(runtime,"vcharge"),"the cannon shows its charge but no count");
	Check(runtime.SetState({{"vehicle_weapon",1.0}},error,.3),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.35);
	Check(Shown(runtime,"vweap1") && !Shown(runtime,"vcharge"),"the machinegun has no charge bar");
	Check(runtime.SetState({{"vehicle_health",0.2},{"vehicle_shield",0.0}},error,.4),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.45);
	Check(Shown(runtime,"vhull-track-warn") && Shown(runtime,"vhull-fill-warn") && Shown(runtime,"vhull-label-warn") &&
		Shown(runtime,"vshield-track-warn") && Shown(runtime,"vshield-icon-warn"),"a quarter hull and no shield turn their gauges #FF3300");
	// The fill drains toward the trailing end: its window moves, the art stays.
	const auto shift = runtime.PresentedValue("vhull-win","transform");
	Check(shift && Near(float(shift->data[0]),(119.6f-79*0.2f)*1.5f,0.1f),"the hull fill's leading edge sits at 20% from the trailing end");
	// The drawn fill follows the window (RmlUi clips only overflowing content):
	// at half hull the leading half of the track carries no fill.
	const auto hullFillLead = [&](double pct, double at) {
		Check(runtime.SetState({{"vehicle_health",pct},{"vehicle_shield",0.55}},error,at),error.c_str());
		host.draws.clear(); runtime.Frame(RemasteredViewport(1280,720),at+.005);
		float lo = 1e9f;
		for (const auto& draw : host.draws) for (const auto& v : draw.vertices) {
			if (v.a < .3f || v.a > .4f || std::fabs(v.r/v.a-.702f) > .03f || v.x < hull.x || v.x > hull.x+hull.width) continue;
			lo = std::min(lo,v.x);
		}
		return lo;
	};
	const float fullLead = hullFillLead(1.0,.46), halfLead = hullFillLead(0.5,.47);
	Check(fullLead < hull.x+hull.width && halfLead > fullLead+.3f*hull.width,"a half hull draws only the trailing half of its fill");

	// Entry: the lines begin 2 s after showExitmessage; the prompt shows Exit.
	Check(runtime.SetState({{"vehicle_weapon",3.0},{"vehicle_health",0.8},{"vehicle_shield",0.55}},error,.5),error.c_str());
	Check(runtime.RunEvent("showExitmessage",.5,effects,error),"entering runs");
	runtime.Frame(RemasteredViewport(1280,720),.6);
	Check(Shown(runtime,"vmsg") && Shown(runtime,"vpr") && Shown(runtime,"vpr-exit") && !Shown(runtime,"vpr-lock"),
		"entering shows the lines' group and the Exit prompt");
	const auto line1 = runtime.PresentedValue("vmsg1-text","text"), line2 = runtime.PresentedValue("vmsg2-text","text");
	Check(line1 && line1->text == "#str_200942" && line2 && line2->text == "#str_200941","the rocket pods' lines name the other weapon first");
	const auto closed = runtime.PresentedValue("vmsg1-win","transform");
	Check(closed && Near(float(closed->data[0]),-348*1.5f,0.5f),"the first line is closed before 2 s");
	runtime.Frame(RemasteredViewport(1280,720),.5+2.3);
	const auto open = runtime.PresentedValue("vmsg1-win","transform");
	Check(open && Near(float(open->data[0]),0,0.5f),"the first line has wiped open at 2.25 s");
	const auto key = runtime.PresentedValue("vpr-key","text");
	Check(key && key->text == "SPACE","the prompt names the jump key");
	Bounds prompt;
	Check(runtime.GetBounds("vpr",prompt) && Near(prompt.x+prompt.width,320+629*1.5f,1.5f),"the prompt trails at 629 u");
	Check(Shown(runtime,"vpr-chip") && !Shown(runtime,"vpr-pad-round") && !Shown(runtime,"vpr-pad-mouse"),"a keyboard key is a keycap");
	// A controller's button draws its family's glyph (13.5): the engine gives
	// the kind in "<key>.glyph" and the label in the key state.
	const auto glyphShows = [&](double kind, const char* label, std::initializer_list<const char*> shown, std::initializer_list<const char*> hidden) {
		Check(runtime.SetState({{"oq4_key_jump.glyph",kind},{"oq4_key_jump",std::string(label)}},error,2.9),error.c_str());
		runtime.Frame(RemasteredViewport(1280,720),2.95);
		bool ok = !Shown(runtime,"vpr-chip");
		for (const char* id : shown) ok = ok && Shown(runtime,id);
		for (const char* id : hidden) ok = ok && !Shown(runtime,id);
		Bounds trailing;
		return ok && runtime.GetBounds("vpr",trailing) && Near(trailing.x+trailing.width,320+629*1.5f,1.5f);
	};
	Check(glyphShows(1,"A",{"vpr-pad-round","vpr-ring","vpr-letter"},{"vpr-cross","vpr-dots","vpr-stick"}) &&
		runtime.PresentedValue("vpr-letter","text")->text == "A","an Xbox south button reads A in a ring");
	Check(glyphShows(2,"",{"vpr-pad-round","vpr-ring","vpr-cross"},{"vpr-letter","vpr-circle"}),"a PlayStation south button draws the cross");
	Check(glyphShows(5,"",{"vpr-triangle"},{"vpr-cross","vpr-square"}),"a PlayStation north button draws the triangle");
	Check(glyphShows(6,"",{"vpr-dots","vpr-dot6"},{"vpr-dot7","vpr-dot9","vpr-letter"}),"an unknown family lights the south position");
	Check(glyphShows(10,"LB",{"vpr-pad-bumper","vpr-bumper","vpr-bumper-label"},{"vpr-pad-round"}) &&
		runtime.PresentedValue("vpr-bumper-label","text")->text == "LB","a bumper reads its family's name");
	Check(glyphShows(11,"ZR",{"vpr-pad-trigger","vpr-trigger-label"},{"vpr-pad-bumper"}),"a trigger draws the trigger");
	Check(glyphShows(13,"",{"vpr-pad-dpad","vpr-dpad","vpr-arm13"},{"vpr-arm12","vpr-arm14","vpr-arm15"}),"the d-pad lights its bound arm");
	Check(glyphShows(16,"L3",{"vpr-pad-round","vpr-stick","vpr-stick-label"},{"vpr-letter"}),"a stick click draws the stick");
	Check(glyphShows(17,"",{"vpr-pad-pill","vpr-menu"},{"vpr-view"}) && glyphShows(18,"",{"vpr-view"},{"vpr-menu"}),
		"menu and view draw their symbols");
	Check(glyphShows(19,"",{"vpr-home"},{"vpr-letter"}) && glyphShows(20,"",{"vpr-pad-touch"},{"vpr-pad-round"}) &&
		glyphShows(21,"R4",{"vpr-pad-paddle","vpr-paddle-label"},{}),"home, the touchpad and a paddle draw theirs");
	Check(glyphShows(22,"OPTIONS",{"vpr-pad-button","vpr-button-label"},{"vpr-pad-pill"}) &&
		runtime.PresentedValue("vpr-button-label","text")->text == "OPTIONS","another button is a pill with its label");
	Check(glyphShows(23,"",{"vpr-pad-mouse","vpr-mouse","vpr-mouse-left"},{"vpr-mouse-right","vpr-mouse-middle"}) &&
		glyphShows(27,"",{"vpr-mouse-middle","vpr-wheel-down"},{"vpr-wheel-up"}) && glyphShows(28,"4",{"vpr-side-label"},{"vpr-mouse-left"}),
		"the mouse lights the bound button or the wheel's direction");
	Check(runtime.SetState({{"oq4_key_jump.glyph",0.0},{"oq4_key_jump",std::string("SPACE")}},error,3.0),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),3.05);
	Check(Shown(runtime,"vpr-chip") && !Shown(runtime,"vpr-pad-mouse"),"the keyboard's keycap returns");
	runtime.Frame(RemasteredViewport(1280,720),.5+5.6);
	Check(!Shown(runtime,"vmsg"),"the lines are gone after 5.5 s");
	Check(runtime.SetState({{"vehicle_locked",1.0}},error,6.2),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),6.25);
	Check(Shown(runtime,"vpr-lock") && !Shown(runtime,"vpr-exit"),"a locked vehicle shows the lock instead of Exit");

	// Hits and electrical damage.
	Check(runtime.RunEvent("vehicleHit",6.3,effects,error),"a hit runs");
	runtime.Frame(RemasteredViewport(1280,720),6.35);
	const auto flash = runtime.PresentedValue("vhit","opacity");
	Check(flash && flash->data[0] > .8 && flash->data[0] < 1,"a hit flashes the edges");
	Bounds hitLeft, hitRight, hitTop, hitBottom;
	Check(runtime.GetBounds("vhit-l",hitLeft) && runtime.GetBounds("vhit-r",hitRight) && runtime.GetBounds("vhit-t",hitTop) &&
		runtime.GetBounds("vhit-b",hitBottom) && Near(hitLeft.x,0) && Near(hitRight.x+hitRight.width,1280,1.f) && Near(hitTop.y,0) &&
		Near(hitBottom.y+hitBottom.height,720,1.f) && Near(hitRight.width,0.22f*640*1.5f,1.f),"the flash's strips hold all four edges of the view");
	{
		// Reduced flashing (13.10) caps every strip at 0.40.
		host.cvars["ui_reducedFlashing"] = true;
		runtime.Frame(RemasteredViewport(1280,720),6.36);
		bool capped = true;
		for (const char* strip : {"vhit-l","vhit-r","vhit-t","vhit-b"}) {
			const auto value = runtime.PresentedValue(strip,"opacity");
			capped = capped && value && Near(float(value->data[0]),.4f,.001f);
		}
		Check(capped,"reduced flashing caps the vehicle hit glow at 0.40");
		host.cvars["ui_reducedFlashing"] = false;
		runtime.Frame(RemasteredViewport(1280,720),6.37);
	}
	runtime.Frame(RemasteredViewport(1280,720),6.9);
	const auto gone = runtime.PresentedValue("vhit","opacity");
	Check(gone && gone->data[0] < 1e-3,"the flash is gone after 500 ms");
	Check(runtime.RunEvent("electricWarningOn",7,effects,error),"electrical damage runs");
	runtime.Frame(RemasteredViewport(1280,720),7.05);
	Check(Shown(runtime,"velec-l-on") && Shown(runtime,"velec-r-on"),"electrical damage shows both side warnings");
	Check(runtime.RunEvent("electricWarningOff",7.1,effects,error) && runtime.RunEvent("hideExitmessage",7.1,effects,error),"leaving runs");
	runtime.Frame(RemasteredViewport(1280,720),7.15);
	Check(!Shown(runtime,"velec-l-on") && !Shown(runtime,"vpr"),"leaving clears the warning and the prompt");

	// Minimal: the status bar and the prompt stay; the hit flash, warnings,
	// radio, boss bar and notices go.
	RemasteredLayoutPreference(host,2);
	Check(runtime.RunEvent("showExitmessage",7.2,effects,error),"boarding again runs");
	runtime.Frame(RemasteredViewport(1280,720),7.25);
	Bounds minimalHull;
	Check(runtime.GetBounds("vhull-gauge",minimalHull) && Near(minimalHull.x,hull.x) && Near(minimalHull.y,hull.y),
		"Minimal keeps the vehicle status bar in place");
	Check(!Shown(runtime,"hit-full") && !Shown(runtime,"cl-full") && !Shown(runtime,"cr-full") && !Shown(runtime,"tr-full") &&
		!Shown(runtime,"tc-full") && !Shown(runtime,"cc-full"),"Minimal leaves out the hit flash, warnings, radio, boss bar and notices");
	RemasteredLayoutPreference(host,1);
	Check(runtime.RunEvent("hideExitmessage",7.3,effects,error),"leaving again runs");
	runtime.Frame(RemasteredViewport(1280,720),7.35);
	Check(Shown(runtime,"hit-full") && Shown(runtime,"tr-full"),"Remastered brings them back");
	Check(host.errors == 0,"no runtime diagnostics across the vehicle HUD exercise");
	runtime.Shutdown();
}

// The air-defence cannon (guis/vehicles/airdef_cannon.gui, 14.12): the nailgun
// lens held still with its load bars and reticle, the transmission display the
// stock lacks, and the HUD prompt in place of the centred exit line.
static void CheckRemasteredAirdefHud(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%s %u:%u %s\n",path,unsigned(d.line),unsigned(d.column),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.hud.remastered.airdef","Remastered air-defence HUD validates");
	RemasteredLayoutPreference(host,1);
	Runtime runtime(host);
	Check(runtime.Initialize(),"retained runtime");
	Check(runtime.LoadDocument(source,"guis/hud/remastered_airdef.q4ui",diagnostics),"Remastered air-defence HUD loads into a runtime");
	std::string error;
	Runtime::EventEffects effects;
	host.softFocus = true;
	Check(runtime.SetState(RemasteredLayout(1280,720),error,0),error.c_str());
	Check(runtime.RunEvent("hudJoin",0,effects,error),"the join program runs");
	host.softened.clear(); host.draws.clear();
	runtime.Frame(RemasteredViewport(1280,720),.1);
	Check(host.errors == 0 && !host.draws.empty(),"the cannon HUD draws without runtime errors");
	Bounds optic;
	Check(runtime.GetBounds("ad-optic",optic) && Near(optic.x,160) && Near(optic.y,0) && Near(optic.width,960) && Near(optic.height,720),
		"the optic fills the 4:3 center");
	Check(!host.softened.empty(),"the lens softens the view beyond its aperture");
	Bounds reticle, load, ring;
	Check(Shown(runtime,"ad-reticle") && runtime.GetBounds("ad-reticle",reticle) && runtime.GetBounds("ad-load",load) &&
		runtime.GetBounds("ad-ring2",ring) && Near(load.x,optic.x) && Near(ring.width,optic.width),"the reticle, load bars and ring are laid out in the optic");
	Check(!Shown(runtime,"vpr") && !Shown(runtime,"radio"),"no prompt or transmission before boarding or chatter");

	// Boarding shows the jump key's Exit; a script's lock shows Vehicle locked.
	Check(runtime.SetState({{"oq4_key_jump",std::string("SPACE")}},error,.2),error.c_str());
	Check(runtime.RunEvent("showExitmessage",.2,effects,error),"boarding runs");
	runtime.Frame(RemasteredViewport(1280,720),.25);
	Bounds prompt;
	Check(Shown(runtime,"vpr") && Shown(runtime,"vpr-exit") && !Shown(runtime,"vpr-lock") && runtime.GetBounds("vpr",prompt) &&
		Near(prompt.x+prompt.width,320+629*1.5f,1.5f),"the HUD prompt trails at 629 u with Exit");
	const auto key = runtime.PresentedValue("vpr-key","text");
	Check(key && key->text == "SPACE","the prompt names the jump control");
	Check(runtime.RunEvent("showLockmessage",.3,effects,error),"a lock runs");
	runtime.Frame(RemasteredViewport(1280,720),.35);
	Check(Shown(runtime,"vpr-lock") && !Shown(runtime,"vpr-exit"),"a lock reads Vehicle locked");
	Check(runtime.RunEvent("hideLockmessage",.4,effects,error),"unlocking runs");

	// The transmission display the stock cannon HUD lacks, and the save notice.
	Check(runtime.RunEvent("radioChatterUp",.4,effects,error) && runtime.RunEvent("saveMessage",.4,effects,error),"chatter and a save run");
	runtime.Frame(RemasteredViewport(1280,720),1.2);
	Check(Shown(runtime,"radio") && Shown(runtime,"saved") && Shown(runtime,"vpr-exit"),"the transmission display and the save notice show");

	// Wide and tall views keep the optic round and centred.
	Check(runtime.SetState(RemasteredLayout(2560,1080),error,1.3),error.c_str());
	runtime.Frame(RemasteredViewport(2560,1080),1.35);
	Check(runtime.GetBounds("ad-optic",optic) && Near(optic.x,560,1.f) && Near(optic.width,1440,1.f) && Near(optic.height,1080,1.f),
		"at 21:9 the optic keeps its proportions at the center");
	Check(runtime.SetState(RemasteredLayout(720,1280),error,1.4),error.c_str());
	runtime.Frame(RemasteredViewport(720,1280),1.45);
	Check(runtime.GetBounds("ad-optic",optic) && Near(optic.width,720,1.f) && Near(optic.height,540,1.f) && Near(optic.y,370,1.f),
		"in a tall view the optic fits the width");

	// Minimal keeps the optic, the prompt and the save notice.
	Check(runtime.SetState(RemasteredLayout(1280,720),error,1.5),error.c_str());
	RemasteredLayoutPreference(host,2);
	runtime.Frame(RemasteredViewport(1280,720),1.55);
	Check(!Shown(runtime,"tr-full") && Shown(runtime,"vpr") && Shown(runtime,"ad-reticle"),"Minimal keeps the optic and the prompt");
	RemasteredLayoutPreference(host,1);
	Check(runtime.RunEvent("hideExitmessage",1.6,effects,error),"leaving runs");
	runtime.Frame(RemasteredViewport(1280,720),1.65);
	Check(!Shown(runtime,"vpr"),"leaving clears the prompt");
	Check(host.errors == 0,"no runtime diagnostics across the cannon exercise");
	host.softFocus = false;
	runtime.Shutdown();
}

// The Awakening's space cannon (turretplayerhud.gui, 14.12): the ship-status
// panel under the transmission display with its damage, markers, darts and
// view cone, and the HUD prompt the turret HUD lacked.
static void CheckRemasteredTurretHud(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%s %u:%u %s\n",path,unsigned(d.line),unsigned(d.column),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.hud.remastered.turret","Remastered space cannon HUD validates");
	RemasteredLayoutPreference(host,1);
	Runtime runtime(host);
	Check(runtime.Initialize(),"retained runtime");
	Check(runtime.LoadDocument(source,"guis/hud/remastered_turret.q4ui",diagnostics),"Remastered space cannon HUD loads into a runtime");
	std::string error;
	Runtime::EventEffects effects;
	StateValues values = RemasteredLayout(1280,720);
	values.insert({{"oq4_key_jump",std::string("SPACE")}});
	Check(runtime.SetState(values,error,0),error.c_str());
	Check(runtime.RunEvent("hudJoin",0,effects,error),"the join program runs");
	runtime.Frame(RemasteredViewport(1280,720),.1);
	Check(host.errors == 0,"the space cannon HUD frames without runtime errors");
	// The panel: the stock 128 u box at 512,32 of the top-trailing anchor.
	Bounds panel;
	Check(Shown(runtime,"ship") && runtime.GetBounds("ship",panel) && Near(panel.x,320+512*1.5f,1.f) && Near(panel.y,32*1.5f,1.f) &&
		Near(panel.width,128*1.5f,1.f),"the ship panel sits under the transmission display");
	Bounds gauge;
	Check(!runtime.GetBounds("health-gauge",gauge) && !runtime.GetBounds("strip",gauge),"the gun keeps no gauges or weapon strip");
	Check(Shown(runtime,"vpr") && Shown(runtime,"vpr-exit"),"the crewed gun shows Exit at once");
	// Damage: each section lights on its event and clears on its _off.
	Check(runtime.RunEvent("ship_health_01_on",.2,effects,error) && runtime.RunEvent("ship_health_02_on",.2,effects,error),"damage runs");
	runtime.Frame(RemasteredViewport(1280,720),.25);
	Check(Shown(runtime,"ship-damage1") && Shown(runtime,"ship-damage2") && !Shown(runtime,"ship-damage3"),"the damaged sections light");
	Check(runtime.RunEvent("ship_health_01_off",.3,effects,error),"a repair runs");
	runtime.Frame(RemasteredViewport(1280,720),.35);
	Check(!Shown(runtime,"ship-damage1") && Shown(runtime,"ship-damage2"),"a repaired section clears");
	// A marker spins at 0.35 turn a second.
	Check(runtime.RunEvent("ship_target_03_on",.4,effects,error),"a marker runs");
	runtime.Frame(RemasteredViewport(1280,720),.4);
	runtime.Frame(RemasteredViewport(1280,720),.9);
	const auto spin = runtime.PresentedValue("ship-target3","transform");
	Check(Shown(runtime,"ship-target3") && !Shown(runtime,"ship-target1") && spin && std::fabs(float(spin->data[4])) > 40,
		"the objective marker shows and spins");
	// A dart pulses while it fades over 3 s.
	Check(runtime.RunEvent("warning_NE",1.0,effects,error),"a warning runs");
	runtime.Frame(RemasteredViewport(1280,720),1.01);
	const auto lit = runtime.PresentedValue("ship-warn-NE","opacity");
	Check(lit && float(lit->data[0]) > .8,"the dart lights");
	runtime.Frame(RemasteredViewport(1280,720),4.2);
	const auto faded = runtime.PresentedValue("ship-warn-NE","opacity");
	Check(faded && float(faded->data[0]) < .01,"the dart has faded after 3 s");
	// The view cone follows the gun relative to the ship's nose.
	Check(runtime.SetState({{"playerYaw",120.0},{"oq4_shipYaw",90.0}},error,4.3),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),4.35);
	const auto cone = runtime.PresentedValue("ship-cone","transform");
	Check(cone && Near(float(cone->data[4]),-30,.01f),"the cone turns with the gun's heading off the nose");
	// Minimal keeps the panel and the prompt.
	RemasteredLayoutPreference(host,2);
	runtime.Frame(RemasteredViewport(1280,720),4.4);
	Check(!Shown(runtime,"tr-full") && Shown(runtime,"ship") && Shown(runtime,"vpr"),"Minimal keeps the ship panel and the prompt");
	RemasteredLayoutPreference(host,1);
	Check(host.errors == 0,"no runtime diagnostics across the space cannon exercise");
	runtime.Shutdown();
}

// The Awakening's MCC cannon (cannonplayerhud.gui, 14.12): the MCC's hull
// readout stacked under the transmission display, filled in proportion and
// warning at 25%, and the HUD prompt's Exit.
static void CheckRemasteredMccHud(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%s %u:%u %s\n",path,unsigned(d.line),unsigned(d.column),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.hud.remastered.mcc","Remastered MCC cannon HUD validates");
	RemasteredLayoutPreference(host,1);
	Runtime runtime(host);
	Check(runtime.Initialize(),"retained runtime");
	Check(runtime.LoadDocument(source,"guis/hud/remastered_mcc.q4ui",diagnostics),"Remastered MCC cannon HUD loads into a runtime");
	std::string error;
	Runtime::EventEffects effects;
	StateValues values = RemasteredLayout(1280,720);
	values.insert({{"oq4_key_jump",std::string("SPACE")}});
	Check(runtime.SetState(values,error,0),error.c_str());
	Check(runtime.RunEvent("hudJoin",0,effects,error),"the join program runs");
	runtime.Frame(RemasteredViewport(1280,720),.1);
	Check(host.errors == 0,"the MCC cannon HUD frames without runtime errors");
	// The readout: 36 u above the stock group, directly under the transmission display.
	Bounds gauge;
	Check(runtime.GetBounds("mcc-gauge",gauge) && Near(gauge.x,320+494*1.5f,1.f) && Near(gauge.y,(80-36)*1.5f,1.f) &&
		Near(gauge.width,136*1.5f,1.f),"the hull gauge stacks under the transmission display");
	auto hull = runtime.PresentedValue("mcc-hull","text");
	Check(hull && hull->text == "2000","a whole MCC reads 2000");
	Check(Shown(runtime,"mcc-fill") && !Shown(runtime,"mcc-fill-warn") && Shown(runtime,"vpr") && Shown(runtime,"vpr-exit"),
		"the full track and Exit show");
	// The drawn extent of the hull fill: #B3D06E at 0.35, premultiplied.
	const auto fillExtent = [&]() {
		float lo = 1e9f, hi = -1e9f;
		for (const auto& draw : host.draws) for (const auto& v : draw.vertices) {
			if (v.a < .3f || v.a > .4f || std::fabs(v.r/v.a-.702f) > .03f || std::fabs(v.g/v.a-.816f) > .03f) continue;
			lo = std::min(lo,v.x); hi = std::max(hi,v.x);
		}
		return std::pair{lo,hi};
	};
	host.draws.clear(); runtime.Frame(RemasteredViewport(1280,720),.15);
	const auto fullFill = fillExtent();
	// The track fills in proportion: half the hull moves the window's trailing edge to the middle.
	Check(runtime.SetState({{"mcchealth",1000.0}},error,.2),error.c_str());
	host.draws.clear();
	runtime.Frame(RemasteredViewport(1280,720),.25);
	const auto halfFill = fillExtent();
	Check(fullFill.second > fullFill.first+.8f*gauge.width && Near(halfFill.first,fullFill.first,1.f) &&
		halfFill.second < fullFill.second-.3f*gauge.width && halfFill.second > fullFill.first+.3f*gauge.width,
		"half the hull draws half the fill from the leading end");
	const auto shift = runtime.PresentedValue("mcc-win","transform");
	const float expected = ((5.4f+114.2f*.5f)*136.f/125.f-136.f)*1.5f;
	hull = runtime.PresentedValue("mcc-hull","text");
	Check(shift && Near(float(shift->data[0]),expected,.1f) && hull && hull->text == "1000","half the hull fills half the track");
	// At or below 25% the readout warns.
	Check(runtime.SetState({{"mcchealth",400.0}},error,.3),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.35);
	const auto colour = runtime.PresentedValue("mcc-hull","color");
	Check(Shown(runtime,"mcc-fill-warn") && Shown(runtime,"mcc-track-warn") && !Shown(runtime,"mcc-fill") && colour &&
		Near(float(colour->data[0]),1) && Near(float(colour->data[1]),.2f,.01f),"a quarter hull warns in #FF3300");
	// Minimal keeps the readout and the prompt.
	RemasteredLayoutPreference(host,2);
	runtime.Frame(RemasteredViewport(1280,720),.4);
	Check(!Shown(runtime,"tr-full") && Shown(runtime,"vpr") && runtime.GetBounds("mcc-gauge",gauge),"Minimal keeps the hull readout and the prompt");
	RemasteredLayoutPreference(host,1);
	Check(host.errors == 0,"no runtime diagnostics across the MCC cannon exercise");
	runtime.Shutdown();
}

// openQ4's HUD for the Awakening's chaingun turret (turret_hud.gui, 14.12): the
// gunner's health and armor gauges, the barrel heat on the gauge where ammunition
// shows on foot and under the crosshair, warning from vehicle_heatwarn, OVERHEATED
// while the gun vents, and the HUD prompt's Exit.
static void CheckRemasteredChaingunTurretHud(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%s %u:%u %s\n",path,unsigned(d.line),unsigned(d.column),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.hud.remastered.chaingun_turret","Remastered chaingun turret HUD validates");
	RemasteredLayoutPreference(host,1);
	Runtime runtime(host);
	Check(runtime.Initialize(),"retained runtime");
	Check(runtime.LoadDocument(source,"guis/hud/remastered_chaingun_turret.q4ui",diagnostics),"Remastered chaingun turret HUD loads into a runtime");
	std::string error;
	Runtime::EventEffects effects;
	StateValues values = RemasteredLayout(1280,720);
	values.insert({{"oq4_key_jump",std::string("SPACE")},{"player_health",100.0},{"player_healthpct",1.0},
		{"player_armor",50.0},{"player_armorpct",.5},{"vehicle_heat",0.0},{"vehicle_overheat",0.0},{"vehicle_heatwarn",.75}});
	Check(runtime.SetState(values,error,0),error.c_str());
	Check(runtime.RunEvent("hudJoin",0,effects,error),"the join program runs");
	runtime.Frame(RemasteredViewport(1280,720),.1);
	Check(host.errors == 0,"the chaingun turret HUD frames without runtime errors");
	// The three gauges sit where the on-foot status bar's do: heat where ammunition shows.
	Bounds heat, health, armor;
	Check(runtime.GetBounds("heat-gauge",heat) && runtime.GetBounds("health-gauge",health) && runtime.GetBounds("armor-gauge",armor) &&
		Near(heat.y,health.y,.5f) && Near(health.x-heat.x,(278-101)*1.5f,1.f) && Near(armor.x-health.x,(414-278)*1.5f,1.f) &&
		Near(heat.width,125*1.5f,1.f),"heat, health and armor take the on-foot gauges' places");
	auto number = runtime.PresentedValue("heat-num-text","text");
	auto life = runtime.PresentedValue("health-num-text","text");
	Check(number && number->text == "0" && life && life->text == "100","a cold gun reads 0 beside the gunner's 100");
	Check(!Shown(runtime,"xheat") && Shown(runtime,"heat-fill-cool") && !Shown(runtime,"heat-fill-hot") && Shown(runtime,"vpr") &&
		Shown(runtime,"vpr-exit"),"a cold gun shows no crosshair bar, the cool track and Exit");
	// Half the limit: the crosshair bar shows, scaled to half from its leading edge.
	Check(runtime.SetState({{"vehicle_heat",.5}},error,.2),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.25);
	number = runtime.PresentedValue("heat-num-text","text");
	const auto scale = runtime.PresentedValue("xheat-cool","transform");
	Check(number && number->text == "50" && Shown(runtime,"xheat") && Shown(runtime,"xheat-cool") && !Shown(runtime,"xheat-hot") &&
		scale && Near(float(scale->data[2]),.5f,.01f) && Near(float(scale->data[0]),-20*1.5f*.5f,.1f),"half the limit fills half the bar");
	// From the warning level the heat turns #FF3300.
	Check(runtime.SetState({{"vehicle_heat",.8}},error,.3),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.35);
	const auto colour = runtime.PresentedValue("heat-numerals","color");
	Check(Shown(runtime,"heat-fill-hot") && Shown(runtime,"heat-track-hot") && !Shown(runtime,"heat-fill-cool") &&
		Shown(runtime,"xheat-hot") && !Shown(runtime,"xheat-over") && colour && Near(float(colour->data[0]),1) &&
		Near(float(colour->data[1]),.2f,.01f),"a hot gun warns in #FF3300");
	// Overheated: OVERHEATED under the crosshair while it vents, whatever the heat.
	Check(runtime.SetState({{"vehicle_heat",.6},{"vehicle_overheat",1.0}},error,.4),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.45);
	Check(Shown(runtime,"xheat-over") && Shown(runtime,"xheat-hot") && Shown(runtime,"heat-fill-hot"),"a venting gun says OVERHEATED");
	// The gunner's armor gauge goes with the last of the armor, as on foot.
	Check(runtime.SetState({{"player_armor",0.0},{"player_armorpct",0.0}},error,.5),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.55);
	Check(!Shown(runtime,"armor-gauge") && Shown(runtime,"heat-gauge"),"no armor, no armor gauge");
	// Minimal keeps the gauges, the crosshair's heat and the prompt.
	RemasteredLayoutPreference(host,2);
	runtime.Frame(RemasteredViewport(1280,720),.6);
	Check(!Shown(runtime,"tr-full") && Shown(runtime,"xheat") && Shown(runtime,"heat-gauge") && Shown(runtime,"vpr"),
		"Minimal keeps the gauges, the crosshair's heat and the prompt");
	RemasteredLayoutPreference(host,1);
	Check(host.errors == 0,"no runtime diagnostics across the chaingun turret exercise");
	runtime.Shutdown();
}

// The Awakening's speeder bike (speederplayerhud.gui, 14.12): the vehicle status
// bar with the bike's own glyphs, the race display 18 u lower with its padded
// timer and last-10-seconds red, the boost in the HUD prompt and the streak.
static void CheckRemasteredSpeederHud(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%s %u:%u %s\n",path,unsigned(d.line),unsigned(d.column),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.hud.remastered.speeder","Remastered speeder HUD validates");
	RemasteredLayoutPreference(host,1);
	Runtime runtime(host);
	Check(runtime.Initialize(),"retained runtime");
	Check(runtime.LoadDocument(source,"guis/hud/remastered_speeder.q4ui",diagnostics),"Remastered speeder HUD loads into a runtime");
	std::string error;
	Runtime::EventEffects effects;
	host.softFocus = true;
	StateValues values = RemasteredLayout(1280,720);
	values.insert({{"vehicle_health",0.8},{"vehicle_shield",0.55},{"vehicle_weapon",0.0},{"vehicle_weaponcharge",0.5},
		{"vehicle_locked",0.0},{"oq4_key_jump",std::string("SPACE")},{"oq4_key_crouch",std::string("C")},{"oq4_boost",0.0}});
	Check(runtime.SetState(values,error,0),error.c_str());
	Check(runtime.RunEvent("hudJoin",0,effects,error),"the join program runs");
	host.streaks.clear(); host.draws.clear();
	runtime.Frame(RemasteredViewport(1280,720),.1);
	Check(host.errors == 0,"the speeder HUD frames without runtime errors");
	Check(host.streaks.empty(),"no streak before a boost");

	// The bike lists its machinegun first: weapon 0 shows the machinegun's
	// panel without a charge bar, weapon 1 the cannon's with it.
	Bounds hull;
	Check(runtime.GetBounds("vhull-gauge",hull) && Near(hull.x,160+241.5f*1.5f,1.f),"the status bar centres on the bottom edge");
	Check(Shown(runtime,"vweap0") && !Shown(runtime,"vweap1") && !Shown(runtime,"vcharge") && !Shown(runtime,"vcount"),
		"the machinegun shows its panel, no charge bar and no count");
	Check(runtime.SetState({{"vehicle_weapon",1.0}},error,.2),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.25);
	Check(Shown(runtime,"vweap1") && Shown(runtime,"vcharge") && !Shown(runtime,"vcount"),"the cannon shows its recharge");
	Check(runtime.SetState({{"vehicle_health",0.2}},error,.3),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.35);
	Check(Shown(runtime,"vhull-track-warn") && Shown(runtime,"vhull-label-warn"),"the hull warning the expansion removed is back");
	Check(runtime.SetState({{"vehicle_health",0.8}},error,.4),error.c_str());

	// The race display: the plate 18 u below the stock, the pips lit per checkpoint.
	host.draws.clear(); runtime.Frame(RemasteredViewport(1280,720),.45);
	float top = 1e9f, bottom = -1e9f, left = 1e9f, right = -1e9f;
	for (const auto& draw : host.draws) for (const auto& v : draw.vertices) {
		// The plate's black at 0.454, in the top half (the status bar's backing is lower).
		if (std::fabs(v.a-.454f) > .003f || v.r > .01f || v.y > 360) continue;
		top = std::min(top,v.y); bottom = std::max(bottom,v.y); left = std::min(left,v.x); right = std::max(right,v.x);
	}
	Check(Near(top,(15+18)*1.5f,1.5f) && Near(bottom,(60.5f+18)*1.5f,1.5f) && Near(left,160+68.1f*1.5f,1.5f) &&
		Near(right,160+571.4f*1.5f,1.5f),"the race plate sits 18 u lower at the top centre");
	Check(!Shown(runtime,"race-pip0"),"no checkpoint lights no pip");
	Check(runtime.RunEvent("showprogress30",.5,effects,error),"a checkpoint runs");
	runtime.Frame(RemasteredViewport(1280,720),.55);
	Check(Shown(runtime,"race-pip0") && Shown(runtime,"race-pip2") && !Shown(runtime,"race-pip3"),"three checkpoints light three pips");

	// The timer shows as the script runs it, the hundredths two digits wide.
	Check(!Shown(runtime,"race-sec") && !Shown(runtime,"race-hun"),"no placeholder timer before the race");
	Check(runtime.SetState({{"currenttime",42.0},{"currenttime2",5.0}},error,.6),error.c_str());
	Check(runtime.RunEvent("updatetimer",.6,effects,error) && runtime.RunEvent("updatetimer2",.6,effects,error),"the countdown runs");
	runtime.Frame(RemasteredViewport(1280,720),.65);
	const auto seconds = runtime.PresentedValue("race-sec","text"), tens = runtime.PresentedValue("race-hun-tens","text"),
		units = runtime.PresentedValue("race-hun-units","text");
	Check(Shown(runtime,"race-sec") && Shown(runtime,"race-hun") && Shown(runtime,"race-point") && seconds && seconds->text == "42" &&
		tens && tens->text == "0" && units && units->text == "5","the timer reads 42.05");
	Bounds sec;
	Check(runtime.GetBounds("race-sec",sec) && Near(sec.x+sec.width,160+158*1.5f,1.f),"the seconds trail at 158 u");
	// The last 10 s turn the seconds #FF3333 over 100 ms, and back.
	Check(runtime.SetState({{"currenttime",9.0}},error,.7),error.c_str());
	Check(runtime.RunEvent("updatetimer",.7,effects,error),"the countdown runs into its last 10 s");
	runtime.Frame(RemasteredViewport(1280,720),.75);
	auto colour = runtime.PresentedValue("race-sec","color");
	Check(colour && colour->data[1] < .8 && colour->data[1] > .2,"the seconds are turning red");
	runtime.Frame(RemasteredViewport(1280,720),.85);
	colour = runtime.PresentedValue("race-sec","color");
	Check(colour && Near(float(colour->data[0]),1) && Near(float(colour->data[1]),.2f,.01f) && Near(float(colour->data[2]),.2f,.01f),
		"the last 10 s read #FF3333");
	Check(runtime.SetState({{"currenttime",30.0}},error,.9),error.c_str());
	Check(runtime.RunEvent("updatetimer",.9,effects,error),"a new area adds time");
	runtime.Frame(RemasteredViewport(1280,720),1.05);
	colour = runtime.PresentedValue("race-sec","color");
	Check(colour && Near(float(colour->data[0]),.882f,.01f) && Near(float(colour->data[2]),.404f,.01f),"more time turns the seconds gold");
	Check(runtime.RunEvent("hidespeedertimer",1.1,effects,error) && runtime.RunEvent("hidespeedertimer2",1.1,effects,error),"the timer hides");
	runtime.Frame(RemasteredViewport(1280,720),1.15);
	Check(!Shown(runtime,"race-sec") && !Shown(runtime,"race-hun"),"a hidden timer leaves the plate");

	// The prompt: Boost with the crouch key while ready, Boosting while it
	// pushes, nothing while it recharges; Exit (or the lock) trails.
	Check(runtime.RunEvent("showExitmessage",1.2,effects,error),"boarding runs");
	runtime.Frame(RemasteredViewport(1280,720),1.25);
	const auto crouch = runtime.PresentedValue("vpr-boost-key","text");
	Check(Shown(runtime,"vpr") && Shown(runtime,"vpr-boost") && !Shown(runtime,"vpr-boosting") && Shown(runtime,"vpr-exit") && crouch &&
		crouch->text == "C","a ready boost shows the crouch key before Exit");
	Bounds boostItem, exitItem, prompt;
	Check(runtime.GetBounds("vpr-boost",boostItem) && runtime.GetBounds("vpr-exit",exitItem) && boostItem.x < exitItem.x &&
		Near(exitItem.x-(boostItem.x+boostItem.width),11*1.5f,.6f) && runtime.GetBounds("vpr",prompt) &&
		Near(prompt.x+prompt.width,320+629*1.5f,1.5f),"Boost leads Exit by 11 u and the prompt trails at 629 u");
	Check(runtime.SetState({{"oq4_boost",1.0},{"oq4_boost_envelope",0.5}},error,1.3),error.c_str());
	host.streaks.clear();
	runtime.Frame(RemasteredViewport(1280,720),1.35);
	Check(Shown(runtime,"vpr-boosting") && !Shown(runtime,"vpr-boost"),"a pushing boost shows the hazard and Boosting");
	Check(host.streaks.size() == 1 && Near(host.streaks[0],.5f),"the view streaks with the boost's envelope");
	host.reducedMotion = true; host.streaks.clear();
	runtime.Frame(RemasteredViewport(1280,720),1.4);
	Check(host.streaks.empty(),"reduced motion never streaks");
	host.reducedMotion = false;
	Check(runtime.SetState({{"oq4_boost",2.0},{"oq4_boost_envelope",0.0}},error,1.45),error.c_str());
	host.streaks.clear();
	runtime.Frame(RemasteredViewport(1280,720),1.5);
	Check(!Shown(runtime,"vpr-boosting") && !Shown(runtime,"vpr-boost") && Shown(runtime,"vpr-exit") && host.streaks.empty(),
		"a recharging boost leaves only Exit and no streak");
	Check(runtime.SetState({{"vehicle_locked",1.0}},error,1.55),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),1.6);
	Check(Shown(runtime,"vpr-lock") && !Shown(runtime,"vpr-exit"),"the race's lock shows Vehicle locked");

	// The entry lines the expansion's HUD never played: the cannon's first.
	const auto line1 = runtime.PresentedValue("vmsg1-text","text"), line2 = runtime.PresentedValue("vmsg2-text","text");
	Check(Shown(runtime,"vmsg") && line1 && line1->text == "#str_200939" && line2 && line2->text == "#str_200940",
		"boarding with the cannon names the machinegun first");

	// Minimal keeps the status bar, the race display and the prompt, never the streak.
	RemasteredLayoutPreference(host,2);
	Check(runtime.SetState({{"oq4_boost",1.0},{"oq4_boost_envelope",1.0}},error,1.7),error.c_str());
	host.streaks.clear();
	runtime.Frame(RemasteredViewport(1280,720),1.75);
	Check(!Shown(runtime,"tr-full") && !Shown(runtime,"hit-full") && Shown(runtime,"race-plate") && Shown(runtime,"vpr") &&
		runtime.GetBounds("vhull-gauge",hull) && host.streaks.empty(),"Minimal keeps the race display, status bar and prompt without the streak");
	RemasteredLayoutPreference(host,1);
	Check(host.errors == 0,"no runtime diagnostics across the speeder exercise");
	host.softFocus = false;
	runtime.Shutdown();
}

// The Awakening's GEV (gev_hud.gui, 14.12), whose art never shipped: the
// status cluster centred on the bottom edge, the health bar draining from the
// top, the hull turning under the turret, the shield stages and the failed
// shields' pulse, the turned weapon glyph over the charge bar, and Exit.
static void CheckRemasteredGevHud(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%s %u:%u %s\n",path,unsigned(d.line),unsigned(d.column),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.hud.remastered.gev","Remastered GEV HUD validates");
	Check(document.Model().canvasHeight == 720 && document.Model().canvasMinWidth == 960,"the GEV HUD uses the 4:3 center-screen canvas");
	RemasteredLayoutPreference(host,1);
	Runtime runtime(host);
	Check(runtime.Initialize(),"retained runtime");
	Check(runtime.LoadDocument(source,"guis/hud/remastered_gev.q4ui",diagnostics),"Remastered GEV HUD loads into a runtime");
	std::string error;
	Runtime::EventEffects effects;
	StateValues values = RemasteredLayout(1280,720);
	values.insert({{"vehicle_health",1.0},{"vehicle_shield",1.0},{"vehicle_weapon",0.0},{"vehicle_weaponammo",0.0},
		{"vehicle_weaponcharge",1.0},{"vehicle_rotate",0.0},{"vehicle_locked",0.0},{"oq4_key_jump",std::string("SPACE")}});
	Check(runtime.SetState(values,error,0),error.c_str());
	Check(runtime.RunEvent("hudJoin",0,effects,error),"the join program runs");
	host.draws.clear();
	runtime.Frame(RemasteredViewport(1280,720),.1);
	Check(host.errors == 0,"the GEV HUD frames without runtime errors");

	// The cluster centres on the bottom edge: its sheared plates (black 0.60)
	// span 7.0-212.0 u in the stock and 210.5 u further on.
	Bounds cluster;
	Check(runtime.GetBounds("gev",cluster) && Near(cluster.x,160+210.5f*1.5f,1.f),"the cluster moves 210.5 u toward the centre");
	float left = 1e9f, right = -1e9f;
	for (const auto& draw : host.draws) for (const auto& v : draw.vertices) {
		if (std::fabs(v.a-.6f) > .003f || v.r > .01f) continue;
		left = std::min(left,v.x); right = std::max(right,v.x);
	}
	Check(Near(left,160+(210.5f+7.04f)*1.5f,1.5f) && Near(right,160+(210.5f+211.96f)*1.5f,1.5f) && Near((left+right)*.5f,640,1.f),
		"the plates centre on the bottom edge");

	// The health bar drains from the top: #FF9E1A keeps its foot at 465 u.
	const auto healthExtent = [&]() {
		float top = 1e9f, bottom = -1e9f;
		for (const auto& draw : host.draws) for (const auto& v : draw.vertices) {
			if (v.a < .05f || v.x > 640 || std::fabs(v.r/v.a-1) > .03f || std::fabs(v.g/v.a-.621f) > .03f || std::fabs(v.b/v.a-.102f) > .03f) continue;
			top = std::min(top,v.y); bottom = std::max(bottom,v.y);
		}
		return std::pair{top,bottom};
	};
	const auto whole = healthExtent();
	Check(Near(whole.first,419*1.5f,1.5f) && Near(whole.second,465*1.5f,1.5f),"a whole GEV fills the 46 u bar");
	Check(runtime.SetState({{"vehicle_health",0.5}},error,.2),error.c_str());
	host.draws.clear(); runtime.Frame(RemasteredViewport(1280,720),.25);
	const auto half = healthExtent();
	const auto drain = runtime.PresentedValue("gev-health-fill","transform");
	Check(Near(half.first,(419+23)*1.5f,1.5f) && Near(half.second,465*1.5f,1.5f) && drain && Near(float(drain->data[1]),23*1.5f*.5f,.05f) &&
		Near(float(drain->data[3]),.5f,.001f),"half the health shortens the bar from the top");

	// The hull turns with the view relative to it; the turret stays put.
	Check(runtime.SetState({{"vehicle_rotate",30.0}},error,.3),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.35);
	const auto turn = runtime.PresentedValue("gev-hull","transform");
	Check(turn && Near(float(turn->data[4]),-30,.01f),"a view 30 degrees right of the hull turns the hull counter-clockwise");
	Bounds glyph;
	Check(runtime.GetBounds("gev-glyph",glyph) && Near(glyph.x+glyph.width*.5f,160+(210.5f+77)*1.5f,1.f) &&
		Near(glyph.y+glyph.height*.5f,444*1.5f,1.f),"the hull glyph sits on the stock base's centre");

	// The shield stages fade out in turn; at none, the inner arcs pulse.
	const auto opacity = [&](const char* id) {
		const auto value = runtime.PresentedValue(id,"opacity");
		return value ? float(value->data[0]) : -1.f;
	};
	Check(Near(opacity("gev-shield1"),1) && Near(opacity("gev-shield2"),1) && Near(opacity("gev-shield3"),1) &&
		!Shown(runtime,"gev-shield-out"),"full shields light all three stages");
	Check(runtime.SetState({{"vehicle_shield",0.6}},error,.4),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.45);
	Check(Near(opacity("gev-shield1"),0) && Near(opacity("gev-shield2"),.5f) && Near(opacity("gev-shield3"),1),
		"60% shields drop the outer stage and halve the middle one");
	Check(runtime.SetState({{"vehicle_shield",0.1}},error,.5),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.55);
	Check(Near(opacity("gev-shield2"),0) && Near(opacity("gev-shield3"),.25f) && !Shown(runtime,"gev-shield-out"),
		"10% shields leave a quarter of the inner stage");
	Check(runtime.SetState({{"vehicle_shield",0.0}},error,.6),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),2.2);
	const float lit = opacity("gev-shield-out-lit");
	runtime.Frame(RemasteredViewport(1280,720),2.7);
	const float dim = opacity("gev-shield-out-lit");
	Check(Shown(runtime,"gev-shield-out") && Near(opacity("gev-shield3"),0) && Near(lit,1) && Near(dim,0),
		"failed shields pulse #FF591A to #80591A every 500 ms");
	host.reducedMotion = true;
	runtime.Frame(RemasteredViewport(1280,720),2.75);
	runtime.Frame(RemasteredViewport(1280,720),3.25);
	Check(Near(opacity("gev-shield-out-lit"),1),"reduced motion holds the lit colour");
	host.reducedMotion = false;

	// The weapon: the cannon's glyph turned 60 degrees with the infinity mark;
	// the rocket pods' count instead; the charge bar from its leading end.
	Check(Shown(runtime,"gev-weapon0") && !Shown(runtime,"gev-weapon1") && !Shown(runtime,"gev-weapon2") &&
		Shown(runtime,"gev-infinity") && !Shown(runtime,"gev-count"),"the cannon shows its glyph and the infinity mark");
	const auto turned = runtime.PresentedValue("gev-weapon0","transform");
	Check(turned && Near(float(turned->data[4]),60),"the weapon glyph turns 60 degrees");
	Check(runtime.SetState({{"vehicle_weapon",2.0},{"vehicle_weaponammo",12.0},{"vehicle_weaponcharge",0.5}},error,3.3),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),3.35);
	const auto count = runtime.PresentedValue("gev-count-text","text");
	const auto charge = runtime.PresentedValue("gev-charge-fill","transform");
	Check(Shown(runtime,"gev-weapon2") && !Shown(runtime,"gev-weapon0") && Shown(runtime,"gev-count") && !Shown(runtime,"gev-infinity") &&
		count && count->text == "12","the rocket pods count their rockets");
	Check(charge && Near(float(charge->data[0]),-26.5f*1.5f*.5f,.05f) && Near(float(charge->data[2]),.5f,.001f),
		"half a charge fills half the bar from its leading end");

	// The opaque-backing option deepens the plates.
	host.cvars["ui_retainedOpaqueBacking"] = true;
	runtime.Frame(RemasteredViewport(1280,720),3.4);
	Check(Shown(runtime,"gev-plate-hull-opaque") && !Shown(runtime,"gev-plate-hull"),"opaque backing deepens the plates");
	host.cvars["ui_retainedOpaqueBacking"] = false;

	// The HUD prompt: Exit with the jump key, or the lock; the transmission
	// display top-trailing.
	Check(runtime.RunEvent("showExitmessage",3.5,effects,error),"boarding runs");
	runtime.Frame(RemasteredViewport(1280,720),3.55);
	Bounds prompt;
	Check(Shown(runtime,"vpr") && Shown(runtime,"vpr-exit") && !Shown(runtime,"vpr-lock") && runtime.GetBounds("vpr",prompt) &&
		Near(prompt.x+prompt.width,320+629*1.5f,1.5f),"Exit trails at 629 u");
	Check(runtime.SetState({{"vehicle_locked",1.0}},error,3.6),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),3.65);
	Check(Shown(runtime,"vpr-lock") && !Shown(runtime,"vpr-exit"),"a locked GEV shows Vehicle locked");
	Check(runtime.RunEvent("radioChatterUp",3.7,effects,error),"a transmission runs");
	runtime.Frame(RemasteredViewport(1280,720),3.75);
	Check(Shown(runtime,"radio"),"the transmission display shows");

	// Minimal keeps the cluster and the prompt.
	RemasteredLayoutPreference(host,2);
	runtime.Frame(RemasteredViewport(1280,720),3.8);
	Check(!Shown(runtime,"tr-full") && Shown(runtime,"vpr") && runtime.GetBounds("gev",cluster),"Minimal keeps the cluster and the prompt");
	RemasteredLayoutPreference(host,1);
	Check(host.errors == 0,"no runtime diagnostics across the GEV exercise");
	runtime.Shutdown();
}

// The Medical Labs implant activation (14.19): hidden until `activate`, the
// cells lit two passes over from 500 ms in the stock order, the corner pieces
// and readouts fading from 4150 ms and the overlay gone at 4650 ms; every
// activate restarts it. `hurt` and `saveMessage` run on their own. Reduced
// motion keeps steady faint cells and still readouts, and no travelling data.
static void CheckRemasteredMedchangeHud(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%s %u:%u %s\n",path,unsigned(d.line),unsigned(d.column),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.hud.remastered.medchange","Remastered implant overlay validates");
	Check(document.Model().canvasHeight == 720 && document.Model().canvasMinWidth == 960,"the implant overlay uses the 4:3 center-screen canvas");
	RemasteredLayoutPreference(host,1);
	Runtime runtime(host);
	Check(runtime.Initialize(),"retained runtime");
	Check(runtime.LoadDocument(source,"guis/hud/remastered_medchange.q4ui",diagnostics),"Remastered implant overlay loads into a runtime");
	std::string error;
	Runtime::EventEffects effects;
	Check(runtime.SetState(RemasteredLayout(1280,720),error,0),error.c_str());
	Check(runtime.RunEvent("hudJoin",0,effects,error),"the join program runs");
	runtime.Frame(RemasteredViewport(1280,720),.1);
	Check(host.errors == 0,"the implant overlay frames without runtime errors");
	Check(!Shown(runtime,"med-all") && !Shown(runtime,"med-hurt") && !Shown(runtime,"med-saved"),"the overlay starts hidden");
	const auto opacity = [&](const char* id) {
		const auto value = runtime.PresentedValue(id,"opacity");
		return value ? float(value->data[0]) : -1.f;
	};

	// The first cell (hex10) lights at 500 ms and fades over 500 ms; the
	// sixth (hex4) waits until 750 ms; the readouts cycle every 100 ms.
	Check(runtime.RunEvent("activate",1,effects,error),"activate runs");
	runtime.Frame(RemasteredViewport(1280,720),1.55);
	Check(Shown(runtime,"med-all") && Near(opacity("med-p0"),.9f,.03f) && Near(opacity("med-p5"),0,.001f),
		"the cells light in the stock order from 500 ms");
	Check(Near(opacity("med-proc0-v5"),1,.001f) && Near(opacity("med-proc0-v0"),0,.001f),"the processor readout steps every 100 ms");
	Check(Near(opacity("med-x-v5"),1,.001f) && Near(opacity("med-x-v0"),0,.001f) && Near(opacity("med-z-v2"),1,.001f),
		"the X/Y/Z readouts cycle the stock data at 100 and 200 ms");
	const auto fall = runtime.PresentedValue("med-data0","transform");
	runtime.Frame(RemasteredViewport(1280,720),1.6);
	const auto fell = runtime.PresentedValue("med-data0","transform");
	Check(Shown(runtime,"med-data") && fall && fell && float(fell->data[1]) > float(fall->data[1]),"the rune data falls");
	// The second pass: hex10 again 37 steps later, at 2350 ms.
	runtime.Frame(RemasteredViewport(1280,720),3.4);
	Check(Near(opacity("med-p0"),.9f,.03f),"the second pass relights the first cell");
	// From 4150 ms the corner pieces and readouts fade, and at 4650 ms the overlay hides.
	runtime.Frame(RemasteredViewport(1280,720),5.2);
	Check(Near(opacity("med-frame"),.9f,.03f) && Shown(runtime,"med-all"),"the frame fades from 4150 ms");
	runtime.Frame(RemasteredViewport(1280,720),5.7);
	Check(!Shown(runtime,"med-all"),"the overlay hides at 4650 ms");

	// Another activate restarts every clock.
	Check(runtime.RunEvent("activate",6,effects,error),"activate runs again");
	runtime.Frame(RemasteredViewport(1280,720),6.55);
	Check(Shown(runtime,"med-all") && Near(opacity("med-p0"),.9f,.03f) && Near(opacity("med-frame"),1,.001f),"a repeat activation starts over");

	// Hurt: a red edge pulse from 0.6 over a second, with the overlay or without.
	Check(runtime.RunEvent("hurt",6.6,effects,error),"hurt runs");
	runtime.Frame(RemasteredViewport(1280,720),7.1);
	Check(Shown(runtime,"med-hurt") && Near(opacity("med-hurt"),.3f,.03f),"the hurt pulse falls from 0.6");
	runtime.Frame(RemasteredViewport(1280,720),7.7);
	Check(!Shown(runtime,"med-hurt"),"the hurt pulse ends after a second");

	// The save notice: 2000 ms, then fading over 1000 ms.
	Check(runtime.RunEvent("saveMessage",7.8,effects,error),"the save notice runs");
	runtime.Frame(RemasteredViewport(1280,720),8.9);
	Check(Shown(runtime,"med-saved") && Near(opacity("med-saved"),1,.001f),"the save notice shows");
	runtime.Frame(RemasteredViewport(1280,720),10.9);
	Check(!Shown(runtime,"med-saved"),"the save notice leaves after 3 s");

	// Reduced motion: steady faint cells, still readouts and no travelling data.
	host.reducedMotion = true;
	runtime.SetReducedMotion(true,11);
	Check(runtime.RunEvent("activate",11,effects,error),"activate runs under reduced motion");
	runtime.Frame(RemasteredViewport(1280,720),11.2);
	Check(Near(opacity("med-p0"),.1f,.005f) && Near(opacity("med-p20"),.1f,.005f),"reduced motion keeps steady faint cells");
	runtime.Frame(RemasteredViewport(1280,720),11.65);
	Check(Near(opacity("med-p0"),.1f,.005f) && Near(opacity("med-proc0-v0"),1,.001f) && Near(opacity("med-proc0-v3"),0,.001f),
		"no cell flashes and the readouts hold still");
	Check(!Shown(runtime,"med-data"),"reduced motion leaves out the travelling data");
	runtime.Frame(RemasteredViewport(1280,720),15.4);
	Check(Near(opacity("med-center"),.5f,.03f),"the steady cells fade with the frame");
	runtime.Frame(RemasteredViewport(1280,720),15.7);
	Check(!Shown(runtime,"med-all"),"the timed activation still hides at 4650 ms");
	host.reducedMotion = false;
	runtime.SetReducedMotion(false,15.8);
	Check(host.errors == 0,"no runtime diagnostics across the implant overlay exercise");
	runtime.Shutdown();
}

// The objectives display and notices (wristcomm.gui, 14.11).
static void CheckRemasteredObjectivesHud(ScreenHost& host, const char* path, bool strogg) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%s %u:%u %s\n",path,unsigned(d.line),unsigned(d.column),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == std::string("openq4.hud.remastered.objectives")+(strogg ? ".strogg" : ""),
		"Remastered objectives validate");
	RemasteredLayoutPreference(host,1);
	Runtime runtime(host);
	Check(runtime.Initialize(),"retained runtime");
	Check(runtime.LoadDocument(source,"guis/hud/remastered_objectives.q4ui",diagnostics),"Remastered objectives load into a runtime");
	std::string error;
	Runtime::EventEffects effects;
	Check(runtime.SetState(RemasteredLayout(1280,720),error,0),error.c_str());
	Check(runtime.RunEvent("hudJoin",0,effects,error),"the join program runs");
	runtime.Frame(RemasteredViewport(1280,720),.1);
	Check(host.errors == 0 && !Shown(runtime,"on") && !Shown(runtime,"od") && !Shown(runtime,"odisp"),"nothing shows at rest");

	// The new-objective notice: the top-leading 200 u plate, its own title.
	Check(runtime.SetState({{"oq4_new_title",std::string("Reach the hangar control room")},
		{"oq4_new_shot",std::string("gfx/objectives/none")}},error,.2),error.c_str());
	Check(runtime.RunEvent("newObjective",.2,effects,error),"a new objective runs");
	runtime.Frame(RemasteredViewport(1280,720),.2+.3);
	Check(Shown(runtime,"on"),"the new-objective notice shows");
	const auto frame = runtime.PresentedValue("on-frame","opacity"), text = runtime.PresentedValue("on-text","opacity");
	Check(frame && Near(float(frame->data[0]),.4f,.01f) && text && Near(float(text->data[0]),1,.01f),"the frame rests at 0.40 under full text");
	const auto title = runtime.PresentedValue("on-title","text");
	Check(title && title->text == "Reach the hangar control room","the notice names the new objective");
	Bounds head;
	Check(runtime.GetBounds("on-head",head) && Near(head.x,9*1.5f,1.f),"the heading sits at the leading corner");
	// The completion notice: mirrored at the top-trailing corner, on its own timer.
	Check(runtime.SetState({{"oq4_done_title",std::string("Find the comms relay")}},error,.6),error.c_str());
	Check(runtime.RunEvent("newObjectiveComplete",.6,effects,error),"a completion runs");
	runtime.Frame(RemasteredViewport(1280,720),.6+.3);
	Bounds done;
	Check(Shown(runtime,"od") && runtime.GetBounds("od-head",done) && Near(done.x+done.width,320+631*1.5f,1.f),
		"the completion heading trails at 631 u");
	Check(runtime.RunEvent("oq4_closeNewObjective",1,effects,error),"the new notice closes");
	runtime.Frame(RemasteredViewport(1280,720),1.2);
	Check(!Shown(runtime,"on") && Shown(runtime,"od"),"closing one notice leaves the other up");
	Check(runtime.RunEvent("oq4_closeDoneObjective",1.3,effects,error),"the completion closes");
	runtime.Frame(RemasteredViewport(1280,720),1.5);
	Check(!Shown(runtime,"od"),"both notices are gone");

	// The display: five objectives scroll three at a time, then the completed ones.
	StateValues list = {{"oq4_obj_count",5.0},{"oq4_done_count",2.0},{"oq4_obj_scroll",0.0},{"oq4_done0",std::string("Done A")},
		{"oq4_done1",std::string("Done B")}};
	for (int i = 0; i < 5; ++i) {
		list[("oq4_obj"+std::to_string(i)+"_title")] = std::string("Objective ")+std::to_string(i);
		list[("oq4_obj"+std::to_string(i)+"_text")] = std::string("Description of objective ")+std::to_string(i);
	}
	Check(runtime.SetState(list,error,1.6),error.c_str());
	Check(runtime.RunEvent("wristcommShow",1.6,effects,error),"the display opens");
	runtime.Frame(RemasteredViewport(1280,720),1.6+.3);
	Check(Shown(runtime,"odisp") && Shown(runtime,"odisp-e0") && Shown(runtime,"odisp-e2") && !Shown(runtime,"odisp-e3"),
		"three entries show at once");
	Check(Shown(runtime,"odisp-scroll") && Shown(runtime,"odisp-more") && !Shown(runtime,"odisp-done"),
		"a longer list shows its scrollbar and the collapsed COMPLETED header");
	Bounds e0, e1;
	Check(runtime.GetBounds("odisp-e0",e0) && runtime.GetBounds("odisp-e1",e1) && Near(e1.y-e0.y,143*1.5f,1.5f),
		"entries keep the stock 143 u pitch");
	Check(runtime.SetState({{"oq4_obj_scroll",3.0}},error,2),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),2.05);
	Check(!Shown(runtime,"odisp-e0") && Shown(runtime,"odisp-e3") && Shown(runtime,"odisp-e4") && Shown(runtime,"odisp-done") &&
		Shown(runtime,"odisp-d1") && !Shown(runtime,"odisp-more"),"scrolled to the end, the completed objectives follow");
	Check(runtime.SetState({{"oq4_obj_count",0.0},{"oq4_done_count",0.0},{"oq4_obj_scroll",0.0}},error,2.1),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),2.15);
	Check(Shown(runtime,"odisp-empty") && !Shown(runtime,"odisp-scroll"),"no objective shows the empty plate");
	// Held (the stock), the display has no prompt; with Toggle Objectives the
	// HUD prompt names the scores control and Close at the trailing corner.
	Check(!Shown(runtime,"opr"),"a held display shows no Close prompt");
	Check(runtime.SetState({{"oq4_obj_toggle",true},{"oq4_key_scores",std::string("TAB")}},error,2.16),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),2.17);
	const auto scores = runtime.PresentedValue("opr-key","text");
	Bounds closePrompt;
	Check(Shown(runtime,"opr") && scores && scores->text == "TAB" && runtime.GetBounds("opr",closePrompt) &&
		Near(closePrompt.x+closePrompt.width,320+629*1.5f,1.5f) && Near(closePrompt.y+closePrompt.height,476*1.5f,1.5f),
		"a toggled display shows the scores key and Close at the bottom-trailing corner");
	Check(runtime.RunEvent("wristcommHide",2.2,effects,error),"the display closes");
	runtime.Frame(RemasteredViewport(1280,720),2.25);
	Check(!Shown(runtime,"odisp") && !Shown(runtime,"opr"),"the display and its prompt are gone");

	// A failed objective names itself.
	Check(runtime.SetState({{"oq4_failed_title",std::string("Protect the convoy")}},error,2.3),error.c_str());
	Check(runtime.RunEvent("objectiveFailed",2.3,effects,error),"a failure runs");
	runtime.Frame(RemasteredViewport(1280,720),2.35);
	const auto failed = runtime.PresentedValue("of-title","text");
	Check(Shown(runtime,"of") && failed && failed->text == "Protect the convoy","the failure names the objective");

	// Minimal drops the new and completed notices; a failure still shows.
	RemasteredLayoutPreference(host,2);
	runtime.Frame(RemasteredViewport(1280,720),2.4);
	Check(!Shown(runtime,"tl-full") && !Shown(runtime,"tr-full") && Shown(runtime,"of"),"Minimal keeps only the failure of the notices");
	RemasteredLayoutPreference(host,1);
	runtime.Frame(RemasteredViewport(1280,720),2.45);
	Check(Shown(runtime,"tl-full") && Shown(runtime,"tr-full"),"Remastered brings the notices back");
	Check(host.errors == 0,"no runtime diagnostics across the objectives exercise");
	runtime.Shutdown();
}

// The scoreboard (scoreboard.gui, 14.14): the multiplayer card as tall as its
// lists, its header and match line, panels, rows, your marker, bots, friends,
// the dead, ping bars, scrolling and Tourney's tree.
static void CheckRemasteredScoreboardHud(ScreenHost& host, const char* path) {
	const auto source = Read(path);
	Document document; std::vector<Diagnostic> diagnostics;
	if (!document.Load(source,diagnostics)) for (const auto& d : diagnostics) std::fprintf(stderr,"%s %u:%u %s\n",path,unsigned(d.line),unsigned(d.column),d.message.c_str());
	Check(diagnostics.empty() && document.Model().id == "openq4.hud.remastered.scoreboard","Remastered scoreboard validates");
	RemasteredLayoutPreference(host,1);
	Runtime runtime(host);
	Check(runtime.Initialize(),"retained runtime");
	Check(runtime.LoadDocument(source,"guis/hud/remastered_scoreboard.q4ui",diagnostics),"Remastered scoreboard loads into a runtime");
	std::string error;
	Runtime::EventEffects effects;
	StateValues values = RemasteredLayout(1280,720);
	values.insert({{"servername",std::string("openQ4 Community")},{"serverip",std::string("203.0.113.24:28004")},
		{"servergametype",std::string("Capture the Flag")},{"servermap",std::string("q4ctf1")},{"serverlimit",std::string("Capture Limit: 5")},
		{"servertimelimit",std::string("Time Limit: 15")},{"timeleft",std::string("7:12")},{"infinity",false},
		{"oq4_sb_pitch",16.0},{"oq4_sb_kills",true},{"oq4_sb_rank",std::string("3RD OF 13")},
		{"oq4_sb0_kind",1.0},{"oq4_sb0_score",std::string("3")},{"oq4_sb0_count",2.0},
		{"oq4_sb0_0_name",std::string("You")},{"oq4_sb0_0_score",21.0},{"oq4_sb0_0_ping",24.0},{"oq4_sb0_0_flags",257.0},
		{"oq4_sb0_1_name",std::string("Bolt")},{"oq4_sb0_1_score",9.0},{"oq4_sb0_1_flags",4.0},
		{"oq4_sb1_kind",2.0},{"oq4_sb1_score",std::string("2")},{"oq4_sb1_count",1.0},
		{"oq4_sb1_0_name",std::string("Vex")},{"oq4_sb1_0_score",24.0},{"oq4_sb1_0_ping",150.0},{"oq4_sb1_0_flags",2.0},
		{"oq4_sb2_kind",3.0},{"oq4_sb2_count",1.0},{"oq4_sb2_0_name",std::string("Quill")},{"oq4_sb2_0_ping",44.0}});
	Check(runtime.SetState(values,error,0),error.c_str());
	Check(runtime.RunEvent("hudJoin",0,effects,error),"the join program runs");
	runtime.Frame(RemasteredViewport(1280,720),.1);
	Check(host.errors == 0,"the scoreboard frames without runtime errors");
	// The card: 98 dp to the lists, each panel's 6 dp and 22 dp band and its
	// 24 dp rows, then 36 dp under them, centred over the view.
	Bounds card;
	Check(Shown(runtime,"sb-frame") && runtime.GetBounds("sb-card",card) && Near(card.width,744.f,1.f) && Near(card.height,98.f+76+52+52+36,1.f) &&
		Near(card.x+card.width/2,640.f,1.f) && Near(card.y+card.height/2,360.f,1.f),"the card is as tall as its lists, centred over the view");
	const auto title = runtime.PresentedValue("sb-title","text"), mode = runtime.PresentedValue("sb-mode","text");
	Check(Shown(runtime,"sb-title") && title && title->text == "openQ4 Community" && !Shown(runtime,"sb-title-t") && Shown(runtime,"sb-match") &&
		mode && mode->text == "Capture the Flag","the header names the server and the match line the mode");
	Check(Shown(runtime,"sb0") && Shown(runtime,"sb0-band1") && !Shown(runtime,"sb0-band2") && Shown(runtime,"sb1-band2") &&
		Shown(runtime,"sb2-band3") && Shown(runtime,"sb0-title1") && Shown(runtime,"sb2-title3"),
		"your team, the other team and the spectators each open with their band and title");
	Check(Shown(runtime,"sb0r0") && Shown(runtime,"sb0r1") && !Shown(runtime,"sb0r2"),"a panel shows its rows");
	const auto mine = runtime.PresentedValue("sb0r0-band","opacity"), theirs = runtime.PresentedValue("sb0r1-band","opacity");
	Check(mine && Near(float(mine->data[0]),.29f,.01f) && theirs && Near(float(theirs->data[0]),.08f,.01f) && Shown(runtime,"sb0r0-marker") &&
		!Shown(runtime,"sb0r1-marker"),"your row brightens its band and adds the marker");
	const auto pal = runtime.PresentedValue("sb0r0-friend","opacity"), stranger = runtime.PresentedValue("sb0r1-friend","opacity");
	Check(pal && Near(float(pal->data[0]),1.f,.01f) && stranger && Near(float(stranger->data[0]),.125f,.01f),"a friend's symbol lights");
	Check(Shown(runtime,"sb0r1-bot") && !Shown(runtime,"sb0r1-clan") && !Shown(runtime,"sb0r1-meter") && Shown(runtime,"sb0r1-bars"),
		"a bot carries the BOT tag and no ping bars, keeping their room");
	const auto dim = runtime.PresentedValue("sb1r0-content","opacity");
	Check(dim && Near(float(dim->data[0]),.5f,.01f) && Shown(runtime,"sb1r0-skull"),"a dead player's row drops to 0.50 behind a skull");
	Check(Shown(runtime,"sb0r0-bars4") && Shown(runtime,"sb1r0-bars2"),"ping shows the matching network-quality bars");
	const auto watcher = runtime.PresentedValue("sb2r0-score","opacity");
	Check(Shown(runtime,"sb-h-kills") && watcher && Near(float(watcher->data[0]),0.f,.01f),"KILLS shows in flag modes; spectators have no scores");
	Bounds r0, r1;
	Check(runtime.GetBounds("sb0r0",r0) && runtime.GetBounds("sb0r1",r1) && Near(r1.y-r0.y,16*1.5f,1.f),"rows keep the published pitch");
	const auto rank = runtime.PresentedValue("sb-rank","text");
	Check(Shown(runtime,"sb-rank") && rank && rank->text == "3RD OF 13","the header reads your place");
	Check(Shown(runtime,"sb-clock"),"a timed match shows the clock");
	Check(runtime.SetState({{"oq4_sb1_kind",-1.0},{"oq4_sb0_kind",0.0},{"oq4_sb_kills",false},{"oq4_sb_pitch",20.0},{"infinity",true}},error,.2),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.25);
	Check(Shown(runtime,"sb0-band0") && !Shown(runtime,"sb1") && !Shown(runtime,"sb-h-kills") && !Shown(runtime,"sb-clock"),
		"deathmatch lists the players in one panel without KILLS");
	Check(runtime.GetBounds("sb0r0",r0) && runtime.GetBounds("sb0r1",r1) && Near(r1.y-r0.y,20*1.5f,1.f) && runtime.GetBounds("sb-card",card) &&
		Near(card.height,98.f+(28+60)+(28+30)+36,1.f),"the rows and the card follow the pitch");
	// Beyond its height the board scrolls, over the count of the rows below.
	Check(!Shown(runtime,"sb-more"),"a board that fits counts no rows below");
	Check(runtime.SetState({{"oq4_sb_below",7.0},{"oq4_sb_below_text",std::string("7 MORE")}},error,.26),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.27);
	const auto more = runtime.PresentedValue("sb-more-text","text"), address = runtime.PresentedValue("sb-address","text");
	Check(Shown(runtime,"sb-more") && Shown(runtime,"sb-more-arrow") && more && more->text == "7 MORE" && address &&
		address->text == "203.0.113.24:28004","a scrolling board counts the rows below beside the address");
	Check(runtime.SetState({{"oq4_sb_below",0.0},{"oq4_sb_below_text",std::string()},{"oq4_sb_pitch",16.0}},error,.28),error.c_str());
	runtime.Frame(RemasteredViewport(1280,720),.29);
	Check(!Shown(runtime,"sb-more"),"the count leaves at the end of the list");

	// Tourney: the wider card sets the bracket chart under its match line, and
	// the chart answers the stock arena events.
	Check(runtime.SetState({{"gametype",2.0},{"name1_round1_bracket1",std::string("Vex")},{"name2_round1_bracket1",std::string("Kaz")},
		{"score1_round1_bracket1",std::string("3")},{"score2_round1_bracket1",std::string("1")},{"round",1.0},{"bracket",1.0},
		{"empty",false},{"tourney_count",std::string("Playing tournament 1 of 3")}},error,.3),error.c_str());
	Check(runtime.RunEvent("arenaInit",.3,effects,error),"an arena starts");
	runtime.Frame(RemasteredViewport(1280,720),.35);
	const auto count = runtime.PresentedValue("sb-count","text");
	Check(Shown(runtime,"sbt") && Shown(runtime,"sb-title-t") && !Shown(runtime,"sb-title") && Shown(runtime,"sb-match") &&
		Shown(runtime,"sb-count") && !Shown(runtime,"sb-limit") && count && count->text == "Playing tournament 1 of 3" &&
		Shown(runtime,"tb11-words") && Shown(runtime,"tb11-plain") && !Shown(runtime,"tb11-empty"),
		"Tourney sets the bracket chart in its card, the tournament count on the match line");
	Bounds panelTop, first, final_, semi;
	Check(runtime.GetBounds("sb-card",card) && Near(card.width,928.f,1.f) && runtime.GetBounds("sb0",panelTop) && panelTop.y - card.y >= 329.f,
		"the wider card lists the players under the chart");
	Check(runtime.GetBounds("tb11",first) && runtime.GetBounds("tb41",final_) && runtime.GetBounds("tb31",semi) &&
		Near(first.width,110.f,1.f) && Near(final_.width,136.f,1.f) && Near(final_.x+final_.width/2,card.x+card.width/2,1.f) &&
		Near(semi.y+semi.height/2,final_.y+final_.height/2,1.f),"the halves converge on the wider final in the card's centre");
	Check(runtime.SetState({{"sel",1.0}},error,.4) && runtime.RunEvent("arenaFocus",.4,effects,error),"your arena is focused");
	runtime.Frame(RemasteredViewport(1280,720),.45);
	Check(Shown(runtime,"tb11-sel1") && Shown(runtime,"tb11-mine") && !Shown(runtime,"tb11-plain") && !Shown(runtime,"tb11-focus"),
		"your arena's rails and your half turn aqua");
	const auto unlit = runtime.PresentedValue("tl11","opacity");
	Check(unlit && Near(float(unlit->data[0]),.22f,.01f),"an undecided arena's link waits unlit");
	Check(runtime.SetState({{"winner",1.0}},error,.5) && runtime.RunEvent("arenaDone",.5,effects,error),"the arena finishes");
	runtime.Frame(RemasteredViewport(1280,720),.55);
	const auto loser = runtime.PresentedValue("tb11-n2","color"), victor = runtime.PresentedValue("tb11-s1","color"),
		lit = runtime.PresentedValue("tl11","opacity");
	Check(Shown(runtime,"tb11-done") && !Shown(runtime,"tb11-sel1") && !Shown(runtime,"tb11-mine") && loser && Near(float(loser->data[3]),.45f,.01f) &&
		victor && Near(float(victor->data[0]),1,.01f) && Near(float(victor->data[2]),.137f,.01f) && lit && Near(float(lit->data[0]),.7f,.01f),
		"a decided arena turns blue, its loser dims, the winner's score takes the value color and its link lights");
	Check(runtime.SetState({{"round",2.0},{"bracket",3.0},{"empty",true}},error,.6) && runtime.RunEvent("arenaInit",.6,effects,error),
		"an empty arena starts");
	runtime.Frame(RemasteredViewport(1280,720),.65);
	Check(Shown(runtime,"tb23-empty") && !Shown(runtime,"tb23-words"),"an empty arena reads EMPTY");
	Check(runtime.SetState({{"round",4.0},{"bracket",1.0},{"empty",false},{"name1_round4_bracket1",std::string("Vex")},
		{"name2_round4_bracket1",std::string("Rook")}},error,.7) && runtime.RunEvent("arenaInit",.7,effects,error) &&
		runtime.SetState({{"winner",2.0}},error,.75) && runtime.RunEvent("arenaDone",.75,effects,error),"the final is decided");
	runtime.Frame(RemasteredViewport(1280,720),.8);
	Check(Shown(runtime,"tb41-champion2") && !Shown(runtime,"tb41-champion1"),"the champion gains the marker");
	Check(host.errors == 0,"no runtime diagnostics across the scoreboard exercise");
	runtime.Shutdown();
}

// --hud-bench <document>: per-operation cost of a Remastered HUD document in
// the runtime (events, state batches, frames), for performance work.
static void BenchRemasteredHud(const char* path) {
	ScreenHost host;
	host.cacheLayers=true;
	const auto source = Read(path);
	std::vector<Diagnostic> diagnostics;
	RemasteredLayoutPreference(host,1);
	Runtime runtime(host);
	Check(runtime.Initialize(),"retained runtime");
	const bool loaded=runtime.LoadDocument(source,"guis/hud/bench.q4ui",diagnostics);
	for (const auto& diagnostic:diagnostics) std::fprintf(stderr,"HUD_BENCH %s: %s\n",diagnostic.pointer.c_str(),diagnostic.message.c_str());
	Check(loaded,"bench document loads");
	std::string error;
	Runtime::EventEffects effects;
	StateValues values = RemasteredLayout(1280,720);
	values.insert({{"gametype",1.0}});
	runtime.SetState(values,error,0);
	runtime.RunEvent("hudJoin",0,effects,error);
	runtime.Frame(RemasteredViewport(1280,720),.01);
	using clock = std::chrono::steady_clock;
	const auto ms = [](clock::time_point a, clock::time_point b) { return std::chrono::duration<double,std::milli>(b-a).count(); };
	double t = .02;
	auto start = clock::now();
	// An event the game sends often: a health update on foot, a transmit change in multiplayer.
	const char* event = runtime.HasEvent("updateHealth") ? "updateHealth" : "hide_transmit";
	for (int i = 0; i < 50; ++i) { runtime.RunEvent(event,t,effects,error); t += .016; }
	const double eventMs = ms(start,clock::now())/50;
	start = clock::now();
	for (int i = 0; i < 50; ++i) { runtime.SetState({{"hud.scale",1.0+(i%2)*1e-6}},error,t); t += .016; }
	const double stateMs = ms(start,clock::now())/50;
	start = clock::now();
	for (int i = 0; i < 50; ++i) { runtime.Frame(RemasteredViewport(1280,720),t); t += .016; }
	const double frameMs = ms(start,clock::now())/50;
	double changedFrameMs = 0, changedFrameOnlyMs = 0; std::uint64_t scanned = 0;
	for (int i = 0; i < 50; ++i) {
		const auto a = clock::now();
		// A readout change, as the game makes: a number on foot, the clock in multiplayer.
		if (!runtime.SetState({{"player_health",double(50+i%2)}},error,t))
			runtime.SetState({{"oq4_clock",std::string(i%2 ? "7:05" : "7:04")}},error,t);
		const auto b = clock::now();
		runtime.Frame(RemasteredViewport(1280,720),t); t += .016;
		const auto c = clock::now();
		changedFrameMs += ms(a,c); changedFrameOnlyMs += ms(b,c); scanned += runtime.Statistics().motionInputsScanned;
	}
	changedFrameMs /= 50; changedFrameOnlyMs /= 50;
	std::printf("HUD_BENCH_CHANGED frame_after_change_ms=%.3f scanned=%llu\n",changedFrameOnlyMs,(unsigned long long)(scanned/50));
	std::printf("HUD_BENCH document=%s event_ms=%.3f state_ms=%.3f frame_ms=%.3f state_frame_ms=%.3f errors=%d\n",path,eventMs,stateMs,frameMs,changedFrameMs,host.errors);
	runtime.Frame(RemasteredViewport(1280,720),t);
	const auto s = runtime.Statistics();
	std::printf("HUD_BENCH_FRAME update_ms=%.3f render_ms=%.3f vector_compile_ms=%.3f vectors=%llu compiled=%llu hits=%llu geometry_compiles=%llu draws=%llu vertices=%llu layers=%llu layer_cache_hits=%llu direct_masks=%llu\n",
		s.updateMilliseconds,s.renderMilliseconds,s.vectorCompileMilliseconds,(unsigned long long)s.vectorElements,(unsigned long long)s.vectorPathsCompiled,
		(unsigned long long)s.vectorCacheHits,(unsigned long long)s.geometryCompiles,(unsigned long long)s.drawCalls,(unsigned long long)s.submittedVertices,
		(unsigned long long)s.layerComposites,(unsigned long long)s.layerCacheHits,(unsigned long long)s.directMaskDraws);
	runtime.Shutdown();
}

// Bounded playback-graph cost, with actual state evaluation and drawing.
// Synthetic peaks avoid microphone/network input; engine audio_graphs.py
// separately exercises the real decoded sound channel and render target.
static void BenchHudAudio(const char* path) {
	ScreenHost host; RemasteredLayoutPreference(host,1);
	Runtime runtime(host); std::vector<Diagnostic> diagnostics; std::string error; Runtime::EventEffects effects;
	Check(runtime.LoadDocument(Read(path),"audio-bench.q4ui",diagnostics),"audio benchmark document loads");
	const bool radio=runtime.HasEvent("radioChatterUp");
	const bool live=runtime.GetState().contains(radio ? "radio.peak.0" : "voice.self.peak.0");
	Check(runtime.SetState(RemasteredLayout(1280,720),error,0) && runtime.RunEvent("hudJoin",0,effects,error),"audio benchmark starts");
	double time=0;
	for (int mode : {0,1,2,0}) {
		for (const char* event : radio ? std::vector<const char*>{mode ? "radioChatterUp" : "radioChatterDown"} :
			std::vector<const char*>{mode ? "show_transmit_self" : "hide_transmit_self",mode ? "show_transmit" : "hide_transmit"})
			Check(runtime.RunEvent(event,time,effects,error),"audio benchmark visibility");
		double total=0,render=0,update=0; std::uint64_t vertices=0,compiled=0;
		for (int frame=0;frame<140;++frame) {
			time+=1.0/60; host.draws.clear();
			StateValues values;
			if (mode && live) for (const char* prefix : radio ? std::vector<const char*>{"radio.peak."} :
				std::vector<const char*>{"voice.self.peak.","voice.other.peak."})
				for (int i=0;i<40;++i) values[std::string(prefix)+std::to_string(i)]=mode==1 ? .5 : double((frame*7+i*13)%101)/100;
			const auto start=std::chrono::steady_clock::now();
			Check(runtime.SetState(values,error,time),"audio benchmark peak batch");
			runtime.Frame(RemasteredViewport(1280,720),time);
			if (frame<20) continue;
			total+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
			const auto& s=runtime.Statistics(); render+=s.renderMilliseconds; update+=s.updateMilliseconds;
			vertices+=s.submittedVertices; compiled+=s.vectorPathsCompiled;
		}
		std::printf("AUDIO_BENCH document=%s mode=%s frame_ms=%.4f update_ms=%.4f render_ms=%.4f vertices=%llu compiled=%.2f\n",
			path,mode==0 ? "hidden" : mode==1 ? "steady" : "changing",total/120,update/120,render/120,
			(unsigned long long)(vertices/120),double(compiled)/120);
	}
	Check(host.errors==0,"audio benchmark has no runtime diagnostics");
}
