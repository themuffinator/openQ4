//----------------------------------------------------------------
// NavMesh.h
//
// Runtime navigation mesh for openQ4 multiplayer bots.
//
// Quake 4 never shipped .aas files for any of its multiplayer maps, so the
// SDK's AAS routing has nothing to route over there.  Rather than require a
// per-map offline compile, this builds the navigation graph at map load by
// probing the live collision world with the player's own bounding box, the
// same way a modern navmesh generator voxelises a scene - except the "voxels"
// come from the engine's collision traces, so anything the graph claims is
// walkable is walkable by construction.
//
// The result is a multi-level grid navmesh: one node per (grid cell, floor
// level) pair, linked to its neighbours by traversal edges that record how the
// move has to be made (walk, drop, jump pad, teleporter).  Routing is A* over
// those links, and the resulting corner list is string-pulled against the
// collision world so bots move in straight lines instead of tracing the grid.
//----------------------------------------------------------------

#ifndef __GAME_MP_NAVMESH_H__
#define __GAME_MP_NAVMESH_H__

class idEntity;
class idPlayer;

//----------------------------------------------------------------
// How a link between two nodes has to be travelled.  The bot's movement code
// switches on this: walking is steering, a drop is a walk off a ledge, and the
// two volume links are "get into the volume and let it carry you".
//----------------------------------------------------------------
typedef enum {
	NAVTRAVEL_WALK,				// flat ground, stairs, ramps
	NAVTRAVEL_DROP,				// step off a ledge, one way
	NAVTRAVEL_JUMP,				// hop up onto a ledge a step cannot reach
	NAVTRAVEL_JUMPPAD,			// walk into an rvJumpPad, one way
	NAVTRAVEL_TELEPORT,			// walk into a teleporter trigger, one way
	NAVTRAVEL_NUM
} navTravelType_t;

//----------------------------------------------------------------
// A single navigation node.  origin sits on the floor, at the centre of its
// grid cell, so it is directly usable as a movement target.
//----------------------------------------------------------------
typedef struct navNode_s {
	idVec3					origin;
	int						gx;				// grid coordinates, for neighbour lookups
	int						gy;
	int						firstLink;		// head of the singly linked outgoing link list, -1 if isolated
	int						area;			// weakly connected component id (links taken as undirected)
	int						component;		// strongly connected component id, for exact directed reachability
	int						flags;			// NAVNODE_* bits
} navNode_t;

// A standing point inside a damaging but survivable volume.  Routing pays a
// penalty to enter it rather than refusing it, so a map whose only connection
// crosses a pain floor stays connected; lethal volumes never produce nodes.
static const int NAVNODE_HAZARD			= BIT( 0 );
// A standing point whose box touches a live jump pad or teleporter volume.
// Whoever stands there is carried away, so the transport is its only exit and
// it is never a place to stop.
static const int NAVNODE_TRANSPORT		= BIT( 1 );
// A standing point whose hull hangs more than NAV_RIM_OVERHANG past an edge:
// the last few units of a platform, the rim of a catwalk.  A player can stand
// there, so it stays a node, but routing pays to walk through it so a chain a
// cell inland is preferred wherever one exists.
static const int NAVNODE_RIM			= BIT( 2 );

// Places one jump pad can put a player down: its natural landing and the
// floors beside its arc that air control reaches.
static const int NAV_MAX_TRANSPORT_LANDINGS	= 4;

//----------------------------------------------------------------
// How a player in flight brings it down on a landing with air control: steer
// for the horizontal velocity that arrives over the landing as the arc comes
// down to its height, not at the landing itself.  Full air control toward the
// spot overshoots and swings past it and back - over a pad's shaft, the
// difference between the ledge beside it and the bottom of the shaft.  The bot
// in flight and the navmesh's pad simulation both steer with this, so a
// landing the graph links is the one the bot makes.  Returns false when the
// velocity is already right, which means coast.
//----------------------------------------------------------------
static const float NAV_AIR_STEER_DEADBAND	= 12.0f;	// velocity error, units/sec, worth correcting

ID_INLINE bool NavAirSteer( const idVec3 &origin, const idVec3 &velocity, const idVec3 &landing,
							float gravity, idVec3 &wishDir ) {
	if ( gravity <= 1.0f ) {
		return false;
	}
	const float above = origin.z - landing.z;
	const float discriminant = velocity.z * velocity.z + 2.0f * gravity * above;
	float time = ( discriminant >= 0.0f ) ? ( velocity.z + idMath::Sqrt( discriminant ) ) / gravity :
											velocity.z / gravity;	// out of reach: steer by the apex
	if ( time < 0.1f ) {
		time = 0.1f;
	}

	idVec3 error( ( landing.x - origin.x ) / time - velocity.x,
				  ( landing.y - origin.y ) / time - velocity.y, 0.0f );
	if ( error.LengthSqr() < NAV_AIR_STEER_DEADBAND * NAV_AIR_STEER_DEADBAND ) {
		return false;
	}
	error.Normalize();
	wishDir = error;
	return true;
}

//----------------------------------------------------------------
// A goal a cost field is asked to price.  When a field is given weights it
// ranks targets by weight / ( route cost + extra ) - extra being whatever the
// caller adds outside the graph, such as the exact leg from the node to the
// item - and stops as soon as no unpriced target could still enter the best
// few.  A zero weight asks for the target to be priced regardless.
//----------------------------------------------------------------
typedef struct navFieldTarget_s {
	int						node;
	float					weight;			// the most reaching it can be worth
	float					minWeight;		// the least, once priced (a timed item's wait discount)
	float					extra;
} navFieldTarget_t;

//----------------------------------------------------------------
// Counters read by bot_profile.  They are cumulative until ResetStats.
//----------------------------------------------------------------
typedef struct navStats_s {
	int						pathSearches;	// FindPath calls
	int						pathFailures;	// of which found no route
	int						quickRejects;	// refused by component reachability without searching
	int						snapFailures;	// an endpoint had no proven node within reach
	int						searchFailures;	// the search itself, or its repairs, found nothing
	int						legFailures;	// the final shortcut onto the exact goal was unsafe
	int						repairSearches;	// extra A* passes after a selected edge proved stale
	int						nodesExpanded;	// A* pops, all passes
	int						fieldSearches;	// single-source cost fields
	int						fieldNodes;		// nodes those fields settled
	double					searchTicks;	// clock ticks spent inside FindPath and cost fields
	double					fieldTicks;		// of which inside cost fields
} navStats_t;

typedef struct navLink_s {
	int						node;			// destination node
	int						next;			// next link out of the same source node, -1 terminates
	float					cost;
	short					travelType;		// navTravelType_t
	short					actionEntityNum;// pad/teleporter that owns this link, -1 otherwise
} navLink_t;

//----------------------------------------------------------------
// One corner of a route.  travelType describes how the bot gets to this
// corner from the previous one, so the movement code knows when it is walking
// and when it has to commit to a jump pad or a teleporter.
//----------------------------------------------------------------
typedef struct navCorner_s {
	idVec3					origin;
	short					travelType;
	short					actionEntityNum;// preserves volume identity through string pulling
} navCorner_t;

class rvNavPath {
public:
							rvNavPath( void ) {
								corners.SetGranularity( 32 );
								travelStart.Zero();
								routeStart.Zero();
								travelCost = 0.0f;
								hasTravelCost = false;
							}

	void					Clear( void ) {
								corners.Clear();
								travelStart.Zero();
								routeStart.Zero();
								travelCost = 0.0f;
								hasTravelCost = false;
							}
	int						Num( void ) const { return corners.Num(); }
	bool					IsEmpty( void ) const { return corners.Num() == 0; }
	const navCorner_t &		operator[]( int index ) const { return corners[index]; }

	// Where the route's first leg begins on the ground: the search origin, or
	// the floor under it when the search was made in the air.  Zero for a
	// path FindPath did not produce.
	const idVec3 &			RouteStart( void ) const { return routeStart; }

	void					Append( const idVec3 &origin, int travelType, int actionEntityNum = -1 );

	// Total length of the remaining route, used to compare goals.
	float					Length( void ) const;
	// Weighted route cost from the exact origin used for the search.  Falls
	// back to geometric corner length when the caller supplies another origin.
	float					LengthFrom( const idVec3 &origin ) const;

private:
	friend class rvNavMesh;

	void					SetTravelCost( const idVec3 &origin, float cost );

	idList<navCorner_t>		corners;
	idVec3					travelStart;	// exact origin used by the route search
	idVec3					routeStart;		// that origin on the floor the route starts from
	float					travelCost;		// graph cost, including traversal penalties/shortcuts
	bool					hasTravelCost;
};

//----------------------------------------------------------------
// rvNavMesh
//----------------------------------------------------------------
class rvNavMesh {
public:
							rvNavMesh( void );
							~rvNavMesh( void );

	// Probe the collision world and build the graph.  Safe to call again; the
	// previous graph is thrown away first.  Returns false if nothing walkable
	// was found, which is the "this map cannot host bots" answer.
	bool					Build( void );
	void					Clear( void );

	bool					IsValid( void ) const { return nodes.Num() > 0; }
	int						NumNodes( void ) const { return nodes.Num(); }
	int						NumLinks( void ) const { return links.Num(); }
	int						GetBuildMilliseconds( void ) const { return buildMsec; }

	// Diagnostics for "navmesh probe": the nodes around a point with their
	// links and support verdicts, and a height map of the ground around it.
	void					Probe( const idVec3 &point, float radius ) const;

	const navNode_t &		GetNode( int index ) const { return nodes[index]; }

	// Nearest sampled standable node.  Returns -1 when the point is too far
	// from the mesh entirely.  Routing/recovery callers can require a proven
	// walkable connection from the exact point instead of snapping through a
	// thin wall or onto another floor layer.
	int						FindNearestNode( const idVec3 &origin, float maxRadius = 256.0f,
											 bool requireWalkable = false ) const;

	// The node a route to or from this exact point would use: an airborne
	// point is first projected onto the floor it would land on, exactly as
	// FindPath snaps its ends.  -1 when the point has no proven node.
	int						SnapNode( const idVec3 &point ) const;

	// SnapNode for an entity's origin, cached per entity spawn and position.
	// Items, flag bases and control volumes stand still for a whole map, so a
	// decision tick pays a table lookup instead of a ring of walkability probes.
	int						NodeForEntity( const idEntity *ent ) const;

	// Exact directed reachability between two nodes from the strongly connected
	// component condensation.  Constant time; never searches.
	bool					NodeCanReach( int fromNode, int toNode ) const;

	// A* from start to goal.  Both ends are snapped onto the mesh first.  The
	// returned path is string-pulled and ends at the exact goal, except that an
	// airborne endpoint may conservatively end at its projected landing floor.
	// nearGoal is for pursuit: a player standing where no node is proven
	// walkable - a tight diagonal corridor, the lip of a ledge - is routed to
	// the nearest node on the same floor instead of refused.
	// A finite maxImpactDelta excludes locally repaired edges whose predicted
	// landing would exceed the caller's currently survivable fall-damage tier.
	bool					FindPath( const idVec3 &start, const idVec3 &goal, rvNavPath &path,
									  bool nearGoal = false, float maxImpactDelta = idMath::INFINITY ) const;

	// Directed "is there any route at all" test.  Answered from the component
	// condensation first; only a reachable pair pays for the route proof.
	bool					IsReachable( const idVec3 &start, const idVec3 &goal ) const;

	// Random node with a directed route from origin, for roaming.  When
	// pathOut is supplied, the successful search is returned for immediate use.
	bool					RandomReachablePoint( const idVec3 &origin, idVec3 &result,
												 rvNavPath *pathOut = NULL, float maxImpactDelta = idMath::INFINITY ) const;

	// One bounded single-source search from origin that prices every node a
	// goal choice might want, instead of one A* per candidate.  When targets
	// are given the search stops as soon as every one of them is priced.
	// Returns the start node, or -1.  The field is valid until the next
	// search of any kind; CostFieldDistance answers infinity once replaced.
	int						BuildCostField( const idVec3 &origin, float maxCost,
											const idList<int> *targets = NULL ) const;
	// The ranked form prices targets exactly, best landmark bound first, and
	// stops once the keepBest highest-scoring are certain; the rest answer
	// infinity.  A good pickup nearby spares the search the rest of the map.
	int						BuildCostField( const idVec3 &origin, float maxCost,
											const navFieldTarget_t *targets, int numTargets,
											int keepBest ) const;
	float					CostFieldDistance( int node ) const;

	// Exact route cost between two nodes, NAV_INFINITE_COST past maxCost or
	// without a route.  No corners and no live validation: FindPath does both.
	float					PathCost( int startNode, int goalNode, float maxCost ) const;

	// A lower bound on the route cost between two nodes, in constant time:
	// the landmark bound, which sees walls and floors where straight-line
	// distance does not.  At least 1e29 when no route exists at all.
	float					RouteLowerBound( int fromNode, int toNode ) const;

	// Would an agent standing on this floor point touch a hazard volume?
	// lethalOnly ignores survivable pain volumes.  Lava and slime count.
	bool					IsHazardous( const idVec3 &floor, bool lethalOnly ) const;

	// The jump pad or teleporter, by entity number, whose volume these world
	// bounds touch, or -1.  Touching one carries the player away.
	int						TransportAt( const idBounds &absBounds ) const;
	// The same for a standing box on this floor point, tested against the
	// volume's own brush where it has one: a diagonal pad's bounding box
	// covers floor beside it that the pad never touches.
	int						TransportAtFloor( const idVec3 &floor ) const;
	// The live transport, other than ignoreEntityNum, that a standing box
	// carried from one origin to another would touch - exactly, against the
	// volume's brush where it has one - or -1.  A box already touching one
	// counts: the pad throws on any frame of contact.
	int						TransportOnMove( const idVec3 &from, const idVec3 &to, int ignoreEntityNum ) const;
	// The centre of a live transport's volume, false for an unknown entity.
	bool					TransportCentre( int entityNum, idVec3 &centre ) const;

	// Is this landing of the pad one only air control reaches - not where the
	// pad's own arc comes down?  A bot rides a natural landing and steers for
	// a steered one.
	bool					IsSteeredPadLanding( int padEntityNum, const idVec3 &landing ) const;
	// The proven landing reached without air input. Used after an unexpected
	// touch of a pad, when steering for the old walking route would brake it.
	bool					NaturalPadLanding( int padEntityNum, idVec3 &landing ) const;
	bool					LandingSurvivable( const idVec3 &from, const navCorner_t &corner, float maxImpactDelta ) const;
	bool					LandingContinuationClear( const idVec3 &origin, const idVec3 &next ) const;
	// A planned drop must cross its source rim. Recheck the live pose, momentum
	// and ground input before overriding the ordinary walking edge brake.
	bool					DropTakeoffClear( const idVec3 &origin, const idVec3 &velocity, const idVec3 &landing,
										 const idVec3 &wish, float maxImpactDelta ) const {
		return RunningDropClear( origin, landing, &velocity, &wish, maxImpactDelta );
	}
	// Whether a fall from this state under one fixed horizontal air input (zero
	// for none) comes down on supported, non-lethal ground with room to stop,
	// within maxImpactDelta of landing damage.
	// landedSupported, when given, reports a supported, affordable landing
	// even when it leaves no room to stop. touchedAffordable reports only a
	// physical, affordable non-transport touchdown; it does not certify support.
	// brakedAffordable is recovery-only: the hull retains floor contact through
	// the landing frame and braking, even where broad route support is absent.
	bool					AirInputLandingSafe( const idVec3 &origin, const idVec3 &velocity, const idVec3 &wish,
											 float maxImpactDelta, idVec3 *touchdown = NULL,
											 bool *landedSupported = NULL, bool *touchedAffordable = NULL,
											 bool *brakedAffordable = NULL, int knockMsec = 0, const idPlayer *self = NULL ) const;
	bool					RecoveryBrakeClear( const trace_t &impact, const idVec3 &velocity,
											 float remainingSeconds, int knockMsec ) const;
	bool					KnockSlideLandingSafe( const idVec3 &origin, const idVec3 &velocity,
											 int knockMsec, float maxImpactDelta, const idVec3 &wish = vec3_origin ) const;
	// Diagnostics: would a route leg between these points be accepted as a
	// walk now, and if not, which test refuses it.
	bool					WalkLegAccepted( const idVec3 &from, const idVec3 &to, const char **failure ) const {
								return WalkableLine( from, to, failure );
							}
	// A grounded hull can overhang a rim even though its center is unsupported.
	// Only recovery uses this short, continuously supported path back inland.
	bool					RecoveryStepClear( const idVec3 &origin, const idVec3 &target , idVec3 *supportedOrigin = NULL) const;
	// Recheck a routed jump at its actual takeoff, including lateral momentum
	// and the corner acceptance offset that the graph's central proof omits.
	bool					JumpTakeoffClear( const idVec3 &origin, const idVec3 &velocity,
										 const idVec3 &landing, float gravity, float wishSpeed,
										 const idVec3 *firstWish = NULL, float firstWishSpeed = 0.0f ) const;
	// A discretionary combat/recovery hop must prove touchdown as well as the
	// short walking lookahead. Deliberate route jumps keep their own proof.
	bool					DodgeJumpClear( const idVec3 &origin, const idVec3 &velocity,
										const idVec3 &direction, float gravity, float effectiveWishSpeed = 0.0f ) const;

	const navStats_t &		GetStats( void ) const { return stats; }
	void					ResetStats( void ) const;
	int						NumComponents( void ) const { return numComponents; }
	int						NumLandmarks( void ) const { return numLandmarks; }
	int						NumHazards( void ) const { return hazards.Num(); }
	int						NumTransports( void ) const { return transports.Num(); }

	void					DebugDraw( const idVec3 &viewOrigin, float radius ) const;

	static const idBounds &	GetAgentBounds( void );

private:
	// A damaging volume found at build time.  Lethal ones refuse ground; the
	// rest make the ground they cover expensive.
	typedef struct navHazard_s {
		idBounds				bounds;
		bool					lethal;
	} navHazard_t;

	typedef struct navEntitySnap_s {
		int						spawnId;		// gameLocal.spawnIds value the snap belongs to, -1 empty
		idVec3					origin;			// entity origin when snapped
		int						node;
		int						time;			// when it was snapped; a failed snap is retried after a while
	} navEntitySnap_t;

	// A jump pad or teleporter whose link was built: the volume that carries
	// whoever touches it, and where it can put them down.
	typedef struct navTransport_s {
		idBounds				bounds;
		int						entityNum;
		int						travelType;
		int						destinations[NAV_MAX_TRANSPORT_LANDINGS];	// landing nodes
		float					costs[NAV_MAX_TRANSPORT_LANDINGS];			// and what reaching each costs
		int						numDestinations;
		bool					naturalFirst;	// destinations[0] is where the pad's own arc comes down
	} navTransport_t;

	// -- generation --
	void					GatherSeeds( idList<idVec3> &seeds ) const;
	void					GatherHazards( void );
	int						HazardAtFloor( const idVec3 &floor ) const;
	void					FloodFill( const idList<idVec3> &seeds );
	void					AddGapJumpLinks( void );
	bool					GapJumpClear( const idVec3 &start, const idVec3 &end, float runSpeed,
										  float jumpSpeed, float gravity, bool stepHop = false,
										  const idVec3 *actualVelocity = NULL, float actualWishSpeed = 0.0f,
										  const idVec3 *firstWish = NULL, float firstWishSpeed = 0.0f ) const;
	int						LevelNodeInCell( int gx, int gy, float z, float tolerance ) const;
	void					AddOffMeshLinks( void );
	bool					PadLanding( idEntity *pad, const idVec3 &launch, const idVec3 *steerTo,
										idVec3 &landing, float *flightSeconds = NULL, float *impactDelta = NULL ) const;
	bool					PadLandings( idEntity *pad, int from, const idBounds &entryBounds, int targetNode,
										 idList<int> &landings, idList<float> &costs ) const;
	bool					PadFlightReaches( idEntity *pad, const idVec3 *launches, int numLaunches,
											  const idVec3 &target, float &centreFlight, bool natural ) const;
	int						NearestLandingNode( const idVec3 &touchdown, int from, const idBounds &entryBounds ) const;
	void					SealTransportVolumes( void );
	void					CompactLinks( void );
	bool					LineEntersTransport( const idVec3 &from, const idVec3 &to ) const;
	void					BuildAreas( void );
	void					BuildComponents( void );
	void					BuildLandmarks( void );
	void					Dijkstra( int source, bool reverse, float *distances ) const;
	void					RankedSettle( int startNode, float maxCost, float startLeg,
										  const navFieldTarget_t *targets, const int *remaining,
										  int numRemaining, bool ranked, int keepBest, float *best,
										  int &numBest, idList<int> &pricedNode,
										  idList<float> &pricedCost ) const;
	float					Heuristic( int node, int goal ) const;

	bool					SampleFloor( const idVec3 &start, float maxDrop, idVec3 &floorOut ) const;
	bool					GroundUnder( const idVec3 &point, idVec3 &ground ) const;
	bool					TryStep( const idVec3 &fromFloor, const idVec3 &toCentre, idVec3 &toFloor, int &travelType ) const;
	bool					RunningDropClear( const idVec3 &from, const idVec3 &to,
										 const idVec3 *initialVelocity = NULL, const idVec3 *groundWish = NULL,
										 float maxImpactDelta = idMath::INFINITY ) const;

	int						AddNode( int gx, int gy, const idVec3 &origin );
	int						FindNode( int gx, int gy, float z ) const;
	// The cell's hash bucket.  idHashIndex::GenerateKey adds its two inputs,
	// so plain coordinates put every anti-diagonal of the grid in one chain -
	// about eighty nodes deep on q4dm5, walked for every cell a search or a
	// snap looks at.  Mixing each coordinate first spreads the cells out.
	int						CellKey( int gx, int gy ) const {
								return nodeHash.GenerateKey( (int)( (unsigned int)gx * 73856093u ),
															 (int)( (unsigned int)gy * 19349663u ) );
							}
	void					LinkNodes( int from, int to, float cost, int travelType,
									   int actionEntityNum = -1 );

	// Snap a world point onto the mesh, adding a link entity's landing spot.
	int						NodeForEntityPoint( const idVec3 &point ) const;

	idVec3					CellCentre( int gx, int gy, float z ) const;
	void					CellCoords( const idVec3 &origin, int &gx, int &gy ) const;

	// -- routing --
	bool					FindPathInternal( const idVec3 &start, const idVec3 &goal, rvNavPath &path,
											  bool nearGoal, float maxImpactDelta ) const;
	// A corner-to-corner leg: a climbing or descending line is proven from
	// either side of its corner as well, where the bot may count it reached.
	bool					WalkableLeg( const idVec3 &from, const idVec3 &to ) const;
	// failure, when given, names the first test a refused line failed.
	bool					WalkableLine( const idVec3 &from, const idVec3 &to,
										  const char **failure = NULL ) const;
	bool					IsLethalFloor( const idVec3 &floor ) const;
	bool					LegTouchesGoal( const idVec3 &from, const idVec3 &goal, idVec3 &touch ) const;
	bool					LinkTraversable( int fromNode, int linkIndex ) const;
	int						FindLink( int fromNode, int toNode ) const;
	void					StringPull( const idList<int> &nodePath, const idVec3 &goal, rvNavPath &path ) const;
	int						LinkTravelType( int fromNode, int toNode ) const;
	int						LinkActionEntity( int fromNode, int toNode ) const;

	// The heap carries its own ordering key.  Reading fScore[] at compare time
	// would be wrong: relaxing a node already in the heap rewrites its score
	// and would silently reorder entries that are already placed.
	typedef struct navHeapEntry_s {
		int						node;
		float					key;
	} navHeapEntry_t;

	void					HeapPush( int node, float key ) const;
	int						HeapPop( void ) const;
	void					BeginSearch( void ) const;

	idList<navNode_t>		nodes;
	idList<navLink_t>		links;
	idHashIndex				nodeHash;
	idList<navHazard_t>		hazards;
	bool					lethalLiquidSeen;	// flood fill refused lava or slime somewhere
	idList<navTransport_t>	transports;
	int						transportNodes;		// nodes sealed to their transport

	// Solids that can move after the build - movers, doors, plats, rotators and
	// loose physics objects - by entity number and spawn id.  Route validation
	// only ever asks about these; everything else was proven at generation.
	idList<int>				dynamicSolids;
	idList<int>				dynamicSolidSpawnIds;
	void					GatherDynamicSolids( void );
	bool					DynamicTranslation( trace_t &trace, const idVec3 &start, const idVec3 &end ) const;
	bool					DynamicSolidNear( const idBounds &bounds ) const;

	idVec3					gridOrigin;
	float					cellSize;
	int						numAreas;

	// Strongly connected components and their condensation's transitive
	// closure, one bit row per component.  Tarjan numbers components in reverse
	// topological order, which is what lets the closure be built in one pass.
	int						numComponents;
	int						componentWords;	// 32-bit words per closure row, 0 when the closure was too large
	idList<unsigned int>	componentReach;

	// ALT landmarks: exact travel costs from and to a few far-apart nodes.  The
	// triangle inequality turns them into a lower bound on any route, which is
	// both much tighter than straight-line distance around walls and immune to
	// the cheap long-range transports that collapse heuristicScale.
	int						numLandmarks;
	idList<int>				landmarkNodes;
	// One row per node: cost( landmark -> node ) then cost( node -> landmark )
	// for each landmark in turn, so a lookup touches one cache line.
	idList<float>			landmarkCost;

	// Reverse adjacency in compressed rows, built once for the backward
	// landmark searches and released afterwards.
	idList<int>				reverseOffset;	// nodes + 1 entries
	idList<int>				reverseSource;	// edge source nodes grouped by destination
	idList<float>			reverseCost;	// matching edge costs

	int						buildMsec;
	int						gapJumpLinks;
	int						trenchRimLinks;	// gap jumps over a trench to its far rim
	// Smallest stored edge-cost/geometric-distance ratio.  Scaling Euclidean
	// distance by this keeps A* admissible even with cheap long-range
	// transports; the landmarks supply the real guidance on top of it.
	float					heuristicScale;

	// Scratch state for the A* pass.  Mutable because routing is
	// logically a const query - the alternative is allocating these on every
	// search, and bots search often.
	mutable idList<float>	gScore;
	mutable idList<float>	fScore;
	mutable idList<int>		cameFrom;
	mutable idList<int>		openTag;		// search serial number, avoids clearing the arrays
	mutable idList<int>		closedTag;
	mutable idList<navHeapEntry_t>	heap;	// binary min-heap, ordered by its own stored key
	mutable int				searchSerial;
	// Links a route search has excluded after they proved stale, one tag per
	// link stamped with the search's own serial, so the exclusion test is a
	// load rather than a scan of everything rejected so far.
	mutable idList<int>		linkRejectTag;
	mutable int				rejectSerial;
	mutable int				fieldSerial;	// searchSerial of the live cost field, 0 when none
	mutable idList<int>		fieldTargetTag;	// searchSerial marks a node the live field must price
	mutable bool			fieldPriced;	// the live field is the ranked form's priced table
	mutable idList<int>		fieldPricedTag;	// fieldSerial marks a node that table priced
	mutable idList<float>	fieldPricedCost;

	mutable idList<navEntitySnap_t>	entitySnaps;	// indexed by entity number
	mutable navStats_t		stats;
	mutable int				nextSnapReportTime;	// bot_debug rate limit for endpoint snap failures
};

extern rvNavMesh			navMesh;

#endif /* !__GAME_MP_NAVMESH_H__ */
