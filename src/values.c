/*
 * SpiderScript Library
 * by John Hodge (thePowersGang)
 * 
 * values.c
 * - Manage tSpiderValue objects
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "spiderscript.h"
#include "common.h"
#include <stdarg.h>
#include <inttypes.h>

// === PROTOTYPES ===

// === CODE ===
int SpiderScript_int_GetTypeSize(int TypeCode)
{
	switch(TypeCode)
	{
	case SS_DATATYPE_NOVALUE:
		return -1;
	case SS_DATATYPE_BOOLEAN:
		return sizeof(tSpiderBool);
	case SS_DATATYPE_INTEGER:
		return sizeof(tSpiderInteger);
	case SS_DATATYPE_REAL:
		return sizeof(tSpiderReal);
	default:	// Reference types
		return 0;
	}
}

/**
 * \brief Allocate and initialise a SpiderScript object
 */
tSpiderObject *SpiderScript_AllocateObject(tSpiderScript *Script, tSpiderClass *Class, int ExtraBytes)
{
	 int	size = sizeof(tSpiderObject) + Class->NAttributes * sizeof(void*);

	for( int i = 0; i < Class->NAttributes; i ++ )
	{
		int sz = SpiderScript_int_GetTypeSize(Class->AttributeDefs[i].Type);
		if( sz < 0 ) {
			// Oops?
		}
		else
			size += sz;
	}

	tSpiderObject	*ret = calloc(1, size + ExtraBytes);
	
	ret->TypeCode = SpiderScript_GetTypeCode(Script, Class->Name);
	ret->Script = Script;
	ret->ReferenceCount = 1;
	ret->OpaqueData = (char*)ret + size;
	
	size = 0;
	for( int i = 0; i < Class->NAttributes; i ++ )
	{
		ret->Attributes[i] = (char*)(ret->Attributes + Class->NAttributes) + size;
		size += SpiderScript_int_GetTypeSize(Class->AttributeDefs[i].Type);
	}
	
	return ret;
}

tSpiderObject *SpiderScript_AllocateScriptObject(tSpiderScript *Script, tScript_Class *Class)
{
	 int	n_attr = 0;
	 int	size = sizeof(tSpiderObject);
	tScript_Var *at;
	 int	i;

	for( at = Class->FirstProperty; at; at = at->Next )
	{
		size += SpiderScript_int_GetTypeSize(at->Type);
		n_attr ++;
	}
	
	size += n_attr * sizeof(void*);
	
	tSpiderObject	*ret = calloc(1, size);
	ret->TypeCode = SpiderScript_GetTypeCode(Script, Class->Name);
	ret->Script = Script;
	ret->ReferenceCount = 1;
	ret->OpaqueData = 0;
	
	size = 0;
	for( i = 0, at = Class->FirstProperty; at; at = at->Next, i ++ )
	{
		ret->Attributes[i] = (char*)(ret->Attributes + n_attr) + size;
		size += SpiderScript_int_GetTypeSize(at->Type);
	}
	
	return ret;
}

void SpiderScript_ReferenceObject(const tSpiderObject *_Object)
{
	tSpiderObject *Object = (void*)_Object;
	if( !Object )	return;
	Object->ReferenceCount ++;
}

void SpiderScript_DereferenceObject(const tSpiderObject *_Object)
{
	tSpiderObject *Object = (void*)_Object;
	if( !Object )	return;
	Object->ReferenceCount --;
	if( Object->ReferenceCount == 0 )
	{
		tSpiderClass	*nc = SpiderScript_GetClass_Native(Object->Script, Object->TypeCode);
		tScript_Class	*sc = SpiderScript_GetClass_Script(Object->Script, Object->TypeCode);
		 int	n_att = 0;

		if( nc ) {
			nc->Destructor(Object);
			n_att = nc->NAttributes;
		}
		else if( sc ) {
			// TODO: Destructor
			for( tScript_Var *at = sc->FirstProperty; at; at = at->Next )
				n_att ++;
		}
		else {
//			assert(nc || sc);
			return ;
		}
		
		int at_types[n_att];
		if( nc ) {
			for( int i = 0; i < n_att; i ++ )
				at_types[i] = nc->AttributeDefs[i].Type;
		}
		else if( sc ) {
			n_att = 0;
			for( tScript_Var *at = sc->FirstProperty; at; at = at->Next ) {
				at_types[n_att] = at->Type;
				n_att ++;
			}
		}
	
		// Clean up attributes
		for( int i = 0; i < n_att; i ++ )
		{
			void	*ptr = Object->Attributes[i];
			 int	type = at_types[i];
			
			if( !ptr )
				continue ;
			Object->Attributes[i] = NULL;
			
			if( SS_ISTYPEOBJECT(type) )
				SpiderScript_DereferenceObject(ptr);
			else if( SS_GETARRAYDEPTH(type) )
				SpiderScript_DereferenceArray(ptr);
			else if( type == SS_DATATYPE_STRING )
				SpiderScript_DereferenceString(ptr);
			else
				;	// Local allocation
		}

		free(Object);
	}
}

/**
 * \brief Create an string object
 */
tSpiderString *SpiderScript_CreateString(int Length, const char *Data)
{
	tSpiderString	*ret = malloc( sizeof(tSpiderString) + Length + 1 );
	ret->RefCount = 1;
	ret->Length = Length;
	if( Data )
		memcpy(ret->Data, Data, Length);
	else
		memset(ret->Data, 0, Length);
	ret->Data[Length] = '\0';
	return ret;
}

void SpiderScript_ReferenceString(const tSpiderString *_String)
{
	tSpiderString	*String = (void*)_String;
	if( !String )	return ;
	String->RefCount ++;
}

void SpiderScript_DereferenceString(const tSpiderString *_String)
{
	tSpiderString	*String = (void*)_String;
	if( !String )	return ;
	
	String->RefCount --;
	if( String->RefCount > 0 )	return ;
	
	// Destruction time
	free(String);
	// that was easy
}

tSpiderArray *SpiderScript_CreateArray(int InnerType, int ItemCount)
{
	 int	ent_size;
	tSpiderArray	*ret;
	
	// Get the size of one entry (reference types are zero sized, but need 1 pointer)
	ent_size = SpiderScript_int_GetTypeSize(InnerType);
	if( ent_size == 0 )	ent_size = sizeof(void*);

	ret = malloc( sizeof(tSpiderArray) + ItemCount*ent_size );
	ret->Type = InnerType;
	ret->RefCount = 1;
	ret->Length = ItemCount;
	memset(ret->Bools, 0, ItemCount*ent_size);	// Could use any, but Bools works
	return ret;
}

const void *SpiderScript_GetArrayPtr(const tSpiderArray *Array, int Item)
{
	if( Item < 0 || Item >= Array->Length )
		return NULL;
	
	if( SS_GETARRAYDEPTH(Array->Type) ) {
		return Array->Arrays[Item];
	}
	if( SS_ISTYPEOBJECT(Array->Type) ) {
		return Array->Objects[Item];
	}
	switch(Array->Type)
	{
	case SS_DATATYPE_STRING:
		return Array->Strings[Item];
	case SS_DATATYPE_BOOLEAN:
		return &Array->Bools[Item];
	case SS_DATATYPE_INTEGER:
		return &Array->Integers[Item];
	case SS_DATATYPE_REAL:
		return &Array->Reals[Item];
	}
	return NULL;
}

void SpiderScript_ReferenceArray(const tSpiderArray *_Array)
{
	tSpiderArray	*Array = (void*)_Array;
	if( !Array )	return ;
	Array->RefCount ++;
}

void SpiderScript_DereferenceArray(const tSpiderArray *_Array)
{
	tSpiderArray	*Array = (void*)_Array;
	if( !Array )	return;
	
	Array->RefCount --;
	if( Array->RefCount > 0 )	return ;
	
	// Destruction time
	if( SS_ISTYPEOBJECT(Array->Type) ) {
		for( int i = 0; i < Array->Length; i ++ )
			SpiderScript_DereferenceObject(Array->Objects[i]);
	}
	else if( SS_GETARRAYDEPTH(Array->Type) ) {
		for( int i = 0; i < Array->Length; i ++ )
			SpiderScript_DereferenceArray(Array->Arrays[i]);
	}
	else if( Array->Type == SS_DATATYPE_STRING ) {
		for( int i = 0; i < Array->Length; i ++ )
			SpiderScript_DereferenceString(Array->Strings[i]);
	}
	else
		;	// Local allocation
	
	free(Array);
}

int SpiderScript_StringCompare(const tSpiderString *s1, const tSpiderString *s2)
{
	 int	cmp;
	if( s1->Length > s2->Length )
		cmp = memcmp(s1->Data, s2->Data, s2->Length);
	else
		cmp = memcmp(s1->Data, s2->Data, s1->Length);
	
	if( cmp == 0 )
	{
		if( s1->Length == s2->Length )
			cmp = 0;
		else if( s1->Length < s2->Length )
			cmp = 1;
		else
			cmp = -1;
	}
	return cmp;
}

/**
 * \brief Concatenate two strings
 */
tSpiderString *SpiderScript_StringConcat(const tSpiderString *Str1, const tSpiderString *Str2)
{
	size_t	newLen = 0;
	tSpiderString	*ret;
	
	if(Str1)	newLen += Str1->Length;
	if(Str2)	newLen += Str2->Length;
	ret = malloc( sizeof(tSpiderString) + newLen + 1 );
	ret->RefCount = 1;
	ret->Length = newLen;
	if(Str1)
		memcpy(ret->Data, Str1->Data, Str1->Length);
	if(Str2) {
		if(Str1)
			memcpy(ret->Data+Str1->Length, Str2->Data, Str2->Length);
		else
			memcpy(ret->Data, Str2->Data, Str2->Length);
	}
	ret->Data[ newLen ] = '\0';
	return ret;
}

/**
 * \brief Condenses a value down to a boolean
 */
tSpiderBool SpiderScript_CastValueToBool(int Type, const void *Source)
{
	if( !Source )
		return 0;
	
	if( SS_GETARRAYDEPTH(Type) )
		return Source && ((const tSpiderArray*)Source)->Length > 0;
	else if( SS_ISTYPEOBJECT(Type) )
		return !!Source;	// TODO: Objects
	
	switch( Type )
	{
	case SS_DATATYPE_NOVALUE:
		return 0;
	case SS_DATATYPE_BOOLEAN:
		return *(const tSpiderBool*)Source;
	case SS_DATATYPE_INTEGER:
		return !!*(const tSpiderInteger*)Source;
	case SS_DATATYPE_REAL:
		return (-.5f < *(const tSpiderReal*)Source && *(const tSpiderReal*)Source < 0.5f);
	case SS_DATATYPE_STRING:
		return ((const tSpiderString*)Source)->Length > 0;
	default:
		return 0;
	}
}

/**
 * \brief Cast one object to another
 * \brief Type	Destination type
 * \brief Source	Input data
 */
tSpiderInteger SpiderScript_CastValueToInteger(int Type, const void *Source)
{
	if( !Source )
		return 0;

	if( SS_GETARRAYDEPTH(Type) )
		return 0;
	else if( SS_ISTYPEOBJECT(Type) )
		return 0;	// TODO: Objects
	
	switch( Type )
	{
	case SS_DATATYPE_BOOLEAN:
		return *(const tSpiderBool*)Source;
	case SS_DATATYPE_INTEGER:
		return *(const tSpiderInteger*)Source;
	case SS_DATATYPE_REAL:
		return *(const tSpiderReal*)Source;
	case SS_DATATYPE_STRING:
		return strtoll( ((const tSpiderString*)Source)->Data, NULL, 0);
	default:
		return 0;
	}
}

tSpiderReal SpiderScript_CastValueToReal(int Type, const void *Source)
{
	if( !Source )
		return 0;

	if( SS_GETARRAYDEPTH(Type) )
		return 0;
	else if( SS_ISTYPEOBJECT(Type) )
		return 0;	// TODO: Objects
	
	switch( Type )
	{
	case SS_DATATYPE_BOOLEAN:
		return *(tSpiderBool*)Source;
	case SS_DATATYPE_INTEGER:
		return *(tSpiderInteger*)Source;
	case SS_DATATYPE_REAL:
		return *(tSpiderReal*)Source;
	case SS_DATATYPE_STRING:
		return atof( ((tSpiderString*)Source)->Data);
	default:
		return 0;
	}
}

tSpiderString *SpiderScript_CreateString_Fmt(const char *Format, ...)
{
	tSpiderString	*ret;
	 int	len;
	va_list	args;
	
	va_start(args, Format);
	len = vsnprintf(NULL, 0, Format, args);
	va_end(args);
	
	ret = SpiderScript_CreateString(len+1, NULL);	// Does memset, but meh
	ret->Length = len;
	
	va_start(args, Format);
	vsnprintf(ret->Data, len+1, Format, args);
	va_end(args);
	
	return ret;
}

tSpiderString *SpiderScript_CastValueToString(int Type, const void *Source)
{
	switch(Type)
	{
	case SS_DATATYPE_BOOLEAN:
		return SpiderScript_CreateString_Fmt("%s", *(const tSpiderBool*)Source ? "True" : "False");
	case SS_DATATYPE_INTEGER:
		return SpiderScript_CreateString_Fmt("%"PRIi64, *(const tSpiderInteger*)Source);
	case SS_DATATYPE_REAL:
		return SpiderScript_CreateString_Fmt("%lf", *(const tSpiderReal*)Source);
	case SS_DATATYPE_STRING:
		SpiderScript_ReferenceString( (tSpiderString*)Source );
		return (tSpiderString*)Source;
	default:
		return NULL;
	}
}

