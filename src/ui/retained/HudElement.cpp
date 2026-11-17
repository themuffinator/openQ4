// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "HudElement.h"
#include "Runtime.h"
#include "VectorElement.h"
#include <RmlUi/Core/RenderManager.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace openq4::ui {
namespace {
VectorVertex Mix(const VectorVertex& a, const VectorVertex& b, double t) {
	return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t,a.a+(b.a-a.a)*t};
}
// GUI clipping precedes its affine transform. Clip to that exact transformed
// quad, including mirrors/shears, instead of replacing it with an AABB.
VectorMesh Clip(const VectorMesh& mesh, const std::array<VectorPoint,4>& quad) {
	const auto cross = [](VectorPoint a,VectorPoint b,VectorPoint p) { return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x); };
	const double sign = cross(quad[0],quad[1],quad[2]) >= 0 ? 1 : -1;
	// Which of the quad's edges each vertex lies outside of.
	std::vector<unsigned char> outside(mesh.vertices.size());
	bool inside = true;
	for (size_t i=0;i<mesh.vertices.size();++i) {
		unsigned char code = 0;
		for (int edge=0;edge<4;++edge)
			if (sign*cross(quad[edge],quad[(edge+1)%4],{mesh.vertices[i].x,mesh.vertices[i].y})<0) code |= 1<<edge;
		outside[i] = code; inside = inside && !code;
	}
	// The usual crosshair is already inside its window. Preserve its disjoint
	// coverage quads instead of splitting them into duplicated triangle lists;
	// the host can then cache the exact pixel coverage in its atlas.
	if (inside) return mesh;
	VectorMesh result;
	std::vector<int> kept(mesh.vertices.size(),-1);
	std::vector<VectorVertex> polygon, clipped;
	for (size_t i=0;i+2<mesh.indices.size();i+=3) {
		const int a=mesh.indices[i],b=mesh.indices[i+1],c=mesh.indices[i+2];
		// Wholly beyond one edge: nothing of it survives the clip.
		if (outside[a] & outside[b] & outside[c]) continue;
		// Wholly inside: the clip would return it unchanged.
		if (!(outside[a] | outside[b] | outside[c])) {
			for (const int index:{a,b,c}) {
				if (kept[index]<0) { kept[index]=static_cast<int>(result.vertices.size()); result.vertices.push_back(mesh.vertices[index]); }
				result.indices.push_back(kept[index]);
			}
			continue;
		}
		polygon.assign({mesh.vertices[a],mesh.vertices[b],mesh.vertices[c]});
		for (int edge = 0; edge < 4 && !polygon.empty(); ++edge) {
			clipped.clear();
			auto previous = polygon.back();
			double before = sign*cross(quad[edge],quad[(edge+1)%4],{previous.x,previous.y});
			for (const auto& next : polygon) {
				const double after = sign*cross(quad[edge],quad[(edge+1)%4],{next.x,next.y});
				if ((before >= 0) != (after >= 0)) clipped.push_back(Mix(previous,next,before/(before-after)));
				if (after >= 0) clipped.push_back(next);
				previous=next; before=after;
			}
			polygon.swap(clipped);
		}
		if (polygon.size()<3) continue;
		const int base=static_cast<int>(result.vertices.size());
		result.vertices.insert(result.vertices.end(),polygon.begin(),polygon.end());
		for (size_t n=1;n+1<polygon.size();++n) result.indices.insert(result.indices.end(),{base,base+static_cast<int>(n),base+static_cast<int>(n+1)});
	}
	return result;
}
// The output cells a quad can touch. Coverage outside them is clipped away
// anyway, so a window onto a large design only rasterises its own part.
VectorPixelBounds QuadBounds(const std::array<VectorPoint,4>& quad,int width,int height) {
	double x0=quad[0].x,x1=x0,y0=quad[0].y,y1=y0;
	for (const auto& p:quad) { x0=std::min(x0,p.x); x1=std::max(x1,p.x); y0=std::min(y0,p.y); y1=std::max(y1,p.y); }
	const auto cell=[](double value,int extent) { return static_cast<int>(std::clamp(value,0.0,static_cast<double>(extent))); };
	return {cell(std::floor(x0)-1,width),cell(std::floor(y0)-1,height),cell(std::ceil(x1)+1,width),cell(std::ceil(y1)+1,height)};
}
// Artwork that is one antialiased solid fill of its whole frame, whatever the
// window or tiling a quad asks of it, fills exactly that quad.
std::optional<VectorColour> SolidFill(const Node& node) {
	if (node.paths.size()!=1) return std::nullopt;
	const auto& path=node.paths.front();
	if (path.fill.type!=PaintType::Solid || path.stroke.paint.type!=PaintType::None || path.blend!=PathBlend::Normal ||
		!path.antialias || path.commands.size()!=5 || path.commands.front().op!=PathOperation::Move ||
		path.commands.back().op!=PathOperation::Close) return std::nullopt;
	bool corners[4]={};
	for (size_t i=0;i<4;++i) {
		if (i && path.commands[i].op!=PathOperation::Line) return std::nullopt;
		const auto p=path.commands[i].points[0].Resolve(1,1);
		const int corner=p.x==0 && p.y==0 ? 0 : p.x==1 && p.y==0 ? 1 : p.x==1 && p.y==1 ? 2 : p.x==0 && p.y==1 ? 3 : -1;
		if (corner<0 || corners[corner]) return std::nullopt;
		corners[corner]=true;
	}
	return path.fill.colour;
}
}
// Every point a path can paint, in the artwork's 128 dp frame: its control
// points hold the curves, and a stroke reaches out by its widest join or cap.
HudElement::PathExtent HudElement::PathExtent::Of(const VectorPath& path) {
	PathExtent extent;
	double x0=std::numeric_limits<double>::infinity(),y0=x0,x1=-x0,y1=-x0;
	for (const auto& command:path.commands) {
		const int count=command.op==PathOperation::Move || command.op==PathOperation::Line ? 1 :
			command.op==PathOperation::Quadratic ? 2 : command.op==PathOperation::Cubic ? 3 : 0;
		for (int i=0;i<count;++i) {
			const auto p=command.points[i].Resolve(128,128);
			x0=std::min(x0,p.x); x1=std::max(x1,p.x); y0=std::min(y0,p.y); y1=std::max(y1,p.y);
		}
	}
	if (!std::isfinite(x0) || !std::isfinite(x1) || !std::isfinite(y0) || !std::isfinite(y1)) return extent;
	if (path.stroke.paint.type!=PaintType::None) {
		// half the width from the centre line, a square cap's corner further,
		// and a miter join out to its limit
		const double cap=path.stroke.cap==StrokeCap::Square ? 1.5 : 1;
		const double reach=path.stroke.join==StrokeJoin::Miter ? std::max(path.stroke.miterLimit,cap) : cap;
		const double pad=.5*path.stroke.widthDp*reach;
		if (!std::isfinite(pad) || !std::isfinite(path.stroke.minimumPixels)) return extent;
		x0-=pad; y0-=pad; x1+=pad; y1+=pad;
		extent.pixels=.5*path.stroke.minimumPixels*reach;
	}
	extent.low={x0,y0}; extent.high={x1,y1}; extent.known=true;
	return extent;
}
bool HudElement::PathExtent::Touches(const VectorTransform& transform,const VectorPixelBounds& cells) const {
	if (!known) return true;
	double x0=std::numeric_limits<double>::infinity(),y0=x0,x1=-x0,y1=-x0;
	for (const VectorPoint corner:{low,VectorPoint{high.x,low.y},high,VectorPoint{low.x,high.y}}) {
		const auto p=transform.Apply(corner);
		x0=std::min(x0,p.x); x1=std::max(x1,p.x); y0=std::min(y0,p.y); y1=std::max(y1,p.y);
	}
	// a pixel and more for the coverage fringe and any minimum stroke width
	const double margin=pixels+2;
	return !(x1+margin<cells.left || x0-margin>cells.right || y1+margin<cells.top || y0-margin>cells.bottom);
}
const std::vector<HudElement::PathExtent>& HudElement::Extents(const Node& node) {
	auto found=extents.find(&node);
	if (found==extents.end()) {
		std::vector<PathExtent> result;
		for (const auto& path:node.paths) result.push_back(PathExtent::Of(path));
		found=extents.emplace(&node,std::move(result)).first;
	}
	return found->second;
}
namespace {
bool Finite(const HudPrimitive& item) {
	for (auto v:item.quad) if (!std::isfinite(v) || std::abs(v)>1000000) return false;
	for (auto v:item.uv) if (!std::isfinite(v) || std::abs(v)>10000) return false;
	for (auto v:item.colour) if (!std::isfinite(v)) return false;
	return true;
}
}
// The paths of an artwork window, tiled as its UVs ask, clipped to the quad.
bool HudElement::CompileWindow(const HudPrimitive& item,const Node& node,const std::array<VectorPoint,4>& quad,
	const VectorPixelBounds& bounds,Compiled& compiled,std::string& error) {
	const double du=item.uv[2]-item.uv[0],dv=item.uv[3]-item.uv[1];
	// Solid primitives have degenerate UVs; artwork still fills the quad.
	const bool solid=du==0 || dv==0;
	const double u0=solid?0:item.uv[0],v0=solid?0:item.uv[1],u1=solid?1:item.uv[2],v1=solid?1:item.uv[3];
	const int left=static_cast<int>(std::floor(std::min(u0,u1))),right=static_cast<int>(std::ceil(std::max(u0,u1)));
	const int top=static_cast<int>(std::floor(std::min(v0,v1))),bottom=static_cast<int>(std::ceil(std::max(v0,v1)));
	if ((right-left)*(bottom-top)>256) { error="HUD artwork tile limit: "+item.artwork; return false; }
	const auto& reach=Extents(node);
	for (int v=top;v<bottom;++v) for (int u=left;u<right;++u) {
		VectorOptions options; options.widthDp=128; options.heightDp=128;
		options.transform.a=(quad[1].x-quad[0].x)/(u1-u0)/128;
		options.transform.b=(quad[1].y-quad[0].y)/(u1-u0)/128;
		options.transform.c=(quad[3].x-quad[0].x)/(v1-v0)/128;
		options.transform.d=(quad[3].y-quad[0].y)/(v1-v0)/128;
		options.transform.tx=quad[0].x+(u-u0)*128*options.transform.a+(v-v0)*128*options.transform.c;
		options.transform.ty=quad[0].y+(u-u0)*128*options.transform.b+(v-v0)*128*options.transform.d;
		options.pixelBounds=bounds;
		for (size_t p=0;p<node.paths.size();++p) {
			const auto& path=node.paths[p];
			// A window onto one piece of a design skips the paths of the
			// other pieces, rather than compiling them only to clip them.
			if (!reach[p].Touches(options.transform,bounds)) continue;
			VectorMesh mesh;
			if (!TessellatePath(path,options,mesh,error)) return false;
			compiled.meshes.push_back(Clip(mesh,quad)); compiled.blends.push_back(path.blend);
		}
	}
	return true;
}
bool HudElement::Prepare(const std::vector<HudPrimitive>& items,const Node& root,int width,int height,
	RuntimeStatistics& stats,std::string& error,bool& changed) {
	changed=entries.size()!=items.size();
	// a failed pass may have left entries the batches no longer describe
	if (!ready) batched=false;
	ready=false; statistics=&stats;
	if (items.size()>32768 || width<=0 || height<=0) { error="HUD presentation exceeds its limits"; return false; }
	auto* manager=GetRenderManager();
	if (!manager) { error="HUD render manager unavailable"; return false; }
	if (catalog.empty()) {
		std::vector<const Node*> nodes{&root};
		while (!nodes.empty()) {
			const auto* node=nodes.back(); nodes.pop_back(); catalog.emplace(node->id,node);
			for (const auto& child:node->children) nodes.push_back(&child);
		}
	}
	entries.resize(items.size());
	for (size_t index=0;index<items.size();++index) {
		const auto& item=items[index]; auto& entry=entries[index];
		if (!Finite(item)) { error="Non-finite HUD presentation"; return false; }
		const bool sameShape=entry.valid && entry.width==width && entry.height==height &&
			entry.previous.generated==item.generated &&
			entry.previous.artwork==item.artwork && entry.previous.material==item.material &&
			entry.previous.quad==item.quad && entry.previous.uv==item.uv;
		if (sameShape && entry.previous.colour==item.colour) { entry.settled=std::min(entry.settled+1,255u); continue; }
		changed=true; entry.settled=0; entry.dirty=true;
		entry.geometry.clear();
		if (!sameShape) {
			entry.compiled.reset(); entry.offset={}; entry.texture={}; entry.valid=false;
			auto compiled=std::make_shared<Compiled>();
			std::array<VectorPoint,4> quad;
			for (int i=0;i<4;++i) quad[i]={item.quad[2*i]*width/640.0,item.quad[2*i+1]*height/480.0};
			if (item.artwork.empty()) {
				if (item.material.empty()) { error="HUD picture has no material"; return false; }
				entry.texture=manager->LoadTexture("material:"+item.material);
				const auto size=entry.texture.GetDimensions();
				if (size.x<=0 || size.y<=0) { error="Missing HUD image: "+item.material; return false; }
				VectorMesh mesh;
				for (const auto& p:quad) mesh.vertices.push_back({p.x,p.y,1,1,1,1});
				mesh.indices={0,1,2,0,2,3}; compiled->meshes.push_back(std::move(mesh)); compiled->blends.push_back(PathBlend::Normal);
			} else {
				const auto found=catalog.find(item.artwork);
				if (found==catalog.end() || found->second->paths.empty()) { error="Missing HUD artwork: "+item.artwork; return false; }
				// The game's bars, strokes and dots, which move every frame while
				// the crosshair animates: the quad is the shape, so its coverage
				// needs neither the path compiler nor a clip to the quad, which
				// would also cut the fringe the coverage put outside it.
				bool covered=false;
				if (item.generated) {
					auto solid=solids.find(item.artwork);
					if (solid==solids.end()) solid=solids.emplace(item.artwork,SolidFill(*found->second)).first;
					if (solid->second) {
						VectorOptions options; options.pixelBounds=QuadBounds(quad,width,height);
						VectorMesh mesh;
						if (CoverConvexPolygon(quad.data(),quad.size(),*solid->second,options,mesh,error)) {
							compiled->meshes.push_back(std::move(mesh)); compiled->blends.push_back(PathBlend::Normal);
							covered=true;
						} else if (!error.empty()) return false;
					}
				}
				if (!covered && item.generated) {
					// A window onto the game's art: its shape and its place within
					// a pixel name it, and the whole pixels it moved become the
					// entry's offset, so a piece that only moved is not compiled again.
					WindowKey key; key.artwork=item.artwork; key.uv=item.uv; key.width=width; key.height=height;
					const VectorPoint origin{std::floor(quad[0].x),std::floor(quad[0].y)};
					key.shape[0]=std::lround((quad[0].x-origin.x)*4); key.shape[1]=std::lround((quad[0].y-origin.y)*4);
					for (int i=1;i<4;++i) {
						key.shape[2*i]=std::lround((quad[i].x-quad[0].x)*64); key.shape[2*i+1]=std::lround((quad[i].y-quad[0].y)*64);
					}
					std::array<VectorPoint,4> local;
					local[0]={key.shape[0]/4.0,key.shape[1]/4.0};
					for (int i=1;i<4;++i) local[i]={local[0].x+key.shape[2*i]/64.0,local[0].y+key.shape[2*i+1]/64.0};
					auto cached=windows.find(key);
					if (cached==windows.end()) {
						if (!CompileWindow(item,*found->second,local,VectorPixelBounds{-1000000,-1000000,1000000,1000000},*compiled,error)) return false;
						if (windows.size()>=64) {
							auto oldest=windows.begin();
							for (auto it=windows.begin();it!=windows.end();++it) if (it->second.used<oldest->second.used) oldest=it;
							windows.erase(oldest);
						}
						cached=windows.emplace(std::move(key),CachedWindow{compiled,0}).first;
					}
					cached->second.used=++windowClock;
					entry.compiled=cached->second.compiled; entry.offset=origin;
					entry.previous=item; entry.width=width; entry.height=height; entry.valid=true;
					continue;
				}
				if (!covered && !CompileWindow(item,*found->second,quad,QuadBounds(quad,width,height),*compiled,error)) return false;
			}
			entry.compiled=std::move(compiled);
		}
		entry.previous=item; entry.width=width; entry.height=height; entry.valid=true;
	}
	if (changed) batched=false;
	ready=true; return true;
}
namespace {
Rml::byte Byte(double x) { return static_cast<Rml::byte>(std::lround(std::clamp(x,0.0,1.0)*255)); }
// One compiled mesh in the item's colour, appended to an RmlUi mesh.
void Append(const HudPrimitive& item,const VectorMesh& source,PathBlend blend,VectorPoint offset,Rml::Mesh& mesh) {
	const int base=static_cast<int>(mesh.vertices.size());
	const double a=std::clamp(static_cast<double>(item.colour[3]),0.0,1.0);
	mesh.vertices.reserve(mesh.vertices.size()+source.vertices.size());
	for (size_t n=0;n<source.vertices.size();++n) {
		const auto& p=source.vertices[n];
		Rml::ColourbPremultiplied colour(Byte(p.r*item.colour[0]*a),Byte(p.g*item.colour[1]*a),Byte(p.b*item.colour[2]*a),Byte(p.a*a));
		if (blend==PathBlend::Additive) colour.alpha=0;
		else if (blend==PathBlend::Multiply) colour={Byte(1-p.a*a+p.r*item.colour[0]*a),Byte(1-p.a*a+p.g*item.colour[1]*a),Byte(1-p.a*a+p.b*item.colour[2]*a),Byte(p.a*a)};
		Rml::Vector2f uv{0,0};
		if (item.artwork.empty()) uv={item.uv[(n==1 || n==2)?2:0],item.uv[n>=2?3:1]};
		mesh.vertices.push_back({{static_cast<float>(p.x+offset.x),static_cast<float>(p.y+offset.y)},colour,uv});
	}
	mesh.indices.reserve(mesh.indices.size()+source.indices.size());
	for (const int index:source.indices) mesh.indices.push_back(base+index);
}
// The game's vector pieces draw as one: consecutive ones share a geometry,
// split only where a multiply path needs its own texture or an image or glyph
// comes between. Normal and additive paths differ only in their vertices.
bool Batched(const HudPrimitive& item) { return item.generated && !item.artwork.empty(); }
}
void HudElement::Batch() {
	// Unchanged for this many passes, a piece counts as settled.
	constexpr unsigned SettledPasses=4;
	std::vector<BatchedDraw> previous; previous.swap(batches);
	batched=true;
	for (size_t first=0;first<entries.size();) {
		if (!Batched(entries[first].previous)) {
			BatchedDraw draw; draw.entry=first; batches.push_back(std::move(draw)); ++first; continue;
		}
		const bool settled=entries[first].settled>=SettledPasses;
		size_t last=first; bool dirty=entries[first].dirty;
		while (last+1<entries.size() && Batched(entries[last+1].previous) && (entries[last+1].settled>=SettledPasses)==settled)
			dirty|=entries[++last].dirty;
		BatchedDraw draw; draw.first=first; draw.last=last;
		const auto kept=std::find_if(previous.begin(),previous.end(),[&](const BatchedDraw& old) {
			return old.entry==BatchedDraw::Combined && old.first==first && old.last==last;
		});
		if (!dirty && kept!=previous.end()) { draw.geometry.swap(kept->geometry); draw.multiply.swap(kept->multiply); }
		else {
			Rml::Mesh mesh; bool open=false, multiplyRun=false;
			const auto flush=[&] {
				if (open && !mesh.indices.empty()) {
					draw.geometry.push_back(GetRenderManager()->MakeGeometry(std::move(mesh))); draw.multiply.push_back(multiplyRun);
					++statistics->vectorUploads;
				}
				mesh=Rml::Mesh(); open=false;
			};
			for (size_t index=first;index<=last;++index) {
				const auto& entry=entries[index];
				if (!entry.compiled) continue;
				const auto& compiled=*entry.compiled;
				for (size_t i=0;i<compiled.meshes.size();++i) {
					const auto& source=compiled.meshes[i];
					if (source.indices.empty()) continue;
					const bool multiply=compiled.blends[i]==PathBlend::Multiply;
					if (open && (multiply!=multiplyRun || mesh.vertices.size()+source.vertices.size()>131072)) flush();
					if (!open) { open=true; multiplyRun=multiply; }
					Append(entry.previous,source,compiled.blends[i],entry.offset,mesh);
				}
			}
			flush();
		}
		for (size_t index=first;index<=last;++index) entries[index].dirty=false;
		batches.push_back(std::move(draw));
		first=last+1;
	}
}
void HudElement::OnRender() {
	if (!ready || !statistics) return;
	auto* manager=GetRenderManager(); if (!manager) return;
	const auto state=manager->GetState(); manager->SetTransform(nullptr);
	if (!batched) Batch();
	for (auto& draw:batches) {
		if (draw.entry==BatchedDraw::Combined) {
			for (size_t i=0;i<draw.geometry.size();++i) {
				if (draw.multiply[i]) {
					if (!multiply) multiply=manager->LoadTexture(std::string("material:")+MultiplySource);
					draw.geometry[i].Render({0,0},multiply);
				} else draw.geometry[i].Render({0,0},Rml::Texture{});
			}
			continue;
		}
		auto& entry=entries[draw.entry];
		const auto& item=entry.previous;
		if (!entry.compiled) continue;
		const auto& compiled=*entry.compiled;
		if (entry.geometry.empty()) {
			for (size_t i=0;i<compiled.meshes.size();++i) {
				Rml::Mesh mesh; Append(item,compiled.meshes[i],compiled.blends[i],entry.offset,mesh);
				entry.geometry.push_back(manager->MakeGeometry(std::move(mesh))); ++statistics->vectorUploads;
			}
		}
		for (size_t i=0;i<entry.geometry.size();++i) {
			if (compiled.blends[i]==PathBlend::Multiply) {
				if (!multiply) multiply=manager->LoadTexture(std::string("material:")+MultiplySource);
				entry.geometry[i].Render({0,0},multiply);
			} else entry.geometry[i].Render({0,0},entry.texture);
		}
	}
	for (const auto& entry:entries) if (!entry.previous.artwork.empty()) ++statistics->vectorElements;
	manager->SetState(state);
}
}
