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
#include <assert.h>

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
		"%s%s%s expects argument %i to be %s, got %s",
		CName, (CName?"->":""), FName,
		Num,
		SpiderScript_GetTypeName(Script, Expected),
		SpiderScript_GetTypeName(Script, Got)
		);
}

int SpiderScript_ThrowException_ArgError(tSpiderScript *Script, const char *CName, const char *Name, int Num,
	tSpiderTypeRef Expected, tSpiderTypeRef Got)
{
	return SpiderScript_ThrowException(Script, SS_EXCEPTION_ARGUMENT,
		"%s%s%s expects argument %i to be %s, got %s",
		CName, (CName?"->":""), Name,
		Num,
		SpiderScript_GetTypeName(Script, Expected),
		SpiderScript_GetTypeName(Script, Got)
		);
}

int SpiderScript_ThrowException_NullRef(tSpiderScript *Script, const char *Location)
{
	return SpiderScript_ThrowException(Script, SS_EXCEPTION_NULLDEREF, "%s", Location);
}

int SpiderScript_ThrowException_ForceExit(tSpiderScript *Script, int ExitCode)
{
	return SpiderScript_ThrowException(Script, SS_EXCEPTION_FORCEEXIT, "exit(%i)", ExitCode);
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

const tSpiderBacktrace *SpiderScript_GetBacktrace(tSpiderScript *Script, int *Size)
{
	*Size = Script->BacktraceSize;
	return Script->Backtrace;
}

void SpiderScript_PushBacktrace(tSpiderScript *Script,
	const char *FcnName, size_t Ofs, const char *FileName, unsigned int Line)
{
	assert( Script->BacktraceSize >= 0 );
	assert( Script->BacktraceSize <= MAX_BACKTRACE_SIZE );
	if( Script->BacktraceSize != MAX_BACKTRACE_SIZE )
	{
		Script->Backtrace[Script->BacktraceSize].Function = FcnName;
		Script->Backtrace[Script->BacktraceSize].Offset = Ofs;
		Script->Backtrace[Script->BacktraceSize].File = FileName;
		Script->Backtrace[Script->BacktraceSize].Line = Line;
		Script->BacktraceSize ++;
	}
}

void SpiderScript_SetCatchTarget(tSpiderScript *Script, jmp_buf *Target, jmp_buf *OldTargetSaved)
{
	// TODO: Have this?
}

void SpiderScript_ClearException(tSpiderScript *Script)
{
	Script->BacktraceSize = 0;
	Script->CurException = 0;
	if( Script->CurExceptionString )
		free( Script->CurExceptionString );
}


