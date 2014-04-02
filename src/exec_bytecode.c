/*
 * SpiderScript Library
 * by John Hodge (thePowersGang)
 * 
 * exec_bytecode.c
 * - Execute bytecode
 */
#define DEBUG	0
#include <stdlib.h>
#include <stdint.h>
#include "common.h"
#include "bytecode.h"
#include <stdio.h>
#include <string.h>
#include "ast.h"
#include <inttypes.h>
#include <stdarg.h>


#define DEREF_BEFORE_SET	1

// Values for BytecodeTraceLevel
// 1: Opcode trace
// 2: Register trace
#define TRACECOND(code)	do{ if(Script->BytecodeTraceLevel>=SS_TRACE_OPCODES){code;} }while(0)
#define DEBUG_F(v...)	TRACECOND(printf(v))

#define TODO(str)	SpiderScript_RuntimeError(Script, "TODO: Impliment bytecode"str); bError = 1; break

// === TYPES ===
typedef struct sBC_StackEnt	tBC_StackEnt;

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
// === CODE ===
int Bytecode_int_IsStackEntTrue(tSpiderScript *Script, tBC_StackEnt *Ent)
{
	if( Ent->Type.Def == NULL ) {
		SpiderScript_RuntimeError(Script, "_IsStackEntTrue on void");
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
			return SpiderScript_CastValueToBool(TYPE_STRING, Ent->String);
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

void Bytecode_int_ReferenceValue(tSpiderTypeRef Type, void *Ptr)
{
	if( Type.Def == NULL ) {
	}
	else if( SS_GETARRAYDEPTH(Type) ) {
		SpiderScript_ReferenceArray(Ptr);
	}
	else if( SS_ISTYPEOBJECT(Type) ) {
		SpiderScript_ReferenceObject(Ptr);
	}
	else if( SS_ISCORETYPE(Type, SS_DATATYPE_STRING) ) {
		SpiderScript_ReferenceString(Ptr);
	}
	else {
		// Primitive type
	}
}
void Bytecode_int_DereferenceValue(tSpiderTypeRef Type, void *Ptr)
{
	if( Type.Def == NULL ) {
	}
	else if( SS_GETARRAYDEPTH(Type) ) {
		SpiderScript_DereferenceArray(Ptr);
	}
	else if( SS_ISTYPEOBJECT(Type) ) {
		SpiderScript_DereferenceObject(Ptr);
	}
	else if( SS_ISCORETYPE(Type, SS_DATATYPE_STRING) ) {
		SpiderScript_DereferenceString(Ptr);
	}
	else {
		// Primitive type
	}
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
	if( SS_ISTYPEREFERENCE(Type) ) {
		Ent->Object = (void*)Source;
		Bytecode_int_ReferenceValue(Type, (void*)Source);
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

static int Bytecode_int_PrintEscapedString(size_t len, const char *src)
{
	for( size_t i = 0; i < len; i ++ )
	{
		switch(src[i])
		{
		case '\n':	printf("\\n");	break;
		case '\r':	printf("\\r");	break;
		case '"':	printf("\"");	break;
		case '\t':	printf("\t");	break;
		default:
			if(src[i] < ' ')
				printf("\\0%o", src[i]);
			else
				printf("%c", src[i]);
			break;
		}
	}
	return 0;
}

void Bytecode_int_PrintStackValue(tSpiderScript *Script, const tBC_StackEnt *Ent)
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
			if( Ent->String ) {
				printf("String (%zu \"", Ent->String->Length);
				Bytecode_int_PrintEscapedString( Ent->String->Length, Ent->String->Data );
				printf("\")");
			}
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

#define PRINT_STACKVAL(val)	TRACECOND(Bytecode_int_PrintStackValue(Script, &val))
#define PRINT_STR(len, ptr)	TRACECOND(Bytecode_int_PrintEscapedString(len, ptr))

// TODO: Emit notice if a reference type is lost?
#if DEREF_BEFORE_SET
# define PRESET_DEREF(reg)	Bytecode_int_DerefStackValue(Script, &reg)
#else
# define PRESET_DEREF(reg)	do{}while(0)
#endif

#define DEREF_STACKVAL(val)	Bytecode_int_DerefStackValue(Script, &val)
#define REF_STACKVAL(val)	Bytecode_int_RefStackValue(Script, &val)
#define _BC_ASSERTTYPE(have, exp, name) \
	if(!SS_TYPESEQUAL(have,exp)) {\
		SpiderScript_RuntimeError(Script, "Type mismatch expected %s, got %s for "name, \
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

	// Clean up
	for( int i = 0; i < NArguments; i ++ )
		Bytecode_int_DerefStackValue(Script, &args[i]);

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
int Bytecode_int_CallExternFunction(tSpiderScript *Script, tBC_Op *op, const int arg_count, const tBC_StackEnt *Args[], tBC_StackEnt *RV)
{
	 int	id = op->Content.Function.ID;
	 int	rv = 0;
	const void	*args[arg_count];
	tSpiderTypeRef	arg_types[arg_count];
	tSpiderTypeRef	rettype = {0,0};
	tBC_StackEnt	ret;
	tSpiderObject	*obj = NULL;

	// Read arguments
	for( int i = 0; i < arg_count; i ++ )
	{
		DEBUG_F("- Arg %i = ", i);
		// Arg 0 is at top of stack (EntryCount-1)
		arg_types[i] = Bytecode_int_GetSpiderValueC(Script, Args[i], &args[i]);
		if( arg_types[i].Def == NULL ) {
			rv = -1;
			DEBUG_F("VOID\n");
			SpiderScript_RuntimeError(Script, "Argument %i popped void", i);
			goto cleanup;
		}
		PRINT_STACKVAL(*Args[i]);
		DEBUG_F("\n");
	}
	
	// Call the function etc.
	if( op->Operation == BC_OP_CALLFUNCTION )
	{
		rv = SpiderScript_int_ExecuteFunction(Script, id, &rettype,
			&ret.Boolean, arg_count, arg_types, args, &op->CacheEnt);
		if(rv < 0 && SpiderScript_GetException(Script,NULL)!=SS_EXCEPTION_FORCEEXIT)
			SpiderScript_RuntimeError(Script, "Calling function %s failed",
				SpiderScript_int_GetFunctionName(Script, id)
				);
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
		if(rv < 0 && SpiderScript_GetException(Script,NULL)!=SS_EXCEPTION_FORCEEXIT)
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
				if(rv < 0 && SpiderScript_GetException(Script,NULL)!=SS_EXCEPTION_FORCEEXIT)
					SpiderScript_RuntimeError(Script, "Calling method %s->%s failed",
						SpiderScript_GetTypeName(Script, arg_types[0]),
						SpiderScript_int_GetMethodName(Script, arg_types[0], id)
						);
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
	DEBUG_F("%p %02i ", op, op->Operation);\
} while(0)

#define REG(idx)	(registers[idx])

/**
 * \brief Execute a bytecode function with a stack
 */
int Bytecode_int_ExecuteFunction(tSpiderScript *Script, tScript_Function *Fcn, int ArgCount, const tBC_StackEnt *Args[], tBC_StackEnt *RetVal)
{
	static const int MAX_REGISTERS = 100;
	static const int MAX_GLOBALS = 16;
	 int	ast_op, i, rv;
	tBC_Op	*op;
	const int	imp_global_count = Fcn->BCFcn->MaxGlobalCount;
	const int	num_registers = Fcn->BCFcn->MaxRegisters;
	tSpiderTypeRef	type;
	void	*ptr;
	 int	bError = 0, function_offset = 0;
	const char	*last_file = NULL;
	 int	last_line = 0;
	 int	itype;
	tBC_StackEnt	*reg_dst, *reg1, *reg2;
	tBC_StackEnt	tmp_ent;

	if( num_registers > MAX_REGISTERS ) {
		SpiderScript_RuntimeError(Script, "Function requested %i registers, %i max",
			num_registers, MAX_REGISTERS);
		return -1;
	}
	if( imp_global_count > MAX_GLOBALS ) {
		SpiderScript_RuntimeError(Script, "Function requested %i globals, %i max",
			imp_global_count, MAX_GLOBALS);
		return -1;
	}
	

	if( ArgCount < Fcn->ArgumentCount || (!Fcn->IsVariable && ArgCount != Fcn->ArgumentCount) ) {
		return SpiderScript_ThrowException_ArgCount(Script, Fcn->Name,
			(Fcn->IsVariable ? -Fcn->ArgumentCount : Fcn->ArgumentCount), ArgCount);
	}
	const int	VArgC = ArgCount - Fcn->ArgumentCount;
	const tBC_StackEnt	**VArgs = Args + Fcn->ArgumentCount;
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
		REF_STACKVAL(registers[i]);
	}

	// Execute!
	op = Fcn->BCFcn->Operations;
	while(op)
	{
		 int	op_valid = 0;
		if( Script->BytecodeTraceLevel >= SS_TRACE_REGDUMP )
		{
			for( int i = 0; i < num_registers; i ++ )
			{
				if( registers[i].Type.Def )
					DEBUG_F("R%i = ", i); PRINT_STACKVAL(registers[i]); DEBUG_F("\n");
			}
		}

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
		case BC_OP_NOTEPOSITION:
			STATE_HDR();
			DEBUG_F("NOTEPOSITION %s:%i\n", op->Content.RefStr->Data, op->DstReg);
			last_file = op->Content.RefStr->Data;
			last_line = op->DstReg;
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
			DEBUG_F("JUMPIF #%i R%i - ", op->DstReg, OP_REG2(op));
			PRINT_STACKVAL(*reg1); DEBUG_F("\n");
			if( Bytecode_int_IsStackEntTrue(Script, reg1) )
				nextop = jmp_target;
			break;
		case BC_OP_JUMPIFNOT:
			STATE_HDR();
			jmp_target = Fcn->BCFcn->Labels[ op->DstReg ]->Next;
			DEBUG_F("JUMPIFNOT #%i R%i - ", op->DstReg, OP_REG2(op));
			PRINT_STACKVAL(*reg1); DEBUG_F("\n");
			if( !Bytecode_int_IsStackEntTrue(Script, reg1) )
				nextop = jmp_target;
			break;

		case BC_OP_IMPORTGLOBAL: {
			STATE_HDR();
			DEBUG_F("IMPORTGLOBAL #%i '%s'\n", op->DstReg,
				op->Content.String.Data);
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
			STATE_HDR();
			DEBUG_F("TAGREGISTER %i %s\n", op->DstReg, op->Content.String.Data);
			break;

		// Create an array
		case BC_OP_CREATEARRAY:
			STATE_HDR();
			i = OP_REG2(op);
			if( i < 0 || i >= Script->BCTypeCount ) {
				SpiderScript_RuntimeError(Script, "Type index out of range (%i >= %i)",
					i, Script->BCTypeCount);
				bError = 1;
				break;
			}
			
			PRESET_DEREF(*reg_dst);
			reg_dst->Type = Script->BCTypes[i];
			if( reg_dst->Type.ArrayDepth == 0 ) {
				SpiderScript_RuntimeError(Script, "Invalid type when creating an array");
				bError = 1;
				break;
			}
			reg_dst->Type.ArrayDepth --;
			DEBUG_F("CREATEARRAY R%i = %s ",
				op->DstReg,
				SpiderScript_GetTypeName(Script, reg_dst->Type));
			if( !SS_ISCORETYPE(reg2->Type, SS_DATATYPE_INTEGER) ) {
				SpiderScript_RuntimeError(Script, "Array size is not integer");
				bError = 1;
				break;
			}
			// TODO: Range checks?
			DEBUG_F("[%i]", (int)reg2->Integer);
			if( reg2->Integer < 0 ) {
				SpiderScript_ThrowException(Script, SS_EXCEPTION_INDEX_OOB,
					"Array size is <0 (%i)", (int)reg2->Integer);
				bError = 1;
				break;
			}
			reg_dst->Array = SpiderScript_CreateArray(reg_dst->Type, reg2->Integer );
			reg_dst->Type.ArrayDepth ++;
			DEBUG_F("\n");
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
			if( slot >= imp_global_count ) {
				SpiderScript_RuntimeError(Script, "Global slot %i invalid (%i slots) - '%s'",
					slot, imp_global_count, Fcn->Name);
				bError = 1; break;
			}
			if( !globals[slot] ) {
				SpiderScript_RuntimeError(Script, "Global slot %i empty", slot);
				bError = 1; break;
			}
			DEBUG_F("GETGLOBAL R%i = #%i %s [%p=", op->DstReg, slot, globals[slot]->Name,
				globals[slot]->Ptr);
			
			PRESET_DEREF(*reg_dst);
			Bytecode_int_SetFromSpiderValue(Script, reg_dst, globals[slot]->Type, globals[slot]->Ptr);
			PRINT_STACKVAL(*reg_dst);
			DEBUG_F("]\n");
			} break;
		case BC_OP_SETGLOBAL: {
			 int	slot = OP_REG2(op);
			STATE_HDR();
			
			if( slot >= imp_global_count ) {
				SpiderScript_RuntimeError(Script, "Loading from invalid slot %i (%i max) - %s",
					slot, imp_global_count, Fcn->Name);
				bError = 1; break;
			}
			if( !globals[slot] ) {
				SpiderScript_RuntimeError(Script, "Global slot %i empty", slot);
				bError = 1; break;
			}
			DEBUG_F("SETGLOBAL #%i %s = R%i ", slot, globals[slot]->Name, op->DstReg);
			PRINT_STACKVAL(*reg_dst);
			DEBUG_F("\n");
		
			if( !SS_TYPESEQUAL(reg_dst->Type, globals[slot]->Type) ) {
				SpiderScript_RuntimeError(Script,
					"Saving to global, types don't match (src %s dst %s)",
					SpiderScript_GetTypeName(Script, reg_dst->Type),
					SpiderScript_GetTypeName(Script, globals[slot]->Type)
					);
				bError = 1;
				break;
			}
			
			// Deref existing
			Bytecode_int_DereferenceValue(globals[slot]->Type, globals[slot]->Ptr);
			Bytecode_int_RefStackValue(Script, reg_dst);
			if( SS_ISTYPEREFERENCE(globals[slot]->Type) )
				globals[slot]->Ptr = reg_dst->String;
			else {
				void *ptr = globals[slot]->Ptr;
				switch(globals[slot]->Type.Def->Core)
				{
				case SS_DATATYPE_REAL:    *(tSpiderReal*)ptr    = reg_dst->Real;	break;
				case SS_DATATYPE_INTEGER: *(tSpiderInteger*)ptr = reg_dst->Integer;	break;
				case SS_DATATYPE_BOOLEAN: *(tSpiderBool*)ptr    = reg_dst->Integer;	break;
				default:
					SpiderScript_RuntimeError(Script,
						"Saving to global, unhandled type %s",
						SpiderScript_GetTypeName(Script, globals[slot]->Type)
						);
					bError = 1;
					break;
				}
			}
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
				
				DEBUG_F("SETINDEX R%i [ R%i=%li ] = R%i ",
					OP_REG2(op), OP_REG3(op),
					reg2->Integer, op->DstReg);
				PRINT_STACKVAL(*reg_dst);
				DEBUG_F("\n");
				type = Bytecode_int_GetSpiderValue(Script, reg_dst, &ptr);
				if(type.Def == NULL ) { bError = 1; break; }
			
				rv = AST_ExecuteNode_Index(Script, NULL, array, reg2->Integer, type, ptr);
				if( rv < 0 ) { bError = 1; break; }
			}
			else {
				tSpiderArray	*array = reg1->Array;
				DEBUG_F("INDEX R%i = %li ", op->DstReg, reg2->Integer);
				PRESET_DEREF(*reg_dst);
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
			STATE_HDR();
			DEBUG_F("GETELEMENT R%i = R%i->#%i [", op->DstReg, OP_REG2(op), OP_REG3(op));
			// - Core types can't have elements :)
			if( !SS_ISTYPEOBJECT(reg1->Type) ) {
				SpiderScript_RuntimeError(Script, "GETELEMENT on non-object %s\n",
					SpiderScript_GetTypeName(Script, reg1->Type));
				bError = 1;
				break;
			}
			PRESET_DEREF(*reg_dst);
			type = AST_ExecuteNode_Element(Script, &reg_dst->Boolean,
				reg1->Object, OP_REG3(op), TYPE_VOID, NULL);
			if( type.Def == NULL ) {
				SpiderScript_RuntimeError(Script, "Error getting element %i of %p",
					OP_REG3(op), reg1->Object);
				bError = 1;
				break;
			}
			reg_dst->Type = type;
			PRINT_STACKVAL(*reg_dst); DEBUG_F("]\n");
			break;

		case BC_OP_SETELEMENT:
			STATE_HDR();
			DEBUG_F("SETELEMENT R%i->#%i = R%i [", OP_REG2(op), OP_REG3(op), op->DstReg);
			PRINT_STACKVAL(*reg_dst); DEBUG_F("]\n");
			
			// - Core types can't have elements
			if( !SS_ISTYPEOBJECT(reg1->Type) ) {
				SpiderScript_RuntimeError(Script, "SETELEMENT on non-object %s\n",
					SpiderScript_GetTypeName(Script, reg1->Type));
				bError = 1;
				break;
			}
			type = Bytecode_int_GetSpiderValue(Script, reg_dst, &ptr);
			if( type.Def == NULL ) { bError = 1; break; }

			AST_ExecuteNode_Element(Script, NULL, reg1->Object, OP_REG3(op), type, ptr);
			break;

		// Constants:
		case BC_OP_LOADINT:
			STATE_HDR();
			DEBUG_F("LOADINT R%i = 0x%lx\n", op->DstReg, op->Content.Integer);
			PRESET_DEREF(*reg_dst);
			reg_dst->Type = TYPE_INTEGER;
			reg_dst->Integer = op->Content.Integer;
			break;
		case BC_OP_LOADREAL:
			STATE_HDR();
			DEBUG_F("LOADREAL R%i = %lf\n", op->DstReg, op->Content.Real);
			PRESET_DEREF(*reg_dst);
			reg_dst->Type = TYPE_REAL;
			reg_dst->Real = op->Content.Real;
			break;
		case BC_OP_LOADSTRING:
			STATE_HDR();
			DEBUG_F("LOADSTR R%i = %zi \"", op->DstReg, op->Content.String.Length);
			PRINT_STR(op->Content.String.Length, op->Content.String.Data);
			DEBUG_F("\"\n");
			PRESET_DEREF(*reg_dst);
			reg_dst->Type = TYPE_STRING;
			reg_dst->String = SpiderScript_CreateString(
				op->Content.String.Length, op->Content.String.Data);
			break;
		case BC_OP_LOADNULLREF:
			STATE_HDR();
			type = Script->BCTypes[OP_REG2(op)];
			DEBUG_F("LOADNULL R%i = %s\n", op->DstReg, SpiderScript_GetTypeName(Script, type));
			if( SS_ISTYPEREFERENCE( type ) )
				;
			else {
				SpiderScript_RuntimeError(Script, "LOADNULL with non-object %s",
					SpiderScript_GetTypeName(Script, type));
				bError = 1;
				break;
			}
			PRESET_DEREF(*reg_dst);
			reg_dst->Type = type;
			reg_dst->String = NULL;
			break;

		case BC_OP_CLEARREG:
			STATE_HDR();
			DEBUG_F("CLEAR R%i [", op->DstReg); PRINT_STACKVAL(*reg_dst); DEBUG_F("]\n");
			DEREF_STACKVAL(*reg_dst);
			reg_dst->Type = TYPE_VOID;
			reg_dst->Integer = 0;
			break;
		case BC_OP_MOV:
			STATE_HDR();
			DEBUG_F("MOV R%i := R%i\n", op->DstReg, OP_REG2(op));
			if( op->DstReg != OP_REG2(op) ) {
				PRESET_DEREF(*reg_dst);
				*reg_dst = *reg1;
				REF_STACKVAL(*reg_dst);
			}
			break;

		case BC_OP_CAST:
			STATE_HDR();
			itype = OP_REG2(op);
			PRESET_DEREF(*reg_dst);
			reg_dst->Type.ArrayDepth = 0;
			reg_dst->Type.Def = SpiderScript_GetCoreType(itype);
			DEBUG_F("CAST R%i(%s) = R%i(%s)\n",
				op->DstReg,
				SpiderScript_GetTypeName(Script, reg_dst->Type),
				OP_REG3(op),
				SpiderScript_GetTypeName(Script, reg2->Type)
				);
			if( SS_TYPESEQUAL(reg_dst->Type, reg2->Type) ) {
				// Warn?
				memcpy(reg_dst, reg2, sizeof(*reg_dst));
			}
			else if( itype == SS_DATATYPE_INTEGER && SS_ISCORETYPE(reg2->Type, SS_DATATYPE_REAL) ) {
				reg_dst->Integer = reg2->Real;
			}
			else if( itype == SS_DATATYPE_REAL && SS_ISCORETYPE(reg2->Type, SS_DATATYPE_INTEGER) ) {
				reg_dst->Real = reg2->Integer;
			}
			else
			{
				tSpiderTypeRef	type;
				type = Bytecode_int_GetSpiderValue(Script, reg2, &ptr);
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
			DEBUG_F(" = "); PRINT_STACKVAL(*reg_dst); DEBUG_F("\n");
			break;

		// Unary Operations
		case BC_OP_BOOL_LOGICNOT:
			STATE_HDR();
			DEBUG_F("BC_OP_BOOL_LOGICNOT R%i := R%i", op->DstReg, OP_REG2(op));
			PRESET_DEREF(*reg_dst);
			reg_dst->Type = TYPE_BOOLEAN;
			reg_dst->Boolean = !Bytecode_int_IsStackEntTrue(Script, reg1);
			break;
		
		case BC_OP_INT_BITNOT:
			STATE_HDR();
			DEBUG_F("BC_OP_INT_BITNOT R%i := R%i", op->DstReg, OP_REG2(op));
			_BC_ASSERTTYPE(reg1->Type, TYPE_INTEGER, "reg1");
			
			PRESET_DEREF(*reg_dst);
			reg_dst->Type = TYPE_INTEGER;
			reg_dst->Integer = ~reg1->Integer;
			break;
		case BC_OP_INT_NEG:
			STATE_HDR();
			DEBUG_F("BC_OP_INT_NEG R%i := R%i", op->DstReg, OP_REG2(op));
			_BC_ASSERTTYPE(reg1->Type, TYPE_INTEGER, "reg1");
			
			PRESET_DEREF(*reg_dst);
			reg_dst->Type = TYPE_INTEGER;
			reg_dst->Integer = -reg1->Integer;
			break;
		
		case BC_OP_REAL_NEG:
			STATE_HDR();
			DEBUG_F("BC_OP_REAL_NEG R%i := R%i", op->DstReg, OP_REG2(op));
			_BC_ASSERTTYPE(reg1->Type, TYPE_REAL, "reg1");
			
			PRESET_DEREF(*reg_dst);
			reg_dst->Type = TYPE_REAL;
			reg_dst->Real = -reg1->Real;
			break;

#define BINOPHDR(opcode) \
		case opcode: \
			STATE_HDR();\
			DEBUG_F(#opcode" R%i := R%i [", op->DstReg, OP_REG2(op)); \
			PRINT_STACKVAL(*reg1);\
			DEBUG_F("], R%i [", OP_REG3(op));\
			PRINT_STACKVAL(*reg2);\
			DEBUG_F("]\n");
#define BINOPHDR_TYPE(opcode, srctype, dsttype) \
			BINOPHDR(opcode) \
			_BC_ASSERTTYPE(reg1->Type, srctype, "reg1");\
			_BC_ASSERTTYPE(reg2->Type, srctype, "reg2");\
			PRESET_DEREF(*reg_dst); \
			reg_dst->Type = dsttype;
#define BINOPI(opcode, opr, dsttype, dstfld) \
			BINOPHDR_TYPE(opcode, TYPE_INTEGER, dsttype)\
			reg_dst->dstfld = reg1->Integer opr reg2->Integer; \
			break;
#define BINOPR(opcode, opr, dsttype, dstfld) \
			BINOPHDR_TYPE(opcode, TYPE_REAL, dsttype)\
			reg_dst->dstfld = reg1->Real opr reg2->Real; \
			DEBUG_F(" = "); PRINT_STACKVAL(*reg_dst); DEBUG_F("\n"); \
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
	
		BINOPHDR(BC_OP_BOOL_EQUALS)
			reg_dst->Type = TYPE_BOOLEAN;
			reg_dst->Boolean = Bytecode_int_IsStackEntTrue(Script, reg1)
				== Bytecode_int_IsStackEntTrue(Script, reg2);
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
		
		BINOPI(BC_OP_INT_BITAND, &, TYPE_INTEGER, Integer)
		BINOPI(BC_OP_INT_BITOR,  |, TYPE_INTEGER, Integer)
		BINOPI(BC_OP_INT_BITXOR, ^, TYPE_INTEGER, Integer)
				
		BINOPI(BC_OP_INT_ADD,      +, TYPE_INTEGER, Integer)
		BINOPI(BC_OP_INT_SUBTRACT, -, TYPE_INTEGER, Integer)
		BINOPI(BC_OP_INT_MULTIPLY, *, TYPE_INTEGER, Integer)
		BINOPHDR_TYPE(BC_OP_INT_DIVIDE, TYPE_INTEGER, TYPE_INTEGER)
			if( reg2->Integer == 0 ) {
				bError = 1;
				SpiderScript_ThrowException(Script, SS_EXCEPTION_ARITH, "Divide by zero");
			}
			else {
				reg_dst->Integer = reg1->Integer / reg2->Integer;
			}
			break;
		BINOPI(BC_OP_INT_MODULO,   %, TYPE_INTEGER, Integer)

		BINOPI(BC_OP_INT_BITSHIFTLEFT,  <<, TYPE_INTEGER, Integer)
		BINOPI(BC_OP_INT_BITSHIFTRIGHT, >>, TYPE_INTEGER, Integer)
		
		BINOPHDR_TYPE(BC_OP_INT_BITROTATELEFT, TYPE_INTEGER, TYPE_INTEGER)
			reg_dst->Integer = (reg1->Integer << reg2->Integer) | (reg1->Integer >> (64-reg2->Integer));
			break;
		
		BINOPI(BC_OP_INT_EQUALS,       ==, TYPE_BOOLEAN, Boolean)
		BINOPI(BC_OP_INT_NOTEQUALS,    !=, TYPE_BOOLEAN, Boolean)
		BINOPI(BC_OP_INT_LESSTHAN,     < , TYPE_BOOLEAN, Boolean)
		BINOPI(BC_OP_INT_LESSTHANEQ,   <=, TYPE_BOOLEAN, Boolean)
		BINOPI(BC_OP_INT_GREATERTHAN,  > , TYPE_BOOLEAN, Boolean)
		BINOPI(BC_OP_INT_GREATERTHANEQ,>=, TYPE_BOOLEAN, Boolean)
		
		BINOPR(BC_OP_REAL_ADD,      +, TYPE_REAL, Real)
		BINOPR(BC_OP_REAL_SUBTRACT, -, TYPE_REAL, Real)
		BINOPR(BC_OP_REAL_MULTIPLY, *, TYPE_REAL, Real)
		BINOPR(BC_OP_REAL_DIVIDE,   /, TYPE_REAL, Real)
	
		BINOPR(BC_OP_REAL_EQUALS,       ==, TYPE_BOOLEAN, Boolean)
		BINOPR(BC_OP_REAL_NOTEQUALS,    !=, TYPE_BOOLEAN, Boolean)
		BINOPR(BC_OP_REAL_LESSTHAN,     < , TYPE_BOOLEAN, Boolean)
		BINOPR(BC_OP_REAL_LESSTHANEQ,   <=, TYPE_BOOLEAN, Boolean)
		BINOPR(BC_OP_REAL_GREATERTHAN,  > , TYPE_BOOLEAN, Boolean)
		BINOPR(BC_OP_REAL_GREATERTHANEQ,>=, TYPE_BOOLEAN, Boolean)

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
			DEBUG_F("BINOP_STR_%s R%i = ", opstr, op->DstReg);
			
			_BC_ASSERTTYPE(reg1->Type, TYPE_STRING, "reg1");

			DEBUG_F("R%i [", OP_REG2(op)); PRINT_STACKVAL(*reg1); DEBUG_F("] ");
			DEBUG_F("R%i [", OP_REG3(op)); PRINT_STACKVAL(*reg2); DEBUG_F("]\n");

			// Get RVal
			Bytecode_int_GetSpiderValue(Script, reg2, &ptr);
			
			itype = AST_ExecuteNode_BinOp_String(Script, &tmp_ent.Boolean, ast_op,
				reg1->String, reg2->Type.Def->Core, ptr);
			if( itype == -1 ) {
				SpiderScript_RuntimeError(Script,
					"_ExecuteNode_BinOp[%s] for types %s<op>%s returned -1",
					opstr,
					SpiderScript_GetTypeName(Script, reg1->Type),
					SpiderScript_GetTypeName(Script, reg2->Type));
				bError = 1;
				break;
			}
			tmp_ent.Type.ArrayDepth = 0;
			tmp_ent.Type.Def = SpiderScript_GetCoreType(itype);
			PRESET_DEREF(*reg_dst);
			*reg_dst = tmp_ent;
			DEBUG_F(" = ("); PRINT_STACKVAL(*reg_dst); DEBUG_F(")\n");
			break;

		// Functions etc
		case BC_OP_CREATEOBJ:    opstr = "CREATEOBJ"; if(0)
		case BC_OP_CALLFUNCTION: opstr = "CALLFCN"; if(0)
		case BC_OP_CALLMETHOD:   opstr = "CALLMETHOD";
			{
			STATE_HDR();
			
			tScript_Function	*fcn = NULL;
			 int	id = op->Content.Function.ID;
			 int	arg_count = op->Content.Function.ArgCount & 0xFF;
			bool	is_varg_passthrough = !!((op->Content.Function.ArgCount >> 8)&1);
			
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

			int extra_args = (is_varg_passthrough ? VArgC : 0);
			const tBC_StackEnt	*args[arg_count+extra_args];
			for(int i = 0; i < arg_count; i ++ ) {
				args[i] = &REG( op->Content.Function.ArgRegs[i] );
			}
			for( int i = 0; i < extra_args; i ++ ) {
				args[arg_count+i] = VArgs[i];
			}
			
			// Either a local call, or a remote call
			PRESET_DEREF(*reg_dst);

			DEBUG_F("%s.%s R%i, 0x%x,", opstr, (fcn?"L":"R"), op->DstReg, id);
			for(int i = 0; i < arg_count; i ++ ) {
				DEBUG_F(" R%i", op->Content.Function.ArgRegs[i]);
			}
			if( is_varg_passthrough )
				DEBUG_F(" ...(%i)", extra_args);
			DEBUG_F("\n");

			if( fcn )
			{
				if( !fcn->BCFcn ) {
					SpiderScript_RuntimeError(Script,
						"Function #%i %s is not compiled", id, fcn->Name);
					bError = 1;
					break;
				}
				rv = Bytecode_int_ExecuteFunction(Script, fcn,
					arg_count+extra_args, args, reg_dst);
				if( rv ) {
					bError = 1;
					break;
				}
			}
			else
			{
				rv = Bytecode_int_CallExternFunction( Script, op,
					arg_count+extra_args, args, reg_dst );
				if( rv ) {
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

			DEBUG_F("RETURN R%i\n", op->DstReg);
			nextop = NULL;	// non-error stop
			break;
	
		case BC_OP_EXCEPTION_PUSH:
			STATE_HDR();
			DEBUG_F("EXCEPTION PUSH %i\n", op->DstReg);
			TODO("BC_OP_EXCEPTION_PUSH");
			break;
		case BC_OP_EXCEPTION_CHECK:
			STATE_HDR();
			DEBUG_F("EXCEPTION CHECK %i %i\n", op->DstReg, OP_REG2(op));
			TODO("BC_OP_EXCEPTION_CHECK");
			break;
		case BC_OP_EXCEPTION_POP:
			STATE_HDR();
			DEBUG_F("EXCEPTION POP\n");
			TODO("BC_OP_EXCEPTION_POP");
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
		function_offset ++;
	}
	
	// Clean up
	DEBUG_F("> Cleaning up\n");
	// - Restore stack
	for( int i = 0; i < num_registers; i ++ )
	{
		DEREF_STACKVAL( registers[i] );
	}

	if( bError ) {
		SpiderScript_PushBacktrace(
			Script,
			Fcn->Name, function_offset,
			last_file, last_line
			);
	}

	DEBUG_F("--- Return %i\n", bError);
	return bError;
}

