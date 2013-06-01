/*
 * Acess2 - SpiderScript
 * Interpreter Library
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <spiderscript.h>
#include "common.h"
#include "ast.h"
#include "bytecode_gen.h"
#include <stdarg.h>

// === IMPORTS ===
extern int	Parse_Buffer(tSpiderScript *Script, const char *Buffer, const char *Filename);
extern int	SpiderScript_int_LoadBytecode(tSpiderScript *Script, const char *Name);

// === PROTOTYPES ===

// === CODE ===
/**
 * \brief Library Entry Point
 */
int SoMain()
{
	return 0;
}

/**
 * \brief Parse a script
 */
tSpiderScript *SpiderScript_ParseFile(tSpiderVariant *Variant, const char *Filename)
{
	char	*data;
	 int	fLen;
	FILE	*fp;
	tSpiderScript	*ret;
	
	fp = fopen(Filename, "r");
	if( !fp ) {
		return NULL;
	}
	
	fseek(fp, 0, SEEK_END);
	fLen = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	
	// Allocate and read data
	data = malloc(fLen + 1);
	if(!data)	return NULL;
	fLen = fread(data, 1, fLen, fp);
	fclose(fp);
	if( fLen < 0 ) {
		free(data);
		return NULL;
	}
	data[fLen] = '\0';
	
	
	// Create the script
	ret = calloc(1,sizeof(tSpiderScript));
	ret->Variant = Variant;
	
	if( Parse_Buffer(ret, data, Filename) ) {
		free(data);
		SpiderScript_Free( ret );
		return NULL;
	}
	free(data);

	// TODO: Create function/class arrays (instead of linked lists)

	// Convert the script into (parsed) bytecode	
	if( SpiderScript_BytecodeScript(ret) != 0 ) {
		SpiderScript_Free(ret);
		return NULL;
	}
	
	return ret;
}

tSpiderScript *SpiderScript_LoadBytecode(tSpiderVariant *Variant, const char *Filename)
{
	tSpiderScript *ret = malloc(sizeof(tSpiderScript));
	ret->Variant = Variant;
	ret->Functions = NULL;
	ret->FirstClass = NULL;

	if( SpiderScript_int_LoadBytecode(ret, Filename) ) {
		SpiderScript_Free(ret);
		return NULL;
	}
	
	return ret;
}

/**
 * \brief Free a script
 */
void SpiderScript_Free(tSpiderScript *Script)
{
	tScript_Function *fcn;
	tScript_Class	*sc;
	void	*n;
	
	// Free functions
 	for( fcn = Script->Functions; fcn; fcn = n )
	{
		if(fcn->ASTFcn)	AST_FreeNode( fcn->ASTFcn );
		if(fcn->BCFcn)	Bytecode_DeleteFunction( fcn->BCFcn );

		n = fcn->Next;
		free( fcn );
	}

	for(sc = Script->FirstClass; sc; sc = n)
	{
		tScript_Var *at;
		for( at = sc->FirstProperty; at; at = n )
		{
			n = at->Next;
			free(at);
		}
		
		for( fcn = sc->FirstFunction; fcn; fcn = n )
		{
			if(fcn->ASTFcn)	AST_FreeNode( fcn->ASTFcn );
			if(fcn->BCFcn)	Bytecode_DeleteFunction( fcn->BCFcn );
			n = fcn->Next;
			free(fcn);
		}
		n = sc->Next;
		free(sc);
	}	
	
	for( tScript_Var *var = Script->FirstGlobal; var; var = n )
	{
		n = var->Next;
		if( SS_GETARRAYDEPTH(var->Type) )
			SpiderScript_DereferenceArray(var->Ptr);
		else if( SS_ISTYPEOBJECT(var->Type) )
			SpiderScript_DereferenceObject(var->Ptr);
		else if( var->Type.Def == &gSpiderScript_StringType )
			SpiderScript_DereferenceString(var->Ptr);
		else
			;
		free(var);
	}
	Script->FirstGlobal = NULL;
	Script->LastGlobal = NULL;

	free(Script);
}

void SpiderScript_RuntimeError(tSpiderScript *Script, const char *Format, ...)
{
	va_list	args;
	va_start(args, Format);
	char *msg = mkstrv(Format, args);
	va_end(args);

	if( Script->Variant->HandleError )
		Script->Variant->HandleError(Script, msg);
	else
		fprintf(stderr, "Runtime Error: %s\n", msg);

	free(msg);
}

char *mkstrv(const char *format, va_list args)
{
	va_list args_saved;
	
	va_copy(args_saved, args);
	int len = vsnprintf(NULL, 0, format, args_saved);
	va_end(args_saved);
	
	char *ret = malloc(len + 1);
	vsnprintf(ret, len+1, format, args);
	
	return ret;
}

char *mkstr(const char *format, ...)
{
	va_list	args;
	va_start(args, format);
	char *ret = mkstrv(format, args);
	va_end(args);
	return ret;
}

