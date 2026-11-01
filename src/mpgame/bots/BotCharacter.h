//----------------------------------------------------------------
// BotCharacter.h
//
// Bot personalities: skill levels, play styles and named characters.
//
// The bot itself only knows how to see, aim, move and shoot.  Everything that
// decides HOW WELL and IN WHAT MANNER it does those things lives here, because
// none of it belongs in code: a server operator who wants a different roster,
// a mod that wants its own cast, or a designer retuning difficulty should all
// be editing text files, not rebuilding the game.
//
// Three layers resolve into one flat botTraits_t per bot per spawn:
//
//   1. SKILL LEVEL 1..5 - the baseline curve.  Every attribute has a value at
//      each level, interpolated for the fractional levels bot_skillVariance
//      produces.  This layer alone is a complete, playable bot; it is what you
//      get with bot_characters 0.
//   2. PLAY STYLE - a named archetype (rusher, sniper, roamer, hunter,
//      ambusher, skirmisher) that biases the baseline.  A style says what a
//      bot wants to do, not how well it does it, so the same style is
//      recognisable at every skill level.
//   3. CHARACTER - identity: display name, player model, skill band, personal
//      per-attribute bias, and per-skill-level overrides.
//
// Dialogue is kept beside those layers rather than inside them.  A dedicated
// character chat file owns the voice while the character file owns gameplay,
// so either can be revised without turning the other into an unreadable wall
// of content.  Inline chat blocks remain accepted for older add-ons.
//
// Layers 2 and 3 do not restate the attribute list.  They carry sparse
// modifier statements - set, add, scale - naming a trait field by the same
// word used in botTraits_t, resolved through a static field table.  Adding an
// attribute therefore means adding one row to that table and one line to the
// baseline curve; no parser, style or character file needs to know it exists.
//
// Every trait is a float, including the millisecond and degree fields.  That
// keeps the modifier system uniform - one table, one apply path, one clamp -
// and the handful of consumers that want an int cast at the point of use.
//
// Files live in the pak and are enumerated through the file system, so mods
// and pk4s work without a build step:
//
//   botfiles/styles/<style>.style
//   botfiles/characters/<name>.bot
//   botfiles/chats/<name>.chat
//
// (The pre-existing botfiles/bots/*.c, botfiles/items.c and botfiles/weapons.c
// are dead Quake 3 format prototypes that nothing parses.  This system
// deliberately uses new subdirectories and new extensions so a reader never
// picks them up.)
//----------------------------------------------------------------

#ifndef __GAME_MP_BOTCHARACTER_H__
#define __GAME_MP_BOTCHARACTER_H__

class idLexer;

//----------------------------------------------------------------
// Limits.
//----------------------------------------------------------------

// bot_skill runs 1..5.  The baseline table has one row per level and the
// fractional levels bot_skillVariance produces are lerped between two rows.
static const int	BOT_SKILL_LEVELS		= 5;

// Per-weapon preference biases carried by a style or a character.  Twelve
// weapons exist in multiplayer - the stock ten plus the Dark Matter Gun and
// the napalm gun - so a style with an opinion about every one of them leaves
// four slots for weapon mods and future additions.
static const int	BOT_MAX_WEAPON_BIAS		= 16;

// A chat line is dropped by idMultiplayerGame::ProcessChatMessage when the
// decorated name plus the text reaches 240 characters.  Budget the text well
// under that so a long bot name can never silently eat a line.
static const int	BOT_CHAT_MAX_LEN		= 160;

// Reply content is deliberately bounded.  These files are mod supplied and
// live for the lifetime of the game module, so a typo must not turn one
// character into an unbounded collection of strings.
static const int	BOT_MAX_REPLY_RULES		= 32;
static const int	BOT_MAX_REPLY_TRIGGERS	= 32;
static const int	BOT_MAX_REPLY_LINES		= 32;
static const int	BOT_REPLY_TRIGGER_MAX_LEN = 64;

//----------------------------------------------------------------
// A personality's opinion of one weapon.  1.0 is neutral, 0.0 means the bot
// will not choose it unless it has nothing else.  Held by name rather than by
// slot because slots come from the player entityDef and mean nothing until a
// player exists, while rvBot::UpdateWeapon already walks its preference list
// by name.
//----------------------------------------------------------------
typedef struct botWeaponBias_s {
	idStr			weapon;					// weapon class name, e.g. "weapon_railgun"
	float			bias;					// multiplier on that weapon's range score
} botWeaponBias_t;

//----------------------------------------------------------------
// botTraits_t
//
// The resolved personality of one bot.  Produced once per spawn from
// (character, skill level, variance) and then read every frame; nothing in
// here is state, so it can be recomputed at any time without losing anything.
//
// Field names here are exactly the key words accepted in a .style or a .bot
// file.  Keep the two in step - the field table in BotCharacter.cpp is the
// only place that mapping is written down.
//----------------------------------------------------------------
typedef struct botTraits_s {
	// -- vision and reaction --
	float			sightRange;				// world units; beyond this a player is simply not noticed
	float			fov;					// degrees, full cone; a player outside it is not noticed at all
	float			reactionMsec;			// delay between a fresh sighting and being allowed to fire
	float			reactionVarianceMsec;	// +/- random spread rolled per acquisition, so it is never a fixed tick
	float			reacquireMsec;			// out of view longer than this and the next sighting is a fresh one
	float			reacquireFraction;		// 0..1, fraction of the full reaction paid on a re-acquisition inside that window
	float			peripheralAngle;		// degrees off view centre at which the peripheral penalty is full
	float			peripheralPenaltyMsec;	// added reaction for a target at or beyond peripheralAngle, scaled linearly

	// -- aim --
	float			turnSpeed;				// degrees/second, hard cap on the view slew rate
	float			turnAccel;				// degrees/second^2; low values make the view lag and then overshoot
	float			turnDamping;			// damping ratio of the slew; below 1.0 overshoots and hunts, 1.0 settles cleanly
	float			aimTrackTimeConst;		// seconds; first-order lag of the believed target position behind the real one
	float			aimAnticipation;		// 0..1, share of the target's perceived motion the aim follows instead of trailing
	float			aimTremorDeg;			// degrees, amplitude of the continuous hand tremor
	float			aimTremorRateHz;		// Hz, how fast that tremor wanders; low and wide reads as an unsteady hand
	float			aimTrackError;			// degrees of cross-view lag per 100 degrees/second of target angular rate
	float			aimSettleMsec;			// how long the aim must sit inside the fire cone before the trigger is allowed
	float			aimLead;				// 0..1, how correctly a moving target is led with a projectile weapon
	float			aimLeadError;			// 0..1, random fraction of the computed lead added or removed per shot

	// -- trigger --
	float			fireConeDeg;			// half angle; the aim must be this close to the shot point to fire
	float			holdFireTurnRate;		// degrees/second; the bot will not fire while the view is slewing faster than this
	float			burstMinMsec;			// automatic weapons: shortest burst
	float			burstMaxMsec;			// automatic weapons: longest burst
	float			burstPauseMsec;			// gap between bursts, during which the aim re-settles

	// -- mistakes --
	float			mistakeChance;			// 0..1, chance a combat decision comes out wrong
	float			mistakeMsec;			// how long one mistake lasts before the bot recovers from it

	// -- movement and engagement --
	float			strafeChance;			// 0..1, how much the bot dodges while fighting
	float			dodgeReactMsec;			// delay between taking fire and starting to dodge it
	float			jumpChance;				// 0..1, chance a dodge becomes a jump instead of a sidestep
	float			combatRange;			// world units, the distance the bot tries to hold from its enemy
	float			rangeDiscipline;		// 0..1, how hard it works to stay at combatRange
	float			aggression;				// 0..1, push in versus back off when the trade is even
	float			patience;				// 0..1, willingness to hold a position and wait rather than go looking
	float			retreatHealth;			// 0..1 of max health; below this the bot breaks off and looks for health
	float			pursuit;				// 0..1, how long a lost enemy is chased before the bot gives up on it
	float			itemFocus;				// 0..1, how much an item goal outweighs an enemy goal

	// -- tactical personality --
	float			initiative;				// 0..1, willingness to take an imperfect shot instead of waiting
	float			targetStickiness;		// 0..1, reluctance to abandon the current enemy for a better one
	float			opportunism;			// 0..1, preference for finishing already-wounded enemies
	float			vengefulness;			// 0..1, preference for the opponent that last killed this bot
	float			suppressionMsec;		// keep firing this long at the last seen position after contact breaks
	float			strafeRhythmMsec;		// base interval between combat strafe direction changes
	float			strafeRhythmVarianceMsec;// random spread added to the strafe interval
	float			weaponSwitchMsec;		// interval between range/weapon-choice re-evaluations
	float			aimHeight;				// -0.4..0.4 of target height, personal vertical shot bias

	// -- decision quality --
	float			weaponSkill;			// 0..1, chance the range-correct weapon is chosen over merely the first available
	float			targetSelection;		// 0..1, chance the best enemy is chosen rather than simply the nearest
	float			hearing;				// multiplier on how far gunfire, footsteps and pickups are heard
	float			itemTiming;				// 0..1, chance a witnessed pickup's respawn is tracked, and how precisely

	// -- chat --
	float			chatiness;				// 0..1, chance a chat-worthy event actually produces a line
	float			chatDelayScale;			// multiplier on bot_chatDelay; a slower bot takes longer to "type"

	// -- weapon preference --
	// Not reachable from the trait field table: these are named entries, not
	// scalars, and they merge rather than overwrite - a character's bias for
	// one weapon leaves the rest of its style's opinions intact.
	int				numWeaponBias;
	botWeaponBias_t	weaponBias[BOT_MAX_WEAPON_BIAS];
} botTraits_t;

//----------------------------------------------------------------
// One statement out of a .style or .bot file: what to do to which trait.
//
// set replaces the skill baseline outright, add offsets it, scale multiplies
// it.  Order matters and is the order the statements appear in the file, so a
// style can set a value and a character can then scale what the style chose.
//----------------------------------------------------------------
typedef enum {
	BOTTRAITOP_SET,				// field = value
	BOTTRAITOP_ADD,				// field += value
	BOTTRAITOP_SCALE,			// field *= value
	BOTTRAITOP_NUM
} botTraitOp_t;

typedef struct botTraitMod_s {
	short			field;					// index into the static trait field table
	short			op;						// botTraitOp_t
	float			value;
} botTraitMod_t;

//----------------------------------------------------------------
// One row of the static trait field table: the word a file uses, where the
// float lives, and the range every modifier result is clamped into.  The
// clamp is what stops a typo in a character file from producing a bot that
// turns at 90000 degrees a second.
//----------------------------------------------------------------
typedef struct botTraitField_s {
	const char *	name;					// key as written in a .style / .bot file
	int				offset;					// byte offset of the float within botTraits_t
	float			min;
	float			max;
} botTraitField_t;

//----------------------------------------------------------------
// An ordered list of trait modifiers.  Styles, characters and per-skill-level
// override blocks are all just one of these.
//----------------------------------------------------------------
class rvBotTraitMods {
public:
							rvBotTraitMods( void ) { mods.SetGranularity( 8 ); }

	void					Clear( void ) { mods.Clear(); }
	int						Num( void ) const { return mods.Num(); }
	bool					IsEmpty( void ) const { return mods.Num() == 0; }

	void					Append( int field, int op, float value );

	// Reads "<field> <value>" with the op word already consumed.  An unknown
	// field warns and is skipped, never fatal - a character file is content,
	// and content must not be able to kill a server.
	bool					ParseStatement( idLexer &lexer, int op, const char *sourceName );

	// Applies every statement in order, clamping after each one.
	void					Apply( botTraits_t &traits ) const;

private:
	idList<botTraitMod_t>	mods;
};

//----------------------------------------------------------------
// The events a bot has something to say about.  Kept small and concrete:
// every one of these is a moment a human player would actually type.
//----------------------------------------------------------------
typedef enum rvBotChatEvent_e {
	BOTCHAT_ENTERGAME,			// joined the match
	BOTCHAT_LEVELSTART,			// the match went live on this map
	BOTCHAT_KILL,				// killed someone
	BOTCHAT_KILL_GAUNTLET,		// killed someone with the gauntlet - the humiliation line
	BOTCHAT_KILL_STREAK,		// killed several in a row without dying
	BOTCHAT_KILL_REVENGE,		// killed the player who killed it last
	BOTCHAT_DEATH,				// killed by an enemy
	BOTCHAT_DEATH_ACCIDENT,		// died to the map or to its own splash, with nobody to blame
	BOTCHAT_ITEM_DENIED,		// an item it was going for was taken out from under it
	BOTCHAT_LEAD_TAKEN,			// moved into first place
	BOTCHAT_LEAD_LOST,			// lost first place
	BOTCHAT_MATCH_WIN,			// the match ended and it won
	BOTCHAT_MATCH_LOSE,			// the match ended and it did not
	BOTCHAT_FAREWELL,			// leaving the server
	BOTCHAT_ORDER,				// a team-mate gave it an order in team chat; $other is who
	BOTCHAT_NUM
} rvBotChatEvent;

//----------------------------------------------------------------
// What a chat line may refer to.  Substituted for $self, $other, $weapon,
// $map and $item.  Any token with no value is replaced with nothing rather
// than left as literal text, so a line that names $item still reads sensibly
// when it fires for an event that has no item.
//----------------------------------------------------------------
typedef struct botChatTokens_s {
	idStr			self;					// the speaking bot's display name
	idStr			other;					// opponent, killer or victim
	idStr			weapon;					// readable weapon name
	idStr			map;					// current map name
	idStr			item;					// item that changed hands

	void			Clear( void ) { self.Clear(); other.Clear(); weapon.Clear(); map.Clear(); item.Clear(); }
} botChatTokens_t;

//----------------------------------------------------------------
// A conversational reply rule.  Triggers are normalized when parsed so the
// runtime only performs bounded whole-word comparisons.
//----------------------------------------------------------------
typedef enum botReplySource_e {
	BOTREPLYSOURCE_ANY,
	BOTREPLYSOURCE_PLAYER,
	BOTREPLYSOURCE_BOT
} botReplySource_t;

typedef enum botReplyAddress_e {
	BOTREPLYADDRESS_EITHER,
	BOTREPLYADDRESS_REQUIRED,
	BOTREPLYADDRESS_FORBIDDEN
} botReplyAddress_t;

typedef struct botReplyRule_s {
	idStr					name;
	int						priority;
	botReplySource_t		source;
	botReplyAddress_t		addressed;
	idStrList				triggers;
	idStrList				lines;
	mutable int				lastLine;

	void					Clear( void ) {
		name.Clear();
		priority = 0;
		source = BOTREPLYSOURCE_ANY;
		addressed = BOTREPLYADDRESS_EITHER;
		triggers.Clear();
		lines.Clear();
		lastLine = -1;
	}
} botReplyRule_t;

//----------------------------------------------------------------
// rvBotStyle
//
// A play archetype.  Biases behaviour, never quality: a skill 1 sniper and a
// skill 5 sniper both hold range and both prefer the rail gun, and only one of
// them hits with it.
//----------------------------------------------------------------
class rvBotStyle {
public:
							rvBotStyle( void );

	void					Clear( void );

	const char *			GetName( void ) const { return name.c_str(); }
	const char *			GetDescription( void ) const { return description.c_str(); }
	const char *			GetFileName( void ) const { return fileName.c_str(); }

	// Reads one "style <name> { ... }" block.  Returns false only when the
	// block is structurally unreadable; unknown keys inside it warn and are
	// skipped.
	bool					Parse( idLexer &lexer, const char *sourceName );

	// Applies the style body, then its override block for this level.
	void					Apply( botTraits_t &traits, int skillLevel ) const;

private:
	friend class			rvBotCharacterManager;

	idStr					name;
	idStr					description;
	idStr					fileName;			// where it came from, for botcharacters and for warnings

	idStr					inherit;			// another style this one starts from, resolved after every file is read
	const rvBotStyle *		parent;				// NULL when inherit is empty or unresolved

	rvBotTraitMods			mods;
	rvBotTraitMods			skillMods[BOT_SKILL_LEVELS];

	// The "weapon <class> <bias>" statements.  Not part of rvBotTraitMods
	// because a modifier is a field index and a number with nowhere to put a
	// name, and not per skill level because which weapon a bot reaches for is
	// taste, not competence - the one thing a style is forbidden to change with
	// difficulty.
	idList<botWeaponBias_t>	weaponBias;
};

//----------------------------------------------------------------
// rvBotCharacter
//
// One named opponent: who it is, how it plays, how good it is, and what it
// says.  A character with no style inherits nothing and is a pure skill curve
// with a name and a voice.
//----------------------------------------------------------------
class rvBotCharacter {
public:
							rvBotCharacter( void );

	void					Clear( void );

	const char *			GetName( void ) const { return name.c_str(); }
	const char *			GetDescription( void ) const { return description.c_str(); }
	const char *			GetFileName( void ) const { return fileName.c_str(); }
	const char *			GetStyleName( void ) const { return style ? style->GetName() : styleName.c_str(); }
	const rvBotStyle *		GetStyle( void ) const { return style; }

	int						GetMinSkill( void ) const { return minSkill; }
	int						GetMaxSkill( void ) const { return maxSkill; }
	bool					FitsSkill( int skillLevel ) const { return skillLevel >= minSkill && skillLevel <= maxSkill; }

	bool					IsInUse( void ) const { return inUse; }

	// Player model decl names.  Empty means "leave the default alone".  The
	// team variants exist because idPlayer rejects a model whose decl team
	// does not match the player's side.
	const char *			GetModel( void ) const { return model.c_str(); }
	const char *			GetModelMarine( void ) const { return modelMarine.c_str(); }
	const char *			GetModelStrogg( void ) const { return modelStrogg.c_str(); }

	bool					HasChat( rvBotChatEvent event ) const;
	int						NumChatLines( rvBotChatEvent event ) const;
	bool					HasReply( void ) const { return replies.Num() > 0; }

	// Reads one "character <name> { ... }" block.
	bool					Parse( idLexer &lexer, const char *sourceName );

	// Applies the style, then the character body, then this level's overrides.
	void					Apply( botTraits_t &traits, int skillLevel ) const;

private:
	friend class			rvBotCharacterManager;

	// Reads one "chat <event> { ... }" block from either the canonical
	// characterChat file or a legacy inline character block.  Lines are
	// validated as they are read rather than at broadcast time, because
	// ProcessChatMessage drops an over-long line with nothing but a warning and
	// the author would never find out which of their lines had gone silent.
	bool					ParseChatBlock( idLexer &lexer, const char *sourceName );
	bool					ParseReplyBlock( idLexer &lexer, const char *sourceName );

	idStr					name;				// display name; becomes ui_name
	idStr					description;
	idStr					fileName;

	idStr					styleName;
	const rvBotStyle *		style;				// resolved after every style file is read

	int						minSkill;			// lowest bot_skill this character is picked for
	int						maxSkill;			// highest

	idStr					model;
	idStr					modelMarine;
	idStr					modelStrogg;

	rvBotTraitMods			mods;
	rvBotTraitMods			skillMods[BOT_SKILL_LEVELS];

	// Merged over the style's list rather than replacing it, so a character
	// with an opinion about one weapon keeps its archetype's view of the rest.
	idList<botWeaponBias_t>	weaponBias;

	idStrList				chat[BOTCHAT_NUM];
	idList<botReplyRule_t>	replies;

	// Which line was used last, so the same one never comes out twice running.
	// Mutable because picking a line is logically a read of the character.
	mutable int				lastChat[BOTCHAT_NUM];

	bool					inUse;				// a bot currently owns this character
};

//----------------------------------------------------------------
// rvBotCharacterManager
//
// Owns the parsed styles and characters and is the only thing the bot code
// talks to.  Loaded once at game init rather than per map, because characters
// describe opponents and opponents outlive a map change - bots hold their
// client slots across one.
//----------------------------------------------------------------
class rvBotCharacterManager {
public:
							rvBotCharacterManager( void );

	// Enumerates styles, characters and their separate chat files through the
	// file system, so pk4s and mods are picked up.  Safe to call twice; the
	// previous contents are dropped first.
	void					Init( void );
	void					Shutdown( void );

	// Re-reads every file without a map change.  Bots keep their character
	// assignment by name where the name still exists, and re-resolve their
	// traits on the next frame.  Backs the botreload console command.
	bool					Reload( void );

	bool					IsLoaded( void ) const { return loaded; }
	int						NumCharacters( void ) const { return characters.Num(); }
	int						NumStyles( void ) const { return styles.Num(); }

	const rvBotCharacter *	GetCharacter( int index ) const { return characters[index]; }
	const rvBotStyle *		GetStyle( int index ) const { return styles[index]; }

	// Case insensitive.  NULL when there is no such name.
	const rvBotCharacter *	FindCharacter( const char *name ) const;
	const rvBotStyle *		FindStyle( const char *name ) const;

	// Picks a character that no bot is currently using and whose skill band
	// contains skillLevel, honouring bot_forceCharacter.  Falls back to the
	// band, then to any character at all, before returning NULL - a full
	// roster must never stop a bot being added.  markUsed false is a peek.
	const rvBotCharacter *	PickCharacter( int skillLevel, bool markUsed = true );

	// Claims a character the caller already has in hand, for the one case that
	// is not a fresh pick: re-binding a live bot to its own character by name
	// after a reload has emptied every in-use flag.
	void					MarkCharacterUsed( const rvBotCharacter *character );

	void					ReleaseCharacter( const rvBotCharacter *character );
	void					ReleaseAllCharacters( void );

	// The whole point.  Produces the flat per-bot personality:
	//   baseline( EffectiveSkill( skillLevel, variance, seed ) )
	//   -> style body -> style skill overrides
	//   -> character body -> character skill overrides
	// character may be NULL (bot_characters 0), which yields the pure curve.
	// seed is the bot's client number, so the variance roll is stable for the
	// life of that bot instead of re-rolling on every respawn.
	void					ResolveTraits( const rvBotCharacter *character, int skillLevel, float variance, int seed, botTraits_t &traits ) const;

	// The fractional skill level a bot actually plays at.  Deterministic in
	// seed so botlist reports the same number the bot is using.
	static float			EffectiveSkill( int skillLevel, float variance, int seed );

	// The layer 1 curve, evaluated at a fractional level and clamped to 1..5.
	static void				BaselineTraits( float skillLevel, botTraits_t &traits );
	static void				ClampTraits( botTraits_t &traits );

	// Trait field table, shared by the parser and by any diagnostic that wants
	// to print traits by name.
	static int				NumTraitFields( void );
	static const botTraitField_t *	GetTraitField( int index );
	static int				FindTraitField( const char *name );			// -1 when unknown
	static float *			TraitFieldPtr( botTraits_t &traits, int field );

	// Weapon preference lookup against the resolved traits.  Returns 1.0 for
	// any weapon the personality says nothing about.
	static float			WeaponBias( const botTraits_t &traits, const char *weaponName );

	// Chooses a line for an event and substitutes the tokens into it.  False
	// when the character has nothing to say for that event, which is normal -
	// silence is a valid answer and callers must not invent a fallback line.
	bool					ChatLine( const rvBotCharacter *character, rvBotChatEvent event, const botChatTokens_t &tokens, idStr &out ) const;

	// Matches content-defined conversational replies.  Text and character
	// names use the same normalizer so punctuation, case and colour escapes
	// cannot turn substring matches into accidental replies.
	bool					HasReply( const rvBotCharacter *character, const idStr &normalizedText, bool sourceIsBot, bool addressed ) const;
	bool					ReplyLine( const rvBotCharacter *character, const idStr &normalizedText, bool sourceIsBot, bool addressed, const botChatTokens_t &tokens, idStr &out ) const;
	static void				NormalizeReplyText( const char *text, idStr &out );
	static bool				ReplyPhraseMatches( const idStr &normalizedText, const idStr &normalizedPhrase );
	static bool				ReplyNameMatches( const char *name, const idStr &normalizedText );

	static const char *		ChatEventName( rvBotChatEvent event );
	static rvBotChatEvent	ChatEventForName( const char *name );		// BOTCHAT_NUM when unknown

	// Flood control.  Chat has no rate limit anywhere in the engine, and a
	// client whose reliable queue overflows is dropped, so unbounded bot
	// chatter can kick real players off a server.  Returns false when this bot
	// or the server as a whole has spoken too recently; stamps both throttles
	// when it returns true.
	bool					AllowChat( int clientNum );
	void					ResetChatThrottle( void );

	// Move the throttle stamps forward by one frozen competitive-pause frame.
	void					ShiftChatThrottle( int deltaMsec );

	// Console command backings: botcharacters and botreload.
	void					ListCharacters( void ) const;

private:
	bool					LoadStyleFile( const char *fileName );
	bool					LoadCharacterFile( const char *fileName );
	bool					LoadChatFile( const char *fileName );
	void					ResolveInheritance( void );
	void					FreeAll( void );

	idList<rvBotStyle *>		styles;
	idList<rvBotCharacter *>	characters;

	bool					loaded;

	int						nextGlobalChatTime;
	int						nextClientChatTime[MAX_CLIENTS];
};

extern rvBotCharacterManager	botCharacterManager;

#endif /* !__GAME_MP_BOTCHARACTER_H__ */
