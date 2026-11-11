// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
static void CheckStatusBars(ScreenHost& host, char** paths) {
	host.cvars["ui_retainedOpaqueBacking"] = false;
	for (int family=0; family<2; ++family) {
		Runtime hud(host); std::vector<Diagnostic> diagnostics; std::string error;
		const bool loaded=hud.Initialize() && hud.LoadDocument(Read(paths[family]),"guis/hud/status.rui",diagnostics);
		for (const auto& d:diagnostics) std::fprintf(stderr,"status %s: %s\n",d.pointer.c_str(),d.message.c_str());
		Check(loaded,"status bar document loads through the production schema");
		Check(hud.SetState({{"health.1",std::string("1")},{"health.2",std::string("0")},{"health.3",std::string("0")},
			{"ammo.2",std::string("4")},{"ammo.3",std::string("0")},{"clip",true},{"weapon",1.0}},error,0),"live digits and selected weapon bind");
		Viewport viewport; viewport.canvasHeight=720; viewport.canvasMinWidth=960;
		for (const auto size : {std::pair{1280,720},std::pair{1920,540},std::pair{405,720},std::pair{3840,2160}}) {
			viewport.width=size.first; viewport.height=size.second;
			host.draws.clear(); hud.Frame(viewport,1);
			Bounds before,after,bar;
			Check(hud.GetBounds("health-gauge",before) && hud.GetBounds("status-bar",bar),"gauges lay out");
			Check(Near(bar.x+bar.width/2,size.first/2.f,1.5f),"whole status bar stays centered at every aspect ratio");
			Check(bar.y>=0 && bar.y+bar.height<=size.second+1,"status bar stays on the bottom edge including portrait");
			Check(hud.SetState({{"armor.visible",false},{"armor.fill",0.0}},error,1),"armor can disappear");
			host.draws.clear();hud.Frame(viewport,1.1); Check(hud.GetBounds("health-gauge",after),"health persists without armor");
			Check(Near(before.x,after.x,.01f),"armor disappearance never moves the status bar");
			Check(hud.PresentedValue("armor-gauge","display")->text=="none","zero armor plate is hidden");
			Check(hud.SetState({{"armor.visible",true},{"armor.fill",.5},{"health.fill",.5}},error,1.1),"armor returns and values drain");
			host.draws.clear();hud.Frame(viewport,1.2);
			const auto fill=hud.PresentedValue("health-fill","width");
			Check(fill && Near(float(fill->data[0]),1.5f*(family?119:115)*.5f,.001f),"half health clips half the unscaled fill width");
			size_t vertices=0;for(const auto& draw:host.draws)vertices+=draw.vertices.size();
			std::printf("status family=%d viewport=%dx%d vertices=%zu\n",family,size.first,size.second,vertices);
			Check(vertices*64<16*1024*1024,"gauges use less than one sixth of the 96 MiB Vulkan GUI ring even at 4K");
		}
		Check(hud.SetState({{"low",true},{"pulse",.65},{"health.gain",0.0}},error,2),"damage crosses the low-health boundary");hud.Frame(viewport,2);
		Check(hud.PresentedValue("health-track-low","display")->text=="block" && hud.PresentedValue("health-track-normal","display")->text=="none","low health switches the plate color immediately");
		Check(Near(float(hud.PresentedValue("health-rim","opacity")->data[0]),.65f,.001f),"low-health rim follows the real-time pulse");
		Check(hud.SetState({{"low",false},{"health.gain",1.0}},error,2.1),"recovery publishes a gain");hud.Frame(viewport,2.1);
		Check(hud.PresentedValue("health-rim","opacity")->data[0]==0,"recovery clears low-health feedback immediately");
		Check(Near(float(hud.PresentedValue("health-number","transform")->data[2]),1.2f,.001f),"gain enlarges digits to 120 percent");
		Check(hud.PresentedValue("health-number-3","text")->text=="0","tabular readout uses live family glyphs");
		Check(hud.SetState({{"gun",false},{"ammo.gain",.6}},error,2.2),"legacy silhouette visibility and ammunition gains bind");hud.Frame(viewport,2.2);
		Check(hud.PresentedValue("weapon-art1","display")->text=="none","script-hidden weapon art stays hidden");
		Check(Near(float(hud.PresentedValue("ammo-icon-gain1","opacity")->data[0]),.6f,.001f),"ammo gains brighten the selected icon");
		Check(hud.SetState({{"mp",true},{"clip",false},{"weapon",7.0}},error,3),"multiplayer total-ammo mode");hud.Frame(viewport,3);
		Check(hud.PresentedValue("reserve-number","display")->text=="none","MP and clipless weapons show no reserve");
		Check(hud.PresentedValue("ammo-fill-mp7","display")->text=="block","MP railgun selects its green fill");
		Check(hud.PresentedValue("weapon-art7","display")->text=="none","MP hides the SP weapon silhouette");
		hud.SetReducedMotion(true,4);
		Check(hud.SetState({{"phase",.25}},error,4),"instrument phase updates under reduced motion");hud.Frame(viewport,4);
		Check(Near(float(hud.PresentedValue("ekg-normal","transform")->data[0]),23.4375f,.001f),"EKG remains a moving instrument under reduced motion");
		host.draws.clear();
	}
}
