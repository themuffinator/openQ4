// Copyright (C) 2007 Id Software, Inc.
//

#include "BSE_Envelope.h"
#include "BSE_Particle.h"
#include "BSE_SpawnDomains.h"
#include "BSE.h"

#include <math.h>
#include <string.h>

namespace {
ID_INLINE float SpawnRandom01() {
	return rvRandom::flrand(0.0f, 1.0f);
}

ID_INLINE float SpawnLerp(float a, float b, float t) {
	return a + (b - a) * t;
}

ID_INLINE void SpawnGetNormalInternal(idVec3* normal, const idVec3& point, const idVec3* centre) {
	if (!normal) {
		return;
	}

	if (centre) {
		*normal = point - *centre;
	}
	else {
		*normal = point;
	}

	const float lenSqr = normal->LengthSqr();
	if (lenSqr > 1e-6f) {
		normal->NormalizeFast();
	}
}

ID_INLINE idVec3 SpawnBoxPoint(const rvParticleParms& parms) {
	return idVec3(
		rvRandom::flrand(parms.mMins.x, parms.mMaxs.x),
		rvRandom::flrand(parms.mMins.y, parms.mMaxs.y),
		rvRandom::flrand(parms.mMins.z, parms.mMaxs.z));
}

ID_INLINE idVec3 SpawnSurfaceBoxPoint(const rvParticleParms& parms) {
	idVec3 result = SpawnBoxPoint(parms);

	switch (rvRandom::irand(0, 5)) {
	case 0:
		result.x = parms.mMins.x;
		break;
	case 1:
		result.x = parms.mMaxs.x;
		break;
	case 2:
		result.y = parms.mMins.y;
		break;
	case 3:
		result.y = parms.mMaxs.y;
		break;
	case 4:
		result.z = parms.mMins.z;
		break;
	default:
		result.z = parms.mMaxs.z;
		break;
	}

	return result;
}

ID_INLINE idVec3 SpawnSpherePoint(const rvParticleParms& parms, bool surfaceOnly) {
	const idVec3 centre = (parms.mMins + parms.mMaxs) * 0.5f;
	const idVec3 radius = (parms.mMaxs - parms.mMins) * 0.5f;

	const float z = rvRandom::flrand(-1.0f, 1.0f);
	const float phi = rvRandom::flrand(0.0f, idMath::TWO_PI);
	const float radial = surfaceOnly ? 1.0f : SpawnRandom01();
	const float ring = idMath::Sqrt(Max(0.0f, 1.0f - z * z));

	idVec3 dir(ring * idMath::Cos(phi), ring * idMath::Sin(phi), z);
	return idVec3(
		centre.x + dir.x * radius.x * radial,
		centre.y + dir.y * radius.y * radial,
		centre.z + dir.z * radius.z * radial);
}

ID_INLINE idVec3 SpawnCylinderPoint(const rvParticleParms& parms, bool surfaceOnly) {
	const float t = SpawnRandom01();
	const float phi = rvRandom::flrand(0.0f, idMath::TWO_PI);
	const float radial = surfaceOnly ? 1.0f : SpawnRandom01();

	const float x = SpawnLerp(parms.mMins.x, parms.mMaxs.x, t);
	const float cy = 0.5f * (parms.mMins.y + parms.mMaxs.y);
	const float cz = 0.5f * (parms.mMins.z + parms.mMaxs.z);
	const float ry = 0.5f * (parms.mMaxs.y - parms.mMins.y);
	const float rz = 0.5f * (parms.mMaxs.z - parms.mMins.z);

	return idVec3(x, cy + idMath::Cos(phi) * ry * radial, cz + idMath::Sin(phi) * rz * radial);
}
}

TSpawnFunc rvParticleParms::spawnFunctions[SPF_COUNT] = {
	/*00*/ &SpawnStub,
	/*01*/ &SpawnNone1,
	/*02*/ &SpawnNone2,
	/*03*/ &SpawnNone3,
	/*04*/ &SpawnStub,
	/*05*/ &SpawnOne1,
	/*06*/ &SpawnOne2,
	/*07*/ &SpawnOne3,
	/*08*/ &SpawnStub,
	/*09*/ &SpawnPoint1,
	/*10*/ &SpawnPoint2,
	/*11*/ &SpawnPoint3,
	/*12*/ &SpawnStub,
	/*13*/ &SpawnLinear1,
	/*14*/ &SpawnLinear2,
	/*15*/ &SpawnLinear3,
	/*16*/ &SpawnStub,
	/*17*/ &SpawnBox1,
	/*18*/ &SpawnBox2,
	/*19*/ &SpawnBox3,
	/*20*/ &SpawnStub,
	/*21*/ &SpawnSurfaceBox1,
	/*22*/ &SpawnSurfaceBox2,
	/*23*/ &SpawnSurfaceBox3,
	/*24*/ &SpawnStub,
	/*25*/ &SpawnBox1,
	/*26*/ &SpawnSphere2,
	/*27*/ &SpawnSphere3,
	/*28*/ &SpawnStub,
	/*29*/ &SpawnSurfaceBox1,
	/*30*/ &SpawnSurfaceSphere2,
	/*31*/ &SpawnSurfaceSphere3,
	/*32*/ &SpawnStub,
	/*33*/ &SpawnBox1,
	/*34*/ &SpawnSphere2,
	/*35*/ &SpawnCylinder3,
	/*36*/ &SpawnStub,
	/*37*/ &SpawnSurfaceBox1,
	/*38*/ &SpawnSurfaceSphere2,
	/*39*/ &SpawnSurfaceCylinder3,
	/*40*/ &SpawnStub,
	/*41*/ &SpawnStub,
	/*42*/ &SpawnSpiral2,
	/*43*/ &SpawnSpiral3,
	/*44*/ &SpawnStub,
	/*45*/ &SpawnStub,
	/*46*/ &SpawnStub,
	/*47*/ &SpawnModel3,
};

void sdModelInfo::CalculateSurfRemap(void) {
	for (int i = 0; i < NUM_SURF_REMAP; ++i) {
		surfRemap[i] = 0;
	}

	if (!model || model->NumSurfaces() <= 0) {
		return;
	}

	int totalTris = 0;
	for (int i = 0; i < model->NumSurfaces(); ++i) {
		const modelSurface_t* surf = model->Surface(i);
		if (!surf || !surf->geometry) {
			continue;
		}
		totalTris += surf->geometry->numIndexes / 3;
	}

	if (totalTris <= 0) {
		return;
	}

	int slot = 0;
	for (int i = 0; i < model->NumSurfaces() && slot < NUM_SURF_REMAP; ++i) {
		const modelSurface_t* surf = model->Surface(i);
		if (!surf || !surf->geometry) {
			continue;
		}

		const int tris = surf->geometry->numIndexes / 3;
		const int count = Max(1, idMath::FtoiFast((float)NUM_SURF_REMAP * ((float)tris / (float)totalTris) + 0.5f));
		for (int j = 0; j < count && slot < NUM_SURF_REMAP; ++j) {
			surfRemap[slot++] = i;
		}
	}

	while (slot < NUM_SURF_REMAP) {
		surfRemap[slot] = surfRemap[slot > 0 ? slot - 1 : 0];
		++slot;
	}
}

bool rvParticleParms::Compare(const rvParticleParms& comp) const {
	if (mSpawnType != comp.mSpawnType || mFlags != comp.mFlags) {
		return false;
	}

	if ((mModelInfo == NULL) != (comp.mModelInfo == NULL)) {
		return false;
	}
	if (mModelInfo && comp.mModelInfo && mModelInfo->model != comp.mModelInfo->model) {
		return false;
	}

	const float eps = 0.001f;
	if (!mMins.Compare(comp.mMins, eps) || !mMaxs.Compare(comp.mMaxs, eps)) {
		return false;
	}

	return idMath::Fabs(mRange - comp.mRange) <= eps;
}

void rvParticleParms::HandleRelativeParms(float* death, float* init, int count) {
	if ((mFlags & PPFLAG_RELATIVE) == 0 || death == NULL || init == NULL) {
		return;
	}

	for (int i = 0; i < count; ++i) {
		death[i] += init[i];
	}
}

void rvParticleParms::GetMinsMaxs(idVec3& mins, idVec3& maxs) {
	mins.Zero();
	maxs.Zero();

	switch (mSpawnType & ~0x3) {
	case SPF_ONE_0:
		mins.Set(1.0f, 1.0f, 1.0f);
		maxs = mins;
		break;
	case SPF_POINT_0:
		mins = mMins;
		maxs = mMins;
		break;
	case SPF_LINEAR_0:
	case SPF_BOX_0:
	case SPF_SURFACE_BOX_0:
	case SPF_SPHERE_0:
	case SPF_SURFACE_SPHERE_0:
	case SPF_CYLINDER_0:
	case SPF_SURFACE_CYLINDER_0:
	case SPF_SPIRAL_0:
	case SPF_MODEL_0:
		mins = mMins;
		maxs = mMaxs;
		break;
	default:
		break;
	}
}

void SpawnStub(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	if (result) {
		result[0] = 0.0f;
		result[1] = 0.0f;
		result[2] = 0.0f;
	}
	if (normal) {
		normal->Set(0.0f, 0.0f, 1.0f);
	}
}

void SpawnNone1(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	result[0] = 0.0f;
}

void SpawnNone2(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	result[0] = 0.0f;
	result[1] = 0.0f;
}

void SpawnNone3(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	idVec3& out = *reinterpret_cast<idVec3*>(result);
	out.Zero();
	SpawnGetNormalInternal(normal, out, centre);
}

void SpawnOne1(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	result[0] = 1.0f;
}

void SpawnOne2(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	result[0] = 1.0f;
	result[1] = 1.0f;
}

void SpawnOne3(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	idVec3& out = *reinterpret_cast<idVec3*>(result);
	out.Set(1.0f, 1.0f, 1.0f);
	SpawnGetNormalInternal(normal, out, centre);
}

void SpawnPoint1(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	result[0] = parms.mMins.x;
}

void SpawnPoint2(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	result[0] = parms.mMins.x;
	result[1] = parms.mMins.y;
}

void SpawnPoint3(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	idVec3& out = *reinterpret_cast<idVec3*>(result);
	out = parms.mMins;
	SpawnGetNormalInternal(normal, out, centre);
}

void SpawnLinear1(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	result[0] = SpawnLerp(parms.mMins.x, parms.mMaxs.x, SpawnRandom01());
}

void SpawnLinear2(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	const float t = (parms.mFlags & PPFLAG_LINEARSPACING) ? result[0] : SpawnRandom01();
	result[0] = SpawnLerp(parms.mMins.x, parms.mMaxs.x, t);
	result[1] = SpawnLerp(parms.mMins.y, parms.mMaxs.y, t);
}

void SpawnLinear3(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	idVec3& out = *reinterpret_cast<idVec3*>(result);
	const float t = (parms.mFlags & PPFLAG_LINEARSPACING) ? out.x : SpawnRandom01();
	out.x = SpawnLerp(parms.mMins.x, parms.mMaxs.x, t);
	out.y = SpawnLerp(parms.mMins.y, parms.mMaxs.y, t);
	out.z = SpawnLerp(parms.mMins.z, parms.mMaxs.z, t);
	SpawnGetNormalInternal(normal, out, centre);
}

void SpawnBox1(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	result[0] = rvRandom::flrand(parms.mMins.x, parms.mMaxs.x);
}

void SpawnBox2(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	result[0] = rvRandom::flrand(parms.mMins.x, parms.mMaxs.x);
	result[1] = rvRandom::flrand(parms.mMins.y, parms.mMaxs.y);
}

void SpawnBox3(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	idVec3& out = *reinterpret_cast<idVec3*>(result);
	out = SpawnBoxPoint(parms);
	SpawnGetNormalInternal(normal, out, centre);
}

void SpawnSurfaceBox1(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	result[0] = rvRandom::irand(0, 1) ? parms.mMins.x : parms.mMaxs.x;
}

void SpawnSurfaceBox2(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	idVec3 sample = SpawnSurfaceBoxPoint(parms);
	result[0] = sample.x;
	result[1] = sample.y;
}

void SpawnSurfaceBox3(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	idVec3& out = *reinterpret_cast<idVec3*>(result);
	out = SpawnSurfaceBoxPoint(parms);
	SpawnGetNormalInternal(normal, out, centre);
}

void SpawnSphere1(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	result[0] = SpawnSpherePoint(parms, false).x;
}

void SpawnSphere2(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	idVec3 p = SpawnSpherePoint(parms, false);
	result[0] = p.x;
	result[1] = p.y;
}

void SpawnSphere3(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	idVec3& out = *reinterpret_cast<idVec3*>(result);
	out = SpawnSpherePoint(parms, false);
	SpawnGetNormalInternal(normal, out, centre);
}

void SpawnSurfaceSphere1(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	result[0] = SpawnSpherePoint(parms, true).x;
}

void SpawnSurfaceSphere2(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	idVec3 p = SpawnSpherePoint(parms, true);
	result[0] = p.x;
	result[1] = p.y;
}

void SpawnSurfaceSphere3(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	idVec3& out = *reinterpret_cast<idVec3*>(result);
	out = SpawnSpherePoint(parms, true);
	SpawnGetNormalInternal(normal, out, centre);
}

void SpawnCylinder3(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	idVec3& out = *reinterpret_cast<idVec3*>(result);
	out = SpawnCylinderPoint(parms, false);
	SpawnGetNormalInternal(normal, out, centre);
}

void SpawnSurfaceCylinder3(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	idVec3& out = *reinterpret_cast<idVec3*>(result);
	out = SpawnCylinderPoint(parms, true);
	SpawnGetNormalInternal(normal, out, centre);
}

void SpawnSpiral2(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	if (parms.mFlags & PPFLAG_LINEARSPACING) {
		result[0] = SpawnLerp(parms.mMins.x, parms.mMaxs.x, result[0]);
	}
	else {
		result[0] = rvRandom::flrand(parms.mMins.x, parms.mMaxs.x);
	}

	const float range = (idMath::Fabs(parms.mRange) > BSE_TIME_EPSILON) ? parms.mRange : 1.0f;
	const float theta = idMath::TWO_PI * (result[0] / range);
	result[1] = idMath::Cos(theta) * rvRandom::flrand(parms.mMins.y, parms.mMaxs.y);
}

void SpawnSpiral3(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	idVec3& out = *reinterpret_cast<idVec3*>(result);
	if (parms.mFlags & PPFLAG_LINEARSPACING) {
		out.x = SpawnLerp(parms.mMins.x, parms.mMaxs.x, out.x);
	}
	else {
		out.x = rvRandom::flrand(parms.mMins.x, parms.mMaxs.x);
	}

	const float range = (idMath::Fabs(parms.mRange) > BSE_TIME_EPSILON) ? parms.mRange : 1.0f;
	const float theta = idMath::TWO_PI * (out.x / range);
	const float c = idMath::Cos(theta);
	const float s = idMath::Sin(theta);
	const float ry = rvRandom::flrand(parms.mMins.y, parms.mMaxs.y);
	const float rz = rvRandom::flrand(parms.mMins.z, parms.mMaxs.z);

	out.y = c * ry - s * rz;
	out.z = c * rz + s * ry;

	if (normal) {
		normal->x = centre ? 0.0f : out.x;
		normal->y = out.y;
		normal->z = out.z;

		const float lenSqr = normal->LengthSqr();
		if (lenSqr > 1e-6f) {
			normal->NormalizeFast();
		}
		else {
			normal->Set(0.0f, 0.0f, 1.0f);
		}
	}
}

void SpawnModel3(float* result, const rvParticleParms& parms, idVec3* normal, const idVec3* centre) {
	idVec3& out = *reinterpret_cast<idVec3*>(result);

	if (parms.mModelInfo && parms.mModelInfo->model) {
		idBounds bounds = parms.mModelInfo->model->Bounds(NULL);
		out.x = rvRandom::flrand(bounds[0].x, bounds[1].x);
		out.y = rvRandom::flrand(bounds[0].y, bounds[1].y);
		out.z = rvRandom::flrand(bounds[0].z, bounds[1].z);
	}
	else {
		out = SpawnBoxPoint(parms);
	}

	SpawnGetNormalInternal(normal, out, centre);
}
