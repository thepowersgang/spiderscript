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
#include <assert.h>

// === IMPORTS ===

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

tSpiderScript_TypeDef *SpiderScript_ResolveObject(tSpiderScript *Script, const char *DefaultNamespaces[], const char *Name)
{
	 int	id;
	void	*lident;
	for( int i = 0; i == 0 || (DefaultNamespaces && DefaultNamespaces[i-1]); i ++ )
	{
		const char *ns = DefaultNamespaces ? DefaultNamespaces[i] : NULL;
		
		if( Script ) {
			id = SpiderScript_int_GetScriptClass(Script, Script->FirstClass, ns, Name, &lident);
			if( id != -1 ) {
				return &((tScript_Class*)lident)->TypeInfo;
			}
		}
		id = SpiderScript_int_GetNativeClass(Script, gapExportedClasses, ns, Name, &lident);
		if( id != -1 ) {
			return &((tSpiderClass*)lident)->TypeDef;
		}
		id = SpiderScript_int_GetNativeClass(Script, Script->Variant->Classes, ns, Name, &lident);
		if( id != -1 ) {
			return &((tSpiderClass*)lident)->TypeDef;
		}
	}
	
	return NULL;
}

// --------------------------------------------------------------------
// External API Functions
// --------------------------------------------------------------------
int SpiderScript_ExecuteFunction(tSpiderScript *Script, const char *Function,
	tSpiderTypeRef *RetType, void *RetData,
	int NArguments, const tSpiderTypeRef *ArgTypes, const void * const Arguments[],
	void **Ident
	)
{
	int id = 0;
	if( !Ident || !*Ident ) {
		id = SpiderScript_ResolveFunction(Script, NULL, Function, Ident);
		if( id != -1 && (id >> 16) == 0 && Ident ) {
			*Ident = (void*)( (intptr_t)*Ident | 1 );
		}
	}
	return SpiderScript_int_ExecuteFunction(Script, id,
		RetType, RetData, NArguments, ArgTypes, Arguments, Ident);
}

int SpiderScript_ExecuteMethod(tSpiderScript *Script, const char *Function,
	tSpiderTypeRef *RetType, void *RetData,
	const int NArguments, const tSpiderTypeRef *ArgTypes, const void * const Arguments[],
	void **Ident
	)
{
	void *ident = NULL;
	const tSpiderObject	*Object;

	if( NArguments < 1 || !SS_ISTYPEOBJECT(ArgTypes[0]) || !Arguments[0] ) {
		// NOTE: It's an exception (not an assert), because this is an external API function
		SpiderScript_ThrowException(Script, SS_EXCEPTION_BUG,
			strdup("Method call with invalid `this` argument"));
		return -1;
	}
	Object = Arguments[0];
	if( !Object ) {
		SpiderScript_ThrowException(Script, SS_EXCEPTION_NULLDEREF, "");
		return -1;
	}

	 int	fcn_idx = 0;
	if( !Ident || !*Ident )
	{
		assert(Object->TypeDef);
		if( Object->TypeDef->Class == SS_TYPECLASS_SCLASS )
		{
			tScript_Class *sc = Object->TypeDef->SClass;
			tScript_Function	*sf;
			for( sf = sc->FirstFunction; sf; sf = sf->Next )
			{
				if( strcmp(sf->Name, Function) == 0 )
					break ;
				fcn_idx ++;
			}
			if( !sf )	return -1;
			ident = (void*)( (intptr_t)sf | 1 );
		}
		else if( Object->TypeDef->Class == SS_TYPECLASS_NCLASS )
		{
			tSpiderClass	*nc = Object->TypeDef->NClass;
			tSpiderFunction	*fcn;
			for( fcn = nc->Methods; fcn; fcn = fcn->Next )
			{
				if( strcmp(fcn->Name, Function) == 0 )
					break ;
				fcn_idx ++;
			}
			if( !fcn )	return -1;
			ident = fcn;
		}
		if( Ident )
			*Ident = ident;
	}
	else
		ident = *Ident;
	
	return SpiderScript_int_ExecuteMethod(Script, fcn_idx,
		RetType, RetData, NArguments, ArgTypes, Arguments, &ident);
}


int SpiderScript_CreateObject(tSpiderScript *Script, const char *ClassName,
	tSpiderObject **RetData, int NArguments, const tSpiderTypeRef *ArgTypes, const void * const Arguments[],
	void **Ident
	)
{
	tSpiderScript_TypeDef	*type;

	// Can't do caching speedup because the type code is needed
	type = SpiderScript_ResolveObject(Script, NULL, ClassName);
	return SpiderScript_int_ConstructObject(Script, type, RetData, NArguments, ArgTypes, Arguments, NULL);
}

int SpiderScript_CreateObject_Type(tSpiderScript *Script, const tSpiderScript_TypeDef *TypeCode,
	tSpiderObject **RetData, int NArguments, const tSpiderTypeRef *ArgTypes, const void * const Arguments[],
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
	tSpiderTypeRef *RetType, void *RetData,
	int NArguments, const tSpiderTypeRef *ArgTypes, const void * const Arguments[],
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
		if( RetType )
			*RetType = sfcn->Prototype.ReturnType;

		// Execute!
		return Bytecode_ExecuteFunction(Script, sfcn, RetData, NArguments, ArgTypes, Arguments);
	}
	else if(fcn)
	{
		if( FunctionIdent )
			*FunctionIdent = fcn;
		if( RetType )
			*RetType = fcn->Prototype->ReturnType;

		// Execute!
		int rv = fcn->Handler( Script, RetData, NArguments, ArgTypes, Arguments );
		if( rv < 0 )	return rv;
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
	tSpiderTypeRef *RetType, void *RetData,
	const int NArguments, const tSpiderTypeRef *const ArgTypes, const void * const Arguments[],
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
	// TODO: Disabled because return type/prototype needs to be saved
	if( !RetType ) {
		if( FunctionIdent && *FunctionIdent ) {
			if( *(intptr_t*)FunctionIdent & 1 )
				sf = (void*)( *(intptr_t*)FunctionIdent & ~1 );
			else
				fcn = *FunctionIdent;
		}
	}

	// Only do the lookup if the cache is NULL
	if( !sf && !fcn )
	{
		const char	*fcnname = NULL, *classname = NULL;
		const tSpiderFcnProto	*proto = NULL;
		 int	min_arguments = 0;
		bool	proto_is_heap = false;
		// Script-defined classes
		if( Object->TypeDef->Class == SS_TYPECLASS_SCLASS )
		{
			sc = Object->TypeDef->SClass;
			for( i = 0, sf = sc->FirstFunction; sf; sf = sf->Next )
			{
				if( i == MethodID )
					break ;
				i ++;
			}
			if( !sf )
			{
				SpiderScript_ThrowException(Script, SS_EXCEPTION_NAMEERROR,
					"Class '%s' does not have a method id %i", sc->Name, MethodID);
				return -1;
			}

			classname = sc->Name;
			fcnname = sf->Name;
			proto = &sf->Prototype;
			min_arguments = sf->ArgumentCount;
		}
		else if( Object->TypeDef->Class == SS_TYPECLASS_NCLASS )
		{
			nc = Object->TypeDef->NClass;
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
					"Class '%s' does not have a method #%i", nc->Name, MethodID);
				return -1;
			}

			for( i = 0; fcn->Prototype->Args[i].Def != NULL; i ++ )
				;
			
			classname = nc->Name;
			fcnname = fcn->Name;
			proto = fcn->Prototype;
			min_arguments = i;
		}
		else if( Object->TypeDef->Class == SS_TYPECLASS_GENERIC )
		{
			tSpiderGenericInst	*inst = Object->TypeDef->Generic;
			nc = inst->Template->NClass;
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
					"Class '%s' does not have a method #%i", inst->Name, MethodID);
				return -1;
			}

			for( i = 0; fcn->Prototype->Args[i].Def != NULL; i ++ )
				;
			
			classname = nc->Name;
			fcnname = fcn->Name;
			proto = SpiderScript_int_TemplateApply(Script, inst, fcn->Prototype);
			min_arguments = i;
			proto_is_heap = true;
		}
		else
		{
			return SpiderScript_ThrowException(Script, SS_EXCEPTION_TYPEMISMATCH,
				"Method call on non-object");
		}

		if( NArguments < min_arguments || (!proto->bVariableArgs && NArguments != min_arguments) )
		{
			return SpiderScript_ThrowException_ArgCountC(Script, classname, fcnname,
				(proto->bVariableArgs ? -min_arguments : min_arguments), NArguments);
		}

		// Check the type of the arguments
		// - Start at 1 to skip 'this'
		for( i = 1; i < min_arguments; i ++ )
		{
			if( SS_ISCORETYPE(proto->Args[i], SS_DATATYPE_UNDEF) )
				continue ;
			if( !SS_TYPESEQUAL(ArgTypes[i], proto->Args[i]) )
			{
				return SpiderScript_ThrowException_ArgError(Script,
					classname, fcnname,
					i+1, proto->Args[i], ArgTypes[i]);
			}
		}

		if( RetType )
			*RetType = proto->ReturnType;
		
		if( proto_is_heap )
			free( (void*)proto );
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
		tSpiderTypeRef	typeref = {.ArrayDepth=0,.Def=Object->TypeDef};
		SpiderScript_ThrowException(Script, SS_EXCEPTION_BUG,
			"BUG - Method call %s #%i did not resolve",
			SpiderScript_GetTypeName(Script, typeref), MethodID
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
int SpiderScript_int_ConstructObject(tSpiderScript *Script, const tSpiderScript_TypeDef *TypeDef,
	tSpiderObject **RetData, int NArguments, const tSpiderTypeRef *ArgTypes, const void * const Arguments[],
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
			sc = (void*)( *(intptr_t*) FunctionIdent >> 1 << 1 );
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
	if( !nc && !sc )
	{
		switch(TypeDef->Class)
		{
		case SS_TYPECLASS_SCLASS:
			sc = TypeDef->SClass;
			break;
		case SS_TYPECLASS_NCLASS:
			nc = TypeDef->NClass;
			break;
		case SS_TYPECLASS_GENERIC:
			nc = TypeDef->Generic->Template->NClass;
			break;
		default:
			break;
		}
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
		obj = nc->Constructor( Script, TypeDef, NArguments, ArgTypes, Arguments );
		if( !obj )	return -1;

		*RetData = obj;
		return 0;
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
			tSpiderTypeRef	argtypes[NArguments+1];
			args[0] = obj;
			argtypes[0].ArrayDepth = 0;
			argtypes[0].Def = &sc->TypeInfo;
			memcpy(args+1, Arguments, NArguments*sizeof(void*));
			memcpy(argtypes+1, ArgTypes, NArguments*sizeof(argtypes[0]));
			rv = Bytecode_ExecuteFunction(Script, f, NULL, NArguments+1, argtypes, args);
			if( rv < 0 )	return -1;
		}
	
		return 0;
	}
	else	// Not found?
	{
		SpiderScript_ThrowException(Script, SS_EXCEPTION_NAMEERROR,
			mkstr("Undefined reference to class %s\n",
				SpiderScript_GetTypeName(Script, (tSpiderTypeRef){TypeDef,0}))
			);
		return -1;
	}
}

const char *SpiderScript_int_GetFunctionName(tSpiderScript *Script, int FunctionID)
{
	tScript_Function	*sf;
	tSpiderFunction	*fcn = NULL;
	int i = FunctionID & 0xFFFF;
	switch( FunctionID >> 16 )
	{
	case 0:	// Script
		for( sf = Script->Functions; sf && i --; sf = sf->Next )
			;
		return sf ? sf->Name : "-BADID-";
	case 1:	// Exports
		if( i < giNumExportedFunctions )
			fcn = gapExportedFunctions[i];
		return fcn ? fcn->Name : "-BADID-";
	case 2:	// Variant
		if( i < Script->Variant->nFunctions )
			fcn = Script->Variant->Functions[i];
		return fcn ? fcn->Name : "-BADID-";
	default:
		return "-BADTYPE-";
	}
}
const char *SpiderScript_int_GetMethodName(tSpiderScript *Script, tSpiderTypeRef ObjType, int MethodID)
{
	if( ObjType.ArrayDepth > 0 )
		return "#ARRAY#";
	if( ObjType.Def->Class == SS_TYPECLASS_SCLASS )
	{
		tScript_Class	*sc = ObjType.Def->SClass;
		tScript_Function	*sf = sc->FirstFunction;
		for( int i = 0; sf && i != MethodID; sf = sf->Next, i ++ )
			;
		return sf ? sf->Name : "-BADID-";
	}
	else if( ObjType.Def->Class == SS_TYPECLASS_NCLASS )
	{
		tSpiderClass	*nc = ObjType.Def->NClass;
		tSpiderFunction	*fcn = nc->Methods;
		// Search for the function
		for( int i = 0; fcn && i != MethodID; fcn = fcn->Next, i ++ )
			;
		return fcn ? fcn->Name : "-BADID-";
	}
	else if( ObjType.Def->Class == SS_TYPECLASS_GENERIC )
	{
		tSpiderClass	*nc = ObjType.Def->Generic->Template->NClass;
		tSpiderFunction	*fcn = nc->Methods;
		// Search for the function
		for( int i = 0; fcn && i != MethodID; fcn = fcn->Next, i ++ )
			;
		return fcn ? fcn->Name : "-BADID-";
	}
	else
	{
		return "-BADTYPE-";
	}

}

void AST_RuntimeMessageV(tSpiderScript *Script, tAST_Node *Node, const char *Type, const char *Format, va_list args)
{
	size_t len = 0;
	va_list largs;
	va_copy(largs, args);
	len += snprintf(NULL, 0, "%s:%i: %s: ", (Node?Node->File:"<none>"), (Node?Node->Line:0), Type);
	len += vsnprintf(NULL, 0, Format, largs);
	len += snprintf(NULL, 0, "\n");
	va_end(largs);
	
	char buf[len+1];
	size_t ofs = 0;
	ofs += snprintf(buf+ofs, len-ofs, "%s:%i: %s: ", (Node?Node->File:"<none>"), (Node?Node->Line:0), Type);
	ofs += vsnprintf(buf+ofs, len-ofs, Format, args);
	ofs += snprintf(buf+ofs, len-ofs, "\n");
	
	if( Script->Variant->HandleError )
		Script->Variant->HandleError(Script, buf);
	else
		fprintf(stderr, "%s", buf);
}
void AST_RuntimeMessage(tSpiderScript *Script, tAST_Node *Node, const char *Type, const char *Format, ...)
{
	va_list	args;
	va_start(args, Format);
	AST_RuntimeMessageV(Script, Node, Type, Format, args);
	va_end(args);
}
void AST_RuntimeError(tSpiderScript *Script, tAST_Node *Node, const char *Format, ...)
{
	va_list	args;
	va_start(args, Format);
	AST_RuntimeMessageV(Script, Node, "error", Format, args);
	va_end(args);
}
