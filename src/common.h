/*
 * SpiderScript
 * - By John Hodge (thePowersGang)
 */
#ifndef _COMMON_H_
#define _COMMON_H_

#include <spiderscript.h>
#include <stdarg.h>

#define CONSTRUCTOR_NAME	"__constructor"
#define BC_NS_SEPARATOR	'@'

typedef struct sScript_Function	tScript_Function;
typedef struct sScript_Arg	tScript_Arg;
typedef struct sScript_Class	tScript_Class;
typedef struct sScript_Var	tScript_Var;

struct sSpiderScript
{
	tSpiderVariant	*Variant;
	
	tScript_Function	*Functions;
	tScript_Function	*LastFunction;
	
	tScript_Class	*FirstClass;
	tScript_Class	*LastClass;
	
	 int	CurException;
	char	*CurExceptionString;
	
	tScript_Var	*FirstGlobal;
	tScript_Var	*LastGlobal;
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

struct sScript_Var
{
	tScript_Var	*Next;
	void	*Ptr;
	 int	Type;
	char	*Name;
};

struct sScript_Class
{
	tScript_Class	*Next;
	
	tScript_Function	*FirstFunction;
	tScript_Function	*LastFunction;
	
	tScript_Var	*FirstProperty;
	tScript_Var	*LastProperty;
	 int	nProperties;

	tSpiderScript_DataType	TypeCode;	

	char	Name[];
};

extern char	*mkstr(const char *Format, ...);
extern char	*mkstrv(const char *format, va_list args);

extern int	SpiderScript_BytecodeScript(tSpiderScript *Script);
extern tSpiderClass *SpiderScript_GetClass_Native(tSpiderScript *Script, int Type);
extern tScript_Class *SpiderScript_GetClass_Script(tSpiderScript *Script, int Type);

extern tSpiderFunction	*gapExportedFunctions[];
extern int	giNumExportedFunctions;
extern tSpiderClass	*gapExportedClasses[];
extern int	giNumExportedClasses;

extern int	Bytecode_ExecuteFunction(tSpiderScript *Script, tScript_Function *Fcn,
	void *RetValue, int NArgs, const int *ArgTypes, const void * const Args[]);

extern int	SpiderScript_ResolveObject(tSpiderScript *Script, const char *Namespaces[], const char *Name, void **IDent);
extern int	SpiderScript_ResolveFunction(tSpiderScript *Script, const char *Namespaces[], const char *Name, void **Ident);

extern int	SpiderScript_int_ExecuteFunction(tSpiderScript *Script, int FunctionID,
	void *RetData, int ArgumentCount, const int ArgTypes[], const void * const Args[],
	void **Ident
	);
extern int	SpiderScript_int_ConstructObject(tSpiderScript *Script, int TypeCode,
	tSpiderObject **RetData, int ArgumentCount, const int ArgTypes[], const void * const Args[],
	void **Ident
	);
extern int	SpiderScript_int_ExecuteMethod(tSpiderScript *Script, int MethodID,
	void *RetData, int ArgumentCount, const int ArgTypes[], const void * const Args[],
	void **Ident
	);

extern tSpiderObject	*SpiderScript_AllocateScriptObject(tSpiderScript *Script, tScript_Class *Class);

extern int	SpiderScript_int_GetTypeSize(int TypeCode);

extern void	SpiderScript_RuntimeError(tSpiderScript *Script, const char *Format, ...);
#endif

