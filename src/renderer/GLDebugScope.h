// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __GL_DEBUG_SCOPE_H__
#define __GL_DEBUG_SCOPE_H__

#include "qgl.h"

bool R_GLDebugScope_Available( void );
bool R_GLDebugObjectLabels_Available( void );

void R_GLDebug_LabelObject( GLenum identifier, GLuint name, const char *label );
void R_GLDebug_LabelBuffer( GLuint name, const char *label );
void R_GLDebug_LabelTexture( GLuint name, const char *label );
void R_GLDebug_LabelFramebuffer( GLuint name, const char *label );
void R_GLDebug_LabelProgram( GLuint name, const char *label );
void R_GLDebug_LabelVertexArray( GLuint name, const char *label );
void R_GLDebug_LabelSampler( GLuint name, const char *label );

class idGLDebugScope {
public:
	idGLDebugScope( const char *name, unsigned int id = 0 );
	~idGLDebugScope();

private:
	idGLDebugScope( const idGLDebugScope & );
	idGLDebugScope &operator=( const idGLDebugScope & );

	bool active;
};

#endif /* !__GL_DEBUG_SCOPE_H__ */
