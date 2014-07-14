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
#include <assert.h>
#include <stdlib.h>

#define SS_DATATYPE_FLAG_MASK	0x3000
#define SS_DATATYPE_FLAG_INT	0x0000
#define SS_DATATYPE_FLAG_NCLASS	0x1000
#define SS_DATATYPE_FLAG_SCLASS	0x2000
#define SS_DATATYPE_FLAG_BCLASS	0x3000	// Builtin class

// === GLOBALS ===
const char	*casSpiderScript_InternalTypeNames[] = {
	"void",
	"undefined",
	"Boolean",
	"Integer",
	"Real",
	"String"
	};
const int	ciSpiderScript_NumInternalTypeNames = sizeof(casSpiderScript_InternalTypeNames)/sizeof(char*);
const tSpiderScript_TypeDef	gSpiderScript_AnyType     = {.Class=SS_TYPECLASS_CORE,{.Core=SS_DATATYPE_UNDEF}};
const tSpiderScript_TypeDef	gSpiderScript_BoolType    = {.Class=SS_TYPECLASS_CORE,{.Core=SS_DATATYPE_BOOLEAN}};
const tSpiderScript_TypeDef	gSpiderScript_IntegerType = {.Class=SS_TYPECLASS_CORE,{.Core=SS_DATATYPE_INTEGER}};
const tSpiderScript_TypeDef	gSpiderScript_RealType    = {.Class=SS_TYPECLASS_CORE,{.Core=SS_DATATYPE_REAL}};
const tSpiderScript_TypeDef	gSpiderScript_StringType  = {.Class=SS_TYPECLASS_CORE,{.Core=SS_DATATYPE_STRING}};

const tSpiderScript_TypeDef	gSpiderScript_TemplateInst = {.Class=SS_TYPECLASS_TPLARG,{.ArgNum=-1}};
const tSpiderScript_TypeDef	gSpiderScript_TemplateArg0 = {.Class=SS_TYPECLASS_TPLARG,{.ArgNum=0}};

// === CODE ===
const char *SpiderScript_GetTypeName(tSpiderScript *Script, tSpiderTypeRef Type)
{
	if( Type.ArrayDepth )
	{
		const int buflen = 40;
		const int bufcount = 3;
		static __thread int static_bufidx = 0;
		static __thread char static_bufs[3][40];
		
		 int	bufid = static_bufidx;
		static_bufidx = (static_bufidx + 1) % bufcount;
		 int	depth = Type.ArrayDepth;
		
		snprintf(static_bufs[bufid], buflen, "%s[%i]", SpiderScript_GetTypeName_D(Type.Def), depth);
		return static_bufs[bufid];
	}
	else
	{
		return SpiderScript_GetTypeName_D(Type.Def);
	}
}

const char *SpiderScript_GetTypeName_D(const tSpiderScript_TypeDef *Def)
{
	if( Def == NULL )
		return "#VOID";
	
	switch(Def->Class)
	{
	case SS_TYPECLASS_CORE:
		if(Def->Core >= ciSpiderScript_NumInternalTypeNames)
			return "#OoRCore";
		return casSpiderScript_InternalTypeNames[Def->Core];
	case SS_TYPECLASS_NCLASS:
		return Def->NClass->Name;
	case SS_TYPECLASS_SCLASS:
		return Def->SClass->Name;
	case SS_TYPECLASS_FCNPTR:
		return "#FcnPtr";
	case SS_TYPECLASS_GENERIC:
		return Def->Generic->Name;
	case SS_TYPECLASS_TPLARG:
		return "#TplArg";
	}
	return "#UNK";
}

int SpiderScript_FormatTypeStrV(tSpiderScript *Script, char *Data, int MaxLen, const char *Template, tSpiderTypeRef Type)
{
	 int	len = 0;

	#define addch(ch) do{if(len <MaxLen)Data[len] = ch;len++;}while(0)
	#define adds(s)	do{const char *_=s;while(*_){addch(*_);_++;}}while(0)
	for( ; *Template; Template ++)
	{
		if( *Template != '%' ) {
			addch(*Template);
			continue ;
		}
		
		Template++;
		switch( *Template )
		{
		case '%':
			addch('%');
			break;
		case 's':	// String representation
			adds(SpiderScript_GetTypeName(Script, Type));
			//if( SS_GETARRAYDEPTH(Type) ) {
			//	addch('#');
			//	assert( SS_GETARRAYDEPTH(Type) < 10 );
			//	addch('0' + SS_GETARRAYDEPTH(Type));
			//}
			break;
		}
	}
	if( len < MaxLen )
		Data[len] = '\0';
	#undef addch
	#undef adds
	return len;
}

char *SpiderScript_FormatTypeStr1(tSpiderScript *Script, const char *Template, tSpiderTypeRef Type1)
{
	int len = SpiderScript_FormatTypeStrV(Script, NULL, 0, Template, Type1);
	char *ret = malloc(len+1);
	SpiderScript_FormatTypeStrV(Script, ret, len+1, Template, Type1);
	return ret;
}

const tSpiderScript_TypeDef *SpiderScript_GetCoreType(tSpiderScript_CoreType Type)
{
	switch(Type)
	{
	case SS_DATATYPE_NOVALUE:	return NULL;
	case SS_DATATYPE_UNDEF: 	return &gSpiderScript_AnyType;
	case SS_DATATYPE_STRING:	return &gSpiderScript_StringType;
	case SS_DATATYPE_INTEGER:	return &gSpiderScript_IntegerType;
	case SS_DATATYPE_REAL:  	return &gSpiderScript_RealType;
	case SS_DATATYPE_BOOLEAN:	return &gSpiderScript_BoolType;
	default:
		fprintf(stderr, "BUG: SpiderScript_GetCoreType unk %i\n", Type);
		exit(-1);
	}
}

const tSpiderScript_TypeDef *SpiderScript_GetType(tSpiderScript *Script, const char *Name)
{
	return SpiderScript_GetTypeEx(Script, Name, strlen(Name));
}

const tSpiderScript_TypeDef *SpiderScript_GetTypeEx(tSpiderScript *Script, const char *Name, int NameLen)
{
	// #1 - Internal types
	assert( ciSpiderScript_NumInternalTypeNames == NUM_SS_DATATYPES );
	for( int i = 0; i < NUM_SS_DATATYPES; i ++ )
	{
		if( strncmp(Name, casSpiderScript_InternalTypeNames[i], NameLen) != 0 )
			continue ;
		if( casSpiderScript_InternalTypeNames[i][NameLen] != '\0' )
			continue ;
		return SpiderScript_GetCoreType(i);
	}
	
	// #2 - Classes (Native)
	{
		 int	i = 0;
		for( i = 0; i < giNumExportedClasses; i ++ )
		{
			tSpiderClass *class = gapExportedClasses[i];
			if( strncmp(Name, class->Name, NameLen) != 0 )
				continue ;
			if( class->Name[NameLen] != '\0' )
				continue ;
			return &class->TypeDef;
		}
		for( i = 0; i < Script->Variant->nClasses; i++ )
		{
			tSpiderClass *class = Script->Variant->Classes[i];
			if( strncmp(Name, class->Name, NameLen) != 0 )
				continue ;
			if( class->Name[NameLen] != '\0' )
				continue ;
			return &class->TypeDef;
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
			return &class->TypeInfo;
		}
	}

//	printf("Type '%.*s' undefined\n", NameLen, Name);
	return SS_ERRPTR;
}

const tSpiderScript_TypeDef *SpiderScript_CreateGeneric(tSpiderScript *Script,
	const tSpiderScript_TypeDef *Template, const tSpiderTypeRef InnerType)
{
	tSpiderGenericInst	**pnp = &Script->TemplateInstances;
	
	assert(Template);
	assert(Template->Class == SS_TYPECLASS_NCLASS);
	assert(Template->NClass);
	
	 int	n_args = Template->NClass->NMetaArgs;
	// TODO: Multiple arguments
	assert(n_args == 1);
	
	for( tSpiderGenericInst *inst = Script->TemplateInstances; inst; pnp = &inst->Next, inst = inst->Next )
	{
		if( inst->Template < Template )
			continue ;
		if( inst->Template > Template )
			break ;
	
		if( !SS_TYPESEQUAL(inst->InnerTypes[0], InnerType) )
			continue ;
		
		return &inst->Def;
	}
	
	const char *inner_name = SpiderScript_GetTypeName(Script, InnerType);
	size_t	namelen = strlen(Template->NClass->Name) + 2 + strlen(inner_name) + 1;
	size_t size = offsetof(tSpiderGenericInst, InnerTypes[n_args]) + namelen;
	tSpiderGenericInst *inst = malloc( size );
	inst->Next = NULL;
	inst->Name = (const char*)&inst->InnerTypes[n_args];
	snprintf((char*)inst->Name, namelen, "%s<%s>", Template->NClass->Name, inner_name);
	inst->Def.Class = SS_TYPECLASS_GENERIC;
	inst->Def.Generic = inst;
	inst->Template = Template;
	inst->InnerTypes[0] = InnerType;
	
	inst->Next = *pnp;
	*pnp = inst;
	
	return &inst->Def;
}
tSpiderTypeRef SpiderScript_int_TemplateApply_Type(tSpiderGenericInst *Inst, const tSpiderTypeRef Type)
{
	if( Type.Def == NULL )
		return Type;
	if( Type.Def->Class != SS_TYPECLASS_TPLARG )
		return Type;
	tSpiderTypeRef arg = (Type.Def->ArgNum == -1 ? (tSpiderTypeRef){&Inst->Def,0} : Inst->InnerTypes[Type.Def->ArgNum]);
	
	tSpiderTypeRef ret = arg;
	if( Type.ArrayDepth ) {
		ret.ArrayDepth += Type.ArrayDepth;
	}
	
	//printf("Template Apply: %s using %s = %s\n",
	//	SpiderScript_GetTypeName(NULL, Type),
	//	SpiderScript_GetTypeName(NULL, arg),
	//	SpiderScript_GetTypeName(NULL, ret)
	//	);
	
	return ret;
}
tSpiderFcnProto *SpiderScript_int_TemplateApply(tSpiderScript *Script, tSpiderGenericInst *Inst, const tSpiderFcnProto *TplProto)
{
	int argc = 0;
	while( TplProto->Args[argc].Def != NULL )
		argc ++;
	
	tSpiderFcnProto *ret = malloc( offsetof(tSpiderFcnProto, Args[argc+1]) );
	
	ret->ReturnType = SpiderScript_int_TemplateApply_Type(Inst, TplProto->ReturnType);
	ret->bVariableArgs = TplProto->bVariableArgs;
	for( int i = 0; i < argc; i ++ )
		ret->Args[i] = SpiderScript_int_TemplateApply_Type(Inst, TplProto->Args[i]);
	ret->Args[argc] = (tSpiderTypeRef){0};
	
	return ret;
}


#if 0
tSpiderClass *SpiderScript_GetClass_Native(tSpiderScript *Script, int Type)
{
	 int	idx = Type & 0xFFF;
	if( SS_GETARRAYDEPTH(Type) )
		return NULL;
	switch( (Type & SS_DATATYPE_FLAG_MASK) )
	{
	case SS_DATATYPE_FLAG_NCLASS:
		if( idx >= Script->Variant->nClasses )	return NULL;
		return Script->Variant->Classes[idx];
	case SS_DATATYPE_FLAG_BCLASS:
		if( idx >= giNumExportedClasses )	return NULL;
		return gapExportedClasses[ idx ];
	default:
		return NULL;
	}
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
#endif

