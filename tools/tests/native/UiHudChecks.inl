// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// The Remastered HUD's anchors (HudLayoutFor, published by RetainedHud.cpp):
// with no window inset they are the layout from before the window's safe area
// counted, at every view, scale, inset and width setting; with insets each edge
// keeps the larger of the player's distance and the window's, so readouts on
// the edge anchors stand in the safe rectangle and the center column fits it.
static void CheckHudLayout() {
	const auto ratio=[](int width,int height,const std::array<float,4>& safe) {
		Viewport viewport; viewport.width=width; viewport.height=height; viewport.canvasHeight=720; viewport.canvasMinWidth=960;
		viewport.safeLeft=safe[0]; viewport.safeTop=safe[1]; viewport.safeRight=safe[2]; viewport.safeBottom=safe[3];
		return viewport.DpRatio();
	};
	// The player's anchors (section 14.4) at a canvas ratio, without the window.
	const auto player=[](int width,int height,float ratio,float scale,float percent,int hudWidth) {
		const float w=width/ratio,h=height/ratio;
		const float cap=hudWidth==0 ? 1280.f : hudWidth==1 ? 720.f*64.f/27.f : w;
		const float side=std::max(0.f,(w-std::min(w,cap))*.5f);
		const float inset=percent*.01f;
		const float span=w-2*(side+inset*w);
		return HudLayout{w,h,side+inset*w,inset*h,side+inset*w,inset*h,scale,std::min(scale,std::max(.25f,span/690.f))};
	};
	const auto same=[](const HudLayout& a,const HudLayout& b) {
		return Near(a.width,b.width,1e-3f) && Near(a.height,b.height,1e-3f) && Near(a.left,b.left,1e-3f) && Near(a.top,b.top,1e-3f) &&
			Near(a.right,b.right,1e-3f) && Near(a.bottom,b.bottom,1e-3f) && Near(a.scale,b.scale,1e-6f) && Near(a.centerScale,b.centerScale,1e-6f);
	};
	const std::pair<int,int> views[]={{1280,720},{1920,1080},{2520,1080},{3840,1080},{1024,768},{960,720},{1280,1024},{720,1280},{1080,2400},{2400,1080}};
	for (const auto& [width,height]:views) for (float scale:{.5f,1.f,1.5f,2.f}) for (float percent:{0.f,2.5f,10.f}) for (int hudWidth:{0,1,2}) {
		const float plain=ratio(width,height,{});
		Check(plain==std::min(height/720.f,width/960.f),"with no window inset the HUD canvas fills the view");
		Check(same(HudLayoutFor(float(width),float(height),plain,{0,0,0,0},hudWidth,percent,scale),player(width,height,plain,scale,percent,hudWidth)),
			"with no window inset the HUD anchors are the player's alone");
	}
	struct Shape { int width,height; std::array<float,4> safe; };
	// A phone in portrait (status bar and cutout, gesture bar), in landscape
	// (leading cutout, trailing rounded corner, gesture bar) and rounded corners.
	for (const Shape& shape:{Shape{1080,2400,{0,96,0,48}},Shape{2400,1080,{96,0,24,40}},Shape{1280,720,{24,24,24,24}}})
		for (float percent:{0.f,2.5f,10.f}) for (int hudWidth:{0,1,2}) for (float scale:{1.f,2.f}) {
			const float dp=ratio(shape.width,shape.height,shape.safe);
			const auto layout=HudLayoutFor(float(shape.width),float(shape.height),dp,shape.safe,hudWidth,percent,scale);
			const auto own=player(shape.width,shape.height,dp,scale,percent,hudWidth);
			Check(Near(layout.width*dp,float(shape.width),.01f) && Near(layout.height*dp,float(shape.height),.01f),
				"the HUD's view stays the whole window, so full-screen effects stay full-bleed");
			Check(Near(layout.left,std::max(own.left,shape.safe[0]/dp),1e-3f) && Near(layout.top,std::max(own.top,shape.safe[1]/dp),1e-3f) &&
				Near(layout.right,std::max(own.right,shape.safe[2]/dp),1e-3f) && Near(layout.bottom,std::max(own.bottom,shape.safe[3]/dp),1e-3f),
				"each edge anchor keeps the larger of the player's distance and the window's inset");
			Check(layout.left*dp>=shape.safe[0]-.01f && layout.top*dp>=shape.safe[1]-.01f && layout.right*dp>=shape.safe[2]-.01f &&
				layout.bottom*dp>=shape.safe[3]-.01f,"the edge anchors stand in the safe rectangle");
			// The 690 dp center column, centered on the view, fits between the
			// side anchors unless the narrowest scale already applies.
			const float half=345*layout.centerScale;
			Check(layout.centerScale<=.25f || (layout.width/2-half>=layout.left-1e-3f && layout.width/2+half<=layout.width-layout.right+1e-3f),
				"the center column fits the safe width");
		}
	// The 16:9 width cap's side screen already clears a leading cutout: nothing moves.
	{
		const float dp=ratio(2400,1080,{96,0,0,0});
		const auto layout=HudLayoutFor(2400,1080,dp,{96,0,0,0},0,0,1);
		Check(Near(layout.left,160,1e-3f) && Near(layout.right,160,1e-3f),"a cutout inside the capped side screen moves no anchor");
	}
	// A larger Safe Area Inset wins over the window's.
	{
		const float dp=ratio(1080,2400,{0,96,0,48});
		const auto layout=HudLayoutFor(1080,2400,dp,{0,96,0,48},1,10,1);
		Check(Near(layout.top,.1f*layout.height,1e-3f) && Near(layout.bottom,.1f*layout.height,1e-3f),"a deeper Safe Area Inset keeps its place");
	}
	// An inset that is not a distance counts as none.
	Check(same(HudLayoutFor(1280,720,1,{-20,NAN,INFINITY,0},1,0,1),HudLayoutFor(1280,720,1,{0,0,0,0},1,0,1)),"a malformed window inset counts as none");
}

static void CheckHud(const char* path) {
	const HudAmmoReadout loaded{"machinegun",30,90,true}, fired{"machinegun",29,90,true}, reloaded{"machinegun",40,80,true};
	Check(HudAmmoGains({},loaded)==0,"joining does not count the initial ammunition as a gain");
	Check(HudAmmoGains(loaded,fired)==0,"firing never pops a readout");
	Check(HudAmmoGains(fired,reloaded)==HudAmmoGainClip,"reload pops only the rising clip, not the reserve");
	Check(HudAmmoGains(loaded,{"machinegun",30,110,true})==HudAmmoGainReserve,"pickup pops only the rising reserve");
	Check(HudAmmoGains(loaded,{"shotgun",50,150,true})==0,"switching weapon cannot look like an ammunition gain");
	Check(HudAmmoGains({"blaster",-1,-1,false},{"blaster",-1,100,false})==0,"unlimited ammunition is not a numeric gain");
	Check(HudAmmoGains({"railgun",-1,20,false},{"railgun",-1,30,false})==HudAmmoGainTotal,"a clipless or MP weapon pops its total");
	CheckHudLayout();
	const auto source=Read(path); Document doc; std::vector<Diagnostic> diagnostics;
	if (!doc.Load(source,diagnostics)) for (const auto& d:diagnostics) std::fprintf(stderr,"%s\n",d.message.c_str());
	Check(diagnostics.empty(),"HUD artwork validates as an editable canonical document");
	ScreenHost host; Runtime runtime(host);
	// RmlUi sees a VFS resource name, never a host drive path/protocol.
	const char* resource="guis/hud/artwork.q4ui";
	Check(runtime.LoadDocument(source,resource,diagnostics),"HUD catalog loads through retained runtime");
	Viewport viewport; viewport.width=1280;viewport.height=720;
	std::string error;
	int tested=0;
	for (const auto& node:doc.Model().root.children) {
		HudPrimitive primitive;primitive.artwork=node.id;primitive.quad={100,100,180,100,180,180,100,180};
		Check(runtime.SetHudPresentation({primitive},viewport,error),error.c_str());
		host.draws.clear();runtime.Frame(viewport,.001*tested++);
		Check(host.errors==0,"every HUD symbol and panel compiles without runtime errors");
		if (node.id!="clear") Check(!host.draws.empty(),"HUD artwork produces native retained geometry");
	}
	for (bool strogg:{false,true}) {
		HudPrimitive trace; trace.artwork=strogg?"strogg-ekg":"ekg";
		const float w=strogg?129.f:125.f,h=strogg?67.f:59.f;
		trace.quad={0,0,w,0,w,h,0,h};
		Check(runtime.SetHudPresentation({trace},viewport,error),"Classic EKG prepares");
		host.draws.clear();runtime.Frame(viewport,.001*tested++);
		Check(!host.draws.empty(),"Classic EKG has native geometry");
		for (const auto& draw:host.draws) for (const auto& v:draw.vertices) {
			if (v.r==0) continue; // Unlit antialias vertices emit no additive light.
			const float x=v.x*640/viewport.width,y=v.y*480/viewport.height;
			// Analytic antialias coverage extends to the containing pixel cell.
			const float fringe=std::max(640.f/viewport.width,480.f/viewport.height);
			Check(x>=(strogg?5.1f:5.4f)-fringe && x<=(strogg?124.f:119.6f)+fringe &&
				y>=(strogg?15.7f:13.8f)-fringe && y<=(strogg?48.7f:44.7f)+fringe,"Classic EKG is clipped inside the fill plate");
			if (!strogg && y>34.6f+fringe) {
				// A pixel corner extends diagonally beyond a curved mask edge.
				const float diagonal=std::hypot(640.f/viewport.width,480.f/viewport.height);
				const bool clears=x<109 && (x-109)*(x-109)+(y-50.8f)*(y-50.8f)>=(16.6f-diagonal)*(16.6f-diagonal);
				if (!clears) std::fprintf(stderr,"Classic EKG notch vertex %.4f %.4f, coverage %.4f, fringe %.4f\n",x,y,v.r,fringe);
				Check(clears,"Classic EKG clears the concave icon notch");
			}
		}
	}
	for (const char* art:{"direction","direction-chevron"}) {
		HudPrimitive hit;hit.artwork=art;hit.quad={0,0,640,0,640,480,0,480};
		Check(runtime.SetHudPresentation({hit},viewport,error),"Classic directional artwork prepares");
		host.draws.clear();runtime.Frame(viewport,.001*tested++);
		Check(!host.draws.empty(),"Classic directional artwork renders");
		const bool dome=std::string(art)=="direction";
		for (const auto& draw:host.draws) for (const auto& v:draw.vertices) {
			if (v.r==0) continue;
			const float x=v.x*640/viewport.width,y=v.y*480/viewport.height;
			Check(x>=(dome?169.f:274.f) && x<=(dome?471.f:366.f) &&
				y>=(dome?36.f:125.f) && y<=(dome?142.f:153.f),
				"Classic hit dome and missile chevron retain their transparent canvas margins");
		}
	}
	HudPrimitive item;item.artwork="solid";item.quad={20,20,60,30,70,60,30,50};item.uv={.25f,.25f,.75f,.75f};
	Check(runtime.SetHudPresentation({item},viewport,error),"clipped sheared HUD primitive prepares");
	host.draws.clear();runtime.Frame(viewport,1);
	Check(!host.draws.empty(),"clipped sheared primitive renders");
	for (const auto& draw:host.draws) for (const auto& v:draw.vertices) {
		// Normalised coordinates invert the viewport mapping; every emitted
		// vertex must lie inside all four directed edges of the sheared clip.
		const float x=v.x*640/viewport.width,y=v.y*480/viewport.height;
		for (int e=0;e<4;++e) {
			const int n=(e+1)%4;
			const float side=(item.quad[2*n]-item.quad[2*e])*(y-item.quad[2*e+1])-
				(item.quad[2*n+1]-item.quad[2*e+1])*(x-item.quad[2*e]);
			Check(side>=-.001f,"HUD clipping preserves the exact transformed boundary");
		}
	}
	const auto first=host.draws.front().vertices.front();
	item.colour={.2f,.4f,.8f,.5f};
	Check(runtime.SetHudPresentation({item},viewport,error),"HUD recolour prepares without editing catalog");
	host.draws.clear();runtime.Frame(viewport,2);
	Check(!host.draws.empty() && host.draws.front().vertices.front().a < first.a,"HUD tint updates cached geometry");
	Check(runtime.SetHudPresentation({item},viewport,error),"unchanged frame accepted");
	runtime.Frame(viewport,3);Check(runtime.Statistics().vectorUploads==0,"unchanged HUD geometry is retained");
	runtime.Frame(viewport,3.1);
	Check(runtime.Statistics().frameReplays==1,"unchanged captured HUD replays its presentation");
	const auto count=host.draws.size();
	item.uv={1,1,0,0};Check(runtime.SetHudPresentation({item},viewport,error),"mirrored HUD art prepares");runtime.Frame(viewport,4);
	Check(runtime.Statistics().frameReplays==0 && host.draws.size()>count,"a changed captured HUD cannot replay old geometry");
	item.artwork="missing";Check(!runtime.SetHudPresentation({item},viewport,error),"unknown artwork fails before drawing any part of the HUD");
	host.draws.clear();runtime.Frame(viewport,5);Check(host.draws.empty(),"failed frame cannot present stale or partial geometry");
	item.artwork="solid";viewport.width=720;viewport.height=1280;
	Check(runtime.SetHudPresentation({item},viewport,error),"portrait viewport prepares");runtime.Frame(viewport,6);
	Check(runtime.SetHudPresentation({},viewport,error),"hidden HUD clears presentation");
	host.draws.clear();runtime.Frame(viewport,7);Check(host.draws.empty(),"hidden HUD draws nothing");
	runtime.CloseDocument();
	Check(runtime.LoadDocument(source,resource,diagnostics) && runtime.SetHudPresentation({item},viewport,error),"HUD recreates after document/resource reload");
	runtime.Frame(viewport,8);Check(host.errors==0,"HUD lifetime clean");
	HudPrimitive glyph;glyph.material="hud-test-font";glyph.glyph=true;
	glyph.quad={0,0,32,0,32,32,0,32};glyph.uv={.125f,.25f,.375f,.75f};
	Check(runtime.SetHudPresentation({glyph},viewport,error),"font atlas is prepared before the frame");
	host.draws.clear();runtime.Frame(viewport,9);
	Check(host.draws.size()==1 && Near(host.draws[0].vertices[0].u,.125f,.001f) &&
		Near(host.draws[0].vertices[2].v,.75f,.001f),"glyph atlas UVs survive the retained submission");
	host.missingMaterials.insert("hud-missing-photo");glyph.material="hud-missing-photo";
	Check(!runtime.SetHudPresentation({item,glyph},viewport,error),"a missing picture fails the whole frame before publication");
	host.draws.clear();runtime.Frame(viewport,10);Check(host.draws.empty(),"missing picture cannot leave a partial HUD");
	runtime.Shutdown();
	Check(runtime.LoadDocument(source,resource,diagnostics) && runtime.SetHudPresentation({item},viewport,error),"HUD survives the renderer's shutdown/reload path");
	runtime.Frame(viewport,11);
	std::printf("HUD: %d editable vector symbols/panels exercised\n",tested);
}
