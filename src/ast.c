/*
 * Acess2 Init
 * - Script AST Manipulator
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "ast.h"

// === IMPORTS ===
extern void	SyntaxError(tParser *Parser, int bFatal, const char *Message, ...);

// === CODE ===
/**
 * \brief Append a function to a script
 */
int AST_AppendFunction(tSpiderScript *Script, const char *Name, int ReturnType, tAST_Node *Args, tAST_Node *Code)
{
	tScript_Function	*fcn;
	 int	arg_count = 0, arg_bytes = 0;
	tAST_Node	*arg;

	// Count and size arguments
	for(arg = Args; arg; arg = arg->NextSibling)
	{
		arg_count ++;
		arg_bytes += sizeof(fcn->Arguments[0]) + strlen(Args->DefVar.Name) + 1;
	}

	// Allocate information
	fcn = malloc( sizeof(tScript_Function) + arg_bytes + strlen(Name) + 1 );
	if(!fcn)	return -1;
	fcn->Next = NULL;
	fcn->Name = (char*)&fcn->Arguments[arg_count];
	strcpy(fcn->Name, Name);
	fcn->ReturnType = ReturnType;
	fcn->ArgumentCount = arg_count;
	fcn->ASTFcn = Code;
	fcn->BCFcn = NULL;
	
	// Set arguments
	arg_bytes = strlen(Name) + 1;	// Used as an offset into fcn->Name
	arg_count = 0;
	for(arg = Args; arg; arg = arg->NextSibling)
	{
		fcn->Arguments[arg_count].Name = fcn->Name + arg_bytes;
		strcpy(fcn->Arguments[arg_count].Name, arg->DefVar.Name);
		fcn->Arguments[arg_count].Type = arg->DefVar.DataType;
		arg_bytes += strlen(arg->DefVar.Name) + 1;
		arg_count ++;
	}
	
	if(Script->LastFunction == NULL) {
		Script->Functions = Script->LastFunction = fcn;
	}
	else {
		Script->LastFunction->Next = fcn;
		Script->LastFunction = fcn;
	}
	
	return 0;
}

/**
 * \name Node Manipulation
 * \{
 */
#define WRITE_N(_buffer, _offset, _len, _dataptr) do { \
	if(_buffer)	memcpy((char*)_buffer + _offset, _dataptr, _len);\
	_offset += _len; \
} while(0)

#define WRITE_8(_buffer, _offset, _val) do {\
	uint8_t	v = (_val);\
	WRITE_N(_buffer, _offset, 1, &v);\
} while(0)
#define WRITE_16(_buffer, _offset, _val) do {\
	uint16_t	v = (_val);\
	WRITE_N(_buffer, _offset, 2, &v);\
} while(0)
#define WRITE_32(_buffer, _offset, _val) do {\
	uint32_t	v = (_val);\
	WRITE_N(_buffer, _offset, 4, &v);\
} while(0)
#define WRITE_64(_buffer, _offset, _val) do {\
	uint64_t	v = (_val);\
	WRITE_N(_buffer, _offset, 8, &v);\
} while(0)
#define WRITE_REAL(_buffer, _offset, _val) do {\
	double	v = (_val);\
	WRITE_N(_buffer, _offset, sizeof(double), &v);\
} while(0)

#define WRITE_STR(_buffer, _offset, _string) do {\
	int len = strlen(_string);\
	WRITE_16(_buffer, _offset, len);\
	WRITE_N(_buffer, _offset, len, _string);\
	if((_offset & 1) == 1)WRITE_8(_buffer, _offset, 0); \
	if((_offset & 3) == 2)WRITE_16(_buffer, _offset, 0); \
} while(0)
#define WRITE_NODELIST(_buffer, _offset, _listHead)	do {\
	tAST_Node *node; \
	size_t ptr = -1;\
	for(node=(_listHead); node; node = node->NextSibling) {\
		ptr = _offset;\
		_offset += AST_WriteNode(_buffer, _offset, node); \
		WRITE_32(_buffer, ptr, ptr); \
	} \
	if(ptr != -1){ptr -= 4; WRITE_32(_buffer, ptr, 0);} \
} while(0)

/**
 * \brief Writes a script dump to a buffer
 * \return Size of encoded data
 * \note If \a Buffer is NULL, no write is done, but the size is still returned
 */
size_t AST_WriteScript(void *Buffer, tSpiderScript *Script)
{
	tScript_Function	*fcn;
	size_t	ret = 0, ptr = 0;
	 int	i;
	
	for( fcn = Script->Functions; fcn; fcn = fcn->Next )
	{
//		printf("fcn = %p, fcn->Name = %p\n", fcn, fcn->Name);
		ptr = ret;
		WRITE_32(Buffer, ret, 0);	// Next
		WRITE_STR(Buffer, ret, fcn->Name);
		WRITE_32(Buffer, ret, fcn->ArgumentCount);
		for( i = 0; i < fcn->ArgumentCount; i ++ )
		{
			WRITE_16(Buffer, ret, fcn->Arguments[i].Type);
			WRITE_STR(Buffer, ret, fcn->Arguments[i].Name);
		}
		ret += AST_WriteNode(Buffer, ret, fcn->ASTFcn);
		WRITE_32(Buffer, ptr, ret);	// Actually set `Next`
	}
	if( ptr )
	{
		ptr -= 4;
		WRITE_32(Buffer, ptr, 0);	// Clear next for final
	}
	
	return ret;
}

/**
 * \brief Write a node to a file
 */
size_t AST_WriteNode(void *Buffer, size_t Offset, tAST_Node *Node)
{
	size_t	baseOfs = Offset;
	
	if(!Node) {
		//fprintf(stderr, "Possible Bug - NULL passed to AST_WriteNode\n");
		WRITE_32(Buffer, Offset, 0);
		WRITE_16(Buffer, Offset, NODETYPE_NOP);
		WRITE_16(Buffer, Offset, 0);	// Line (0)
		return 0;
	}
	
	WRITE_32(Buffer, Offset, 0);	// Next
	WRITE_16(Buffer, Offset, Node->Type);
	// TODO: Scan the buffer for the location of the filename (with NULL byte)
	//       else, write the string at the end of the node
	WRITE_16(Buffer, Offset, Node->Line);	// Line
	//WRITE_32(Buffer, Offset, 0);	// File
	
	switch(Node->Type)
	{
	// Block of code
	case NODETYPE_BLOCK:
		WRITE_NODELIST(Buffer, Offset, Node->Block.FirstChild);
		break;
	
	// Function Call
	case NODETYPE_METHODCALL:
		Offset += AST_WriteNode(Buffer, Offset, Node->FunctionCall.Object);
	case NODETYPE_FUNCTIONCALL:
	case NODETYPE_CREATEOBJECT:
		// TODO: Search for the same function name and add a pointer
		WRITE_STR(Buffer, Offset, Node->FunctionCall.Name);
		WRITE_NODELIST(Buffer, Offset, Node->FunctionCall.FirstArg);
		break;
	
	// If node
	case NODETYPE_IF:
		Offset += AST_WriteNode(Buffer, Offset, Node->If.Condition);
		Offset += AST_WriteNode(Buffer, Offset, Node->If.True);
		Offset += AST_WriteNode(Buffer, Offset, Node->If.False);
		break;
	
	// Looping Construct (For loop node)
	case NODETYPE_LOOP:
		WRITE_8(Buffer, Offset, Node->For.bCheckAfter);
		WRITE_STR(Buffer, Offset, Node->For.Tag);
		Offset += AST_WriteNode(Buffer, Offset, Node->For.Init);
		Offset += AST_WriteNode(Buffer, Offset, Node->For.Condition);
		Offset += AST_WriteNode(Buffer, Offset, Node->For.Increment);
		Offset += AST_WriteNode(Buffer, Offset, Node->For.Code);
		break;
	
	// Asignment
	case NODETYPE_ASSIGN:
		WRITE_8(Buffer, Offset, Node->Assign.Operation);
		Offset += AST_WriteNode(Buffer, Offset, Node->Assign.Dest);
		Offset += AST_WriteNode(Buffer, Offset, Node->Assign.Value);
		break;
	
	// Casting
	case NODETYPE_CAST:
		WRITE_8(Buffer, Offset, Node->Cast.DataType);
		Offset += AST_WriteNode(Buffer, Offset, Node->Cast.Value);
		break;
	
	// Define a variable
	case NODETYPE_DEFVAR:
		WRITE_32(Buffer, Offset, Node->DefVar.DataType);
		// TODO: Duplicate compress the strings
		WRITE_STR(Buffer, Offset, Node->DefVar.Name);
		Offset += AST_WriteNode(Buffer, Offset, Node->DefVar.InitialValue);
		break;
	
	// Scope Reference
	case NODETYPE_SCOPE:
	case NODETYPE_ELEMENT:
		WRITE_STR(Buffer, Offset, Node->Scope.Name);
		Offset += AST_WriteNode(Buffer, Offset, Node->UniOp.Value);
		break;
	
	// Unary Operations
	case NODETYPE_RETURN:
	case NODETYPE_BWNOT:
	case NODETYPE_LOGICALNOT:
	case NODETYPE_NEGATE:
	case NODETYPE_POSTINC:
	case NODETYPE_POSTDEC:
		Offset += AST_WriteNode(Buffer, Offset, Node->UniOp.Value);
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
	case NODETYPE_EQUALS:	case NODETYPE_NOTEQUALS:
	case NODETYPE_LESSTHAN:	case NODETYPE_LESSTHANEQUAL:
	case NODETYPE_GREATERTHAN:	case NODETYPE_GREATERTHANEQUAL:
		Offset += AST_WriteNode(Buffer, Offset, Node->BinOp.Left);
		Offset += AST_WriteNode(Buffer, Offset, Node->BinOp.Right);
		break;
	
	// Node types with no children
	case NODETYPE_NOP:
		break;
	case NODETYPE_VARIABLE:
	case NODETYPE_CONSTANT:
	case NODETYPE_BREAK:
	case NODETYPE_CONTINUE:
		// TODO: De-Duplicate the strings
		WRITE_STR(Buffer, Offset, Node->Variable.Name);
		break;
	case NODETYPE_STRING:
		WRITE_32(Buffer, Offset, Node->Constant.String.Length);
		WRITE_N(Buffer, Offset, Node->Constant.String.Length, Node->Constant.String.Data);
		break;
	case NODETYPE_INTEGER:
		WRITE_64(Buffer, Offset, Node->Constant.Integer);
		break;
	case NODETYPE_REAL:
		WRITE_REAL(Buffer, Offset, Node->Constant.Real);
		break;
	case NODETYPE_NULL:
		break;
	
	//default:
	//	fprintf(stderr, "AST_WriteNode: Unknown node type %i\n", Node->Type);
	//	break;
	}
	
	return Offset - baseOfs;
}

/**
 * \brief Free a node and all subnodes
 */
void AST_FreeNode(tAST_Node *Node)
{
	tAST_Node	*node;
	
	if(!Node)	return ;
	
	// Referenced counted file name
	(*(int*)(Node->File - sizeof(int))) -= 1;
	if( *(int*)(Node->File - sizeof(int)) == 0 )
		free( (void*)(Node->File - sizeof(int)) );
	
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
	case NODETYPE_METHODCALL:
		AST_FreeNode(Node->FunctionCall.Object);
	case NODETYPE_FUNCTIONCALL:
	case NODETYPE_CREATEOBJECT:
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
	
	case NODETYPE_SCOPE:
	case NODETYPE_ELEMENT:
		AST_FreeNode(Node->Scope.Element);
		break;
	
	// Define a variable
	case NODETYPE_DEFVAR:
		AST_FreeNode(Node->DefVar.InitialValue);
		break;
	
	// Unary Operations
	case NODETYPE_RETURN:
	case NODETYPE_BWNOT:
	case NODETYPE_LOGICALNOT:
	case NODETYPE_NEGATE:
	case NODETYPE_POSTINC:
	case NODETYPE_POSTDEC:
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
	case NODETYPE_EQUALS:	case NODETYPE_NOTEQUALS:
	case NODETYPE_LESSTHAN:	case NODETYPE_LESSTHANEQUAL:
	case NODETYPE_GREATERTHAN:	case NODETYPE_GREATERTHANEQUAL:
		AST_FreeNode( Node->BinOp.Left );
		AST_FreeNode( Node->BinOp.Right );
		break;
	
	// Node types with no children
	case NODETYPE_NOP:	break;
	case NODETYPE_NULL:	break;
	case NODETYPE_VARIABLE:	break;
	case NODETYPE_CONSTANT:	break;
	case NODETYPE_BREAK:
	case NODETYPE_CONTINUE:	break;
	
	case NODETYPE_STRING:
	case NODETYPE_INTEGER:
	case NODETYPE_REAL:
		if( Node->ValueCache )
			SpiderScript_DereferenceValue(Node->ValueCache);
		Node->ValueCache = NULL;
		break;
	}
	free( Node );
}

tAST_Node *AST_int_AllocateNode(tParser *Parser, int Type, int ExtraSize)
{
	tAST_Node	*ret = malloc( sizeof(tAST_Node) + ExtraSize );
	ret->NextSibling = NULL;
	ret->File = Parser->Filename;	*(int*)(Parser->Filename - sizeof(int)) += 1;
	ret->Line = Parser->CurLine;
	ret->Type = Type;
	
	// Runtime Caching
	ret->BlockState = NULL;
	ret->BlockIdent = 0;
	ret->ValueCache = NULL;
	
	return ret;
}

tAST_Node *AST_NewCodeBlock(tParser *Parser)
{
	tAST_Node	*ret = AST_int_AllocateNode(Parser, NODETYPE_BLOCK, 0 );
	
	ret->Block.FirstChild = NULL;
	ret->Block.LastChild = NULL;
	
	return ret;
}

void AST_AppendNode(tAST_Node *Parent, tAST_Node *Child)
{
	// Ignore NULL children
	if( !Child )	return ;
	
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
	default:
		fprintf(stderr, "BUG REPORT: AST_AppendNode on an invalid node type (%i)\n", Parent->Type);
		break;
	}
}

tAST_Node *AST_NewIf(tParser *Parser, tAST_Node *Condition, tAST_Node *True, tAST_Node *False)
{
	tAST_Node	*ret = AST_int_AllocateNode(Parser, NODETYPE_IF, 0);
	ret->If.Condition = Condition;
	ret->If.True = True;
	ret->If.False = False;
	return ret;
}

tAST_Node *AST_NewLoop(tParser *Parser, const char *Tag, tAST_Node *Init, int bPostCheck, tAST_Node *Condition, tAST_Node *Increment, tAST_Node *Code)
{
	tAST_Node	*ret;
	if(!Tag)	Tag = "";
	// NOTE: The +3) & ~3 is to align the size to 4 bytes, and shut valgrind up
	// - GCC sometimes inlines strlen as a loop of dword reads, triggering valgrind
	ret = AST_int_AllocateNode(Parser, NODETYPE_LOOP, (strlen(Tag) + 1 + 3) & ~3);
	ret->For.Init = Init;
	ret->For.bCheckAfter = !!bPostCheck;
	ret->For.Condition = Condition;
	ret->For.Increment = Increment;
	ret->For.Code = Code;
	strcpy(ret->For.Tag, Tag);
	return ret;
}

tAST_Node *AST_NewAssign(tParser *Parser, int Operation, tAST_Node *Dest, tAST_Node *Value)
{
	tAST_Node	*ret = AST_int_AllocateNode(Parser, NODETYPE_ASSIGN, 0);
	
	if( Dest->Type != NODETYPE_VARIABLE && Dest->Type != NODETYPE_ELEMENT && Dest->Type != NODETYPE_INDEX )
	{
		free(ret);
		SyntaxError(Parser, 1, "Assign target is not a variable or attribute (instead %i)",
			Dest->Type);
		AST_FreeNode(Dest);
		AST_FreeNode(Value);
		return NULL;
	}
	
	ret->Assign.Operation = Operation;
	ret->Assign.Dest = Dest;
	ret->Assign.Value = Value;
	
	return ret;
}

tAST_Node *AST_NewCast(tParser *Parser, int Target, tAST_Node *Value)
{
	tAST_Node	*ret = AST_int_AllocateNode(Parser, NODETYPE_CAST, 0);
	
	ret->Cast.DataType = Target;
	ret->Cast.Value = Value;
	
	return ret;
}

tAST_Node *AST_NewBinOp(tParser *Parser, int Operation, tAST_Node *Left, tAST_Node *Right)
{
	tAST_Node	*ret = AST_int_AllocateNode(Parser, Operation, 0);
	
	ret->BinOp.Left = Left;
	ret->BinOp.Right = Right;
	
	return ret;
}

/**
 */
tAST_Node *AST_NewUniOp(tParser *Parser, int Operation, tAST_Node *Value)
{
	tAST_Node	*ret = AST_int_AllocateNode(Parser, Operation, 0);
	
	ret->UniOp.Value = Value;
	
	return ret;
}

tAST_Node *AST_NewBreakout(tParser *Parser, int Type, const char *DestTag)
{
	 int	len = (DestTag ? strlen(DestTag) : 0);
	tAST_Node	*ret = AST_int_AllocateNode(Parser, Type, len + 1);
	
	if( DestTag )
		strcpy(ret->Variable.Name, DestTag);
	else
		ret->Variable.Name[0] = '\0';
	
	return ret;
}

tAST_Node *AST_NewNop(tParser *Parser)
{
	tAST_Node	*ret = AST_int_AllocateNode(Parser, NODETYPE_NOP, 0);
	
	return ret;
}

/**
 * \brief Create a new string node
 */
tAST_Node *AST_NewString(tParser *Parser, const char *String, int Length)
{
	tAST_Node	*ret = AST_int_AllocateNode(Parser, NODETYPE_STRING, Length + 1);
	
	ret->Constant.Type = SS_DATATYPE_STRING;
	ret->Constant.ReferenceCount = 1;
	ret->Constant.String.Length = Length;
	memcpy(ret->Constant.String.Data, String, Length);
	ret->Constant.String.Data[Length] = '\0';
	
	return ret;
}

/**
 * \brief Create a new integer node
 */
tAST_Node *AST_NewInteger(tParser *Parser, int64_t Value)
{
	tAST_Node	*ret = AST_int_AllocateNode(Parser, NODETYPE_INTEGER, 0);
	ret->Constant.Type = SS_DATATYPE_INTEGER;
	ret->Constant.ReferenceCount = 1;
	ret->Constant.Integer = Value;
	return ret;
}

/**
 * \brief Create a new real number node
 */
tAST_Node *AST_NewReal(tParser *Parser, double Value)
{
	tAST_Node	*ret = AST_int_AllocateNode(Parser, NODETYPE_REAL, 0);
	ret->Constant.Type = SS_DATATYPE_REAL;
	ret->Constant.ReferenceCount = 1;
	ret->Constant.Real = Value;
	return ret;
}

/**
 * \brief Return a null value
 */
tAST_Node *AST_NewNull(tParser *Parser)
{
	tAST_Node	*ret = AST_int_AllocateNode(Parser, NODETYPE_NULL, 0);
	
	return ret;
}

/**
 * \brief Create a new variable reference node
 */
tAST_Node *AST_NewVariable(tParser *Parser, const char *Name)
{
	tAST_Node	*ret = AST_int_AllocateNode(Parser, NODETYPE_VARIABLE, strlen(Name) + 1 );
	strcpy(ret->Variable.Name, Name);
	return ret;
}

/**
 * \brief Create a new variable definition node
 */
tAST_Node *AST_NewDefineVar(tParser *Parser, int Type, const char *Name)
{
	tAST_Node	*ret = AST_int_AllocateNode(Parser, NODETYPE_DEFVAR, strlen(Name) + 1 );
	
	ret->DefVar.DataType = Type;
	ret->DefVar.InitialValue = NULL;
	strcpy(ret->DefVar.Name, Name);
	
	return ret;
}

/**
 * \brief Create a new runtime constant reference node
 */
tAST_Node *AST_NewConstant(tParser *Parser, const char *Name)
{
	tAST_Node	*ret = AST_int_AllocateNode(Parser, NODETYPE_CONSTANT, strlen(Name) + 1 );
	
	strcpy(ret->Variable.Name, Name);
	
	return ret;
}

/**
 * \brief Create a function call node
 * \note Argument list is manipulated using AST_AppendFunctionCallArg
 */
tAST_Node *AST_NewFunctionCall(tParser *Parser, const char *Name)
{
	tAST_Node	*ret = AST_int_AllocateNode(Parser, NODETYPE_FUNCTIONCALL, strlen(Name) + 1 );
	
	ret->FunctionCall.Object = NULL;
	ret->FunctionCall.FirstArg = NULL;
	ret->FunctionCall.LastArg = NULL;
	ret->FunctionCall.NumArgs = 0;
	strcpy(ret->FunctionCall.Name, Name);
	
	return ret;
}
tAST_Node *AST_NewMethodCall(tParser *Parser, tAST_Node *Object, const char *Name)
{
	tAST_Node	*ret = AST_int_AllocateNode(Parser, NODETYPE_METHODCALL, strlen(Name) + 1 );
	
	ret->FunctionCall.Object = Object;
	ret->FunctionCall.FirstArg = NULL;
	ret->FunctionCall.LastArg = NULL;
	ret->FunctionCall.NumArgs = 0;
	strcpy(ret->FunctionCall.Name, Name);
	
	return ret;
}

tAST_Node *AST_NewCreateObject(tParser *Parser, const char *Name)
{
	tAST_Node	*ret = AST_int_AllocateNode(Parser, NODETYPE_CREATEOBJECT, strlen(Name) + 1 );
	
	ret->FunctionCall.Object = NULL;
	ret->FunctionCall.FirstArg = NULL;
	ret->FunctionCall.LastArg = NULL;
	ret->FunctionCall.NumArgs = 0;
	strcpy(ret->FunctionCall.Name, Name);
	
	return ret;
}

/**
 * \brief Append an argument to a function call
 */
void AST_AppendFunctionCallArg(tAST_Node *Node, tAST_Node *Arg)
{
	if( Node->Type != NODETYPE_FUNCTIONCALL
	 && Node->Type != NODETYPE_CREATEOBJECT
	 && Node->Type != NODETYPE_METHODCALL)
	{
		fprintf(stderr, "BUG REPORT: AST_AppendFunctionCallArg on an invalid node type (%i)\n", Node->Type);
		return ;
	}
	
	if(Node->FunctionCall.LastArg) {
		Node->FunctionCall.LastArg->NextSibling = Arg;
		Node->FunctionCall.LastArg = Arg;
	}
	else {
		Node->FunctionCall.FirstArg = Arg;
		Node->FunctionCall.LastArg = Arg;
	}
	Node->FunctionCall.NumArgs ++;
}

/**
 * \brief Add a scope node
 */
tAST_Node *AST_NewScopeDereference(tParser *Parser, const char *Name, tAST_Node *Child)
{
	tAST_Node	*ret = AST_int_AllocateNode(Parser, NODETYPE_SCOPE, strlen(Name) + 1 );
	ret->Scope.Element = Child;
	strcpy(ret->Scope.Name, Name);
	return ret;
}

/**
 * \brief Add a scope node
 */
tAST_Node *AST_NewClassElement(tParser *Parser, tAST_Node *Object, const char *Name)
{
	tAST_Node	*ret = AST_int_AllocateNode(Parser, NODETYPE_ELEMENT, strlen(Name) + 1 );
	ret->Scope.Element = Object;
	strcpy(ret->Scope.Name, Name);
	return ret;
}

/**
 * \}
 */