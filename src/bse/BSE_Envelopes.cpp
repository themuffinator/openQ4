// BSP_Envelopes.cpp
//




#include "BSE_Envelope.h"

/*
==========================
rvEnvParms::GetMinMax
==========================
*/
bool rvEnvParms::GetMinMax(float& min, float& max)
{
	bool result = false; // al

	if (mTable)
	{
		min = mTable->GetMinValue();
		result = true;
		max = mTable->GetMaxValue();
	}
	else
	{
		min = 0.0;
		max = 0.0;
		result = false;
	}
	return result;
}

/*
==========================
rvEnvParms::Init
==========================
*/
void rvEnvParms::Init(void)
{
	mTable = 0;
	mIsCount = 1;
	mFastLookUp = 0;
	mEnvOffset.z = 0.0;
	mEnvOffset.y = 0.0;
	mEnvOffset.x = 0.0;
	mRate.x = 1.0;
	mRate.y = 1.0;
	mRate.z = 1.0;
}

/*
==========================
rvEnvParms::Compare
==========================
*/
bool rvEnvParms::Compare(const rvEnvParms& comp) const
{
	return this->mTable == comp.mTable
		&& this->mIsCount == comp.mIsCount
		&& this->mRate.x == comp.mRate.x
		&& this->mRate.y == comp.mRate.y
		&& this->mRate.z == comp.mRate.z
		&& this->mEnvOffset.x == comp.mEnvOffset.x
		&& this->mEnvOffset.y == comp.mEnvOffset.y
		&& this->mEnvOffset.z == comp.mEnvOffset.z;
}

/*
==========================
rvEnvParms::operator=
==========================
*/
void rvEnvParms::operator=(const rvEnvParms& copy)
{
	if (this == &copy) {
		return;
	}

	mTable = copy.mTable;
	mIsCount = copy.mIsCount;
	mStatic = copy.mStatic;
	mFastLookUp = copy.mFastLookUp;
	mEnvOffset = copy.mEnvOffset;
	mRate = copy.mRate;
}

void rvEnvParms::CalcRate(float* rate, float duration, int count)
{
	if (!rate || count <= 0) {
		return;
	}

	const float safeDuration = Max(0.002f, duration);
	for (int i = 0; i < count; ++i) {
		rate[i] = mIsCount ? (rate[i] / safeDuration) : rate[i];
	}
}

void rvEnvParms::Evaluate3(const float time, const float* start, const float* rate, const float* end, float* dest)
{
	if (!start || !rate || !end || !dest) {
		return;
	}

	if (mTable) {
		const float tx = rate[0] * time + mEnvOffset.x;
		const float ty = rate[1] * time + mEnvOffset.y;
		const float tz = rate[2] * time + mEnvOffset.z;

		const float vx = mTable->TableLookup(tx);
		const float vy = mTable->TableLookup(ty);
		const float vz = mTable->TableLookup(tz);

		dest[0] = (end[0] - start[0]) * vx + start[0];
		dest[1] = (end[1] - start[1]) * vy + start[1];
		dest[2] = (end[2] - start[2]) * vz + start[2];
	}
	else {
		dest[0] = start[0];
		dest[1] = start[1];
		dest[2] = start[2];
	}
}

void rvEnvParms::Evaluate(class rvEnvParms1Particle& env, float time, float oneOverDuration, float* dest)
{
	if (!dest) {
		return;
	}

	if (mTable) {
		float rate = mRate.x;
		if (mIsCount) {
			rate *= oneOverDuration;
		}
		const float lookup = rate * time + mEnvOffset.x;
		const float value = mTable->TableLookup(lookup);
		*dest = (env.mEnd - env.mStart) * value + env.mStart;
	}
	else {
		*dest = env.mStart;
	}
}

void rvEnvParms::Evaluate(class rvEnvParms2Particle& env, float time, float oneOverDuration, float* dest)
{
	const idDeclTable* v6; // ecx
	float rate_4; // [esp+14h] [ebp-8h]
	float rate_4a; // [esp+14h] [ebp-8h]
	float v9; // [esp+18h] [ebp-4h]
	float timea; // [esp+24h] [ebp+8h]
	float timeb; // [esp+24h] [ebp+8h]
	float result; // [esp+28h] [ebp+Ch]
	float resulta; // [esp+28h] [ebp+Ch]
	float resultb; // [esp+28h] [ebp+Ch]
	float resultc; // [esp+28h] [ebp+Ch]

	if (this->mFastLookUp)
	{
		rate_4 = this->mRate.x;
		if (this->mIsCount)
			rate_4 = rate_4 * oneOverDuration;
		timea = rate_4 * time + this->mEnvOffset.x;
		result = mTable->TableLookup(timea);
		*dest = env.mStart.x + (env.mEnd.x - env.mStart.x) * result;
		dest[1] = result * (env.mEnd.y - env.mStart.y) + env.mStart.y;
	}
	else
	{
		v6 = this->mTable;
		if (this->mTable)
		{
			rate_4a = this->mRate.x;
			v9 = this->mRate.y;
			if (this->mIsCount)
			{
				rate_4a = rate_4a * oneOverDuration;
				v9 = oneOverDuration * v9;
			}
			resulta = rate_4a * time + this->mEnvOffset.x;
			resultb = v6->TableLookup(resulta);//   ((double(__stdcall*)(_DWORD))v6->TableLookup)(LODWORD(resulta));
			*dest = (env.mEnd.x - env.mStart.x) * resultb + env.mStart.x;
			timeb = v9 * time + this->mEnvOffset.y;
			resultc = mTable->TableLookup(timeb);  //((double(__stdcall*)(_DWORD))this->mTable->TableLookup)(LODWORD(timeb));
			dest[1] = (env.mEnd.y - env.mStart.y) * resultc + env.mStart.y;
		}
		else
		{
			*dest = env.mStart.x;
			dest[1] = env.mStart.y;
		}
	}
}


void rvEnvParms::Evaluate(class rvEnvParms3Particle& env, float time, float oneOverDuration, float* dest)
{
	const idDeclTable* v6; // ecx
	float v8; // [esp+18h] [ebp-10h]
	float v9; // [esp+1Ch] [ebp-Ch]
	float v10; // [esp+20h] [ebp-8h]
	float v11; // [esp+24h] [ebp-4h]
	float enva; // [esp+2Ch] [ebp+4h]
	float timea; // [esp+30h] [ebp+8h]
	float timeb; // [esp+30h] [ebp+8h]
	float result; // [esp+34h] [ebp+Ch]
	float resulta; // [esp+34h] [ebp+Ch]
	float resultb; // [esp+34h] [ebp+Ch]
	float resultc; // [esp+34h] [ebp+Ch]
	float resultd; // [esp+34h] [ebp+Ch]

	if (this->mFastLookUp)
	{
		v8 = this->mRate.x;
		if (this->mIsCount)
			v8 = v8 * oneOverDuration;
		timea = v8 * time + this->mEnvOffset.x;
		result = this->mTable->TableLookup(timea);
		*dest = env.mStart.x + (env.mEnd.x - env.mStart.x) * result;
		dest[1] = (env.mEnd.y - env.mStart.y) * result + env.mStart.y;
		dest[2] = result * (env.mEnd.z - env.mStart.z) + env.mStart.z;
	}
	else
	{
		v6 = this->mTable;
		if (this->mTable)
		{
			v9 = this->mRate.x;
			v10 = this->mRate.y;
			v11 = this->mRate.z;
			if (this->mIsCount)
			{
				v9 = v9 * oneOverDuration;
				v10 = v10 * oneOverDuration;
				v11 = oneOverDuration * v11;
			}
			resulta = v9 * time + this->mEnvOffset.x;
			resultb = v6->TableLookup(resulta);
			*dest = (env.mEnd.x - env.mStart.x) * resultb + env.mStart.x;
			enva = v10 * time + this->mEnvOffset.y;
			resultc = this->mTable->TableLookup(enva);
			dest[1] = (env.mEnd.y - env.mStart.y) * resultc + env.mStart.y;
			timeb = v11 * time + this->mEnvOffset.z;
			resultd = this->mTable->TableLookup(timeb);
			dest[2] = (env.mEnd.z - env.mStart.z) * resultd + env.mStart.z;
		}
		else
		{
			*dest = env.mStart.x;
			dest[1] = env.mStart.y;
			dest[2] = env.mStart.z;
		}
	}
}

void rvEnvParms::Finalize(void)
{
	bool v1; // zf

	v1 = this->mTable == 0;
	this->mFastLookUp = 0;
	if (!v1)
		this->mFastLookUp = this->mRate.y == this->mRate.x
		&& this->mRate.z == this->mRate.x
		&& this->mEnvOffset.y == this->mEnvOffset.x
		&& this->mEnvOffset.z == this->mEnvOffset.x;
}

void rvEnvParms1::Init(const rvEnvParms& copy, float duration) {
	mTable = copy.mTable;
	mEnvOffset = copy.mEnvOffset.x;
	mStart = 0.0f;
	mEnd = 0.0f;
	mRate = copy.mRate.x;
}

void rvEnvParms1::Evaluate(const float time, float* dest) {
	if (!dest) {
		return;
	}
	if (mTable) {
		const float value = mTable->TableLookup(mRate * time + mEnvOffset);
		*dest = mStart + (mEnd - mStart) * value;
	}
	else {
		*dest = mStart;
	}
}

void rvEnvParms2::Init(const rvEnvParms& copy, float duration) {
	mTable = copy.mTable;
	mEnvOffset.Set(copy.mEnvOffset.x, copy.mEnvOffset.y);
	mFastLookup = (copy.mRate.x == copy.mRate.y) && (copy.mEnvOffset.x == copy.mEnvOffset.y);
	mStart.Zero();
	mEnd.Zero();
	mRate.Set(copy.mRate.x, copy.mRate.y);
}

void rvEnvParms2::Evaluate(const float time, float* dest) {
	if (!dest) {
		return;
	}
	if (mTable) {
		if (mFastLookup) {
			const float value = mTable->TableLookup(mRate.x * time + mEnvOffset.x);
			dest[0] = mStart.x + (mEnd.x - mStart.x) * value;
			dest[1] = mStart.y + (mEnd.y - mStart.y) * value;
		}
		else {
			const float valueX = mTable->TableLookup(mRate.x * time + mEnvOffset.x);
			const float valueY = mTable->TableLookup(mRate.y * time + mEnvOffset.y);
			dest[0] = mStart.x + (mEnd.x - mStart.x) * valueX;
			dest[1] = mStart.y + (mEnd.y - mStart.y) * valueY;
		}
	}
	else {
		dest[0] = mStart.x;
		dest[1] = mStart.y;
	}
}

void rvEnvParms3::Init(const rvEnvParms& copy, float duration) {
	mTable = copy.mTable;
	mEnvOffset = copy.mEnvOffset;
	mFastLookup = (copy.mRate.x == copy.mRate.y) && (copy.mRate.x == copy.mRate.z) && (copy.mEnvOffset.x == copy.mEnvOffset.y) && (copy.mEnvOffset.x == copy.mEnvOffset.z);
	mStart.Zero();
	mEnd.Zero();
	mRate = copy.mRate;
}

void rvEnvParms3::Evaluate(const float time, float* dest) {
	if (!dest) {
		return;
	}
	if (mTable) {
		if (mFastLookup) {
			const float value = mTable->TableLookup(mRate.x * time + mEnvOffset.x);
			dest[0] = mStart.x + (mEnd.x - mStart.x) * value;
			dest[1] = mStart.y + (mEnd.y - mStart.y) * value;
			dest[2] = mStart.z + (mEnd.z - mStart.z) * value;
		}
		else {
			const float valueX = mTable->TableLookup(mRate.x * time + mEnvOffset.x);
			const float valueY = mTable->TableLookup(mRate.y * time + mEnvOffset.y);
			const float valueZ = mTable->TableLookup(mRate.z * time + mEnvOffset.z);
			dest[0] = mStart.x + (mEnd.x - mStart.x) * valueX;
			dest[1] = mStart.y + (mEnd.y - mStart.y) * valueY;
			dest[2] = mStart.z + (mEnd.z - mStart.z) * valueZ;
		}
	}
	else {
		dest[0] = mStart.x;
		dest[1] = mStart.y;
		dest[2] = mStart.z;
	}
}

void rvEnvParms1Particle::Init(const rvEnvParms& copy, float duration) {
	mStart = 0.0f;
	mEnd = 0.0f;
}

void rvEnvParms1Particle::Evaluate(const rvEnvParms& params, const float time, float oneOverDuration, float* dest) {
	const_cast<rvEnvParms&>(params).Evaluate(*this, time, oneOverDuration, dest);
}

void rvEnvParms2Particle::Init(const rvEnvParms& copy, float duration) {
	mStart.Zero();
	mEnd.Zero();
}

void rvEnvParms2Particle::Evaluate(const rvEnvParms& params, const float time, float oneOverDuration, float* dest) {
	const_cast<rvEnvParms&>(params).Evaluate(*this, time, oneOverDuration, dest);
}

void rvEnvParms3Particle::Init(const rvEnvParms& copy, float duration) {
	mStart.Zero();
	mEnd.Zero();
}

void rvEnvParms3Particle::Evaluate(const rvEnvParms& params, const float time, float oneOverDuration, float* dest) {
	const_cast<rvEnvParms&>(params).Evaluate(*this, time, oneOverDuration, dest);
}

void rvEnvParms3Particle::Rotate(const rvAngles& angles)
{
	this->mStart.x = angles.pitch + this->mStart.x;
	this->mStart.y = angles.yaw + this->mStart.y;
	this->mStart.z = angles.roll + this->mStart.z;
	this->mEnd.x = angles.pitch + this->mEnd.x;
	this->mEnd.y = this->mEnd.y + angles.yaw;
	this->mEnd.z = angles.roll + this->mEnd.z;
}

void rvEnvParms::SetDefaultType()
{
	if (!this->mTable)
	{
		this->mTable = (const idDeclTable*)declManager->FindType(
			DECL_TABLE,
			"linear",
			1);
		this->mIsCount = 1;
		this->mFastLookUp = 1;
	}
}
