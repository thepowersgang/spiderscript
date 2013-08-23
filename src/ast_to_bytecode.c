/*
 * SpiderScript Library
 *
 * AST to Bytecode Conversion
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "common.h"
#include "ast.h"
#include "bytecode_gen.h"
#include "bytecode_ops.h"
#include <assert.h>

#define DEBUG	0
#define TRACE_VAR_LOOKUPS	0
#define TRACE_TYPE_STACK	0
#define MAX_NAMESPACE_DEPTH	10
#define MAX_STACK_DEPTH	10	// This is for one function, so shouldn't need more
#define MAX_GLOBALS	32

#define POP_UNDEF	((tSpiderTypeRef){0,0})
#define POP_STRING	((tSpiderTypeRef){.ArrayDepth=0,.Def=&gSpiderScript_StringType})
#define POP_REAL	((tSpiderTypeRef){.ArrayDepth=0,.Def=&gSpiderScript_RealType})
#define POP_INTEGER	((tSpiderTypeRef){.ArrayDepth=0,.Def=&gSpiderScript_IntegerType})
#define POP_BOOLEAN	((tSpiderTypeRef){.ArrayDepth=0,.Def=&gSpiderScript_BoolType})

#define STACKPOP_RV_NULL	1
#define STACKPOP_RV_UFLOW	-1
#define STACKPOP_RV_MISMATCH	-2

#if DEBUG >= 1
# define DEBUGS1(s, v...)	printf("%s: "s"\n", __func__, ## v)
#else
# define DEBUGS1(...)	do{}while(0)
#endif

// === TYPES ===
typedef struct sAST_BlockInfo
{
	struct sAST_BlockInfo	*Parent;
	tScript_Function	*Function;
	void	*Handle;
	tSpiderScript	*Script;
	const char	*Tag;

	 int	BreakTarget;
	 int	ContinueTarget;

	tSpiderTypeRef	NullType;	// Type used for `null`

	 int	StackDepth;
	struct {
		tSpiderTypeRef	Type;
		void	*Info;
	}	Stack[MAX_STACK_DEPTH];	// Stores types of stack values

	tScript_Var	*ImportedGlobals[MAX_GLOBALS];	
	tScript_Var	*FirstVar;
	tAST_Node	*CurNode;
} tAST_BlockInfo;

// === PROTOTYPES ===
// Node Traversal
 int	AST_ConvertNode(tAST_BlockInfo *Block, tAST_Node *Node, int bKeepValue);
 int	BC_ConstructObject(tAST_BlockInfo *Block, tAST_Node *Node, const char *Namespaces[], const char *Name, int NArgs, tSpiderTypeRef ArgTypes[]);
 int	BC_CallFunction(tAST_BlockInfo *Block, tAST_Node *Node, tSpiderTypeRef *RetType, const char *Namespaces[], const char *Name, int NArgs, tSpiderTypeRef ArgTypes[]);
 int	BC_SaveValue(tAST_BlockInfo *Block, tAST_Node *DestNode);
 int	BC_CastValue(tAST_BlockInfo *Block, tAST_Node *Node, tSpiderTypeRef DestType, tSpiderTypeRef SourceType);

// Variables
 int 	BC_Variable_Define(tAST_BlockInfo *Block, tAST_Node *DefNode, tSpiderTypeRef Type, const char *Name);
 int	BC_Variable_DefImportGlobal(tAST_BlockInfo *Block, tAST_Node *DefNode, tSpiderTypeRef Type, const char *Name);
 int	BC_Variable_SetValue(tAST_BlockInfo *Block, tAST_Node *VarNode);
 int	BC_Variable_GetValue(tAST_BlockInfo *Block, tAST_Node *VarNode);
void	BC_Variable_Clear(tAST_BlockInfo *Block);
// - Errors
void	AST_RuntimeMessage(tAST_Node *Node, const char *Type, const char *Format, ...);
void	AST_RuntimeError(tAST_Node *Node, const char *Format, ...);
// - Type stack
 int	_StackPush(tAST_BlockInfo *Block, tAST_Node *Node, tSpiderTypeRef Type, void *Info);
 int	_StackPop(tAST_BlockInfo *Block, tAST_Node *Node, tSpiderTypeRef WantedType, tSpiderTypeRef *Type, void **Info);
// - Helpers
tSpiderTypeRef	_GetCoreType(tSpiderScript_CoreType CoreType);

// === GLOBALS ===
// int	giNextBlockIdent = 1;

// === CODE ===
int SpiderScript_BytecodeScript(tSpiderScript *Script)
{
	tScript_Function	*fcn;
	tScript_Class	*sc;
	for(fcn = Script->Functions; fcn; fcn = fcn->Next)
	{
		if( Bytecode_ConvertFunction(Script, fcn) == 0 )
			return -1;
	}
	for(sc = Script->FirstClass; sc; sc = sc->Next)
	{
		for(fcn = sc->FirstFunction; fcn; fcn = fcn->Next)
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
	tAST_BlockInfo bi = {0};
	 int	i;

	// Check if the function has already been converted
	if(Fcn->BCFcn)	return Fcn->BCFcn;
	
	ret = Bytecode_CreateFunction(Script, Fcn);
	if(!ret)	return NULL;
	
	bi.Handle = ret;
	bi.Function = Fcn;
	bi.Script = Script;
	
	// Parse arguments
	for( i = 0; i < Fcn->ArgumentCount; i ++ )
	{
		BC_Variable_Define(&bi, Fcn->ASTFcn, Fcn->Arguments[i].Type, Fcn->Arguments[i].Name);
	}

	if( AST_ConvertNode(&bi, Fcn->ASTFcn, 0) )
	{
		AST_RuntimeError(Fcn->ASTFcn, "Error in converting function");
		BC_Variable_Clear(&bi);
		Bytecode_DeleteFunction(ret);
		return NULL;
	}
	BC_Variable_Clear(&bi);

	// TODO: Detect reaching the end of non-void
//	Bytecode_AppendConstInt(ret, 0);
//	Bytecode_AppendReturn(ret);
	Fcn->BCFcn = ret;

	return ret;
}

// Indepotent operation
#define CHECK_IF_NEEDED(b_warn) do { if(!bKeepValue) {\
	if(b_warn)AST_RuntimeMessage(Node, "Bytecode", "Operation without saving");\
	Bytecode_AppendDelete(Block->Handle);\
	_StackPop(Block, Node, POP_UNDEF, NULL, NULL);\
} } while(0)

/**
 * \brief Convert a node into bytecode
 * \param Block	Execution context
 * \param Node	Node to execute
 */
int AST_ConvertNode(tAST_BlockInfo *Block, tAST_Node *Node, int bKeepValue)
{
	tAST_Node	*node;
	 int	ret = 0;
	tSpiderTypeRef	type, type2;
	 int	i, op = 0;
	// int	bAddedValue = 1;	// Used to tell if the value needs to be deleted
	void	*ident;	// used for classes
	tScript_Class	*sc;
	tSpiderClass *nc;

	DEBUGS1("Node->Type = %i", Node->Type);
	switch(Node->Type)
	{
	// No Operation
	case NODETYPE_NOP:
		//bAddedValue = 0;
		break;
	
	// Code block
	case NODETYPE_BLOCK:
		Bytecode_AppendEnterContext(Block->Handle);	// Create a new block
		{
			tAST_BlockInfo	blockInfo = {0};
			blockInfo.Parent = Block;
			blockInfo.Script = Block->Script;
			blockInfo.Function = Block->Function;
			blockInfo.Handle = Block->Handle;
			// Loop over all nodes, or until the return value is set
			for(node = Node->Block.FirstChild;
				node;
				node = node->NextSibling )
			{
				ret = AST_ConvertNode(&blockInfo, node, 0);
				if(ret) {
					BC_Variable_Clear(&blockInfo);
					return ret;
				}
				if( blockInfo.StackDepth != 0 ) {
					AST_RuntimeError(node, "Stack not reset at end of node");
					blockInfo.StackDepth = 0;
				}
			}
			
			BC_Variable_Clear(&blockInfo);
		}
		Bytecode_AppendLeaveContext(Block->Handle);	// Leave this context
		break;
	
	// Assignment
	case NODETYPE_ASSIGN:
		// Perform assignment operation
		if( Node->Assign.Operation != NODETYPE_NOP )
		{
			tSpiderTypeRef	t1, t2;
			
			ret = AST_ConvertNode(Block, Node->Assign.Dest, 1);
			if(ret)	return ret;
			// TODO: Support <op>= on objects?
			ret = _StackPop(Block, Node, POP_UNDEF, &t1, NULL);
			if(ret)	return ret;
			
			ret = AST_ConvertNode(Block, Node->Assign.Value, 1);
			if(ret)	return ret;
			ret = _StackPop(Block, Node, POP_UNDEF, &t2, NULL);
			if(ret)	return ret;

			switch(Node->Assign.Operation)
			{
			// General Binary Operations
			case NODETYPE_ADD:	op = BC_OP_ADD;	break;
			case NODETYPE_SUBTRACT:	op = BC_OP_SUBTRACT;	break;
			case NODETYPE_MULTIPLY:	op = BC_OP_MULTIPLY;	break;
			case NODETYPE_DIVIDE:	op = BC_OP_DIVIDE;	break;
			case NODETYPE_MODULO:	op = BC_OP_MODULO;	break;
			case NODETYPE_BWAND:	op = BC_OP_BITAND;	break;
			case NODETYPE_BWOR:	op = BC_OP_BITOR;	break;
			case NODETYPE_BWXOR:	op = BC_OP_BITXOR;	break;
			case NODETYPE_BITSHIFTLEFT:	op = BC_OP_BITSHIFTLEFT;	break;
			case NODETYPE_BITSHIFTRIGHT:	op = BC_OP_BITSHIFTRIGHT;	break;
			case NODETYPE_BITROTATELEFT:	op = BC_OP_BITROTATELEFT;	break;

			default:
				AST_RuntimeError(Node, "Unknown operation in ASSIGN %i", Node->Assign.Operation);
				break;
			}
			ret = _StackPush(Block, Node, t1, NULL);
			if(ret < 0)	return -1;
			Bytecode_AppendBinOp(Block->Handle, op);
		}
		else
		{
			ret = AST_ConvertNode(Block, Node->Assign.Value, 1);
			if(ret)	return ret;
		}
		
		if( bKeepValue ) {
			ret = _StackPop(Block, Node, POP_UNDEF, &type, &ident);
			if(ret < 0)	return -1;
			ret = _StackPush(Block, Node, type, ident);
			if(ret < 0)	return -1;
			ret = _StackPush(Block, Node, type, ident);
			if(ret < 0)	return -1;
			Bytecode_AppendDuplicate(Block->Handle);
		}
		
		ret = BC_SaveValue(Block, Node->Assign.Dest);
		if(ret)	return ret;
		break;
	
	// Post increment/decrement
	case NODETYPE_POSTINC:
	case NODETYPE_POSTDEC:
		// Save original value if requested
		if(bKeepValue) {
			ret = BC_Variable_GetValue(Block, Node->UniOp.Value);
			if(ret)	return ret;
		}

		// Left value
		// TODO: Support post inc/dec on objects
		ret = AST_ConvertNode(Block, Node->UniOp.Value, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, Node, POP_INTEGER, NULL, NULL);
		if(ret < 0)	return -1;
	
		// Constant 1	
		Bytecode_AppendConstInt(Block->Handle, 1);
		ret = _StackPush(Block, Node, POP_INTEGER, NULL);
		if(ret < 0)	return -1;

		// Operation	
		if( Node->Type == NODETYPE_POSTDEC )
			Bytecode_AppendBinOp(Block->Handle, BC_OP_SUBTRACT);
		else
			Bytecode_AppendBinOp(Block->Handle, BC_OP_ADD);

		// Wrieback
		ret = BC_SaveValue(Block, Node->UniOp.Value);
		if(ret)	return ret;
		break;

	// Function Call
	case NODETYPE_METHODCALL: {
		 int	nargs = 1;	// `this`
		
		// Push arguments to the stack
		for(node = Node->FunctionCall.FirstArg; node; node = node->NextSibling)
			nargs ++;
		
		tSpiderTypeRef argtypes[nargs];
		
		ret = AST_ConvertNode(Block, Node->FunctionCall.Object, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, Node, POP_UNDEF, &argtypes[0], NULL);
		if(ret)	return -1;
		
		int i = 1;
		for(node = Node->FunctionCall.FirstArg; node; node = node->NextSibling, i++)
		{
			// Convert argument
			ret = AST_ConvertNode(Block, node, 1);
			if(ret)	return ret;
			
			// Pop type off the stack
			ret = _StackPop(Block, Node, POP_UNDEF, &argtypes[i], NULL);
			if(ret)	return -1;
		}
	
		ret = BC_CallFunction(Block, Node, &type, NULL, Node->FunctionCall.Name, nargs, argtypes);
		if(ret < 0)	return ret;
		
		if( type.Def != NULL ) {
			CHECK_IF_NEEDED(0);	// Don't warn
			// TODO: Implement warn_unused_ret
		}
		else if( bKeepValue ) {
			AST_RuntimeError(Node, "void value not ignored as it aught to be");
			return -1;
		}
		
		} break;
	case NODETYPE_FUNCTIONCALL:
	case NODETYPE_CREATEOBJECT: {
		 int	nargs = 0;
		const char	*namespaces[] = {NULL};	// TODO: Default/imported namespaces
		
		// Count arguments
		for(node = Node->FunctionCall.FirstArg; node; node = node->NextSibling)
			nargs ++;

		// Push arguments to the stack and get the types
		tSpiderTypeRef	arg_types[nargs];
		nargs = 0;
		for(node = Node->FunctionCall.FirstArg; node; node = node->NextSibling)
		{
			ret = AST_ConvertNode(Block, node, 1);
			if(ret)	return ret;
			
			ret = _StackPop(Block, node, POP_UNDEF, &arg_types[nargs], 0);
			if(ret < 0)	return -1;
			nargs ++;
		}
		
		// Call the function
		if( Node->Type == NODETYPE_CREATEOBJECT )
		{
			ret = BC_ConstructObject(Block, Node, namespaces,
				Node->FunctionCall.Name, nargs, arg_types);
			if(ret < 0)	return ret;
			type2.Def = (void*)-1;
		}
		else
		{
			ret = BC_CallFunction(Block, Node, &type2, namespaces,
				Node->FunctionCall.Name, nargs, arg_types);
			if(ret < 0)	return ret;
		}

		if( type2.Def != NULL ) {
			CHECK_IF_NEEDED(0);	// Don't warn
			// TODO: Implement warn_unused_ret
		}
		else if( bKeepValue ) {
			AST_RuntimeError(Node, "void value not ignored as it aught to be");
			return -1;
		}
		
		} break;
	
	case NODETYPE_CREATEARRAY:
		ret = AST_ConvertNode(Block, Node->Cast.Value, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, Node->Cast.Value, POP_INTEGER, NULL, NULL);
		if(ret < 0)	return -1;

		Bytecode_AppendCreateArray(Block->Handle, Node->Cast.DataType);
		ret = _StackPush(Block, Node, Node->Cast.DataType, NULL);
		if(ret < 0)	return ret;
		ret = 0;
		break;

	// Conditional
	case NODETYPE_IF: {
		 int	if_end;
		ret = AST_ConvertNode(Block, Node->If.Condition, 1);
		if(ret)	return ret;
		
		// Note: Technically should be boolean, but there's logic in execution to handle it
		ret = _StackPop(Block, Node->If.Condition, POP_UNDEF, NULL, NULL);
		if(ret < 0)	return -1;
	
		if_end = Bytecode_AllocateLabel(Block->Handle);

		if( Node->If.False->Type != NODETYPE_NOP )
		{
			 int	if_true = Bytecode_AllocateLabel(Block->Handle);
			
			Bytecode_AppendCondJump(Block->Handle, if_true);
	
			// False
			ret = AST_ConvertNode(Block, Node->If.False, 0);
			if(ret)	return ret;
			Bytecode_AppendJump(Block->Handle, if_end);
			Bytecode_SetLabel(Block->Handle, if_true);
		}
		else
		{
			Bytecode_AppendCondJumpNot(Block->Handle, if_end);
		}
		
		// True
		ret = AST_ConvertNode(Block, Node->If.True, 0);
		if(ret)	return ret;

		// End
		Bytecode_SetLabel(Block->Handle, if_end);
		} break;

	// Ternary
	case NODETYPE_TERNARY: {
		ret = AST_ConvertNode(Block, Node->If.Condition, 1);
		if(ret)	return ret;

		ret = _StackPop(Block, Node->If.Condition, POP_UNDEF, &type, NULL);
		if(ret < 0)	return -1;
		
		int if_end = Bytecode_AllocateLabel(Block->Handle);
		
		if( Node->If.True )
		{
			int if_false = Bytecode_AllocateLabel(Block->Handle);
			// Actual Ternary
			Bytecode_AppendCondJumpNot(Block->Handle, if_false);
			
			ret = AST_ConvertNode(Block, Node->If.True, 1);
			if(ret)	return ret;
			ret = _StackPop(Block, Node, POP_UNDEF, &type, NULL);
			if(ret < 0)	return -1;
			Bytecode_AppendJump(Block->Handle, if_end);
			
			Bytecode_SetLabel(Block->Handle, if_false);
			ret = AST_ConvertNode(Block, Node->If.False, 1);
			if(ret)	return ret;
			ret = _StackPop(Block, Node, type, NULL, NULL);
			if(ret < 0)	return -1;
		}
		else
		{
			Block->NullType = type;
			// Null-Coalescing
			Bytecode_AppendDuplicate(Block->Handle);
			Bytecode_AppendCondJump(Block->Handle, if_end);
			
			ret = AST_ConvertNode(Block, Node->If.False, 1);
			if(ret)	return ret;
			ret = _StackPop(Block, Node, type, NULL, NULL);
			if(ret < 0)	return -1;
		}
		Bytecode_SetLabel(Block->Handle, if_end);
		// No pop, as the ternary operator generates a result
		ret = _StackPush(Block, Node, type, NULL);
		if(ret < 0)	return -1;

		} break;	

	// Switch construct
	case NODETYPE_SWITCH: {
		ret = AST_ConvertNode(Block, Node->BinOp.Left, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, Node, POP_UNDEF, &type, NULL);
		if(ret<0)	return ret;

		 int	saved_break = Block->BreakTarget;
		const char	*saved_tag = Block->Tag;
		 int	switch_end = Bytecode_AllocateLabel(Block->Handle);
		Block->BreakTarget = switch_end;
		Block->Tag = "";
	
		// Count cases	
		 int	nCases = 0;
		for( tAST_Node *node = Node->BinOp.Right; node; node = node->NextSibling )
			nCases ++;
		
		// Insert condition checks
		 int	case_labels[nCases];
		 int	i = 0, default_index = -1;
		for( tAST_Node *node = Node->BinOp.Right; node; node = node->NextSibling, i++ )
		{
			case_labels[i] = Bytecode_AllocateLabel(Block->Handle);
			if( node->BinOp.Left ) {
				Bytecode_AppendDuplicate(Block->Handle);
				ret = AST_ConvertNode(Block, node->BinOp.Left, 1);
				if(ret)	return ret;
				ret = _StackPop(Block, Node, type, NULL, NULL);
				if(ret < 0)	return ret;
				Bytecode_AppendBinOp(Block->Handle, BC_OP_EQUALS);
				Bytecode_AppendCondJump(Block->Handle, case_labels[i]);
			}
			else {
				if( default_index != -1 ) {
					AST_RuntimeError(node, "Multiple 'default' labels in switch");
					return -1;
				}
				default_index = i;
			}
		}
		if( default_index == -1 )
			Bytecode_AppendJump(Block->Handle, switch_end);
		else
			Bytecode_AppendJump(Block->Handle, default_index);
	
		// Code	
		i = 0;
		for( tAST_Node *node = Node->BinOp.Right; node; node = node->NextSibling, i++ )
		{
			Bytecode_SetLabel(Block->Handle, case_labels[i]);
			ret = AST_ConvertNode(Block, node->BinOp.Right, 0);
			if(ret)	return ret;
		}
		Bytecode_SetLabel(Block->Handle, switch_end);
		
		Block->BreakTarget = saved_break;
		Block->Tag = saved_tag;
		break; }

	// Loop
	case NODETYPE_LOOP: {
		 int	loop_start, loop_end, code_end;
		 int	saved_break, saved_continue;
		const char	*saved_tag;

		// Initialise
		ret = AST_ConvertNode(Block, Node->For.Init, 0);
		if(ret)	return ret;
		
		loop_start = Bytecode_AllocateLabel(Block->Handle);
		code_end = Bytecode_AllocateLabel(Block->Handle);
		loop_end = Bytecode_AllocateLabel(Block->Handle);

		saved_break = Block->BreakTarget;
		saved_continue = Block->ContinueTarget;
		saved_tag = Block->Tag;
		Block->BreakTarget = loop_end;
		Block->ContinueTarget = code_end;
		Block->Tag = Node->For.Tag;

		Bytecode_SetLabel(Block->Handle, loop_start);

		// Check initial condition
		if( !Node->For.bCheckAfter )
		{
			ret = AST_ConvertNode(Block, Node->For.Condition, 1);
			if(ret)	return ret;
			Bytecode_AppendUniOp(Block->Handle, BC_OP_LOGICNOT);
			// Boolean magic in exec_bytecode.c
			ret = _StackPop(Block, Node->For.Condition, POP_UNDEF, NULL, NULL);
			if(ret < 0)	return -1;
			Bytecode_AppendCondJump(Block->Handle, loop_end);
		}
	
		// Code
		ret = AST_ConvertNode(Block, Node->For.Code, 0);
		if(ret)	return ret;

		Bytecode_SetLabel(Block->Handle, code_end);
	
		// Increment
		ret = AST_ConvertNode(Block, Node->For.Increment, 0);
		if(ret)	return ret;

		// Tail check
		if( Node->For.bCheckAfter )
		{
			ret = AST_ConvertNode(Block, Node->For.Condition, 1);
			if(ret)	return ret;
			// Boolean magic in exec_bytecode.c
			ret = _StackPop(Block, Node->If.Condition, POP_UNDEF, NULL, NULL);
			if(ret < 0)	return ret;
			Bytecode_AppendCondJump(Block->Handle, loop_start);
		}
		else
		{
			Bytecode_AppendJump(Block->Handle, loop_start);
		}

		Bytecode_SetLabel(Block->Handle, loop_end);

		Block->BreakTarget = saved_break;
		Block->ContinueTarget = saved_continue;
		Block->Tag = saved_tag;
		} break;
	
	// Return
	case NODETYPE_RETURN:
		// Special case for `return null;`
		if( Node->UniOp.Value->Type == NODETYPE_NULL ) {
			if( SS_ISTYPEREFERENCE(Block->Function->ReturnType) )
				;
			else {
				AST_RuntimeError(Node, "Cannot return null when not a reference type");
				return -1;
			}
			Bytecode_AppendConstNull(Block->Handle, Block->Function->ReturnType);
		}
		else {
			ret = AST_ConvertNode(Block, Node->UniOp.Value, 1);
			if(ret)	return ret;
			// Pop return type and check that it's sane
			ret = _StackPop(Block, Node->UniOp.Value, Block->Function->ReturnType, NULL, NULL);
			if(ret < 0)	return -1;
		}
		Bytecode_AppendReturn(Block->Handle);
		break;
	
	case NODETYPE_BREAK:
	case NODETYPE_CONTINUE: {
		tAST_BlockInfo	*bi = Block;
		if( Node->Variable.Name[0] ) {
			while(bi && (!bi->Tag || strcmp(bi->Tag, Node->Variable.Name) != 0))
				bi = bi->Parent;
		}
		else {
			while(bi && !bi->Tag)
				bi = bi->Parent;
		}
		if( !bi ) {
			AST_RuntimeError(Node, "Unable to find continue/break target '%s'",
				Node->Variable.Name);
			return 1;
		}
		
		if( Node->Type == NODETYPE_BREAK ) {
			if( bi->BreakTarget == 0 ) {
				AST_RuntimeError(Node, "Break target invalid");
				return 1;
			}
			Bytecode_AppendJump(Block->Handle, bi->BreakTarget);
		}
		else {
			if( bi->ContinueTarget == 0 ) {
				AST_RuntimeError(Node, "Continue target invalid");
				return 1;
			}
			Bytecode_AppendJump(Block->Handle, bi->ContinueTarget);
		}
		} break;
	
	// Define a variable
	case NODETYPE_DEFVAR:
		ret = BC_Variable_Define(Block, Node, Node->DefVar.DataType, Node->DefVar.Name);
		if(ret)	return ret;
		
		if( Node->DefVar.InitialValue )
		{
			ret = AST_ConvertNode(Block, Node->DefVar.InitialValue, 1);
			if(ret)	return ret;
			ret = _StackPop(Block, Node->DefVar.InitialValue, Node->DefVar.DataType, NULL, NULL);
			if(ret < 0)	return -1;
			Bytecode_AppendSaveVar(Block->Handle, Node->DefVar.Name);
		}
		break;
	
	// Define/Import a global variable
	case NODETYPE_DEFGLOBAL:
		ret = BC_Variable_DefImportGlobal(Block, Node, Node->DefVar.DataType, Node->DefVar.Name);
		if(ret)	return ret;
		
		if( Node->DefVar.InitialValue )
		{
			ret = AST_ConvertNode(Block, Node->DefVar.InitialValue, 1);
			if(ret)	return ret;
			ret = _StackPop(Block, Node->DefVar.InitialValue, Node->DefVar.DataType, NULL, NULL);
			if(ret < 0)	return -1;
			Bytecode_AppendSaveVar(Block->Handle, Node->DefVar.Name);
		}
		break;
	
	// Variable
	case NODETYPE_VARIABLE:
		ret = BC_Variable_GetValue( Block, Node );
		if(ret)	return ret;
		CHECK_IF_NEEDED(1);
		break;
	
	// Element of an Object
	case NODETYPE_ELEMENT:
		ret = AST_ConvertNode( Block, Node->Scope.Element, 1 );
		if(ret)	return ret;

		ret = _StackPop(Block, Node, POP_UNDEF, &type, &ident);
		if(ret < 0)	return -1;

		if( type.ArrayDepth || type.Def == NULL ) {
			AST_RuntimeError(Node, "");
			return -1;
		}
		else if( type.Def->Class == SS_TYPECLASS_NCLASS ) {
			nc = type.Def->NClass;
			for( i = 0; i < nc->NAttributes; i ++ ) {
				if( strcmp(Node->Scope.Name, nc->AttributeDefs[i].Name) == 0 )
					break;
			}
			if( i == nc->NAttributes )
				AST_RuntimeError(Node, "Class %s does not have an attribute '%s'",
					nc->Name, Node->Scope.Name);
			type2 = nc->AttributeDefs[i].Type;
		}
		else if( type.Def->Class == SS_TYPECLASS_SCLASS ) {
			sc = type.Def->SClass;
			tScript_Var *at;
			for( at = sc->FirstProperty; at; at = at->Next )
			{
				if( strcmp(Node->Scope.Name, at->Name) == 0 )
					break;
			}
			if( !at )
				AST_RuntimeError(Node, "Class %s does not have an attribute '%s'",
					sc->Name, Node->Scope.Name);
			type2 = at->Type;
		}
		else {
			AST_RuntimeError(Node, "Getting element of non-class type %i", ret);
		}

		// TODO: Don't save the element name, instead store the index into the attribute array
		Bytecode_AppendElement(Block->Handle, Node->Scope.Name);
		
		ret = _StackPush(Block, Node, type2, NULL);
		if(ret < 0)	return -1;
		CHECK_IF_NEEDED(1);
		break;

	// Cast a value to another
	case NODETYPE_CAST:
		ret = AST_ConvertNode(Block, Node->Cast.Value, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, Node, POP_UNDEF, &type, NULL);
		if(ret < 0)	return -1;

		ret = BC_CastValue(Block, Node, Node->Cast.DataType, type);
		CHECK_IF_NEEDED(1);
		break;

	// Index into an array
	case NODETYPE_INDEX:
		// - Array
		ret = AST_ConvertNode(Block, Node->BinOp.Left, 1);
		if(ret)	return ret;
		//  > Type check
		ret = _StackPop(Block, Node, POP_UNDEF, &type, NULL);
		if(ret < 0)	return -1;
		
		// - Offset
		ret = AST_ConvertNode(Block, Node->BinOp.Right, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, Node, POP_INTEGER, &type2, NULL);
		if(ret < 0)	return -1;
		
		if(SS_GETARRAYDEPTH(type) != 0)
		{
			Bytecode_AppendIndex(Block->Handle);
		
			// Update the array depth
			type.ArrayDepth --;
			ret = _StackPush(Block, Node, type, NULL);
			if( ret < 0 )	return -1;
		}
		else if( SS_ISTYPEOBJECT(type) )
		{
			tSpiderTypeRef	args[] = {type, type2};
//			char	*name = SpiderScript_FormatTypeStr1(Block->Script, "operator [](%s)", type2);
//			ret = BC_CallFunction(Block, Node, NULL, NULL, name, 2, args);
//			free(name);
			ret = BC_CallFunction(Block, Node, NULL, NULL, "operator []", 2, args);
			if(ret < 0)	return -1;
		}
		else
		{
			AST_RuntimeError(Node, "Type mismatch, Expected an array, got %i", ret);
			return -2;
		}
		
		CHECK_IF_NEEDED(1);
		break;

	// TODO: Implement runtime constants
	case NODETYPE_CONSTANT:
		// TODO: Scan namespace for constant name
		AST_RuntimeError(Node, "TODO - Runtime Constants");
		return -1;
	
	// Constant Values
	case NODETYPE_NULL:
		Bytecode_AppendConstNull(Block->Handle, Block->NullType);
		ret = _StackPush(Block, Node, Block->NullType, NULL);
		if(ret < 0)	return -1;
		CHECK_IF_NEEDED(1);
		break;
	case NODETYPE_BOOLEAN:
		Bytecode_AppendConstInt(Block->Handle, 0);
		ret = _StackPush(Block, Node, POP_BOOLEAN, NULL);
		if(ret < 0)	return -1;
		CHECK_IF_NEEDED(1);
		break;
	case NODETYPE_INTEGER:
		Bytecode_AppendConstInt(Block->Handle, Node->ConstInt);
		ret = _StackPush(Block, Node, POP_INTEGER, NULL);
		if(ret < 0)	return -1;
		CHECK_IF_NEEDED(1);
		break;
	case NODETYPE_STRING:
		Bytecode_AppendConstString(Block->Handle, Node->ConstString->Data, Node->ConstString->Length);
		ret = _StackPush(Block, Node, POP_STRING, NULL);
		if(ret < 0)	return -1;
		CHECK_IF_NEEDED(1);
		break;
	case NODETYPE_REAL:
		Bytecode_AppendConstReal(Block->Handle, Node->ConstReal);
		ret = _StackPush(Block, Node, POP_REAL, NULL);
		if(ret < 0)	return -1;
		CHECK_IF_NEEDED(1);
		break;
	
	case NODETYPE_DELETE:
		// TODO: POP_NOVALUE?
		ret = _StackPush(Block, Node, POP_UNDEF, NULL);
		if(ret < 0)	return -1;
		BC_SaveValue(Block, Node->UniOp.Value);
		if( bKeepValue ) {
			AST_RuntimeError(Node, "'delete' does not return any value");
			return -1;
		}
		return 0;

	// --- Operations ---
	// Boolean Operations
	case NODETYPE_LOGICALNOT:	// Logical NOT (!)
		if(!op)	op = BC_OP_LOGICNOT;
	case NODETYPE_BWNOT:	// Bitwise NOT (~)
		if(!op)	op = BC_OP_BITNOT;
	case NODETYPE_NEGATE:	// Negation (-)
		if(!op)	op = BC_OP_NEG;
		ret = AST_ConvertNode(Block, Node->UniOp.Value, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, Node->UniOp.Value, POP_UNDEF, &type, NULL);
		if(ret < 0)	return -1;

		if( SS_GETARRAYDEPTH(type) != 0) {
			AST_RuntimeError(Node, "Unary operation on array is invalid");
			return -1;
		}
		else if( SS_ISTYPEOBJECT(type) ) {
			const char *name;
			tSpiderTypeRef args[] = {type};
			switch(Node->Type)
			{
			case NODETYPE_LOGICALNOT:	name = "operator !";	break;
			case NODETYPE_BWNOT:	name = "operator ~";	break;
			case NODETYPE_NEGATE:	name = "operator -";	break;
			default:
				AST_RuntimeError(Node, "BUG - Node %i unhandled in UniOp on Object", Node->Type);
				return -1;
			}
			// TODO: Somehow handle if the object doesn't expose an "operator !" and use the UniOp instead
			ret = BC_CallFunction(Block, Node, NULL, NULL, name, 1, args);
			if(ret < 0)	return ret;
		}
		else if( type.Def != NULL || type.Def->Class == SS_TYPECLASS_CORE )
		{
			Bytecode_AppendUniOp(Block->Handle, op);
			i = AST_ExecuteNode_UniOp_GetType(Block->Script, Node->Type, type.Def->Core);
			if( i <= 0 ) {
				AST_RuntimeError(Node, "Invalid unary operation #%i on 0x%x", Node->Type, type);
				return -1;
			}
			ret = _StackPush(Block, Node, type, NULL);
			if(ret < 0)	return -1;
			type = _GetCoreType(i);
		}
		else
		{
			AST_RuntimeError(Node, "Unary boolean on invalid type %s",
				SpiderScript_GetTypeName(Block->Script, type));
			return -1;
		}
		
		CHECK_IF_NEEDED(1);
		break;

	// Reference Stuff
	case NODETYPE_REFEQUALS:	if(!op)	op = 1;
	case NODETYPE_REFNOTEQUALS:	if(!op)	op = 2;
		op --;
		// Left first
		ret = AST_ConvertNode(Block, Node->BinOp.Left, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, Node->BinOp.Left, POP_UNDEF, &type, NULL);
		if(ret < 0)	return -1;

		if( SS_GETARRAYDEPTH(type) )
			;	// Array - can be ref-compared
		else if(SS_ISTYPEREFERENCE(type))
			;	// Object - OK too
		else if( SS_ISCORETYPE(type, SS_DATATYPE_STRING) )
			;	// Strings - yup
		else {
			// Value type - nope.avi
			AST_RuntimeError(Node, "Can't use reference comparisons on value types");
			return -1;
		}

		// Then right
		if( Node->BinOp.Right->Type != NODETYPE_NULL )
		{
			ret = AST_ConvertNode(Block, Node->BinOp.Right, 1);
			if(ret)	return ret;
			ret = _StackPop(Block, Node->BinOp.Right, type, NULL, NULL);
			if(ret < 0)	return -1;
			
		}
		else
		{
			Bytecode_AppendConstNull(Block->Handle, type);
		}
		if( op == 0 )
			Bytecode_AppendBinOp(Block->Handle, BC_OP_REFEQUALS);
		else
			Bytecode_AppendBinOp(Block->Handle, BC_OP_REFNOTEQUALS);
		
		ret = _StackPush(Block, Node, POP_BOOLEAN, NULL);
		if(ret < 0)	return -1;
		
		CHECK_IF_NEEDED(1);
		break;

	// Logic
	case NODETYPE_LOGICALAND:	if(!op)	op = BC_OP_LOGICAND;
	case NODETYPE_LOGICALOR:	if(!op)	op = BC_OP_LOGICOR;
	case NODETYPE_LOGICALXOR:	if(!op)	op = BC_OP_LOGICXOR;
	// Comparisons
	case NODETYPE_EQUALS:   	if(!op)	op = BC_OP_EQUALS;
	case NODETYPE_NOTEQUALS:	if(!op)	op = BC_OP_NOTEQUALS;
	case NODETYPE_LESSTHAN: 	if(!op)	op = BC_OP_LESSTHAN;
	case NODETYPE_GREATERTHAN:	if(!op)	op = BC_OP_GREATERTHAN;
	case NODETYPE_LESSTHANEQUAL:	if(!op)	op = BC_OP_LESSTHANOREQUAL;
	case NODETYPE_GREATERTHANEQUAL:	if(!op)	op = BC_OP_GREATERTHANOREQUAL;
	// General Binary Operations
	case NODETYPE_ADD:	if(!op)	op = BC_OP_ADD;
	case NODETYPE_SUBTRACT:	if(!op)	op = BC_OP_SUBTRACT;
	case NODETYPE_MULTIPLY:	if(!op)	op = BC_OP_MULTIPLY;
	case NODETYPE_DIVIDE:	if(!op)	op = BC_OP_DIVIDE;
	case NODETYPE_MODULO:	if(!op)	op = BC_OP_MODULO;
	case NODETYPE_BWAND:	if(!op)	op = BC_OP_BITAND;
	case NODETYPE_BWOR:	if(!op)	op = BC_OP_BITOR;
	case NODETYPE_BWXOR:	if(!op)	op = BC_OP_BITXOR;
	case NODETYPE_BITSHIFTLEFT:	if(!op)	op = BC_OP_BITSHIFTLEFT;
	case NODETYPE_BITSHIFTRIGHT:	if(!op)	op = BC_OP_BITSHIFTRIGHT;
	case NODETYPE_BITROTATELEFT:	if(!op)	op = BC_OP_BITROTATELEFT;
		// Left (because it's the output type)
		ret = AST_ConvertNode(Block, Node->BinOp.Left, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, Node->BinOp.Left, POP_UNDEF, &type, NULL);
		if(ret < 0)	return -1;

		// Right
		ret = AST_ConvertNode(Block, Node->BinOp.Right, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, Node->BinOp.Right, POP_UNDEF, &type2, NULL);
		if(ret < 0)	return -1;
		
		if( SS_GETARRAYDEPTH(type) != 0 ) {
			AST_RuntimeError(Node, "Binary operation on array is invalid");
			return -1;
		}
		else if( SS_ISTYPEOBJECT(type) ) {
			const char *name_tpl;
			tSpiderTypeRef args[] = {type, type2};
			switch(Node->Type)
			{
			case NODETYPE_LOGICALAND:	name_tpl = "operator &&(%s)";	break;
			case NODETYPE_LOGICALOR:	name_tpl = "operator ||(%s)";	break;
			case NODETYPE_LOGICALXOR:	name_tpl = "operator ^^(%s)";	break;
			case NODETYPE_EQUALS:   	name_tpl = "operator ==(%s)";	break;
			case NODETYPE_NOTEQUALS:	name_tpl = "operator !=(%s)";	break;
			case NODETYPE_LESSTHAN: 	name_tpl = "operator <(%s)";	break;
			case NODETYPE_LESSTHANEQUAL:	name_tpl = "operator <=(%s)";	break;
			case NODETYPE_GREATERTHAN: 	name_tpl = "operator >(%s)";	break;
			case NODETYPE_GREATERTHANEQUAL:	name_tpl = "operator >=(%s)";	break;
			case NODETYPE_ADD:	name_tpl = "operator +(%s)";	break;
			case NODETYPE_SUBTRACT:	name_tpl = "operator -(%s)";	break;
			case NODETYPE_MULTIPLY:	name_tpl = "operator *(%s)";	break;
			case NODETYPE_DIVIDE:	name_tpl = "operator %(%s)";	break;
			case NODETYPE_BWAND:	name_tpl = "operator &(%s)";	break;
			case NODETYPE_BWOR:	name_tpl = "operator |(%s)";	break;
			case NODETYPE_BWXOR:	name_tpl = "operator ^(%s)";	break;
			case NODETYPE_BITSHIFTLEFT:	name_tpl = "operator <<(%s)";	break;
			case NODETYPE_BITSHIFTRIGHT:	name_tpl = "operator >>(%s)";	break;
			case NODETYPE_BITROTATELEFT:	name_tpl = "operator <<<(%s)";	break;
			default:
				AST_RuntimeError(Node, "BUG - Node %i unhandled in BinOp on Object", Node->Type);
				return -1;
			}
			char	*name = SpiderScript_FormatTypeStr1(Block->Script, name_tpl, type2);
			ret = BC_CallFunction(Block, Node, &type, NULL, name, 2, args);
			free(name);
			if(ret < 0)	return ret;
			break ;
		}
		else if( type.Def != NULL || type.Def->Class == SS_TYPECLASS_CORE )
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
			i = AST_ExecuteNode_BinOp_GetType(Block->Script, Node->Type, ltype, rtype);
			if( i > 0 ) {
				// All good
				type = _GetCoreType(i);
			}
			else if( i < 0 ) {
				i = -i;
				tSpiderTypeRef	tgt_type = _GetCoreType(i);
				// Implicit cast
				if( Block->Script->Variant->bImplicitCasts ) {
					BC_CastValue(Block, Node, tgt_type, type2);
					_StackPop(Block, Node, tgt_type, NULL, NULL);
					i = AST_ExecuteNode_BinOp_GetType(Block->Script, Node->Type, ltype, i);
					type = _GetCoreType(i);
				}
				else {
					AST_RuntimeError(Node, "Cast required for %s #%i %s",
						SpiderScript_GetTypeName(Block->Script, type),
						op,
						SpiderScript_GetTypeName(Block->Script, tgt_type)
						);
					return -1;
				}
			}
			else {
				// Bad combo / no implicit
				AST_RuntimeError(Node, "Invalid binary operation (%s #%i %s)",
					SpiderScript_GetTypeName(Block->Script, type),
					op,
					SpiderScript_GetTypeName(Block->Script, type2)
					);
				return -1;
			}
			Bytecode_AppendBinOp(Block->Handle, op);
		}
		else
		{
			AST_RuntimeError(Node, "Binary operation on invalid type");
			return -1;
		}
		_StackPush(Block, Node, type, NULL);
		CHECK_IF_NEEDED(1);
		break;
	
	default:
		AST_RuntimeError(Node, "BUG - SpiderScript AST_ConvertNode Unimplemented %i", Node->Type);
		return -1;
	}
	
	Block->NullType = POP_UNDEF;

	DEBUGS1("Left NT%i", Node->Type);
	return 0;
}

int BC_ConstructObject(tAST_BlockInfo *Block, tAST_Node *Node, const char *Namespaces[], const char *Name, int NArgs, tSpiderTypeRef ArgTypes[])
{
	 int	ret;
	
	// Look up object
	tSpiderScript_TypeDef *def = SpiderScript_ResolveObject(Block->Script, Namespaces, Name);
	if( def == NULL )
	{
		AST_RuntimeError(Node, "Undefined reference to class %s", Name);
		return -1;
	}
	
	if( def->Class == SS_TYPECLASS_SCLASS )
	{
		tScript_Class	*sc = def->SClass;
		tScript_Function *sf;

		for( sf = sc->FirstFunction; sf; sf = sf->Next )
		{
			if( strcmp(sf->Name, CONSTRUCTOR_NAME) == 0 )
				break; 
		}
	
		if( sf )
		{
			// Argument count check
			if( NArgs+1 != sf->ArgumentCount ) {
				AST_RuntimeError(Node, "Constructor for %s takes %i arguments, passed %i",
					Name, sf->ArgumentCount, NArgs);
				return -1;
			}
			// Type checks
			for( int i = 1; i < NArgs+1; i ++ )
			{
				if( !SS_TYPESEQUAL(sf->Arguments[i].Type, ArgTypes[i-1]) ) {
					// Sad to be chucked
					AST_RuntimeError(Node, "Argument %i of %s constructor should be %i, given %i",
						i, Name, sf->Arguments[i].Type, ArgTypes[i-1]);
					return -1;
				}
			}
		}
		else
		{
			// No constructor, no arguments
			if( NArgs != 0 ) {
				AST_RuntimeError(Node,
					"Class %s has no constructor, no arguments allowed", Name);
				return -1;
			}
		}
	}
	else if( def->Class == SS_TYPECLASS_NCLASS )
	{
		tSpiderClass	*nc = def->NClass;
		tSpiderFunction *nf = nc->Constructor;
		 int	minArgc = 0;
		 int	bVariable = 0;

		if( !nf ) {
			minArgc = 0;
			bVariable = 0;
		}
		else {
			for( minArgc = 0; nf->Prototype->Args[minArgc].Def; minArgc ++ )
				;
			bVariable = nf->Prototype->bVariableArgs;
		}
		// Argument count check
		if( NArgs < minArgc || (!bVariable && NArgs > minArgc) ) {
			AST_RuntimeError(Node, "Constructor %s takes %i arguments, passed %i",
				Name, minArgc, NArgs);
			return -1;
		}
		// Type checks
		for( int i = 0; i < NArgs; i ++ )
		{
			if( !SS_TYPESEQUAL(nf->Prototype->Args[i], ArgTypes[i]) ) {
				// Sad to be chucked
				AST_RuntimeError(Node, "Argument %i of constructor %s should be %s, given %s",
					i, Name,
					SpiderScript_GetTypeName(Block->Script, nf->Prototype->Args[i]),
					SpiderScript_GetTypeName(Block->Script, ArgTypes[i]));
				return -1;
			}
		}
	}

	tSpiderTypeRef type = {.ArrayDepth = 0, .Def = def};
	Bytecode_AppendCreateObj(Block->Handle, type, NArgs);
		
	// Push return type
	ret = _StackPush(Block, Node, type, NULL);
	if(ret < 0)	return -1;
	
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
int BC_CallFunction(tAST_BlockInfo *Block, tAST_Node *Node, tSpiderTypeRef *ReturnType, const char *Namespaces[], const char *Name, int NArgs, tSpiderTypeRef ArgTypes[])
{
	 int	id = 0;
	 int	ret;
	tSpiderTypeRef	ret_type;
	tScript_Function *sf = NULL;
	tSpiderFunction  *nf = NULL;

	if( Namespaces == NULL )
	{
		if( NArgs < 1 ) {
			AST_RuntimeError(Node, "BUG - BC_CallFunction(Namespaces == NULL, NArgs < 1)");
			return -1;
		}		

		DEBUGS1("Getting method");
		if( ArgTypes[0].ArrayDepth ) {
			AST_RuntimeError(Node, "Method call on array");
			return -1;
		}
		if( ArgTypes[0].Def == NULL ) {
			AST_RuntimeError(Node, "Method call on NULL");
			return -1;
		}
		if( ArgTypes[0].Def->Class == SS_TYPECLASS_NCLASS )
		{
			tSpiderClass *nc = ArgTypes[0].Def->NClass;
			for( nf = nc->Methods; nf; nf = nf->Next, id ++ )
			{
				if( strcmp(nf->Name, Name) == 0 ) {
					break;
				}
			}
			if( !nf ) {
				AST_RuntimeError(Node, "Class %s does not have a method '%s'", nc->Name, Name);
				return -1;
			}
		}
		else if( ArgTypes[0].Def->Class == SS_TYPECLASS_SCLASS )
		{
			tScript_Class	*sc = ArgTypes[0].Def->SClass;
			// Script class
			for( sf = sc->FirstFunction; sf; sf = sf->Next, id ++ )
			{
				if( strcmp(sf->Name, Name) == 0 ) {
					break;
				}
			}
			if( !sf ) {
				AST_RuntimeError(Node, "Class %s does not have a method '%s'", sc->Name, Name);
				return -1;
			}
		}
		else
		{
			AST_RuntimeError(Node, "Method call on non-object (0x%x)", ArgTypes[0]);
			return -1;
		}
		DEBUGS1("Found sf=%p nf=%p", sf, nf);
	}
	else
	{
		void *ident;
		id = SpiderScript_ResolveFunction(Block->Script, Namespaces, Name, &ident);
		if( id == -1 ) {
			AST_RuntimeError(Node, "Undefined reference to %s", Name);
			return -1;
		}
		
		// TODO: Assuming the internals is hacky
		if( id >> 16 )
			nf = ident;
		else
			sf = ident;
	}

	if( sf )
	{
		// Argument count check
		if( NArgs != sf->ArgumentCount ) {
			AST_RuntimeError(Node, "%s takes %i arguments, passed %i",
				Name, sf->ArgumentCount, NArgs);
			return -1;
		}
		// Type checks
		for( int i = 0; i < NArgs; i ++ )
		{
			if( !SS_TYPESEQUAL(sf->Arguments[i].Type, ArgTypes[i]) ) {
				// Sad to be chucked
				AST_RuntimeError(Node, "Argument %i of %s should be %i, given %i",
					i, Name, sf->Arguments[i].Type, ArgTypes[i]);
				return -1;
			}
		}
	
		ret_type = sf->ReturnType;	
	}
	else if( nf )
	{
		 int	minArgc = 0;
		 int	bVariable = 0;
		
		for( minArgc = 0; nf->Prototype->Args[minArgc].Def != NULL; minArgc ++ )
			;
		bVariable = !!(nf->Prototype->bVariableArgs);
		DEBUGS1("minArgc = %i, bVariable = %i", minArgc, bVariable);

		// Check argument count
		if( NArgs < minArgc || (!bVariable && NArgs > minArgc) ) {
			AST_RuntimeError(Node, "%s takes %i%s arguments, passed %i",
				Name, minArgc, (bVariable?"+":""), NArgs);
			return -1;
		}

		// Check argument types (and passing too few arguments)
		for( int i = 0; i < minArgc; i ++ )
		{
			#if 0
			if( i == 0 && !Namespaces ) {
				if( nf->ArgTypes[i] != -2 ) {
					// TODO: Should I chuck?
				}
				continue ;
			}
			#endif
			tSpiderTypeRef	argtype = nf->Prototype->Args[i];
			// undefined = any type
			if( SS_ISCORETYPE(argtype, SS_DATATYPE_UNDEF) ) {
				continue ;
			}
			if( !SS_TYPESEQUAL(argtype, ArgTypes[i]) ) {
				AST_RuntimeError(Node, "Argument %i of %s should be %s, given %s",
					i, Name,
					SpiderScript_GetTypeName(Block->Script, argtype),
					SpiderScript_GetTypeName(Block->Script, ArgTypes[i])
					);
				return -1;
			}
		}
	
		ret_type = nf->Prototype->ReturnType;
	}
	else
	{
		AST_RuntimeError(Node, "Can't find '%s'", Name);
		return -1;
	}

	DEBUGS1("Add call bytecode op");
	if( Namespaces == NULL )
		Bytecode_AppendMethodCall(Block->Handle, id, NArgs);
	else
		Bytecode_AppendFunctionCall(Block->Handle, id, NArgs);
	
	// Push return type
	if( ret_type.Def != NULL )
	{
		ret = _StackPush(Block, Node, ret_type, NULL);
		if(ret < 0)	return -1;
		DEBUGS1("Push return type 0x%x", ret_type);
	}
	else
		DEBUGS1("Not pushing for void");

	if( ReturnType )
		*ReturnType = ret_type;	

	return 0;
}

int BC_SaveValue(tAST_BlockInfo *Block, tAST_Node *DestNode)
{
	 int	ret, i;
	tSpiderTypeRef	type, type2;
	void	*ident;

	switch(DestNode->Type)
	{
	// Variable, simple
	case NODETYPE_VARIABLE:
		ret = BC_Variable_SetValue( Block, DestNode );
		if(ret)	return ret;
		break;
	// Array index
	case NODETYPE_INDEX:
		// Array
		ret = AST_ConvertNode(Block, DestNode->BinOp.Left, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, DestNode->BinOp.Left, POP_UNDEF, &type, NULL);
		if(ret < 0)	return -1;
		if(SS_GETARRAYDEPTH(type) == 0) {
			AST_RuntimeError(DestNode, "Type mismatch, Expected an array, got %s",
				SpiderScript_GetTypeName(Block->Script, type));
			return -2;
		}
		type.ArrayDepth --;
		
		// Offset/index
		ret = AST_ConvertNode(Block, DestNode->BinOp.Right, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, DestNode->BinOp.Right, POP_INTEGER, &type2, NULL);
		if(ret < 0)	return -1;

		// Assignment value
		ret = _StackPop(Block, DestNode, POP_UNDEF, &type2, NULL);
		if(ret < 0)	return -1;
		if(type2.Def == 0 && SS_ISTYPEOBJECT(type)) {
			// Asigning NULL to an object array entry
			Bytecode_AppendConstNull(Block->Handle, type);
		}
		
		Bytecode_AppendSetIndex( Block->Handle );
		break;
	// Object element
	case NODETYPE_ELEMENT:
		ret = AST_ConvertNode(Block, DestNode->Scope.Element, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, DestNode->Scope.Element, POP_UNDEF, &type, &ident);
		if(ret)	return -1;

		if(type.ArrayDepth)
		{
			AST_RuntimeError(DestNode, "Attempting to access object element of array");
			return -2;
		}
		if(!type.Def)
		{
			AST_RuntimeError(DestNode, "BUG Accessing element of undefined object");
			return -2;
		}

		// Find type of the element
		if(type.Def->Class == SS_TYPECLASS_NCLASS)
		{
			tSpiderClass *nc = type.Def->NClass;
			for( i = 0; i < nc->NAttributes; i ++ ) {
				if( strcmp(DestNode->Scope.Name, nc->AttributeDefs[i].Name) == 0 )
					break;
			}
			if( i == nc->NAttributes ) {
				AST_RuntimeError(DestNode, "Class %s does not have an attribute %s",
					nc->Name, DestNode->Scope.Name);
				return -2;
			}
			type = nc->AttributeDefs[i].Type;
		}
		else if(type.Def->Class == SS_TYPECLASS_SCLASS)
		{
			tScript_Class	*sc  = type.Def->SClass;
			tScript_Var *at;
			for( at = sc->FirstProperty; at; at = at->Next )
			{
				if( strcmp(DestNode->Scope.Name, at->Name) == 0 )
					break;
			}
			if( !at ) {
				AST_RuntimeError(DestNode, "Class %s does not have an attribute %s",
					sc->Name, DestNode->Scope.Name);
				return -2;
			}
			type = at->Type;
		}
		else {
			AST_RuntimeError(DestNode, "Setting element of non-class type %s",
				SpiderScript_GetTypeName(Block->Script, type) );
			return -2;
		}

		// Assignment
		ret = _StackPop(Block, DestNode, type, &type2, NULL);
		if(ret < 0)	return -1;
		if(ret == STACKPOP_RV_NULL && SS_ISTYPEOBJECT(type)) {
			Bytecode_AppendConstNull(Block->Handle, type);
		}
		
		Bytecode_AppendSetElement( Block->Handle, DestNode->Scope.Name );
		break;
	// Anything else
	default:
		// TODO: Support assigning to object attributes
		AST_RuntimeError(DestNode, "Assignment target is not a LValue");
		return -1;
	}
	return 0;
}

int BC_CastValue(tAST_BlockInfo *Block, tAST_Node *Node, tSpiderTypeRef DestType, tSpiderTypeRef SourceType)
{
	 int	ret;
	if( SS_GETARRAYDEPTH(SourceType) && SS_ISCORETYPE(DestType, SS_DATATYPE_BOOLEAN) ) {
		AST_RuntimeError(Node, "Invalid cast from array (0x%x)", SourceType);
		return 1;
	}
	else if( SS_ISTYPEOBJECT(SourceType) ) {
		char *name = SpiderScript_FormatTypeStr1(Block->Script, "operator (%s)", DestType);
		tSpiderTypeRef args[] = {SourceType};
		tSpiderTypeRef	type;
		ret = BC_CallFunction(Block, Node, &type, NULL, name, 1, args);
		free(name);
		if(ret < 0)	return ret;
		if( !SS_TYPESEQUAL(type, DestType) ) {
			AST_RuntimeError(Node, "BUG - Cast from %s to %s does not returns %s",
				SpiderScript_GetTypeName(Block->Script, SourceType),
				SpiderScript_GetTypeName(Block->Script, DestType),
				SpiderScript_GetTypeName(Block->Script, type)
				);
			return -1;
		}
	}
	else if( DestType.ArrayDepth || !DestType.Def || DestType.Def->Class != SS_TYPECLASS_CORE ) {
		AST_RuntimeError(Node, "Invalid cast from %s to %s",
			SpiderScript_GetTypeName(Block->Script, SourceType),
			SpiderScript_GetTypeName(Block->Script, DestType)
			);
		return -1;
	}
	else {
		Bytecode_AppendCast(Block->Handle, DestType.Def->Core);
		ret = _StackPush(Block, Node, DestType, NULL);
		if(ret < 0)	return -1;
	}
	return 0;
}

const tScript_Var *BC_Variable_Lookup(tAST_BlockInfo *Block, tAST_Node *VarNode, const char *Name, int CreateType)
{
	tScript_Var	*var = NULL;
	
	for( tAST_BlockInfo *bs = Block; bs; bs = bs->Parent )
	{
		for( var = bs->FirstVar; var; var = var->Next )
		{
			if( strcmp(var->Name, Name) == 0 )
				break;
		}
		if(var)	break;
		
		for( int i = 0; i < MAX_GLOBALS; i ++ )
		{
			if( !bs->ImportedGlobals[i] )
				continue ;
			if( strcmp(bs->ImportedGlobals[i]->Name, Name) == 0 ) {
				var = bs->ImportedGlobals[i];
				break;
			}
		}
		if(var)	break;
	}

	if( !var )
	{
//		if( Block->Script->Variant->bDyamicTyped && CreateType != SS_DATATYPE_UNDEF ) {
//			// Define variable
//			var = BC_Variable_Define(Block, CreateType, VarNode->Variable.Name, NULL);
//		}
//		else
//		{
			if( VarNode->Type != NODETYPE_DEFGLOBAL )
			{
				AST_RuntimeError(VarNode, "Variable '%s' is undefined", Name);
			}
			return NULL;
//		}
	}
		
	#if TRACE_VAR_LOOKUPS
	AST_RuntimeMessage(VarNode, "debug", "Variable lookup of '%s' %p type %i",
		Name, var, var->Type);
	#endif
	
	return var;
}

/**
 * \brief Define a variable
 * \param Block	Current block state
 * \param Type	Type of the variable
 * \param Name	Name of the variable
 * \return Boolean Failure
 */
int BC_Variable_Define(tAST_BlockInfo *Block, tAST_Node *Node, tSpiderTypeRef Type, const char *Name)
{
	tScript_Var	*var, *prev = NULL;
	
	for( var = Block->FirstVar; var; prev = var, var = var->Next )
	{
		if( strcmp(var->Name, Name) == 0 ) {
			AST_RuntimeError(Node, "Redefinition of variable '%s'", Name);
			return -1;
		}
	}
	
	var = malloc( sizeof(tScript_Var) + strlen(Name) + 1 );
	var->Next = NULL;
	var->Type = Type;
	var->Name = (char*)(var + 1);
	strcpy(var->Name, Name);
	
	if(prev)	prev->Next = var;
	else	Block->FirstVar = var;
	
	Bytecode_AppendDefineVar(Block->Handle, Name, Type);
	return 0;
}

int BC_Variable_DefImportGlobal(tAST_BlockInfo *Block, tAST_Node *DefNode, tSpiderTypeRef Type, const char *Name)
{
	if( BC_Variable_Lookup(Block, DefNode, Name, SS_DATATYPE_UNDEF) ) {
		AST_RuntimeError(DefNode, "Global %s collides with exisint name", Name);
		return -1;
	}

	// Find a free slot
	 int	slot;
	for( slot = 0; slot < MAX_GLOBALS && Block->ImportedGlobals[slot]; slot ++ )
		;
	if( slot == MAX_GLOBALS ) {
		AST_RuntimeError(DefNode, "Too many globals in function, %i max", MAX_GLOBALS);
		return -1;
	}

	// Locate the global in the script
	tScript_Var	*var;
	for( var = Block->Script->FirstGlobal; var; var = var->Next )
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
		if( !Block->Script->FirstGlobal )
			Block->Script->FirstGlobal = var;
		else
			Block->Script->LastGlobal->Next = var;
		Block->Script->LastGlobal = var;
	}
	Block->ImportedGlobals[slot] = var;

	Bytecode_AppendImportGlobal(Block->Handle, Name, Type);
	
	return 0;
}

/**
 * \brief Set the value of a variable
 * \return Boolean Failure
 */
int BC_Variable_SetValue(tAST_BlockInfo *Block, tAST_Node *VarNode)
{
	const tScript_Var *var = BC_Variable_Lookup(Block, VarNode, VarNode->Variable.Name, SS_DATATYPE_UNDEF);
	if(!var)	return -1;

	tSpiderTypeRef	type;
	int ret = _StackPop(Block, VarNode, var->Type, &type, NULL);
	if( ret )	return -1;
	if( SS_ISTYPEOBJECT(var->Type) ) {
		Bytecode_AppendConstNull(Block->Handle, var->Type);
	}
	Bytecode_AppendSaveVar(Block->Handle, VarNode->Variable.Name);
	return 0;
}

/**
 * \brief Get the value of a variable
 */
int BC_Variable_GetValue(tAST_BlockInfo *Block, tAST_Node *VarNode)
{
	const tScript_Var *var = BC_Variable_Lookup(Block, VarNode, VarNode->Variable.Name, 0);	
	if(!var)	return -1;

	// NOTE: Abuses ->Object as the info pointer	
	_StackPush(Block, VarNode, var->Type, NULL);
	Bytecode_AppendLoadVar(Block->Handle, VarNode->Variable.Name);
	return 0;
}

void BC_Variable_Clear(tAST_BlockInfo *Block)
{
	tScript_Var	*var;
	
	for( var = Block->FirstVar; var; )
	{
		tScript_Var	*tv = var->Next;
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

int _StackPush(tAST_BlockInfo *Block, tAST_Node *Node, tSpiderTypeRef Type, void *Info)
{
	if(Block->StackDepth == MAX_STACK_DEPTH - 1) {
		AST_RuntimeError(Node, "BUG - Stack overflow in AST-Bytecode conversion (node=%i)",
			Node->Type);
		return -1;
	}

#if 0
	if( SS_TYPESEQUAL(Type, POP_UNDEF) ) {
		AST_RuntimeError(Node, "BUG - Pushed SS_DATATYPE_UNDEF (NT%i)", Node->Type);
		*(char*)0 = 1;
		return -1;
	}
#endif

	#if TRACE_TYPE_STACK
	AST_RuntimeMessage(Node, "_StackPush", "%x - NT%i", Type, Node->Type);
	#endif
	Block->StackDepth ++;
	Block->Stack[ Block->StackDepth ].Type = Type;
	Block->Stack[ Block->StackDepth ].Info = Info;
	return 0;
}

int _StackPop(tAST_BlockInfo *Block, tAST_Node *Node, tSpiderTypeRef WantedType, tSpiderTypeRef *OutType, void **Info)
{
	tSpiderTypeRef	havetype;
	if(Block->StackDepth == 0) {
		AST_RuntimeError(Node, "BUG - Stack underflow in AST-Bytecode conversion (node=%i)",
			Node->Type);
		return STACKPOP_RV_UFLOW;
	}
	havetype = Block->Stack[ Block->StackDepth ].Type;
	#if TRACE_TYPE_STACK
	AST_RuntimeMessage(Node, "_StackPop", "%x(==%x) - NT%i", havetype, WantedType, Node->Type);
	#endif
	// Wanted == POP_UNDEF/void means any type is desired (or complex checks are done)
	if( !SS_TYPESEQUAL(WantedType, POP_UNDEF) && !SS_TYPESEQUAL(havetype, POP_UNDEF) )
	{
		if( !SS_TYPESEQUAL(havetype, WantedType) ) {
			AST_RuntimeError(Node, "AST-Bytecode - Type mismatch (wanted %s got %s)",
				SpiderScript_GetTypeName(Block->Script, WantedType),
				SpiderScript_GetTypeName(Block->Script, havetype)
				);
			return STACKPOP_RV_MISMATCH;
		}
	}
	if(Info)
		*Info = Block->Stack[Block->StackDepth].Info;
	Block->StackDepth--;
	if( OutType )
		*OutType = havetype;
	if( SS_TYPESEQUAL(havetype, POP_UNDEF) )
		return STACKPOP_RV_NULL;
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

