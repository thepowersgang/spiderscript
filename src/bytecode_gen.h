/*
 * SpiderScript library
 * - By John Hodge (thePowersGang)
 * 
 * bytecodev2.h
 * - Infinite register machine bytecode generation
 */
#ifndef _BYTECODE_V2_H
#define _BYTECODE_V2_H

enum {
	UNIOP_LOGICNOT,
	UNIOP_BITNOT,
	UNIOP_NEG
};
enum {
	BINOP_LOGICAND,
	BINOP_LOGICOR,
	BINOP_LOGICXOR,
	BINOP_EQUALS,
	BINOP_NOTEQUALS,
	BINOP_LESSTHAN,
	BINOP_LESSTHANOREQUAL,
	BINOP_GREATERTHAN,
	BINOP_GREATERTHANOREQUAL,
	
	BINOP_ADD,
	BINOP_SUBTRACT,
	BINOP_MULTIPLY,
	BINOP_DIVIDE,
	BINOP_MODULO,
	BINOP_BITAND,
	BINOP_BITOR,
	BINOP_BITXOR,
	BINOP_BITSHIFTLEFT,
	BINOP_BITSHIFTRIGHT,
	BINOP_BITROTATELEFT,
};

extern int	Bytecode_int_GetTypeIdx(tSpiderScript *Script, tSpiderTypeRef Type);

extern tBC_Function	*Bytecode_CreateFunction(tSpiderScript *Script, tScript_Function *ScriptFcn);
extern  int	Bytecode_CommitFunction(tBC_Function *Handle);
extern void	Bytecode_DeleteFunction(tBC_Function *Handle);

extern void	Bytecode_AppendEnterContext(tBC_Function *Handle);
extern void	Bytecode_AppendLeaveContext(tBC_Function *Handle);

extern  int	Bytecode_AllocateLabel(tBC_Function *Handle);
extern void	Bytecode_SetLabel(tBC_Function *Handle, int Label);
extern void	Bytecode_AppendJump(tBC_Function *Handle, int Label);
extern void	Bytecode_AppendCondJump(tBC_Function *Handle, int Label, int CReg);
extern void	Bytecode_AppendCondJumpNot(tBC_Function *Handle, int Label, int CReg);

extern void	Bytecode_AppendConstNull(tBC_Function *Handle, int DstReg, tSpiderTypeRef Type);
extern void	Bytecode_AppendConstInt(tBC_Function *Handle, int DstReg, tSpiderInteger Value);
extern void	Bytecode_AppendConstReal(tBC_Function *Handle, int DstReg, tSpiderReal Value);
extern void	Bytecode_AppendConstString(tBC_Function *Handle, int DstReg, const void *Data, size_t Len);

extern void	Bytecode_AppendCreateArray(tBC_Function *Handle, int RetReg, tSpiderTypeRef Type, int SizeReg); 
extern void	Bytecode_AppendCreateObj(tBC_Function *Handle, tSpiderScript_TypeDef *Def, int RetReg, size_t NArgs, int ArgRegs[]);
extern void	Bytecode_AppendFunctionCall(tBC_Function *Handle, uint32_t ID, int RetReg, size_t NArgs, int ArgRegs[]);
extern void	Bytecode_AppendMethodCall(tBC_Function *Handle, uint32_t ID, int RetReg, size_t NArgs, int ArgRegs[]);

extern void	Bytecode_AppendReturn(tBC_Function *Handle, int ReturnReg);

extern void	Bytecode_AppendMov(tBC_Function *Handle, int DstReg, int SrcReg);
extern void	Bytecode_AppendBinOpInt(tBC_Function *Handle, int DstReg, int Op, int LReg, int RReg);
extern void	Bytecode_AppendBinOpReal(tBC_Function *Handle, int DstReg, int Op, int LReg, int RReg);
extern void	Bytecode_AppendBinOpString(tBC_Function *Handle, int DstReg, int Op, int LReg, int RReg);
extern void	Bytecode_AppendBinOpRef(tBC_Function *Handle, int DstReg, int Op, int LReg, int RReg);

extern void	Bytecode_AppendUniInt(tBC_Function *Handle, int DstReg, int Op, int SrcReg);
extern void	Bytecode_AppendFloatNegate(tBC_Function *Handle, int DstReg, int SrcReg);

extern void	Bytecode_AppendIndex(tBC_Function *Handle, int DstReg, int ArrReg, int IdxReg);
extern void	Bytecode_AppendSetIndex(tBC_Function *Handle, int ArrReg, int IdxReg, int ValReg);
extern void	Bytecode_AppendElement(tBC_Function *Handle, int DstReg, int ObjReg, int ElementIndex);
extern void	Bytecode_AppendSetElement(tBC_Function *Handle, int ObjReg, int ElementIndex, int ValReg);

extern void	Bytecode_AppendCast(tBC_Function *Handle, int DstReg, tSpiderScript_CoreType Type, int SrcReg);

extern void	Bytecode_AppendDefineVar(tBC_Function *Handle, int Reg, const char *Name, tSpiderTypeRef Type);

extern void	Bytecode_AppendImportGlobal(tBC_Function *Handle, int Slot, const char *Name, tSpiderTypeRef Type);
extern void	Bytecode_AppendSaveGlobal(tBC_Function *Handle, int Slot, int SrcReg);
extern void	Bytecode_AppendReadGlobal(tBC_Function *Handle, int Slot, int DstReg);

#endif

