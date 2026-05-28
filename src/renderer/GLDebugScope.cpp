// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "GLDebugScope.h"

static bool R_GLDebug_HasDebugOutput( void ) {
	return glConfig.backendCaps.hasDebugOutput;
}

bool R_GLDebugScope_Available( void ) {
	return R_GLDebug_HasDebugOutput() && glPushDebugGroup != NULL && glPopDebugGroup != NULL;
}

bool R_GLDebugObjectLabels_Available( void ) {
	return R_GLDebug_HasDebugOutput() && glObjectLabel != NULL;
}

void R_GLDebug_LabelObject( GLenum identifier, GLuint name, const char *label ) {
	if ( name == 0 || label == NULL || label[0] == '\0' || !R_GLDebugObjectLabels_Available() ) {
		return;
	}
	glObjectLabel( identifier, name, static_cast<GLsizei>( idStr::Length( label ) ), label );
}

void R_GLDebug_LabelBuffer( GLuint name, const char *label ) {
	R_GLDebug_LabelObject( GL_BUFFER, name, label );
}

void R_GLDebug_LabelTexture( GLuint name, const char *label ) {
	R_GLDebug_LabelObject( GL_TEXTURE, name, label );
}

void R_GLDebug_LabelFramebuffer( GLuint name, const char *label ) {
	R_GLDebug_LabelObject( GL_FRAMEBUFFER, name, label );
}

void R_GLDebug_LabelProgram( GLuint name, const char *label ) {
	R_GLDebug_LabelObject( GL_PROGRAM, name, label );
}

void R_GLDebug_LabelVertexArray( GLuint name, const char *label ) {
	R_GLDebug_LabelObject( GL_VERTEX_ARRAY, name, label );
}

void R_GLDebug_LabelSampler( GLuint name, const char *label ) {
	R_GLDebug_LabelObject( GL_SAMPLER, name, label );
}

idGLDebugScope::idGLDebugScope( const char *name, unsigned int id ) {
	active = false;
	if ( name == NULL || name[0] == '\0' || !R_GLDebugScope_Available() ) {
		return;
	}
	glPushDebugGroup( GL_DEBUG_SOURCE_APPLICATION, static_cast<GLuint>( id ), static_cast<GLsizei>( idStr::Length( name ) ), name );
	active = true;
}

idGLDebugScope::~idGLDebugScope() {
	if ( active && glPopDebugGroup != NULL ) {
		glPopDebugGroup();
	}
}
