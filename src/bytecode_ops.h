/**
 */
#ifndef _BYTECODE_OPS_H_
#define _BYTECODE_OPS_H_

enum eBC_Ops
{
	BC_OP_NOP,

	BC_OP_ENTERCONTEXT,
	BC_OP_LEAVECONTEXT,
	BC_OP_NOTEPOSITION,

	BC_OP_TAGREGISTER,	// aka name a variable
	BC_OP_IMPORTGLOBAL,
	BC_OP_GETGLOBAL,
	BC_OP_SETGLOBAL,

	BC_OP_LOADNULLREF,	
	BC_OP_LOADINT,
	BC_OP_LOADREAL,
	BC_OP_LOADSTRING,

	BC_OP_RETURN,
	BC_OP_CLEARREG,
	BC_OP_MOV,
	BC_OP_REFEQ,
	BC_OP_REFNEQ,

	BC_OP_JUMP,
	BC_OP_JUMPIF,
	BC_OP_JUMPIFNOT,

	BC_OP_CREATEARRAY,
	BC_OP_CREATEOBJ,	
	BC_OP_CALLFUNCTION,
	BC_OP_CALLMETHOD,
	
	BC_OP_GETINDEX,
	BC_OP_SETINDEX,
	BC_OP_GETELEMENT,
	BC_OP_SETELEMENT,

	BC_OP_CAST,

	BC_OP_BOOL_EQUALS,
	BC_OP_BOOL_LOGICNOT,
	BC_OP_BOOL_LOGICAND,
	BC_OP_BOOL_LOGICOR,
	BC_OP_BOOL_LOGICXOR,

	BC_OP_INT_BITNOT,
	BC_OP_INT_NEG,

	BC_OP_INT_BITAND,
	BC_OP_INT_BITOR,
	BC_OP_INT_BITXOR,

	BC_OP_INT_BITSHIFTLEFT,
	BC_OP_INT_BITSHIFTRIGHT,
	BC_OP_INT_BITROTATELEFT,
	BC_OP_INT_ADD,
	BC_OP_INT_SUBTRACT,
	BC_OP_INT_MULTIPLY,
	BC_OP_INT_DIVIDE,
	BC_OP_INT_MODULO,
	BC_OP_INT_EQUALS,
	BC_OP_INT_NOTEQUALS,
	BC_OP_INT_LESSTHAN,
	BC_OP_INT_LESSTHANEQ,
	BC_OP_INT_GREATERTHAN,
	BC_OP_INT_GREATERTHANEQ,
	
	BC_OP_REAL_NEG,
	BC_OP_REAL_ADD,
	BC_OP_REAL_SUBTRACT,
	BC_OP_REAL_MULTIPLY,
	BC_OP_REAL_DIVIDE,

	BC_OP_REAL_EQUALS,
	BC_OP_REAL_NOTEQUALS,
	BC_OP_REAL_LESSTHAN,
	BC_OP_REAL_LESSTHANEQ,
	BC_OP_REAL_GREATERTHAN,
	BC_OP_REAL_GREATERTHANEQ,

	BC_OP_STR_EQUALS,
	BC_OP_STR_NOTEQUALS,
	BC_OP_STR_LESSTHAN,
	BC_OP_STR_LESSTHANEQ,
	BC_OP_STR_GREATERTHAN,
	BC_OP_STR_GREATERTHANEQ,
	BC_OP_STR_ADD,	

	BC_OP_EXCEPTION_PUSH,
	BC_OP_EXCEPTION_CHECK,
	BC_OP_EXCEPTION_POP,
};

extern const enum eOpEncodingType {
	BC_OPENC_UNK,
	BC_OPENC_NOOPRS,
	BC_OPENC_REG1,
	BC_OPENC_REG2,
	BC_OPENC_REG3,
	BC_OPENC_STRING,
} caOpEncodingTypes[];

#endif
