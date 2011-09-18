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
extern tAST_Variable *Variable_Define(tAST_BlockState *Block, int Type, const char *Name);
extern void	Variable_SetValue(tAST_BlockState *Block, const char *Name, tSpiderValue *Value);
extern void	Variable_Destroy(tAST_Variable *Variable);

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
	char	cacheFilename[strlen(Filename)+6+1];
	char	*data;
	 int	fLen;
	FILE	*fp;
	tSpiderScript	*ret;
	
	strcpy(cacheFilename, Filename);
	strcat(cacheFilename, ".cache");
	
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
	ret->LastFunction = NULL;
	
	ret->CurNamespace = NULL;
	if( Parse_Buffer(ret, data, Filename) ) {
		free(data);
		free(ret);
		return NULL;
	}
	
	free(data);
	
	
	// HACK!!
	{
		size_t	size;
		
		printf("Total Size: ");	fflush(stdout);
		size = AST_WriteScript(NULL, ret);
		printf("0x%x bytes\n", (unsigned)size);
		
		fp = fopen(cacheFilename, "wb");
		if(!fp)	return ret;
		
		data = malloc(size);
		size = AST_WriteScript(data, ret);
		fwrite(data, size, 1, fp);
		free(data);
		fclose(fp);
	}
	
	return ret;
}

int SpiderScript_SaveBytecode(tSpiderScript *Script, const char *DestFile)
{
	return Bytecode_ConvertScript(Script, DestFile);
}

/**
 * \brief Free a script
 */
void SpiderScript_Free(tSpiderScript *Script)
{
	tScript_Function	*fcn = Script->Functions;
	tScript_Function	*nextFcn;
	
	// Free functions
	while(fcn)
	{
		if(fcn->ASTFcn)	AST_FreeNode( fcn->ASTFcn );
		if(fcn->BCFcn)	Bytecode_DeleteFunction( fcn->BCFcn );

		nextFcn = fcn->Next;
		free( fcn );
		fcn = nextFcn;
	}
	
	free(Script);
}
