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

#define DEBUG	0
#define TRACE_VAR_LOOKUPS	0
#define TRACE_TYPE_STACK	0
#define MAX_NAMESPACE_DEPTH	10
#define MAX_STACK_DEPTH	10	// This is for one function, so shouldn't need more
#define SS_DATATYPE_UNDEF	-1

#if DEBUG >= 1
# define DEBUGS1(s, v...)	printf("%s: "s"\n", __func__, ## v)
#else
# define DEBUGS1(...)	do{}while(0)
#endif

// === IMPORTS ===
// TODO: These should not be here
extern tSpiderFunction	*gpExports_First;
extern char *SpiderScript_FormatTypeStr1(tSpiderScript *Script, const char *Template, int Type1);

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

	 int	StackDepth;
	struct {
		int	Type;
		void	*Info;
	}	Stack[MAX_STACK_DEPTH];	// Stores types of stack values
	
	tAST_Variable	*FirstVar;
	tAST_Node	*CurNode;
} tAST_BlockInfo;

// === PROTOTYPES ===
// Node Traversal
 int	AST_ConvertNode(tAST_BlockInfo *Block, tAST_Node *Node, int bKeepValue);
 int	BC_ConstructObject(tAST_BlockInfo *Block, tAST_Node *Node, const char *Namespaces[], const char *Name, int NArgs, int ArgTypes[]);
 int	BC_CallFunction(tAST_BlockInfo *Block, tAST_Node *Node, const char *Namespaces[], const char *Name, int NArgs, int ArgTypes[]);
 int	BC_SaveValue(tAST_BlockInfo *Block, tAST_Node *DestNode);
 int	BC_CastValue(tAST_BlockInfo *Block, tAST_Node *Node, int DestType, int SourceType);

// Variables
 int 	BC_Variable_Define(tAST_BlockInfo *Block, tAST_Node *DefNode, int Type, const char *Name);
 int	BC_Variable_SetValue(tAST_BlockInfo *Block, tAST_Node *VarNode);
 int	BC_Variable_GetValue(tAST_BlockInfo *Block, tAST_Node *VarNode);
void	BC_Variable_Clear(tAST_BlockInfo *Block);
// - Errors
void	AST_RuntimeMessage(tAST_Node *Node, const char *Type, const char *Format, ...);
void	AST_RuntimeError(tAST_Node *Node, const char *Format, ...);
// - Type stack
 int	_StackPush(tAST_BlockInfo *Block, tAST_Node *Node, int Type, void *Info);
 int	_StackPop(tAST_BlockInfo *Block, tAST_Node *Node, int WantedType, void **Info);

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
	
	ret = Bytecode_CreateFunction(Fcn);
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
	_StackPop(Block, Node, SS_DATATYPE_UNDEF, NULL);\
} } while(0)

/**
 * \brief Convert a node into bytecode
 * \param Block	Execution context
 * \param Node	Node to execute
 */
int AST_ConvertNode(tAST_BlockInfo *Block, tAST_Node *Node, int bKeepValue)
{
	tAST_Node	*node;
	 int	ret = 0, type;
	 int	i, op = 0;
	 int	bAddedValue = 1;	// Used to tell if the value needs to be deleted
	void	*ident;	// used for classes
	tScript_Class	*sc;
	tSpiderClass *nc;

	DEBUGS1("Node->Type = %i", Node->Type);
	switch(Node->Type)
	{
	// No Operation
	case NODETYPE_NOP:
		bAddedValue = 0;
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
			 int	t1, t2;
			
			ret = AST_ConvertNode(Block, Node->Assign.Dest, 1);
			if(ret)	return ret;
			t1 = _StackPop(Block, Node, SS_DATATYPE_UNDEF, NULL);
			if(t1 < 0)	return -1;
			
			ret = AST_ConvertNode(Block, Node->Assign.Value, 1);
			if(ret)	return ret;
			t2 = _StackPop(Block, Node, SS_DATATYPE_UNDEF, NULL);
			if(t2 < 0)	return -1;


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
			ret = _StackPop(Block, Node, SS_DATATYPE_UNDEF, &ident);
			if(ret < 0)	return -1;
			ret = _StackPush(Block, Node, ret, ident);
			if(ret < 0)	return -1;
			ret = _StackPush(Block, Node, ret, ident);
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
		
		Bytecode_AppendConstInt(Block->Handle, 1);
		ret = _StackPush(Block, Node, SS_DATATYPE_INTEGER, NULL);
		if(ret < 0)	return -1;
		
		ret = AST_ConvertNode(Block, Node->UniOp.Value, 1);
		if(ret)	return ret;

		if( Node->Type == NODETYPE_POSTDEC )
			Bytecode_AppendBinOp(Block->Handle, BC_OP_SUBTRACT);
		else
			Bytecode_AppendBinOp(Block->Handle, BC_OP_ADD);

		ret = _StackPop(Block, Node, SS_DATATYPE_INTEGER, NULL);	// TODO: Check for objects too
		if(ret < 0)	return -1;
		ret = BC_SaveValue(Block, Node->UniOp.Value);
		if(ret)	return ret;
		break;

	// Function Call
	case NODETYPE_METHODCALL: {
		 int	nargs = 1;	// `this`
		
		// Push arguments to the stack
		for(node = Node->FunctionCall.FirstArg; node; node = node->NextSibling)
			nargs ++;
		
		int argtypes[nargs];
		
		ret = AST_ConvertNode(Block, Node->FunctionCall.Object, 1);
		if(ret)	return ret;
		type = _StackPop(Block, Node, SS_DATATYPE_UNDEF, NULL);
		if(type < 0)	return -1;
		argtypes[0] = type;
		
		int i = 1;
		for(node = Node->FunctionCall.FirstArg; node; node = node->NextSibling, i++)
		{
			// Convert argument
			ret = AST_ConvertNode(Block, node, 1);
			if(ret)	return ret;
			
			// Pop type off the stack
			argtypes[i] = _StackPop(Block, Node, SS_DATATYPE_UNDEF, NULL);
			if(argtypes[i] < 0)	return -1;
		}
	
		ret = BC_CallFunction(Block, Node, NULL, Node->FunctionCall.Name, nargs, argtypes);
		if(ret < 0)	return ret;
		
		if( ret != 0 ) {
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
		int	arg_types[nargs];
		nargs = 0;
		for(node = Node->FunctionCall.FirstArg; node; node = node->NextSibling)
		{
			ret = AST_ConvertNode(Block, node, 1);
			if(ret)	return ret;
			
			arg_types[nargs] = _StackPop(Block, node, SS_DATATYPE_UNDEF, 0);
			if(arg_types[nargs] < 0)	return -1;
			nargs ++;
		}
		
		// Call the function
		if( Node->Type == NODETYPE_CREATEOBJECT )
		{
			ret = BC_ConstructObject(Block, Node, namespaces, Node->FunctionCall.Name, nargs, arg_types);
			if(ret < 0)	return ret;
		}
		else
		{
			ret = BC_CallFunction(Block, Node, namespaces, Node->FunctionCall.Name, nargs, arg_types);
			if(ret < 0)	return ret;
		}

		if( ret != 0 ) {
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
		ret = _StackPop(Block, Node->Cast.Value, SS_DATATYPE_INTEGER, NULL);
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
		ret = _StackPop(Block, Node->If.Condition, SS_DATATYPE_UNDEF, NULL);
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
			ret = _StackPop(Block, Node->For.Condition, SS_DATATYPE_UNDEF, NULL);
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
			ret = _StackPop(Block, Node->If.Condition, SS_DATATYPE_UNDEF, NULL);
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
		ret = AST_ConvertNode(Block, Node->UniOp.Value, 1);
		if(ret)	return ret;
		Bytecode_AppendReturn(Block->Handle);
		// Pop return type and check that it's sane
		ret = _StackPop(Block, Node->UniOp.Value, Block->Function->ReturnType, NULL);
		if(ret < 0)	return -1;
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
			ret = _StackPop(Block, Node->DefVar.InitialValue, Node->DefVar.DataType, NULL);
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

		ret = _StackPop(Block, Node, SS_DATATYPE_UNDEF, &ident);
		if(ret < 0)	return -1;

		nc = SpiderScript_GetClass_Native(Block->Script, ret);
		sc = SpiderScript_GetClass_Script(Block->Script, ret);

		if(nc) {
			for( i = 0; i < nc->NAttributes; i ++ ) {
				if( strcmp(Node->Scope.Name, nc->AttributeDefs[i].Name) == 0 )
					break;
			}
			if( i == nc->NAttributes )
				AST_RuntimeError(Node, "Class %s does not have an attribute '%s'",
					nc->Name, Node->Scope.Name);
			ret = nc->AttributeDefs[i].Type;
		}
		else if(sc) {
			tScript_Class_Var *at;
			for( at = sc->FirstProperty; at; at = at->Next )
			{
				if( strcmp(Node->Scope.Name, at->Name) == 0 )
					break;
			}
			if( !at )
				AST_RuntimeError(Node, "Class %s does not have an attribute '%s'",
					sc->Name, Node->Scope.Name);
			ret = at->Type;
		}
		else {
			AST_RuntimeError(Node, "Getting element of non-class type %i", ret);
		}

		// TODO: Don't save the element name, instead store the index into the attribute array
		Bytecode_AppendElement(Block->Handle, Node->Scope.Name);
		
		ret = _StackPush(Block, Node, ret, NULL);
		if(ret < 0)	return -1;
		CHECK_IF_NEEDED(1);
		break;

	// Cast a value to another
	case NODETYPE_CAST:
		ret = AST_ConvertNode(Block, Node->Cast.Value, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, Node, SS_DATATYPE_UNDEF, NULL);
		if(ret < 0)	return -1;

		ret = BC_CastValue(Block, Node, Node->Cast.DataType, ret);
		CHECK_IF_NEEDED(1);
		break;

	// Index into an array
	case NODETYPE_INDEX:
		// - Array
		ret = AST_ConvertNode(Block, Node->BinOp.Left, 1);
		if(ret)	return ret;
		//  > Type check
		ret = _StackPop(Block, Node, SS_DATATYPE_UNDEF, NULL);
		if(ret < 0)	return -1;
		type = ret;
		
		// - Offset
		ret = AST_ConvertNode(Block, Node->BinOp.Right, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, Node, SS_DATATYPE_INTEGER, NULL);
		if(ret < 0)	return -1;
		
		if(SS_GETARRAYDEPTH(type) != 0)
		{
			Bytecode_AppendIndex(Block->Handle);
		
			// Update the array depth
			type = SS_DOWNARRAY(type);	// Decrease the array level
			ret = _StackPush(Block, Node, type, NULL);
			if( ret < 0 )	return -1;
		}
		else if( SS_ISTYPEOBJECT(type) )
		{
			 int	args[] = {type, ret};	// `ret` is the index type
			char	*name = SpiderScript_FormatTypeStr1(Block->Script, "operator [](%s)", ret);
			ret = BC_CallFunction(Block, Node, NULL, name, 2, args);
			free(name);
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
	case NODETYPE_STRING:
		Bytecode_AppendConstString(Block->Handle, Node->ConstString->Data, Node->ConstString->Length);
		ret = _StackPush(Block, Node, SS_DATATYPE_STRING, NULL);
		if(ret < 0)	return -1;
		CHECK_IF_NEEDED(1);
		break;
	case NODETYPE_INTEGER:
		Bytecode_AppendConstInt(Block->Handle, Node->ConstInt);
		ret = _StackPush(Block, Node, SS_DATATYPE_INTEGER, NULL);
		if(ret < 0)	return -1;
		CHECK_IF_NEEDED(1);
		break;
	case NODETYPE_REAL:
		Bytecode_AppendConstReal(Block->Handle, Node->ConstReal);
		ret = _StackPush(Block, Node, SS_DATATYPE_REAL, NULL);
		if(ret < 0)	return -1;
		CHECK_IF_NEEDED(1);
		break;
	
	case NODETYPE_DELETE:
		ret = _StackPush(Block, Node, SS_DATATYPE_NOVALUE, NULL);
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
		type = _StackPop(Block, Node->UniOp.Value, SS_DATATYPE_UNDEF, NULL);
		if(type < 0)	return -1;

		if( SS_GETARRAYDEPTH(type) != 0) {
			AST_RuntimeError(Node, "Unary operation on array is invalid");
			return -1;
		}
		else if( SS_ISTYPEOBJECT(type) ) {
			const char *name;
			int args[] = {type};
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
			ret = BC_CallFunction(Block, Node, NULL, name, 1, args);
			if(ret < 0)	return ret;
		}
		else {
			Bytecode_AppendUniOp(Block->Handle, op);
			i = AST_ExecuteNode_UniOp_GetType(Block->Script, Node->Type, type);
			if( i <= 0 ) {
				AST_RuntimeError(Node, "Invalid unary operation #%i on 0x%x", Node->Type, type);
				return -1;
			}
			ret = _StackPush(Block, Node, type, NULL);
			if(ret < 0)	return -1;
			type = i;
		}
		
		CHECK_IF_NEEDED(1);
		break;

	// Reference Stuff
	case NODETYPE_REFEQUALS:	if(!op)	op = 1;
	case NODETYPE_REFNOTEQUALS:	if(!op)	op = 2;
		op --;
		// Left (because it's the output type)
		ret = AST_ConvertNode(Block, Node->BinOp.Left, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, Node->BinOp.Left, SS_DATATYPE_UNDEF, NULL);
		if(ret < 0)	return -1;
		type = ret;	// Save

		if( SS_GETARRAYDEPTH(type) )
			;	// Array - can be ref-compared
		else if(SS_ISTYPEOBJECT(type))
			;	// Object - OK too
		else if( type == SS_DATATYPE_STRING )
			;	// Strings - yup
		else {
			// Value type - nope.avi
			AST_RuntimeError(Node, "Can't use reference comparisons on value types");
			return -1;
		}

		if( Node->BinOp.Right->Type != NODETYPE_NULL )
		{
			// Right
			ret = AST_ConvertNode(Block, Node->BinOp.Right, 1);
			if(ret)	return ret;
			ret = _StackPop(Block, Node->BinOp.Right, type, NULL);
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
		
		ret = _StackPush(Block, Node, SS_DATATYPE_BOOLEAN, NULL);
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
		ret = _StackPop(Block, Node->BinOp.Left, SS_DATATYPE_UNDEF, NULL);
		if(ret < 0)	return -1;
		type = ret;	// Save

		// Right
		ret = AST_ConvertNode(Block, Node->BinOp.Right, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, Node->BinOp.Right, SS_DATATYPE_UNDEF, NULL);
		if(ret < 0)	return -1;
		
		if( SS_GETARRAYDEPTH(type) != 0 ) {
			AST_RuntimeError(Node, "Binary operation on array is invalid");
			return -1;
		}
		else if( SS_ISTYPEOBJECT(type) ) {
			const char *name_tpl;
			int args[] = {type, ret};
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
			char	*name = SpiderScript_FormatTypeStr1(Block->Script, name_tpl, ret);
			ret = BC_CallFunction(Block, Node, NULL, name, 2, args);
			free(name);
			if(ret < 0)	return ret;
			break ;
		}
		else {
			i = AST_ExecuteNode_BinOp_GetType(Block->Script, Node->Type, type, ret);
			if( i > 0 ) {
				// All good
				type = i;
			}
			else if( i < 0 && Block->Script->Variant->bImplicitCasts ) {
				// Implicit cast
				i = -i;
				BC_CastValue(Block, Node, i, ret);
				_StackPop(Block, Node, i, NULL);
				type = AST_ExecuteNode_BinOp_GetType(Block->Script, Node->Type, type, i);
			}
			else {
				// Bad combo / no implicit
				AST_RuntimeError(Node, "Invalid binary operation (0x%x #%i 0x%x)",
					type, op, ret);
				return -1;
			}
			Bytecode_AppendBinOp(Block->Handle, op);
		}
		_StackPush(Block, Node, type, NULL);
		CHECK_IF_NEEDED(1);
		break;
	
	default:
		AST_RuntimeError(Node, "BUG - SpiderScript AST_ConvertNode Unimplemented %i", Node->Type);
		return -1;
	}

	DEBUGS1("Left NT%i", Node->Type);
	return 0;
}

int BC_ConstructObject(tAST_BlockInfo *Block, tAST_Node *Node, const char *Namespaces[], const char *Name, int NArgs, int ArgTypes[])
{
	tSpiderClass	*nc;
	tScript_Class	*sc;
	 int	type;
	 int	ret;
	void	*unused;
	
	// Look up object
	type = SpiderScript_ResolveObject(Block->Script, Namespaces, Name, &unused);
	if( type == -1 )
	{
		AST_RuntimeError(Node, "Undefined reference to class %s", Name);
		return -1;
	}
	
	if( (sc = SpiderScript_GetClass_Script(Block->Script, type)) )
	{
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
				if( sf->Arguments[i].Type != ArgTypes[i-1] ) {
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
	else if( (nc = SpiderScript_GetClass_Native(Block->Script, type)) )
	{
		tSpiderFunction *nf = nc->Constructor;
		 int	minArgc = 0;
		 int	bVariable = 0;

		if( !nf ) {
			minArgc = 0;
			bVariable = 0;
		}
		else {
			for( minArgc = 0; nf->ArgTypes[minArgc] && nf->ArgTypes[minArgc] != -1; minArgc ++ )
				;
			bVariable = (nf->ArgTypes[minArgc] == -1);
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
			if( nf->ArgTypes[i] != ArgTypes[i] ) {
				// Sad to be chucked
				AST_RuntimeError(Node, "Argument %i of constructor %s should be %i, given %i",
					i, Name, nf->ArgTypes[i], ArgTypes[i]);
				return -1;
			}
		}
	}

	Bytecode_AppendCreateObj(Block->Handle, type, NArgs);
		
	// Push return type
	ret = _StackPush(Block, Node, type, NULL);
	if(ret < 0)	return -1;
	
	return type;
}

/**
 * \brief Inserts a function call
 * \param Block	AST block state
 * \param Node	Node that caused the function call (used for line numbers
 * \param Namespaces	NULL-terminated list of namespaces to search (set to NULL for method call)
 * \param Name	Name of the function to call
 * \param NArgs	Argument count
 * \param ArgTypes	Types of each argument
 * \return Boolean failure
 * \note If Namespaces == NULL, then ArgTypes[0] is used as the object type for the method call
 */
int BC_CallFunction(tAST_BlockInfo *Block, tAST_Node *Node, const char *Namespaces[], const char *Name, int NArgs, int ArgTypes[])
{
	 int	id = 0;
	 int	ret, ret_type;
	tScript_Function *sf = NULL;
	tSpiderFunction  *nf = NULL;

	if( Namespaces == NULL )
	{
		tSpiderClass	*nc;
		tScript_Class	*sc;
	
		if( NArgs < 1 ) {
			AST_RuntimeError(Node, "BUG - BC_CallFunction(Namespaces == NULL, NArgs < 1)");
			return -1;
		}		

		DEBUGS1("Getting method");
		if( (nc = SpiderScript_GetClass_Native(Block->Script, ArgTypes[0])) )
		{
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
		else if( (sc = SpiderScript_GetClass_Script(Block->Script, ArgTypes[0])) )
		{
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
			if( sf->Arguments[i].Type != ArgTypes[i] ) {
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
		
		for( minArgc = 0; nf->ArgTypes[minArgc] != 0 && nf->ArgTypes[minArgc] != -1; minArgc ++ )
			;
		bVariable = (nf->ArgTypes[minArgc] == -1);
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
			if( i == 0 && !Namespaces ) {
				if( nf->ArgTypes[i] != -2 ) {
					// TODO: Should I chuck?
				}
				continue ;
			}
			if( nf->ArgTypes[i] != ArgTypes[i] ) {
				// Sad to be chucked
				AST_RuntimeError(Node, "Argument %i of %s should be %i, given %i",
					i, Name, nf->ArgTypes[i], ArgTypes[i]);
				return -1;
			}
		}
	
		ret_type = nf->ReturnType;
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
	if( ret_type != 0 )
	{
		ret = _StackPush(Block, Node, ret_type, NULL);
		if(ret < 0)	return -1;
		DEBUGS1("Push return type 0x%x", ret_type);
	}
	else
		DEBUGS1("Not pushing for void");
	
	return ret_type;
}

int BC_SaveValue(tAST_BlockInfo *Block, tAST_Node *DestNode)
{
	 int	ret, type, i;
	void	*ident;
	tSpiderClass *nc;
	tScript_Class	*sc;

	switch(DestNode->Type)
	{
	// Variable, simple
	case NODETYPE_VARIABLE:
		ret = BC_Variable_SetValue( Block, DestNode );
		if(ret)	return ret;
		break;
	// Array index
	case NODETYPE_INDEX:
		ret = AST_ConvertNode(Block, DestNode->BinOp.Left, 1);	// Array
		if(ret)	return ret;
		ret = _StackPop(Block, DestNode->BinOp.Left, SS_DATATYPE_UNDEF, NULL);
		if(ret < 0)	return -1;
		if(SS_GETARRAYDEPTH(ret) == 0) {
			AST_RuntimeError(DestNode, "Type mismatch, Expected an array, got %i",
				ret);
			return -2;
		}
		type = SS_DOWNARRAY(ret);
		
		ret = AST_ConvertNode(Block, DestNode->BinOp.Right, 1);	// Offset
		if(ret)	return ret;
		ret = _StackPop(Block, DestNode->BinOp.Right, SS_DATATYPE_INTEGER, NULL);
		if(ret < 0)	return -1;

		ret = _StackPop(Block, DestNode, SS_DATATYPE_UNDEF, NULL);
		if( ret == 0 && SS_ISTYPEOBJECT(type) ) {
			Bytecode_AppendConstNull(Block->Handle, type);
		}
		else if( ret != type ) {
			AST_RuntimeError(DestNode, "Type mismatch when assigning to array (expected 0x%x, got 0x%x)",
				type, ret);
			return -1;
		}
		
		Bytecode_AppendSetIndex( Block->Handle );
		break;
	// Object element
	case NODETYPE_ELEMENT:
		ret = AST_ConvertNode(Block, DestNode->Scope.Element, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, DestNode->Scope.Element, SS_DATATYPE_UNDEF, &ident);
		if(ret < 0)	return -1;

		sc = SpiderScript_GetClass_Script(Block->Script, ret);
		nc = SpiderScript_GetClass_Native(Block->Script, ret);
		
		if(nc) {
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
		else if(sc) {
			tScript_Class_Var *at;
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
			AST_RuntimeError(DestNode, "Setting element of non-class type %i", ret);
			return -2;
		}

		ret = _StackPop(Block, DestNode, type, NULL);
		if( ret == 0 && SS_ISTYPEOBJECT(type) ) {
			Bytecode_AppendConstNull(Block->Handle, type);
		}
		else if( ret != type ) {
			AST_RuntimeError(DestNode, "Type mismatch when assigning to element '%s' (expected 0x%x, got 0x%x)",
				DestNode->Scope.Name, type, ret);
			return -1;
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

int BC_CastValue(tAST_BlockInfo *Block, tAST_Node *Node, int DestType, int SourceType)
{
	 int	ret;
	if( SS_GETARRAYDEPTH(SourceType) && DestType != SS_DATATYPE_BOOLEAN ) {
		AST_RuntimeError(Node, "Invalid cast from array (0x%x)", SourceType);
		return 1;
	}
	else if( SS_ISTYPEOBJECT(SourceType) ) {
		char *name = SpiderScript_FormatTypeStr1(Block->Script, "operator (%s)", DestType);
		int args[] = {SourceType};
		ret = BC_CallFunction(Block, Node, NULL, name, 1, args);
		free(name);
		if(ret < 0)	return ret;
		if( ret != DestType ) {
			AST_RuntimeError(Node, "BUG - Cast from 0x%x to 0x%x does not return correct type, instead 0x%x",
				SourceType, DestType, ret);
			return -1;
		}
	}
	else {
		Bytecode_AppendCast(Block->Handle, DestType);
		ret = _StackPush(Block, Node, DestType, NULL);
		if(ret < 0)	return -1;
	}
	return 0;
}

/**
 * \brief Define a variable
 * \param Block	Current block state
 * \param Type	Type of the variable
 * \param Name	Name of the variable
 * \return Boolean Failure
 */
int BC_Variable_Define(tAST_BlockInfo *Block, tAST_Node *Node, int Type, const char *Name)
{
	tAST_Variable	*var, *prev = NULL;
	
	for( var = Block->FirstVar; var; prev = var, var = var->Next )
	{
		if( strcmp(var->Name, Name) == 0 ) {
			AST_RuntimeError(Node, "Redefinition of variable '%s'", Name);
			return -1;
		}
	}
	
	var = malloc( sizeof(tAST_Variable) + strlen(Name) + 1 );
	var->Next = NULL;
	var->Type = Type;
	strcpy(var->Name, Name);
	
	if(prev)	prev->Next = var;
	else	Block->FirstVar = var;
	
	Bytecode_AppendDefineVar(Block->Handle, Name, Type);
	return 0;
}

tAST_Variable *BC_Variable_Lookup(tAST_BlockInfo *Block, tAST_Node *VarNode, int CreateType)
{
	tAST_Variable	*var = NULL;
	tAST_BlockInfo	*bs;
	
	for( bs = Block; bs; bs = bs->Parent )
	{
		for( var = bs->FirstVar; var; var = var->Next )
		{
			if( strcmp(var->Name, VarNode->Variable.Name) == 0 )
				break;
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
			AST_RuntimeError(VarNode, "Variable '%s' is undefined", VarNode->Variable.Name);
			return NULL;
//		}
	}
		
	#if TRACE_VAR_LOOKUPS
	AST_RuntimeMessage(VarNode, "debug", "Variable lookup of '%s' %p type %i",
		VarNode->Variable.Name, var, var->Type);
	#endif
	
	return var;
}

/**
 * \brief Set the value of a variable
 * \return Boolean Failure
 */
int BC_Variable_SetValue(tAST_BlockInfo *Block, tAST_Node *VarNode)
{
	tAST_Variable	*var;
	
	// TODO: Implicit definition type
	var = BC_Variable_Lookup(Block, VarNode, SS_DATATYPE_UNDEF);
	if(!var)	return -1;

	int type = _StackPop(Block, VarNode, SS_DATATYPE_UNDEF, NULL);
	if( type == 0 && SS_ISTYPEOBJECT(var->Type) ) {
		Bytecode_AppendConstNull(Block->Handle, var->Type);
	}
	else if( type != var->Type ) {
		AST_RuntimeError(VarNode, "Type mismatch when assigning to '%s' (expected 0x%x, got 0x%x)",
			VarNode->Variable.Name, var->Type, type);
		return -1;
	}
	Bytecode_AppendSaveVar(Block->Handle, VarNode->Variable.Name);
	return 0;
}

/**
 * \brief Get the value of a variable
 */
int BC_Variable_GetValue(tAST_BlockInfo *Block, tAST_Node *VarNode)
{
	tAST_Variable	*var;

	var = BC_Variable_Lookup(Block, VarNode, 0);	
	if(!var)	return -1;

	// NOTE: Abuses ->Object as the info pointer	
	_StackPush(Block, VarNode, var->Type, NULL);
	Bytecode_AppendLoadVar(Block->Handle, VarNode->Variable.Name);
	return 0;
}

void BC_Variable_Clear(tAST_BlockInfo *Block)
{
	tAST_Variable	*var;
	for( var = Block->FirstVar; var; )
	{
		tAST_Variable	*tv = var->Next;
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

int _StackPush(tAST_BlockInfo *Block, tAST_Node *Node, int Type, void *Info)
{
	if(Block->StackDepth == MAX_STACK_DEPTH - 1) {
		AST_RuntimeError(Node, "BUG - Stack overflow in AST-Bytecode conversion (node=%i)",
			Node->Type);
		return -1;
	}

	if( Type == SS_DATATYPE_UNDEF ) {
		AST_RuntimeError(Node, "BUG - Pushed SS_DATATYPE_UNDEF (NT%i)", Node->Type);
		return -1;
	}

	#if TRACE_TYPE_STACK
	AST_RuntimeMessage(Node, "_StackPush", "%x - NT%i", Type, Node->Type);
	#endif
	Block->StackDepth ++;
	Block->Stack[ Block->StackDepth ].Type = Type;
	Block->Stack[ Block->StackDepth ].Info = Info;
	return Type;
}

int _StackPop(tAST_BlockInfo *Block, tAST_Node *Node, int WantedType, void **Info)
{
	 int	havetype;
	if(Block->StackDepth == 0) {
		AST_RuntimeError(Node, "BUG - Stack underflow in AST-Bytecode conversion (node=%i)",
			Node->Type);
		return -1;
	}
	havetype = Block->Stack[ Block->StackDepth ].Type;
	#if TRACE_TYPE_STACK
	AST_RuntimeMessage(Node, "_StackPop", "%x(==%x) - NT%i", havetype, WantedType, Node->Type);
	#endif
	if(WantedType != SS_DATATYPE_UNDEF && havetype != SS_DATATYPE_UNDEF)
	{
		if( havetype != WantedType ) {
			AST_RuntimeError(Node, "AST-Bytecode - Type mismatch (wanted %x got %x)",
				WantedType, havetype);
			// TODO: Message?
			return -2;
		}
	}
	if(Info)
		*Info = Block->Stack[Block->StackDepth].Info;
	Block->StackDepth--;
	return havetype;
}

