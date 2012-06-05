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

// === IMPORTS ===

// === STRUCTURES ===

// === PROTOTYPES ===
tBC_Op	*Bytecode_int_AllocateOp(int Operation, int ExtraBytes);
 int	Bytecode_int_AddVariable(tBC_Function *Handle, const char *Name);

// === GLOBALS ===

// === CODE ===
tBC_Function *Bytecode_CreateFunction(tScript_Function *Fcn)
{
	tBC_Function *ret;
	 int	i;

	ret = malloc(sizeof(tBC_Function));
	if(!ret)	return NULL;
	
	ret->LabelSpace = ret->LabelCount = 0;
	ret->Labels = NULL;

	ret->MaxVariableCount = 0;
	ret->CurContextDepth = 0;	
	ret->VariableCount = ret->VariableSpace = 0;
	ret->VariableNames = NULL;

	ret->OperationCount = 0;
	ret->Operations = NULL;
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

int Bytecode_int_AddVariable(tBC_Function *Handle, const char *Name)
{
	if(Handle->VariableCount == Handle->VariableSpace) {
		void	*tmp;
		Handle->VariableSpace += 10;
		tmp = realloc(Handle->VariableNames, Handle->VariableSpace * sizeof(Handle->VariableNames[0]));
		if(!tmp)	return -1;	// TODO: Error
		Handle->VariableNames = tmp;
	}
	Handle->VariableNames[Handle->VariableCount] = Name;
	Handle->VariableCount ++;
	// Get max count (used when executing to get the frame size)
	if(Handle->VariableCount - Handle->CurContextDepth >= Handle->MaxVariableCount)
		Handle->MaxVariableCount = Handle->VariableCount - Handle->CurContextDepth;
//	printf("_AddVariable: %s given %i\n", Name, Handle->VariableCount - Handle->CurContextDepth - 1);
	return Handle->VariableCount - Handle->CurContextDepth - 1;
}

int Bytecode_int_GetVarIndex(tBC_Function *Handle, const char *Name)
{
	 int	i, context_depth = Handle->CurContextDepth;
	// Get the start of this context
	for( i = Handle->VariableCount; i --; )
	{
		if( !Handle->VariableNames[i] ) {
			context_depth --;
			continue ;
		}
		if( strcmp(Name, Handle->VariableNames[i]) == 0 )
			return i - context_depth;
	}
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
	case BC_OP_JUMP:
	case BC_OP_JUMPIF:
	case BC_OP_JUMPIFNOT:
	case BC_OP_LOADVAR:
	case BC_OP_SAVEVAR:
	case BC_OP_CAST:
	case BC_OP_DEFINEVAR:
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
	if(Bytecode_int_OpUsesString(_op) || !Bytecode_int_OpUsesInteger(_op)) printf("%s:%i - op BUG\n",__FILE__,__LINE__); \
	Bytecode_int_AppendOp(Handle, op);\
}

#define DEF_BC_STRINT(_op, _str, _int) { \
	tBC_Op *op = Bytecode_int_AllocateOp(_op, strlen(_str));\
	op->Content.StringInt.Integer = _int;\
	strcpy(op->Content.StringInt.String, _str);\
	if(!Bytecode_int_OpUsesString(_op) || !Bytecode_int_OpUsesInteger(_op)) printf("%s:%i - op BUG\n",__FILE__,__LINE__); \
	Bytecode_int_AppendOp(Handle, op);\
}
#define DEF_BC_STR(_op, _str) {\
	tBC_Op *op = Bytecode_int_AllocateOp(_op, strlen(_str));\
	strcpy(op->Content.StringInt.String, _str);\
	if(!Bytecode_int_OpUsesString(_op) || Bytecode_int_OpUsesInteger(_op)) printf("%s:%i - op BUG\n",__FILE__,__LINE__); \
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
void Bytecode_AppendConstNull(tBC_Function *Handle)
	DEF_BC_NONE(BC_OP_LOADNULL)

// --- Indexing / Scoping
void Bytecode_AppendElement(tBC_Function *Handle, const char *Name)
	DEF_BC_STR(BC_OP_ELEMENT, Name)
void Bytecode_AppendSetElement(tBC_Function *Handle, const char *Name)
	DEF_BC_STR(BC_OP_SETELEMENT, Name)
void Bytecode_AppendIndex(tBC_Function *Handle)
	DEF_BC_NONE(BC_OP_INDEX)
void Bytecode_AppendSetIndex(tBC_Function *Handle)
	DEF_BC_NONE(BC_OP_SETINDEX);

void Bytecode_AppendCreateObj(tBC_Function *Handle, int Type, int ArgumentCount)
{
	tBC_Op *op = Bytecode_int_AllocateOp(BC_OP_CREATEOBJ, 0);
	op->Content.Function.ID = Type;
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

void Bytecode_AppendBinOp(tBC_Function *Handle, int Operation)
	DEF_BC_NONE(Operation)
void Bytecode_AppendUniOp(tBC_Function *Handle, int Operation)
	DEF_BC_NONE(Operation)
void Bytecode_AppendCast(tBC_Function *Handle, int Type)
	DEF_BC_INT(BC_OP_CAST, Type)
void Bytecode_AppendDuplicate(tBC_Function *Handle)
	DEF_BC_NONE(BC_OP_DUPSTACK);
void Bytecode_AppendDelete(tBC_Function *Handle)
	DEF_BC_NONE(BC_OP_DELSTACK);

// Does some bookeeping to allocate variable slots at compile time
void Bytecode_AppendEnterContext(tBC_Function *Handle)
{
	Handle->CurContextDepth ++;
	Bytecode_int_AddVariable(Handle, NULL);	// NULL to record the extent of this	

	DEF_BC_NONE(BC_OP_ENTERCONTEXT)
}
void Bytecode_AppendLeaveContext(tBC_Function *Handle)
{
	 int	i;
	for( i = Handle->VariableCount; i --; )
	{
		if( Handle->VariableNames[i] == NULL )	break;
	}
	Handle->CurContextDepth --;
	Handle->VariableCount = i;

	DEF_BC_NONE(BC_OP_LEAVECONTEXT);
}
//void Bytecode_AppendImportNamespace(tBC_Function *Handle, const char *Name);
//	DEF_BC_STRINT(BC_OP_IMPORTNS, Name, 0)
void Bytecode_AppendDefineVar(tBC_Function *Handle, const char *Name, int Type)
{
	 int	i;
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
//	printf("Variable %s given slot %i\n", Name, i);	

	DEF_BC_STRINT(BC_OP_DEFINEVAR, Name, (Type&0xFFFFFF) | (i << 24))
}
