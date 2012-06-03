/*
 * SpiderScript Library
 *
 * AST Execution
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "common.h"
#include "ast.h"

#define USE_AST_EXEC	0
#define TRACE_VAR_LOOKUPS	0
#define TRACE_NODE_RETURNS	0

// === IMPORTS ===

// === PROTOTYPES ===
// - Node Execution
tSpiderValue	*AST_ExecuteNode(tAST_BlockState *Block, tAST_Node *Node);
tSpiderValue	*AST_ExecuteNode_BinOp(tSpiderScript *Script, tAST_Node *Node, int Operation, tSpiderValue *Left, tSpiderValue *Right);
tSpiderValue	*AST_ExecuteNode_UniOp(tSpiderScript *Script, tAST_Node *Node, int Operation, tSpiderValue *Value);
tSpiderValue	*AST_ExecuteNode_Index(tSpiderScript *Script, tAST_Node *Node, tSpiderValue *Array, int Index, tSpiderValue *SaveValue);
// - Errors
void	AST_RuntimeMessage(tAST_Node *Node, const char *Type, const char *Format, ...);
void	AST_RuntimeError(tAST_Node *Node, const char *Format, ...);

// === GLOBALS ===
 int	giNextBlockIdent = 1;

// === CODE ===
tSpiderValue *AST_ExecuteFunction(tSpiderScript *Script, tScript_Function *Fcn, int NArguments, tSpiderValue **Arguments)
{
	return ERRPTR;
}


tSpiderValue *AST_ExecuteNode_UniOp(tSpiderScript *Script, tAST_Node *Node, int Operation, tSpiderValue *Value)
{
	tSpiderValue	*ret;
	#if 0
	if( Value->Type == SS_DATATYPE_OBJECT )
	{
		const char	*fcnname;
		switch(Operation)
		{
		case NODETYPE_NEGATE:	fcnname = "-ve";	break;
		case NODETYPE_BWNOT:	fcnname = "~";	break;
		default:	fcnname = NULL;	break;
		}
		
		if( fcnname )
		{
			ret = Object_ExecuteMethod(Value->Object, fcnname, );
			if( ret != ERRPTR )
				return ret;
		}
	}
	#endif
	switch(Value->Type)
	{
	// Integer Operations
	case SS_DATATYPE_INTEGER:
		if( Value->ReferenceCount == 1 )
			SpiderScript_ReferenceValue(ret = Value);
		else
			ret = SpiderScript_CreateInteger(0);
		switch(Operation)
		{
		case NODETYPE_NEGATE:	ret->Integer = -Value->Integer;	break;
		case NODETYPE_BWNOT:	ret->Integer = ~Value->Integer;	break;
		default:
			AST_RuntimeError(Node, "SpiderScript internal error: Exec,UniOP,Integer unknown op %i", Operation);
			SpiderScript_DereferenceValue(ret);
			ret = ERRPTR;
			break;
		}
		break;
	// Real number Operations
	case SS_DATATYPE_REAL:
		switch(Operation)
		{
		case NODETYPE_NEGATE:	ret = SpiderScript_CreateInteger( -Value->Real );	break;
		default:
			AST_RuntimeError(Node, "SpiderScript internal error: Exec,UniOP,Real unknown op %i", Operation);
			ret = ERRPTR;
			break;
		}
		break;
	
	default:
		AST_RuntimeError(NULL, "Invalid operation (%i) on type (%i)", Operation, Value->Type);
		ret = ERRPTR;
		break;
	}
	
	return ret;
}

tSpiderValue *AST_ExecuteNode_BinOp(tSpiderScript *Script, tAST_Node *Node, int Operation, tSpiderValue *Left, tSpiderValue *Right)
{
	tSpiderValue	*preCastValue = Right;
	tSpiderValue	*ret;
	
	// Convert types
	if( Left && Right && Left->Type != Right->Type )
	{
		#if 0
		// Object types
		// - Operator overload functions
		if( Left->Type == SS_DATATYPE_OBJECT )
		{
			const char	*fcnname;
			switch(Operation)
			{
			case NODETYPE_ADD:	fcnname = "+";	break;
			case NODETYPE_SUBTRACT:	fcnname = "-";	break;
			case NODETYPE_MULTIPLY:	fcnname = "*";	break;
			case NODETYPE_DIVIDE:	fcnname = "/";	break;
			case NODETYPE_MODULO:	fcnname = "%";	break;
			case NODETYPE_BWAND:	fcnname = "&";	break;
			case NODETYPE_BWOR: 	fcnname = "|";	break;
			case NODETYPE_BWXOR:	fcnname = "^";	break;
			case NODETYPE_BITSHIFTLEFT:	fcnname = "<<";	break;
			case NODETYPE_BITSHIFTRIGHT:fcnname = ">>";	break;
			case NODETYPE_BITROTATELEFT:fcnname = "<<<";	break;
			default:	fcnname = NULL;	break;
			}
			
			if( fcnname )
			{
				ret = Object_ExecuteMethod(Left->Object, fcnname, Right);
				if( ret != ERRPTR )
					return ret;
				// Fall through and try casting (which will usually fail)
			}
		}
		#endif
		
		// If implicit casts are allowed, convert Right to Left's type
		if(Script->Variant->bImplicitCasts)
		{
			Right = SpiderScript_CastValueTo(Left->Type, Right);
			if(Right == ERRPTR)
				return ERRPTR;
		}
		// If statically typed, this should never happen, but catch it anyway
		else {
			AST_RuntimeError(Node, "Implicit cast not allowed (from %i to %i)", Right->Type, Left->Type);
			return ERRPTR;
		}
	}
	
	// NULL Check
	if( Left == NULL || Right == NULL ) {
		if(Right && Right != preCastValue)	free(Right);
		return NULL;
	}

	// Catch comparisons
	switch(Operation)
	{
	case NODETYPE_EQUALS:
	case NODETYPE_NOTEQUALS:
	case NODETYPE_LESSTHAN:
	case NODETYPE_GREATERTHAN:
	case NODETYPE_LESSTHANEQUAL:
	case NODETYPE_GREATERTHANEQUAL: {
		 int	cmp;
		ret = NULL;
		// Do operation
		switch(Left->Type)
		{
		// - String Compare (does a strcmp, well memcmp)
		case SS_DATATYPE_STRING:
			// Call memcmp to do most of the work
			cmp = memcmp(
				Left->String.Data, Right->String.Data,
				(Left->String.Length < Right->String.Length) ? Left->String.Length : Right->String.Length
				);
			// Handle reaching the end of the string
			if( cmp == 0 ) {
				if( Left->String.Length == Right->String.Length )
					cmp = 0;
				else if( Left->String.Length < Right->String.Length )
					cmp = 1;
				else
					cmp = -1;
			}
			break;
		
		// - Integer Comparisons
		case SS_DATATYPE_INTEGER:
			if( Left->Integer == Right->Integer )
				cmp = 0;
			else if( Left->Integer < Right->Integer )
				cmp = -1;
			else
				cmp = 1;
			break;
		// - Real Number Comparisons
		case SS_DATATYPE_REAL:
			cmp = (Left->Real - Right->Real) / Right->Real * 10000;	// < 0.1% difference is equality
			break;
		default:
			AST_RuntimeError(Node, "TODO - Comparison of type %i", Left->Type);
			ret = ERRPTR;
			break;
		}
		
		// Error check
		if( ret != ERRPTR )
		{
			if(Left->ReferenceCount == 1 && Left->Type != SS_DATATYPE_STRING)
				SpiderScript_ReferenceValue(ret = Left);
			else
				ret = SpiderScript_CreateInteger(0);
			
			// Create return
			switch(Operation)
			{
			case NODETYPE_EQUALS:	ret->Integer = (cmp == 0);	break;
			case NODETYPE_NOTEQUALS:	ret->Integer = (cmp != 0);	break;
			case NODETYPE_LESSTHAN:	ret->Integer = (cmp < 0);	break;
			case NODETYPE_GREATERTHAN:	ret->Integer = (cmp > 0);	break;
			case NODETYPE_LESSTHANEQUAL:	ret->Integer = (cmp <= 0);	break;
			case NODETYPE_GREATERTHANEQUAL:	ret->Integer = (cmp >= 0);	break;
			default:
				AST_RuntimeError(Node, "Exec,CmpOp unknown op %i", Operation);
				SpiderScript_DereferenceValue(ret);
				ret = ERRPTR;
				break;
			}
		}
		if(Right && Right != preCastValue)	free(Right);
		return ret;
		}

	// Fall through and sort by type instead
	default:
		break;
	}
	
	// Do operation
	switch(Left->Type)
	{
	// String Concatenation
	case SS_DATATYPE_STRING:
		switch(Operation)
		{
		case NODETYPE_ADD:	// Concatenate
			ret = SpiderScript_StringConcat(Left, Right);
			break;
		// TODO: Support python style 'i = %i' % i ?
		// Might do it via a function call
		// Implement it via % with an array, but getting past the cast will be fun
//		case NODETYPE_MODULUS:
//			break;
		// TODO: Support string repititions
//		case NODETYPE_MULTIPLY:
//			break;

		default:
			AST_RuntimeError(Node, "SpiderScript internal error: Exec,BinOP,String unknown op %i", Operation);
			ret = ERRPTR;
			break;
		}
		break;
	// Integer Operations
	case SS_DATATYPE_INTEGER:
		if( Left->ReferenceCount == 1 )
			SpiderScript_ReferenceValue(ret = Left);
		else
			ret = SpiderScript_CreateInteger(0);
		switch(Operation)
		{
		case NODETYPE_ADD:	ret->Integer = Left->Integer + Right->Integer;	break;
		case NODETYPE_SUBTRACT:	ret->Integer = Left->Integer - Right->Integer;	break;
		case NODETYPE_MULTIPLY:	ret->Integer = Left->Integer * Right->Integer;	break;
		case NODETYPE_DIVIDE:	ret->Integer = Left->Integer / Right->Integer;	break;
		case NODETYPE_MODULO:	ret->Integer = Left->Integer % Right->Integer;	break;
		case NODETYPE_BWAND:	ret->Integer = Left->Integer & Right->Integer;	break;
		case NODETYPE_BWOR: 	ret->Integer = Left->Integer | Right->Integer;	break;
		case NODETYPE_BWXOR:	ret->Integer = Left->Integer ^ Right->Integer;	break;
		case NODETYPE_BITSHIFTLEFT: ret->Integer = Left->Integer << Right->Integer;	break;
		case NODETYPE_BITSHIFTRIGHT:ret->Integer = Left->Integer >> Right->Integer;	break;
		case NODETYPE_BITROTATELEFT:
			ret->Integer = (Left->Integer << Right->Integer) | (Left->Integer >> (64-Right->Integer));
			break;
		default:
			AST_RuntimeError(Node, "SpiderScript internal error: Exec,BinOP,Integer unknown op %i", Operation);
			SpiderScript_DereferenceValue(ret);
			ret = ERRPTR;
			break;
		}
		break;
	
	// Real Numbers
	case SS_DATATYPE_REAL:
		if( Left->ReferenceCount == 1 )
			SpiderScript_ReferenceValue(ret = Left);
		else
			ret = SpiderScript_CreateReal(0);
		switch(Operation)
		{
		case NODETYPE_ADD:	ret->Real = Left->Real + Right->Real;	break;
		case NODETYPE_SUBTRACT:	ret->Real = Left->Real - Right->Real;	break;
		case NODETYPE_MULTIPLY:	ret->Real = Left->Real * Right->Real;	break;
		case NODETYPE_DIVIDE:	ret->Real = Left->Real / Right->Real;	break;
		default:
			AST_RuntimeError(Node, "SpiderScript internal error: Exec,BinOP,Real unknown op %i", Operation);
			SpiderScript_DereferenceValue(ret);
			ret = ERRPTR;
			break;
		}
		break;
	
	default:
		AST_RuntimeError(Node, "BUG - Invalid operation (%i) on type (%i)", Operation, Left->Type);
		ret = ERRPTR;
		break;
	}
	
	if(Right && Right != preCastValue)	free(Right);
	
	return ret;
}

tSpiderValue *AST_ExecuteNode_Index(tSpiderScript *Script, tAST_Node *Node,
	tSpiderValue *Array, int Index, tSpiderValue *SaveValue)
{
	// Quick sanity check
	if( !Array )
	{
		AST_RuntimeError(Node, "Indexing NULL, not a good idea");
		return ERRPTR;
	}

	// Array?
	if( SS_GETARRAYDEPTH(Array->Type) )
	{
		if( Index < 0 || Index >= Array->Array.Length ) {
			AST_RuntimeError(Node, "Array index out of bounds %i not in (0, %i]",
				Index, Array->Array.Length);
			return ERRPTR;
		}
		
		if( SaveValue != ERRPTR )
		{
			if( SaveValue && SaveValue->Type != SS_DOWNARRAY(Array->Type) ) {
				// TODO: Implicit casting
				AST_RuntimeError(Node, "Type mismatch assiging to array element");
				return ERRPTR;
			}
			SpiderScript_DereferenceValue( Array->Array.Items[Index] );
			Array->Array.Items[Index] = SaveValue;
			SpiderScript_ReferenceValue( Array->Array.Items[Index] );
			return NULL;
		}
		else
		{
			SpiderScript_ReferenceValue( Array->Array.Items[Index] );
			return Array->Array.Items[Index];
		}
	}
	else
	{
		AST_RuntimeError(Node, "TODO - Implement indexing on non-arrays (type = %x)",
			Array->Type);
		return ERRPTR;
	}
}

/**
 * \brief Get/Set the value of an element/attribute of a class
 * \param Script	Executing script
 * \param Node	Current execution node (only used for AST_RuntimeError)
 * \param Object	Object value
 * \param ElementName	Name of the attribute to be accessed
 * \param SaveValue	Value to set the element to (if ERRPTR, element value is returned)
 */
tSpiderValue *AST_ExecuteNode_Element(tSpiderScript *Script, tAST_Node *Node,
	tSpiderValue *Object, const char *ElementName, tSpiderValue *SaveValue)
{
	 int	i;
	tSpiderValue	*ret;	

	if( !Object ) {
		AST_RuntimeError(Node, "Tried to access an element of NULL");
		return ERRPTR;
	}
	
	tSpiderClass *nc;
	tScript_Class	*sc;
	
	nc = SpiderScript_GetClass_Native(Script, Object->Type);
	sc = SpiderScript_GetClass_Script(Script, Object->Type);

	if( nc ) {
		for( i = 0; i < nc->NAttributes; i ++ )
		{
			if( strcmp(ElementName, nc->AttributeDefs[i].Name) == 0 )
				break ;
		}
		if( i == nc->NAttributes ) {
			AST_RuntimeError(Node, "No attribute %s of %s", ElementName, nc->Name);
			return ERRPTR;
		}
	}
	else if( sc ) {
		tScript_Class_Var *at;
		for( i = 0, at = sc->FirstProperty; at; at = at->Next, i ++ )
		{
			if( strcmp(ElementName, at->Name) == 0 )
				break;
		}
		if( !at ) {
			AST_RuntimeError(Node, "No attribute %s of %s", ElementName, sc->Name);
			return ERRPTR;
		}
	}
	else {
		AST_RuntimeError(Node, "Unable to get element of type %i", Object->Type);
		return ERRPTR;
	}
	
	if( SaveValue != ERRPTR ) {
		SpiderScript_DereferenceValue( Object->Object->Attributes[i] );
		Object->Object->Attributes[i] = SaveValue;
		SpiderScript_ReferenceValue(SaveValue);
		return NULL;
	}
	else {
		ret = Object->Object->Attributes[i];
		SpiderScript_ReferenceValue(ret);
		return ret;
	}
}


