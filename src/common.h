/*
 * SpiderScript
 * - By John Hodge (thePowersGang)
 */
#ifndef _COMMON_H_
#define _COMMON_H_

#include <spiderscript.h>

#define CONSTRUCTOR_NAME	"__constructor"

typedef struct sScript_Function	tScript_Function;
typedef struct sScript_Arg	tScript_Arg;
typedef struct sScript_Class	tScript_Class;
typedef struct sScript_Class_Var	tScript_Class_Var;

struct sSpiderScript
{
	tSpiderVariant	*Variant;
	
	tScript_Function	*Functions;
	tScript_Function	*LastFunction;
	
	tScript_Class	*FirstClass;
	tScript_Class	*LastClass;
	
//	char	*CurNamespace;	//!< Current namespace prefix (NULL = Root) - No trailing .
};

struct sScript_Arg
{

	 int	Type;
	char	*Name;
};

struct sScript_Function
{
	tScript_Function	*Next;
	// char	*Namespace;
	char	*Name;

	 int	ReturnType;
	
	struct sAST_Node	*ASTFcn;
	struct sBC_Function	*BCFcn;

	 int	ArgumentCount;
	tScript_Arg	Arguments[];
};

struct sScript_Class_Var
{
	tScript_Class_Var	*Next;
	 int	Type;
	char	Name[];
};

struct sScript_Class
{
	tScript_Class	*Next;
	
	tScript_Function	*FirstFunction;
	tScript_Function	*LastFunction;
	
	tScript_Class_Var	*FirstProperty;
	tScript_Class_Var	*LastProperty;
	 int	nProperties;

	tSpiderScript_DataType	TypeCode;	

	char	Name[];
};

extern int	SpiderScript_BytecodeScript(tSpiderScript *Script);
extern tSpiderClass *SpiderScript_GetClass_Native(tSpiderScript *Script, int Type);
extern tScript_Class *SpiderScript_GetClass_Script(tSpiderScript *Script, int Type);
extern tSpiderFunction	*gpExports_First;
extern tSpiderClass	*gpExports_FirstClass;
extern int	Bytecode_ExecuteFunction(tSpiderScript *Script, tScript_Function *Fcn,
	void *RetValue, int NArgs, const int *ArgTypes, const void * const Args[]);

extern int	SpiderScript_ResolveObject(tSpiderScript *Script, const char *Namespaces[], const char *Name);
extern int	SpiderScript_ResolveFunction(tSpiderScript *Script, const char *Namespaces[], const char *Name, void **Ident);

extern int	SpiderScript_int_ExecuteFunction(tSpiderScript *Script, int FunctionID,
	void *RetData, int ArgumentCount, const int ArgTypes[], const void * const Args[],
	void **Ident
	);
extern int	SpiderScript_int_ConstructObject(tSpiderScript *Script, int TypeCode,
	tSpiderObject **RetData, int ArgumentCount, const int ArgTypes[], const void * const Args[],
	void **Ident
	);
extern int	SpiderScript_int_ExecuteMethod(tSpiderScript *Script, tSpiderObject *Object, int MethodID,
	void *RetData, int ArgumentCount, const int ArgTypes[], const void * const Args[],
	void **Ident
	);

extern tSpiderObject	*SpiderScript_AllocateScriptObject(tSpiderScript *Script, tScript_Class *Class);

extern int	SpiderScript_int_GetTypeSize(int TypeCode);
#endif

