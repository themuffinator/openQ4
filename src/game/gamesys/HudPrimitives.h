// Shared SP/MP gameplay presentation. Quake 4 SDK EULA; see src/game/LICENSE.
#pragma once

// Assemble game-owned overlays once, then publish atomically through the UI.
// Stock UI and an unavailable retained view use the same evaluated geometry.
class rvHudPrimitives {
public:
	void Quad(const idVec2 (&points)[4],const idVec4& uv,const idVec4& color,const idMaterial* material,bool glyph=false) {
		uiHudPrimitive_t item;
		for (int i=0;i<4;++i) item.points[i]=points[i];
		item.uv=uv;item.color=color;item.material=material;item.glyph=glyph;items.Append(item);
	}
	void Rect(float x,float y,float width,float height,const idVec4& color,const idMaterial* material,
		const idVec4& uv=idVec4(0,0,1,1),bool glyph=false) {
		if (width<=0 || height<=0 || color.w<=0) return;
		const idVec2 points[4]={idVec2(x,y),idVec2(x+width,y),idVec2(x+width,y+height),idVec2(x,y+height)};
		Quad(points,uv,color,material,glyph);
	}
	void Draw(const char* source) const {
		if (items.Num() && !uiManager->DrawHudPrimitives(source,items)) {
			for (int i=0;i<items.Num();++i) {
				const auto& p=items[i];renderSystem->SetColor(p.color);
				const idVec2 uv[4]={idVec2(p.uv.x,p.uv.y),idVec2(p.uv.z,p.uv.y),idVec2(p.uv.z,p.uv.w),idVec2(p.uv.x,p.uv.w)};
				renderSystem->DrawStretchTri(p.points[0],p.points[3],p.points[2],uv[0],uv[3],uv[2],p.material);
				renderSystem->DrawStretchTri(p.points[0],p.points[2],p.points[1],uv[0],uv[2],uv[1],p.material);
			}
		}
		renderSystem->SetColor(colorWhite);
	}
	int Num() const { return items.Num(); }
	// A producer that draws every frame keeps one list, so its storage is
	// reused rather than grown again from nothing.
	void Clear() { items.SetNum(0,false); }
	void Reserve(int count) { if (items.NumAllocated()<count) items.Resize(count); }
private:
	idList<uiHudPrimitive_t> items;
};
