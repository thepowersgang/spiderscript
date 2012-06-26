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

// === GLOBALS ===
 int	giNextBlockIdent = 1;

// === CODE ===
int AST_ExecuteNode_UniOp_GetType(tSpiderScript *Script, int Op, int Type)
{
	switch(Type)
	{
	case SS_DATATYPE_BOOLEAN:
		switch(Op)
		{
		case NODETYPE_LOGICALNOT:
			return SS_DATATYPE_BOOLEAN;
		default:
			return 0;
		}
	case SS_DATATYPE_INTEGER:
		switch(Op)
		{
		case NODETYPE_LOGICALNOT:
			return SS_DATATYPE_BOOLEAN;
		case NODETYPE_NEGATE:
		case NODETYPE_BWNOT:
			return SS_DATATYPE_INTEGER;
		default:
			return 0;
		}
	case SS_DATATYPE_REAL:
		switch(Op)
		{
		case NODETYPE_LOGICALNOT:
			return SS_DATATYPE_BOOLEAN;
		case NODETYPE_NEGATE:
			return SS_DATATYPE_REAL;
		default:
			return 0;
		}
	default:
		return 0;
	}
}

tSpiderInteger AST_ExecuteNode_UniOp_Integer(tSpiderScript *Script, int Op, tSpiderInteger Value)
{
	switch(Op)
	{
	case NODETYPE_NEGATE:	return -Value;
	case NODETYPE_BWNOT:	return ~Value;
	default:
		SpiderScript_ThrowException(Script, SS_EXCEPTION_BUG,
			mkstr("Exec,UniOP,Integer unknown op %i", Op)
			);
		return 0;
	}
	
}

tSpiderReal AST_ExecuteNode_UniOp_Real(tSpiderScript *Script, int Operation, tSpiderReal Value)
{
	switch(Operation)
	{
	case NODETYPE_NEGATE:	return -Value;
	default:
		SpiderScript_ThrowException(Script, SS_EXCEPTION_BUG,
			mkstr("Exec,UniOP,Real unknown op %i", Operation)
			);
		return 0;
	}
}

/**
 * \brief Gets the return type of an operation
 * \return >0 = Valid, BinOp would return this type
 * \return =0 = Invalid operation (no casting possible)
 * \return <0 = Valid operation, but needs to be cast to -ve this value
 */
int AST_ExecuteNode_BinOp_GetType(tSpiderScript *Script, int Op, int LType, int RType)
{
	switch(LType)
	{
	case SS_DATATYPE_BOOLEAN:
		switch(Op)
		{
		case NODETYPE_EQUALS:
		case NODETYPE_NOTEQUALS:
		case NODETYPE_LESSTHAN: 
		case NODETYPE_GREATERTHAN:
		case NODETYPE_LESSTHANEQUAL:
		case NODETYPE_GREATERTHANEQUAL:
		
		case NODETYPE_LOGICALAND:
		case NODETYPE_LOGICALOR:
		case NODETYPE_LOGICALXOR:
			if( RType != SS_DATATYPE_BOOLEAN )	return -SS_DATATYPE_BOOLEAN;
			return SS_DATATYPE_BOOLEAN;
		}
		break;
	case SS_DATATYPE_INTEGER:
		switch(Op)
		{
		case NODETYPE_EQUALS:
		case NODETYPE_NOTEQUALS:
		case NODETYPE_LESSTHAN: 
		case NODETYPE_GREATERTHAN:
		case NODETYPE_LESSTHANEQUAL:
		case NODETYPE_GREATERTHANEQUAL:
			if( RType != SS_DATATYPE_INTEGER )	return -SS_DATATYPE_INTEGER;
			return SS_DATATYPE_BOOLEAN;
		case NODETYPE_ADD:
		case NODETYPE_SUBTRACT:
		case NODETYPE_MULTIPLY:
		case NODETYPE_DIVIDE:
		case NODETYPE_MODULO:
		case NODETYPE_BWAND:
		case NODETYPE_BWOR:
		case NODETYPE_BWXOR:
		case NODETYPE_BITSHIFTLEFT:
		case NODETYPE_BITSHIFTRIGHT:
		case NODETYPE_BITROTATELEFT:
			if( RType != SS_DATATYPE_INTEGER )	return -SS_DATATYPE_INTEGER;
			return SS_DATATYPE_INTEGER;
		}
		break;
	case SS_DATATYPE_REAL:
		if( RType != SS_DATATYPE_REAL )	return -SS_DATATYPE_REAL;
		switch(Op)
		{
		case NODETYPE_EQUALS:
		case NODETYPE_NOTEQUALS:
		case NODETYPE_LESSTHAN: 
		case NODETYPE_GREATERTHAN:
		case NODETYPE_LESSTHANEQUAL:
		case NODETYPE_GREATERTHANEQUAL:
			return SS_DATATYPE_BOOLEAN;
		case NODETYPE_ADD:
		case NODETYPE_SUBTRACT:
		case NODETYPE_MULTIPLY:
		case NODETYPE_DIVIDE:
			return SS_DATATYPE_REAL;
		}
		break;
	case SS_DATATYPE_STRING:
		switch(Op)
		{
		case NODETYPE_EQUALS:
		case NODETYPE_NOTEQUALS:
		case NODETYPE_LESSTHAN:
		case NODETYPE_GREATERTHAN:
		case NODETYPE_LESSTHANEQUAL:
		case NODETYPE_GREATERTHANEQUAL:
			if( RType != SS_DATATYPE_STRING )	return -SS_DATATYPE_STRING;
			return SS_DATATYPE_BOOLEAN;
		case NODETYPE_ADD:	// Concatenate
			if( RType != SS_DATATYPE_STRING )	return -SS_DATATYPE_STRING;
			return SS_DATATYPE_STRING;
		}
		break;
	}
	return 0;
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
		SpiderScript_ThrowException(Script, SS_EXCEPTION_BUG,
			mkstr("Exec,BinOp,Integer unknown op %i", Op)
			);
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
		SpiderScript_ThrowException(Script, SS_EXCEPTION_BUG,
			mkstr("Exec,BinOp,Real unknown op %i", Op)
			);
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

	// TODO: Modulo = python-esque printf?
	// TODO: Multiply = repeat?	

	default:
		SpiderScript_ThrowException(Script, SS_EXCEPTION_BUG,
			mkstr("Exec,BinOp,String unknown op %i", Op)
			);
		return -1;
	}
}

int AST_ExecuteNode_Index(tSpiderScript *Script, void *RetData,
	tSpiderArray *Array, int Index, int NewType, void *NewData)
{
	 int	size;

	// Quick sanity check
	if( !Array ) {
		SpiderScript_ThrowException(Script, SS_EXCEPTION_NULLDEREF, strdup("Indexed a NULL array"));
		return -1;
	}

	// Array?
	if( Index < 0 || Index >= Array->Length ) {
		// TODO: Include extra information
		SpiderScript_ThrowException(Script, SS_EXCEPTION_INDEX_OOB, strdup("Index out of bounds"));
		return -1;
	}

	size = SpiderScript_int_GetTypeSize(Array->Type);
	if( size == -1 ) {
		SpiderScript_ThrowException(Script, SS_EXCEPTION_BUG, strdup("Array type unhandled"));
		return -1;
	}

	if( NewData )
	{
		if( NewType != Array->Type ) {
			// TODO: Implicit casting?
			SpiderScript_ThrowException(Script, SS_EXCEPTION_TYPEMISMATCH,
				strdup("Type mismatch assiging to array element"));
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
		if( SS_GETARRAYDEPTH(Array->Type) ) {
			*(tSpiderArray**)RetData = Array->Arrays[Index];
			SpiderScript_ReferenceArray( Array->Arrays[Index] );
		}
		else if( SS_ISTYPEOBJECT(Array->Type) ) {
			*(tSpiderObject**)RetData = Array->Objects[Index];
			SpiderScript_ReferenceObject( Array->Objects[Index] );
		}
		else if( Array->Type == SS_DATATYPE_STRING ) {
			*(tSpiderString**)RetData = Array->Strings[Index];
			SpiderScript_ReferenceString( Array->Strings[Index] );
		}
		else {
			memcpy(RetData, Array->Bools + size*Index, size);
		}
		return Array->Type;
	}
}

/**
 * \brief Get/Set the value of an element/attribute of a class
 * \param Script	Executing script
 * \param RetData	Location to store element value (if requested)
 * \param Object	Object value
 * \param ElementName	Name of the attribute to be accessed
 * \param NewType	Type of data to save (used for validation)
 * \param NewData	Pointer to data to store in element
 */
int AST_ExecuteNode_Element(tSpiderScript *Script, void *RetData,
	tSpiderObject *Object, const char *ElementName, int NewType, void *NewData)
{
	 int	i, type;
	const char	*className;

	if( !Object ) {
		SpiderScript_ThrowException(Script, SS_EXCEPTION_NULLDEREF,
			strdup("Tried to access an element of NULL"));
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
			SpiderScript_ThrowException(Script, SS_EXCEPTION_BADELEMENT,
				mkstr("No attribute %s of %s", ElementName, nc->Name)
				);
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
			SpiderScript_ThrowException(Script, SS_EXCEPTION_BADELEMENT,
				mkstr("No attribute %s of %s", ElementName, nc->Name)
				);
			return -1;
		}
		type = at->Type;
		className = sc->Name;
	}
	else {
		SpiderScript_ThrowException(Script, SS_EXCEPTION_TYPEMISMATCH,
			mkstr("Unable to get element of type 0x%x", Object->TypeCode)
			);
		return -1;
	}
	
	int size = SpiderScript_int_GetTypeSize(type);
	if( size == -1 ) {
		SpiderScript_ThrowException(Script, SS_EXCEPTION_BUG,
			mkstr("Type of element %s of %s is invalid (%i)",
				ElementName, className, type)
			);
		return -1;
	}
	
	if( NewData )
	{
		if( type != NewType ) {
			SpiderScript_ThrowException(Script, SS_EXCEPTION_TYPEMISMATCH,
				mkstr("Assignment of element '%s' of '%s' mismatch (%i should be %i)",
					ElementName, className, NewType, type)
				);
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


