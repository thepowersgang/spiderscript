/*
 * SpiderScript Library
 * by John Hodge (thePowersGang)
 * 
 * bytecode_gen.c
 * - Generate bytecode
 */
#include <stdlib.h>
#include <stdint.h>
#include "bytecode_ops.h"
#include <stdio.h>
#include "bytecode_gen.h"
#include <string.h>
#include "bytecode.h"
#include <assert.h>

// === IMPORTS ===

// === STRUCTURES ===

// === PROTOTYPES ===
tBC_Op	*Bytecode_int_AllocateOp(int Operation, int ExtraBytes);
 int	Bytecode_int_AddVariable(tBC_Function *Handle, const char *Name);

// === GLOBALS ===

// === CODE ===
tBC_Function *Bytecode_CreateFunction(tSpiderScript *Script, tScript_Function *Fcn)
{
	tBC_Function *ret;
	 int	i;

	ret = calloc(sizeof(tBC_Function), 1);
	if(!ret)	return NULL;
	ret->Script = Script;
	
#if 0
	ret->LabelSpace = ret->LabelCount = 0;
	ret->Labels = NULL;

	ret->MaxVariableCount = 0;
	ret->CurContextDepth = 0;	
	ret->VariableCount = ret->VariableSpace = 0;
	ret->VariableNames = NULL;

	ret->OperationCount = 0;
	ret->Operations = NULL;
#endif
	ret->OperationsEnd = (void*)&ret->Operations;

	for( i = 0; i < Fcn->ArgumentCount; i ++ )
	{
		Bytecode_int_AddVariable(ret, Fcn->Arguments[i].Name);
	}

	return ret;
}

void Bytecode_DeleteFunction(tBC_Function *Fcn)
{
	tBC_Op	*op;
	for( op = Fcn->Operations; op; )
	{
		tBC_Op	*nextop = op->Next;
		free(op);
		op = nextop;
	}
	if( Fcn->VariableNames )
		free(Fcn->VariableNames);
	if( Fcn->GlobalNames )
		free(Fcn->GlobalNames);
	free(Fcn->Labels);
	free(Fcn);
}

int Bytecode_AllocateLabel(tBC_Function *Handle)
{
	 int	ret;
	
	if( Handle->LabelCount == Handle->LabelSpace ) {
		void *tmp;
		Handle->LabelSpace += 20;	// TODO: Don't hardcode increment
		tmp = realloc(Handle->Labels, Handle->LabelSpace * sizeof(Handle->Labels[0]));
		if( !tmp ) {
			Handle->LabelSpace -= 20;
			return -1;
		}
		Handle->Labels = tmp;
	}
	ret = Handle->LabelCount ++;
	Handle->Labels[ret] = 0;
	return ret;
}

void Bytecode_SetLabel(tBC_Function *Handle, int Label)
{
	if(Label < 0)	return ;
	
	if(Label >= Handle->LabelCount)	return ;
	
	if( Handle->Labels[Label] != 0 ) {
		fprintf(stderr, "BUG - Re-setting of label %i\n", Label);
	}
	else
		Handle->Labels[Label] = Handle->OperationsEnd;
	return ;
}

void Bytecode_int_AppendOp(tBC_Function *Fcn, tBC_Op *Op)
{
	Op->Next = NULL;
	if( Fcn->Operations )
		Fcn->OperationsEnd->Next = Op;
	else
		Fcn->Operations = Op;
	Fcn->OperationsEnd = Op;
}

int _AddVariable(int *Count, int *Space, int Depth, const char ***NamesBuf, const char *Name, int *MaxVars)
{
	if(*Count == *Space) {
		void	*tmp;
		*Space += 10;
		tmp = realloc(*NamesBuf, *Space * sizeof(const char *));
		if(!tmp)	return -1;	// TODO: Error
		*NamesBuf = tmp;
	}
	(*NamesBuf)[*Count] = Name;
	(*Count) ++;
	// Get max count (used when executing to get the frame size)
	if(*Count - Depth >= *MaxVars)
		*MaxVars = *Count - Depth;
//	if( Name )
//		printf("_AddVariable: %s given %i\n", Name, *Count - Depth - 1);
	return *Count - Depth - 1;
	
}

int Bytecode_int_AddVariable(tBC_Function *Handle, const char *Name)
{
	return _AddVariable(&Handle->VariableCount, &Handle->VariableSpace, Handle->CurContextDepth,
		&Handle->VariableNames, Name, &Handle->MaxVariableCount);
}
int Bytecode_int_AddGlobal(tBC_Function *Handle, const char *Name)
{
	return _AddVariable(&Handle->GlobalCount, &Handle->GlobalSpace, Handle->CurContextDepth,
		&Handle->GlobalNames, Name, &Handle->MaxGlobalCount);
}

int _GetVarIndex(int Count, int Depth, const char **Names, const char *Name)
{
	for( int i = Count; i --; )
	{
		// Ignore (and account for) Start-of-block markers
		if( !Names[i] ) {
			Depth --;
			continue ;
		}
		if( strcmp(Name, Names[i]) == 0 )
			return i - Depth;
	}
	return -1;
}

int Bytecode_int_GetTypeIdx(tSpiderScript *Script, tSpiderTypeRef Type)
{
	for( int i = 0; i < Script->BCTypeCount; i ++ )
	{
		if( SS_TYPESEQUAL(Script->BCTypes[i], Type) ) {
			return i;
		}
	}
	if( Script->BCTypeCount == Script->BCTypeSpace )
	{
		Script->BCTypeSpace += 10;
		void *tmp = realloc(Script->BCTypes, Script->BCTypeSpace * sizeof(*Script->BCTypes));
		if(!tmp) {
			perror("Bytecode_int_GetTypeIdx");
			return -1;
		}
		Script->BCTypes = tmp;
	}
	
	memcpy(&Script->BCTypes[Script->BCTypeCount], &Type, sizeof(Type));
	
	return Script->BCTypeCount++;
}

int Bytecode_int_GetVarIndex(tBC_Function *Handle, const char *Name)
{
	 int	rv;
	
	// Locals
	rv = _GetVarIndex(Handle->VariableCount, Handle->CurContextDepth, Handle->VariableNames, Name);
	if( rv >= 0 ) {
//		printf("Variable %s in slot %i\n", Name, rv);
		return rv;
	}
	
	// Globals
	rv = _GetVarIndex(Handle->GlobalCount, Handle->CurContextDepth, Handle->GlobalNames, Name);
	if( rv >= 0 )
		return rv | 0x80;

	// TODO: Some form of error	
	return -1;
}

int Bytecode_int_OpUsesString(int Op)
{
	switch(Op)
	{
	case BC_OP_ELEMENT:
	case BC_OP_SETELEMENT:
	case BC_OP_CALLMETHOD:
	case BC_OP_CALLFUNCTION:
	case BC_OP_CREATEOBJ:
	case BC_OP_DEFINEVAR:
	case BC_OP_IMPGLOBAL:
		return 1;
	default:
		return 0;
	}
}

int Bytecode_int_OpUsesInteger(int Op)
{
	switch(Op)
	{
	case BC_OP_CALLMETHOD:
	case BC_OP_CALLFUNCTION:
	case BC_OP_CREATEOBJ:
	case BC_OP_CREATEARRAY:
	case BC_OP_JUMP:
	case BC_OP_JUMPIF:
	case BC_OP_JUMPIFNOT:
	case BC_OP_LOADVAR:
	case BC_OP_SAVEVAR:
	case BC_OP_CAST:
	case BC_OP_DEFINEVAR:
	case BC_OP_IMPGLOBAL:
	case BC_OP_LOADNULL:
		return 1;
	default:
		return 0;
	}
}

tBC_Op *Bytecode_int_AllocateOp(int Operation, int ExtraBytes)
{
	tBC_Op	*ret;

	ret = malloc(sizeof(tBC_Op) + ExtraBytes);
	if(!ret)	return NULL;

	ret->Next = NULL;
	ret->Operation = Operation;
	ret->CacheEnt = NULL;

	return ret;
}


#define DEF_BC_NONE(_op) { \
	tBC_Op *op = Bytecode_int_AllocateOp(_op, 0); \
	op->Content.Integer = 0; \
	if(Bytecode_int_OpUsesString(_op) || Bytecode_int_OpUsesInteger(_op)) printf("%s:%i - op BUG\n",__FILE__,__LINE__); \
	Bytecode_int_AppendOp(Handle, op);\
}

#define DEF_BC_INT(_op, _int) {\
	tBC_Op *op = Bytecode_int_AllocateOp(_op, 0);\
	op->Content.StringInt.Integer = _int;\
	if(Bytecode_int_OpUsesString(_op) || !Bytecode_int_OpUsesInteger(_op))\
		printf("%s:%i - op "#_op" not _INT\n",__FILE__,__LINE__); \
	Bytecode_int_AppendOp(Handle, op);\
}

#define DEF_BC_STRINT(_op, _str, _int) { \
	tBC_Op *op = Bytecode_int_AllocateOp(_op, strlen(_str));\
	op->Content.StringInt.Integer = _int;\
	strcpy(op->Content.StringInt.String, _str);\
	if(!Bytecode_int_OpUsesString(_op) || !Bytecode_int_OpUsesInteger(_op))\
		printf("%s:%i BUG - op "#_op" not _STRINT\n",__FILE__,__LINE__); \
	Bytecode_int_AppendOp(Handle, op);\
}
#define DEF_BC_STR(_op, _str) {\
	tBC_Op *op = Bytecode_int_AllocateOp(_op, strlen(_str));\
	strcpy(op->Content.StringInt.String, _str);\
	if(!Bytecode_int_OpUsesString(_op) || Bytecode_int_OpUsesInteger(_op))\
		printf("%s:%i BUG - op "#_op" not _STR\n",__FILE__,__LINE__); \
	Bytecode_int_AppendOp(Handle, op);\
}

// --- Flow Control
void Bytecode_AppendJump(tBC_Function *Handle, int Label)
	DEF_BC_INT(BC_OP_JUMP, Label)
void Bytecode_AppendCondJump(tBC_Function *Handle, int Label)
	DEF_BC_INT(BC_OP_JUMPIF, Label)
void Bytecode_AppendCondJumpNot(tBC_Function *Handle, int Label)
	DEF_BC_INT(BC_OP_JUMPIFNOT, Label)
void Bytecode_AppendReturn(tBC_Function *Handle)
	DEF_BC_NONE(BC_OP_RETURN);

// --- Variables
void Bytecode_AppendLoadVar(tBC_Function *Handle, const char *Name)
	DEF_BC_INT(BC_OP_LOADVAR, Bytecode_int_GetVarIndex(Handle, Name))
void Bytecode_AppendSaveVar(tBC_Function *Handle, const char *Name)	// (Obj->)?var = 
	DEF_BC_INT(BC_OP_SAVEVAR, Bytecode_int_GetVarIndex(Handle, Name))

// --- Constants
void Bytecode_AppendConstInt(tBC_Function *Handle, uint64_t Value)
{
	tBC_Op *op = Bytecode_int_AllocateOp(BC_OP_LOADINT, 0);
	op->Content.Integer = Value;
	Bytecode_int_AppendOp(Handle, op);
}
void Bytecode_AppendConstReal(tBC_Function *Handle, double Value)
{
	tBC_Op *op = Bytecode_int_AllocateOp(BC_OP_LOADREAL, 0);
	op->Content.Real = Value;
	Bytecode_int_AppendOp(Handle, op);
}
void Bytecode_AppendConstString(tBC_Function *Handle, const void *Data, size_t Length)
{
	tBC_Op *op = Bytecode_int_AllocateOp(BC_OP_LOADSTR, Length+1);
	op->Content.StringInt.Integer = Length;
	memcpy(op->Content.StringInt.String, Data, Length);
	op->Content.StringInt.String[Length] = 0;
	Bytecode_int_AppendOp(Handle, op);
}
void Bytecode_AppendConstNull(tBC_Function *Handle, tSpiderTypeRef Type)
	DEF_BC_INT(BC_OP_LOADNULL, Bytecode_int_GetTypeIdx(Handle->Script, Type))

// --- Indexing / Scoping
void Bytecode_AppendElement(tBC_Function *Handle, const char *Name)
	DEF_BC_STR(BC_OP_ELEMENT, Name)
void Bytecode_AppendSetElement(tBC_Function *Handle, const char *Name)
	DEF_BC_STR(BC_OP_SETELEMENT, Name)
void Bytecode_AppendIndex(tBC_Function *Handle)
	DEF_BC_NONE(BC_OP_INDEX)
void Bytecode_AppendSetIndex(tBC_Function *Handle)
	DEF_BC_NONE(BC_OP_SETINDEX);

void Bytecode_AppendCreateObj(tBC_Function *Handle, tSpiderTypeRef Type, int ArgumentCount)
{
	tBC_Op *op = Bytecode_int_AllocateOp(BC_OP_CREATEOBJ, 0);
	op->Content.Function.ID = Bytecode_int_GetTypeIdx(Handle->Script, Type);
	op->Content.Function.ArgCount = ArgumentCount;
	Bytecode_int_AppendOp(Handle, op);
}
void Bytecode_AppendMethodCall(tBC_Function *Handle, int Index, int ArgumentCount)
{
	tBC_Op *op = Bytecode_int_AllocateOp(BC_OP_CALLMETHOD, 0);
	op->Content.Function.ID = Index;
	op->Content.Function.ArgCount = ArgumentCount;
	Bytecode_int_AppendOp(Handle, op);
}
void Bytecode_AppendFunctionCall(tBC_Function *Handle, int ID, int ArgumentCount)
{
	tBC_Op *op = Bytecode_int_AllocateOp(BC_OP_CALLFUNCTION, 0);
	op->Content.Function.ID = ID;
	op->Content.Function.ArgCount = ArgumentCount;
	Bytecode_int_AppendOp(Handle, op);
}
void Bytecode_AppendCreateArray(tBC_Function *Handle, tSpiderTypeRef Type)
	DEF_BC_INT(BC_OP_CREATEARRAY, Bytecode_int_GetTypeIdx(Handle->Script, Type))

void Bytecode_AppendBinOp(tBC_Function *Handle, int Operation)
	DEF_BC_NONE(Operation)
void Bytecode_AppendUniOp(tBC_Function *Handle, int Operation)
	DEF_BC_NONE(Operation)
void Bytecode_AppendCast(tBC_Function *Handle, tSpiderTypeRef Type)
	DEF_BC_INT(BC_OP_CAST, Bytecode_int_GetTypeIdx(Handle->Script, Type))
void Bytecode_AppendDuplicate(tBC_Function *Handle)
	DEF_BC_NONE(BC_OP_DUPSTACK);
void Bytecode_AppendDelete(tBC_Function *Handle)
	DEF_BC_NONE(BC_OP_DELSTACK);

// Does some bookeeping to allocate variable slots at compile time
void Bytecode_AppendEnterContext(tBC_Function *Handle)
{
//	printf("_EnterContext: %i %i,%i\n", Handle->CurContextDepth, Handle->GlobalCount, Handle->VariableCount);
	Handle->CurContextDepth ++;
	Bytecode_int_AddVariable(Handle, NULL);	// NULL to record the extent of this	
	Bytecode_int_AddGlobal(Handle, NULL);	// NULL to record the extent of this	

	DEF_BC_NONE(BC_OP_ENTERCONTEXT)
}
void Bytecode_AppendLeaveContext(tBC_Function *Handle)
{
	 int	i;

	assert(Handle->CurContextDepth);
	assert(Handle->VariableCount);
	assert(Handle->GlobalCount);

	for( i = Handle->VariableCount; i && Handle->VariableNames[i-1] != NULL; i -- );
	Handle->VariableCount = i-1;
	for( i = Handle->GlobalCount; i && Handle->GlobalNames[i-1] != NULL; i -- );
	Handle->GlobalCount = i-1;
	Handle->CurContextDepth --;
//	printf("_LeaveContext: %i %i,%i\n", Handle->CurContextDepth, Handle->GlobalCount, Handle->VariableCount);

	DEF_BC_NONE(BC_OP_LEAVECONTEXT);
}
//void Bytecode_AppendImportNamespace(tBC_Function *Handle, const char *Name);
//	DEF_BC_STRINT(BC_OP_IMPORTNS, Name, 0)
void Bytecode_AppendDefineVar(tBC_Function *Handle, const char *Name, tSpiderTypeRef Type)
{
	 int	i, typeid;
	#if 1
	// Get the start of this context
	for( i = Handle->VariableCount; i --; )
	{
		if( Handle->VariableNames[i] == NULL )	break;
	}
	// Check for duplicate allocation
	for( i ++; i < Handle->VariableCount; i ++ )
	{
		if( strcmp(Name, Handle->VariableNames[i]) == 0 )
			return ;
	}
	#endif

	i = Bytecode_int_AddVariable(Handle, Name);
	typeid = Bytecode_int_GetTypeIdx(Handle->Script, Type);

	DEF_BC_STRINT(BC_OP_DEFINEVAR, Name, (typeid&0xFFFFFF) | (i << 24))
}
void Bytecode_AppendImportGlobal(tBC_Function *Handle, const char *Name, tSpiderTypeRef Type)
{
	int i = Bytecode_int_AddGlobal(Handle, Name);
	int typeid = Bytecode_int_GetTypeIdx(Handle->Script, Type);
	DEF_BC_STRINT(BC_OP_IMPGLOBAL, Name,  (typeid&0xFFFFFF) | (i << 24))
}
