/*
* SpiderScript Library
* by John Hodge (thePowersGang)
* 
* bytecode_makefile.c
* - Generate a bytecode file
*/
#include <stdlib.h>
#include "common.h"
#include "ast.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define BC_NS_SEPARATOR	'@'

// === IMPORTS ===

// === PROTOTYPES ===
void	AST_RuntimeMessage(tAST_Node *Node, const char *Type, const char *Format, ...);
void	AST_RuntimeError(tAST_Node *Node, const char *Format, ...);

// === CODE ===
/**
 * \brief Defines the group of functions to look up classes / functions
 */
#define DEF_GETFCNCLASS(_type, _name) \
_type *_name(tSpiderScript *Script, _type *First, const char *BasePath, const char *Path) { \
	_type	*e; \
	 int	baseLen = (BasePath ? strlen(BasePath) : 0); \
	for( e = First; e; e = e->Next ) \
	{ \
		 int	ofs = baseLen ? baseLen + 1 : 0; \
		if( baseLen ) { \
			if( strncmp(e->Name, BasePath, baseLen) != 0 )	continue ; \
			if( e->Name[baseLen] != BC_NS_SEPARATOR )	continue ; \
		} \
		if( strcmp(e->Name + ofs, Path) == 0 ) \
			return e; \
	} \
	return NULL; \
}

DEF_GETFCNCLASS(tSpiderFunction, SpiderScript_int_GetNativeFunction)
DEF_GETFCNCLASS(tSpiderClass, SpiderScript_int_GetNativeClass)
DEF_GETFCNCLASS(tScript_Function, SpiderScript_int_GetScriptFunction)
DEF_GETFCNCLASS(tScript_Class, SpiderScript_int_GetScriptClass)


/**
 * \brief Execute a script function
 * \param Script	Script context to execute in
 * \param Namespace	Namespace to search for the function
 * \param Function	Function name to execute
 * \param NArguments	Number of arguments to pass
 * \param Arguments	Arguments passed
 */
int SpiderScript_ExecuteFunctionEx(tSpiderScript *Script,
	const char *Function, const char *DefaultNamespaces[],
	void *RetData, int NArguments, const int *ArgTypes, const void * const Arguments[],
	void **FunctionIdent, int bExecute
	)
{
	tSpiderFunction	*fcn = NULL;
	tScript_Function	*sfcn = NULL;
	 int	i;

	// Check for a chached name
	if( FunctionIdent && *FunctionIdent ) {
		if( *(intptr_t*)FunctionIdent & 1 )
			sfcn = (void*)( *(intptr_t*)FunctionIdent & ~1 );
		else
			fcn = *FunctionIdent;
	}

	// Scan list, Last item should always be NULL, so abuse that to check non-prefixed	
	if( !sfcn && !fcn )
	{
		for( i = 0; i == 0 || (DefaultNamespaces && DefaultNamespaces[i-1]); i ++ )
		{
			const char *ns = DefaultNamespaces ? DefaultNamespaces[i] : NULL;
			fcn = SpiderScript_int_GetNativeFunction(Script, Script->Variant->Functions, ns, Function);
			if( fcn )	break;

			fcn = SpiderScript_int_GetNativeFunction(Script, gpExports_First, ns, Function);
			if( fcn )	break;
		
			// TODO: Script namespacing
			sfcn = SpiderScript_int_GetScriptFunction(Script, Script->Functions, ns, Function);
			if( sfcn )	break ;
		}
	}

	// Find the function in the script?
	if( sfcn )
	{
		// Abuses alignment requirements on almost all platforms
		if( FunctionIdent )
			*FunctionIdent = (void*)( (intptr_t)sfcn | 1 );

		// Execute!
		if( bExecute )
			return Bytecode_ExecuteFunction(Script, sfcn, RetData, NArguments, ArgTypes, Arguments);
		else
			return 0;
	}
	else if(fcn)
	{
		if( FunctionIdent )
			*FunctionIdent = fcn;

		// Execute!
		if( bExecute )
			return fcn->Handler( Script, RetData, NArguments, ArgTypes, Arguments );
		else
			return 0;
	}
	else
	{
		fprintf(stderr, "Undefined reference to function '%s'\n", Function);
		return -1;
	}
}

/**
 * \brief Execute an object method function
 * \param Script	Script context to execute in
 * \param Object	Object in which to find the method
 * \param MethodName	Name of method to call
 * \param NArguments	Number of arguments to pass
 * \param Arguments	Arguments passed
 */
int SpiderScript_ExecuteMethod(tSpiderScript *Script,
	tSpiderObject *Object, const char *MethodName,
	void *RetData, int NArguments, const int *ArgTypes, const void * const Arguments[],
	void **FunctionIdent
	)
{
	tSpiderFunction	*fcn = NULL;
	tScript_Function *sf = NULL;
	int	newtypes[NArguments+1];
	const void	*newargs [NArguments+1];
	 int	i;
	tScript_Class	*sc;
	tSpiderClass *nc;
	
	// Create the "this" argument
	newargs[0] = Object;
	memcpy(&newargs[1], Arguments, NArguments*sizeof(void*));
	newtypes[0] = Object->TypeCode;
	memcpy(&newtypes[1], ArgTypes, NArguments*sizeof(void*));

	// Check for a chached name
	if( FunctionIdent && *FunctionIdent ) {
		if( *(intptr_t*)FunctionIdent & 1 )
			sf = (void*)( *(intptr_t*)FunctionIdent & ~1 );
		else
			fcn = *FunctionIdent;
	}

	// Only do the lookup if the cache is NULL
	if( !sf && !fcn )
	{
		// Script-defined classes
		if( (sc = SpiderScript_GetClass_Script(Script, Object->TypeCode)) )
		{
			for( sf = sc->FirstFunction; sf; sf = sf->Next )
			{
				if( strcmp(sf->Name, MethodName) == 0 )
					break ;
			}
			if( !sf )
			{
				AST_RuntimeError(NULL, "Class '%s' does not have a method '%s'",
						sc->Name, MethodName);
				return -1;
			}

			if( NArguments+1 != sf->ArgumentCount ) {
				AST_RuntimeError(NULL, "%s->%s requires %i arguments, %i given",
					sc->Name, MethodName, sf->ArgumentCount, NArguments);
				return -1;
			}

			// Type checking (eventually will not be needed)
			for( i = 0; i < 1+NArguments; i ++ )
			{
				if( newtypes[i] != sf->Arguments[i].Type )
				{
					AST_RuntimeError(NULL, "Argument %i of %s->%s should be %i, got %i",
						i+1, sc->Name, MethodName, sf->Arguments[i].Type, newtypes[i]);
					return -1;
				}
			}
		}
		else if( (nc = SpiderScript_GetClass_Native(Script, Object->TypeCode)) )
		{
			// Search for the function
			for( fcn = nc->Methods; fcn; fcn = fcn->Next )
			{
				if( strcmp(fcn->Name, MethodName) == 0 )
					break;
			}
			// Error
			if( !fcn )
			{
				AST_RuntimeError(NULL, "Class '%s' does not have a method '%s'",
					nc->Name, MethodName);
				return -1;
			}
			
			// Check the type of the arguments
			for( i = 0; fcn->ArgTypes[i]; i ++ )
			{
				if( i >= NArguments ) {
					for( ; fcn->ArgTypes[i]; i ++ )	;
					AST_RuntimeError(NULL, "Argument count mismatch (%i passed, %i expected)",
						NArguments, i);
					return -1;
				}
				if( ArgTypes[i] != fcn->ArgTypes[i] )
				{
					AST_RuntimeError(NULL, "Argument type mismatch (%i, expected %i)",
						ArgTypes[i], fcn->ArgTypes[i]);
					return -1;
				}
			}
			
			// Call handler
		}
		else
		{
			AST_RuntimeError(NULL, "Method call on non-object");
			return -1;
		}
	}

	// Call function
	if( sf )
	{
		// Abuses alignment requirements on almost all platforms
		if( FunctionIdent )
			*FunctionIdent = (void*)( (intptr_t)sf | 1 );
		return Bytecode_ExecuteFunction(Script, sf, RetData, NArguments+1, newtypes, newargs);
	}
	else if( fcn )
	{
		if( FunctionIdent )
			*FunctionIdent = fcn;
		return fcn->Handler(Script, RetData, NArguments+1, newtypes, newargs);
	}
	else {
		AST_RuntimeError(NULL, "BUG - Method call '%s' did not resolve", MethodName);
		return -1;
	}
		
}

/**
 * \brief Execute a script function
 * \param Script	Script context to execute in
 * \param Function	Function name to execute
 * \param NArguments	Number of arguments to pass
 * \param Arguments	Arguments passed
 */
int SpiderScript_CreateObject(tSpiderScript *Script,
	const char *ClassPath, const char *DefaultNamespaces[],
	tSpiderObject **RetData, int NArguments, const int *ArgTypes, const void * const Arguments[],
	void **FunctionIdent, int bExecute
	)
{
	tSpiderClass	*class = NULL;
	tScript_Class	*sc = NULL;
	 int	i;	

	// Check for the cache
	if( FunctionIdent && *FunctionIdent ) {
		if( (intptr_t)*FunctionIdent & 1 )
			sc = (void*)( *(intptr_t*) FunctionIdent & ~1ULL );
		else
			class = *FunctionIdent;
	}

	if( bExecute && !RetData ) {
		AST_RuntimeError(NULL, "Object being discarded, not creating");
		return -1;
	}

	// Scan list, Last item should always be NULL, so abuse that to check without a prefix
	if( !class && !sc )
	{
		for( i = 0; i == 0 || DefaultNamespaces[i-1]; i ++ )
		{
			const char *ns = DefaultNamespaces[i];
			class = SpiderScript_int_GetNativeClass(Script, Script->Variant->Classes, ns, ClassPath);
			if( class )	break;
	
//			class = SpiderScript_int_GetNativeClass(Script, &gExportNamespaceRoot, ns, ClassPath);
//			if( class )	break;
			
			sc = SpiderScript_int_GetScriptClass(Script, Script->FirstClass, ns, ClassPath);
			if( sc )	break;
		}
	}
	
	// Execute!
	if(class)
	{
		if( FunctionIdent )
			*FunctionIdent = class;	

		// Call constructor
		if( bExecute )
		{
			tSpiderObject	*obj;
			// TODO: Type Checking
			class->Constructor->Handler( Script, &obj, NArguments, ArgTypes, Arguments );

			*RetData = obj;
			return 0;		
		}
		else
		{
			return 0;
		}
	}
	else if( sc )
	{
		if( FunctionIdent )
			*FunctionIdent = (void*)( (intptr_t)sc | 1 );
		
		// Call constructor
		if( bExecute )
		{
			tSpiderObject	*obj;
			tScript_Function	*f;
			
			obj = SpiderScript_AllocateScriptObject(Script, sc);

			// Call constructor?
			for( f = sc->FirstFunction; f; f = f->Next )
			{
				if( strcmp(f->Name, CONSTRUCTOR_NAME) == 0 )
					break;
			}
			
			*RetData = obj;
			
			if( f )
			{
				const void	*args[NArguments+1];
				 int	argtypes[NArguments+1];
				args[0] = obj;
				argtypes[0] = sc->TypeCode;
				memcpy(args+1, Arguments, NArguments*sizeof(void*));
				memcpy(argtypes+1, ArgTypes, NArguments*sizeof(int));
				Bytecode_ExecuteFunction(Script, f, NULL, NArguments+1, argtypes, args);
			}
	
			return 0;
		}
		return 0;
	}
	else	// Not found?
	{
		fprintf(stderr, "Undefined reference to class '%s'\n", ClassPath);
		return -1;
	}
}

void AST_RuntimeMessage(tAST_Node *Node, const char *Type, const char *Format, ...)
{
	va_list	args;
	
	if(Node) {
		fprintf(stderr, "%s:%i: ", Node->File, Node->Line);
	}
	fprintf(stderr, "%s: ", Type);
	va_start(args, Format);
	vfprintf(stderr, Format, args);
	va_end(args);
	fprintf(stderr, "\n");
}
void AST_RuntimeError(tAST_Node *Node, const char *Format, ...)
{
	va_list	args;
	
	if(Node) {
		fprintf(stderr, "%s:%i: ", Node->File, Node->Line);
	}
	fprintf(stderr, "error: ");
	va_start(args, Format);
	vfprintf(stderr, Format, args);
	va_end(args);
	fprintf(stderr, "\n");
}
