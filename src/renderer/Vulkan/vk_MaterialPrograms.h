// Copyright (C) 2026 DarkMatter Productions
#ifndef OPENQ4_VK_MATERIALPROGRAMS_H
#define OPENQ4_VK_MATERIALPROGRAMS_H
#include "../materialprogram/GLSLCompiler.h"
#include "../materialprogram/ARBTranslator.h"
#include <vector>

vkGLSLProgramFamily_t VK_MaterialPrograms_NativeFamily( const char *program );
bool VK_MaterialPrograms_Validate( const newShaderStage_t *stage );
bool VK_MaterialPrograms_NeedsStencil( const idMaterial *material, const float *registers );
// Advances when a reload or shutdown discards the authored/native source
// selection, so a cached NeedsStencil verdict must be recomputed.
int VK_MaterialPrograms_NeedsStencilGeneration();
bool VK_Exec_BuildAuthoredMaterialUniforms( const viewDef_t *viewDef, const drawSurf_t *surf,
    const shaderStage_t *stage, const drawInteraction_t *interaction, oq4material::UniformBlock &block );
bool VK_MaterialPrograms_Bind( const newShaderStage_t *stage,
    const oq4material::UniformBlock &uniforms, int stateBits, bool separateColor,
    const drawInteraction_t *interaction = NULL );

// Authored ARB assembly. File names are the glprogs-relative names the
// material declared; either may be NULL for the fixed-function stage it
// replaces. The parameter list says which uniform slot carries which
// program.local/program.env value. NULL means the pair cannot be drawn.
const std::vector<oq4material::ARBParameter> *VK_MaterialPrograms_ARBParameters(
    const char *vertexFile, const char *fragmentFile );
bool VK_MaterialPrograms_BindARB( const char *vertexFile, const char *fragmentFile,
    const newShaderStage_t *stage, const oq4material::UniformBlock &uniforms, int stateBits, bool separateColor );
// The declared glprogs file name of a registered ARB program (vk_Backend.cpp).
const char *VK_MaterialProgramFileName( unsigned int target, unsigned int handle );
// Whether one ARB program, on its own, translates for this backend.
bool VK_MaterialPrograms_ARBTranslatable( unsigned int target, const char *file );

// True the first time a subject/reason pair is seen since the last reload, so
// a rejected or approximated stage is reported once instead of every frame.
// The set is bounded; once full, later pairs are counted as suppressed.
bool VK_MaterialPrograms_ReportOnce( const char *subject, const char *reason );

// reloadGLSLprograms/reloadARBprograms: versions are re-validated on next use;
// unchanged sources reuse their version. The table is bounded: a new version
// evicts the least recently drawn stale one that no frame in flight uses.
void VK_MaterialPrograms_Reload();
// reportShaderPrograms: every version with its state, diagnostic, draws and
// skips since the last reload, plus capacity and suppression counters.
void VK_MaterialPrograms_Report( bool detailed = true );
void VK_MaterialPrograms_BeginFrame( int slot );
void VK_MaterialPrograms_Shutdown();

#endif
