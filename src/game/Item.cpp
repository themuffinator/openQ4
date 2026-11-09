// RAVEN BEGIN
// bdube: note that this file is no longer merged with legacy engine updates
//
// MERGE_DATE 09/30/2004




#include "Game_local.h"


/*
===============================================================================

  idItem

===============================================================================
*/

const idEventDef EV_DropToFloor( "<dropToFloor>" );
const idEventDef EV_RespawnItem( "respawn" );
const idEventDef EV_RespawnFx( "<respawnFx>" );
const idEventDef EV_GetPlayerPos( "<getplayerpos>" );
const idEventDef EV_HideObjective( "<hideobjective>", "e" );
const idEventDef EV_CamShot( "<camshot>" );

typedef struct itemFlatColorFallback_s {
	const char *	nameFragment;
	float			red;
	float			green;
	float			blue;
} itemFlatColorFallback_t;

// Most stock multiplayer icons are guide-expanded materials with explicit
// red/green/blue constants.  These entries cover the stock icons that are
// direct images (notably powerups), plus semantic fallbacks for modified decls.
static const itemFlatColorFallback_t itemFlatColorFallbacks[] = {
	{ "ammoregen",			1.00f, 1.00f, 0.00f },
	{ "healthshard",		0.50f, 1.00f, 0.50f },
	{ "healthsmall",		1.00f, 1.00f, 0.20f },
	{ "healthlarge",		1.00f, 0.50f, 0.00f },
	{ "healthmega",			0.00f, 0.50f, 1.00f },
	{ "armorshard",			0.00f, 0.50f, 1.00f },
	{ "armorsmall",			1.00f, 1.00f, 0.00f },
	{ "armorlarge",			1.00f, 0.00f, 0.00f },
	{ "machinegun",			1.00f, 1.00f, 0.00f },
	{ "nailgun",			0.60f, 0.80f, 0.80f },
	{ "railgun",			0.00f, 1.00f, 0.00f },
	{ "shotgun",			1.00f, 0.50f, 0.00f },
	{ "hyperblaster",		0.00f, 0.45f, 1.00f },
	{ "blaster",			0.85f, 0.85f, 0.85f },
	{ "rocket",				1.00f, 0.20f, 0.00f },
	{ "grenade",			0.20f, 0.56f, 0.07f },
	{ "lightning",			1.00f, 1.00f, 0.73f },
	{ "darkmatter",			0.77f, 0.00f, 1.00f },
	{ "gauntlet",			0.00f, 0.85f, 1.00f },
	{ "napalm",				1.00f, 0.75f, 0.25f },
	{ "item_fire",			1.00f, 0.75f, 0.25f },
	{ "quaddamage",			0.00f, 0.94f, 1.00f },
	{ "quad_damage",		0.00f, 0.94f, 1.00f },
	{ "haste",				0.80f, 1.00f, 0.00f },
	{ "regen",				1.00f, 0.00f, 0.00f },
	{ "invis",				0.00f, 0.00f, 1.00f },
	{ "guard",				0.00f, 0.45f, 1.00f },
	{ "doubler",			1.00f, 0.00f, 0.00f },
	{ "scout",				0.00f, 1.00f, 0.00f },
	{ "deadzone",			0.93f, 0.84f, 0.52f },
	{ "moderator",			0.85f, 0.85f, 0.85f },
	{ "sb_flag_marine",		0.42f, 0.64f, 0.17f },
	{ "flag_marine",		0.42f, 0.64f, 0.17f },
	{ "marine_flag",		0.42f, 0.64f, 0.17f },
	{ "sb_flag_strogg",		1.00f, 0.48f, 0.02f },
	{ "flag_strogg",		1.00f, 0.48f, 0.02f },
	{ "strogg_flag",		1.00f, 0.48f, 0.02f },
	{ "ctf_one_flag",		0.85f, 0.85f, 0.85f }
};

static bool Item_FlatColorIsFinite( const idVec3 &color ) {
	return !FLOAT_IS_NAN( color.x ) && !FLOAT_IS_INF( color.x ) &&
		!FLOAT_IS_NAN( color.y ) && !FLOAT_IS_INF( color.y ) &&
		!FLOAT_IS_NAN( color.z ) && !FLOAT_IS_INF( color.z );
}

static void Item_SetClampedFlatColor( const idVec3 &source, idVec4 &color ) {
	color.Set(
		idMath::ClampFloat( 0.0f, 1.0f, source.x ),
		idMath::ClampFloat( 0.0f, 1.0f, source.y ),
		idMath::ClampFloat( 0.0f, 1.0f, source.z ),
		1.0f );
}

static bool Item_TryFlatColorFromName( const char *name, idVec4 &color ) {
	if ( name == NULL || name[0] == '\0' ) {
		return false;
	}

	idStr normalizedName = name;
	normalizedName.ToLower();
	const int numFallbacks = (int)( sizeof( itemFlatColorFallbacks ) / sizeof( itemFlatColorFallbacks[0] ) );
	for ( int i = 0; i < numFallbacks; i++ ) {
		const itemFlatColorFallback_t &fallback = itemFlatColorFallbacks[i];
		if ( normalizedName.Find( fallback.nameFragment ) >= 0 ) {
			color.Set( fallback.red, fallback.green, fallback.blue, 1.0f );
			return true;
		}
	}
	return false;
}

static bool Item_TryFlatColorFromMaterial( const char *materialName, idVec4 &color ) {
	if ( materialName == NULL || materialName[0] == '\0' ) {
		return false;
	}

	const idMaterial *material = declManager->FindMaterial( materialName, false );
	if ( material == NULL ) {
		return false;
	}

	const int textLength = material->GetTextLength();
	if ( textLength <= 0 ) {
		return false;
	}

	idStr materialText;
	materialText.Fill( ' ', textLength );
	material->GetText( &materialText[0] );

	idLexer lexer( LEXFL_NOERRORS | LEXFL_NOSTRINGCONCAT );
	if ( !lexer.LoadMemory( materialText.c_str(), materialText.Length(), material->GetName() ) ) {
		return false;
	}

	idVec3 stageColor;
	stageColor.Zero();
	int componentMask = 0;
	int braceDepth = 0;
	int stageDepth = -1;
	idToken token;

	while ( lexer.ReadToken( &token ) ) {
		if ( token.Icmp( "{" ) == 0 ) {
			braceDepth++;
			if ( braceDepth == 2 ) {
				stageDepth = braceDepth;
				componentMask = 0;
			}
			continue;
		}
		if ( token.Icmp( "}" ) == 0 ) {
			if ( braceDepth == stageDepth ) {
				stageDepth = -1;
				componentMask = 0;
			}
			braceDepth--;
			continue;
		}
		if ( stageDepth < 0 ) {
			continue;
		}

		int component = -1;
		if ( token.Icmp( "red" ) == 0 ) {
			component = 0;
		} else if ( token.Icmp( "green" ) == 0 ) {
			component = 1;
		} else if ( token.Icmp( "blue" ) == 0 ) {
			component = 2;
		}
		if ( component < 0 ) {
			continue;
		}

		idToken value;
		if ( !lexer.ReadToken( &value ) || value.type != TT_NUMBER ) {
			componentMask &= ~( 1 << component );
			continue;
		}

		stageColor[component] = value.GetFloatValue();
		componentMask |= 1 << component;
		if ( componentMask == 0x7 && Item_FlatColorIsFinite( stageColor ) ) {
			Item_SetClampedFlatColor( stageColor, color );
			return true;
		}
	}

	return false;
}

// RAVEN BEGIN
// abahr:
const idEventDef EV_SetGravity( "<setGravity>" );
// RAVEN END

CLASS_DECLARATION( idEntity, idItem )
	EVENT( EV_DropToFloor,		idItem::Event_DropToFloor )
	EVENT( EV_Touch,			idItem::Event_Touch )
	EVENT( EV_Activate,			idItem::Event_Trigger )
	EVENT( EV_RespawnItem,		idItem::Event_Respawn )
	EVENT( EV_RespawnFx,		idItem::Event_RespawnFx )
// RAVEN BEGIN
// abahr
	EVENT( EV_SetGravity,		idItem::Event_SetGravity )
// RAVEN END

END_CLASS

/*
================
idItem::GetSimpleItemStyle
================
*/
int idItem::GetSimpleItemStyle( void ) {
	return idMath::ClampInt( 0, 3, g_simpleItems.GetInteger() );
}

/*
================
idItem::ResolveFlatDiffuseColor
================
*/
void idItem::ResolveFlatDiffuseColor( const idDict &args, idVec4 &color ) {
	idVec3 overrideColor;
	if ( args.GetVector( "flatDiffuseColor", NULL, overrideColor ) && Item_FlatColorIsFinite( overrideColor ) ) {
		Item_SetClampedFlatColor( overrideColor, color );
		return;
	}

	const char *simpleIcon = args.GetString( "mtr_simple_icon" );
	const char *inventoryIcon = args.GetString( "inv_icon" );
	const char *weaponIcon = args.GetString( "mtr_icon" );
	const char *className = args.GetString( "classname" );

	// Guide-expanded simple/inventory icons carry their authored RGB as literal
	// stage constants, which is the closest representation of the icon color.
	if ( Item_TryFlatColorFromMaterial( simpleIcon, color ) ||
		Item_TryFlatColorFromMaterial( inventoryIcon, color ) ) {
		return;
	}

	if ( Item_TryFlatColorFromName( simpleIcon, color ) ||
		Item_TryFlatColorFromName( inventoryIcon, color ) ||
		Item_TryFlatColorFromName( weaponIcon, color ) ||
		Item_TryFlatColorFromName( className, color ) ) {
		return;
	}

	if ( Item_TryFlatColorFromMaterial( weaponIcon, color ) ) {
		return;
	}

	// Mods with image-only icons can opt into an exact color through
	// flatDiffuseColor.  Until then, a restrained neutral is more readable than
	// guessing a hue from an unavailable texture.
	color.Set( 0.85f, 0.85f, 0.85f, 1.0f );
}

/*
================
idItem::UpdateFlatDiffusePresentation
================
*/
void idItem::UpdateFlatDiffusePresentation( void ) {
	renderEntity.flatDiffuseColor.Zero();
	renderEntity.flatDiffuseFlags = 0;

	const int style = GetSimpleItemStyle();
	if ( !gameLocal.isMultiplayer || simpleItem || style < 2 ) {
		return;
	}

	ResolveFlatDiffuseColor( spawnArgs, renderEntity.flatDiffuseColor );
	renderEntity.flatDiffuseFlags = REF_FLAT_DIFFUSE;
	if ( style >= 3 ) {
		renderEntity.flatDiffuseFlags |= REF_FLAT_DIFFUSE_SWEEP;
	}
}


/*
================
idItem::idItem
================
*/
idItem::idItem() {
	spin = false;
	inView = false;
	skin = NULL;
	pickupSkin = NULL;
	inViewTime = 0;
	lastCycle = 0;
	lastRenderViewTime = -1;
	itemShellHandle = -1;
	shellMaterial = NULL;
	orgOrigin.Zero();
	canPickUp = true;
	fl.networkSync = true;
	trigger = NULL;
	syncPhysics = false;
	srvReady = -1;
	clReady = -1;
	effectIdle = NULL;
	itemPVSArea = 0;
	effectIdle = NULL;
	simpleItem = false;
	pickedUp = false;
}

/*
================
idItem::~idItem
================
*/
idItem::~idItem() {
	// remove the highlight shell
	if ( itemShellHandle != -1 ) {
		gameRenderWorld->FreeEntityDef( itemShellHandle );
	}
	if ( trigger ) {
		delete trigger;
	}
	
	SetPhysics( NULL );
}

/*
================
idItem::Save
================
*/
void idItem::Save( idSaveGame *savefile ) const {
   	savefile->WriteClipModel( trigger );
	savefile->WriteBool( spin );

	savefile->WriteSkin( skin );
	savefile->WriteSkin( pickupSkin );

	savefile->WriteVec3( orgOrigin );

	savefile->WriteBool( pulse );
	savefile->WriteBool( canPickUp );

	savefile->WriteStaticObject( physicsObj );

//	savefile->WriteInt(itemShellHandle);	// cnicholson: Set at end of Restore, do not save
	savefile->WriteMaterial( shellMaterial );

	savefile->WriteBool( inView );
	savefile->WriteInt( inViewTime );
	savefile->WriteInt( lastCycle );
	savefile->WriteInt( lastRenderViewTime );
}

/*
================
idItem::Restore
================
*/
void idItem::Restore( idRestoreGame *savefile ) {
	savefile->ReadClipModel( trigger );
	savefile->ReadBool( spin );

	savefile->ReadSkin( skin );
	savefile->ReadSkin( pickupSkin );

	savefile->ReadVec3( orgOrigin );

	savefile->ReadBool( pulse );
	savefile->ReadBool( canPickUp );

	savefile->ReadPhysicsObject( physicsObj, this );

//	savefile->ReadInt(itemShellHandle);	// cnicholson: Set at end of function, do not restore
	savefile->ReadMaterial( shellMaterial );

	savefile->ReadBool( inView );
	savefile->ReadInt( inViewTime );
	savefile->ReadInt( lastCycle );
	savefile->ReadInt( lastRenderViewTime );

	RestorePhysics( &physicsObj );
	
	physicsObj.SetSelf( this );
	
	itemShellHandle = -1;
}

/*
================
idItem::UpdateRenderEntity
================
*/
bool idItem::UpdateRenderEntity( renderEntity_s *renderEntity, const renderView_t *renderView ) const {
	if( simpleItem ) {
		return false;
	}

	if ( lastRenderViewTime == renderView->time ) {
		return false;
	}

	lastRenderViewTime = renderView->time;

	// check for glow highlighting if near the center of the view
	idVec3 dir = renderEntity->origin - renderView->vieworg;
	dir.Normalize();
	float d = dir * renderView->viewaxis[0];

	// two second pulse cycle
	float cycle = ( renderView->time - inViewTime ) / 2000.0f;

	if ( d > 0.94f ) {
		if ( !inView ) {
			inView = true;
			if ( cycle > lastCycle ) {
				// restart at the beginning
				inViewTime = renderView->time;
				cycle = 0.0f;
			}
		}
	} else {
		if ( inView ) {
			inView = false;
			lastCycle = ceil( cycle );
		}
	}

	// fade down after the last pulse finishes 
	if ( !inView && cycle > lastCycle ) {
		renderEntity->shaderParms[4] = 0.0f;
	} else {
		// pulse up in 1/4 second
		cycle -= (int)cycle;
		if ( cycle < 0.1f ) {
			renderEntity->shaderParms[4] = cycle * 10.0f;
		} else if ( cycle < 0.2f ) {
			renderEntity->shaderParms[4] = 1.0f;
		} else if ( cycle < 0.3f ) {
			renderEntity->shaderParms[4] = 1.0f - ( cycle - 0.2f ) * 10.0f;
		} else {
			// stay off between pulses
			renderEntity->shaderParms[4] = 0.0f;
		}
	}

	// update every single time this is in view
	return true;
}

/*
================
idItem::UpdateTrigger
================
*/
void idItem::UpdateTrigger ( void ) {
	// update trigger position
// RAVEN BEGIN
// ddynerman: multiple clip worlds
	trigger->Link( this, 0, GetPhysics()->GetOrigin(), mat3_identity );
// RAVEN END
}

/*
================
idItem::ModelCallback
================
*/
bool idItem::ModelCallback( renderEntity_t *renderEntity, const renderView_t *renderView ) {
	const idItem *ent;

	// this may be triggered by a model trace or other non-view related source
	if ( !renderView ) {
		return false;
	}

	ent = static_cast<idItem *>(gameLocal.entities[ renderEntity->entityNum ]);
	if ( !ent ) {
		gameLocal.Error( "idItem::ModelCallback: callback with NULL game entity" );
	}

	return ent->UpdateRenderEntity( renderEntity, renderView );
}


/*
================
idItem::GetPhysicsToVisualTransform
================
*/
bool idItem::GetPhysicsToVisualTransform( idVec3 &origin, idMat3 &axis ) {
	if( simpleItem ) {
		if ( gameLocal.GetLocalPlayer() && gameLocal.GetLocalPlayer()->GetRenderView() ) {
			if( gameLocal.GetLocalPlayer()->spectating ) {
				idPlayer* spec = (idPlayer*)gameLocal.entities[ gameLocal.GetLocalPlayer()->spectator ];
				if( spec && spec->GetRenderView() ) {
					axis = spec->GetRenderView()->viewaxis;
				} else {
					// openQ4: the followed player can be gone before the next
					// snapshot retargets us; axis was left unset here and the
					// caller used it anyway, for every simple item that frame
					axis = gameLocal.GetLocalPlayer()->GetRenderView()->viewaxis;
				}
			} else {
				axis = gameLocal.GetLocalPlayer()->GetRenderView()->viewaxis;	
			}
		} else {
			// dedicated server for instance
			axis = mat3_identity;
		}
		origin = idVec3( 0.0f, 0.0f, 32.0f );
		return true;
	}

	if( !spin || (gameLocal.isServer && !gameLocal.isListenServer) ) {
		return false;
	}

	idAngles ang;
	ang.pitch = ang.roll = 0.0f;
	ang.yaw = ( gameLocal.time & 4095 ) * 360.0f / -4096.0f;
	axis = ang.ToMat3() * GetPhysics()->GetAxis();

	float scale = 0.005f;
	float offset = entityNumber * 0.685145f;		// rjohnson: just a random number here to shift the cos curve

	origin.Zero();

	origin += GetPhysics()->GetAxis()[2] * (4.0f + idMath::Cos( ( ( gameLocal.time + 1000 ) * scale ) + offset ) * 4.0f);

	return true;
}

// RAVEN BEGIN
// mekberg: added
/*
================
idItem::Collide
================
*/
bool idItem::Collide( const trace_t &collision, const idVec3 &velocity ) {
	idEntity* lol = gameLocal.entities[ collision.c.entityNum ];
	if ( gameLocal.isMultiplayer && collision.c.contents & CONTENTS_ITEMCLIP && lol && !lol->IsType( idItem::GetClassType() ) ) {
		PostEventMS( &EV_Remove, 0 );
	}
	return false;
}
// RAVEN END

/*
================
idItem::Think
================
*/
void idItem::Think( void ) {
	if ( thinkFlags & TH_PHYSICS ) {
		RunPhysics();		
		UpdateTrigger();
	}

	if ( gameLocal.IsMultiplayer() && g_skipItemShadowsMP.GetBool() ) {
		renderEntity.suppressShadowInViewID = gameLocal.localClientNum + 1;
	} else {
		renderEntity.suppressShadowInViewID = 0;
	}

	if( !(simpleItem && pickedUp) ) {
		UpdateVisuals();
		Present();
	}
}

/*
================
idItem::Present
================
*/
void idItem::Present( void ) {
	idEntity::Present();

	if ( !fl.hidden && pulse ) {
		// also add a highlight shell model
		renderEntity_t	shell;

		shell = renderEntity;

		// we will mess with shader parms when the item is in view
		// to give the "item pulse" effect
		shell.callback = idItem::ModelCallback;
		shell.entityNum = entityNumber;
		shell.customShader = shellMaterial;
		if ( itemShellHandle == -1 ) {
			itemShellHandle = gameRenderWorld->AddEntityDef( &shell );
		} else {
			gameRenderWorld->UpdateEntityDef( itemShellHandle, &shell );
		}
	}
}

/*
================
idItem::InstanceJoin
================
*/
void idItem::InstanceJoin( void ) {
	idEntity::InstanceJoin();

	UpdateModelTransform();
	if ( !simpleItem && spawnArgs.GetString( "fx_idle" ) ) {
		PlayEffect( "fx_idle", renderEntity.origin, renderEntity.axis, true );
	}
}

/*
================
idItem::InstanceLeave
================
*/
void idItem::InstanceLeave( void ) {
	idEntity::InstanceLeave();

	StopEffect( "fx_idle", true );
}

/*
================
idItem::Spawn
================
*/
void idItem::Spawn( void ) {
	idStr		giveTo;
	idEntity *	ent;
	idVec3		vSize;
	idBounds	bounds(vec3_origin);

	// check for triggerbounds, which allows for non-square triggers (useful for, say, a CTF flag)	
	if ( spawnArgs.GetVector( "triggerbounds", "16 16 16", vSize )) {
		bounds.AddPoint(idVec3( vSize.x*0.5f,  vSize.y*0.5f, 0.0f));
		bounds.AddPoint(idVec3(-vSize.x*0.5f, -vSize.y*0.5f, vSize.z));
	}
	else {
		// create a square trigger for item pickup
		float tsize;
		spawnArgs.GetFloat( "triggersize", "16.0", tsize );
		bounds.ExpandSelf( tsize );
	}

// RAVEN BEGIN
// mwhitlock: Dynamic memory consolidation
	RV_PUSH_HEAP_MEM(this);
// RAVEN END
	trigger = new idClipModel( idTraceModel( bounds ));
// RAVEN BEGIN
// mwhitlock: Dynamic memory consolidation
	RV_POP_HEAP();
// RAVEN END
// RAVEN BEGIN
// ddynerman: multiple clip worlds
	trigger->Link( this, 0, GetPhysics()->GetOrigin(), GetPhysics()->GetAxis() );
// RAVEN END

	physicsObj.SetSelf ( this );
// RAVEN BEGIN
// mwhitlock: Dynamic memory consolidation
	RV_PUSH_HEAP_MEM(this);
// RAVEN END

// jmarshall
	modelindex = gameLocal.GetBotItemEntry(spawnArgs.GetString("modelindex"));
// jmarshall end

	physicsObj.SetClipModel( new idClipModel( GetPhysics()->GetClipModel() ), 1.0f );

// RAVEN BEGIN
// mwhitlock: Dynamic memory consolidation
	RV_POP_HEAP();
// RAVEN END
	physicsObj.SetOrigin( GetPhysics()->GetOrigin() );
	physicsObj.SetAxis( GetPhysics()->GetAxis() );
	physicsObj.SetGravity( gameLocal.GetGravity() );
	physicsObj.SetContents( 0 );
	physicsObj.SetClipMask( MASK_SOLID );
	physicsObj.SetFriction( 0.0f, 0.0f, 6.0f );
	SetPhysics( &physicsObj );

	if ( spawnArgs.GetBool( "start_off" ) ) {
		trigger->SetContents( 0 );
		Hide();
	} else {
		trigger->SetContents( CONTENTS_TRIGGER );
	}

	giveTo = spawnArgs.GetString( "owner" );
	if ( giveTo.Length() ) {
		ent = gameLocal.FindEntity( giveTo );
		if ( !ent ) {
			gameLocal.Error( "Item couldn't find owner '%s'", giveTo.c_str() );
		}
		PostEventMS( &EV_Touch, 0, ent, NULL );
	}

	if ( spawnArgs.GetBool( "spin" ) || gameLocal.isMultiplayer ) {
		spin = true;
		BecomeActive( TH_THINK );
	}

	// pulse ( and therefore itemShellHandle ) was taken out and shot. do not sync
	//pulse = !spawnArgs.GetBool( "nopulse" );
	pulse = false;
	orgOrigin = GetPhysics()->GetOrigin();

	canPickUp = !( spawnArgs.GetBool( "triggerFirst" ) || spawnArgs.GetBool( "no_touch" ) );

	inViewTime = -1000;
	lastCycle = -1;
	itemShellHandle = -1;
// RAVEN BEGIN
// abahr: move texture to def file for precaching
	shellMaterial = declManager->FindMaterial( spawnArgs.GetString("mtr_highlight", "_default") );
	PostEventMS( &EV_SetGravity, 0 );
// RAVEN END
	if ( spawnArgs.GetString( "skin", NULL ) ) {
		skin = declManager->FindSkin( spawnArgs.GetString( "skin" ), false );
		if( skin ) {
			SetSkin( skin );
			srvReady = 1;
		}
	} else {
		skin = NULL;
	}

	if ( spawnArgs.GetString( "skin_pickup", NULL ) ) {
		pickupSkin = declManager->FindSkin( spawnArgs.GetString( "skin_pickup" ), false );
	} else {
		pickupSkin = NULL;
	}

	syncPhysics = spawnArgs.GetBool( "net_syncPhysics", "0" );

	if ( srvReady == -1 ) {
		srvReady = IsHidden() ? 0 : 1;
	}

// RAVEN BEGIN
// mekberg: added for removing pickups in mp in pits
	if ( gameLocal.isMultiplayer ) {
		trigger->SetContents( trigger->GetContents() | CONTENTS_ITEMCLIP );
	}
// RAVEN END

	if( gameLocal.isMultiplayer ) {
		itemPVSArea = gameLocal.pvs.GetPVSArea( GetPhysics()->GetOrigin() );
	} else {
		itemPVSArea = 0;
	}

	simpleItemScale = spawnArgs.GetFloat( "simple_icon_scale", "20.0" );

	simpleItem = GetSimpleItemStyle() == 1 && gameLocal.isMultiplayer && !IsType( rvItemCTFFlag::GetClassType() );
	if( simpleItem ) {
		memset( &renderEntity, 0, sizeof( renderEntity ) );
		renderEntity.axis		= mat3_identity;
		renderEntity.shaderParms[ SHADERPARM_RED ]				= 1.0f;
		renderEntity.shaderParms[ SHADERPARM_GREEN ]			= 1.0f;
		renderEntity.shaderParms[ SHADERPARM_BLUE ]				= 1.0f;
		renderEntity.shaderParms[ SHADERPARM_ALPHA ]			= 1.0f;
		renderEntity.shaderParms[ SHADERPARM_SPRITE_WIDTH ]		= simpleItemScale;
		renderEntity.shaderParms[ SHADERPARM_SPRITE_HEIGHT ]	= simpleItemScale;
		renderEntity.hModel = renderModelManager->FindModel( "_sprite" );
		renderEntity.callback = NULL;
		renderEntity.numJoints = 0;
		renderEntity.joints = NULL;
		renderEntity.customSkin = 0;
		renderEntity.noShadow = true;
		renderEntity.noSelfShadow = true;
		renderEntity.customShader = declManager->FindMaterial( spawnArgs.GetString( "mtr_simple_icon" ) );

		renderEntity.referenceShader = 0;
		renderEntity.bounds = renderEntity.hModel->Bounds( &renderEntity );
		SetAxis( mat3_identity );
	} else {
		if ( spawnArgs.GetString( "fx_idle" ) ) {
			UpdateModelTransform();
			effectIdle = PlayEffect( "fx_idle", renderEntity.origin, renderEntity.axis, true );
		}
	}

	UpdateFlatDiffusePresentation();
	
	GetPhysics( )->SetClipMask( GetPhysics( )->GetClipMask( ) | CONTENTS_ITEMCLIP );
	pickedUp = false;
}

/*
================
idItem::Event_SetGravity
================
*/
void idItem::Event_SetGravity() {
	// If the item isnt a dropped item then see if it should settle itself
	// to the floor or not
	if ( !spawnArgs.GetBool( "dropped" ) ) {
		if ( spawnArgs.GetBool( "nodrop" ) ) {
			physicsObj.PutToRest();
		} else {
	 		PostEventMS( &EV_DropToFloor, 0 );
		}
	}
}
// RAVEN END

/*
================
idItem::GetAttributes
================
*/
void idItem::GetAttributes( idDict &attributes ) {
	int					i;
	const idKeyValue	*arg;

	for( i = 0; i < spawnArgs.GetNumKeyVals(); i++ ) {
		arg = spawnArgs.GetKeyVal( i );
		if ( arg->GetKey().Left( 4 ) == "inv_" ) {
			attributes.Set( arg->GetKey().Right( arg->GetKey().Length() - 4 ), arg->GetValue() );
		}
	}
}

/*
================
idItem::GiveToPlayer
================
*/
bool idItem::GiveToPlayer( idPlayer *player, bool updateHud ) {
	if ( player == NULL ) {
		return false;
	}

	if ( spawnArgs.GetBool( "inv_carry" ) ) {
		return player->GiveInventoryItem( &spawnArgs, updateHud );
	} 
	
	// Handle the special ammo pickup that gives ammo for the weapon the player currently has
	if ( spawnArgs.GetBool( "item_currentWeaponAmmo" ) ) {
		// openQ4: there is no weapon object for the frames between idPlayer::SetWeapon
		// freeing the old one and the new one being built, and running over an item is
		// exactly how that window gets opened.
		if ( player->weapon == NULL ) {
			return false;
		}
		const char *ammoName = player->weapon->GetAmmoNameForIndex(player->weapon->GetAmmoType());
		if ( player->weapon->TotalAmmoCount() != player->weapon->maxAmmo && player->weapon->AmmoRequired() ) {
			player->GiveItem(ammoName);
			player->GiveInventoryItem( &spawnArgs );
			return true;
		}
		return false;
	} 

	return player->GiveItem( this, updateHud );
}

/*
===============
idItem::SendPickupMsg
===============
*/
void idItem::SendPickupMsg( int clientNum ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_GAME_MESSAGE_SIZE];

	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.WriteByte( GAME_UNRELIABLE_MESSAGE_EVENT );
	msg.WriteBits( gameLocal.GetSpawnId( this ), 32 );
	msg.WriteByte( EVENT_PICKUP );
	msg.WriteByte( clientNum );

	// send as unreliable to client picking up the item, so it can play HUD things
	gameLocal.SendUnreliableMessagePVS( msg, this, itemPVSArea );	
}

/*
================
idItem::Pickup
================
*/
bool idItem::Pickup( idPlayer *player ) {
	//dropped weapon?
	bool dropped = spawnArgs.GetBool( "dropped" );

	if ( gameLocal.isMultiplayer && !dropped && spawnArgs.FindKey( "weaponclass" ) 
		&& gameLocal.IsWeaponsStayOn() && gameLocal.time > player->lastPickupTime + 1000 ) {
		
		idDict attr;
		GetAttributes( attr );
		const idKeyValue* arg = attr.FindKey( "weapon" );

		if ( arg ) {
			if ( !player->inventory.Give( player, player->spawnArgs, arg->GetKey(), arg->GetValue(), NULL, false, dropped, true ) ) {
				StartSound( "snd_noacquire", SND_CHANNEL_ITEM, 0, false, NULL );
			}
		}
	}

	// only predict noacquire on client
	if ( gameLocal.isClient ) {
		return false;
	}

	int givenToPlayer = spawnArgs.GetInt( "givenToPlayer", "-1" );
	if ( player == NULL || ( givenToPlayer != -1 && givenToPlayer != player->entityNumber ) ) {
		// idPlayer::GiveItem spawns an idItem for pickup, which appears at the origin before being picked
		// and could sometimes be picked by someone else, particularly in buy mode when there is a play spawn sitting on the origin ;-)
		return false;
	}

	if ( !GiveToPlayer( player ) ) {
		return false;
	}

	if ( gameLocal.isServer ) {
		SendPickupMsg( player->entityNumber );
	}

	// Check for global acquire sounds in multiplayer
 	if ( gameLocal.isMultiplayer && spawnArgs.GetBool( "globalAcquireSound" ) ) {
		gameLocal.mpGame.PlayGlobalItemAcquireSound( entityDefNumber );
	} else {
		StartSound( "snd_acquire", SND_CHANNEL_ITEM, 0, false, NULL );
	}
		
	// trigger our targets
	ActivateTargets( player );

	player->lastPickupTime = gameLocal.time;

	//if a placed item and si_weaponStay is on and we're a weapon, don't remove and respawn
	if ( gameLocal.IsMultiplayer() ) {
		if ( !dropped )	{
			if ( spawnArgs.FindKey( "weaponclass" ) ) { 
				if ( gameLocal.IsWeaponsStayOn() ) {
					return true;
				}
			}
		}
	}

	// clear our contents so the object isn't picked up twice
	GetPhysics()->SetContents( 0 );

	// hide the model, or switch to the pickup skin
	if ( pickupSkin ) {
		SetSkin( pickupSkin );
		srvReady = 0;
	} else {
		Hide();
		BecomeInactive( TH_THINK );
	}

	pickedUp = true;

	// allow SetSkin or Hide() to get called regardless of simpleitem mode
	if( simpleItem ) {
		FreeModelDef();
		UpdateVisuals();
	}

	// remove the highlight shell
	if ( itemShellHandle != -1 ) {
		gameRenderWorld->FreeEntityDef( itemShellHandle );
		itemShellHandle = -1;
	}

	// asalmon: Added option for a differnt respawn rate based on gametype.
	float respawn = spawnArgs.GetFloat(va("respawn_%s",gameLocal.serverInfo.GetString( "si_gameType" )), "-1.0");
	if( respawn == -1.0f ) {
		respawn = spawnArgs.GetFloat( "respawn", "5.0" );
	}

	bool no_respawn = spawnArgs.GetBool( "no_respawn" );

	if ( !gameLocal.isMultiplayer ) {
		respawn = 0.0f;
	} else if ( gameLocal.mpGame.IsBuyingAllowedInTheCurrentGameMode() ) {
		if ( givenToPlayer != -1 ) {
			respawn = 0.0f;
		}
	}

	if ( respawn && !dropped && !no_respawn ) {
		const char *sfx = spawnArgs.GetString( "fx_Respawn" );
		if ( sfx && *sfx ) {
			PostEventSec( &EV_RespawnFx, respawn - 0.5f );
		}
		PostEventSec( &EV_RespawnItem, respawn );
	} else if ( !spawnArgs.GetBool( "inv_objective" ) && !no_respawn ) {
		// give some time for the pickup sound to play
		// FIXME: Play on the owner
		if ( !spawnArgs.GetBool( "inv_carry" ) ) {
			PostEventMS( &EV_Remove, 5000 );
		}
	}
	
	trigger->SetContents( 0 );	
	
	StopEffect( "fx_idle" );

	return true;
}

/*
================
idItem::Hide
================
*/
void idItem::Hide( void ) {
	srvReady = 0;
	idEntity::Hide( );	
	trigger->SetContents( 0 );
}

/*
================
idItem::Show
================
*/
void idItem::Show( void ) {
	srvReady = 1;
	idEntity::Show( );
	trigger->SetContents( CONTENTS_TRIGGER );
}

/*
================
idItem::ClientStale
================
*/
bool idItem::ClientStale( void ) {
	idEntity::ClientStale();

	StopEffect( "fx_idle" );
	return false;
}

/*
================
idItem::ClientUnstale
================
*/
void idItem::ClientUnstale( void ) {
	idEntity::ClientUnstale();

	UpdateModelTransform();
	if ( !simpleItem && spawnArgs.GetString( "fx_idle" ) ) {
		PlayEffect( "fx_idle", renderEntity.origin, renderEntity.axis, true );
	}
}

/*
================
idItem::ClientPredictionThink
================
*/
void idItem::ClientPredictionThink( void ) {
	// only think forward because the state is not synced through snapshots
	if ( !gameLocal.isNewFrame ) {
		return;
	}
	Think();
}

/*
================
idItem::WriteFromSnapshot
================
*/
void idItem::WriteToSnapshot( idBitMsgDelta &msg ) const {
	if ( syncPhysics ) {
		physicsObj.WriteToSnapshot( msg );
	}
	assert( srvReady != -1 );
	msg.WriteBits( ( srvReady == 1 ), 1 );
}

/*
================
idItem::ReadFromSnapshot
================
*/
void idItem::ReadFromSnapshot( const idBitMsgDelta &msg ) {
	if ( syncPhysics ) {
		physicsObj.ReadFromSnapshot( msg );
	}
	int newReady = ( msg.ReadBits( 1 ) != 0 );
	idVec3 resetOrigin( 0, 0, 0 );
	// client spawns the ent with ready == -1 so the state set happens at least once
	if ( newReady != clReady ) {
		if ( newReady ) {
			// g_simpleItems might force a hide even with a pickup skin
			if ( pickupSkin ) {
				SetSkin( skin );
			} else {
				SetSkin( skin );
				Show();
			}

			if( simpleItem ) {
				UpdateVisuals();			
			}
			pickedUp = false;

			if ( effectIdle.GetEntity( ) ) {
				UpdateModelTransform();
				effectIdle->SetOrigin( resetOrigin );
			} else if ( spawnArgs.GetString( "fx_idle" ) && !simpleItem ) {
				UpdateModelTransform();
				effectIdle = PlayEffect( "fx_idle", renderEntity.origin, renderEntity.axis, true );
			}
		} else {
			if ( pickupSkin ) {
				SetSkin( pickupSkin );
			} else {
				Hide();
			}

			if( simpleItem ) {
				FreeModelDef();
				UpdateVisuals();
			}
			
			pickedUp = true;

			StopEffect( "fx_idle" );
			effectIdle = NULL;
		}
	}
	clReady = newReady;
}

/*
================
idItem::Event_Pickup
================
*/
void idItem::Event_Pickup( int clientNum ) {
	idPlayer *player;

	assert( gameLocal.isClient );
	
	// play pickup sound
	if ( !spawnArgs.GetBool( "globalAcquireSound" ) ) {
		StartSound( "snd_acquire", SND_CHANNEL_ITEM, 0, false, NULL );
	}
	
	if( clientNum == gameLocal.localClientNum ) {
		player = (idPlayer*)gameLocal.entities[ clientNum ];
		player->lastPickupTime = gameLocal.time;
		if ( player ) {
			player->GiveItem( this );
		}
	}
}

/*
================
idItem::ClientReceiveEvent
================
*/
bool idItem::ClientReceiveEvent( int event, int time, const idBitMsg &msg ) {
	switch( event ) {
		case EVENT_PICKUP: {
			int clientNum = msg.ReadByte();
			if( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
				Event_Pickup( clientNum );
			}
			return true;
		}
		case EVENT_RESPAWNFX: {
			Event_RespawnFx();
			return true;
		}
		default: {
			return idEntity::ClientReceiveEvent( event, time, msg );
		}
	}
//unreachable
//	return false;
}

/*
================
idItem::Event_DropToFloor
================
*/
void idItem::Event_DropToFloor( void ) {
 	// don't drop the floor if bound to another entity
 	if ( GetBindMaster() != NULL && GetBindMaster() != this ) {
 		return;
 	}
 	
 	physicsObj.DropToFloor( );
}

/*
================
idItem::Event_Touch
================
*/
void idItem::Event_Touch( idEntity *other, trace_t *trace ) {
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
	if ( !other->IsType( idPlayer::GetClassType() ) ) {
// RAVEN END
		return;
	}

	if ( !canPickUp ) {
		return;
	}

	Pickup( static_cast<idPlayer *>(other) );
}

/*
================
idItem::Event_Trigger
================
*/
void idItem::Event_Trigger( idEntity *activator ) {
	if ( !canPickUp && spawnArgs.GetBool( "triggerFirst" ) ) {
		canPickUp = true;
		return;
	}

// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
	if ( activator && activator->IsType( idPlayer::GetClassType() ) ) {
// RAVEN END
		Pickup( static_cast<idPlayer *>( activator ) );
	}
}

/*
================
idItem::Event_Respawn
================
*/
void idItem::Event_Respawn( void ) {

	// with simple items, re-show the item, but still set srvReady to true to let clients
	// know how to deal
	if ( pickupSkin ) {
		srvReady = true;
		if( !simpleItem ) {
			SetSkin( skin );
		}
	} 
	
	if( !pickupSkin ) {
		BecomeActive( TH_THINK );	
		Show();
	}

	if( simpleItem ) {
		Show();
	}

	pickedUp = false;

	inViewTime = -1000;
	lastCycle = -1;
	trigger->SetContents ( CONTENTS_TRIGGER );
	SetOrigin( orgOrigin );
	StartSound( "snd_respawn", SND_CHANNEL_ITEM, 0, false, NULL );
	PostEventMS( &EV_SetGravity, 0 );
	CancelEvents( &EV_RespawnItem ); // don't double respawn

	if ( !simpleItem && spawnArgs.GetString( "fx_idle" ) ) {
		UpdateModelTransform();
		PlayEffect( "fx_idle", renderEntity.origin, renderEntity.axis, true );
	}
}

/*
================
idItem::Event_RespawnFx
================
*/
void idItem::Event_RespawnFx( void ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_GAME_MESSAGE_SIZE];

	if ( gameLocal.isServer ) {
		msg.Init( msgBuf, sizeof( msgBuf ) );
		msg.WriteByte( GAME_UNRELIABLE_MESSAGE_EVENT );
		msg.WriteBits( gameLocal.GetSpawnId( this ), 32 );
		msg.WriteByte( EVENT_RESPAWNFX );
		// send unreliable PVS-sensitive
		gameLocal.SendUnreliableMessagePVS( msg, this, gameLocal.pvs.GetPVSArea( GetPhysics()->GetOrigin() ) );
	}

	if( !simpleItem ) {
		gameLocal.PlayEffect( spawnArgs, "fx_respawn", GetPhysics()->GetOrigin(), idVec3(0,0,1).ToMat3(), false, vec3_origin );
	}
}

/*
===============================================================================

  idItemPowerup

===============================================================================
*/

/*
===============
idItemPowerup
===============
*/

CLASS_DECLARATION( idItem, idItemPowerup )
END_CLASS

/*
================
idItemPowerup::idItemPowerup
================
*/
idItemPowerup::idItemPowerup() {
	time = 0;
	type = 0;
	droppedTime = 0;
	unique = false;
}

/*
================
idItemPowerup::Save
================
*/
void idItemPowerup::Save( idSaveGame *savefile ) const {
	savefile->WriteInt( time );
	savefile->WriteInt( type );
	savefile->WriteInt( droppedTime );	// cnicholson: Added unsaved var
}

/*
================
idItemPowerup::Restore
================
*/
void idItemPowerup::Restore( idRestoreGame *savefile ) {
	savefile->ReadInt( time );
	savefile->ReadInt( type );
	savefile->ReadInt( droppedTime );	// cnicholson: Added unrestored var
}

/*
================
idItemPowerup::Spawn
================
*/
void idItemPowerup::Spawn( void ) {
	time = SEC2MS( spawnArgs.GetInt( "time", "30" ) );
	// SEC2MS screws up when we want -1 time (no expiration)
	if( spawnArgs.GetInt( "time" ) == -1 ) {
		time = -1;
	}

	type = spawnArgs.GetInt( "type", "0" );
	
	// If the powerup was dropped then make it dissapear using its remaining time.
	if ( spawnArgs.GetBool( "dropped" ) && time != -1 ) {
		droppedTime = gameLocal.time + time;
		PostEventMS( &EV_Remove, time );
	}

	// unique powerpus won't respawn while a player has them
	unique = spawnArgs.GetBool( "unique", "0" );
	if( unique ) {
		spawnArgs.SetBool( "no_respawn", true );
	}

	if ( !idStr::Icmp( spawnArgs.GetString( "team" ), "strogg" ) ) {
		team = TEAM_STROGG;
	} else if( !idStr::Icmp( spawnArgs.GetString( "team" ), "marine" ) ) {
		team = TEAM_MARINE;
	} else {
		team = -1;
	}
}

/*
================
idItemPowerup::GiveToPlayer
================
*/
bool idItemPowerup::GiveToPlayer( idPlayer *player, bool updateHud ) {
	if ( player == NULL || player->spectating ) {
		return false;
	}

	// only one arena CTF powerup at a time
	if ( type >= POWERUP_AMMOREGEN && type <= POWERUP_SCOUT ) {
		if ( ( player->inventory.powerups & ARENA_POWERUP_MASK ) != 0 ) {
			return false;
		} 
	}

	// in flavours of arena CTF (or are idItemPowerups only used in Arena? or even, are idItemPowerups MP only?), 
	//	ensure that items with a team can only be picked up by members of that team
	if ( gameLocal.IsMultiplayer() && gameLocal.IsTeamGame() && team >= 0 ) {
		if( team != player->team ) {
			return false;
		}
	}

	if ( droppedTime > 0 ) {
		player->GivePowerUp( type, droppedTime - gameLocal.time );
	} else {
		player->GivePowerUp( type, time );
	}

	// also call idItem::GiveToPlayer so any inv_* keywords get applied
	idItem::GiveToPlayer( player, updateHud );

	return true;
}

/*
================
idItemPowerup::Think
================
*/
void idItemPowerup::Think( void ) {
	int i;

	// idItem::Think() only needs to be called if we're spawned in
	if( !IsHidden() || (pickupSkin && GetSkin() != pickupSkin ) ) {
		// only get here if spawned in
		idItem::Think();
		return;
	}

	if( !unique ) {
		// non-unique despawned powerups don't need to think
		return;
	}

	for( i = 0; i < gameLocal.numClients; i++ ) {
		idPlayer* p = (idPlayer*)gameLocal.entities[ i ];
		if( p == NULL ) {
			continue;
		}

		// only spawn back in if noone on your team has the powerup
		if( p->PowerUpActive( type ) && p->team == team ) {
			break;
		}
	}

	if( i >= gameLocal.numClients ) {
		PostEventMS( &EV_RespawnItem, 0 );
	}
}

/*
================
idItemPowerup::Pickup
================
*/
bool idItemPowerup::Pickup( idPlayer* player ) {
	// regular pickup routine, but unique items need to think to know when to respawn
	bool pickup;
	
	if( gameLocal.isClient ) {
		// no client-side powerup prediction
		return false;
	}

	pickup = idItem::Pickup( player );
	if( unique ) {
		BecomeActive( TH_THINK );
	}

	return pickup;
}

/*
===============================================================================

  idObjective

===============================================================================
*/

CLASS_DECLARATION( idItem, idObjective )
	EVENT( EV_Activate,			idObjective::Event_Trigger )
	EVENT( EV_HideObjective,	idObjective::Event_HideObjective )
	EVENT( EV_GetPlayerPos,		idObjective::Event_GetPlayerPos )
 	EVENT( EV_CamShot,			idObjective::Event_CamShot )
END_CLASS

/*
================
idObjective::idObjective
================
*/
idObjective::idObjective() {
	playerPos.Zero();

// RAVEN BEGIN
// mekberg: store triggered time for timed removal.
	triggerTime = 0;
// RAVEN END
}

/*
================
idObjective::Save
================
*/
void idObjective::Save( idSaveGame *savefile ) const {
	savefile->WriteVec3( playerPos );
}

/*
================
idObjective::Restore
================
*/
void idObjective::Restore( idRestoreGame *savefile ) {
	savefile->ReadVec3( playerPos );
// RAVEN BEGIN
// jnewquist: Don't do this on Xenon, we want prebuilt textures
#ifndef _XENON
 	PostEventMS( &EV_CamShot, 250 );
#endif
// RAVEN END
}

/*
================
idObjective::Spawn
================
*/
void idObjective::Spawn( void ) {
	Hide();
// RAVEN BEGIN
// jnewquist: Only post a camshot event if the spawn args request it
#ifndef _XENON
	const char *camName;
	if ( spawnArgs.GetString( "camShot", "", &camName ) ) {
		common->Warning( "SpawnArg camShot on %s is not recommended.", spawnArgs.GetString( "name" ) );
 		PostEventMS( &EV_CamShot, 250 );
	}
#endif
// RAVEN END
}

/*
================
idObjective::Event_Screenshot
================
*/
void idObjective::Event_CamShot( ) {
	const char *camName;
// RAVEN BEGIN
// bdube: changed screenshot location
 	idStr shotName = "gfx/objectives/";
 	shotName += spawnArgs.GetString( "screenshot" );
 	shotName.SetFileExtension( ".tga" );
 // RAVEN END
	if ( spawnArgs.GetString( "camShot", "", &camName ) ) {
		idEntity *ent = gameLocal.FindEntity( camName );
		if ( ent && ent->cameraTarget ) {
			const renderView_t *view = ent->cameraTarget->GetRenderView();
			renderView_t fullView = *view;
			fullView.width = SCREEN_WIDTH;
			fullView.height = SCREEN_HEIGHT;

			// draw a view to a texture
			renderSystem->CropRenderSize( 256, 256, true );
			gameRenderWorld->RenderScene( &fullView );
			// TTimo: I don't think this jives with SMP code, but I grepped through the final maps and didn't see any using it
 			renderSystem->CaptureRenderToFile( shotName );
			renderSystem->UnCrop();
		}
	}
}  

/*
================
idObjective::Event_Trigger
================
*/
void idObjective::Event_Trigger( idEntity *activator ) {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( player ) {
		if ( spawnArgs.GetString( "inv_objective", NULL ) ) {
// RAVEN BEGIN
// abahr: changed player->hud to player->GetHud so when in a vehicle we update the vehicle hud
// twhitaker: changed player->hud to player->GetObjectiveHud(), to resolve all issues
	 		if ( player && player->GetObjectiveHud() ) {
// bdube: changed screenshot location
				idStr shotName = spawnArgs.GetString( "screenshot" );

				player->GetObjectiveHud()->SetStateString( "screenshot", shotName );
				player->GetObjectiveHud()->SetStateString( "objective", "1" );
				player->GetObjectiveHud()->SetStateString( "objectivetext", common->GetLocalizedString( spawnArgs.GetString( "objectivetext" ) ) );
				player->GetObjectiveHud()->SetStateString( "objectivetitle", common->GetLocalizedString( spawnArgs.GetString( "objectivetitle" ) ) );
// RAVEN END
 				player->GiveObjective( spawnArgs.GetString( "objectivetitle" ), spawnArgs.GetString( "objectivetext" ), shotName );

				// a tad slow but keeps from having to update all objectives in all maps with a name ptr
				for( int i = 0; i < gameLocal.num_entities; i++ ) {
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
					if ( gameLocal.entities[ i ] && gameLocal.entities[ i ]->IsType( idObjectiveComplete::GetClassType() ) ) {
// RAVEN END
						if ( idStr::Icmp( spawnArgs.GetString( "objectivetitle" ), gameLocal.entities[ i ]->spawnArgs.GetString( "objectivetitle" ) ) == 0 ){
							gameLocal.entities[ i ]->spawnArgs.SetBool( "objEnabled", true );
							break;
						}
					}
				}

				PostEventMS( &EV_GetPlayerPos, 2000 );
// RAVEN BEGIN
// mekberg: store triggered time for timed removal.
				triggerTime = gameLocal.time;
// RAVEN END
			}
		}
	}
}

/*
================
idObjective::Event_GetPlayerPos
================
*/
void idObjective::Event_GetPlayerPos() {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( player ) {
		playerPos = player->GetPhysics()->GetOrigin();
		PostEventMS( &EV_HideObjective, 100, player );
	}
}

/*
================
idObjective::Event_HideObjective
================
*/
void idObjective::Event_HideObjective(idEntity *e) {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( player ) {
		idVec3 v = player->GetPhysics()->GetOrigin() - playerPos;
// RAVEN BEGIN
// mekberg: hide time done internally now
		if ( v.Length() > 64.0f || gameLocal.time > triggerTime + 5000 ) {
// RAVEN END
			player->HideObjective ( "oq4_closeNewObjective" );
			PostEventMS( &EV_Remove, 0 );
		} else {
			PostEventMS( &EV_HideObjective, 100, player );
		}
	}
}

/*
===============================================================================

  idMoveableItem
	
===============================================================================
*/

CLASS_DECLARATION( idItem, idMoveableItem )
 	EVENT( EV_Gib,			idMoveableItem::Event_Gib )
END_CLASS

/*
================
idMoveableItem::idMoveableItem
================
*/
idMoveableItem::idMoveableItem() {
}

/*
================
idMoveableItem::~idMoveableItem
================
*/
idMoveableItem::~idMoveableItem() {
	// If this entity has been allocated, but not spawned, the physics object will
	// not have a self pointer, and will not unregister itself from the entity.
	SetPhysics( NULL );
}

/*
================
idMoveableItem::Save
================
*/
void idMoveableItem::Save( idSaveGame *savefile ) const {
   	savefile->WriteStaticObject( physicsObj );
}

/*
================
idMoveableItem::Restore
================
*/
void idMoveableItem::Restore( idRestoreGame *savefile ) {
	savefile->ReadPhysicsObject( physicsObj, this );
	RestorePhysics ( &physicsObj );
}

/*
================
idMoveableItem::Spawn
================
*/
void idMoveableItem::Spawn( void ) {
	idTraceModel trm;
	float density, friction, bouncyness;
	idStr clipModelName;
	idBounds bounds;

	// if the model should be shrinked
	if ( spawnArgs.GetBool( "clipshrink" ) ) {
		trm.Shrink( CM_CLIP_EPSILON );
	}

	// get rigid body properties
	spawnArgs.GetFloat( "density", "0.5", density );
	density = idMath::ClampFloat( 0.001f, 1000.0f, density );
	spawnArgs.GetFloat( "friction", "0.05", friction );
	friction = idMath::ClampFloat( 0.0f, 1.0f, friction );
	spawnArgs.GetFloat( "bouncyness", "0.6", bouncyness );
	bouncyness = idMath::ClampFloat( 0.0f, 1.0f, bouncyness );

	// setup the physics
	physicsObj.SetSelf( this );

// RAVEN BEGIN
// mwhitlock: Dynamic memory consolidation
	RV_PUSH_HEAP_MEM(this);
	// check if a clip model is set
	spawnArgs.GetString( "itemclipmodel", "", clipModelName );
	if ( clipModelName[0] ) {
		if ( collisionModelManager->TrmFromModel( gameLocal.GetMapName(), clipModelName, trm ) ) {
			physicsObj.SetClipModel( new idClipModel( trm ), density );
		} else {
			// fallback
			physicsObj.SetClipModel( new idClipModel( GetPhysics()->GetClipModel() ), density );
		}
	} else {
		// fallback
		physicsObj.SetClipModel( new idClipModel( GetPhysics()->GetClipModel() ), density );
	}

// mwhitlock: Dynamic memory consolidation
	RV_POP_HEAP();
// RAVEN END
	physicsObj.SetOrigin( GetPhysics()->GetOrigin() );
	physicsObj.SetAxis( GetPhysics()->GetAxis() );
	physicsObj.SetBouncyness( bouncyness );
	physicsObj.SetFriction( 0.6f, 0.6f, friction );
	physicsObj.SetGravity( gameLocal.GetGravity() );
	physicsObj.SetContents( CONTENTS_RENDERMODEL );
	physicsObj.SetClipMask( MASK_SOLID | CONTENTS_MOVEABLECLIP );
	SetPhysics( &physicsObj );

// RAVEN BEGIN
// mekberg: added
	if ( spawnArgs.GetBool( "noimpact" ) || spawnArgs.GetBool( "notPushable" ) ) {
		physicsObj.DisableImpact();
	}
// RAVEN END
}

/*
================
idMoveableItem::Think
================
*/
void idMoveableItem::Think( void ) {
	RunPhysics();
	UpdateTrigger( );
	Present();
}

/*
================
idMoveableItem::DropItem
================
*/
idEntity *idMoveableItem::DropItem( const char *classname, const idVec3 &origin, const idMat3 &axis, const idVec3 &velocity, int activateDelay, int removeDelay ) {
	idDict args;
	idEntity *item;

	args.Set( "classname", classname );
	args.Set( "dropped", "1" );

 	// we sometimes drop idMoveables here, so set 'nodrop' to 1 so that it doesn't get put on the floor
 	args.Set( "nodrop", "1" ); 

	if ( activateDelay ) {
		args.SetBool( "triggerFirst", true );
	}

	gameLocal.SpawnEntityDef( args, &item );
	if ( item ) {
		// set item position
		item->GetPhysics()->SetOrigin( origin );
		item->GetPhysics()->SetAxis( axis );
		item->GetPhysics()->SetLinearVelocity( velocity );
 		item->UpdateVisuals();
		if ( activateDelay ) {
			item->PostEventMS( &EV_Activate, activateDelay, item );
		}
		if ( !removeDelay ) {
			removeDelay = 5 * 60 * 1000;
		}
		// always remove a dropped item after 5 minutes in case it dropped to an unreachable location
		item->PostEventMS( &EV_Remove, removeDelay );
	}

 	return item;
}

/*
================
idMoveableItem::DropItems

  The entity should have the following key/value pairs set:
 	"def_drop<type>Item"			"item def"
 	"drop<type>ItemJoint"			"joint name"
 	"drop<type>ItemRotation"		"pitch yaw roll"
 	"drop<type>ItemOffset"			"x y z"
 	"skin_drop<type>"				"skin name"
  To drop multiple items the following key/value pairs can be used:
 	"def_drop<type>Item<X>"			"item def"
 	"drop<type>Item<X>Joint"		"joint name"
 	"drop<type>Item<X>Rotation"		"pitch yaw roll"
 	"drop<type>Item<X>Offset"		"x y z"
  where <X> is an aribtrary string.
================
*/
void idMoveableItem::DropItems( idAnimatedEntity  *ent, const char *type, idList<idEntity *> *list ) {
	const idKeyValue *kv;
	const char *skinName, *c, *jointName;
	idStr key, key2;
	idVec3 origin;
	idMat3 axis;
	idAngles angles;
	const idDeclSkin *skin;
	jointHandle_t joint;
	idEntity *item;

	// drop all items
	kv = ent->spawnArgs.MatchPrefix( va( "def_drop%sItem", type ), NULL );
	while ( kv ) {

		c = kv->GetKey().c_str() + kv->GetKey().Length();
		if ( idStr::Icmp( c - 5, "Joint" ) != 0 && idStr::Icmp( c - 8, "Rotation" ) != 0 ) {

			key = kv->GetKey().c_str() + 4;
			key2 = key;
			key += "Joint";
			key2 += "Offset";
			jointName = ent->spawnArgs.GetString( key );
			joint = ent->GetAnimator()->GetJointHandle( jointName );
			if ( !ent->GetJointWorldTransform( joint, gameLocal.time, origin, axis ) ) {
				gameLocal.Warning( "%s refers to invalid joint '%s' on entity '%s'\n", key.c_str(), jointName, ent->name.c_str() );
				origin = ent->GetPhysics()->GetOrigin();
				axis = ent->GetPhysics()->GetAxis();
			}
			if ( g_dropItemRotation.GetString()[0] ) {
				angles.Zero();
				sscanf( g_dropItemRotation.GetString(), "%f %f %f", &angles.pitch, &angles.yaw, &angles.roll );
			} else {
				key = kv->GetKey().c_str() + 4;
				key += "Rotation";
				ent->spawnArgs.GetAngles( key, "0 0 0", angles );
			}
			axis *= angles.ToMat3() * axis;

			origin += ent->spawnArgs.GetVector( key2, "0 0 0" );

			item = DropItem( kv->GetValue(), origin, axis, vec3_origin, 0, 0 );
			if ( list && item ) {
				list->Append( item );
			}
		}

		kv = ent->spawnArgs.MatchPrefix( va( "def_drop%sItem", type ), kv );
	}

	// change the skin to hide all items
 	skinName = ent->spawnArgs.GetString( va( "skin_drop%s", type ) );
	if ( skinName[0] ) {
		skin = declManager->FindSkin( skinName );
		ent->SetSkin( skin );
	}
}

/*
======================
idMoveableItem::WriteToSnapshot
======================
*/
void idMoveableItem::WriteToSnapshot( idBitMsgDelta &msg ) const {
	physicsObj.WriteToSnapshot( msg );
}

/*
======================
idMoveableItem::ReadFromSnapshot
======================
*/
void idMoveableItem::ReadFromSnapshot( const idBitMsgDelta &msg ) {
	physicsObj.ReadFromSnapshot( msg );
 	if ( msg.HasChanged() ) {
		UpdateVisuals();
 	}
}

/*
============
idMoveableItem::Gib
============
*/
void idMoveableItem::Gib( const idVec3 &dir, const char *damageDefName ) {
	// remove the entity
	PostEventMS( &EV_Remove, 0 );
}

/*
============
idMoveableItem::Event_Gib
============
*/
void idMoveableItem::Event_Gib( const char *damageDefName ) {
	Gib( idVec3( 0, 0, 1 ), damageDefName );
}

/*
===============================================================================

  idItemRemover

===============================================================================
*/

CLASS_DECLARATION( idEntity, idItemRemover )
	EVENT( EV_Activate,		idItemRemover::Event_Trigger )
END_CLASS

/*
================
idItemRemover::Spawn
================
*/
void idItemRemover::Spawn( void ) {
}

/*
================
idItemRemover::RemoveItem
================
*/
void idItemRemover::RemoveItem( idPlayer *player ) {
	const char *remove;
	
	remove = spawnArgs.GetString( "remove" );
	player->RemoveInventoryItem( remove );
}

/*
================
idItemRemover::Event_Trigger
================
*/
void idItemRemover::Event_Trigger( idEntity *activator ) {
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
	if ( activator->IsType( idPlayer::GetClassType() ) ) {
// RAVEN END
		RemoveItem( static_cast<idPlayer *>(activator) );
	}
}

/*
===============================================================================

  idObjectiveComplete

===============================================================================
*/

CLASS_DECLARATION( idItemRemover, idObjectiveComplete )
	EVENT( EV_Activate,			idObjectiveComplete::Event_Trigger )
	EVENT( EV_HideObjective,	idObjectiveComplete::Event_HideObjective )
	EVENT( EV_GetPlayerPos,		idObjectiveComplete::Event_GetPlayerPos )
END_CLASS

/*
================
idObjectiveComplete::idObjectiveComplete
================
*/
idObjectiveComplete::idObjectiveComplete() {
	playerPos.Zero();

// RAVEN BEGIN
// mekberg: store triggered time for timed removal.
	triggerTime = 0;
// RAVEN END
}

/*
================
idObjectiveComplete::Save
================
*/
void idObjectiveComplete::Save( idSaveGame *savefile ) const {
	savefile->WriteVec3( playerPos );
}

/*
================
idObjectiveComplete::Restore
================
*/
void idObjectiveComplete::Restore( idRestoreGame *savefile ) {
	savefile->ReadVec3( playerPos );
}

/*
================
idObjectiveComplete::Spawn
================
*/
void idObjectiveComplete::Spawn( void ) {
 	spawnArgs.SetBool( "objEnabled", false );
	Hide();
}

/*
================
idObjectiveComplete::Event_Trigger
================
*/
void idObjectiveComplete::Event_Trigger( idEntity *activator ) {
 	if ( !spawnArgs.GetBool( "objEnabled" ) ) {
 		return;
 	}
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( player ) {
		RemoveItem( player );

		if ( spawnArgs.GetString( "inv_objective", NULL ) ) {
// RAVEN BEGIN
// abahr: changed player->hud to player->GetHud so if in a vehicle we update that hud
// twhitaker: moved objective system to it's own hud, to solve vehicle hud issues.
	 		if ( player->GetObjectiveHud() ) {
				player->GetObjectiveHud()->SetStateString( "objective", "2" );
				player->GetObjectiveHud()->SetStateString( "objectivetext", common->GetLocalizedString( spawnArgs.GetString( "objectivetext" ) ) );
				player->GetObjectiveHud()->SetStateString( "objectivetitle", common->GetLocalizedString( spawnArgs.GetString( "objectivetitle" ) ) );
// RAVEN END
				player->CompleteObjective( spawnArgs.GetString( "objectivetitle" ) );
				PostEventMS( &EV_GetPlayerPos, 2000 );
// RAVEN BEGIN
// mekberg: store triggered time for timed removal.
				triggerTime = gameLocal.time;
// RAVEN END
			}
		}
	}
}

/*
================
idObjectiveComplete::Event_GetPlayerPos
================
*/
void idObjectiveComplete::Event_GetPlayerPos() {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( player ) {
		playerPos = player->GetPhysics()->GetOrigin();
		PostEventMS( &EV_HideObjective, 100, player );
	}
}

/*
================
idObjectiveComplete::Event_HideObjective
================
*/
void idObjectiveComplete::Event_HideObjective( idEntity *e ) {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( player && player->GetObjectiveHud() ) {
		idVec3 v = player->GetPhysics()->GetOrigin();
		v -= playerPos;
// RAVEN BEGIN
// mekberg: hide time done internally now
		if ( v.Length() > 64.0f || gameLocal.time > triggerTime + 5000 ) {
// RAVEN END
			player->HideObjective ( "oq4_closeDoneObjective" );
			PostEventMS( &EV_Remove, 0 );
		} else {
			PostEventMS( &EV_HideObjective, 100, player );
		}
	}
}

/*
===============================================================================

  rvObjectiveFailed

===============================================================================
*/
CLASS_DECLARATION( idItemRemover, rvObjectiveFailed )
	EVENT( EV_Activate,			rvObjectiveFailed::Event_Trigger )
END_CLASS

rvObjectiveFailed::rvObjectiveFailed ( void ) {
}

/*
================
rvObjectiveFailed::Event_Trigger
================
*/
void rvObjectiveFailed::Event_Trigger( idEntity *activator ) {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( !player || !player->GetObjectiveHud() ) {
		return;
	}
	if ( !spawnArgs.GetString( "inv_objective", NULL ) ) {
		return;
	}	

	player->GetObjectiveHud()->SetStateString( "objective", "2" );
	player->GetObjectiveHud()->SetStateString( "objectivetext", common->GetLocalizedString( spawnArgs.GetString( "objectivetext" ) ) );
	player->GetObjectiveHud()->SetStateString( "objectivetitle", common->GetLocalizedString( spawnArgs.GetString( "objectivetitle" ) ) );
	player->FailObjective( spawnArgs.GetString( "objectivetitle" ) );
}

/*
===============================================================================

  rvItemCTFFlag

===============================================================================
*/

const idEventDef EV_ResetFlag ( "<resetflag>" );
const idEventDef EV_LinkTrigger( "<linktrigger>" );
CLASS_DECLARATION( idItem, rvItemCTFFlag )
	EVENT( EV_ResetFlag,		rvItemCTFFlag::Event_ResetFlag )
	EVENT( EV_LinkTrigger,		rvItemCTFFlag::Event_LinkTrigger )
END_CLASS

/*
================
rvItemCTFFlag::rvItemCTFFlag
================
*/
rvItemCTFFlag::rvItemCTFFlag() {
}


/*
================
rvItemCTFFlag::Spawn
================
*/
void rvItemCTFFlag::Spawn () {
/* rjohnson: I fixed the crash so we don't need to remove them...
	//don't spawn outside of CTF games
	if( (gameLocal.gameType != GAME_CTF) && (gameLocal.gameType != GAME_1F_CTF) &&
		(gameLocal.gameType != GAME_ARENA_CTF) && (gameLocal.gameType != GAME_ARENA_1F_CTF) ){
		//don't spawn!
		gameLocal.Error("Flag spawn on in non-CTF gametype.");
		PostEventMS ( &EV_Remove, 0 );
		return;
	}
*/
	spawnArgs.GetBool ( "dropped", "0", dropped );
	spawnArgs.GetInt ( "team", "0", team );

	bool reset = false;
	spawnArgs.GetBool ( "reset", "0", reset );

	switch ( team ) {
		case TEAM_MARINE: {
			powerup = POWERUP_CTF_MARINEFLAG;
			gameLocal.mpGame.SetFlagEntity( this, TEAM_MARINE );
			break;
		}
		case TEAM_STROGG: {
			powerup = POWERUP_CTF_STROGGFLAG;	
			gameLocal.mpGame.SetFlagEntity( this, TEAM_STROGG );
			break;
		}
		case TEAM_MAX: {
			powerup = POWERUP_CTF_ONEFLAG;
			break;
		}
		default:
			gameLocal.Warning ( "Unknown ctf flag team '%d' on entity '%s'", team, name.c_str() );
			PostEventMS ( &EV_Remove, 0 );
			return;
	}
	
	if ( dropped ) {
		if ( !gameLocal.isClient ) {
			((rvCTFGameState*)gameLocal.mpGame.GetGameState())->SetFlagState( team, FS_DROPPED );
		}	
		if ( reset ) {
			//PostEventSec( &EV_ResetFlag, 1 );
		} else {
			PostEventSec( &EV_ResetFlag, 30 );
		}

		// Let powerups settle for a frame before allowing them to be picked up - we need to
		// make sure we hit the dropped state for VO, etc to work properly
		trigger->SetContents( 0 );
		PostEventSec( &EV_LinkTrigger, 0 );
	} else {
		if ( !gameLocal.isClient ) {
			((rvCTFGameState*)gameLocal.mpGame.GetGameState())->SetFlagState( team, FS_AT_BASE );
		}	
	}

	GetPhysics( )->SetClipMask( GetPhysics( )->GetClipMask( ) | CONTENTS_ITEMCLIP );

	spin = false;
}

/*
================
rvItemCTFFlag::Event_LinkTrigger
================
*/
void rvItemCTFFlag::Event_LinkTrigger( void ) {
	trigger->SetContents( CONTENTS_TRIGGER | CONTENTS_ITEMCLIP );
}

/*
================
rvItemCTFFlag::GiveToPlayer
================
*/
bool rvItemCTFFlag::GiveToPlayer( idPlayer* player, bool updateHud ) {
	(void)updateHud;
	if ( !gameLocal.IsMultiplayer() ) {
		return false;
	}

	if ( player->spectating || ((gameLocal.mpGame.GetGameState())->GetMPGameState() != GAMEON && (gameLocal.mpGame.GetGameState())->GetMPGameState() != SUDDENDEATH && !cvarSystem->GetCVarBool( "g_testCTF" )) ) {
		return false;
	}

	int teamPowerup;
	int enemyPowerup;

	bool canPickup = ( team == player->team );
	
	switch ( player->team ) {
		default:
		case TEAM_MARINE:
			teamPowerup  = POWERUP_CTF_MARINEFLAG;
			enemyPowerup = POWERUP_CTF_STROGGFLAG;
			break;
			
		case TEAM_STROGG:
			teamPowerup  = POWERUP_CTF_STROGGFLAG;
			enemyPowerup = POWERUP_CTF_MARINEFLAG;
			break;
	}

	if( gameLocal.gameType == GAME_1F_CTF || gameLocal.gameType == GAME_ARENA_1F_CTF ) {
		// in one flag CTF, we score touching the enemy's flag
		canPickup = ( team == gameLocal.mpGame.OpposingTeam( player->team ) );
		enemyPowerup = POWERUP_CTF_ONEFLAG;
	}
	
	// If the player runs over their own flag the only thing they
	// can do is score or return it.

	// when the player touches the one flag, he always gets it
	if ( canPickup ) {
		// If the flag was dropped, return it		
		if ( dropped ) {
			gameLocal.mpGame.AddPlayerTeamScore( player, 2 );
			statManager->FlagReturned( player );
			ResetFlag ( teamPowerup );
// RITUAL BEGIN
// squirrel: Mode-agnostic buymenus
			player->GiveCash( (float)gameLocal.mpGame.mpBuyingManager.GetIntValueForKey( "playerCashAward_flagReturned", 0 ) );
// RITUAL END
		} else if ( player->PowerUpActive ( enemyPowerup ) ) {
			// If they have the enemy flag then they score
			if ( !gameLocal.mpGame.CanCapture ( player->team ) ) {
				return false;
			}
			
			ResetFlag( enemyPowerup );
			
			gameLocal.mpGame.FlagCaptured( player );
		}
		return false;
	} 

	// only pickup one flag in arena CTF
	if( ( gameLocal.gameType == GAME_1F_CTF || gameLocal.gameType == GAME_ARENA_1F_CTF ) && team != TEAM_MAX ) {
		return false;
	}

	player->GivePowerUp( enemyPowerup, -1 );
// RITUAL BEGIN
// squirrel: Mode-agnostic buymenus
	player->GiveCash( (float)gameLocal.mpGame.mpBuyingManager.GetIntValueForKey( "playerCashAward_flagStolen", 0 ) );
// RITUAL END
	return true;
}

/*
================
rvItemCTFFlag::Pickup
================
*/
bool rvItemCTFFlag::Pickup( idPlayer *player ) {
	if( gameLocal.isClient ) {
		// no client-side CTF flag prediction
		return false;
	}

	if ( !GiveToPlayer( player ) ) {
		return false;
	}

	//	ServerSendEvent( EVENT_PICKUP, NULL, false, -1 );
	if ( gameLocal.isServer ) {
		SendPickupMsg( player->entityNumber );
	}

	// Check for global acquire sounds in multiplayer
	if ( gameLocal.isMultiplayer && spawnArgs.GetBool( "globalAcquireSound" ) ) {
		gameLocal.mpGame.PlayGlobalItemAcquireSound( entityDefNumber );
	} else {
		StartSound( "snd_acquire", SND_CHANNEL_ITEM, 0, false, NULL );
	}

	// trigger our targets
	ActivateTargets( player );

	// clear our contents so the object isn't picked up twice
	GetPhysics()->SetContents( 0 );

	// hide the model
	Hide();

	gameLocal.mpGame.SetFlagEntity( NULL, team );

	gameLocal.mpGame.AddPlayerTeamScore( player, 1 );

	if( gameLocal.gameType == GAME_CTF || gameLocal.gameType == GAME_ARENA_CTF ) { 
		((rvCTFGameState*)gameLocal.mpGame.GetGameState())->SetFlagState( team, FS_TAKEN );
	} else if( gameLocal.gameType == GAME_1F_CTF || gameLocal.gameType == GAME_ARENA_1F_CTF ) {
		((rvCTFGameState*)gameLocal.mpGame.GetGameState())->SetFlagState( team, (player->team == TEAM_MARINE ? FS_TAKEN_MARINE : FS_TAKEN_STROGG) );
	}
	
	((rvCTFGameState*)gameLocal.mpGame.GetGameState())->SetFlagCarrier( team, player->entityNumber );

	if ( spawnArgs.GetBool( "dropped" ) ) {
		PostEventMS( &EV_Remove, 0 );
	}

	BecomeInactive( TH_THINK );
	
	return true;
}

/*
================
rvItemCTFFlag::ResetFlag
================
*/
void rvItemCTFFlag::ResetFlag( int powerup ) {
	idEntity* ent;
	for ( ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
		// Make sure no players have the flag anymore
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( ent->IsType ( idPlayer::GetClassType() ) ) {
// RAVEN END
			static_cast<idPlayer*>(ent)->ClearPowerup ( powerup );
			continue;
		}
	
		// If its not a CTF flag item then skip it
		if ( !ent->IsType( rvItemCTFFlag::Type ) ) {
			continue;			
		}
		
		// Make sure its the right type first
		rvItemCTFFlag* flag;
		flag = static_cast<rvItemCTFFlag*>(ent);
		if ( flag->powerup != powerup ) {
			continue;
		}
		
		if ( flag->dropped ) {			
			flag->PostEventMS( &EV_Remove, 0 );
		} else {
			flag->PostEventMS( &EV_RespawnItem, 0 );
			gameLocal.mpGame.SetFlagEntity( flag, flag->team );
		}
	}

	if ( !gameLocal.isClient ) {
		int team = -1;
		if ( powerup == POWERUP_CTF_MARINEFLAG ) {
			team = TEAM_MARINE;
		} else if ( powerup == POWERUP_CTF_STROGGFLAG ) {
			team = TEAM_STROGG;
		} else if ( powerup == POWERUP_CTF_ONEFLAG ) {
			team = TEAM_MAX;
		}
		
		((rvCTFGameState*)gameLocal.mpGame.GetGameState())->SetFlagState( team, FS_AT_BASE );
	}	

}
  
/*
================
rvItemCTFFlag::Event_ResetFlag
================
*/
void rvItemCTFFlag::Event_ResetFlag( void ) {
	ResetFlag( powerup );
}

void rvItemCTFFlag::Think( void ) {
	idItem::Think();
}

/*
================
rvItemCTFFlag::Collide
================
*/
bool rvItemCTFFlag::Collide( const trace_t &collision, const idVec3 &velocity ) {
	idEntity* lol = gameLocal.entities[ collision.c.entityNum ];
	
	if ( collision.c.contents & CONTENTS_ITEMCLIP && lol && !lol->IsType( idItem::GetClassType() ) ) {
		ResetFlag( powerup );
	}
	return false;
}
// RAVEN END




const idEventDef EV_ResetSpawn ( "<resetspawn>" );
CLASS_DECLARATION( idItemPowerup, riDeadZonePowerup )
	EVENT( EV_ResetSpawn,		riDeadZonePowerup::Event_ResetSpawn )
END_CLASS

/*
================
riDeadZonePowerup::idItemPowerup
================
*/
riDeadZonePowerup::riDeadZonePowerup() {
}

/*
================
riDeadZonePowerup::Save
================
*/
void riDeadZonePowerup::Save( idSaveGame *savefile ) const {
}

/*
================
riDeadZonePowerup::Restore
================
*/
void riDeadZonePowerup::Restore( idRestoreGame *savefile ) {
}


/*
================
riDeadZonePowerup::Show
================
*/
void riDeadZonePowerup::Show()
{
	idItem::Show();
}


/*
================
riDeadZonePowerup::Spawn
================
*/
void riDeadZonePowerup::Spawn( void ) {
	powerup = POWERUP_DEADZONE;

	time = SEC2MS( gameLocal.serverInfo.GetInt("si_deadZonePowerupTime") );

	if ( spawnArgs.GetBool( "dropped" ) ) {
		time = SEC2MS( spawnArgs.GetInt( "time", "10" ) );
		if ( time > SEC2MS(10) )
			time = SEC2MS(10);

		Show();
		CancelEvents(&EV_Remove);
		PostEventSec( &EV_ResetSpawn, MS2SEC(time) );
	}
	else
		Hide();
}


/*
================
riDeadZonePowerup::Pickup
================
*/
bool riDeadZonePowerup::Pickup( idPlayer* player ) {
	// regular pickup routine, but unique items need to think to know when to respawn
	bool pickup;
	if ( player->PowerUpActive(POWERUP_DEADZONE) )
		return false;

	pickup = idItemPowerup::Pickup( player );

	if ( spawnArgs.GetBool( "dropped" ) ) {
		// Cancel the respawn so the powerup doesn't get removed 
		// from the player that just picked it up.
		PostEventMS( &EV_Remove, 0 );
		CancelEvents( &EV_ResetSpawn );
	}

	return pickup;
}

/*
================
riDeadZonePowerup::ResetSpawn
================
*/
void riDeadZonePowerup::ResetSpawn( int powerup ) {
	int count = 1;
	idEntity* ent;
	riDeadZonePowerup* spawnSpot = 0;
	for ( ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {

		// If its not a DeadZone powerup then skip it
		if ( !ent->IsType( riDeadZonePowerup::Type ) ) {
			continue;			
		}
		
		// Make sure its the right type first
		riDeadZonePowerup* flag;
		flag = static_cast<riDeadZonePowerup*>(ent);
		if ( flag->powerup != powerup ) {
			continue;
		}
		
		if ( flag->spawnArgs.GetBool("dropped", "0") && flag == this ) {			
			flag->PostEventMS( &EV_Remove, 0 );
		} else {
			if ( !flag->IsVisible() ) {
				if ( !(rand()%count)  ) {
					spawnSpot = flag;
					if ( flag->spawnArgs.GetBool("dropped", "0") )
						gameLocal.DPrintf("WARNING: Trying to spawn a powerup at a DROPPED location!");
				}
				count++;
			}		
		}
	}	
	
	if ( spawnSpot ) {
		spawnSpot->Show();
		spawnSpot->PostEventMS( &EV_RespawnItem, 0 );			
	}
	else {
		gameLocal.DPrintf("WARNING: Failed to find a valid spawn spot!");
	}
}
  
/*
================
riDeadZonePowerup::Collide
================
*/
bool riDeadZonePowerup::Collide( const trace_t &collision, const idVec3 &velocity ) {
	idEntity* lol = gameLocal.entities[ collision.c.entityNum ];
	if ( gameLocal.isMultiplayer && collision.c.contents & CONTENTS_ITEMCLIP && lol && !lol->IsType( idItem::GetClassType() ) ) {
		PostEventMS( &EV_ResetSpawn, 0 ); // Just respawn it.
	}
	return false;
}

/*
================
riDeadZonePowerup::Event_ResetFlag
================
*/
void riDeadZonePowerup::Event_ResetSpawn( void ) {
	ResetSpawn( POWERUP_DEADZONE );
}
