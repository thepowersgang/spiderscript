/*
 * SpiderScript Library
 * by John Hodge (thePowersGang)
 * 
 * bytecode_gen.c
 * - Generate bytecode
 */
#include <stdlib.h>
#include <stdint.h>
#include "common.h"
#include "bytecode_ops.h"
#include <stdio.h>
#include "bytecode_gen.h"
#include <string.h>
#include "bytecode.h"
#include <assert.h>

#define BUG(str, v...)	fprintf(stderr, "BUG %s:%i: "str"\n", __FILE__,__LINE__,## v)

// === IMPORTS ===

// === STRUCTURES ===

// === PROTOTYPES ===
tBC_Op	*Bytecode_int_AllocateOp(enum eBC_Ops Operation, int ExtraBytes);
 int	Bytecode_int_AddVariable(tBC_Function *Handle, const char *Name);

// === GLOBALS ===
const enum eOpEncodingType caOpEncodingTypes[] = {
	[BC_OP_NOP] = BC_OPENC_NOOPRS,
	[BC_OP_ENTERCONTEXT] = BC_OPENC_NOOPRS,
	[BC_OP_LEAVECONTEXT] = BC_OPENC_NOOPRS,

	[BC_OP_TAGREGISTER] = BC_OPENC_STRING,
	[BC_OP_IMPORTGLOBAL] = BC_OPENC_STRING,
	[BC_OP_GETGLOBAL] = BC_OPENC_REG2,
	[BC_OP_SETGLOBAL] = BC_OPENC_REG2,

	[BC_OP_LOADNULLREF] = BC_OPENC_REG2,
	[BC_OP_LOADINT] = BC_OPENC_UNK,
	[BC_OP_LOADREAL] = BC_OPENC_UNK,
	[BC_OP_LOADSTRING] = BC_OPENC_STRING,

	[BC_OP_RETURN] = BC_OPENC_REG1,
	[BC_OP_CLEARREG] = BC_OPENC_REG1,
	[BC_OP_MOV] = BC_OPENC_REG2,
	[BC_OP_REFEQ]  = BC_OPENC_REG3,
	[BC_OP_REFNEQ] = BC_OPENC_REG3,
	
	[BC_OP_JUMP]      = BC_OPENC_REG1,
	[BC_OP_JUMPIF]    = BC_OPENC_REG2,
	[BC_OP_JUMPIFNOT] = BC_OPENC_REG2,

	[BC_OP_CREATEARRAY] = BC_OPENC_REG3,
	[BC_OP_CREATEOBJ]    = BC_OPENC_UNK,
	[BC_OP_CALLFUNCTION] = BC_OPENC_UNK,
	[BC_OP_CALLMETHOD]   = BC_OPENC_UNK,

	[BC_OP_GETINDEX] = BC_OPENC_REG3,
	[BC_OP_SETINDEX] = BC_OPENC_REG3,
	[BC_OP_GETELEMENT] = BC_OPENC_REG3,
	[BC_OP_SETELEMENT] = BC_OPENC_REG3,

	[BC_OP_CAST] = BC_OPENC_REG3,

	[BC_OP_INT_NEG]    = BC_OPENC_REG2,
	[BC_OP_INT_BITNOT] = BC_OPENC_REG2,
	
	[BC_OP_INT_BITAND] = BC_OPENC_REG3,
	[BC_OP_INT_BITOR] = BC_OPENC_REG3,
	[BC_OP_INT_BITXOR] = BC_OPENC_REG3,

	[BC_OP_INT_BITSHIFTLEFT] = BC_OPENC_REG3,
	[BC_OP_INT_BITSHIFTRIGHT] = BC_OPENC_REG3,
	[BC_OP_INT_BITROTATELEFT] = BC_OPENC_REG3,
	[BC_OP_INT_ADD] = BC_OPENC_REG3,
	[BC_OP_INT_SUBTRACT] = BC_OPENC_REG3,
	[BC_OP_INT_MULTIPLY] = BC_OPENC_REG3,
	[BC_OP_INT_DIVIDE] = BC_OPENC_REG3,
	[BC_OP_INT_MODULO] = BC_OPENC_REG3,

	[BC_OP_INT_EQUALS] = BC_OPENC_REG3,
	[BC_OP_INT_NOTEQUALS] = BC_OPENC_REG3,
	[BC_OP_INT_LESSTHAN] = BC_OPENC_REG3,
	[BC_OP_INT_LESSTHANEQ] = BC_OPENC_REG3,
	[BC_OP_INT_GREATERTHAN] = BC_OPENC_REG3,
	[BC_OP_INT_GREATERTHANEQ] = BC_OPENC_REG3,

	[BC_OP_REAL_NEG] = BC_OPENC_REG2,
	
	[BC_OP_REAL_ADD]      = BC_OPENC_REG3,
	[BC_OP_REAL_SUBTRACT] = BC_OPENC_REG3,
	[BC_OP_REAL_MULTIPLY] = BC_OPENC_REG3,
	[BC_OP_REAL_DIVIDE]   = BC_OPENC_REG3,
	[BC_OP_REAL_EQUALS] = BC_OPENC_REG3,
	[BC_OP_REAL_NOTEQUALS] = BC_OPENC_REG3,
	[BC_OP_REAL_LESSTHAN] = BC_OPENC_REG3,
	[BC_OP_REAL_LESSTHANEQ] = BC_OPENC_REG3,
	[BC_OP_REAL_GREATERTHAN] = BC_OPENC_REG3,
	[BC_OP_REAL_GREATERTHANEQ] = BC_OPENC_REG3,

	[BC_OP_STR_EQUALS] = BC_OPENC_REG3,
	[BC_OP_STR_NOTEQUALS] = BC_OPENC_REG3,
	[BC_OP_STR_LESSTHAN] = BC_OPENC_REG3,
	[BC_OP_STR_LESSTHANEQ] = BC_OPENC_REG3,
	[BC_OP_STR_GREATERTHAN] = BC_OPENC_REG3,
	[BC_OP_STR_GREATERTHANEQ] = BC_OPENC_REG3,
	
	[BC_OP_STR_ADD] = BC_OPENC_REG3,

	[BC_OP_EXCEPTION_POP] = BC_OPENC_NOOPRS,
};

// === CODE ===
tBC_Function *Bytecode_CreateFunction(tSpiderScript *Script, tScript_Function *Fcn)
{
	tBC_Function *ret;

	ret = calloc(sizeof(tBC_Function), 1);
	if(!ret)	return NULL;
	ret->Script = Script;
	ret->OperationsEnd = (void*)&ret->Operations;

	return ret;
}

int Bytecode_CommitFunction(tBC_Function *Fcn, int MaxReg, int MaxGlobal)
{
	Fcn->MaxRegisters = MaxReg;
	Fcn->MaxGlobalCount = MaxGlobal;
	return 0;
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

int Bytecode_int_OpIsNoOprs(enum eBC_Ops Operation) {
	return (caOpEncodingTypes[Operation] == BC_OPENC_NOOPRS);
}
int Bytecode_int_OpIsRegInt1(enum eBC_Ops Operation) {
	return (caOpEncodingTypes[Operation] == BC_OPENC_REG1);
}
int Bytecode_int_OpIsRegInt2(enum eBC_Ops Operation) {
	return (caOpEncodingTypes[Operation] == BC_OPENC_REG2);
}
int Bytecode_int_OpIsRegInt3(enum eBC_Ops Operation) {
	return (caOpEncodingTypes[Operation] == BC_OPENC_REG3);
}

tBC_Op *Bytecode_int_AllocateOp(enum eBC_Ops Operation, int ExtraBytes)
{
	tBC_Op	*ret;

	ret = malloc(sizeof(tBC_Op) + ExtraBytes);
	if(!ret)	return NULL;

	ret->Next = NULL;
	ret->Operation = Operation;
	ret->CacheEnt = NULL;

	return ret;
}


#define DEF_BC_NONE(_op) {\
	tBC_Op *op = Bytecode_int_AllocateOp(_op, 0); \
	if(!Bytecode_int_OpIsNoOprs(_op)) BUG("Encoding of %s not 'NOOPR'",#_op); \
	Bytecode_int_AppendOp(Handle, op);\
}
#define DEF_BC_RIn(_op,n,reg1,reg2,reg3) { \
	tBC_Op *op = Bytecode_int_AllocateOp(_op, 0); \
	op->DstReg = reg1; \
	op->Content.RegInt.RegInt2 = reg2; \
	op->Content.RegInt.RegInt3 = reg3; \
	if(!Bytecode_int_OpIsRegInt##n(_op)) BUG("Encoding of %s not 'RI"#n"'",#_op); \
	Bytecode_int_AppendOp(Handle, op);\
}
#define DEF_BC_RI1(_op,reg1)	DEF_BC_RIn(_op,1,reg1,0,0)
#define DEF_BC_RI2(_op,reg1,reg2)	DEF_BC_RIn(_op,2,reg1,reg2,0)
#define DEF_BC_RI3(_op,reg1,reg2,reg3)	DEF_BC_RIn(_op,3,reg1,reg2,reg3)

// --- Flow Control
void Bytecode_AppendJump(tBC_Function *Handle, int Label)
	DEF_BC_RI1(BC_OP_JUMP, Label)
void Bytecode_AppendCondJump(tBC_Function *Handle, int Label, int CReg)
	DEF_BC_RI2(BC_OP_JUMPIF, Label, CReg)
void Bytecode_AppendCondJumpNot(tBC_Function *Handle, int Label, int CReg)
	DEF_BC_RI2(BC_OP_JUMPIFNOT, Label, CReg)
void Bytecode_AppendReturn(tBC_Function *Handle, int Reg)
	DEF_BC_RI1(BC_OP_RETURN, Reg);

// --- Constants
void Bytecode_AppendConstNull(tBC_Function *Handle, int Dst, tSpiderTypeRef Type)
	DEF_BC_RI2(BC_OP_LOADNULLREF, Dst, Bytecode_int_GetTypeIdx(Handle->Script, Type))
void Bytecode_AppendConstInt(tBC_Function *Handle, int Dst, tSpiderInteger Value)
{
	tBC_Op *op = Bytecode_int_AllocateOp(BC_OP_LOADINT, 0);
	op->DstReg = Dst;
	op->Content.Integer = Value;
	Bytecode_int_AppendOp(Handle, op);
}
void Bytecode_AppendConstReal(tBC_Function *Handle, int Dst, double Value)
{
	tBC_Op *op = Bytecode_int_AllocateOp(BC_OP_LOADREAL, 0);
	op->DstReg = Dst;
	op->Content.Real = Value;
	Bytecode_int_AppendOp(Handle, op);
}
void Bytecode_AppendConstString(tBC_Function *Handle, int Dst, const void *Data, size_t Length)
{
	tBC_Op *op = Bytecode_int_AllocateOp(BC_OP_LOADSTRING, Length+1);
	op->DstReg = Dst;
	op->Content.String.Length = Length;
	memcpy(op->Content.String.Data, Data, Length);
	op->Content.String.Data[Length] = 0;
	Bytecode_int_AppendOp(Handle, op);
}

// --- Indexing / Scoping
void Bytecode_AppendElement(tBC_Function *Handle, int Dst, int Obj, int EleIdx)
	DEF_BC_RI3(BC_OP_GETELEMENT, Dst, Obj, EleIdx)
void Bytecode_AppendSetElement(tBC_Function *Handle, int Obj, int EleIdx, int SrcReg)
	DEF_BC_RI3(BC_OP_SETELEMENT, SrcReg, Obj, EleIdx)
void Bytecode_AppendIndex(tBC_Function *Handle, int Dst, int Array, int Idx)
	DEF_BC_RI3(BC_OP_GETINDEX, Dst, Array, Idx)
void Bytecode_AppendSetIndex(tBC_Function *Handle, int Array, int Idx, int Src)
	DEF_BC_RI3(BC_OP_SETINDEX, Src, Array, Idx);

void Bytecode_int_AppendCall(tBC_Function *Handle, enum eBC_Ops Op, int RetReg, int FcnIdx, int ArgC, int ArgRegs[])
{
	tBC_Op *op = Bytecode_int_AllocateOp(Op, sizeof(int)*ArgC);
	op->DstReg = RetReg;
	op->Content.Function.ID = FcnIdx;
	op->Content.Function.ArgCount = ArgC;
	for( int i = 0; i < ArgC; i ++ )
		op->Content.Function.ArgRegs[i] = ArgRegs[i];
	Bytecode_int_AppendOp(Handle, op);
}

void Bytecode_AppendCreateObj(tBC_Function *Handle, tSpiderScript_TypeDef *Def, int RetReg, size_t NArgs, int ArgRegs[])
{
	tSpiderTypeRef	type = {Def, 0};
	Bytecode_int_AppendCall(Handle, BC_OP_CREATEOBJ, RetReg, Bytecode_int_GetTypeIdx(Handle->Script, type),
		NArgs, ArgRegs);
}
void Bytecode_AppendMethodCall(tBC_Function *Handle, uint32_t ID, int RetReg, size_t NArgs, int ArgRegs[])
{
	Bytecode_int_AppendCall(Handle, BC_OP_CALLMETHOD, RetReg, ID, NArgs, ArgRegs);
}
void Bytecode_AppendFunctionCall(tBC_Function *Handle, uint32_t ID, int RetReg, size_t NArgs, int ArgRegs[])
{
	Bytecode_int_AppendCall(Handle, BC_OP_CALLFUNCTION, RetReg, ID, NArgs, ArgRegs);
}
void Bytecode_AppendCreateArray(tBC_Function *Handle, int RetReg, tSpiderTypeRef Type, int SizeReg) 
	DEF_BC_RI3(BC_OP_CREATEARRAY, RetReg, Bytecode_int_GetTypeIdx(Handle->Script, Type), SizeReg)

void Bytecode_AppendCast(tBC_Function *Handle, int DstReg, tSpiderScript_CoreType Type, int SrcReg)
	DEF_BC_RI3(BC_OP_CAST, DstReg, Type, SrcReg)
void Bytecode_AppendClearReg(tBC_Function *Handle, int Reg)
	DEF_BC_RI1(BC_OP_CLEARREG, Reg)
void Bytecode_AppendMov(tBC_Function *Handle, int DstReg, int SrcReg)
	DEF_BC_RI2(BC_OP_MOV, DstReg, SrcReg)
//void Bytecode_AppendDeref(tBC_Function *Handle, int Reg)
//	DEF_BC_RI1(BC_OP_DEREF, Reg)
enum eBC_Ops Bytecode_int_GetBinOpInt(enum eBC_BinOp Op)
{
	switch(Op)
	{
	case BINOP_EQ:	return BC_OP_INT_EQUALS;
	case BINOP_NE:	return BC_OP_INT_NOTEQUALS;
	case BINOP_LT:	return BC_OP_INT_LESSTHAN;
	case BINOP_LE:	return BC_OP_INT_LESSTHANEQ;
	case BINOP_GT:	return BC_OP_INT_GREATERTHAN;
	case BINOP_GE:	return BC_OP_INT_GREATERTHANEQ;
	
	case BINOP_ADD: 	return BC_OP_INT_ADD;
	case BINOP_SUB: 	return BC_OP_INT_SUBTRACT;
	case BINOP_MUL: 	return BC_OP_INT_MULTIPLY;
	case BINOP_DIV: 	return BC_OP_INT_DIVIDE;
	case BINOP_MOD: 	return BC_OP_INT_MODULO;
	default:
		BUG("BinOpInt %i unhandled", Op);
		return BC_OP_NOP;
	}
}
void Bytecode_AppendBinOpInt(tBC_Function *Handle, int Op, int DstReg, int Reg1, int Reg2)
	DEF_BC_RI3(Bytecode_int_GetBinOpInt(Op), DstReg, Reg1, Reg2)
enum eBC_Ops Bytecode_int_GetBinOpReal(enum eBC_BinOp Op)
{
	switch(Op)
	{
	case BINOP_ADD: return BC_OP_REAL_ADD;
	case BINOP_SUB:	return BC_OP_REAL_SUBTRACT;
	case BINOP_MUL:	return BC_OP_REAL_MULTIPLY;
	case BINOP_DIV:	return BC_OP_REAL_DIVIDE;
	default:
		BUG("BinOpReal %i unhandled", Op);
		return BC_OP_NOP;
	}
}
void Bytecode_AppendBinOpReal(tBC_Function *Handle, int Op, int DstReg, int Reg1, int Reg2)
	DEF_BC_RI3(Bytecode_int_GetBinOpReal(Op), DstReg, Reg1, Reg2)
enum eBC_Ops Bytecode_int_GetBinOpStr(enum eBC_BinOp Op)
{
	switch(Op)
	{
	case BINOP_ADD:	return BC_OP_STR_ADD;
	default:
		BUG("BinOpStr %i unhandled", Op);
		return BC_OP_NOP;
	}
}
void Bytecode_AppendBinOpString(tBC_Function *Handle, int Op, int DstReg, int Reg1, int Reg2)
	DEF_BC_RI3(Bytecode_int_GetBinOpStr(Op), DstReg, Reg1, Reg2)
enum eBC_Ops Bytecode_int_GetBinOpRef(enum eBC_BinOp Op)
{
	switch(Op)
	{
	case BINOP_EQ:	return BC_OP_REFEQ;
	case BINOP_NE:	return BC_OP_REFNEQ;
	default:
		BUG("BinOpRef %i unhandled", Op);
		return BC_OP_NOP;
	}
}
void Bytecode_AppendBinOpRef(tBC_Function *Handle, int Op, int DstReg, int Reg1, int Reg2)
	DEF_BC_RI3(Bytecode_int_GetBinOpRef(Op), DstReg, Reg1, Reg2)

void Bytecode_AppendFloatNegate(tBC_Function *Handle, int DstReg, int SrcReg)
	DEF_BC_RI2(BC_OP_REAL_NEG, DstReg, SrcReg)
enum eBC_Ops Bytecode_int_GetUniOpInt(enum eBC_UniOp Op)
{
	switch(Op)
	{
	case UNIOP_BITNOT:	return BC_OP_INT_BITNOT;
	case UNIOP_NEG: 	return BC_OP_INT_NEG;
	default:
		BUG("UniOpInt %i unhandled", Op);
		return BC_OP_NOP;
	}
}
void Bytecode_AppendUniInt(tBC_Function *Handle, int Op, int DstReg, int SrcReg)
	DEF_BC_RI2(Bytecode_int_GetUniOpInt(Op), DstReg, SrcReg)
void Bytecode_AppendEnterContext(tBC_Function *Handle)
	DEF_BC_NONE(BC_OP_ENTERCONTEXT)
void Bytecode_AppendLeaveContext(tBC_Function *Handle)
	DEF_BC_NONE(BC_OP_LEAVECONTEXT)

void Bytecode_AppendDefineVar(tBC_Function *Handle, int Reg, const char *Name, tSpiderTypeRef Type)
{
	tBC_Op *op = Bytecode_int_AllocateOp(BC_OP_TAGREGISTER, strlen(Name)+1);
	op->DstReg = Reg;
	op->Content.String.Length = strlen(Name);
	strcpy(op->Content.String.Data, Name);
	Bytecode_int_AppendOp(Handle, op);
}
void Bytecode_AppendImportGlobal(tBC_Function *Handle, int Slot, const char *Name, tSpiderTypeRef Type)
{
	tBC_Op *op = Bytecode_int_AllocateOp(BC_OP_IMPORTGLOBAL, strlen(Name)+1);
	op->DstReg = Slot;
	op->Content.String.Length = strlen(Name);
	strcpy(op->Content.String.Data, Name);
	Bytecode_int_AppendOp(Handle, op);
}
void Bytecode_AppendSaveGlobal(tBC_Function *Handle, int Slot, int SrcReg)
	DEF_BC_RI2(BC_OP_SETGLOBAL, SrcReg, Slot)
void Bytecode_AppendReadGlobal(tBC_Function *Handle, int Slot, int DstReg)
	DEF_BC_RI2(BC_OP_GETGLOBAL, DstReg, Slot)
