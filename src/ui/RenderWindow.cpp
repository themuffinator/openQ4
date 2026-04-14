/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "DeviceContext.h"
#include "Window.h"
#include "UserInterfaceLocal.h"
#include "RenderWindow.h"
#include "../renderer/tr_local.h"

namespace {

static idMat3 RenderWindowRotationToAxis( const idWinVec4 &rotation ) {
	return idAngles( rotation.x(), rotation.y(), rotation.z() ).ToMat3();
}

template <typename T>
static int RenderWindowGetSurfaceMask( const T *model, const char *surfaceName ) {
	if constexpr ( requires( const T &renderModel ) { renderModel.GetSurfaceMask( surfaceName ); } ) {
		return ( model != NULL ) ? model->GetSurfaceMask( surfaceName ) : 0;
	}

	return 0;
}

static void RenderWindowInitShaderParms( renderEntity_t &renderEntity ) {
	renderEntity.shaderParms[0] = 1.0f;
	renderEntity.shaderParms[1] = 1.0f;
	renderEntity.shaderParms[2] = 1.0f;
	renderEntity.shaderParms[3] = 1.0f;
}

static bool RenderWindowResolveViewport( idDeviceContext *dc, const idRectangle &drawRect, int &x, int &y, int &width, int &height, float &fovY ) {
	float viewportX = drawRect.x;
	float viewportY = drawRect.y;
	float viewportW = drawRect.w;
	float viewportH = drawRect.h;

	if ( viewportW <= 0.0f || viewportH <= 0.0f ) {
		return false;
	}

	// Match normal GUI drawing: clip in authored GUI space, then apply the menu's aspect correction.
	if ( dc != NULL && dc->ClippedCoords( &viewportX, &viewportY, &viewportW, &viewportH, NULL, NULL, NULL, NULL ) ) {
		return false;
	}

	if ( viewportW <= 0.0f || viewportH <= 0.0f ) {
		return false;
	}

	const float authoredAspect = viewportH / viewportW;
	fovY = 2.0f * atan( authoredAspect ) * idMath::M_RAD2DEG;

	if ( dc != NULL ) {
		dc->AdjustCoords( &viewportX, &viewportY, &viewportW, &viewportH );

		const float uiViewportWidth = static_cast<float>( glConfig.uiViewportWidth );
		const float uiViewportHeight = static_cast<float>( glConfig.uiViewportHeight );
		const float framebufferWidth = static_cast<float>( glConfig.vidWidth );
		const float framebufferHeight = static_cast<float>( glConfig.vidHeight );
		if ( uiViewportWidth > 0.0f && uiViewportHeight > 0.0f && framebufferWidth > 0.0f && framebufferHeight > 0.0f ) {
			const float uiScaleX = uiViewportWidth / static_cast<float>( VIRTUAL_WIDTH );
			const float uiScaleY = uiViewportHeight / static_cast<float>( VIRTUAL_HEIGHT );

			const float physicalX = static_cast<float>( glConfig.uiViewportX ) + ( viewportX * uiScaleX );
			const float physicalY = static_cast<float>( glConfig.uiViewportY ) + ( viewportY * uiScaleY );
			const float physicalW = viewportW * uiScaleX;
			const float physicalH = viewportH * uiScaleY;

			viewportX = physicalX * ( static_cast<float>( VIRTUAL_WIDTH ) / framebufferWidth );
			viewportY = physicalY * ( static_cast<float>( VIRTUAL_HEIGHT ) / framebufferHeight );
			viewportW = physicalW * ( static_cast<float>( VIRTUAL_WIDTH ) / framebufferWidth );
			viewportH = physicalH * ( static_cast<float>( VIRTUAL_HEIGHT ) / framebufferHeight );
		}
	}

	width = Max( 1, idMath::Ftoi( viewportW + 0.5f ) );
	height = Max( 1, idMath::Ftoi( viewportH + 0.5f ) );
	x = idMath::Ftoi( viewportX );
	y = idMath::Ftoi( viewportY );
	return true;
}

}

idRenderWindow::idRenderWindow( idDeviceContext *d, idUserInterfaceLocal *g ) : idWindow( d, g ) {
	dc = d;
	gui = g;
	CommonInit();
}

idRenderWindow::idRenderWindow( idUserInterfaceLocal *g ) : idWindow( g ) {
	gui = g;
	CommonInit();
}

idRenderWindow::~idRenderWindow() {
	FreeModelJoints();
	renderSystem->FreeRenderWorld( world );
}

void idRenderWindow::FreeModelJoints() {
	for ( int i = 0; i < MAX_RENDERWINDOW_MODELS; ++i ) {
		if ( worldEntity[i].joints ) {
			Mem_Free16( worldEntity[i].joints );
			worldEntity[i].joints = NULL;
		}
	}
}

void idRenderWindow::CommonInit() {
	world = renderSystem->AllocRenderWorld();
	needsRender = true;

	for ( int i = 0; i < MAX_RENDERWINDOW_LIGHTS; ++i ) {
		lightOrigin[i] = idVec4( -128.0f, 0.0f, 0.0f, 1.0f );
		lightColor[i] = idVec4( 1.0f, 1.0f, 1.0f, 1.0f );
		lightDefs[i] = -1;
		useLight[i] = false;
		memset( &rLights[i], 0, sizeof( rLights[i] ) );
	}
	useLight[0] = true;

	modelOrigin.Zero();
	viewOffset = idVec4( -128.0f, 0.0f, 0.0f, 1.0f );
	customSkin = "";
	customShader = "NONE";
	needUpdate.Clear();

	for ( int i = 0; i < MAX_RENDERWINDOW_MODELS; ++i ) {
		modelAnim[i] = NULL;
		animLength[i] = 0;
		animEndTime[i] = -1;
		modelDef[i] = -1;
		memset( &worldEntity[i], 0, sizeof( worldEntity[i] ) );
	}

	updateAnimation = true;
}

void idRenderWindow::BuildAnimation( int time ) {
	if ( !updateAnimation ) {
		return;
	}

	for ( int i = 0; i < MAX_RENDERWINDOW_MODELS; ++i ) {
		renderEntity_t &renderEntity = worldEntity[i];
		renderEntity.suppressSurfaceMask = 0;

		if ( !renderEntity.hModel || !animName[i].Length() || !animClass[i].Length() ) {
			continue;
		}

		renderEntity.numJoints = renderEntity.hModel->NumJoints();
		if ( renderEntity.numJoints <= 0 ) {
			continue;
		}

		renderEntity.joints = static_cast<idJointMat *>( Mem_Alloc16( renderEntity.numJoints * sizeof( *renderEntity.joints ) ) );

		const idDict *animDef = gameEdit->FindEntityDefDict( animClass[i].c_str(), false );
		if ( animDef ) {
			for ( const idKeyValue *kv = animDef->MatchPrefix( "hidesurface", NULL ); kv; kv = animDef->MatchPrefix( "hidesurface", kv ) ) {
				if ( kv->GetValue().Length() ) {
					renderEntity.suppressSurfaceMask |= RenderWindowGetSurfaceMask( renderEntity.hModel, kv->GetValue().c_str() );
				}
			}
		}

		modelAnim[i] = gameEdit->ANIM_GetAnimFromEntityDef( animClass[i].c_str(), animName[i].c_str() );
		if ( modelAnim[i] ) {
			animLength[i] = gameEdit->ANIM_GetLength( modelAnim[i] );
			animEndTime[i] = time + animLength[i];
		}
	}

	updateAnimation = false;
}

void idRenderWindow::PreRender() {
	if ( !needsRender ) {
		return;
	}

	world->InitFromMap( NULL );

	idDict spawnArgs;
	for ( int i = 0; i < MAX_RENDERWINDOW_LIGHTS; ++i ) {
		lightDefs[i] = -1;
		if ( !useLight[i] ) {
			continue;
		}

		spawnArgs.Clear();
		spawnArgs.Set( "classname", "light" );
		spawnArgs.Set( "name", va( "light_%d", i ) );
		spawnArgs.Set( "origin", lightOrigin[i].ToVec3().ToString() );
		spawnArgs.Set( "_color", lightColor[i].ToVec3().ToString() );
		gameEdit->ParseSpawnArgsToRenderLight( &spawnArgs, &rLights[i] );
		lightDefs[i] = world->AddLightDef( &rLights[i] );
	}

	if ( !modelName[0].Length() ) {
		common->Warning( "Window '%s' in gui '%s': no model set", GetName(), GetGui()->GetSourceFile() );
	}

	FreeModelJoints();
	memset( worldEntity, 0, sizeof( worldEntity ) );

	for ( int i = 0; i < MAX_RENDERWINDOW_MODELS; ++i ) {
		modelAnim[i] = NULL;
		animLength[i] = 0;
		animEndTime[i] = -1;
		modelDef[i] = -1;
	}

	const idMat3 baseAxis = RenderWindowRotationToAxis( modelRotate );

	spawnArgs.Clear();
	spawnArgs.Set( "classname", "func_static" );
	spawnArgs.Set( "model", modelName[0].c_str() );
	spawnArgs.Set( "skin", customSkin.c_str() );
	spawnArgs.Set( "origin", modelOrigin.c_str() );
	gameEdit->ParseSpawnArgsToRenderEntity( &spawnArgs, &worldEntity[0] );
	if ( worldEntity[0].hModel ) {
		worldEntity[0].axis = baseAxis;
		RenderWindowInitShaderParms( worldEntity[0] );
		if ( customShader.Length() && idStr::Icmp( customShader.c_str(), "NONE" ) != 0 ) {
			worldEntity[0].customShader = declManager->FindMaterial( customShader.c_str() );
		}
		modelDef[0] = world->AddEntityDef( &worldEntity[0] );
	}

	for ( int i = 1; i < MAX_RENDERWINDOW_MODELS; ++i ) {
		if ( !modelName[i].Length() ) {
			continue;
		}

		spawnArgs.Clear();
		spawnArgs.Set( "classname", "func_static" );
		spawnArgs.Set( "model", modelName[i].c_str() );
		spawnArgs.Set( "origin", "0 0 0" );
		gameEdit->ParseSpawnArgsToRenderEntity( &spawnArgs, &worldEntity[i] );
		if ( !worldEntity[i].hModel ) {
			continue;
		}

		worldEntity[i].axis = baseAxis;
		RenderWindowInitShaderParms( worldEntity[i] );
		if ( customShader.Length() && idStr::Icmp( customShader.c_str(), "NONE" ) != 0 ) {
			worldEntity[i].customShader = declManager->FindMaterial( customShader.c_str() );
		}
		modelDef[i] = world->AddEntityDef( &worldEntity[i] );
	}

	world->PushMarkedDefs();
	needsRender = false;
}

void idRenderWindow::Render( int time ) {
	for ( int i = 0; i < MAX_RENDERWINDOW_LIGHTS; ++i ) {
		if ( !useLight[i] || lightDefs[i] == -1 ) {
			continue;
		}

		rLights[i].origin = lightOrigin[i].ToVec3();
		rLights[i].shaderParms[SHADERPARM_RED] = lightColor[i].x();
		rLights[i].shaderParms[SHADERPARM_GREEN] = lightColor[i].y();
		rLights[i].shaderParms[SHADERPARM_BLUE] = lightColor[i].z();
		world->UpdateLightDef( lightDefs[i], &rLights[i] );
	}

	if ( !worldEntity[0].hModel ) {
		return;
	}

	if ( updateAnimation ) {
		BuildAnimation( time );
	}

	if ( modelAnim[0] && worldEntity[0].joints ) {
		if ( time > animEndTime[0] ) {
			animEndTime[0] = time + animLength[0];
		}
		gameEdit->ANIM_CreateAnimFrame(
			worldEntity[0].hModel,
			modelAnim[0],
			worldEntity[0].numJoints,
			worldEntity[0].joints,
			time + animLength[0] - animEndTime[0],
			vec3_origin,
			false );
	}

	worldEntity[0].axis = RenderWindowRotationToAxis( modelRotate );
	if ( modelDef[0] != -1 ) {
		world->UpdateEntityDef( modelDef[0], &worldEntity[0] );
	}

	for ( int i = 1; i < MAX_RENDERWINDOW_MODELS; ++i ) {
		if ( !worldEntity[i].hModel ) {
			continue;
		}

		if ( modelAnim[i] && worldEntity[i].joints ) {
			if ( time > animEndTime[i] ) {
				animEndTime[i] = time + animLength[i];
			}
			gameEdit->ANIM_CreateAnimFrame(
				worldEntity[i].hModel,
				modelAnim[i],
				worldEntity[i].numJoints,
				worldEntity[i].joints,
				time + animLength[i] - animEndTime[i],
				vec3_origin,
				false );
		}

		if ( worldEntity[0].joints && jointName[i].Length() ) {
			const jointHandle_t joint = worldEntity[0].hModel->GetJointHandle( jointName[i].c_str() );
			if ( joint != INVALID_JOINT ) {
				const idJointMat &jointTransform = worldEntity[0].joints[joint];
				worldEntity[i].origin = worldEntity[0].origin + jointTransform.ToVec3() * worldEntity[0].axis;
				worldEntity[i].axis = jointTransform.ToMat3();
				worldEntity[i].axis *= worldEntity[0].axis;
			}
		}

		if ( modelDef[i] != -1 ) {
			world->UpdateEntityDef( modelDef[i], &worldEntity[i] );
		}
	}

	world->PushMarkedDefs();
}

void idRenderWindow::Draw( int time, float x, float y ) {
	if ( needUpdate.Length() && gui->GetStateBool( needUpdate.c_str(), "0" ) ) {
		needsRender = true;
		updateAnimation = true;
		gui->SetStateBool( needUpdate.c_str(), false );
	}

	PreRender();
	Render( time );

	memset( &refdef, 0, sizeof( refdef ) );
	refdef.vieworg = viewOffset.ToVec3();
	refdef.viewaxis.Identity();
	refdef.shaderParms[0] = 1.0f;
	refdef.shaderParms[1] = 1.0f;
	refdef.shaderParms[2] = 1.0f;
	refdef.shaderParms[3] = 1.0f;
	if ( !RenderWindowResolveViewport( dc, drawRect, refdef.x, refdef.y, refdef.width, refdef.height, refdef.fov_y ) ) {
		return;
	}
	refdef.fov_x = 90.0f;
	refdef.time = time;
	world->RenderScene( &refdef );
}

void idRenderWindow::PostParse() {
	idWindow::PostParse();
}

idWinVar *idRenderWindow::GetWinVarByName( const char *_name, bool fixup, drawWin_t **owner ) {
	if ( idStr::Icmp( _name, "model" ) == 0 ) {
		return &modelName[0];
	}
	if ( idStr::Icmp( _name, "anim" ) == 0 ) {
		return &animName[0];
	}
	if ( idStr::Icmp( _name, "animClass" ) == 0 ) {
		return &animClass[0];
	}

	for ( int i = 1; i < MAX_RENDERWINDOW_MODELS; ++i ) {
		if ( idStr::Icmp( _name, va( "model%d", i ) ) == 0 ) {
			return &modelName[i];
		}
		if ( idStr::Icmp( _name, va( "joint%d", i ) ) == 0 ) {
			return &jointName[i];
		}
		if ( idStr::Icmp( _name, va( "anim%d", i ) ) == 0 ) {
			return &animName[i];
		}
		if ( idStr::Icmp( _name, va( "animClass%d", i ) ) == 0 ) {
			return &animClass[i];
		}
	}

	if ( idStr::Icmp( _name, "lightOrigin" ) == 0 ) {
		useLight[0] = true;
		return &lightOrigin[0];
	}
	if ( idStr::Icmp( _name, "lightColor" ) == 0 ) {
		useLight[0] = true;
		return &lightColor[0];
	}

	for ( int i = 0; i < MAX_RENDERWINDOW_LIGHTS; ++i ) {
		if ( idStr::Icmp( _name, va( "lightOrigin%d", i ) ) == 0 ) {
			useLight[i] = true;
			return &lightOrigin[i];
		}
		if ( idStr::Icmp( _name, va( "lightColor%d", i ) ) == 0 ) {
			useLight[i] = true;
			return &lightColor[i];
		}
	}

	if ( idStr::Icmp( _name, "modelOrigin" ) == 0 ) {
		return &modelOrigin;
	}
	if ( idStr::Icmp( _name, "modelRotate" ) == 0 ) {
		return &modelRotate;
	}
	if ( idStr::Icmp( _name, "viewOffset" ) == 0 ) {
		return &viewOffset;
	}
	if ( idStr::Icmp( _name, "customShader" ) == 0 ) {
		return &customShader;
	}
	if ( idStr::Icmp( _name, "skin" ) == 0 ) {
		return &customSkin;
	}
	if ( idStr::Icmp( _name, "needsRender" ) == 0 ) {
		return &needsRender;
	}

	return idWindow::GetWinVarByName( _name, fixup, owner );
}

bool idRenderWindow::ParseInternalVar( const char *_name, idParser *src ) {
	if ( idStr::Icmp( _name, "needUpdate" ) == 0 ) {
		ParseString( src, needUpdate );
		return true;
	}
	return idWindow::ParseInternalVar( _name, src );
}
