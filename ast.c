/*
 * Acess2 Init
 * - Script AST Manipulator
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

// === CODE ===
tAST_Script *AST_NewScript(void)
{
	tAST_Script	*ret = malloc( sizeof(tAST_Script) );
	
	ret->Functions = NULL;
	ret->LastFunction = NULL;
	
	return ret;
}

/**
 * \brief Append a function to a script
 */
tAST_Function *AST_AppendFunction(tAST_Script *Script, const char *Name)
{
	tAST_Function	*ret;
	
	ret = malloc( sizeof(tAST_Function) + strlen(Name) + 1 );
	ret->Next = NULL;
	strcpy(ret->Name, Name);
	ret->Code = NULL;
	ret->Arguments = NULL;
	
	if(Script->LastFunction == NULL) {
		Script->Functions = Script->LastFunction = ret;
	}
	else {
		Script->LastFunction->Next = ret;
		Script->LastFunction = ret;
	}
	
	return ret;
}

void AST_AppendFunctionArg(tAST_Function *Function, tAST_Node *Node)
{
	if( !Function->Arguments ) {
		Function->Arguments_Last = Function->Arguments = Node;
	}
	else {
		Function->Arguments_Last->NextSibling = Node;
		Function->Arguments_Last = Node;
	}
}

/**
 * \brief Set the code for a function
 */
void AST_SetFunctionCode(tAST_Function *Function, tAST_Node *Root)
{
	Function->Code = Root;
}

/**
 * \name Node Manipulation
 * \{
 */
/**
 * \brief Get the in-memory size of a node
 */
size_t AST_GetNodeSize(tAST_Node *Node)
{
	size_t	ret;
	tAST_Node	*node;
	
	if(!Node)
		return 0;
	
	ret = sizeof(tAST_Node*) + sizeof(tAST_NodeType)
		+ sizeof(const char *) + sizeof(int);
	
	switch(Node->Type)
	{
	// Block of code
	case NODETYPE_BLOCK:
		ret += sizeof(Node->Block);
		for( node = Node->Block.FirstChild; node; )
		{
			ret += AST_GetNodeSize(node);
			node = node->NextSibling;
		}
		break;
	
	// Function Call
	case NODETYPE_FUNCTIONCALL:
		ret += sizeof(Node->FunctionCall) + strlen(Node->FunctionCall.Name) + 1;
		for( node = Node->FunctionCall.FirstArg; node; )
		{
			ret += AST_GetNodeSize(node);
			node = node->NextSibling;
		}
		break;
	
	// If node
	case NODETYPE_IF:
		ret += sizeof(Node->If);
		ret += AST_GetNodeSize(Node->If.Condition);
		ret += AST_GetNodeSize(Node->If.True);
		ret += AST_GetNodeSize(Node->If.False);
		break;
	
	// Looping Construct (For loop node)
	case NODETYPE_LOOP:
		ret += sizeof(Node->For);
		ret += AST_GetNodeSize(Node->For.Init);
		ret += AST_GetNodeSize(Node->For.Condition);
		ret += AST_GetNodeSize(Node->For.Increment);
		ret += AST_GetNodeSize(Node->For.Code);
		break;
	
	// Asignment
	case NODETYPE_ASSIGN:
		ret += sizeof(Node->Assign);
		ret += AST_GetNodeSize(Node->Assign.Dest);
		ret += AST_GetNodeSize(Node->Assign.Value);
		break;
	
	// Casting
	case NODETYPE_CAST:
		ret += sizeof(Node->Cast);
		ret += AST_GetNodeSize(Node->Cast.Value);
		break;
	
	// Define a variable
	case NODETYPE_DEFVAR:
		ret += sizeof(Node->DefVar) + strlen(Node->DefVar.Name) + 1;
		for( node = Node->DefVar.LevelSizes; node; )
		{
			ret += AST_GetNodeSize(node);
			node = node->NextSibling;
		}
		break;
	
	// Unary Operations
	case NODETYPE_RETURN:
		ret += sizeof(Node->UniOp);
		ret += AST_GetNodeSize(Node->UniOp.Value);
		break;
	
	// Binary Operations
	case NODETYPE_INDEX:
	case NODETYPE_ADD:
	case NODETYPE_SUBTRACT:
	case NODETYPE_MULTIPLY:
	case NODETYPE_DIVIDE:
	case NODETYPE_MODULO:
	case NODETYPE_BITSHIFTLEFT:
	case NODETYPE_BITSHIFTRIGHT:
	case NODETYPE_BITROTATELEFT:
	case NODETYPE_BWAND:	case NODETYPE_LOGICALAND:
	case NODETYPE_BWOR: 	case NODETYPE_LOGICALOR:
	case NODETYPE_BWXOR:	case NODETYPE_LOGICALXOR:
	case NODETYPE_EQUALS:
	case NODETYPE_LESSTHAN:
	case NODETYPE_GREATERTHAN:
		ret += sizeof(Node->BinOp);
		ret += AST_GetNodeSize( Node->BinOp.Left );
		ret += AST_GetNodeSize( Node->BinOp.Right );
		break;
	
	// Node types with no children
	case NODETYPE_NOP:
		break;
	case NODETYPE_VARIABLE:
	case NODETYPE_CONSTANT:
		ret += sizeof(Node->Variable) + strlen(Node->Variable.Name) + 1;
		break;
	case NODETYPE_STRING:
		ret += sizeof(Node->String) + Node->String.Length;
		break;
	case NODETYPE_INTEGER:
		ret += sizeof(Node->Integer);
		break;
	case NODETYPE_REAL:
		ret += sizeof(Node->Real);
		break;
	}
	return ret;
}

#if 0
/**
 * \brief Write a node to a file
 */
void AST_WriteNode(FILE *FP, tAST_Node *Node)
{
	tAST_Node	*node;
	intptr_t	ptr;
	 int	ret;
	
	if(!Node)	return ;
	
	ptr = ftell(FP) + AST_GetNodeSize(Node);
	fwrite(&ptr, sizeof(ptr), 1, FP);
	fwrite(&Node->Type, sizeof(Node->Type), 1, FP);
	ptr = 0;	fwrite(&ptr, sizeof(ptr), 1, FP);	// File
	fwrite(&Node->Line, sizeof(Node->Line), 1, FP);
	
	ret = sizeof(tAST_Node*) + sizeof(tAST_NodeType)
		+ sizeof(const char *) + sizeof(int);
	
	switch(Node->Type)
	{
	// Block of code
	case NODETYPE_BLOCK:
		ret += sizeof(Node->Block);
		for( node = Node->Block.FirstChild; node; )
		{
			ret += AST_GetNodeSize(node);
			node = node->NextSibling;
		}
		break;
	
	// Function Call
	case NODETYPE_FUNCTIONCALL:
		ret += sizeof(Node->FunctionCall) + strlen(Node->FunctionCall.Name) + 1;
		for( node = Node->FunctionCall.FirstArg; node; )
		{
			ret += AST_GetNodeSize(node);
			node = node->NextSibling;
		}
		break;
	
	// If node
	case NODETYPE_IF:
		ret += sizeof(Node->If);
		ret += AST_GetNodeSize(Node->If.Condition);
		ret += AST_GetNodeSize(Node->If.True);
		ret += AST_GetNodeSize(Node->If.False);
		break;
	
	// Looping Construct (For loop node)
	case NODETYPE_LOOP:
		ret += sizeof(Node->For);
		ret += AST_GetNodeSize(Node->For.Init);
		ret += AST_GetNodeSize(Node->For.Condition);
		ret += AST_GetNodeSize(Node->For.Increment);
		ret += AST_GetNodeSize(Node->For.Code);
		break;
	
	// Asignment
	case NODETYPE_ASSIGN:
		ret += sizeof(Node->Assign);
		ret += AST_GetNodeSize(Node->Assign.Dest);
		ret += AST_GetNodeSize(Node->Assign.Value);
		break;
	
	// Casting
	case NODETYPE_CAST:
		ret += sizeof(Node->Cast);
		ret += AST_GetNodeSize(Node->Cast.Value);
		break;
	
	// Define a variable
	case NODETYPE_DEFVAR:
		ret += sizeof(Node->DefVar) + strlen(Node->DefVar.Name) + 1;
		for( node = Node->DefVar.LevelSizes; node; )
		{
			ret += AST_GetNodeSize(node);
			node = node->NextSibling;
		}
		break;
	
	// Unary Operations
	case NODETYPE_RETURN:
		ret += sizeof(Node->UniOp);
		ret += AST_GetNodeSize(Node->UniOp.Value);
		break;
	
	// Binary Operations
	case NODETYPE_INDEX:
	case NODETYPE_ADD:
	case NODETYPE_SUBTRACT:
	case NODETYPE_MULTIPLY:
	case NODETYPE_DIVIDE:
	case NODETYPE_MODULO:
	case NODETYPE_BITSHIFTLEFT:
	case NODETYPE_BITSHIFTRIGHT:
	case NODETYPE_BITROTATELEFT:
	case NODETYPE_BWAND:	case NODETYPE_LOGICALAND:
	case NODETYPE_BWOR: 	case NODETYPE_LOGICALOR:
	case NODETYPE_BWXOR:	case NODETYPE_LOGICALXOR:
	case NODETYPE_EQUALS:
	case NODETYPE_LESSTHAN:
	case NODETYPE_GREATERTHAN:
		ret += sizeof(Node->BinOp);
		ret += AST_GetNodeSize( Node->BinOp.Left );
		ret += AST_GetNodeSize( Node->BinOp.Right );
		break;
	
	// Node types with no children
	case NODETYPE_NOP:
		break;
	case NODETYPE_VARIABLE:
	case NODETYPE_CONSTANT:
		ret += sizeof(Node->Variable) + strlen(Node->Variable.Name) + 1;
		break;
	case NODETYPE_STRING:
		ret += sizeof(Node->String) + Node->String.Length;
		break;
	case NODETYPE_INTEGER:
		ret += sizeof(Node->Integer);
		break;
	case NODETYPE_REAL:
		ret += sizeof(Node->Real);
		break;
	}
	return ret;
}
#endif

/**
 * \brief Free a node and all subnodes
 */
void AST_FreeNode(tAST_Node *Node)
{
	tAST_Node	*node;
	
	if(!Node)	return ;
	
	switch(Node->Type)
	{
	// Block of code
	case NODETYPE_BLOCK:
		for( node = Node->Block.FirstChild; node; )
		{
			tAST_Node	*savedNext = node->NextSibling;
			AST_FreeNode(node);
			node = savedNext;
		}
		break;
	
	// Function Call
	case NODETYPE_FUNCTIONCALL:
		for( node = Node->FunctionCall.FirstArg; node; )
		{
			tAST_Node	*savedNext = node->NextSibling;
			AST_FreeNode(node);
			node = savedNext;
		}
		break;
	
	// If node
	case NODETYPE_IF:
		AST_FreeNode(Node->If.Condition);
		AST_FreeNode(Node->If.True);
		AST_FreeNode(Node->If.False);
		break;
	
	// Looping Construct (For loop node)
	case NODETYPE_LOOP:
		AST_FreeNode(Node->For.Init);
		AST_FreeNode(Node->For.Condition);
		AST_FreeNode(Node->For.Increment);
		AST_FreeNode(Node->For.Code);
		break;
	
	// Asignment
	case NODETYPE_ASSIGN:
		AST_FreeNode(Node->Assign.Dest);
		AST_FreeNode(Node->Assign.Value);
		break;
	
	// Casting
	case NODETYPE_CAST:
		AST_FreeNode(Node->Cast.Value);
		break;
	
	// Define a variable
	case NODETYPE_DEFVAR:
		for( node = Node->DefVar.LevelSizes; node; )
		{
			tAST_Node	*savedNext = node->NextSibling;
			AST_FreeNode(node);
			node = savedNext;
		}
		break;
	
	// Unary Operations
	case NODETYPE_RETURN:
		AST_FreeNode(Node->UniOp.Value);
		break;
	
	// Binary Operations
	case NODETYPE_INDEX:
	case NODETYPE_ADD:
	case NODETYPE_SUBTRACT:
	case NODETYPE_MULTIPLY:
	case NODETYPE_DIVIDE:
	case NODETYPE_MODULO:
	case NODETYPE_BITSHIFTLEFT:
	case NODETYPE_BITSHIFTRIGHT:
	case NODETYPE_BITROTATELEFT:
	case NODETYPE_BWAND:	case NODETYPE_LOGICALAND:
	case NODETYPE_BWOR: 	case NODETYPE_LOGICALOR:
	case NODETYPE_BWXOR:	case NODETYPE_LOGICALXOR:
	case NODETYPE_EQUALS:
	case NODETYPE_LESSTHAN:
	case NODETYPE_GREATERTHAN:
		AST_FreeNode( Node->BinOp.Left );
		AST_FreeNode( Node->BinOp.Right );
		break;
	
	// Node types with no children
	case NODETYPE_NOP:	break;
	case NODETYPE_VARIABLE:	break;
	case NODETYPE_CONSTANT:	break;
	case NODETYPE_STRING:	break;
	case NODETYPE_INTEGER:	break;
	case NODETYPE_REAL:	break;
	}
	free( Node );
}

tAST_Node *AST_NewCodeBlock(void)
{
	tAST_Node	*ret = malloc( sizeof(tAST_Node) );
	
	ret->NextSibling = NULL;
	//ret->Line = Parser->CurLine;
	ret->Type = NODETYPE_BLOCK;
	ret->Block.FirstChild = NULL;
	ret->Block.LastChild = NULL;
	
	return ret;
}

void AST_AppendNode(tAST_Node *Parent, tAST_Node *Child)
{
	Child->NextSibling = NULL;
	switch( Parent->Type )
	{
	case NODETYPE_BLOCK:
		if(Parent->Block.FirstChild == NULL) {
			Parent->Block.FirstChild = Parent->Block.LastChild = Child;
		}
		else {
			Parent->Block.LastChild->NextSibling = Child;
			Parent->Block.LastChild = Child;
		}
		break;
	case NODETYPE_DEFVAR:
		if(Parent->DefVar.LevelSizes == NULL) {
			Parent->DefVar.LevelSizes = Parent->DefVar.LevelSizes_Last = Child;
		}
		else {
			Parent->DefVar.LevelSizes_Last->NextSibling = Child;
			Parent->DefVar.LevelSizes_Last = Child;
		}
		break;
	default:
		fprintf(stderr, "BUG REPORT: AST_AppendNode on an invalid node type (%i)\n", Parent->Type);
		break;
	}
}

tAST_Node *AST_NewIf(tParser *Parser, tAST_Node *Condition, tAST_Node *True, tAST_Node *False)
{
	tAST_Node	*ret = malloc( sizeof(tAST_Node) );
	ret->NextSibling = NULL;
	ret->Line = Parser->CurLine;
	ret->Type = NODETYPE_IF;
	ret->If.Condition = Condition;
	ret->If.True = True;
	ret->If.False = False;
	return ret;
}

tAST_Node *AST_NewLoop(tParser *Parser, tAST_Node *Init, int bPostCheck, tAST_Node *Condition, tAST_Node *Increment, tAST_Node *Code)
{
	tAST_Node	*ret = malloc( sizeof(tAST_Node) );
	ret->NextSibling = NULL;
	ret->Line = Parser->CurLine;
	ret->Type = NODETYPE_LOOP;
	ret->For.Init = Init;
	ret->For.bCheckAfter = !!bPostCheck;
	ret->For.Condition = Condition;
	ret->For.Increment = Increment;
	ret->For.Code = Code;
	return ret;
}

tAST_Node *AST_NewAssign(tParser *Parser, int Operation, tAST_Node *Dest, tAST_Node *Value)
{
	tAST_Node	*ret = malloc( sizeof(tAST_Node) );
	
	ret->NextSibling = NULL;
	ret->Line = Parser->CurLine;
	ret->Type = NODETYPE_ASSIGN;
	ret->Assign.Operation = Operation;
	ret->Assign.Dest = Dest;
	ret->Assign.Value = Value;
	
	return ret;
}

tAST_Node *AST_NewCast(tParser *Parser, int Target, tAST_Node *Value)
{
	tAST_Node	*ret = malloc( sizeof(tAST_Node) );
	
	ret->NextSibling = NULL;
	ret->Line = Parser->CurLine;
	ret->Type = NODETYPE_CAST;
	ret->Cast.DataType = Target;
	ret->Cast.Value = Value;
	
	return ret;
}

tAST_Node *AST_NewBinOp(tParser *Parser, int Operation, tAST_Node *Left, tAST_Node *Right)
{
	tAST_Node	*ret = malloc( sizeof(tAST_Node) );
	
	ret->NextSibling = NULL;
	ret->Line = Parser->CurLine;
	ret->Type = Operation;
	ret->BinOp.Left = Left;
	ret->BinOp.Right = Right;
	
	return ret;
}

/**
 */
tAST_Node *AST_NewUniOp(tParser *Parser, int Operation, tAST_Node *Value)
{
	tAST_Node	*ret = malloc( sizeof(tAST_Node) );
	
	ret->NextSibling = NULL;
	ret->Line = Parser->CurLine;
	ret->Type = Operation;
	ret->UniOp.Value = Value;
	
	return ret;
}

/**
 * \brief Create a new string node
 */
tAST_Node *AST_NewString(tParser *Parser, const char *String, int Length)
{
	tAST_Node	*ret = malloc( sizeof(tAST_Node) + Length + 1 );
	
	ret->NextSibling = NULL;
	ret->Line = Parser->CurLine;
	ret->Type = NODETYPE_STRING;
	ret->String.Length = Length;
	memcpy(ret->String.Data, String, Length);
	ret->String.Data[Length] = '\0';
	
	return ret;
}

/**
 * \brief Create a new integer node
 */
tAST_Node *AST_NewInteger(tParser *Parser, uint64_t Value)
{
	tAST_Node	*ret = malloc( sizeof(tAST_Node) );
	ret->NextSibling = NULL;
	ret->Line = Parser->CurLine;
	ret->Type = NODETYPE_INTEGER;
	ret->Integer = Value;
	return ret;
}

/**
 * \brief Create a new variable reference node
 */
tAST_Node *AST_NewVariable(tParser *Parser, const char *Name)
{
	tAST_Node	*ret = malloc( sizeof(tAST_Node) + strlen(Name) + 1 );
	ret->NextSibling = NULL;
	ret->Line = Parser->CurLine;
	ret->Type = NODETYPE_VARIABLE;
	strcpy(ret->Variable.Name, Name);
	return ret;
}

/**
 * \brief Create a new variable definition node
 */
tAST_Node *AST_NewDefineVar(tParser *Parser, int Type, const char *Name)
{
	tAST_Node	*ret = malloc( sizeof(tAST_Node) + strlen(Name) + 1 );
	ret->NextSibling = NULL;
	ret->Line = Parser->CurLine;
	ret->Type = NODETYPE_DEFVAR;
	ret->DefVar.DataType = Type;
	ret->DefVar.LevelSizes = NULL;
	strcpy(ret->DefVar.Name, Name);
	return ret;
}

/**
 * \brief Create a new runtime constant reference node
 */
tAST_Node *AST_NewConstant(tParser *Parser, const char *Name)
{
	tAST_Node	*ret = malloc( sizeof(tAST_Node) + strlen(Name) + 1 );
	ret->NextSibling = NULL;
	ret->Line = Parser->CurLine;
	ret->Type = NODETYPE_CONSTANT;
	strcpy(ret->Variable.Name, Name);
	return ret;
}

/**
 * \brief Create a function call node
 * \note Argument list is manipulated using AST_AppendFunctionCallArg
 */
tAST_Node *AST_NewFunctionCall(tParser *Parser, const char *Name)
{
	tAST_Node	*ret = malloc( sizeof(tAST_Node) + strlen(Name) + 1 );
	
	ret->NextSibling = NULL;
	ret->Line = Parser->CurLine;
	ret->Type = NODETYPE_FUNCTIONCALL;
	ret->FunctionCall.FirstArg = NULL;
	ret->FunctionCall.LastArg = NULL;
	strcpy(ret->FunctionCall.Name, Name);
	return ret;
}

/**
 * \brief Append an argument to a function call
 */
void AST_AppendFunctionCallArg(tAST_Node *Node, tAST_Node *Arg)
{
	if( Node->Type != NODETYPE_FUNCTIONCALL )	return ;
	
	if(Node->FunctionCall.LastArg) {
		Node->FunctionCall.LastArg->NextSibling = Arg;
		Node->FunctionCall.LastArg = Arg;
	}
	else {
		Node->FunctionCall.FirstArg = Arg;
		Node->FunctionCall.LastArg = Arg;
	}
}

/**
 * \}
 */