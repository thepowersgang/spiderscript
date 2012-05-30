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
extern tSpiderFunction	*gpExports_First;
extern tSpiderValue	*AST_ExecuteFunction(tSpiderScript *Script, tScript_Function *Fcn, int NArguments, tSpiderValue **Arguments);
extern tSpiderValue	*Bytecode_ExecuteFunction(tSpiderScript *Script, tScript_Function *Fcn, int NArguments, tSpiderValue **Args);

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
tSpiderValue *SpiderScript_ExecuteFunction(tSpiderScript *Script,
	const char *Function,
	const char *DefaultNamespaces[],
	int NArguments, tSpiderValue **Arguments,
	void **FunctionIdent, int bExecute
	)
{
	tSpiderValue	*ret = ERRPTR;
	tSpiderFunction	*fcn = NULL;
	tScript_Function	*sfcn = NULL;
	 int	i;

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
	// TODO: Script namespacing
	if( sfcn )
	{
		// Abuses alignment requirements on almost all platforms
		if( FunctionIdent )
			*FunctionIdent = (void*)( (intptr_t)sfcn | 1 );

		// Execute!
		if( bExecute )
		{
			if( sfcn->BCFcn )
				ret = Bytecode_ExecuteFunction(Script, sfcn, NArguments, Arguments);
			else
				ret = AST_ExecuteFunction(Script, sfcn, NArguments, Arguments);
		}
		else
		{
			ret = NULL;
		}

		return ret;
	}

	else if(fcn)
	{
		if( FunctionIdent )
			*FunctionIdent = fcn;		

		// Execute!
		if( bExecute )
			ret = fcn->Handler( Script, NArguments, Arguments );
		else
			ret = NULL;
	
		return ret;
	}
	else
	{
		fprintf(stderr, "Undefined reference to function '%s'\n", Function);
		return ERRPTR;
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
tSpiderValue *SpiderScript_ExecuteMethod(tSpiderScript *Script,
	tSpiderObject *Object, const char *MethodName,
	int NArguments, tSpiderValue **Arguments)
{
	tSpiderFunction	*fcn;
	tSpiderValue	this;
	tSpiderValue	*newargs[NArguments+1];
	 int	i;
	tScript_Class	*sc;
	tSpiderClass *nc;
	
	// Create the "this" argument
	this.Type = Object->TypeCode;
	this.ReferenceCount = 1;
	this.Object = Object;
	newargs[0] = &this;
	memcpy(&newargs[1], Arguments, NArguments*sizeof(tSpiderValue*));

	// Check type	
	if( (sc = SpiderScript_GetClass_Script(Script, Object->TypeCode)) )
	{
		tScript_Function *sf;

		for( sf = sc->FirstFunction; sf; sf = sf->Next )
		{
			if( strcmp(sf->Name, MethodName) == 0 )
				break ;
		}
		if( !sf )
		{
			AST_RuntimeError(NULL, "Class '%s' does not have a method '%s'",
				sc->Name, MethodName);
			return ERRPTR;
		}

		if( NArguments+1 != sf->ArgumentCount ) {
			AST_RuntimeError(NULL, "%s->%s requires %i arguments, %i given",
				sc->Name, MethodName, sf->ArgumentCount, NArguments);
			return ERRPTR;
		}

		// Type checking (eventually will not be needed)
		for( i = 0; i < 1+NArguments; i ++ )
		{
			if( newargs[i] && newargs[i]->Type != sf->Arguments[i].Type )
			{
				AST_RuntimeError(NULL, "Argument %i of %s->%s should be %i, got %i",
					i+1, sc->Name, MethodName, sf->Arguments[i].Type, Arguments[i]->Type);
				return ERRPTR;
			}
		}

		// Call function
		if( sf->BCFcn )
			return Bytecode_ExecuteFunction(Script, sf, NArguments+1, newargs);
		else
			return AST_ExecuteFunction(Script, sf, NArguments+1, newargs);
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
			return ERRPTR;
		}
		
		// Check the type of the arguments
		for( i = 0; fcn->ArgTypes[i]; i ++ )
		{
			if( i >= NArguments ) {
				for( ; fcn->ArgTypes[i]; i ++ )	;
				AST_RuntimeError(NULL, "Argument count mismatch (%i passed, %i expected)",
					NArguments, i);
				return ERRPTR;
			}
			if( Arguments[i] && Arguments[i]->Type != fcn->ArgTypes[i] )
			{
				AST_RuntimeError(NULL, "Argument type mismatch (%i, expected %i)",
					Arguments[i]->Type, fcn->ArgTypes[i]);
				return ERRPTR;
			}
		}
		
		// Call handler
		return fcn->Handler(Script, NArguments+1, newargs);
	}
	else
	{
		AST_RuntimeError(NULL, "Method call on non-object");
		return ERRPTR;
	}
}

/**
 * \brief Execute a script function
 * \param Script	Script context to execute in
 * \param Function	Function name to execute
 * \param NArguments	Number of arguments to pass
 * \param Arguments	Arguments passed
 */
tSpiderValue *SpiderScript_CreateObject(tSpiderScript *Script,
	const char *ClassPath, const char *DefaultNamespaces[],
	int NArguments, tSpiderValue **Arguments,
	void **FunctionIdent, int bExecute
	)
{
	tSpiderValue	*ret = ERRPTR;
	tSpiderClass	*class = NULL;
	tScript_Class	*sc = NULL;
	 int	i;	

	if( FunctionIdent && *FunctionIdent ) {
		if( (intptr_t)*FunctionIdent & 1 )
			sc = (void*)( *(intptr_t*) FunctionIdent & ~1ULL );
		else
			class = *FunctionIdent;
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
			obj = class->Constructor( Script, NArguments, Arguments );
			if( obj == NULL || obj == ERRPTR )
				return (void *)obj;
			
			// Creatue return object
			ret = malloc( sizeof(tSpiderValue) );
			ret->Type = SpiderScript_GetTypeCode(Script, class->Name);
			ret->ReferenceCount = 1;
			ret->Object = obj;
			
			return ret;
		}
		else
		{
			return NULL;
		}
	}
	else if( sc )
	{
		void	*ident = (void*)( (intptr_t)sc | 1 );
		
		if( FunctionIdent )
			*FunctionIdent = ident;
		
		// Call constructor
		if( bExecute )
		{
			tSpiderObject	*obj;
			tScript_Function	*f;
			
			obj = calloc( 1, sizeof(tSpiderObject) + sc->nProperties*sizeof(tSpiderValue*) );
			if(!obj)	return ERRPTR;
	
			obj->TypeCode = sc->TypeCode;
			obj->ReferenceCount = 1;

			// Call constructor?
			for( f = sc->FirstFunction; f; f = f->Next )
			{
				if( strcmp(f->Name, CONSTRUCTOR_NAME) == 0 )
					break;
			}
			
			ret = malloc( sizeof(tSpiderValue) );
			ret->Type = sc->TypeCode;
			ret->ReferenceCount = 1;
			ret->Object = obj;
			if( f )
			{
				tSpiderValue	*args[NArguments+1];
				args[0] = ret;
				memcpy(args+1, Arguments, NArguments*sizeof(tSpiderValue*));
				if( f->BCFcn )
					Bytecode_ExecuteFunction(Script, f, NArguments+1, args);
				else
					AST_ExecuteFunction(Script, f, NArguments+1, args);
			}
	
			return ret;
		}
		else
		{
			return NULL;
		}
	}
	else	// Not found?
	{
		fprintf(stderr, "Undefined reference to class '%s'\n", ClassPath);
		return ERRPTR;
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
