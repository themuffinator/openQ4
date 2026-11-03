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



#include "../framework/Session_local.h"
#include "../imagetools/ImageTools.h"

#include "DeviceContext.h"
#include "Window.h"
#include "UserInterfaceLocal.h"
#include "GameBustOutWindow.h"

#define BALL_RADIUS		12.f
#define BALL_SPEED		250.f
#define BALL_MAXSPEED	450.f

#define S_UNIQUE_CHANNEL	6

static const int GAME_BUSTOUT_MAX_SAVE_ENTITIES = 512;
static const int GAME_BUSTOUT_SAVE_MAGIC = 'O' | ( 'Q' << 8 ) | ( 'B' << 16 ) | ( 'O' << 24 );
static const int GAME_BUSTOUT_SAVE_VERSION = 1;

static void GameBustOut_WriteSaveInt( idFile *savefile, const int value ) {
	savefile->WriteInt( value );
}

static int GameBustOut_ReadLegacySaveInt( idFile *savefile, const char *fieldName ) {
	int value = 0;
	const int offset = savefile->Tell();
	const int bytesRead = savefile->Read( &value, sizeof( value ) );
	if ( bytesRead != sizeof( value ) ) {
		common->Error( "idGameBustOutWindow::ReadFromSaveGame: truncated %s at offset %d (read %d of %d)",
			fieldName ? fieldName : "integer", offset, bytesRead, static_cast<int>( sizeof( value ) ) );
	}
	return value;
}

static int GameBustOut_ReadEndianSaveInt( idFile *savefile, const char *fieldName ) {
	int value = 0;
	const int offset = savefile->Tell();
	const int bytesRead = savefile->ReadInt( value );
	if ( bytesRead != sizeof( value ) ) {
		common->Error( "idGameBustOutWindow::ReadFromSaveGame: truncated %s at offset %d (read %d of %d)",
			fieldName ? fieldName : "integer", offset, bytesRead, static_cast<int>( sizeof( value ) ) );
	}
	return value;
}

static int GameBustOut_ReadSaveInt( idFile *savefile, const char *fieldName, int saveVersion ) {
	if ( saveVersion >= GAME_BUSTOUT_SAVE_VERSION ) {
		return GameBustOut_ReadEndianSaveInt( savefile, fieldName );
	}
	return GameBustOut_ReadLegacySaveInt( savefile, fieldName );
}

static int GameBustOut_ReadSaveVersion( idFile *savefile ) {
	const int markerOffset = savefile->Tell();
	const int marker = GameBustOut_ReadEndianSaveInt( savefile, "format marker" );
	if ( marker != GAME_BUSTOUT_SAVE_MAGIC ) {
		savefile->Seek( markerOffset, FS_SEEK_SET );
		return 0;
	}

	const int version = GameBustOut_ReadEndianSaveInt( savefile, "format version" );
	if ( version <= 0 || version > GAME_BUSTOUT_SAVE_VERSION ) {
		common->Error( "idGameBustOutWindow::ReadFromSaveGame: unsupported format version %d", version );
	}
	return version;
}

static void GameBustOut_ValidateSaveCount( const char *fieldName, int count ) {
	if ( count < 0 || count > GAME_BUSTOUT_MAX_SAVE_ENTITIES ) {
		common->Error( "idGameBustOutWindow::ReadFromSaveGame: invalid %s %d",
			fieldName ? fieldName : "count", count );
	}
}

static void GameBustOut_ValidateEntityIndex( const char *fieldName, int index, int entityCount ) {
	if ( index < 0 || index >= entityCount ) {
		common->Error( "idGameBustOutWindow::ReadFromSaveGame: invalid %s %d (entity count %d)",
			fieldName ? fieldName : "entity index", index, entityCount );
	}
}

// Raw copies of these fields wrote exactly the bytes the typed GUI encoders
// write on every supported target (roadmap Q4-06).
template< class type >
static void GameBustOut_ReadSaveField( idFile *savefile, type &value, const char *fieldName ) {
	OpenQ4_ReadSaveGameField( savefile, value, "idGameBustOutWindow::ReadFromSaveGame", fieldName );
}

template< class type >
static void GameBustOut_WriteSaveField( idFile *savefile, const type &value, const char *fieldName ) {
	OpenQ4_WriteSaveGameField( savefile, value, "idGameBustOutWindow::WriteToSaveGame", fieldName );
}

static powerupType_t GameBustOut_ReadSavePowerup( idFile *savefile, const char *fieldName, int saveVersion ) {
	const int value = GameBustOut_ReadSaveInt( savefile, fieldName, saveVersion );
	if ( value < POWERUP_NONE || value > POWERUP_MULTIBALL ) {
		common->Error( "idGameBustOutWindow::ReadFromSaveGame: invalid %s %d",
			fieldName ? fieldName : "powerup", value );
	}
	return static_cast<powerupType_t>( value );
}

/*
*****************************************************************************
* BOEntity	
****************************************************************************
*/
BOEntity::BOEntity(idGameBustOutWindow* _game) {
	game = _game;
	visible = true;

	materialName = "";
	material = NULL;
	width = height = 8;
	color = colorWhite;
	powerup = POWERUP_NONE;

	position.Zero();
	velocity.Zero();

	removed = false;
	fadeOut = 0;
}

BOEntity::~BOEntity() {
}

/*
======================
BOEntity::WriteToSaveGame
======================
*/
void BOEntity::WriteToSaveGame( idFile *savefile ) {

	GameBustOut_WriteSaveField( savefile, visible, "visible" );

	game->WriteSaveGameString( materialName, savefile );

	GameBustOut_WriteSaveField( savefile, width, "width" );
	GameBustOut_WriteSaveField( savefile, height, "height" );

	GameBustOut_WriteSaveField( savefile, color, "color" );
	GameBustOut_WriteSaveField( savefile, position, "position" );
	GameBustOut_WriteSaveField( savefile, velocity, "velocity" );

	GameBustOut_WriteSaveInt( savefile, static_cast<int>( powerup ) );
	GameBustOut_WriteSaveField( savefile, removed, "removed" );
	GameBustOut_WriteSaveField( savefile, fadeOut, "fadeOut" );
}

/*
======================
BOEntity::ReadFromSaveGame
======================
*/
void BOEntity::ReadFromSaveGame( idFile *savefile, idGameBustOutWindow* _game, int saveVersion ) {
	game = _game;

	GameBustOut_ReadSaveField( savefile, visible, "entity visible flag" );

	game->ReadSaveGameString( materialName, savefile );
	SetMaterial( materialName );

	GameBustOut_ReadSaveField( savefile, width, "entity width" );
	GameBustOut_ReadSaveField( savefile, height, "entity height" );

	GameBustOut_ReadSaveField( savefile, color, "entity color" );
	GameBustOut_ReadSaveField( savefile, position, "entity position" );
	GameBustOut_ReadSaveField( savefile, velocity, "entity velocity" );

	powerup = GameBustOut_ReadSavePowerup( savefile, "entity powerup", saveVersion );
	GameBustOut_ReadSaveField( savefile, removed, "entity removed flag" );
	GameBustOut_ReadSaveField( savefile, fadeOut, "entity fade-out flag" );
}

/*
======================
BOEntity::SetMaterial
======================
*/
void BOEntity::SetMaterial(const char* name) {
	materialName = name;
	material = declManager->FindMaterial( name );
	material->SetSort( SS_GUI );
}

/*
======================
BOEntity::SetSize
======================
*/
void BOEntity::SetSize( float _width, float _height ) {
	width = _width;
	height = _height;
}

/*
======================
BOEntity::SetVisible
======================
*/
void BOEntity::SetColor( float r, float g, float b, float a ) {
	color.x = r;
	color.y = g;
	color.z = b;
	color.w = a;
}

/*
======================
BOEntity::SetVisible
======================
*/
void BOEntity::SetVisible( bool isVisible ) {
	visible = isVisible;
}

/*
======================
BOEntity::Update
======================
*/
void BOEntity::Update( float timeslice, int guiTime ) {
	
	if ( !visible ) {
		return;
	}

	// Move the entity
	position += velocity * timeslice;

	// Fade out the ent
	if ( fadeOut ) {
		color.w -= timeslice * 2.5;

		if ( color.w <= 0.f ) {
			color.w = 0.f;
			removed = true;
		}
	}
}

/*
======================
BOEntity::Draw
======================
*/
void BOEntity::Draw(idDeviceContext *dc) {
	if ( visible ) {
		dc->DrawMaterialRotated( position.x, position.y, width, height, material, color, 1.0f, 1.0f, DEG2RAD(0.f) );
	}
}

/*
*****************************************************************************
* BOBrick
****************************************************************************
*/
BOBrick::BOBrick( void ) {
	ent = NULL;
	x = y = width = height = 0;
	powerup = POWERUP_NONE;
	isBroken = false;
}

BOBrick::BOBrick( BOEntity *_ent, float _x, float _y, float _width, float _height ) {
	ent = _ent;
	x = _x;
	y = _y;
	width = _width;
	height = _height;
	powerup = POWERUP_NONE;

	isBroken = false;

	ent->position.x = x;
	ent->position.y = y;
	ent->SetSize( width, height );
	ent->SetMaterial( "game/bustout/brick" );

	ent->game->entities.Append( ent );
}

BOBrick::~BOBrick( void ) {
}

/*
======================
BOBrick::WriteToSaveGame
======================
*/
void BOBrick::WriteToSaveGame( idFile *savefile ) {
	GameBustOut_WriteSaveField( savefile, x, "x" );
	GameBustOut_WriteSaveField( savefile, y, "y" );
	GameBustOut_WriteSaveField( savefile, width, "width" );
	GameBustOut_WriteSaveField( savefile, height, "height" );

	GameBustOut_WriteSaveInt( savefile, static_cast<int>( powerup ) );
	GameBustOut_WriteSaveField( savefile, isBroken, "isBroken" );

	int index = ent->game->entities.FindIndex( ent );
	GameBustOut_WriteSaveInt( savefile, index );
}

/*
======================
BOBrick::ReadFromSaveGame
======================
*/
void BOBrick::ReadFromSaveGame( idFile *savefile, idGameBustOutWindow *game, int saveVersion ) {
	GameBustOut_ReadSaveField( savefile, x, "brick x" );
	GameBustOut_ReadSaveField( savefile, y, "brick y" );
	GameBustOut_ReadSaveField( savefile, width, "brick width" );
	GameBustOut_ReadSaveField( savefile, height, "brick height" );

	powerup = GameBustOut_ReadSavePowerup( savefile, "brick powerup", saveVersion );
	GameBustOut_ReadSaveField( savefile, isBroken, "brick broken flag" );

	int index;
	index = GameBustOut_ReadSaveInt( savefile, "brick entity index", saveVersion );
	GameBustOut_ValidateEntityIndex( "brick entity index", index, game->entities.Num() );
	ent = game->entities[index];
}

/*
======================
BOBrick::SetColor
======================
*/
void BOBrick::SetColor( idVec4 bcolor ) {
	ent->SetColor( bcolor.x, bcolor.y, bcolor.z, bcolor.w );
}

/*
======================
BOBrick::checkCollision
======================
*/
collideDir_t BOBrick::checkCollision( idVec2 pos, idVec2 vel ) {
	idVec2	ptA, ptB;
	float	dist;

	collideDir_t	result = COLLIDE_NONE;

	if ( isBroken ) {
		return result;
	}

	// Check for collision with each edge
	idVec2 vec;

	// Bottom
	ptA.x = x;
	ptA.y = y + height;

	ptB.x = x + width;
	ptB.y = y + height;

	if ( vel.y < 0 && pos.y > ptA.y ) {
		if( pos.x > ptA.x && pos.x < ptB.x ) {
			dist = pos.y - ptA.y;

			if ( dist < BALL_RADIUS ) {
				result = COLLIDE_DOWN;
			}
		} else {
			if ( pos.x <= ptA.x ) {
				vec = pos - ptA;
			} else {
				vec = pos - ptB;
			}

			if ( (idMath::Fabs(vec.y) > idMath::Fabs(vec.x)) && (vec.LengthFast() < BALL_RADIUS) ) {
				result = COLLIDE_DOWN;
			}
		}
	}

	if ( result == COLLIDE_NONE ) {
		// Top
		ptA.y = y;
		ptB.y = y;

		if ( vel.y > 0 && pos.y < ptA.y ) {
			if( pos.x > ptA.x && pos.x < ptB.x ) {
				dist = ptA.y - pos.y;

				if ( dist < BALL_RADIUS ) {
					result = COLLIDE_UP;
				}
			} else {
				if ( pos.x <= ptA.x ) {
					vec = pos - ptA;
				} else {
					vec = pos - ptB;
				}

				if ( (idMath::Fabs(vec.y) > idMath::Fabs(vec.x)) && (vec.LengthFast() < BALL_RADIUS) ) {
					result = COLLIDE_UP;
				}
			}
		}

		if ( result == COLLIDE_NONE ) {
			// Left side
			ptA.x = x;
			ptA.y = y;

			ptB.x = x;
			ptB.y = y + height;

			if ( vel.x > 0 && pos.x < ptA.x ) {
				if( pos.y > ptA.y && pos.y < ptB.y ) {
					dist = ptA.x - pos.x;

					if ( dist < BALL_RADIUS ) {
						result = COLLIDE_LEFT;
					}
				} else {
					if ( pos.y <= ptA.y ) {
						vec = pos - ptA;
					} else {
						vec = pos - ptB;
					}

					if ( (idMath::Fabs(vec.x) >= idMath::Fabs(vec.y)) && (vec.LengthFast() < BALL_RADIUS) ) {
						result = COLLIDE_LEFT;
					}
				}
			}

			if ( result == COLLIDE_NONE ) {
				// Right side
				ptA.x = x + width;
				ptB.x = x + width;

				if ( vel.x < 0 && pos.x > ptA.x ) {
					if( pos.y > ptA.y && pos.y < ptB.y ) {
						dist = pos.x - ptA.x;

						if ( dist < BALL_RADIUS ) {
							result = COLLIDE_LEFT;
						}
					} else {
						if ( pos.y <= ptA.y ) {
							vec = pos - ptA;
						} else {
							vec = pos - ptB;
						}

						if ( (idMath::Fabs(vec.x) >= idMath::Fabs(vec.y)) && (vec.LengthFast() < BALL_RADIUS) ) {
							result = COLLIDE_LEFT;
						}
					}
				}

			}
		}
	}

	return result;
}

/*
*****************************************************************************
* idGameBustOutWindow
****************************************************************************
*/
idGameBustOutWindow::idGameBustOutWindow(idDeviceContext *d, idUserInterfaceLocal *g) : idWindow(d, g) {
	dc = d;
	gui = g;
	CommonInit();
}

idGameBustOutWindow::idGameBustOutWindow(idUserInterfaceLocal *g) : idWindow(g) {
	gui = g;
	CommonInit();
}

idGameBustOutWindow::~idGameBustOutWindow() {
	entities.DeleteContents(true);

	Mem_Free( levelBoardData );
}

/*
=============================
idGameBustOutWindow::WriteToSaveGame
=============================
*/
void idGameBustOutWindow::WriteToSaveGame( idFile *savefile ) {
	idWindow::WriteToSaveGame( savefile );

	gamerunning.WriteToSaveGame( savefile );
	onFire.WriteToSaveGame( savefile );
	onContinue.WriteToSaveGame( savefile );
	onNewGame.WriteToSaveGame( savefile );
	onNewLevel.WriteToSaveGame( savefile );

	GameBustOut_WriteSaveInt( savefile, GAME_BUSTOUT_SAVE_MAGIC );
	GameBustOut_WriteSaveInt( savefile, GAME_BUSTOUT_SAVE_VERSION );

	GameBustOut_WriteSaveField( savefile, timeSlice, "timeSlice" );
	GameBustOut_WriteSaveField( savefile, gameOver, "gameOver" );
	GameBustOut_WriteSaveInt( savefile, numLevels );

	// Board Data is loaded when GUI is loaded, don't need to save

	GameBustOut_WriteSaveInt( savefile, numBricks );
	GameBustOut_WriteSaveInt( savefile, currentLevel );

	GameBustOut_WriteSaveField( savefile, updateScore, "updateScore" );
	GameBustOut_WriteSaveInt( savefile, gameScore );
	GameBustOut_WriteSaveInt( savefile, nextBallScore );

	GameBustOut_WriteSaveInt( savefile, bigPaddleTime );
	GameBustOut_WriteSaveField( savefile, paddleVelocity, "paddleVelocity" );

	GameBustOut_WriteSaveField( savefile, ballSpeed, "ballSpeed" );
	GameBustOut_WriteSaveInt( savefile, ballsRemaining );
	GameBustOut_WriteSaveInt( savefile, ballsInPlay );
	GameBustOut_WriteSaveField( savefile, ballHitCeiling, "ballHitCeiling" );

	// Write Entities
	int i;
	int numberOfEnts = entities.Num();
	GameBustOut_WriteSaveInt( savefile, numberOfEnts );
	for ( i=0; i<numberOfEnts; i++ ) {
		entities[i]->WriteToSaveGame( savefile );
	}

	// Write Balls
	numberOfEnts = balls.Num();
	GameBustOut_WriteSaveInt( savefile, numberOfEnts );
	for ( i=0; i<numberOfEnts; i++ ) {
		int ballIndex = entities.FindIndex( balls[i] );
		GameBustOut_WriteSaveInt( savefile, ballIndex );
	}

	// Write Powerups
	numberOfEnts = powerUps.Num();
	GameBustOut_WriteSaveInt( savefile, numberOfEnts );
	for ( i=0; i<numberOfEnts; i++ ) {
		int powerIndex = entities.FindIndex( powerUps[i] );
		GameBustOut_WriteSaveInt( savefile, powerIndex );
	}

	// Write paddle
	paddle->WriteToSaveGame( savefile );

	// Write Bricks
	int row;
	for ( row=0; row<BOARD_ROWS; row++ ) {
		numberOfEnts = board[row].Num();
		GameBustOut_WriteSaveInt( savefile, numberOfEnts );
		for ( i=0; i<numberOfEnts; i++ ) {
			board[row][i]->WriteToSaveGame( savefile );
		}
	}
}

/*
=============================
idGameBustOutWindow::ReadFromSaveGame
=============================
*/
void idGameBustOutWindow::ReadFromSaveGame( idFile *savefile ) {
	idWindow::ReadFromSaveGame( savefile );
	int i;

	// Clear out existing paddle and entities from GUI load
	delete paddle;
	paddle = NULL;
	balls.Clear();
	powerUps.Clear();
	for ( i = 0; i < BOARD_ROWS; i++ ) {
		board[i].DeleteContents( true );
	}
	entities.DeleteContents( true );

	gamerunning.ReadFromSaveGame( savefile );
	onFire.ReadFromSaveGame( savefile );
	onContinue.ReadFromSaveGame( savefile );
	onNewGame.ReadFromSaveGame( savefile );
	onNewLevel.ReadFromSaveGame( savefile );

	const int saveVersion = GameBustOut_ReadSaveVersion( savefile );

	GameBustOut_ReadSaveField( savefile, timeSlice, "time slice" );
	GameBustOut_ReadSaveField( savefile, gameOver, "game-over flag" );
	numLevels = GameBustOut_ReadSaveInt( savefile, "level count", saveVersion );

	// Board Data is loaded when GUI is loaded, don't need to save

	numBricks = GameBustOut_ReadSaveInt( savefile, "brick count", saveVersion );
	currentLevel = GameBustOut_ReadSaveInt( savefile, "current level", saveVersion );

	GameBustOut_ReadSaveField( savefile, updateScore, "update-score flag" );
	gameScore = GameBustOut_ReadSaveInt( savefile, "score", saveVersion );
	nextBallScore = GameBustOut_ReadSaveInt( savefile, "next ball score", saveVersion );

	bigPaddleTime = GameBustOut_ReadSaveInt( savefile, "big paddle time", saveVersion );
	GameBustOut_ReadSaveField( savefile, paddleVelocity, "paddle velocity" );

	GameBustOut_ReadSaveField( savefile, ballSpeed, "ball speed" );
	ballsRemaining = GameBustOut_ReadSaveInt( savefile, "balls remaining", saveVersion );
	ballsInPlay = GameBustOut_ReadSaveInt( savefile, "balls in play", saveVersion );
	GameBustOut_ReadSaveField( savefile, ballHitCeiling, "ball-hit-ceiling flag" );

	int numberOfEnts;

	// Read entities
	numberOfEnts = GameBustOut_ReadSaveInt( savefile, "entity count", saveVersion );
	GameBustOut_ValidateSaveCount( "entity count", numberOfEnts );
	for ( i=0; i<numberOfEnts; i++ ) {
		BOEntity *ent;

		ent = new BOEntity( this );
		ent->ReadFromSaveGame( savefile, this, saveVersion );
		entities.Append( ent );
	}

	// Read balls
	numberOfEnts = GameBustOut_ReadSaveInt( savefile, "ball count", saveVersion );
	GameBustOut_ValidateSaveCount( "ball count", numberOfEnts );
	for ( i=0; i<numberOfEnts; i++ ) {
		int ballIndex;
		ballIndex = GameBustOut_ReadSaveInt( savefile, "ball entity index", saveVersion );
		GameBustOut_ValidateEntityIndex( "ball entity index", ballIndex, entities.Num() );
		balls.Append( entities[ballIndex] );
	}

	// Read powerups
	numberOfEnts = GameBustOut_ReadSaveInt( savefile, "powerup count", saveVersion );
	GameBustOut_ValidateSaveCount( "powerup count", numberOfEnts );
	for ( i=0; i<numberOfEnts; i++ ) {
		int powerIndex;
		powerIndex = GameBustOut_ReadSaveInt( savefile, "powerup entity index", saveVersion );
		GameBustOut_ValidateEntityIndex( "powerup entity index", powerIndex, entities.Num() );
		powerUps.Append( entities[powerIndex] );
	}

	// Read paddle
	paddle = new BOBrick();
	paddle->ReadFromSaveGame( savefile, this, saveVersion );

	// Read board
	int row;
	for ( row=0; row<BOARD_ROWS; row++ ) {
		numberOfEnts = GameBustOut_ReadSaveInt( savefile, "board row brick count", saveVersion );
		GameBustOut_ValidateSaveCount( "board row brick count", numberOfEnts );
		for ( i=0; i<numberOfEnts; i++ ) {
			BOBrick *brick = new BOBrick();
			brick->ReadFromSaveGame( savefile, this, saveVersion );
			board[row].Append( brick );
		}
	}
}

/*
=============================
idGameBustOutWindow::ResetGameState
=============================
*/
void idGameBustOutWindow::ResetGameState() {
	gamerunning = false;
	gameOver = false;
	onFire = false;
	onContinue = false;
	onNewGame = false;
	onNewLevel = false;

	// Keep the mini-game aligned to an exact 60 Hz step.
	timeSlice = 1.0f / 60.0f;
	ballsRemaining = 3;
	ballSpeed = BALL_SPEED;
	ballsInPlay = 0;
	updateScore = false;
	numBricks = 0;
	currentLevel = 1;
	gameScore = 0;
	bigPaddleTime = 0;
	nextBallScore = gameScore + 10000;

	ClearBoard();
}

/*
=============================
idGameBustOutWindow::CommonInit
=============================
*/
void idGameBustOutWindow::CommonInit() {
	BOEntity *ent;

	// Precache images
	declManager->FindMaterial( "game/bustout/ball" );
	declManager->FindMaterial( "game/bustout/doublepaddle" );
	declManager->FindMaterial( "game/bustout/powerup_bigpaddle" );
	declManager->FindMaterial( "game/bustout/powerup_multiball" );
	declManager->FindMaterial( "game/bustout/brick" );

	// Precache sounds
	declManager->FindSound( "arcade_ballbounce" );
	declManager->FindSound( "arcade_brickhit" );
	declManager->FindSound( "arcade_missedball" );
	declManager->FindSound( "arcade_sadsound" );
	declManager->FindSound( "arcade_extraball" );
	declManager->FindSound( "arcade_powerup" );

	ResetGameState();

	numLevels = 0;
	boardDataLoaded = false;
	levelBoardData = NULL;

	// Create Paddle
	ent = new BOEntity( this );
	paddle = new BOBrick( ent, 260.f, 440.f, 96.f, 24.f );
	paddle->ent->SetMaterial( "game/bustout/paddle" );
}

/*
=============================
idGameBustOutWindow::HandleEvent
=============================
*/
const char *idGameBustOutWindow::HandleEvent(const sysEvent_t *event, bool *updateVisuals) {
	int key = event->evValue;

	// need to call this to allow proper focus and capturing on embedded children
	const char *ret = idWindow::HandleEvent(event, updateVisuals);

	if ( event->evType == SE_KEY ) {

		if ( !event->evValue2 ) {
			return ret;
		}
		if ( key == K_MOUSE1) {
			// Mouse was clicked
			if ( ballsInPlay == 0 ) {
				BOEntity *ball = CreateNewBall();

				ball->SetVisible( true );
				ball->position.x = paddle->ent->position.x + 48.f;
				ball->position.y = 430.f;

				ball->velocity.x = ballSpeed;
				ball->velocity.y = -ballSpeed*2.f;
				ball->velocity.NormalizeFast();
				ball->velocity *= ballSpeed;
			}
		} else {
			return ret;
		}
	}

	return ret;
}

/*
=============================
idGameBustOutWindow::ParseInternalVar
=============================
*/
bool idGameBustOutWindow::ParseInternalVar(const char *_name, idParser *src) {
	if ( idStr::Icmp(_name, "gamerunning") == 0 ) {
		gamerunning = src->ParseBool();
		return true;
	}
	if ( idStr::Icmp(_name, "onFire") == 0 ) {
		onFire = src->ParseBool();
		return true;
	}
	if ( idStr::Icmp(_name, "onContinue") == 0 ) {
		onContinue = src->ParseBool();
		return true;
	}
	if ( idStr::Icmp(_name, "onNewGame") == 0 ) {
		onNewGame = src->ParseBool();
		return true;
	}
	if ( idStr::Icmp(_name, "onNewLevel") == 0 ) {
		onNewLevel = src->ParseBool();
		return true;
	}
	if ( idStr::Icmp(_name, "numLevels") == 0 ) {
		numLevels = src->ParseInt();

		// Load all the level images
		LoadBoardFiles();
		return true;
	}

	return idWindow::ParseInternalVar(_name, src);
}

/*
=============================
idGameBustOutWindow::GetWinVarByName
=============================
*/
idWinVar *idGameBustOutWindow::GetWinVarByName(const char *_name, bool winLookup, drawWin_t** owner) {
	idWinVar *retVar = NULL;

	if ( idStr::Icmp(_name, "gamerunning") == 0 ) {
		retVar = &gamerunning;
	} else 	if ( idStr::Icmp(_name, "onFire") == 0 ) {
		retVar = &onFire;
	} else 	if ( idStr::Icmp(_name, "onContinue") == 0 ) {
		retVar = &onContinue;
	} else 	if ( idStr::Icmp(_name, "onNewGame") == 0 ) {
		retVar = &onNewGame;
	} else 	if ( idStr::Icmp(_name, "onNewLevel") == 0 ) {
		retVar = &onNewLevel;
	}

	if(retVar) {
		return retVar;
	}

	return idWindow::GetWinVarByName(_name, winLookup, owner);
}

/*
=============================
idGameBustOutWindow::PostParse
=============================
*/
void idGameBustOutWindow::PostParse() {
	idWindow::PostParse();
}

/*
=============================
idGameBustOutWindow::Draw
=============================
*/
void idGameBustOutWindow::Draw(int time, float x, float y) {
	int i;

	//Update the game every frame before drawing
	UpdateGame();

	for( i = entities.Num()-1; i >= 0; i-- ) {
		entities[i]->Draw(dc);
	}
}

/*
=============================
idGameBustOutWindow::Activate
=============================
*/
const char *idGameBustOutWindow::Activate(bool activate) {
	return "";
}


/*
=============================
idGameBustOutWindow::UpdateScore
=============================
*/
void idGameBustOutWindow::UpdateScore() {

	if ( gameOver ) {
		gui->HandleNamedEvent( "GameOver" );
		return;
	}

	// Check for level progression
	if ( numBricks == 0 ) {
		ClearBalls();

		gui->HandleNamedEvent( "levelComplete" );
	}

	// Check for new ball score
	if ( gameScore >= nextBallScore ) {
		ballsRemaining++;
		gui->HandleNamedEvent( "extraBall" );

		// Play sound
		session->sw->PlayShaderDirectly( "arcade_extraball", S_UNIQUE_CHANNEL );

		nextBallScore = gameScore + 10000;
	}

	gui->SetStateString( "player_score", va("%i", gameScore ) );
	gui->SetStateString( "balls_remaining", va("%i", ballsRemaining ) );
	gui->SetStateString( "current_level", va("%i", currentLevel ) );
	gui->SetStateString( "next_ball_score", va("%i", nextBallScore ) );
}

/*
=============================
idGameBustOutWindow::ClearBoard
=============================
*/
void idGameBustOutWindow::ClearBoard( void ) {
	int i,j;

	ClearPowerups();

	ballHitCeiling = false;

	for ( i=0; i<BOARD_ROWS; i++ ) {
		for ( j=0; j<board[i].Num(); j++ ) {

			BOBrick *brick = board[i][j];
			brick->ent->removed = true;
		}

		board[i].DeleteContents( true );
	}
}

/*
=============================
idGameBustOutWindow::ClearPowerups
=============================
*/
void idGameBustOutWindow::ClearPowerups( void ) {
	while ( powerUps.Num() ) {
		powerUps[0]->removed = true;
		powerUps.RemoveIndex( 0 );
	}
}

/*
=============================
idGameBustOutWindow::ClearBalls
=============================
*/
void idGameBustOutWindow::ClearBalls( void ) {
	while ( balls.Num() ) {
		balls[0]->removed = true;
		balls.RemoveIndex( 0 );
	}

	ballsInPlay = 0;
}

/*
=============================
idGameBustOutWindow::LoadBoardFiles
=============================
*/
void idGameBustOutWindow::LoadBoardFiles( void ) {
	int i;
	int w,h;
	ID_TIME_T time;
	int boardSize;
	byte *currentBoard;

	if ( boardDataLoaded ) {
		return;
	}

	boardSize = 9 * 12 * 4;
	if ( numLevels <= 0 || numLevels > idMath::INT_MAX / boardSize ) {
		common->Warning( "Hell Bust-Out has invalid level count %d", numLevels );
		return;
	}
	levelBoardData = (byte*)Mem_Alloc( boardSize * numLevels );
	memset( levelBoardData, 0, boardSize * numLevels );

	currentBoard = levelBoardData;

	for ( i=0; i<numLevels; i++ ) {
		byte *pic;
		idStr	name = "guis/assets/bustout/level";
		name += (i+1);
		name += ".tga";

		R_LoadImage( name, &pic, &w, &h, &time, false );

		if ( pic != NULL ) {
			if ( w != 9 || h != 12 ) {
				common->DWarning( "Hell Bust-Out level image not correct dimensions! (%d x %d)", w, h );
				Mem_Free(pic);
				currentBoard += boardSize;
				continue;
			}

			memcpy( currentBoard, pic, boardSize );
			Mem_Free(pic);
		}

		currentBoard += boardSize;
	}

	boardDataLoaded = true;
}

/*
=============================
idGameBustOutWindow::SetCurrentBoard
=============================
*/
void idGameBustOutWindow::SetCurrentBoard( void ) {
	int i,j;
	int realLevel = ((currentLevel-1) % numLevels);
	int boardSize;
	byte *currentBoard;
	float	bx = 11.f;
	float	by = 24.f;
	float	stepx = 619.f / 9.f;
	float	stepy = ( 256 / 12.f );

	boardSize = 9 * 12 * 4;
	currentBoard = levelBoardData + ( realLevel * boardSize );

	for ( j=0; j<BOARD_ROWS; j++ ) {
		bx = 11.f;

		for ( i=0; i<9; i++ ) {
			int pixelindex = (j*9*4) + (i*4);

			if ( currentBoard[pixelindex + 3] ) {
				idVec4 bcolor;
				float pType = 0.f;

				BOEntity *bent = new BOEntity( this );
				BOBrick *brick = new BOBrick( bent, bx, by, stepx, stepy );

				bcolor.x = currentBoard[pixelindex + 0] / 255.f;
				bcolor.y = currentBoard[pixelindex + 1] / 255.f;
				bcolor.z = currentBoard[pixelindex + 2] / 255.f;
				bcolor.w = 1.f;
				brick->SetColor( bcolor );

				pType = currentBoard[pixelindex + 3] / 255.f;
				if ( pType > 0.f && pType < 1.f ) {
					if ( pType < 0.5f ) {
						brick->powerup = POWERUP_BIGPADDLE;
					} else {
						brick->powerup = POWERUP_MULTIBALL;
					}
				}

				board[j].Append( brick );
				numBricks++;
			}

			bx += stepx;
		}

		by += stepy;
	}
}

/*
=============================
idGameBustOutWindow::CreateNewBall
=============================
*/
BOEntity * idGameBustOutWindow::CreateNewBall( void ) {
	BOEntity *ball;

	ball = new BOEntity( this );
	ball->position.x = 300.f;
	ball->position.y = 416.f;
	ball->SetMaterial( "game/bustout/ball" );
	ball->SetSize( BALL_RADIUS*2.f, BALL_RADIUS*2.f );
	ball->SetVisible( false );

	ballsInPlay++;

	balls.Append( ball );
	entities.Append( ball );

	return ball;
}

/*
=============================
idGameBustOutWindow::CreatePowerup
=============================
*/
BOEntity * idGameBustOutWindow::CreatePowerup( BOBrick *brick ) {
	BOEntity *powerEnt = new BOEntity( this );

	powerEnt->position.x = brick->x;
	powerEnt->position.y = brick->y;
	powerEnt->velocity.x = 0.f;
	powerEnt->velocity.y = 64.f;

	powerEnt->powerup = brick->powerup;

	switch( powerEnt->powerup ) {
		case POWERUP_BIGPADDLE:
			powerEnt->SetMaterial( "game/bustout/powerup_bigpaddle" );
			break;
		case POWERUP_MULTIBALL:
			powerEnt->SetMaterial( "game/bustout/powerup_multiball" );
			break;
		default:
			powerEnt->SetMaterial( "textures/common/nodraw" );
			break;
	}

	powerEnt->SetSize( 619/9, 256/12 );
	powerEnt->SetVisible( true );

	powerUps.Append( powerEnt );
	entities.Append( powerEnt );

	return powerEnt;
}

/*
=============================
idGameBustOutWindow::UpdatePowerups
=============================
*/
void idGameBustOutWindow::UpdatePowerups( void ) {
	idVec2 pos;

	for ( int i=0; i < powerUps.Num(); i++ ) {
		BOEntity *pUp = powerUps[i];

		// Check for powerup falling below screen
		if ( pUp->position.y > 480 ) {

			powerUps.RemoveIndex( i );
			pUp->removed = true;
			continue;
		}

		// Check for the paddle catching a powerup
		pos.x = pUp->position.x + ( pUp->width / 2 );
		pos.y = pUp->position.y + ( pUp->height / 2 );

		collideDir_t collision = paddle->checkCollision( pos, pUp->velocity );
		if ( collision != COLLIDE_NONE ) {
			BOEntity *ball;

			// Give the powerup to the player
			switch( pUp->powerup ) {
				case POWERUP_BIGPADDLE:
					bigPaddleTime = gui->GetTime() + 15000;
					break;
				case POWERUP_MULTIBALL:
					// Create 2 new balls in the spot of the existing ball
					for ( int b=0; b<2; b++ ) {
						ball = CreateNewBall();
						ball->position = balls[0]->position;
						ball->velocity = balls[0]->velocity;

						if ( b == 0 ) {
							ball->velocity.x -= 35.f;
						} else {
							ball->velocity.x += 35.f;
						}
						ball->velocity.NormalizeFast();
						ball->velocity *= ballSpeed;

						ball->SetVisible( true );
					}
					break;
				default:
					break;
			}

			// Play the sound
			session->sw->PlayShaderDirectly( "arcade_powerup", S_UNIQUE_CHANNEL );

			// Remove it
			powerUps.RemoveIndex( i );
			pUp->removed = true;
		}
	}
}

/*
=============================
idGameBustOutWindow::UpdatePaddle
=============================
*/
void idGameBustOutWindow::UpdatePaddle( void ) {
	idVec2 cursorPos;
	float  oldPos = paddle->x;

	cursorPos.x = gui->CursorX();
	cursorPos.y = gui->CursorY();

	if ( bigPaddleTime > gui->GetTime() ) {
		paddle->x = cursorPos.x - 80.f;
		paddle->width = 160;
		paddle->ent->width = 160;
		paddle->ent->SetMaterial( "game/bustout/doublepaddle" );
	} else {
		paddle->x = cursorPos.x - 48.f;
		paddle->width = 96;
		paddle->ent->width = 96;
		paddle->ent->SetMaterial( "game/bustout/paddle" );
	}
	paddle->ent->position.x = paddle->x;

	paddleVelocity = (paddle->x - oldPos);
}

/*
=============================
idGameBustOutWindow::UpdateBall
=============================
*/
void idGameBustOutWindow::UpdateBall( void ) {
	int ballnum,i,j;
	bool playSoundBounce = false;
	bool playSoundBrick = false;
	static int bounceChannel = 1;

	if ( ballsInPlay == 0 ) {
		return;
	}

	for ( ballnum = 0; ballnum < balls.Num(); ballnum++ ) {
		BOEntity *ball = balls[ballnum];

		// Check for ball going below screen, lost ball
		if ( ball->position.y > 480.f ) {
			ball->removed = true;
			continue;
		}

		// Check world collision
		if ( ball->position.y < 20 && ball->velocity.y < 0 ) {
			ball->velocity.y = -ball->velocity.y;

			// Increase ball speed when it hits ceiling
			if ( !ballHitCeiling ) {
				ballSpeed *= 1.25f;
				ballHitCeiling = true;
			}
			playSoundBounce = true;
		}

		if ( ball->position.x > 608 && ball->velocity.x > 0 ) {
			ball->velocity.x = -ball->velocity.x;
			playSoundBounce = true;
		} else if ( ball->position.x < 8 && ball->velocity.x < 0 ) {
			ball->velocity.x = -ball->velocity.x;
			playSoundBounce = true;
		}

		// Check for Paddle collision
		idVec2 ballCenter = ball->position + idVec2( BALL_RADIUS, BALL_RADIUS );
		collideDir_t collision = paddle->checkCollision( ballCenter, ball->velocity );

		if ( collision == COLLIDE_UP ) {
			if ( ball->velocity.y > 0 ) {
				idVec2	paddleVec( paddleVelocity*2, 0 );
				float	centerX;
				
				if ( bigPaddleTime > gui->GetTime() ) {
					centerX = paddle->x + 80.f;
				} else {
					centerX = paddle->x + 48.f;
				}

				ball->velocity.y = -ball->velocity.y;

				paddleVec.x += (ball->position.x - centerX) * 2;

				ball->velocity += paddleVec;
				ball->velocity.NormalizeFast();
				ball->velocity *= ballSpeed;

				playSoundBounce = true;
			}
		} else if ( collision == COLLIDE_LEFT || collision == COLLIDE_RIGHT ) {
			if ( ball->velocity.y > 0 ) {
				ball->velocity.x = -ball->velocity.x;
				playSoundBounce = true;
			}
		}

		collision = COLLIDE_NONE;

		// Check for collision with bricks
		for ( i=0; i<BOARD_ROWS; i++ ) {
			int num = board[i].Num();

			for ( j=0; j<num; j++ ) {
				BOBrick *brick = (board[i])[j];

				collision = brick->checkCollision( ballCenter, ball->velocity );
				if ( collision ) {
					// Now break the brick if there was a collision
					brick->isBroken = true;
					brick->ent->fadeOut = true;

					if ( brick->powerup > POWERUP_NONE ) {
						CreatePowerup( brick );
					}

					numBricks--;
					gameScore += 100;
					updateScore = true;

					// Go ahead an forcibly remove the last brick, no fade
					if ( numBricks == 0 ) {
						brick->ent->removed = true;
					}
					board[i].Remove( brick );
					break;
				}
			}

			if ( collision ) {
				playSoundBrick = true;
				break;
			}
		}

		if ( collision == COLLIDE_DOWN || collision == COLLIDE_UP ) {
			ball->velocity.y *= -1;
		} else if ( collision == COLLIDE_LEFT || collision == COLLIDE_RIGHT ) {
			ball->velocity.x *= -1;
		}

		if ( playSoundBounce ) {
			session->sw->PlayShaderDirectly( "arcade_ballbounce", bounceChannel );
		} else if ( playSoundBrick ) {
			session->sw->PlayShaderDirectly( "arcade_brickhit", bounceChannel );
		}

		if ( playSoundBounce || playSoundBrick ) {
			bounceChannel++;
			if ( bounceChannel == 4 ) {
				bounceChannel = 1;
			}
		}
	}

	// Check to see if any balls were removed from play
	for ( ballnum=0; ballnum<balls.Num(); ballnum++ ) {
		if ( balls[ballnum]->removed ) {
			ballsInPlay--;
			balls.RemoveIndex( ballnum );
		}
	}

	// If all the balls were removed, update the game accordingly
	if ( ballsInPlay == 0 ) {
		if ( ballsRemaining == 0 ) {
			gameOver = true;

			// Game Over sound
			session->sw->PlayShaderDirectly( "arcade_sadsound", S_UNIQUE_CHANNEL );
		} else {
			ballsRemaining--;

			// Ball was lost, but game is not over
			session->sw->PlayShaderDirectly( "arcade_missedball", S_UNIQUE_CHANNEL );
		}

		ClearPowerups();
		updateScore = true;
	}
}

/*
=============================
idGameBustOutWindow::UpdateGame
=============================
*/
void idGameBustOutWindow::UpdateGame() {
	int i;

	if ( onNewGame ) {
		ResetGameState();

		// Create Board
		SetCurrentBoard();

		gamerunning = true;
	}
	if ( onContinue ) {
		gameOver = false;
		ballsRemaining = 3;

		onContinue = false;
	}
	if ( onNewLevel ) {
		currentLevel++;

		ClearBoard();
		SetCurrentBoard();

		ballSpeed = BALL_SPEED * ( 1.f + ((float)currentLevel/5.f) );
		if ( ballSpeed > BALL_MAXSPEED ) {
			ballSpeed = BALL_MAXSPEED;
		}
		updateScore = true;
		onNewLevel = false;
	}

	if(gamerunning == true) {

		UpdatePaddle();
		UpdateBall();
		UpdatePowerups();

		for( i = 0; i < entities.Num(); i++ ) {
			entities[i]->Update( timeSlice, gui->GetTime() );
		}

		// Delete entities that need to be deleted
		for( i = entities.Num()-1; i >= 0; i-- ) {
			if( entities[i]->removed ) {
				BOEntity* ent = entities[i];
				delete ent;
				entities.RemoveIndex(i);
			}
		}

		if ( updateScore ) {
			UpdateScore();
			updateScore = false;
		}
	}
}
