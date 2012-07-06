/*
* SpiderScript Library
* by John Hodge (thePowersGang)
* 
* exec.c
* - Execution handlers
*/
#include <stdlib.h>
#include "common.h"
#include "ast.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

// === IMPORTS ===

// === PROTOTYPES ===
void	AST_RuntimeMessage(tAST_Node *Node, const char *Type, const char *Format, ...);
void	AST_RuntimeError(tAST_Node *Node, const char *Format, ...);

// === CODE ===
#define DO_CHECK()	do {\
		 int	ofs = baseLen ? baseLen + 1 : 0; \
		if( baseLen ) { \
			if( strncmp(e->Name, BasePath, baseLen) != 0 )	continue ; \
			if( e->Name[baseLen] != BC_NS_SEPARATOR )	continue ; \
		} \
		if( strcmp(e->Name + ofs, Path) == 0 ) { \
			if(Ident)	*Ident = e; \
			return i; \
		} \
	}while(0)
/**
 * \brief Defines the group of functions to look up classes / functions
 */
#define DEF_GETFCNCLASS(_type, _name) \
int _name(tSpiderScript *Script, _type *First, const char *BasePath, const char *Path, void **Ident) { \
	_type	*e; \
	 int	baseLen = (BasePath ? strlen(BasePath) : 0); \
	 int	i = 0; \
	for( e = First; e; e = e->Next, i ++ ) DO_CHECK(); \
	return -1; \
}
#define DEF_GETFCNCLASSA(_type, _name) \
int _name(tSpiderScript *Script, _type **List, const char *BasePath, const char *Path, void **Ident) { \
	_type	*e; \
	 int	baseLen = (BasePath ? strlen(BasePath) : 0); \
	 int	i = 0; \
	for( i = 0; (e = List[i]); i ++ ) DO_CHECK(); \
	return -1; \
}

DEF_GETFCNCLASSA(tSpiderFunction, SpiderScript_int_GetNativeFunction)
DEF_GETFCNCLASSA(tSpiderClass, SpiderScript_int_GetNativeClass)
DEF_GETFCNCLASS(tScript_Function, SpiderScript_int_GetScriptFunction)
DEF_GETFCNCLASS(tScript_Class, SpiderScript_int_GetScriptClass)

int SpiderScript_ResolveFunction(tSpiderScript *Script, const char *DefaultNamespaces[], const char *Function,
	void **Ident)
{
	 int	id, i;
	
	// Scan list, Last item should always be NULL, so abuse that to check non-prefixed	
	for( i = 0; i == 0 || (DefaultNamespaces && DefaultNamespaces[i-1]); i ++ )
	{
		const char *ns = DefaultNamespaces ? DefaultNamespaces[i] : NULL;
		
		id = SpiderScript_int_GetScriptFunction(Script, Script->Functions, ns, Function, Ident);
		if( id != -1 )
			return id | (0 << 16);
		
		id = SpiderScript_int_GetNativeFunction(Script, gapExportedFunctions, ns, Function, Ident);
		if( id != -1 )
			return id | (1 << 16);

		id = SpiderScript_int_GetNativeFunction(Script, Script->Variant->Functions, ns, Function, Ident);
		if( id != -1 )
			return id | (2 << 16);
	}
	
	return -1;
}

int SpiderScript_ResolveObject(tSpiderScript *Script, const char *DefaultNamespaces[], const char *Name, void **Ident)
{
	int i, id;
	
	for( i = 0; i == 0 || (DefaultNamespaces && DefaultNamespaces[i-1]); i ++ )
	{
		const char *ns = DefaultNamespaces ? DefaultNamespaces[i] : NULL;
		
		id = SpiderScript_int_GetScriptClass(Script, Script->FirstClass, ns, Name, Ident);
		if( id != -1 )
			return id | 0x2000;
		id = SpiderScript_int_GetNativeClass(Script, gapExportedClasses, ns, Name, Ident);
		if( id != -1 )
			return id | 0x3000;
		id = SpiderScript_int_GetNativeClass(Script, Script->Variant->Classes, ns, Name, Ident);
		if( id != -1 )
			return id | 0x1000;
	}
	
	return -1;
}

// --------------------------------------------------------------------
// External API Functions
// --------------------------------------------------------------------
int SpiderScript_ExecuteFunction(tSpiderScript *Script, const char *Function,
	void *RetData, int NArguments, const int *ArgTypes, const void * const Arguments[],
	void **Ident
	)
{
	int id = 0;
	if( !Ident || !*Ident )
		id = SpiderScript_ResolveFunction(Script, NULL, Function, Ident);
	return SpiderScript_int_ExecuteFunction(Script, id, RetData, NArguments, ArgTypes, Arguments, Ident);
}

int SpiderScript_ExecuteMethod(tSpiderScript *Script, const char *Function,
	void *RetData, int NArguments, const int *ArgTypes, const void * const Arguments[],
	void **Ident
	)
{
	void *ident = NULL;
	const tSpiderObject	*Object;

	if( NArguments < 1 || !SS_ISTYPEOBJECT(ArgTypes[0]) || !Arguments[0] ) {
		// NOTE: It's a bug, because this is an external API function
		SpiderScript_ThrowException(Script, SS_EXCEPTION_BUG,
			strdup("Method call with invalid `this` argument"));
		return -1;
	}
	Object = Arguments[0];

	if( !Ident || !*Ident )
	{
		tScript_Class	*sc;
		tSpiderClass	*nc;
		if( (sc = SpiderScript_GetClass_Script(Script, Object->TypeCode)) )
		{
			tScript_Function	*sf;
			for( sf = sc->FirstFunction; sf; sf = sf->Next )
			{
				if( strcmp(sf->Name, Function) == 0 )
					break ;
			}
			if( !sf )	return -1;
			ident = (void*)( (intptr_t)sf | 1 );
		}
		else if( (nc = SpiderScript_GetClass_Native(Script, Object->TypeCode)) )
		{
			tSpiderFunction	*fcn;
			for( fcn = nc->Methods; fcn; fcn = fcn->Next )
			{
				if( strcmp(fcn->Name, Function) == 0 )
					break ;
			}
			if( !fcn )	return -1;
			ident = fcn;
		}
		if( Ident )
			*Ident = ident;
	}
	else
		ident = *Ident;
	
	return SpiderScript_int_ExecuteMethod(Script, -1, RetData, NArguments, ArgTypes, Arguments, &ident);
}


int SpiderScript_CreateObject(tSpiderScript *Script, const char *ClassName,
	tSpiderObject **RetData, int NArguments, const int *ArgTypes, const void * const Arguments[],
	void **Ident
	)
{
	void	*ident = NULL;
	 int	type;

	// Can't do caching speedup because the type code is needed
	type = SpiderScript_ResolveObject(Script, NULL, ClassName, &ident);
	if( Ident )
		*Ident = ident;
	return SpiderScript_int_ConstructObject(Script, type, RetData, NArguments, ArgTypes, Arguments, &ident);
}

int SpiderScript_CreateObject_Type(tSpiderScript *Script, int TypeCode,
	tSpiderObject **RetData, int NArguments, const int *ArgTypes, const void * const Arguments[],
	void **Ident
	)
{
	return SpiderScript_int_ConstructObject(Script, TypeCode, RetData, NArguments, ArgTypes, Arguments, Ident);
}

// --------------------------------------------------------------------
// Internal Handlers
// --------------------------------------------------------------------
/**
 * \brief Execute a script function
 * \param Script	Script context to execute in
 * \param Namespace	Namespace to search for the function
 * \param Function	Function name to execute
 * \param NArguments	Number of arguments to pass
 * \param Arguments	Arguments passed
 */
int SpiderScript_int_ExecuteFunction(tSpiderScript *Script, int FunctionID,
	void *RetData, int NArguments, const int *ArgTypes, const void * const Arguments[],
	void **FunctionIdent
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
		i = FunctionID & 0xFFFF;
		switch( FunctionID >> 16 )
		{
		case 0:	// Script
			for( sfcn = Script->Functions; sfcn && i --; sfcn = sfcn->Next )
				;
			break;
		case 1:	// Exports
			if( i < giNumExportedFunctions )
				fcn = gapExportedFunctions[i];
			break;
		case 2:	// Variant
			if( i < Script->Variant->nFunctions )
				fcn = Script->Variant->Functions[i];
			break;
		}
	}

	// Find the function in the script?
	if( sfcn )
	{
		// Abuses alignment requirements on almost all platforms
		if( FunctionIdent )
			*FunctionIdent = (void*)( (intptr_t)sfcn | 1 );

		// Execute!
		return Bytecode_ExecuteFunction(Script, sfcn, RetData, NArguments, ArgTypes, Arguments);
	}
	else if(fcn)
	{
		if( FunctionIdent )
			*FunctionIdent = fcn;

		// Execute!
		int rv = fcn->Handler( Script, RetData, NArguments, ArgTypes, Arguments );
		if( rv < 0 )	return rv;
		if( rv != fcn->ReturnType ) {
			fprintf(stderr, "BUG - Function %s didn't return correct type (0x%x instead of 0x%x)\n",
				fcn->Name, rv, fcn->ReturnType);
			return -1;
		}
		return rv;
	}
	else
	{
		fprintf(stderr, "Undefined reference to function ID 0x%x\n", FunctionID);
		return -1;
	}
}

/**
 * \brief Execute an object method function
 * \param Script	Script context to execute in
 * \param MethodName	Name of method to call
 * \param NArguments	Number of arguments to pass
 * \param Arguments	Arguments passed (with `this`
 */
int SpiderScript_int_ExecuteMethod(tSpiderScript *Script, int MethodID,
	void *RetData, int NArguments, const int *ArgTypes, const void * const Arguments[],
	void **FunctionIdent
	)
{
	tSpiderFunction	*fcn = NULL;
	tScript_Function *sf = NULL;
	 int	i;
	tScript_Class	*sc;
	tSpiderClass *nc;
	tSpiderObject	*Object;

	if( NArguments < 1 ) {
		SpiderScript_ThrowException(Script, SS_EXCEPTION_ARGUMENT,
			strdup("Method call with no `this` argument"));
		return -1;
	}
	if( !SS_ISTYPEOBJECT(ArgTypes[0]) ) {
		SpiderScript_ThrowException(Script, SS_EXCEPTION_TYPEMISMATCH,
			mkstr("Method call with invalid `this` argument (0x%x)", ArgTypes[0])
			);
		return -1;
	}
	if( !Arguments[0] ) {
		SpiderScript_ThrowException(Script, SS_EXCEPTION_NULLDEREF,
			strdup("Method call with invalid `this` argument (NULL)")
			);
		return -1;
	}
	Object = (void*)Arguments[0];

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
			for( i = 0, sf = sc->FirstFunction; sf; sf = sf->Next )
			{
				if( i == MethodID )
					break ;
				i ++;
			}
			if( !sf )
			{
				SpiderScript_ThrowException(Script, SS_EXCEPTION_NAMEERROR,
					mkstr("Class '%s' does not have a method id %i", sc->Name, MethodID)
					);
				return -1;
			}

			if( NArguments != sf->ArgumentCount ) {
				SpiderScript_ThrowException(Script, SS_EXCEPTION_ARGUMENT,
					mkstr("%s->#%i requires %i arguments, %i given",
						sc->Name, MethodID, sf->ArgumentCount, NArguments)
					);
				return -1;
			}

			// Type checking (eventually will not be needed)
			for( i = 1; i < NArguments; i ++ )
			{
				if( ArgTypes[i] != sf->Arguments[i].Type )
				{
					SpiderScript_ThrowException(Script, SS_EXCEPTION_ARGUMENT,
						mkstr("Argument %i of %s->%s should be %i, got %i",
							i+1, sc->Name, sf->Name, sf->Arguments[i].Type, ArgTypes[i])
						);
					return -1;
				}
			}
		}
		else if( (nc = SpiderScript_GetClass_Native(Script, Object->TypeCode)) )
		{
			// Search for the function
			for( i = 0, fcn = nc->Methods; fcn; fcn = fcn->Next )
			{
				if( i == MethodID )
					break ;
				i ++;
			}
			// Error
			if( !fcn )
			{
				SpiderScript_ThrowException(Script, SS_EXCEPTION_NAMEERROR,
					mkstr("Class '%s' does not have a method #%i", nc->Name, MethodID)
					);
				return -1;
			}

			for( i = 0; fcn->ArgTypes[i] != 0 && fcn->ArgTypes[i] != -1; i ++ );
			 int	minArgc = i;
			 int	bVaraible = (fcn->ArgTypes[i] == -1);

			if( NArguments < minArgc || (!bVaraible && NArguments != minArgc) ) {
				SpiderScript_ThrowException(Script, SS_EXCEPTION_ARGUMENT,
					mkstr("Argument count mismatch (%i passed, %i%s expected)",
						1+NArguments, i, (bVaraible?"+":""))
					);
				return -1;
			}

			// Check the type of the arguments
			// - Start at 1 to skip the -2 for 'this class'
			for( i = 1; i < minArgc; i ++ )
			{
				if( ArgTypes[i] != fcn->ArgTypes[i] )
				{
					SpiderScript_ThrowException(Script, SS_EXCEPTION_ARGUMENT,
						mkstr("Argument type mismatch (0x%x, expected 0x%x)",
							ArgTypes[i], fcn->ArgTypes[i])
						);
					return -1;
				}
			}
		}
		else
		{
			SpiderScript_ThrowException(Script, SS_EXCEPTION_TYPEMISMATCH,
				strdup("Method call on non-object")
				);
			return -1;
		}
	}

	// Call function
	if( sf )
	{
		// Abuses alignment requirements on almost all platforms
		if( FunctionIdent )
			*FunctionIdent = (void*)( (intptr_t)sf | 1 );
		return Bytecode_ExecuteFunction(Script, sf, RetData, NArguments, ArgTypes, Arguments);
	}
	else if( fcn )
	{
		if( FunctionIdent )
			*FunctionIdent = fcn;
		return fcn->Handler(Script, RetData, NArguments, ArgTypes, Arguments);
	}
	else {
		SpiderScript_ThrowException(Script, SS_EXCEPTION_BUG,
			mkstr("BUG - Method call 0x%x->#%i did not resolve",Object->TypeCode, MethodID)
			);
		return -1;
	}
		
}

/**
 * \brief Create an object
 * \param Script	Script context to execute in
 * \param Function	Function name to execute
 * \param NArguments	Number of arguments to pass
 * \param Arguments	Arguments passed
 */
int SpiderScript_int_ConstructObject(tSpiderScript *Script, int Type,
	tSpiderObject **RetData, int NArguments, const int *ArgTypes, const void * const Arguments[],
	void **FunctionIdent
	)
{
	tSpiderClass	*nc = NULL;
	tScript_Class	*sc = NULL;
	tSpiderObject	*obj;
	 int	rv;

	// Check for the cache
	if( FunctionIdent && *FunctionIdent ) {
		if( (intptr_t)*FunctionIdent & 1 )
			sc = (void*)( *(intptr_t*) FunctionIdent & ~1ULL );
		else
			nc = *FunctionIdent;
	}

	if( !RetData ) {
		SpiderScript_ThrowException(Script, SS_EXCEPTION_BUG,
			strdup("Object being discarded, not creating")
			);
		return -1;
	}

	// Find class
	if( !sc && !sc )
	{
		sc = SpiderScript_GetClass_Script(Script, Type);
		nc = SpiderScript_GetClass_Native(Script, Type);
	}
	
	// Execute!
	if( nc )
	{
		if( FunctionIdent )
			*FunctionIdent = nc;

		if( !nc->Constructor ) {
			// Uh, oops?
			return -1;
		}

		// Call constructor
		// TODO: Type Checking?
		// TODO: Return?
		rv = nc->Constructor->Handler( Script, &obj, NArguments, ArgTypes, Arguments );
		if( rv < 0 )	return -1;

		*RetData = obj;
		return Type;
	}
	else if( sc )
	{
		if( FunctionIdent )
			*FunctionIdent = (void*)( (intptr_t)sc | 1 );
		
		// Call constructor
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
			rv = Bytecode_ExecuteFunction(Script, f, NULL, NArguments+1, argtypes, args);
			if( rv < 0 )	return -1;
		}
	
		return Type;
	}
	else	// Not found?
	{
		SpiderScript_ThrowException(Script, SS_EXCEPTION_NAMEERROR,
			mkstr("Undefined reference to class 0x%x\n", Type)
			);
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
