/*
 * SpiderScript Library
 * by John Hodge (thePowersGang)
 * 
 * exceptions.c
 * - Exception handling
 *
 * TODO: Allow throwing of objects?
 * TODO: OR have 'custom' exception types
 */
#include <spiderscript.h>
#include "common.h"
#include <stdlib.h>
#include <stdarg.h>

// === CODE ===
int SpiderScript_ThrowException_ArgCountC(tSpiderScript *Script, const char *CName, const char *FName,
	int Exp, int Got)
{
	return SpiderScript_ThrowException(Script, SS_EXCEPTION_ARGUMENT,
		"%s->%s expects %i%s arguments, %i given", CName, FName, abs(Exp), (Exp<0?"+":""), Got);
}
int SpiderScript_ThrowException_ArgCount(tSpiderScript *Script, const char *Name, int Exp, int Got)
{
	return SpiderScript_ThrowException(Script, SS_EXCEPTION_ARGUMENT,
		"%s expects %i%s arguments, %i given", Name, abs(Exp), (Exp<0?"+":""), Got);
}

int SpiderScript_ThrowException_ArgErrorC(tSpiderScript *Script, const char *CName, const char *FName, int Num,
	tSpiderTypeRef Expected, tSpiderTypeRef Got)
{
	return SpiderScript_ThrowException(Script, SS_EXCEPTION_ARGUMENT,
		"%s->%s expects argument %i to be %s, got %s",
		CName, FName,
		Num,
		SpiderScript_GetTypeName(Script, Expected),
		SpiderScript_GetTypeName(Script, Got)
		);
}

int SpiderScript_ThrowException_ArgError(tSpiderScript *Script, const char *Name, int Num,
	tSpiderTypeRef Expected, tSpiderTypeRef Got)
{
	return SpiderScript_ThrowException(Script, SS_EXCEPTION_ARGUMENT,
		"%s expects argument %i to be %s, got %s",
		Name,
		Num,
		SpiderScript_GetTypeName(Script, Expected),
		SpiderScript_GetTypeName(Script, Got)
		);
}

int SpiderScript_ThrowException(tSpiderScript *Script, int ExceptionID, char *Message, ...)
{
	if( Script->CurException ) {
		// TODO: Should anything happen when an exception is thrown before the last is cleared?
		if( Script->CurExceptionString )
			free( Script->CurExceptionString );
	}
	
	Script->CurException = ExceptionID;
	va_list	args;
	va_start(args, Message);	
	Script->CurExceptionString = mkstrv(Message, args);
	va_end(args);
	
	return -1;
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


