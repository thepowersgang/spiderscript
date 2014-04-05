/*
 * SpiderScript Library
 *
 * AST to BytecodeV2
 */
#define DEBUG	0
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "common.h"
#include "ast.h"
#include "bytecode_gen.h"
#include <assert.h>

#define TRACE_VAR_LOOKUPS	0
#define TRACE_TYPE_STACK	0
#define MAX_NAMESPACE_DEPTH	10
#define MAX_REGISTERS	64	// This is for one function
#define MAX_GLOBALS	32

#define TYPE_VOID	((tSpiderTypeRef){0,0})
#define TYPE_STRING	((tSpiderTypeRef){.ArrayDepth=0,.Def=&gSpiderScript_StringType})
#define TYPE_REAL	((tSpiderTypeRef){.ArrayDepth=0,.Def=&gSpiderScript_RealType})
#define TYPE_INTEGER	((tSpiderTypeRef){.ArrayDepth=0,.Def=&gSpiderScript_IntegerType})
#define TYPE_BOOLEAN	((tSpiderTypeRef){.ArrayDepth=0,.Def=&gSpiderScript_BoolType})

#define STACKPOP_RV_NULL	1
#define STACKPOP_RV_UFLOW	-1
#define STACKPOP_RV_MISMATCH	-2

// === TYPES ===
typedef int	tRegister;
typedef struct sVariable
{
	struct sVariable	*Next;
	tSpiderTypeRef	Type;
	tRegister	Register;
	char	Name[0];
} tVariable;
typedef struct sAST_FuncInfo
{
	tScript_Function	*Function;
	void	*Handle;
	tSpiderScript	*Script;
	
	 int	MaxRegisters;
	 int	NumAllocatedRegs;
	struct sRegInfo {
		tAST_Node	*Node;
		tSpiderTypeRef	Type;
		void	*Info;
		 int	RefCount;
	}	Registers[MAX_REGISTERS];	// Stores types of stack values

	 int	MaxGlobals;
	 int	NumGlobals;	
	tScript_Var	*ImportedGlobals[MAX_GLOBALS];	
} tAST_FuncInfo;
typedef struct sAST_BlockInfo
{
	struct sAST_BlockInfo	*Parent;
	tAST_FuncInfo	*Func;
	 int	Level;
	 int	OrigNumGlobals;	// Number of globals in-scope when block was entered
	 int	OrigNumAllocatedRegs;	// Same for registers (sanity check)
	const char	*Tag;

	 int	BreakTarget;
	 int	ContinueTarget;

	tSpiderTypeRef	NullType;	// Inferred type for NULL

	tVariable	*FirstVar;
	tAST_Node	*CurNode;
} tAST_BlockInfo;

// === PROTOTYPES ===
// Node Traversal
 int	AST_ConvertNode(tAST_BlockInfo *Block, tAST_Node *Node, tRegister *Result);
 int	BC_PrepareBlock(tAST_BlockInfo *ParentBlock, tAST_BlockInfo *ChildBlock);
 int	BC_FinaliseBlock(tAST_BlockInfo *ParentBlock, tAST_Node *Node, tAST_BlockInfo *ChildBlock);
 int	BC_ConstructObject(tAST_BlockInfo *Block, tAST_Node *Node, tRegister *Result, const char *Namespaces[], const char *Name, int NArgs, tRegister ArgRegs[], bool VArgsPassThrough);
 int	BC_CallFunction(tAST_BlockInfo *Block, tAST_Node *Node, tRegister *Result, const char *Namespaces[], const char *Name, int NArgs, tRegister ArgRegs[], bool VArgsPassThrough);
 int	BC_int_GetElement(tAST_BlockInfo *Block, tAST_Node *Node, tSpiderTypeRef ObjType, const char *Name, tSpiderTypeRef *EleType);
 int	BC_SaveValue(tAST_BlockInfo *Block, tAST_Node *DestNode, tRegister Register);
 int	BC_CastValue(tAST_BlockInfo *Block, tAST_Node *Node, tSpiderTypeRef DestType, tRegister SrcReg, tRegister *Result);

// Variables
const tScript_Var	*BC_Variable_LookupGlobal(tAST_BlockInfo *Block, tAST_Node *Node, const char *Name, int *Index);
const tVariable	*BC_Variable_Lookup(tAST_BlockInfo *Block, tAST_Node *Node, const char *Name, tSpiderTypeRef CreateType);
 int 	BC_Variable_Define(tAST_BlockInfo *Block, tAST_Node *DefNode, tSpiderTypeRef Type, const char *Name, const tVariable **VarPtr);
 int	BC_Variable_DefImportGlobal(tAST_BlockInfo *Block, tAST_Node *DefNode, tSpiderTypeRef Type, const char *Name);
 int	BC_Variable_SetValue(tAST_BlockInfo *Block, tAST_Node *VarNode, tRegister Register);
 int	BC_Variable_GetValue(tAST_BlockInfo *Block, tAST_Node *VarNode, tRegister *Result);
void	BC_Variable_Delete(tAST_BlockInfo *Block, tVariable *Var);
void	BC_Variable_Clear(tAST_BlockInfo *Block);
 int	BC_BinOp(tAST_BlockInfo *Block, int Operation, tRegister RegOut, tRegister RegL, tRegister RegR);
// - Type stack
 int	_AllocateRegister(tAST_BlockInfo *Block, tAST_Node *Node, tSpiderTypeRef Type, void *Info, tRegister *RegPtr);
void	_DumpRegisters(const tAST_BlockInfo *Block);
 int	_ReferenceRegister(tAST_BlockInfo *Block, tRegister Reg);
 int	_GetRegisterInfo(tAST_BlockInfo *Block, tRegister Register, tSpiderTypeRef *Type, void **Info);
 int	_AssertRegType(tAST_BlockInfo *Block, tAST_Node *Node, tRegister Register, tSpiderTypeRef Type);
 int	_ReleaseRegister(tAST_BlockInfo *Block, tRegister Register);
// - Helpers
tSpiderTypeRef	_GetCoreType(tSpiderScript_CoreType CoreType);

// === GLOBALS ===
// int	giNextBlockIdent = 1;

// === CODE ===
int SpiderScript_BytecodeScript(tSpiderScript *Script)
{
	for(tScript_Function *fcn = Script->Functions; fcn; fcn = fcn->Next)
	{
		if( Bytecode_ConvertFunction(Script, fcn) == 0 )
			return -1;
	}
	for(tScript_Class *sc = Script->FirstClass; sc; sc = sc->Next)
	{
		for(tScript_Function *fcn = sc->FirstFunction; fcn; fcn = fcn->Next)
		{
			if( Bytecode_ConvertFunction(Script, fcn) == 0 )
				return -1;
		}
	}
	return 0;
}

/**
 * \brief Convert a function into bytecode
 */
tBC_Function *Bytecode_ConvertFunction(tSpiderScript *Script, tScript_Function *Fcn)
{
	tBC_Function	*ret;
	tAST_FuncInfo	fi = {0};
	tAST_BlockInfo	bi = {0};

	// Check if the function has already been converted
	if(Fcn->BCFcn)	return Fcn->BCFcn;
	
	ret = Bytecode_CreateFunction(Script, Fcn);
	if(!ret)	return NULL;
	
	fi.Handle = ret;
	fi.Function = Fcn;
	fi.Script = Script;
	bi.Func = &fi;

	Fcn->ASTFcn = AST_Optimise(Fcn->ASTFcn);

	DEBUGS1("%p %s %i args", Fcn, Fcn->Name, Fcn->ArgumentCount);
	
	// Parse arguments
	for( int i = 0; i < Fcn->ArgumentCount; i ++ )
	{
		int rv = BC_Variable_Define(&bi, Fcn->ASTFcn, Fcn->Prototype.Args[i], Fcn->ArgNames[i], NULL);
		if(rv) {
			AST_RuntimeError(Script, Fcn->ASTFcn, "Error in creating arguments");
			BC_Variable_Clear(&bi);
			Bytecode_DeleteFunction(ret);
			return NULL;
		}
	}

	if( AST_ConvertNode(&bi, Fcn->ASTFcn, 0) )
	{
		AST_RuntimeError(Script, Fcn->ASTFcn, "Error in converting function");
		BC_Variable_Clear(&bi);
		Bytecode_DeleteFunction(ret);
		return NULL;
	}
	BC_Variable_Clear(&bi);

	if( fi.NumAllocatedRegs ) {
		AST_RuntimeError(Script, Fcn->ASTFcn, "Leaked regs when converting %s", Fcn->Name);
		_DumpRegisters(&bi);
	}

	Bytecode_CommitFunction(ret, fi.MaxRegisters+1, fi.MaxGlobals+1);

	// TODO: Detect reaching the end of non-void
//	Bytecode_AppendConstInt(ret, 0);
//	Bytecode_AppendReturn(ret);
	Fcn->BCFcn = ret;

	return ret;
}

// Indepotent operation
#define NO_RESULT() do { \
	if(ResultRegister) { \
		 AST_RuntimeMessage(Block->Func->Script, Node, \
			"Bytecode", "Getting result of void operation"); \
		return -1; \
	} \
} while(0)
#define SET_RESULT(reg, b_warn)	do { \
	if(ResultRegister) {\
		*ResultRegister = reg; \
	} \
	else { \
		if(b_warn) \
			AST_RuntimeMessage(Block->Func->Script, Node, \
				"Bytecode", "Result of operation unused"); \
		_ReleaseRegister(Block, reg); \
	}\
} while(0)

#define AST_NODEERROR(v...)	AST_RuntimeError(Block->Func->Script, Node, v)

/**
 * \brief Convert a node into bytecode
 * \param Block	Execution context
 * \param Node	Node to execute
 */
int AST_ConvertNode(tAST_BlockInfo *Block, tAST_Node *Node, tRegister *ResultRegister)
{
	tSpiderScript	*const script = Block->Func->Script;
	 int	ret = 0;
	tSpiderTypeRef	type, type2;
	 int	i, op = 0;
	void	*ident;	// used for classes
	
	tRegister	rreg, reg1, reg2, vreg;

	DEBUGS2_DOWN();
	DEBUGS1("Node->Type = %i", Node->Type);

	if( Node == NULL ) {
		NO_RESULT();
		Block->NullType = TYPE_VOID;
		DEBUGS2_UP();
		return 0;
	}

	switch(Node->Type)
	{
	// No Operation
	case NODETYPE_NOP:
		NO_RESULT();
		break;
	
	// Code block
	case NODETYPE_BLOCK: {
		tAST_BlockInfo	blockInfo = {0};
		BC_PrepareBlock(Block, &blockInfo);
		// Loop over all nodes, or until the return value is set
		for(tAST_Node *node = Node->Block.FirstChild; node; node = node->NextSibling )
		{
			Bytecode_AppendPos(Block->Func->Handle, node->File, node->Line);
			ret = AST_ConvertNode(&blockInfo, node, 0);
			if(ret) {
				BC_Variable_Clear(&blockInfo);
				return ret;
			}
		}
		BC_FinaliseBlock(Block, Node, &blockInfo);	
		NO_RESULT();
		} break;
	
	// Assignment
	case NODETYPE_ASSIGN:
		// Perform assignment operation
		if( Node->Assign.Operation != NODETYPE_NOP )
		{
			ret = AST_ConvertNode(Block, Node->Assign.Dest, &reg1);
			if(ret)	return ret;
			// TODO: Support <op>= on objects
			
			ret = AST_ConvertNode(Block, Node->Assign.Value, &reg2);
			if(ret)	return ret;

			switch(Node->Assign.Operation)
			{
			// General Binary Operations
			case NODETYPE_ADD:	op = BINOP_ADD;	break;
			case NODETYPE_SUBTRACT:	op = BINOP_SUB;	break;
			case NODETYPE_MULTIPLY:	op = BINOP_MUL;	break;
			case NODETYPE_DIVIDE:	op = BINOP_DIV;	break;
			case NODETYPE_MODULO:	op = BINOP_MOD;	break;
			case NODETYPE_BWAND:	op = BINOP_BITAND;	break;
			case NODETYPE_BWOR:	op = BINOP_BITOR;	break;
			case NODETYPE_BWXOR:	op = BINOP_BITXOR;	break;
			case NODETYPE_BITSHIFTLEFT:	op = BINOP_BITSHIFTLEFT;	break;
			case NODETYPE_BITSHIFTRIGHT:	op = BINOP_BITSHIFTRIGHT;	break;
			case NODETYPE_BITROTATELEFT:	op = BINOP_BITROTATELEFT;	break;

			default:
				AST_NODEERROR("Unknown operation in ASSIGN %i", Node->Assign.Operation);
				break;
			}
			// TODO: Check if this operation is valid
			BC_BinOp(Block, op, reg1, reg1, reg2);
			_ReleaseRegister(Block, reg2);
		}
		else
		{
			ret = AST_ConvertNode(Block, Node->Assign.Value, &reg1);
			if(ret)	return ret;
		}
		
		ret = BC_SaveValue(Block, Node->Assign.Dest, reg1);
		if(ret)	return ret;
		SET_RESULT(reg1, 0);
		break;
	
	// Post increment/decrement
	case NODETYPE_POSTINC:
	case NODETYPE_POSTDEC:
		// Left value
		ret = AST_ConvertNode(Block, Node->UniOp.Value, &vreg);
		if(ret)	return ret;
		// TODO: Support post inc/dec on objects
		ret = _AssertRegType(Block, Node, vreg, TYPE_INTEGER);
		if(ret)	return ret;
		
		// If the result is being used, allocate a reg for it
		if( ResultRegister ) {
			void	*info;
			_GetRegisterInfo(Block, vreg, &type, &info);
			ret = _AllocateRegister(Block, Node, type, info, &rreg);
			if(ret)	return ret;
			Bytecode_AppendMov(Block->Func->Handle, rreg, vreg);
			*ResultRegister = rreg;
		}
	
		// Constant 1
		ret = _AllocateRegister(Block, Node, TYPE_INTEGER, NULL, &rreg);
		if(ret)	return ret;
		Bytecode_AppendConstInt(Block->Func->Handle, rreg, 1);

		// Operation
		BC_BinOp(Block, (Node->Type == NODETYPE_POSTDEC ? BINOP_SUB : BINOP_ADD),
			vreg, vreg, rreg);
		_ReleaseRegister(Block, rreg);

		// Wrieback
		ret = BC_SaveValue(Block, Node->UniOp.Value, vreg);
		if(ret)	return ret;
		_ReleaseRegister(Block, vreg);
		break;

	// Function Call
	case NODETYPE_METHODCALL:
	case NODETYPE_FUNCTIONCALL:
	case NODETYPE_CREATEOBJECT: {
		const char	*namespaces[] = {NULL};	// TODO: Default/imported namespaces
		const char	**nss_ptr = (Node->Type == NODETYPE_METHODCALL ? NULL : namespaces);
		
		// Count arguments
		 int	nargs = 0;
		if( Node->Type == NODETYPE_METHODCALL )
			nargs ++;
		for(tAST_Node *node = Node->FunctionCall.FirstArg; node; node = node->NextSibling)
			nargs ++;

		// Evaluate arguments
		tRegister	argregs[nargs];
		nargs = 0;
		if( Node->Type == NODETYPE_METHODCALL ) {
			ret = AST_ConvertNode(Block, Node->FunctionCall.Object, &argregs[nargs++]);
			if(ret)	return ret;
		}
		for(tAST_Node *node = Node->FunctionCall.FirstArg; node; node = node->NextSibling)
		{
			ret = AST_ConvertNode(Block, node, &argregs[nargs++]);
			if(ret)	return ret;
		}
		
		// Call the function
		if( Node->Type == NODETYPE_CREATEOBJECT )
		{
			ret = BC_ConstructObject(Block, Node, &vreg, namespaces,
				Node->FunctionCall.Name, nargs, argregs,
				Node->FunctionCall.IsVArgPassthrough);
		}
		else
		{
			ret = BC_CallFunction(Block, Node, &vreg, nss_ptr,
				Node->FunctionCall.Name, nargs, argregs,
				Node->FunctionCall.IsVArgPassthrough);
		}
		if(ret)	return ret;

		// Clean up registers
		for( int i = 0; i < nargs; i ++ )
			_ReleaseRegister(Block, argregs[i]);		

		_GetRegisterInfo(Block, vreg, &type, NULL);
		if( ResultRegister ) {
			if( type.Def == NULL ) {
				AST_NODEERROR("void value not ignored as it aught to be");
				return -1;
			}
			*ResultRegister = vreg;
		}
		else {
			_ReleaseRegister(Block, vreg);
			if( type.Def != NULL ) {
				// TODO: Implement warn_unused_ret
			}
		}
		
		} break;
	
	case NODETYPE_CREATEARRAY:
		// - Size
		ret = AST_ConvertNode(Block, Node->Cast.Value, &vreg);
		if(ret)	return ret;
		_AssertRegType(Block, Node->Cast.Value, vreg, TYPE_INTEGER);

		ret = _AllocateRegister(Block, Node, Node->Cast.DataType, NULL, &rreg);
		if(ret)	return ret;
		Bytecode_AppendCreateArray(Block->Func->Handle, rreg, Node->Cast.DataType, vreg);
		_ReleaseRegister(Block, vreg);

		SET_RESULT(rreg, 1);		
		break;

	// Conditional
	case NODETYPE_IF: {
		 int	if_end;
		ret = AST_ConvertNode(Block, Node->If.Condition, &vreg);
		if(ret)	return ret;
		// Note: Technically should be boolean, but there's logic in execution to handle it
	
		if_end = Bytecode_AllocateLabel(Block->Func->Handle);

		if( Node->If.False->Type != NODETYPE_NOP )
		{
			 int	if_true = Bytecode_AllocateLabel(Block->Func->Handle);
			
			Bytecode_AppendCondJump(Block->Func->Handle, if_true, vreg);
	
			// False
			ret = AST_ConvertNode(Block, Node->If.False, NULL);
			if(ret)	return ret;
			Bytecode_AppendJump(Block->Func->Handle, if_end);
			Bytecode_SetLabel(Block->Func->Handle, if_true);
		}
		else
		{
			Bytecode_AppendCondJumpNot(Block->Func->Handle, if_end, vreg);
		}
		
		_ReleaseRegister(Block, vreg);
		
		// True
		ret = AST_ConvertNode(Block, Node->If.True, NULL);
		if(ret)	return ret;

		// End
		Bytecode_SetLabel(Block->Func->Handle, if_end);
		
		NO_RESULT();
		} break;

	// Ternary
	case NODETYPE_TERNARY: {
		tRegister result_reg;
		ret = AST_ConvertNode(Block, Node->If.Condition, &vreg);
		if(ret)	return ret;

		ret = _GetRegisterInfo(Block, vreg, &type, NULL);
		if(ret)	return ret;
		
		int if_end = Bytecode_AllocateLabel(Block->Func->Handle);
		
		if( Node->If.True )
		{
			int if_false = Bytecode_AllocateLabel(Block->Func->Handle);
			tRegister	trueval_reg, falseval_reg;
			// Actual Ternary
			Bytecode_AppendCondJumpNot(Block->Func->Handle, if_false, vreg);
			_ReleaseRegister(Block, vreg);
			
			// - True
			ret = AST_ConvertNode(Block, Node->If.True, &trueval_reg);
			if(ret)	return ret;
			ret = _GetRegisterInfo(Block, trueval_reg, &type, NULL);
			if(ret)	return ret;
			// > Result
			ret = _AllocateRegister(Block, Node, type, NULL, &result_reg);
			if(ret)	return ret;
			Bytecode_AppendMov(Block->Func->Handle, result_reg, trueval_reg);
			Bytecode_AppendJump(Block->Func->Handle, if_end);
			// - False
			Bytecode_SetLabel(Block->Func->Handle, if_false);
			ret = AST_ConvertNode(Block, Node->If.False, &falseval_reg);
			if(ret)	return ret;
			ret = _AssertRegType(Block, Node->If.False, falseval_reg, type);
			if(ret)	return ret;
			Bytecode_AppendMov(Block->Func->Handle, result_reg, falseval_reg);

			_ReleaseRegister(Block, trueval_reg);
			_ReleaseRegister(Block, falseval_reg);
		}
		else
		{
			Block->NullType = type;
			// Null-Coalescing
			ret = _AllocateRegister(Block, Node, type, NULL, &result_reg);
			if(ret)	return ret;
			Bytecode_AppendMov(Block->Func->Handle, result_reg, vreg);
			Bytecode_AppendCondJump(Block->Func->Handle, if_end, vreg);
			
			ret = AST_ConvertNode(Block, Node->If.False, &rreg);
			if(ret)	return ret;
			ret = _AssertRegType(Block, Node->If.False, rreg, type);
			Bytecode_AppendMov(Block->Func->Handle, result_reg, rreg);
			
			_ReleaseRegister(Block, rreg);
			_ReleaseRegister(Block, vreg);
		}
		Bytecode_SetLabel(Block->Func->Handle, if_end);

		SET_RESULT(result_reg, 1);

		} break;	

	// Loop
	case NODETYPE_LOOP: {
		tAST_BlockInfo	blockInfo = {0};
		tAST_BlockInfo	*parentBlock = Block;
		BC_PrepareBlock(parentBlock, &blockInfo);
		Block = &blockInfo;
		
		 int	loop_start, loop_end, code_end;

		// Initialise
		ret = AST_ConvertNode(Block, Node->For.Init, NULL);
		if(ret)	return ret;
		
		loop_start = Bytecode_AllocateLabel(Block->Func->Handle);
		code_end = Bytecode_AllocateLabel(Block->Func->Handle);
		loop_end = Bytecode_AllocateLabel(Block->Func->Handle);

		Block->BreakTarget = loop_end;
		Block->ContinueTarget = code_end;
		Block->Tag = Node->For.Tag;

		Bytecode_SetLabel(Block->Func->Handle, loop_start);

		// Check initial condition
		if( !Node->For.bCheckAfter )
		{
			ret = AST_ConvertNode(Block, Node->For.Condition, &vreg);
			if(ret)	return ret;
			// Boolean magic in exec_bytecode.c
			Bytecode_AppendCondJumpNot(Block->Func->Handle, loop_end, vreg);
			_ReleaseRegister(Block, vreg);
		}
	
		// Code
		ret = AST_ConvertNode(Block, Node->For.Code, NULL);
		if(ret)	return ret;

		Bytecode_SetLabel(Block->Func->Handle, code_end);
	
		// Increment
		ret = AST_ConvertNode(Block, Node->For.Increment, NULL);
		if(ret)	return ret;

		// Tail check
		if( Node->For.bCheckAfter )
		{
			ret = AST_ConvertNode(Block, Node->For.Condition, &vreg);
			if(ret)	return ret;
			// Boolean magic in exec_bytecode.c
			Bytecode_AppendCondJump(Block->Func->Handle, loop_start, vreg);
			_ReleaseRegister(Block, vreg);
		}
		else
		{
			Bytecode_AppendJump(Block->Func->Handle, loop_start);
		}

		Bytecode_SetLabel(Block->Func->Handle, loop_end);

		Block = parentBlock;
		BC_FinaliseBlock(Block, Node, &blockInfo);
		NO_RESULT();
		} break;
	// 'foreach' loop
	// "for( $array : $entry )"
	// "for( $array : $index, $entry )"
	case NODETYPE_ITERATE: {
		
		ret = AST_ConvertNode(Block, Node->Iterator.Value, &vreg);
		if(ret)	return ret;
		ret = _GetRegisterInfo(Block, vreg, &type, NULL);
		if(ret)	return ret;
		
		tAST_BlockInfo	blockInfo = {0};
		tAST_BlockInfo	*parentBlock = Block;
		BC_PrepareBlock(parentBlock, &blockInfo);
		Block = &blockInfo;

		int loop_start = Bytecode_AllocateLabel(Block->Func->Handle);
		int loop_end   = Bytecode_AllocateLabel(Block->Func->Handle);
		Block->Tag = Node->Iterator.Tag;
		Block->BreakTarget = loop_end;
		Block->ContinueTarget = loop_start;
		
		if( SS_GETARRAYDEPTH(type) )
		{
			tRegister	iterator;
			tRegister	maximum;
			tRegister	comparison;
			// Get length, iterate 0 ... n-1
			
			if( Node->Iterator.IndexVar != NULL ) {
				const tVariable *var;
				ret = BC_Variable_Define(Block, Node, TYPE_INTEGER, Node->Iterator.IndexVar, &var);
				if(ret)	return ret;
				iterator = var->Register;
			}
			else {
				ret = _AllocateRegister(Block, Node, TYPE_INTEGER, NULL, &iterator);	// Iterator
				if(ret)	return ret;
			}
		
			// Initialise loop state (iterator=-1,max=sizeof(array))	
			Bytecode_AppendConstInt(Block->Func->Handle, iterator, -1);
			const char *namespaces[] = {NULL};
			tRegister	args[] = {vreg};
			ret = BC_CallFunction(Block, Node, &maximum, namespaces, "sizeof", 1, args, false);
			if(ret)	return ret;
			ret = _AssertRegType(Block, Node, maximum, TYPE_INTEGER);
			if(ret)	return ret;

			// Create value variable
			type.ArrayDepth -= 1;
			const tVariable *var;
			ret = BC_Variable_Define(Block, Node, type, Node->Iterator.ValueVar, &var);
			if(ret)	return ret;
		
			// Loop header (increment, comparison)
			Bytecode_SetLabel(Block->Func->Handle, loop_start);
			ret = _AllocateRegister(Block, Node, TYPE_INTEGER, NULL, &comparison);
			if(ret)	return ret;
			Bytecode_AppendConstInt(Block->Func->Handle, comparison, 1);
			Bytecode_AppendBinOpInt(Block->Func->Handle, BINOP_ADD, iterator, iterator, comparison);
			Bytecode_AppendBinOpInt(Block->Func->Handle, BINOP_GE, comparison, iterator, maximum);
			Bytecode_AppendCondJump(Block->Func->Handle, loop_end, comparison);
			_ReleaseRegister(Block, comparison);
			
			// Set iteration variable
			Bytecode_AppendIndex(Block->Func->Handle, var->Register, vreg, iterator);
			
			// Content
			ret = AST_ConvertNode(Block, Node->Iterator.Code, NULL);
			if(ret)	return ret;
			
			// Loop tail
			Bytecode_AppendJump(Block->Func->Handle, loop_start);
			Bytecode_SetLabel(Block->Func->Handle, loop_end);
			
			if( Node->Iterator.IndexVar == NULL )
				_ReleaseRegister(Block, iterator);
			_ReleaseRegister(Block, maximum);
		}
		else
		{
			// oops :)
			AST_NODEERROR("foreach on unsupported type");
		}
		
		Block = parentBlock;
		_ReleaseRegister(Block, vreg);
		BC_FinaliseBlock(Block, Node, &blockInfo);
		NO_RESULT();
		break; }

	// Try/catch block
#if 0
	case NODETYPE_TRY: {
		 int	handler = Bytecode_AllocateLabel(Block->Func->Handle);
		 int	post_handler = Bytecode_AllocateLabel(Block->Func->Handle);
		// TODO: Support finally?
		// 'finally': is called before stack unwinding, and upon leaving try/catch
		

		Bytecode_AppendPushExceptHandler(Block->Func->Handle, handler);
		ret = AST_ConvertNode(Block, Node->TryCatch.Code, NULL);
		if(ret)	return ret;

		Bytecode_AppendJump(Block->Func->Handle, post_handler);
		Bytecode_SetLabel(Block->Func->Handle, handler);

		for( tAST_Node *cnode = Node->TryCatch.FirstCatch; cnode; cnode = cnode->NextSibling )
		{
			int next = Bytecode_AllocateLabel(Block->Func->Handle);
			// TODO: Create block
			const tVariable *var;
			ret = BC_Variable_Define(Block, cnode, cnode->Catch.Type, cnode->Catch.Name, &var);
			if(ret)	return ret;
			Bytecode_AppendCheckException(Block->Func->Handle, cnode->Catch.Type, var->Register, next);

			ret = AST_ConvertNode(Block, cnode->Catch.Code, NULL);
			if(ret)	return ret;

			BC_Variable_Delete(Block, BC_Variable_Lookup(Block, cnode, cnode->Catch.Name, TYPE_VOID));

			Bytecode_AppendJump(Block->Func->Handle, post_handler);
			Bytecode_SetLabel(Block->Func->Handle, next);
		}
	
		Bytecode_SetLabel(Block->Func->Handle, post_handler);
		Bytecode_AppendPopExecptHandler(Block->Func->Handle);

		} break;
#endif


	case NODETYPE_SWITCH: {
		tAST_BlockInfo	blockInfo = {0};
		tAST_BlockInfo	*parentBlock = Block;
		BC_PrepareBlock(parentBlock, &blockInfo);
		Block = &blockInfo;
		
		 int	switch_end = Bytecode_AllocateLabel(Block->Func->Handle);
		Block->BreakTarget = switch_end;
		Block->Tag = "";
		
		ret = AST_ConvertNode(Block, Node->BinOp.Left, &vreg);
		if(ret)	return ret;
		_GetRegisterInfo(Block, vreg, &type2, NULL);
		
		// Count cases	
		 int	nCases = 0;
		for( tAST_Node *node = Node->BinOp.Right; node; node = node->NextSibling )
			nCases ++;
		
		// Insert condition checks
		 int	case_labels[nCases];
		 int	i = 0, default_index = -1;
		for( tAST_Node *node = Node->BinOp.Right; node; node = node->NextSibling, i++ )
		{
			case_labels[i] = Bytecode_AllocateLabel(Block->Func->Handle);
			if( node->BinOp.Left )
			{
				// TODO: Ensure that .Left is a constant of the same type as vreg
				ret = AST_ConvertNode(Block, node->BinOp.Left, &reg1);
				if(ret)	return ret;
				_AssertRegType(Block, node->BinOp.Left, reg1, type2);
				//void *infoptr = NULL;
				//_GetRegisterInfo(Block, node->BinOp.Left, reg1, &infoptr);
				//ASSERT(infoptr != NULL);
				BC_BinOp(Block, BINOP_EQ, reg1, reg1, vreg);
				Bytecode_AppendCondJump(Block->Func->Handle, case_labels[i], reg1);
				_ReleaseRegister(Block, reg1);
			}
			else {
				if( default_index != -1 ) {
					AST_RuntimeError(Block->Func->Script, node,
						"Multiple 'default' labels in switch");
					return -1;
				}
				default_index = i;
			}
		}
		
		Bytecode_AppendJump(Block->Func->Handle,
			default_index == -1 ? switch_end : case_labels[default_index]
			);
		_ReleaseRegister(Block, vreg);
	
		// Code	
		i = 0;
		for( tAST_Node *node = Node->BinOp.Right; node; node = node->NextSibling, i++ )
		{
			Bytecode_SetLabel(Block->Func->Handle, case_labels[i]);
			ret = AST_ConvertNode(Block, node->BinOp.Right, NULL);
			if(ret)	return ret;
			// No jump, as usually end with 'break'
		}
		Bytecode_SetLabel(Block->Func->Handle, switch_end);
		BC_FinaliseBlock(parentBlock, Node, Block);
		Block = parentBlock;
		NO_RESULT();
		break; }
	
	// Return
	case NODETYPE_RETURN:
		// Special case for `return null;`
		Block->NullType = Block->Func->Function->Prototype.ReturnType;
		
		if( Node->UniOp.Value->Type == NODETYPE_NOP)
		{
			Bytecode_AppendReturn(Block->Func->Handle, 0);
		}
		else
		{
			ret = AST_ConvertNode(Block, Node->UniOp.Value, &vreg);
			if(ret)	return ret;
			ret = _AssertRegType(Block, Node->UniOp.Value, vreg, Block->Func->Function->Prototype.ReturnType);
			if(ret)	return ret;
			
			Bytecode_AppendReturn(Block->Func->Handle, vreg);
			_ReleaseRegister(Block, vreg);
		}
		NO_RESULT();
		break;
	
	case NODETYPE_BREAK:
	case NODETYPE_CONTINUE: {
		tAST_BlockInfo	*bi = Block;
		if( Node->Variable.Name[0] ) {
			while(bi && (!bi->Tag || strcmp(bi->Tag, Node->Variable.Name) != 0))
				bi = bi->Parent;
		}
		else {
			while(bi && !bi->Tag) {
				bi = bi->Parent;
			}
		}
		if( !bi ) {
			AST_NODEERROR("Unable to find continue/break target '%s'",
				Node->Variable.Name);
			return 1;
		}
		
		if( Node->Type == NODETYPE_BREAK ) {
			if( bi->BreakTarget == -1 ) {
				AST_NODEERROR("Break target invalid");
				return 1;
			}
			Bytecode_AppendJump(Block->Func->Handle, bi->BreakTarget);
		}
		else {
			if( bi->ContinueTarget == -1 ) {
				AST_NODEERROR("Continue target invalid");
				return 1;
			}
			Bytecode_AppendJump(Block->Func->Handle, bi->ContinueTarget);
		}
		NO_RESULT();
		} break;
	
	// Define a variable
	case NODETYPE_DEFVAR:

		if( SS_TYPESEQUAL(Node->DefVar.DataType, TYPE_VOID)  )
		{
			// Automatic type
			if( Node->DefVar.InitialValue == NULL ) {
				AST_NODEERROR("Use of 'auto' without an initial value");
				return 1;
			}
			
			// Convert initial value, and get return type
			ret = AST_ConvertNode(Block, Node->DefVar.InitialValue, &vreg);
			if(ret)	return ret;
			_GetRegisterInfo(Block, vreg, &type, NULL);
			
			// Set to return type
			const tVariable *var;
			ret = BC_Variable_Define(Block, Node, type, Node->DefVar.Name, &var);
			if(ret)	return ret;
			
			Bytecode_AppendMov(Block->Func->Handle, var->Register, vreg);
			_ReleaseRegister(Block, vreg);
		}
		else
		{
			const tVariable *var;
			ret = BC_Variable_Define(Block, Node, Node->DefVar.DataType, Node->DefVar.Name, &var);
			if(ret)	return ret;
		
			if( Node->DefVar.InitialValue )
			{
				ret = AST_ConvertNode(Block, Node->DefVar.InitialValue, &vreg);
				if(ret)	return ret;
				ret = _AssertRegType(Block, Node->DefVar.InitialValue, vreg, Node->DefVar.DataType);
				if(ret)	return -1;
				
				Bytecode_AppendMov(Block->Func->Handle, var->Register, vreg);
				_ReleaseRegister(Block, vreg);
			}
		}
		NO_RESULT();
		break;
	// Define/Import a global variable
	case NODETYPE_DEFGLOBAL:
		ret = BC_Variable_DefImportGlobal(Block, Node, Node->DefVar.DataType, Node->DefVar.Name);
		if(ret)	return ret;
		
		if( Node->DefVar.InitialValue )
		{
			ret = AST_ConvertNode(Block, Node->DefVar.InitialValue, &vreg);
			if(ret)	return ret;
			ret = _AssertRegType(Block, Node->DefVar.InitialValue, vreg, Node->DefVar.DataType);
			if(ret)	return -1;
			
			 int	slot;
			BC_Variable_LookupGlobal(Block, Node, Node->DefVar.Name, &slot);
			Bytecode_AppendSaveGlobal(Block->Func->Handle, slot, vreg);
			_ReleaseRegister(Block, vreg);
		}
		NO_RESULT();
		break;
	
	
	// Variable
	case NODETYPE_VARIABLE:
		ret = BC_Variable_GetValue( Block, Node, &rreg );
		if(ret)	return ret;
		SET_RESULT(rreg, 1);
		break;
	
	// Element of an Object
	case NODETYPE_ELEMENT: {
		ret = AST_ConvertNode( Block, Node->Scope.Element, &rreg );
		if(ret)	return ret;

		ret = _GetRegisterInfo(Block, rreg, &type, &ident);
		if(ret)	return ret;

		// Find type of the element
		int index = BC_int_GetElement(Block, Node, type, Node->Scope.Name, &type2);
		if(index < 0)	return index;

		ret = _AllocateRegister(Block, Node, type2, NULL, &vreg);
		if(ret)	return ret;
		
		Bytecode_AppendElement(Block->Func->Handle, vreg, rreg, index);
		_ReleaseRegister(Block, rreg);
		
		SET_RESULT(vreg, 1);
		break; }

	// Cast a value to another
	case NODETYPE_CAST:
		ret = AST_ConvertNode(Block, Node->Cast.Value, &rreg);
		if(ret)	return ret;

		ret = BC_CastValue(Block, Node, Node->Cast.DataType, rreg, &vreg);
		_ReleaseRegister(Block, rreg);
		if(ret)	return ret;
		SET_RESULT(vreg, 1);
		break;

	// Index into an array
	case NODETYPE_INDEX:
		// - Array
		ret = AST_ConvertNode(Block, Node->BinOp.Left, &reg1);
		if(ret)	return ret;
		//  > Type check
		ret = _GetRegisterInfo(Block, reg1, &type, NULL);
		if(ret)	return ret;
		
		// - Offset
		ret = AST_ConvertNode(Block, Node->BinOp.Right, &reg2);
		if(ret)	return ret;
		ret = _AssertRegType(Block, Node->BinOp.Right, reg2, TYPE_INTEGER);
		if(ret)	return ret;

		if(SS_GETARRAYDEPTH(type) != 0)
		{
			type.ArrayDepth --;
			ret = _AllocateRegister(Block, Node, type, NULL, &rreg);
			if(ret)	return ret;
			Bytecode_AppendIndex(Block->Func->Handle, rreg, reg1, reg2);
		}
		else if( SS_ISTYPEOBJECT(type) )
		{
			tRegister	args[] = {reg1, reg2};
			ret = BC_CallFunction(Block, Node, &rreg, NULL, "operator []", 2, args, false);
			if(ret)	return -1;
		}
		else
		{
			AST_NODEERROR("Type mismatch, Expected an array, got %i", ret);
			return -2;
		}
		_ReleaseRegister(Block, reg1);
		_ReleaseRegister(Block, reg2);
		SET_RESULT(rreg, 1);
		break;

	// TODO: Implement runtime constants
	case NODETYPE_CONSTANT:
		// TODO: Scan namespace for constant name
		AST_NODEERROR("TODO - Runtime Constants");
		return -1;
	
	// Constant Values
	case NODETYPE_NULL:
		if( SS_TYPESEQUAL(Block->NullType, TYPE_VOID) ) {
			AST_NODEERROR("null on non-reference");
			return -2;
		}
		ret = _AllocateRegister(Block, Node, Block->NullType, NULL, &rreg);
		if(ret)	return ret;
		Bytecode_AppendConstNull(Block->Func->Handle, rreg, Block->NullType);
		SET_RESULT(rreg, 1);
		break;
	case NODETYPE_BOOLEAN:
		ret = _AllocateRegister(Block, Node, TYPE_BOOLEAN, NULL, &rreg);
		if(ret)	return ret;
		Bytecode_AppendConstInt(Block->Func->Handle, rreg, Node->ConstBoolean);
		SET_RESULT(rreg, 1);
		break;
	case NODETYPE_INTEGER:
		ret = _AllocateRegister(Block, Node, TYPE_INTEGER, NULL, &rreg);
		if(ret)	return ret;
		Bytecode_AppendConstInt(Block->Func->Handle, rreg, Node->ConstInt);
		SET_RESULT(rreg, 1);
		break;
	case NODETYPE_REAL:
		ret = _AllocateRegister(Block, Node, TYPE_REAL, NULL, &rreg);
		if(ret)	return ret;
		Bytecode_AppendConstReal(Block->Func->Handle, rreg, Node->ConstReal);
		SET_RESULT(rreg, 1);
		break;
	case NODETYPE_STRING:
		ret = _AllocateRegister(Block, Node, TYPE_STRING, NULL, &rreg);
		if(ret)	return ret;
		Bytecode_AppendConstString(Block->Func->Handle, rreg,
			Node->ConstString->Data, Node->ConstString->Length);
		SET_RESULT(rreg, 1);
		break;
	
	case NODETYPE_DELETE:
		// TODO: POP_NOVALUE?
		#if 0
		ret = _StackPush(Block, Node, POP_UNDEF, NULL);
		if(ret < 0)	return -1;
		BC_SaveValue(Block, Node->UniOp.Value);
		if( bKeepValue ) {
			AST_NODEERROR("'delete' does not return any value");
			return -1;
		}
		#endif
		NO_RESULT();
		return 0;

	// --- Operations ---
	// Boolean Operations
	case NODETYPE_LOGICALNOT:	// Logical NOT (!)
		if(!op)	op = UNIOP_LOGICNOT;
	case NODETYPE_BWNOT:	// Bitwise NOT (~)
		if(!op)	op = UNIOP_BITNOT;
	case NODETYPE_NEGATE:	// Negation (-)
		if(!op)	op = UNIOP_NEG;
		ret = AST_ConvertNode(Block, Node->UniOp.Value, &reg1);
		if(ret)	return ret;
		ret = _GetRegisterInfo(Block, reg1, &type, NULL);
		if(ret)	return ret;

		if( SS_GETARRAYDEPTH(type) != 0) {
			AST_NODEERROR("Unary operation on array is invalid");
			return -1;
		}
		else if( SS_ISTYPEOBJECT(type) ) {
			const char *name;
			tRegister args[] = {reg1};
			switch(op)
			{
			case UNIOP_LOGICNOT:	name = "operator !";	break;
			case UNIOP_BITNOT:	name = "operator ~";	break;
			case UNIOP_NEG: 	name = "operator -";	break;
			default:
				AST_NODEERROR("BUG - UniOp %i unhandled on Object", op);
				return -1;
			}
			// TODO: Global scope overload on operators
			// TODO: Somehow handle if the object doesn't expose an "operator !"
			// and use the UniOp instead (for references)?
			// - Nah, that leads to unpredictable behavior
			ret = BC_CallFunction(Block, Node, &rreg, NULL, name, 1, args, false);
			if(ret)	return ret;
		}
		else if( type.Def != NULL && type.Def->Class == SS_TYPECLASS_CORE )
		{
			i = AST_ExecuteNode_UniOp_GetType(script, Node->Type, type.Def->Core);
			if( i <= 0 ) {
				AST_NODEERROR("Invalid unary operation #%i on %s", Node->Type,
					SpiderScript_GetTypeName(script, type));
				return -1;
			}
			type = _GetCoreType(i);
			ret = _AllocateRegister(Block, Node, type, NULL, &rreg);
			if(ret)	return ret;
			switch(i)
			{
			// Integers and booleans are functionally the same (for this)
			case SS_DATATYPE_BOOLEAN:
			case SS_DATATYPE_INTEGER:
				Bytecode_AppendUniInt(Block->Func->Handle, op, rreg, reg1);
				break;
			case SS_DATATYPE_REAL:
				assert(Node->Type == NODETYPE_NEGATE);
				Bytecode_AppendFloatNegate(Block->Func->Handle, rreg, reg1);
				break;
			default:
				assert(0);
			}
		}
		else
		{
			AST_NODEERROR("Unary boolean on invalid type %s",
				SpiderScript_GetTypeName(script, type));
			return -1;
		}
		_ReleaseRegister(Block, reg1);
		SET_RESULT(rreg, 1);
		break;

	// Reference Stuff
	case NODETYPE_REFEQUALS:	if(!op)	op = BINOP_EQ;
	case NODETYPE_REFNOTEQUALS:	if(!op)	op = BINOP_NE;

		// If the left node is NULL, then swap
		// - Allows "null != FunctionCall()" to work
		if( Node->BinOp.Left->Type == NODETYPE_NULL )
		{
			tAST_Node *node = Node->BinOp.Left;
			Node->BinOp.Left = Node->BinOp.Right;
			Node->BinOp.Right = node;
		}
		// Left first
		ret = AST_ConvertNode(Block, Node->BinOp.Left, &reg1);
		if(ret)	return ret;
		ret = _GetRegisterInfo(Block, reg1, &type, NULL);
		if(ret)	return ret;

		// Check if reference comparisons are allowed
		if( SS_GETARRAYDEPTH(type) )
			;	// Array - can be ref-compared
		else if(SS_ISTYPEREFERENCE(type))
			;	// Object - OK too
		else if( SS_ISCORETYPE(type, SS_DATATYPE_STRING) )
			;	// Strings - yup
		else {
			// Value type - nope.avi
			AST_NODEERROR("Can't use reference comparisons on value types");
			return -1;
		}

		// Then right
		Block->NullType = type;
		ret = AST_ConvertNode(Block, Node->BinOp.Right, &reg2);
		if(ret)	return ret;
		ret = _AssertRegType(Block, Node->BinOp.Right, reg2, type);
		if(ret)	return ret;
		
		ret = _AllocateRegister(Block, Node, TYPE_BOOLEAN, NULL, &rreg);
		if(ret)	return ret;
		Bytecode_AppendBinOpRef(Block->Func->Handle, op, rreg, reg1, reg2);
		_ReleaseRegister(Block, reg1);
		_ReleaseRegister(Block, reg2);
		SET_RESULT(rreg, 1);
		break;

	// Logic
	case NODETYPE_LOGICALAND:	if(!op)	op = BINOP_LOGICAND;
	case NODETYPE_LOGICALOR:	if(!op)	op = BINOP_LOGICOR;
	case NODETYPE_LOGICALXOR:	if(!op)	op = BINOP_LOGICXOR;
	// Comparisons
	case NODETYPE_EQUALS:   	if(!op)	op = BINOP_EQ;
	case NODETYPE_NOTEQUALS:	if(!op)	op = BINOP_NE;
	case NODETYPE_LESSTHAN: 	if(!op)	op = BINOP_LT;
	case NODETYPE_GREATERTHAN:	if(!op)	op = BINOP_GT;
	case NODETYPE_LESSTHANEQUAL:	if(!op)	op = BINOP_LE;
	case NODETYPE_GREATERTHANEQUAL:	if(!op)	op = BINOP_GE;
	// General Binary Operations
	case NODETYPE_ADD:	if(!op)	op = BINOP_ADD;
	case NODETYPE_SUBTRACT:	if(!op)	op = BINOP_SUB;
	case NODETYPE_MULTIPLY:	if(!op)	op = BINOP_MUL;
	case NODETYPE_DIVIDE:	if(!op)	op = BINOP_DIV;
	case NODETYPE_MODULO:	if(!op)	op = BINOP_MOD;
	case NODETYPE_BWAND:	if(!op)	op = BINOP_BITAND;
	case NODETYPE_BWOR:	if(!op)	op = BINOP_BITOR;
	case NODETYPE_BWXOR:	if(!op)	op = BINOP_BITXOR;
	case NODETYPE_BITSHIFTLEFT:	if(!op)	op = BINOP_BITSHIFTLEFT;
	case NODETYPE_BITSHIFTRIGHT:	if(!op)	op = BINOP_BITSHIFTRIGHT;
	case NODETYPE_BITROTATELEFT:	if(!op)	op = BINOP_BITROTATELEFT;
		DEBUGS2("Binop");
		// Left (because it's the output type)
		ret = AST_ConvertNode(Block, Node->BinOp.Left, &reg1);
		if(ret)	return ret;
		ret = _GetRegisterInfo(Block, reg1, &type, NULL);
		if(ret)	return ret;

		// Right
		ret = AST_ConvertNode(Block, Node->BinOp.Right, &reg2);
		if(ret)	return ret;
		ret = _GetRegisterInfo(Block, reg2, &type2, NULL);
		if(ret)	return ret;
		
		if( SS_GETARRAYDEPTH(type) != 0 ) {
			AST_NODEERROR("Binary operation on array is invalid");
			return -1;
		}
		else if( SS_ISTYPEOBJECT(type) ) {
			const char *name_tpl;
			tRegister args[] = {reg1, reg2};
			//#define suf	"(%s)"
			#define suf	""
			switch(Node->Type)
			{
			case NODETYPE_LOGICALAND:	name_tpl = "operator &&"suf;	break;
			case NODETYPE_LOGICALOR:	name_tpl = "operator ||"suf;	break;
			case NODETYPE_LOGICALXOR:	name_tpl = "operator ^^"suf;	break;
			case NODETYPE_EQUALS:   	name_tpl = "operator =="suf;	break;
			case NODETYPE_NOTEQUALS:	name_tpl = "operator !="suf;	break;
			case NODETYPE_LESSTHAN: 	name_tpl = "operator <"suf;	break;
			case NODETYPE_LESSTHANEQUAL:	name_tpl = "operator <="suf;	break;
			case NODETYPE_GREATERTHAN: 	name_tpl = "operator >"suf;	break;
			case NODETYPE_GREATERTHANEQUAL:	name_tpl = "operator >="suf;	break;
			case NODETYPE_ADD:	name_tpl = "operator +"suf;	break;
			case NODETYPE_SUBTRACT:	name_tpl = "operator -"suf;	break;
			case NODETYPE_MULTIPLY:	name_tpl = "operator *"suf;	break;
			case NODETYPE_DIVIDE:	name_tpl = "operator /"suf;	break;
			case NODETYPE_MODULO:	name_tpl = "operator %%"suf;	break;
			case NODETYPE_BWAND:	name_tpl = "operator &"suf;	break;
			case NODETYPE_BWOR:	name_tpl = "operator |"suf;	break;
			case NODETYPE_BWXOR:	name_tpl = "operator ^"suf;	break;
			case NODETYPE_BITSHIFTLEFT:	name_tpl = "operator <<"suf;	break;
			case NODETYPE_BITSHIFTRIGHT:	name_tpl = "operator >>"suf;	break;
			case NODETYPE_BITROTATELEFT:	name_tpl = "operator <<<"suf;	break;
			default:
				AST_NODEERROR("BUG - Node %i unhandled in BinOp on Object", Node->Type);
				return -1;
			}
			#undef suf
			char	*name = SpiderScript_FormatTypeStr1(script, name_tpl, type2);
			ret = BC_CallFunction(Block, Node, &rreg, NULL, name, 2, args, false);
			free(name);
			if(ret)	return ret;
		}
		else if( type.Def != NULL && type.Def->Class == SS_TYPECLASS_CORE )
		{
			tSpiderScript_CoreType	ltype, rtype;
			ltype = type.Def->Core;
			
			// Non-core types are reported as UNDEF
			if( type2.ArrayDepth || type2.Def == NULL || type2.Def->Class != SS_TYPECLASS_CORE ) {
				rtype = SS_DATATYPE_UNDEF;
			}
			else {
				rtype = type2.Def->Core;
			}
			i = AST_ExecuteNode_BinOp_GetType(script, Node->Type, ltype, rtype);
			if( i > 0 ) {
				// All good
				type = _GetCoreType(i);
			}
			else if( i < 0 ) {
				i = -i;
				tSpiderTypeRef	tgt_type = _GetCoreType(i);
				// Implicit cast
				if( script->Variant->bImplicitCasts )
				{
					// Cast into temp register
					tRegister	casted_reg;
					ret = BC_CastValue(Block, Node, tgt_type, reg2, &casted_reg);
					if(ret)	return ret;
					ret = _AssertRegType(Block, Node, casted_reg, tgt_type);
					if(ret)	return ret;
					// Release old righthand reg and replace with casted
					_ReleaseRegister(Block, reg2);
					reg2 = casted_reg;
					// Get actual result
					i = AST_ExecuteNode_BinOp_GetType(script, Node->Type, ltype, i);
					assert(i > 0);
					type = _GetCoreType(i);
				}
				else {
					AST_NODEERROR("Cast required for %s #%i %s",
						SpiderScript_GetTypeName(script, type),
						op,
						SpiderScript_GetTypeName(script, tgt_type)
						);
					return -1;
				}
			}
			else {
				// Bad combo / no implicit
				AST_NODEERROR("Invalid binary operation (%s #%i %s)",
					SpiderScript_GetTypeName(script, type),
					op,
					SpiderScript_GetTypeName(script, type2)
					);
				return -1;
			}
			ret = _AllocateRegister(Block, Node, type, NULL, &rreg);
			if(ret)	return ret;
			BC_BinOp(Block, op, rreg, reg1, reg2);
		}
		else
		{
			AST_NODEERROR("Binary operation on invalid type");
			return -1;
		}
		_ReleaseRegister(Block, reg1);
		_ReleaseRegister(Block, reg2);
		SET_RESULT(rreg, 1);
		break;
	
	default:
		AST_NODEERROR("BUG - SpiderScript AST_ConvertNode Unimplemented %i", Node->Type);
		return -1;
	}
	
	Block->NullType = TYPE_VOID;

	DEBUGS1("Left NT%i", Node->Type);
	DEBUGS2_UP();
	return 0;
}

int BC_PrepareBlock(tAST_BlockInfo *Block, tAST_BlockInfo *ChildBlock)
{
	Bytecode_AppendEnterContext(Block->Func->Handle);	// Create a new block
	ChildBlock->ContinueTarget = -1;
	ChildBlock->BreakTarget = -1;
	ChildBlock->Tag = NULL;
	ChildBlock->Parent = Block;
	ChildBlock->Func = Block->Func;
	ChildBlock->OrigNumGlobals = Block->Func->NumGlobals;
	ChildBlock->OrigNumAllocatedRegs = Block->Func->NumAllocatedRegs;
	return 0;
}

int BC_FinaliseBlock(tAST_BlockInfo *ParentBlock, tAST_Node *Node, tAST_BlockInfo *ChildBlock)
{
	BC_Variable_Clear(ChildBlock);
	if(ChildBlock->OrigNumAllocatedRegs != ParentBlock->Func->NumAllocatedRegs) {
		AST_RuntimeMessage(ParentBlock->Func->Script, Node, "bug", "Leaked registers");
	}
	// Clean up imported globals
	assert(ParentBlock->Func->NumGlobals >= ChildBlock->OrigNumGlobals);
	for( int i = ChildBlock->OrigNumGlobals; i < ParentBlock->Func->NumGlobals; i ++ )
		ParentBlock->Func->ImportedGlobals[i] = NULL;
	Bytecode_AppendLeaveContext(ParentBlock->Func->Handle);	// Leave this context
	return 0;
}

int BC_ConstructObject(tAST_BlockInfo *Block, tAST_Node *Node, tRegister *RetReg, const char *Namespaces[], const char *Name, int NArgs, tRegister ArgRegs[], bool VArgsPassThrough)
{
	tSpiderScript	*const script = Block->Func->Script;
	 int	ret;
	tSpiderTypeRef	type;
	
	// Look up object
	tSpiderScript_TypeDef *def = SpiderScript_ResolveObject(script, Namespaces, Name);
	if( def == NULL )
	{
		AST_NODEERROR("Undefined reference to class %s", Name);
		return -1;
	}
	
	const tSpiderFcnProto	*proto;
	bool	explicit_this = false;
	if( def->Class == SS_TYPECLASS_SCLASS )
	{
		tScript_Class	*sc = def->SClass;
		tScript_Function *sf;

		for( sf = sc->FirstFunction; sf; sf = sf->Next )
		{
			if( strcmp(sf->Name, CONSTRUCTOR_NAME) == 0 )
				break; 
			// TODO: Handle overloaded functions
		}
	
		proto = (sf ? &sf->Prototype : NULL);
		explicit_this = true;
	}
	else if( def->Class == SS_TYPECLASS_NCLASS )
	{
		tSpiderClass	*nc = def->NClass;
		tSpiderFunction *nf = nc->Constructor;

		// TODO: Allow overloaded constructors

		proto = (nf ? nf->Prototype : NULL);
	}
	else
	{
		AST_NODEERROR("Construction of non-object");
		return -1;
	}
	
	if( proto )
	{
		 int	minArgc = 0;
		for( ; proto->Args[minArgc].Def; minArgc ++ )
			;
		bool bVariable = proto->bVariableArgs;
		 int	ofs = (explicit_this ? 1 : 0);

		// Argument count check
		if( ofs+NArgs < minArgc || (!bVariable && ofs+NArgs > minArgc) ) {
			AST_NODEERROR( "Constructor %s takes %i%s arguments, passed %i",
				Name, minArgc, (bVariable ? "+" : ""), NArgs);
			return -1;
		}
		// Type checks
		for( int i = ofs; i < minArgc; i ++ )
		{
			ret = _GetRegisterInfo(Block, ArgRegs[i-ofs], &type, NULL);
			if(ret) return ret;
			if( !SS_TYPESEQUAL(proto->Args[i], type) ) {
				// Sad to be chucked
				AST_NODEERROR( "Argument %i of constructor %s should be %s, given %s",
					i-ofs, Name,
					SpiderScript_GetTypeName(script, proto->Args[i]),
					SpiderScript_GetTypeName(script, type)
					);
				return -1;
			}
		}
	}
	else
	{
		if( NArgs > 0 )
		{
			AST_NODEERROR("Constructor for %s takes no arguments",
				Name);
			return -1;
		}
	}

	type.ArrayDepth = 0;
	type.Def = def;
	
	tRegister	retreg;
	
	ret = _AllocateRegister(Block, Node, type, NULL, &retreg);
	if(ret)	return ret;
	
	Bytecode_AppendCreateObj(Block->Func->Handle, def, retreg, NArgs, ArgRegs, VArgsPassThrough);
	
	assert(RetReg);
	*RetReg = retreg;
	
	return 0;
}

/**
 * \brief Inserts a function call
 * \param Block	AST block state
 * \param Node	Node that caused the function call (used for line numbers
 * \param ReturnType	Return type of function
 * \param Namespaces	NULL-terminated list of namespaces to search (set to NULL for method call)
 * \param Name	Name of the function to call
 * \param NArgs	Argument count
 * \param ArgTypes	Types of each argument
 * \return Boolean failure
 * \note If Namespaces == NULL, then ArgTypes[0] is used as the object type for the method call
 */
int BC_CallFunction(tAST_BlockInfo *Block, tAST_Node *Node, tRegister *RetReg, const char *Namespaces[], const char *Name, int NArgs, tRegister ArgRegs[], bool VArgsPassThrough)
{
	tSpiderScript	*Script = Block->Func->Script;
	 int	id = 0;
	 int	ret;
	tSpiderTypeRef	ret_type;
	tRegister	retreg;
	const tSpiderFcnProto	*proto = NULL;
	
	DEBUGS1("BC_CallFunction '%s'", Name);

	if( Namespaces == NULL )
	{
		if( NArgs < 1 ) {
			AST_NODEERROR( "BUG - BC_CallFunction(Namespaces == NULL, NArgs < 1)");
			return -1;
		}		

		DEBUGS1("Getting method");
		tSpiderTypeRef	thistype;
		ret = _GetRegisterInfo(Block, ArgRegs[0], &thistype, NULL);
		if(ret)	return ret;
		if( thistype.ArrayDepth ) {
			AST_NODEERROR("Method call on array");
			return -1;
		}
		if( thistype.Def == NULL ) {
			AST_NODEERROR("Method call on NULL");
			return -1;
		}
		if( thistype.Def->Class == SS_TYPECLASS_NCLASS )
		{
			tSpiderClass *nc = thistype.Def->NClass;
			tSpiderFunction  *nf;
			for( nf = nc->Methods; nf; nf = nf->Next, id ++ )
			{
				if( strcmp(nf->Name, Name) == 0 )
					break;
				// TODO: Overloads
			}
			if( !nf ) {
				AST_NODEERROR("Class %s does not have a method '%s'", nc->Name, Name);
				return -1;
			}
			proto = nf->Prototype;
		}
		else if( thistype.Def->Class == SS_TYPECLASS_SCLASS )
		{
			tScript_Class	*sc = thistype.Def->SClass;
			tScript_Function *sf;
			// Script class
			for( sf = sc->FirstFunction; sf; sf = sf->Next, id ++ )
			{
				if( strcmp(sf->Name, Name) == 0 )
					break;
				// TODO: Overloads
			}
			if( !sf ) {
				AST_NODEERROR("Class %s does not have a method '%s'", sc->Name, Name);
				return -1;
			}
			proto = &sf->Prototype;
		}
		else
		{
			AST_NODEERROR("Method call on non-object (%s)",
				SpiderScript_GetTypeName(Block->Func->Script, thistype));
			return -1;
		}
		DEBUGS1("Found sf=%p nf=%p", sf, nf);
	}
	else
	{
		void *ident;
		id = SpiderScript_ResolveFunction(Block->Func->Script, Namespaces, Name, &ident);
		if( id == -1 ) {
			AST_NODEERROR("Undefined reference to %s", Name);
			return -1;
		}
		
		// TODO: Assuming the internals is hacky
		if( id >> 16 )
			proto = ((tSpiderFunction*)ident)->Prototype;
		else
			proto = &((tScript_Function*)ident)->Prototype;
	}
	
	if( !proto )
	{
		AST_NODEERROR("Can't find '%s'", Name);
		return -1;
	}
	else
	{
		 int	minArgc = 0;
		for( ; proto->Args[minArgc].Def; minArgc ++ )
			;
		bool bVariable = proto->bVariableArgs;

		DEBUGS1("minArgc = %i, bVariable = %i", minArgc, bVariable);

		// Argument count check
		if( NArgs < minArgc || (!bVariable && NArgs > minArgc) ) {
			AST_NODEERROR("%s takes %i%s arguments, passed %i",
				Name, minArgc, (bVariable ? "+" : ""),
				NArgs);
			return -1;
		}
		// Type checks
		for( int i = 0; i < minArgc; i ++ )
		{
			tSpiderTypeRef	type;
			ret = _GetRegisterInfo(Block, ArgRegs[i], &type, NULL);
			if(ret) return ret;
			// undefined = any type
			if( SS_ISCORETYPE(proto->Args[i], SS_DATATYPE_UNDEF) ) {
				continue ;
			}
			if( !SS_TYPESEQUAL(proto->Args[i], type) ) {
				// Sad to be chucked
				AST_NODEERROR( "Argument %i of %s should be %s, given %s",
					i, Name,
					SpiderScript_GetTypeName(Script, proto->Args[i]),
					SpiderScript_GetTypeName(Script, type)
					);
				return -1;
			}
		}
		
		ret_type = proto->ReturnType;
	}

	ret = _AllocateRegister(Block, Node, ret_type, NULL, &retreg);
	if(ret)	return ret;

	DEBUGS1("Add call bytecode op");
	// TODO: For passthough, add flag
	if( Namespaces == NULL )
		Bytecode_AppendMethodCall(Block->Func->Handle, id, retreg, NArgs, ArgRegs, VArgsPassThrough);
	else
		Bytecode_AppendFunctionCall(Block->Func->Handle, id, retreg, NArgs, ArgRegs, VArgsPassThrough);
	
	*RetReg = retreg;
	// Push return type
	DEBUGS1("Return type %s", SpiderScript_GetTypeName(Block->Func->Script, ret_type));
	// Released by caller?
//	if( ret_type.Def == NULL ) {
//		_ReleaseRegister(Block, retreg);
//	}

	return 0;
}

int BC_BinOp(tAST_BlockInfo *Block, int Op, tRegister rreg, tRegister reg1, tRegister reg2)
{
	 int	ret;
	tSpiderTypeRef	type;
	//printf("Op=%i by %p\n", Op, __builtin_return_address(0));

	ret = _GetRegisterInfo(Block, reg1, &type, NULL);
	if(ret)	return ret;
	
	assert(type.ArrayDepth == 0);
	assert(type.Def);
	assert(type.Def->Class == SS_TYPECLASS_CORE);

	switch(type.Def->Core)
	{
	case SS_DATATYPE_BOOLEAN:
		Bytecode_AppendBinOpBool(Block->Func->Handle, Op, rreg, reg1, reg2);
		break;
	case SS_DATATYPE_INTEGER:
		Bytecode_AppendBinOpInt(Block->Func->Handle, Op, rreg, reg1, reg2);
		break;
	case SS_DATATYPE_REAL:
		Bytecode_AppendBinOpReal(Block->Func->Handle, Op, rreg, reg1, reg2);
		break;
	case SS_DATATYPE_STRING:
		Bytecode_AppendBinOpString(Block->Func->Handle, Op, rreg, reg1, reg2);
		break;
	default:
		assert(0);
	}
	return 0;
}

int BC_int_GetElement(tAST_BlockInfo *Block, tAST_Node *Node, tSpiderTypeRef ObjType, const char *Name, tSpiderTypeRef *EleType)
{
	if(!ObjType.Def)
	{
		AST_NODEERROR("BUG Accessing element of void");
		return -2;
	}
	else if(ObjType.Def->Class == SS_TYPECLASS_NCLASS)
	{
		tSpiderClass *nc = ObjType.Def->NClass;
		for( int i = 0; i < nc->NAttributes; i ++ )
		{
			if( strcmp(Name, nc->AttributeDefs[i].Name) == 0 ) {
				*EleType = nc->AttributeDefs[i].Type;
				return i;
			}
		}
		AST_NODEERROR("Class %s does not have an attribute %s", nc->Name, Name);
		return -2;
	}
	else if(ObjType.Def->Class == SS_TYPECLASS_SCLASS)
	{
		tScript_Class	*sc  = ObjType.Def->SClass;
		for( int i = 0; i < sc->nProperties; i ++ )
		{
			if( strcmp(Name, sc->Properties[i]->Name) == 0 ) {
				*EleType = sc->Properties[i]->Type;
				return i;
			}
		}
		AST_NODEERROR("Class %s does not have an attribute %s", sc->Name, Name);
		return -2;
	}
	else {
		AST_NODEERROR("Setting element of non-class type %s",
			SpiderScript_GetTypeName(Block->Func->Script, ObjType) );
		return -2;
	}
	
}

int BC_SaveValue(tAST_BlockInfo *Block, tAST_Node *DestNode, tRegister ValReg)
{
	 int	ret;
	tSpiderTypeRef	type;
	tRegister	objreg, idxreg;

	switch(DestNode->Type)
	{
	// Variable, simple
	case NODETYPE_VARIABLE:
		ret = BC_Variable_SetValue( Block, DestNode, ValReg );
		if(ret)	return ret;
		break;
	// Array index
	case NODETYPE_INDEX:
		// Array
		ret = AST_ConvertNode(Block, DestNode->BinOp.Left, &objreg);
		if(ret)	return ret;
		
		ret = _GetRegisterInfo(Block, objreg, &type, NULL);
		if(ret)	return ret;
		
		if(SS_GETARRAYDEPTH(type) == 0) {
			AST_RuntimeError(Block->Func->Script, DestNode,
				"Type mismatch, Expected an array, got %s",
				SpiderScript_GetTypeName(Block->Func->Script, type));
			return -2;
		}
		type.ArrayDepth --;
		
		// Offset/index
		ret = AST_ConvertNode(Block, DestNode->BinOp.Right, &idxreg);
		if(ret)	return ret;
		ret = _AssertRegType(Block, DestNode->BinOp.Right, idxreg, TYPE_INTEGER);
		if(ret)	return ret;

		// Assignment value
		ret = _AssertRegType(Block, DestNode, ValReg, type);
		if(ret)	return ret;
		// TODO: If `ValReg` is void, then it may have been `null`
		
		Bytecode_AppendSetIndex( Block->Func->Handle, objreg, idxreg, ValReg );
		_ReleaseRegister(Block, objreg);
		_ReleaseRegister(Block, idxreg);
		break;
	// Object element
	case NODETYPE_ELEMENT: {
		ret = AST_ConvertNode(Block, DestNode->Scope.Element, &objreg);
		if(ret)	return ret;
		ret = _GetRegisterInfo(Block, objreg, &type, NULL);
		if(ret)	return ret;

		// Find type of the element
		int index = BC_int_GetElement(Block, DestNode, type, DestNode->Scope.Name, &type);
		if(index<0)	return index;
		// Check types
		ret = _AssertRegType(Block, DestNode, ValReg, type);
		if(ret)	return ret;
		// TODO: Same as before, may need to handle `null`
		
		Bytecode_AppendSetElement( Block->Func->Handle, objreg, index, ValReg );
		_ReleaseRegister(Block, objreg);
		break; }
	// Anything else
	default:
		AST_RuntimeError(Block->Func->Script, DestNode, "Assignment target is not a LValue");
		return -1;
	}
	return 0;
}

int BC_CastValue(tAST_BlockInfo *Block, tAST_Node *Node, tSpiderTypeRef DestType, tRegister SrcReg, tRegister *DstReg)
{
	tSpiderScript	*const script = Block->Func->Script;
	 int	ret;
	tSpiderTypeRef	SourceType;
	ret = _GetRegisterInfo(Block, SrcReg, &SourceType, NULL);
	if(ret)	return ret;

	if( SS_GETARRAYDEPTH(SourceType) && !SS_ISCORETYPE(DestType, SS_DATATYPE_BOOLEAN) ) {
		AST_NODEERROR("Invalid cast from array (0x%x)", SourceType);
		return 1;
	}
	// Objects cast using methods
	else if( SS_ISTYPEOBJECT(SourceType) ) {
		char *name = SpiderScript_FormatTypeStr1(script, "operator (%s)", DestType);
		tRegister args[] = {SrcReg};
		ret = BC_CallFunction(Block, Node, DstReg, NULL, name, 1, args, false);
		free(name);
		if(ret)	return ret;
	}
	// Can't cast to (Array), (void), or non-core
	else if( DestType.ArrayDepth || !DestType.Def || DestType.Def->Class != SS_TYPECLASS_CORE ) {
		AST_NODEERROR("Invalid cast from %s to %s",
			SpiderScript_GetTypeName(script, SourceType),
			SpiderScript_GetTypeName(script, DestType)
			);
		return -1;
	}
	#if 0
	// Cast to a string invokes a function call
	else if( DestType.Def->Core == SS_DATATYPE_STRING ) {
		const char	*nss[] = {"", NULL};
		ret = BC_CallFunction(Block, Node, DstReg, nss, "Lang.MakeString", 1, &SrcReg);
		if(ret)	return ret;
	}
	// Same for casting from a string
	else if( SS_ISCORETYPE(SourceType, SS_DATATYPE_STRING) ) {
		const char	*nss[] = {"", NULL};
		char *name = SpiderScript_FormatTypeStr1(script, "Lang.ParseString%s", DestType);
		ret = BC_CallFunction(Block, Node, DstReg, nss, name, 1, &SrcReg);
		free(name);
		if(ret)	return ret;
	}
	#endif
	else {
		assert(DstReg);
		ret = _AllocateRegister(Block, Node, DestType, NULL, DstReg);
		if(ret)	return ret;
		
		Bytecode_AppendCast(Block->Func->Handle, *DstReg, DestType.Def->Core, SrcReg);
	}
	
	// <BugCheck>
	tSpiderTypeRef	type;
	ret = _GetRegisterInfo(Block, *DstReg, &type, NULL);
	if( !SS_TYPESEQUAL(type, DestType) ) {
		AST_NODEERROR("BUG - Cast from %s to %s does not returns %s",
			SpiderScript_GetTypeName(script, SourceType),
			SpiderScript_GetTypeName(script, DestType),
			SpiderScript_GetTypeName(script, type)
			);
		return -1;
	}
	// </BugCheck>
	
	return 0;
}

const tScript_Var *BC_Variable_LookupGlobal(tAST_BlockInfo *Block, tAST_Node *Node, const char *Name, int *Index)
{
	for( int i = 0; i < MAX_GLOBALS; i ++ )
	{
		if( !Block->Func->ImportedGlobals[i] )
			continue ;
		if( strcmp(Block->Func->ImportedGlobals[i]->Name, Name) == 0 ) {
			if( Index )	*Index = i;
			return Block->Func->ImportedGlobals[i];
		}
	}
	return NULL;
}

const tVariable *BC_Variable_Lookup(tAST_BlockInfo *Block, tAST_Node *Node, const char *Name, tSpiderTypeRef CreateType)
{
	tVariable	*var = NULL;

	DEBUGS1("Lookup '%s'", Name);
	for( tAST_BlockInfo *bs = Block; bs; bs = bs->Parent )
	{
		DEBUGS2("> Block %p", bs);
		for( var = bs->FirstVar; var; var = var->Next )
		{
			DEBUGS2("- == '%s'", var->Name);
			if( strcmp(var->Name, Name) == 0 )
			{
				#if TRACE_VAR_LOOKUPS
				AST_RuntimeMessage(VarNode, "debug", "Variable lookup of '%s' %p type %i",
					Name, var, var->Type);
				#endif
				
				return var;
			}
		}
	}

	//if( Block->Func->Script->Variant->bDyamicTyped && CreateType != TYPE_VOID ) {
	//	// Define variable
	//	var = BC_Variable_Define(Block, CreateType, VarNode->Variable.Name, CreateType, NULL);
	//	#if TRACE_VAR_LOOKUPS
	//	AST_RuntimeMessage(Node, "debug", "Variable <%s> '%s' implicit defined",
	//		Name, SpiderScript_GetTypeName(Block->Func->Script, CreateType));
	//	#endif
	//}
	//if( Node && Node->Type != NODETYPE_DEFGLOBAL && Node->Type != NODETYPE_DEFVAR )
	//{
	//	AST_NODEERROR("Variable '%s' is undefined", Name);
	//}
	return NULL;
}

/**
 * \brief Define a variable
 * \param Block	Current block state
 * \param Type	Type of the variable
 * \param Name	Name of the variable
 * \return Boolean Failure
 */
int BC_Variable_Define(tAST_BlockInfo *Block, tAST_Node *Node, tSpiderTypeRef Type, const char *Name, const tVariable **VarPtr)
{
	if( BC_Variable_LookupGlobal(Block, Node, Name, NULL) ) {
		AST_NODEERROR("Definition of '%s' collides with imported global", Name);
		return -1;
	}

	if( BC_Variable_Lookup(Block, NULL, Name, TYPE_VOID) ) {
		AST_NODEERROR("Redefinition of variable '%s'", Name);
		return -1;
	}

	 int	ret;
	
	tRegister reg;
	ret = _AllocateRegister(Block, Node, Type, NULL, &reg);
	if(ret)	return ret;

	tVariable *var = malloc( sizeof(tScript_Var) + strlen(Name) + 1 );
	var->Next = NULL;
	var->Type = Type;
	var->Register = reg;
	//var->Name = (char*)(var + 1);
	strcpy(var->Name, Name);

	var->Next = Block->FirstVar;
	Block->FirstVar = var;
	
	Bytecode_AppendDefineVar(Block->Func->Handle, reg, Name, Type);	

	DEBUGS1("%p %s '%s' (Reg %i)", Block,
		SpiderScript_GetTypeName(Block->Func->Script, Type), Name, reg);

	if( VarPtr )
		*VarPtr = var;

	return 0;
}

int BC_Variable_DefImportGlobal(tAST_BlockInfo *Block, tAST_Node *DefNode, tSpiderTypeRef Type, const char *Name)
{
	if( BC_Variable_Lookup(Block, DefNode, Name, TYPE_VOID) ) {
		AST_RuntimeError(Block->Func->Script, DefNode,
			"Global '%s' collides with exising name", Name);
		return -1;
	}
	
	if( BC_Variable_LookupGlobal(Block, DefNode, Name, NULL) ) {
		AST_RuntimeError(Block->Func->Script, DefNode,
			"Global %s is already imported", Name);
		return -1;
	}

	// Globals cannot be de-scoped except by a block closing
	// - This allows a simple allocation scheme (and simplifies cleanup)
	if( Block->Func->NumGlobals == MAX_GLOBALS ) {
		AST_RuntimeError(Block->Func->Script, DefNode,
			"Too many globals in function, %i max", MAX_GLOBALS);
		return -1;
	}
	int slot = Block->Func->NumGlobals;
	Block->Func->NumGlobals ++;
	if( Block->Func->MaxGlobals < Block->Func->NumGlobals )
		Block->Func->MaxGlobals = Block->Func->NumGlobals;

	tSpiderScript	*const script = Block->Func->Script;
	// Locate the global in the script
	tScript_Var	*var;
	for( var = script->FirstGlobal; var; var = var->Next )
	{
		if( strcmp(var->Name, Name) == 0 )
			break;
	}
	// - If it's not there, create a new one
	if( !var )
	{
		 int	size = SS_ISTYPEREFERENCE(Type) ? 0 : SpiderScript_int_GetTypeSize(Type);
		assert(size >= 0);
		var = malloc( sizeof(tScript_Var) + size + strlen(Name) + 1 );
		var->Type = Type;
		var->Ptr = var + 1;
		var->Name = (char*)var->Ptr + size;
		if( size == 0 )
			var->Ptr = 0;
		strcpy(var->Name, Name);
		var->Next = NULL;
		if( !script->FirstGlobal )
			script->FirstGlobal = var;
		else
			script->LastGlobal->Next = var;
		script->LastGlobal = var;
	}
	Block->Func->ImportedGlobals[slot] = var;

	Bytecode_AppendImportGlobal(Block->Func->Handle, slot, Name, Type);
	
	return 0;
}

/**
 * \brief Set the value of a variable
 * \return Boolean Failure
 */
int BC_Variable_SetValue(tAST_BlockInfo *Block, tAST_Node *Node, tRegister ValReg)
{
	// Imported globals first
	{
		 int	index;
		const tScript_Var *global = BC_Variable_LookupGlobal(Block, Node, Node->Variable.Name, &index);
		if( global )
		{
			int ret = _AssertRegType(Block, Node, ValReg, global->Type);
			if(ret)	return ret;
			Bytecode_AppendSaveGlobal(Block->Func->Handle, index, ValReg);
			return 0;
		}
	}

	// Locals second (shouldn't be overlap with the above)
	{
		tSpiderTypeRef	type = TYPE_VOID;
		//if(Block->Func->Script->Variant->bDynamicTyped)
		//	_GetRegisterInfo(Block, ValReg, &type, NULL);
		const tVariable	*var = BC_Variable_Lookup(Block, Node, Node->Variable.Name, type);
		if( var )
		{
			Bytecode_AppendMov(Block->Func->Handle, var->Register, ValReg);
			return 0;
		}
	}

	// Member variables last (may overlap)
	{
		// TODO: Detect member function and emit reference to 'this->variable'
	}
	
	AST_NODEERROR("Variable '%s' does not exist", Node->Variable.Name);
	return -1;
}

/**
 * \brief Get the value of a variable
 */
int BC_Variable_GetValue(tAST_BlockInfo *Block, tAST_Node *Node, tRegister *ValRegPtr)
{
	assert(ValRegPtr);
	 int	ret;
	{
		 int	index;
		const tScript_Var *global = BC_Variable_LookupGlobal(Block, Node, Node->Variable.Name, &index);
		if( global )
		{
			ret = _AllocateRegister(Block, Node, global->Type, NULL, ValRegPtr);
			if(ret)	return ret;
			Bytecode_AppendReadGlobal(Block->Func->Handle, index, *ValRegPtr);
			return 0;
		}
	}
	
	{
		const tVariable *var = BC_Variable_Lookup(Block, Node, Node->Variable.Name, TYPE_VOID);
		if(var)
		{
			_ReferenceRegister(Block, var->Register);
			*ValRegPtr = var->Register;
			return 0;
		}
	}

	AST_NODEERROR("Variable '%s' does not exist", Node->Variable.Name);
	return -1;
}

void BC_Variable_Delete(tAST_BlockInfo *Block, tVariable *Var)
{
	if( Var == Block->FirstVar ) {
		Block->FirstVar = Var->Next;
	}
	else {
		for( tVariable *prev = Block->FirstVar; prev; prev = prev->Next )
		{
			if( prev->Next == Var ) {
				prev->Next = Var->Next;
			}
		}
	}
	_ReleaseRegister(Block, Var->Register);
	free(Var);
}

void BC_Variable_Clear(tAST_BlockInfo *Block)
{
	DEBUGS1("");
	for( tVariable *var = Block->FirstVar; var; )
	{
		tVariable	*tv = var->Next;
		_ReleaseRegister(Block, var->Register);
		free( var );
		var = tv;
	}
	Block->FirstVar = NULL;
}

#if 0
void AST_RuntimeMessage(tAST_Node *Node, const char *Type, const char *Format, ...)
{
	va_list	args;
	
	if(Node) {
		fprintf(stderr, "%s:%i: ", Node->File, Node->Line);
	}
	fprintf(stderr, "%s: ", Type);
	va_start(args, Format);
	vfprintf(stderr, Format, args);
	va_end(args);
	fprintf(stderr, "\n");
}
void AST_RuntimeError(tAST_Node *Node, const char *Format, ...)
{
	va_list	args;
	
	if(Node) {
		fprintf(stderr, "%s:%i: ", Node->File, Node->Line);
	}
	fprintf(stderr, "error: ");
	va_start(args, Format);
	vfprintf(stderr, Format, args);
	va_end(args);
	fprintf(stderr, "\n");
}
#endif

int _AllocateRegister(tAST_BlockInfo *Block, tAST_Node *Node, tSpiderTypeRef Type, void *Info, int *RegPtr)
{
	assert(RegPtr);
	for( int i = 0; i < MAX_REGISTERS; i ++ )
	{
		struct sRegInfo	*ri = &Block->Func->Registers[i];
		if( ri->Type.Def == NULL )
		{
			ri->Node = Node;
			ri->Type = Type;
			ri->Info = Info;
			ri->RefCount = 1;
			*RegPtr = i;
			Block->Func->NumAllocatedRegs ++;
			if( i > Block->Func->MaxRegisters )
				Block->Func->MaxRegisters = i;
			DEBUGS2("Alloc R%i", i);
			return 0;
		}
	}
	AST_NODEERROR("Out of avaliable registers");
	_DumpRegisters(Block);
	return 1;
}
void _DumpRegisters(const tAST_BlockInfo *Block)
{
	for( int i = 0; i < MAX_REGISTERS; i ++ )
	{
		const struct sRegInfo	*ri = &Block->Func->Registers[i];
		if(ri->Type.Def == NULL)	continue ;
		tAST_Node *Node = ri->Node;
		AST_NODEERROR("[%i] %s %p NT%i", i,
			SpiderScript_GetTypeName(Block->Func->Script, ri->Type),
			ri->Info,
			ri->Node->Type
			);
	}
}
int _ReferenceRegister(tAST_BlockInfo *Block, tRegister Register)
{
	DEBUGS2("Reference R%i", Register);
	assert(Register >= 0);
	assert(Register < MAX_REGISTERS);
	struct sRegInfo	*ri = &Block->Func->Registers[Register];
	assert(ri->RefCount);
	ri->RefCount ++;
	return 0;
}
int _GetRegisterInfo(tAST_BlockInfo *Block, int Register, tSpiderTypeRef *Type, void **Info)
{
	assert(Register >= 0);
	assert(Register < MAX_REGISTERS);
	struct sRegInfo	*ri = &Block->Func->Registers[Register];
	assert(ri->RefCount);
	if( Type )
		*Type = ri->Type;
	if( Info )
		*Info = ri->Info;
	return 0;
}
int _AssertRegType(tAST_BlockInfo *Block, tAST_Node *Node, int Register, tSpiderTypeRef Type)
{
	struct sRegInfo	*ri = &Block->Func->Registers[Register];
	assert(ri->RefCount);
	if( !SS_TYPESEQUAL(ri->Type, Type) ) {
		AST_NODEERROR("Type mismatch, expected %s got %s",
			SpiderScript_GetTypeName(Block->Func->Script, Type),
			SpiderScript_GetTypeName(Block->Func->Script, ri->Type)
			);
		return 1;
	}
	return 0;
}
int _ReleaseRegister(tAST_BlockInfo *Block, int Register)
{
	assert(Register >= 0);
	assert(Register < MAX_REGISTERS);
	struct sRegInfo	*ri = &Block->Func->Registers[Register];
	assert(ri->RefCount);
	ri->RefCount --;
	if( ri->RefCount == 0 )
	{
		DEBUGS2("Release R%i Free", Register);
		if( SS_ISTYPEREFERENCE(ri->Type) ) {
			// Emit dereference
			Bytecode_AppendClearReg(Block->Func->Handle, Register);
		}
		ri->Type.Def = NULL;
		Block->Func->NumAllocatedRegs --;
	}
	else
	{
		DEBUGS2("Release R%i (Ref=%i)", Register, ri->RefCount);
	}
	return 0;
}

tSpiderTypeRef _GetCoreType(tSpiderScript_CoreType CoreType)
{
	tSpiderTypeRef	type;
	type.ArrayDepth = 0;
	switch(CoreType)
	{
	case SS_DATATYPE_BOOLEAN:	type.Def = &gSpiderScript_BoolType;	break;
	case SS_DATATYPE_INTEGER:	type.Def = &gSpiderScript_IntegerType;	break;
	case SS_DATATYPE_REAL:   	type.Def = &gSpiderScript_RealType;	break;
	case SS_DATATYPE_STRING: 	type.Def = &gSpiderScript_StringType;	break;
	default:
		type.Def = NULL;
		break;
	}
	return type;
}

