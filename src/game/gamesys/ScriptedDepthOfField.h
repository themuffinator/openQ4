// openQ4 additions to the SDK game sources; see src/game/LICENSE.
//
// Map-scripted depth of field, shared by the SP and MP game modules.
//
// Map scripts drive the renderer's thin-lens depth of field for cinematics:
//
//   sys.setDepthOfField( float focusDistance, float fStop, float blendTime );
//   sys.setDepthOfFieldFocus( entity focus, float fStop, float blendTime );
//   sys.setDepthOfFieldLens( float focalLength, float focusRange, float maxBlur );
//   sys.clearDepthOfField( float blendTime );
//
// Distances are world units along the view axis (0 autofocuses on the centre
// of the view), blend times seconds, focal lengths millimetres (0 follows the
// view's field of view), maxBlur pixels of a 1080-line view (0 is the
// default). setDepthOfFieldFocus follows the entity (an actor's eyes) as it
// and the camera move. Every change eases from where the effect is now.
//
// The script compiler declares these events itself the first time a script
// calls one (idCompiler::DeclareImplicitEvent), so Quake 4's own scripts and
// their saved programs are unchanged. The state lives in the worldspawn's
// spawn args, which a savegame already carries, so a save made mid-shot
// restores it without a save-format change. The player's Depth of Field
// setting (r_depthOfField) decides whether it draws.

#ifndef __GAME_SCRIPTED_DEPTH_OF_FIELD_H__
#define __GAME_SCRIPTED_DEPTH_OF_FIELD_H__

namespace ScriptedDepthOfField {
	// focusEntity names an entity to follow, or is empty for focusDistance.
	void	Set( float focusDistance, const char *focusEntity, float fStop, float blendSeconds );
	void	SetLens( float focalLength, float focusRange, float maxBlur );
	void	Clear( float blendSeconds );
	// The request for a view at the current game time; amount 0 when none.
	renderViewDepthOfField_t Evaluate( const idVec3 &viewOrigin, const idMat3 &viewAxis );
}

#endif /* !__GAME_SCRIPTED_DEPTH_OF_FIELD_H__ */
