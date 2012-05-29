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
	
	char	*CurNamespace;	//!< Current namespace prefix (NULL = Root) - No trailing .
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
	
	char	Name[];
};

#endif

