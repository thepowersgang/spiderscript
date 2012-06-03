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

#define TRACE_VAR_LOOKUPS	0
#define TRACE_TYPE_STACK	0
#define MAX_NAMESPACE_DEPTH	10
#define MAX_STACK_DEPTH	10	// This is for one function, so shouldn't need more

// === IMPORTS ===
extern tSpiderFunction	*gpExports_First;

// === TYPES ===
typedef struct sAST_BlockInfo
{
	struct sAST_BlockInfo	*Parent;
	void	*Handle;
	tSpiderScript	*Script;
	const char	*Tag;

	 int	BreakTarget;
	 int	ContinueTarget;

	 int	NamespaceDepth;
	const char	*CurNamespaceStack[MAX_NAMESPACE_DEPTH];
	
	 int	StackDepth;
	struct {
		int	Type;
		void	*Info;
	}	Stack[MAX_STACK_DEPTH];	// Stores types of stack values
	
	tAST_Variable	*FirstVar;
} tAST_BlockInfo;

// === PROTOTYPES ===
// Node Traversal
 int	AST_ConvertNode(tAST_BlockInfo *Block, tAST_Node *Node, int bKeepValue);
 int	BC_SaveValue(tAST_BlockInfo *Block, tAST_Node *DestNode);
// Variables
 int 	BC_Variable_Define(tAST_BlockInfo *Block, int Type, const char *Name);
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
	bi.Script = Script;
	
	// Parse arguments
	for( i = 0; i < Fcn->ArgumentCount; i ++ )
	{
		BC_Variable_Define(&bi, Fcn->Arguments[i].Type, Fcn->Arguments[i].Name);
	}

	if( AST_ConvertNode(&bi, Fcn->ASTFcn, 0) )
	{
		AST_RuntimeError(Fcn->ASTFcn, "Error in converting function");
		Bytecode_DeleteFunction(ret);
		BC_Variable_Clear(&bi);
		return NULL;
	}
	BC_Variable_Clear(&bi);


	Bytecode_AppendConstNull(ret);
	Bytecode_AppendReturn(ret);
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
	 int	ret = 0;
	 int	i, op = 0;
	 int	bAddedValue = 1;	// Used to tell if the value needs to be deleted
	void	*ident;	// used for classes
	tScript_Class	*sc;
	tSpiderClass *nc;
	
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
			blockInfo.Handle = Block->Handle;
			// Loop over all nodes, or until the return value is set
			for(node = Node->Block.FirstChild;
				node;
				node = node->NextSibling )
			{
				ret = AST_ConvertNode(&blockInfo, node, 0);
				if(ret)	return ret;
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
		 int	nargs = 0;
		
		ret = AST_ConvertNode(Block, Node->FunctionCall.Object, 1);
		if(ret)	return ret;
		
		ret = _StackPop(Block, Node, SS_DATATYPE_UNDEF, NULL);
		if(ret < 0)	return -1;
		
		nc = SpiderScript_GetClass_Native(Block->Script, ret);
		sc = SpiderScript_GetClass_Script(Block->Script, ret);
		if(!nc && !sc)
			AST_RuntimeError(Node, "Method call on non-object");
			// Sad to be chucked
		
		// Push arguments to the stack
		for(node = Node->FunctionCall.FirstArg; node; node = node->NextSibling)
		{
			nargs ++;
		}
		
		int argtypes[nargs];
		int i = 0;
		for(node = Node->FunctionCall.FirstArg; node; node = node->NextSibling, i++)
		{
			// Convert argument
			ret = AST_ConvertNode(Block, node, 1);
			if(ret)	return ret;
			
			// Pop type off the stack
			argtypes[i] = _StackPop(Block, Node, SS_DATATYPE_UNDEF, NULL);
			if(argtypes[i] < 0)	return -1;
		}

		// Do type checking on arguments
		 int	ret_type = 0;
		if( sc )
		{
			// Script class
			tScript_Function *m;
			for( m = sc->FirstFunction; m; m = m->Next )
			{
				if( strcmp(m->Name, Node->FunctionCall.Name) == 0 ) {
					break;
				}
			}
			if( !m )
				AST_RuntimeError(Node, "Class %s does not have a method %s", sc->Name, Node->FunctionCall.Name);
			ret_type = m->ReturnType;
			// TODO: Typechecking on script class methods
		}
		else
		{
			// Native class
			tSpiderFunction *m;
			for( m = nc->Methods; m; m = m->Next )
			{
				if( strcmp(m->Name, Node->FunctionCall.Name) == 0 ) {
					break;
				}
			}
			if( !m )
				AST_RuntimeError(Node, "Class %s does not have a method %s", nc->Name, Node->FunctionCall.Name);
			ret_type = m->ReturnType;
			// TODO: Typechecking on native class methods
		}
		
		Bytecode_AppendMethodCall(Block->Handle, Node->FunctionCall.Name, nargs);

		ret = _StackPush(Block, Node, ret_type, 0);
		if(ret < 0)	return -1;
	
		CHECK_IF_NEEDED(0);	// Don't warn
		// TODO: Implement warn_unused_ret
		
		} break;
	case NODETYPE_FUNCTIONCALL:
	case NODETYPE_CREATEOBJECT: {
		 int	nargs = 0;
		const char	*namespaces[] = {NULL};	// TODO: Default/imported namespaces
		
		// Get name (mangled into a single string)
		 int	newnamelen = 0;
		char	*manglename;
		for( i = 0; i < Block->NamespaceDepth; i ++ )
			newnamelen += strlen(Block->CurNamespaceStack[i]) + 1;
		newnamelen += strlen(Node->FunctionCall.Name) + 1;
		
		manglename = alloca(newnamelen);
		newnamelen = 0;
		for( i = 0; i < Block->NamespaceDepth; i ++ ) {
			strcpy(manglename+newnamelen, Block->CurNamespaceStack[i]);
			newnamelen += strlen(Block->CurNamespaceStack[i]) + 1;
			manglename[ newnamelen - 1 ] = BC_NS_SEPARATOR;
		}
		strcpy(manglename + newnamelen, Node->FunctionCall.Name);
		newnamelen += strlen(Node->FunctionCall.Name) + 1;
		Block->NamespaceDepth = 0;
	
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
			void	*ident = NULL;
			
			// Look up object
			if( SpiderScript_CreateObject(Block->Script, manglename, namespaces, 0, NULL, &ident, 0) == ERRPTR ) {
				AST_RuntimeError(Node, "Undefined reference to class %s", manglename);
				return -1;
			}
			
			if( (intptr_t)ident & 1 )
			{
				sc = (void*)( (intptr_t)ident & ~1ULL );
				tScript_Function *sf;

				for( sf = sc->FirstFunction; sf; sf = sf->Next )
				{
					if( strcmp(sf->Name, CONSTRUCTOR_NAME) == 0 )
						break; 
				}
			
				if( sf )
				{
					// Argument count check
					if( nargs+1 != sf->ArgumentCount ) {
						AST_RuntimeError(Node, "Constructor for %s takes %i arguments, passed %i",
							manglename, sf->ArgumentCount, nargs);
						return -1;
					}
					// Type checks
					for( int i = 1; i < nargs+1; i ++ )
					{
						if( sf->Arguments[i].Type != arg_types[i-1] ) {
							// Sad to be chucked
							AST_RuntimeError(Node, "Argument %i of %s constructor should be %i, given %i",
								i, manglename, sf->Arguments[i].Type, arg_types[i-1]);
							return -1;
						}
					}
				}
				else
				{
					// No constructor, no arguments
					if( nargs != 0 ) {
						AST_RuntimeError(Node, "Class %s has no constructor, no arguments allowed", manglename);
						return -1;
					}
				}
			}
			else
			{
//				tSpiderClass	*class = ident;
				
				// TODO: Impliment argument types in native constructors
			}
	
			Bytecode_AppendCreateObj(Block->Handle, manglename, nargs);
				
			// Push return type
			ret = _StackPush(Block, Node, SpiderScript_GetTypeCode(Block->Script, manglename), NULL);
			if(ret < 0)	return -1;
		}
		else
		{
			void	*ident = NULL;
			
			// Look up function definition
			if( SpiderScript_ExecuteFunction(Block->Script, manglename, namespaces, 0, NULL, &ident, 0) == ERRPTR ) {
				// Sad will be chucked
				AST_RuntimeError(Node, "Undefined reference to %s", manglename);
				return -1;
			}
		
			// HACK - Uses the internal caching of SpiderScript_ExecuteFunction to tell what type a function is	
			if( (intptr_t)ident & 1 )
			{
				tScript_Function *sf = (void*)( (intptr_t)ident & ~1ULL );
				// Argument count check
				if( nargs != sf->ArgumentCount ) {
					AST_RuntimeError(Node, "%s takes %i arguments, passed %i",
						manglename, sf->ArgumentCount, nargs);
					return -1;
				}
				// Type checks
				for( int i = 0; i < nargs; i ++ )
				{
					if( sf->Arguments[i].Type != arg_types[i] ) {
						// Sad to be chucked
						AST_RuntimeError(Node, "Argument %i of %s should be %i, given %i",
							i, manglename, sf->Arguments[i].Type, arg_types[i]);
						return -1;
					}
				}
				
				// Push return type
				ret = _StackPush(Block, Node, sf->ReturnType, NULL);
				if(ret < 0)	return -1;
			}
			else
			{
				tSpiderFunction *nf = ident;
				 int	minArgc = 0;
				 int	bVariable = 0;
				
				for( minArgc = 0; nf->ArgTypes[minArgc] != 0 && nf->ArgTypes[minArgc] != -1; minArgc ++ )
					;
				bVariable = (nf->ArgTypes[minArgc] == -1);

				// Check argument count
				if( nargs < minArgc || (!bVariable && nargs > minArgc) ) {
					AST_RuntimeError(Node, "%s takes %i%s arguments, passed %i",
						manglename, i, (bVariable?"+":""), nargs);
					return -1;
				}

				// Check argument types (and passing too few arguments)
				for( int i = 0; i < nargs && i < minArgc; i ++ )
				{
					if( nf->ArgTypes[i] != arg_types[i] ) {
						// Sad to be chucked
						AST_RuntimeError(Node, "Argument %i of %s should be %i, given %i",
							i, manglename, nf->ArgTypes[i], arg_types[i]);
						return -1;
					}
				}
				
				// Push return type
				ret = _StackPush(Block, Node, nf->ReturnType, 0);
				if(ret < 0)	return -1;
			}
				
			Bytecode_AppendFunctionCall(Block->Handle, manglename, nargs);
		}		

		CHECK_IF_NEEDED(0);	// Don't warn
		// TODO: Implement warn_unused_ret
		} break;
	
	// Conditional
	case NODETYPE_IF: {
		 int	if_end;
		ret = AST_ConvertNode(Block, Node->If.Condition, 1);
		if(ret)	return ret;
		// TODO: Should be boolean/integer, but meh
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
			ret = _StackPop(Block, Node->For.Condition, SS_DATATYPE_UNDEF, NULL);	// Boolean?
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
			ret = _StackPop(Block, Node->If.Condition, SS_DATATYPE_UNDEF, NULL);	// Boolean?
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
		// TODO: Get function return type and check with stack
		ret = _StackPop(Block, Node->UniOp.Value, SS_DATATYPE_UNDEF, NULL);
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
		// TODO: Check if BreakTarget/ContinueTarget are valid
		if( Node->Type == NODETYPE_BREAK )
			Bytecode_AppendJump(Block->Handle, bi->BreakTarget);
		else
			Bytecode_AppendJump(Block->Handle, bi->ContinueTarget);
		} break;
	
	// Define a variable
	case NODETYPE_DEFVAR:
		ret = BC_Variable_Define(Block, Node->DefVar.DataType, Node->DefVar.Name);
		if(ret)	return ret;
		
		if( Node->DefVar.InitialValue )
		{
			ret = AST_ConvertNode(Block, Node->DefVar.InitialValue, 1);
			if(ret)	return ret;
			// TODO: Why is the pop here?
			ret = _StackPop(Block, Node->DefVar.InitialValue, Node->DefVar.DataType, NULL);
			if(ret < 0)	return -1;
			Bytecode_AppendSaveVar(Block->Handle, Node->DefVar.Name);
		}
		break;
	
	// Scope
	case NODETYPE_SCOPE:
		if( Block->NamespaceDepth == MAX_NAMESPACE_DEPTH ) {
			AST_RuntimeError(Node, "Exceeded max explicit namespace depth (%i)", MAX_NAMESPACE_DEPTH);
			return 2;
		}
		Block->CurNamespaceStack[ Block->NamespaceDepth ] = Node->Scope.Name;
		Block->NamespaceDepth ++;
		ret = AST_ConvertNode(Block, Node->Scope.Element, bKeepValue);
		if(ret)	return ret;
		if( Block->NamespaceDepth != 0 ) {
			AST_RuntimeError(Node, "Namespace scope used but no element at the end");
		}
		bAddedValue = 0;
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

		// TODO: Check if the type is castable (if object, if it has a cast operator)	
	
		Bytecode_AppendCast(Block->Handle, Node->Cast.DataType);
		ret = _StackPush(Block, Node, Node->Cast.DataType, NULL);
		if(ret < 0)	return -1;
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
		
		// TODO: Support indexing on objects
		if(SS_GETARRAYDEPTH(ret) == 0) {
			AST_RuntimeError(Node, "Type mismatch, Expected an array, got %i", ret);
			return -2;
		}
		i = ret;	// Hackily save the datatype

		// - Offset
		ret = AST_ConvertNode(Block, Node->BinOp.Right, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, Node, SS_DATATYPE_INTEGER, NULL);
		if(ret < 0)	return -1;
		
		Bytecode_AppendIndex(Block->Handle);
		
		// Update the array depth
		i = SS_DOWNARRAY(i);	// Decrease the array level
		ret = _StackPush(Block, Node, i, NULL);
		if(ret < 0)	return -1;
		
		CHECK_IF_NEEDED(1);
		break;

	// TODO: Implement runtime constants
	case NODETYPE_CONSTANT:
		// TODO: Scan namespace for constant name
		AST_RuntimeError(Node, "TODO - Runtime Constants");
		Block->NamespaceDepth = 0;
		return -1;
	
	// Constant Values
	case NODETYPE_STRING:
		Bytecode_AppendConstString(Block->Handle, Node->Constant.String.Data, Node->Constant.String.Length);
		ret = _StackPush(Block, Node, SS_DATATYPE_STRING, NULL);
		if(ret < 0)	return -1;
		CHECK_IF_NEEDED(1);
		break;
	case NODETYPE_INTEGER:
		Bytecode_AppendConstInt(Block->Handle, Node->Constant.Integer);
		ret = _StackPush(Block, Node, SS_DATATYPE_INTEGER, NULL);
		if(ret < 0)	return -1;
		CHECK_IF_NEEDED(1);
		break;
	case NODETYPE_REAL:
		Bytecode_AppendConstReal(Block->Handle, Node->Constant.Real);
		ret = _StackPush(Block, Node, SS_DATATYPE_REAL, NULL);
		if(ret < 0)	return -1;
		CHECK_IF_NEEDED(1);
		break;
	case NODETYPE_NULL:
		Bytecode_AppendConstNull(Block->Handle);
		ret = _StackPush(Block, Node, SS_DATATYPE_UNDEF, NULL);
		if(ret < 0)	return -1;
		CHECK_IF_NEEDED(1);
		break;

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
		ret = _StackPop(Block, Node->UniOp.Value, SS_DATATYPE_UNDEF, NULL);	// TODO: Integer/Real/Undef
		if(ret < 0)	return -1;

		Bytecode_AppendUniOp(Block->Handle, op);
		ret = _StackPush(Block, Node, ret, NULL);	// TODO: Logic = _INTEGER, Neg = No change
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
		i = ret;	// Save

		// Right
		ret = AST_ConvertNode(Block, Node->BinOp.Right, 1);
		if(ret)	return ret;
		ret = _StackPop(Block, Node->BinOp.Right, SS_DATATYPE_UNDEF, NULL);
		if(ret < 0)	return -1;
		
		// TODO: Check if the types can be added

		Bytecode_AppendBinOp(Block->Handle, op);
		_StackPush(Block, Node, i, NULL);
		CHECK_IF_NEEDED(1);
		break;
	
	default:
		AST_RuntimeError(Node, "BUG - SpiderScript AST_ConvertNode Unimplemented %i", Node->Type);
		return -1;
	}

	return 0;
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
		
		Bytecode_AppendSetIndex( Block->Handle );
		_StackPop(Block, DestNode, type, NULL);
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
				AST_RuntimeError(DestNode, "Class %s does not have an attribute %s", nc->Name, DestNode->Scope.Name);
				return -2;
			}
			ret = nc->AttributeDefs[i].Type;
		}
		else if(sc) {
			tScript_Class_Var *at;
			for( at = sc->FirstProperty; at; at = at->Next )
			{
				if( strcmp(DestNode->Scope.Name, at->Name) == 0 )
					break;
			}
			if( !at ) {
				AST_RuntimeError(DestNode, "Class %s does not have an attribute %s", sc->Name, DestNode->Scope.Name);
				return -2;
			}
			ret = at->Type;
		}
		else {
			AST_RuntimeError(DestNode, "Setting element of non-class type %i", ret);
			return -2;
		}

		_StackPop(Block, DestNode, ret, NULL);
		
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

/**
 * \brief Define a variable
 * \param Block	Current block state
 * \param Type	Type of the variable
 * \param Name	Name of the variable
 * \return Boolean Failure
 */
int BC_Variable_Define(tAST_BlockInfo *Block, int Type, const char *Name)
{
	tAST_Variable	*var, *prev = NULL;
	
	for( var = Block->FirstVar; var; prev = var, var = var->Next )
	{
		if( strcmp(var->Name, Name) == 0 ) {
			AST_RuntimeError(NULL, "Redefinition of variable '%s'", Name);
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

	// TODO: Check types

	_StackPop(Block, VarNode, var->Type, (void**)&var->Object);
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
	_StackPush(Block, VarNode, var->Type, var->Object);
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
	AST_RuntimeMessage(Node, "_StackPop", "%x(?==%x) - NT%i", havetype, WantedType, Node->Type);
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

