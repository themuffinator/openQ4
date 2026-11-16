/* openQ4 corrections to names the Awakening's content mistypes; see LICENSES/QUAKE-4-SDK-EULA.rtf. */
#ifndef __AWAKENING_CONTENTREPAIRS_H__
#define __AWAKENING_CONTENTREPAIRS_H__

// The alpha's scripts and definitions name some dialogue keys, sounds and
// joints that do not exist, beside ones that do and plainly are what was meant.
// The engine fails all of these quietly: the line is not spoken, the effect not
// shown, the eyes left at the feet. Each function here maps one such name to
// the surviving one, only in The Awakening's single player, only while the
// written name is still missing, and never by guessing from spelling at run
// time: every pair is listed with its evidence in ContentRepairs.inl and
// docs/dev/awakening-missions.md.

// Adds the dialogue keys a map's script speaks but its speaker lacks.
// Call with the map entity's arguments after its definition's defaults.
void Awakening_RestoreDialogueKeys( idDict &args );

// Replaces a map entity's sound key that names nothing, or a line left over
// from the retail map it was built from. Call wherever the entity's media is
// gathered: its spawn arguments and both precache passes.
void Awakening_CorrectEntitySounds( idDict &args );

// For a line whose sound declaration cannot reach its recording: the lip-sync
// declaration to start it under and the recording to play. False, with both
// left alone, for every other line.
bool Awakening_SpeechNames( const char *speech, const char *&lipSync, const char *&sound );

// The recorded line a stale radio sound name refers to, or the name itself.
const char *Awakening_RecordedLine( const char *sound );

// True for a radio line named by a script but declared nowhere. The radio then
// stays silent instead of sounding the engine's missing-sample tone.
bool Awakening_UnwrittenLine( const char *sound );

// The joint a missing joint name plainly meant, or INVALID_JOINT.
jointHandle_t Awakening_IntendedJoint( const idAnimator *animator, const char *name );

// The effect a frame command's fx_ key was written for, when no definition
// using that model supplies the key; otherwise NULL.
const idDecl *Awakening_IntendedEffect( idEntity *ent, const char *key );

// Loads those effects with the map; call while the map's media is precached.
void Awakening_PrecacheContentRepairs( void );

// As above for a frame command's effect. A joint the mesh has no counterpart
// for can be placed from the model's symmetry: jointOffset is then the offset
// from the returned joint, in that joint's space.
jointHandle_t Awakening_IntendedEffectJoint( idEntity *ent, const char *name, idVec3 &jointOffset );

#endif /* !__AWAKENING_CONTENTREPAIRS_H__ */
