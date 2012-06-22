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

// === IMPORTS ===
extern  int	Parse_Buffer(tSpiderScript *Script, const char *Buffer, const char *Filename);
extern int	SpiderScript_int_LoadBytecode(tSpiderScript *Script, const char *Name);

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
	ret = malloc(sizeof(tSpiderScript));
	ret->Variant = Variant;
	ret->Functions = NULL;
	ret->FirstClass = NULL;
	
	if( Parse_Buffer(ret, data, Filename) ) {
		free(data);
		free(ret);
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
		tScript_Class_Var *at;
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

	free(Script);
}
