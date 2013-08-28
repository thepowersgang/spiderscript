/*
 * SpiderScript
 * - Bytecode definitions
 */
#ifndef _BYTECODE_H_
#define _BYTECODE_H_

#include "bytecode_ops.h"

typedef struct sBC_Op	tBC_Op;

struct sBC_Op
{
	tBC_Op	*Next;
	enum eBC_Ops	Operation;

	void	*CacheEnt;	// Used to runtime cache function calls

	 int	DstReg;
	union {
		struct {
			 int	RegInt2;	// Src/Destination
			 int	RegInt3;	// Src
		} RegInt;
		struct {
			 int	ID;
			 int	ArgCount;
			 int	ArgRegs[];
		} Function;
		
		double	Real;
		uint64_t	Integer;
		struct {
			size_t	Length;
			char	Data[];
		} String;
	} Content;
};

struct sBC_Function
{
	tSpiderScript	*Script;

	 int	LabelCount;
	 int	LabelSpace;
	tBC_Op	**Labels;

	 int	MaxGlobalCount;
	 int	MaxRegisters;
	
	 int	OperationCount;
	tBC_Op	*Operations;
	tBC_Op	*OperationsEnd;
};

extern int	Bytecode_int_OpUsesString(int Op);
extern int	Bytecode_int_OpUsesInteger(int Op);
extern int	Bytecode_int_GetTypeIdx(tSpiderScript *Script, tSpiderTypeRef Type);

#endif
