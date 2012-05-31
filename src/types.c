/*
 * SpiderScript
 * - By John Hodge (thePowersGang)
 *
 * types.c
 * - Type codes
 */
#include <spiderscript.h>
#include "common.h"
#include <string.h>
#include <stdio.h>

#define SS_DATATYPE_FLAG_MASK	0x3000
#define SS_DATATYPE_FLAG_INT	0x1000
#define SS_DATATYPE_FLAG_NCLASS	0x1000
#define SS_DATATYPE_FLAG_SCLASS	0x2000

// === GLOBALS ===
const char	*casSpiderScript_InternalTypeNames[] = {
	"void",
	"Variable",
	"Boolean",
	"Integer",
	"Real",
	"String"
	};

// === CODE ===
const char *SpiderScript_GetTypeName(tSpiderScript *Script, int Type)
{
	tSpiderClass *nc;
	tScript_Class *sc;

	Type &= 0xFFFF;	// Remove array level?

	nc = SpiderScript_GetClass_Native(Script, Type);
	sc = SpiderScript_GetClass_Script(Script, Type);

	if( nc )
		return nc->Name;
	else if( sc )
		return sc->Name;
	else if( Type < 0 || Type >= NUM_SS_DATATYPES )
		return "INVAL";
	else
		return casSpiderScript_InternalTypeNames[Type];
}

int SpiderScript_GetTypeCode(tSpiderScript *Script, const char *Name)
{
	return SpiderScript_GetTypeCodeEx(Script, Name, strlen(Name));
}

int SpiderScript_GetTypeCodeEx(tSpiderScript *Script, const char *Name, int NameLen)
{
	 int	depth = 0;

	#if 0	
	while( isdigit(*Name) )
		depth = depth * 10 + (*Name++ - '0');
	#endif	

//	printf("Getting type for '%.*s'\n", NameLen, Name);

	// #1 - Internal types
	for( int i = 0; i <= SS_DATATYPE_STRING; i ++ )
	{
		if( strncmp(Name, casSpiderScript_InternalTypeNames[i], NameLen) != 0 )
			continue ;
		if( casSpiderScript_InternalTypeNames[i][NameLen] != '\0' )
			continue ;
		return SS_MAKEARRAYN(i, depth);
	}
	
	// #2 - Classes (Native)
	{
		 int	i = 0;
		for( tSpiderClass *class = Script->Variant->Classes; class; class = class->Next, i++ )
		{
			if( strncmp(Name, class->Name, NameLen) != 0 )
				continue ;
			if( class->Name[NameLen] != '\0' )
				continue ;
			return SS_MAKEARRAYN(SS_DATATYPE_FLAG_NCLASS | i, depth);
		}
	}
	
	// #3 - Classes (Script)
	{
		 int	i = 0;
		for( tScript_Class *class = Script->FirstClass; class; class = class->Next, i++ )
		{
			if( strncmp(Name, class->Name, NameLen) != 0 )
				continue ;
			if( class->Name[NameLen] != '\0' )
				continue ;
			return SS_MAKEARRAYN(SS_DATATYPE_FLAG_SCLASS | i, depth);
		}
	}

//	printf("Type '%.*s' undefined\n", NameLen, Name);
	return -1;
}

tSpiderClass *SpiderScript_GetClass_Native(tSpiderScript *Script, int Type)
{
	if( SS_GETARRAYDEPTH(Type) )
		return NULL;
	if( (Type & SS_DATATYPE_FLAG_MASK) != SS_DATATYPE_FLAG_NCLASS )
		return NULL;
	
	// O(n) ... not a good idea, should speed this up
	tSpiderClass *ret = Script->Variant->Classes;
	for( int i = (Type & 0xFFF); ret && i --; ret = ret->Next )
		;
	return ret;
}

tScript_Class *SpiderScript_GetClass_Script(tSpiderScript *Script, int Type)
{
	if( SS_GETARRAYDEPTH(Type) )
		return NULL;
	if( (Type & SS_DATATYPE_FLAG_MASK) != SS_DATATYPE_FLAG_SCLASS )
		return NULL;
	
	// O(n) ... not a good idea, should speed this up
	tScript_Class *ret = Script->FirstClass;
	for( int i = (Type & 0xFFF); ret && i --; ret = ret->Next )
		;
	return ret;
}

