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
#include <stdarg.h>

#define TRACE	0

#if TRACE
# define DEBUG_F(v...)	printf(v)
# define DEBUGS1(f,a...)	printf("%s:%i - "f"\n", __func__, __LINE__,## a)
#else
# define DEBUG_F(v...)	
# define DEBUGS1(f,a...)	do{}while(0)
#endif

// === TYPES ===
typedef struct sBC_StackEnt	tBC_StackEnt;
typedef struct sBC_Stack	tBC_Stack;

#define ET_FUNCTION_START	-2

struct sBC_StackEnt
{
	tSpiderScript_DataType	Type;
	union {
		tSpiderBool	Boolean;
		tSpiderInteger	Integer;
		tSpiderReal 	Real;
		tSpiderString	*String;
		tSpiderArray	*Array;
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
 int	Bytecode_ExecuteFunction(tSpiderScript *Script, tScript_Function *Fcn,
	void *RetData, int NArgs, const int *ArgTypes, const void * const *Args);
 int	Bytecode_int_ExecuteFunction(tSpiderScript *Script, tScript_Function *Fcn, tBC_Stack *Stack, int ArgCount);

// === CODE ===
int Bytecode_int_StackPop(tSpiderScript *Script, tBC_Stack *Stack, tBC_StackEnt *Dest)
{
	if( Stack->EntryCount == 0 ) {
		SpiderScript_RuntimeError(Script, "Popping an empty stack");
		return 1;
	}
	Stack->EntryCount --;
	*Dest = Stack->Entries[Stack->EntryCount];
	return 0;
}

int Bytecode_int_StackPush(tSpiderScript *Script, tBC_Stack *Stack, tBC_StackEnt *Src)
{
	if( Src->Type == -1 ) {
		SpiderScript_RuntimeError(Script, "Pushing type -1 to stack");
		return 1;
	}
	if( Stack->EntryCount == Stack->EntrySpace )	return 1;
	Stack->Entries[Stack->EntryCount] = *Src;
	Stack->EntryCount ++;
	return 0;
}

int Bytecode_int_IsStackEntTrue(tSpiderScript *Script, tBC_StackEnt *Ent)
{
	switch(Ent->Type)
	{
	case ET_FUNCTION_START:
		SpiderScript_RuntimeError(Script, "BUG - _IsStackEntTrue on ET_FUNCTION_START");
		return -1;
	case SS_DATATYPE_NOVALUE:
		return 0;
	case SS_DATATYPE_BOOLEAN:
		return !!Ent->Boolean;
	case SS_DATATYPE_INTEGER:
		return !!Ent->Integer;
	case SS_DATATYPE_REAL:
		return !(-.5f < Ent->Real && Ent->Real < 0.5f);
	case SS_DATATYPE_STRING:
		return SpiderScript_CastValueToBool(SS_DATATYPE_STRING, Ent->String);
	default:
		if( SS_GETARRAYDEPTH(Ent->Type) )
			return SpiderScript_CastValueToBool(Ent->Type, Ent->Array);
		else if( SS_ISTYPEOBJECT(Ent->Type) )
			return SpiderScript_CastValueToBool(Ent->Type, Ent->Object);
		else {
			SpiderScript_RuntimeError(Script, "BUG - Type 0x%x unhandled in _IsStackEntTrue", Ent->Type);
			return -1;
		}
	}
}

/**
 * \brief Gets a direct-to-data pointer from a stack entry
 * \note *Ent should be valid until *Dest is unused
 */
int Bytecode_int_GetSpiderValue(tSpiderScript *Script, tBC_StackEnt *Ent, void **Dest)
{
	switch(Ent->Type)
	{
	case ET_FUNCTION_START:
		SpiderScript_RuntimeError(Script, "_GetSpiderValue on ET_FUNCTION_START");
		return -1;
	case SS_DATATYPE_NOVALUE:
		SpiderScript_RuntimeError(Script, "_GetSpiderValue on SS_DATATYPE_NOVALUE");
		return 0;
	// Direct types
	case SS_DATATYPE_BOOLEAN:
		DEBUGS1("(Boolean)%s", Ent->Boolean ? "true" : "false"); if(0)
	case SS_DATATYPE_INTEGER:
		DEBUGS1("(Integer)%"PRIi64, Ent->Integer); if(0)
	case SS_DATATYPE_REAL:
		DEBUGS1("(Real)%lf", Ent->Real);
		*Dest = &Ent->Boolean;
		break;
	case SS_DATATYPE_STRING:
		*Dest = Ent->String;
		break;
	default:
		if( SS_GETARRAYDEPTH(Ent->Type) ) {
			*Dest = Ent->Array;
		}
		else if( SS_ISTYPEOBJECT(Ent->Type) ) {
			*Dest = Ent->Object;
		}
		else {
			SpiderScript_RuntimeError(Script, "BUG - Type 0x%x unhandled in _GetSpiderValue", Ent->Type);
			return 0;
		}
		break;
	}
	return Ent->Type;
}

/**
 * \brief Sets a stack entry from a direct-to-data pointer
 */
void Bytecode_int_SetSpiderValue(tSpiderScript *Script, tBC_StackEnt *Ent, int Type, const void *Source)
{
	if(!Source) {
		Ent->Type = SS_DATATYPE_NOVALUE;
		return ;
	}
	Ent->Type = Type;
	switch(Type)
	{
	case SS_DATATYPE_BOOLEAN:
		Ent->Boolean = *(const tSpiderBool*)Source;
		break;
	case SS_DATATYPE_INTEGER:
		Ent->Integer = *(const tSpiderInteger*)Source;
		break;
	case SS_DATATYPE_REAL:
		Ent->Real = *(const tSpiderReal*)Source;
		break;
	case SS_DATATYPE_STRING:
		Ent->String = *(tSpiderString*const*)Source;
		SpiderScript_ReferenceString(Ent->String);
		break;
	
	default:
		if( SS_GETARRAYDEPTH(Type) ) {
			Ent->Array = (void*)Source;
			SpiderScript_ReferenceArray(Ent->Array);
		}
		else if( SS_ISTYPEOBJECT(Type) ) {
			Ent->Object = (void*)Source;
			SpiderScript_ReferenceObject(Ent->Object);
		}
		else {
			SpiderScript_RuntimeError(Script, "BUG - Type 0x%x unhandled in _SetSpiderValue", Type);
			Ent->Type = SS_DATATYPE_NOVALUE;
		}
		break;
	}
}

void Bytecode_int_DerefStackValue(tSpiderScript *Script, tBC_StackEnt *Ent)
{
	switch(Ent->Type)
	{
	case SS_DATATYPE_NOVALUE:
	case SS_DATATYPE_BOOLEAN:
	case SS_DATATYPE_INTEGER:
	case SS_DATATYPE_REAL:
		break;
	case SS_DATATYPE_STRING:
		SpiderScript_DereferenceString(Ent->String);
		break;
	default:
		if( SS_GETARRAYDEPTH(Ent->Type) )
			SpiderScript_DereferenceArray(Ent->Array);
		else if( SS_ISTYPEOBJECT(Ent->Type) )
			SpiderScript_DereferenceObject(Ent->Object);
		else
			SpiderScript_RuntimeError(Script, "BUG - Type 0x%x unhandled in _DerefStackValue", Ent->Type);
		break;
	}
	Ent->Type = SS_DATATYPE_NOVALUE;
}
void Bytecode_int_RefStackValue(tSpiderScript *Script, tBC_StackEnt *Ent)
{
	switch(Ent->Type)
	{
	case SS_DATATYPE_NOVALUE:
	case SS_DATATYPE_BOOLEAN:
	case SS_DATATYPE_INTEGER:
	case SS_DATATYPE_REAL:
		break;
	case SS_DATATYPE_STRING:
		SpiderScript_ReferenceString(Ent->String);
		break;
	default:
		if( SS_GETARRAYDEPTH(Ent->Type) )
			SpiderScript_ReferenceArray(Ent->Array);
		else if( SS_ISTYPEOBJECT(Ent->Type) )
			SpiderScript_ReferenceObject(Ent->Object);
		else
			SpiderScript_RuntimeError(Script, "BUG - Type 0x%x unhandled in _RefStackValue", Ent->Type);
		break;
	}
}

void Bytecode_int_PrintStackValue(tSpiderScript *Script, tBC_StackEnt *Ent)
{
	switch(Ent->Type)
	{
	case SS_DATATYPE_NOVALUE:
		printf("_NOVALUE");
		break;
	case SS_DATATYPE_BOOLEAN:
		printf("%s", (Ent->Boolean ? "true" : "false"));
		break;
	case SS_DATATYPE_INTEGER:
		printf("0x%"PRIx64, Ent->Integer);
		break;
	case SS_DATATYPE_REAL:
		printf("%lf", Ent->Real);
		break;
	case SS_DATATYPE_STRING:
		if( Ent->String )
			printf("String (%u bytes)", (unsigned int)Ent->String->Length);
		else
			printf("String (null)");
		break;
	default:
		if( SS_GETARRAYDEPTH(Ent->Type) )
			printf("Array 0x%x %p", SS_DOWNARRAY(Ent->Type), Ent->Array);
		else if( SS_ISTYPEOBJECT(Ent->Type) )
			printf("Object 0x%x %p", Ent->Type, Ent->Object);
		else
			SpiderScript_RuntimeError(Script, "BUG - Type 0x%x unhandled in _PrintStackValue", Ent->Type);
		break;
	}
}

#if TRACE
# define PRINT_STACKVAL(val)	Bytecode_int_PrintStackValue(Script, &val)
#else
# define PRINT_STACKVAL(val)
#endif

#define GET_STACKVAL(dst)	do{int _ret;if((_ret=Bytecode_int_StackPop(Script, Stack, &dst))) { \
	SpiderScript_RuntimeError(Script, "Stack pop failed, empty stack");\
	return _ret; \
}} while(0)
#define PUT_STACKVAL(src)	do{int _ret;if((_ret=Bytecode_int_StackPush(Script, Stack, &src))) { \
	SpiderScript_RuntimeError(Script, "Stack push failed, full stack");\
	return _ret; \
}} while(0)
#define DEREF_STACKVAL(val)	Bytecode_int_DerefStackValue(Script, &val)
#define OP_INDX(op_ptr)	((op_ptr)->Content.StringInt.Integer)
#define OP_STRING(op_ptr)	((op_ptr)->Content.StringInt.String)

int Bytecode_ExecuteFunction(tSpiderScript *Script, tScript_Function *Fcn,
	void *RetData, int NArguments, const int *ArgTypes, const void * const Args[]
	)
{
	const int	stack_size = 100;
	tBC_Stack	*stack;
	tBC_StackEnt	val;
	 int	i, ret;
	
	stack = malloc(sizeof(tBC_Stack) + stack_size*sizeof(tBC_StackEnt));
	stack->EntrySpace = stack_size;
	stack->EntryCount = 0;

	// Push arguments in order (so top is last arg)
	for( i = 0; i < NArguments; i ++ )
	{
		Bytecode_int_SetSpiderValue(Script, &val, ArgTypes[i], Args[i]);
		Bytecode_int_StackPush(Script, stack, &val);
	}

	// Call
	ret = Bytecode_int_ExecuteFunction(Script, Fcn, stack, NArguments);

	if( !ret && Fcn->ReturnType != SS_DATATYPE_NOVALUE ) {
		// Get return value
		if( Bytecode_int_StackPop(Script, stack, &val) ) {
			free(stack);
			return -1;
		}

		if( Fcn->ReturnType != val.Type )
		{
			SpiderScript_RuntimeError(Script, "'%s' Returned type 0x%x not 0x%x",
				Fcn->Name, val.Type, Fcn->ReturnType);
			free(stack);
			return -1;
		}
		memcpy(RetData, &val.Boolean, SpiderScript_int_GetTypeSize(val.Type));
	}
	if( !ret )
		ret = Fcn->ReturnType;
	free(stack);

	return ret;
}

/**
 * \brief Call an external function (may recurse into Bytecode_ExecuteFunction, but may not)
 */
int Bytecode_int_CallExternFunction(tSpiderScript *Script, tBC_Stack *Stack, tBC_Op *op)
{
	 int	id = op->Content.Function.ID;
	 int	arg_count = op->Content.Function.ArgCount;
	 int	i, rv = 0;
	const void	*args[arg_count];
	 int	arg_types[arg_count];
	tBC_StackEnt	val1, ret;
	tSpiderObject	*obj = NULL;

	DEBUG_F("CALL (general) 0x%x %i args\n", id, arg_count);

	// Read arguments
	for( i = 0; i < arg_count; i ++ )
	{
		int stack_ofs = (Stack->EntryCount - 1) - (arg_count-1 - i);
		// Arg 0 is at top of stack (EntryCount-1)
		arg_types[i] = Bytecode_int_GetSpiderValue(Script, &Stack->Entries[stack_ofs], (void**)&args[i]);
		if( arg_types[i] == -1 ) {
			rv = -1;
			break;
		}
		DEBUG_F("- Arg %i = 0x%x %p\n", i, arg_types[i], args[i]);
	}
	
	// Call the function etc.
	if( rv )
		;
	else if( op->Operation == BC_OP_CALLFUNCTION )
	{
		rv = SpiderScript_int_ExecuteFunction(Script, id,
			&ret.Boolean, arg_count, arg_types, args, &op->CacheEnt);
		if(rv < 0)
			SpiderScript_RuntimeError(Script, "Calling function 0x%x failed", id);
	}
	else if( op->Operation == BC_OP_CREATEOBJ )
	{
		rv = SpiderScript_int_ConstructObject(Script, id,
			&ret.Object, arg_count, arg_types, args, &op->CacheEnt);
		if(rv < 0)
			SpiderScript_RuntimeError(Script, "Creating object 0x%x failed", id);
	}
	else if( op->Operation == BC_OP_CALLMETHOD )
	{
		if( arg_count <= 0 || !SS_ISTYPEOBJECT(arg_types[0]) ) {
			SpiderScript_RuntimeError(Script, "OP_CALLMETHOD on non object");
			rv = -1;
		}
		else
		{
			obj = (void*)args[0];
			
			DEBUG_F("- Object 0x%x %p\n", arg_types[0], obj);
			if( obj ) {
				rv = SpiderScript_int_ExecuteMethod(Script, id,
					&ret.Boolean, arg_count, arg_types, args, &op->CacheEnt);
				if(rv < 0)
					SpiderScript_RuntimeError(Script, "Calling method 0x%x of 0x%x failed", id, arg_types[0]);
				
				// TODO: Should a dereference be done?
			}
			else {
				rv = -1;
				SpiderScript_RuntimeError(Script, "NULL object dereference");
			}
		}
	}
	else
	{
		SpiderScript_RuntimeError(Script, "BUG - Unknown operation for CALL/CREATEOBJ (%i)", op->Operation);
		rv = -1;
	}
	// Clean up args
	for( i = arg_count; i --; ) {
		GET_STACKVAL(val1);
		DEREF_STACKVAL(val1);
	}
	if(rv < 0) {
		return -1;
	}
	// Get and push return
	if( rv != 0 ) {
		ret.Type = rv;
		PUT_STACKVAL(ret);
	}

	return 0;
}

#define STATE_HDR()	DEBUG_F("%p %2i %02i ", op, Stack->EntryCount, op->Operation)

/**
 * \brief Execute a bytecode function with a stack
 */
int Bytecode_int_ExecuteFunction(tSpiderScript *Script, tScript_Function *Fcn, tBC_Stack *Stack, int ArgCount)
{
	 int	ast_op, i, rv;
	tBC_Op	*op;
	tBC_StackEnt	val1, val2, rval;
	 int	local_var_count = Fcn->BCFcn->MaxVariableCount;
	tBC_StackEnt	*local_vars;
	 int	type;
	void	*ptr, *ptr2;
	 int	bError = 0;

	if( ArgCount > Fcn->ArgumentCount )	return -1;
	DEBUG_F("Fcn->ArgumentCount = %i\n", Fcn->ArgumentCount);
	
	// Initialise local vars
	local_vars = malloc( sizeof(tBC_StackEnt) * local_var_count );	// Includes arguments
	for( i = 0; i < local_var_count; i ++ )
		local_vars[i].Type = SS_DATATYPE_NOVALUE;
	
	// Pop off arguments
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
			DEBUG_F("JUMPIF #%i %p - ", OP_INDX(op), jmp_target);
			GET_STACKVAL(val1);
			PRINT_STACKVAL(val1); DEBUG_F("\n");
			if( Bytecode_int_IsStackEntTrue(Script, &val1) )
				nextop = jmp_target;
			DEREF_STACKVAL(val1);
			break;
		case BC_OP_JUMPIFNOT:
			STATE_HDR();
			jmp_target = Fcn->BCFcn->Labels[ OP_INDX(op) ]->Next;
			DEBUG_F("JUMPIFNOT #%i %p - ", OP_INDX(op), jmp_target);
			GET_STACKVAL(val1);
			PRINT_STACKVAL(val1); DEBUG_F("\n");
			if( !Bytecode_int_IsStackEntTrue(Script, &val1) )
				nextop = jmp_target;
			DEREF_STACKVAL(val1);
			break;
		
		// Define variables
		case BC_OP_DEFINEVAR: {
			 int	type, slot;
			type = OP_INDX(op) & 0xFFFFFF;
			slot = OP_INDX(op) >> 24;
			if(slot < 0 || slot >= local_var_count) {
				DEBUG_F("ERROR: slot %i out of range (max %i)\n", slot, local_var_count);
				bError = 1;
				break;
			}
			STATE_HDR();
			DEBUG_F("DEFVAR %i of type 0x%x\n", slot, type);
			// Clear out if the slot is reused
			if( local_vars[slot].Type != SS_DATATYPE_NOVALUE ) {
				DEREF_STACKVAL( local_vars[slot] );
				local_vars[slot].Type = SS_DATATYPE_NOVALUE;
			}
			memset(&local_vars[slot], 0, sizeof(local_vars[0]));
			local_vars[slot].Type = type;
			} break;

		// Create an array
		case BC_OP_CREATEARRAY:
			val1.Type = OP_INDX(op);
			DEBUG_F("CREATEARRAY 0x%x ", SS_DOWNARRAY(val1.Type));
			GET_STACKVAL(val2);
			if( val2.Type != SS_DATATYPE_INTEGER ) {
				SpiderScript_RuntimeError(Script, "Array size is not integer");
				bError = 1;
				break;
			}
			// TODO: Range checks?
			DEBUG_F("[%i]", (int)val2.Integer);
			val1.Array = SpiderScript_CreateArray( SS_DOWNARRAY(val1.Type), val2.Integer );
			PUT_STACKVAL(val1);
			break;

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
				SpiderScript_RuntimeError(Script, "Loading from invalid slot %i", slot);
				bError = 1; break;
			}
			DEBUG_F("("); PRINT_STACKVAL(local_vars[slot]); DEBUG_F(")\n");
			PUT_STACKVAL(local_vars[slot]);
			Bytecode_int_RefStackValue( Script, &local_vars[slot] );
			} break;
		case BC_OP_SAVEVAR: {
			 int	slot = OP_INDX(op);
			STATE_HDR();
			DEBUG_F("SAVEVAR %i = ", slot);
			if( slot < 0 || slot >= local_var_count ) {
				SpiderScript_RuntimeError(Script, "Loading from invalid slot %i", slot);
				bError = 1; break;
			}
			// Remove whatever was in there before
			DEBUG_F("[Deref "); PRINT_STACKVAL(local_vars[slot]); DEBUG_F("] ");
			DEREF_STACKVAL( local_vars[slot] );
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
			GET_STACKVAL(val2);	// Array
			
			// Check that index is an integer
			if( val1.Type != SS_DATATYPE_INTEGER ) {
				bError = 1; break;
			}

			if( SS_GETARRAYDEPTH(val2.Type) == 0 ) {
				bError = 1;
				break;
			}

			if( op->Operation == BC_OP_SETINDEX ) {
				tSpiderArray	*array = val2.Array;
				GET_STACKVAL(val2);
				
				DEBUG_F("SETINDEX %li ", val1.Integer); PRINT_STACKVAL(val2); DEBUG_F("\n");
				type = Bytecode_int_GetSpiderValue(Script, &val2, &ptr);
				if(type < 0 ) {
					bError = 1;
					break;
				}
			
				rv = AST_ExecuteNode_Index(Script, NULL, array, val1.Integer, type, ptr);
				if( rv < 0 ) {
					bError = 1;
					break;
				}
			
				SpiderScript_DereferenceArray(array);
				DEREF_STACKVAL( val2 );
			}
			else {
				tSpiderArray	*array = val2.Array;
				DEBUG_F("INDEX %li ", val1.Integer);
				rv = AST_ExecuteNode_Index(Script, &val2.Boolean, array, val1.Integer, 0, NULL);
				if( rv < 0 ) {
					bError = 1;
					break;
				}
				val2.Type = rv;
				
				PUT_STACKVAL(val2);
				DEBUG_F("[Got "); PRINT_STACKVAL(val2); DEBUG_F("]\n");
				
				SpiderScript_DereferenceArray(array);
			}
			break;
		
		// Object element (get or set)
		case BC_OP_ELEMENT:
		case BC_OP_SETELEMENT:
			STATE_HDR();
			
			GET_STACKVAL(val1);
			// - Integers/Reals can't have elements :)
			if( !SS_ISTYPEOBJECT(val1.Type) ) {
				DEBUG_F("(SET)ELEMENT on non-object 0x%x\n", val1.Type);
				bError = 1;
				break;
			}

			if( op->Operation == BC_OP_SETELEMENT ) {
				GET_STACKVAL(val2);
				DEBUG_F("SETELEMENT %s ", OP_STRING(op)); PRINT_STACKVAL(val2); DEBUG_F("\n");
				type = Bytecode_int_GetSpiderValue(Script, &val2, &ptr);
				if( type < 0 ) { bError = 1; break; }

				AST_ExecuteNode_Element(Script, NULL, val1.Object, OP_STRING(op), type, ptr);
			}
			else {
				DEBUG_F("ELEMENT %s ", OP_STRING(op));
				
				type = AST_ExecuteNode_Element(Script, &val2.Boolean, val1.Object, OP_STRING(op), 0, NULL);
				if( type == -1 ) {
					SpiderScript_RuntimeError(Script, "Error getting element %s of %p",
						OP_STRING(op), val1.Object);
					bError = 1;
					break;
				}
				val2.Type = type;
				PUT_STACKVAL(val2);
				DEBUG_F("[Got "); PRINT_STACKVAL(val2); DEBUG_F("]\n");
			}
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
			val1.String = SpiderScript_CreateString(OP_INDX(op), OP_STRING(op));
			PUT_STACKVAL(val1);
			break;
		case BC_OP_LOADNULL:
			STATE_HDR();
			DEBUG_F("LOADNULL 0x%x\n", OP_INDX(op));
			if( SS_GETARRAYDEPTH( OP_INDX(op) ) )
				;
			else if( SS_ISTYPEOBJECT( OP_INDX(op) ) )
				;
			else if( OP_INDX(op) == SS_DATATYPE_STRING )
				;
			else {
				SpiderScript_RuntimeError(Script, "LOADNULL with non-object 0x%x", OP_INDX(op));
				bError = 1;
				break;
			}
			val1.Type = OP_INDX(op);
			val1.Integer = 0;
			PUT_STACKVAL(val1);
			break;

		case BC_OP_CAST:
			STATE_HDR();
			val2.Type = OP_INDX(op);
			GET_STACKVAL(val1);
			DEBUG_F("CAST to %i from %i\n", val2.Type, val1.Type);
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
			else
			{
				 int	type;
				type = Bytecode_int_GetSpiderValue(Script, &val1, &ptr);
				if( type < 0 ) { bError = 1; break; }
				switch(val2.Type)
				{
				case SS_DATATYPE_BOOLEAN:
					val2.Boolean = SpiderScript_CastValueToBool(type, ptr);
					break;
				case SS_DATATYPE_INTEGER:
					val2.Integer = SpiderScript_CastValueToInteger(type, ptr);
					break;
				case SS_DATATYPE_REAL:
					val2.Real = SpiderScript_CastValueToReal(type, ptr);
					break;
				case SS_DATATYPE_STRING:
					val2.String = SpiderScript_CastValueToString(type, ptr);
					break;
				default:
					SpiderScript_RuntimeError(Script, "No cast for type 0x%x", val2.Type);
					bError = 1;
					break;
				}
				DEREF_STACKVAL(val1);
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
			Bytecode_int_RefStackValue(Script, &val1);
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
			val2.Integer = !Bytecode_int_IsStackEntTrue(Script, &val1);
			PUT_STACKVAL(val2);
			DEREF_STACKVAL(val1);
			break;
		
		case BC_OP_BITNOT:
			if(!ast_op)	ast_op = NODETYPE_BWNOT,	opstr = "BITNOT";
		case BC_OP_NEG:
			if(!ast_op)	ast_op = NODETYPE_NEGATE,	opstr = "NEG";

			STATE_HDR();
			GET_STACKVAL(val1);
			DEBUG_F("%s", opstr);
			DEBUG_F(" ("); PRINT_STACKVAL(val1); DEBUG_F(")\n");

			switch(val1.Type)
			{
			case SS_DATATYPE_INTEGER:
				val1.Integer = AST_ExecuteNode_UniOp_Integer(Script, ast_op, val1.Integer);
				break;
			case SS_DATATYPE_REAL:
				val1.Real = AST_ExecuteNode_UniOp_Real(Script, ast_op, val1.Real);
				break;
			default:
				SpiderScript_RuntimeError(Script, "No _ExecuteNode_UniOp[%s] for type 0x%x", opstr, val1.Type);
				bError = 1;
				break;
			}
			PUT_STACKVAL(val1);
			break;

		// Reference comparisons
		case BC_OP_REFEQUALS:
		case BC_OP_REFNOTEQUALS:
			STATE_HDR();
			DEBUG_F("%s", (op->Operation == BC_OP_REFEQUALS) ? "REFEQUALS" : "REFNOTEQUALS");
			GET_STACKVAL(val1);
			GET_STACKVAL(val2);
			DEBUG_F(" ("); PRINT_STACKVAL(val1); DEBUG_F(")");
			DEBUG_F(" ("); PRINT_STACKVAL(val2); DEBUG_F(")");

			if( val1.Type != val2.Type ) {
				SpiderScript_RuntimeError(Script, "Type mismatch in REF(NOT)EQUALS (0x%x != 0x%x)",
					val1.Type, val2.Type);
				bError = 1;
				break;
			}

			Bytecode_int_GetSpiderValue(Script, &val1, &ptr);
			Bytecode_int_GetSpiderValue(Script, &val2, &ptr2);

			DEREF_STACKVAL(val1);
			DEREF_STACKVAL(val2);

			val1.Type = SS_DATATYPE_BOOLEAN;
			if( op->Operation == BC_OP_REFEQUALS )
				val1.Boolean = (ptr == ptr2);
			else
				val1.Boolean = (ptr != ptr2);
			DEBUG_F(" = ("); PRINT_STACKVAL(val1); DEBUG_F(")\n");

			PUT_STACKVAL(val1);
			break;

		// Binary Operations
		case BC_OP_LOGICAND:
			DEBUG_F("LOGICAND\n");	if(0)
		case BC_OP_LOGICOR:
			DEBUG_F("LOGICOR\n");	if(0)
		case BC_OP_LOGICXOR:
			DEBUG_F("LOGICXOR\n");
	
			STATE_HDR();

			GET_STACKVAL(val1);
			GET_STACKVAL(val2);
			
			switch(op->Operation)
			{
			case BC_OP_LOGICAND:
				i = Bytecode_int_IsStackEntTrue(Script, &val1) && Bytecode_int_IsStackEntTrue(Script, &val2);
				break;
			case BC_OP_LOGICOR:
				i = Bytecode_int_IsStackEntTrue(Script, &val1) || Bytecode_int_IsStackEntTrue(Script, &val2);
				break;
			case BC_OP_LOGICXOR:
				i = Bytecode_int_IsStackEntTrue(Script, &val1) ^ Bytecode_int_IsStackEntTrue(Script, &val2);
				break;
			}
			DEREF_STACKVAL(val1);
			DEREF_STACKVAL(val2);

			val1.Type = SS_DATATYPE_BOOLEAN;
			val1.Boolean = i;
			PUT_STACKVAL(val1);
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

			Bytecode_int_GetSpiderValue(Script, &val2, &ptr);
			switch( val1.Type )
			{
			case SS_DATATYPE_INTEGER:
				type = AST_ExecuteNode_BinOp_Integer(Script,
					&rval.Boolean, ast_op, val1.Integer, val2.Type, ptr);
				break;
			case SS_DATATYPE_REAL:
				type = AST_ExecuteNode_BinOp_Real(Script,
					&rval.Boolean, ast_op, val1.Real, val2.Type, ptr);
				break;
			case SS_DATATYPE_STRING:
				type = AST_ExecuteNode_BinOp_String(Script,
					&rval.Boolean, ast_op, val1.String, val2.Type, ptr);
				break;
			default:
				SpiderScript_RuntimeError(Script, "No _ExecuteNode_BinOp[%s] for type 0x%x", opstr, val1.Type);
				type = -1;
				break;
			}
			if( type == -1 ) {
				SpiderScript_RuntimeError(Script, "_ExecuteNode_BinOp[%s] for type 0x%x returned -1",
					opstr, val1.Type);
				bError = 1;
				break;
			}
			DEREF_STACKVAL(val1);
			DEREF_STACKVAL(val2);
			rval.Type = type;
			DEBUG_F(" = ("); PRINT_STACKVAL(rval); DEBUG_F(")\n");
			PUT_STACKVAL(rval);
			break;

		// Functions etc
		case BC_OP_CREATEOBJ:
		case BC_OP_CALLFUNCTION:
		case BC_OP_CALLMETHOD:
			STATE_HDR();

			if( op->Operation == BC_OP_CALLFUNCTION )
			{
				tScript_Function	*fcn = NULL;
				 int	id = op->Content.Function.ID;
				 int	arg_count = op->Content.Function.ArgCount;
				// Check current script functions (for fast call)
				if( (id >> 16) == 0 ) {
					DEBUG_F("CALL (local) 0x%x %i args\n", id, arg_count);
					for(fcn = Script->Functions; fcn && id --; fcn = fcn->Next)
						;
					if( !fcn ) {
						SpiderScript_RuntimeError(Script, "Function ID #%i is invalid", id);
						bError = 1;
						break;
					}
					if( !fcn->BCFcn ) {
						SpiderScript_RuntimeError(Script, "Function #%i %s is not compiled", id, fcn->Name);
						bError = 1;
						break;
					}
					Bytecode_int_ExecuteFunction(Script, fcn, Stack, arg_count);
					break ;
				}
			}
		
			// Slower call
			if( Bytecode_int_CallExternFunction( Script, Stack, op ) ) {
				bError = 1;
				break;
			}
			break;

		case BC_OP_RETURN:
			STATE_HDR();

			DEBUG_F("RETURN\n");
			nextop = NULL;	// non-error stop
			break;

		default:
			// TODO:
			STATE_HDR();
			SpiderScript_RuntimeError(Script, "Unknown operation %i\n", op->Operation);
			bError = 1;
			break;
		}
		if( bError )
			break ;
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
			DEREF_STACKVAL(local_vars[i]);
			DEBUG_F("\n");
		}
		else
			DEBUG_F("Var %i - empty\n", i);
	}
	
	// - Restore stack
	DEBUG_F("Restoring stack...\n");
	if( Stack->EntryCount == 0 )
		;
	else if( Stack->Entries[Stack->EntryCount - 1].Type == ET_FUNCTION_START )
		Stack->EntryCount --;
	else
	{
		 int	n_rolled = 1;
		GET_STACKVAL(val1);
		while( Stack->EntryCount && Stack->Entries[ --Stack->EntryCount ].Type != ET_FUNCTION_START )
		{
			DEREF_STACKVAL( Stack->Entries[Stack->EntryCount] );
			n_rolled ++;
		}
		PUT_STACKVAL(val1);
		DEBUG_F("Rolled back %i entries\n", n_rolled);
	}

	free(local_vars);

	DEBUG_F("Return %i\n", bError);
	return bError;
}

