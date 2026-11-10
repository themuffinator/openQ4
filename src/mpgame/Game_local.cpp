#include "../idlib/precompiled.h"
#pragma hdrstop

#include "Game_local.h"

#include "../idlib/NumericString.h"
#include "../sys/AutoVersion.h"

struct game_name_s {
	game_name_s( void ) {
		sprintf( string, "Q4MP %s", VERSION_STRING_DOTTED );
	}
	char string[256];
} game_name;

idCVar gamename( "gamename", game_name.string, CVAR_GAME | CVAR_SERVERINFO | CVAR_ROM, "" );

// RAVEN BEGIN
#include "../bse/BSEInterface.h"
#include "Projectile.h"
#include "client/ClientEffect.h"
#include "ai/AI.h"
#include "ai/AI_Manager.h"
#include "ai/AAS_tactical.h"
#include "Game_Log.h"
// RAVEN END

//#define UI_DEBUG	1

#ifdef GAME_DLL

idSys *						sys = NULL;
idCommon *					common = NULL;
idCmdSystem *				cmdSystem = NULL;
idCVarSystem *				cvarSystem = NULL;
idFileSystem *				fileSystem = NULL;
idNetworkSystem *			networkSystem = NULL;
idRenderSystem *			renderSystem = NULL;
idSoundSystem *				soundSystem = NULL;
idRenderModelManager *		renderModelManager = NULL;
idUserInterfaceManager *	uiManager = NULL;
idDeclManager *				declManager = NULL;
idAASFileManager *			AASFileManager = NULL;
idCollisionModelManager *	collisionModelManager = NULL;

// RAVEN BEGIN
// jscott: game interface to the fx system
rvBSEManager *				bse = NULL;
// RAVEN END
idVRSystem *				vrSystem = NULL;
idUsercmdGen *				usercmdGen = NULL;

idCVar *					idCVar::staticVars = NULL;

// RAVEN BEGIN
// rjohnson: new help system for cvar ui
//idCVarHelp *				idCVarHelp::staticCVarHelps = NULL;
//idCVarHelp *				idCVarHelp::staticCVarHelpsTail = NULL;
// RAVEN END

idCVar com_forceGenericSIMD( "com_forceGenericSIMD", "0", CVAR_BOOL|CVAR_SYSTEM, "force generic platform independent SIMD" );

int Sys_Milliseconds( void ) {
	return sys ? sys->Milliseconds() : 0;
}

#endif

idRenderWorld *				gameRenderWorld = NULL;		// all drawing is done to this world

static gameExport_t			gameExport;

static bool rvProjectedLightFloatValid( const float value ) {
	return !FLOAT_IS_NAN( value ) && !FLOAT_IS_INF( value );
}

static bool rvProjectedLightVecFinite( const idVec3 &value ) {
	return rvProjectedLightFloatValid( value[0] ) && rvProjectedLightFloatValid( value[1] ) && rvProjectedLightFloatValid( value[2] );
}

static bool rvProjectedLightVecUsable( const idVec3 &value ) {
	static const float VECTOR_EPSILON_SQR = 1.0e-6f;
	return rvProjectedLightVecFinite( value ) && value.LengthSqr() > VECTOR_EPSILON_SQR;
}

void rvNormalizeProjectedRenderLight( renderLight_t &light, const char *ownerName, const char *lightName ) {
	if ( light.pointLight ) {
		return;
	}

	const bool targetValid = rvProjectedLightVecUsable( light.target );
	const bool rightValid = rvProjectedLightVecUsable( light.right );
	const bool upValid = rvProjectedLightVecUsable( light.up );

	if ( targetValid ) {
		if ( !rvProjectedLightVecFinite( light.start ) ) {
			light.start.Zero();
		}
		if ( !rvProjectedLightVecUsable( light.end ) || !rvProjectedLightVecUsable( light.end - light.start ) ) {
			light.end = light.target;
		}
	}

	const bool depthValid = rvProjectedLightVecFinite( light.start ) && rvProjectedLightVecUsable( light.end - light.start );
	const bool basisValid = rightValid && upValid && rvProjectedLightVecUsable( light.up.Cross( light.right ) );

	if ( targetValid && basisValid && depthValid ) {
		return;
	}

	gameLocal.Warning(
		"Projected render light '%s' on '%s' has invalid projection vectors target=%s right=%s up=%s start=%s end=%s; demoting to point light",
		( lightName && lightName[0] ) ? lightName : "<unnamed>",
		( ownerName && ownerName[0] ) ? ownerName : "<unknown>",
		light.target.ToString( 2 ),
		light.right.ToString( 2 ),
		light.up.ToString( 2 ),
		light.start.ToString( 2 ),
		light.end.ToString( 2 )
	);

	light.pointLight = true;
	light.target.Zero();
	light.right.Zero();
	light.up.Zero();
	light.start.Zero();
	light.end.Zero();
}

// global animation lib
// RAVEN BEGIN
// jsinger: changed to a pointer to prevent its constructor from allocating
//          memory before the unified allocator could be initialized
idAnimManager				*animationLib = NULL;
// RAVEN END

// the rest of the engine will only reference the "game" variable, while all local aspects stay hidden
idGameLocal					gameLocal;
idGame *					game = &gameLocal;	// statically pointed at an idGameLocal

const char *idGameLocal::sufaceTypeNames[ MAX_SURFACE_TYPES ] = {
	"none",	"metal", "stone", "flesh", "wood", "cardboard", "liquid", "glass", "plastic",
	"ricochet", "surftype10", "surftype11", "surftype12", "surftype13", "surftype14", "surftype15"
};

/*
===========
GetGameAPI
============
*/

#if __GNUC__ >= 4
#pragma GCC visibility push(default)
#endif
extern "C" gameExport_t *GetGameAPI( gameImport_t *import ) {

	if ( import->version == GAME_API_VERSION ) {

		// set interface pointers used by the game
		sys							= import->sys;
		common						= import->common;
		cmdSystem					= import->cmdSystem;
		cvarSystem					= import->cvarSystem;
		fileSystem					= import->fileSystem;
		networkSystem				= import->networkSystem;
		renderSystem				= import->renderSystem;
		soundSystem					= import->soundSystem;
		renderModelManager			= import->renderModelManager;
		uiManager					= import->uiManager;
		declManager					= import->declManager;
		AASFileManager				= import->AASFileManager;
		collisionModelManager		= import->collisionModelManager;
// RAVEN BEGIN
// jscott: import the fx system
		bse							= import->bse;
// RAVEN END
		vrSystem					= import->vrSystem;
		usercmdGen					= import->usercmdGen;

// RAVEN BEGIN
// dluetscher: import the memory system variables
#ifdef _RV_MEM_SYS_SUPPORT
		::currentHeapArena			= import->heapArena;
		rvSetAllSysHeaps( import->systemHeapArray );
#endif
// RAVEN END
	}

	// set interface pointers used by idLib
	idLib::sys					= sys;
	idLib::common				= common;
	idLib::cvarSystem			= cvarSystem;
	idLib::fileSystem			= fileSystem;

	// setup export interface
	gameExport.version = GAME_API_VERSION;
	gameExport.game = game;
	gameExport.gameEdit = gameEdit;
// RAVEN BEGIN
// bdube: game log
	gameExport.gameLog = gameLog;
// RAVEN END	

	return &gameExport;
}
#if __GNUC__ >= 4
#pragma GCC visibility pop
#endif

/*
===========
TestGameAPI
============
*/
void TestGameAPI( void ) {
	gameImport_t testImport;
	gameExport_t testExport;

	testImport.sys						= ::sys;
	testImport.common					= ::common;
	testImport.cmdSystem				= ::cmdSystem;
	testImport.cvarSystem				= ::cvarSystem;
	testImport.fileSystem				= ::fileSystem;
	testImport.networkSystem			= ::networkSystem;
	testImport.renderSystem				= ::renderSystem;
	testImport.soundSystem				= ::soundSystem;
	testImport.renderModelManager		= ::renderModelManager;
	testImport.uiManager				= ::uiManager;
	testImport.declManager				= ::declManager;
	testImport.AASFileManager			= ::AASFileManager;
	testImport.collisionModelManager	= ::collisionModelManager;

// RAVEN BEGIN
// jscott: import the fx system
	testImport.bse						= ::bse;
// RAVEN END

	testExport = *GetGameAPI( &testImport );
}

/*
================
idGameLocal::BuildModList
================
*/
void idGameLocal::BuildModList( ) {
	int i;
	idStr currentMod;

	int numServers = networkSystem->GetNumScannedServers();

	if ( filterMod >= 0 && filterMod < modList.Num() ) {
		currentMod = modList[ filterMod ];
	} else {
		currentMod = "";
	}

	modList.Clear();
	for (i = 0; i < numServers; i++) {
		const scannedServer_t *server;
		idStr modname;

		server = networkSystem->GetScannedServerInfo( i );

		server->serverInfo.GetString( "fs_game", "", modname );
		modname.ToLower();
		modList.AddUnique( modname );
	}

	modList.Sort();

	if ( modList.Num() > 0 && (modList[ 0 ].Cmp( "" ) == 0) ) {
		modList.RemoveIndex( 0 );
	}

	filterMod = modList.Num();
	for (i = 0; i < modList.Num(); i++) {
		if ( modList[ i ].Icmp( currentMod ) == 0 ) {
			filterMod = i;
		}
	}
}

/*
================
FilterByMod
================
*/
static int FilterByMod( const int* serverIndex ) {
	const scannedServer_t *server;

	if ( gameLocal.filterMod < 0 || gameLocal.filterMod >= gameLocal.modList.Num() ) {
		return (int)false;
	}

	server = networkSystem->GetScannedServerInfo( *serverIndex );
	const idStr& modFilter = gameLocal.modList[ gameLocal.filterMod ];
	return static_cast<int>( modFilter.Icmp( server->serverInfo.GetString( "fs_game" ) ) != 0 );
}

static sortInfo_t filterByMod = {
	SC_ALL,
	NULL,
	FilterByMod,
	"#str_123006"
};

/*
===========
idGameLocal::idGameLocal
============
*/
idGameLocal::idGameLocal() :
	mapMediaPrecacheSeen( 4096 ),
	moduleIdLibInitialized( false ),
	moduleShutdownStarted( false ),
	moduleShutdownFinalized( false ),
	moduleEventInitStarted( false ),
	moduleClassInitStarted( false ),
	moduleProgramInitStarted( false ) {
	Clear();
}

/*
===========
idGameLocal::Clear
============
*/
void idGameLocal::Clear( void ) {
	int i;

	serverInfo.Clear();
	repeaterInfo.Clear();
	numClients = 0;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		userInfo[i].Clear();
		persistentPlayerInfo[i].Clear();
	}
	usercmds = NULL;
	memset( entities, 0, sizeof( entities ) );
	memset( spawnIds, -1, sizeof( spawnIds ) );
	firstFreeIndex = 0;
	num_entities = 0;
	spawnedEntities.Clear();
	activeEntities.Clear();
	presentationEntities.Clear();
	presentationClientEntities.Clear();
	numEntitiesToDeactivate = 0;
	sortPushers = false;
	sortTeamMasters = false;
	persistentLevelInfo.Clear();
	memset( globalShaderParms, 0, sizeof( globalShaderParms ) );
	random.SetSeed( 0 );
	world = NULL;
	frameCommandThread = NULL;
	testmodel = NULL;
	ShutdownInstances();
	clip.Clear();
	
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		clientsPVS[ i ].i = -1;
		clientsPVS[ i ].h = -1;
	}
	freePlayerPVS = false;
	pvs.Shutdown();
	sessionCommand.Clear();
	locationEntities = NULL;
	editEntities = NULL;
	entityHash.Clear( 1024, MAX_GENTITIES );
	inCinematic = false;
	cinematicSkipTime = 0;
	cinematicStopTime = 0;
	cinematicMaxSkipTime = 0;
	framenum = 0;
	previousTime = 0;
	time = 0;
	presentationClock.Reset();
	presentationGameplayCount = 0;
	presentationRendererDepth = 0;
	presentationRendererEffectCount = 0;
	presentationPrepStartTicks = 0.0;
	presentationClockSample = -1.0f;
	presentationSceneFraction = -1.0f;
	presentationAnimationTime = -1;
	autoExecAfterMapLoadStartTime = 0;
	autoExecAfterMapLoadPending = false;
	autoExecAfterMapLoadWaitingLogged = false;
	vacuumAreaNum = 0;

// RAVEN BEGIN
// abahr
	gravityInfo.Clear();
	scriptObjectProxies.Clear();
// RAVEN END

	mapFileName.Clear();
// RAVEN BEGIN
// rjohnson: entity usage stats
	mapFileNameStripped.Clear();
// RAVEN END
	mapFile = NULL;
	spawnCount = INITIAL_SPAWN_COUNT;
	memset( isMapEntity, 0, sizeof( bool ) * MAX_GENTITIES );

	camera = NULL;

// RAVEN BEGIN
// jscott: for portal skies
	portalSky = NULL;
// RAVEN END

	aasList.Clear();
	aasNames.Clear();
// RAVEN BEGIN
// bdube: added
	lastAIAlertEntity = NULL;
	lastAIAlertEntityTime = 0;
	lastAIAlertActor = NULL;
	lastAIAlertActorTime = 0;
// RAVEN END
	spawnArgs.Clear();
	gravity.Set( 0, 0, -1 );
	playerPVS.i = -1;
	playerPVS.h = -1;
	playerConnectedAreas.i = -1;
	playerConnectedAreas.h = -1;
	mapMediaPrecacheActive = false;
	mapMediaPrecacheSeen.Clear();
	mapMediaPrecacheRequests = 0;
	mapMediaPrecacheSkips = 0;
	gamestate = GAMESTATE_UNINITIALIZED;
	skipCinematic = false;
	influenceActive = false;

	localClientNum = 0;
	isMultiplayer = false;
	isServer = false;
	isClient = false;
	isTVClient = false;
	realClientTime = 0;
	isNewFrame = true;
	clientReplayTics = 0;
	lastChainReplayTics = 0;
	entityDefBits = 0;

	nextGibTime = 0;
// RITUAL BEGIN
// squirrel: added DeadZone multiplayer mode
	unFreezeTime = 0;
	isFrozen = false;
// RITUAL END
	globalMaterial = NULL;
	newInfo.Clear();
	lastGUIEnt = NULL;
	lastGUI = 0;
	
	memset( clientEntityStates, 0, sizeof( clientEntityStates ) );
	memset( clientPVS, 0, sizeof( clientPVS ) );
	memset( clientSnapshots, 0, sizeof( clientSnapshots ) );

	ReallocViewers( 0 );

	maxViewers = 0;
	maxViewer = 0;
	viewers = NULL;
	viewerEntityStates = NULL;
	viewerPVS = NULL;
	viewerSnapshots = NULL;
	viewerUnreliableMessages = NULL;

	eventQueue.Init();

// RAVEN BEGIN
// bdube: client entities
	clientSpawnCount = INITIAL_SPAWN_COUNT;
	clientSpawnedEntities.Clear();
	memset( clientEntities, 0, sizeof( clientEntities ) );
	memset( clientSpawnIds, -1, sizeof( clientSpawnIds ) );

	gameDebug.Shutdown ( );
	gameLogLocal.Shutdown ( );
	currentThinkingEntity = NULL;
// RAVEN END

	ResetMPLagCompensationHistory();

	demoState = DEMO_NONE;
	serverDemo = false;
	timeDemo = false;

	memset( &usercmd, 0, sizeof( usercmd ) );
	memset( &oldUsercmd, 0, sizeof( oldUsercmd ) );
	followPlayer = -1;	// start free flying
	serverDemoInstances = false;

	demo_protocol = 0;

	instancesEntityIndexWatermarks.Clear();
	clientInstanceFirstFreeIndex = MAX_CLIENTS;
	minSpawnIndex = MAX_CLIENTS;

	modList.Clear();
	filterMod = -1;
	if ( networkSystem ) {
		networkSystem->UseSortFunction( filterByMod, false );
	}
	clientAckSequence = -1;
}

/*
===========
idGameLocal::Init

  initialize the game object, only happens once at startup, not each level load
============
*/
// RAVEN BEGIN
// jsinger: attempt to eliminate cross-DLL allocation issues
extern idHashTable<rvViseme> *visemeTable100;
extern idHashTable<rvViseme> *visemeTable66;
extern idHashTable<rvViseme> *visemeTable33;
#ifdef RV_UNIFIED_ALLOCATOR
void idGameLocal::Init( void *(*allocator)(size_t size), void (*deallocator)( void *ptr ), size_t (*msize)(void *ptr) ) {
#else
void idGameLocal::Init( void ) {
#endif
// RAVEN END
	const idDict *dict;
	idAAS *aas;

	// A loaded game object gets one early service-aware shutdown and one late
	// module-lifetime finalization.  Reset the one-shot guards before any part
	// of initialization can report an error.
	moduleShutdownStarted = false;
	moduleShutdownFinalized = false;
	moduleEventInitStarted = false;
	moduleClassInitStarted = false;
	moduleProgramInitStarted = false;
	assert( !moduleIdLibInitialized );

#ifndef GAME_DLL

	TestGameAPI();

#else

	mHz = common->GetUserCmdHz();
	msec = common->GetUserCmdMSec();

// RAVEN BEGIN
// jsinger: attempt to eliminate cross-DLL allocation issues
#ifdef RV_UNIFIED_ALLOCATOR
	Memory::InitAllocator(allocator, deallocator, msize);
#endif
// RAVEN END
	// initialize idLib
	idLib::Init();
	moduleIdLibInitialized = true;

	// register static cvars declared in the game
	idCVar::RegisterStaticVars();

	// initialize processor specific SIMD
	idSIMD::InitProcessor( "game", com_forceGenericSIMD.GetBool() );

#endif
// RAVEN BEGIN
// jsinger: these need to be initialized after the InitAllocator call above in order
//          to avoid crashes when the allocator is used.
	animationLib = new idAnimManager();
	visemeTable100 = new idHashTable<rvViseme>;
	visemeTable66 = new idHashTable<rvViseme>;
	visemeTable33 = new idHashTable<rvViseme>;
// RAVEN END

	Printf( "------------- Initializing Game -------------\n" );
	Printf( "gamename: %s\n", game_name.string );
	Printf( "gamedate: %s\n", __DATE__ );

	// openQ4: fail loudly if the gametype table, the enum and the si_gameType
	// completion list have drifted apart
	MPValidateGameTypeTable();

	// openQ4: the first user info a connection sends must already say what this client
	// extrapolates, before ClientRun keeps it current.
	UpdateAdvertisedRemoteExtrapolation();

// RAVEN BEGIN
// rjohnson: new help system for cvar ui
	//idCVarHelp::RegisterStatics();

// jsinger: added to support serialization/deserialization of binary decls
#ifdef RV_BINARYDECLS
	idStr prefix="";
	if(cvarSystem->GetCVarBool("com_binaryDeclRead"))
	{
		prefix = "binary/";
	}
	declManager->RegisterDeclType( "model",				DECL_MODELDEF,		idDeclAllocator<idDeclModelDef>, idDeclStreamAllocator<idDeclModelDef> );
	//declManager->RegisterDeclType( "export",			DECL_MODELEXPORT,	idDeclAllocator<idDecl>, idDeclStreamAllocator<idDecl> );
	declManager->RegisterDeclType( "camera",			DECL_CAMERADEF,		idDeclAllocator<idDeclCameraDef>, idDeclStreamAllocator<idDeclCameraDef> );
	declManager->RegisterDeclFolderWrapper( prefix + "def",			".def",			DECL_ENTITYDEF );
	declManager->RegisterDeclFolderWrapper( prefix + "af",			".af",			DECL_AF );
#else
// RAVEN END
	// register game specific decl types
	declManager->RegisterDeclType( "model",				DECL_MODELDEF,		idDeclAllocator<idDeclModelDef> );
	declManager->RegisterDeclType( "export",			DECL_MODELEXPORT,	idDeclAllocator<idDecl> );

// RAVEN BEGIN
// rjohnson: camera is now contained in a def for frame commands
	declManager->RegisterDeclType( "camera",			DECL_CAMERADEF,		idDeclAllocator<idDeclCameraDef> );
// RAVEN END
	// register game specific decl folders
// RAVEN BEGIN
#ifndef RV_SINGLE_DECL_FILE
	declManager->RegisterDeclFolderWrapper( "def",			".def",			DECL_ENTITYDEF );
// bdube: not used in quake 4
//	declManager->RegisterDeclFolder( "fx",					".fx",			DECL_FX );
//	declManager->RegisterDeclFolder( "particles",			".prt",			DECL_PARTICLE );
	declManager->RegisterDeclFolderWrapper( "af",			".af",			DECL_AF );
//	declManager->RegisterDeclFolderWrapper( "newpdas",		".pda",			DECL_PDA );
#else
	if(!cvarSystem->GetCVarBool("com_SingleDeclFile"))
	{
		declManager->RegisterDeclFolderWrapper( "def",			".def",			DECL_ENTITYDEF );
		declManager->RegisterDeclFolderWrapper( "af",			".af",			DECL_AF );
	}
	else
	{
		// loads the second set of decls from the file which will contain
		// modles, cameras
		declManager->LoadDeclsFromFile();
		declManager->FinishLoadingDecls();
	}
#endif
#endif  // RV_BINARYDECLS
// RAVEN END

	cmdSystem->AddCommand( "listModelDefs", idListDecls_f<DECL_MODELDEF>, CMD_FL_SYSTEM|CMD_FL_GAME, "lists model defs" );
	cmdSystem->AddCommand( "printModelDefs", idPrintDecls_f<DECL_MODELDEF>, CMD_FL_SYSTEM|CMD_FL_GAME, "prints a model def", idCmdSystem::ArgCompletion_Decl<DECL_MODELDEF> );

	Clear();

	moduleEventInitStarted = true;
	idEvent::Init();
// RAVEN BEGIN
// jnewquist: Register subclasses explicitly so they aren't dead-stripped
	idClass::RegisterClasses();
// RAVEN END
	moduleClassInitStarted = true;
	idClass::Init();

	InitConsoleCommands();
	// load default scripts
	moduleProgramInitStarted = true;
	program.Startup( SCRIPT_DEFAULT );
	
	// set up the aas
// RAVEN BEGIN
// ddynerman: added false as 2nd parameter, otherwise def will be created
	dict = FindEntityDefDict( "aas_types", false );
// RAVEN END
	if ( !dict ) {
		if ( !g_allowAssetlessStartup.GetBool() ) {
			Error( "Unable to find entityDef for 'aas_types'" );
		}
		Printf( "aas_types decl not found; continuing without AAS because g_allowAssetlessStartup is enabled.\n" );
	}

	// allocate space for the aas
	const idKeyValue *kv = dict ? dict->MatchPrefix( "type" ) : NULL;
	while( kv != NULL ) {
// RAVEN BEGIN
// jnewquist: Tag scope and callees to track allocations using "new".
		MEM_SCOPED_TAG(tag,MA_AAS);
// RAVEN END
		aas = idAAS::Alloc();
		aasList.Append( aas );
		aasNames.Append( kv->GetValue() );
		kv = dict->MatchPrefix( "type", kv );
	}

	gamestate = GAMESTATE_NOMAP;

	Printf( "...%d aas types\n", aasList.Num() );
	Printf( "game initialized.\n" );
	Printf( "---------------------------------------------\n" );
	
// RAVEN BEGIN
// bdube: debug stuff
	gameDebug.Init();
	gameLogLocal.Init();

// jscott: facial animation init
	if( !FAS_Init( "annosoft" ) ) {
		common->Warning( "Failed to load viseme file" );
	}

	// Bot personalities are read once here rather than per map: a character
	// describes an opponent, and opponents outlive a map change because bots
	// hold their client slots across one.
	botCharacterManager.Init();
	botManager.Init();

// jnewquist: Tag scope and callees to track allocations using "new".
	MEM_SCOPED_TAG(tag,MA_RENDER);
// shouchard:  make sure ban list starts out in a known state
	banListLoaded = false;
	banListChanged = false;
	memset( clientGuids, 0, sizeof( clientGuids ) );
// RAVEN END

// RAVEN BEGIN
// mwhitlock: Dynamic memory consolidation
#if defined(_RV_MEM_SYS_SUPPORT)
	for(int i=0;i<MAX_CLIENTS;i++)
	{
		persistentPlayerInfo[i].SetAllocatorHeap(rvGetSysHeap(RV_HEAP_ID_PERMANENT));
	}
	entityHash.SetAllocatorHeap(rvGetSysHeap(RV_HEAP_ID_LEVEL));
	gravityInfo.SetAllocatorHeap(rvGetSysHeap(RV_HEAP_ID_LEVEL));
	scriptObjectProxies.SetAllocatorHeap(rvGetSysHeap(RV_HEAP_ID_LEVEL));
	entityUsageList.SetAllocatorHeap(rvGetSysHeap(RV_HEAP_ID_LEVEL));
	ambientLights.SetAllocatorHeap(rvGetSysHeap(RV_HEAP_ID_LEVEL));
	instances.SetAllocatorHeap(rvGetSysHeap(RV_HEAP_ID_LEVEL));
	clip.SetAllocatorHeap(rvGetSysHeap(RV_HEAP_ID_LEVEL));
#endif
// RAVEN END

	networkSystem->AddSortFunction( filterByMod );

	incompatibleMaps = 0;
}

/*
===========
idGameLocal::Shutdown

  shut down the entire game
============
*/
void idGameLocal::Shutdown( void ) {
	idSaveGame::ClearDictionaryCache();

	int		i;
	if ( moduleShutdownStarted ) {
		return;
	}
	moduleShutdownStarted = true;

	if ( !common ) {
		return;
	}

	// Fatal startup failures can arrive before the runtime is complete.  Release
	// only subsystems whose initialization began; the late phase still removes
	// callbacks and releases animation/idLib after declarations are gone.
	if ( gamestate == GAMESTATE_UNINITIALIZED ) {
		aasList.DeleteContents( true );
		aasNames.Clear();
		if ( moduleEventInitStarted ) {
			idEvent::Shutdown();
			moduleEventInitStarted = false;
		}
		if ( moduleProgramInitStarted ) {
			program.Shutdown();
			moduleProgramInitStarted = false;
		}
		if ( moduleClassInitStarted ) {
			idClass::Shutdown();
			moduleClassInitStarted = false;
		}
		return;
	}

// RAVEN BEGIN
// jscott: FAS
	FAS_Shutdown();

	// Before the character manager, not after: every live bot is holding a
	// character pointer, and rvBotManager::Shutdown is what hands those back.
	// Freeing the roster first would leave each bot pointing at released memory.
	botManager.Shutdown();
	botCharacterManager.Shutdown();

// shouchard:  clean up ban list stuff
	SaveBanList();
	FlushBanList();
// RAVEN END

	Printf( "--------------- Game Shutdown ---------------\n" );

	networkSystem->RemoveSortFunction( filterByMod );

	mpGame.Shutdown();

	MapShutdown();

	aasList.DeleteContents( true );
	aasNames.Clear();

	idAI::FreeObstacleAvoidanceNodes();

	// shutdown the model exporter
	idModelExport::Shutdown();

	if ( moduleEventInitStarted ) {
		idEvent::Shutdown();
		moduleEventInitStarted = false;
	}

	if ( moduleProgramInitStarted ) {
		program.Shutdown();
		moduleProgramInitStarted = false;
	}

	if ( moduleClassInitStarted ) {
		idClass::Shutdown();
		moduleClassInitStarted = false;
	}

	// clear list with forces
	idForce::ClearForceList();

	// free the program data
	program.FreeData();

	// delete the .map file
	delete mapFile;
	mapFile = NULL;

// RAVEN BEGIN
// jscott: free up static objects
	for( i = 0; i < MAX_CLIENTS; i++ ) {
		userInfo[i].Clear();
		persistentPlayerInfo[i].Clear();
	}

	for( i = 0; i < entityUsageList.Num(); i++ ) {
		entityUsageList[i].Clear();
	}

	serverInfo.Clear();
	repeaterInfo.Clear();
	persistentLevelInfo.Clear();
	sessionCommand.Clear();
	mapFileName.Clear();
	mapFileNameStripped.Clear();
	entityUsageList.Clear();

	newInfo.Clear();
	spawnArgs.Clear();
	shakeSounds.Clear();
	aiManager.Clear();
// RAVEN END

	// free memory allocated by class objects
	Clear();

	// animationLib remains live until ShutdownAfterDecls() because model decl
	// idAnim destructors still decrement idMD5Anim references.

// RAVEN BEGIN
// rjohnson: entity usage stats
	entityUsageList.Clear();
// RAVEN END

	Printf( "---------------------------------------------\n" );

	instances.DeleteContents( true );
}

/*
===========
idGameLocal::ShutdownAfterDecls

  release module-owned support state after engine decl destruction
============
*/
void idGameLocal::ShutdownAfterDecls( void ) {
	if ( moduleShutdownFinalized ) {
		return;
	}
	moduleShutdownFinalized = true;

	// Remove every engine callback into this binary immediately before unload.
	// Keeping this in the late phase also covers a partially completed Init().
	if ( cmdSystem != NULL ) {
		ShutdownConsoleCommands();
	}
	if ( cvarSystem != NULL ) {
		cvarSystem->RemoveFlaggedAutoCompletion( CVAR_GAME );
	}

	if ( animationLib != NULL ) {
		animationLib->Shutdown();
		delete animationLib;
		animationLib = NULL;
	}

	delete visemeTable100;
	visemeTable100 = NULL;
	delete visemeTable66;
	visemeTable66 = NULL;
	delete visemeTable33;
	visemeTable33 = NULL;

#ifdef GAME_DLL
	if ( moduleIdLibInitialized ) {
		// All module-owned decl objects and animation references are gone now.
		Mem_EnableLeakTest( "game" );
		idLib::ShutDown();
		moduleIdLibInitialized = false;
	}
#endif
}

/*
===========
idGameLocal::SaveGame

save the current player state, level name, and level state
the session may have written some data to the file already
============
*/
// RAVEN BEGIN
// mekberg: added saveTypes
void idGameLocal::SaveGame( idFile *f, saveType_t saveType ) {
// RAVEN END
	int i;
	idEntity *ent;
	idEntity *link;

	//remove weapon effect entites from the world
	if(	GetLocalPlayer() && 
		!GetLocalPlayer()->IsInVehicle() &&	
		GetLocalPlayer()->weapon )	{
		
		GetLocalPlayer()->weapon->PreSave();
	}

	idSaveGame savegame( f );

	if (g_flushSave.GetBool( ) == true ) { 
		// force flushing with each write... for tracking down
		// save game bugs.
		f->ForceFlush();
	}

	savegame.WriteBuildNumber( BUILD_NUMBER );

	// go through all entities and threads and add them to the object list
	for( i = 0; i < MAX_GENTITIES; i++ ) {
		ent = entities[i];

		if ( ent ) {
			if ( ent->GetTeamMaster() && ent->GetTeamMaster() != ent ) {
				continue;
			}
			for ( link = ent; link != NULL; link = link->GetNextTeamEntity() ) {
				savegame.AddObject( link );
			}
		}
	}

	idList<idThread *> threads;
	threads = idThread::GetThreads();

	for( i = 0; i < threads.Num(); i++ ) {
		savegame.AddObject( threads[i] );
	}

// RAVEN BEGIN
// abahr: saving clientEntities
	rvClientEntity* clientEnt = NULL;
	for( i = 0; i < MAX_CENTITIES; ++i ) {
		clientEnt = clientEntities[ i ];
		if( !clientEnt ) {
			continue;
		}
//		if( clientEnt->IsType( rvClientEffect::GetClassType() )){
//			continue;
//		}
		savegame.AddObject( clientEnt );
	}
// RAVEN END

	// write out complete object list
	savegame.WriteObjectList();

	program.Save( &savegame );

	savegame.WriteInt( g_skill.GetInteger() );

	savegame.WriteDict( &serverInfo );

	savegame.WriteInt( numClients );
	for( i = 0; i < numClients; i++ ) {
// RAVEN BEGIN
// mekberg: don't write out userinfo. Grab from cvars
//		savegame.WriteDict( &userInfo[ i ] );
// RAVEN END
//		savegame.WriteUsercmd( usercmds[ i ] );
		savegame.WriteDict( &persistentPlayerInfo[ i ] );
	}

	for( i = 0; i < MAX_GENTITIES; i++ ) {
		savegame.WriteObject( entities[ i ] );
		savegame.WriteInt( spawnIds[ i ] );
	}

// RAVEN BEGIN
// abahr: more clientEntities saving
	for( i = 0; i < MAX_CENTITIES; i++ ) {
//		if( clientEntities[ i ] && clientEntities[ i ]->IsType( rvClientEffect::GetClassType() )){
//			continue;
//		}
		savegame.WriteObject( clientEntities[ i ] );
		savegame.WriteInt( clientSpawnIds[ i ] );
	}
// RAVEN END

	savegame.WriteInt( firstFreeIndex );
	savegame.WriteInt( num_entities );

	// enityHash is restored by idEntity::Restore setting the entity name.

	savegame.WriteObject( world );

	savegame.WriteInt( spawnedEntities.Num() );
	for( ent = spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
		savegame.WriteObject( ent );
	}

// RAVEN BEGIN
// abahr: saving scriptObject proxies
	savegame.WriteInt( scriptObjectProxies.Num() );
	for( i = 0; i < scriptObjectProxies.Num(); ++i ) {
		scriptObjectProxies[i].Save( &savegame );
	}
// abahr: save out client stuff
	savegame.WriteInt( clientSpawnedEntities.Num() );
	for( clientEnt = clientSpawnedEntities.Next(); clientEnt != NULL; clientEnt = clientEnt->spawnNode.Next() ) {
		savegame.WriteObject( clientEnt );
	}
// RAVEN END

	savegame.WriteInt( activeEntities.Num() );
	for( ent = activeEntities.Next(); ent != NULL; ent = ent->activeNode.Next() ) {
		savegame.WriteObject( ent );
	}

	savegame.WriteInt( numEntitiesToDeactivate );
	savegame.WriteBool( sortPushers );
	savegame.WriteBool( sortTeamMasters );
	savegame.WriteDict( &persistentLevelInfo );

	for( i = 0; i < MAX_GLOBAL_SHADER_PARMS; i++ ) {
		savegame.WriteFloat( globalShaderParms[ i ] );
	}

	savegame.WriteInt( random.GetSeed() );
	savegame.WriteObject( frameCommandThread );

	// clip
	// push
	// pvs

	testmodel = NULL;
// RAVEN BEGIN
// bdube: no test fx
//	testFx = NULL;
// RAVEN END

	savegame.WriteString( sessionCommand );
// RAVEN BEGIN
// nmckenzie: Let the AI system save itself too.
	aiManager.Save( &savegame );
// RAVEN END

	// FIXME: save smoke particles

	savegame.WriteInt( cinematicSkipTime );
	savegame.WriteInt( cinematicStopTime );
	savegame.WriteInt( cinematicMaxSkipTime );
	savegame.WriteBool( inCinematic );
	savegame.WriteBool( skipCinematic );

	savegame.WriteBool( isMultiplayer );
	savegame.WriteInt( gameType );

	savegame.WriteInt( framenum );
	savegame.WriteInt( previousTime );
	savegame.WriteInt( time );

	savegame.WriteInt( vacuumAreaNum );

	savegame.WriteInt( entityDefBits );
	savegame.WriteBool( isServer );
	savegame.WriteBool( isClient );
// RAVEN BEGIN
	savegame.WriteBool( isListenServer );
// RAVEN END

	savegame.WriteInt( localClientNum );

	// snapshotEntities is used for multiplayer only

	savegame.WriteInt( realClientTime );
	savegame.WriteBool( isNewFrame );

	savegame.WriteBool( mapCycleLoaded );
	savegame.WriteInt( spawnCount );

	if ( !locationEntities ) {
		savegame.WriteInt( 0 );
	} else {
		savegame.WriteInt( gameRenderWorld->NumAreas() );
		for( i = 0; i < gameRenderWorld->NumAreas(); i++ ) {
			savegame.WriteObject( locationEntities[ i ] );
		}
	}

	savegame.WriteObject( camera );

	savegame.WriteMaterial( globalMaterial );

// RAVEN BEGIN
// bdube: added
	lastAIAlertActor.Save( &savegame );
	lastAIAlertEntity.Save( &savegame );
	savegame.WriteInt( lastAIAlertEntityTime );
	savegame.WriteInt( lastAIAlertActorTime );
// RAVEN END

	savegame.WriteDict( &spawnArgs );

	savegame.WriteInt( playerPVS.i );
	savegame.WriteInt( playerPVS.h );
	savegame.WriteInt( playerConnectedAreas.i );
	savegame.WriteInt( playerConnectedAreas.h );

	savegame.WriteVec3( gravity );

	// gamestate

	savegame.WriteBool( influenceActive );
	savegame.WriteInt( nextGibTime );

	// spawnSpots
	// initialSpots
	// currentInitialSpot
	// newInfo
	// makingBuild
	// shakeSounds

	// write out pending events
	idEvent::Save( &savegame );

	savegame.Close();

// RAVEN BEGIN
// jshepard: resume weapon operation
	if(	GetLocalPlayer() && 
		!GetLocalPlayer()->IsInVehicle() &&	
		GetLocalPlayer()->weapon )	{
		GetLocalPlayer()->weapon->PostSave();
	}
// RAVEN END
}

void idGameLocal::NotifySaveGameComplete() {
	if ( GetLocalPlayer() && GetLocalPlayer()->GetHud() ) {
		GetLocalPlayer()->SaveMessage();
	}
}

/*
===========
idGameLocal::GetPersistentPlayerInfo
============
*/
const idDict &idGameLocal::GetPersistentPlayerInfo( int clientNum ) {
	idEntity	*ent;

	persistentPlayerInfo[ clientNum ].Clear();
	ent = entities[ clientNum ];
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
	if ( ent && ent->IsType( idPlayer::GetClassType() ) ) {
// RAVEN END
		static_cast<idPlayer *>(ent)->SavePersistantInfo();
	}

	return persistentPlayerInfo[ clientNum ];
}

/*
===========
idGameLocal::SetPersistentPlayerInfo
============
*/
void idGameLocal::SetPersistentPlayerInfo( int clientNum, const idDict &playerInfo ) {
	TIME_THIS_SCOPE( __FUNCLINE__);
	persistentPlayerInfo[ clientNum ] = playerInfo;
}

/*
============
idGameLocal::Printf
============
*/
void idGameLocal::Printf( const char *fmt, ... ) const {
	va_list		argptr;
	char		text[MAX_STRING_CHARS];

	va_start( argptr, fmt );
	idStr::vsnPrintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	common->Printf( "%s", text );
}

/*
============
idGameLocal::DPrintf
============
*/
void idGameLocal::DPrintf( const char *fmt, ... ) const {
	va_list		argptr;
	char		text[MAX_STRING_CHARS];

	if ( !developer.GetBool() ) {
		return;
	}

	va_start( argptr, fmt );
	idStr::vsnPrintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	common->Printf( "%s", text );
}

/*
============
idGameLocal::Warning
============
*/
void idGameLocal::Warning( const char *fmt, ... ) const {
	va_list		argptr;
	char		text[MAX_STRING_CHARS];

	va_start( argptr, fmt );
	idStr::vsnPrintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	common->Warning( "%s", text );
}

/*
============
idGameLocal::DWarning
============
*/
void idGameLocal::DWarning( const char *fmt, ... ) const {
	va_list		argptr;
	char		text[MAX_STRING_CHARS];
	idThread *	thread;

	if ( !developer.GetBool() ) {
		return;
	}

	va_start( argptr, fmt );
	idStr::vsnPrintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	thread = idThread::CurrentThread();
	if ( thread ) {
		thread->Warning( "%s", text );
	} else {
		common->DWarning( "%s", text );
	}
}

/*
============
idGameLocal::Error
============
*/
void idGameLocal::Error( const char *fmt, ... ) const {
	va_list		argptr;
	char		text[MAX_STRING_CHARS];
	idThread *	thread;

	va_start( argptr, fmt );
	idStr::vsnPrintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

// RAVEN BEGIN
// scork: some model errors arrive here during validation which kills the whole process, so let's just warn about them instead...
	if ( common->DoingDeclValidation() ) {
		this->Warning( "%s", text );
		return;
	}
// RAVEN END

	thread = idThread::CurrentThread();
	if ( thread ) {
		thread->Error( "%s", text );
	} else {
		common->Error( "%s", text );
	}
}

/*
===============
gameError
===============
*/
void gameError( const char *fmt, ... ) {
	va_list		argptr;
	char		text[MAX_STRING_CHARS];

	va_start( argptr, fmt );
	idStr::vsnPrintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	gameLocal.Error( "%s", text );
}

/*
===========
idGameLocal::SetLocalClient
============
*/
void idGameLocal::SetLocalClient( int clientNum ) {
	localClientNum = clientNum;
}

/*
===========
idGameLocal::SetUserInfo
============
*/
const idDict* idGameLocal::SetUserInfo( int clientNum, const idDict &userInfo, bool isClient ) {
	
	TIME_THIS_SCOPE( __FUNCLINE__);
	
	int i;
	bool modifiedInfo = false;

	this->isClient = isClient;

	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		idGameLocal::userInfo[ clientNum ] = userInfo;

		// A bot's user info is broadcast by the engine with nothing but a name
		// in it, so restore the rest of its identity on every update.
		if ( !isClient && botManager.IsBot( clientNum ) ) {
			botManager.FillUserInfo( clientNum, idGameLocal::userInfo[ clientNum ] );
			modifiedInfo = true;
		}

		// server sanity
		if ( !isClient ) {

			// don't let numeric nicknames, it can be exploited to go around kick and ban commands from the server
			if ( idStr::IsNumeric( this->userInfo[ clientNum ].GetString( "ui_name" ) ) ) {
#if UI_DEBUG
				common->Printf( "SetUserInfo: client %d changed name from %s to %s_\n", 
					clientNum, idGameLocal::userInfo[ clientNum ].GetString( "ui_name" ), idGameLocal::userInfo[ clientNum ].GetString( "ui_name" ) );
#endif
				idGameLocal::userInfo[ clientNum ].Set( "ui_name", va( "%s_", idGameLocal::userInfo[ clientNum ].GetString( "ui_name" ) ) );
				modifiedInfo = true;
			}
		
			// don't allow dupe nicknames
			for ( i = 0; i < numClients; i++ ) {
				if ( i == clientNum ) {
					continue;
				}
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
				if ( entities[ i ] && entities[ i ]->IsType( idPlayer::GetClassType() ) ) {
// RAVEN END
					if ( !idStr::Icmp( idGameLocal::userInfo[ clientNum ].GetString( "ui_name" ), idGameLocal::userInfo[ i ].GetString( "ui_name" ) ) ) {
#if UI_DEBUG
						common->Printf( "SetUserInfo: client %d changed name from %s to %s_ because of client %d\n", 
							clientNum, idGameLocal::userInfo[ clientNum ].GetString( "ui_name" ), idGameLocal::userInfo[ clientNum ].GetString( "ui_name" ), i );
#endif
						idGameLocal::userInfo[ clientNum ].Set( "ui_name", va( "%s_", idGameLocal::userInfo[ clientNum ].GetString( "ui_name" ) ) );
						modifiedInfo = true;
						i = -1;	// rescan
						continue;
					}
				}
			}
		}

// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( entities[ clientNum ] && entities[ clientNum ]->IsType( idPlayer::GetClassType() ) ) {
// RAVEN END
			modifiedInfo |= static_cast<idPlayer *>( entities[ clientNum ] )->UserInfoChanged();
		}

		if ( !isClient ) {
			// now mark this client in game
			mpGame.EnterGame( clientNum );
		}
	}

	if ( modifiedInfo ) {
		newInfo = idGameLocal::userInfo[ clientNum ];
		return &newInfo;
	}
	return NULL;
}

/*
===========
idGameLocal::GetUserInfo
============
*/
const idDict* idGameLocal::GetUserInfo( int clientNum ) {
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
	if ( entities[ clientNum ] && entities[ clientNum ]->IsType( idPlayer::GetClassType() ) ) {
// RAVEN END
		return &userInfo[ clientNum ];
	}
	return NULL;
}

/*
===========
idGameLocal::IsClientActive
============
*/
bool idGameLocal::IsClientActive( int clientNum ) {
	if ( entities[ clientNum ] && entities[ clientNum ]->IsType( idPlayer::GetClassType() ) ) {
		return true;
	}

	return false;
}

/*
===========
idGameLocal::RepeaterSetUserInfo
============
*/
const idDict* idGameLocal::RepeaterSetUserInfo( int clientNum, const idDict &userInfo ) {

	TIME_THIS_SCOPE( __FUNCLINE__);

	assert( cvarSystem->GetCVarInteger( "ri_maxViewers" ) > clientNum );
	ReallocViewers( cvarSystem->GetCVarInteger( "ri_maxViewers" ) );

	idDict *info;
	
	int i;
	bool modifiedInfo = false;

	info = &viewers[ clientNum ].info;
	*info = userInfo;

	// don't let numeric nicknames, it can be exploited to go around kick and ban commands from the server
	if ( idStr::IsNumeric( info->GetString( "ui_name" ) ) ) {
		info->Set( "ui_name", va( "%s_", info->GetString( "ui_name" ) ) );
		modifiedInfo = true;
	}

	// don't allow dupe nicknames
	for ( i = 0; i < maxViewer; i++ ) {
		if ( i == clientNum ) {
			continue;
		}

		if ( !viewers[ i ].active ) {
			continue;
		}

		if ( !idStr::Icmp( info->GetString( "ui_name" ), viewers[ i ].info.GetString( "ui_name" ) ) ) {
			info->Set( "ui_name", va( "%s_", info->GetString( "ui_name" ) ) );
			modifiedInfo = true;
			i = -1;	// rescan
			continue;
		}
	}

	if ( modifiedInfo ) {
		newInfo = *info;
		return &newInfo;
	}

	return NULL;
}

/*
===========
idGameLocal::UpdateRepeaterInfo
============
*/

void idGameLocal::UpdateRepeaterInfo( bool transmit ) {
	if ( !isRepeater ) {
		return;
	}

	repeaterInfo = serverInfo;
	repeaterInfo.Set( "si_tv", "1" );

	// fudge a few cvars so old browsers display correctly
	repeaterInfo.Set( "si_usePass", ri_useViewerPass.GetBool() ? "1" : "0" );

	const char *ri_name = cvarSystem->GetCVarString( "ri_name" );
	if ( ri_name[0] ) {
		if ( !serverInfo.GetString( "ri_origName" ) [0] ) {
			repeaterInfo.Set( "ri_origName", serverInfo.GetString( "si_name" ) );
		}
		repeaterInfo.Set( "si_name", ri_name );
	}

	const char *si_serverURL = cvarSystem->GetCVarString( "si_serverURL" );
	if ( si_serverURL[0] ) {
		if ( !serverInfo.GetString( "ri_origServerURL" ) [0] ) {
			repeaterInfo.Set( "ri_origServerURL", serverInfo.GetString( "si_serverURL" ) );
		}
		repeaterInfo.Set( "si_serverURL", si_serverURL );
	}

	repeaterInfo.Copy( *cvarSystem->MoveCVarsToDict( CVAR_REPEATERINFO ) );

	networkSystem->RepeaterSetInfo( repeaterInfo );

	if ( transmit ) {
		// Let our clients know the server info changed
		idBitMsg	outMsg;
		byte		msgBuf[MAX_GAME_MESSAGE_SIZE];

		outMsg.Init( msgBuf, sizeof( msgBuf ) );
		outMsg.WriteByte( GAME_RELIABLE_MESSAGE_SERVERINFO );
		outMsg.WriteDeltaDict( gameLocal.repeaterInfo, NULL );
// jmarshall - engine has no repeater reliable channel
		//networkSystem->RepeaterSendReliableMessage( -1, outMsg );
// jmarshall end
	}
}

/*
===========
idGameLocal::SetServerInfo
============
*/
void idGameLocal::SetServerInfo( const idDict &_serverInfo ) {
	TIME_THIS_SCOPE( __FUNCLINE__);
	
	idBitMsg	outMsg;
	byte		msgBuf[MAX_GAME_MESSAGE_SIZE];
	bool timeLimitChanged = false;

// RAVEN BEGIN
// mekberg: clear announcer and reschedule time announcements
	if ( ( isClient || isListenServer ) && mpGame.GetGameState( ) && mpGame.GetGameState( )->GetMPGameState( ) == GAMEON &&
		serverInfo.GetInt( "si_timelimit" ) != _serverInfo.GetInt( "si_timelimit" ) ) {
		timeLimitChanged = true;
	}

	serverInfo = _serverInfo;

	if ( timeLimitChanged ) {
		mpGame.ClearAnnouncerSounds( );
		mpGame.ScheduleTimeAnnouncements( );
	}
// RAVEN END

	UpdateRepeaterInfo( true );

	if ( isServer ) {
		// Let our clients know the server info changed
		outMsg.Init( msgBuf, sizeof( msgBuf ) );
		outMsg.WriteByte( GAME_RELIABLE_MESSAGE_SERVERINFO );
		outMsg.WriteDeltaDict( gameLocal.serverInfo, NULL );
		networkSystem->ServerSendReliableMessage( -1, outMsg );
	} else if ( isClient ) {
		networkSystem->ClientSetServerInfo( gameLocal.serverInfo );
		isTVClient = ( gameLocal.serverInfo.GetInt( "si_tv" ) == 1 ) ? true : false;
	}
}

/*
===================
idGameLocal::LoadMap

Initializes all map variables common to both save games and spawned games.
===================
*/
// Retail CTF maps have the two scoring bases but no neutral flag. Add the
// existing neutral-flag entity at the most central unteamed pickup location.
// This uses authored, reachable item positions instead of guessing a point in
// solid geometry. Prepare the same entity list for every mode on both peers;
// normal entity filtering keeps the extra slot empty outside One Flag.
static void AddMissingNeutralFlag( idMapFile *map ) {
	idVec3 bases[ TEAM_MAX ];
	bool foundBase[ TEAM_MAX ] = { false, false };
	for ( int index = 1; index < map->GetNumEntities(); ++index ) {
		idDict args = map->GetEntity( index )->epairs;
		const idDeclEntityDef *definition = gameLocal.FindEntityDef( args.GetString( "classname" ), false );
		if ( definition == NULL ) {
			continue;
		}
		args.SetDefaults( &definition->dict );
		if ( idStr::Icmp( args.GetString( "spawnclass" ), "rvItemCTFFlag" ) != 0 ||
			args.GetBool( "dropped" ) || args.GetBool( "not_multiplayer" ) ) {
			continue;
		}
		const int team = args.GetInt( "team", "-1" );
		if ( team == TEAM_MAX ) {
			return; // A map author's objective always takes precedence.
		}
		if ( team >= 0 && team < TEAM_MAX ) {
			foundBase[ team ] = args.GetVector( "origin", "0 0 0", bases[ team ] );
		}
	}
	if ( !foundBase[ TEAM_MARINE ] || !foundBase[ TEAM_STROGG ] ||
		gameLocal.FindEntityDef( "mp_ctf_one_flag", false ) == NULL ) {
		return;
	}

	int selected = -1;
	float bestDistance = idMath::INFINITY;
	for ( int index = 1; index < map->GetNumEntities(); ++index ) {
		const idDict &args = map->GetEntity( index )->epairs;
		const char *classname = args.GetString( "classname" );
		if ( idStr::FindText( classname, "item_" ) != 0 &&
			idStr::FindText( classname, "weapon_" ) != 0 &&
			idStr::FindText( classname, "powerup_" ) != 0 ) {
			continue;
		}
		idVec3 origin;
		if ( args.GetBool( "not_multiplayer" ) || *args.GetString( "team" ) != '\0' ||
			!args.GetVector( "origin", "0 0 0", origin ) ) {
			continue;
		}
		// Minimize the longer straight-line distance to either base. Map order
		// breaks exact ties deterministically on the server and clients.
		const float distance = Max( ( origin - bases[ TEAM_MARINE ] ).LengthSqr(),
			( origin - bases[ TEAM_STROGG ] ).LengthSqr() );
		if ( distance < bestDistance ) {
			bestDistance = distance;
			selected = index;
		}
	}
	if ( selected < 0 ) {
		return;
	}

	idStr name = "openq4_neutral_flag";
	for ( int suffix = 1; ; ++suffix ) {
		bool inUse = false;
		for ( int index = 1; index < map->GetNumEntities(); ++index ) {
			if ( name.Icmp( map->GetEntity( index )->epairs.GetString( "name" ) ) == 0 ) {
				inUse = true;
				break;
			}
		}
		if ( !inUse ) {
			break;
		}
		name = va( "openq4_neutral_flag_%d", suffix );
	}
	idMapEntity *flag = new idMapEntity;
	flag->epairs.Set( "classname", "mp_ctf_one_flag" );
	flag->epairs.Set( "name", name.c_str() );
	flag->epairs.Set( "origin", map->GetEntity( selected )->epairs.GetString( "origin" ) );
	flag->epairs.SetBool( "nodrop", true );
	map->AddEntity( flag );
}

void idGameLocal::LoadMap( const char *mapName, int randseed ) {
	int i;
// RAVEN BEGIN
// jnewquist: Tag scope and callees to track allocations using "new".
	MEM_SCOPED_TAG(tag,MA_PARSER);
// RAVEN END
	bool sameMap = (mapFile && idStr::Icmp(mapFile->GetName(), mapName) == 0);
	const int loadStartMsec = Sys_Milliseconds();
	int phaseStartMsec = loadStartMsec;
	int mapParseMsec = 0;
	int mapResolveMsec = 0;
	int collisionMsec = 0;
	int entityStringMsec = 0;

	networkSystem->SetLoadingText( mapName );

	// Animations decoded from the generated cache must not outlive a pure lock
	// (or fs_restrict): once the filesystem refuses those reads, derive them
	// again from the pure paks before this map uses them.
	if ( !fileSystem->GeneratedCacheReadsEnabled( GENERATED_CACHE_ANIMATION ) ) {
		const int reloadedAnims = animationLib->ReloadGeneratedCacheAnims();
		if ( reloadedAnims > 0 ) {
			Printf( "%i animations decoded from the generated cache reloaded: generated-cache reads are refused\n", reloadedAnims );
		}
	}

	InitAsyncNetwork();

	// these can changed based upon sp / mp
	mHz = common->GetUserCmdHz();
	msec = common->GetUserCmdMSec();

	if ( !sameMap || ( mapFile && mapFile->NeedsReload() ) ) {
		// load the .map file
		if ( mapFile ) {
			delete mapFile;
		}
		mapFile = new idMapFile;
		if ( !mapFile->Parse( idStr( mapName ) + ".map" ) ) {
			delete mapFile;
			mapFile = NULL;
			Error( "Couldn't load %s", mapName );
		}
		mapParseMsec = Sys_Milliseconds() - phaseStartMsec;
		phaseStartMsec = Sys_Milliseconds();
// RAVEN BEGIN
// rjohnson: added resolve for handling func_groups and other aspects.  Before, radiant would do this processing on a map destroying the original data
		mapFile->Resolve();
// RAVEN END
		mapResolveMsec = Sys_Milliseconds() - phaseStartMsec;
	}
	mapFileName = mapFile->GetName();
	
	assert(!idStr::Cmp(mapFileName, mapFile->GetName()));
	
// RAVEN BEGIN
// rjohnson: entity usage stats
	mapFileNameStripped = mapFileName;
	mapFileNameStripped.StripFileExtension();
	mapFileNameStripped.StripPath();

	const char*	entityFilter;

	gameLocal.serverInfo.GetString( "si_entityFilter", "", &entityFilter );
	if ( entityFilter && *entityFilter ) {
		mapFileNameStripped += " ";
		mapFileNameStripped += entityFilter;
	}
// RAVEN END

	// load the collision map
	networkSystem->SetLoadingText( common->GetLocalizedString( "#str_107668" ) );
	phaseStartMsec = Sys_Milliseconds();
	collisionModelManager->LoadMap( mapFile, false );
	collisionMsec = Sys_Milliseconds() - phaseStartMsec;
	phaseStartMsec = Sys_Milliseconds();
	if ( !mapFile->ApplyEntityStringFiles() ) {
		Error( "Couldn't apply entity-string files for %s", mapName );
	}
	if ( isMultiplayer ) {
		AddMissingNeutralFlag( mapFile );
	}
	entityStringMsec = Sys_Milliseconds() - phaseStartMsec;
	if ( cvarSystem->GetCVarBool( "com_showLevelLoadTimes" ) ) {
		Printf(
			"Game LoadMap phases: mapParse=%d mapResolve=%d collision=%d entityStrings=%d total=%d msec\n",
			mapParseMsec,
			mapResolveMsec,
			collisionMsec,
			entityStringMsec,
			Sys_Milliseconds() - loadStartMsec );
	}

	numClients = 0;

	// initialize all entities for this game
	memset( entities, 0, sizeof( entities ) );
	usercmds = NULL;
	memset( spawnIds, -1, sizeof( spawnIds ) );
	spawnCount = INITIAL_SPAWN_COUNT;
	
	spawnedEntities.Clear();
	activeEntities.Clear();
	presentationEntities.Clear();
	presentationClientEntities.Clear();
	numEntitiesToDeactivate = 0;
	sortTeamMasters = false;
	sortPushers = false;
	lastGUIEnt = NULL;
	lastGUI = 0;

// RAVEN BEGIN
// bdube: client entities
	clientSpawnCount = INITIAL_SPAWN_COUNT;
	clientSpawnedEntities.Clear ( );
	memset ( clientSpawnIds, -1, sizeof(clientSpawnIds ) );
	memset ( clientEntities, 0, sizeof(clientEntities) );
	firstFreeClientIndex = 0;
// RAVEN END

	globalMaterial = NULL;

	memset( globalShaderParms, 0, sizeof( globalShaderParms ) );

	// always leave room for the max number of clients,
	// even if they aren't all used, so numbers inside that
	// range are NEVER anything but clients
	num_entities	= MAX_CLIENTS;
	firstFreeIndex	= MAX_CLIENTS;

	// reset the random number generator.
	random.SetSeed( isMultiplayer ? randseed : 0 );

	camera			= NULL;
	world			= NULL;
	testmodel		= NULL;
// RAVEN BEGIN
// bdube: not using id effects
//	testFx			= NULL;
// RAVEN END

// RAVEN BEGIN
// bdube: merged
	lastAIAlertEntity = NULL;
	lastAIAlertEntityTime = 0;
	lastAIAlertActor = NULL;
	lastAIAlertActorTime = 0;
// RAVEN END
	
	previousTime	= 0;
	time			= 0;
	framenum		= 0;
	sessionCommand = "";
	nextGibTime		= 0;
// RITUAL BEGIN
// squirrel: added DeadZone multiplayer mode
	unFreezeTime	= 0;
	isFrozen		= 0;
// RITUAL END

	vacuumAreaNum = -1;		// if an info_vacuum is spawned, it will set this

// RAVEN BEGIN
// abahr
	gravityInfo.Clear();
	scriptObjectProxies.Clear();
// RAVEN END

	if ( !editEntities ) {
		editEntities = new idEditEntities;
	}

	if( gameLocal.isMultiplayer ) {
		gravity.Set( 0, 0, -g_mp_gravity.GetFloat() );
	} else {
		gravity.Set( 0, 0, -g_gravity.GetFloat() );
	}

	spawnArgs.Clear();

// RAVEN BEGIN
// nmckenzie:
	//make sure we clear all reachabilities we marked as blocked!
	aiManager.UnMarkAllReachBlocked();
	aiManager.Clear();
// RAVEN END

	skipCinematic = false;
	inCinematic = false;
	cinematicSkipTime = 0;
	cinematicStopTime = 0;
	cinematicMaxSkipTime = 0;

// RAVEN BEGIN
// ddynerman: main world instance
	PACIFIER_UPDATE;
	AddInstance( 0, true );
	assert( instances.Num() == 1 && instances[ 0 ]->GetInstanceID() == 0 );
// RAVEN END
	pvs.Init();
// RAVEN BEGIN
// mwhitlock: Xenon texture streaming
#if defined(_XENON)
	// Experimental use of game's PVS for reducing amount of stuff streamed. Will
	// do this a bit more cleanly if I decide to keep this.
	extern idPVS* pvsForStreaming;
	pvsForStreaming=&pvs;
#endif
// RAVEN END

	playerPVS.i = -1;
	playerConnectedAreas.i = -1;

	// load navigation system for all the different monster sizes
	for( i = 0; i < aasNames.Num(); i++ ) {
		aasList[ i ]->Init( idStr( mapFileName ).SetFileExtension( aasNames[ i ] ).c_str(), mapFile->GetGeometryCRC() );
	}

// RAVEN BEGIN
// cdr: Obstacle Avoidance
	AI_MoveInitialize();
// RAVEN END

	FindEntityDef( "preCacheExtras", false );

	if ( !sameMap ) {
		mapFile->RemovePrimitiveData();
	}

// RAVEN BEGIN
// ddynerman: ambient light list
	ambientLights.Clear();
// RAVEN END
}

/*
===================
idGameLocal::LocalMapRestart
===================
*/
void idGameLocal::LocalMapRestart( int instance ) {
	int i, latchSpawnCount;

	Printf( "----------- Game Map Restart (%s) ------------\n", instance == -1 ? "all instances" : va( "instance %d", instance ) );

	// client always respawns everything, so make sure it picks up the right map entities
	if( instance == -1 || isClient ) {
		memset( isMapEntity, 0, sizeof(bool) * MAX_GENTITIES );
	} else {
		assert( instance >= 0 && instance < instances.Num() );

		for( int i = 0; i < instances[ instance ]->GetNumMapEntities(); i++ ) {
			if ( instances[ instance ]->GetMapEntityNumber( i ) >= 0 && instances[ instance ]->GetMapEntityNumber( i ) < MAX_GENTITIES ) {
				isMapEntity[ instances[ instance ]->GetMapEntityNumber( i ) ] = false;
			}
		}
	}

	gamestate = GAMESTATE_SHUTDOWN;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( entities[ i ] && entities[ i ]->IsType( idPlayer::GetClassType() ) && (isClient || instance == -1 || entities[ i ]->GetInstance() == instance ) ) {
// RAVEN END
			static_cast< idPlayer * >( entities[ i ] )->PrepareForRestart();
		}
	}

	eventQueue.Shutdown();

	MapClear( false, instance );

	// clear the sound system
	soundSystem->StopAllSounds( SOUNDWORLD_GAME );

	// clear icons
	iconManager->Shutdown();

	// the spawnCount is reset to zero temporarily to spawn the map entities with the same spawnId
	// if we don't do that, network clients are confused and don't show any map entities
	latchSpawnCount = spawnCount;
	spawnCount = INITIAL_SPAWN_COUNT;

	gamestate = GAMESTATE_STARTUP;

	program.Restart();

	InitScriptForMap();

	MapPopulate( instance );

	// once the map is populated, set the spawnCount back to where it was so we don't risk any collision
	// (note that if there are no players in the game, we could just leave it at it's current value)
	spawnCount = latchSpawnCount;

	// setup the client entities again
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( entities[ i ] && entities[ i ]->IsType( idPlayer::GetClassType() ) && (isClient || instance == -1 || entities[ i ]->GetInstance() == instance ) ) {
// RAVEN END
			static_cast< idPlayer * >( entities[ i ] )->Restart();
		}
	}

	gamestate = GAMESTATE_ACTIVE;

	Printf( "--------------------------------------\n" );
}

/*
===================
idGameLocal::MapRestart
===================
*/
void idGameLocal::MapRestart( int instance ) {
	idBitMsg	outMsg;
	byte		msgBuf[MAX_GAME_MESSAGE_SIZE];
	idDict		newInfo;
	int			i;
	const idKeyValue *keyval, *keyval2;

	if ( isClient ) {
		SetGameType();
		LocalMapRestart( instance );
	} else {

		if ( mpGame.PickMap( "", true ) ) {
			common->Warning( "map %s and gametype %s are incompatible, aborting map change", si_map.GetString(), si_gameType.GetString() );
			if ( (++incompatibleMaps) >= 50 ) {
				common->Warning( "too many incompatible maps for gametype %s; setting gametype to DM.", si_gameType.GetString() );
				si_gameType.SetString( "DM" );
			}
			return;
		}

		incompatibleMaps = 0;

		newInfo = *cvarSystem->MoveCVarsToDict( CVAR_SERVERINFO );
		// this has to be after the cvars are moved to the dict
		for ( i = 0; i < newInfo.GetNumKeyVals(); i++ ) {

			keyval = newInfo.GetKeyVal( i );
			keyval2 = serverInfo.FindKey( keyval->GetKey() );
			if ( !keyval2 ) {
				break;
			}
			// a select set of si_ changes will cause a full restart of the server
			if ( keyval->GetValue().Icmp( keyval2->GetValue() ) &&
				( !keyval->GetKey().Icmp( "si_pure" ) || !keyval->GetKey().Icmp( "si_map" ) ) ) {
				break;
			}
//RAVEN BEGIN
			//asalmon: need to restart if the game type has changed but the map has not cause someone could be connecting
			if(  keyval->GetValue().Icmp( keyval2->GetValue() ) && (!keyval->GetKey().Icmp("si_gametype")))
			{
				break;
			}
//RAVEN END

			// some calculations about time are done from the frame, and
			// that changes when si_fps changes.  full restart resets to 0.
			if ( keyval->GetValue().Icmp( keyval2->GetValue() ) &&
				!keyval->GetKey().Icmp( "si_fps" ) ) {
				break;
			}
		}
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, "rescanSI" " " __FILE__ " " __LINESTR__ );

		SetGameType();

		mpGame.isBuyingAllowedRightNow = false;

		if ( i != newInfo.GetNumKeyVals() ) {
			gameLocal.sessionCommand = "nextMap";
		} else {
			outMsg.Init( msgBuf, sizeof( msgBuf ) );
			outMsg.WriteByte( GAME_RELIABLE_MESSAGE_RESTART );
			outMsg.WriteBits( 1, 1 );
			outMsg.WriteDeltaDict( serverInfo, NULL );
			networkSystem->ServerSendReliableMessage( -1, outMsg );

			if ( isRepeater ) {
				outMsg.Init( msgBuf, sizeof( msgBuf ) );
				outMsg.WriteByte( GAME_RELIABLE_MESSAGE_RESTART );
				outMsg.WriteBits( 1, 1 );
				outMsg.WriteDeltaDict( repeaterInfo, NULL );
// jmarshall - engine has no repeater reliable channel
				//networkSystem->RepeaterSendReliableMessage( -1, outMsg );
// jmarshall end
			}

			LocalMapRestart( instance );
			mpGame.MapRestart();
		}
	}
}

/*
===================
idGameLocal::VerifyServerSettings_f
===================
*/
void idGameLocal::VerifyServerSettings_f( const idCmdArgs &args ) {
	if ( gameLocal.mpGame.RejectManagedLegacyMutation( "verify server settings" ) ) {
		return;
	}
	gameLocal.mpGame.PickMap( si_gameType.GetString() );
}

/*
===================
idGameLocal::MapRestart_f
===================
*/
void idGameLocal::MapRestart_f( const idCmdArgs &args ) {
	// openQ4: the Arena campaign's own Restart match, authorized for its live
	// match token; every other restart of an Arena match is administration.
	if ( args.Argc() == 1 && gameLocal.mpGame.ArenaCampaignRestartAuthorized() ) {
		gameLocal.mpGame.RestartArenaCampaignMatch();
		return;
	}
	if ( gameLocal.mpGame.RejectManagedLegacyMutation( "server map restart" ) ) {
		return;
	}
	if ( !gameLocal.isMultiplayer || gameLocal.isClient ) {
		common->Printf( "server is not running - use spawnServer\n" );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "spawnServer\n" );
		return;
	}
	
	int instance = -1;
	if( args.Argc() > 1 ) {
		instance = atoi( args.Args( 1 ) );
		if( instance < 0 || instance >= gameLocal.GetNumInstances() ) {
			common->Warning( "idGameLocal::MapRestart_f() - Invalid instance '%d' specified\n", instance );
			return;
		}
		gameLocal.LocalMapRestart( instance );
		return;
	}

	gameLocal.MapRestart( instance );
}

/*
===================
idGameLocal::NextMap
===================
*/
bool idGameLocal::NextMap( void ) {
	const function_t	*func;
	idThread			*thread;
	idDict				newInfo;
	const idKeyValue	*keyval, *keyval2;
	int					i;
	const char			*mapCycleList, *currentMap;

//RAVEN BEGIN
//asalmon: pick another map on Xenon
#ifdef _XENON
	//Live()->PickMap();
	return true;
#endif
//RAVEN END

// RAVEN BEGIN
// rjohnson: traditional map cycle
//		si_mapCycle "mp/q4dm4;mp/q4dm5;mp/q4dm6"
	mapCycleList = si_mapCycle.GetString();
	if ( mapCycleList && strlen( mapCycleList ) ) {
		idLexer src;
		idToken token, firstFound;
		int		numMaps = 0;
		bool	foundMap = false;

		src.FreeSource();
		src.SetFlags( LEXFL_NOFATALERRORS | LEXFL_ALLOWPATHNAMES | LEXFL_ALLOWMULTICHARLITERALS | LEXFL_ALLOWBACKSLASHSTRINGCONCAT );
		src.LoadMemory( mapCycleList, idLib::SizeToInt( strlen( mapCycleList ), "idGameLocal::NextMap" ), "idGameLocal::NextMap" );
		if ( src.IsLoaded() ) {

			currentMap = si_map.GetString();
			while( src.ReadToken( &token ) ) {
				if ( token == ";" ) {
					continue;
				}
				numMaps++;

				if ( numMaps == 1 ) {
					// guarantee that we use a map no matter what ( or when we wrap )
					firstFound = token;
				}
				if ( foundMap ) {
					firstFound = token;
					break;
				}
				
				if ( idStr::Icmp( token, currentMap ) == 0 ) {
					foundMap = true;
				}
			}

			if ( firstFound != "" ) {
				si_map.SetString( firstFound );
				return true;
			}
		}

		return false;
	}
// RAVEN END

	if ( !g_mapCycle.GetString()[0] ) {
		Printf( common->GetLocalizedString( "#str_104294" ) );
		return false;
	}
	if ( fileSystem->ReadFile( g_mapCycle.GetString(), NULL, NULL ) < 0 ) {
		if ( fileSystem->ReadFile( va( "%s.scriptcfg", g_mapCycle.GetString() ), NULL, NULL ) < 0 ) {
			Printf( "map cycle script '%s': not found\n", g_mapCycle.GetString() );
			return false;
		} else {
			g_mapCycle.SetString( va( "%s.scriptcfg", g_mapCycle.GetString() ) );
		}
	}

	Printf( "map cycle script: '%s'\n", g_mapCycle.GetString() );
	func = program.FindFunction( "mapcycle::cycle" );
	if ( !func ) {
		program.CompileFile( g_mapCycle.GetString(), false );
		func = program.FindFunction( "mapcycle::cycle" );
	}
	if ( !func ) {
		Printf( "Couldn't find mapcycle::cycle\n" );
		return false;
	}
	thread = new idThread( func );
	thread->Start();
	delete thread;

	newInfo = *cvarSystem->MoveCVarsToDict( CVAR_SERVERINFO );
	for ( i = 0; i < newInfo.GetNumKeyVals(); i++ ) {
		keyval = newInfo.GetKeyVal( i );
		keyval2 = serverInfo.FindKey( keyval->GetKey() );
		if ( !keyval2 || keyval->GetValue().Icmp( keyval2->GetValue() ) ) {
			break;
		}
	}
	return ( i != newInfo.GetNumKeyVals() );
}

/*
===================
idGameLocal::NextMap_f
===================
*/
void idGameLocal::NextMap_f( const idCmdArgs &args ) {
	if ( gameLocal.mpGame.RejectManagedLegacyMutation( "server next map" ) ) {
		return;
	}
	if ( !gameLocal.isMultiplayer || gameLocal.isClient ) {
		common->Printf( "server is not running\n" );
		return;
	}

	gameLocal.NextMap( );
	// next map was either voted for or triggered by a server command - always restart
	gameLocal.MapRestart( );
}

/*
===============
idGameLocal::GetStartingIndexForInstance
===============
*/
int idGameLocal::GetStartingIndexForInstance( int instanceID ) {
	if ( isServer ) {
		assert( instancesEntityIndexWatermarks.Num() >= instanceID );
		if ( instanceID == 0 ) {
			return MAX_CLIENTS;
		} else {
			// the high watermark of the previous instance is the starting index of the next one
			return instancesEntityIndexWatermarks[ instanceID - 1 ];
		}
	} else {
		assert( instanceID == 0 );
		return clientInstanceFirstFreeIndex;
	}
}

/*
===============
idGameLocal::ServerSetEntityIndexWatermark
keep track of the entity layout at the server - specially when there are multiple instances ( tourney )
===============
*/
void idGameLocal::ServerSetEntityIndexWatermark( int instanceID ) {
	if ( isClient ) {
		return;
	}
	instancesEntityIndexWatermarks.AssureSize( instanceID + 1, MAX_CLIENTS );
	// make sure there is no drift. if a value was already set it has to match
	// otherwise that means the server is repopulating with different indexes, and that would likely lead to net corruption
	assert( instancesEntityIndexWatermarks[ instanceID ] == MAX_CLIENTS || instancesEntityIndexWatermarks[ instanceID ] == firstFreeIndex );
	instancesEntityIndexWatermarks[ instanceID ] = firstFreeIndex;
}

/*
===============
idGameLocal::ServerSetMinSpawnIndex
===============
*/
void idGameLocal::ServerSetMinSpawnIndex( void ) {
	if ( isClient ) {
		return;
	}
	// setup minSpawnIndex with enough headroom so gameplay entities don't cause bad offsets
	// only needed on server, clients are completely slaved up to server entity layout
	if ( !idStr::Icmp( serverInfo.GetString( "si_gameType" ), "Tourney" ) ) {
		minSpawnIndex = MAX_CLIENTS + GetNumMapEntities() * MAX_INSTANCES;
	}
}

/*
===================
idGameLocal::MapPopulate
===================
*/
void idGameLocal::MapPopulate( int instance ) {
	const int populateStartMsec = Sys_Milliseconds();
	int phaseStartMsec = populateStartMsec;
	int restartMsec = 0;
	int locationsMsec = 0;
	int spawnInitMsec = 0;
	int eventsMsec = 0;

// RAVEN BEGIN
// jnewquist: Tag scope and callees to track allocations using "new".
	MEM_SCOPED_TAG(tag,MA_ENTITY);
// RAVEN END

	if ( isMultiplayer ) {
		cvarSystem->SetCVarBool( "r_skipSpecular", false );
	}

	minSpawnIndex = MAX_CLIENTS;

	// parse the key/value pairs and spawn entities
// RAVEN BEGIN
// ddynerman: instance code
	// reload the instances
	if ( instance == -1 ) {
		int i;
		firstFreeIndex = MAX_CLIENTS;
		for ( i = 0; i < instances.Num(); i++ ) {
			if ( instances[ i ] ) {
				instances[ i ]->Restart();

				ServerSetEntityIndexWatermark( i );
			}
		}
	} else {
		assert( instance >= 0 && instance < instances.Num() );
		instances[ instance ]->Restart();
	}
// RAVEN END
	restartMsec = Sys_Milliseconds() - phaseStartMsec;
	phaseStartMsec = Sys_Milliseconds();

	ServerSetMinSpawnIndex();

	// mark location entities in all connected areas
	SpreadLocations();
	locationsMsec = Sys_Milliseconds() - phaseStartMsec;
	phaseStartMsec = Sys_Milliseconds();

	// RAVEN BEGIN
	// ddynerman: prepare the list of spawn spots
	InitializeSpawns();
	// RAVEN END
	spawnInitMsec = Sys_Milliseconds() - phaseStartMsec;
	phaseStartMsec = Sys_Milliseconds();

	// execute pending events before the very first game frame
	// this makes sure the map script main() function is called
	// before the physics are run so entities can bind correctly
	Printf( "------------ Processing events --------------\n" );
	idEvent::ServiceEvents();
	eventsMsec = Sys_Milliseconds() - phaseStartMsec;
	if ( cvarSystem->GetCVarBool( "com_showLevelLoadTimes" ) ) {
		Printf(
			"Game populate phases: restart=%d locations=%d spawnInit=%d events=%d total=%d msec\n",
			restartMsec,
			locationsMsec,
			spawnInitMsec,
			eventsMsec,
			Sys_Milliseconds() - populateStartMsec );
	}
}

/*
===================
idGameLocal::InitFromNewMap
===================
*/
void idGameLocal::InitFromNewMap( const char *mapName, idRenderWorld *renderWorld, bool isServer, bool isClient, int randseed ) {

	TIME_THIS_SCOPE( __FUNCLINE__);
	const int initStartMsec = Sys_Milliseconds();
	int phaseStartMsec = initStartMsec;
	int startupMsec = 0;
	int loadMapMsec = 0;
	int scriptMsec = 0;
	int populateMsec = 0;
	int mpPrecacheMsec = 0;
	int animationFlushMsec = 0;
	
	this->isServer = isServer;
	this->isClient = isClient;
// RAVEN BEGIN
// ddynerman: listen server
	this->isListenServer = isServer && !cvarSystem->GetCVarBool( "net_serverDedicated" );
// RAVEN END
	this->isMultiplayer = isServer || isClient;
	
	if ( this->isClient && gameLocal.serverInfo.GetInt( "si_tv" ) == 1 ) {
		this->isTVClient = true;
	}

	if ( cvarSystem->GetCVarInteger( "net_serverDownload" ) == 3 ) {
		networkSystem->HTTPEnable( this->isServer || this->isRepeater );
	}

	if ( !this->isMultiplayer )
		gameLocal.Error( "This mod is for multiplayer only" );

	PACIFIER_UPDATE;

//RAVEN BEGIN
//asalmon: stats for single player
	if (!this->isMultiplayer) {
#ifdef _MPBETA
		return;
#else
		statManager->EndGame();
#ifdef _XENON
		Live()->DeleteSPSession(true);
#endif
		statManager->Shutdown();
		statManager->Init();
		statManager->BeginGame();
		statManager->ClientConnect(0);
#ifdef _XENON
		Live()->CreateSPSession();
#endif
#endif // _MPBETA
	}
//RAVEN END

	if ( mapFileName.Length() ) {
		MapShutdown();
	}

	Printf( "-------------- Game Map Init ----------------\n" );

	gamestate = GAMESTATE_STARTUP;

	gameRenderWorld = renderWorld;

// RAVEN BEGIN
// mwhitlock: Dynamic memory consolidation
#if defined(_RV_MEM_SYS_SUPPORT)
	animationLib->BeginLevelLoad();
#endif
	
	SetGameType();

	startupMsec = Sys_Milliseconds() - phaseStartMsec;
	phaseStartMsec = Sys_Milliseconds();
	BeginMapMediaPrecache();
	LoadMap( mapName, randseed );
	loadMapMsec = Sys_Milliseconds() - phaseStartMsec;
	phaseStartMsec = Sys_Milliseconds();

	InitScriptForMap();
	scriptMsec = Sys_Milliseconds() - phaseStartMsec;
	phaseStartMsec = Sys_Milliseconds();

	MapPopulate();
	populateMsec = Sys_Milliseconds() - phaseStartMsec;
	phaseStartMsec = Sys_Milliseconds();

	mpGame.Reset();

	mpGame.Precache();
	EndMapMediaPrecache();
	mpPrecacheMsec = Sys_Milliseconds() - phaseStartMsec;
	phaseStartMsec = Sys_Milliseconds();

// RAVEN BEGIN
// mwhitlock: Dynamic memory consolidation
#if defined(_RV_MEM_SYS_SUPPORT)
	animationLib->EndLevelLoad();
#endif
// RAVEN END

	// free up any unused animations
// RAVEN BEGIN
// jsinger: animationLib changed to a pointer
	animationLib->FlushUnusedAnims();
// RAVEN END
	animationFlushMsec = Sys_Milliseconds() - phaseStartMsec;
	if ( cvarSystem->GetCVarBool( "com_showLevelLoadTimes" ) ) {
		Printf(
			"Game init phases: startup=%d loadMap=%d script=%d populate=%d mpPrecache=%d animationFlush=%d total=%d msec\n",
			startupMsec,
			loadMapMsec,
			scriptMsec,
			populateMsec,
			mpPrecacheMsec,
			animationFlushMsec,
			Sys_Milliseconds() - initStartMsec );
	}

	gamestate = GAMESTATE_ACTIVE;
	autoExecAfterMapLoadStartTime = 0;
	const char *autoExecAfterMapLoad = g_autoExecAfterMapLoad.GetString();
	const char *autoExecAfterMapLoadCvar = cvarSystem->GetCVarString( "g_autoExecAfterMapLoad" );
	const bool autoExecAfterMapLoadArmed =
		( autoExecAfterMapLoad && autoExecAfterMapLoad[ 0 ] != '\0' ) ||
		( autoExecAfterMapLoadCvar && autoExecAfterMapLoadCvar[ 0 ] != '\0' );
	autoExecAfterMapLoadPending = false;
	autoExecAfterMapLoadWaitingLogged = false;
	if ( autoExecAfterMapLoadArmed ) {
		Printf( "AutoExecAfterMapLoad: armed for first active draw (delay %d ms)\n", g_autoExecAfterMapLoadDelayMs.GetInteger() );
		Printf( "AutoExecAfterMapLoad: waiting for first active draw\n" );
		autoExecAfterMapLoadWaitingLogged = true;
	}

	Printf( "---------------------------------------------\n" );
}

/*
=================
idGameLocal::InitFromSaveGame
=================
*/
static void GameLocal_ValidateSaveGameCount( idRestoreGame &savegame, int count, int maxCount, const char *detail ) {
	if ( count < 0 || count > maxCount ) {
		savegame.Error( "idGameLocal::InitFromSaveGame: invalid %s %d (max %d)", detail, count, maxCount );
	}
}

template< class T, int Count >
static void GameLocal_RestoreEntityTable( idRestoreGame &savegame, T *(&table)[Count], int (&spawnIds)[Count], int unregistered, const char *detail ) {
	for ( int i = 0; i < Count; i++ ) {
		T *restoredEntity = NULL;
		int restoredSpawnId;
		savegame.ReadObject( restoredEntity );
		savegame.ReadInt( restoredSpawnId );
		// Publish only complete, unique registrations. A repeated reference would
		// otherwise overwrite its first identity and leave a dangling table slot
		// when error recovery deletes the partially restored map.
		if ( restoredEntity != NULL ) {
			// Client-effect constructors can carry an old registration which
			// LoadMap cleared. Only an entry published earlier in this table is
			// a duplicate; an unused constructor number is replaced below.
			const int previous = restoredEntity->entityNumber;
			if ( i == unregistered || ( previous >= 0 && previous < i && table[previous] == restoredEntity ) ) {
				savegame.Error( "idGameLocal::InitFromSaveGame: invalid %s registration at %d (already %d)", detail, i, restoredEntity->entityNumber );
			}
			restoredEntity->entityNumber = i;
		}
		table[i] = restoredEntity;
		spawnIds[i] = restoredSpawnId;
	}
}

bool idGameLocal::InitFromSaveGame( const char *mapName, idRenderWorld *renderWorld, idFile *saveGameFile ) {
	TIME_THIS_SCOPE( __FUNCLINE__);
	
	int i;
	int num;
	idEntity *ent;
	idDict si;

	if ( mapFileName.Length() ) {
		MapShutdown();
	}

	Printf( "---------- Game Map Init SaveGame -----------\n" );

	gamestate = GAMESTATE_STARTUP;

	gameRenderWorld = renderWorld;

	idRestoreGame savegame( saveGameFile );

	savegame.ReadBuildNumber();
	if ( !savegame.IsOpenQ4SaveGameCompatible() ) {
		Warning(
			"Savegame payload is not compatible with this openQ4 build: %s",
			savegame.GetOpenQ4SaveGameCompatibilityError() );
		program.Restart();
		return false;
	}

	// Create the list of all objects in the game
	savegame.CreateObjects();

	// Load the idProgram, also checking to make sure scripting hasn't changed since the savegame
	if ( program.Restore( &savegame ) == false ) {

		// Abort the load process, and let the session know so that it can restart the level
		// with the player persistent data.
		savegame.DeleteObjects();
		program.Restart();

		return false;
	}

	// load the map needed for this savegame
	LoadMap( mapName, 0 );

	savegame.ReadInt( i );
	g_skill.SetInteger( i );

	// precache any media specified in the map
	BeginMapMediaPrecache();
	for ( i = 0; i < mapFile->GetNumEntities(); i++ ) {
		idMapEntity *mapEnt = mapFile->GetEntity( i );

		if ( !InhibitEntitySpawn( mapEnt->epairs ) ) {
			CacheDictionaryMedia( &mapEnt->epairs );
			const char *classname = mapEnt->epairs.GetString( "classname" );
			if ( classname[0] != '\0' ) {
				FindEntityDef( classname, false );
			}
		}
	}
	EndMapMediaPrecache();

	savegame.ReadDict( &si );
	SetServerInfo( si );

	savegame.ReadInt( numClients );
	GameLocal_ValidateSaveGameCount( savegame, numClients, MAX_CLIENTS, "client count" );
	for( i = 0; i < numClients; i++ ) {
// RAVEN BEGIN
// mekberg: don't read in userinfo. Grab from cvars
//		savegame.ReadDict( &userInfo[ i ] );
// RAVEN END
//		savegame.ReadUsercmd( usercmds[ i ] );
		savegame.ReadDict( &persistentPlayerInfo[ i ] );
	}

	GameLocal_RestoreEntityTable( savegame, entities, spawnIds, ENTITYNUM_NONE, "entity" );

	// Precache the player
// RAVEN BEGIN
// bdube: changed so we actually cache stuff
	FindEntityDef( idPlayer::GetSpawnClassname( ) );

// abahr: saving clientEntities
	GameLocal_RestoreEntityTable( savegame, clientEntities, clientSpawnIds, -1, "client entity" );
// RAVEN END

	savegame.ReadInt( firstFreeIndex );
	savegame.ReadInt( num_entities );
	if ( firstFreeIndex < 0 || firstFreeIndex >= MAX_GENTITIES ) {
		savegame.Error( "idGameLocal::InitFromSaveGame: invalid first free entity index %d", firstFreeIndex );
	}
	GameLocal_ValidateSaveGameCount( savegame, num_entities, MAX_GENTITIES, "entity count" );

	// enityHash is restored by idEntity::Restore setting the entity name.

	savegame.ReadObject( world );

	savegame.ReadInt( num );
	GameLocal_ValidateSaveGameCount( savegame, num, MAX_GENTITIES, "spawned entity count" );
	for( i = 0; i < num; i++ ) {
		savegame.ReadObject( ent );
		assert( ent );
		if ( ent ) {
			ent->spawnNode.AddToEnd( spawnedEntities );
		}
	}

// RAVEN BEGIN
// abahr: save scriptObject proxies
	savegame.ReadInt( num );
	GameLocal_ValidateSaveGameCount( savegame, num, MAX_GENTITIES + MAX_CENTITIES, "script object proxy count" );
	scriptObjectProxies.SetNum( num );
	for( i = 0; i < num; ++i ) {
		scriptObjectProxies[i].Restore( &savegame );
	}
// abahr: save client entity stuff
	rvClientEntity* clientEnt = NULL;
	savegame.ReadInt( num );
	GameLocal_ValidateSaveGameCount( savegame, num, MAX_CENTITIES, "client spawned entity count" );
	for( i = 0; i < num; ++i ) {
		savegame.ReadObject( clientEnt );
		assert( clientEnt );
		if ( clientEnt  ) {
			clientEnt->spawnNode.AddToEnd( clientSpawnedEntities );
		}
	}
// RAVEN END

	savegame.ReadInt( num );
	GameLocal_ValidateSaveGameCount( savegame, num, MAX_GENTITIES, "active entity count" );
	for( i = 0; i < num; i++ ) {
		savegame.ReadObject( ent );
		assert( ent );
		if ( ent ) {
			ent->activeNode.AddToEnd( activeEntities );
		}
	}

	savegame.ReadInt( numEntitiesToDeactivate );
	GameLocal_ValidateSaveGameCount( savegame, numEntitiesToDeactivate, MAX_GENTITIES, "deactivation count" );
	savegame.ReadBool( sortPushers );
	savegame.ReadBool( sortTeamMasters );
	savegame.ReadDict( &persistentLevelInfo );

	for( i = 0; i < MAX_GLOBAL_SHADER_PARMS; i++ ) {
		savegame.ReadFloat( globalShaderParms[ i ] );
	}

	savegame.ReadInt( i );
	random.SetSeed( i );

	idThread::RestoreOwnedThread( &savegame, frameCommandThread );

	// clip
	// push
	// pvs

	// testmodel = "<NULL>"
	// testFx = "<NULL>"

	savegame.ReadString( sessionCommand );
// RAVEN BEGIN
// nmckenzie: Let the AI system load itself too.
	aiManager.Restore( &savegame );
// RAVEN END

	// FIXME: save smoke particles

	savegame.ReadInt( cinematicSkipTime );
	savegame.ReadInt( cinematicStopTime );
	savegame.ReadInt( cinematicMaxSkipTime );
	savegame.ReadBool( inCinematic );
	savegame.ReadBool( skipCinematic );

	savegame.ReadBool( isMultiplayer );
	savegame.ReadInt( (int &)gameType );

	savegame.ReadInt( framenum );
	savegame.ReadInt( previousTime );
	savegame.ReadInt( time );

	savegame.ReadInt( vacuumAreaNum );

	savegame.ReadInt( entityDefBits );
	savegame.ReadBool( isServer );
	savegame.ReadBool( isClient );
// RAVEN BEGIN
	savegame.ReadBool( isListenServer );
// RAVEN END

	savegame.ReadInt( localClientNum );
	// openQ4: GetLocalPlayer indexes entities[] with this
	if ( localClientNum < -1 || localClientNum >= MAX_CLIENTS ) {
		savegame.Error( "idGameLocal::InitFromSaveGame: invalid local client %d", localClientNum );
	}

	// snapshotEntities is used for multiplayer only

	savegame.ReadInt( realClientTime );
	savegame.ReadBool( isNewFrame );

	savegame.ReadBool( mapCycleLoaded );
	savegame.ReadInt( spawnCount );

	savegame.ReadInt( num );
	if ( num < 0 ) {
		savegame.Error( "idGameLocal::InitFromSaveGame: invalid location entity area count %d", num );
	}
	if ( num ) {
		if ( num != gameRenderWorld->NumAreas() ) {
			savegame.Error( "idGameLocal::InitFromSaveGame: number of areas in map differs from save game." );
		}

		locationEntities = new idLocationEntity *[ num ];
		for( i = 0; i < num; i++ ) {
			savegame.ReadObject( locationEntities[ i ] );
		}
	}

	savegame.ReadObject( camera );

	savegame.ReadMaterial( globalMaterial );

// RAVEN BEGIN
// bdube: added
	lastAIAlertActor.Restore( &savegame );
	lastAIAlertEntity.Restore( &savegame );
	savegame.ReadInt( lastAIAlertEntityTime );
	savegame.ReadInt( lastAIAlertActorTime );
// RAVEN END

	savegame.ReadDict( &spawnArgs );

	savegame.ReadInt( playerPVS.i );
	savegame.ReadInt( (int &)playerPVS.h );
	savegame.ReadInt( playerConnectedAreas.i );
	savegame.ReadInt( (int &)playerConnectedAreas.h );

	savegame.ReadVec3( gravity );

	// gamestate is restored after restoring everything else

	savegame.ReadBool( influenceActive );
	savegame.ReadInt( nextGibTime );

	// spawnSpots
	// initialSpots
	// currentInitialSpot
	// newInfo
	// makingBuild
	// shakeSounds

	// Read out pending events
	idEvent::Restore( &savegame );

	savegame.RestoreObjects();

	mpGame.Reset();

	mpGame.Precache();

	// free up any unused animations
// RAVEN BEGIN
// jsinger: animationLib changed to a pointer
	animationLib->FlushUnusedAnims();
// RAVEN END

	savegame.ReleaseObjects();
	gamestate = GAMESTATE_ACTIVE;

	Printf( "--------------------------------------\n" );

	return true;
}

/*
===========
idGameLocal::MapClear
===========
*/
// RAVEN BEGIN
// ddynerman: multiple instances 
void idGameLocal::MapClear( bool clearClients, int instance ) {

	// openQ4: the classic dynamic light handles belong to the render world
	// being torn down here.
	G_FreeClassicLights();
// RAVEN END
	int i;

	// openQ4: the rewind history describes positions in the world being torn down.
	// LoadMap winds gameLocal.time back to 0 and a map restart does not wind it
	// back at all, so entries kept across either transition are still inside the
	// rewind window - shots in the first moments of a map or a round would rewind
	// opponents to where they stood before it.
	ResetMPLagCompensationHistory();

// RAVEN BEGIN
// bdube: delete client entities first since they reference real entities
	for( i = 0; i < MAX_CENTITIES; i++ ) {
		// on the server we need to keep around client entities bound to entities in our instance2
		if( gameLocal.isServer && gameLocal.GetLocalPlayer() && instance != -1 &&
			instance != gameLocal.GetLocalPlayer()->GetInstance() && 
			clientEntities[ i ] && clientEntities[ i ]->GetBindMaster() && 
			clientEntities[ i ]->GetBindMaster()->GetInstance() == gameLocal.GetLocalPlayer()->GetInstance() ) {
			continue;
		}
		delete clientEntities[ i ];
		clientEntities[ i ] = NULL;
		clientSpawnIds[ i ] = -1;
	}
// RAVEN END

	for( i = ( clearClients ? 0 : MAX_CLIENTS ); i < MAX_GENTITIES; i++ ) {
		if( instance >= 0 && entities[ i ] && entities[ i ]->GetInstance() != instance ) {
			continue;
		}

		delete entities[ i ];
		// ~idEntity is in charge of setting the pointer to NULL
		// it will also clear pending events for this entity
		assert( !entities[ i ] );
// RAVEN BEGIN
// see FIXME in idRestoreGame::Error
		entities[ i ] = NULL;
// RAVEN END
		spawnIds[ i ] = -1;
	}

	entityHash.Clear( 1024, MAX_GENTITIES );
// RAVEN BEGIN
// rjohnson: reset spawnedEntities during clear to ensure no left over pieces that get remapped to a new id ( causing bad snapshot reading )
	if ( instance == -1 ) {
		spawnedEntities.Clear();
	}
// RAVEN END
	presentationEntities.Clear();
	presentationClientEntities.Clear();

	if ( !clearClients ) {
		// add back the hashes of the clients/stuff in other instances
		for ( i = 0; i < MAX_GENTITIES; i++ ) {
			if ( !entities[ i ] ) {
				continue;
			}
			entityHash.Add( entityHash.GenerateKey( entities[ i ]->name.c_str(), true ), i );
// RAVEN BEGIN
// rjohnson: reset spawnedEntities during clear to ensure no left over pieces that get remapped to a new id ( causing bad snapshot reading )
			if ( instance == -1 ) {
				entities[ i ]->spawnNode.AddToEnd( spawnedEntities );
			}
// RAVEN END
		}
	}

// RAVEN BEGIN
// jscott: clear out portal skies
	portalSky = NULL;
// abahr:
	gravityInfo.Clear();
	scriptObjectProxies.Clear();
// RAVEN END

	delete frameCommandThread;
	frameCommandThread = NULL;

	if ( editEntities ) {
		delete editEntities;
		editEntities = NULL;
	}

	delete[] locationEntities;
	locationEntities = NULL;
	
// RAVEN BEGIN
// ddynerman: mp clear
	if( gameLocal.isMultiplayer ) {
		ClearForwardSpawns();
		for( i = 0; i < TEAM_MAX; i++ ) {
			teamSpawnSpots[ i ].Clear();
		}
		mpGame.ClearMap();
	}
	ambientLights.Clear();
// RAVEN END

	// set the free index back at MAX_CLIENTS for registering entities again
	// the deletion of map entities causes the index to go down, but it may not have been left exactly at 32
	// under such conditions, the map populate that will follow may be offset
	firstFreeIndex = MAX_CLIENTS;
}

// RAVEN BEGIN
// ddynerman: instance-specific clear
void idGameLocal::InstanceClear( void ) {
	// note: clears all ents EXCEPT those in the instance
	int i;

	for( i = 0; i < MAX_CENTITIES; i++ ) {
		delete clientEntities[ i ];
		assert( !clientEntities[ i ] );
		clientSpawnIds[ i ] = -1;
	}

	for( i = MAX_CLIENTS; i < MAX_GENTITIES; i++ ) {
		if( i == ENTITYNUM_CLIENT || i == ENTITYNUM_WORLD || i == ENTITYNUM_NONE ) {
			continue;
		}

		if( entities[ i ] && entities[ i ]->fl.persistAcrossInstances ) {
			//common->DPrintf( "Instance %d: persistant: excluding ent from clear: %s (%s)\n", instance, entities[ i ]->name.c_str(), entities[ i ]->GetClassname() );
			continue;
		}

		//if( entities[ i ] ) {
			//Printf( "Instance %d: CLEARING ent from clear: %s (%s)\n", instance, entities[ i ]->name.c_str(), entities[ i ]->GetClassname() );
		//}
		
		delete entities[ i ];
		// ~idEntity is in charge of setting the pointer to NULL
		// it will also clear pending events for this entity
		assert( !entities[ i ] );
		// RAVEN BEGIN
		// see FIXME in idRestoreGame::Error
		entities[ i ] = NULL;
		// RAVEN END
		spawnIds[ i ] = -1;
	}

	entityHash.Clear( 1024, MAX_GENTITIES );

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( !entities[ i ] ) {
			continue;
		}
		entityHash.Add( entityHash.GenerateKey( entities[ i ]->name.c_str(), true ), i );
	}

// RAVEN BEGIN
// jscott: clear out portal skies
	portalSky = NULL;
	// abahr:
	gravityInfo.Clear();
	scriptObjectProxies.Clear();
// RAVEN END

	delete frameCommandThread;
	frameCommandThread = NULL;

	if ( editEntities ) {
		delete editEntities;
		editEntities = NULL;
	}

	delete[] locationEntities;
	locationEntities = NULL;

	// RAVEN BEGIN
	// ddynerman: mp clear
	if( gameLocal.isMultiplayer ) {
		ClearForwardSpawns();
		for( i = 0; i < TEAM_MAX; i++ ) {
			teamSpawnSpots[ i ].Clear();
		}
		mpGame.ClearMap();
	}
	ambientLights.Clear();

	nextLagoCheck = 0;
}
// RAVEN END

/*
===========
idGameLocal::MapShutdown
============
*/
void idGameLocal::MapShutdown( void ) {
	idSaveGame::ClearDictionaryCache();
	Printf( "------------ Game Map Shutdown --------------\n" );

	mpGame.PrepareForMapShutdown();
	gamestate = GAMESTATE_SHUTDOWN;
	idRestoreGame::PrepareMapShutdown();

	// The navmesh describes the map that is going away.
	botManager.OnMapShutdown();

	if ( soundSystem ) {
		//soundSystem->ResetListener();
	}

// RAVEN BEGIN
// rjohnson: new blur special effect
	renderSystem->ShutdownSpecialEffects();
// RAVEN END

	// clear out camera if we're in a cinematic
	if ( inCinematic ) {
		camera = NULL;
		inCinematic = false;
	}

// RAVEN BEGIN
// jscott: cleanup playbacks
	gameEdit->ShutdownPlaybacks();
// RAVEN END

	MapClear( true );

	idRestoreGame::DeleteOrphanedObjects();

	instancesEntityIndexWatermarks.Clear();
	clientInstanceFirstFreeIndex = MAX_CLIENTS;	

// RAVEN BEGIN
// jscott: make sure any spurious events are killed
	idEvent::ClearEventList();

	// reset the script to the state it was before the map was started
	program.Restart();

// bdube: game debug
	gameDebug.Shutdown( );
	gameLogLocal.Shutdown( );
// RAVEN END

	iconManager->Shutdown();

	pvs.Shutdown();

// RAVEN BEGIN
// ddynerman: MP multiple instances
	ShutdownInstances();
// mwhitlock: Dynamic memory consolidation
	clip.Clear();
// RAVEN END
	idClipModel::ClearTraceModelCache();

// RAVEN BEGIN
// mwhitlock: Dynamic memory consolidation
#if defined(_RV_MEM_SYS_SUPPORT)
	idForce::ClearForceList();
#endif
// RAVEN END

	ShutdownAsyncNetwork();

	// Drop this map's collision-model reference before clearing the name.  Level
	// transitions reuse the same game instance, so waiting for full game shutdown
	// leaves previous maps pinned in the fixed collision-model table.
	collisionModelManager->FreeMap( GetMapName() );
	collisionModelManager->PurgeModels();

	mapFileName.Clear();

	gameRenderWorld = NULL;

	gamestate = GAMESTATE_NOMAP;

	camera = NULL;
	portalSky = NULL;
	
	Printf( "---------------------------------------------\n" );
}

/*
===================
idGameLocal::DumpOggSounds
===================
*/
void idGameLocal::DumpOggSounds( void ) {
	int i, j, k, size, totalSize;
	idFile *file;
	idStrList oggSounds, weaponSounds;
	const idSoundShader *soundShader;
	const soundShaderParms_t *parms;
	idStr soundName;

	for ( i = 0; i < declManager->GetNumDecls( DECL_SOUND ); i++ ) {
		soundShader = static_cast<const idSoundShader *>(declManager->DeclByIndex( DECL_SOUND, i, false ));
		parms = soundShader->GetParms();

		if ( soundShader->EverReferenced() && soundShader->GetState() != DS_DEFAULTED ) {

			const_cast<idSoundShader *>(soundShader)->EnsureNotPurged();

			for ( j = 0; j < soundShader->GetNumSounds(); j++ ) {
				soundName = soundShader->GetSound( j );
				soundName.BackSlashesToSlashes();

				// don't OGG sounds that cause a shake because that would
				// cause continuous seeking on the OGG file which is expensive
				if ( parms->shakes != 0.0f ) {
					shakeSounds.AddUnique( soundName );
					continue;
				}

				// if not voice over or combat chatter
				if (	soundName.Find( "/vo/", false ) == -1 &&
						soundName.Find( "/combat_chatter/", false ) == -1 &&
						soundName.Find( "/bfgcarnage/", false ) == -1 &&
						soundName.Find( "/enpro/", false ) == - 1 &&
						soundName.Find( "/soulcube/energize_01.wav", false ) == -1 ) {
					// don't OGG weapon sounds
					if (	soundName.Find( "weapon", false ) != -1 ||
							soundName.Find( "gun", false ) != -1 ||
							soundName.Find( "bullet", false ) != -1 ||
							soundName.Find( "bfg", false ) != -1 ||
							soundName.Find( "plasma", false ) != -1 ) {
						weaponSounds.AddUnique( soundName );
						continue;
					}
				}

				for ( k = 0; k < shakeSounds.Num(); k++ ) {
					if ( shakeSounds[k].IcmpPath( soundName ) == 0 ) {
						break;
					}
				}
				if ( k < shakeSounds.Num() ) {
					continue;
				}

				oggSounds.AddUnique( soundName );
			}
		}
	}

	file = fileSystem->OpenFileWrite( "makeogg.bat", "fs_savepath" );
	if ( file == NULL ) {
		common->Warning( "Couldn't open makeogg.bat" );
		return;
	}

	// list all the shake sounds
	totalSize = 0;
	for ( i = 0; i < shakeSounds.Num(); i++ ) {
		size = fileSystem->ReadFile( shakeSounds[i], NULL, NULL );
		totalSize += size;
		shakeSounds[i].Replace( "/", "\\" );
		file->Printf( "echo \"%s\" (%d kB)\n", shakeSounds[i].c_str(), size >> 10 );
	}
	file->Printf( "echo %d kB in shake sounds\n\n\n", totalSize >> 10 );

	// list all the weapon sounds
	totalSize = 0;
	for ( i = 0; i < weaponSounds.Num(); i++ ) {
		size = fileSystem->ReadFile( weaponSounds[i], NULL, NULL );
		totalSize += size;
		weaponSounds[i].Replace( "/", "\\" );
		file->Printf( "echo \"%s\" (%d kB)\n", weaponSounds[i].c_str(), size >> 10 );
	}
	file->Printf( "echo %d kB in weapon sounds\n\n\n", totalSize >> 10 );

	// list commands to convert all other sounds to ogg
	totalSize = 0;
	for ( i = 0; i < oggSounds.Num(); i++ ) {
// RAVEN BEGIN
// rjohnson: changed path to raven's directories
// jnewquist: Use filesystem search path to get absolute file path
		idStr	tempFile;
		idFile *f = fileSystem->OpenFileRead( oggSounds[i] );
		if ( !f ) {
			continue;
		}
		size = f->Length();
		totalSize += size;
		tempFile = f->GetFullPath();
		fileSystem->CloseFile(f);
		f = NULL;
		tempFile.Replace( "/", "\\" );

// jnewquist: prevent alterations to files relative to cdpath
		const char *cdPath = cvarSystem->GetCVarString("fs_cdpath");
		const int cdPathLen = idStr::Length(cdPath);
		if ( cdPathLen > 0 && idStr::Icmpn(cdPath, tempFile, cdPathLen) == 0 ) {
			file->Printf( "rem Ignored file from CD path: %s\n", tempFile.c_str() );
			continue;
		}

		file->Printf( "echo %d / %d\n", i, oggSounds.Num() );
		file->Printf( "k:\\utility\\oggenc -q 0 \"%s\"\n", tempFile.c_str() );
		file->Printf( "del \"%s\"\n", tempFile.c_str() );
// RAVEN END
	}
	file->Printf( "\n\necho %d kB in OGG sounds\n\n\n", totalSize >> 10 );

	fileSystem->CloseFile( file );

	shakeSounds.Clear();
}

/*
===================
idGameLocal::GetShakeSounds
===================
*/
void idGameLocal::GetShakeSounds( const idDict *dict ) {
	const idSoundShader *soundShader;
	const char *soundShaderName;
	idStr soundName;

	soundShaderName = dict->GetString( "s_shader" );
	if ( soundShaderName[0] != '\0' && dict->GetFloat( "s_shakes" ) != 0.0f ) {
		soundShader = declManager->FindSound( soundShaderName );

		for ( int i = 0; i < soundShader->GetNumSounds(); i++ ) {
			soundName = soundShader->GetSound( i );
			soundName.BackSlashesToSlashes();

			shakeSounds.AddUnique( soundName );
		}
	}
}

/*
===================
idGameLocal::BeginMapMediaPrecache
===================
*/
enum openQ4MapMediaProfileKind_t {
	MAP_MEDIA_MODEL,
	MAP_MEDIA_SOUND,
	MAP_MEDIA_GUI,
	MAP_MEDIA_MATERIAL,
	MAP_MEDIA_EFFECT,
	MAP_MEDIA_SKIN,
	MAP_MEDIA_DEF,
	MAP_MEDIA_ANIMATION,
	MAP_MEDIA_ICON,
	MAP_MEDIA_PROFILE_KIND_COUNT
};

static bool openQ4ProfileMapMediaPrecache;
static int openQ4MapMediaPrecacheCounts[ MAP_MEDIA_PROFILE_KIND_COUNT ];
static double openQ4MapMediaPrecacheClockTicks[ MAP_MEDIA_PROFILE_KIND_COUNT ];
static int openQ4MapMediaModelDefCount;
static int openQ4MapMediaRenderModelCount;
static int openQ4MapMediaCollisionModelCount;
static double openQ4MapMediaModelDefClockTicks;
static double openQ4MapMediaRenderModelClockTicks;
static double openQ4MapMediaCollisionModelClockTicks;

static double OpenQ4_BeginMapMediaPrecacheCall( void ) {
	return openQ4ProfileMapMediaPrecache ? idLib::sys->GetClockTicks() : 0.0;
}

static void OpenQ4_EndMapMediaPrecacheCall( openQ4MapMediaProfileKind_t kind, double startClockTicks ) {
	if ( !openQ4ProfileMapMediaPrecache ) {
		return;
	}
	openQ4MapMediaPrecacheCounts[ kind ]++;
	openQ4MapMediaPrecacheClockTicks[ kind ] += idLib::sys->GetClockTicks() - startClockTicks;
}

void idGameLocal::BeginMapMediaPrecache( void ) {
	mapMediaPrecacheSeen.Clear();
	mapMediaPrecacheRequests = 0;
	mapMediaPrecacheSkips = 0;
	mapMediaPrecacheActive = !cvarSystem->GetCVarBool( "com_makingBuild" );
	openQ4ProfileMapMediaPrecache = cvarSystem->GetCVarBool( "com_showLevelLoadTimes" );
	memset( openQ4MapMediaPrecacheCounts, 0, sizeof( openQ4MapMediaPrecacheCounts ) );
	memset( openQ4MapMediaPrecacheClockTicks, 0, sizeof( openQ4MapMediaPrecacheClockTicks ) );
	openQ4MapMediaModelDefCount = 0;
	openQ4MapMediaRenderModelCount = 0;
	openQ4MapMediaCollisionModelCount = 0;
	openQ4MapMediaModelDefClockTicks = 0.0;
	openQ4MapMediaRenderModelClockTicks = 0.0;
	openQ4MapMediaCollisionModelClockTicks = 0.0;
}

/*
===================
idGameLocal::EndMapMediaPrecache
===================
*/
void idGameLocal::EndMapMediaPrecache( void ) {
	if ( mapMediaPrecacheActive && cvarSystem->GetCVarBool( "com_showLevelLoadTimes" ) && mapMediaPrecacheRequests > 0 ) {
		Printf( "Game media precache dedup: skipped=%d unique=%d requests=%d\n",
			mapMediaPrecacheSkips,
			mapMediaPrecacheSeen.Num(),
			mapMediaPrecacheRequests );
		const double ticksPerMillisecond = idLib::sys->ClockTicksPerSecond() * 0.001;
		Printf(
			"Game media precache phases: model=%.1f/%d sound=%.1f/%d gui=%.1f/%d material=%.1f/%d effect=%.1f/%d skin=%.1f/%d defInclusive=%.1f/%d animation=%.1f/%d icon=%.1f/%d msec/items\n",
			openQ4MapMediaPrecacheClockTicks[ MAP_MEDIA_MODEL ] / ticksPerMillisecond, openQ4MapMediaPrecacheCounts[ MAP_MEDIA_MODEL ],
			openQ4MapMediaPrecacheClockTicks[ MAP_MEDIA_SOUND ] / ticksPerMillisecond, openQ4MapMediaPrecacheCounts[ MAP_MEDIA_SOUND ],
			openQ4MapMediaPrecacheClockTicks[ MAP_MEDIA_GUI ] / ticksPerMillisecond, openQ4MapMediaPrecacheCounts[ MAP_MEDIA_GUI ],
			openQ4MapMediaPrecacheClockTicks[ MAP_MEDIA_MATERIAL ] / ticksPerMillisecond, openQ4MapMediaPrecacheCounts[ MAP_MEDIA_MATERIAL ],
			openQ4MapMediaPrecacheClockTicks[ MAP_MEDIA_EFFECT ] / ticksPerMillisecond, openQ4MapMediaPrecacheCounts[ MAP_MEDIA_EFFECT ],
			openQ4MapMediaPrecacheClockTicks[ MAP_MEDIA_SKIN ] / ticksPerMillisecond, openQ4MapMediaPrecacheCounts[ MAP_MEDIA_SKIN ],
			openQ4MapMediaPrecacheClockTicks[ MAP_MEDIA_DEF ] / ticksPerMillisecond, openQ4MapMediaPrecacheCounts[ MAP_MEDIA_DEF ],
			openQ4MapMediaPrecacheClockTicks[ MAP_MEDIA_ANIMATION ] / ticksPerMillisecond, openQ4MapMediaPrecacheCounts[ MAP_MEDIA_ANIMATION ],
			openQ4MapMediaPrecacheClockTicks[ MAP_MEDIA_ICON ] / ticksPerMillisecond, openQ4MapMediaPrecacheCounts[ MAP_MEDIA_ICON ] );
		Printf(
			"Game model precache phases: modelDef=%.1f/%d renderModel=%.1f/%d collisionModel=%.1f/%d msec/items\n",
			openQ4MapMediaModelDefClockTicks / ticksPerMillisecond, openQ4MapMediaModelDefCount,
			openQ4MapMediaRenderModelClockTicks / ticksPerMillisecond, openQ4MapMediaRenderModelCount,
			openQ4MapMediaCollisionModelClockTicks / ticksPerMillisecond, openQ4MapMediaCollisionModelCount );
	}
	mapMediaPrecacheActive = false;
	openQ4ProfileMapMediaPrecache = false;
	mapMediaPrecacheSeen.Clear();
}

/*
===================
idGameLocal::RememberMapMediaPrecache
===================
*/
bool idGameLocal::RememberMapMediaPrecache( const char *kind, const char *value ) {
	if ( !mapMediaPrecacheActive || value == NULL || value[0] == '\0' ) {
		return false;
	}

	mapMediaPrecacheRequests++;

	const char *kindText = kind != NULL ? kind : "";
	const int kindLength = (int)strlen( kindText );
	const int valueLength = (int)strlen( value );
	const int keyLength = kindLength + 1 + valueLength;

	char keyBuffer[ MAX_STRING_CHARS ];
	idStr longKey;
	const char *key;
	if ( keyLength < (int)sizeof( keyBuffer ) ) {
		int keyIndex = 0;
		for ( int i = 0; i < kindLength; i++ ) {
			keyBuffer[ keyIndex++ ] = idStr::ToLower( (byte)kindText[ i ] );
		}
		keyBuffer[ keyIndex++ ] = ':';
		for ( int i = 0; i < valueLength; i++ ) {
			const char c = value[ i ] == '\\' ? '/' : value[ i ];
			keyBuffer[ keyIndex++ ] = idStr::ToLower( (byte)c );
		}
		keyBuffer[ keyIndex ] = '\0';
		key = keyBuffer;
	} else {
		longKey = kindText;
		longKey += ":";
		longKey += value;
		longKey.BackSlashesToSlashes();
		longKey.ToLower();
		key = longKey.c_str();
	}

	if ( mapMediaPrecacheSeen.Get( key ) ) {
		mapMediaPrecacheSkips++;
		return true;
	}

	int remembered = 1;
	mapMediaPrecacheSeen.Set( key, remembered );
	return false;
}

/*
===================
idGameLocal::CacheDictionaryMedia

This is called after parsing an EntityDef and for each entity spawnArgs before
merging the entitydef.  It could be done post-merge, but that would
avoid the fast pre-cache check associated with each entityDef
===================
*/
void idGameLocal::CacheDictionaryMedia( const idDict *dict ) {
	idDict spawnerArgs;
	
	TIME_THIS_SCOPE( __FUNCLINE__);

	if ( dict == NULL ) {
#ifndef _CONSOLE
		if ( cvarSystem->GetCVarBool( "com_makingBuild") ) {
			DumpOggSounds();
		}
#endif
		return;
	}

#ifndef _CONSOLE
	if ( cvarSystem->GetCVarBool( "com_makingBuild" ) ) {
		GetShakeSounds( dict );
	}
#endif

	int numVals = dict->GetNumKeyVals();
	
	for ( int i = 0; i < numVals; ++i ) {
		const idKeyValue *kv = dict->GetKeyVal( i );
		
		#define MATCH(s) \
			(!kv->GetKey().Icmpn( s, sizeof( s ) - 1 ))
		/**/
		
		if ( !kv || !kv->GetValue().Length() ) {
			continue;
		}
		
		if ( MATCH( "model" ) ) {
			
			TIME_THIS_SCOPE( __FUNCLINE__);
			
			MEM_SCOPED_TAG(tag,MA_MODEL);
			if ( RememberMapMediaPrecache( "model", kv->GetValue() ) ) {
				continue;
			}
			const double mediaProfileStart = OpenQ4_BeginMapMediaPrecacheCall();
			declManager->MediaPrint( "Precaching model %s\n", kv->GetValue().c_str() );
			// precache model/animations
			const double modelDefProfileStart = OpenQ4_BeginMapMediaPrecacheCall();
			const idDecl *modelDef = declManager->FindType( DECL_MODELDEF, kv->GetValue(), false );
			if ( openQ4ProfileMapMediaPrecache ) {
				openQ4MapMediaModelDefCount++;
				openQ4MapMediaModelDefClockTicks += idLib::sys->GetClockTicks() - modelDefProfileStart;
			}
			if ( modelDef == NULL ) {
				// precache the render model
				const double renderModelProfileStart = OpenQ4_BeginMapMediaPrecacheCall();
				renderModelManager->FindModel( kv->GetValue() );
				if ( openQ4ProfileMapMediaPrecache ) {
					openQ4MapMediaRenderModelCount++;
					openQ4MapMediaRenderModelClockTicks += idLib::sys->GetClockTicks() - renderModelProfileStart;
				}
				// precache .cm files only
				const double collisionModelProfileStart = OpenQ4_BeginMapMediaPrecacheCall();
				collisionModelManager->PreCacheModel( GetMapName(), kv->GetValue() );
				if ( openQ4ProfileMapMediaPrecache ) {
					openQ4MapMediaCollisionModelCount++;
					openQ4MapMediaCollisionModelClockTicks += idLib::sys->GetClockTicks() - collisionModelProfileStart;
				}
			}
			OpenQ4_EndMapMediaPrecacheCall( MAP_MEDIA_MODEL, mediaProfileStart );
		} else if ( MATCH( "s_shader" ) ) {
			
			TIME_THIS_SCOPE( __FUNCLINE__);
			
			MEM_SCOPED_TAG(tag,MA_SOUND);
			if ( RememberMapMediaPrecache( "sound", kv->GetValue() ) ) {
				continue;
			}
			const double mediaProfileStart = OpenQ4_BeginMapMediaPrecacheCall();
			declManager->FindType( DECL_SOUND, kv->GetValue() );
			OpenQ4_EndMapMediaPrecacheCall( MAP_MEDIA_SOUND, mediaProfileStart );
		} else if ( MATCH( "snd_" ) ) {
			
			TIME_THIS_SCOPE( __FUNCLINE__);
			
			MEM_SCOPED_TAG(tag,MA_SOUND);
			if ( RememberMapMediaPrecache( "sound", kv->GetValue() ) ) {
				continue;
			}
			const double mediaProfileStart = OpenQ4_BeginMapMediaPrecacheCall();
			declManager->FindType( DECL_SOUND, kv->GetValue() );
			OpenQ4_EndMapMediaPrecacheCall( MAP_MEDIA_SOUND, mediaProfileStart );
		} else if ( MATCH( "gui_" ) ) {
			
			TIME_THIS_SCOPE( __FUNCLINE__);
			
			MEM_SCOPED_TAG(tag,MA_GUI);
			if ( !idStr::Icmp( kv->GetKey(), "gui_noninteractive" )
				|| !idStr::Icmpn( kv->GetKey(), "gui_parm", 8 )	
				|| !idStr::Icmp( kv->GetKey(), "gui_inventory" ) ) {
				// unfortunate flag names, they aren't actually a gui
			} else {
				if ( RememberMapMediaPrecache( "gui", kv->GetValue() ) ) {
					continue;
				}
				const double mediaProfileStart = OpenQ4_BeginMapMediaPrecacheCall();
				declManager->MediaPrint( "Precaching gui %s\n", kv->GetValue().c_str() );
				// Media discovery must not orphan interactive/menu GUI instances.
				uiManager->FindGui( kv->GetValue().c_str(), true, false, true );
				OpenQ4_EndMapMediaPrecacheCall( MAP_MEDIA_GUI, mediaProfileStart );
			}
		} else if ( MATCH( "texture" ) ) {
			
			TIME_THIS_SCOPE( __FUNCLINE__);
			
			MEM_SCOPED_TAG(tag,MA_MATERIAL);
			if ( RememberMapMediaPrecache( "material", kv->GetValue() ) ) {
				continue;
			}
			const double mediaProfileStart = OpenQ4_BeginMapMediaPrecacheCall();
			declManager->FindType( DECL_MATERIAL, kv->GetValue() );
			OpenQ4_EndMapMediaPrecacheCall( MAP_MEDIA_MATERIAL, mediaProfileStart );
		} else if ( MATCH( "mtr_" ) ) {
			
			TIME_THIS_SCOPE( __FUNCLINE__);
			
			MEM_SCOPED_TAG(tag,MA_MATERIAL);
			if ( RememberMapMediaPrecache( "material", kv->GetValue() ) ) {
				continue;
			}
			const double mediaProfileStart = OpenQ4_BeginMapMediaPrecacheCall();
			declManager->FindType( DECL_MATERIAL, kv->GetValue() );
			OpenQ4_EndMapMediaPrecacheCall( MAP_MEDIA_MATERIAL, mediaProfileStart );
		} else if ( MATCH( "screenShot" ) ) {
		
			TIME_THIS_SCOPE( __FUNCLINE__);
			
			MEM_SCOPED_TAG(tag,MA_MATERIAL);
			if ( RememberMapMediaPrecache( "material", kv->GetValue() ) ) {
				continue;
			}
			const double mediaProfileStart = OpenQ4_BeginMapMediaPrecacheCall();
			declManager->FindType( DECL_MATERIAL, kv->GetValue() );
			OpenQ4_EndMapMediaPrecacheCall( MAP_MEDIA_MATERIAL, mediaProfileStart );
		} else if ( MATCH( "inv_icon" ) ) {
			
			TIME_THIS_SCOPE( __FUNCLINE__);
			
			MEM_SCOPED_TAG(tag,MA_MATERIAL);
			if ( RememberMapMediaPrecache( "material", kv->GetValue() ) ) {
				continue;
			}
			const double mediaProfileStart = OpenQ4_BeginMapMediaPrecacheCall();
			declManager->FindType( DECL_MATERIAL, kv->GetValue() );
			OpenQ4_EndMapMediaPrecacheCall( MAP_MEDIA_MATERIAL, mediaProfileStart );
		} else if ( MATCH( "teleport" ) ) {
			
			TIME_THIS_SCOPE( __FUNCLINE__);
			
			MEM_SCOPED_TAG(tag,MA_EFFECT);
			int teleportType = atoi( kv->GetValue() );
			const char *p = ( teleportType ) ? va( "effects/teleporter%i.fx", teleportType ) : "effects/teleporter.fx";
			if ( RememberMapMediaPrecache( "effect", p ) ) {
				continue;
			}
			const double mediaProfileStart = OpenQ4_BeginMapMediaPrecacheCall();
			declManager->FindType( DECL_EFFECT, p );
			OpenQ4_EndMapMediaPrecacheCall( MAP_MEDIA_EFFECT, mediaProfileStart );
		} else if ( MATCH( "fx_" ) ) {
			
			TIME_THIS_SCOPE( __FUNCLINE__);
			
			MEM_SCOPED_TAG(tag,MA_EFFECT);
			if ( RememberMapMediaPrecache( "effect", kv->GetValue() ) ) {
				continue;
			}
			const double mediaProfileStart = OpenQ4_BeginMapMediaPrecacheCall();
			declManager->MediaPrint( "Precaching fx %s\n", kv->GetValue().c_str() );
			declManager->FindEffect( kv->GetValue() );
			OpenQ4_EndMapMediaPrecacheCall( MAP_MEDIA_EFFECT, mediaProfileStart );
		} else if ( MATCH( "skin" ) ) {
			
			TIME_THIS_SCOPE( __FUNCLINE__);
			
			MEM_SCOPED_TAG(tag,MA_MATERIAL);
			if ( RememberMapMediaPrecache( "skin", kv->GetValue() ) ) {
				continue;
			}
			const double mediaProfileStart = OpenQ4_BeginMapMediaPrecacheCall();
			declManager->MediaPrint( "Precaching skin %s\n", kv->GetValue().c_str() );
			declManager->FindType( DECL_SKIN, kv->GetValue() );
			OpenQ4_EndMapMediaPrecacheCall( MAP_MEDIA_SKIN, mediaProfileStart );
		} else if ( MATCH( "def_" ) ) {
			
			TIME_THIS_SCOPE( __FUNCLINE__);
			
			MEM_SCOPED_TAG(tag,MA_DECL);
			if ( RememberMapMediaPrecache( "def", kv->GetValue() ) ) {
				continue;
			}
			const double mediaProfileStart = OpenQ4_BeginMapMediaPrecacheCall();
			FindEntityDef( kv->GetValue().c_str(), false );
			OpenQ4_EndMapMediaPrecacheCall( MAP_MEDIA_DEF, mediaProfileStart );
		} else if ( MATCH( "playback_" ) ) {
			
			TIME_THIS_SCOPE( __FUNCLINE__);
			
			MEM_SCOPED_TAG(tag,MA_ANIM);
			if ( RememberMapMediaPrecache( "playback", kv->GetValue() ) ) {
				continue;
			}
			const double mediaProfileStart = OpenQ4_BeginMapMediaPrecacheCall();
			declManager->MediaPrint( "Precaching playback %s\n", kv->GetValue().c_str() );
			declManager->FindPlayback( kv->GetValue() );
			OpenQ4_EndMapMediaPrecacheCall( MAP_MEDIA_ANIMATION, mediaProfileStart );
		} else if ( MATCH( "lipsync_" ) ) {
			
			TIME_THIS_SCOPE( __FUNCLINE__);
			
			MEM_SCOPED_TAG(tag,MA_ANIM);
			if ( RememberMapMediaPrecache( "lipsync", kv->GetValue() ) ) {
				continue;
			}
			const double mediaProfileStart = OpenQ4_BeginMapMediaPrecacheCall();
			declManager->MediaPrint( "Precaching lipsync %s\n", kv->GetValue().c_str() );
			declManager->FindLipSync( kv->GetValue() );
			declManager->FindSound ( kv->GetValue() );
			OpenQ4_EndMapMediaPrecacheCall( MAP_MEDIA_ANIMATION, mediaProfileStart );
		} else if ( MATCH( "icon " ) ) {
			
			TIME_THIS_SCOPE( __FUNCLINE__);
			
			MEM_SCOPED_TAG(tag,MA_MATERIAL);
			idStr iconPrecacheKey = kv->GetKey();
			iconPrecacheKey += "=";
			iconPrecacheKey += kv->GetValue();
			if ( RememberMapMediaPrecache( "icon", iconPrecacheKey.c_str() ) ) {
				continue;
			}
			const double mediaProfileStart = OpenQ4_BeginMapMediaPrecacheCall();
			idLexer  src ( LEXFL_ALLOWPATHNAMES );
			idToken  token;
			idToken	 token2;
			src.LoadMemory( kv->GetValue(), kv->GetValue().Length(), "icon" );
			
			src.ReadToken ( &token );
			if ( src.CheckTokenString ( "," ) ) {
				int x, y, w, h;
				src.ReadToken ( &token2 ) ;
				x = token2.GetIntValue ( );
				src.ExpectTokenString ( "," );
				
				src.ReadToken ( &token2 ) ;
				y = token2.GetIntValue ( );
				src.ExpectTokenString ( "," );

				src.ReadToken ( &token2 ) ;
				w = token2.GetIntValue ( );
				src.ExpectTokenString ( "," );

				src.ReadToken ( &token2 ) ;
				h = token2.GetIntValue ( );
				
				uiManager->RegisterIcon ( kv->GetKey ( ).c_str() + 5, token, x, y, w, h );
			} else { 
				uiManager->RegisterIcon ( kv->GetKey ( ).c_str() + 5, token );
			}
			OpenQ4_EndMapMediaPrecacheCall( MAP_MEDIA_ICON, mediaProfileStart );
		} else if ( MATCH( "spawn_" ) ) {
			
			TIME_THIS_SCOPE( __FUNCLINE__);
			
			MEM_SCOPED_TAG(tag,MA_DECL);
			spawnerArgs.Set ( kv->GetKey ( ).c_str() + 6, kv->GetValue ( ) );
		}
		
		#undef MATCH
	}

	if ( spawnerArgs.GetNumKeyVals() ) {
		CacheDictionaryMedia ( &spawnerArgs );
	}
// RAVEN END
}

/*
===========
idGameLocal::InitScriptForMap
============
*/
void idGameLocal::InitScriptForMap( void ) {

// RAVEN BEGIN
// jnewquist: Tag scope and callees to track allocations using "new".
	MEM_SCOPED_TAG(tag,MA_DEFAULT);
// RAVEN END
	// create a thread to run frame commands on
	frameCommandThread = new idThread();
	frameCommandThread->ManualDelete();
	frameCommandThread->SetThreadName( "frameCommands" );


// jnewquist: Tag scope and callees to track allocations using "new".
	MEM_SCOPED_TAG_SET(tag,MA_SCRIPT);

	// run the main game script function (not the level specific main)
	const function_t *func = program.FindFunction( SCRIPT_DEFAULTFUNC );


// jnewquist: Tag scope and callees to track allocations using "new".
	MEM_SCOPED_TAG_SET(tag,MA_DEFAULT);

	if ( func != NULL ) {
		idThread *thread = new idThread( func );
		if ( thread->Start() ) {
			// thread has finished executing, so delete it
			delete thread;
		}
	}

// RAVEN END

}

/*
===========
idGameLocal::SpawnPlayer
============
*/
void idGameLocal::SpawnPlayer( int clientNum, bool isBot, const char* botName ) {
	TIME_THIS_SCOPE( __FUNCLINE__);

	idEntity	*ent;
	idDict		args;
// RAVEN BEGIN
// jnewquist: Tag scope and callees to track allocations using "new".
	MEM_SCOPED_TAG(tag,MA_ENTITY);
// RAVEN END

	// they can connect
	common->DPrintf( "SpawnPlayer: %i\n", clientNum );

	// Claim the slot before the entity exists: idPlayer::Spawn reads the user
	// info, and a bot with only the engine's bare name in there would decide it
	// was a spectator sitting in the join menu.
	botManager.OnSpawnPlayer( clientNum, isBot, botName );
	if ( isBot && clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		botManager.FillUserInfo( clientNum, userInfo[ clientNum ] );
	}

	args.SetInt( "spawn_entnum", clientNum );
	args.Set( "name", va( "player%d", clientNum + 1 ) );
// RAVEN BEGIN
// bdube: changed marine class
	args.Set( "classname", idPlayer::GetSpawnClassname() );
// RAVEN END
	
	// This takes a really long time.
	PACIFIER_UPDATE;
	if ( !SpawnEntityDef( args, &ent ) || !entities[ clientNum ] ) {
		Error( "Failed to spawn player as '%s'", args.GetString( "classname" ) );
	}

	// make sure it's a compatible class
	if ( !ent->IsType( idPlayer::GetClassType() ) ) {
		Error( "'%s' spawned the player as a '%s'.  Player spawnclass must be a subclass of idPlayer.", args.GetString( "classname" ), ent->GetClassname() );
	}

	PACIFIER_UPDATE;

	if ( clientNum != ENTITYNUM_NONE ) {
		if ( clientNum >= numClients ) {
			numClients = clientNum + 1;
		}
	}

	mpGame.SpawnPlayer( clientNum );
}

/*
================
idGameLocal::GetClientByNum
================
*/
idPlayer *idGameLocal::GetClientByNum( int current ) const {
	if ( current == MAX_CLIENTS ) {
		return NULL;
	}
	if ( current == ENTITYNUM_NONE ) {
		return static_cast<idPlayer *>( entities[ current ] );
	}
	if ( current < 0 || current >= numClients ) {
		// that's a bit nasty but I suppose it has it's use
		current = 0;
	}
	if ( entities[current] ) {
		return static_cast<idPlayer *>( entities[ current ] );
	}
	return NULL;
}

/*
================
idGameLocal::GetClientByName
================
*/
idPlayer *idGameLocal::GetClientByName( const char *name ) const {
	int i;
	idEntity *ent;
	for ( i = 0 ; i < numClients ; i++ ) {
		ent = entities[ i ];
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( ent && ent->IsType( idPlayer::GetClassType() ) ) {
// RAVEN END
// RAVEN BEGIN
// bdube: escape codes
			if ( idStr::IcmpNoEscape( name, userInfo[ i ].GetString( "ui_name" ) ) == 0 ) {
// RAVEN END			
				return static_cast<idPlayer *>( ent );
			}
		}
	}
	return NULL;
}

/*
================
idGameLocal::GetClientByCmdArgs
================
*/
idPlayer *idGameLocal::GetClientByCmdArgs( const idCmdArgs &args ) const {
	idPlayer *player;
	idStr client = args.Argv( 1 );
	if ( !client.Length() ) {
		return NULL;
	}
	// we don't allow numeric ui_name so this can't go wrong
	if ( client.IsNumeric() ) {
		player = GetClientByNum( atoi( client.c_str() ) );
	} else {
		player = GetClientByName( client.c_str() );
	}
	if ( !player ) {
		common->Printf( "Player '%s' not found\n", client.c_str() );
	}
	return player;
}

/*
================
idGameLocal::GetClientNumByName
================
*/
int idGameLocal::GetClientNumByName( const char *name ) const {
	int i;
	idEntity *ent;
	for ( i = 0 ; i < numClients ; i++ ) {
		ent = entities[ i ];

		if ( ent && ent->IsType( idPlayer::GetClassType() ) ) {
			if ( idStr::IcmpNoEscape( name, userInfo[ i ].GetString( "ui_name" ) ) == 0 ) {
				return i;
			}
		}
	}
	return -1;
}

/*
================
idGameLocal::GetNextClientNum
================
*/
int idGameLocal::GetNextClientNum( int _current ) const {
	int i, current;

	current = 0;
	for ( i = 0; i < numClients; i++) {
		current = ( _current + i + 1 ) % numClients;
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( entities[ current ] && entities[ current ]->IsType( idPlayer::GetClassType() ) ) {
// RAVEN END
			return current;
		}
	}

	return current;
}

/*
================
idGameLocal::GetLocalPlayer

Nothing in the game tic should EVER make a decision based on what the
local client number is, it shouldn't even be aware that there is a
draw phase even happening.

localClientNum == MAX_CLIENTS when playing server demos
don't send back the wrong entity obviously
================
*/
idPlayer *idGameLocal::GetLocalPlayer() const {
	if ( localClientNum == MAX_CLIENTS ) {
		return static_cast<idPlayer *>( entities[ ENTITYNUM_NONE ] );
	}

	if ( localClientNum < 0 || localClientNum >= MAX_CLIENTS ) {
		return NULL;
	}

	if ( !entities[ localClientNum ] || !entities[ localClientNum ]->IsType( idPlayer::GetClassType() ) ) {
		// not fully in game yet
		return NULL;
	}
	
	return static_cast<idPlayer *>( entities[ localClientNum ] );
}

/*
================
idGameLocal::SetupClientPVS
for client spectating others, get the pvs of spectated
================
*/
pvsHandle_t idGameLocal::GetClientPVS( idPlayer *player, pvsType_t type ) {
	if ( player->GetPrivateCameraView() ) {
		return pvs.SetupCurrentPVS( player->GetPrivateCameraView()->GetPVSAreas(), player->GetPrivateCameraView()->GetNumPVSAreas() );
	} else if ( camera ) {
		return pvs.SetupCurrentPVS( camera->GetPVSAreas(), camera->GetNumPVSAreas() );
	} else {
		if ( player->spectating && player->spectator != player->entityNumber && entities[ player->spectator ] ) {
			player = static_cast<idPlayer*>( entities[ player->spectator ] );
		}
		return pvs.SetupCurrentPVS( player->GetPVSAreas(), player->GetNumPVSAreas() );
	}
}

// RAVEN BEGIN
// jscott: for portal skies
/*
================
idGameLocal::GetSpawnCount
================
*/
int idGameLocal::GetSpawnCount ( void ) const {
	return spawnCount;
}

/*
================
idGameLocal::SetupPortalSkyPVS
================
*/
bool idGameLocal::SetupPortalSkyPVS( idPlayer *player ) {

	int			i, count, numAreas;
	const int	*areaNums;
	bool		*visibleAreas;

	if( !portalSky ) {

		return( false );
	}

	// Allocate room for the area flags
	numAreas = gameRenderWorld->NumAreas();
	visibleAreas = ( bool * )_alloca( numAreas );
	memset( visibleAreas, 0, numAreas );

	// Grab the areas the player can see....
	count = player->GetNumPVSAreas();
	areaNums = player->GetPVSAreas();
	for( i = 0; i < count; i++ ) {

		// Work out the referenced areas
		gameRenderWorld->FindVisibleAreas( player->GetPhysics()->GetOrigin(), areaNums[i], visibleAreas );
	}

	// Do any of the visible areas have a skybox?
	for( i = 0; i < numAreas; i++ ) {

		if( !visibleAreas[i] ) {

			continue;
		}

		if( gameRenderWorld->HasSkybox( i ) ) {

			break;
		}
	}

	// .. if any one has a skybox component, then merge in the portal sky
	return ( i != numAreas );
}
// RAVEN END

/*
===============
idGameLocal::UpdateClientsPVS
===============
*/
void idGameLocal::UpdateClientsPVS( void ) {
	int i;
	for ( i = 0; i < numClients; i++ ) {
		if ( !entities[ i ] ) {
			continue;
		}
		assert( clientsPVS[ i ].i == -1 );
		clientsPVS[ i ] = GetClientPVS( static_cast< idPlayer * >( entities[ i ] ), PVS_NORMAL );
	}
}

/*
================
idGameLocal::SetupPlayerPVS
================
*/
void idGameLocal::SetupPlayerPVS( void ) {
	int			i = 0;
	idPlayer *	player = NULL;
	pvsHandle_t	otherPVS, newPVS;

	UpdateClientsPVS( );

	playerPVS.i = -1;
	for ( i = 0; i < numClients; i++ ) {
		// openQ4: numClients is a high water mark, not a count, so a disconnect leaves holes
		// below it.  Bailing out on the first hole left the merged player PVS empty for the
		// rest of the frame; skip the hole instead, exactly like UpdateClientsPVS does.
		if ( !entities[i] ) {
			continue;
		}
		assert( entities[i]->IsType( idPlayer::GetClassType() ) );

		player = static_cast<idPlayer *>( entities[ i ] );

		if ( playerPVS.i == -1 ) {
			playerPVS = clientsPVS[ i ];
			freePlayerPVS = false;	// don't try to free it as long as we stick to the client PVS
		} else {
			otherPVS = clientsPVS[ i ];
			newPVS = pvs.MergeCurrentPVS( playerPVS, otherPVS );
			if ( freePlayerPVS ) {
				pvs.FreeCurrentPVS( playerPVS );
				freePlayerPVS = false;
			}
			playerPVS = newPVS;
			freePlayerPVS = true;	// that merged one will need to be freed
		}

// RAVEN BEGIN
// jscott: for portal skies
		portalSkyVisible = SetupPortalSkyPVS( player );
		if ( portalSkyVisible ) {

			otherPVS = pvs.SetupCurrentPVS( portalSky->GetPVSAreas(), portalSky->GetNumPVSAreas() );
			newPVS = pvs.MergeCurrentPVS( playerPVS, otherPVS );

			if ( freePlayerPVS ) {
				pvs.FreeCurrentPVS( playerPVS );
				freePlayerPVS = false;
			}
			pvs.FreeCurrentPVS( otherPVS );
			playerPVS = newPVS;
			freePlayerPVS = true;
		}
// RAVEN END

		if ( playerConnectedAreas.i == -1 ) {
			playerConnectedAreas = GetClientPVS( player, PVS_CONNECTED_AREAS );
		} else {
			otherPVS = GetClientPVS( player, PVS_CONNECTED_AREAS );
			newPVS = pvs.MergeCurrentPVS( playerConnectedAreas, otherPVS );
			pvs.FreeCurrentPVS( playerConnectedAreas );
			pvs.FreeCurrentPVS( otherPVS );
			playerConnectedAreas = newPVS;
		}
	}
}

/*
================
idGameLocal::FreePlayerPVS
================
*/
void idGameLocal::FreePlayerPVS( void ) {
	int i;

	// only clear playerPVS if it's a different handle than the one in clientsPVS
	if ( freePlayerPVS && playerPVS.i != -1 ) {
		pvs.FreeCurrentPVS( playerPVS );
		freePlayerPVS = false;
	}
	playerPVS.i = -1;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( clientsPVS[ i ].i >= 0 ) {
			pvs.FreeCurrentPVS( clientsPVS[ i ] );
			clientsPVS[i].i = -1;
		}
	}
	if ( playerConnectedAreas.i != -1 ) {
		pvs.FreeCurrentPVS( playerConnectedAreas );
		playerConnectedAreas.i = -1;
	}
}

/*
================
idGameLocal::InPlayerPVS

  should only be called during entity thinking and event handling
================
*/
bool idGameLocal::InPlayerPVS( idEntity *ent ) const {
	if ( playerPVS.i == -1 ) {
		return false;
	}
    return pvs.InCurrentPVS( playerPVS, ent->GetPVSAreas(), ent->GetNumPVSAreas() );
}

/*
================
idGameLocal::InPlayerConnectedArea

  should only be called during entity thinking and event handling
================
*/
bool idGameLocal::InPlayerConnectedArea( idEntity *ent ) const {
	if ( playerConnectedAreas.i == -1 ) {
		// openQ4: playerConnectedAreas is only ever built by SetupPlayerPVS, which
		// runs from RunFrame - a path a network client never takes.  The one caller
		// that matters on a client is rvClientEffect::Think, which feeds this
		// straight into renderEffect.inConnectedArea; answering "no" there declares
		// every bound effect unreachable from the view.  With no connected-area set
		// to consult, the honest answer on a client is "can't tell", and the only
		// safe reading of that is visible.
		return isClient;
	}
    return pvs.InCurrentPVS( playerConnectedAreas, ent->GetPVSAreas(), ent->GetNumPVSAreas() );
}


/*
================
idGameLocal::UpdateGravity
================
*/
void idGameLocal::UpdateGravity( void ) {
	idEntity *ent;
	
	idCVar* gravityCVar = NULL;

	if( gameLocal.isMultiplayer ) {
		gravityCVar = &g_mp_gravity;
	} else {
		gravityCVar = &g_gravity;
	}

	if ( gravityCVar->IsModified() ) {
		if ( gravityCVar->GetFloat() == 0.0f ) {
			gravityCVar->SetFloat( 1.0f );
		}
        gravity.Set( 0, 0, -gravityCVar->GetFloat() );

		// update all physics objects
		for( ent = spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
			if ( ent->IsType( idAFEntity_Generic::GetClassType() ) ) {
// RAVEN END
				idPhysics *phys = ent->GetPhysics();
				if ( phys ) {
					phys->SetGravity( gravity );
				}
// RAVEN BEGIN
// ddynerman: jump pads
			} else if ( ent->IsType( rvJumpPad::GetClassType() ) ) {
				ent->PostEventMS( &EV_FindTargets, 0 );
			}
// RAVEN END
		}
		gravityCVar->ClearModified();
	}
}

/*
================
idGameLocal::GetGravity
================
*/
const idVec3 &idGameLocal::GetGravity( void ) const {
	return gravity;
}

/*
================
idGameLocal::SortActiveEntityList

  Sorts the active entity list such that pushing entities come first,
  actors come next and physics team slaves appear after their master.
================
*/
void idGameLocal::SortActiveEntityList( void ) {
	idEntity *ent, *next_ent, *master, *part;

	// if the active entity list needs to be reordered to place physics team masters at the front
	if ( sortTeamMasters ) {
		// Sort bind masters first
		for ( ent = activeEntities.Next(); ent != NULL; ent = next_ent ) {
			next_ent = ent->activeNode.Next();
			for ( part = ent->GetBindMaster ( ); part; part = part->GetBindMaster ( ) ) {
				// Ensure we dont rerun the whole active entity list if our cached next_ent is one 
				// of the entities we are moving
				if ( next_ent == part ) {
					next_ent = next_ent->activeNode.Next();
					part = ent->GetBindMaster ( );
					continue;
				}
				part->activeNode.Remove();
				part->activeNode.AddToFront( activeEntities );												
			}
		}				
	
		for ( ent = activeEntities.Next(); ent != NULL; ent = next_ent ) {
			next_ent = ent->activeNode.Next();
			master = ent->GetTeamMaster();
			if ( master && master == ent ) {
				ent->activeNode.Remove();
				ent->activeNode.AddToFront( activeEntities );												
			}
		}
	}

	// if the active entity list needs to be reordered to place pushers at the front
	if ( sortPushers ) {

		for ( ent = activeEntities.Next(); ent != NULL; ent = next_ent ) {
			next_ent = ent->activeNode.Next();
			master = ent->GetTeamMaster();
			if ( !master || master == ent ) {
				// check if there is an actor on the team
				for ( part = ent; part != NULL; part = part->GetNextTeamEntity() ) {
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
					if ( part->GetPhysics()->IsType( idPhysics_Actor::GetClassType() ) ) {
// RAVEN END
						break;
					}
				}
				// if there is an actor on the team
				if ( part ) {
					ent->activeNode.Remove();
					ent->activeNode.AddToFront( activeEntities );
				}
			}
		}

		for ( ent = activeEntities.Next(); ent != NULL; ent = next_ent ) {
			next_ent = ent->activeNode.Next();
			master = ent->GetTeamMaster();
			if ( !master || master == ent ) {
				// check if there is an entity on the team using parametric physics
				for ( part = ent; part != NULL; part = part->GetNextTeamEntity() ) {
					if ( part->GetPhysics()->IsType( idPhysics_Parametric::GetClassType() ) ) {
						break;
					}
					if ( part->GetPhysics()->IsType( rvPhysics_Spline::GetClassType() ) ) {
						break;
					}
				}
				// if there is an entity on the team using parametric physics
				if ( part ) {
					ent->activeNode.Remove();
					ent->activeNode.AddToFront( activeEntities );
				}
			}
		}
	}

	sortTeamMasters = false;
	sortPushers = false;
}

/*
================
idGameLocal::MenuFrame
Called each session frame when a map is not running (e.g. usually in the main menu)
================
*/
void idGameLocal::MenuFrame( void ) { }

/*
================
idGameLocal::RunFrame
================
*/
// RAVEN BEGIN
gameReturn_t idGameLocal::RunFrame( const usercmd_t *clientCmds, int activeEditors, bool lastCatchupFrame, int serverGameFrame ) {
	idEntity *	ent;
	int			num;
	float		ms;
	idTimer		timer_think, timer_events, timer_singlethink;
	idTimer		timer_misc, timer_misc2;
	gameReturn_t ret;
	idPlayer	*player;
	const renderView_t *view;
	bool		competitiveGameplayFrozen = false;
	bool		arenaCeremonyFrozen = false;

	editors = activeEditors;
	isLastPredictFrame = lastCatchupFrame;
	mHz = common->GetUserCmdHz();

	assert( !isClient );

	player = GetLocalPlayer();

	// openQ4: clear the debug lines each server frame, as single player does.
	// A remote client clears them on every snapshot (ClientReadSnapshot), but a
	// listen server reads none, so bot_debugNav and every other server-side
	// draw piled up until the renderer's fixed line pool was full and new lines
	// were silently dropped.
	if ( gameRenderWorld != NULL ) {
		gameRenderWorld->DebugClear( time );
	}

	do {

		previousTime = time;

		// sanity - game frame is counted on both sides of the DLL fence
		// but I've seen cases where it would be offset and we need to make sure that doesn't happen
		if ( serverGameFrame != framenum ) {
			common->Warning( "core and game frame counters are out of sync: %d/%d", serverGameFrame, framenum );
			if ( serverGameFrame > framenum ) {
				int delta = serverGameFrame - framenum;
				time += delta * GetMSec();
				framenum = serverGameFrame;
			} if ( serverGameFrame < framenum ) {
				// don't do anything, let the core catchup to us
				memset( &ret, 0, sizeof( ret ) );
				return ret;
			}
		}
		
		// update the game time
		framenum++;
		// bdube: use GetMSec access rather than USERCMD_TIME
		time += GetMSec();
		msec = time - previousTime;

		realClientTime = time;
		if ( isMultiplayer ) {
			mpGame.BeginCompetitiveFrame();
			// The Arena final tableau freezes the whole world, not just the
			// combatants: projectiles hang, movers stop, effects hold. It reuses
			// the competitive pause's per-entity ThinkMatchPaused path rather than
			// adding a pause kind to mpMatchSession, whose phase invariants exist
			// to serve remote clients an Arena match never has.
			arenaCeremonyFrozen = mpGame.ArenaCampaignFreezesWorld();
			// The single-player Arena also pauses behind its menu. Unlike the
			// tableau, a pause interrupts a live match, so its frames move every
			// match deadline on as well - the clock, rounds, respawns, bots.
			if ( !arenaCeremonyFrozen && mpGame.ArenaCampaignPausesWorld() ) {
				mpGame.RebaseArenaCampaignPauseFrame( Max( 0, msec ) );
				arenaCeremonyFrozen = true;
			}
			competitiveGameplayFrozen = mpGame.IsGameplayFrozen() || arenaCeremonyFrozen;
		}
		{
TIME_THIS_SCOPE("idGameLocal::RunFrame - gameDebug.BeginFrame()");
		// bdube: added advanced debug support
		gameDebug.BeginFrame( );
		gameLogLocal.BeginFrame( time );
		}

#ifdef GAME_DLL
		// allow changing SIMD usage on the fly
		if ( com_forceGenericSIMD.IsModified() ) {
			idSIMD::InitProcessor( "game", com_forceGenericSIMD.GetBool() );
		}
#endif

		// make sure the random number counter is used each frame so random events
		// are influenced by the player's actions
		if ( !competitiveGameplayFrozen ) {
			random.RandomInt();
		}

		if ( player && !( inCinematic && skipCinematic ) ) {
			// update the renderview so that any gui videos play from the right frame
			view = player->GetRenderView();
			if ( view ) {
				gameRenderWorld->SetRenderView( view );
			}
		}

		// If modview is running then let it think
		common->ModViewThink( );	

		// rjohnson: added option for guis to always think
		common->RunAlwaysThinkGUIs( time );

		// nmckenzie: Let AI System stuff update itself.
		if ( !isMultiplayer ) {
#ifndef _MPBETA
			aiManager.RunFrame();
#endif // !_MPBETA
		}

		timer_misc.Start();
		
		// set the user commands for this frame
		usercmds = clientCmds;

		// bots fill in their own slots of that array before anything reads it
		if ( !competitiveGameplayFrozen ) {
			botManager.Think();
		}

		// create a merged pvs for all players
		// do this before we process events, which may rely on PVS info
		SetupPlayerPVS();

		// process events on the server
		if ( !competitiveGameplayFrozen ) {
			ServerProcessEntityNetworkEventQueue();
		}

		// update our gravity vector if needed.
		UpdateGravity();

		if ( isLastPredictFrame && !competitiveGameplayFrozen ) {
			// jscott: effect system uses gravity and the player PVS
			bse->StartFrame();
		}

		// sort the active entity list
		SortActiveEntityList();

		timer_think.Clear();
		timer_think.Start();

		// jscott: for timing and effect handling
		timer_misc2.Start();
		timer_misc.Stop();

		// let entities think
		if ( competitiveGameplayFrozen ) {
			num = 0;
			// Resting/dormant entities may still own absolute gameplay deadlines
			// (dropped items, trigger cooldowns, mover trajectories).  Visit the
			// complete spawned set exactly once rather than only active thinkers.
			for ( ent = spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
				currentThinkingEntity = ent;
				// Everyone, including the viewer. idPlayer::ThinkMatchPaused
				// already consumes the usercmd, zeroes movement and impulses, and
				// calls UpdateViewAngles() - which is where the Arena tableau's
				// free-look orbit is honoured. An earlier version exempted the
				// local player here on the belief that it only shifted deadlines;
				// that was wrong, and it left the viewer's own body animating,
				// their powerups expiring and their corpse dissolving inside a
				// world that had visibly stopped.
				ent->ThinkMatchPaused( msec );
				currentThinkingEntity = NULL;
				num++;
			}
		} else if ( g_timeentities.GetFloat() ) {
			// rjohnson: will now draw entity info for long thinkers
			idPlayer *player;
			idVec3 origin;

			player = GetLocalPlayer();
			if ( player ) {
				origin = player->GetPhysics()->GetOrigin();
			}

			idBounds	viewTextBounds( origin );
			viewTextBounds.ExpandSelf( 128.0f );

			num = 0;

			for( ent = activeEntities.Next(); ent != NULL; ent = ent->activeNode.Next() ) {
				if ( g_cinematic.GetBool() && inCinematic && !ent->cinematic ) {
					ent->GetPhysics()->UpdateTime( time );
					continue;
				}
				timer_singlethink.Clear();
				timer_singlethink.Start();
				// ddynerman: save the current thinking entity for instance-dependent
				currentThinkingEntity = ent;
				ent->Think();
				currentThinkingEntity = NULL;
				timer_singlethink.Stop();
				ms = timer_singlethink.Milliseconds();
				if ( ms >= g_timeentities.GetFloat() ) {
					// rjohnson: will now draw entity info for long thinkers
					Printf( "%d: entity '%s' [%s]: %.1f ms\n", time, ent->name.c_str(), ent->GetPhysics()->GetOrigin().ToString(), ms );

					if ( ms >= g_timeentities.GetFloat() * 3.0f )
					{
						ent->mLastLongThinkColor = colorRed;
					}
					else
					{
						ent->mLastLongThinkColor[0] = 1.0f;
						ent->mLastLongThinkColor[1] = 2.0f - (( ms - g_timeentities.GetFloat()) / g_timeentities.GetFloat() );
						ent->mLastLongThinkColor[2] = 0.0f;
						ent->mLastLongThinkColor[3] = 1.0f;
					}
					ent->DrawDebugEntityInfo( 0, &viewTextBounds, &ent->mLastLongThinkColor );
					ent->mLastLongThinkTime = time + 2000;
				}
				else if ( ent->mLastLongThinkTime > time )
				{
					ent->DrawDebugEntityInfo( 0, &viewTextBounds, &ent->mLastLongThinkColor );
				}
				num++;
			}
		} else {
			if ( inCinematic ) {
				num = 0;
				for( ent = activeEntities.Next(); ent != NULL; ent = ent->activeNode.Next() ) {
					if ( g_cinematic.GetBool() && !ent->cinematic ) {
						ent->GetPhysics()->UpdateTime( time );
						continue;
					}
					// ddynerman: save the current thinking entity for instance-dependent
					currentThinkingEntity = ent;
					ent->Think();
					currentThinkingEntity = NULL;
					num++;
				}
			} else {
				num = 0;
				for( ent = activeEntities.Next(); ent != NULL; ent = ent->activeNode.Next() ) {
					// ddynerman: save the current thinking entity for instance-dependent
					currentThinkingEntity = ent;
					ent->Think();
					currentThinkingEntity = NULL;
					num++;
				}
			}
		}

		// remove any entities that have stopped thinking
		if ( numEntitiesToDeactivate ) {
			idEntity *next_ent;
			int c = 0;
			for( ent = activeEntities.Next(); ent != NULL; ent = next_ent ) {
				next_ent = ent->activeNode.Next();
				if ( !ent->thinkFlags ) {
					ent->activeNode.Remove();
					c++;
				}
			}
			//assert( numEntitiesToDeactivate == c );
			numEntitiesToDeactivate = 0;
		}

		timer_think.Stop();
		timer_events.Clear();
		timer_events.Start();

		if ( isLastPredictFrame && !competitiveGameplayFrozen ) {
			// bdube: client entities
			rvClientEntity* cent;
			for( cent = clientSpawnedEntities.Next(); cent != NULL; cent = cent->spawnNode.Next() ) {
				cent->Think();			
			}	
		}

		// service any pending events
		if ( competitiveGameplayFrozen ) {
			if ( !idEvent::ShiftEvents( msec ) ) {
				Warning( "competitive pause could not rebase the posted-event queue" );
			}
		} else {
			idEvent::ServiceEvents();
		}

		// nrausch: player could have been deleted in an event
		player = GetLocalPlayer();

		timer_events.Stop();

		if ( isLastPredictFrame && !competitiveGameplayFrozen ) {
			// jscott: effect system uses gravity and the player PVS
			bse->EndFrame();
		}

		if ( isLastPredictFrame ) {
			// authoritative transforms are final for this tic; snapshot them so
			// presentation frames can draw movers at the camera's presentation time
			SamplePresentationEntityPoses();
		}

		// do multiplayer related stuff
		if ( isMultiplayer ) {
			mpGame.Run();
			CheckAutoExecAfterMapLoad();
		}

		// openQ4: snapshot the players after they have moved this frame so hitscan lag
		// compensation has a position history to rewind through.
		CaptureMPLagCompensationFrame();
		botManager.FinishFrameProfile();

		// free the player pvs
		FreePlayerPVS();

		// display how long it took to calculate the current game frame
		if ( g_frametime.GetBool() ) {
			Printf( "game %d: all:%.1f th:%.1f ev:%.1f %d ents \n",
				time, timer_think.Milliseconds() + timer_events.Milliseconds(),
				timer_think.Milliseconds(), timer_events.Milliseconds(), num );
		}

		// build the return value
		ret.consistencyHash = 0;
		ret.sessionCommand[0] = 0;

		// see if a target_sessionCommand has forced a changelevel
		if ( sessionCommand.Length() ) {
			idStr::Copynz( ret.sessionCommand, sessionCommand, sizeof( ret.sessionCommand ) );
			sessionCommand = "";
			break;
		}

		// make sure we don't loop forever when skipping a cinematic
		if ( skipCinematic && ( time > cinematicMaxSkipTime ) ) {
			Warning( "Exceeded maximum cinematic skip length.  Cinematic may be looping infinitely." );
			skipCinematic = false;
			break;
		}

		// jscott: additional timings
		timer_misc2.Stop();

		if ( g_frametime.GetInteger() > 1 )  {
			gameLocal.Printf( "misc:%.1f misc2:%.1f\n", timer_misc.Milliseconds(), timer_misc2.Milliseconds() );
		}
		// bdube: let gameDebug know that its not in a game frame anymore
		gameDebug.EndFrame( );
		gameLogLocal.EndFrame( );

	} while( ( inCinematic || ( time < cinematicStopTime ) ) && skipCinematic );

	ret.syncNextGameFrame = skipCinematic;
	if ( skipCinematic ) {
		//soundSystem->EndCinematic();
		soundSystem->SetMute( false );
		skipCinematic = false;		
	}

	// show any debug info for this frame
	RunDebugInfo();
	D_DrawDebugLines();

	g_simpleItems.ClearModified();
	if ( lastCatchupFrame ) idSaveGame::PopulateDictionaryCache();
	return ret;
}
// RAVEN END

/*
================
idGameLocal::RepeaterFrame
================
*/
void idGameLocal::RepeaterFrame( const userOrigin_t *clientOrigins, bool lastCatchupFrame, int serverGameFrame ) {
	int i;

	// passing serverGameFrame is strictly a sanity check
	// the adjustment in case of problem in RunFrame should cascade to here and never be a long term problem
	static int FrameDelta = 0;
	if ( serverGameFrame != framenum ) {
		// don't want to spam the warning. don't have a good repro case to fix this but still want to track the error (seen it happen)
		int delta = serverGameFrame - framenum;
		if ( delta != FrameDelta ) {
			if ( framenum != 0 ) {	// to skip printing the initial messages
				common->Warning( "repeater frame and game frame out of sync: %d/%d", serverGameFrame, framenum );
			}
			FrameDelta = delta;
		}
	} else {
		if ( FrameDelta != 0 ) {
			common->Warning( "repeater in sync with game frame at %d", serverGameFrame );
		}
		FrameDelta = 0;
	}

	ReallocViewers( cvarSystem->GetCVarInteger( "ri_maxViewers" ) );

	for ( i = 0; i < maxViewer; i++ ) {
		if ( !viewers[ i ].active ) {
			continue;
		}

		if ( viewers[ i ].nopvs ) {
			continue;
		}

		viewers[ i ].origin = clientOrigins[ i ];

		if ( viewers[ i ].origin.followClient >= 0 && entities[ viewers[ i ].origin.followClient ] && entities[ viewers[ i ].origin.followClient ]->IsType( idPlayer::GetClassType() ) ) {
			// if the viewer is following someone, use their origin
			idPlayer *player = static_cast< idPlayer * >( entities[ viewers[ i ].origin.followClient ] );
			viewers[ i ].origin.origin = player->GetEyePosition();
		}

		viewers[ i ].pvsArea = pvs.GetPVSArea( viewers[ i ].origin.origin );
	}
}

/*
======================================================================

  Game view drawing

======================================================================
*/

/*
====================
idGameLocal::GetScreenAspectRatio
====================
*/
float idGameLocal::GetScreenAspectRatio( void ) const {
	int screenWidth = ( renderSystem != NULL ) ? renderSystem->GetScreenWidth() : 0;
	int screenHeight = ( renderSystem != NULL ) ? renderSystem->GetScreenHeight() : 0;

	if ( screenWidth > 0 && screenHeight > 0 ) {
		return ( float )screenWidth / ( float )screenHeight;
	}

	if ( cvarSystem != NULL ) {
		screenWidth = cvarSystem->GetCVarInteger( "r_customWidth" );
		screenHeight = cvarSystem->GetCVarInteger( "r_customHeight" );
		if ( screenWidth > 0 && screenHeight > 0 ) {
			return ( float )screenWidth / ( float )screenHeight;
		}
	}

	return 4.0f / 3.0f;
}

/*
====================
idGameLocal::CalcFov

Calculates the horizontal and vertical field of view based on a horizontal field of view and custom aspect ratio
====================
*/
void idGameLocal::CalcFov( float base_fov, float &fov_x, float &fov_y ) const {
	float	x;
	float	y;
	const float referenceAspect = 4.0f / 3.0f;
	const float aspectRatio = idMath::ClampFloat( 0.1f, 10.0f, GetScreenAspectRatio() );

// RAVEN BEGIN
// jnewquist: Option to adjust vertical fov instead of horizontal for non 4:3 modes
	if ( g_fixedHorizFOV.GetBool() ) {
		x = aspectRatio / idMath::Tan( base_fov / 360.0f * idMath::PI );
		y = idMath::ATan( 1.0f, x );
		fov_y = y * 360.0f / idMath::PI;
		fov_x = base_fov;
		return;
	}
// RAVEN END

	// first, calculate the vertical fov from the legacy 4:3 baseline.
	x = referenceAspect / idMath::Tan( base_fov / 360.0f * idMath::PI );
	y = idMath::ATan( 1.0f, x );
	fov_y = y * 360.0f / idMath::PI;

	// FIXME: somehow, this is happening occasionally
	assert( fov_y > 0 );
	if ( fov_y <= 0 ) {
		Printf( sys->FPU_GetState() );
		Error( "idGameLocal::CalcFov: bad result" );
	}

	if ( idMath::Fabs( aspectRatio - referenceAspect ) < 0.001f ) {
		fov_x = base_fov;
		return;
	}

	y = 1.0f / idMath::Tan( fov_y / 360.0f * idMath::PI );
	fov_x = idMath::ATan( aspectRatio, y ) * 360.0f / idMath::PI;

	if ( fov_x < base_fov ) {
		fov_x = base_fov;
		x = aspectRatio / idMath::Tan( fov_x / 360.0f * idMath::PI );
		fov_y = idMath::ATan( 1.0f, x ) * 360.0f / idMath::PI;
	}

	// FIXME: somehow, this is happening occasionally
	assert( ( fov_x > 0 ) && ( fov_y > 0 ) );
	if ( ( fov_y <= 0 ) || ( fov_x <= 0 ) ) {
		Printf( sys->FPU_GetState() );
		Error( "idGameLocal::CalcFov: bad result" );
	}
}

void ClearClipProfile( void );
void DisplayClipProfile( void );

// What the frozen scene's camera shows against the authoritative view, for the
// late-look latency analysis (tools/tests/presentation_look_runtime.py). Noted
// once per prepared draw, before the capture records it.
static struct {
	float	drawnYaw, authoritativeYaw, lookYaw, inputYaw, drawnZ;
	int		look;
} presentationLookDiagnostic;

static void NotePresentationLookDiagnostic( idPlayer *player ) {
	presentationLookDiagnostic.drawnYaw = player->GetRenderView() != NULL ? player->GetRenderView()->viewaxis.ToAngles().yaw : 0.0f;
	presentationLookDiagnostic.drawnZ = player->GetRenderView() != NULL ? player->GetRenderView()->vieworg.z : 0.0f;
	presentationLookDiagnostic.authoritativeYaw = player->viewAngles.yaw;
	presentationLookDiagnostic.look = player->GetPresentationLookDiagnostic( presentationLookDiagnostic.inputYaw, presentationLookDiagnostic.lookYaw ) ? 1 : 0;
}


/*
================
idGameLocal::PreparePlayerSceneForRender

This is a presentation-only pass.  It never advances player or weapon logic;
it rebuilds the render view and resubmits the viewmodel for the current render
sample between authoritative game tics.
================
*/
void idGameLocal::PreparePlayerSceneForRender( idPlayer *player ) {
	presentationClockSample = -1.0f;
	presentationSceneFraction = -1.0f;
	presentationAnimationTime = -1;
	if ( player == NULL || GetDemoState() == DEMO_PLAYING || IsTimeDemo() ) {
		return;
	}
	presentationPrepStartTicks = idLib::sys->GetClockTicks();

	// Freeze one draw sample before CalculateRenderView() asks for the camera
	// fraction.  Animation lives on the previous-to-current interval, whereas
	// renderView.time is the current tic plus the same elapsed offset.
	const int authoritativeInterval = Max( 0, time - previousTime );
	presentationClockSample = GetPresentationTicFraction();
	presentationSceneFraction = GetPresentationInterpolationFraction();
	const int maxSequentialInterval = static_cast<int>( idMath::Ceil( common->GetUserCmdMsecFloat() ) );
	const bool sequentialInterval = authoritativeInterval > 0 && authoritativeInterval <= maxSequentialInterval &&
		GetMHz() == common->GetUserCmdHz();
	if ( sequentialInterval ) {
		const int presentationOffset = idMath::Ftoi( authoritativeInterval * presentationSceneFraction + 0.5f );
		presentationAnimationTime = idMath::ClampInt( previousTime, time, previousTime + presentationOffset );
	} else {
		presentationAnimationTime = time;
	}
	player->CalculateRenderView();
	NotePresentationLookDiagnostic( player );
	// The weapon's scope GUI is an in-world surface drawn with the 3D scene, so
	// its yaw has to be current before that scene is rendered.  Updating it from
	// DrawHUD set it during the 2D overlay, after R_RenderGuiSurf had already
	// drawn the scope for this frame, which left the compass a frame behind the
	// gun: invisible while the view is still, and a varying offset that shimmers
	// and swings the rotated element out of its window while turning.
	player->UpdateZoomGuiViewState();

	// Movers and everything riding them share the camera's presentation time.
	// Without this the eye is drawn interpolated while the lift under it is
	// drawn a whole tic ahead, and the two beat against each other at 60 Hz.
	// When the drawn camera is not the interpolated first-person pose -- a
	// cinematic, a private camera, third person, or any frame the camera had to
	// snap on -- the whole scene goes back on the authoritative clock together.
	if ( player->IsPresentationViewInterpolated() ) {
		UpdatePresentationEntityPoses();
	} else {
		presentationSceneFraction = -1.0f;
		presentationAnimationTime = -1;
		ClearPresentationEntityPoses();
	}

	RecordPresentationClockSample();

	if ( player->weaponViewModel.GetEntity() != NULL ) {
		player->weaponViewModel->UpdatePresentationWeapon( player->CanShowWeaponViewmodel() );
	}
}

void idGameLocal::EndPresentationSceneForRender( void ) {
	presentationClockSample = -1.0f;
	presentationSceneFraction = -1.0f;
	presentationAnimationTime = -1;
}

/*
================
idGameLocal::Draw

makes rendering and sound system calls
================
*/
bool idGameLocal::Draw( int clientNum, bool menuPaused ) {

	// openQ4: the classic dynamic lights are a client presentation layer, so
	// fade and retire them on the render frame, before the view is built.
	G_UpdateClassicLights();
//	DisplayClipProfile( );
//	ClearClipProfile( );

	if ( isMultiplayer ) {
		const bool drawn = mpGame.Draw( clientNum );
		if ( drawn ) {
			CheckAutoExecAfterMapLoad();
		}
		return drawn;
	}

	idPlayer *player = static_cast<idPlayer *>(entities[ clientNum ]);

	if ( !player ) {
		return false;
	}

	PreparePlayerSceneForRender( player );

// RAVEN BEGIN
// mwhitlock: Xenon texture streaming.
#if defined(_XENON)
	renderView_t *view = player->GetRenderView();
	// nrausch: view doesn't necessarily exist yet
	if ( !view ) {
		player->CalculateRenderView();
		view = player->GetRenderView();
	}
#endif
// RAVEN END
	// render the scene
	player->playerView.RenderPlayerView( player->hud );
	EndPresentationSceneForRender();

// RAVEN BEGIN
// bdube: debugging HUD
	gameDebug.DrawHud( );
// RAVEN END

	CheckAutoExecAfterMapLoad();
	return true;
}

/*
================
idGameLocal::CheckAutoExecAfterMapLoad

Execute a one-shot cfg after the first active map frames, for deterministic validation harnesses.
================
*/
void idGameLocal::CheckAutoExecAfterMapLoad( void ) {
	if ( gamestate != GAMESTATE_ACTIVE ) {
		return;
	}

	const char *execPath = g_autoExecAfterMapLoad.GetString();
	if ( !execPath || execPath[ 0 ] == '\0' ) {
		execPath = cvarSystem->GetCVarString( "g_autoExecAfterMapLoad" );
	}
	const bool wantAutoExec = execPath && execPath[ 0 ] != '\0';

	if ( wantAutoExec ) {
		if ( !autoExecAfterMapLoadPending ) {
			autoExecAfterMapLoadPending = true;
			autoExecAfterMapLoadStartTime = Sys_Milliseconds();
			if ( !autoExecAfterMapLoadWaitingLogged ) {
				common->Printf( "AutoExecAfterMapLoad: waiting for first active draw\n" );
				autoExecAfterMapLoadWaitingLogged = true;
			}
			common->Printf( "AutoExecAfterMapLoad: first active draw observed; waiting %d ms before exec\n",
				Max( 0, g_autoExecAfterMapLoadDelayMs.GetInteger() ) );
		}
	} else if ( autoExecAfterMapLoadPending || autoExecAfterMapLoadWaitingLogged ) {
		common->Printf( "AutoExecAfterMapLoad: skipped because g_autoExecAfterMapLoad was cleared before execution\n" );
		autoExecAfterMapLoadPending = false;
		autoExecAfterMapLoadWaitingLogged = false;
	}

	if ( !autoExecAfterMapLoadPending ) {
		return;
	}

	const int nowMs = Sys_Milliseconds();
	int delayMs = g_autoExecAfterMapLoadDelayMs.GetInteger();
	if ( delayMs < 0 ) {
		delayMs = 0;
	}
	if ( delayMs > 0 && ( nowMs - autoExecAfterMapLoadStartTime ) < delayMs ) {
		return;
	}

	bool safePath = true;
	for ( const char *scan = execPath; scan && *scan; scan++ ) {
		if ( *scan == '\n' || *scan == '\r' || *scan == ';' || *scan == '"' ) {
			safePath = false;
			break;
		}
	}

	if ( safePath ) {
		idCmdArgs execArgs;
		execArgs.AppendArg( "exec" );
		execArgs.AppendArg( execPath );
		common->Printf( "AutoExecAfterMapLoad: executed %s at %d ms\n",
			execPath,
			nowMs - autoExecAfterMapLoadStartTime );
		// Draw() also services this hook. Executing the cfg synchronously from
		// there can make commands such as screenshot recursively enter the
		// session render path before the current frame has completed.
		cmdSystem->BufferCommandArgs( CMD_EXEC_APPEND, execArgs );
	} else {
		common->Warning( "AutoExecAfterMapLoad: skipped unsafe cfg path '%s'", execPath ? execPath : "" );
	}

	autoExecAfterMapLoadPending = false;
	autoExecAfterMapLoadWaitingLogged = false;
	g_autoExecAfterMapLoad.SetString( "" );
	cvarSystem->SetCVarString( "g_autoExecAfterMapLoad", "" );
}

/*
================
idGameLocal::HandleESC
================
*/
escReply_t idGameLocal::HandleESC( idUserInterface **gui ) {

//RAVEN BEGIN
//asalmon: xbox dedicated server needs to bring up a special menu
#ifdef _XBOX
	if( cvarSystem->GetCVarBool( "net_serverDedicated" ))
	{
		return ESC_MAIN;
	}
#endif
//RAVEN END

	if ( isMultiplayer ) {
		idPlayer *localPlayer = GetLocalPlayer();
		if ( localPlayer && localPlayer->HandleESC() ) return ESC_IGNORE;
		// openQ4: once an Arena match has ended, Escape skips its ceremony to the
		// report instead of opening a menu that could abandon the result.
		if ( mpGame.ArenaCampaignOwnsEscape() ) return ESC_IGNORE;
		*gui = StartMenu();
		// we may set the gui back to NULL to hide it
		return ESC_GUI;
	}
	idPlayer *player = GetLocalPlayer();
	if ( player ) {
		if ( player->HandleESC() ) {
			return ESC_IGNORE;
		} else {
			return ESC_MAIN;
		}
	}
	return ESC_MAIN;
}

/*
================
idGameLocal::UpdatePlayerPostMainMenu
================
*/
void idGameLocal::UpdatePlayerPostMainMenu()	{
	idPlayer* player = GetLocalPlayer();

	//dedicated server?
	if( !player)	{
		return;
	}	
	
	//crosshair may have changed
	player->UpdateHudWeapon();
}

/*
================
idGameLocal::StartMenu
================
*/
idUserInterface* idGameLocal::StartMenu( void ) {
	if ( !isMultiplayer ) {
		return NULL;
	}
	return mpGame.StartMenu();
}

/*
================
idGameLocal::GetClientStats
================
*/
void idGameLocal::GetClientStats( int clientNum, char *data, const int len ) {
	mpGame.PlayerStats( clientNum, data, len );
}

/*
================
idGameLocal::SwitchTeam
================
*/
void idGameLocal::SwitchTeam( int clientNum, int team ) {

	idPlayer *   player;
	player = clientNum >= 0 ? static_cast<idPlayer *>( gameLocal.entities[ clientNum ] ) : NULL;

	if ( !player ) {
		return;
	}

	int oldTeam = player->team;

	// Put in spectator mode
	if ( team == -1 ) {
		static_cast< idPlayer * >( entities[ clientNum ] )->Spectate( true );
	}
	// Switch to a team
	else {
		mpGame.SwitchToTeam ( clientNum, oldTeam, team );
	}
}

/*
================
idGameLocal::HandleGuiCommands
================
*/
const char* idGameLocal::HandleGuiCommands( const char *menuCommand ) {
	if ( !isMultiplayer ) {
		return NULL;
	}
	return mpGame.HandleGuiCommands( menuCommand );
}

/*
================
idGameLocal::HandleMainMenuCommands
================
*/
void MPResolveMatchLimitFor( const idDict &serverInfo, int gameType, const char *&limitLabel, int &limitValue );

void idGameLocal::HandleMainMenuCommands( const char *menuCommand, idUserInterface *gui ) {
	if ( !idStr::Icmp( menuCommand, "retainedLoadingServer" ) ) {
		// openQ4: the retained loading screen's server card names the mode,
		// the limits it enforces and whether it has teams, from the server info
		// the session passes as query_si_* (serverInfo is set when the map
		// spawns, after the loading screen presents).
		idDict query;
		static const char *const keys[] = { "si_gameType", "si_fragLimit", "si_captureLimit", "si_controlTime", "si_roundLimit",
			"si_scoreLimit", "si_timeLimit" };
		for ( int i = 0; i < static_cast<int>( sizeof( keys ) / sizeof( keys[0] ) ); i++ ) {
			query.Set( keys[ i ], gui->State().GetString( va( "query_%s", keys[ i ] ) ) );
		}
		const mpGameTypeInfo_t *info = MPGameTypeByName( query.GetString( "si_gameType" ) );
		if ( info == NULL ) {
			return;
		}
		const char *limitLabel = "";
		int limitValue = 0;
		MPResolveMatchLimitFor( query, info->type, limitLabel, limitValue );
		idStr limits;
		if ( limitValue > 0 ) {
			limits = va( "%s %d", limitLabel, limitValue );
		}
		if ( query.GetInt( "si_timeLimit" ) > 0 ) {
			limits += limits.Length() ? " - " : "";
			limits += va( "%s %d", common->GetLocalizedString( "#str_200061" ), query.GetInt( "si_timeLimit" ) );
		}
		// An Arena Campaign launch presents the campaign's own game type: its
		// Duels run as one-on-one deathmatch, and the card must not say so.
		const char *arenaGameType = cvarSystem->GetCVarBool( "ui_arenaCampaignActive" ) ?
			cvarSystem->GetCVarString( "ui_arenaMatchGameType" ) : "";
		gui->SetStateString( "server_gametype", idStr::Cmpn( arenaGameType, "#str_", 5 ) == 0 ?
			common->GetLocalizedString( arenaGameType ) : common->GetLocalizedString( info->localizedName ) );
		gui->SetStateString( "server_limit", limits.c_str() );
		gui->SetStateBool( "server_team_mode", MPGameTypeHasAny( info->type, GTF_TEAM ) );
		gui->SetStateBool( "server_answered", true );
	} else if ( !idStr::Icmp( menuCommand, "retainedMultiplayerCover" ) ) {
		// openQ4: the session's retained menu card covers mainGui.
		mpGame.SetRetainedMenuCover( true, gui );
	} else if ( !idStr::Icmp( menuCommand, "retainedMultiplayerVariant" ) ) {
		// openQ4: which card the session covers the menu with.
		gui->SetStateBool( "mp.welcome", mpGame.RetainedMenuWelcome() );
	} else if ( !idStr::Icmp( menuCommand, "retainedMultiplayerUncover" ) ) {
		mpGame.SetRetainedMenuCover( false, gui );
	} else if ( !idStr::Icmp( menuCommand, "retainedMultiplayerState" ) ) {
		mpGame.PublishRetainedMenu( gui );
	} else if ( !idStr::Icmp( menuCommand, "initCreateServerSettings" ) ) {
		// openQ4: the vote gametype index is the menu dropdown index
		gui->SetStateInt( "currentGametype", mpGame.GameTypeToVote( si_gameType.GetString() ) );
	} else if ( !idStr::Icmp( menuCommand, "filterByNextMod" ) ) {
		BuildModList();

		if ( modList.Num() > 0 && (filterMod < 0 || filterMod >= modList.Num()) ) {
			filterMod = 0;
			networkSystem->UseSortFunction( filterByMod, true );
			gui->SetStateString( "filterMod", modList[ filterMod ].c_str() );
		} else {
			++filterMod;
			if ( filterMod < modList.Num() ) {
				networkSystem->UseSortFunction( filterByMod, true );
				gui->SetStateString( "filterMod", modList[ filterMod ].c_str() );
			} else {
				filterMod = -1;
				networkSystem->UseSortFunction( filterByMod, false );
				gui->SetStateString( "filterMod", common->GetLocalizedString( "#str_123008" ) );
			}
		}
	} else if ( !idStr::Icmp( menuCommand, "filterByPrevMod" ) ) {
		BuildModList();

		if ( modList.Num() > 0 && (filterMod < 0 || filterMod >= modList.Num()) ) {
			filterMod = modList.Num() - 1;
			networkSystem->UseSortFunction( filterByMod, true );
			gui->SetStateString( "filterMod", modList[ filterMod ].c_str() );
		} else {
			--filterMod;
			if ( filterMod >= 0 ) {
				networkSystem->UseSortFunction( filterByMod, true );
				gui->SetStateString( "filterMod", modList[ filterMod ].c_str() );
			} else {
				filterMod = -1;
				networkSystem->UseSortFunction( filterByMod, false );
				gui->SetStateString( "filterMod", common->GetLocalizedString( "#str_123008" ) );
			}
		}
	} else if ( !idStr::Icmp( menuCommand, "updateFilterByMod" ) ) {
		if ( filterMod < 0 || filterMod >= modList.Num() ) {
			gui->SetStateString( "filterMod", common->GetLocalizedString( "#str_123008" ) );
		} else {
			gui->SetStateString( "filterMod", modList[ filterMod ].c_str() );
		}
	} else if ( !idStr::Icmp( menuCommand, "server_clearSort" ) ) {
		filterMod = -1;
		gui->SetStateString( "filterMod", common->GetLocalizedString( "#str_123008" ) );
	}

	return;
}

/*
================
idGameLocal::GetLevelMap

  should only be used for in-game level editing
================
*/
idMapFile *idGameLocal::GetLevelMap( void ) {
// RAVEN BEGIN
// rhummer: Added the HasBeenResolved check, if resolve has been run then it doesn't have func_groups.
//			Which we probably don't want, so force the map to be reloaded.
	if ( mapFile && mapFile->HasPrimitiveData() && !mapFile->HasBeenResloved() ) {
// RAVEN END
		return mapFile;
	}
	if ( !mapFileName.Length() ) {
		return NULL;
	}

	if ( mapFile ) {
		delete mapFile;
	}

	mapFile = new idMapFile;
	if ( !mapFile->Parse( mapFileName ) ) {
		delete mapFile;
		mapFile = NULL;
	}

	return mapFile;
}

/*
================
idGameLocal::GetMapName
================
*/
const char *idGameLocal::GetMapName( void ) const {
	return mapFileName.c_str();
}

/*
================
idGameLocal::CallFrameCommand
================
*/
void idGameLocal::CallFrameCommand( idEntity* ent, const char* frameCommand ) {
	const function_t *func = program.FindFunction( frameCommand );
	if ( !func ) {
		Warning( "Script function '%s' not found.", frameCommand );
		return;
	}
	CallFrameCommand ( ent, func );
}

void idGameLocal::CallFrameCommand( idEntity *ent, const function_t *frameCommand ) {
	frameCommandThread->CallFunction( ent, frameCommand, true );
	frameCommandThread->Execute();
}

/*
================
idGameLocal::CallObjectFrameCommand
================
*/
void idGameLocal::CallObjectFrameCommand( idEntity *ent, const char *frameCommand ) {
	const function_t *func;

	func = ent->scriptObject.GetFunction( frameCommand );
	if ( !func ) {
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( !ent->IsType( idTestModel::GetClassType() ) ) {
// RAVEN END
			Error( "Unknown function '%s' called for frame command on entity '%s'", frameCommand, ent->name.c_str() );
		}
	} else {
		frameCommandThread->CallFunction( ent, func, true );
		frameCommandThread->Execute();
	}
}

/*
================
idGameLocal::ShowTargets
================
*/
void idGameLocal::ShowTargets( void ) {
	idMat3		axis = GetLocalPlayer()->viewAngles.ToMat3();
	idVec3		up = axis[ 2 ] * 5.0f;
	const idVec3 &viewPos = GetLocalPlayer()->GetPhysics()->GetOrigin();
	idBounds	viewTextBounds( viewPos );
	idBounds	viewBounds( viewPos );
	idBounds	box( idVec3( -4.0f, -4.0f, -4.0f ), idVec3( 4.0f, 4.0f, 4.0f ) );
	idEntity	*ent;
	idEntity	*target;
	int			i;
	idBounds	totalBounds;

	viewTextBounds.ExpandSelf( 128.0f );
	viewBounds.ExpandSelf( 512.0f );
	for( ent = spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
		totalBounds = ent->GetPhysics()->GetAbsBounds();
		for( i = 0; i < ent->targets.Num(); i++ ) {
			target = ent->targets[ i ].GetEntity();
			if ( target ) {
				totalBounds.AddBounds( target->GetPhysics()->GetAbsBounds() );
			}
		}

		if ( !viewBounds.IntersectsBounds( totalBounds ) ) {
			continue;
		}

		float dist;
		idVec3 dir = totalBounds.GetCenter() - viewPos;
		dir.NormalizeFast();
		totalBounds.RayIntersection( viewPos, dir, dist );
		float frac = ( 512.0f - dist ) / 512.0f;
		if ( frac < 0.0f ) {
			continue;
		}

		gameRenderWorld->DebugBounds( ( ent->IsHidden() ? colorLtGrey : colorOrange ) * frac, ent->GetPhysics()->GetAbsBounds() );
		if ( viewTextBounds.IntersectsBounds( ent->GetPhysics()->GetAbsBounds() ) ) {
			idVec3 center = ent->GetPhysics()->GetAbsBounds().GetCenter();
			gameRenderWorld->DrawText( ent->name.c_str(), center - up, 0.1f, colorWhite * frac, axis, 1 );
			gameRenderWorld->DrawText( ent->GetEntityDefName(), center, 0.1f, colorWhite * frac, axis, 1 );
			gameRenderWorld->DrawText( va( "#%d", ent->entityNumber ), center + up, 0.1f, colorWhite * frac, axis, 1 );
		}

		for( i = 0; i < ent->targets.Num(); i++ ) {
			target = ent->targets[ i ].GetEntity();
			if ( target ) {
				gameRenderWorld->DebugArrow( colorYellow * frac, ent->GetPhysics()->GetAbsBounds().GetCenter(), target->GetPhysics()->GetOrigin(), 10, 0 );
				gameRenderWorld->DebugBounds( colorGreen * frac, box, target->GetPhysics()->GetOrigin() );
			}
		}
	}
}

/*
================
idGameLocal::RunDebugInfo
================
*/
void idGameLocal::RunDebugInfo( void ) {
	idEntity *ent;
	idPlayer *player;

	player = GetLocalPlayer();
	if ( !player ) {
		return;
	}

	const idVec3 &origin = player->GetPhysics()->GetOrigin();

	if ( g_showEntityInfo.GetBool() ) {
		idBounds	viewTextBounds( origin );
		idBounds	viewBounds( origin );

		viewTextBounds.ExpandSelf( 128.0f );
		viewBounds.ExpandSelf( 512.0f );
		for( ent = spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
			// don't draw the worldspawn
			if ( ent == world ) {
				continue;
			}

// RAVEN BEGIN
// rjohnson: moved entity info out of idGameLocal into its own function
			ent->DrawDebugEntityInfo(&viewBounds, &viewTextBounds);
// RAVEN END
		}
	}

	// debug tool to draw bounding boxes around active entities
	if ( g_showActiveEntities.GetBool() ) {
		for( ent = activeEntities.Next(); ent != NULL; ent = ent->activeNode.Next() ) {
			idBounds	b = ent->GetPhysics()->GetBounds();
			if ( b.GetVolume() <= 0 ) {
				b[0][0] = b[0][1] = b[0][2] = -8;
				b[1][0] = b[1][1] = b[1][2] = 8;
			}
			if ( ent->fl.isDormant ) {
				gameRenderWorld->DebugBounds( colorYellow, b, ent->GetPhysics()->GetOrigin() );
			} else {
				gameRenderWorld->DebugBounds( colorGreen, b, ent->GetPhysics()->GetOrigin() );
			}
		}
	}

// RAVEN BEGIN
// bdube: client entities
	if ( cl_showEntityInfo.GetBool ( ) ) {
		rvClientEntity* cent;
		for( cent = clientSpawnedEntities.Next(); cent != NULL; cent = cent->spawnNode.Next() ) {
			cent->DrawDebugInfo ( );
		}				
	}
// RAVEN END

	if ( g_showTargets.GetBool() ) {
		ShowTargets();
	}

	if ( g_showTriggers.GetBool() ) {
		idTrigger::DrawDebugInfo();
	}

	if ( ai_showCombatNodes.GetBool() ) {
//		idCombatNode::DrawDebugInfo();
	}

	if ( ai_showPaths.GetBool() ) {
		idPathCorner::DrawDebugInfo();
	}

	if ( g_editEntityMode.GetBool() ) {
		if ( gameLocal.isMultiplayer ) 	{
			g_editEntityMode.SetInteger(0);
			Printf( "Not allowed in multiplayer.\n" );
		} else {
			editEntities->DisplayEntities();
		}
	}

	if ( g_showCollisionWorld.GetBool() ) {
		// use g_maxShowDistance value instead of 128.0f
		collisionModelManager->DrawModel( clip[0]->GetWorldCollisionModel(), vec3_origin, mat3_identity, origin, mat3_identity, g_maxShowDistance.GetFloat() );
	}

	if ( g_showCollisionModels.GetBool() ) {
		if( g_showCollisionModels.GetInteger() == 2 ) {
			clip[ 0 ]->DrawClipModels( player->GetEyePosition(), g_maxShowDistance.GetFloat(), pm_thirdPerson.GetBool() ? NULL : player, &idPlayer::GetClassType() );
		} else {
			clip[ 0 ]->DrawClipModels( player->GetEyePosition(), g_maxShowDistance.GetFloat(), pm_thirdPerson.GetBool() ? NULL : player );
		}
		
	}

	if ( g_showCollisionTraces.GetBool() ) {
		clip[ 0 ]->PrintStatistics();
	}

	if ( g_showPVS.GetInteger() ) {
		pvs.DrawPVS( origin, ( g_showPVS.GetInteger() == 2 ) ? PVS_ALL_PORTALS_OPEN : PVS_NORMAL );
	}

// RAVEN BEGIN
// rjohnson: added debug hud
	if ( gameDebug.IsHudActive ( DBGHUD_PHYSICS ) )	{
		clip[ 0 ]->DebugHudStatistics();
	}

	clip[ 0 ]->ClearStatistics();
// RAVEN END

	if ( aas_test.GetInteger() >= 0 ) {
		idAAS *aas = GetAAS( aas_test.GetInteger() );
		if ( aas ) {
			aas->Test( origin );
			if ( ai_testPredictPath.GetBool() ) {
				idVec3 velocity;
				predictedPath_t path;

				velocity.x = idMath::Cos( DEG2RAD( player->viewAngles.yaw ) ) * 100.0f;
				velocity.y = idMath::Sin( DEG2RAD( player->viewAngles.yaw ) ) * 100.0f;
				velocity.z = 0.0f;
				idAI::PredictPath( player, aas, origin, velocity, 1000, 100, SE_ENTER_OBSTACLE | SE_BLOCKED | SE_ENTER_LEDGE_AREA, path );
			}
		}
// RAVEN BEGIN 
// rjohnson: added more debug drawing
		if ( aas_showAreas.GetInteger() == 3 ) {
			for( int i=0; i<aasNames.Num(); i++ ) {
				aas = GetAAS( i );
				if ( !aas || !aas->IsValid() ) {
					continue;
				}

				if ( aas->GetSettings()->debugDraw ) {
					aas->ShowAreas( origin );
				}
			}
		}
		if ( aas_showProblemAreas.GetInteger() == 3 ) {
			for( int i=0; i<aasNames.Num(); i++ ) {
				aas = GetAAS( i );
				if ( !aas || !aas->IsValid() ) {
					continue;
				}

				if ( aas->GetSettings()->debugDraw ) {
					aas->ShowAreas( origin, true );
				}
			}
		}
// RAVEN END
	}

	if ( ai_showObstacleAvoidance.GetInteger() == 2 ) {
		idAAS *aas = GetAAS( 0 );
		if ( aas ) {
			idVec3 seekPos;
			obstaclePath_t path;

			seekPos = player->GetPhysics()->GetOrigin() + player->viewAxis[0] * 200.0f;
			idAI::FindPathAroundObstacles( player->GetPhysics(), aas, NULL, player->GetPhysics()->GetOrigin(), seekPos, path );
		}
	}

	// collision map debug output
	idVec3 debugViewOrigin;
	idMat3 debugViewAxis;
	player->GetViewPos( debugViewOrigin, debugViewAxis );
	collisionModelManager->DebugOutput( debugViewOrigin, debugViewAxis );

// RAVEN BEGIN
// jscott: for debugging playbacks
	if( g_showPlayback.GetInteger() ) {
		gameEdit->DrawPlaybackDebugInfo();
	}
// RAVEN END

// RAVEN BEGIN
// ddynerman: SD's clip sector code
	if ( g_showClipSectors.GetBool() ) {
		clip[ 0 ]->DrawClipSectors();
	}

	if ( g_showAreaClipSectors.GetFloat() ) {
		clip[ 0 ]->DrawAreaClipSectors( g_showAreaClipSectors.GetFloat() );
	}
// RAVEN END
}

/*
==================
idGameLocal::NumAAS
==================
*/
int	idGameLocal::NumAAS( void ) const {
	return aasList.Num();
}

/*
==================
idGameLocal::GetAAS
==================
*/
idAAS *idGameLocal::GetAAS( int num ) const {
	if ( ( num >= 0 ) && ( num < aasList.Num() ) ) {
		if ( aasList[ num ] && aasList[ num ]->GetSettings() ) {
			return aasList[ num ];
		}
	}
	return NULL;
}

/*
==================
idGameLocal::GetAAS
==================
*/
idAAS *idGameLocal::GetAAS( const char *name ) const {
	int i;

	for ( i = 0; i < aasNames.Num(); i++ ) {
		if ( aasNames[ i ] == name ) {
			if ( !aasList[ i ]->GetSettings() ) {
				return NULL;
			} else {
				return aasList[ i ];
			}
		}
	}
	return NULL;
}

/*
==================
idGameLocal::SetAASAreaState
==================
*/
void idGameLocal::SetAASAreaState( const idBounds &bounds, const int areaContents, bool closed ) {
	int i;

	for( i = 0; i < aasList.Num(); i++ ) {
		aasList[ i ]->SetAreaState( bounds, areaContents, closed );
	}
}

/*
==================
idGameLocal::AddAASObstacle
==================
*/
aasHandle_t idGameLocal::AddAASObstacle( const idBounds &bounds ) {
	int i;
	aasHandle_t obstacle;
	aasHandle_t check;

	if ( !aasList.Num() ) {
		return -1;
	}

	obstacle = aasList[ 0 ]->AddObstacle( bounds );
	for( i = 1; i < aasList.Num(); i++ ) {
		check = aasList[ i ]->AddObstacle( bounds );
		assert( check == obstacle );
	}

	return obstacle;
}

/*
==================
idGameLocal::RemoveAASObstacle
==================
*/
void idGameLocal::RemoveAASObstacle( const aasHandle_t handle ) {
	int i;

	for( i = 0; i < aasList.Num(); i++ ) {
		aasList[ i ]->RemoveObstacle( handle );
	}
}

/*
==================
idGameLocal::RemoveAllAASObstacles
==================
*/
void idGameLocal::RemoveAllAASObstacles( void ) {
	int i;

	for( i = 0; i < aasList.Num(); i++ ) {
		aasList[ i ]->RemoveAllObstacles();
	}
}

// RAVEN BEGIN
// mwhitlock: added entity memory usage stuff.
/*
==================
idGameLocal::GetEntityMemoryUsage

Compute combined total memory footprint of server and client entity storage.
==================
*/

size_t idGameLocal::GetEntityMemoryUsage ( void ) const {

	// Server ents.
	size_t serverEntitiesSize = 0;
	for( idEntity *ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
		 serverEntitiesSize += sizeof( idEntity );
	}
	
	// Client ents.
	size_t clientEntitiesSize = 0;
	for( rvClientEntity *ent = gameLocal.clientSpawnedEntities.Next() ; ent != NULL; ent = ent->spawnNode.Next() ) {
		 clientEntitiesSize += ent->Size();
	}
	
	return serverEntitiesSize + clientEntitiesSize;
}
// RAVEN END

/*
==================
idGameLocal::CheatsOk
==================
*/
bool idGameLocal::CheatsOk( bool requirePlayer ) {
	idPlayer *player;

	if ( isMultiplayer && !cvarSystem->GetCVarBool( "net_allowCheats" ) ) {
		Printf( "Not allowed in multiplayer.\n" );
		return false;
	}

	if ( !developer.GetBool() ) {
		return false;
	}

	player = GetLocalPlayer();
	if ( !requirePlayer || ( player && ( player->health > 0 ) ) ) {
		return true;
	}

	Printf( "You must be alive to use this command.\n" );

	return false;
}

/*
===================
idGameLocal::RegisterEntity
===================
*/
void idGameLocal::RegisterEntity( idEntity *ent ) {
	int spawn_entnum;

	if ( spawnCount >= ( 1 << ( 32 - GENTITYNUM_BITS ) ) ) {
		Error( "idGameLocal::RegisterEntity: spawn count overflow" );
	}

	firstFreeIndex = Max( minSpawnIndex, firstFreeIndex );

	if ( !spawnArgs.GetInt( "spawn_entnum", "0", spawn_entnum ) ) {
		while ( entities[firstFreeIndex] && firstFreeIndex < ENTITYNUM_MAX_NORMAL ) {
			firstFreeIndex++;
		}
		if ( firstFreeIndex >= ENTITYNUM_MAX_NORMAL ) {
			Error( "no free entities" );
		}
		spawn_entnum = firstFreeIndex++;
	} else {
		assert( spawn_entnum < MAX_CLIENTS || spawn_entnum >= minSpawnIndex );
		// openQ4: the number comes from spawn args, which a map or a script can
		// set, and it indexes entities[] and spawnIds[] below
		if ( spawn_entnum < 0 || spawn_entnum >= ENTITYNUM_NONE ) {
			Error( "idGameLocal::RegisterEntity: spawn_entnum %d out of range", spawn_entnum );
		}
	}

	entities[ spawn_entnum ] = ent;
	spawnIds[ spawn_entnum ] = spawnCount++;
	ent->entityNumber = spawn_entnum;
	ent->spawnNode.AddToEnd( spawnedEntities );
	ent->spawnArgs.TransferKeyValues( spawnArgs );

	if ( spawn_entnum >= num_entities ) {
		num_entities++;
	}

// RAVEN BEGIN
// bdube: keep track of last time an entity was registered	
	entityRegisterTime = time;
// RAVEN END
}

/*
===================
idGameLocal::UnregisterEntity
===================
*/
void idGameLocal::UnregisterEntity( idEntity *ent ) {
	assert( ent );

	if ( editEntities ) {
		editEntities->RemoveSelectedEntity( ent );
	}

	if ( ( ent->entityNumber != ENTITYNUM_NONE || ent == entities[ ENTITYNUM_NONE ] ) && ( entities[ ent->entityNumber ] == ent ) ) {
		ent->spawnNode.Remove();
		entities[ ent->entityNumber ] = NULL;
		spawnIds[ ent->entityNumber ] = -1;
		if ( ent->entityNumber >= MAX_CLIENTS && ent->entityNumber < firstFreeIndex ) {
			firstFreeIndex = ent->entityNumber;
		}
		ent->entityNumber = ENTITYNUM_NONE;
	}

// RAVEN BEGIN
// bdube: keep track of last time an entity was registered	
	entityRegisterTime = time;
// RAVEN END
}

/*
===============
idGameLocal::SkipEntityIndex
===============
*/
void idGameLocal::SkipEntityIndex( void ) {
	assert( entities[ firstFreeIndex ] == NULL );
	firstFreeIndex++;
	if ( firstFreeIndex >= ENTITYNUM_MAX_NORMAL ) {
		Error( "no free entities" );
	}
}

/*
================
idGameLocal::SpawnEntityType
================
*/
idEntity *idGameLocal::SpawnEntityType( const idTypeInfo &classdef, const idDict *args, bool bIsClientReadSnapshot ) {
	idClass *obj;

#ifdef _DEBUG
	if ( isClient ) {
		assert( bIsClientReadSnapshot );
	}
#endif

// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
	if ( !classdef.IsType( idEntity::GetClassType() ) ) {
// RAVEN END
		Error( "Attempted to spawn non-entity class '%s'", classdef.classname );
	}

	try {
		if ( args ) {
			spawnArgs = *args;
		} else {
			spawnArgs.Clear();
		}
		obj = classdef.CreateInstance();
		obj->CallSpawn();
	}
	
	catch( idAllocError & ) {
		obj = NULL;
	}
	spawnArgs.Clear();

	return static_cast<idEntity *>(obj);
}

/*
===================
idGameLocal::SpawnClientEntityDef

Finds the spawn function for the client entity and calls it,
returning false if not found
===================
*/
bool idGameLocal::SpawnClientEntityDef( const idDict &args, rvClientEntity **cent, bool setDefaults, const char* spawn ) {
	const char	*classname;
	idTypeInfo	*cls;
	idClass		*obj;
	idStr		error;
	const char  *name;

	if ( cent ) {
		*cent = NULL;
	}

	spawnArgs = args;

	if ( spawnArgs.GetBool( "nospawn" ) ){
		//not meant to actually spawn, just there for some compiling process
		return false;
	}

	if ( spawnArgs.GetString( "name", "", &name ) ) {
		error = va( " on '%s'", name );
	}

	spawnArgs.GetString( "classname", NULL, &classname );

	const idDeclEntityDef *def = FindEntityDef( classname, false );
	if ( !def ) {
		// RAVEN BEGIN
		// jscott: a NULL classname would crash Warning()
		if( classname ) {
			Warning( "Unknown classname '%s'%s.", classname, error.c_str() );
		}
		// RAVEN END
		return false;
	}

	spawnArgs.SetDefaults( &def->dict );

	// check if we should spawn a class object
	if( spawn == NULL ) {
		spawnArgs.GetString( "spawnclass", NULL, &spawn );
	}

	if ( spawn ) {

		cls = idClassSubstitution::Resolve( idClass::GetClass( spawn ) );
		if ( !cls ) {
			Warning( "Could not spawn '%s'.  Class '%s' not found%s.", classname, spawn, error.c_str() );
			return false;
		}

		obj = cls->CreateInstance();
		if ( !obj ) {
			Warning( "Could not spawn '%s'. Instance could not be created%s.", classname, error.c_str() );
			return false;
		}

		obj->CallSpawn();

		// openQ4: a client entity the full table refused is already queued
		// for removal and never finished spawning; callers must not use it.
		if ( obj->IsType( rvClientEntity::GetClassType() ) && static_cast<rvClientEntity*>(obj)->entityNumber == -1 ) {
			return false;
		}

		if ( cent && obj->IsType( rvClientEntity::GetClassType() ) ) {
			*cent = static_cast<rvClientEntity*>(obj);
		}

		return true;
	}

	Warning( "%s doesn't include a spawnfunc%s.", classname, error.c_str() );
	return false;
}

/*
===================
idGameLocal::SpawnEntityDef

Finds the spawn function for the entity and calls it,
returning false if not found
===================
*/
bool idGameLocal::SpawnEntityDef( const idDict &args, idEntity **ent, bool setDefaults ) {
	const char	*classname;
	const char	*spawn;
	idTypeInfo	*cls;
	idClass		*obj;
	idStr		error;
	const char  *name;

	TIME_THIS_SCOPE( __FUNCLINE__);
	
	if ( ent ) {
		*ent = NULL;
	}

	spawnArgs = args;

	if ( spawnArgs.GetBool( "nospawn" ) )
	{//not meant to actually spawn, just there for some compiling process
		return false;
	}

	if ( spawnArgs.GetString( "name", "", &name ) ) {
// RAVEN BEGIN
// jscott: fixed sprintf to idStr
		error = va( " on '%s'", name );
// RAVEN END
	}
	spawnArgs.GetString( "classname", NULL, &classname );

	// Callers that disable defaults pass a dictionary that has already been
	// resolved from its entityDef. Avoid repeating the declaration lookup and
	// default merge on those prepared spawn paths.
	const idDeclEntityDef *def = NULL;
	if ( setDefaults || g_keepEntityStats.GetBool() ) {
		def = FindEntityDef( classname, false );
	}
	if ( setDefaults && !def ) {
// RAVEN BEGIN
// jscott: a NULL classname would crash Warning()
		if( classname ) {
			Warning( "Unknown classname '%s'%s.", classname, error.c_str() );
		}
// RAVEN END
		return false;
	}

	if ( setDefaults ) {
		spawnArgs.SetDefaults( &def->dict );
	}

// RAVEN BEGIN
// rjohnson: entity usage stats
	if ( g_keepEntityStats.GetBool() ) {
		if ( idStr::Icmp( classname, "func_spawner" ) == 0 || 
			 idStr::Icmp( classname, "func_spawner_enemy" ) == 0 ) {
			// special case for spawners
			for( int i = 1; ; i++ ) {
				char		tempSpawn[128];
				const char	*tempClassname;

				sprintf( tempSpawn, "def_spawn_type%d", i );
				tempClassname = spawnArgs.GetString( tempSpawn, NULL );
				if ( tempClassname ) {
					const idDeclEntityDef *tempDef = FindEntityDef( tempClassname, false );

					if ( tempDef ) {
						idDict	tempArgs = tempDef->dict;

						tempArgs.Set( "mapFileName", mapFileNameStripped );
						entityUsageList.Insert( tempArgs );
					}
				} else {
					break;
				}
			}
		}
		else if ( def != NULL && def->dict.GetBool( "report_stats" ) ) {
			idDict	tempArgs = spawnArgs;
			tempArgs.Set( "mapFileName", mapFileNameStripped );
			entityUsageList.Insert( tempArgs );
		}
	}
// RAVEN END

	// check if we should spawn a class object
	spawnArgs.GetString( "spawnclass", NULL, &spawn );
	if ( spawn ) {

		cls = idClassSubstitution::Resolve( idClass::GetClass( spawn ) );
		if ( !cls ) {
			Warning( "Could not spawn '%s'.  Class '%s' not found%s.", classname, spawn, error.c_str() );
			return false;
		}

		obj = cls->CreateInstance();
		if ( !obj ) {
			Warning( "Could not spawn '%s'. Instance could not be created%s.", classname, error.c_str() );
			return false;
		}

		obj->CallSpawn();

// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( ent && obj->IsType( idEntity::GetClassType() ) ) {
// RAVEN END
			*ent = static_cast<idEntity *>(obj);
		}

		return true;
	}

	// check if we should call a script function to spawn
	spawnArgs.GetString( "spawnfunc", NULL, &spawn );
	if ( spawn ) {
		const function_t *func = program.FindFunction( spawn );
		if ( !func ) {
			Warning( "Could not spawn '%s'.  Script function '%s' not found%s.", classname, spawn, error.c_str() );
			return false;
		}
		idThread *thread = new idThread( func );
		thread->DelayedStart( 0 );

		return true;
	}

	Warning( "%s doesn't include a spawnfunc or spawnclass%s.", classname, error.c_str() );
	return false;
}

// abahr:
idEntity* idGameLocal::SpawnEntityDef( const char* entityDefName, const idDict* additionalArgs ) {
	idDict			finalArgs;
	const idDict*	entityDict = NULL;
	idEntity*		entity = NULL;
	
	TIME_THIS_SCOPE( __FUNCLINE__);

	if( !entityDefName ) {
		return NULL;
	}

	entityDict = FindEntityDefDict( entityDefName, false );
	if( !entityDict ) {
		return NULL;
	}

	if( !additionalArgs ) {
		SpawnEntityDef( *entityDict, &entity );
	} else {
		finalArgs.Copy( *entityDict );
		finalArgs.Copy( *additionalArgs );
		SpawnEntityDef( finalArgs, &entity );
	}

	return entity;
}
// RAVEN END

/*
================
idGameLocal::FindEntityDef
================
*/
const idDeclEntityDef *idGameLocal::FindEntityDef( const char *name, bool makeDefault ) const {
	TIME_THIS_SCOPE( __FUNCLINE__);
	const idDecl *decl = NULL;
	if ( isMultiplayer ) {
		decl = declManager->FindType( DECL_ENTITYDEF, va( "%s_mp", name ), false );
	}
	if ( !decl ) {
		decl = declManager->FindType( DECL_ENTITYDEF, name, makeDefault );
	}
	return static_cast<const idDeclEntityDef *>( decl );
}

/*
================
idGameLocal::FindEntityDefDict
================
*/
const idDict *idGameLocal::FindEntityDefDict( const char *name, bool makeDefault ) const {
	TIME_THIS_SCOPE( __FUNCLINE__);
	const idDeclEntityDef *decl = FindEntityDef( name, makeDefault );
	return decl ? &decl->dict : NULL;
}

/*
================
idGameLocal::InhibitEntitySpawn
================
*/
bool idGameLocal::InhibitEntitySpawn( idDict &spawnArgs ) {
	
	bool result = false;

	if ( isMultiplayer ) {
		spawnArgs.GetBool( "not_multiplayer", "0", result );
	} else if ( g_skill.GetInteger() == 0 ) {
		spawnArgs.GetBool( "not_easy", "0", result );
	} else if ( g_skill.GetInteger() == 1 ) {
		spawnArgs.GetBool( "not_medium", "0", result );
	} else {
		spawnArgs.GetBool( "not_hard", "0", result );
	}

	const char *name;
#ifndef ID_DEMO_BUILD
	if ( g_skill.GetInteger() == 3 ) { 
		name = spawnArgs.GetString( "classname" );
		if ( idStr::Icmp( name, "item_medkit" ) == 0 || idStr::Icmp( name, "item_medkit_small" ) == 0 ) {
			result = true;
		}
	}
#endif

// RAVEN BEGIN
// bdube: suppress ents that don't match the entity filter
	const char* entityFilter;
	if ( serverInfo.GetString( "si_entityFilter", "", &entityFilter ) && *entityFilter ) {
		bool allowed = spawnArgs.GetBool( va( "filter_%s", entityFilter ) );
		// One Flag uses the stock CTF layout when a map has no mode-specific
		// setting. Explicit One Flag inclusions and exclusions take precedence.
		const mpGameTypeInfo_t *mode = MPGameType( gameType );
		if ( isMultiplayer && MPGameTypeHasAny( gameType, GTF_ONEFLAG ) &&
			idStr::Icmp( entityFilter, mode->entityFilter ) == 0 ) {
			const idKeyValue *specific = spawnArgs.FindKey( va( "filter_%s", mode->name ) );
			if ( specific != NULL ) {
				allowed = spawnArgs.GetBool( specific->GetKey() );
			}
		}
		if ( spawnArgs.MatchPrefix( "filter_" ) && !allowed ) {
			return true;
		}
	}
// RAVEN END

// RITUAL BEGIN
// squirrel: suppress ents that aren't supported in Buying modes (if that's the mode we're in)
	if ( mpGame.IsBuyingAllowedInTheCurrentGameMode() ) {
		if ( spawnArgs.GetBool( "disableSpawnInBuying", "0" ) ) {
			return true;
		}

		/// Don't spawn weapons or armor vests or ammo in Buying modes
		idStr classname = spawnArgs.GetString( "classname" );
		if( idStr::FindText( classname, "weapon_" ) == 0 || 
			idStr::FindText( classname, "item_armor_small" ) == 0 ||
			idStr::FindText( classname, "ammo_" ) == 0 ||
			idStr::FindText( classname, "item_armor_large" ) == 0 )
		{
			return true;
		}
	}
// RITUAL END

// openQ4 BEGIN
	// A mode where everybody spawns with the whole arsenal, full ammo and full
	// armour is not played over the item layout, and Quake Live's Clan Arena
	// factory clears the weapon, ammo, holdable, health, armour and powerup
	// spawns for exactly that reason.  Leaving them in turns Clan Arena back
	// into Team DM with a free loadout: the fight goes to whoever reaches the
	// mega health, and a player who wins an exchange tops straight back up.
	//
	// This runs at map populate on the server and on every client alike -
	// SetGameType always precedes MapPopulate - so entity numbering stays in
	// step, and ResetRound's map restart re-applies it every round.
	if ( isMultiplayer && MPGameTypeHasAny( gameType, GTF_FULLARSENAL ) ) {
		const char *classname = spawnArgs.GetString( "classname" );

		// An objective is not a pickup.  Attack & Defend also carries
		// GTF_FULLARSENAL and plays with a flag, so the flag class is named
		// rather than guessed at from the classname prefix.
		const bool objective = spawnArgs.GetBool( "inv_objective" ) ||
			idStr::Icmp( spawnArgs.GetString( "spawnclass" ), "rvItemCTFFlag" ) == 0;

		if ( !objective &&
			 ( idStr::FindText( classname, "weapon_" ) == 0 ||
			   idStr::FindText( classname, "ammo_" ) == 0 ||
			   idStr::FindText( classname, "item_" ) == 0 ||
			   idStr::FindText( classname, "powerup_" ) == 0 ) ) {
			return true;
		}
	}
// openQ4 END

	// suppress deadzone triggers if we're not running DZ
	if ( idStr::Icmp( serverInfo.GetString( "si_gameType" ), "DeadZone" ) != 0 ) {
		if ( idStr::Icmp( spawnArgs.GetString( "classname" ), "trigger_controlzone" ) == 0 ) {
			return true;
		}
	}

	return result;
}

/*
================
idGameLocal::SetSkill
================
*/
void idGameLocal::SetSkill( int value ) {
	int skill_level;

	if ( value < 0 ) {
		skill_level = 0;
	} else if ( value > 3 ) {
		skill_level = 3;
	} else {
		skill_level = value;
	}

	g_skill.SetInteger( skill_level );
}

/*
==============
idGameLocal::GameState

Used to allow entities to know if they're being spawned during the initial spawn.
==============
*/
gameState_t	idGameLocal::GameState( void ) const {
	return gamestate;
}

/*
==============
idGameLocal::SpawnMapEntities

Parses textual entity definitions out of an entstring and spawns gentities.
==============
*/
// RAVEN BEGIN
// ddynerman: multiple game instances
void idGameLocal::SpawnMapEntities( int instance, unsigned short* entityNumIn, unsigned short* entityNumOut, int* startSpawnCount ) {
// RAVEN END
	int			i;
	int			num;
	int			inhibit;
	int			latchedSpawnCount = -1;
	idMapEntity	*mapEnt;
	int			numEntities;
	idDict		args;
	idDict		items;
	bool		itemWithoutIconSeen = false;
	const bool profileMapSpawn = cvarSystem->GetCVarBool( "com_showLevelLoadTimes" );
	idTimer mapSpawnTimer;
	idTimer mapCopyTimer;
	idTimer mapDefLookupTimer;
	idTimer mapDefaultsTimer;
	idTimer mapInhibitTimer;
	idTimer mapMediaTimer;
	idTimer mapEntitySpawnTimer;
	if ( profileMapSpawn ) {
		mapSpawnTimer.Start();
	}

	Printf( "Spawning entities\n" );
	
	TIME_THIS_SCOPE( __FUNCLINE__);

	if ( mapFile == NULL ) {
		Printf("No mapfile present\n");
		return;
	}

	SetSkill( g_skill.GetInteger() );

	numEntities = mapFile->GetNumEntities();
	if ( numEntities == 0 ) {
		Error( "...no entities" );
	}
	const int maxInitialMapEntities = ENTITYNUM_MAX_NORMAL - MAX_CLIENTS + 1;
	if ( numEntities > maxInitialMapEntities ) {
		Error( "Map entity string contains %d entities; the runtime limit is %d", numEntities, maxInitialMapEntities );
	}

// RAVEN BEGIN
// ddynerman:	on the server, perform world-initialization when spawning instance 0 only and while starting up
//				on the client, perform world-init when spawning while starting up, as client main instance may not be ID 0
//				always spawn world-init in single-player
	num = inhibit = 0;

	// the worldspawn is a special that performs any global setup
	// needed by a level
	if( ( gameLocal.isServer && instance == 0 && gamestate == GAMESTATE_STARTUP ) || (gameLocal.isClient && gamestate == GAMESTATE_STARTUP) || !gameLocal.isMultiplayer ) { 
		mapEnt = mapFile->GetEntity( 0 );
		args = mapEnt->epairs;
		args.SetInt( "spawn_entnum", ENTITYNUM_WORLD );
		
		// on the client, spawnCount won't always start at INITIAL_SPAWN_COUNT (see rvInstance::Populate())
		// make sure the world and physics ent get the right spawn id
		if( gameLocal.isClient && spawnCount != INITIAL_SPAWN_COUNT ) {
			latchedSpawnCount = spawnCount;
			spawnCount = INITIAL_SPAWN_COUNT;
		}

// abahr: precache stuff on worldSpawn like player def and other misc things
		CacheDictionaryMedia( &args );
// jnewquist: Use accessor for static class type
		if ( !SpawnEntityDef( args ) || !entities[ ENTITYNUM_WORLD ] || !entities[ ENTITYNUM_WORLD ]->IsType( idWorldspawn::GetClassType() ) ) {
			Error( "Problem spawning world entity" );
		}
		num++;

// bdube: dummy entity for client entities with physics
		args.Clear ( );
		args.SetInt( "spawn_entnum", ENTITYNUM_CLIENT );
// jnewquist: Use accessor for static class type 
		if ( !SpawnEntityType( rvClientPhysics::GetClassType(), &args, true ) || !entities[ ENTITYNUM_CLIENT ] ) {	
			Error( "Problem spawning client physics entity" );
		}

		if( gameLocal.isClient && latchedSpawnCount != -1 ) {
			spawnCount = latchedSpawnCount;
		}

		isMapEntity[ ENTITYNUM_CLIENT ] = true;
		isMapEntity[ ENTITYNUM_WORLD ] = true;
// RAVEN END
	}

	// capture spawn count of start of map entities (after we've spawned in the physics ents)
	if ( startSpawnCount ) {
		(*startSpawnCount) = spawnCount;
	}

	for ( i = 1 ; i < numEntities ; i++ ) {

		if ( profileMapSpawn ) {
			mapCopyTimer.Start();
		}
		mapEnt = mapFile->GetEntity( i );
		args = mapEnt->epairs;
		if ( profileMapSpawn ) {
			mapCopyTimer.Stop();
			mapDefLookupTimer.Start();
		}

// RAVEN BEGIN
// ddynerman: merge the dicts ahead of SpawnEntityDef() so we can inhibit using merged info
		const idDeclEntityDef* entityDef = FindEntityDef( args.GetString( "classname" ), false );
		if ( profileMapSpawn ) {
			mapDefLookupTimer.Stop();
		}
		
		if( entityDef == NULL ) {
			gameLocal.Error( "idGameLocal::SpawnMapEntities() - Unknown entity classname '%s'\n", args.GetString( "classname" ) );
			return;
		}
		if ( profileMapSpawn ) {
			mapDefaultsTimer.Start();
		}
		args.SetDefaults( &(entityDef->dict) );
// RAVEN END
		if ( profileMapSpawn ) {
			mapDefaultsTimer.Stop();
			mapInhibitTimer.Start();
		}
		const bool inhibitSpawn = InhibitEntitySpawn( args );
		if ( profileMapSpawn ) {
			mapInhibitTimer.Stop();
		}

		if ( !inhibitSpawn ) {

			if( args.GetBool( "inv_item" ) ) {
				// openQ4: an item with no inv_icon was looked up and stored under an
				// empty key, a developer warning on core1 and process2. Track it
				// apart, keeping retail's once-per-icon loading text.
				const char *invIcon = args.GetString( "inv_icon" );
				const bool iconSeen = invIcon[ 0 ] ? items.GetBool( invIcon ) : itemWithoutIconSeen;
				if( !iconSeen ) {
					networkSystem->AddLoadingIcon( invIcon );
					networkSystem->SetLoadingText( common->GetLocalizedString( args.GetString( "inv_name" ) ) );
					if ( invIcon[ 0 ] ) {
						items.SetBool( invIcon, true );
					} else {
						itemWithoutIconSeen = true;
					}
				}
			}

			// precache any media specified in the map entity
			// EntityDef parsing has already cached inherited/default media.
			// Only scan the map-authored overrides here instead of walking the
			// complete merged dictionary again for every entity instance.
			if ( profileMapSpawn ) {
				mapMediaTimer.Start();
			}
			CacheDictionaryMedia( &mapEnt->epairs );
			if ( profileMapSpawn ) {
				mapMediaTimer.Stop();
			}
// RAVEN BEGIN
			if ( instance != 0 ) {
// ddynerman: allow this function to be called multiple-times to respawn map entities in other instances
				args.SetInt( "instance", instance );
				args.Set( "name", va( "%s_instance%d", args.GetString( "name" ), instance ) );
				//gameLocal.Printf( "Instance %d: Spawning %s (class: %s)\n", instance, args.GetString( "name" ), args.GetString( "classname" ) ); 
				
				// need a better way to do this - map entities that target other map entities need
				// to target the ones in the correct instance.  
				idStr target;
				if( args.GetString( "target", "", target ) ) {
					args.Set( "target", va( "%s_instance%d", target.c_str(), instance ) );
				}

				// and also udpate binds on a per-instance bases
				idStr bind;
				if( args.GetString( "bind", "", bind ) ) {
					args.Set( "bind", va( "%s_instance%d", bind.c_str(), instance ) );
				}

				// and also door triggers
				idStr triggerOnOpen;
				if( args.GetString( "triggerOnOpen", "", triggerOnOpen ) ) {
					args.Set( "triggerOnOpen", va( "%s_instance%d", triggerOnOpen.c_str(), instance ) );
				}

				idStr triggerOpened;
				if( args.GetString( "triggerOpened", "", triggerOpened ) ) {
					args.Set( "triggerOpened", va( "%s_instance%d", triggerOpened.c_str(), instance ) );
				}
			}

			idEntity* ent = NULL;
			if ( profileMapSpawn ) {
				mapEntitySpawnTimer.Start();
			}
			SpawnEntityDef( args, &ent, false );
			if ( profileMapSpawn ) {
				mapEntitySpawnTimer.Stop();
			}
			//common->Printf( "pop: spawn map ent %d at %d ( %s )\n", i, ent->entityNumber, args.GetString( "name" ) );
	
			if ( ent && entityNumOut ) {
				entityNumOut[ i ] = ent->entityNumber;
			}

			if ( gameLocal.GetLocalPlayer() && ent && gameLocal.isServer && instance != gameLocal.GetLocalPlayer()->GetInstance() ) {
				// if we're spawning entities not in our instance, tell them not to draw
				ent->BecomeInactive( TH_UPDATEVISUALS );
			}

			if ( ent ) {
				isMapEntity[ ent->entityNumber ] = true;
			}
// RAVEN END
			num++;
		} else {
			// keep counting and leave an empty slot in the entity list for inhibited entities
			// this so we maintain the same layout as the server and don't change it across restarts with different inhibit schemes
			//common->Printf( "pop: skip map ent %d at index %d ( %s )\n", i, firstFreeIndex, args.GetString( "name" ) );
			SkipEntityIndex();
			inhibit++;
		}
	}

	Printf( "...%i entities spawned, %i inhibited\n\n", num, inhibit );
	if ( profileMapSpawn ) {
		mapSpawnTimer.Stop();
		Printf(
			"Map entity spawn phases: copy=%.1f defLookup=%.1f defaults=%.1f inhibit=%.1f media=%.1f entitySpawn=%.1f other=%.1f total=%.1f msec\n",
			mapCopyTimer.Milliseconds(),
			mapDefLookupTimer.Milliseconds(),
			mapDefaultsTimer.Milliseconds(),
			mapInhibitTimer.Milliseconds(),
			mapMediaTimer.Milliseconds(),
			mapEntitySpawnTimer.Milliseconds(),
			Max( 0.0, mapSpawnTimer.Milliseconds() -
				mapCopyTimer.Milliseconds() -
				mapDefLookupTimer.Milliseconds() -
				mapDefaultsTimer.Milliseconds() -
				mapInhibitTimer.Milliseconds() -
				mapMediaTimer.Milliseconds() -
				mapEntitySpawnTimer.Milliseconds() ),
			mapSpawnTimer.Milliseconds() );
	}
}

/*
================
idGameLocal::AddEntityToHash
================
*/
void idGameLocal::AddEntityToHash( const char *name, idEntity *ent ) {
	if ( FindEntity( name ) ) {
		Error( "Multiple entities named '%s'", name );
	}
	entityHash.Add( entityHash.GenerateKey( name, true ), ent->entityNumber );
}

/*
================
idGameLocal::RemoveEntityFromHash
================
*/
bool idGameLocal::RemoveEntityFromHash( const char *name, idEntity *ent ) {
	int hash, i;

	hash = entityHash.GenerateKey( name, true );
	for ( i = entityHash.First( hash ); i != -1; i = entityHash.Next( i ) ) {
		if ( entities[i] && entities[i] == ent && entities[i]->name.Icmp( name ) == 0 ) {
			entityHash.Remove( hash, i );
			return true;
		}
	}
	return false;
}

/*
================
idGameLocal::GetTargets
================
*/
int idGameLocal::GetTargets( const idDict &args, idList< idEntityPtr<idEntity> > &list, const char *ref ) const {
	int i, num, refLength;
	const idKeyValue *arg;
	idEntity *ent;

	list.Clear();

	refLength = idLib::SizeToInt( strlen( ref ), "idGameLocal::GetTargets" );
	num = args.GetNumKeyVals();
	for( i = 0; i < num; i++ ) {

		arg = args.GetKeyVal( i );
		if ( arg->GetKey().Icmpn( ref, refLength ) == 0 ) {

			ent = FindEntity( arg->GetValue() );
			if ( ent ) {
				idEntityPtr<idEntity> &entityPtr = list.Alloc();
                entityPtr = ent;
			}
		}
	}

	return list.Num();
}

/*
=============
idGameLocal::GetTraceEntity

returns the master entity of a trace.  for example, if the trace entity is the player's head, it will return the player.
=============
*/
idEntity *idGameLocal::GetTraceEntity( const trace_t &trace ) const {
	idEntity *master;

	if ( !entities[ trace.c.entityNum ] ) {
		return NULL;
	}
	master = entities[ trace.c.entityNum ]->GetBindMaster();
	if ( master ) {
		return master;
	}
	return entities[ trace.c.entityNum ];
}

/*
=============
idGameLocal::ArgCompletion_EntityName

Argument completion for entity names
=============
*/
void idGameLocal::ArgCompletion_EntityName( const idCmdArgs &args, void(*callback)( const char *s ) ) {
	int i;

	for( i = 0; i < gameLocal.num_entities; i++ ) {
		if ( gameLocal.entities[ i ] ) {
			callback( va( "%s %s", args.Argv( 0 ), gameLocal.entities[ i ]->name.c_str() ) );
		}
	}
}

/*
=============
idGameLocal::ArgCompletion_AIName

Argument completion for idAI entity names
=============
*/
void idGameLocal::ArgCompletion_AIName( const idCmdArgs &args, void(*callback)( const char *s ) ) {
	int i;

	for( i = 0; i < gameLocal.num_entities; i++ ) {
		if ( gameLocal.entities[ i ] && gameLocal.entities[ i ]->IsType( idAI::GetClassType() ) ) {
			callback( va( "%s %s", args.Argv( 0 ), gameLocal.entities[ i ]->name.c_str() ) );
		}
	}
}

/*
=============
idGameLocal::FindEntity

Returns the entity whose name matches the specified string.
=============
*/
idEntity *idGameLocal::FindEntity( const char *name ) const {
	int hash, i;

	hash = entityHash.GenerateKey( name, true );
	for ( i = entityHash.First( hash ); i != -1; i = entityHash.Next( i ) ) {
		if ( entities[i] && entities[i]->name.Icmp( name ) == 0 ) {
			return entities[i];
		}
	}

	return NULL;
}

/*
=============
idGameLocal::FindEntityUsingDef

Searches all active entities for the next one using the specified entityDef.

Searches beginning at the entity after from, or the beginning if NULL
NULL will be returned if the end of the list is reached.
=============
*/
idEntity *idGameLocal::FindEntityUsingDef( idEntity *from, const char *match ) const {
	idEntity	*ent;

	if ( !from ) {
		ent = spawnedEntities.Next();
	} else {
		ent = from->spawnNode.Next();
	}

	for ( ; ent != NULL; ent = ent->spawnNode.Next() ) {
		assert( ent );
		if ( idStr::Icmp( ent->GetEntityDefName(), match ) == 0 ) {
			return ent;
		}
	}

	return NULL;
}

/*
=============
idGameLocal::FindTraceEntity

Searches all active entities for the closest ( to start ) match that intersects
the line start,end
=============
*/
idEntity *idGameLocal::FindTraceEntity( idVec3 start, idVec3 end, const idTypeInfo &c, const idEntity *skip ) const {
	idEntity *ent;
	idEntity *bestEnt;
	float scale;
	float bestScale;
	idBounds b;

	bestEnt = NULL;
	bestScale = 1.0f;
	for( ent = spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
		if ( ent->IsType( c ) && ent != skip ) {
			b = ent->GetPhysics()->GetAbsBounds().Expand( 16 );
			if ( b.RayIntersection( start, end-start, scale ) ) {
				if ( scale >= 0.0f && scale < bestScale ) {
					bestEnt = ent;
					bestScale = scale;
				}
			}
		}
	}

	return bestEnt;
}

/*
================
idGameLocal::EntitiesWithinRadius
================
*/
int idGameLocal::EntitiesWithinRadius( const idVec3 org, float radius, idEntity **entityList, int maxCount ) const {
	idEntity *ent;
	idBounds bo( org );
	int entCount = 0;

	bo.ExpandSelf( radius );
	for( ent = spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
		if ( ent->GetPhysics()->GetAbsBounds().IntersectsBounds( bo ) ) {
			// openQ4: maxCount was never looked at; callers pass arrays on the stack
			if ( entCount >= maxCount ) {
				Warning( "idGameLocal::EntitiesWithinRadius: max count" );
				break;
			}
			entityList[entCount++] = ent;
		}
	}

	return entCount;
}

/*
=================
idGameLocal::KillBox

Kills all entities that would touch the proposed new positioning of ent. The ent itself will not being killed.
Checks if player entities are in the teleporter, and marks them to die at teleport exit instead of immediately.
If catch_teleport, this only marks teleport players for death on exit
=================
*/
void idGameLocal::KillBox( idEntity *ent, bool catch_teleport ) {
	int			i;
	int			num;
	idEntity *	hit;
	idClipModel *cm;
	idClipModel *clipModels[ MAX_GENTITIES ];
	idPhysics	*phys;

	phys = ent->GetPhysics();
	if ( !phys->GetNumClipModels() ) {
		return;
	}

// RAVEN BEGIN
// ddynerman: multiple clip worlds
	num = ClipModelsTouchingBounds( ent, phys->GetAbsBounds(), phys->GetClipMask(), clipModels, MAX_GENTITIES );
// RAVEN END
	for ( i = 0; i < num; i++ ) {
		cm = clipModels[ i ];

		// don't check render entities
		if ( cm->IsRenderModel() ) {
			continue;
		}

		hit = cm->GetEntity();
		if ( ( hit == ent ) || !hit->fl.takedamage ) {
			continue;
		}

		if ( !phys->ClipContents( cm ) ) {
			continue;
		}

		// nail it
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( hit->IsType( idPlayer::GetClassType() ) && static_cast< idPlayer * >( hit )->IsInTeleport() ) {
// RAVEN END
			static_cast< idPlayer * >( hit )->TeleportDeath( ent->entityNumber );
		} else if ( !catch_teleport ) {
			hit->Damage( ent, ent, vec3_origin, "damage_telefrag", 1.0f, INVALID_JOINT );
		}

		if ( !gameLocal.isMultiplayer ) {
			// let the mapper know about it
			Warning( "'%s' telefragged '%s'", ent->name.c_str(), hit->name.c_str() );
		}
	}
}

/*
================
idGameLocal::RequirementMet
================
*/
bool idGameLocal::RequirementMet( idEntity *activator, const idStr &requirement, int removeItem ) {
	if ( requirement.Length() ) {
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( activator->IsType( idPlayer::GetClassType() ) ) {
// RAVEN END
			idPlayer *player = static_cast<idPlayer *>(activator);
			idDict *item = player->FindInventoryItem( requirement );
			if ( item ) {
				if ( removeItem ) {
					player->RemoveInventoryItem( item );
				}
				return true;
			} else {
				return false;
			}
		}
	}

	return true;
}


/*
================
idGameLocal::IsWeaponsStayOn
================
*/
bool idGameLocal::IsWeaponsStayOn( void ) {
	/// Override weapons stay when buying is active
	if( isMultiplayer && mpGame.IsBuyingAllowedInTheCurrentGameMode() ) {
		return false;
	}

	return serverInfo.GetBool( "si_weaponStay" );
}


/*
============
idGameLocal::AlertAI
============
*/
void idGameLocal::AlertAI( idEntity *ent ) {
// RAVEN BEGIN
// bdube: merged
	if ( ent ) {
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( ent->IsType( idActor::GetClassType() ) ) {
// RAVEN END
			// alert them for the next frame
			lastAIAlertActorTime = time + GetMSec();
			lastAIAlertActor = static_cast<idActor *>( ent );
		} else {
			lastAIAlertEntityTime = time + GetMSec();
			lastAIAlertEntity = ent;
		}
	} else {
		lastAIAlertEntityTime = 0;
		lastAIAlertActorTime = 0;
		lastAIAlertEntity = NULL;
		lastAIAlertActor = NULL;
	}
// RAVEN END
}

// RAVEN BEGIN
// bdube: alert entity returns an entity, alert actor a an actor
/*
============
idGameLocal::GetAlertEntity
============
*/
idEntity *idGameLocal::GetAlertEntity( void ) {
	if ( lastAIAlertEntityTime >= time ) {
		return lastAIAlertEntity.GetEntity();
	}

	return NULL;
}

/*
============
idGameLocal::GetAlertActor
============
*/
idActor *idGameLocal::GetAlertActor( void ) {
	if ( lastAIAlertActorTime >= time ) {
		return lastAIAlertActor.GetEntity();
	}

	return NULL;
}
// RAVEN END

/*
============
SortClipModelsByEntity
============
*/
static int SortClipModelsByEntity( const void* left, const void* right ) {
	idEntity* leftEntity  = left  ? (*((const idClipModel**)left))->GetEntity() : NULL;
	idEntity* rightEntity = right ? (*((const idClipModel**)right))->GetEntity() : NULL;

	int entityNumLeft = (leftEntity) ? leftEntity->entityNumber : 0;
	int entityNumRight = (rightEntity) ? rightEntity->entityNumber : 0;

	return entityNumLeft - entityNumRight;
}

// RAVEN BEGIN
/*
============
idGameLocal::RadiusDamage
Returns the number of actors damaged
============
*/
// abahr: changed to work with deathPush
void idGameLocal::RadiusDamage( const idVec3 &origin, idEntity *inflictor, idEntity *attacker, idEntity *ignoreDamage, idEntity *ignorePush, const char *damageDefName, float dmgPower, int* hitCount ) {
	if ( isMultiplayer && mpGame.IsGameplayFrozen() ) {
		return;
	}
	float		dist, damageScale, attackerDamageScale, attackerPushScale;
	idEntity*	ent = NULL;
	idEntity*	lastEnt = NULL;
	idClipModel* clipModel = NULL;
	idClipModel* clipModelList[ MAX_GENTITIES ];
	int			numListedClipModels;
	modelTrace_t	result;
	idVec3 		v, damagePoint, dir;
	int			i, damage, radius, push;

	const idDict *damageDef = FindEntityDefDict( damageDefName, false );
	if ( !damageDef ) {
		Warning( "Unknown damageDef '%s'", damageDefName );
		return;
	}

	damageDef->GetInt( "damage", "20", damage );
	damageDef->GetInt( "radius", "50", radius );
	damageDef->GetInt( "push", va( "%d", damage * 100 ), push );
	damageDef->GetFloat( "attackerDamageScale", "0.5", attackerDamageScale );
	if( gameLocal.isMultiplayer ) {
		damageDef->GetFloat( "attackerPushScale", "2", attackerPushScale );
	} else {
		damageDef->GetFloat( "attackerPushScale", "0", attackerPushScale );
	}

	if ( radius < 1 ) {
		radius = 1;
	}


// ddynerman: multiple clip worlds
	numListedClipModels = ClipModelsTouchingBounds( inflictor, idBounds(origin).Expand(radius), MASK_ALL, clipModelList, MAX_GENTITIES );
	if( numListedClipModels > 0 ) {
		//Sort list by unique entities for easier searching
		qsort( clipModelList, numListedClipModels, sizeof(clipModelList[0]), SortClipModelsByEntity );
	}

	if ( inflictor ) {
		inflictor = inflictor->GetDamageEntity ( );
	}
	if ( attacker ) {
		attacker = attacker->GetDamageEntity ( );
	}
	if ( ignoreDamage ) {
		ignoreDamage = ignoreDamage->GetDamageEntity ( );
	}

	for( int c = 0; c < numListedClipModels; ++c ) {
		clipModel = clipModelList[ c ];
		assert( clipModel );

		ent = clipModel->GetEntity();
		
		// Skip all entitys that arent primary damage entities
		if ( !ent || ent != ent->GetDamageEntity ( ) ) {
			continue;
		}

		// Dont damage inflictor or the ignore entity
		if( ent == inflictor || ent == ignoreDamage ) {
			continue;
		}

		idBounds absBounds = clipModel->GetAbsBounds();

		// find the distance from the edge of the bounding box
		for ( i = 0; i < 3; i++ ) {
			if ( origin[ i ] < absBounds[0][ i ] ) {
				v[ i ] = absBounds[0][ i ] - origin[ i ];
			} else if ( origin[ i ] > absBounds[1][ i ] ) {
				v[ i ] = origin[ i ] - absBounds[1][ i ];
			} else {
				v[ i ] = 0;
			}
		}

		dist = v.Length();
		if ( dist >= radius ) {
			continue;
		}

		if( gameRenderWorld->FastWorldTrace(result, origin, absBounds.GetCenter()) ) {
			continue;
		}
		
		RadiusPushClipModel ( inflictor, origin, push, clipModel );

		// Only damage unique entities.  This works because we have a sorted list
		if( lastEnt == ent ) {
			continue;
		}

		lastEnt = ent;

		if ( ent->CanDamage( origin, damagePoint, ignoreDamage ) ) {						
			// push the center of mass higher than the origin so players
			// get knocked into the air more
			if( gameLocal.isMultiplayer ) {
				// fudge the direction in MP to account for player height difference and origin shift
				// 31.875 = origin is 23.875 units lower in Q4 than Q3 + player is 8 units taller in Q4
				dir = ( ent->GetPhysics()->GetOrigin() + idVec3( 0.0f, 0.0f, 31.875f ) ) - origin;
			} else {
				dir = ent->GetPhysics()->GetOrigin() - origin;
			}		
			
			dir[2] += 24;
 
 			// get the damage scale
			damageScale = dmgPower * ( 1.0f - dist / radius );
			
			if ( ent == attacker ) {
				damageScale *= attackerDamageScale;
			}

			dir.Normalize();
			ent->Damage( inflictor, attacker, dir, damageDefName, damageScale, CLIPMODEL_ID_TO_JOINT_HANDLE(ent->GetPhysics()->GetClipModel()->GetId()) );

			// for stats, count the first 
			if( attacker && attacker->IsType( idPlayer::GetClassType() ) && inflictor && inflictor->IsType( idProjectile::GetClassType() ) && ent->IsType( idPlayer::GetClassType() ) && hitCount ) {
				// with splash damage projectiles, one projectile fire can damage multiple people.  If anyone is hit, 
				// the shot counts as a hit but only report accuracy on the first one to avoid accuracies > 100%
				statManager->WeaponHit( (const idActor*)attacker, ent, ((idProjectile*)inflictor)->methodOfDeath, (*hitCount) == 0 );
				(*hitCount)++;
			}
		} 
	}
}
// RAVEN END

/*
==============
idGameLocal::RadiusPush
==============
*/
void idGameLocal::RadiusPush( const idVec3 &origin, const float radius, const float push, const idEntity *inflictor, const idEntity *ignore, float inflictorScale, const bool quake ) {
	int i, numListedClipModels;
	idClipModel *clipModel;
	idClipModel *clipModelList[ MAX_GENTITIES ];
	idVec3 dir;
	idBounds bounds;
	modelTrace_t result;
	idEntity *ent;
	float scale;

// RAVEN BEGIN
// abahr: need to use gravity instead of assuming z is up
	dir = -GetCurrentGravity( const_cast<idEntity*>(inflictor) ).ToNormal();
// RAVEN END

	bounds = idBounds( origin ).Expand( radius );

	// get all clip models touching the bounds
// RAVEN BEGIN
// ddynerman: multiple clip worlds
	numListedClipModels = ClipModelsTouchingBounds( inflictor, bounds, -1, clipModelList, MAX_GENTITIES );

// bdube: getdamageentity
	if ( inflictor ) {
		inflictor = ((idEntity*)inflictor)->GetDamageEntity ( );
	}
	if ( ignore ) {
		ignore = ((idEntity*)ignore)->GetDamageEntity ( );
	}
// RAVEN END

	// apply impact to all the clip models through their associated physics objects
	for ( i = 0; i < numListedClipModels; i++ ) {

		clipModel = clipModelList[i];

		// never push render models
		if ( clipModel->IsRenderModel() ) {
			continue;
		}

		ent = clipModel->GetEntity();
// RAVEN BEGIN
// bdube: damage entity		
		if ( !ent || ent != ent->GetDamageEntity ( ) ) {
			continue;
		}
// RAVEN END
		
		// never push projectiles
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( ent->IsType( idProjectile::GetClassType() ) ) {
// RAVEN END
			continue;
		}

		// players use "knockback" in idPlayer::Damage
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( ent->IsType( idPlayer::GetClassType() ) && !quake ) {
// RAVEN END
			continue;
		}

		// don't push the ignore entity
		if ( ent == ignore ) {
			continue;
		}

		if ( gameRenderWorld->FastWorldTrace( result, origin, clipModel->GetOrigin() ) ) {
			continue;
		}

		// scale the push for the inflictor
		if ( ent == inflictor ) {
			scale = inflictorScale;
		} else {
			scale = 1.0f;
		}

		if ( quake ) {
			clipModel->GetEntity()->ApplyImpulse( world, clipModel->GetId(), clipModel->GetOrigin(), scale * push * dir );
		} else {
// RAVEN BEGIN
// bdube: inflictor
			RadiusPushClipModel( (idEntity*)inflictor, origin, scale * push, clipModel );
// RAVEN END
		}
	}
}

/*
==============
idGameLocal::RadiusPushClipModel
==============
*/
// RAVEN BEGIN
// bdube: inflictor
void idGameLocal::RadiusPushClipModel( idEntity* inflictor, const idVec3 &origin, const float push, const idClipModel *clipModel ) {
// RAVEN END
	int i, j;
	float dot, dist, area;
	const idTraceModel *trm;
	const traceModelPoly_t *poly;
	idFixedWinding w;
	idVec3 v, localOrigin, center, impulse;

	trm = clipModel->GetTraceModel();
	if ( !trm || 1 ) {
		impulse = clipModel->GetAbsBounds().GetCenter() - origin;
		impulse.Normalize();
// RAVEN BEGIN
// abahr: removed because z isn't always up
		//impulse.z += 1.0f;
		//impulse = idVec3( 0.0, 0.0, 1.0 );
		clipModel->GetEntity()->ApplyImpulse( inflictor, clipModel->GetId(), clipModel->GetOrigin(), push * impulse, true );
// RAVEN END
		return;
	}

	localOrigin = ( origin - clipModel->GetOrigin() ) * clipModel->GetAxis().Transpose();
	for ( i = 0; i < trm->numPolys; i++ ) {
		poly = &trm->polys[i];

		center.Zero();
		for ( j = 0; j < poly->numEdges; j++ ) {
			v = trm->verts[ trm->edges[ abs(poly->edges[j]) ].v[ INTSIGNBITSET( poly->edges[j] ) ] ];
			center += v;
			v -= localOrigin;
			v.NormalizeFast();	// project point on a unit sphere
			w.AddPoint( v );
		}
		center /= poly->numEdges;
		v = center - localOrigin;
		dist = v.NormalizeFast();
		dot = v * poly->normal;
		if ( dot > 0.0f ) {
			continue;
		}
		area = w.GetArea();
		// impulse in polygon normal direction
		impulse = poly->normal * clipModel->GetAxis();
		// always push up for nicer effect
		impulse.z -= 1.0f;
		// scale impulse based on visible surface area and polygon angle
		impulse *= push * ( dot * area * ( 1.0f / ( 4.0f * idMath::PI ) ) );
		// scale away distance for nicer effect
		impulse *= ( dist * 2.0f );
		// impulse is applied to the center of the polygon
		center = clipModel->GetOrigin() + center * clipModel->GetAxis();

// RAVEN BEGIN
// bdube: inflictor
		clipModel->GetEntity()->ApplyImpulse( inflictor, clipModel->GetId(), center, impulse );
// RAVEN END
	}
}

/*
===============
idGameLocal::ProjectDecal
===============
*/
void idGameLocal::ProjectDecal( const idVec3 &origin, const idVec3 &dir, float depth, bool parallel, float size, const char *material, float angle ) {
	float s, c;
	idMat3 axis, axistemp;
	idFixedWinding winding;
	idVec3 windingOrigin, projectionOrigin;

	static idVec3 decalWinding[4] = {
		idVec3(  1.0f,  1.0f, 0.0f ),
		idVec3( -1.0f,  1.0f, 0.0f ),
		idVec3( -1.0f, -1.0f, 0.0f ),
		idVec3(  1.0f, -1.0f, 0.0f )
	};

	if ( !g_decals.GetBool() ) {
		return;
	}

// RAVEN BEGIN
// rjohnson: no decals on dedicated server
	if ( isMultiplayer && !isClient && !isListenServer ) {
		// no decals on dedicated server
		return;
	}
// RAVEN END

	// randomly rotate the decal winding
	idMath::SinCos16( ( angle ) ? angle : random.RandomFloat() * idMath::TWO_PI, s, c );

	// winding orientation
	axis[2] = dir;
	axis[2].Normalize();
	axis[2].NormalVectors( axistemp[0], axistemp[1] );
	axis[0] = axistemp[ 0 ] * c + axistemp[ 1 ] * -s;
	axis[1] = axistemp[ 0 ] * -s + axistemp[ 1 ] * -c;

	windingOrigin = origin + depth * axis[2];
	if ( parallel ) {
		projectionOrigin = origin - depth * axis[2];
	} else {
		projectionOrigin = origin;
	}

	size *= 0.5f;

	winding.Clear();
	winding += idVec5( windingOrigin + ( axis * decalWinding[0] ) * size, idVec2( 1, 1 ) );
	winding += idVec5( windingOrigin + ( axis * decalWinding[1] ) * size, idVec2( 0, 1 ) );
	winding += idVec5( windingOrigin + ( axis * decalWinding[2] ) * size, idVec2( 0, 0 ) );
	winding += idVec5( windingOrigin + ( axis * decalWinding[3] ) * size, idVec2( 1, 0 ) );
	gameRenderWorld->ProjectDecalOntoWorld( winding, projectionOrigin, parallel, depth * 0.5f, declManager->FindMaterial( material ), time );
}

/*
==============
idGameLocal::BloodSplat
==============
*/
// RAVEN BEGIN
// ddynerman: multiple collision models
void idGameLocal::BloodSplat( const idEntity* ent, const idVec3 &origin, const idVec3 &dirArg, float size, const char *material ) {

	float halfSize = size * 0.5f;
	idVec3 verts[] = {	idVec3( 0.0f, +halfSize, +halfSize ),
						idVec3( 0.0f, +halfSize, -halfSize ),
						idVec3( 0.0f, -halfSize, -halfSize ),
						idVec3( 0.0f, -halfSize, +halfSize ) };
	idVec3 dir = dirArg;
	idTraceModel trm;
	idClipModel mdl;
	trace_t results;

// RAVEN BEGIN
// mekberg: changed from g_bloodEffects to g_decals
	if ( !g_decals.GetBool() ) {
		return;
	}
// RAVEN END

	size = halfSize + random.RandomFloat() * halfSize;
	trm.SetupPolygon( verts, 4 );
	mdl.LoadModel( trm, NULL );

	// I don't want dir to be axis aligned, as it is more likely to streak them (because most architecture is axis aligned
	dir.Set( dirArg.x*.1f, dirArg.y*.1f, -1 );
	dir.Normalize();

// RAVEN BEGIN
// ddynerman: multiple clip worlds
	Translation( ent, results, origin, origin + dir * 72.0f, &mdl, mat3_identity, CONTENTS_SOLID, NULL );
// RAVEN END
	ProjectDecal( results.endpos, dir, 2.0f * size, true, size, material );
}

/*
=============
idGameLocal::SetCamera
=============
*/
void idGameLocal::SetCamera( idCamera *cam ) {
	int i;
	idEntity *ent;
	idAI *ai;

	// this should fix going into a cinematic when dead.. rare but happens
	idPlayer *client = GetLocalPlayer();
	if ( client->health <= 0 || client->pfl.dead ) {
		return;
	}

	camera = cam;
	if ( camera ) {
// RAVEN BEGIN
// bdube: tool support
		inCinematic = false;
		if( !( gameLocal.editors & ( EDITOR_MODVIEW | EDITOR_PLAYBACKS ) ) ) {
			inCinematic = true;
		}
// RAVEN END

		if ( skipCinematic && camera->spawnArgs.GetBool( "disconnect" ) ) {
			camera->spawnArgs.SetBool( "disconnect", false );
			cvarSystem->SetCVarFloat( "r_znear", 3.0f );
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "disconnect\n" );
			skipCinematic = false;
			return;
		}

		if ( time > cinematicStopTime ) {
			cinematicSkipTime = time + CINEMATIC_SKIP_DELAY;
		}

		// set r_znear so that transitioning into/out of the player's head doesn't clip through the view
		cvarSystem->SetCVarFloat( "r_znear", 1.0f );
		
		// hide all the player models
		for( i = 0; i < numClients; i++ ) {
			if ( entities[ i ] ) {
				client = static_cast< idPlayer* >( entities[ i ] );
				client->EnterCinematic();
			}
		}

		if ( !cam->spawnArgs.GetBool( "ignore_enemies" ) ) {
			// kill any active monsters that are enemies of the player
			for ( ent = spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
				if ( ent->cinematic || ent->fl.isDormant ) {
					// only kill entities that aren't needed for cinematics and aren't dormant
					continue;
				}
				
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
				if ( ent->IsType( idAI::GetClassType() ) ) {
// RAVEN END
					ai = static_cast<idAI *>( ent );
					if ( !ai->GetEnemy() || !ai->IsActive() ) {
						// no enemy, or inactive, so probably safe to ignore
						continue;
					}
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
				} else if ( ent->IsType( idProjectile::GetClassType() ) ) {
// RAVEN END
					// remove all projectiles
				} else if ( ent->spawnArgs.GetBool( "cinematic_remove" ) ) {
					// remove anything marked to be removed during cinematics
				} else {
					// ignore everything else
					continue;
				}

				// remove it
				DPrintf( "removing '%s' for cinematic\n", ent->GetName() );
				ent->PostEventMS( &EV_Remove, 0 );
			}
		}

		if ( inCinematic && g_autoSkipCinematics.GetBool() ) {
			SkipCinematic();
		}

	} else {
		inCinematic = false;
		cinematicStopTime = time + msec;

		// restore r_znear
		cvarSystem->SetCVarFloat( "r_znear", 3.0f );

		// show all the player models
		for( i = 0; i < numClients; i++ ) {
			if ( entities[ i ] ) {
				idPlayer *client = static_cast< idPlayer* >( entities[ i ] );
				client->ExitCinematic();
			}
		}
	}
}

// RAVEN BEGIN
// jscott: for portal skies
/*
=============
idGameLocal::GetPortalSky
=============
*/
idCamera *idGameLocal::GetPortalSky( void ) const
{
	if( !portalSkyVisible ) {

		return( NULL );
	}
	return( portalSky );
}
/*
=============
idGameLocal::SetPortalSky
=============
*/
void idGameLocal::SetPortalSky( idCamera *cam ) 
{
	portalSky = cam;
}
// RAVEN END

/*
=============
idGameLocal::GetCamera
=============
*/
idCamera *idGameLocal::GetCamera( void ) const {
	return camera;
}

/*
=============
idGameLocal::SkipCinematic
=============
*/
bool idGameLocal::SkipCinematic( void ) {
	if ( camera ) {
		if ( camera->spawnArgs.GetBool( "disconnect" ) ) {
			camera->spawnArgs.SetBool( "disconnect", false );
			cvarSystem->SetCVarFloat( "r_znear", 3.0f );
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "disconnect\n" );
			skipCinematic = false;
			return false;
		}

		if ( camera->spawnArgs.GetBool( "instantSkip" ) ) {
			camera->Stop();
			return false;
		}
	}

	soundSystem->SetMute( true );
	if ( !skipCinematic ) {
		skipCinematic = true;
		cinematicMaxSkipTime = gameLocal.time + SEC2MS( g_cinematicMaxSkipTime.GetFloat() );
	}

	return true;
}

// RAVEN BEGIN
/*
======================
idGameLocal::StartViewEffect

For passing in an effect triggered view effect
======================
*/
void idGameLocal::StartViewEffect( int type, float time, float scale )
{
	idPlayer		*player;
	idPlayerView	*view;

	player = GetLocalPlayer();
	if( player )
	{
		view = &player->playerView;

		switch( type )
		{
		case VIEWEFFECT_DOUBLEVISION:
			view->SetDoubleVisionParms( time, scale );
			break;

		case VIEWEFFECT_SHAKE:
			if( !gameLocal.isMultiplayer ) {
				view->SetShakeParms( time, scale );
			}
			break;

		case VIEWEFFECT_TUNNEL:
			view->SetTunnelParms( time, scale );
			break;

		default:
			gameLocal.Warning( "Invalid view effect" );
			break;
		}
	}
}

/*
======================
idGameLocal::GetPlayerView
======================
*/
void idGameLocal::GetPlayerView( idVec3 &origin, idMat3 &axis )
{
	idPlayer		*player;
	renderView_t	*view;

	player = GetLocalPlayer();
	if( player )
	{
		view = player->GetRenderView();
		if ( !view ) {
			player->CalculateRenderView();
			view = player->GetRenderView();
			assert( view );
		}
		origin = view->vieworg;
		axis = view->viewaxis;
	}
	else
	{
		origin = vec3_origin;
		axis = mat3_identity;
	}
}

/*
======================
idGameLocal::Translation

small portion of physics required for the effects system
======================
*/
void idGameLocal::Translation( trace_t &trace, idVec3 &source, idVec3 &dest, idTraceModel *trm, int clipMask ) {
	if( !trm ) {
		clip[0]->Translation( trace, source, dest, NULL, mat3_identity, clipMask, NULL, NULL );
	} else {
		idClipModel		cm;
	
		cm.LoadModel( *trm, NULL );
		clip[0]->Translation( trace, source, dest, &cm, mat3_identity, clipMask, NULL, NULL );
	}
}

/*
======================
idGameLocal::SpawnClientMoveable
======================
*/
void idGameLocal::SpawnClientMoveable( const char* name, int lifetime, const idVec3& origin, const idMat3& axis, const idVec3& velocity, const idVec3& angular_velocity ) {
	const bool debugDebris = cvarSystem && cvarSystem->GetCVarInteger( "bse_debug" ) > 0;

	// find the debris def
	const idDict* args = gameLocal.FindEntityDefDict( name, false );
	if ( !args ) {
		if ( debugDebris ) {
			common->Warning( "BSE debris: entityDef '%s' not found", name ? name : "" );
		}
		return;
	}

	// Ensure client moveables never last forever
	if ( lifetime <= 0 ) {
		lifetime = SEC2MS(args->GetFloat( "duration", "5" ));
	}
	int burn_time = idMath::ClampInt( 0, lifetime, SEC2MS(args->GetFloat( "burn_time", "2.5" )) );
	
	// Spawn the debris
	
	rvClientMoveable* cent = NULL;
	// force the args to spawn a rvClientMoveable
	SpawnClientEntityDef( *args, (rvClientEntity**)(&cent), false, "rvClientMoveable" );
	
	if( !cent ) {
		if ( debugDebris ) {
			common->Warning( "BSE debris: failed to spawn client moveable '%s'", name ? name : "" );
		}
		return;
	}

	if ( debugDebris ) {
		common->Printf( "BSE debris: spawned client moveable '%s' lifetime=%d\n", name ? name : "", lifetime );
	}
 
	cent->SetOrigin( origin );
	cent->SetAxis( axis );
		
	cent->GetPhysics()->SetLinearVelocity( velocity );
	cent->GetPhysics()->SetAngularVelocity( angular_velocity );

	if ( !burn_time ) {
		//just disappear
		cent->PostEventMS( &EV_Remove, lifetime );	
	} else {
		cent->PostEventMS( &CL_FadeOut, lifetime-burn_time, burn_time );
	}
}

/*
======================
idGameLocal::DebugSet
======================
*/
void idGameLocal::DebugSetString( const char* name, const char* value ) {
	gameDebug.SetString( name, value );
}
void idGameLocal::DebugSetFloat( const char* name, float value ) {
	gameDebug.SetFloat( name, value );
}
void idGameLocal::DebugSetInt( const char* name, int value ) {
	gameDebug.SetInt( name, value );
}

/*
======================
idGameLocal::DebugGetStat
======================
*/
const char* idGameLocal::DebugGetStatString ( const char* name ) {
	return gameDebug.GetStatString( name );
}

int idGameLocal::DebugGetStatInt ( const char* name ) {
	return gameDebug.GetStatInt( name );
}

float idGameLocal::DebugGetStatFloat ( const char* name ) {
	return gameDebug.GetStatFloat( name );
}

/*
======================
idGameLocal::IsDebugHudActive
======================
*/
bool idGameLocal::IsDebugHudActive ( void ) const {
	return gameDebug.IsHudActive( DBGHUD_ANY );
}


// rjohnson: added player info support for note taking system
/*
======================
idGameLocal::GetPlayerInfo
======================
*/
bool idGameLocal::GetPlayerInfo( idVec3 &origin, idMat3 &axis, int PlayerNum, idAngles *deltaViewAngles, int reqClientNum ) {
	idPlayer	*player;

	if ( PlayerNum == -1 ) {
		player = GetLocalPlayer();
	} else {
		player = GetClientByNum( PlayerNum );
	}

	if( reqClientNum != -1 ) {
		idPlayer* reqClient = GetClientByNum( reqClientNum );
		if( reqClient && player ) {
			if( reqClient->GetInstance() != player->GetInstance() ) {
				return false;
			}
		}
	}

	if ( !player ) {
		return false;
	}

	player->GetViewPos( origin, axis );
	origin = player->GetPhysics()->GetOrigin();

	if ( deltaViewAngles ) {
		*deltaViewAngles = player->GetDeltaViewAngles();
	}

	return true;
};

/*
======================
idGameLocal::SetCurrentPlayerInfo
======================
*/
void idGameLocal::SetPlayerInfo( idVec3 &origin, idMat3 &axis, int PlayerNum ) {
	idPlayer	*player;

	if ( PlayerNum == -1 ) {
		player = GetLocalPlayer();
	} else {
		player = GetClientByNum( PlayerNum );
	}

	if ( !player ) {
		return;
	}

	player->Teleport( origin, axis.ToAngles(), NULL );
// RAVEN BEGIN
// ddynerman: save the current thinking entity for instance-dependent
	currentThinkingEntity = player;
	player->CalculateFirstPersonView();
	player->CalculateRenderView();
	currentThinkingEntity = NULL;
// RAVEN END

	return;
};

bool idGameLocal::PlayerChatDisabled( int clientNum ) {
	if( clientNum < 0 || clientNum >= MAX_CLIENTS || !entities[ clientNum ] ) {
		return false;
	}

	return !( ((idPlayer*)entities[ clientNum ])->isChatting || ((idPlayer*)entities[ clientNum ])->pfl.dead );
}

void idGameLocal::SetViewComments( const char *text ) {
	idPlayer	*player;

	player = GetLocalPlayer();

	if ( !player ) {
		return;
	}

	if ( text ) {
		player->hud->SetStateString( "viewcomments", text );
		player->hud->HandleNamedEvent( "showViewComments" );
	}
	else {
		player->hud->SetStateString( "viewcomments", "" );
		player->hud->HandleNamedEvent( "hideViewComments" );
	}
}

/*
===================
idGameLocal::GetNumGravityAreas
===================
*/
int	idGameLocal::GetNumGravityAreas() const {
	return gravityInfo.Num();
}

/*
===================
idGameLocal::GetGravityInfo
===================
*/
const rvGravityArea* idGameLocal::GetGravityInfo( int index ) const {
	return gravityInfo[ index ];
}

/*
===================
idGameLocal::SetGravityArea
===================
*/
void idGameLocal::SetGravityInfo( int index, rvGravityArea* info ) {
	gravityInfo[ index ] = info;
}

/*
===================
idGameLocal::AddUniqueGravityInfo
===================
*/
void idGameLocal::AddUniqueGravityInfo( rvGravityArea* info ) {
	gravityInfo.AddUnique( info );
}

/*
===================
idGameLocal::GetCurrentGravityInfoIndex
===================
*/
int idGameLocal::GetCurrentGravityInfoIndex( const idVec3& origin ) const {
	int numGravityAreas = GetNumGravityAreas();
	if( !numGravityAreas ) {
		return -1;
	}

	int areaNum = gameRenderWorld->PointInArea( origin );

	for( int ix = 0; ix < numGravityAreas; ++ix ) {
		if( !gameRenderWorld->AreasAreConnected(GetGravityInfo(ix)->GetArea(), areaNum, PS_BLOCK_GRAVITY) ) {
			continue;
		}
		 
		return ix;
	}

	return -1;
}

/*
===================
idGameLocal::InGravityArea
===================
*/
bool idGameLocal::InGravityArea( idEntity* entity ) const {
	return GetCurrentGravityInfoIndex( entity ) >= 0;
}

/*
===================
idGameLocal::GetCurrentGravityInfoIndex
===================
*/
int idGameLocal::GetCurrentGravityInfoIndex( idEntity* entity ) const {
	return GetCurrentGravityInfoIndex( entity->GetPhysics()->GetOrigin() );
}

/*
===================
idGameLocal::GetCurrentGravity
===================
*/
const idVec3 idGameLocal::GetCurrentGravity( idEntity* entity ) const {
	int index = GetCurrentGravityInfoIndex( entity );
	return (index >= 0) ? gravityInfo[ index ]->GetGravity(entity) : GetGravity();
}

/*
===================
idGameLocal::GetCurrentGravity
===================
*/
const idVec3 idGameLocal::GetCurrentGravity( const idVec3& origin, const idMat3& axis ) const {
	int index = GetCurrentGravityInfoIndex( origin );
	return (index >= 0) ? gravityInfo[ index ]->GetGravity(origin, axis, MASK_SOLID, NULL) : GetGravity();
}

/*
===================
idGameLocal::InGravityArea
===================
*/
bool idGameLocal::InGravityArea( rvClientEntity* entity ) const {
	return GetCurrentGravityInfoIndex( entity ) >= 0;
}

/*
===================
idGameLocal::GetCurrentGravityInfoIndex
===================
*/
int idGameLocal::GetCurrentGravityInfoIndex( rvClientEntity* entity ) const {
	return GetCurrentGravityInfoIndex( entity->GetPhysics()->GetOrigin() );
}

/*
===================
idGameLocal::GetCurrentGravity
===================
*/
const idVec3 idGameLocal::GetCurrentGravity( rvClientEntity* entity ) const {
	int index = GetCurrentGravityInfoIndex( entity );
	return (index >= 0) ? gravityInfo[ index ]->GetGravity(entity) : GetGravity();
}

/*
===================
idGameLocal::ReferenceScriptObjectProxy
===================
*/
idEntity* idGameLocal::ReferenceScriptObjectProxy( const char* scriptObjectName ) {
	idEntity*		proxy = NULL;
	idEntityPtr<idEntity> safeProxy;
	idDict			args;
	idScriptObject* object = NULL;

	for( int ix = 0; ix < scriptObjectProxies.Num(); ++ix ) {
		proxy = scriptObjectProxies[ ix ].GetEntity();
		assert( proxy );
		
		object = &proxy->scriptObject;
		if( !object->data ) {
			object->SetType( scriptObjectName );
			proxy->ConstructScriptObject();
			return proxy;
		}
	}

	args.Set( "classname", "func_static" );
	args.Set( "scriptobject", scriptObjectName );
	args.SetBool( "noclipmodel", true );
	bool spawned = SpawnEntityDef(args, &proxy);
	if ( !spawned ) {
		assert( 0 );
	}
	safeProxy = proxy;
	scriptObjectProxies.AddUnique( safeProxy );
	return proxy;
}

/*
===================
idGameLocal::ReleaseScriptObjectProxy
===================
*/
void idGameLocal::ReleaseScriptObjectProxy( const char* proxyName ) {
	idScriptObject* object = NULL;
	idEntity*		entity = NULL;

	for( int ix = 0; ix < scriptObjectProxies.Num(); ++ix ) {
		entity = scriptObjectProxies[ ix ].GetEntity();
		if( entity && !idStr::Icmp(entity->GetName(), proxyName) ) {
			object = &entity->scriptObject;
			if( !object ) {
				continue;
			}
			
			entity->DeconstructScriptObject();
			object->Free();
		}
	}
}

// RAVEN BEGIN
// rjohnson: entity usage stats
void idGameLocal::ListEntityStats( const idCmdArgs &args ) {
	int				i, j;
	idStr			currentMap;
	idList<idStr>	uniqueMapNames;


	for( i = 1; i < args.Argc(); i++ ) {
		if ( idStr::Icmp( args.Argv( i ), "clear" ) == 0 ) {
			entityUsageList.Clear();
			common->Printf("Entity stats cleared.\n");
			return;
		}
	}

	for( i = 0; i < entityUsageList.Num(); i++ ) {
		entityUsageList[ i ].SetInt( "reported_stat", false );
	}

	for( i = 0; i < entityUsageList.Num(); i++ ) {
		idStr	mapFileName, className;
		int		count;

		if ( entityUsageList[ i ].GetInt( "reported_stat" ) ) {
			continue;
		}

		entityUsageList[ i ].GetString( "mapFileName", "none", mapFileName );
		if ( currentMap != mapFileName )
		{
			if ( i ) {
				common->Printf( "\n" );
			}
			common->Printf( "================ %s ================\n", mapFileName.c_str() );
			currentMap = mapFileName;
			uniqueMapNames.Insert( mapFileName );
		}

		entityUsageList[ i ].GetString( "classname", "none", className );
		count = 0;

		for( j = i; j < entityUsageList.Num(); j++ ) {
			idStr	checkMapFileName, checkClassName;

			entityUsageList[ j ].GetString( "mapFileName", "none", checkMapFileName );
			if ( checkMapFileName != mapFileName ) {
				break;
			}

			entityUsageList[ j ].GetString( "classname", "none", checkClassName );

			if ( checkClassName == className ) {
				entityUsageList[ j ].SetInt( "reported_stat", 1 );
				count++;
			}
		}

		common->Printf("%d\t%s\n", count, className.c_str() );
	}

	common->Printf( "\n" );
	common->Printf( "\n" );
	common->Printf( "================ OVERALL ================\n" );

	for( i = 0; i < entityUsageList.Num(); i++ ) {
		idStr	mapFileName, className;
		int		count;

		if ( entityUsageList[ i ].GetInt( "reported_stat" ) == 2 ) {
			continue;
		}

		entityUsageList[ i ].GetString( "classname", "none", className );
		count = 0;

		for( j = i; j < entityUsageList.Num(); j++ ) {
			idStr	checkClassName;

			entityUsageList[ j ].GetString( "classname", "none", checkClassName );

			if ( checkClassName == className ) {
				entityUsageList[ j ].SetInt( "reported_stat", 2 );
				count++;
			}
		}

		common->Printf("%d\t%s\n", count, className.c_str() );
	}

	idFile *FH = fileSystem->OpenFileWrite( "EntityStats.csv" );
	if ( FH ) {
		int	size = sizeof( int ) * uniqueMapNames.Num();
		int	*count = ( int * )_alloca( size );

		FH->Printf("\"Definition\"");
		for( i = 0; i < uniqueMapNames.Num(); i++ ) {
			FH->Printf( ",\"%s\"", uniqueMapNames[ i ].c_str() );
		}
		FH->Printf(",Total\n");

		for( i = 0; i < entityUsageList.Num(); i++ ) {
			idStr	className;
			int		total;

			if ( entityUsageList[ i ].GetInt( "reported_stat" ) == 3 ) {
				continue;
			}

			entityUsageList[ i ].GetString( "classname", "none", className );

			memset( count, 0, size );
			for( j = i; j < entityUsageList.Num(); j++ )
			{
				idStr	checkMapFileName, checkClassName;

				entityUsageList[ j ].GetString( "classname", "none", checkClassName );

				if ( checkClassName == className ) {
					entityUsageList[ j ].SetInt( "reported_stat", 3 );
					entityUsageList[ j ].GetString( "mapFileName", "none", checkMapFileName );

					int loc = uniqueMapNames.FindIndex( checkMapFileName );
					if ( loc >= 0 ) {
						count[ loc ]++;
					}
				}
			}

			total = 0;
			FH->Printf( "\"%s\"", className.c_str() );
			for( j = 0; j < uniqueMapNames.Num(); j++ ) {
				FH->Printf( ",%d", count[ j ] );
				total += count[ j ];
			}
			FH->Printf( ",%d\n", total );
		}

		fileSystem->CloseFile( FH );
	}
}
// RAVEN END

/*
======================
idGameLocal::SpreadLocations

Now that everything has been spawned, associate areas with location entities
======================
*/
void idGameLocal::SpreadLocations() {
	idEntity *ent;

// RAVEN BEGIN
	if( !gameRenderWorld ) {
		common->Error( "GameRenderWorld is NULL!" );
	}
// RAVEN END

	// allocate the area table
	int	numAreas = gameRenderWorld->NumAreas();
	locationEntities = new idLocationEntity *[ numAreas ];
	memset( locationEntities, 0, numAreas * sizeof( *locationEntities ) );

	// for each location entity, make pointers from every area it touches
	for( ent = spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( !ent->IsType( idLocationEntity::GetClassType() ) ) {
// RAVEN END
			continue;
		}
		idVec3	point = ent->spawnArgs.GetVector( "origin" );
		int areaNum = gameRenderWorld->PointInArea( point );
		if ( areaNum < 0 ) {
			Printf( "SpreadLocations: location '%s' is not in a valid area\n", ent->spawnArgs.GetString( "name" ) );
			continue;
		}
		if ( areaNum >= numAreas ) {
			Error( "idGameLocal::SpreadLocations: areaNum >= gameRenderWorld->NumAreas()" );
		}
		if ( locationEntities[areaNum] ) {
			Warning( "location entity '%s' overlaps '%s'", ent->spawnArgs.GetString( "name" ),
				locationEntities[areaNum]->spawnArgs.GetString( "name" ) );
			continue;
		}
		locationEntities[areaNum] = static_cast<idLocationEntity *>(ent);

		// spread to all other connected areas
		for ( int i = 0 ; i < numAreas ; i++ ) {
			if ( i == areaNum ) {
				continue;
			}
			if ( gameRenderWorld->AreasAreConnected( areaNum, i, PS_BLOCK_LOCATION ) ) {
				locationEntities[i] = static_cast<idLocationEntity *>(ent);
			}
		}
	}
}

/*
===================
idGameLocal::AddLocation
===================
*/
idLocationEntity* idGameLocal::AddLocation( const idVec3& point, const char* name ) {
	int areaNum = gameRenderWorld->PointInArea( point );
	if ( areaNum < 0 ) {
		Warning ( "idGameLocal::AddLocation: cannot add location entity '%s' at '%g %g %g'\n", name, point.x, point.y, point.z );
		return NULL;
	}
	if ( areaNum >= gameRenderWorld->NumAreas() ) {
		Error( "idGameLocal::AddLocation: areaNum >= gameRenderWorld->NumAreas()" );
	}
	if ( locationEntities[areaNum] ) {
		Warning ( "idGameLocal::AddLocation: location '%s' already exists at '%g %g %g'\n", locationEntities[areaNum]->GetName(), point.x, point.y, point.z );
		return NULL;
	}

	// Spawn the new location entity
	idDict args;
	args.Set ( "location", name );
	locationEntities[areaNum] = static_cast<idLocationEntity*>(SpawnEntityType ( idLocationEntity::GetClassType(), &args ));

	// spread to all other connected areas
	for ( int i = gameRenderWorld->NumAreas() - 1 ; i >= 0 ; i-- ) {
		if ( i == areaNum ) {
			continue;
		}
		if ( gameRenderWorld->AreasAreConnected( areaNum, i, PS_BLOCK_LOCATION ) ) {
			locationEntities[i] = static_cast<idLocationEntity *>(locationEntities[areaNum]);
		}
	}
	
	return locationEntities[areaNum];
}

/*
===================
idGameLocal::LocationForPoint

The player checks the location each frame to update the HUD text display
May return NULL
===================
*/
idLocationEntity *idGameLocal::LocationForPoint( const idVec3 &point ) {
	if ( !locationEntities ) {
		// before SpreadLocations() has been called
		return NULL;
	}

	int areaNum = gameRenderWorld->PointInArea( point );
	if ( areaNum < 0 ) {
		return NULL;
	}
	if ( areaNum >= gameRenderWorld->NumAreas() ) {
		Error( "idGameLocal::LocationForPoint: areaNum >= gameRenderWorld->NumAreas()" );
	}

	return locationEntities[ areaNum ];
}

/*
============
idGameLocal::SetPortalState
============
*/
void idGameLocal::SetPortalState( qhandle_t portal, int blockingBits ) {
	idBitMsg outMsg;
	byte msgBuf[ MAX_GAME_MESSAGE_SIZE ];

	if ( !gameLocal.isClient ) {
		outMsg.Init( msgBuf, sizeof( msgBuf ) );
		outMsg.WriteByte( GAME_RELIABLE_MESSAGE_PORTAL );
		outMsg.WriteLong( portal );
		outMsg.WriteBits( blockingBits, NUM_RENDER_PORTAL_BITS );
		networkSystem->ServerSendReliableMessage( -1, outMsg );
	}
	gameRenderWorld->SetPortalState( portal, blockingBits );
}

/*
============
idGameLocal::sortSpawnPoints
============
*/
int idGameLocal::sortSpawnPoints( const void *ptr1, const void *ptr2 ) {
	const spawnSpot_t *spot1 = static_cast<const spawnSpot_t *>( ptr1 );
	const spawnSpot_t *spot2 = static_cast<const spawnSpot_t *>( ptr2 );
	float diff;

	diff = spot1->dist - spot2->dist;
	if ( diff < 0.0f ) {
		return 1;
	} else if ( diff > 0.0f ) {
		return -1;
	} else {
		return 0;
	}
}

// RAVEN BEGIN
// ddynerman: new gametype specific spawn code
// TODO this should be moved to idMultiplayerGame
/*
===========
idGameLocal::InitializeSpawns
randomize the order of the initial spawns
prepare for a sequence of initial player spawns
============
*/
void idGameLocal::InitializeSpawns( void ) {
	idEntity* spot = NULL;

	// initialize the spawns for clients as well, need them for free fly demo replays
	if ( !isMultiplayer ) {
		return;
	}

	spawnSpots.Clear();

	for( int i = 0; i < TEAM_MAX; i++ ) {
		teamSpawnSpots[i].Clear();
	}
	
	spot = FindEntityUsingDef( NULL, "info_player_team" );
	while( spot ) {
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if( spot->IsType ( idPlayerStart::GetClassType() ) ) {
// RAVEN END
			if( !idStr::Icmp(spot->spawnArgs.GetString("team"), "strogg") ) {
				teamSpawnSpots[TEAM_STROGG].Append( static_cast<idPlayerStart*>(spot) );
			} else if( !idStr::Icmp(spot->spawnArgs.GetString("team"), "marine") ) {
				teamSpawnSpots[TEAM_MARINE].Append( static_cast<idPlayerStart*>(spot) );
			}

			// spawnSpots contains info_player_team as well as info_player_deathmatch
			spawnSpots.Append ( static_cast<idPlayerStart*>(spot) );
			
		}

		spot = FindEntityUsingDef( spot, "info_player_team" );
	}

	spot = FindEntityUsingDef( NULL, "info_player_deathmatch" );
	while( spot ) {
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if( spot->IsType ( idPlayerStart::GetClassType() ) ) {
// RAVEN END
			spawnSpots.Append ( static_cast<idPlayerStart*>(spot) );
		}
		spot = FindEntityUsingDef( spot, "info_player_deathmatch" );
	}

	while( spot ) {
		// RAVEN BEGIN
		// jnewquist: Use accessor for static class type 
		if( spot->IsType ( idPlayerStart::GetClassType() ) ) {
			// RAVEN END
			if( !idStr::Icmp(spot->spawnArgs.GetString("team"), "strogg") ) {
				teamSpawnSpots[TEAM_STROGG].Append( static_cast<idPlayerStart*>(spot) );
			} else if( !idStr::Icmp(spot->spawnArgs.GetString("team"), "marine") ) {
				teamSpawnSpots[TEAM_MARINE].Append( static_cast<idPlayerStart*>(spot) );
			}

			// spawnSpots contains info_player_team as well as info_player_deathmatch
			spawnSpots.Append ( static_cast<idPlayerStart*>(spot) );

		}

		spot = FindEntityUsingDef( spot, "info_player_team" );
	}

	if( IsFlagGameType() && ( teamSpawnSpots[ TEAM_STROGG ].Num() == 0 || teamSpawnSpots[ TEAM_MARINE ].Num() == 0 ) ) {
		Error( "InitializeSpawns() - Map must have at least one Marine and one Strogg spawn for CTF gametype.");
	}

	if( spawnSpots.Num() == 0 ) {
		Error( "InitializeSpawns() - Map must have a spawn spot." );
	}

	common->Printf( "%d general spawns\n", spawnSpots.Num() );
	common->Printf( "%d team spawns (%d strogg/%d marine)\n", teamSpawnSpots[TEAM_STROGG].Num() + teamSpawnSpots[TEAM_MARINE].Num(),
														 teamSpawnSpots[TEAM_STROGG].Num(), teamSpawnSpots[TEAM_MARINE].Num());
}

/*
===========
idGameLocal::UpdateForwardSpawn
ddynerman: Updates forward spawn lists
===========
*/
void idGameLocal::UpdateForwardSpawns( rvCTFAssaultPlayerStart* point, int team ) {
	teamForwardSpawnSpots[ team ].Append( point );
}

/*
===========
idGameLocal::ClearForwardSpawn
ddynerman: Clears forward spawn lists
===========
*/
void idGameLocal::ClearForwardSpawns( void ) {
	for( int i = 0; i < TEAM_MAX; i++ ) {
		teamForwardSpawnSpots[ i ].Clear();
	}
}

/*
===========
idGameLocal::SpotWouldTelefrag
===========
*/
bool idGameLocal::SpotWouldTelefrag( idPlayer* player, idPlayerStart* spawn ) {
	idPlayer*	playerList[ MAX_CLIENTS ];
	idBounds	bound = player->GetPhysics()->GetBounds();

	bound.TranslateSelf( spawn->GetPhysics()->GetOrigin() );
	int numEntities = PlayersTouchingBounds( player, bound, CONTENTS_BODY, playerList, MAX_CLIENTS );

	return !( numEntities == 0 );
}

/*
===========
idGameLocal::SelectSpawnSpot
ddynerman: Selects a spawn spot randomly from spots furthest from the player
			This is taken from q3
===========
*/
idEntity* idGameLocal::SelectSpawnPoint( idPlayer* player ) {
	if( !isMultiplayer ) {
		idEntity* ent = FindEntityUsingDef( NULL, "info_player_start" );
		if ( !ent ) {
			Error( "No info_player_start on map.\n" );
		}
		return ent;
	}

	if ( player == NULL ) {
		return NULL;
	}

	// Give spectators any old random spot
	if ( player->team < 0 || player->team >= TEAM_MAX || player->spectating ) {
		common->DPrintf("Returning a random spot\n");
		return spawnSpots[ random.RandomInt( spawnSpots.Num() ) ];
	}

	idList<spawnSpot_t> weightedSpawns;
	idList<idPlayerStart*>* spawnArray = NULL;

	// Pick which spawns to use based on gametype
// RITUAL BEGIN
// squirrel: added DeadZone multiplayer mode
// RITUAL END
// openQ4: this used to be two hard-coded gametype lists with no fallback, so
// any gametype not named in either one hit a fatal error the first time a
// player spawned.  Modes built around team bases prefer their own base
// spawns; everything else, and any base mode on a map with no team spawns,
// uses the general list.
	if( MPGameTypeHasAny( gameType, GTF_ANY_OBJECTIVE ) ) {
		if( teamForwardSpawnSpots[ player->team ].Num() ) {
			spawnArray = &teamForwardSpawnSpots[ player->team ];
		} else if( teamSpawnSpots[ player->team ].Num() ) {
			spawnArray = &teamSpawnSpots[ player->team ];
		}
	}

	if ( spawnArray == NULL || spawnArray->Num() == 0 ) {
		spawnArray = &spawnSpots;
	}

	if ( spawnArray->Num() == 0 ) {
		Error( "SelectSpawnPoint() - no spawn points on this map." );
		return NULL;
	}

	idVec3 refPos;
	if ( player->lastKiller != NULL && !player->lastKiller->spectating && player->lastKiller->GetInstance() == player->GetInstance() ) {
		refPos = player->lastKiller->GetPhysics()->GetOrigin();
	} else {
		refPos = player->GetPhysics()->GetOrigin();
	}

	for ( int i = 0; i < spawnArray->Num(); i++ ) {
		idPlayerStart* spot = (*spawnArray)[i];

		if ( spot->GetInstance() != player->GetInstance() || SpotWouldTelefrag( player, spot ) ) {
			continue;
		}

		idVec3	pos = spot->GetPhysics()->GetOrigin();
		float	dist = ( pos - refPos ).LengthSqr();

		spawnSpot_t	newSpot;
		
		newSpot.dist = dist;
		newSpot.ent = (*spawnArray)[ i ];
		weightedSpawns.Append( newSpot );
	}

	if ( weightedSpawns.Num() == 0 ) {
		// no spawns avaialable, spawn randomly
		common->DPrintf("no spawns avaialable, spawn randomly\n");
		return (*spawnArray)[ random.RandomInt( spawnArray->Num() ) ];
	}

	qsort( ( void * )weightedSpawns.Ptr(), weightedSpawns.Num(), sizeof( spawnSpot_t ), ( int (*)(const void *, const void *) )sortSpawnPoints );

	int rnd = rvRandom::flrand( 0.0, 1.0 ) * (weightedSpawns.Num() / 2);
	return weightedSpawns[ rnd ].ent;
}
/*
================
idGameLocal::UpdateServerInfoFlags
================
*/
// RAVEN BEGIN
// ddynerman: new gametype strings
void idGameLocal::SetGameType( void ) {
	gameType = GAME_SP;

	if ( idStr::Icmp( serverInfo.GetString( "si_gameType" ), "singleplayer" ) ) {
		mpGame.SetGameType();
	}
}
// RAVEN END

/*
================
idGameLocal::SetGlobalMaterial
================
*/
void idGameLocal::SetGlobalMaterial( const idMaterial *mat ) {
	globalMaterial = mat;
}

/*
================
idGameLocal::GetGlobalMaterial
================
*/
const idMaterial *idGameLocal::GetGlobalMaterial() {
	return globalMaterial;
}

/*
================
idGameLocal::GetSpawnId
================
*/
int idGameLocal::GetSpawnId( const idEntity* ent ) const {
	return PackEntitySpawnId( gameLocal.spawnIds[ ent->entityNumber ], ent->entityNumber );
}

// Buffered observations of the frozen scene clock, including its exact host
// sample and integer game-step boundary. The first observation is not an anchor
// checkpoint: offline replay must account for the preceding clock state.
// Printing is deferred until collection ends to avoid changing each draw's pace.
static const int MAX_PRESENTATION_CLOCK_SAMPLES = 8192;
struct presentationClockSample_t {
	double	realTimeHighRes;
	int		gameStep, rate;
	float	drawnYaw, authoritativeYaw, lookYaw, inputYaw, drawnZ;
	int		look;
	int		clientDebris, afBlends, listed, afFrames, gameplay, bse;
	float	prepMs;
	float	clockFraction;
	int		realTime;
	int		gameTime;
	float	fraction;
};
static presentationClockSample_t presentationClockCaptureSamples[ MAX_PRESENTATION_CLOCK_SAMPLES ];
static int presentationClockCaptureCount = 0;
static bool presentationClockCaptureDumped = false;

/*
================
idGameLocal::NotePresentationGameplay

A prepared draw (PreparePlayerSceneForRender to EndPresentationSceneForRender)
may move what is drawn but must never dispatch events, run scripts, step
physics or start effects: repeated draws, stereo eyes and subviews would run
them again. Entry points call this; inside a scene it counts the breach for
g_presentationClockCapture and names the first few.
================
*/
void idGameLocal::NotePresentationGameplay( const char *what ) {
	if ( presentationClockSample < 0.0f ) {
		return;
	}
	presentationGameplayCount++;
	static int reported = 0;
	if ( reported < 8 ) {
		reported++;
		Printf( "presentationGameplay: %s ran inside a prepared draw\n", what );
	}
}

/*
================
idGameLocal::NotePresentationEffect

PlayEffect's guard. The renderer services BSE inside RenderScene, and a particle
impact or nested segment may start an effect through idGame::PlayEffect there,
once per particle or authoritative effect step rather than once per draw. Those
are counted apart (bse=); any other effect started inside a prepared draw is
gameplay.
================
*/
void idGameLocal::NotePresentationEffect( void ) {
	if ( presentationClockSample < 0.0f ) {
		return;
	}
	if ( presentationRendererDepth > 0 ) {
		presentationRendererEffectCount++;
		return;
	}
	NotePresentationGameplay( "idGameLocal::PlayEffect" );
}

/*
================
idGameLocal::RecordPresentationClockSample
================
*/
void idGameLocal::RecordPresentationClockSample( void ) {
	const int wanted = idMath::ClampInt( 0, MAX_PRESENTATION_CLOCK_SAMPLES,
		g_presentationClockCapture.GetInteger() );
	if ( wanted <= 0 ) {
		presentationClockCaptureCount = 0;
		presentationClockCaptureDumped = false;
		return;
	}
	if ( presentationClockCaptureDumped || presentationSceneFraction < 0.0f ) {
		return;
	}

	presentationClockSample_t &sample =
		presentationClockCaptureSamples[ presentationClockCaptureCount++ ];
	sample.realTimeHighRes = presentationClock.SampleTimeMilliseconds();
	sample.gameStep = GetMSec();
	sample.rate = common->GetUserCmdHz();
	sample.clockFraction = presentationClockSample;
	sample.realTime = Sys_Milliseconds();
	sample.gameTime = time;
	sample.fraction = presentationSceneFraction;
	sample.drawnYaw = presentationLookDiagnostic.drawnYaw;
	sample.drawnZ = presentationLookDiagnostic.drawnZ;
	// the camera, look, entity poses and client debris prepared for this draw
	sample.prepMs = static_cast<float>( ( idLib::sys->GetClockTicks() - presentationPrepStartTicks ) * 1000.0 /
		idLib::sys->ClockTicksPerSecond() );
	sample.authoritativeYaw = presentationLookDiagnostic.authoritativeYaw;
	sample.lookYaw = presentationLookDiagnostic.lookYaw;
	sample.inputYaw = presentationLookDiagnostic.inputYaw;
	sample.look = presentationLookDiagnostic.look;
	sample.clientDebris = presentationClientEntities.Num();
	sample.afBlends = idAnimator::presentationAFBlendCount;
	idAnimator::presentationAFBlendCount = 0;
	sample.listed = presentationEntities.Num();
	sample.afFrames = idAnimator::presentationAFFrameCount;
	idAnimator::presentationAFFrameCount = 0;
	sample.gameplay = presentationGameplayCount;
	presentationGameplayCount = 0;
	sample.bse = presentationRendererEffectCount;
	presentationRendererEffectCount = 0;
	if ( presentationClockCaptureCount < wanted ) {
		return;
	}

	presentationClockCaptureDumped = true;
	Printf( "presentationClock: %d samples\n", presentationClockCaptureCount );
	for ( int i = 0; i < presentationClockCaptureCount; i++ ) {
		Printf( "presentationClockSample rt=%d t=%d f=%.4f hrt=%.6f step=%d hz=%d phase=%.9f drawYaw=%.4f authYaw=%.4f look=%d lookYaw=%.4f inYaw=%.4f debris=%d afBlends=%d listed=%d afFrames=%d gameplay=%d bse=%d drawZ=%.3f prepMs=%.4f\n",
			presentationClockCaptureSamples[ i ].realTime,
			presentationClockCaptureSamples[ i ].gameTime,
			presentationClockCaptureSamples[ i ].fraction,
			presentationClockCaptureSamples[ i ].realTimeHighRes,
			presentationClockCaptureSamples[ i ].gameStep,
			presentationClockCaptureSamples[ i ].rate,
			presentationClockCaptureSamples[ i ].clockFraction,
			presentationClockCaptureSamples[ i ].drawnYaw,
			presentationClockCaptureSamples[ i ].authoritativeYaw,
			presentationClockCaptureSamples[ i ].look,
			presentationClockCaptureSamples[ i ].lookYaw,
			presentationClockCaptureSamples[ i ].inputYaw,
			presentationClockCaptureSamples[ i ].clientDebris,
			presentationClockCaptureSamples[ i ].afBlends,
			presentationClockCaptureSamples[ i ].listed,
			presentationClockCaptureSamples[ i ].afFrames,
			presentationClockCaptureSamples[ i ].gameplay,
			presentationClockCaptureSamples[ i ].bse,
			presentationClockCaptureSamples[ i ].drawnZ,
			presentationClockCaptureSamples[ i ].prepMs );
	}
	presentationClockCaptureCount = 0;
}

/*
================
idGameLocal::GetPresentationTicFraction

How far real time has advanced into the current authoritative tic, in [0,1].
The anchors are transient: map loads and restores reseed them instead of
changing save-game or network state.

The anchor is *predicted* forward one tic at a time; it is not re-sampled from
the clock every time `time` changes.  Re-sampling looks right and is not.  The
only code that ever observes a new tic is a draw frame, and a draw frame lands
somewhere in [0, one render interval) after the boundary that produced the tic --
a different lag every tic.  Re-anchoring there therefore injects a fresh phase
error every tic, so everything drawn from this fraction carries a 60 Hz sawtooth
whose amplitude is a whole render interval of motion.  It is worst at exactly the
frame rates that put several draws inside one tic, which is why the view weapon
visibly stuttered the faster the renderer ran.

Predicting forward keeps the phase.  The observation is then used only to correct
drift, and asymmetrically, because its error is one-sided: a tic can be seen late
by scheduling noise but never early, so an observation earlier than the predicted
boundary is real information about a faster clock and is followed at once, while
lateness is corrected at a strict crawl.  The anchor settles on the minimum
observed lag, which is the boundary itself.
================
*/
float idGameLocal::GetPresentationTicFraction( void ) const {
	if ( presentationClockSample >= 0.0f ) {
		return presentationClockSample;
	}
	const double realTime = idLib::sys->GetClockTicks() * 1000.0 / idLib::sys->ClockTicksPerSecond();
	return presentationClock.Phase( time, Max( 1, GetMSec() ), common->GetUserCmdHz(), realTime,
		!isMultiplayer && g_stopTime.GetBool() );
}

/*
================
idGameLocal::GetPresentationTimeMsec

Maps the engine's presentation clock onto the current simulation snapshot.
================
*/
int idGameLocal::GetPresentationTimeMsec( void ) const {
	if ( ( !isMultiplayer && g_stopTime.GetBool() ) || GetDemoState() == DEMO_PLAYING || IsTimeDemo() ) {
		return time;
	}

	return presentationClock.MaterialTime( time, Max( 0, GetMSec() ), GetPresentationTicFraction() );
}

/*
================
idGameLocal::GetPresentationInterpolationFraction
================
*/
float idGameLocal::GetPresentationInterpolationFraction( void ) const {
	if ( GetDemoState() == DEMO_PLAYING || IsTimeDemo() ) {
		return 1.0f;
	}

	// A newly anchored tic starts at the previous authoritative pose and
	// reaches the current pose over one usercmd interval.  That deliberate,
	// bounded latency avoids extrapolating the camera through collisions.
	if ( presentationSceneFraction >= 0.0f ) {
		return presentationSceneFraction;
	}

	return GetPresentationTicFraction();
}

/*
================
idGameLocal::ClearPresentationEntityPoses

Drop every entity out of the presentation list, handing any interpolated pose
still in the renderer back to its authoritative value first.
================
*/
void idGameLocal::ClearPresentationEntityPoses( void ) {
	idEntity *ent;
	idEntity *next;

	for ( ent = presentationEntities.Next(); ent != NULL; ent = next ) {
		next = ent->presentationNode.Next();
		ent->RestoreAuthoritativePresentationPose();
		ent->presentationNode.Remove();
	}

	rvClientEntity *cent;
	rvClientEntity *nextCent;
	for ( cent = presentationClientEntities.Next(); cent != NULL; cent = nextCent ) {
		nextCent = cent->presentationNode.Next();
		cent->RestoreAuthoritativePresentationPose();
		cent->presentationNode.Remove();
	}
}

/*
================
idGameLocal::SamplePresentationEntityPoses

Take the authoritative 60 Hz transform sample for entities that can have
changed this tic. Keep last frame's presentation members until they settle so
their authoritative pose is restored, then admit newly active movers. Run once
per game frame, after all thinking and pushing is finished.
================
*/
void idGameLocal::SamplePresentationEntityPoses( void ) {
	idEntity *ent;
	idEntity *next;

	// Cinematic fast-forward can execute many thousands of simulation tics in a
	// single host frame. None of those intermediate poses is presented. Avoid a
	// full spawned-entity walk for every skipped tic and discard any pose that
	// was pushed by the last visible frame.
	if ( skipCinematic || !g_presentationInterpolation.GetBool() || GetDemoState() == DEMO_PLAYING || IsTimeDemo() ) {
		ClearPresentationEntityPoses();
		return;
	}

	// A member that moved or animated last frame must be sampled once more even
	// if it deactivated, otherwise its final interpolated pose could remain in
	// the render world. Remove it only after SamplePresentationPose restores or
	// rejects it.
	for ( ent = presentationEntities.Next(); ent != NULL; ent = next ) {
		next = ent->presentationNode.Next();
		if ( !ent->SamplePresentationPose() ) {
			ent->presentationNode.Remove();
		}
	}

	// Active entities are the authoritative roots that can move, animate, or
	// update visuals this tic. An active physics team master also moves bound
	// team members from its own RunPhysics walk, even when those members have no
	// independent think flags, so include that bounded chain. Active slaves are
	// skipped when their active master already owns the chain; an independently
	// active slave of an inactive master still samples itself.
	for ( ent = activeEntities.Next(); ent != NULL; ent = ent->activeNode.Next() ) {
		idEntity *teamMaster = ent->GetTeamMaster();
		if ( teamMaster != NULL && teamMaster != ent && teamMaster->IsActive() ) {
			continue;
		}
		idEntity *candidate = ent;
		do {
			if ( !candidate->presentationNode.InList()
					&& candidate->SamplePresentationPose() ) {
				candidate->presentationNode.AddToEnd( presentationEntities );
			}
			candidate = teamMaster == ent
				? candidate->GetNextTeamEntity() : NULL;
		} while ( candidate != NULL );
	}
}

/*
================
idGameLocal::UpdatePresentationEntityPoses

Presentation-only pass.  Re-anchors everything that moved this tic onto the same
interpolated presentation time the first-person camera is drawn at.
================
*/
void idGameLocal::UpdatePresentationEntityPoses( void ) {
	idEntity *ent;
	idEntity *next;

	int updated = 0;
	for ( ent = presentationEntities.Next(); ent != NULL; ent = next ) {
		next = ent->presentationNode.Next();
		ent->UpdatePresentationPose();
		updated++;
	}

	// Unbound client debris, brass and gibs, which run their own physics. They
	// list themselves from Think() while they move; see rvClientMoveable.
	rvClientEntity *cent;
	rvClientEntity *nextCent;
	for ( cent = presentationClientEntities.Next(); cent != NULL; cent = nextCent ) {
		nextCent = cent->presentationNode.Next();
		cent->UpdatePresentationPose();
	}

	// Whether the local player's own body reached the presentation clock is not
	// observable from outside: in first person its surfaces are suppressed, so the
	// only witness is its shadow.  Report it directly rather than inferring it.
	if ( g_showPresentationPose.GetBool() ) {
		idPlayer *localPlayer = GetLocalPlayer();
		if ( localPlayer != NULL ) {
			// Per FRAME, not per tic: the point is to see whether the drawn body
			// advances between authoritative tics or holds still and then jumps.
			// The root comes from the interpolated presentation pose; a joint mod is
			// whatever CreatePresentationFrame handed the skeleton this frame.
			idVec3 rootOrigin;
			idMat3 rootAxis;
			const bool haveRoot = localPlayer->GetPresentationPose( rootOrigin, rootAxis );
			float jointModYaw = 0.0f;
			int jointModCount = 0;
			idAnimator *animator = localPlayer->GetAnimator();
			if ( animator != NULL ) {
				jointModCount = animator->NumJointMods();
				int jointnum = 0;
				idMat3 modMat;
				if ( animator->GetJointModDiagnostic( 0, jointnum, modMat ) ) {
					jointModYaw = modMat.ToAngles().yaw;
				}
			}
			// The drawn skeleton itself: if this is constant between frames inside a
			// tic then the presentation pose never reaches the model, whatever the
			// root and the modifier are doing.
			idVec3 drawnJoint;
			bool haveDrawnJoint = false;
			if ( animator != NULL ) {
				const int probeJoint = animator->NumJoints() > 4 ? 4 : 0;
				haveDrawnJoint = animator->GetPresentationJointDiagnostic( probeJoint, drawnJoint );
			}
			// rt/t are the raw inputs the presentation clock is a function of, so a
			// capture can be replayed offline against any candidate clock.
			Printf( "presentationFrame: rt=%d t=%d f=%.3f rootYaw=%.3f (have=%d) jointMods=%d mod0Yaw=%.3f animT=%d drawn=%d %.3f %.3f %.3f\n",
				Sys_Milliseconds(), time,
				GetPresentationInterpolationFraction(),
				haveRoot ? rootAxis.ToAngles().yaw : 0.0f,
				haveRoot ? 1 : 0, jointModCount, jointModYaw,
				GetPresentationAnimationTimeMsec(),
				haveDrawnJoint ? 1 : 0, drawnJoint.x, drawnJoint.y, drawnJoint.z );
		}
		if ( localPlayer != NULL && time != lastPresentationPoseReportTime ) {
			lastPresentationPoseReportTime = time;
			Printf( "presentation: entities=%d localPlayer(listed=%d allows=%d moved=%d canInterp=%d) fraction=%.3f animTime=%d\n",
				updated,
				localPlayer->presentationNode.InList() ? 1 : 0,
				localPlayer->AllowsPresentationInterpolation() ? 1 : 0,
				localPlayer->PresentationPoseMoved() ? 1 : 0,
				localPlayer->CanInterpolatePresentationPose() ? 1 : 0,
				GetPresentationInterpolationFraction(),
				GetPresentationAnimationTimeMsec() );
		}
	}
}

/*
================
idGameLocal::InterpolatePresentationAxis
================
*/
idMat3 idGameLocal::InterpolatePresentationAxis( const idMat3 &from, const idMat3 &to, float fraction ) const {
	if ( fraction <= 0.0f ) {
		return from;
	}
	if ( fraction >= 1.0f ) {
		return to;
	}

	idQuat blended;
	blended.Slerp( from.ToQuat(), to.ToQuat(), fraction );
	return blended.ToMat3();
}

/*
================
idGameLocal::ThrottleUserInfo
================
*/
void idGameLocal::ThrottleUserInfo( void ) {
	mpGame.ThrottleUserInfo();
}

/*
===========
idGameLocal::ValidateServerSettings
============
*/
bool idGameLocal::ValidateServerSettings( const char* map, const char* gametype ) {
	// PickMap uses si_map directly
	// PickMap returns wether we would have to change the maps, which means settings are invalid
	assert( !idStr::Icmp( si_map.GetString(), map ) );
	if ( mpGame.PickMap( gametype, true ) ) {
		common->Printf( "map '%s' and gametype '%s' are not compatible\n", map, gametype );
		return false;
	}
	return true;
}

/*
===========
idGameLocal::NeedRestart
============
*/
bool idGameLocal::NeedRestart() {

	idDict		newInfo;
	const idKeyValue *keyval, *keyval2;

	newInfo = *cvarSystem->MoveCVarsToDict( CVAR_SERVERINFO );

	for ( int i = 0; i < newInfo.GetNumKeyVals(); i++ ) {
		keyval = newInfo.GetKeyVal( i );
		keyval2 = serverInfo.FindKey( keyval->GetKey() );
		if ( !keyval2 ) {
			return true;
		}
		// a select set of si_ changes will cause a full restart of the server
		if ( keyval->GetValue().Icmp( keyval2->GetValue() ) && ( !keyval->GetKey().Icmp( "si_pure" ) || !keyval->GetKey().Icmp( "si_map" ) || !keyval->GetKey().Icmp( "si_fps" ) ) ) {
			return true;
		}
	}
	return false;
}

// RAVEN BEGIN
// jshepard: update player hud to alert to end of level
/*
===================
idGameLocal::UpdateEndLevel
===================
*/
void idGameLocal::UpdateEndLevel()	{
	idPlayer * player = GetLocalPlayer();

	if( player && player->GetHud() ) {
		player->GetHud()->HandleNamedEvent( "showExit" );
	}
}


// bdube: added
/*
================
idGameLocal::GetEffect

Get the handle of the effect with the given name
================
*/
const idDecl *idGameLocal::GetEffect ( const idDict& args, const char* effectName, const rvDeclMatType* materialType ) {
	const char *effectFile = NULL;
	const idDecl* effectDecl = NULL;

	float chance = args.GetFloat ( idStr("effectchance ") + effectName, "1" );	
	if ( random.RandomFloat ( ) > chance ) {
		return NULL;
	}

	// we should ALWAYS be playing sounds from the def.
	// hardcoded sounds MUST be avoided at all times because they won't get precached.
	assert( !idStr::Icmpn( effectName, "fx_", 3 ) );

	if ( materialType )	{
		idStr		temp;
		const char*	result = NULL;
		
		temp = effectName;
		temp += "_";

		temp += materialType->GetName();
	
		// See if the given material effect is specified
		idStr	testMP = temp;
		testMP += "_mp";

		result = args.GetString( testMP );

		if ( !result || !*result ) {
			result = args.GetString( temp );
		}
		if ( result && *result) {
			effectDecl = ( const idDecl * )declManager->FindEffect( result, false );
			if ( effectDecl ) {
				return effectDecl;
			}
		}
	}	

	// grab the non material effect name
	idStr alternateEffect = effectName;
	alternateEffect += "_mp";
	
	idStr classname = args.GetString( "classname" );
	if ( !alternateEffect.Icmp( "fx_fly_mp" ) ) {
		if ( 
			( !g_nailTrail.GetBool() && !classname.Icmpn( "projectile_nail", 15   ) ) ||
			( !g_rocketTrail.GetBool() && !classname.Icmpn( "projectile_rocket", 17 ) ) || 
			( !g_napalmTrail.GetBool() && !classname.Icmpn( "projectile_napalm", 17 ) ) ) 
		{
			alternateEffect += "_low";
		} else if ( !g_grenadeTrail.GetBool() && !classname.Icmpn( "projectile_grenade", 18 ) ) 
		{
			return NULL;
		}
	} else if ( !alternateEffect.Icmp( "fx_path_mp" ) ) 
	{
		if ( !g_railTrail.GetBool() && !classname.Icmpn( "hitscan_railgun", 15 ) ) 
		{
			alternateEffect += "_low";
		}
	}

	effectFile = args.GetString( alternateEffect );
	if ( effectFile && *effectFile ) {
		effectDecl = ( const idDecl * )declManager->FindEffect( effectFile, false );
		if ( effectDecl ) {
			return effectDecl;
		}
	}

	effectFile = args.GetString( effectName );
	if ( effectFile && *effectFile ) {
		effectDecl = ( const idDecl * )declManager->FindEffect( effectFile, false );
		if ( effectDecl ) {
			return effectDecl;
		}
	}

	// Compatibility fallback: keep grenade launcher smoke trails available even
	// when projectile defs omit fx_fly/fx_fly_mp or point at a missing decl.
	if ( !classname.Icmpn( "projectile_grenade", 18 ) &&
		 ( !idStr::Icmp( effectName, "fx_fly" ) || !idStr::Icmp( effectName, "fx_fly_mp" ) ) ) {
		effectDecl = ( const idDecl * )declManager->FindEffect( "effects/weapons/grenadelauncher/trail_mp", false );
		if ( !effectDecl ) {
			effectDecl = ( const idDecl * )declManager->FindEffect( "effects/weapons/grenadelauncher/trail", false );
		}
		if ( effectDecl ) {
			return effectDecl;
		}
	}

	if ( !effectFile || !*effectFile ) {
		return NULL;
	}

	return( ( const idDecl * )declManager->FindEffect( effectFile ) );
}

// openQ4 BEGIN
/*
================
idGameLocal::LiquidContentsAtCollision

Returns the liquid content bits crossed by a collision, or zero if it was not a liquid surface.

The stock water test looked only at the hit entity's physics contents, which is true for a water
brush bound to an entity and never true for a world brush - so water built into the map, the normal
case, never registered. Testing the collision material as well fixes that, and covers lava and
slime at the same time.
================
*/
int idGameLocal::LiquidContentsAtCollision( const idEntity *hitEnt, const trace_t &collision ) const {
	int contents = 0;

	if ( hitEnt && hitEnt->GetPhysics() ) {
		contents |= hitEnt->GetPhysics()->GetContents();
	}
	if ( collision.c.material ) {
		contents |= collision.c.material->GetContentFlags();
	}

	return contents & MASK_WATER;
}

/*
================
idGameLocal::LiquidContentsAtPoint

Which liquid, if any, a world point is inside.
================
*/
int idGameLocal::LiquidContentsAtPoint( const idVec3 &point, const idEntity *passEntity ) {
	return Contents( passEntity, point, NULL, mat3_identity, MASK_WATER, passEntity ) & MASK_WATER;
}

/*
================
idGameLocal::LiquidBoundaryBetween

Finds the surface between a point known to be inside a liquid and one known to be outside. Point
contents is used instead of a collision trace because a trace that begins inside a contents brush
is start-solid and cannot report the exit face.
================
*/
idVec3 idGameLocal::LiquidBoundaryBetween( const idVec3 &inside, const idVec3 &outside, int liquidContents, const idEntity *passEntity ) {
	idVec3 insidePoint = inside;
	idVec3 outsidePoint = outside;

	for ( int i = 0; i < 12; i++ ) {
		const idVec3 middle = ( insidePoint + outsidePoint ) * 0.5f;
		if ( LiquidContentsAtPoint( middle, passEntity ) & liquidContents ) {
			insidePoint = middle;
		} else {
			outsidePoint = middle;
		}
	}

	return ( insidePoint + outsidePoint ) * 0.5f;
}

/*
================
idGameLocal::LiquidTypeName

The name used to build presentation keys, e.g. "fx_impact_lava". Lava wins over slime if a mapper
somehow sets both, because it is the more dangerous of the two.
================
*/
const char *idGameLocal::LiquidTypeName( int liquidContents ) const {
	if ( liquidContents & CONTENTS_LAVA ) {
		return "lava";
	}
	if ( liquidContents & CONTENTS_SLIME ) {
		return "slime";
	}
	return "water";
}

/*
================
idGameLocal::PlayLiquidImpact

Splash and sound for anything that breaks a liquid surface. The caller's own def is asked first, so
a weapon keeps whatever fx_impact_water it already ships, and the shared liquid_openq4 def fills in
for everything that specifies nothing.
================
*/
void idGameLocal::PlayLiquidImpact( int liquidContents, const idVec3 &point, const idVec3 &normal, idEntity *ent, const idDict *callerArgs, bool broadcast ) {
	if ( !liquidContents ) {
		return;
	}

	const char *liquid = LiquidTypeName( liquidContents );
	const idDict *liquidDict = FindEntityDefDict( "liquid_openq4", false );
	const idDecl *effect = NULL;
	bool callerEffect = false;

	if ( callerArgs ) {
		effect = GetEffect( *callerArgs, va( "fx_impact_%s", liquid ) );
		callerEffect = ( effect != NULL );
	}
	if ( !effect && liquidDict ) {
		effect = GetEffect( *liquidDict, va( "fx_impact_%s", liquid ) );
	}
	if ( effect ) {
		PlayEffect( effect, point, normal.ToMat3(), false, vec3_origin, broadcast );
	}

	// Stock weapon splash effects already carry their own sound. The shared openQ4 effects do not,
	// so give those one positional impact shader without doubling a weapon-authored splash.
	if ( ent && liquidDict && !callerEffect ) {
		const char *shaderName = liquidDict->GetString( va( "snd_impact_%s", liquid ), "" );
		if ( shaderName && shaderName[0] ) {
			const idSoundShader *shader = declManager->FindSound( shaderName, false );
			if ( shader ) {
				ent->StartSoundShader( shader, SND_CHANNEL_ANY, 0, broadcast, NULL );
			}
		}
	}
}
// openQ4 END

/*
================
idGameLocal::LiquidLevelForEntity

Quake 3's PM_SetWaterLevel for anything that is not the player. idPhysics_Player keeps its own
water level because it needs it inside the move; monsters, corpses and everything else have no such
state, so this derives the same 0-3 ladder from the entity's own bounds.
================
*/
int idGameLocal::LiquidLevelForEntity( idEntity *ent, int &liquidType ) {
	liquidType = 0;

	if ( !ent || !ent->GetPhysics() ) {
		return 0;
	}

	const idBounds &bounds = ent->GetPhysics()->GetBounds();
	const idVec3 origin = ent->GetPhysics()->GetOrigin();
	const float height = bounds[1].z - bounds[0].z;
	if ( height <= 0.0f ) {
		return 0;
	}

	idVec3 point = origin;

	// feet
	point.z = origin.z + bounds[0].z + 1.0f;
	const int feetContents = Contents( ent, point, NULL, mat3_identity, MASK_WATER, ent ) & MASK_WATER;
	if ( !feetContents ) {
		return 0;
	}
	liquidType = feetContents;

	// waist
	point.z = origin.z + bounds[0].z + height * 0.5f;
	if ( !( Contents( ent, point, NULL, mat3_identity, MASK_WATER, ent ) & MASK_WATER ) ) {
		return 1;
	}

	// head
	point.z = origin.z + bounds[1].z - 1.0f;
	if ( !( Contents( ent, point, NULL, mat3_identity, MASK_WATER, ent ) & MASK_WATER ) ) {
		return 2;
	}

	return 3;
}

/*
================
idGameLocal::PlayLiquidSoundOn

Plays one of the shared liquid_openq4 sounds on an entity. A missing def or key is silence, not an
error, so a partial asset set still works.
================
*/
void idGameLocal::PlayLiquidSoundOn( idEntity *ent, const char *key, const s_channelType channel ) {
	if ( !ent ) {
		return;
	}

	const idDict *liquidDict = FindEntityDefDict( "liquid_openq4", false );
	if ( !liquidDict ) {
		return;
	}

	const char *shaderName = liquidDict->GetString( key, "" );
	if ( !shaderName || !shaderName[0] ) {
		return;
	}

	const idSoundShader *shader = declManager->FindSound( shaderName, false );
	if ( !shader ) {
		return;
	}

	// broadcast: liquid events are decided on the server, because a client only runs full movement
	// for the player it predicts and would never see anything else break the surface
	ent->StartSoundShader( shader, channel, 0, true, NULL );
}

/*
================
idGameLocal::PlayLiquidEffectAt
================
*/
void idGameLocal::PlayLiquidEffectAt( const char *key, const idVec3 &origin ) {
	const idDict *liquidDict = FindEntityDefDict( "liquid_openq4", false );
	if ( !liquidDict ) {
		return;
	}

	const idDecl *effect = GetEffect( *liquidDict, key );
	if ( !effect ) {
		return;
	}

	PlayEffect( effect, origin, idVec3( 0.0f, 0.0f, 1.0f ).ToMat3(), false, vec3_origin, true );
}

/*
================
idGameLocal::PlayLiquidTrail

The wake something leaves crossing a liquid: bubbles, not smoke. Smoke does not survive underwater,
so a projectile trail or a tracer that is drawn there has to be replaced rather than tinted.

Returns false when this liquid has no trail authored, so the caller can fall back to whatever it
would normally have drawn.
================
*/
bool idGameLocal::PlayLiquidTrail( int liquidContents, const idVec3 &start, const idVec3 &end ) {
	if ( !liquidContents ) {
		return false;
	}

	const idDict *liquidDict = FindEntityDefDict( "liquid_openq4", false );
	if ( !liquidDict ) {
		return false;
	}

	const idDecl *effect = GetEffect( *liquidDict, va( "fx_trace_trail_%s", LiquidTypeName( liquidContents ) ) );
	if ( !effect ) {
		return false;
	}

	idVec3 dir = end - start;
	if ( dir.Normalize() <= 0.0f ) {
		dir.Set( 0.0f, 0.0f, 1.0f );
	}

	PlayEffect( effect, start, dir.ToMat3(), false, end, true );
	return true;
}

/*
================
idGameLocal::PlayLiquidTrailToExit

Draws only the part of a weapon trace that is inside liquid. The final weapon hit can be far beyond
the pool, so locate the exit face instead of stretching bubbles through the air after it.
================
*/
bool idGameLocal::PlayLiquidTrailToExit( int liquidContents, const idVec3 &start, const idVec3 &end, const idEntity *passEntity ) {
	if ( !liquidContents ) {
		return false;
	}

	idVec3 trailEnd = end;
	idVec3 direction = end - start;
	if ( direction.Normalize() > 0.0f ) {
		const idVec3 endProbe = end - direction * 0.5f;
		if ( !( LiquidContentsAtPoint( endProbe, passEntity ) & liquidContents ) ) {
			trailEnd = LiquidBoundaryBetween( start, endProbe, liquidContents, passEntity );
		}
	}

	return PlayLiquidTrail( liquidContents, start, trailEnd );
}

/*
================
idGameLocal::PlayEffect

Plays an effect at the given origin using the given direction
================
*/
rvClientEffect* idGameLocal::PlayEffect( 
	const idDecl			*effect, 
	const idVec3&			origin, 
	const idMat3&			axis, 
	bool					loop, 
	const idVec3&			endOrigin, 
	bool					broadcast,
	bool					predictedBit,
	effectCategory_t		category,
	const idVec4&			effectTint ) {

	gameLocal.NotePresentationEffect();
	if ( !effect ) {
		return NULL;
	}

	if ( !gameLocal.isNewFrame ) {
		return NULL;
	}

	if ( isServer && broadcast ) {
		idBitMsg	msg;
		byte		msgBuf[MAX_GAME_MESSAGE_SIZE];
		idCQuat		quat;

		quat = axis.ToCQuat();
		msg.Init( msgBuf, sizeof( msgBuf ) );
		msg.BeginWriting();
		msg.WriteByte( GAME_UNRELIABLE_MESSAGE_EFFECT );
		idGameLocal::WriteDecl( msg, effect );
		msg.WriteFloat( origin.x );
		msg.WriteFloat( origin.y );
		msg.WriteFloat( origin.z );
		msg.WriteFloat( quat.x );
		msg.WriteFloat( quat.y );
		msg.WriteFloat( quat.z );
		msg.WriteFloat( endOrigin.x );
		msg.WriteFloat( endOrigin.y );
		msg.WriteFloat( endOrigin.z );
		msg.WriteByte( category );
		msg.WriteBits( loop, 1 );
		msg.WriteBits( predictedBit, 1 );

		// send to everyone who has start or end in it's PVS
		SendUnreliableMessagePVS( msg, currentThinkingEntity, pvs.GetPVSArea( origin ), pvs.GetPVSArea( endOrigin ) );
	}

	if ( isServer && localClientNum < 0 ) {
		// no effects on dedicated server
		return NULL;
	}

	if ( bse->Filtered( effect->GetName(), category ) ) {
		// Effect filtered out
		return NULL;
	}
// RAVEN END

	if ( gameLocal.isListenServer && currentThinkingEntity && gameLocal.GetLocalPlayer() ) {
		if ( currentThinkingEntity->GetInstance() != gameLocal.GetLocalPlayer()->GetInstance() ) {
			return NULL;
		}
	}
	
	// mwhitlock: Dynamic memory consolidation
	RV_PUSH_SYS_HEAP_ID(RV_HEAP_ID_MULTIPLE_FRAME);
	rvClientEffect* clientEffect = new rvClientEffect( effect );
	RV_POP_HEAP();

	if( !clientEffect ) {
		common->Warning( "Failed to create effect \'%s\'\n", effect->GetName() );
		return NULL;
	}

	if( clientEffect->entityNumber == -1 ) {
		common->Warning( "Failed to spawn effect \'%s\'\n", effect->GetName() );
		delete clientEffect;
		return NULL;
	}

	clientEffect->SetOrigin( origin );
	clientEffect->SetAxis( axis );
	clientEffect->SetGravity( GetCurrentGravity( origin, axis ) );
	if ( !clientEffect->Play( gameLocal.time, loop, endOrigin ) ) {
		delete clientEffect;
		return NULL;
	}

	clientEffect->GetRenderEffect()->shaderParms[ SHADERPARM_RED ]		= effectTint[ 0 ];
	clientEffect->GetRenderEffect()->shaderParms[ SHADERPARM_GREEN ]	= effectTint[ 1 ];
	clientEffect->GetRenderEffect()->shaderParms[ SHADERPARM_BLUE ]		= effectTint[ 2 ];
	clientEffect->GetRenderEffect()->shaderParms[ SHADERPARM_ALPHA ]	= effectTint[ 3 ];

	return clientEffect;
}

void idGameLocal::CheckPlayerWhizzBy( idVec3 start, idVec3 end, idEntity* hitEnt, idEntity *attacker )
{
	//FIXME: make this client-side?  Work in MP?
	if ( gameLocal.isMultiplayer ) {
		return;
	}
	idPlayer* player = gameLocal.GetLocalPlayer();
	if ( !player ) {
		return;
	}
	if ( player->IsHidden() ) {
		return;
	}
	if ( player == attacker ) {
		return;
	}
	if ( player == hitEnt ) {
		return;
	}
	idVec3 center = player->firstPersonViewOrigin;
	idVec3 diff = end-center;
	if ( diff.Length() < 64.0f ) {
		//hit too close - didn't actually pass by, will hear impact sound instead
		return;
	}
	idVec3 closestPoint = player->firstPersonViewOrigin;
	if ( closestPoint.ProjectToLineSeg( start, end ) ) {
		//on line seg
		diff = closestPoint-center;
		if ( diff.Length() < 48.0f ) {
			//close enough to hear whizz-by
			idVec3 dir = end-start;
			dir.Normalize();
			idVec3 fxStart = closestPoint+dir*-32.0f;
			idVec3 fxEnd = closestPoint+dir*32.0f;
			player->PlayEffect( "fx_whizby", fxStart, player->firstPersonViewAxis, false, fxEnd );
		}
	}
}

/*
================
idGameLocal::ResetMPLagCompensationHistory

openQ4: server side hitscan lag compensation, ported from the single player tree so the
multiplayer game library gets the same rewind behaviour.
================
*/
void idGameLocal::ResetMPLagCompensationHistory( void ) {
	mpLagCompHistoryHead = -1;
	for ( int clientNum = 0; clientNum < MAX_CLIENTS; clientNum++ ) {
		for ( int i = 0; i < MP_LAGCOMP_HISTORY; i++ ) {
			mpLagCompHistory[ clientNum ][ i ].time = 0;
			mpLagCompHistory[ clientNum ][ i ].origin = vec3_origin;
			mpLagCompHistory[ clientNum ][ i ].axis = mat3_identity;
			mpLagCompHistory[ clientNum ][ i ].valid = false;
		}
	}
}

/*
================
idGameLocal::CaptureMPLagCompensationFrame
================
*/
void idGameLocal::CaptureMPLagCompensationFrame( void ) {
	if ( !isServer || !isMultiplayer ) {
		return;
	}

	mpLagCompHistoryHead = ( mpLagCompHistoryHead + 1 ) % MP_LAGCOMP_HISTORY;

	for ( int clientNum = 0; clientNum < MAX_CLIENTS; clientNum++ ) {
		mpLagCompFrame_t &frame = mpLagCompHistory[ clientNum ][ mpLagCompHistoryHead ];
		frame.time = time;
		frame.valid = false;

		idEntity *ent = entities[ clientNum ];
		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}

		idPlayer *player = static_cast< idPlayer * >( ent );
		// A corpse is not a hitscan target and its physics may already belong to
		// a ragdoll, so do not record it - and do not let a shot a moment after
		// the respawn rewind onto where the body fell.
		if ( player->spectating || player->health <= 0 ) {
			continue;
		}

		frame.origin = player->GetPhysics()->GetOrigin();
		frame.axis = player->GetPhysics()->GetAxis();
		frame.valid = true;
	}
}

/*
================
idGameLocal::InvalidateMPLagCompensationHistory

openQ4: forget one client's recorded positions.  Anything that places a player
somewhere they did not walk to - a respawn or a teleport - makes every earlier
sample a lie about where they could be shot, and the rewind window is long
enough to reach across one.
================
*/
void idGameLocal::InvalidateMPLagCompensationHistory( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}

	for ( int i = 0; i < MP_LAGCOMP_HISTORY; i++ ) {
		mpLagCompHistory[ clientNum ][ i ].valid = false;
	}
}

/*
================
idGameLocal::SelectMPLagCompensationFrame
================
*/
bool idGameLocal::SelectMPLagCompensationFrame( int clientNum, int targetTime, mpLagCompFrame_t &outFrame ) const {
	if ( mpLagCompHistoryHead < 0 || clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return false;
	}

	// openQ4: never rewind further than the configured window.  Without this the
	// fallback below happily returns the oldest entry in the ring however old it
	// is, which after a map load - where gameLocal.time restarts at 0 and every
	// stamp is in the "future" - means the oldest surviving sample rather than
	// anything near the shot.
	const int maxRewindMS = idMath::ClampInt( 0, 1000, net_mpLagCompMaxMS.GetInteger() );
	const int oldestAcceptableTime = time - maxRewindMS;

	const mpLagCompFrame_t *oldestFrame = NULL;

	for ( int i = 0; i < MP_LAGCOMP_HISTORY; i++ ) {
		const int index = ( mpLagCompHistoryHead - i + MP_LAGCOMP_HISTORY ) % MP_LAGCOMP_HISTORY;
		const mpLagCompFrame_t &frame = mpLagCompHistory[ clientNum ][ index ];
		if ( !frame.valid ) {
			continue;
		}
		// a stamp ahead of us is stale state from before a map load reset the clock
		if ( frame.time > time || frame.time < oldestAcceptableTime ) {
			continue;
		}

		oldestFrame = &frame;
		if ( frame.time <= targetTime ) {
			outFrame = frame;
			return true;
		}
	}

	if ( oldestFrame ) {
		outFrame = *oldestFrame;
		return true;
	}

	return false;
}

/*
================
idGameLocal::EstimateMPLagCompensation

How old the remote players on one client's screen are, from numbers the server bounds itself.
Valid for any slot whether or not lag compensation is on, so lagCompStatus can show what a
rewind would be before anybody turns it on.
================
*/
void idGameLocal::EstimateMPLagCompensation( int clientNum, mpLagCompEstimate_t &estimate ) const {
	memset( &estimate, 0, sizeof( estimate ) );
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}

	// This used to start from time - shooter->usercmd.gameTime, which cannot measure anything:
	// the server files a client's command at THAT client's own frame index and executes the
	// slot for the frame it is running, and idGameLocal::RunFrame has already advanced time past
	// it, so the difference is a structural GetMSec() at any ping - and a duplicated command is
	// re-stamped with the server's own clock to the same effect.  The ping/2 fallback underneath
	// was therefore unreachable.
	//
	// What the rewind actually owes the shooter is the age of the world it was aiming at.  Take
	// the client's own reported prediction lead - which is what it is drawing ahead by - and cap
	// it with the round trip the server measured itself, so a client cannot choose its own
	// rewind by misreporting or by sitting on its ping replies.
	int reported = networkSystem->ServerGetClientPrediction( clientNum );
	int measured = networkSystem->ServerGetClientPing( clientNum );

	// Both report 99999 for a slot that is not fully connected, and reported arrives as a
	// client-authored short, so it can be anywhere in -32768..32767.
	if ( reported < 0 || reported >= 99999 ) {
		reported = 0;
	}
	reported = idMath::ClampInt( 0, 1000, reported );
	if ( measured < 0 || measured >= 99999 ) {
		measured = 0;
	}

	const int claim = ( reported > 0 ) ? reported + GetMSec() : 0;
	const int ceiling = ( measured > 0 ) ? measured + net_mpLagCompSlackMS.GetInteger() : 0;

	estimate.leadMS = reported;
	estimate.measuredMS = measured;
	estimate.advertisedTics = AdvertisedRemoteExtrapolationTics( clientNum );
	estimate.extrapolationMS = RemoteExtrapolationMS( clientNum, reported );

	// A listen server's own player and a bot both leave these at zero and rewind nothing, which
	// is right: neither has a wire between it and the world it aimed at.
	estimate.poseAgeMS = Max( 0, Min( claim, ceiling ) - estimate.extrapolationMS );
}

/*
================
idGameLocal::ComputeMPLagCompensationRewind
================
*/
bool idGameLocal::ComputeMPLagCompensationRewind( const idPlayer *shooter, int &rewindMS, mpLagCompEstimate_t *estimate ) const {
	rewindMS = 0;

	if ( !isServer || !isMultiplayer || !shooter || !net_mpLagCompensation.GetBool() ) {
		return false;
	}

	const int maxRewindMS = idMath::ClampInt( 0, 1000, net_mpLagCompMaxMS.GetInteger() );
	if ( maxRewindMS <= 0 ) {
		return false;
	}

	const int clientNum = shooter->entityNumber;
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return false;
	}

	mpLagCompEstimate_t local;
	mpLagCompEstimate_t &used = ( estimate != NULL ) ? *estimate : local;
	EstimateMPLagCompensation( clientNum, used );

	// The bias tunes the rewind of a measured remote connection.  A listen server's own player, a
	// bot or a slot without a measured round trip has no wire to compensate, so it must not
	// manufacture latency for them.
	if ( used.leadMS <= 0 || used.measuredMS <= 0 ) {
		return false;
	}

	// The rewind is the displayed pose age: the lead the shooter draws the world behind by, less
	// what it extrapolates remote players forward for itself.
	const int rewindEstimateMS = used.poseAgeMS + net_mpLagCompBiasMS.GetInteger();
	rewindMS = idMath::ClampInt( 0, maxRewindMS, rewindEstimateMS );
	return rewindMS > 0;
}

/*
================
idGameLocal::AdvertisedRemoteExtrapolationTics

The net_mpRemoteExtrapolation a client put in its user info.  The text is client-authored, so
only a plain decimal inside net_mpPredictMaxFrames' own range counts; anything else - an older
client without the key included - is read as legacy prediction, which extrapolates nothing.
================
*/
int idGameLocal::AdvertisedRemoteExtrapolationTics( int clientNum ) const {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return 0;
	}
	int tics = 0;
	if ( !idNumericString::ParseUnsignedBounded( userInfo[ clientNum ].GetString( "net_mpRemoteExtrapolation", "0" ), 64, tics ) ) {
		return 0;
	}
	return tics;
}

/*
================
idGameLocal::RemoteExtrapolationMS

How far ahead of its last snapshot a client already draws remote players for itself.

Lag compensation and client-side remote extrapolation are two corrections to the same error,
and applying both at full strength does not halve it - it inverts it, and the shooter has to
lead backwards.  So whatever a client extrapolates forward has to come off the rewind.

In mode 1 a client re-simulates each remote player across the replay passes from the snapshot
up to its own predicted frame, at most net_mpPredictMaxFrames tics a chain
(idPlayer::AllowsFullRemotePrediction), so it draws remotes min( lead, those tics ) ahead of
the snapshot.  In legacy mode a remote moves once per client frame and sits a whole lead
behind.  The client advertises the tic budget (net_mpRemoteExtrapolation); the server bounds
it by the lead it already trusts, so the advertisement can only ever shorten the rewind.
================
*/
int idGameLocal::RemoteExtrapolationMS( int clientNum, int leadMS ) const {
	if ( leadMS <= 0 ) {
		return 0;
	}
	return Min( leadMS, AdvertisedRemoteExtrapolationTics( clientNum ) * GetMSec() );
}

/*
================
idGameLocal::UpdateAdvertisedRemoteExtrapolation

Keep net_mpRemoteExtrapolation equal to what this client actually simulates.  Called every
client frame: a change to either prediction cvar, or a server echoing a different value back in
the user info, is corrected on the next frame, and the engine sends the user info again.
================
*/
void idGameLocal::UpdateAdvertisedRemoteExtrapolation( void ) {
	const int advertised = net_mpPredictMode.GetBool() ? idMath::ClampInt( 1, 64, net_mpPredictMaxFrames.GetInteger() ) : 0;
	if ( net_mpRemoteExtrapolation.GetInteger() != advertised ) {
		net_mpRemoteExtrapolation.SetInteger( advertised );
	}
}


/*
================
idGameLocal::BeginMPLagCompensation
================
*/
bool idGameLocal::BeginMPLagCompensation( const idPlayer *shooter, mpLagCompRestore_t restoreState[MAX_CLIENTS], int &restoreCount ) {
	restoreCount = 0;

	int rewindMS = 0;
	mpLagCompEstimate_t estimate;
	if ( !ComputeMPLagCompensationRewind( shooter, rewindMS, &estimate ) ) {
		return false;
	}

	const int targetTime = time - rewindMS;

	for ( int clientNum = 0; clientNum < MAX_CLIENTS; clientNum++ ) {
		if ( clientNum == shooter->entityNumber ) {
			continue;
		}

		idEntity *ent = entities[ clientNum ];
		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}

		idPlayer *player = static_cast< idPlayer * >( ent );
		// openQ4: dead players are not valid hitscan targets, and their physics may already
		// have been handed over to a ragdoll, so leave them alone entirely.
		if ( player->spectating || player->health <= 0 || player->GetInstance() != shooter->GetInstance() ) {
			continue;
		}

		mpLagCompFrame_t rewoundFrame;
		if ( !SelectMPLagCompensationFrame( clientNum, targetTime, rewoundFrame ) ) {
			continue;
		}

		mpLagCompRestore_t &restore = restoreState[ restoreCount ];
		restore.player = player;
		restore.physics = player->GetPhysics();
		restore.originalOrigin = restore.physics->GetOrigin();
		restore.originalAxis = restore.physics->GetAxis();
		restore.rewoundOrigin = rewoundFrame.origin;
		restoreCount++;

		restore.physics->SetOrigin( rewoundFrame.origin );
		restore.physics->SetAxis( rewoundFrame.axis );
	}

	if ( net_mpLagCompDebug.GetInteger() > 0 ) {
		common->DPrintf( "MPLagComp: shooter=%d lead=%dms rtt=%dms extrap=%dms (tics %d) age=%dms rewind=%dms targetTime=%d rewound=%d\n",
			shooter->entityNumber, estimate.leadMS, estimate.measuredMS, estimate.extrapolationMS, estimate.advertisedTics,
			estimate.poseAgeMS, rewindMS, targetTime, restoreCount );
	}

	return restoreCount > 0;
}

/*
================
idGameLocal::EndMPLagCompensation
================
*/
void idGameLocal::EndMPLagCompensation( mpLagCompRestore_t restoreState[MAX_CLIENTS], int restoreCount ) {
	for ( int i = restoreCount - 1; i >= 0; i-- ) {
		mpLagCompRestore_t &restore = restoreState[ i ];
		if ( !restore.player ) {
			continue;
		}

		// openQ4: a shot that kills inside the bracket runs idPlayer::Killed ->
		// StartRagdoll, which calls SetPhysics( &af.physicsObj ) - so the player
		// is no longer on the physics object we rewound.  Writing the stored
		// player-physics origin/axis into idPhysics_AF would rotate the whole
		// fresh corpse about body 0.  The corpse has already been placed
		// authoritatively; there is nothing of ours left to undo.
		// (This covers a physics-object swap only.  A teleport keeps the same
		// idPhysics_Player, and is instead handled by the translation term below
		// plus InvalidateMPLagCompensationHistory on the spawn path.)
		if ( restore.player->GetPhysics() != restore.physics ) {
			continue;
		}

		// Preserve any intentional translation that happened while rewound (rare but possible).
		idVec3 translated = restore.physics->GetOrigin() - restore.rewoundOrigin;
		restore.physics->SetOrigin( restore.originalOrigin + translated );
		restore.physics->SetAxis( restore.originalAxis );
	}
}

// Read the translation without consuming the entry: lightning must translate
// all of its selected endpoints before any target is restored.
idVec3 idGameLocal::MPLagCompensationOffset( const idEntity *hit, const mpLagCompRestore_t restoreState[MAX_CLIENTS], int restoreCount ) const {
	for ( int i = 0; hit && i < restoreCount; i++ ) {
		if ( restoreState[ i ].player == hit ) {
			return restoreState[ i ].originalOrigin - restoreState[ i ].rewoundOrigin;
		}
	}
	return vec3_origin;
}

/*
================
idGameLocal::ResolveMPLagCompensationHit

Put a hit target back in the present the moment the trace has resolved against it, and carry
the trace's geometry forward with it.

Everything a hit does afterwards - the damage, the blood and the wound decal, the impact
effect, the PVS areas that effect is broadcast to, the impulse, and the ragdoll the corpse
starts from - was previously computed at the target's PAST position, so a body killed at range
fell where it used to be and the effect was routed to whoever could see there.  The gauntlet
and the lightning gun already restored before they damaged; this makes the hit-scan weapons
agree with them.

Translation only.  A player's physics axis is set once per life from the spawn yaw and
idPhysics_Player never rotates it, so the rewound and original axes are the same and there is
no rotation to carry.
================
*/
bool idGameLocal::ResolveMPLagCompensationHit( const idEntity *hit, mpLagCompRestore_t restoreState[MAX_CLIENTS], int &restoreCount, trace_t &tr, idVec3 *extraPoint ) {
	if ( !hit ) {
		return false;
	}

	for ( int i = 0; i < restoreCount; i++ ) {
		mpLagCompRestore_t &restore = restoreState[ i ];
		if ( restore.player != hit ) {
			continue;
		}

		const idVec3 shift = restore.originalOrigin - restore.rewoundOrigin;

		// Restore this one entry now; EndMPLagCompensation still restores the rest.
		mpLagCompRestore_t single = restore;
		EndMPLagCompensation( &single, 1 );

		tr.c.point += shift;
		tr.endpos += shift;
		tr.c.dist = tr.c.normal * tr.c.point;
		if ( extraPoint ) {
			*extraPoint += shift;
		}

		restoreState[ i ] = restoreState[ --restoreCount ];
		return true;
	}

	return false;
}

/*
================
idGameLocal::HitScan

Run a hitscan trace from the given origin and direction
================
*/
idEntity* idGameLocal::HitScan( 
	const idDict&	hitscanDict, 
	const idVec3&	origOrigin, 
	const idVec3&	origDir, 
	const idVec3&	origFxOrigin, 
	idEntity*		owner, 
	bool			noFX,
	float			damageScale,
// twhitaker: added additionalIgnore parameter
	idEntity*		additionalIgnore,
	int				areas[ 2 ]
	) {

	idVec3		dir;
	idVec3		origin;
	idVec3		fxOrigin;
	idVec3		fxDir;
	idVec3		impulse;
	idVec4		hitscanTint( 1.0f, 1.0f, 1.0f, 1.0f );
	int			reflect;
	float		tracerChance;
	idEntity*	ignore;
	float		penetrate;
	idEntity *	hitResult = NULL;
	mpLagCompRestore_t lagCompRestore[ MAX_CLIENTS ];
	int			lagCompRestoreCount = 0;
	bool		lagCompApplied = false;

	if ( areas ) {
		areas[ 0 ] = pvs.GetPVSArea( origFxOrigin );
		areas[ 1 ] = -1;
	}

	ignore    = owner;
	penetrate = hitscanDict.GetFloat( "penetrate" );

	// openQ4: rewind the other players to where the shooter saw them before tracing.  This
	// is a no-op on a client, in single player and when net_mpLagCompensation is off.
	if ( isServer && isMultiplayer && owner && owner->IsType( idPlayer::GetClassType() ) ) {
		lagCompApplied = BeginMPLagCompensation( static_cast<idPlayer *>( owner ), lagCompRestore, lagCompRestoreCount );
	}

	// openQ4: owner comes straight off the wire in ClientHitScan and is NULL whenever
	// the shooter has not been spawned locally yet, so guard it the same way the
	// powerup lookup below already does.
	if( owner && hitscanDict.GetBool( "hitscanTint" ) && owner->IsType( idPlayer::GetClassType() ) ) {
		hitscanTint = ((idPlayer*)owner)->GetHitscanTint();
	}

	// twhitaker: additionalIgnore parameter
	if ( !additionalIgnore ) {
		additionalIgnore = ignore;
	}	

	origin		 = origOrigin;
	fxOrigin	 = origFxOrigin;
	dir			 = origDir;
	tracerChance = ((g_perfTest_weaponNoFX.GetBool())?0:hitscanDict.GetFloat( "tracerchance", "0" ));

	// Apply player powerups
	if ( owner && owner->IsType( idPlayer::GetClassType() ) ) {
		damageScale *= static_cast<idPlayer*>(owner)->PowerUpModifier(PMOD_PROJECTILE_DAMAGE);
	}
	
	// Run reflections
	for ( reflect = hitscanDict.GetFloat( "reflect", "0" ); reflect >= 0; reflect-- ) {	
		idVec3		start;
		idVec3		end;
		idEntity*	ent;
		idEntity*	actualHitEnt;
		trace_t		tr;
		int			contents;
		int			collisionArea;
		idVec3		collisionPoint;
		bool		tracer;
		int			trailLiquidContents;
		idVec3		trailLiquidStart;
		bool		crossedLiquid;
		
		// Calculate the end point of the trace
		start    = origin;
		if ( g_perfTest_hitscanShort.GetBool() ) {
			end		 = start + (dir.ToMat3() * idVec3(idMath::ClampFloat(0,2048,hitscanDict.GetFloat ( "range", "2048" )),0,0));
		} else {
			end		 = start + (dir.ToMat3() * idVec3(hitscanDict.GetFloat ( "range", "40000" ),0,0));
		}
		if ( g_perfTest_hitscanBBox.GetBool() ) {
			contents = MASK_SHOT_BOUNDINGBOX|CONTENTS_PROJECTILE;
		} else {
			contents = MASK_SHOT_RENDERMODEL|MASK_WATER|CONTENTS_PROJECTILE;
		}

		trailLiquidContents = LiquidContentsAtPoint( start, owner );
		trailLiquidStart = start;
		crossedLiquid = ( trailLiquidContents != 0 );
		if ( crossedLiquid ) {
			// A trace beginning underwater is already through the surface; do not let the contents
			// brush report start-solid or play a false entry splash at the muzzle.
			contents &= ~MASK_WATER;
		}
		
		// Loop the traces to handle cases where something can be shot through
		while ( 1 ) {				
			// Trace to see if we hit any entities
// RAVEN BEGIN
// ddynerman: multiple clip worlds
			if ( hitscanDict.GetFloat( "trace_size", "0" ) > 0.0f )
			{
				float range = hitscanDict.GetFloat ( "range", "1024" );
				if ( range > 4096.0f )
				{
					assert( !(range > 4096.0f) );
					Warning( "idGameLocal::HitScan: hitscan def (%s) with trace_size must have max range of 4096!", hitscanDict.GetString( "classname" ) );
					range = idMath::ClampFloat( 0.0f, 4096.0f, range );
				}
				end	= start + (dir * range);

				idBounds traceBounds;
				traceBounds.Zero();
				traceBounds.ExpandSelf( hitscanDict.GetFloat( "trace_size", "0" ) );
				// twhitaker: additionalIgnore parameter
				TraceBounds( owner, tr, start, end, traceBounds, contents, additionalIgnore );
			}
			else
			{
				// twhitaker: additionalIgnore parameter
				TracePoint( owner, tr, start, end, contents, additionalIgnore );
			}
			//gameRenderWorld->DebugArrow( colorRed, start, end, 10, 5000 );
// RAVEN END
			
			// If the hitscan hit a no impact surface we can just return out
			//assert( tr.c.material );
			if ( tr.fraction >= 1.0f || (tr.c.material && tr.c.material->GetSurfaceFlags() & SURF_NOIMPACT) ) {					
				PlayEffect( hitscanDict, "fx_path", fxOrigin, dir.ToMat3(), false, tr.endpos, false, false, EC_IGNORE, hitscanTint );	
				const bool liquidTracer = crossedLiquid && PlayLiquidTrailToExit( trailLiquidContents, trailLiquidStart, tr.endpos, owner );
				if ( random.RandomFloat( ) < tracerChance ) {
// openQ4 BEGIN
					// Never lay an air tracer over the underwater bubbles drawn above.
					if ( !liquidTracer ) {
						PlayEffect( hitscanDict, "fx_tracer", fxOrigin, dir.ToMat3(), false, tr.endpos );
					}
// openQ4 END
					tracer = true;
				} else {
					tracer = false;
				}

				if ( areas ) {
					collisionArea = pvs.GetPVSArea( tr.endpos );
					if ( collisionArea != areas[0] ) {
						areas[1] = collisionArea;
					}
				}

				hitResult = NULL;
				goto hitScanDone;
			}

			collisionPoint = tr.c.point - ( tr.c.normal * tr.c.point - tr.c.dist ) * tr.c.normal;
			ent			   = entities[ tr.c.entityNum ];
			// Penetration continues along the historical ray. Moving the impact for
			// damage/effects must not bend a rail shot toward the target's current pose.
			const idVec3 traceCollisionPoint = collisionPoint;

			// The trace resolved against the rewound world; everything below acts in the
			// present.  Put this target back before any of it runs, and carry the hit point
			// with it - including the PVS areas the effect broadcast is routed to, which is why
			// that block now sits after this rather than before it.
			if ( lagCompApplied ) {
				ResolveMPLagCompensationHit( ent, lagCompRestore, lagCompRestoreCount, tr, &collisionPoint );
			}

			// computing the collisionArea from the collisionPoint fails sometimes
			if ( areas ) {
				collisionArea  = pvs.GetPVSArea( tr.c.point );
				if ( collisionArea != areas[0] ) {
					areas[1] = collisionArea;
				}
			}
			actualHitEnt   = NULL;
			start		   = traceCollisionPoint;

			// Keep tracing if we hit a liquid
// openQ4 BEGIN
			const int hitLiquidContents = LiquidContentsAtCollision( ent, tr );
			if ( hitLiquidContents ) {
				// Apply force to the liquid entity that was hit
				ent->ApplyImpulse( owner, tr.c.id, tr.c.point, -(hitscanDict.GetFloat( "push", "5000" )) * tr.c.normal );
				if ( !crossedLiquid ) {
					trailLiquidContents = hitLiquidContents;
					trailLiquidStart = collisionPoint + dir * 0.5f;
					crossedLiquid = true;
				}

				// Continue just beyond the contents plane with liquids excluded. Starting at the exact
				// collision point can make the next trace rediscover the same numerical boundary.
				start = collisionPoint + dir * 0.5f;
				contents &= (~MASK_WATER);

				if ( !g_perfTest_weaponNoFX.GetBool() && !GetEffect( hitscanDict, "fx_impact", tr.c.materialType ) ) {
					// the shooter's def says nothing about hitting this liquid, so fall back to the
					// shared liquid presentation rather than splashing nothing at all
					// ClientHitScan replays this whole function on every client, so the splash
					// is already produced locally there.  Broadcasting it as well plays it twice.
					PlayLiquidImpact( hitLiquidContents, collisionPoint, tr.c.normal, ent, &hitscanDict, false );
				}
// openQ4 END

				if ( !g_perfTest_weaponNoFX.GetBool() ) {
					if ( ent->CanPlayImpactEffect( owner, ent ) ) {
						if ( ent->IsType( idMover::GetClassType( ) ) ) {
							ent->PlayEffect( GetEffect( hitscanDict, "fx_impact", tr.c.materialType ), collisionPoint, tr.c.normal.ToMat3(), false, vec3_origin, false, false, EC_IMPACT, hitscanTint );
						} else {
							gameLocal.PlayEffect( GetEffect( hitscanDict, "fx_impact", tr.c.materialType ), collisionPoint, tr.c.normal.ToMat3(), false, vec3_origin, false, false, EC_IMPACT, hitscanTint );
						}
					}
				}

				continue;
			// Reflect off a bounce target?
			} else if ( (tr.c.material->GetSurfaceFlags ( ) & SURF_BOUNCE) && !hitscanDict.GetBool ( "noBounce" ) ) {
				reflect++;
			}

			// If the hit entity is bound to an actor use the actor instead
			if ( ent->fl.takedamage && ent->GetTeamMaster( ) && ent->GetTeamMaster( )->IsType ( idActor::GetClassType() ) ) {
				actualHitEnt = ent;
				ent = ent->GetTeamMaster( );
			}

			if ( !gameLocal.isClient ) {

				// Apply force to the entity that was hit
				ent->ApplyImpulse( owner, tr.c.id, tr.c.point, -tr.c.normal, &hitscanDict );

				// Handle damage to the entity
				if ( ent->fl.takedamage && !(( tr.c.material != NULL ) && ( tr.c.material->GetSurfaceFlags() & SURF_NODAMAGE )) ) {		
					const char*	damage;
				
					damage    = NULL;

					// RAVEN BEGIN
					// jdischler: code from the other project..to ensure that if an attached head is hit, the body will use the head joint
					//	otherwise damage zones for head attachments no-worky
					int hitJoint = CLIPMODEL_ID_TO_JOINT_HANDLE(tr.c.id);
					if ( ent->IsType(idActor::GetClassType()) )
						{
							idActor* entActor = static_cast<idActor*>(ent);
							if ( entActor && entActor->GetHead() && entActor->GetHead()->IsType(idAFAttachment::GetClassType()) )
								{
									idAFAttachment* headEnt = static_cast<idAFAttachment*>(entActor->GetHead());
									if ( headEnt && headEnt->entityNumber == tr.c.entityNum )
										{//hit ent's head, get the proper joint for the head
											hitJoint = entActor->GetAnimator()->GetJointHandle("head");
										}
								}
						}	
					// RAVEN END
					// Inflict damage
					if ( tr.c.materialType ) {
						damage = hitscanDict.GetString( va("def_damage_%s", tr.c.materialType->GetName()) );
					}
					if ( !damage || !*damage ) {
						damage = hitscanDict.GetString ( "def_damage" );
					}

					if ( damage && damage[0] ) {
						// RAVEN BEGIN
						// ddynerman: stats
						if( owner->IsType( idPlayer::GetClassType() ) && ent->IsType( idActor::GetClassType() ) && ent != owner && !((idPlayer*)owner)->pfl.dead ) {
							statManager->WeaponHit( (idActor*)owner, ent, ((idPlayer*)owner)->GetCurrentWeapon() );
						}
						// RAVEN END
						ent->Damage( owner, owner, dir, damage, damageScale, hitJoint );
					}

					// Let the entity add its own damage effect
					if ( !g_perfTest_weaponNoFX.GetBool() ) {
						ent->AddDamageEffect ( tr, dir, damage, owner );
					}
				} else { 
					if ( actualHitEnt
						 && actualHitEnt != ent
						 && (tr.c.material->GetSurfaceFlags ( ) & SURF_BOUNCE)
						 && actualHitEnt->spawnArgs.GetBool( "takeBounceDamage" ) )
						{//bleh...
							const char*	damage = NULL;
							// Inflict damage
							if ( tr.c.materialType ) {
								damage = hitscanDict.GetString( va("def_damage_%s", tr.c.materialType->GetName()) );
							}
							if ( !damage || !*damage ) {
								damage = hitscanDict.GetString ( "def_damage" );
							}
							if ( damage && damage[0] ) {
								actualHitEnt->Damage( owner, owner, dir, damage, damageScale, CLIPMODEL_ID_TO_JOINT_HANDLE( tr.c.id ) );
							}
						}
					if ( !g_perfTest_weaponNoFX.GetBool() ) {
						ent->AddDamageEffect( tr, dir, hitscanDict.GetString ( "def_damage" ), owner );
					}
				}
			}


			// Pass through actors
			if ( ent->IsType ( idActor::GetClassType() ) && penetrate > 0.0f ) {			
				start = traceCollisionPoint;
				additionalIgnore = ent;
				damageScale *= penetrate;
				continue;
			}
			break;
		}
			
		// Path effect 
		fxDir = collisionPoint - fxOrigin;
		fxDir.Normalize( );
		PlayEffect( hitscanDict, "fx_path", fxOrigin, fxDir.ToMat3(), false, collisionPoint, false, false, EC_IGNORE, hitscanTint );	
		const bool liquidTracer = crossedLiquid && PlayLiquidTrailToExit( trailLiquidContents, trailLiquidStart, collisionPoint, owner );
		if ( !ent->fl.takedamage && random.RandomFloat ( ) < tracerChance ) {
// openQ4 BEGIN
			if ( !liquidTracer ) {
				PlayEffect( hitscanDict, "fx_tracer", fxOrigin, fxDir.ToMat3(), false, collisionPoint );
			}
// openQ4 END
			tracer = true;
		} else {
			tracer = false;
		}

		if ( !reflect ) {
			//on initial trace only
			if ( hitscanDict.GetBool( "doWhizz" ) ) {
				//play whizz-by sound if trace is close to player's head
				CheckPlayerWhizzBy( origin, collisionPoint, ent, owner );
			}
		}

		// Play a different effect when reflecting
		if ( !reflect || ent->fl.takedamage ) {
			idMat3 axis;
			
			// Effect axis when hitting actors is along the direction of impact because actor models are 
			// very detailed.
			if ( ent->IsType ( idActor::GetClassType() ) ) {
				axis = ((-dir + tr.c.normal) * 0.5f).ToMat3();
			} else {
				axis = tr.c.normal.ToMat3();
			}
			
			if ( !g_perfTest_weaponNoFX.GetBool() ) {
				if ( ent->CanPlayImpactEffect( owner, ent ) ) {
					if ( ent->IsType( idMover::GetClassType( ) ) ) {
						ent->PlayEffect( GetEffect( hitscanDict, "fx_impact", tr.c.materialType ), collisionPoint, axis, false, vec3_origin, false, false, EC_IMPACT, hitscanTint );
					} else {
						gameLocal.PlayEffect( GetEffect( hitscanDict, "fx_impact", tr.c.materialType ), collisionPoint, axis, false, vec3_origin, false, false, EC_IMPACT, hitscanTint );
					}
				}
			}
			
			// End of reflection
			hitResult = ent;
			goto hitScanDone;
		} else {
			PlayEffect( GetEffect( hitscanDict, "fx_reflect", tr.c.materialType ), collisionPoint, tr.c.normal.ToMat3() );
		}
		
		// Calc new diretion based on bounce
		origin = start;
		fxOrigin = start;
		dir = ( dir - ( 2.0f * DotProduct( dir, tr.c.normal ) * tr.c.normal ) );
		dir.Normalize( );

		// Increase damage scale on reflect		
		damageScale += hitscanDict.GetFloat( "reflect_powerup", "0" );
	}	
	
	assert( false );

hitScanDone:
	// openQ4: every exit from the trace loop funnels through here so the rewound players are
	// always put back, whichever way the hitscan ended.
	if ( lagCompApplied ) {
		EndMPLagCompensation( lagCompRestore, lagCompRestoreCount );
	}
	return hitResult;
}

/*
===================
idGameLocal::RegisterClientEntity
===================
*/
void idGameLocal::RegisterClientEntity( rvClientEntity *cent ) {
	int entityNumber;

	assert ( cent );

	if ( clientSpawnCount >= ( 1 << ( 32 - CENTITYNUM_BITS ) ) ) {
//		Error( "idGameLocal::RegisterClientEntity: spawn count overflow" );
		clientSpawnCount = INITIAL_SPAWN_COUNT;
	}

	// Find a free entity index to use
	while( firstFreeClientIndex < MAX_CENTITIES && clientEntities[firstFreeClientIndex] ) {
		firstFreeClientIndex++;
	}

	if ( firstFreeClientIndex >= MAX_CENTITIES ) {
		cent->PostEventMS ( &EV_Remove, 0 );
		Warning( "idGameLocal::RegisterClientEntity: no free client entities" );
		return;
	}

	entityNumber = firstFreeClientIndex++;

	// Add the client entity to the lists
	clientEntities[ entityNumber ] = cent;
	clientSpawnIds[ entityNumber ] = clientSpawnCount++;
	cent->entityNumber = entityNumber;
	cent->spawnNode.AddToEnd( clientSpawnedEntities );
	cent->spawnArgs.TransferKeyValues( spawnArgs );

	if ( entityNumber >= num_clientEntities ) {
		num_clientEntities++;
	}
}

/*
===================
idGameLocal::UnregisterClientEntity
===================
*/
void idGameLocal::UnregisterClientEntity( rvClientEntity* cent ) {
	assert( cent );
	
	// No entity number then it failed to register
	if ( cent->entityNumber == -1 ) {
		return;
	}
	
	cent->spawnNode.Remove ( );
	cent->bindNode.Remove ( );

	if ( clientEntities [ cent->entityNumber ] == cent ) {
		clientEntities [ cent->entityNumber ] = NULL;
		clientSpawnIds[ cent->entityNumber ] = -1;
		if ( cent->entityNumber < firstFreeClientIndex ) {
			firstFreeClientIndex = cent->entityNumber;
		}
		cent->entityNumber = -1;
	}
}

/*
===================
idGameLocal::Translation
===================
*/
bool idGameLocal::Translation( const idEntity* ent, trace_t &results, const idVec3 &start, const idVec3 &end, const idClipModel *mdl, const idMat3 &trmAxis, int contentMask, const idEntity *passEntity, const idEntity *passEntity2 ) {
	idClip* clipWorld = GetEntityClipWorld( ent );
	
	if ( clipWorld ) {
		return clipWorld->Translation( results, start, end, mdl, trmAxis, contentMask, passEntity, passEntity2 );
	} 
	
	return false;
}

/*
===================
idGameLocal::Rotation
===================
*/
bool idGameLocal::Rotation( const idEntity* ent, trace_t &results, const idVec3 &start, const idRotation &rotation, const idClipModel *mdl, const idMat3 &trmAxis, int contentMask, const idEntity *passEntity ) {
	idClip* clipWorld = GetEntityClipWorld( ent );
	
	if( clipWorld ) {
		return clipWorld->Rotation( results, start, rotation, mdl, trmAxis, contentMask, passEntity );
	}

	return false;
}

/*
===================
idGameLocal::Motion
===================
*/
bool idGameLocal::Motion( const idEntity* ent, trace_t &results, const idVec3 &start, const idVec3 &end, const idRotation &rotation, const idClipModel *mdl, const idMat3 &trmAxis, int contentMask, const idEntity *passEntity ) {
	idClip* clipWorld = GetEntityClipWorld( ent );
	
	if( clipWorld ) {
		return clipWorld->Motion( results, start, end, rotation, mdl, trmAxis, contentMask, passEntity );
	}

	return false;
}


/*
===================
idGameLocal::Contacts
===================
*/
int idGameLocal::Contacts( const idEntity* ent, contactInfo_t *contacts, const int maxContacts, const idVec3 &start, const idVec6 &dir, const float depth, const idClipModel *mdl, const idMat3 &trmAxis, int contentMask, const idEntity *passEntity ) {
	idClip* clipWorld = GetEntityClipWorld( ent );
	
	if( clipWorld ) {
		return clipWorld->Contacts( contacts, maxContacts, start, dir, depth, mdl, trmAxis, contentMask, passEntity );
	}

	return 0;
}

/*
===================
idGameLocal::Contents
===================
*/
int idGameLocal::Contents( const idEntity* ent, const idVec3 &start, const idClipModel *mdl, const idMat3 &trmAxis, int contentMask, const idEntity *passEntity, idEntity **touchedEntity ) {
	idClip* clipWorld = GetEntityClipWorld( ent );
	
	if( clipWorld ) {
		return clipWorld->Contents( start, mdl, trmAxis, contentMask, passEntity, touchedEntity );
	}

	return 0;
}

/*
===================
idGameLocal::TracePoint
===================
*/
bool idGameLocal::TracePoint( const idEntity* ent, trace_t &results, const idVec3 &start, const idVec3 &end, int contentMask, const idEntity *passEntity ) {
	idClip* clipWorld = GetEntityClipWorld( ent );
	
	if( clipWorld ) {
		return clipWorld->TracePoint( results, start, end, contentMask, passEntity );
	}

	return false;
}

/*
===================
idGameLocal::TraceBounds
===================
*/
bool idGameLocal::TraceBounds( const idEntity* ent, trace_t &results, const idVec3 &start, const idVec3 &end, const idBounds &bounds, int contentMask, const idEntity *passEntity ) {
	idClip* clipWorld = GetEntityClipWorld( ent );
	
	if( clipWorld ) {
		return clipWorld->TraceBounds( results, start, end, bounds, contentMask, passEntity );
	}

	return false;
}

/*
===================
idGameLocal::TranslationModel
===================
*/
void idGameLocal::TranslationModel( const idEntity* ent, trace_t &results, const idVec3 &start, const idVec3 &end, const idClipModel *mdl, const idMat3 &trmAxis, int contentMask, idCollisionModel *model, const idVec3 &modelOrigin, const idMat3 &modelAxis ) {
	idClip* clipWorld = GetEntityClipWorld( ent );
	
	if( clipWorld ) {
		clipWorld->TranslationModel( results, start, end, mdl, trmAxis, contentMask, model, modelOrigin, modelAxis );
	}
}

/*
===================
idGameLocal::RotationModel
===================
*/
void idGameLocal::RotationModel( const idEntity* ent, trace_t &results, const idVec3 &start, const idRotation &rotation, const idClipModel *mdl, const idMat3 &trmAxis, int contentMask, idCollisionModel *model, const idVec3 &modelOrigin, const idMat3 &modelAxis ) {
	idClip* clipWorld = GetEntityClipWorld( ent );
	
	if( clipWorld ) {
		clipWorld->RotationModel( results, start, rotation, mdl, trmAxis, contentMask, model, modelOrigin, modelAxis );
	}
}

/*
===================
idGameLocal::ContactsModel
===================
*/
int idGameLocal::ContactsModel( const idEntity* ent, contactInfo_t *contacts, const int maxContacts, const idVec3 &start, const idVec6 &dir, const float depth, const idClipModel *mdl, const idMat3 &trmAxis, int contentMask, idCollisionModel *model, const idVec3 &modelOrigin, const idMat3 &modelAxis ) {
	idClip* clipWorld = GetEntityClipWorld( ent );
	
	if( clipWorld ) {
		return clipWorld->ContactsModel( contacts, maxContacts, start, dir, depth, mdl, trmAxis, contentMask, model, modelOrigin, modelAxis );
	}

	return 0;
}

/*
===================
idGameLocal::ContentsModel
===================
*/
int idGameLocal::ContentsModel( const idEntity* ent, const idVec3 &start, const idClipModel *mdl, const idMat3 &trmAxis, int contentMask, idCollisionModel *model, const idVec3 &modelOrigin, const idMat3 &modelAxis ) {
	idClip* clipWorld = GetEntityClipWorld( ent );
	
	if( clipWorld ) {
		return clipWorld->ContentsModel( start, mdl, trmAxis, contentMask, model, modelOrigin, modelAxis );
	}

	return 0;
}

/*
===================
idGameLocal::TranslationEntities
===================
*/	
void idGameLocal::TranslationEntities( const idEntity* ent, trace_t &results, const idVec3 &start, const idVec3 &end, const idClipModel *mdl, const idMat3 &trmAxis, int contentMask, const idEntity *passEntity, const idEntity *passEntity2 ) {
	idClip* clipWorld = GetEntityClipWorld( ent );
	
	if( clipWorld ) {
		clipWorld->TranslationEntities( results, start, end, mdl, trmAxis, contentMask, passEntity, passEntity2 );
	}
}

/*
===================
idGameLocal::GetModelContactFeature
===================
*/
bool idGameLocal::GetModelContactFeature( const idEntity* ent, const contactInfo_t &contact, const idClipModel *clipModel, idFixedWinding &winding ) const {
	const idClip* clipWorld = GetEntityClipWorld( ent );
	
	if( clipWorld ) {
		return clipWorld->GetModelContactFeature( contact, clipModel, winding );
	}

	return false;
}

/*
===================
idGameLocal::EntitiesTouchingBounds
===================
*/
int idGameLocal::EntitiesTouchingBounds	( const idEntity* ent, const idBounds &bounds, int contentMask, idEntity **entityList, int maxCount ) const {
	const idClip* clipWorld = GetEntityClipWorld( ent );
	
	if( clipWorld ) {
		return clipWorld->EntitiesTouchingBounds( bounds, contentMask, entityList, maxCount );
	}

	return 0;
}

/*
===================
idGameLocal::ClipModelsTouchingBounds
===================
*/
int idGameLocal::ClipModelsTouchingBounds( const idEntity* ent, const idBounds &bounds, int contentMask, idClipModel **clipModelList, int maxCount ) const {
	const idClip* clipWorld = GetEntityClipWorld( ent );
	
	if( clipWorld ) {
		return clipWorld->ClipModelsTouchingBounds( bounds, contentMask, clipModelList, maxCount );
	}

	return 0;
}

/*
===================
idGameLocal::PlayersTouchingBounds
===================
*/
int idGameLocal::PlayersTouchingBounds	( const idEntity* ent, const idBounds &bounds, int contentMask, idPlayer **entityList, int maxCount ) const {
	const idClip* clipWorld = GetEntityClipWorld( ent );
	
	if( clipWorld ) {
		return clipWorld->PlayersTouchingBounds( bounds, contentMask, entityList, maxCount );
	}

	return 0;
}

/*
===================
idGameLocal::GetWorldBounds
===================
*/
const idBounds& idGameLocal::GetWorldBounds( const idEntity* ent ) const {
	const idClip* clipWorld = GetEntityClipWorld( ent );
	
	if ( clipWorld ) {
		return clipWorld->GetWorldBounds();
	}

	return clip[ 0 ]->GetWorldBounds();
}

/*
===================
idGameLocal::GetEntityClipWorld
===================
*/
idClip* idGameLocal::GetEntityClipWorld( const idEntity* ent ) {
	if( ent == NULL ) {
		return clip[ 0 ];
	}

	if( ent->GetClipWorld() < 0 || ent->GetClipWorld() >= clip.Num() ) {
		Warning( "idGameLocal::GetEntityClipWorld() - invalid clip world %d on entity %s (valid range: 0 - %d)\n", ent->GetClipWorld(), ent->GetClassname(), clip.Num() - 1 );
		return NULL;
	}
	return clip[ ent->GetClipWorld() ];
}

/*
===================
idGameLocal::GetEntityClipWorld
===================
*/
const idClip* idGameLocal::GetEntityClipWorld( const idEntity* ent ) const {
	if( ent == NULL ) {
		return clip[ 0 ];
	}

	if( ent->GetClipWorld() < 0 || ent->GetClipWorld() >= clip.Num() ) {
		Warning( "idGameLocal::GetEntityClipWorld() - invalid clip world %d on entity %s (valid range: 0 - %d)\n", ent->GetClipWorld(), ent->GetClassname(), clip.Num() - 1  );
		return NULL;
	}
	return clip[ ent->GetClipWorld() ];
}

/*
===================
idGameLocal::AddClipWorld
===================
*/
int idGameLocal::AddClipWorld( int id ) {
	if( id >= clip.Num() ) {
		// if we want an index higher in the list, fill the intermediate indices with empties
		for( int i = clip.Num(); i <= id; i++ ) {
			clip.Append( NULL );
		}
	}

	if( clip[ id ] == NULL ) {
// RAVEN BEGIN
// mwhitlock: Dynamic memory consolidation
		RV_PUSH_SYS_HEAP_ID(RV_HEAP_ID_LEVEL);
// RAVEN END
		clip[ id ] = new idClip();
// RAVEN BEGIN
// mwhitlock: Dynamic memory consolidation
		RV_POP_HEAP();
// RAVEN END
		clip[ id ]->Init();
	}
	return id;
}

/*
===================
idGameLocal::RemoveClipWorld
===================
*/
void idGameLocal::RemoveClipWorld( int id ) {
	assert( id >= 0 && id < clip.Num() );

	clip[ id ]->Shutdown();
	delete clip[ id ];
	clip[ id ] = NULL;
}

/*
===================
idGameLocal::ShutdownInstances
===================
*/
void idGameLocal::ShutdownInstances( void ) {
	if( gamestate == GAMESTATE_UNINITIALIZED ) {
		return;
	}

	instances.DeleteContents( true );

	// free the trace model used for the defaultClipModel
	idClip::FreeDefaultClipModel();
}

/*
===================
idGameLocal::AddInstance
===================
*/
int idGameLocal::AddInstance( int id, bool deferPopulate ) {
	if ( id == -1 ) {
		id = instances.Num();
	}

	if ( id >= instances.Num() ) {
		// if we want an index higher in the list, fill the intermediate indices with empties
		for( int i = instances.Num(); i <= id; i++ ) {
			instances.Append( NULL );
		}
	}

	if ( instances[ id ] == NULL ) {
// RAVEN BEGIN
// mwhitlock: Dynamic memory consolidation
		RV_PUSH_SYS_HEAP_ID(RV_HEAP_ID_LEVEL);
		instances[ id ] = new rvInstance( id, deferPopulate );
		RV_POP_HEAP();
// RAVEN END

		if ( !deferPopulate ) {
			// keep track of the high watermark
			ServerSetEntityIndexWatermark( id );
		}

		common->DPrintf( "idGameLocal::AddInstance(): Adding instance %d\n", instances[ id ]->GetInstanceID() );
	} else {
		common->DPrintf( "idGameLocal::AddInstance(): Instance %d already exists\n", instances[ id ]->GetInstanceID() );
	}
	
	// keep the min spawn index correctly set
	ServerSetMinSpawnIndex();
	
	return instances[ id ]->GetInstanceID();
}

/*
===================
idGameLocal::RemoveInstance
===================
*/
void idGameLocal::RemoveInstance( int id ) {
	delete instances[ id ];
	instances[ id ] = NULL;
}

/*
===================
idGameLocal::GetPlayerName
Returns the specified player name, max of 64 chars
===================
*/
void idGameLocal::GetPlayerName( int clientNum, char* name ) {
	if( !gameLocal.entities[ clientNum ] ) {
		return;
	}

	idStr::Copynz( name, gameLocal.GetUserInfo( clientNum )->GetString( "ui_name" ), 64 );
}

/*
===================
idGameLocal::GetPlayerClan
Returns the specified player clan, max of 64 chars
===================
*/
void idGameLocal::GetPlayerClan( int clientNum, char* clan ) {
	if( !gameLocal.entities[ clientNum ] ) {
		return;
	}

	idStr::Copynz( clan, gameLocal.GetUserInfo( clientNum )->GetString( "ui_clan" ), 64 );
}

/*
===================
idGameLocal::SetFriend
===================
*/
void idGameLocal::SetFriend( int clientNum, bool isFriend ) {
	if( !gameLocal.GetLocalPlayer() ) {
		Warning( "idGameLocal::SetFriend() - SetFriend() called with NULL local player\n" );
		return;
	}

	gameLocal.GetLocalPlayer()->SetFriend( clientNum, isFriend );
}

/*
===================
idGameLocal::GetLongGametypeName
===================
*/
const char*	idGameLocal::GetLongGametypeName( const char* gametype ) {
	return mpGame.GetLongGametypeName( gametype );
}

void idGameLocal::Cmd_PrintMapEntityNumbers_f( const idCmdArgs& args ) {
	int instance = 0;

	if ( args.Argc() > 1 ) {
		instance = atoi( args.Argv( 1 ) );
	} 

	if( gameLocal.instances[ instance ] ) {
		gameLocal.instances[ instance ]->PrintMapNumbers();
	}
}

void idGameLocal::Cmd_PrintSpawnIds_f( const idCmdArgs& args ) {
	for( int i = 0; i < MAX_GENTITIES; i++ ) {
		if( gameLocal.entities[ i ] ) {
			gameLocal.Printf( "Spawn id %d: %d\n", i, gameLocal.spawnIds[ i ] );
		}
	}
}

/*
===============
idGameLocal::IsTeamPowerups
===============
*/
bool idGameLocal::IsTeamPowerups( void ) {
	if ( !serverInfo.GetBool( "si_isBuyingEnabled" ) ) {
		return false;
	}
	if ( !IsTeamGameType() ) {
		return false;
	}
	return ( gameType != GAME_ARENA_CTF );
}

/*
===============
idGameLocal::GetRandomBotName
===============
*/
void idGameLocal::GetRandomBotName( int clientNum, idStr& name ) {
	(void)clientNum;
	name = "Quake4Bot";
}

// RAVEN BEGIN
// mwhitlock: Dynamic memory consolidation
#if defined(_RV_MEM_SYS_SUPPORT)
/*
===================
idGameLocal::FlushBeforelevelLoad
===================
*/
void idGameLocal::FlushBeforelevelLoad( void )
{
	TIME_THIS_SCOPE( __FUNCLINE__);

#ifndef _XENON
	MapShutdown();
#else
	mpGame.Clear();
#endif
	for(int i = 0; i < aasNames.Num(); i++)
	{
		aasList[i]->Shutdown();
	}
}
#endif
// RAVEN END

// dluetscher: moved the overloaded new/delete to sys_local.cpp and Game_local.cpp (from Heap.h)
//			   so that the tools.dll will link.
#if !defined(_XBOX) && (defined(ID_REDIRECT_NEWDELETE) || defined(_RV_MEM_SYS_SUPPORT))

#undef new
#undef delete
#undef Mem_Alloc
#undef Mem_Free

#ifdef ID_DEBUG_MEMORY
void *operator new( size_t s, int t1, int t2, char *fileName, int lineNumber ) {
	return Mem_Alloc( s, fileName, lineNumber, MemScopedTag_GetTopTag() );
}

void operator delete( void *p, int t1, int t2, char *fileName, int lineNumber ) {
	Mem_Free( p, fileName, lineNumber );
}

void *operator new[]( size_t s, int t1, int t2, char *fileName, int lineNumber ) {
	return Mem_Alloc( s, fileName, lineNumber, MemScopedTag_GetTopTag() );
}

void operator delete[]( void *p, int t1, int t2, char *fileName, int lineNumber ) {
	Mem_Free( p, fileName, lineNumber );
}

void *operator new( size_t s ) {
	return Mem_Alloc( s, "", 0, MemScopedTag_GetTopTag() );
}

void operator delete( void *p ) {
	Mem_Free( p, "", 0 );
}

void *operator new[]( size_t s ) {
	return Mem_Alloc( s, "", 0, MemScopedTag_GetTopTag() );
}

void operator delete[]( void *p ) {
	Mem_Free( p, "", 0 );
}

#else	// #ifdef ID_DEBUG_MEMORY

void *operator new( size_t s ) {
	return Mem_Alloc( s, MemScopedTag_GetTopTag() );
}

void operator delete( void *p ) {
	Mem_Free( p );
}

void *operator new[]( size_t s ) {
	return Mem_Alloc( s, MemScopedTag_GetTopTag() );
}

void operator delete[]( void *p ) {
	Mem_Free( p );
}
#endif	// #else #ifdef ID_DEBUG_MEMORY
#endif	// #if defined(ID_REDIRECT_NEWDELETE) || defined(_RV_MEM_SYS_SUPPORT)
// RAVEN END
