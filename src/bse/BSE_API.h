#ifndef _BSE_API_H_INC_
#define _BSE_API_H_INC_

#include "BSEInterface.h"

class idDecl;

typedef idDecl* (*BSE_AllocDeclEffect_t)(void);

rvBSEManager*				OpenQ4_GetIntegratedBSEManager( void );
rvDeclEffectEdit*			OpenQ4_GetIntegratedBSEDeclEffectEdit( void );
idDecl*						OpenQ4_AllocIntegratedBSEDeclEffect( void );
bool						OpenQ4_IsIntegratedBSEDeclEffect( const idDecl *decl );

extern BSE_AllocDeclEffect_t	bseAllocDeclEffect;

#endif // _BSE_API_H_INC_
