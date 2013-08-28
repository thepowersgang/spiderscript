/*
 */
#ifndef _AST_H_
#define _AST_H_

#include <spiderscript.h>
#include "tokens.h"
#include "common.h"

typedef enum eAST_NodeTypes	tAST_NodeType;
typedef struct sAST_Script	tAST_Script;
typedef struct sAST_Function	tAST_Function;
typedef struct sAST_Node	tAST_Node;
typedef struct sAST_BlockState	tAST_BlockState;
typedef struct sAST_Variable	tAST_Variable;

/**
 * \brief Node Types
 */
enum eAST_NodeTypes
{
	NODETYPE_NOP,
	
	NODETYPE_BLOCK,	//!< Node Block
	
	// 2
	NODETYPE_VARIABLE,	//!< Variable
	NODETYPE_CONSTANT,	//!< Runtime Constant
	NODETYPE_NULL,  	//!< NULL Reference
	NODETYPE_BOOLEAN,	//!< Boolean constant
	NODETYPE_STRING,	//!< String Constant
	NODETYPE_INTEGER,	//!< Integer Constant
	NODETYPE_REAL,	//!< Real Constant
	
	// 9
	NODETYPE_DEFVAR,	//!< Define a variable
	NODETYPE_DEFGLOBAL,	//!< Define a script-global variable
	NODETYPE_ELEMENT,	//!< Reference a class attribute
	NODETYPE_CAST,	//!< Cast a value to another (Uniop)
	
	// 13
	NODETYPE_RETURN,	//!< Return from a function (reserved word)
	NODETYPE_BREAK, 	//!< Break out of a loop
	NODETYPE_CONTINUE,	//!< Next loop iteration
	NODETYPE_DELETE,  	//!< Sets an object pointer to NULL
	NODETYPE_ASSIGN,	//!< Variable assignment operator
	NODETYPE_POSTINC,	//!< Post-increment (i++) - Uniop
	NODETYPE_POSTDEC,	//!< Post-decrement (i--) - Uniop
	NODETYPE_FUNCTIONCALL,	//!< Call a function
	NODETYPE_METHODCALL,	//!< Call a class method
	NODETYPE_CREATEOBJECT,	//!< Create an object
	NODETYPE_CREATEARRAY,	//!< Create an empty array

	// 24
	NODETYPE_SWITCH,
	NODETYPE_CASE,
	
	// 26
	NODETYPE_IF,	//!< Conditional
	NODETYPE_TERNARY,	//!< Ternary / Null-Coalescing
	NODETYPE_LOOP,	//!< Looping Construct
	
	// 29
	NODETYPE_INDEX,	//!< Index into an array
	
	// 30
	NODETYPE_LOGICALNOT,	//!< Logical NOT operator
	NODETYPE_LOGICALAND,	//!< Logical AND operator
	NODETYPE_LOGICALOR, 	//!< Logical OR operator
	NODETYPE_LOGICALXOR,	//!< Logical XOR operator
	
	// 34
	NODETYPE_REFEQUALS,	//!< References are equal
	NODETYPE_REFNOTEQUALS,	//!< References differ
	NODETYPE_EQUALS,	//!< Comparison Equals
	NODETYPE_NOTEQUALS,	//!< Comparison Not Equals
	NODETYPE_LESSTHAN,	//!< Comparison Less Than
	NODETYPE_LESSTHANEQUAL,	//!< Comparison Less Than or Equal
	NODETYPE_GREATERTHAN,	//!< Comparison Greater Than
	NODETYPE_GREATERTHANEQUAL,	//!< Comparison Greater Than or Equal
	
	// 41
	NODETYPE_BWNOT,	//!< Bitwise NOT
	NODETYPE_BWAND,	//!< Bitwise AND
	NODETYPE_BWOR,	//!< Bitwise OR
	NODETYPE_BWXOR,	//!< Bitwise XOR
	
	// 45
	NODETYPE_BITSHIFTLEFT,	//!< Bitwise Shift Left (Grow)
	NODETYPE_BITSHIFTRIGHT,	//!< Bitwise Shift Right (Shrink)
	NODETYPE_BITROTATELEFT,	//!< Bitwise Rotate Left (Grow)
	
	// 48
	NODETYPE_NEGATE,	//!< Negagte
	NODETYPE_ADD,	//!< Add
	NODETYPE_SUBTRACT,	//!< Subtract
	NODETYPE_MULTIPLY,	//!< Multiply
	NODETYPE_DIVIDE,	//!< Divide
	NODETYPE_MODULO,	//!< Modulus
};

struct sAST_Node
{
	tAST_Node	*NextSibling;
	tAST_NodeType	Type;

	const char	*File;
	 int	Line;
	
	void	*BlockState;	//!< BlockState pointer (for cache integrity)
	 int	BlockIdent;	//!< Ident (same as above)
	void	*ValueCache;	//!< Cached value / pointer
	
	union
	{
		struct {
			tAST_Node	*FirstChild;
			tAST_Node	*LastChild;
		}	Block;
		
		struct {
			 int	Operation;
			tAST_Node	*Dest;
			tAST_Node	*Value;
		}	Assign;
		
		struct {
			tAST_Node	*Value;
		}	UniOp;
		
		struct {
			tAST_Node	*Left;
			tAST_Node	*Right;
		}	BinOp;
		
		struct {
			tAST_Node	*Object;
			tAST_Node	*FirstArg;
			tAST_Node	*LastArg;
			 int	NumArgs;
			char	Name[];
		}	FunctionCall;
		
		struct {
			tAST_Node	*Condition;
			tAST_Node	*True;
			tAST_Node	*False;
		}	If;
		
		struct {
			tAST_Node	*Init;
			 int	bCheckAfter;
			tAST_Node	*Condition;
			tAST_Node	*Increment;
			tAST_Node	*Code;
			char	Tag[];
		}	For;
		
		/**
		 * \note Used for \a NODETYPE_VARIABLE and \a NODETYPE_CONSTANT
		 */
		struct {
			char	_unused[0];	// Shut GCC up
			char	Name[];
		}	Variable;
		
		struct {
			tAST_Node	*Element;
			char	Name[];
		}	Scope;	// Used by NODETYPE_SCOPE and NODETYPE_ELEMENT
		
		struct {
			tSpiderTypeRef	DataType;
			tAST_Node	*InitialValue;
			char	Name[];
		}	DefVar;
		
		struct {
			 tSpiderTypeRef	DataType;
			 tAST_Node	*Value;
		}	Cast;
		
		// Used for NODETYPE_REAL, NODETYPE_INTEGER and NODETYPE_STRING
		tSpiderBool	ConstBoolean;
		tSpiderInteger	ConstInt;
		tSpiderReal	ConstReal;
		tSpiderString	*ConstString;
	};
};

//struct sAST_Variable
//{
//	tAST_Variable	*Next;
//	 int	Type;	// Only used for static typing
//	char	Name[];
//};

// === FUNCTIONS ===
extern tAST_Script	*AST_NewScript(void);
extern size_t	AST_WriteScript(void *Buffer, tSpiderScript *Script);
extern size_t	AST_WriteNode(void *Buffer, size_t Offset, tAST_Node *Node);

extern tScript_Class	*AST_AppendClass(tParser *Parser, const char *Name);
extern int	AST_FinaliseClass(tParser *Parser, tScript_Class *Class);
extern int	AST_AppendClassProperty(tParser *Parser, tScript_Class *Class, const char *Name, tSpiderTypeRef Type);
extern int	AST_AppendMethod(tParser *Parser, tScript_Class *Class, const char *Name, tSpiderTypeRef ReturnType, tAST_Node *FirstArg, tAST_Node *Code);
extern int	AST_AppendFunction(tParser *Parser, const char *Name, tSpiderTypeRef ReturnType, tAST_Node *FirstArg, tAST_Node *Code);

extern tAST_Node	*AST_NewNop(tParser *Parser);

extern tAST_Node	*AST_NewString(tParser *Parser, const char *String, int Length);
extern tAST_Node	*AST_NewInteger(tParser *Parser, int64_t Value);
extern tAST_Node	*AST_NewReal(tParser *Parser, double Value);
extern tAST_Node	*AST_NewNullReference(tParser *Parser);
extern tAST_Node	*AST_NewBoolean(tParser *Parser, int Value);

extern tAST_Node	*AST_NewVariable(tParser *Parser, const char *Name);
extern tAST_Node	*AST_NewDefineVar(tParser *Parser, tSpiderTypeRef Type, const char *Name);
extern tAST_Node	*AST_NewConstant(tParser *Parser, const char *Name);
extern tAST_Node	*AST_NewClassElement(tParser *Parser, tAST_Node *Object, const char *Name);

extern tAST_Node	*AST_NewFunctionCall(tParser *Parser, const char *Name);
extern tAST_Node	*AST_NewCreateObject(tParser *Parser, const char *Name);
extern tAST_Node	*AST_NewMethodCall(tParser *Parser, tAST_Node *Object, const char *Name);
extern void	AST_AppendFunctionCallArg(tAST_Node *Node, tAST_Node *Arg);
extern tAST_Node	*AST_NewCreateArray(tParser *Parser, tSpiderTypeRef InnerType, tAST_Node *Size);

extern tAST_Node	*AST_NewCodeBlock(tParser *Parser);
extern void	AST_AppendNode(tAST_Node *Parent, tAST_Node *Child);

extern tAST_Node	*AST_NewIf(tParser *Parser, tAST_Node *Condition, tAST_Node *True, tAST_Node *False);
extern tAST_Node	*AST_NewTernary(tParser *Parser, tAST_Node *Condition, tAST_Node *True, tAST_Node *False);
extern tAST_Node	*AST_NewLoop(tParser *Parser, const char *Tag, tAST_Node *Init, int bPostCheck, tAST_Node *Condition, tAST_Node *Increment, tAST_Node *Code);

extern tAST_Node	*AST_NewAssign(tParser *Parser, int Operation, tAST_Node *Dest, tAST_Node *Value);
extern tAST_Node	*AST_NewCast(tParser *Parser, tSpiderTypeRef Target, tAST_Node *Value);
extern tAST_Node	*AST_NewBinOpN(tParser *Parser, int Operation, tAST_Node *Left, tAST_Node *Right);
extern tAST_Node	*AST_NewBinOp(tParser *Parser, int Operation, tAST_Node *Left, tAST_Node *Right);
extern tAST_Node	*AST_NewUniOp(tParser *Parser, int Operation, tAST_Node *Value);
extern tAST_Node	*AST_NewBreakout(tParser *Parser, int Type, const char *DestTag);

extern void	AST_FreeNode(tAST_Node *Node);

// exec_ast.h
extern tSpiderScript_CoreType	AST_ExecuteNode_UniOp_GetType(tSpiderScript *Script, int Op, tSpiderScript_CoreType Type);
extern tSpiderInteger	AST_ExecuteNode_UniOp_Integer(tSpiderScript *Script, int Op, tSpiderInteger Value);
extern tSpiderReal	AST_ExecuteNode_UniOp_Real   (tSpiderScript *Script, int Op, tSpiderReal Value);

extern int	AST_ExecuteNode_BinOp_GetType(tSpiderScript *Script, int Op, tSpiderScript_CoreType LeftType, tSpiderScript_CoreType RightType);
extern int	AST_ExecuteNode_BinOp_Integer(tSpiderScript *Script, void *Dest,
	int Op, tSpiderInteger Left, int RightType, const void *Right);
extern int	AST_ExecuteNode_BinOp_Real   (tSpiderScript *Script, void *Dest,
	int Op, tSpiderReal    Left, int RightType, const void *Right);
extern int	AST_ExecuteNode_BinOp_String (tSpiderScript *Script, void *Dest,
	int Op, const tSpiderString *Left, int RightType, const void *Right);
extern int	AST_ExecuteNode_Index(tSpiderScript *Script, void *Dest,
	tSpiderArray *Array, int Index, tSpiderTypeRef NewType, void *NewValue);
extern tSpiderTypeRef	AST_ExecuteNode_Element(tSpiderScript *Script, void *Dest,
	tSpiderObject *Object, int ElementIndex, tSpiderTypeRef NewType, void *NewValue);

#endif
