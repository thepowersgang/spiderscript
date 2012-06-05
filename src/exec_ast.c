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
 int	SpiderScript_int_GetTypeSize(int Type);

// === PROTOTYPES ===
// - Errors
void	AST_RuntimeMessage(tAST_Node *Node, const char *Type, const char *Format, ...);
void	AST_RuntimeError(tAST_Node *Node, const char *Format, ...);

// === GLOBALS ===
 int	giNextBlockIdent = 1;

// === CODE ===
tSpiderInteger AST_ExecuteNode_UniOp_Integer(tSpiderScript *Script, int Op, tSpiderInteger Value)
{
	switch(Op)
	{
	case NODETYPE_NEGATE:	return -Value;
	case NODETYPE_BWNOT:	return ~Value;
	default:
		AST_RuntimeError(NULL, "SpiderScript internal error: Exec,UniOP,Integer unknown op %i", Op);
		return 0;
	}
	
}

tSpiderReal AST_ExecuteNode_UniOp_Real(tSpiderScript *Script, int Operation, tSpiderReal Value)
{
	switch(Operation)
	{
	case NODETYPE_NEGATE:	return -Value;
	default:
		AST_RuntimeError(NULL, "SpiderScript internal error: Exec,UniOP,Real unknown op %i", Operation);
		return 0;
	}
}

int AST_ExecuteNode_BinOp_Integer(tSpiderScript *Script, void *RetData,
	int Op,
	tSpiderInteger Left, int RightType, const void *Right)
{
	tSpiderInteger	*ret_i = RetData;
	tSpiderBool	*ret_b = RetData;
	tSpiderInteger	right;
	
	if( RightType != SS_DATATYPE_INTEGER )
		return -1;	
	right = *(const tSpiderInteger*)Right;

	switch(Op)
	{
	case NODETYPE_EQUALS:   	*ret_b = Left == right;	return SS_DATATYPE_BOOLEAN;
	case NODETYPE_NOTEQUALS:	*ret_b = Left != right;	return SS_DATATYPE_BOOLEAN;
	case NODETYPE_LESSTHAN: 	*ret_b = Left <  right;	return SS_DATATYPE_BOOLEAN;
	case NODETYPE_GREATERTHAN:	*ret_b = Left >  right;	return SS_DATATYPE_BOOLEAN;
	case NODETYPE_LESSTHANEQUAL:	*ret_b = Left <= right;	return SS_DATATYPE_BOOLEAN;
	case NODETYPE_GREATERTHANEQUAL:	*ret_b = Left >= right;	return SS_DATATYPE_BOOLEAN;
	case NODETYPE_ADD:	*ret_i = Left + right;	return SS_DATATYPE_INTEGER;
	case NODETYPE_SUBTRACT:	*ret_i = Left - right;	return SS_DATATYPE_INTEGER;
	case NODETYPE_MULTIPLY:	*ret_i = Left * right;	return SS_DATATYPE_INTEGER;
	case NODETYPE_DIVIDE:	*ret_i = Left / right;	return SS_DATATYPE_INTEGER;
	case NODETYPE_MODULO:	*ret_i = Left % right;	return SS_DATATYPE_INTEGER;
	case NODETYPE_BWAND:	*ret_i = Left & right;	return SS_DATATYPE_INTEGER;
	case NODETYPE_BWOR: 	*ret_i = Left | right;	return SS_DATATYPE_INTEGER;
	case NODETYPE_BWXOR:	*ret_i = Left ^ right;	return SS_DATATYPE_INTEGER;
	case NODETYPE_BITSHIFTLEFT: *ret_i = Left << right;	return SS_DATATYPE_INTEGER;
	case NODETYPE_BITSHIFTRIGHT:*ret_i = Left >> right;	return SS_DATATYPE_INTEGER;
	case NODETYPE_BITROTATELEFT:
		*ret_i = (Left << right) | (Left >> (64-right));
		return SS_DATATYPE_INTEGER;
	default:
		AST_RuntimeError(NULL, "SpiderScript internal error: Exec,BinOP,Integer unknown op %i", Op);
		return -1;
	}
}

int AST_ExecuteNode_BinOp_Real(tSpiderScript *Script, void *RetData,
	int Op,
	tSpiderReal Left, int RightType, const void *Right)
{
	tSpiderReal	*ret_r = RetData;
	tSpiderBool	*ret_b = RetData;
	tSpiderReal	right;
	
	if( RightType != SS_DATATYPE_REAL )
		return -1;	
	right = *(const tSpiderReal*)Right;

	switch(Op)
	{
	// TODO: Less exact real number comparisons?
	case NODETYPE_EQUALS:   	*ret_b = Left == right;	return SS_DATATYPE_BOOLEAN;
	case NODETYPE_NOTEQUALS:	*ret_b = Left != right;	return SS_DATATYPE_BOOLEAN;
	case NODETYPE_LESSTHAN: 	*ret_b = Left <  right;	return SS_DATATYPE_BOOLEAN;
	case NODETYPE_GREATERTHAN:	*ret_b = Left >  right;	return SS_DATATYPE_BOOLEAN;
	case NODETYPE_LESSTHANEQUAL:	*ret_b = Left <= right;	return SS_DATATYPE_BOOLEAN;
	case NODETYPE_GREATERTHANEQUAL:	*ret_b = Left >= right;	return SS_DATATYPE_BOOLEAN;
	case NODETYPE_ADD:	*ret_r = Left + right;	return SS_DATATYPE_REAL;
	case NODETYPE_SUBTRACT:	*ret_r = Left - right;	return SS_DATATYPE_REAL;
	case NODETYPE_MULTIPLY:	*ret_r = Left * right;	return SS_DATATYPE_REAL;
	case NODETYPE_DIVIDE:	*ret_r = Left / right;	return SS_DATATYPE_REAL;
	default:
		AST_RuntimeError(NULL, "SpiderScript internal error: Exec,BinOP,Integer unknown op %i", Op);
		return -1;
	}
}

int AST_ExecuteNode_BinOp_String(tSpiderScript *Script, void *RetData,
	int Op, const tSpiderString *Left, int RightType, const void *Right)
{
	tSpiderString	**ret_s = RetData;
	tSpiderBool	*ret_b = RetData;
	const tSpiderString	*right_s = Right;
	 int	cmp;

	switch(Op)
	{
	case NODETYPE_EQUALS:
	case NODETYPE_NOTEQUALS:
	case NODETYPE_LESSTHAN:
	case NODETYPE_GREATERTHAN:
	case NODETYPE_LESSTHANEQUAL:
	case NODETYPE_GREATERTHANEQUAL:
		if( RightType != SS_DATATYPE_STRING )	return -1;
		cmp = SpiderScript_StringCompare(Left, right_s);
		switch(Op)
		{
		case NODETYPE_EQUALS:   	*ret_b = (cmp == 0);	break;
		case NODETYPE_NOTEQUALS:	*ret_b = (cmp != 0);	break;
		case NODETYPE_LESSTHAN: 	*ret_b = (cmp <  0);	break;
		case NODETYPE_GREATERTHAN:	*ret_b = (cmp >  0);	break;
		case NODETYPE_LESSTHANEQUAL:	*ret_b = (cmp <= 0);	break;
		case NODETYPE_GREATERTHANEQUAL:	*ret_b = (cmp >= 0);	break;
		}
		return SS_DATATYPE_BOOLEAN;
	
	case NODETYPE_ADD:	// Concatenate
		if( RightType != SS_DATATYPE_STRING )	return -1;
		*ret_s = SpiderScript_StringConcat(Left, right_s);
		return SS_DATATYPE_STRING;
	default:
		AST_RuntimeError(NULL, "Unknown operation on string (%i)", Op);
		return -1;
	}
}

#if 0
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
#endif

int AST_ExecuteNode_Index(tSpiderScript *Script, void *RetData,
	tSpiderArray *Array, int Index, int NewType, void *NewData)
{
	 int	size;

	// Quick sanity check
	if( !Array ) {
		AST_RuntimeError(NULL, "Indexing NULL, not a good idea");
		return -1;
	}

	// Array?
	if( Index < 0 || Index >= Array->Length ) {
		AST_RuntimeError(NULL, "Array index out of bounds %i not in (0, %i]",
			Index, Array->Length);
		return -1;
	}

	size = SpiderScript_int_GetTypeSize(Array->Type);	
	if( size == -1 ) {
		return -1;
	}

	if( NewData )
	{
		if( NewType != Array->Type ) {
			// TODO: Implicit casting?
			AST_RuntimeError(NULL, "Type mismatch assiging to array element");
			return -1;
		}
		if( SS_GETARRAYDEPTH(NewType) ) {
			SpiderScript_DereferenceArray( Array->Arrays[Index] );
			Array->Arrays[Index] = NewData;
			SpiderScript_ReferenceArray( Array->Arrays[Index] );
		}
		else if( SS_ISTYPEOBJECT(NewType) ) {
			SpiderScript_DereferenceObject( Array->Objects[Index] );
			Array->Objects[Index] = NewData;
			SpiderScript_ReferenceObject  ( Array->Objects[Index] );
		}
		else if( NewType == SS_DATATYPE_STRING ) {
			SpiderScript_DereferenceString( Array->Strings[Index] );
			Array->Strings[Index] = NewData;
			SpiderScript_ReferenceString  ( Array->Strings[Index] );
		}
		else {
			memcpy(Array->Bools + size*Index, NewData, size);
		}
		return Array->Type;
	}
	else
	{
		if( size == 0 )
			size = sizeof(void*);
		memcpy(RetData, Array->Bools + size*Index, size);
		return Array->Type;
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
int AST_ExecuteNode_Element(tSpiderScript *Script, void *RetData,
	tSpiderObject *Object, const char *ElementName, int NewType, void *NewData)
{
	 int	i, type;
	const char	*className;

	if( !Object ) {
		AST_RuntimeError(NULL, "Tried to access an element of NULL");
		return -1;
	}
	
	tSpiderClass *nc;
	tScript_Class	*sc;
	
	nc = SpiderScript_GetClass_Native(Script, Object->TypeCode);
	sc = SpiderScript_GetClass_Script(Script, Object->TypeCode);

	if( nc ) {
		for( i = 0; i < nc->NAttributes; i ++ )
		{
			if( strcmp(ElementName, nc->AttributeDefs[i].Name) == 0 )
				break ;
		}
		if( i == nc->NAttributes ) {
			AST_RuntimeError(NULL, "No attribute %s of %s", ElementName, nc->Name);
			return -1;
		}
		type = nc->AttributeDefs[i].Type;
		className = nc->Name;
	}
	else if( sc ) {
		tScript_Class_Var *at;
		for( i = 0, at = sc->FirstProperty; at; at = at->Next, i ++ )
		{
			if( strcmp(ElementName, at->Name) == 0 )
				break;
		}
		if( !at ) {
			AST_RuntimeError(NULL, "No attribute %s of %s", ElementName, sc->Name);
			return -1;
		}
		type = at->Type;
		className = sc->Name;
	}
	else {
		AST_RuntimeError(NULL, "Unable to get element of type %i", Object->TypeCode);
		return -1;
	}
	
	int size = SpiderScript_int_GetTypeSize(type);
	if( size == -1 ) {
		AST_RuntimeError(NULL, "Type of element %s of %s is invalid (%i)", ElementName, className, type);
		return -1;
	}
	
	if( NewData )
	{
		if( type != NewType ) {
			AST_RuntimeError(NULL, "Assignment of element '%s' of '%s' mismatch (%i should be %i)",
				ElementName, className, NewType, type);
			return -1;
		}
		if( SS_GETARRAYDEPTH(NewType) ) {
			SpiderScript_DereferenceArray( Object->Attributes[i] );
			Object->Attributes[i] = NewData;
			SpiderScript_ReferenceArray( Object->Attributes[i] );
		}
		else if( SS_ISTYPEOBJECT(NewType) ) {
			SpiderScript_DereferenceObject( Object->Attributes[i] );
			Object->Attributes[i] = NewData;
			SpiderScript_ReferenceObject  ( Object->Attributes[i] );
		}
		else if( NewType == SS_DATATYPE_STRING ) {
			SpiderScript_DereferenceString( Object->Attributes[i] );
			Object->Attributes[i] = NewData;
			SpiderScript_ReferenceString  ( Object->Attributes[i] );
		}
		else {
			memcpy(Object->Attributes[i], NewData, size);
		}
	}
	else {
		if( SS_GETARRAYDEPTH(NewType) ) {
			SpiderScript_ReferenceArray( Object->Attributes[i] );
			*(void**)RetData = Object->Attributes[i];
		}
		else if( SS_ISTYPEOBJECT(NewType) ) {
			SpiderScript_ReferenceObject  ( Object->Attributes[i] );
			*(void**)RetData = Object->Attributes[i];
		}
		else if( NewType == SS_DATATYPE_STRING ) {
			SpiderScript_ReferenceString  ( Object->Attributes[i] );
			*(void**)RetData = Object->Attributes[i];
		}
		else {
			memcpy(RetData, Object->Attributes[i], size);
		}
	}
	return type;
}


