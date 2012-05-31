/*
 * SpiderScript Library
 * by John Hodge (thePowersGang)
 * 
 * exec_bytecode.c
 * - Execute bytecode
 */
#include <stdlib.h>
#include <stdint.h>
#include "common.h"
#include "bytecode.h"
#include <stdio.h>
#include <string.h>
#include "ast.h"
#include <inttypes.h>

#define TRACE	0

#if TRACE
# define DEBUG_F(v...)	printf(v)
#else
# define DEBUG_F(v...)
#endif

// === IMPORTS ===
extern void	AST_RuntimeError(tAST_Node *Node, const char *Format, ...);

// === TYPES ===
typedef struct sBC_StackEnt	tBC_StackEnt;
typedef struct sBC_Stack	tBC_Stack;

#define ET_FUNCTION_START	-1

struct sBC_StackEnt
{
	tSpiderScript_DataType	Type;
	union {
		int64_t	Integer;
		double 	Real;
		tSpiderValue	*Reference;	// Used for everything else
		tSpiderObject	*Object;
	};
};

struct sBC_Stack
{
	 int	EntrySpace;
	 int	EntryCount;
	tBC_StackEnt	Entries[];
};

// === PROTOTYPES ===
tSpiderValue	*Bytecode_ExecuteFunction(tSpiderScript *Script, tScript_Function *Fcn, int NArguments, tSpiderValue **Args);
 int	Bytecode_int_ExecuteFunction(tSpiderScript *Script, tScript_Function *Fcn, tBC_Stack *Stack, int ArgCount);

// === CODE ===
int Bytecode_int_StackPop(tBC_Stack *Stack, tBC_StackEnt *Dest)
{
	if( Stack->EntryCount == 0 )	return 1;
	Stack->EntryCount --;
	*Dest = Stack->Entries[Stack->EntryCount];
	return 0;
}

int Bytecode_int_StackPush(tBC_Stack *Stack, tBC_StackEnt *Src)
{
	if( Stack->EntryCount == Stack->EntrySpace )	return 1;
	Stack->Entries[Stack->EntryCount] = *Src;
	Stack->EntryCount ++;
	return 0;
}

int Bytecode_int_IsStackEntTrue(tBC_StackEnt *Ent)
{
	switch(Ent->Type)
	{
	case ET_FUNCTION_START:
		AST_RuntimeError(NULL, "BUG - _IsStackEntTrue on ET_FUNCTION_START");
		return -1;
	case SS_DATATYPE_NOVALUE:
		return 0;
	case SS_DATATYPE_BOOLEAN:
	case SS_DATATYPE_INTEGER:
		return !!Ent->Integer;
	case SS_DATATYPE_REAL:
		return !(-.5f < Ent->Real && Ent->Real < 0.5f);
	case SS_DATATYPE_STRING:
		return SpiderScript_IsValueTrue(Ent->Reference);
	default:
		if( SS_GETARRAYDEPTH(Ent->Type) )
			return SpiderScript_IsValueTrue(Ent->Reference);
		else if( SS_ISTYPEOBJECT(Ent->Type) )
			return Ent->Object != NULL;
		else {
			AST_RuntimeError(NULL, "BUG - Type 0x%x unhandled in _IsStackEntTrue", Ent->Type);
			return -1;
		}
	}
}

tSpiderValue *Bytecode_int_GetSpiderValue(const tBC_StackEnt *Ent, tSpiderValue *tmp)
{
	 int	bAlloc = 0;
	
	switch(Ent->Type)
	{
	case ET_FUNCTION_START:
		AST_RuntimeError(NULL, "_GetSpiderValue on ET_FUNCTION_START");
		return NULL;
	case SS_DATATYPE_NOVALUE:
		AST_RuntimeError(NULL, "_GetSpiderValue on SS_DATATYPE_NOVALUE");
		return NULL;
	case SS_DATATYPE_BOOLEAN:
	case SS_DATATYPE_INTEGER:
	case SS_DATATYPE_REAL:
		bAlloc = 1;
		break;
	case SS_DATATYPE_STRING:
		bAlloc = 0;
		break;
	default:
		if( SS_GETARRAYDEPTH(Ent->Type) ) {
			bAlloc = 0;
		}
		else if( SS_ISTYPEOBJECT(Ent->Type) ) {
			bAlloc = 1;
		}
		else {
			AST_RuntimeError(NULL, "BUG - Type 0x%x unhandled in _GetSpiderValue", Ent->Type);
			return NULL;
		}
		break;
	}

	if( bAlloc ) {
		if(!tmp) {
			tmp = malloc(sizeof(tSpiderValue));
			tmp->ReferenceCount = 1;
		} else {
			// Stops a stack value from having free() called on it
			tmp->ReferenceCount = 2;
		}
		tmp->Type = Ent->Type;
	}
	
	switch(Ent->Type)
	{
	case SS_DATATYPE_BOOLEAN:
		tmp->Integer = Ent->Integer;
		return tmp;
	case SS_DATATYPE_INTEGER:
		tmp->Integer = Ent->Integer;
		return tmp;
	case SS_DATATYPE_REAL:
		tmp->Real = Ent->Real;
		return tmp;
	case SS_DATATYPE_STRING:
		SpiderScript_ReferenceValue(Ent->Reference);
		return Ent->Reference;
	default:
		if( SS_GETARRAYDEPTH(Ent->Type) ) {
			SpiderScript_ReferenceValue(Ent->Reference);
			return Ent->Reference;
		}
		else if( SS_ISTYPEOBJECT(Ent->Type) ) {
			SpiderScript_ReferenceObject(Ent->Object);
			tmp->Object = Ent->Object;
			return tmp;
		}
		break;
	}
	// Should never be reached
	return NULL;
}

void Bytecode_int_SetSpiderValue(tBC_StackEnt *Ent, tSpiderValue *Value)
{
	if(!Value) {
		Ent->Type = SS_DATATYPE_NOVALUE;
		return ;
	}
	Ent->Type = Value->Type;
	switch(Value->Type)
	{
	case SS_DATATYPE_BOOLEAN:
		Ent->Integer = Value->Integer;
		break;
	case SS_DATATYPE_INTEGER:
		Ent->Integer = Value->Integer;
		break;
	case SS_DATATYPE_REAL:
		Ent->Real = Value->Real;
		break;
	case SS_DATATYPE_STRING:
		SpiderScript_ReferenceValue(Value);
		Ent->Reference = Value;
		break;
	
	default:
		if( SS_GETARRAYDEPTH(Value->Type) ) {
			Ent->Reference = Value;
			SpiderScript_ReferenceValue(Value);
		}
		else if( SS_ISTYPEOBJECT(Value->Type) ) {
			Ent->Object = Value->Object;
			SpiderScript_ReferenceObject(Value->Object);
		}
		else {
			AST_RuntimeError(NULL, "BUG - Type 0x%x unhandled in _SetSpiderValue", Value->Type);
			Ent->Type = SS_DATATYPE_NOVALUE;
		}
		break;
	}
}

void Bytecode_int_DerefStackValue(tBC_StackEnt *Ent)
{
	switch(Ent->Type)
	{
	case SS_DATATYPE_NOVALUE:
	case SS_DATATYPE_BOOLEAN:
	case SS_DATATYPE_INTEGER:
	case SS_DATATYPE_REAL:
		break;
	case SS_DATATYPE_STRING:
		SpiderScript_DereferenceValue(Ent->Reference);
		break;
	default:
		if( SS_GETARRAYDEPTH(Ent->Type) )
			SpiderScript_DereferenceValue(Ent->Reference);
		else if( SS_ISTYPEOBJECT(Ent->Type) )
			SpiderScript_DereferenceObject(Ent->Object);
		else
			AST_RuntimeError(NULL, "BUG - Type 0x%x unhandled in _DerefStackValue", Ent->Type);
		break;
	}
	Ent->Type = SS_DATATYPE_NOVALUE;
}
void Bytecode_int_RefStackValue(tBC_StackEnt *Ent)
{
	switch(Ent->Type)
	{
	case SS_DATATYPE_NOVALUE:
	case SS_DATATYPE_BOOLEAN:
	case SS_DATATYPE_INTEGER:
	case SS_DATATYPE_REAL:
		break;
	case SS_DATATYPE_STRING:
		SpiderScript_ReferenceValue(Ent->Reference);
		break;
	default:
		if( SS_GETARRAYDEPTH(Ent->Type) )
			SpiderScript_ReferenceValue(Ent->Reference);
		else if( SS_ISTYPEOBJECT(Ent->Type) )
			SpiderScript_ReferenceObject(Ent->Object);
		else
			AST_RuntimeError(NULL, "BUG - Type 0x%x unhandled in _RefStackValue", Ent->Type);
		break;
	}
}

void Bytecode_int_PrintStackValue(tBC_StackEnt *Ent)
{
	switch(Ent->Type)
	{
	case SS_DATATYPE_NOVALUE:
		printf("null");
		break;
	case SS_DATATYPE_BOOLEAN:
		printf("%s", (Ent->Integer ? "true" : "false"));
		break;
	case SS_DATATYPE_INTEGER:
		printf("0x%"PRIx64, Ent->Integer);
		break;
	case SS_DATATYPE_REAL:
		printf("%lf", Ent->Real);
		break;
	case SS_DATATYPE_STRING:
		printf("String (%i bytes)", Ent->Reference->String.Length);
		break;
	default:
		if( SS_GETARRAYDEPTH(Ent->Type) )
			printf("Array %p", Ent->Reference);
		else if( SS_ISTYPEOBJECT(Ent->Type) )
			printf("Object 0x%x %p", Ent->Type, Ent->Object);
		else
			AST_RuntimeError(NULL, "BUG - Type 0x%x unhandled in _PrintStackValue", Ent->Type);
		break;
	}
}

#if TRACE
# define PRINT_STACKVAL(val)	Bytecode_int_PrintStackValue(&val)
#else
# define PRINT_STACKVAL(val)
#endif

#define GET_STACKVAL(dst)	if((ret = Bytecode_int_StackPop(Stack, &dst))) { \
	AST_RuntimeError(NULL, "Stack pop failed, empty stack");\
	return ret; \
}
#define PUT_STACKVAL(src)	if((ret = Bytecode_int_StackPush(Stack, &src))) { \
	AST_RuntimeError(NULL, "Stack push failed, full stack");\
	return ret; \
}
#define OP_INDX(op_ptr)	((op_ptr)->Content.StringInt.Integer)
#define OP_STRING(op_ptr)	((op_ptr)->Content.StringInt.String)

tSpiderValue *Bytecode_ExecuteFunction(tSpiderScript *Script, tScript_Function *Fcn, int NArguments, tSpiderValue **Args)
{
	const int	stack_size = 100;
	tSpiderValue	*ret;
	tBC_Stack	*stack;
	tBC_StackEnt	val;
	 int	i;
	
	stack = malloc(sizeof(tBC_Stack) + stack_size*sizeof(tBC_StackEnt));
	stack->EntrySpace = stack_size;
	stack->EntryCount = 0;

	// Push arguments in order (so top is last arg)
	for( i = 0; i < NArguments; i ++ )
	{
		Bytecode_int_SetSpiderValue(&val, Args[i]);
		Bytecode_int_StackPush(stack, &val);
	}

	// Call
	Bytecode_int_ExecuteFunction(Script, Fcn, stack, NArguments);

	// Get return value
	if( Bytecode_int_StackPop(stack, &val) ) {
		free(stack);
		return NULL;
	}
	free(stack);

	if( Fcn->ReturnType != val.Type )
	{
		AST_RuntimeError(NULL, "'%s' Returned type 0x%x not 0x%x",
			Fcn->Name, val.Type, Fcn->ReturnType);
		return NULL;
	}
	if( Fcn->ReturnType == SS_DATATYPE_NOVALUE )
		ret = NULL;
	else
		ret = Bytecode_int_GetSpiderValue(&val, NULL);

	return ret;
}

/**
 * \brief Call an external function (may recurse into Bytecode_ExecuteFunction, but may not)
 */
int Bytecode_int_CallExternFunction(tSpiderScript *Script, tBC_Stack *Stack, tBC_Op *op )
{
	const char	*name = OP_STRING(op);
	 int	arg_count = OP_INDX(op);
	 int	i, ret = 0;
	tSpiderValue	*args[arg_count];
	tSpiderValue	*rv;
	tBC_StackEnt	val1;
	const char	*namespaces[] = {NULL};	// TODO: Default/imported namespaces

	DEBUG_F("CALL (general) %s %i args\n", name, arg_count);
	
	// Read arguments
	for( i = arg_count; i --; )
	{
		GET_STACKVAL(val1);
		args[i] = Bytecode_int_GetSpiderValue(&val1, NULL);
		Bytecode_int_DerefStackValue(&val1);
	}
	
	// Call the function etc.
	if( op->Operation == BC_OP_CALLFUNCTION )
	{
		rv = SpiderScript_ExecuteFunction(Script, name, namespaces, arg_count, args, &op->CacheEnt, 1);
	}
	else if( op->Operation == BC_OP_CREATEOBJ )
	{
		rv = SpiderScript_CreateObject(Script, name, namespaces, arg_count, args, &op->CacheEnt, 1);
	}
	else if( op->Operation == BC_OP_CALLMETHOD )
	{
		tSpiderObject	*obj;
		GET_STACKVAL(val1);
		
		if( SS_ISTYPEOBJECT(val1.Type) )
			obj = val1.Object;
		else {
			// Error
			AST_RuntimeError(NULL, "OP_CALLMETHOD on non object");
			return -1;
		}
		rv = SpiderScript_ExecuteMethod(Script, obj, name, arg_count, args);
		Bytecode_int_DerefStackValue(&val1);
	}
	else
	{
		AST_RuntimeError(NULL, "BUG - Unknown operation for CALL/CREATEOBJ (%i)", op->Operation);
		rv = ERRPTR;
	}
	if(rv == ERRPTR) {
		AST_RuntimeError(NULL, "Function call %s failed, op = %i", name, op->Operation);
		return -1;
	}
	// Clean up args
	for( i = arg_count; i --; )
		SpiderScript_DereferenceValue(args[i]);
	// Get and push return
	Bytecode_int_SetSpiderValue(&val1, rv);
	PUT_STACKVAL(val1);
	// Deref return
	SpiderScript_DereferenceValue(rv);

	#if 0
	if(!rv) {
		printf("%s returned NULL\n", name);
	}
	if( rv && rv != ERRPTR && rv->ReferenceCount != 1 ) {
		printf("Return value from %s reference count fail (%i)\n",
			name, rv->ReferenceCount);
	}
	#endif	

	return 0;
}

int Bytecode_int_LocalBinOp_Integer(int Operation, tBC_StackEnt *Val1, tBC_StackEnt *Val2)
{
	switch(Operation)
	{
	case BC_OP_ADD: 	Val1->Integer = Val1->Integer + Val2->Integer;	break;
	case BC_OP_SUBTRACT:	Val1->Integer = Val1->Integer - Val2->Integer;	break;
	case BC_OP_MULTIPLY:	Val1->Integer = Val1->Integer * Val2->Integer;	break;
	case BC_OP_DIVIDE:	Val1->Integer = Val1->Integer / Val2->Integer;	break;
	
	case BC_OP_EQUALS:      	Val1->Integer = (Val1->Integer == Val2->Integer);	break;
	case BC_OP_NOTEQUALS:   	Val1->Integer = (Val1->Integer != Val2->Integer);	break;
	case BC_OP_LESSTHAN:    	Val1->Integer = (Val1->Integer <  Val2->Integer);	break;
	case BC_OP_LESSTHANOREQUAL:	Val1->Integer = (Val1->Integer <= Val2->Integer);	break;
	case BC_OP_GREATERTHAN: 	Val1->Integer = (Val1->Integer >  Val2->Integer);	break;
	case BC_OP_GREATERTHANOREQUAL:	Val1->Integer = (Val1->Integer >= Val2->Integer);	break;
	
	case BC_OP_BITAND:	Val1->Integer = Val1->Integer & Val2->Integer;	break;
	case BC_OP_BITOR:	Val1->Integer = Val1->Integer | Val2->Integer;	break;
	case BC_OP_BITXOR:	Val1->Integer = Val1->Integer ^ Val2->Integer;	break;
	case BC_OP_MODULO:	Val1->Integer = Val1->Integer % Val2->Integer;	break;
	default:	AST_RuntimeError(NULL, "Invalid operation on datatype Integer"); return -1;
	}
	return 0;
}

int Bytecode_int_LocalBinOp_Real(int Operation, tBC_StackEnt *Val1, tBC_StackEnt *Val2)
{
	switch(Operation)
	{
	// Real = Real OP Real
	case BC_OP_ADD: 	Val1->Real = Val1->Real + Val2->Real;	return 0;
	case BC_OP_SUBTRACT:	Val1->Real = Val1->Real - Val2->Real;	return 0;
	case BC_OP_MULTIPLY:	Val1->Real = Val1->Real * Val2->Real;	return 0;
	case BC_OP_DIVIDE:	Val1->Real = Val1->Real / Val2->Real;	return 0;

	// Bool/Integer = Real OP Real
	case BC_OP_EQUALS:      	Val1->Integer = (Val1->Real == Val2->Real);	break;
	case BC_OP_NOTEQUALS:   	Val1->Integer = (Val1->Real != Val2->Real);	break;
	case BC_OP_LESSTHAN:    	Val1->Integer = (Val1->Real <  Val2->Real);	break;
	case BC_OP_LESSTHANOREQUAL:	Val1->Integer = (Val1->Real <= Val2->Real);	break;
	case BC_OP_GREATERTHAN: 	Val1->Integer = (Val1->Real >  Val2->Real);	break;
	case BC_OP_GREATERTHANOREQUAL:	Val1->Integer = (Val1->Real >= Val2->Real);	break;
	
	default:	AST_RuntimeError(NULL, "Invalid operation on datatype Real"); return -1;
	}
	Val1->Type = SS_DATATYPE_INTEGER;	// Becomes logical
	return 0;
}

#define STATE_HDR()	DEBUG_F("%p %2i %02i ", op, Stack->EntryCount, op->Operation)

/**
 * \brief Execute a bytecode function with a stack
 */
int Bytecode_int_ExecuteFunction(tSpiderScript *Script, tScript_Function *Fcn, tBC_Stack *Stack, int ArgCount)
{
	 int	ret, ast_op, i;
	tBC_Op	*op;
	tBC_StackEnt	val1, val2;
	 int	local_var_count = Fcn->BCFcn->MaxVariableCount;
	tBC_StackEnt	local_vars[local_var_count];	// Includes arguments
	tSpiderValue	tmpVal1, tmpVal2;	// temp storage
	tSpiderValue	*pval1, *pval2, *ret_val;

	// Initialise local vars
	for( i = 0; i < local_var_count; i ++ )
		local_vars[i].Type = SS_DATATYPE_NOVALUE;
	
	// Pop off arguments
	if( ArgCount > Fcn->ArgumentCount )	return -1;
	DEBUG_F("Fcn->ArgumentCount = %i\n", Fcn->ArgumentCount);
	// - Handle optional arguments
	for( i = Fcn->ArgumentCount; i > ArgCount; )
	{
		i --;
		local_vars[i].Integer = 0;
		local_vars[i].Type = Fcn->Arguments[i].Type;
	}
	for( ; i --; )
	{
		GET_STACKVAL(local_vars[i]);
		// TODO: Type checks / enforcing
	}
	for( i = 0; i < Fcn->ArgumentCount; i ++ )
	{
		DEBUG_F("Arg %i = ",i); PRINT_STACKVAL(local_vars[i]); DEBUG_F("\n");
	}
	
	// Mark the start
	memset(&val1, 0, sizeof(val1));
	val1.Type = ET_FUNCTION_START;
	PUT_STACKVAL(val1);

	// Execute!
	op = Fcn->BCFcn->Operations;
	while(op)
	{
		const char	*opstr = "";
		tBC_Op	*nextop = op->Next, *jmp_target;
		ast_op = 0;
		switch(op->Operation)
		{
		case BC_OP_NOP:
			STATE_HDR();
			DEBUG_F("NOP\n");
			break;
		// Jumps
		case BC_OP_JUMP:
			STATE_HDR();
			jmp_target = Fcn->BCFcn->Labels[ OP_INDX(op) ]->Next;
			DEBUG_F("JUMP #%i %p\n", OP_INDX(op), jmp_target);
			nextop = jmp_target;
			break;
		case BC_OP_JUMPIF:
			STATE_HDR();
			jmp_target = Fcn->BCFcn->Labels[ OP_INDX(op) ]->Next;
			DEBUG_F("JUMPIF #%i %p\n", OP_INDX(op), jmp_target);
			GET_STACKVAL(val1);
			if( Bytecode_int_IsStackEntTrue(&val1) )
				nextop = jmp_target;
			break;
		case BC_OP_JUMPIFNOT:
			STATE_HDR();
			jmp_target = Fcn->BCFcn->Labels[ OP_INDX(op) ]->Next;
			DEBUG_F("JUMPIFNOT #%i %p\n", OP_INDX(op), jmp_target);
			GET_STACKVAL(val1);
			if( !Bytecode_int_IsStackEntTrue(&val1) )
				nextop = jmp_target;
			break;
		
		// Define variables
		case BC_OP_DEFINEVAR: {
			 int	type, slot;
			type = OP_INDX(op) & 0xFFFF;
			slot = OP_INDX(op) >> 16;
			if(slot < 0 || slot >= local_var_count) {
				DEBUG_F("ERROR: slot %i out of range (max %i)\n", slot, local_var_count);
				return -1;
			}
			STATE_HDR();
			DEBUG_F("DEFVAR %i of type %i\n", slot, type);
			// Clear out if the slot is reused
			if( local_vars[slot].Type != SS_DATATYPE_NOVALUE ) {
				Bytecode_int_DerefStackValue( &local_vars[slot] );
				local_vars[slot].Type = SS_DATATYPE_NOVALUE;
			}
			memset(&local_vars[slot], 0, sizeof(local_vars[0]));
			local_vars[slot].Type = type;
			} break;

		// Enter/Leave context
		// - NOP now		
		case BC_OP_ENTERCONTEXT:
			STATE_HDR();
			DEBUG_F("ENTERCONTEXT\n");
			break;
		case BC_OP_LEAVECONTEXT:
			STATE_HDR();
			DEBUG_F("LEAVECONTEXT\n");
			break;

		// Variables
		case BC_OP_LOADVAR: {
			 int	slot = OP_INDX(op);
			STATE_HDR();
			DEBUG_F("LOADVAR %i ", slot);
			if( slot < 0 || slot >= local_var_count ) {
				AST_RuntimeError(NULL, "Loading from invalid slot %i", slot);
				return -1;
			}
			DEBUG_F("("); PRINT_STACKVAL(local_vars[slot]); DEBUG_F(")\n");
			PUT_STACKVAL(local_vars[slot]);
			Bytecode_int_RefStackValue( &local_vars[slot] );
			} break;
		case BC_OP_SAVEVAR: {
			 int	slot = OP_INDX(op);
			STATE_HDR();
			DEBUG_F("SAVEVAR %i = ", slot);
			if( slot < 0 || slot >= local_var_count ) {
				AST_RuntimeError(NULL, "Loading from invalid slot %i", slot);
				return -1;
			}
			// Remove whatever was in there before
			DEBUG_F("[Deref "); PRINT_STACKVAL(local_vars[slot]); DEBUG_F("] ");
			Bytecode_int_DerefStackValue( &local_vars[slot] );
			// Place new in
			GET_STACKVAL(local_vars[slot]);
			PRINT_STACKVAL(local_vars[slot]);
			DEBUG_F("\n");
			} break;

		// Array index (get or set)
		case BC_OP_INDEX:
		case BC_OP_SETINDEX:
			STATE_HDR();
			GET_STACKVAL(val1);	// Index
			// TODO: Check that index is an integer
			if( val1.Type != SS_DATATYPE_INTEGER ) {
				nextop = NULL;
				break;
			}

			// Get array as raw spider value
			GET_STACKVAL(val2);	// Array
			pval1 = Bytecode_int_GetSpiderValue(&val2, &tmpVal1);
			Bytecode_int_DerefStackValue(&val2);

			if( op->Operation == BC_OP_SETINDEX ) {
				GET_STACKVAL(val2);
				pval2 = Bytecode_int_GetSpiderValue(&val2, NULL);
				Bytecode_int_DerefStackValue(&val2);
				
				DEBUG_F("SETINDEX %li ", val1.Integer); PRINT_STACKVAL(val2); DEBUG_F("\n");
			
				ret_val = AST_ExecuteNode_Index(Script, NULL, pval1, val1.Integer, pval2);
				if(ret_val == ERRPTR) { nextop = NULL; break; }
				SpiderScript_DereferenceValue(pval2);
			}
			else {
				DEBUG_F("INDEX %li ", val1.Integer);
				ret_val = AST_ExecuteNode_Index(Script, NULL, pval1, val1.Integer, ERRPTR);
				if(ret_val == ERRPTR) { nextop = NULL; break; }
				
				Bytecode_int_SetSpiderValue(&val1, ret_val);
				SpiderScript_DereferenceValue(ret_val);
				PUT_STACKVAL(val1);

				DEBUG_F("[Got "); PRINT_STACKVAL(val1); DEBUG_F("]\n");

			}
			// Dereference the array (or object, ...)
			if(pval1 != &tmpVal1)	SpiderScript_DereferenceValue(pval1);
			break;
		
		// Object element (get or set)
		case BC_OP_ELEMENT:
		case BC_OP_SETELEMENT:
			STATE_HDR();
			
			GET_STACKVAL(val1);
			// - Integers/Reals can't have elements :)
			if( !SS_ISTYPEOBJECT(val1.Type) ) {
				DEBUG_F("(SET)ELEMENT on non-object 0x%x\n", val1.Type);
				nextop = NULL;
				break;
			}

			pval1 = Bytecode_int_GetSpiderValue(&val1, NULL);
			Bytecode_int_DerefStackValue(&val1);

			if( op->Operation == BC_OP_SETELEMENT ) {
				GET_STACKVAL(val2);
				pval2 = Bytecode_int_GetSpiderValue(&val2, NULL);
				DEBUG_F("SETELEMENT %s ", OP_STRING(op)); PRINT_STACKVAL(val2); DEBUG_F("\n");
				Bytecode_int_DerefStackValue(&val2);

				ret_val = AST_ExecuteNode_Element(Script, NULL, pval1, OP_STRING(op), pval2);
				if(ret_val == ERRPTR) { DEBUG_F("<ERR>\n"); nextop = NULL; break; }			
				SpiderScript_DereferenceValue(pval2);
			}
			else {
				DEBUG_F("ELEMENT %s ", OP_STRING(op));
				
				ret_val = AST_ExecuteNode_Element(Script, NULL, pval1, OP_STRING(op), ERRPTR);
				if(ret_val == ERRPTR) { DEBUG_F("<ERR>\n"); nextop = NULL; break; }
	
				DEBUG_F("%p", ret_val);

				Bytecode_int_SetSpiderValue(&val2, ret_val);
				SpiderScript_DereferenceValue(ret_val);
				PUT_STACKVAL(val2);
	
				DEBUG_F("[Got "); PRINT_STACKVAL(val2); DEBUG_F("]\n");
			}
			
			SpiderScript_DereferenceValue(pval1);
			break;

		// Constants:
		case BC_OP_LOADINT:
			STATE_HDR();
			DEBUG_F("LOADINT 0x%lx\n", op->Content.Integer);
			val1.Type = SS_DATATYPE_INTEGER;
			val1.Integer = op->Content.Integer;
			PUT_STACKVAL(val1);
			break;
		case BC_OP_LOADREAL:
			STATE_HDR();
			DEBUG_F("LOADREAL %lf\n", op->Content.Real);
			val1.Type = SS_DATATYPE_REAL;
			val1.Real = op->Content.Real;
			PUT_STACKVAL(val1);
			break;
		case BC_OP_LOADSTR:
			STATE_HDR();
			DEBUG_F("LOADSTR %i \"%s\"\n", OP_INDX(op), OP_STRING(op));
			val1.Type = SS_DATATYPE_STRING;
			val1.Reference = SpiderScript_CreateString(OP_INDX(op), OP_STRING(op));
			PUT_STACKVAL(val1);
			break;
		case BC_OP_LOADNULL:
			STATE_HDR();
			DEBUG_F("LOADNULL\n");
			val1.Type = SS_DATATYPE_NOVALUE;
			val1.Reference = NULL;
			PUT_STACKVAL(val1);
			break;

		case BC_OP_CAST:
			STATE_HDR();
			val2.Type = OP_INDX(op);
			DEBUG_F("CAST to %i\n", val2.Type);
			GET_STACKVAL(val1);
			if(val1.Type == val2.Type) {
				PUT_STACKVAL(val1);
				break;
			}
			if( val2.Type == SS_DATATYPE_INTEGER && val1.Type == SS_DATATYPE_REAL ) {
				val2.Integer = val1.Real;
			}
			else if( val2.Type == SS_DATATYPE_REAL && val2.Type == SS_DATATYPE_INTEGER ) {
				val2.Real = val1.Integer;
			}
			else {
				pval1 = Bytecode_int_GetSpiderValue(&val1, &tmpVal1);
				pval2 = SpiderScript_CastValueTo(val2.Type, pval1);
				
				Bytecode_int_SetSpiderValue(&val2, pval2);
				SpiderScript_DereferenceValue(pval2);
				
				if(pval1 != &tmpVal1)	SpiderScript_DereferenceValue(pval1);
				Bytecode_int_DerefStackValue(&val1);
//				printf("CAST (%x->%x) - Original %i references remaining\n",
//					pval1->Type, OP_INDX(op),
//					pval1->ReferenceCount);
			}
			PUT_STACKVAL(val2);
			break;

		case BC_OP_DUPSTACK:
			STATE_HDR();
			DEBUG_F("DUPSTACK ");
			GET_STACKVAL(val1);
			PRINT_STACKVAL(val1);
			DEBUG_F("\n");
			PUT_STACKVAL(val1);
			PUT_STACKVAL(val1);
			Bytecode_int_RefStackValue(&val1);
			break;

		// Discard the top item from the stack
		case BC_OP_DELSTACK:
			STATE_HDR();
			DEBUG_F("DELSTACK\n");
			GET_STACKVAL(val1);
			break;

		// Unary Operations
		case BC_OP_LOGICNOT:
			STATE_HDR();
			DEBUG_F("LOGICNOT\n");
			
			GET_STACKVAL(val1);
			val2.Type = SS_DATATYPE_INTEGER;
			val2.Integer = !Bytecode_int_IsStackEntTrue(&val1);
			Bytecode_int_StackPush(Stack, &val2);
			Bytecode_int_DerefStackValue(&val1);
			break;
		
		case BC_OP_BITNOT:
			if(!ast_op)	ast_op = NODETYPE_BWNOT,	opstr = "BITNOT";
		case BC_OP_NEG:
			if(!ast_op)	ast_op = NODETYPE_NEGATE,	opstr = "NEG";

			STATE_HDR();
			DEBUG_F("%s\n", opstr);

			GET_STACKVAL(val1);
			pval1 = Bytecode_int_GetSpiderValue(&val1, &tmpVal1);
			Bytecode_int_DerefStackValue(&val1);			

			ret_val = AST_ExecuteNode_UniOp(Script, NULL, ast_op, pval1);
			if(pval1 != &tmpVal1)	SpiderScript_DereferenceValue(pval1);
			Bytecode_int_SetSpiderValue(&val1, ret_val);
			if(ret_val != &tmpVal1)	SpiderScript_DereferenceValue(ret_val);
			Bytecode_int_StackPush(Stack, &val1);
			
			break;

		// Binary Operations
		case BC_OP_LOGICAND:
			if(!ast_op)	ast_op = NODETYPE_LOGICALAND,	opstr = "LOGICAND";
		case BC_OP_LOGICOR:
			if(!ast_op)	ast_op = NODETYPE_LOGICALOR,	opstr = "LOGICOR";
		case BC_OP_LOGICXOR:
			if(!ast_op)	ast_op = NODETYPE_LOGICALXOR,	opstr = "LOGICXOR";
	
			STATE_HDR();
			DEBUG_F("%s\n", opstr);

			GET_STACKVAL(val1);
			GET_STACKVAL(val2);
			
			switch(op->Operation)
			{
			case BC_OP_LOGICAND:
				i = Bytecode_int_IsStackEntTrue(&val1) && Bytecode_int_IsStackEntTrue(&val2);
				break;
			case BC_OP_LOGICOR:
				i = Bytecode_int_IsStackEntTrue(&val1) || Bytecode_int_IsStackEntTrue(&val2);
				break;
			case BC_OP_LOGICXOR:
				i = Bytecode_int_IsStackEntTrue(&val1) ^ Bytecode_int_IsStackEntTrue(&val2);
				break;
			}
			Bytecode_int_DerefStackValue(&val1);
			Bytecode_int_DerefStackValue(&val2);

			val1.Type = SS_DATATYPE_INTEGER;
			val1.Integer = i;
			Bytecode_int_StackPush(Stack, &val1);
			break;

		case BC_OP_BITAND:
			if(!ast_op)	ast_op = NODETYPE_BWAND,	opstr = "BITAND";
		case BC_OP_BITOR:
			if(!ast_op)	ast_op = NODETYPE_BWOR, 	opstr = "BITOR";
		case BC_OP_BITXOR:
			if(!ast_op)	ast_op = NODETYPE_BWXOR,	opstr = "BITXOR";

		case BC_OP_BITSHIFTLEFT:
			if(!ast_op)	ast_op = NODETYPE_BITSHIFTLEFT,	opstr = "BITSHIFTLEFT";
		case BC_OP_BITSHIFTRIGHT:
			if(!ast_op)	ast_op = NODETYPE_BITSHIFTRIGHT, opstr = "BITSHIFTRIGHT";
		case BC_OP_BITROTATELEFT:
			if(!ast_op)	ast_op = NODETYPE_BITROTATELEFT, opstr = "BITROTATELEFT";

		case BC_OP_ADD:
			if(!ast_op)	ast_op = NODETYPE_ADD,	opstr = "ADD";
		case BC_OP_SUBTRACT:
			if(!ast_op)	ast_op = NODETYPE_SUBTRACT,	opstr = "SUBTRACT";
		case BC_OP_MULTIPLY:
			if(!ast_op)	ast_op = NODETYPE_MULTIPLY,	opstr = "MULTIPLY";
		case BC_OP_DIVIDE:
			if(!ast_op)	ast_op = NODETYPE_DIVIDE,	opstr = "DIVIDE";
		case BC_OP_MODULO:
			if(!ast_op)	ast_op = NODETYPE_MODULO,	opstr = "MODULO";

		case BC_OP_EQUALS:
			if(!ast_op)	ast_op = NODETYPE_EQUALS,	opstr = "EQUALS";
		case BC_OP_NOTEQUALS:
			if(!ast_op)	ast_op = NODETYPE_NOTEQUALS,	opstr = "NOTEQUALS";
		case BC_OP_LESSTHAN:
			if(!ast_op)	ast_op = NODETYPE_LESSTHAN,	opstr = "LESSTHAN";
		case BC_OP_LESSTHANOREQUAL:
			if(!ast_op)	ast_op = NODETYPE_LESSTHANEQUAL, opstr = "LESSTHANOREQUAL";
		case BC_OP_GREATERTHAN:
			if(!ast_op)	ast_op = NODETYPE_GREATERTHAN,	opstr = "GREATERTHAN";
		case BC_OP_GREATERTHANOREQUAL:
			if(!ast_op)	ast_op = NODETYPE_GREATERTHANEQUAL, opstr = "GREATERTHANOREQUAL";

			STATE_HDR();
			DEBUG_F("BINOP %i %s (bc %i)", ast_op, opstr, op->Operation);

			GET_STACKVAL(val2);	// Right
			GET_STACKVAL(val1);	// Left

			DEBUG_F(" ("); PRINT_STACKVAL(val1); DEBUG_F(")");
			DEBUG_F(" ("); PRINT_STACKVAL(val2); DEBUG_F(")\n");

			// Perform integer operations locally
			if( val1.Type == SS_DATATYPE_INTEGER && val2.Type == SS_DATATYPE_INTEGER )
			{
				if( Bytecode_int_LocalBinOp_Integer(op->Operation, &val1, &val2) ) {
					nextop = NULL;
					break;
				}
				PUT_STACKVAL(val1);
				break;
			}

			if(val1. Type == SS_DATATYPE_REAL && val2.Type == SS_DATATYPE_REAL )
			{
				if( Bytecode_int_LocalBinOp_Real(op->Operation, &val1, &val2) ) {
					nextop = NULL;
					break;
				}
				PUT_STACKVAL(val1);
				break;
			}
		
			pval1 = Bytecode_int_GetSpiderValue(&val1, &tmpVal1);
			pval2 = Bytecode_int_GetSpiderValue(&val2, &tmpVal2);
			Bytecode_int_DerefStackValue(&val1);
			Bytecode_int_DerefStackValue(&val2);

			// Hand to AST execution code
			ret_val = AST_ExecuteNode_BinOp(Script, NULL, ast_op, pval1, pval2);
			if(pval1 != &tmpVal1)	SpiderScript_DereferenceValue(pval1);
			if(pval2 != &tmpVal2)	SpiderScript_DereferenceValue(pval2);

			if(ret_val == ERRPTR) {
				AST_RuntimeError(NULL, "_BinOp returned ERRPTR");
				nextop = NULL;
				break;
			}
			Bytecode_int_SetSpiderValue(&val1, ret_val);
			if(ret_val != &tmpVal1)	SpiderScript_DereferenceValue(ret_val);
			PUT_STACKVAL(val1);
			break;

		// Functions etc
		case BC_OP_CREATEOBJ:
		case BC_OP_CALLFUNCTION:
		case BC_OP_CALLMETHOD:
			STATE_HDR();

			if( op->Operation == BC_OP_CALLFUNCTION )
			{
				tScript_Function	*fcn = NULL;
				const char	*name = OP_STRING(op);
				 int	arg_count = OP_INDX(op);
				DEBUG_F("CALL (local) %s %i args\n", name, arg_count);
				// Check current script functions (for fast call)
				for(fcn = Script->Functions; fcn; fcn = fcn->Next)
				{
					if(strcmp(name, fcn->Name) == 0) {
						break;
					}
				}
				if(fcn && fcn->BCFcn)
				{
					DEBUG_F(" - Fast call\n");
					Bytecode_int_ExecuteFunction(Script, fcn, Stack, arg_count);
					break;
				}
			}
		
			// Slower call
			if( Bytecode_int_CallExternFunction( Script, Stack, op ) ) {
				nextop = NULL;
				break;
			}
			break;

		case BC_OP_RETURN:
			STATE_HDR();

			DEBUG_F("RETURN\n");
			nextop = NULL;
			break;

		default:
			// TODO:
			STATE_HDR();
			AST_RuntimeError(NULL, "Unknown operation %i\n", op->Operation);
			nextop = NULL;
			break;
		}
		op = nextop;
	}
	
	// Clean up
	// - Delete local vars
	DEBUG_F("Nuking vars\n");
	for( i = 0; i < local_var_count; i ++ )
	{
		if( local_vars[i].Type != SS_DATATYPE_NOVALUE )
		{
			DEBUG_F("Var %i - ", i); 
			PRINT_STACKVAL(local_vars[i]);
			Bytecode_int_DerefStackValue(&local_vars[i]);
			DEBUG_F("\n");
		}
		else
			DEBUG_F("Var %i - empty\n", i);
	}
	
	// - Restore stack
	DEBUG_F("Restoring stack...\n");
	if( Stack->Entries[Stack->EntryCount - 1].Type == ET_FUNCTION_START )
		Stack->EntryCount --;
	else
	{
		 int	n_rolled = 1;
		GET_STACKVAL(val1);
		while( Stack->EntryCount && Stack->Entries[ --Stack->EntryCount ].Type != ET_FUNCTION_START )
		{
			Bytecode_int_DerefStackValue( &Stack->Entries[Stack->EntryCount] );
			n_rolled ++;
		}
		PUT_STACKVAL(val1);
		DEBUG_F("Rolled back %i entries\n", n_rolled);
	}
	
	DEBUG_F("Return 0\n");
	return 0;
}

