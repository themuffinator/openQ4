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




#include "Model_local.h"
#include "tr_local.h"	// just for R_FreeWorldInteractions and R_CreateWorldInteractions


class idRenderModelManagerLocal : public idRenderModelManager {
public:
							idRenderModelManagerLocal();
	virtual					~idRenderModelManagerLocal() {}

	virtual void			Init();
	virtual void			Shutdown();
	virtual idRenderModel *	AllocModel();
	virtual void			FreeModel( idRenderModel *model );
	virtual idRenderModel *	FindModel( const char *modelName );
	virtual idRenderModel *	CheckModel( const char *modelName );
	virtual bool			ContainsModel( const idRenderModel *model ) const;
	virtual idRenderModel *	DefaultModel();
	virtual void			AddModel( idRenderModel *model );
	virtual void			RemoveModel( idRenderModel *model );
	virtual void			ReloadModels( bool forceAll = false );
	virtual void			FreeModelVertexCaches();
	virtual void			WritePrecacheCommands( idFile *file );
	virtual void			BeginLevelLoad();
	virtual void			EndLevelLoad();
	virtual int				CountPendingLevelLoads() const;

	virtual	void			PrintMemInfo( MemInfo_t *mi );

private:
	idList<idRenderModel*>	models;
	idHashIndex				hash;
	idRenderModel *			defaultModel;
	idRenderModel *			beamModel;
	idRenderModel *			spriteModel;
	idRenderModel *			trailModel;
	bool					insideLevelLoad;		// don't actually load now

	idRenderModel *			GetModel( const char *modelName, bool createIfNotFound );

	static void				PrintModel_f( const idCmdArgs &args );
	static void				ListModels_f( const idCmdArgs &args );
	static void				ReloadModels_f( const idCmdArgs &args );
	static void				TouchModel_f( const idCmdArgs &args );
};


idRenderModelManagerLocal	localModelManager;
idRenderModelManager *		renderModelManager = &localModelManager;

/*
=================
R_ModelManager_BuildMD5RName

Retail Quake 4 derives prebuilt companions from the canonical source model
name. Static authored assets get a "_static" suffix before the .md5r
extension, skeletal assets keep the base stem unchanged.
=================
*/
static void R_ModelManager_BuildMD5RName( idStr &md5rName, const idStr &canonical, const idStr &extension ) {
	md5rName = canonical;
	md5rName.StripAbsoluteFileExtension();

	if ( extension.Icmp( "ase" ) == 0
		|| extension.Icmp( "lwo" ) == 0
		|| extension.Icmp( "flt" ) == 0 ) {
		md5rName += "_static";
	}

	md5rName += ".md5r";
}

/*
=================
R_ModelManager_FindLoadablePrebuiltMD5R

 Retail prefers a compiled .md5rc companion when present, then falls back to
 the plain .md5r file. OpenQ4 mirrors that probe order and always hands the
 canonical .md5r name to the binary-aware lexer so it can resolve either form.
=================
*/
static bool R_ModelManager_FindLoadablePrebuiltMD5R( const idStr &md5rName, idStr &loadableName ) {
	loadableName.Clear();

	if ( r_forceConvertMD5R.GetBool() ) {
		return false;
	}

	idStr compiledName = md5rName;
	compiledName += "c";

	if ( idFile *compiledFile = fileSystem->OpenFileRead( compiledName ) ) {
		fileSystem->CloseFile( compiledFile );
		loadableName = md5rName;
		return true;
	}

	if ( idFile *sourceFile = fileSystem->OpenFileRead( md5rName ) ) {
		fileSystem->CloseFile( sourceFile );
		loadableName = md5rName;
		return true;
	}

	return false;
}

/*
=================
R_ModelManager_MaybeConvertMD5ToMD5R

 Retail Quake 4 can swap authored MD5 assets over to rvRenderModelMD5R here.
=================
*/
static idRenderModel *R_ModelManager_MaybeConvertMD5ToMD5R( idRenderModelMD5 *sourceModel ) {
	if ( sourceModel == NULL ) {
		return NULL;
	}

	if ( sourceModel->IsDefaultModel()
		|| idAsyncNetwork::serverDedicated.GetInteger() != 0 ) {
		return sourceModel;
	}

	R_DisableUnavailableMD5RCVar( r_convertMD5toMD5R, "the rvRenderModelMD5R runtime" );

	if ( !r_convertMD5toMD5R.GetBool() ) {
		return sourceModel;
	}

	rvRenderModelMD5R *convertedModel = new rvRenderModelMD5R;
	if ( convertedModel == NULL ) {
		common->Error( "ModelManager: out of memory" );
		return sourceModel;
	}

	if ( !convertedModel->InitFromMD5Model( *sourceModel ) ) {
		common->Warning( "Failed to convert the MD5 '%s' to an MD5R", sourceModel->Name() );
		delete convertedModel;
		return sourceModel;
	}

	delete sourceModel;
	return convertedModel;
}

/*
=================
R_ModelManager_MaybeConvertStaticToMD5R

 This mirrors the loader-side seam retail uses for static-model MD5R conversion.
=================
*/
static idRenderModel *R_ModelManager_MaybeConvertStaticToMD5R( idRenderModelStatic *sourceModel ) {
	if ( sourceModel == NULL ) {
		return NULL;
	}

	if ( sourceModel->IsDefaultModel() ) {
		return sourceModel;
	}

	R_DisableUnavailableMD5RCVar( r_convertStaticToMD5R, "the rvRenderModelMD5R runtime" );

	if ( !r_convertStaticToMD5R.GetBool() ) {
		return sourceModel;
	}

	rvRenderModelMD5R *convertedModel = new rvRenderModelMD5R;
	if ( convertedModel == NULL ) {
		common->Error( "ModelManager: out of memory" );
		return sourceModel;
	}

	if ( !convertedModel->InitFromStaticModel( *sourceModel, MD5R_SOURCE_LWO_ASE_FLT ) ) {
		common->Warning( "Failed to convert the static model '%s' to an MD5R", sourceModel->Name() );
		delete convertedModel;
		return sourceModel;
	}

	collisionModelManager->ExtractCollisionModel( sourceModel, sourceModel->Name() );
	delete sourceModel;
	return convertedModel;
}

/*
==============
idRenderModelManagerLocal::idRenderModelManagerLocal
==============
*/
idRenderModelManagerLocal::idRenderModelManagerLocal() {
	defaultModel = NULL;
	beamModel = NULL;
	spriteModel = NULL;
	insideLevelLoad = false;
	trailModel = NULL;
}

/*
==============
idRenderModelManagerLocal::PrintModel_f
==============
*/
void idRenderModelManagerLocal::PrintModel_f( const idCmdArgs &args ) {
	idRenderModel	*model;

	if ( args.Argc() != 2 ) {
		common->Printf( "usage: printModel <modelName>\n" );
		return;
	}

	model = renderModelManager->CheckModel( args.Argv( 1 ) );
	if ( !model ) {
		common->Printf( "model \"%s\" not found\n", args.Argv( 1 ) );
		return;
	}

	model->Print();
}

/*
==============
idRenderModelManagerLocal::ListModels_f
==============
*/
void idRenderModelManagerLocal::ListModels_f( const idCmdArgs &args ) {
	int		totalMem = 0;
	int		inUse = 0;

	common->Printf( " mem   srf verts tris\n" );
	common->Printf( " ---   --- ----- ----\n" );

	for ( int i = 0 ; i < localModelManager.models.Num() ; i++ ) {
		idRenderModel	*model = localModelManager.models[i];

		if ( !model->IsLoaded() ) {
			continue;
		}
		model->List();
		totalMem += model->Memory();
		inUse++;
	}

	common->Printf( " ---   --- ----- ----\n" );
	common->Printf( " mem   srf verts tris\n" );

	common->Printf( "%i loaded models\n", inUse );
	common->Printf( "total memory: %4.1fM\n", (float)totalMem / (1024*1024) );
}

/*
==============
idRenderModelManagerLocal::ReloadModels_f
==============
*/
void idRenderModelManagerLocal::ReloadModels_f( const idCmdArgs &args ) {
	if ( idStr::Icmp( args.Argv(1), "all" ) == 0 ) {
		localModelManager.ReloadModels( true );
	} else {
		localModelManager.ReloadModels( false );
	}
}

/*
==============
idRenderModelManagerLocal::TouchModel_f

Precache a specific model
==============
*/
void idRenderModelManagerLocal::TouchModel_f( const idCmdArgs &args ) {
	const char	*model = args.Argv( 1 );

	if ( !model[0] ) {
		common->Printf( "usage: touchModel <modelName>\n" );
		return;
	}

	common->Printf( "touchModel %s\n", model );
	session->UpdateScreen();
	idRenderModel *m = renderModelManager->CheckModel( model );
	if ( !m ) {
		common->Printf( "...not found\n" );
	}
}

/*
=================
idRenderModelManagerLocal::WritePrecacheCommands
=================
*/
void idRenderModelManagerLocal::WritePrecacheCommands( idFile *f ) {
	for ( int i = 0 ; i < models.Num() ; i++ ) {
		idRenderModel	*model = models[i];

		if ( !model ) {
			continue;
		}
		if ( !model->IsReloadable() ) {
			continue;
		}

		char	str[1024];
		sprintf( str, "touchModel %s\n", model->Name() );
		common->Printf( "%s", str );
		f->Printf( "%s", str );
	}
}

/*
=================
idRenderModelManagerLocal::Init
=================
*/
void idRenderModelManagerLocal::Init() {
	cmdSystem->AddCommand( "listModels", ListModels_f, CMD_FL_RENDERER, "lists all models" );
	cmdSystem->AddCommand( "printModel", PrintModel_f, CMD_FL_RENDERER, "prints model info", idCmdSystem::ArgCompletion_ModelName );
	cmdSystem->AddCommand( "reloadModels", ReloadModels_f, CMD_FL_RENDERER|CMD_FL_CHEAT, "reloads models" );
	cmdSystem->AddCommand( "touchModel", TouchModel_f, CMD_FL_RENDERER, "touches a model", idCmdSystem::ArgCompletion_ModelName );

	insideLevelLoad = false;

	// create a default model
	idRenderModelStatic *model = new idRenderModelStatic;
	model->InitEmpty( "_DEFAULT" );
	model->MakeDefaultModel();
	model->SetLevelLoadReferenced( true );
	defaultModel = model;
	AddModel( model );

	// create the beam model
	idRenderModelStatic *beam = new idRenderModelBeam;
	beam->InitEmpty( "_BEAM" );
	beam->SetLevelLoadReferenced( true );
	beamModel = beam;
	AddModel( beam );

	idRenderModelStatic *sprite = new idRenderModelSprite;
	sprite->InitEmpty( "_SPRITE" );
	sprite->SetLevelLoadReferenced( true );
	spriteModel = sprite;
	AddModel( sprite );
}

/*
=================
idRenderModelManagerLocal::Shutdown
=================
*/
void idRenderModelManagerLocal::Shutdown() {
	models.DeleteContents( true );
	hash.Free();
}

/*
=================
idRenderModelManagerLocal::GetModel
=================
*/
idRenderModel *idRenderModelManagerLocal::GetModel( const char *modelName, bool createIfNotFound ) {
	idStr		canonical;
	idStr		extension;
	idStr		md5rName;
	idStr		loadableMD5RName;

	if ( !modelName || !modelName[0] ) {
		return NULL;
	}

	canonical = modelName;
	canonical.ToLower();

	// see if it is already present
	int key = hash.GenerateKey( modelName, false );
	for ( int i = hash.First( key ); i != -1; i = hash.Next( i ) ) {
		idRenderModel *model = models[i];

		if ( canonical.Icmp( model->Name() ) == 0 ) {
			if ( !model->IsLoaded() ) {
				// reload it if it was purged
				model->LoadModel();
			} else if ( insideLevelLoad && !model->IsLevelLoadReferenced() ) {
				// we are reusing a model already in memory, but
				// touch all the materials to make sure they stay
				// in memory as well
				model->TouchData();
			}
			model->SetLevelLoadReferenced( true );
			return model;
		}
	}

	// see if we can load it

	// determine which subclass of idRenderModel to initialize

	idRenderModel	*model;

	canonical.ExtractFileExtension( extension );

	if ( extension.Icmp( "ase" ) == 0
		|| extension.Icmp( "lwo" ) == 0
		|| extension.Icmp( "flt" ) == 0
		|| extension.Icmp( MD5_MESH_EXT ) == 0 ) {
		R_ModelManager_BuildMD5RName( md5rName, canonical, extension );

		const int md5rKey = hash.GenerateKey( md5rName, false );
		for ( int i = hash.First( md5rKey ); i != -1; i = hash.Next( i ) ) {
			idRenderModel *cachedModel = models[i];

			if ( md5rName.Icmp( cachedModel->Name() ) == 0 ) {
				if ( !cachedModel->IsLoaded() ) {
					cachedModel->LoadModel();
				} else if ( insideLevelLoad && !cachedModel->IsLevelLoadReferenced() ) {
					cachedModel->TouchData();
				}
				cachedModel->SetLevelLoadReferenced( true );
				return cachedModel;
			}
		}

		R_ModelManager_FindLoadablePrebuiltMD5R( md5rName, loadableMD5RName );
	}

	if ( loadableMD5RName.Length() != 0 ) {
		model = new rvRenderModelMD5R;
		model->InitFromFile( loadableMD5RName.c_str() );
	} else if ( ( extension.Icmp( "ase" ) == 0 ) || ( extension.Icmp( "lwo" ) == 0 ) || ( extension.Icmp( "flt" ) == 0 ) ) {
		idRenderModelStatic *staticModel = new idRenderModelStatic;
		staticModel->InitFromFile( modelName );
		model = R_ModelManager_MaybeConvertStaticToMD5R( staticModel );
	} else if ( extension.Icmp( "ma" ) == 0 ) {
		model = new idRenderModelStatic;
		model->InitFromFile( modelName );
	} else if ( extension.Icmp( MD5_MESH_EXT ) == 0 ) {
		idRenderModelMD5 *md5Model = new idRenderModelMD5;
		md5Model->InitFromFile( modelName );
		model = R_ModelManager_MaybeConvertMD5ToMD5R( md5Model );
	} else if ( extension.Icmp( MD5R_MODEL_EXT ) == 0 ) {
		model = new rvRenderModelMD5R;
		model->InitFromFile( modelName );
	} else if ( extension.Icmp( "md3" ) == 0 ) {
		model = new idRenderModelMD3;
		model->InitFromFile( modelName );
	} else if ( extension.Icmp( "prt" ) == 0  ) {
		model = new idRenderModelPrt;
		model->InitFromFile( modelName );
	} else if ( extension.Icmp( "liquid" ) == 0  ) {
		model = new idRenderModelLiquid;
		model->InitFromFile( modelName );
	} else {

		if ( extension.Length() ) {
			common->Warning( "unknown model type '%s'", canonical.c_str() );
		}

		if ( !createIfNotFound ) {
			return NULL;
		}

		idRenderModelStatic	*smodel = new idRenderModelStatic;
		smodel->InitEmpty( modelName );
		smodel->MakeDefaultModel();

		model = smodel;
	}

	model->SetLevelLoadReferenced( true );

	if ( !createIfNotFound && model->IsDefaultModel() ) {
		delete model;
		model = NULL;

		return NULL;
	}

	AddModel( model );

	return model;
}

/*
=================
idRenderModelManagerLocal::AllocModel
=================
*/
idRenderModel *idRenderModelManagerLocal::AllocModel() {
	return new idRenderModelStatic();
}

/*
=================
idRenderModelManagerLocal::FreeModel
=================
*/
void idRenderModelManagerLocal::FreeModel( idRenderModel *model ) {
	if ( !model ) {
		return;
	}
	if ( !dynamic_cast<idRenderModelStatic *>( model ) ) {
		common->Error( "idRenderModelManager::FreeModel: model '%s' is not a static model", model->Name() );
		return;
	}
	if ( model == defaultModel ) {
		common->Error( "idRenderModelManager::FreeModel: can't free the default model" );
		return;
	}
	if ( model == beamModel ) {
		common->Error( "idRenderModelManager::FreeModel: can't free the beam model" );
		return;
	}
	if ( model == spriteModel ) { 
		common->Error( "idRenderModelManager::FreeModel: can't free the sprite model" );
		return;
	}

	R_CheckForEntityDefsUsingModel( model );

	delete model;
}

/*
=================
idRenderModelManagerLocal::FindModel
=================
*/
idRenderModel *idRenderModelManagerLocal::FindModel( const char *modelName ) {
	return GetModel( modelName, true );
}

/*
=================
idRenderModelManagerLocal::CheckModel
=================
*/
idRenderModel *idRenderModelManagerLocal::CheckModel( const char *modelName ) {
	return GetModel( modelName, false );
}

/*
=================
idRenderModelManagerLocal::ContainsModel
=================
*/
bool idRenderModelManagerLocal::ContainsModel( const idRenderModel *model ) const {
	if ( model == NULL ) {
		return false;
	}

	return models.FindIndex( const_cast<idRenderModel *>( model ) ) >= 0;
}

/*
=================
idRenderModelManagerLocal::DefaultModel
=================
*/
idRenderModel *idRenderModelManagerLocal::DefaultModel() {
	return defaultModel;
}

/*
=================
idRenderModelManagerLocal::AddModel
=================
*/
void idRenderModelManagerLocal::AddModel( idRenderModel *model ) {
	hash.Add( hash.GenerateKey( model->Name(), false ), models.Append( model ) );
}

/*
=================
idRenderModelManagerLocal::RemoveModel
=================
*/
void idRenderModelManagerLocal::RemoveModel( idRenderModel *model ) {
	int index = models.FindIndex( model );
	hash.RemoveIndex( hash.GenerateKey( model->Name(), false ), index );
	models.RemoveIndex( index );
}

/*
=================
idRenderModelManagerLocal::ReloadModels
=================
*/
void idRenderModelManagerLocal::ReloadModels( bool forceAll ) {
	if ( forceAll ) {
		common->Printf( "Reloading all model files...\n" );
	} else {
		common->Printf( "Checking for changed model files...\n" );
	}

	R_FreeDerivedData();

	// skip the default model at index 0
	for ( int i = 1 ; i < models.Num() ; i++ ) {
		idRenderModel	*model = models[i];

		// we may want to allow world model reloading in the future, but we don't now
		if ( !model->IsReloadable() ) {
			continue;
		}

		if ( !forceAll ) {
			// check timestamp
			ID_TIME_T current;

			fileSystem->ReadFile( model->Name(), NULL, &current );
			if ( current <= model->Timestamp() ) {
				continue;
			}
		}

		common->DPrintf( "reloading %s.\n", model->Name() );

		model->LoadModel();
	}

	// we must force the world to regenerate, because models may
	// have changed size, making their references invalid
	R_ReCreateWorldReferences();
}

/*
=================
idRenderModelManagerLocal::FreeModelVertexCaches
=================
*/
void idRenderModelManagerLocal::FreeModelVertexCaches() {
	for ( int i = 0 ; i < models.Num() ; i++ ) {
		idRenderModel *model = models[i];
		model->FreeVertexCache();
	}
}

/*
=================
idRenderModelManagerLocal::BeginLevelLoad
=================
*/
void idRenderModelManagerLocal::BeginLevelLoad() {
	insideLevelLoad = true;

	for ( int i = 0 ; i < models.Num() ; i++ ) {
		idRenderModel *model = models[i];

		if ( com_purgeAll.GetBool() && model->IsReloadable() ) {
			R_CheckForEntityDefsUsingModel( model );
			model->PurgeModel();
		}

		model->SetLevelLoadReferenced( false );
	}

	// purge unused triangle surface memory
	R_PurgeTriSurfData( frameData );
}

/*
=================
idRenderModelManagerLocal::EndLevelLoad
=================
*/
void idRenderModelManagerLocal::EndLevelLoad() {
	common->Printf( "----- idRenderModelManagerLocal::EndLevelLoad -----\n" );

	int start = Sys_Milliseconds();

	insideLevelLoad = false;
	int	purgeCount = 0;
	int	keepCount = 0;
	int	loadCount = 0;

	// purge any models not touched
	for ( int i = 0 ; i < models.Num() ; i++ ) {
		idRenderModel *model = models[i];

		if ( !model->IsLevelLoadReferenced() && model->IsLoaded() && model->IsReloadable() ) {

//			common->Printf( "purging %s\n", model->Name() );

			purgeCount++;

			R_CheckForEntityDefsUsingModel( model );

			model->PurgeModel();

		} else {

//			common->Printf( "keeping %s\n", model->Name() );

			keepCount++;
		}
	}

	// purge unused triangle surface memory
	R_PurgeTriSurfData( frameData );

	// load any new ones
	for ( int i = 0 ; i < models.Num() ; i++ ) {
		idRenderModel *model = models[i];

		if ( model->IsLevelLoadReferenced() && !model->IsLoaded() && model->IsReloadable() ) {

			loadCount++;
			model->LoadModel();
			session->AdvanceLoadingAssetQueue( 1 );

			if ( ( loadCount & 15 ) == 0 ) {
				session->PacifierUpdate();
			}
		}
	}

	// _D3XP added this
	int	end = Sys_Milliseconds();
	common->Printf( "%5i models purged from previous level, ", purgeCount );
	common->Printf( "%5i models kept.\n", keepCount );
	if ( loadCount ) {
		common->Printf( "%5i new models loaded in %5.1f seconds\n", loadCount, (end-start) * 0.001 );
	}
	common->Printf( "---------------------------------------------------\n" );
}

/*
=================
idRenderModelManagerLocal::CountPendingLevelLoads
=================
*/
int idRenderModelManagerLocal::CountPendingLevelLoads() const {
	int pendingCount = 0;

	for ( int i = 0; i < models.Num(); i++ ) {
		idRenderModel *model = models[ i ];
		if ( model->IsLevelLoadReferenced() && !model->IsLoaded() && model->IsReloadable() ) {
			pendingCount++;
		}
	}

	return pendingCount;
}

/*
=================
idRenderModelManagerLocal::PrintMemInfo
=================
*/
void idRenderModelManagerLocal::PrintMemInfo( MemInfo_t *mi ) {
	int i, j, totalMem = 0;
	int *sortIndex;
	idFile *f;

	f = fileSystem->OpenFileWrite( mi->filebase + "_models.txt" );
	if ( !f ) {
		return;
	}

	// sort first
	sortIndex = new int[ localModelManager.models.Num()];

	for ( i = 0; i <  localModelManager.models.Num(); i++ ) {
		sortIndex[i] = i;
	}

	for ( i = 0; i <  localModelManager.models.Num() - 1; i++ ) {
		for ( j = i + 1; j <  localModelManager.models.Num(); j++ ) {
			if (  localModelManager.models[sortIndex[i]]->Memory() <  localModelManager.models[sortIndex[j]]->Memory() ) {
				int temp = sortIndex[i];
				sortIndex[i] = sortIndex[j];
				sortIndex[j] = temp;
			}
		}
	}

	// print next
	for ( int i = 0 ; i < localModelManager.models.Num() ; i++ ) {
		idRenderModel	*model = localModelManager.models[sortIndex[i]];
		int mem;

		if ( !model->IsLoaded() ) {
			continue;
		}

		mem = model->Memory();
		totalMem += mem;
		f->Printf( "%s %s\n", idStr::FormatNumber( mem ).c_str(), model->Name() );
	}

	delete sortIndex;
	mi->modelAssetsTotal = totalMem;

	f->Printf( "\nTotal model bytes allocated: %s\n", idStr::FormatNumber( totalMem ).c_str() );
	fileSystem->CloseFile( f );
}
