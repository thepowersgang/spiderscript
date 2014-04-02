/*
 * SpiderScript Library
 * by John Hodge (thePowersGang)
 * 
 * bytecode_gen.c
 * - Generate bytecode
 */
#include <stdlib.h>
#include "ast.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

#define MAX_ADD_CHAIN	32

#if 0
#define LOG(str, v...)	fprintf(stderr, "%s:%i - "str"\n", __FILE__, __LINE__, ##v)
#else
#define LOG(...)	do{}while(0);
#endif

#define _OPT(var)	(var = AST_Optimise(var))

// === CODE ===
tAST_Node *AST_Optimise_MakeString(tAST_Node **ents, int len, int First, int Cur)
{
	tAST_Node *first = ents[First];
	tParser	state = {.Cur={.Line = first->Line}, .Filename = (char*)first->File};
	tAST_Node *ns = AST_NewString(&state, NULL, len);
	len = 0;
	for( int j = First; j < Cur; j++ )
	{
		memcpy(ns->ConstString->Data + len,
			ents[j]->ConstString->Data,
			ents[j]->ConstString->Length
			);
		len += ents[j]->ConstString->Length;
		AST_FreeNode(ents[j]);
	}
	return ns;
}

tAST_Node *AST_Optimise_MakeString2(tAST_Node *Left, tAST_Node *Right)
{
	tParser	state = {.Cur={.Line = Left->Line}, .Filename = (char*)Left->File};
	assert(Left->Type  == NODETYPE_STRING);
	assert(Right->Type == NODETYPE_STRING);
	size_t	len = Left->ConstString->Length + Right->ConstString->Length;
	
	tAST_Node *ns = AST_NewString(&state, NULL, len);
	memcpy(ns->ConstString->Data + 0,
		Left->ConstString->Data, Left->ConstString->Length);
	memcpy(ns->ConstString->Data + Left->ConstString->Length,
		Right->ConstString->Data, Right->ConstString->Length);
	AST_FreeNode(Left);
	AST_FreeNode(Right);
	return ns;
}

tAST_Node *AST_Optimise_OptList(tAST_Node *First)
{
	tAST_Node	*ret = First;
	tAST_Node	**np = &ret;
	for( tAST_Node *node = First; node; np = &node->NextSibling, node = node->NextSibling)
	{
		tAST_Node	*next = node->NextSibling;
		tAST_Node	*newnode = AST_Optimise(node);
		if( newnode != node ) {
			LOG("Replacing %p with %p", node, newnode);
			*np = node = newnode;
			newnode->NextSibling = next;
		}
	}
	return ret;
}

tAST_Node *AST_Optimise_DoMaths(tAST_Node *Node, tAST_Node *L, tAST_Node *R)
{
	switch(L->Type)
	{
	case NODETYPE_INTEGER:
		switch(Node->Type)
		{
		case NODETYPE_ADD:
			if(R->Type == NODETYPE_INTEGER) {
				LOG("Optimised %li+%li", L->ConstInt, R->ConstInt);
				L->ConstInt += R->ConstInt;
				return L;
			}
			break;
		case NODETYPE_SUBTRACT:
			if(R->Type == NODETYPE_INTEGER) {
				LOG("Optimised %li-%li", L->ConstInt, R->ConstInt);
				L->ConstInt -= R->ConstInt;
				return L;
			}
			break;
		case NODETYPE_DIVIDE:
			if(R->Type == NODETYPE_INTEGER) {
				LOG("Optimised %li/%li", L->ConstInt, R->ConstInt);
				L->ConstInt /= R->ConstInt;
				return L;
			}
			break;
		default:
			break;
		}
		break;
	
	case NODETYPE_REAL:
		switch(Node->Type)
		{
		case NODETYPE_ADD:
			if(R->Type == NODETYPE_REAL) {
				LOG("Optimised %lf+%lf", L->ConstReal, R->ConstReal);
				L->ConstReal += R->ConstReal;
				return L;
			}
			break;
		case NODETYPE_SUBTRACT:
			if(R->Type == NODETYPE_REAL) {
				LOG("Optimised %lf-%lf", L->ConstReal, R->ConstReal);
				L->ConstReal -= R->ConstReal;
				return L;
			}
			break;
		case NODETYPE_DIVIDE:
			if(R->Type == NODETYPE_REAL) {
				LOG("Optimised %lf/%lf", L->ConstReal, R->ConstReal);
				L->ConstReal /= R->ConstReal;
				return L;
			}
			break;
		default:
			break;
		}
	default:
		break;
	}
	return NULL;
}

tAST_Node *AST_Optimise(tAST_Node *const Node)
{
	tAST_Node	*l;
	tAST_Node	*r;
	tAST_Node	*tmp;

	if( !Node )
		return NULL;	

	//LOG("Node=%p(%i)", Node, Node->Type);
	switch(Node->Type)
	{
	case NODETYPE_BLOCK:
		l = Node->Block.FirstChild = AST_Optimise_OptList(Node->Block.FirstChild);
		// Reduce single-operation blocks
		if( l && l->NextSibling == NULL )
		{
			LOG("Optimised single item block");
			Node->Block.FirstChild = NULL;
			AST_FreeNode(Node);
			return l;
		}
		break;

	case NODETYPE_ASSIGN:
		_OPT(Node->Assign.Dest);
		_OPT(Node->Assign.Value);
		Node->DataType = Node->Assign.Dest->DataType;
		break;
	
	case NODETYPE_FUNCTIONCALL:
	case NODETYPE_METHODCALL:
		Node->FunctionCall.FirstArg = AST_Optimise_OptList( Node->FunctionCall.FirstArg );
		if( Node->Type == NODETYPE_METHODCALL )
			_OPT(Node->FunctionCall.Object);
		// TODO: Resolve function and get datatype
		break;
	case NODETYPE_CREATEOBJECT:
		Node->FunctionCall.FirstArg = AST_Optimise_OptList( Node->FunctionCall.FirstArg );
		break;

	case NODETYPE_CREATEARRAY:
	case NODETYPE_CAST:
		_OPT(Node->Cast.Value);
		Node->DataType = Node->Cast.DataType;
		break;
	
	// If/Ternary node
	case NODETYPE_IF:
		//Node->DataType = SS_DATATYPE_NOVALUE;
		_OPT(Node->If.Condition);
		_OPT(Node->If.True);
		_OPT(Node->If.False);
		break;
	case NODETYPE_TERNARY:
		_OPT(Node->If.Condition);
		_OPT(Node->If.True);
		_OPT(Node->If.False);
		l = (Node->If.True ? Node->If.True : Node->If.Condition);
		r = Node->If.False;
		if( !SS_TYPESEQUAL(l->DataType, r->DataType) ) {
			// Type mismatch?
		}
		Node->DataType = Node->If.False->DataType;
		break;
	
	// Looping Construct (For loop node)
	case NODETYPE_LOOP:
		_OPT(Node->For.Init);
		_OPT(Node->For.Condition);
		_OPT(Node->For.Increment);
		_OPT(Node->For.Code);
		//Node->DataType = SS_DATATYPE_NOVALUE;
		break;
	case NODETYPE_ITERATE:
		_OPT(Node->Iterator.Code);
		_OPT(Node->Iterator.Value);
		break;
	
	case NODETYPE_SWITCH:
		_OPT(Node->BinOp.Left);
		Node->BinOp.Right = AST_Optimise_OptList( Node->BinOp.Right );
		//Node->DataType = SS_DATATYPE_NOVALUE;
		break;
	case NODETYPE_CASE:
		_OPT(Node->BinOp.Left);
		_OPT(Node->BinOp.Right);
		//Node->DataType = SS_DATATYPE_NOVALUE;
		break;
	
	case NODETYPE_ELEMENT:
		_OPT(Node->Scope.Element);
		// TODO: Resolve object to get type of element
		break;
	
	// Define a variable
	case NODETYPE_DEFVAR:
	case NODETYPE_DEFGLOBAL:
		_OPT(Node->DefVar.InitialValue);
		break;
	
	// Unary Operations
	case NODETYPE_RETURN:
	case NODETYPE_POSTINC:
	case NODETYPE_POSTDEC:
	case NODETYPE_DELETE:
		_OPT(Node->UniOp.Value);
		break;
	case NODETYPE_BWNOT:
		l = _OPT(Node->UniOp.Value);
		switch( l->Type )
		{
		case NODETYPE_INTEGER:	l->ConstInt = ~l->ConstInt;	break;
		default:	break;
		}
		break;
	case NODETYPE_LOGICALNOT:
		l = _OPT(Node->UniOp.Value);
		switch( l->Type )
		{
		case NODETYPE_BOOLEAN:
			l->ConstBoolean = !l->ConstBoolean;
			break;
		case NODETYPE_INTEGER:
			l->Type = NODETYPE_BOOLEAN;
			l->ConstBoolean = (l->ConstInt != 0);
			break;
		default:	break;
		}
		break;
	case NODETYPE_NEGATE:
		l = _OPT(Node->UniOp.Value);
		switch( l->Type )
		{
		case NODETYPE_INTEGER:	l->ConstInt  = -l->ConstInt;	break;
		case NODETYPE_REAL:	l->ConstReal = -l->ConstReal;	break;
		default:	break;
		}
		break;
	
	case NODETYPE_INDEX:
	case NODETYPE_REFEQUALS:
	case NODETYPE_REFNOTEQUALS:
		_OPT( Node->BinOp.Left );
		_OPT( Node->BinOp.Right );
		break;
	
	case NODETYPE_ADD:
		l = _OPT(Node->BinOp.Left);
		r = _OPT(Node->BinOp.Right);

		// TODO: If implicit casting is enabled, convert string + ???
		// into Lang.Strings.Concat(string, ???)
		// TODO: Extend concept of sequenced additions to objects
		#if 0
		if( l->DataType == SS_DATATYPE_STRING )
		{
			const char *const mkstr_name = "Lang.Strings.Concat";
			if( r->Type == NODETYPE_FUNCTIONCALL && strcmp(l->FunctionCall.Name, mkstr_name) == 0 )
			{
				// Copy arguments
			}
			else
			{
				// Single argument
			}
			if( l->Type == NODETYPE_FUNCTIONCALL && strcmp(l->FunctionCall.Name, mkstr_name) == 0 )
			{
				// Add an arguments to the end
			}
			else
			{
				// Create a new one
			}
		}
		#endif
		
		// String merging
		if(l->Type == r->Type && l->Type == NODETYPE_STRING)
		{
			LOG("Optimised '%.*s' + '%.*s'",
				(int)l->ConstString->Length, l->ConstString->Data,
				(int)r->ConstString->Length, r->ConstString->Data
				);
			l = AST_Optimise_MakeString2(l, r);
			Node->BinOp.Left = NULL;
			Node->BinOp.Right = NULL;
			AST_FreeNode(Node);
			return l;
		}
		
		// Maths
		if( (tmp = AST_Optimise_DoMaths(Node, l, r)) ) {
			Node->BinOp.Left = NULL;
			AST_FreeNode(Node);
			return tmp;
		}
		break;
	case NODETYPE_SUBTRACT:
	case NODETYPE_MULTIPLY:
	case NODETYPE_DIVIDE:
	case NODETYPE_MODULO:
	case NODETYPE_BITSHIFTLEFT:
	case NODETYPE_BITSHIFTRIGHT:
		l = _OPT(Node->BinOp.Left);
		r = _OPT(Node->BinOp.Right);
		
		if( (tmp = AST_Optimise_DoMaths(Node, l, r)) ) {
			Node->BinOp.Left = NULL;
			AST_FreeNode(Node);
			return tmp;
		}
		break;
	
	case NODETYPE_BITROTATELEFT:
	case NODETYPE_BWAND:	case NODETYPE_LOGICALAND:
	case NODETYPE_BWOR: 	case NODETYPE_LOGICALOR:
	case NODETYPE_BWXOR:	case NODETYPE_LOGICALXOR:
		l = _OPT(Node->BinOp.Left);
		r = _OPT(Node->BinOp.Right);
		break;
	case NODETYPE_EQUALS:	case NODETYPE_NOTEQUALS:
	case NODETYPE_GREATERTHAN:	case NODETYPE_GREATERTHANEQUAL:
	case NODETYPE_LESSTHAN:	case NODETYPE_LESSTHANEQUAL:
		l = _OPT(Node->BinOp.Left);
		r = _OPT(Node->BinOp.Right);
		break;
	
	case NODETYPE_VARIABLE:
		// TODO: Determine if variable's value is constantly known at this point, and replace
		break;
	
	// Node types that don't optimise (leaf nodes)
	case NODETYPE_NOP:
	case NODETYPE_BREAK:
	case NODETYPE_CONTINUE:
		//Node->DataType = SS_DATATYPE_NOVALUE;
		break;
	case NODETYPE_CONSTANT:
		//Node->DataType = SS_DATATYPE_VOID;
		break;
	case NODETYPE_STRING:
		//Node->DataType = SS_DATATYPE_STRING;
		break;
	case NODETYPE_INTEGER:
		//Node->DataType = SS_DATATYPE_INTEGER;
		break;
	case NODETYPE_REAL:
		//Node->DataType = SS_DATATYPE_REAL;
	case NODETYPE_BOOLEAN:
		//Node->DataType = SS_DATATYPE_BOOLEAN;
		break;
	case NODETYPE_NULL:
		break;
	}
	return Node;
}
