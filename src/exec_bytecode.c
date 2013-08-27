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

// 1: Opcode trace
// 2: Stack trace
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
	tSpiderTypeRef	Type;
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
	void *RetData, int NArgs, const tSpiderTypeRef *ArgTypes, const void * const *Args);
 int	Bytecode_int_ExecuteFunction(tSpiderScript *Script, tScript_Function *Fcn, int ArgCount, const tBC_StackEnt *Args[], tBC_StackEnt *RetVal);

// === CONSTANTS ===
#define TYPE_VOID	((tSpiderTypeRef){0,0})
#define TYPE_STRING	((tSpiderTypeRef){.ArrayDepth=0,.Def=&gSpiderScript_StringType})
#define TYPE_REAL	((tSpiderTypeRef){.ArrayDepth=0,.Def=&gSpiderScript_RealType})
#define TYPE_INTEGER	((tSpiderTypeRef){.ArrayDepth=0,.Def=&gSpiderScript_IntegerType})
#define TYPE_BOOLEAN	((tSpiderTypeRef){.ArrayDepth=0,.Def=&gSpiderScript_BoolType})

// === GLOBALS ===
tSpiderScript_TypeDef	gBytecodeFcnStart;

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
//	if( Src->Type == -1 ) {
//		SpiderScript_RuntimeError(Script, "Pushing type -1 to stack");
//		return 1;
//	}
	if( Stack->EntryCount == Stack->EntrySpace )	return 1;
	Stack->Entries[Stack->EntryCount] = *Src;
	Stack->EntryCount ++;
	return 0;
}

int Bytecode_int_IsStackEntTrue(tSpiderScript *Script, tBC_StackEnt *Ent)
{
	if( Ent->Type.Def == NULL ) {
		SpiderScript_RuntimeError(Script, "_IsStackEntTrue on void");
		return -1;
	}
	if(Ent->Type.Def == &gBytecodeFcnStart) {
		SpiderScript_RuntimeError(Script, "_IsStackEntTrue on ET_FUNCTION_START");
		return -1;
	}
	if( SS_GETARRAYDEPTH(Ent->Type) )
		return SpiderScript_CastValueToBool(Ent->Type, Ent->Array);
	else if( SS_ISTYPEOBJECT(Ent->Type) )
		return SpiderScript_CastValueToBool(Ent->Type, Ent->Object);
	else if( Ent->Type.Def->Class == SS_TYPECLASS_CORE ) {
		switch(Ent->Type.Def->Core)
		{
		case SS_DATATYPE_BOOLEAN:
			return !!Ent->Boolean;
		case SS_DATATYPE_INTEGER:
			return !!Ent->Integer;
		case SS_DATATYPE_REAL:
			return !(-.5f < Ent->Real && Ent->Real < 0.5f);
		case SS_DATATYPE_STRING:
			return SpiderScript_CastValueToBool(
				(tSpiderTypeRef){&gSpiderScript_IntegerType,0}, Ent->String
				);
		default:
			break;
		}
	}
	SpiderScript_RuntimeError(Script, "_IsStackEntTrue on unknown type %s",
		SpiderScript_GetTypeName(Script, Ent->Type));
	return -1;
}

/**
 * \brief Gets a direct-to-data pointer from a stack entry
 * \note *Ent should be valid until *Dest is unused
 */
tSpiderTypeRef Bytecode_int_GetSpiderValue(tSpiderScript *Script, tBC_StackEnt *Ent, void **Dest)
{
	if( Ent->Type.Def == NULL ) {
		SpiderScript_RuntimeError(Script, "_GetSpiderValue on SS_DATATYPE_NOVALUE");
		return Ent->Type;
	}
	if(Ent->Type.Def == &gBytecodeFcnStart) {
		tSpiderTypeRef	ret = {0,0};
		SpiderScript_RuntimeError(Script, "_GetSpiderValue on ET_FUNCTION_START");
		*(int*)0 = 0;
		return ret;
	}
	if( Ent->Type.ArrayDepth ) {
		*Dest = Ent->Array;
	}
	else if( Ent->Type.Def->Class != SS_TYPECLASS_CORE ) {
		// Objects/other
		*Dest = Ent->Object;
	}
	else {
		switch(Ent->Type.Def->Core)
		{
		// Direct types
		case SS_DATATYPE_BOOLEAN:
			//DEBUGS1("(Boolean)%s", Ent->Boolean ? "true" : "false"); if(0)
		case SS_DATATYPE_INTEGER:
			//DEBUGS1("(Integer)%"PRIi64, Ent->Integer); if(0)
		case SS_DATATYPE_REAL:
			//DEBUGS1("(Real)%lf", Ent->Real);
			*Dest = &Ent->Boolean;
			break;
		case SS_DATATYPE_STRING:
			*Dest = Ent->String;
			break;
		default:
			SpiderScript_RuntimeError(Script, "BUG - Type %s unhandled in _GetSpiderValue",
				SpiderScript_GetTypeName(Script, Ent->Type));
			return (tSpiderTypeRef){0,0};
		}
	}
	return Ent->Type;
}

tSpiderTypeRef Bytecode_int_GetSpiderValueC(tSpiderScript *Script, const tBC_StackEnt *Ent, const void **Dest)
{
	return Bytecode_int_GetSpiderValue(Script, (tBC_StackEnt*)Ent, (void**)Dest);
}

/**
 * \brief Sets a stack entry from a direct-to-data pointer
 */
void Bytecode_int_SetFromSpiderValue(tSpiderScript *Script, tBC_StackEnt *Ent, tSpiderTypeRef Type, const void *Source)
{
	if( Type.Def == NULL ) {
		SpiderScript_RuntimeError(Script, "_SetSpiderValue with void");
		Ent->Type.Def = NULL;
		return ;
	}
	Ent->Type = Type;
	if( SS_GETARRAYDEPTH(Type) ) {
		Ent->Array = (void*)Source;
		SpiderScript_ReferenceArray(Ent->Array);
		return ;
	}
	if( SS_ISTYPEOBJECT(Type) ) {
		Ent->Object = (void*)Source;
		SpiderScript_ReferenceObject(Ent->Object);
		return ;
	}

	if( Type.Def->Class != SS_TYPECLASS_CORE ) {
		SpiderScript_RuntimeError(Script, "BUG - Type %s unhandled in _SetSpiderValue",
			SpiderScript_GetTypeName(Script, Type));
		Ent->Type.Def = NULL;
		return ;
	}
	switch(Type.Def->Core)
	{
	case SS_DATATYPE_BOOLEAN:
		if(!Source)
			return ;
		Ent->Boolean = *(const tSpiderBool*)Source;
		break;
	case SS_DATATYPE_INTEGER:
		if(!Source)
			return ;
		Ent->Integer = *(const tSpiderInteger*)Source;
		break;
	case SS_DATATYPE_REAL:
		if(!Source)
			return ;
		Ent->Real = *(const tSpiderReal*)Source;
		break;
	case SS_DATATYPE_STRING:
		Ent->String = (void*)Source;
		SpiderScript_ReferenceString(Ent->String);
		break;
	
	default:
		SpiderScript_RuntimeError(Script, "BUG - Type %s unhandled in _SetSpiderValue",
			SpiderScript_GetTypeName(Script, Type));
		Ent->Type.Def = NULL;
		break;
	}
}

void Bytecode_int_DerefStackValue(tSpiderScript *Script, tBC_StackEnt *Ent)
{
	if( SS_GETARRAYDEPTH(Ent->Type) )
		SpiderScript_DereferenceArray(Ent->Array);
	else if( SS_ISTYPEOBJECT(Ent->Type) )
		SpiderScript_DereferenceObject(Ent->Object);
	else if( SS_ISCORETYPE(Ent->Type, SS_DATATYPE_STRING) )
		SpiderScript_DereferenceString(Ent->String);
	else {
	}
	Ent->Type = (tSpiderTypeRef){0,0};
}
void Bytecode_int_RefStackValue(tSpiderScript *Script, tBC_StackEnt *Ent)
{
	if( SS_GETARRAYDEPTH(Ent->Type) )
		SpiderScript_ReferenceArray(Ent->Array);
	else if( SS_ISTYPEOBJECT(Ent->Type) )
		SpiderScript_ReferenceObject(Ent->Object);
	else if( SS_ISCORETYPE(Ent->Type, SS_DATATYPE_STRING) )
		SpiderScript_ReferenceString(Ent->String);
	else {
	}
}

void Bytecode_int_PrintStackValue(tSpiderScript *Script, tBC_StackEnt *Ent)
{
	if( SS_GETARRAYDEPTH(Ent->Type) )
		printf("Array %s %p", SpiderScript_GetTypeName(Script, Ent->Type), Ent->Array);
	else if( SS_ISTYPEOBJECT(Ent->Type) )
		printf("Object %s %p", SpiderScript_GetTypeName(Script, Ent->Type), Ent->Object);
	else if( Ent->Type.Def == NULL )
		printf("_NOVALUE");
	else if( Ent->Type.Def->Class != SS_TYPECLASS_CORE )
		printf("UNKCLASS(%i)", Ent->Type.Def->Class);
	else {
		switch(Ent->Type.Def->Core)
		{
		case SS_DATATYPE_NOVALUE:
			printf("void");
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
			SpiderScript_RuntimeError(Script, "BUG - Type %s unhandled in _PrintStackValue",
				SpiderScript_GetTypeName(Script, Ent->Type));
			break;
		}
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
#define _BC_ASSERTTYPE(have, exp) \
	if(!SS_TYPESEQUAL(have,exp)) {\
		SpiderScript_RuntimeError(Script, "Type mismatch expected %s, got %s", \
			SpiderScript_GetTypeName(Script, exp),\
			 SpiderScript_GetTypeName(Script, have) \
			); \
		bError = 1; \
		break ; \
	}
#define OP_REG2(op_ptr)	((op_ptr)->Content.RegInt.RegInt2)
#define OP_REG3(op_ptr)	((op_ptr)->Content.RegInt.RegInt3)

int Bytecode_ExecuteFunction(tSpiderScript *Script, tScript_Function *Fcn,
	void *RetData, int NArguments, const tSpiderTypeRef *ArgTypes, const void * const Args[]
	)
{
	tBC_StackEnt	args[NArguments];
	const tBC_StackEnt	*argps[NArguments];
	// Push arguments in order (so top is last arg)
	for( int i = 0; i < NArguments; i ++ )
	{
		Bytecode_int_SetFromSpiderValue(Script, &args[i], ArgTypes[i], Args[i]);
		argps[i] = &args[i];
	}

	tBC_StackEnt	retval;
	// Call
	int ret = Bytecode_int_ExecuteFunction(Script, Fcn, NArguments, argps, &retval);

	if( ret == 0 && Fcn->ReturnType.Def != NULL )
	{
		if( !SS_TYPESEQUAL(Fcn->ReturnType, retval.Type) )
		{
			SpiderScript_RuntimeError(Script, "'%s' returned type %s not stated %s",
				Fcn->Name,
				SpiderScript_GetTypeName(Script, retval.Type),
				SpiderScript_GetTypeName(Script, Fcn->ReturnType)
				);
			return -1;
		}
		DEBUG_F("# Return "); PRINT_STACKVAL(retval); DEBUG_F("\n");
		if( SS_ISTYPEREFERENCE(retval.Type) )
			*(void**)RetData = retval.String;	// Or object, or array
		else
			memcpy(RetData, &retval.Boolean, SpiderScript_int_GetTypeSize(retval.Type));
	}
	if(ret != 0)
	{
		// TODO: Handle exception RV
		ret = -1;
	}

	return ret;
}

/**
 * \brief Call an external function (may recurse into Bytecode_ExecuteFunction, but may not)
 */
int Bytecode_int_CallExternFunction(tSpiderScript *Script, tBC_Op *op, const tBC_StackEnt *Args[], tBC_StackEnt *RV)
{
	 int	id = op->Content.Function.ID;
	 int	arg_count = op->Content.Function.ArgCount;
	 int	rv = 0;
	const void	*args[arg_count];
	tSpiderTypeRef	arg_types[arg_count];
	tSpiderTypeRef	rettype = {0,0};
	tBC_StackEnt	ret;
	tSpiderObject	*obj = NULL;

	DEBUG_F("CALL (general) 0x%x %i args\n", id, arg_count);

	// Read arguments
	for( int i = 0; i < arg_count; i ++ )
	{
		// Arg 0 is at top of stack (EntryCount-1)
		arg_types[i] = Bytecode_int_GetSpiderValueC(Script, Args[i], &args[i]);
		if( arg_types[i].Def == NULL ) {
			rv = -1;
			SpiderScript_RuntimeError(Script, "Argument %i popped void", i);
			goto cleanup;
		}
		DEBUG_F("- Arg %i = ", i);
		PRINT_STACKVAL(*Args[i]);
		DEBUG_F("\n");
	}
	
	// Call the function etc.
	if( op->Operation == BC_OP_CALLFUNCTION )
	{
		rv = SpiderScript_int_ExecuteFunction(Script, id, &rettype,
			&ret.Boolean, arg_count, arg_types, args, &op->CacheEnt);
		if(rv < 0)
			SpiderScript_RuntimeError(Script, "Calling function 0x%x failed", id);
	}
	else if( op->Operation == BC_OP_CREATEOBJ )
	{
		if( 0 > id || id >= Script->BCTypeCount ) {
			SpiderScript_RuntimeError(Script, "Type index out of range %i (0..%i)",
				id, Script->BCTypeCount);
			rv = -1;
			goto cleanup;
		}
		rettype = Script->BCTypes[id];
		if(rettype.ArrayDepth || !SS_ISTYPEOBJECT(rettype)) {
			SpiderScript_RuntimeError(Script, "CREATEOBJ with non-object type %s",
				SpiderScript_GetTypeName(Script, rettype));
			rv = -1;
			goto cleanup;
		}
		
		rv = SpiderScript_int_ConstructObject(Script, rettype.Def, &ret.Object,
			arg_count, arg_types, args, &op->CacheEnt);
		if(rv < 0)
			SpiderScript_RuntimeError(Script, "Creating object %s failed",
				SpiderScript_GetTypeName(Script, rettype));
	}
	else if( op->Operation == BC_OP_CALLMETHOD )
	{
		if( arg_count <= 0 || !SS_ISTYPEOBJECT(arg_types[0]) ) {
			SpiderScript_RuntimeError(Script, "OP_CALLMETHOD(%i)+%i on non object (%s)",
				id, arg_count, SpiderScript_GetTypeName(Script, arg_types[0]));
			rv = -1;
		}
		else
		{
			obj = (void*)args[0];
			
			DEBUG_F("- Object %s %p\n", SpiderScript_GetTypeName(Script, arg_types[0]), obj);
			if( obj ) {
				rv = SpiderScript_int_ExecuteMethod(Script, id, &rettype,
					&ret.Boolean, arg_count, arg_types, args, &op->CacheEnt);
				if(rv < 0)
					SpiderScript_RuntimeError(Script, "Calling method 0x%x of %s failed",
						id, SpiderScript_GetTypeName(Script, arg_types[0]));
				
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
cleanup:
	if(rv < 0) {
		return -1;
	}
	// Get and push return
	if( rettype.Def ) {
		ret.Type = rettype;
		*RV = ret;
		DEBUG_F("- Return value "); PRINT_STACKVAL(ret); DEBUG_F("\n");
	}

	return 0;
}

#define STATE_HDR()	do { \
	op_valid = 1; \
	DEBUG_F("%p %2i %02i ", op, Stack->EntryCount, op->Operation);\
} while(0)

#define REG(idx)	(registers[idx])

/**
 * \brief Execute a bytecode function with a stack
 */
int Bytecode_int_ExecuteFunction(tSpiderScript *Script, tScript_Function *Fcn, int ArgCount, const tBC_StackEnt *Args[], tBC_StackEnt *RetVal)
{
	 int	ast_op, i, rv;
	tBC_Op	*op;
	const int	imp_global_count = Fcn->BCFcn->MaxGlobalCount;
	const int	num_registers = 512;
	tSpiderTypeRef	type;
	void	*ptr;
	 int	bError = 0;
	 int	itype;
	tBC_StackEnt	*reg_dst, *reg1, *reg2;

	if( ArgCount > Fcn->ArgumentCount )	return -1;
	DEBUG_F("--- ExecuteFunction %s (%i args)\n", Fcn->Name, Fcn->ArgumentCount);
	
	// Initialise stack and globals
	tBC_StackEnt	registers[num_registers];
	memset(registers, 0, sizeof(registers));
	tScript_Var *globals[imp_global_count];
	memset(globals, 0, sizeof(globals));
	
	// Pop off arguments
	// - Handle optional arguments
	for( i = Fcn->ArgumentCount; i > ArgCount; )
	{
		i --;
		registers[i].Integer = 0;
		registers[i].Type = Fcn->Arguments[i].Type;
	}
	for( ; i --; )
	{
		registers[i] = *Args[i];
		// TODO: Type checks / enforcing
	}
	for( i = 0; i < Fcn->ArgumentCount; i ++ )
	{
		DEBUG_F("Arg %i = ",i); PRINT_STACKVAL(registers[i]); DEBUG_F("\n");
	}

	// Execute!
	op = Fcn->BCFcn->Operations;
	while(op)
	{
		 int	op_valid = 0;
		#if TRACE >= 2
		for( int i = 0; i < Stack->EntryCount; i ++ )
		{
			DEBUG_F("Stack[%i] = ", i); PRINT_STACKVAL(Stack->Entries[i]); DEBUG_F("\n");
		}
		#endif
		reg_dst = &REG(op->DstReg);
		reg1 = &REG(OP_REG2(op));
		reg2 = &REG(OP_REG3(op));
		
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
			jmp_target = Fcn->BCFcn->Labels[ op->DstReg ]->Next;
			DEBUG_F("JUMP #%i %p\n", op->DstReg, jmp_target);
			nextop = jmp_target;
			break;
		case BC_OP_JUMPIF:
			STATE_HDR();
			jmp_target = Fcn->BCFcn->Labels[ op->DstReg ]->Next;
			DEBUG_F("JUMPIF #%i %p - ", op->DstReg, jmp_target);
			PRINT_STACKVAL(*reg1); DEBUG_F("\n");
			if( Bytecode_int_IsStackEntTrue(Script, reg1) )
				nextop = jmp_target;
			break;
		case BC_OP_JUMPIFNOT:
			STATE_HDR();
			jmp_target = Fcn->BCFcn->Labels[ op->DstReg ]->Next;
			DEBUG_F("JUMPIFNOT #%i %p - ", op->DstReg, jmp_target);
			PRINT_STACKVAL(*reg1); DEBUG_F("\n");
			if( !Bytecode_int_IsStackEntTrue(Script, reg1) )
				nextop = jmp_target;
			break;

		case BC_OP_IMPORTGLOBAL: {
			int slot = op->DstReg;
			if(slot < 0 || slot >= imp_global_count ) {
				SpiderScript_RuntimeError(Script, "Global slot %i out of 0..%i",
					slot, imp_global_count);
				bError = 1;
				break;
			}

			globals[slot] = NULL;
			const char *name = op->Content.String.Data;
			for( tScript_Var *v = Script->FirstGlobal; v; v = v->Next )
			{
				if( strcmp(v->Name, name) == 0 ) {
					globals[slot] = v;
					break;
				}
			}
			if( !globals[slot] ) {
				SpiderScript_RuntimeError(Script, "Reference to undefined global variable '%s'",
					name);
				bError = 1;
				break;
			}

			} break;

		case BC_OP_TAGREGISTER:
			break;

		// Create an array
		case BC_OP_CREATEARRAY:
			i = OP_REG2(op);
			if( i < 0 || i >= Script->BCTypeCount ) {
				SpiderScript_RuntimeError(Script, "Type index out of range (%i >= %i)",
					i, Script->BCTypeCount);
				bError = 1;
				break;
			}
			reg_dst->Type = Script->BCTypes[i];
			if( reg_dst->Type.ArrayDepth == 0 ) {
				SpiderScript_RuntimeError(Script, "Invalid type when creating an array");
				bError = 1;
				break;
			}
			reg_dst->Type.ArrayDepth --;
			DEBUG_F("CREATEARRAY %s ", SpiderScript_GetTypeName(Script, reg_dst->Type));
			if( !SS_ISCORETYPE(reg2->Type, SS_DATATYPE_INTEGER) ) {
				SpiderScript_RuntimeError(Script, "Array size is not integer");
				bError = 1;
				break;
			}
			// TODO: Range checks?
			DEBUG_F("[%i]", (int)reg2->Integer);
			reg_dst->Array = SpiderScript_CreateArray(reg_dst->Type, reg2->Integer );
			reg_dst->Type.ArrayDepth ++;
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
		case BC_OP_GETGLOBAL: {
			 int	slot = OP_REG2(op);
			STATE_HDR();
			DEBUG_F("GETGLOBAL 0x%x ", slot);
			if( slot >= imp_global_count ) {
				SpiderScript_RuntimeError(Script, "Loading from invalid slot %i (%i max) - %s",
					slot, imp_global_count, Fcn->Name);
				bError = 1; break;
			}
			if( !globals[slot] ) {
				SpiderScript_RuntimeError(Script, "Global slot %i empty", slot);
				bError = 1; break;
			}
			
			Bytecode_int_SetFromSpiderValue(Script, reg_dst, globals[slot]->Type, globals[slot]->Ptr);
			} break;
		case BC_OP_SETGLOBAL: {
			 int	slot = OP_REG2(op);
			STATE_HDR();
			
			DEBUG_F("SETGLOBAL 0x%i = ", slot);
			if( slot >= imp_global_count ) {
				SpiderScript_RuntimeError(Script, "Loading from invalid slot %i (%i max) - %s",
					slot, imp_global_count, Fcn->Name);
				bError = 1; break;
			}
			if( !globals[slot] ) {
				SpiderScript_RuntimeError(Script, "Global slot %i empty", slot);
				bError = 1; break;
			}
		
			// Deref existing
			{
				tBC_StackEnt	val1;
				Bytecode_int_SetFromSpiderValue(Script, &val1,
					globals[slot]->Type, globals[slot]->Ptr);
				DEREF_STACKVAL(val1);	// _SetSpiderValue
				DEREF_STACKVAL(val1);	// Whatever was there before
			}
			
			if( !SS_TYPESEQUAL(reg_dst->Type, globals[slot]->Type) ) {
				SpiderScript_RuntimeError(Script,
					"Saving to global, types don't match (%s != %s)",
					SpiderScript_GetTypeName(Script, reg_dst->Type),
					SpiderScript_GetTypeName(Script, globals[slot]->Type)
					);
				bError = 1;
				break;
			}
			Bytecode_int_GetSpiderValue(Script, reg_dst, &globals[slot]->Ptr);
			} break;

		// Array index (get or set)
		case BC_OP_GETINDEX:
		case BC_OP_SETINDEX:
			STATE_HDR();
			
			// Check that index is an integer
			if( !SS_ISCORETYPE(reg2->Type, SS_DATATYPE_INTEGER) ) {
				SpiderScript_RuntimeError(Script, "Array index is not an integer");
				bError = 1;
				break;
			}

			if( SS_GETARRAYDEPTH(reg1->Type) == 0 ) {
				SpiderScript_RuntimeError(Script, "Indexing non-array (%s)",
					SpiderScript_GetTypeName(Script, reg1->Type));
				bError = 1;
				break;
			}

			if( op->Operation == BC_OP_SETINDEX )
			{
				tSpiderArray	*array = reg1->Array;
				
				DEBUG_F("SETINDEX %li ", reg2->Integer); PRINT_STACKVAL(*dst_reg); DEBUG_F("\n");
				type = Bytecode_int_GetSpiderValue(Script, reg_dst, &ptr);
				if(type.Def == NULL ) { bError = 1; break; }
			
				rv = AST_ExecuteNode_Index(Script, NULL, array, reg2->Integer, type, ptr);
				if( rv < 0 ) { bError = 1; break; }
			}
			else {
				tSpiderArray	*array = reg1->Array;
				DEBUG_F("INDEX %li ", reg2->Integer);
				rv = AST_ExecuteNode_Index(Script, &reg_dst->Boolean, array, reg2->Integer,
					TYPE_VOID, NULL);
				if( rv < 0 ) { bError = 1; break; }
				reg_dst->Type = reg1->Type;
				reg_dst->Type.ArrayDepth --;
				
				DEBUG_F("[Got "); PRINT_STACKVAL(*reg_dst); DEBUG_F("]\n");
			}
			break;
		
		// Object element (get or set)
		case BC_OP_GETELEMENT:
		case BC_OP_SETELEMENT:
			STATE_HDR();
			
			// - Integers/Reals can't have elements :)
			if( !SS_ISTYPEOBJECT(reg2->Type) ) {
				DEBUG_F("(SET)ELEMENT on non-object %s\n",
					SpiderScript_GetTypeName(Script, reg2->Type));
				bError = 1;
				break;
			}

			if( op->Operation == BC_OP_SETELEMENT )
			{
				DEBUG_F("SETELEMENT %i ", OP_REG2(op)); PRINT_STACKVAL(*dst_reg); DEBUG_F("\n");
				type = Bytecode_int_GetSpiderValue(Script, reg_dst, &ptr);
				if( type.Def == NULL ) { bError = 1; break; }

				AST_ExecuteNode_Element(Script, NULL, reg1->Object, OP_REG3(op), type, ptr);
			}
			else {
				DEBUG_F("ELEMENT %s ", OP_REG2(op));
				
				type = AST_ExecuteNode_Element(Script, &reg_dst->Boolean,
					reg1->Object, OP_REG3(op), TYPE_VOID, NULL);
				if( type.Def == NULL ) {
					SpiderScript_RuntimeError(Script, "Error getting element %i of %p",
						OP_REG3(op), reg1->Object);
					bError = 1;
					break;
				}
				reg_dst->Type = type;
				DEBUG_F("[Got "); PRINT_STACKVAL(*reg_dst); DEBUG_F("]\n");
			}
			break;

		// Constants:
		case BC_OP_LOADINT:
			STATE_HDR();
			DEBUG_F("LOADINT 0x%lx\n", op->Content.Integer);
			REG(op->DstReg).Type = TYPE_INTEGER;
			REG(op->DstReg).Integer = op->Content.Integer;
			break;
		case BC_OP_LOADREAL:
			STATE_HDR();
			DEBUG_F("LOADREAL %lf\n", op->Content.Real);
			REG(op->DstReg).Type = TYPE_REAL;
			REG(op->DstReg).Real = op->Content.Real;
			break;
		case BC_OP_LOADSTRING:
			STATE_HDR();
			DEBUG_F("LOADSTR %i \"%s\"\n", OP_INDX(op), OP_STRING(op));
			REG(op->DstReg).Type = TYPE_STRING;
			REG(op->DstReg).String = SpiderScript_CreateString(
				op->Content.String.Length, op->Content.String.Data);
			break;
		case BC_OP_LOADNULLREF:
			STATE_HDR();
			type = Script->BCTypes[OP_REG2(op)];
			DEBUG_F("LOADNULL %s\n", SpiderScript_GetTypeName(Script, type));
			if( SS_ISTYPEREFERENCE( type ) )
				;
			else {
				SpiderScript_RuntimeError(Script, "LOADNULL with non-object %s",
					SpiderScript_GetTypeName(Script, type));
				bError = 1;
				break;
			}
			reg_dst->Type = type;
			reg_dst->String = NULL;
			break;

		case BC_OP_MOV:
			DEBUG_F("MOV R%i := R%i\n", op->DstReg, OP_REG2(op));
			*reg_dst = *reg1;
			break;

		case BC_OP_CAST:
			STATE_HDR();
			itype = OP_REG2(op);
			reg_dst->Type.ArrayDepth = 0;
			reg_dst->Type.Def = SpiderScript_GetCoreType(itype);
			DEBUG_F("CAST to %s from %s\n",
				SpiderScript_GetTypeName(Script, reg_dst->Type),
				SpiderScript_GetTypeName(Script, reg1->Type)
				);
			if( SS_TYPESEQUAL(reg_dst->Type, reg1->Type) ) {
				// Warn?
				memcpy(reg_dst, reg1, sizeof(*reg_dst));
				break;
			}
			if( itype == SS_DATATYPE_INTEGER && SS_ISCORETYPE(reg1->Type, SS_DATATYPE_REAL) ) {
				reg_dst->Integer = reg1->Real;
			}
			else if( itype == SS_DATATYPE_REAL && SS_ISCORETYPE(reg1->Type, SS_DATATYPE_INTEGER) ) {
				reg_dst->Real = reg1->Integer;
			}
			else
			{
				tSpiderTypeRef	type;
				type = Bytecode_int_GetSpiderValue(Script, reg1, &ptr);
				if( type.Def == NULL ) { bError = 1; break; }
				switch(itype)
				{
				case SS_DATATYPE_BOOLEAN:
					reg_dst->Boolean = SpiderScript_CastValueToBool(type, ptr);
					break;
				case SS_DATATYPE_INTEGER:
					reg_dst->Integer = SpiderScript_CastValueToInteger(type, ptr);
					break;
				case SS_DATATYPE_REAL:
					reg_dst->Real = SpiderScript_CastValueToReal(type, ptr);
					break;
				case SS_DATATYPE_STRING:
					reg_dst->String = SpiderScript_CastValueToString(type, ptr);
					break;
				default:
					SpiderScript_RuntimeError(Script, "No cast for type %s",
						SpiderScript_GetTypeName(Script, reg_dst->Type));
					bError = 1;
					break;
				}
			}
			break;

		// Unary Operations
		case BC_OP_BOOL_LOGICNOT:
			STATE_HDR();
			DEBUG_F("BC_OP_BOOL_LOGICNOT R%i := R%i", op->DstReg, OP_REG2(op));
			reg_dst = &REG(op->DstReg);
			reg1 = &REG(OP_REG2(op));
			reg_dst->Type = TYPE_BOOLEAN;
			reg_dst->Boolean = !Bytecode_int_IsStackEntTrue(Script, reg1);
			break;
		
		case BC_OP_INT_BITNOT:
			STATE_HDR();
			DEBUG_F("BC_OP_INT_BITNOT R%i := R%i", op->DstReg, OP_REG2(op));
			reg_dst = &REG(op->DstReg);
			reg1 = &REG(OP_REG2(op));
			_BC_ASSERTTYPE(reg1->Type, TYPE_INTEGER);
			
			reg_dst->Type = TYPE_INTEGER;
			reg_dst->Integer = ~reg1->Integer;
			break;
		case BC_OP_INT_NEG:
			STATE_HDR();
			DEBUG_F("BC_OP_INT_NEG R%i := R%i", op->DstReg, OP_REG2(op));
			reg_dst = &REG(op->DstReg);
			reg1 = &REG(OP_REG2(op));
			_BC_ASSERTTYPE(reg1->Type, TYPE_INTEGER);
			
			reg_dst->Type = TYPE_INTEGER;
			reg_dst->Integer = -reg1->Integer;
			break;
		
		case BC_OP_REAL_NEG:
			STATE_HDR();
			DEBUG_F("BC_OP_REAL_NEG R%i := R%i", op->DstReg, OP_REG2(op));
			reg_dst = &REG(op->DstReg);
			reg1 = &REG(OP_REG2(op));
			_BC_ASSERTTYPE(reg1->Type, TYPE_REAL);
			
			reg_dst->Type = TYPE_REAL;
			reg_dst->Real = -reg1->Real;
			break;

#define BINOPHDR(opcode) \
		case opcode: \
			STATE_HDR();\
			DEBUG_F(#opcode "R%i := R%i, R%i", op->DstReg, OP_REG2(op), OP_REG3(op));
#define BINOPHDR_TYPE(opcode, srctype, dsttype) \
			BINOPHDR(opcode) \
			_BC_ASSERTTYPE(reg1->Type, srctype);\
			_BC_ASSERTTYPE(reg2->Type, srctype);\
			reg_dst->Type = dsttype;
#define BINOP(opcode, opr, srctype, dsttype) \
			BINOPHDR_TYPE(opcode, srctype, dsttype)\
			reg_dst->Integer = reg1->Integer opr reg2->Integer; \
			break;

		// Reference comparisons
		BINOPHDR(BC_OP_REFEQ)
			if( !SS_TYPESEQUAL(reg1->Type, reg2->Type) ) {
				SpiderScript_RuntimeError(Script, "Type mismatch in REFEQ (%s != %s)",
					reg1->Type, reg2->Type);
				bError = 1;
				break;
			}
			reg_dst->Type = TYPE_BOOLEAN;
			reg_dst->Boolean = reg1->String == reg2->String;
			break;
		BINOPHDR(BC_OP_REFNEQ)
			if( !SS_TYPESEQUAL(reg1->Type, reg2->Type) ) {
				SpiderScript_RuntimeError(Script, "Type mismatch in REFNEQ (%s != %s)",
					reg1->Type, reg2->Type);
				bError = 1;
				break;
			}
			reg_dst->Type = TYPE_BOOLEAN;
			reg_dst->Boolean = reg1->String != reg2->String;
			break;
	
		BINOPHDR(BC_OP_BOOL_LOGICAND)
			reg_dst->Type = TYPE_BOOLEAN;
			reg_dst->Boolean = Bytecode_int_IsStackEntTrue(Script, reg1)
				&& Bytecode_int_IsStackEntTrue(Script, reg2);
			break;
		BINOPHDR(BC_OP_BOOL_LOGICOR)
			reg_dst->Type = TYPE_BOOLEAN;
			reg_dst->Boolean = Bytecode_int_IsStackEntTrue(Script, reg1)
				|| Bytecode_int_IsStackEntTrue(Script, reg2);
			break;
		BINOPHDR(BC_OP_BOOL_LOGICXOR)
			reg_dst->Type = TYPE_BOOLEAN;
			reg_dst->Boolean = Bytecode_int_IsStackEntTrue(Script, reg1)
				!= Bytecode_int_IsStackEntTrue(Script, reg2);
			break;
		
		BINOP(BC_OP_INT_BITAND, &, TYPE_INTEGER, TYPE_INTEGER)
		BINOP(BC_OP_INT_BITOR,  |, TYPE_INTEGER, TYPE_INTEGER)
		BINOP(BC_OP_INT_BITXOR, ^, TYPE_INTEGER, TYPE_INTEGER)
				
		BINOP(BC_OP_INT_ADD,      +, TYPE_INTEGER, TYPE_INTEGER)
		BINOP(BC_OP_INT_SUBTRACT, -, TYPE_INTEGER, TYPE_INTEGER)
		BINOP(BC_OP_INT_MULTIPLY, *, TYPE_INTEGER, TYPE_INTEGER)
		BINOP(BC_OP_INT_DIVIDE,   /, TYPE_INTEGER, TYPE_INTEGER)
		BINOP(BC_OP_INT_MODULO,   %, TYPE_INTEGER, TYPE_INTEGER)

		BINOP(BC_OP_INT_BITSHIFTLEFT,  <<, TYPE_INTEGER, TYPE_INTEGER)
		BINOP(BC_OP_INT_BITSHIFTRIGHT, >>, TYPE_INTEGER, TYPE_INTEGER)
		
		BINOPHDR_TYPE(BC_OP_INT_BITROTATELEFT, TYPE_INTEGER, TYPE_INTEGER)
			reg_dst->Integer = (reg1->Integer << reg2->Integer) | (reg1->Integer >> (64-reg2->Integer));
			break;
		
		BINOP(BC_OP_INT_EQUALS,       ==, TYPE_INTEGER, TYPE_BOOLEAN)
		BINOP(BC_OP_INT_NOTEQUALS,    !=, TYPE_INTEGER, TYPE_BOOLEAN)
		BINOP(BC_OP_INT_LESSTHAN,     < , TYPE_INTEGER, TYPE_BOOLEAN)
		BINOP(BC_OP_INT_LESSTHANEQ,   <=, TYPE_INTEGER, TYPE_BOOLEAN)
		BINOP(BC_OP_INT_GREATERTHAN,  > , TYPE_INTEGER, TYPE_BOOLEAN)
		BINOP(BC_OP_INT_GREATERTHANEQ,>=, TYPE_INTEGER, TYPE_BOOLEAN)
		
		BINOP(BC_OP_REAL_ADD,      +, TYPE_REAL, TYPE_REAL)
		BINOP(BC_OP_REAL_SUBTRACT, -, TYPE_REAL, TYPE_REAL)
		BINOP(BC_OP_REAL_MULTIPLY, *, TYPE_REAL, TYPE_REAL)
		BINOP(BC_OP_REAL_DIVIDE,   /, TYPE_REAL, TYPE_REAL)
	
		BINOP(BC_OP_REAL_EQUALS,       ==, TYPE_REAL, TYPE_BOOLEAN)
		BINOP(BC_OP_REAL_NOTEQUALS,    !=, TYPE_REAL, TYPE_BOOLEAN)
		BINOP(BC_OP_REAL_LESSTHAN,     < , TYPE_REAL, TYPE_BOOLEAN)
		BINOP(BC_OP_REAL_LESSTHANEQ,   <=, TYPE_REAL, TYPE_BOOLEAN)
		BINOP(BC_OP_REAL_GREATERTHAN,  > , TYPE_REAL, TYPE_BOOLEAN)
		BINOP(BC_OP_REAL_GREATERTHANEQ,>=, TYPE_REAL, TYPE_BOOLEAN)

#undef BINOP

		case BC_OP_STR_EQUALS:
			if(!ast_op)	ast_op = NODETYPE_EQUALS,	opstr = "EQUALS";
		case BC_OP_STR_NOTEQUALS:
			if(!ast_op)	ast_op = NODETYPE_NOTEQUALS,	opstr = "NOTEQUALS";
		case BC_OP_STR_LESSTHAN:
			if(!ast_op)	ast_op = NODETYPE_LESSTHAN,	opstr = "LESSTHAN";
		case BC_OP_STR_LESSTHANEQ:
			if(!ast_op)	ast_op = NODETYPE_LESSTHANEQUAL, opstr = "LESSTHANOREQUAL";
		case BC_OP_STR_GREATERTHAN:
			if(!ast_op)	ast_op = NODETYPE_GREATERTHAN,	opstr = "GREATERTHAN";
		case BC_OP_STR_GREATERTHANEQ:
			if(!ast_op)	ast_op = NODETYPE_GREATERTHANEQUAL, opstr = "GREATERTHANOREQUAL";
		case BC_OP_STR_ADD:
			if(!ast_op)	ast_op = NODETYPE_ADD, opstr = "ADD";

			STATE_HDR();
			DEBUG_F("STR BINOP %i %s (bc %i)", ast_op, opstr, op->Operation);
			
			_BC_ASSERTTYPE(reg1->Type, TYPE_STRING);

			DEBUG_F(" ("); PRINT_STACKVAL(*reg1); DEBUG_F(")");
			DEBUG_F(" ("); PRINT_STACKVAL(*reg2); DEBUG_F(")\n");

			Bytecode_int_GetSpiderValue(Script, reg1, &ptr);
			itype = AST_ExecuteNode_BinOp_String(Script, &reg_dst->Boolean, ast_op,
				reg1->String, reg2->Type.Def->Core, &reg2->Boolean);
			if( itype == -1 ) {
				SpiderScript_RuntimeError(Script,
					"_ExecuteNode_BinOp[%s] for types %s<op>%s returned -1",
					opstr,
					SpiderScript_GetTypeName(Script, reg1->Type),
					SpiderScript_GetTypeName(Script, reg2->Type));
				bError = 1;
				break;
			}
			reg_dst->Type.ArrayDepth = 0;
			reg_dst->Type.Def = SpiderScript_GetCoreType(itype);
			DEBUG_F(" = ("); PRINT_STACKVAL(&reg_dst); DEBUG_F(")\n");
			break;

		// Functions etc
		case BC_OP_CREATEOBJ:
		case BC_OP_CALLFUNCTION:
		case BC_OP_CALLMETHOD: {
			STATE_HDR();
			
			tScript_Function	*fcn = NULL;
			 int	id = op->Content.Function.ID;
			 int	arg_count = op->Content.Function.ArgCount;
			
			if( arg_count >= 1 )
				reg1 = &REG( op->Content.Function.ArgRegs[0] );
			else
				reg1 = NULL;

			if( op->Operation == BC_OP_CALLFUNCTION && (id >> 16) == 0 )
			{
				// Check current script functions (for fast call)
				DEBUG_F("CALL (local) 0x%x %i args\n", id, arg_count);
				for(fcn = Script->Functions; fcn && id --; fcn = fcn->Next)
					;
				if( !fcn ) {
					SpiderScript_RuntimeError(Script,
						"Function ID #%i is invalid", id);
					bError = 1;
					break;
				}
			}
			else if( op->Operation == BC_OP_CALLMETHOD
				&& reg1->Type.Def && reg1->Type.Def->Class == SS_TYPECLASS_SCLASS )
			{
				DEBUG_F("MCALL (local) %s 0x%x %i args\n",
					SpiderScript_GetTypeName(Script, reg1->Type), id, arg_count);
				if( id >= reg1->Type.Def->SClass->nFunctions ) {
					SpiderScript_RuntimeError(Script,
						"Method #%i of %s is invalid", id,
						SpiderScript_GetTypeName(Script, reg1->Type));
					bError = 1;
					break;
				}
				fcn = reg1->Type.Def->SClass->Functions[id];
			}
			else
			{
				fcn = NULL;
			}

			const tBC_StackEnt	*args[arg_count];
			for(int i = 0; i < arg_count; i ++ ) {
				args[i] = &REG( op->Content.Function.ArgRegs[i] );
			}
			
			// Either a local call, or a remote call
			if( fcn )
			{
				if( !fcn->BCFcn ) {
					SpiderScript_RuntimeError(Script,
						"Function #%i %s is not compiled", id, fcn->Name);
					bError = 1;
					break;
				}
				rv = Bytecode_int_ExecuteFunction(Script, fcn, arg_count, args, reg_dst);
				if( rv ) {
					bError = 1;
				}
			}
			else
			{
				if( Bytecode_int_CallExternFunction( Script, op, args, reg_dst ) ) {
					bError = 1;
					break;
				}
			}
			break; }

		case BC_OP_RETURN:
			STATE_HDR();
	
			if( RetVal ) {
				Bytecode_int_RefStackValue(Script, reg_dst);
				*RetVal = *reg_dst;
			}

			DEBUG_F("RETURN\n");
			nextop = NULL;	// non-error stop
			break;
	
		case BC_OP_EXCEPTION_PUSH:
		case BC_OP_EXCEPTION_CHECK:
		case BC_OP_EXCEPTION_POP:
			break;
		}

		if( !op_valid )
		{
			// TODO:
			STATE_HDR();
			SpiderScript_RuntimeError(Script, "Unknown operation %i\n", op->Operation);
			bError = 1;
		}
		// TODO: Handle exceptions by allowing a script to push/pop exception handlers
		if( bError )
			break ;
		op = nextop;
	}
	
	// Clean up
	DEBUG_F("> Cleaning up\n");
	// - Restore stack
	for( int i = 0; i < num_registers; i ++ )
	{
		DEREF_STACKVAL( registers[i] );
	}

	DEBUG_F("--- Return %i\n", bError);
	return bError;
}

