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

typedef struct sBC_Function	tBC_Function;


extern tBC_Function	*Bytecode_CreateFunction(tSpiderScript *Script, tScript_Function *ScriptFcn);
extern  int	Bytecode_CommitFunction(tBC_Function *Handle);
extern void	Bytecode_DeleteFunction(tBC_Function *Handle);

extern  int	Bytecode_AppendEnterContext(void *Handle);
extern  int	Bytecode_AppendLeaveContext(void *Handle);

extern  int	Bytecode_AllocateLabel(void *Handle);
extern  int	Bytecode_SetLabel(void *Handle, int Label);
extern  int	Bytecode_AppendJump(void *Handle, int Label);
extern  int	Bytecode_AppendCondJump(void *Handle, int Label, int CReg);
extern  int	Bytecode_AppendCondJumpNot(void *Handle, int Label, int CReg);

extern  int	Bytecode_AppendConstNull(void *Handle, int DstReg, tSpiderTypeRef Type);
extern  int	Bytecode_AppendConstInt(void *Handle, int DstReg, tSpiderInteger Value);
extern  int	Bytecode_AppendConstReal(void *Handle, int DstReg, tSpiderReal Value);
extern  int	Bytecode_AppendConstString(void *Handle, int DstReg, void *Data, size_t Len);

extern  int	Bytecode_AppendCreateArray(void *Handle, int RetReg, tSpiderTypeRef Type, int SizeReg); 
extern  int	Bytecode_AppendCreateObj(void *Handle, tSpiderScript_TypeDef *Def, int RetReg, size_t NArgs, int ArgRegs[]);
extern  int	Bytecode_AppendFunctionCall(void *Handle, uint32_t ID, int RetReg, size_t NArgs, int ArgRegs[]);
extern  int	Bytecode_AppendMethodCall(void *Handle, uint32_t ID, int RetReg, size_t NArgs, int ArgRegs[]);

extern  int	Bytecode_AppendReturn(void *Handle, int ReturnReg);

extern  int	Bytecode_AppendMov(void *Handle, int DstReg, int SrcReg);
extern  int	Bytecode_AppendBinOpInt(void *Handle, int DstReg, int Op, int LReg, int RReg);
extern  int	Bytecode_AppendBinOpReal(void *Handle, int DstReg, int Op, int LReg, int RReg);
extern  int	Bytecode_AppendBinOpString(void *Handle, int DstReg, int Op, int LReg, int RReg);
extern  int	Bytecode_AppendBinOpRef(void *Handle, int DstReg, int Op, int LReg, int RReg);

extern  int	Bytecode_AppendUniInt(void *Handle, int DstReg, int Op, int SrcReg);
extern  int	Bytecode_AppendFloatNegate(void *Handle, int DstReg, int SrcReg);

extern  int	Bytecode_AppendIndex(void *Handle, int DstReg, int ArrReg, int IdxReg);
extern  int	Bytecode_AppendSetIndex(void *Handle, int ArrReg, int IdxReg, int ValReg);
extern  int	Bytecode_AppendElement(void *Handle, int DstReg, int ObjReg, const char *Name);
extern  int	Bytecode_AppendSetElement(void *Handle, int ObjReg, const char *Name, int ValReg);

extern  int	Bytecode_AppendCast(void *Handle, int DstReg, tSpiderScript_CoreType Type, int SrcReg);

extern  int	Bytecode_AppendDefineVar(void *Handle, int Reg, const char *Name, tSpiderTypeRef Type);

extern  int	Bytecode_AppendImportGlobal(void *Handle, int Slot, const char *Name, tSpiderTypeRef Type);
extern  int	Bytecode_AppendSaveGlobal(void *Handle, int Slot, int SrcReg);
extern  int	Bytecode_AppendReadGlobal(void *Handle, int Slot, int DstReg);

#endif

