/*
 * SpiderScript Library
 * by John Hodge (thePowersGang)
 * 
 * bytecode_makefile.c
 * - Generate a bytecode file
 */
#include <stdlib.h>
#include "ast.h"
#include "bytecode.h"
#include "bytecode_gen.h"
#include "bytecode_ops.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define MAGIC_STR	"SSBC\r\n\xBC\x58"
#define MAGIC_STR_LEN	(sizeof(MAGIC_STR)-1)
#define MAGIC_VERSION	1001

#define DEBUG	0
#if DEBUG
# define TRACE(s,args...)	printf("%s:%i "s"\n", __FILE__, __LINE__ ,## args)
#else
# define TRACE(v...)	do{}while(0)
#endif

#define _ASSERT(l,rel,r,code) do{if(!(l rel r)){\
	fprintf(stderr,"%s:%i: ASSERT %s [%li] %s %s [%li] failed\n", __FILE__, __LINE__, #l, (long)(l), #rel, #r, (long)(r));\
	code;}}while(0)
#define _ASSERT_R(l,rel,r,rv) _ASSERT(l,rel,r,return rv)
#define _ASSERT_G(l,rel,r,target) _ASSERT(l,rel,r,goto target)


// === TYPES ===
typedef struct sStringList	tStringList;
typedef struct sString	tString;

struct sString
{
	tString	*Next;
	 int	Length;
	 int	RefCount;
	char	Data[];
};

struct sStringList
{
	tString	*Head;
	tString	*Tail;
	 int	Count;
};
typedef struct
{
	uint32_t	Length;
	uint32_t	Offset;
} t_lenofs;
typedef struct
{
	FILE	*FP;
	tSpiderScript	*Script;
	 int	NStr;
	t_lenofs	*Strings;
	 int	NTypes;
	tSpiderTypeRef	*Types;
	 int	NClasses;
	struct {
		tScript_Class	*Class;
		 int	NMethods;
		 int	NAttribs;
	}	*Classes;
	size_t	FileSize;
} t_loadstate;

// === IMPORTS ===

// === PROTOTYPES ===
 int	SpiderScript_int_LoadBytecodeStream(tSpiderScript *Script, FILE *fp);
 int	SpiderScript_int_SaveBytecodeStream(tSpiderScript *Script, FILE *fp);
 int	StringList_GetString(tStringList *List, const char *String, int Length);
char	*Bytecode_SerialiseFunction(const tBC_Function *Function, int *Length, tStringList *Strings);
tBC_Function	*Bytecode_DeserialiseFunction(const void *Data, size_t Length, t_loadstate *State);

// === GLOBALS ===

// === CODE ===
static inline uint8_t _get8(t_loadstate *State)
{
	uint8_t	rv;
	if( fread(&rv, 1, 1, State->FP) != 1 )
		return 0;
	return rv;
}
static inline uint16_t _get16(t_loadstate *State)
{
	return _get8(State) | ((uint16_t)_get8(State)<<8);
}	
static inline uint32_t _get32(t_loadstate *State)
{
	return _get16(State) | ((uint32_t)_get16(State)<<16);
}
size_t _get_str(t_loadstate *State, char *Dest, int StringID)
{
	_ASSERT_R(StringID, <, State->NStr, -1);
	if( Dest )
	{
		off_t saved_pos = ftell(State->FP);
		fseek(State->FP, State->Strings[StringID].Offset, SEEK_SET);
		size_t len = fread(Dest, 1, State->Strings[StringID].Length, State->FP);
		_ASSERT_R(len, ==, State->Strings[StringID].Length, -1);
		Dest[ State->Strings[StringID].Length ] = '\0';
		fseek(State->FP, saved_pos, SEEK_SET);
//		printf("String '%s' read\n", Dest);
	}
	return State->Strings[StringID].Length;
}
tSpiderTypeRef _get_type(t_loadstate *State, int TypeID)
{
	tSpiderTypeRef	retz = {0,0};
	_ASSERT_R(TypeID, <, State->NTypes, retz);

	return State->Types[TypeID];
}

tScript_Function *_get_fcn(t_loadstate *State)
{
	tScript_Function *ret;
	
	TRACE("_get_fcn @ 0x%lx", ftell(State->FP));
	// Load main function header
	 int	namestr  = _get16(State);
	off_t	code_ofs = _get32(State);
	size_t	code_len = _get32(State);
	 int	ret_type = _get16(State);
	uint8_t	flags    = _get8(State);
	 int	n_args   = _get16(State);

	TRACE("_get_fcn: Name [%i] 0x%lx+%li RV%i %i args",
		namestr, code_ofs, code_len, ret_type, n_args);
	_ASSERT_R(code_ofs, <, State->FileSize, NULL);
	_ASSERT_R(code_ofs+code_len, <, State->FileSize, NULL);

//	printf("namestr = %i, code_ofs = %x, code_len = %x, ret_type = %x, n_args = %i\n",
//		namestr, (unsigned int)code_ofs, (unsigned int)code_len, ret_type, n_args);

	struct {
		uint32_t	Name;
		uint32_t	Type;
	} args[n_args];

	// Get size of function metadata in memory (and load arguments)
	 int	datasize = 0;
	datasize += sizeof(tScript_Function);
	datasize += n_args * (sizeof(char*) + sizeof(ret->Prototype.Args[0]));
	datasize += sizeof(ret->Prototype.Args[0]);
	size_t	len = _get_str(State, NULL, namestr) + 1;
	_ASSERT_R(len, !=, -1, NULL);
	datasize += len;
	for( int i = 0; i < n_args; i ++ )
	{
		args[i].Name = _get16(State);
		args[i].Type = _get16(State);
		len = _get_str(State, NULL, args[i].Name);
		_ASSERT_R(len, !=, -1, NULL);
		datasize += len + 1;
	}

	// Create and populate metadata structure
	ret = malloc( datasize );
	ret->Next = NULL;
	ret->ArgNames = (void*)&ret->Prototype.Args[n_args+1];
	ret->Name = (void*)&ret->ArgNames[n_args];
	_get_str(State, ret->Name, namestr);
	ret->ArgumentCount = n_args;
	ret->Prototype.ReturnType = _get_type(State, ret_type);
	ret->Prototype.bVariableArgs = (flags & 1);
	ret->ASTFcn = NULL;
	char *nameptr = ret->Name + _get_str(State, NULL, namestr) + 1;
	for( int i = 0; i < n_args; i ++ )
	{
		ret->ArgNames[i] = nameptr;
		int len = _get_str(State, nameptr, args[i].Name);
		ret->Prototype.Args[i] = _get_type(State, args[i].Type);
		nameptr += len + 1;
	}
	
	// Load code
	off_t old_pos = ftell(State->FP);
	void *code = malloc(code_len);
	fseek(State->FP, code_ofs, SEEK_SET);
	len = fread(code, 1, code_len, State->FP);
	if( len != code_len ) {
		free(code);
		_ASSERT_G(len, ==, code_len, _err);
	}
	fseek(State->FP, old_pos, SEEK_SET);

	// Parse back into bytecode
	ret->BCFcn = Bytecode_DeserialiseFunction(code, code_len, State);

	free(code);

	return ret;
_err:
	free(ret);
	free(code);
	return NULL;
}


int SpiderScript_int_LoadBytecode(tSpiderScript *Script, const char *SourceFile)
{
	FILE *fp = fopen(SourceFile, "rb");
	if(!fp)	return -1;
	
	int rv = SpiderScript_int_LoadBytecodeStream(Script, fp);

	fclose(fp);	
	return rv;
}

int SpiderScript_int_LoadBytecodeMem(tSpiderScript *Script, const void *Buffer, size_t Size)
{
	// (const void*)->(void*) is ok because it's a read-only stream
	FILE *fp = fmemopen((void*)Buffer, Size, "r");
	if(!fp)	return -1;
	
	int rv = SpiderScript_int_LoadBytecodeStream(Script, fp);
	
	fclose(fp);	
	return rv;
}

int SpiderScript_int_LoadBytecodeStream(tSpiderScript *Script, FILE *fp)
{	
	t_loadstate	state, *State = &state;
	
	fseek(fp, 0, SEEK_END);
	off_t file_size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	State->FP = fp;
	State->Script = Script;
	State->Strings = NULL;
	State->NStr = 0;
	State->FileSize = file_size;

	// Check magic
	_ASSERT_R(file_size, >, MAGIC_STR_LEN + 5*2+4, 1);
	{
		char magic[MAGIC_STR_LEN];
		if( fread(magic, MAGIC_STR_LEN, 1, fp) != 1 ) {
			TRACE("Failed to read magic");
			return -1;
		}
		if( memcmp(magic, MAGIC_STR, sizeof(magic)) != 0 )
		{
			TRACE("Magic mismatch %.*s", (int)MAGIC_STR_LEN, magic);
			return -1;
		}
	}
	// Check version
	const int	file_ver  = _get16(State);
	if( file_ver != MAGIC_VERSION ) {
		TRACE("Version mismatch %i != %i", file_ver, MAGIC_VERSION);
		return -1;
	}
	
	
	// Get counts
	const int	n_class   = _get16(State);
	const int	n_types   = _get16(State);
	const int	n_globals = _get16(State);
	const int	n_fcn     = _get16(State);
	const int	n_str     = _get16(State);
	const off_t	ofs_str   = _get32(State);
	// TODO: Validation	
	
	TRACE("Header %ic %it %ig %if %is@%lx", n_class, n_types, n_globals, n_fcn, n_str, ofs_str);

	// Load string table
	_ASSERT_R(ofs_str + n_str*(4+4), <=, file_size, 1);
	t_lenofs strings[n_str];
	fseek(fp, ofs_str, SEEK_SET);
	for( int i = 0; i < n_str; i ++ )
	{
		strings[i].Length = _get32(State);
		strings[i].Offset = _get32(State);
		_ASSERT_R(strings[i].Offset, <, file_size, 1);
		_ASSERT_R(strings[i].Offset + strings[i].Length, <=, file_size, 1);
		TRACE("Str %i: 0x%x + %i", i, strings[i].Offset, strings[i].Length);
	}
	fseek(fp, MAGIC_STR_LEN+5*2+4, SEEK_SET);	
	State->Strings = strings;
	State->NStr = n_str;
	
	State->NClasses = n_class;
	State->Classes = malloc(n_class * sizeof(*State->Classes));
//	printf("State->Strings = %p, State->NStr = %i\n", State->Strings, State->NStr);
	
	// Deserialise class definitions
	for( int i = 0; i < n_class; i ++ )
	{
		tScript_Class	*sc;
		int namestr = _get16(State);
		int n_attrib = _get16(State);
		int n_method = _get16(State);

		TRACE("Class %i: [%i] %i,%i", i, namestr, n_attrib, n_method);

		sc = malloc( sizeof(tScript_Class) + _get_str(State, NULL, namestr) + 1 );
		if(!sc)	return -1;

		sc->Next = NULL;
		_get_str(State, sc->Name, namestr);
		sc->FirstFunction = NULL;
		sc->FirstProperty = NULL;
		sc->nProperties = n_attrib;
		sc->nFunctions  = n_method;
		sc->TypeInfo.Class = SS_TYPECLASS_SCLASS;
		sc->TypeInfo.SClass = sc;
		sc->Properties = malloc( n_attrib * sizeof(void*) );
		sc->Functions = malloc( n_method * sizeof(void*) );

		State->Classes[i].Class = sc;
		State->Classes[i].NMethods = n_method;
		State->Classes[i].NAttribs = n_attrib;

		TRACE(" Name = '%s'", sc->Name);

		// TODO: Remove/Error on duplicate classes?

//		printf("Added class '%s'\n", sc->Name);
		// Append
		if( Script->FirstClass )
			Script->LastClass->Next = sc;
		else
			Script->FirstClass = sc;
		Script->LastClass = sc;
	}

	// Deserialse types
	State->Types = malloc(n_types * sizeof(*State->Types));
	State->NTypes = n_types;
	for( int i = 0; i < n_types; i ++ )
	{
		int depth = _get8(State);
		int class = _get8(State);
		int idx = _get16(State);
		
		State->Types[i].ArrayDepth = depth;
		switch(class)
		{
		case SS_TYPECLASS_CORE:
			TRACE("Type %i: Core %i", i, idx);
			State->Types[i].Def = SpiderScript_GetCoreType(idx);
			break;
		case SS_TYPECLASS_SCLASS:
			TRACE("Type %i: SClass %i", i, idx);
			_ASSERT_G(idx, <, n_class, _err);
			State->Types[i].Def = &State->Classes[idx].Class->TypeInfo;
			break;
		case SS_TYPECLASS_NCLASS: {
			TRACE("Type %i: NClass %i", i, idx);
			char str[_get_str(State, NULL, idx)];
			_get_str(State, str, idx);
			State->Types[i].Def = SpiderScript_ResolveObject(Script, NULL, str);
			break; }
		case SS_TYPECLASS_FCNPTR:
			// TODO
			break;
		default:
			_ASSERT_G(class, >, 0, _err);
			_ASSERT_G(class, <=, SS_TYPECLASS_FCNPTR, _err);
			break;
		}
		TRACE(" = %s", SpiderScript_GetTypeName(Script, State->Types[i]));
	}

	// Get globals
	for( int i = 0; i < n_globals; i ++ )
	{
		 int	nameid = _get16(State);
		 int	typeid = _get16(State);
		TRACE("Global %i,%i", nameid, typeid);

		// TODO: Need to handle merging of compiled files?		

		tSpiderTypeRef type = _get_type(State, typeid);
		_ASSERT_G(type.Def,!=,NULL, _err);
		 int	size = SS_ISTYPEREFERENCE(type) ? 0 : SpiderScript_int_GetTypeSize(type);
		assert(size >= 0);
		tScript_Var	*g = malloc( sizeof(tScript_Var) + _get_str(State, NULL, nameid)+1 + size );
		
		g->Type = type;
		g->Ptr = g + 1;
		g->Name = (char*)g->Ptr + size;
		if( size == 0 )
			g->Ptr = 0;
		_get_str(State, g->Name, nameid);
		g->Next = NULL;
		if( !Script->FirstGlobal )
			Script->FirstGlobal = g;
		else
			Script->LastGlobal->Next = g;
		Script->LastGlobal = g;
	}	

	// Parse functions
	for( int i = 0; i < n_fcn; i ++ )
	{
		tScript_Function *fcn;
		
		fcn = _get_fcn(State);
		_ASSERT_G(fcn, !=, NULL, _err);

		TRACE("Fcn %i done", i);

		if( Script->Functions )
			Script->LastFunction->Next = fcn;
		else
			Script->Functions = fcn;
		Script->LastFunction = fcn;
	}
	
	for( int i = 0; i < n_class; i ++ )
	{
		tScript_Class	*sc = State->Classes[i].Class;
		
		// Attributes
		for( int j = 0; j < State->Classes[i].NAttribs; j ++ )
		{
			int name = _get16(State);
			int type = _get16(State);
			
			size_t	namelen = _get_str(State, NULL, name);
			_ASSERT_G(namelen, !=, -1, _err);
			tScript_Var *at = malloc( sizeof(*at) + namelen + 1 );
			at->Next = NULL;
			at->Type = _get_type(State, type);
			at->Name = (void*)(at + 1);
			_get_str(State, at->Name, name);
			TRACE("%s->%s : %s", sc->Name, at->Name, SpiderScript_GetTypeName(Script, at->Type));

			sc->Properties[j] = at;
			if( sc->FirstProperty )
				sc->LastProperty->Next = at;
			else
				sc->FirstProperty = at;
			sc->LastProperty = at;
		}
		
		// Functions
		for( int j = 0; j < State->Classes[i].NMethods; j ++ )
		{
			tScript_Function *fcn = _get_fcn(State);
			_ASSERT_G(fcn, !=, NULL, _err);
//			printf("Added method '%s' of '%s'\n", fcn->Name, sc->Name);
			sc->Functions[j] = fcn;
			if( sc->FirstFunction )
				sc->LastFunction->Next = fcn;
			else
				sc->FirstFunction = fcn;
			sc->LastFunction = fcn;
		}
	}

	free(State->Types);
	free(State->Classes);

	return 0;
_err:
	free(State->Types);
	free(State->Classes);
	return 1;
}

int SpiderScript_SaveBytecode(tSpiderScript *Script, const char *DestFile)
{
	FILE *fp = fopen(DestFile, "wb");
	if(!fp)	return 1;
	
	int rv = SpiderScript_int_SaveBytecodeStream(Script, fp);
	
	fclose(fp);
	return rv;
}

int SpiderScript_SaveBytecodeMem(tSpiderScript *Script, void **BufferPtr, size_t *SizePtr)
{
	// Darnit, why isn't (void**) casted like (void*)
	FILE *fp = open_memstream((char**)BufferPtr, SizePtr);
	if(!fp)	return 1;	

	int rv = SpiderScript_int_SaveBytecodeStream(Script, fp);

	fclose(fp);
	return rv;
}

int SpiderScript_int_SaveBytecodeStream(tSpiderScript *Script, FILE *fp)
{
	tStringList	strings = {0};
	tScript_Function	*fcn;
	tScript_Class	*sc;
	 int	fcn_hdr_offset = 0;
	 int	num_globals = 0, fcn_count = 0, class_count = 0;
	 int	strtab_ofs;

	void _put8(uint8_t val)
	{
		fwrite(&val, 1, 1, fp);
	}
	void _put16(uint16_t val)
	{
		_put8(val & 0xFF);
		_put8(val >> 8);
	}
	void _put32(uint32_t val)
	{
		_put8(val & 0xFF);
		_put8(val >> 8);
		_put8(val >> 16);
		_put8(val >> 24);
	}

	// Create header
	fwrite(MAGIC_STR, MAGIC_STR_LEN, 1, fp);
	_put16(0);	// Class count
	_put16(0);	// Type count
	_put16(0);	// Global count
	_put16(0);	// Function count, to be filled
	_put16(0);	// String count
	_put32(0);	// String table offset
	// TODO: Variant info

	int _putfcn_hdr(tScript_Function *fcn)
	{
		TRACE("Function %s at 0x%lx", fcn->Name, ftell(fp));
		#define BYTES_PER_FCNHDR(argc)	(2+4+4+2+2+(argc)*4)
		_put16( StringList_GetString(&strings, fcn->Name, strlen(fcn->Name)) );
		_put32( 0 );	// Code offset (filled later)
		_put32( 0 );	// Code length
		
		// Prototype
		_put16( Bytecode_int_GetTypeIdx(Script, fcn->Prototype.ReturnType) );
		uint8_t	flags = 0;
		if(fcn->Prototype.bVariableArgs)	flags |= 0x1;
		_put8(flags);
		_put16( fcn->ArgumentCount );
		for( int i = 0; i < fcn->ArgumentCount; i ++ )
		{
			_put16( StringList_GetString(&strings,
				fcn->ArgNames[i], strlen(fcn->ArgNames[i]))
				);
			_put16( Bytecode_int_GetTypeIdx(Script, fcn->Prototype.Args[i]) );
		}
		return 0;
	}

	int _putfcn_code(tScript_Function *fcn, size_t hdr_ofs)
	{
		if( !fcn->BCFcn )
			Bytecode_ConvertFunction(Script, fcn);
		if( !fcn->BCFcn )
			return 1;
	
		off_t	code_pos;
		int	len = 0;
		void	*code;
	
		// Serialise bytecode and get file position	
		code = Bytecode_SerialiseFunction(fcn->BCFcn, &len, &strings);
		code_pos = ftell(fp);
		
		// Update header
		fseek(fp, hdr_ofs + 2, SEEK_SET);
		_put32(code_pos);
		_put32(len);
		fseek(fp, code_pos, SEEK_SET);
		
		// Write code
		fwrite(code, len, 1, fp);
		free(code);
		return 0;
	}

	// Pre-scan types (ensure all types used are in the table)
	for(fcn = Script->Functions; fcn; fcn = fcn->Next, fcn_count ++)
	{
		Bytecode_int_GetTypeIdx(Script, fcn->Prototype.ReturnType);
		for( int i = 0; i < fcn->ArgumentCount; i ++ )
			Bytecode_int_GetTypeIdx(Script, fcn->Prototype.Args[i]);
	}
	for( sc = Script->FirstClass; sc; sc = sc->Next, class_count ++ )
	{
		for(tScript_Var *at = sc->FirstProperty; at; at = at->Next)
		{
			Bytecode_int_GetTypeIdx(Script, at->Type);
		}
		for(fcn = sc->FirstFunction; fcn; fcn = fcn->Next)
		{
			Bytecode_int_GetTypeIdx(Script, fcn->Prototype.ReturnType);
			for( int i = 0; i < fcn->ArgumentCount; i ++ )
				Bytecode_int_GetTypeIdx(Script, fcn->Prototype.Args[i]);
		}
	}

	// 1. Class definitions (Name + Function/attrib counts)
	for( sc = Script->FirstClass; sc; sc = sc->Next )
	{
		 int	n_methods = 0, n_attributes = 0;

		for(fcn = sc->FirstFunction; fcn; fcn = fcn->Next)
			n_methods ++;
		for(tScript_Var *at = sc->FirstProperty; at; at = at->Next)
			n_attributes ++;		

		TRACE("Class %s %i,%i", sc->Name, n_attributes, n_methods);
		_put16( StringList_GetString(&strings, sc->Name, strlen(sc->Name)) );
		_put16( n_attributes );	// Attribute count
		_put16( n_methods );	// Method count
	}

	for( tScript_Var *g = Script->FirstGlobal; g; g = g->Next )
		Bytecode_int_GetTypeIdx(Script, g->Type);

	// 2. Type table
	const int type_count = Script->BCTypeCount;
	for( int i = 0; i < Script->BCTypeCount; i ++ )
	{
		TRACE("Type %i: %s", i, SpiderScript_GetTypeName(Script, Script->BCTypes[i]));
		_ASSERT_R( Script->BCTypes[i].ArrayDepth, <, 256, -1 );
		_put8( Script->BCTypes[i].ArrayDepth );
		if( Script->BCTypes[i].Def == NULL ) {
			_put8(SS_TYPECLASS_CORE);
			_put16(SS_DATATYPE_NOVALUE);
			continue ;
		}
		_put8( Script->BCTypes[i].Def->Class );
		switch( Script->BCTypes[i].Def->Class )
		{
		case SS_TYPECLASS_CORE:
			_put16(Script->BCTypes[i].Def->Core);
			break;
		case SS_TYPECLASS_NCLASS: {
			tSpiderClass *nc = Script->BCTypes[i].Def->NClass;
			_put16( StringList_GetString(&strings, nc->Name, strlen(nc->Name)) );
			break; }
		case SS_TYPECLASS_SCLASS:
			sc = Script->BCTypes[i].Def->SClass;
			_put16( StringList_GetString(&strings, sc->Name, strlen(sc->Name)) );
			break;
		case SS_TYPECLASS_FCNPTR:
			_ASSERT_R( Script->BCTypes[i].Def->Class, !=, SS_TYPECLASS_FCNPTR, -1 );
			break;
		case SS_TYPECLASS_GENERIC: {
			_ASSERT_R( Script->BCTypes[i].Def->Class, !=, SS_TYPECLASS_GENERIC, -1 );
			break; }
		}
	}

	// Globals
	for( tScript_Var *g = Script->FirstGlobal; g; g = g->Next, num_globals ++ )
	{
		_put16( StringList_GetString(&strings, g->Name, strlen(g->Name)) );
		_put16( Bytecode_int_GetTypeIdx(Script, g->Type) );
	}

	// Create function descriptors
	fcn_hdr_offset = ftell(fp);
	for(fcn = Script->Functions; fcn; fcn = fcn->Next)
	{
		if( _putfcn_hdr(fcn) )
			return 2;
	}
	
	for( sc = Script->FirstClass; sc; sc = sc->Next )
	{
		for(tScript_Var *at = sc->FirstProperty; at; at = at->Next)
		{
			_put16( StringList_GetString(&strings, at->Name, strlen(at->Name)) );
			_put16( Bytecode_int_GetTypeIdx(Script, at->Type) );
		}
		for(fcn = sc->FirstFunction; fcn; fcn = fcn->Next)
		{
			if( _putfcn_hdr(fcn) )
				return 2;
		}
		
		// Code pointers will be filled in later
	}

	// Put function code in
	for(fcn = Script->Functions; fcn; fcn = fcn->Next)
	{
		if( _putfcn_code(fcn, fcn_hdr_offset) )
			return 1;
		fcn_hdr_offset += BYTES_PER_FCNHDR(fcn->ArgumentCount);
	}
	// Class member code
	for( sc = Script->FirstClass; sc; sc = sc->Next )
	{
		for( tScript_Var *at = sc->FirstProperty; at; at = at->Next )
			fcn_hdr_offset += 2*2;
		
		for(fcn = sc->FirstFunction; fcn; fcn = fcn->Next)
		{
			if( _putfcn_code(fcn, fcn_hdr_offset) )
				return 1;
			fcn_hdr_offset += BYTES_PER_FCNHDR(fcn->ArgumentCount);
		}
	}

	// String table
	strtab_ofs = ftell(fp);
	{
		 int	string_offset = strtab_ofs + (4+4)*strings.Count;
		tString	*str;
		// Array
		for(str = strings.Head; str; str = str->Next)
		{
			_put32(str->Length);
			_put32(string_offset);
			string_offset += str->Length + 1;
		}
		// Data
		for(str = strings.Head; str;)
		{
			tString	*nextstr = str->Next;
			fwrite(str->Data, str->Length, 1, fp);
			_put8(0);	// NULL separator
			free(str);
			str = nextstr;
		}
		strings.Head = NULL;
		strings.Tail = NULL;
	}

	// Fix header
	fseek(fp, MAGIC_STR_LEN, SEEK_SET);
	_put16(class_count);
	_put16(type_count);
	_put16(num_globals);
	_put16(fcn_count);
	_put16(strings.Count);
	_put32(strtab_ofs);
	assert(type_count == Script->BCTypeCount);
	
	TRACE("Header %ic %it %ig %if %is@%x",
		class_count, Script->BCTypeCount, num_globals, fcn_count, strings.Count, strtab_ofs);

	return 0;
}

int StringList_GetString(tStringList *List, const char *String, int Length)
{
	 int	strIdx = 0;
	tString	*ent;
	for(ent = List->Head; ent; ent = ent->Next, strIdx ++)
	{
		if(ent->Length == Length && memcmp(ent->Data, String, Length) == 0)
			break;
	}
	if( ent ) {
		ent->RefCount ++;
		TRACE("String %i '%.*s' reused", strIdx, Length, String);
	}
	else {
		ent = malloc(sizeof(tString) + Length + 1);
		if(!ent)	return -1;
		ent->Next = NULL;
		ent->Length = Length;
		ent->RefCount = 1;
		memcpy(ent->Data, String, Length);
		ent->Data[Length] = '\0';
		
		if(List->Head)
			List->Tail->Next = ent;
		else
			List->Head = ent;
		List->Tail = ent;
		List->Count ++;
		TRACE("String %i '%.*s' registered", strIdx, Length, String);
	}
	return strIdx;
}

int Bytecode_int_Serialize(const tBC_Function *Function, void *Output, int *LabelOffsets, tStringList *Strings)
{
	 int	len = 0, idx = 0;

	void _put_byte(uint8_t byte)
	{
		uint8_t	*buf = Output;
		if(Output)	buf[len] = byte;
		len ++;
	}

	void _put_dword(uint32_t value)
	{
		uint8_t	*buf = Output;
		if(Output) {
			buf[len+0] = value & 0xFF;
			buf[len+1] = value >> 8;
			buf[len+2] = value >> 16;
			buf[len+3] = value >> 24;
		}
		len += 4;
	}

	void _put_packedint_u(uint64_t value)
	{
		while( value >> 7 )
		{
			_put_byte( (value & 0x7F) | 0x80 );
			value >>= 7;
		}
		_put_byte( value );
	}
	void _put_packedint_s(int64_t value)
	{
		if( value < 0 )
			_put_packedint_u( ((-value) << 1) | 1 );
		else
			_put_packedint_u( value << 1 );
	}

	void _put_index(uint32_t value)
	{
		_put_packedint_u(value);
	}	

	void _put_double(double value)
	{
		// TODO: Machine agnostic
		if(Output) {
			*(double*)( (char*)Output + len ) = value;
		}
		len += sizeof(double);
	}

	void _put_string(const char *str, int len)
	{
		 int	strIdx = 0;
		if( Output ) {
			strIdx = StringList_GetString(Strings, str, len);
		}
	
		// TODO: Relocations?
		_put_index(strIdx);
	}

//	printf("Function->LabelCount = %i\n", Function->LabelCount);
	_put_index(Function->LabelCount);
	_put_index(Function->MaxRegisters);
	_put_index(Function->MaxGlobalCount);
	for( int i = 0; i < Function->LabelCount; i ++ )
	{
		if( Output )
			_put_index(LabelOffsets[i]);
		else
			len += 4;
	}

	for( tBC_Op *op = Function->Operations; op; op = op->Next, idx ++ )
	{
		// If first run, convert labels into instruction offsets
		if( !Output )
		{
			for( int i = 0; i < Function->LabelCount; i ++ )
			{
				if(LabelOffsets[i])	continue;
				if(op != Function->Labels[i])	continue;
				
				LabelOffsets[i] = idx;
			}
		}

		assert(op->Operation < 256);
		_put_byte(op->Operation);
		switch(op->Operation)
		{
		// Special case for inline values
		case BC_OP_LOADINT:
			_put_index(op->DstReg);
			_put_packedint_s(op->Content.Integer);
			break;
		case BC_OP_LOADREAL:
			_put_index(op->DstReg);
			_put_double(op->Content.Real);
			break;
		// Function calls are special
		case BC_OP_CALLFUNCTION:
		case BC_OP_CREATEOBJ:
		case BC_OP_CALLMETHOD:
			_put_index(op->DstReg);
			_put_index(op->Content.Function.ID);
			_put_index(op->Content.Function.ArgCount);
			for( int i = 0; i < (op->Content.Function.ArgCount&0xFF); i ++ )
				_put_index(op->Content.Function.ArgRegs[i]);
			break;
		case BC_OP_NOTEPOSITION:
			_put_index(op->DstReg);
			_put_string(op->Content.RefStr->Data, strlen(op->Content.RefStr->Data));
			break;
		// Everthing else just gets handled nicely
		default:
			switch( caOpEncodingTypes[op->Operation] )
			{
			case BC_OPENC_UNK:
				assert( caOpEncodingTypes[op->Operation] != BC_OPENC_UNK );
				break;
			case BC_OPENC_NOOPRS:
				break;
			case BC_OPENC_REG1:
				_put_index(op->DstReg);
				break;
			case BC_OPENC_REG2:
				_put_index(op->DstReg);
				_put_index(op->Content.RegInt.RegInt2);
				break;
			case BC_OPENC_REG3:
				_put_index(op->DstReg);
				_put_index(op->Content.RegInt.RegInt2);
				_put_index(op->Content.RegInt.RegInt3);
				break;
			case BC_OPENC_STRING:
				_put_index(op->DstReg);
				_put_string(op->Content.String.Data, op->Content.String.Length);
				break;
			}
			break;
		}
	}

	return len;
}

char *Bytecode_SerialiseFunction(const tBC_Function *Function, int *Length, tStringList *Strings)
{
	 int	len;
	 int	*label_offsets;
	char	*code;

	label_offsets = calloc( sizeof(int), Function->LabelCount );
	if(!label_offsets)	return NULL;

	len = Bytecode_int_Serialize(Function, NULL, label_offsets, Strings);

	code = malloc(len);

	// Update length to the correct length (may decrease due to encoding)	
	len = Bytecode_int_Serialize(Function, code, label_offsets, Strings);

	free(label_offsets);

	*Length = len;

	return code;
}


typedef struct
{
	const void	*Data;
	 int	Ofs;
	size_t	Length;
} t_bi;

static inline uint8_t buf_get8(t_bi *Bi)
{
	if( Bi->Ofs == Bi->Length )	return 0;
	const uint8_t	*buf = Bi->Data;
	uint8_t rv = buf[Bi->Ofs++];
	return rv;
}

static inline uint64_t buf_get_packed(t_bi *Bi)
{
	uint8_t	b = buf_get8(Bi);
	uint64_t rv = 0;
	 int	shift = 0;
	while( (b & 0x80) )
	{
		rv |= (uint64_t)(b & 0x7F) << shift;
		b = buf_get8(Bi);
		shift += 7;
	}
	rv |= (uint64_t)(b & 0x7F) << shift;
	return rv;
}
static inline uint32_t buf_get_index(t_bi *Bi)
{
	return buf_get_packed(Bi);
}

static inline int64_t buf_get_signed(t_bi *Bi)
{
	uint64_t u = buf_get_packed(Bi);
	if(u & 1)
		return -(u >> 1);
	else
		return (u >> 1);
}

double buf_get_double(t_bi *Bi)
{
	// TODO: Make machine agnostic
	double rv = *(const double*)( (const char*)Bi->Data + Bi->Ofs);
	Bi->Ofs += sizeof(double);
	return rv;
}


tBC_Function *Bytecode_DeserialiseFunction(const void *Data, size_t Length, t_loadstate *State)
{
	tBC_Op	*op;
	t_bi	bi, *Bi = &bi;
	bi.Data = Data;
	bi.Ofs = 0;
	bi.Length = Length;

	tBC_Function	*ret = malloc( sizeof(tBC_Function) );
	ret->LabelCount = buf_get_index(Bi);
	ret->MaxRegisters = buf_get_index(Bi);
	ret->MaxGlobalCount = buf_get_index(Bi);
	ret->Labels = malloc( sizeof(ret->Labels[0]) * ret->LabelCount );
	ret->Operations = NULL;
	ret->OperationsEnd = NULL;
//	printf("%i labels\n", ret->LabelCount);
	for( int i = 0; i < ret->LabelCount; i ++ )
	{
		// HACK - Save integer label offsets until second pass
		ret->Labels[i] = (void*) (intptr_t) buf_get_index(Bi);
	}

	while( bi.Ofs < Length )
	{
		unsigned int	ot = buf_get8(Bi);
		if( ot > BC_OP_EXCEPTION_POP ) {
			// Oops?
			continue ;
		}
		op = NULL;
		switch( ot )
		{
		// Special case for inline values
		case BC_OP_LOADINT:
			op = malloc(sizeof(tBC_Op));
			op->DstReg = buf_get_index(Bi);
			_ASSERT_G(op->DstReg,<,ret->MaxRegisters,_err);
			op->Content.Integer = buf_get_signed(Bi);
			break;
		case BC_OP_LOADREAL:
			op = malloc(sizeof(tBC_Op));
			op->DstReg = buf_get_index(Bi);
			_ASSERT_G(op->DstReg,<,ret->MaxRegisters,_err);
			op->Content.Real = buf_get_double(Bi);
			break;
		case BC_OP_NOTEPOSITION:
			op = malloc(sizeof(tBC_Op));
			op->DstReg = buf_get_index(Bi);
			op->Content.RefStr = NULL;
			break;
		// Function calls are specail
		case BC_OP_CALLFUNCTION:
		case BC_OP_CREATEOBJ:
		case BC_OP_CALLMETHOD: {
			 int	dstreg = buf_get_index(Bi);
			 int	fcnid = buf_get_index(Bi);
			 int	argc = buf_get_index(Bi);
			op = malloc( sizeof(tBC_Op) + (argc * sizeof(int)) );
			op->DstReg = dstreg;
			_ASSERT_G(op->DstReg,<,ret->MaxRegisters,_err);
			op->Content.Function.ID = fcnid;
			op->Content.Function.ArgCount = argc;
			for( int i = 0; i < (argc&0xFF); i ++ )
				op->Content.Function.ArgRegs[i] = buf_get_index(Bi);
			} break;
		// Everthing else just gets handled nicely
		default:
			switch( caOpEncodingTypes[ot] )
			{
			case BC_OPENC_UNK:
				_ASSERT_R(caOpEncodingTypes[ot], !=, BC_OPENC_UNK, NULL);
				break;
			case BC_OPENC_NOOPRS:
				op = malloc( sizeof(tBC_Op) );
				break;
			case BC_OPENC_REG1:
				op = malloc( sizeof(tBC_Op) );
				op->DstReg = buf_get_index(Bi);
				_ASSERT_G(op->DstReg,<,ret->MaxRegisters,_err);
				break;
			case BC_OPENC_REG2:
				op = malloc( sizeof(tBC_Op) );
				op->DstReg = buf_get_index(Bi);
				_ASSERT_G(op->DstReg,<,ret->MaxRegisters,_err);
				op->Content.RegInt.RegInt2 = buf_get_index(Bi);
				break;
			case BC_OPENC_REG3:
				op = malloc( sizeof(tBC_Op) );
				op->DstReg = buf_get_index(Bi);
				_ASSERT_G(op->DstReg,<,ret->MaxRegisters,_err);
				op->Content.RegInt.RegInt2 = buf_get_index(Bi);
				op->Content.RegInt.RegInt3 = buf_get_index(Bi);
				break;
			case BC_OPENC_STRING: {
				 int	dreg = buf_get_index(Bi);
				 int	sidx = buf_get_index(Bi);
				size_t	slen = _get_str(State, NULL, sidx);
				_ASSERT_R(slen, !=, -1, NULL);
				op = malloc(sizeof(tBC_Op) + slen + 1);
				op->DstReg = dreg;
				_ASSERT_G(op->DstReg,<,ret->MaxRegisters,_err);
				op->Content.String.Length = slen;
				_get_str(State, op->Content.String.Data, sidx);
				} break;
			}
			break;
		}
		assert(op);
		
		// Convert types
		// - Allows a bytecode file to be merged with another script
		{
			 int	t;
			switch(ot)
			{
			case BC_OP_LOADNULLREF:
			case BC_OP_CREATEARRAY:
				t = op->Content.RegInt.RegInt2;
				_ASSERT_G(t, <, State->NTypes, _err);
				t = Bytecode_int_GetTypeIdx(State->Script, State->Types[t]);
				op->Content.RegInt.RegInt2 = t;
				break;
			case BC_OP_CREATEOBJ:
				t = op->Content.Function.ID;
				_ASSERT_G(t, <, State->NTypes, _err);
				op->Content.Function.ID = Bytecode_int_GetTypeIdx(State->Script, State->Types[t]);
				break;
			}
		}
	
		op->Operation = ot;
		op->Next = NULL;
		op->CacheEnt = NULL;
		if( ret->Operations )
			ret->OperationsEnd->Next = op;
		else
			ret->Operations = op;
		ret->OperationsEnd = op;
	}
	
	// Fix labels
	for( int i = 0; i < ret->LabelCount; i ++ )
	{
		 int	idx = 0;
		const int	labelidx = (intptr_t)ret->Labels[i];
		for( op = ret->Operations; op && idx != labelidx; op = op->Next )
			idx ++;
		if( !op ) {
			fprintf(stderr, "Function label #%i is out of range (%i out of 0..%i)\n", i,
				labelidx, idx);
			return NULL;
		}
		ret->Labels[i] = op;
	}

	return ret;
_err:
	free(op);
	// TODO: Free function ops
	free(ret);
	return NULL;
}

