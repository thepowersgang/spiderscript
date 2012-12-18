/*
* SpiderScript Library
* by John Hodge (thePowersGang)
* 
* exceptions.c
* - Exception handling
*/
#include <spiderscript.h>
#include "common.h"
#include <stdlib.h>

// === CODE ===
int SpiderScript_ThrowException_ArgError(tSpiderScript *Script, const char *Name, int Num, int Expected, int Got)
{
	char *msg = mkstr("%s Arg %i - Expected %s, got %s",
		Name,
		SpiderScript_GetTypeName(Script, Expected),
		SpiderScript_GetTypeName(Script, Got)
		);
	return SpiderScript_ThrowException(Script, SS_EXCEPTION_ARGUMENT, msg);
}

int SpiderScript_ThrowException(tSpiderScript *Script, int ExceptionID, char *Message)
{
	if( Script->CurException ) {
		// TODO: Should anything happen when an exception is thrown before the last is cleared?
		if( Script->CurExceptionString )
			free( Script->CurExceptionString );
	}
	
	Script->CurException = ExceptionID;
	Script->CurExceptionString = Message;
	
	// TODO: longjmp to target?
	return 0;
}

int SpiderScript_GetException(tSpiderScript *Script, const char **Message)
{
	if( Message )
		*Message = Script->CurExceptionString;
	return Script->CurException;
}

void SpiderScript_SetCatchTarget(tSpiderScript *Script, jmp_buf *Target, jmp_buf *OldTargetSaved)
{
	// TODO: Have this?
}

void SpiderScript_ClearException(tSpiderScript *Script)
{
	Script->CurException = 0;
	if( Script->CurExceptionString )
		free( Script->CurExceptionString );
}


