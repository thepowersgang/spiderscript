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

#endif

