/*
 * SpiderScript Library
 * by John Hodge (thePowersGang)
 * 
 * bytecode_makefile.c
 * - Generate a bytecode file
 */
#include <stdlib.h>
#include "ast.h"
#include "bytecode_gen.h"
#include <stdio.h>
#include <string.h>

#define MAGIC_STR	"SSBC\r\n\xBC\x56"
#define MAGIC_STR_LEN	(sizeof(MAGIC_STR)-1)

#define _ASSERT(l,rel,r,rv) do{if(!(l rel r)){\
	fprintf(stderr,"%s:%i: ASSERT %s [%li] %s %s [%li] failed\n", __FILE__, __LINE__, #l, (long)(l), #rel, #r, (long)(r));\
	return rv;}}while(0)


// === TYPES ===
typedef struct
{
	uint32_t	Length;
	uint32_t	Offset;
} t_lenofs;
typedef struct
{
	FILE	*FP;
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
extern tBC_Function	*Bytecode_DeserialiseFunction(const void *Data, size_t Length, t_loadstate *State);

// === GLOBALS ===

// === CODE ===
static inline uint8_t _get8(t_loadstate *State)
{
	uint8_t	rv;
	fread(&rv, 1, 1, State->FP);
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
	_ASSERT(StringID, <, State->NStr, -1);
	if( Dest )
	{
		off_t saved_pos = ftell(State->FP);
		fseek(State->FP, State->Strings[StringID].Offset, SEEK_SET);
		fread(Dest, State->Strings[StringID].Length, 1, State->FP);
		Dest[ State->Strings[StringID].Length ] = '\0';
		fseek(State->FP, saved_pos, SEEK_SET);
//		printf("String '%s' read\n", Dest);
	}
	return State->Strings[StringID].Length;
}
tSpiderTypeRef _get_type(t_loadstate *State, int TypeID)
{
	tSpiderTypeRef	retz = {0,0};
	_ASSERT(TypeID, <, State->NTypes, retz);

	return State->Types[TypeID];
}

tScript_Function *_get_fcn(t_loadstate *State)
{
	tScript_Function *ret;
	
	// Load main function header
	 int	namestr  = _get32(State);
	off_t	code_ofs = _get32(State);
	size_t	code_len = _get32(State);
	 int	ret_type = _get32(State);
	 int	n_args   = _get8(State);
	_get8(State); _get8(State); _get8(State);

	_ASSERT(code_ofs, <, State->FileSize, NULL);
	_ASSERT(code_ofs+code_len, <, State->FileSize, NULL);

//	printf("namestr = %i, code_ofs = %x, code_len = %x, ret_type = %x, n_args = %i\n",
//		namestr, (unsigned int)code_ofs, (unsigned int)code_len, ret_type, n_args);

	struct {
		uint32_t	Name;
		uint32_t	Type;
	} args[n_args];

	// Get size of function metadata in memory (and load arguments)
	 int	datasize = 0;
	datasize += sizeof(tScript_Function);
	datasize += n_args * sizeof(ret->Arguments[0]);
	datasize += _get_str(State, NULL, namestr) + 1;
	for( int i = 0; i < n_args; i ++ )
	{
		args[i].Name = _get32(State);
		args[i].Type = _get32(State);
		datasize += _get_str(State, NULL, args[i].Name) + 1;
//		printf(" Arg %i: Name=%i,Type=%x\n", i, args[i].Name, args[i].Type);
	}

	// Create and populate metadata structure
	ret = malloc( datasize );
	ret->Next = NULL;
	ret->Name = (void*)&ret->Arguments[n_args];
	_get_str(State, ret->Name, namestr);
	ret->ArgumentCount = n_args;
	ret->ReturnType = _get_type(State, ret_type);
	ret->ASTFcn = NULL;
	char *nameptr = ret->Name + _get_str(State, NULL, namestr) + 1;
	for( int i = 0; i < n_args; i ++ )
	{
		ret->Arguments[i].Name = nameptr;
		int len = _get_str(State, nameptr, args[i].Name);
		ret->Arguments[i].Type = _get_type(State, args[i].Type);
		nameptr += len + 1;
//		printf(" Arg %i: Name=\"%s\"\n", i, ret->Arguments[i].Name);
	}
	
	// Load code
	off_t old_pos = ftell(State->FP);
	void *code = malloc(code_len);
	fseek(State->FP, code_ofs, SEEK_SET);
	fread(code, code_len, 1, State->FP);
	fseek(State->FP, old_pos, SEEK_SET);

	#if 0
	for( int i = 0; i < code_len; i ++ )
	{
		uint8_t	*buf = code;
		printf("%02x ", buf[i]);
		if( (i & 15) == 15 )	printf("\n");
		else if( (i & 7) == 7 )	printf(" ");
	}
	printf("\n");
	#endif

	// TODO: Parse back into bytecode
	ret->BCFcn = Bytecode_DeserialiseFunction(code, code_len, State);

	free(code);

	return ret;
}


int SpiderScript_int_LoadBytecode(tSpiderScript *Script, const char *SourceFile)
{
	t_loadstate	state, *State;
	FILE	*fp;
	State = &state;

	fp = fopen(SourceFile, "rb");
	if(!fp)	return -1;
	
	fseek(fp, 0, SEEK_END);
	off_t file_size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	State->FP = fp;
	State->Strings = NULL;
	State->NStr = 0;
	State->FileSize = file_size;

	// Check magic
	_ASSERT(file_size, >, MAGIC_STR_LEN + 4*4, 1);
	{
		char magic[MAGIC_STR_LEN];
		fread(magic, MAGIC_STR_LEN, 1, fp);
		if( memcmp(magic, MAGIC_STR, sizeof(magic)) != 0 )
			return -1;
	}
	
	
	// Get counts
	 int	n_class = _get32(State);
	 int	n_types = _get32(State);
	 int	n_fcn   = _get32(State);
	 int	n_str   = _get32(State);
	off_t	ofs_str = _get32(State);
	// TODO: Validation	

//	printf("n_fcn = %i, n_class = %i, n_str = %i\n", n_fcn, n_class, n_str);

	// Load string table
	_ASSERT(ofs_str + n_str*(4+4), <=, file_size, 1);
	t_lenofs strings[n_str];
	fseek(fp, ofs_str, SEEK_SET);
	for( int i = 0; i < n_str; i ++ )
	{
		strings[i].Length = _get32(State);
		strings[i].Offset = _get32(State);
		_ASSERT(strings[i].Offset, <, file_size, 1);
		_ASSERT(strings[i].Offset + strings[i].Length, <=, file_size, 1);
	}
	fseek(fp, MAGIC_STR_LEN+4*4, SEEK_SET);	
	State->Strings = strings;
	State->NStr = n_str;
	
	State->NClasses = n_class;
	State->Classes = malloc(n_class * sizeof(*State->Classes));
//	printf("State->Strings = %p, State->NStr = %i\n", State->Strings, State->NStr);
	
	// Deserialise class definitions
	for( int i = 0; i < n_class; i ++ )
	{
		tScript_Class	*sc;
		int namestr = _get32(State);
		int n_attrib = _get16(State);
		int n_method = _get16(State);

		sc = malloc( sizeof(tScript_Class) + _get_str(State, NULL, namestr) + 1 );
		if(!sc)	return -1;

		sc->Next = NULL;
		_get_str(State, sc->Name, namestr);
		sc->FirstFunction = NULL;
		sc->FirstProperty = NULL;
		sc->nProperties = n_attrib;
		sc->TypeInfo.Class = SS_TYPECLASS_SCLASS;
		sc->TypeInfo.SClass = sc;

		State->Classes[i].Class = sc;
		State->Classes[i].NMethods = n_method;
		State->Classes[i].NAttribs = n_attrib;

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
		case SS_TYPECLASS_SCLASS:
			_ASSERT(idx, <, n_class, -1);
			State->Types[i].Def = &State->Classes[idx].Class->TypeInfo;
			break;
		case SS_TYPECLASS_NCLASS: {
			char str[_get_str(State, NULL, idx)];
			_get_str(State, str, idx);
			State->Types[i].Def = SpiderScript_ResolveObject(NULL, NULL, str);
			break; }
		}
	}
	
	// Parse functions
	for( int i = 0; i < n_fcn; i ++ )
	{
		tScript_Function *fcn;
		fcn = _get_fcn(State);

//		printf("Loaded function '%s'\n", fcn->Name);
	
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
			tScript_Var *at;
			int name = _get32(State);
			int type = _get32(State);
		
			at = malloc( sizeof(*at) + _get_str(State, NULL, name) + 1 );
			at->Next = NULL;
			at->Type = _get_type(State, type);
			_get_str(State, at->Name, name);

			if( sc->FirstProperty )
				sc->LastProperty->Next = at;
			else
				sc->FirstProperty = at;
			sc->LastProperty = at;
		}
		
		// Functions
		for( int j = 0; j < State->Classes[i].NMethods; j ++ )
		{
			tScript_Function *fcn;
			
			fcn = _get_fcn(State);
//			printf("Added method '%s' of '%s'\n", fcn->Name, sc->Name);
			if( sc->FirstFunction )
				sc->LastFunction->Next = fcn;
			else
				sc->FirstFunction = fcn;
			sc->LastFunction = fcn;
		}
	}

	return 0;
}

int SpiderScript_SaveBytecode(tSpiderScript *Script, const char *DestFile)
{
	tStringList	strings = {0};
	tScript_Function	*fcn;
	tScript_Class	*sc;
	FILE	*fp;
	 int	fcn_hdr_offset = 0;
	 int	fcn_count = 0, class_count = 0;
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

	fp = fopen(DestFile, "wb");
	if(!fp)	return 1;
	// Create header
	fwrite(MAGIC_STR, MAGIC_STR_LEN, 1, fp);
	_put32(0);	// Class count
	_put32(0);	// Type count
	_put32(0);	// Function count, to be filled
	_put32(0);	// String count
	_put32(0);	// String table offset
	// TODO: Variant info

	int _putfcn_hdr(tScript_Function *fcn)
	{
		#define BYTES_PER_FCNHDR(argc)	(4+4+4+4+1+3+(argc)*8)
		_put32( StringList_GetString(&strings, fcn->Name, strlen(fcn->Name)) );
		_put32( 0 );	// Code offset (filled later)
		_put32( 0 );	// Code length
		_put32( Bytecode_int_GetTypeIdx(Script, fcn->ReturnType) );
		
		if(fcn->ArgumentCount > 255) {
			// ERROR: Too many args
			fclose(fp);
			return 2;
		}
		_put8( fcn->ArgumentCount );
		_put8( 0 ); _put8( 0 ); _put8( 0 );	// Padding

		// Argument types?
		for( int i = 0; i < fcn->ArgumentCount; i ++ )
		{
			_put32( StringList_GetString(&strings,
				fcn->Arguments[i].Name, strlen(fcn->Arguments[i].Name))
				);
			_put32( Bytecode_int_GetTypeIdx(Script, fcn->Arguments[i].Type) );
		}
		return 0;
	}

	int _putfcn_code(tScript_Function *fcn, size_t hdr_ofs)
	{
		if( !fcn->BCFcn )
			Bytecode_ConvertFunction(Script, fcn);
		if( !fcn->BCFcn )
		{
			fclose(fp);
			return 1;
		}
	
		off_t	code_pos;
		int	len = 0;
		void	*code;
	
		// Serialise bytecode and get file position	
		code = Bytecode_SerialiseFunction(fcn->BCFcn, &len, &strings);
		code_pos = ftell(fp);
		
		// Update header
		fseek(fp, hdr_ofs + 4, SEEK_SET);
		_put32(code_pos);
		_put32(len);
		fseek(fp, code_pos, SEEK_SET);
		
		// Write code
		fwrite(code, len, 1, fp);
		free(code);
		return 0;
	}

	// Pre-scan types (ensure all types used are in the table)
	for(fcn = Script->Functions; fcn; fcn = fcn->Next)
	{
		Bytecode_int_GetTypeIdx(Script, fcn->ReturnType);
		for( int i = 0; i < fcn->ArgumentCount; i ++ )
			Bytecode_int_GetTypeIdx(Script, fcn->Arguments[i].Type);
	}
	for( sc = Script->FirstClass; sc; sc = sc->Next, class_count ++ )
	{
		for(tScript_Var *at = sc->FirstProperty; at; at = at->Next)
		{
			Bytecode_int_GetTypeIdx(Script, at->Type);
		}
	}

	// Create function descriptors
	fcn_hdr_offset = ftell(fp);
	for(fcn = Script->Functions; fcn; fcn = fcn->Next, fcn_count ++)
	{
		if( _putfcn_hdr(fcn) )
			return 2;
	}
	
	for( sc = Script->FirstClass; sc; sc = sc->Next, class_count ++ )
	{
		 int	n_methods = 0, n_attributes = 0;

		for(fcn = sc->FirstFunction; fcn; fcn = fcn->Next)
			n_methods ++;
		for(tScript_Var *at = sc->FirstProperty; at; at = at->Next)
			n_attributes ++;		

		_put32( StringList_GetString(&strings, sc->Name, strlen(sc->Name)) );
		_put16( n_attributes );	// Attribute count
		_put16( n_methods );	// Method count
	
		for(tScript_Var *at = sc->FirstProperty; at; at = at->Next)
		{
			_put32( StringList_GetString(&strings, at->Name, strlen(at->Name)) );
			_put32( Bytecode_int_GetTypeIdx(Script, at->Type) );
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
	for( sc = Script->FirstClass; sc; sc = sc->Next, class_count ++ )
	{
		fcn_hdr_offset += 4+2+2;	// Object header
		
		for( tScript_Var *at = sc->FirstProperty; at; at = at->Next )
			fcn_hdr_offset += 4+4;
		
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
	fseek(fp, 8, SEEK_SET);
	_put32(fcn_count);
	_put32(class_count);
	_put32(strings.Count);
	_put32(strtab_ofs);

	fclose(fp);

	return 0;
}

int StringList_GetString(tStringList *List, const char *String, int Length)
{
	 int	strIdx = 0;
	tString	*ent;
	for(ent = List->Head; ent; ent = ent->Next, strIdx ++)
	{
		if(ent->Length == Length && memcmp(ent->Data, String, Length) == 0)	break;
	}
	if( ent ) {
		ent->RefCount ++;
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
	}
	return strIdx;
}

int Bytecode_int_Serialize(const tBC_Function *Function, void *Output, int *LabelOffsets, tStringList *Strings)
{
	tBC_Op	*op;
	 int	len = 0, idx = 0;
	 int	i;

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

	void _put_index(uint32_t value)
	{
		if( !Output && !value ) {
			len += 5;
			return ;
		}
		if( value < 0x8000 ) {
			_put_byte(value >> 8);
			_put_byte(value & 0xFF);
		}
		else if( value < 0x400000 ) {
			_put_byte( (value >> 16) | 0x80 );
			_put_byte(value >> 8);
			_put_byte(value & 0xFF);
		}
		else {
			_put_byte( 0xC0 );
			_put_byte(value >> 24);
			_put_byte(value >> 16);
			_put_byte(value >> 8 );
			_put_byte(value & 0xFF);
		}
	}	

	void _put_qword(uint64_t value)
	{
		if( value < 0x80 ) {	// 7 bits into 1 byte
			_put_byte(value);
		}
		else if( !(value >> (8+6)) ) {	// 14 bits packed into 2 bytes
			_put_byte( 0x80 | ((value >> 8) & 0x3F) );
			_put_byte( value & 0xFF );
		}
		else if( !(value >> (32+5)) ) {	// 37 bits into 5 bytes
			_put_byte( 0xC0 | ((value >> 32) & 0x1F) );
			_put_dword(value & 0xFFFFFFFF);
		}
		else {
			_put_byte( 0xE0 );	// 64 (actually 68) bits into 9 bytes
			_put_dword(value & 0xFFFFFFFF);
			_put_dword(value >> 32);
		}
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
	for( i = 0; i < Function->LabelCount; i ++ )
	{
		_put_dword(LabelOffsets[i]);
	}

	for( op = Function->Operations; op; op = op->Next, idx ++ )
	{
		// If first run, convert labels into instruction offsets
		if( !Output )
		{
			for( i = 0; i < Function->LabelCount; i ++ )
			{
				if(LabelOffsets[i])	continue;
				if(op != Function->Labels[i])	continue;
				
				LabelOffsets[i] = idx;
			}
		}

		_put_byte(op->Operation);
		switch(op->Operation)
		{
		// Relocate jumps (the value only matters if `Output` is non-NULL)
		case BC_OP_JUMP:
		case BC_OP_JUMPIF:
		case BC_OP_JUMPIFNOT:
			// TODO: Relocations?
			_put_index( Output ? op->Content.StringInt.Integer : Function->LabelCount );
			break;
		// Special case for inline values
		case BC_OP_LOADINT:
			_put_qword(op->Content.Integer);
			break;
		case BC_OP_LOADREAL:
			_put_double(op->Content.Real);
			break;
		case BC_OP_LOADSTR:
			_put_string(op->Content.StringInt.String, op->Content.StringInt.Integer);
			break;
		// Function calls are specail
		case BC_OP_CALLFUNCTION:
		case BC_OP_CREATEOBJ:
		case BC_OP_CALLMETHOD:
			_put_index(op->Content.Function.ID);
			_put_index(op->Content.Function.ArgCount);
			break;
		// Everthing else just gets handled nicely
		default:
			if( Bytecode_int_OpUsesString(op->Operation) )
				_put_string(op->Content.StringInt.String, strlen(op->Content.StringInt.String));
			if( Bytecode_int_OpUsesInteger(op->Operation) )
				_put_index(op->Content.StringInt.Integer);
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

static inline uint32_t buf_get32(t_bi *Bi)
{
	uint32_t rv = buf_get8(Bi);
	rv |= (uint32_t)buf_get8(Bi) << 8;
	rv |= (uint32_t)buf_get8(Bi) << 16;
	rv |= (uint32_t)buf_get8(Bi) << 24;
	return rv;
}
static inline uint32_t buf_get_index(t_bi *Bi)
{
	uint8_t	b = buf_get8(Bi);
	uint32_t rv = 0;
	if( b < 0x80 ) {
		rv |= (uint32_t)(b & 0x7F) << 8;
		rv |= buf_get8(Bi);
	}
	else if( b < 0xC0 ) {
		rv |= (uint32_t)(b & 0x3F) << 16;
		rv |= (uint32_t)buf_get8(Bi) << 8;
		rv |= (uint32_t)buf_get8(Bi) << 0;
	}
	else {
//			rv |= (uint32_t)(b & 0x3F) << 32;
		rv |= (uint32_t)buf_get8(Bi) << 24;
		rv |= (uint32_t)buf_get8(Bi) << 16;
		rv |= (uint32_t)buf_get8(Bi) << 8;
		rv |= (uint32_t)buf_get8(Bi) << 0;
	}
	return rv;
}	

static inline uint64_t buf_get_qword(t_bi *Bi)
{
	uint8_t	b = buf_get8(Bi);
	uint64_t rv = 0;
	if( b < 0x80 ) {	// 7 bits into 1 byte
		rv = b;
	}
	else if( b < 0xC0 ) {	// 14 bits packed into 2 bytes
		rv |= (uint32_t)(b & 0x3F) << 8;
		rv |= (uint32_t)buf_get8(Bi) << 0;
	}
	else if( b < 0xD0 ) {	// 37 bits into 5 bytes
		rv |= (uint64_t)(b & 0x1F) << 32;
		rv |= buf_get32(Bi);
	}
	else {
		rv |= buf_get32(Bi);
		rv |= (uint64_t)buf_get32(Bi) << 32;
	}
	return rv;
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
	tBC_Function	*ret;
	tBC_Op	*op;
	 int	sidx;
	size_t	slen;
	t_bi	bi, *Bi = &bi;
	bi.Data = Data;
	bi.Ofs = 0;
	bi.Length = Length;

	ret = malloc( sizeof(tBC_Function) );
	ret->LabelCount = buf_get_index(Bi);
	ret->Labels = malloc( sizeof(ret->Labels[0]) * ret->LabelCount );
	ret->Operations = NULL;
	ret->OperationsEnd = NULL;
	ret->VariableNames = NULL;
	ret->MaxVariableCount = 0;
//	printf("%i labels\n", ret->LabelCount);
	for( int i = 0; i < ret->LabelCount; i ++ )
	{
		// HACK - Save integer label offsets until second pass
		ret->Labels[i] = (void*) (intptr_t) buf_get32(Bi);
	}

	while( bi.Ofs < Length )
	{
		 int	ot = buf_get8(Bi);
		switch( ot )
		{
		// Special case for inline values
		case BC_OP_LOADINT:
			op = malloc(sizeof(tBC_Op));
			op->Content.Integer = buf_get_qword(Bi);
//			printf("LOADINT 0x%lx\n", op->Content.Integer);
			break;
		case BC_OP_LOADREAL:
			op = malloc(sizeof(tBC_Op));
			op->Content.Real = buf_get_double(Bi);
//			printf("LOADREAL %lf\n", op->Content.Real);
			break;
		case BC_OP_LOADSTR:
			sidx = buf_get_index(Bi);
			slen = _get_str(State, NULL, sidx);
			// TODO: Error check length
			op = malloc(sizeof(tBC_Op) + slen);
			op->Content.StringInt.Integer = slen;
			_get_str(State, op->Content.StringInt.String, sidx);
//			printf("LOADSTR %i bytes\n", op->Content.StringInt.Integer);
			break;
		// Function calls are specail
		case BC_OP_CALLFUNCTION:
		case BC_OP_CREATEOBJ:
		case BC_OP_CALLMETHOD:
			op = malloc( sizeof(tBC_Op) );
			op->Content.Function.ID = buf_get_index(Bi);
			op->Content.Function.ArgCount = buf_get_index(Bi);
			break;
		// Everthing else just gets handled nicely
		case BC_OP_JUMP:
		case BC_OP_JUMPIF:
		case BC_OP_JUMPIFNOT:
		default:
			if( Bytecode_int_OpUsesString(ot) ) {
				sidx = buf_get_index(Bi);
				slen = _get_str(State, NULL, sidx);
				op = malloc(sizeof(tBC_Op) + slen + 1);
				_get_str(State, op->Content.StringInt.String, sidx);
				op->Content.StringInt.String[slen] = 0;
			}
			else {
				slen = 0;
				op = malloc(sizeof(tBC_Op));
			}
			if( Bytecode_int_OpUsesInteger(ot) ) {
				op->Content.StringInt.Integer = buf_get_index(Bi);
			}
//			if( slen )
//				printf("%i '%s' %i\n",ot,op->Content.StringInt.String,op->Content.StringInt.Integer);
//			else
//				printf("%i %i\n",ot,op->Content.StringInt.Integer);
				
			break;
		}

		if( ot == BC_OP_LOADVAR || ot == BC_OP_SAVEVAR ) {
			if( op->Content.StringInt.Integer + 1 > ret->MaxVariableCount )
				ret->MaxVariableCount = op->Content.StringInt.Integer + 1;
		}
		if( ot == BC_OP_DEFINEVAR ) {
			 int	s = (op->Content.StringInt.Integer >> 24);
			if( s + 1 > ret->MaxVariableCount )
				ret->MaxVariableCount = s + 1;
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
		for( op = ret->Operations; op && idx != (intptr_t)ret->Labels[i]; op = op->Next )
			idx ++;
		if( !op ) {
			fprintf(stderr, "Function label %i is out of range (%i > %i)", i, (int)(intptr_t)ret->Labels[i], idx);
		}
		ret->Labels[i] = op;
	}

	return ret;
}

