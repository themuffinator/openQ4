// Copyright (C) 2026 DarkMatter Productions
#ifndef OPENQ4_PBR_NATIVE_CONTRACT_H
#define OPENQ4_PBR_NATIVE_CONTRACT_H

// Per-surface admission for the native PBR owners: the OpenGL classic light
// loop and the native Vulkan interaction renderer. A native owner replaces
// the final classic interaction submit of each light and adds one environment
// draw per surface, so it may only take a surface whose classic interaction
// topology, coverage contract and PBR resources are all proven. Everything
// here is backend-neutral: image readiness uses the public device handle.

class idImage;
class idMaterial;
struct drawSurf_s;
typedef struct drawSurf_s drawSurf_t;

typedef struct pbrNativeMaterial_s {
	idImage *	normalImage;	// PBR normal, or _flat when the material has none
	idImage *	albedoImage;	// sRGB color with linear coverage alpha
	idImage *	dataImage;		// packed ORM, else separate roughness, else _white
	idImage *	metallicImage;	// separate metallic, NULL otherwise
	idImage *	aoImage;		// separate AO, NULL with ORM or scalar AO
	idImage *	emissiveImage;	// sRGB emission, NULL without emission
	int			dataFlags;		// 1 packed ORM, 2 separate metallic, 4 separate roughness
	int			normalFormat;	// 0 flat; otherwise pbrNormalFormat_t + 1
	float		metallic;
	float		roughness;
	float		normalScale;
	float		ao;
	float		emissiveColor[3];
	// Parallax occlusion mapping (REN-11): when set, dataImage carries the
	// height in its alpha and a height h lies at h * parallaxScale +
	// parallaxBias texture-coordinate units above the polygon. Every pass that
	// lights the surface traces the same ray, so the passes stay aligned.
	bool		parallax;
	float		parallaxScale;
	float		parallaxBias;
} pbrNativeMaterial_t;

float	R_PBRNative_RegisterValue( const drawSurf_t *surf, int registerIndex, float fallback );
bool	R_PBRNative_ImageReady( const idImage *image, int expectedUsage );

// Exactly one active classic bump -> diffuse -> specular sequence, so the
// classic decomposition submits once per light stage and the PBR draw can
// replace that submit. Declared duplicates are rejected even when inactive.
bool	R_PBRNative_HasSingleClassicInteractionTopology( const drawSurf_t *surf );

// A perforated material's depth fill owns its coverage. The single alpha-test
// stage must sample the PBR albedo itself with untransformed coordinates.
bool	R_PBRNative_PerforatedCoverageMatches( const drawSurf_t *surf );

// The one authored source-alpha blend stage of a translucent PBR material,
// provably sampling the PBR albedo; its alpha register scales the coverage.
bool	R_PBRNative_TransparentStage( const drawSurf_t *surf, int *stageIndexOut, float *alphaScaleOut );

// Complete material admission. translucentAdmitted is the owner's own view
// decision for ordered transparency; owners without it pass false.
bool	R_PBRNative_Material( const drawSurf_t *surf, bool translucentAdmitted, pbrNativeMaterial_t &out );

// True when stageIndex is the classic additive glow stage the native owner
// replaces with typed linear emission.
bool	R_PBRNative_IsReplacedEmissionStage( const drawSurf_t *surf, int stageIndex );

// Parallax for an admitted PBR surface with this data layout: the height must
// ride in its bound ORM image. Counts the admission for the report.
void	R_PBRNative_ResolveParallax( const drawSurf_t *surf, int dataFlags, const idImage *dataImage,
		bool &parallax, float &scale, float &bias );
// The shader inputs of a parallax surface, identical on both backends:
// params = ( scale, bias, most samples, self-shadow ), all zero when flat;
// fade = ( fade start, fade end, 0, 0 ) in world units.
void	R_PBRNative_ParallaxParameters( const pbrNativeMaterial_t &material, float params[4], float fade[4] );
// The same inputs for the retail Parallaxbump guide (Vulkan classic path):
// its authored scale and bias, the ray when r_parallaxOcclusion is set.
void	R_Parallax_RetailParameters( float scale, float bias, float params[4], float fade[4] );
// Per-frame accounting for the requested/effective report (gfxInfo).
void	R_Parallax_CountDraw( void );
void	R_Parallax_PrintInfo( void );

#endif
