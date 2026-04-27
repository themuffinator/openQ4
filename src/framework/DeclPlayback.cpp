// DeclPlayback.cpp
//


static const int DECL_PLAYBACK_RECORD_GRANULARITY = 60;
static const float DECL_PLAYBACK_DEFAULT_FRAME_RATE = 15.0f;
static const float DECL_PLAYBACK_DELTA_EPSILON = 0.0625f;
static const int DECL_PLAYBACK_LEXER_FLAGS = DECL_LEXER_FLAGS;

static float PlaybackFrameStep( const rvDeclPlayback &playback ) {
	const float frameRate = playback.GetFrameRate();
	return frameRate > 0.0f ? 1.0f / frameRate : 1.0f / DECL_PLAYBACK_DEFAULT_FRAME_RATE;
}

static float PlaybackClampedTime( const rvDeclPlayback &playback, float localTime ) {
	if ( localTime < 0.0f ) {
		return 0.0f;
	}
	if ( localTime > playback.GetDuration() ) {
		return playback.GetDuration();
	}
	return localTime;
}

static bool PlaybackIsExpired( const rvDeclPlayback &playback, float localTime ) {
	return localTime >= playback.GetDuration();
}

rvDeclPlayback::rvDeclPlayback() {
	flags = 0;
	frameRate = DECL_PLAYBACK_DEFAULT_FRAME_RATE;
	duration = 0.0f;
	origin.Zero();
	bounds.Clear();
}

rvDeclPlayback::~rvDeclPlayback() {

}

static int PlaybackRequestedControl( int control ) {
	if ( control < 0 ) {
		return PBFL_GET_POSITION | PBFL_GET_ANGLES | PBFL_GET_BUTTONS | PBFL_GET_VELOCITY | PBFL_GET_ACCELERATION | PBFL_GET_ANGLES_FROM_VEL;
	}
	return control;
}

static void DeclPlayback_ToolPlaybackFinished( void ) {
	if ( declPlaybackEdit != NULL ) {
		declPlaybackEdit->PlaybackFinished();
	}
}

/*
=====================
rvDeclPlayback::ParseSample
=====================
*/
void rvDeclPlayback::ParseSample(idLexer* src, idVec3& pos, idAngles& ang)
{
	idToken token; 

	while ( src->ReadToken( &token ) ) {
		if (token == "}")
		{
			break;
		}
		else if (!token.Icmp("down") || !token.Icmp("up") || !token.Icmp("impulse"))
		{
			src->ParseInt(); // jmarshall: decompiled code doesn't use this, seems like its just skipped.
			continue;
		}
		else if (!token.Icmp("rotate"))
		{
			ang.pitch = src->ParseFloat();
			src->ExpectTokenString(",");
			ang.yaw = src->ParseFloat();
			src->ExpectTokenString(",");
			ang.roll = src->ParseFloat();
			flags |= PBFL_GET_ANGLES;
			continue;
		}
		else if (!token.Icmp("ang"))
		{
			ang.pitch = src->ParseFloat();
			src->ExpectTokenString(",");
			ang.yaw = src->ParseFloat();
			ang.roll = 0.0;
			flags |= PBFL_GET_ANGLES;
			continue;
		}
		else if (!token.Icmp("pos"))
		{
			pos.x = src->ParseFloat();
			src->ExpectTokenString(",");
			pos.y = src->ParseFloat();
			src->ExpectTokenString(",");
			pos.z = src->ParseFloat();
			flags |= PBFL_GET_POSITION;
			continue;
		}
	}
}

/*
=====================
rvDeclPlayback::ParseData
=====================
*/
bool rvDeclPlayback::ParseData(idLexer* src) {
	idToken token;
	idVec3 pos;
	idAngles ang;

	float t = 0;
	pos.Zero();
	ang.Zero();

	if ( !src->ExpectTokenString( "{" ) ) {
		return false;
	}

	while ( src->ReadToken(&token) )
	{

		if (token == "}")
		{
			duration = t;
			return true;
		}

		if (token == "{")
		{
			ParseSample(src, pos, ang);

			points.AddValue(t, pos);
			angles.AddValue(t, ang);
			t += PlaybackFrameStep( *this );
			continue;
		}
	}

	return false;
}

/*
=====================
rvDeclPlayback::ParseButton
=====================
*/
void rvDeclPlayback::ParseButton(idLexer* src, byte& button, rvButtonState& state) {
	idToken token;
	byte impulse = 0;

	state.time = src->ParseFloat();

	while ( src->ReadToken(&token) )
	{

		if (token == "}")
		{
			break;
		}

		if (!token.Icmp("impulse"))
		{
			impulse = src->ParseInt();
			continue;
		}
		else if (!token.Icmp("up"))
		{
			button = ~src->ParseInt() & button;
			continue;
		}
		else if (!token.Icmp("down"))
		{
			button = src->ParseInt() | button;
			continue;
		}
	}

	state.state = button;
	state.impulse = impulse;
}

/*
=====================
rvDeclPlayback::ParseButtons
=====================
*/
bool rvDeclPlayback::ParseButtons(idLexer* src) {
	idToken token;
	byte button = 0;
	rvButtonState state;

	if ( !src->ExpectTokenString( "{" ) ) {
		return false;
	}

	while (true)
	{
		if (!src->ReadToken(&token)) {
			return false;
		}

		if (token == "}")
		{
			return true;
		}

		if (token == "{")
		{
			ParseButton(src, button, state);
			flags |= PBFL_GET_BUTTONS;
			buttons.Append(state);
			continue;
		}
	}
}


/*
=====================
rvDeclPlayback::ParseSequence
=====================
*/
bool rvDeclPlayback::ParseSequence(idLexer* src) {
	src->ExpectTokenString("sequence");
	src->ExpectTokenString("{");

	idToken token;

	while ( src->ReadToken(&token) )
	{

		if (token == "}")
		{
			return true;
		}

		if (!token.Icmp("framerate"))
		{
			frameRate = src->ParseFloat();
			continue;
		}
		else if (!token.Icmp("origin"))
		{
			origin.x = src->ParseFloat();
			src->ExpectTokenString(",");
			origin.y = src->ParseFloat();
			src->ExpectTokenString(",");
			origin.z = src->ParseFloat();
			continue;
		}
		else if (!token.Icmp("destination"))
		{
			idStr dest;
			src->ParseRestOfLine(dest);
			continue;
		}
		else
		{
			src->Error("rvDeclPlayback::ParseSequence: Invalid or unexpected token %s\n", token.c_str());
			return false;
		}
	}

	return false;
}

/*
=====================
rvDeclPlayback::DefaultDefinition
=====================
*/
const char* rvDeclPlayback::DefaultDefinition() const
{
	return "{ sequence { } data { } }";
}

/*
=====================
rvDeclPlayback::Size
=====================
*/
size_t rvDeclPlayback::Size(void) const {
	return sizeof(rvDeclPlayback)
		+ buttons.Allocated()
		+ points.GetNumValues() * sizeof(idVec3)
		+ angles.GetNumValues() * sizeof(idAngles);
}

/*
=====================
rvDeclPlayback::Copy
=====================
*/
void rvDeclPlayback::Copy(rvDeclPlayback* pb) {
	if (pb == NULL) {
		FreeData();
		return;
	}

	flags = pb->flags & ~PBFL_ED_MASK;
	frameRate = pb->frameRate;
	duration = pb->duration;
	origin = pb->origin;
	bounds = pb->bounds;
	points = pb->points;
	angles = pb->angles;
	buttons = pb->buttons;
}

/*
=====================
rvDeclPlayback::SetOrigin
=====================
*/
void rvDeclPlayback::SetOrigin(void) {
	const int numPoints = points.GetNumValues();
	if (numPoints <= 0) {
		return;
	}

	const idVec3 offset = points.GetValue(0);
	origin += offset;
	bounds.Clear();

	for (int i = 0; i < numPoints; ++i) {
		idVec3 relativePoint = points.GetValue(i) - offset;
		points.SetValue(i, relativePoint);
		bounds.AddPoint(relativePoint);
	}
}

/*
=====================
rvDeclPlayback::Start
=====================
*/
void rvDeclPlayback::Start(void) {
	points.Clear();
	angles.Clear();
	buttons.Clear();
	flags = 0;
	duration = 0.0f;
	origin.Zero();
	bounds.Clear();
	points.SetGranularity(DECL_PLAYBACK_RECORD_GRANULARITY);
	angles.SetGranularity(DECL_PLAYBACK_RECORD_GRANULARITY);
	buttons.SetGranularity(DECL_PLAYBACK_RECORD_GRANULARITY);
}

/*
=====================
rvDeclPlayback::Finish
=====================
*/
bool rvDeclPlayback::Finish(float desiredDuration) {
	SetOrigin();

	rvDeclPlayback temp;
	temp.Copy(this);

	if ( base != NULL ) {
		MakeDefault();
	} else {
		FreeData();
	}

	flags = temp.flags;
	frameRate = temp.frameRate;
	origin = temp.origin;
	if (desiredDuration >= 0.0f) {
		duration = desiredDuration;
	} else {
		duration = temp.duration;
	}
	const float speed = desiredDuration > 0.0f ? temp.duration / desiredDuration : 1.0f;
	bounds.Clear();

	byte previousState = 0;
	for (int i = 0; i < temp.buttons.Num(); ++i) {
		rvButtonState state = temp.buttons[i];
		if (state.state == previousState && state.impulse == 0) {
			continue;
		}
		state.time = speed != 0.0f ? state.time / speed : 0.0f;
		buttons.Append(state);
		previousState = state.state;
	}

	float sourceTime = 0.0f;
	float outputTime = 0.0f;
	while (sourceTime <= temp.duration) {
		rvDeclPlaybackData pbd;
		pbd.Init();

		temp.GetCurrentData(PBFL_GET_POSITION | PBFL_GET_ANGLES, sourceTime, 0.0f, &pbd);

		points.AddValue(outputTime, pbd.GetPosition() - origin);
		angles.AddValue(outputTime, pbd.GetAngles());
		bounds.AddPoint(pbd.GetPosition() - origin);

		const float frameStep = PlaybackFrameStep( *this );
		sourceTime += speed * frameStep;
		outputTime += frameStep;
	}

	DeclPlayback_ToolPlaybackFinished();
	return true;
}

/*
=====================
rvDeclPlayback::SetCurrentData
=====================
*/
bool rvDeclPlayback::SetCurrentData(float localTime, int control, rvDeclPlaybackData* pbd) {
	if (pbd == NULL) {
		return false;
	}

	control = PlaybackRequestedControl(control);
	if (control & PBFL_GET_POSITION) {
		points.AddValue(localTime, pbd->GetPosition());
		flags |= PBFL_GET_POSITION;
		bounds.AddPoint(pbd->GetPosition());
	}

	if (control & PBFL_GET_ANGLES) {
		angles.AddValue(localTime, pbd->GetAngles());
		flags |= PBFL_GET_ANGLES;
	}

	if (control & PBFL_GET_BUTTONS) {
		rvButtonState state;
		state.Init(localTime, pbd->GetButtons(), pbd->GetImpulse());
		buttons.Append(state);
		flags |= PBFL_GET_BUTTONS;
	}

	if (localTime > duration) {
		duration = localTime;
	}

	return true;
}

/*
=====================
rvDeclPlayback::GetCurrentOffset
=====================
*/
bool rvDeclPlayback::GetCurrentOffset(float localTime, idVec3& pos) const {
	const bool expired = PlaybackIsExpired( *this, localTime );

	pos.Zero();
	if ( ( flags & PBFL_GET_POSITION ) == 0 || points.GetNumValues() <= 0 ) {
		return expired;
	}

	pos = points.GetCurrentValue( PlaybackClampedTime( *this, localTime ) );
	return expired;
}

/*
=====================
rvDeclPlayback::GetCurrentAngles
=====================
*/
bool rvDeclPlayback::GetCurrentAngles(float localTime, idAngles& ang) const {
	const bool expired = PlaybackIsExpired( *this, localTime );

	ang.Zero();
	if ( ( flags & PBFL_GET_ANGLES ) == 0 || angles.GetNumValues() <= 0 ) {
		return expired;
	}

	ang = angles.GetCurrentValue( PlaybackClampedTime( *this, localTime ) );
	return expired;
}

/*
=====================
rvDeclPlayback::GetCurrentData
=====================
*/
bool rvDeclPlayback::GetCurrentData(int control, float localTime, float lastTime, rvDeclPlaybackData* pbd) const {
	if (pbd == NULL) {
		return true;
	}

	control = PlaybackRequestedControl(control);
	const bool hasPoints = (flags & PBFL_GET_POSITION) != 0 && points.GetNumValues() > 0;
	const bool hasAngles = (flags & PBFL_GET_ANGLES) != 0 && angles.GetNumValues() > 0;
	const bool hasButtons = (flags & PBFL_GET_BUTTONS) != 0 && buttons.Num() > 0;
	const bool expired = PlaybackIsExpired( *this, localTime );
	const float sampleTime = PlaybackClampedTime( *this, localTime );
	float previousTime = PlaybackClampedTime( *this, lastTime );
	if ( previousTime > sampleTime ) {
		previousTime = sampleTime;
	}

	pbd->SetChanged(0);
	pbd->SetImpulse(0);

	idVec3 sampledOffset;
	idVec3 sampledVelocity;
	idAngles sampledAngles;
	sampledOffset.Zero();
	sampledVelocity.Zero();
	sampledAngles.Zero();
	bool haveOffset = false;
	bool haveVelocity = false;
	bool haveAngles = false;

	if (hasPoints) {
		if (control & (PBFL_GET_POSITION | PBFL_GET_VELOCITY | PBFL_GET_ACCELERATION | PBFL_GET_ANGLES_FROM_VEL)) {
			sampledOffset = points.GetCurrentValue(sampleTime);
			haveOffset = true;
		}
		if (control & (PBFL_GET_VELOCITY | PBFL_GET_ACCELERATION | PBFL_GET_ANGLES_FROM_VEL)) {
			sampledVelocity = points.GetCurrentFirstDerivative(sampleTime);
			haveVelocity = true;
		}
		if (control & PBFL_GET_ACCELERATION) {
			pbd->SetAcceleration(points.GetCurrentSecondDerivative(sampleTime));
		} else {
			idVec3 zero;
			zero.Zero();
			pbd->SetAcceleration(zero);
		}
	} else {
		idVec3 zero;
		zero.Zero();
		pbd->SetAcceleration(zero);
	}

	if ((control & PBFL_GET_POSITION) && haveOffset) {
		pbd->SetPosition(origin + sampledOffset);
	} else if (control & PBFL_GET_POSITION) {
		idVec3 zero;
		zero.Zero();
		pbd->SetPosition(zero);
	}

	if ((control & PBFL_GET_VELOCITY) && haveVelocity) {
		pbd->SetVelocity(sampledVelocity);
	} else {
		idVec3 zero;
		zero.Zero();
		pbd->SetVelocity(zero);
	}

	if ((control & PBFL_GET_ANGLES) && hasAngles) {
		sampledAngles = angles.GetCurrentValue(sampleTime);
		haveAngles = true;
	}

	if (!haveAngles && (control & PBFL_GET_ANGLES_FROM_VEL) && haveVelocity &&
		(sampledVelocity.x != 0.0f || sampledVelocity.y != 0.0f || sampledVelocity.z != 0.0f)) {
		sampledAngles = sampledVelocity.ToAngles();
		haveAngles = true;
	}

	if (haveAngles) {
		pbd->SetAngles(sampledAngles);
	} else {
		idAngles zero;
		zero.Zero();
		pbd->SetAngles(zero);
	}

	if ((control & PBFL_GET_BUTTONS) && hasButtons) {
		byte previousButtons = 0;
		byte currentButtons = 0;
		byte currentImpulse = 0;
		byte rollingButtons = 0;

		for (int i = 0; i < buttons.Num(); ++i) {
			const rvButtonState& state = buttons[i];
			if (state.time <= previousTime) {
				previousButtons = state.state;
			}
			if (state.time <= sampleTime) {
				currentButtons = state.state;
			} else {
				break;
			}
		}

		rollingButtons = previousButtons;
		for (int i = 0; i < buttons.Num(); ++i) {
			const rvButtonState& state = buttons[i];
			if (state.time <= previousTime) {
				continue;
			}
			if (state.time > sampleTime) {
				break;
			}

			const byte changedBits = rollingButtons ^ state.state;
			const byte downBits = state.state & changedBits;
			const byte upBits = rollingButtons & changedBits;
			if (downBits != 0) {
				pbd->SetChanged(downBits);
				pbd->SetImpulse(0);
				pbd->CallCallback(PBCB_BUTTON_DOWN, state.time - previousTime);
			}
			if (upBits != 0) {
				pbd->SetChanged(upBits);
				pbd->SetImpulse(0);
				pbd->CallCallback(PBCB_BUTTON_UP, state.time - previousTime);
			}
			if (state.impulse != 0) {
				currentImpulse = state.impulse;
				pbd->SetChanged(0);
				pbd->SetImpulse(state.impulse);
				pbd->CallCallback(PBCB_IMPULSE, state.time - previousTime);
			}

			rollingButtons = state.state;
		}

		pbd->SetButtons(currentButtons);
		pbd->SetChanged(previousButtons ^ currentButtons);
		pbd->SetImpulse(currentImpulse);
	} else {
		pbd->SetButtons(0);
		pbd->SetChanged(0);
		pbd->SetImpulse(0);
	}

	return expired;
}

/*
=====================
rvDeclPlayback::Parse
=====================
*/
bool rvDeclPlayback::Parse(const char* text, const int textLength) {
	return Parse(text, textLength, false);
}

/*
=====================
rvDeclPlayback::Parse
=====================
*/
bool rvDeclPlayback::Parse(const char* text, const int textLength, bool noCaching) {
	idLexer src;
	idToken	token;

	FreeData();
	flags = 0;
	frameRate = DECL_PLAYBACK_DEFAULT_FRAME_RATE;
	duration = 0.0f;
	origin.Zero();
	bounds.Clear();

	src.LoadMemory(text, textLength, GetFileName(), GetLineNum());
	src.SetFlags(DECL_PLAYBACK_LEXER_FLAGS);
	src.SkipUntilString("{");

	if ( !ParseSequence(&src) ) {
		return false;
	}

	while (1) {
		if (!src.ReadToken(&token)) {
			break;
		}

		if (!token.Icmp("}")) {
			SetOrigin();
			return true;
		}
		else if (!token.Icmp("data"))
		{
			ParseData(&src);
			continue;
		}
		else if (!token.Icmp("buttons"))
		{
			ParseButtons(&src);
		}
	}

	(void)noCaching;
	return false;
}

/*
=====================
rvDeclPlayback::FreeData
=====================
*/
void rvDeclPlayback::FreeData(void) {
	points.Clear();
	angles.Clear();
	buttons.Clear();
}

/*
=====================
rvDeclPlayback::RebuildTextSource
=====================
*/
bool rvDeclPlayback::RebuildTextSource(void) {
	idFile_Memory f;

	f.WriteFloatString("\nplayback %s\n{\n", GetName());
	WriteSequence(f);
	WriteData(f);
	WriteButtons(f);
	f.WriteFloatString("}\n\n");

	SetText(f.GetDataPtr());
	return true;
}

/*
=====================
rvDeclPlayback::WriteData
=====================
*/
void rvDeclPlayback::WriteData(idFile_Memory& f) {
	const int numPoints = points.GetNumValues();
	const int numAngles = angles.GetNumValues();
	const int numSamples = numPoints > numAngles ? numPoints : numAngles;

	if (numSamples <= 0 || (flags & (PBFL_GET_POSITION | PBFL_GET_ANGLES)) == 0) {
		return;
	}

	idVec3 oldPosition;
	idAngles oldAngles;
	oldPosition.Zero();
	oldAngles.Zero();

	f.WriteFloatString("\tdata\n\t{\n");
	for (int i = 0; i < numSamples; ++i) {
		f.WriteFloatString("\t\t{ ");

		if ((flags & PBFL_GET_POSITION) && i < numPoints) {
			const idVec3 position = points.GetValue(i);
			if (idMath::Fabs(position.x - oldPosition.x) > DECL_PLAYBACK_DELTA_EPSILON ||
				idMath::Fabs(position.y - oldPosition.y) > DECL_PLAYBACK_DELTA_EPSILON ||
				idMath::Fabs(position.z - oldPosition.z) > DECL_PLAYBACK_DELTA_EPSILON) {
				f.WriteFloatString("pos %.1f,%.1f,%.1f ", position.x, position.y, position.z);
				oldPosition = position;
			}
		}

		if ((flags & PBFL_GET_ANGLES) && i < numAngles) {
			const idAngles sampleAngles = angles.GetValue(i);
			if (idMath::Fabs(sampleAngles.pitch - oldAngles.pitch) > DECL_PLAYBACK_DELTA_EPSILON ||
				idMath::Fabs(sampleAngles.yaw - oldAngles.yaw) > DECL_PLAYBACK_DELTA_EPSILON ||
				idMath::Fabs(sampleAngles.roll - oldAngles.roll) > DECL_PLAYBACK_DELTA_EPSILON) {
				if (sampleAngles.roll == 0.0f) {
					f.WriteFloatString("ang %.1f,%.1f ", sampleAngles.pitch, sampleAngles.yaw);
				} else {
					f.WriteFloatString("rotate %.1f,%.1f,%.1f ", sampleAngles.pitch, sampleAngles.yaw, sampleAngles.roll);
				}
				oldAngles = sampleAngles;
			}
		}

		f.WriteFloatString("}\n");
	}
	f.WriteFloatString("\t}\n");
}

/*
=====================
rvDeclPlayback::WriteButtons
=====================
*/
void rvDeclPlayback::WriteButtons(idFile_Memory& f) {
	if (buttons.Num() <= 0 || (flags & PBFL_GET_BUTTONS) == 0) {
		return;
	}

	byte previousState = 0;

	f.WriteFloatString("\tbuttons\n\t{\n");
	for (int i = 0; i < buttons.Num(); ++i) {
		const rvButtonState& state = buttons[i];
		const byte changedBits = previousState ^ state.state;

		if (changedBits == 0 && state.impulse == 0) {
			continue;
		}

		f.WriteFloatString("\t\t{ %.3g ", state.time);
		if (changedBits != 0) {
			if (state.state & changedBits) {
				f.WriteFloatString("down %d ", static_cast<int>(state.state & changedBits));
			}
			if (previousState & changedBits) {
				f.WriteFloatString("up %d ", static_cast<int>(previousState & changedBits));
			}
			previousState = state.state;
		}
		if (state.impulse != 0) {
			f.WriteFloatString("impulse %d ", static_cast<int>(state.impulse));
		}
		f.WriteFloatString("}\n");
	}
	f.WriteFloatString("\t}\n");
}

/*
=====================
rvDeclPlayback::WriteSequence
=====================
*/
void rvDeclPlayback::WriteSequence(idFile_Memory& f) {
	f.WriteFloatString("\tsequence\n\t{\n");
	f.WriteFloatString("\t\torigin\t\t%.1f,%.1f,%.1f\n", origin.x, origin.y, origin.z);
	f.WriteFloatString("\t\tframeRate\t%.1f\n", frameRate);
	f.WriteFloatString("\t}\n");
}

/*
=====================
rvDeclPlayback::Validate
=====================
*/
bool rvDeclPlayback::Validate( const char *psText, int iTextLength, idStr &strReportTo ) const {
	(void)strReportTo;

	idDecl *decl = declManager->AllocateDecl( DECL_PLAYBACK );
	const bool valid = DeclManager_ValidateParsedDecl( decl, DECL_PLAYBACK, decl != NULL && decl->Parse( psText, iTextLength, false ) );
	if ( decl != NULL ) {
		decl->FreeData();
	}
	DeclManager_FreeAllocatedDecl( decl );
	return valid;
}

class rvDeclPlaybackEditLocal : public rvDeclPlaybackEdit {
public:
	bool Finish(rvDeclPlayback* edit, float desiredDuration) override {
		return edit != NULL && edit->Finish(desiredDuration);
	}

	void SetOrigin(rvDeclPlayback* edit) override {
		if (edit != NULL) {
			edit->SetOrigin();
		}
	}

	void SetOrigin(rvDeclPlayback* edit, idVec3& origin) override {
		if (edit != NULL) {
			edit->SetOrigin(origin);
		}
	}

	void Copy(rvDeclPlayback* edit, rvDeclPlayback* copy) override {
		if (edit != NULL) {
			edit->Copy(copy);
		}
	}

	void PlaybackFinished( void ) override {
	}
};

static rvDeclPlaybackEditLocal localDeclPlaybackEdit;
rvDeclPlaybackEdit* declPlaybackEdit = &localDeclPlaybackEdit;
