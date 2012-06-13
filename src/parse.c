/*
 * Acess2 - SpiderScript
 * - Parser
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <spiderscript.h>
#define WANT_TOKEN_STRINGS	1
#include "tokens.h"
#include "ast.h"
#include "common.h"

#define DEBUG	0
#define	SUPPORT_BREAK_TAGS	1

#if DEBUG >= 2
# define DEBUGS2(s, v...)	printf("%s: "s"\n", __func__, ## v)
#else
# define DEBUGS2(...)	do{}while(0)
#endif

enum eGetIdentMode
{
	GETIDENTMODE_VALUE,
	GETIDENTMODE_NEW,
	GETIDENTMODE_EXPRROOT,
	GETIDENTMODE_CLASS,
	GETIDENTMODE_NAMESPACE,
	GETIDENTMODE_FUNCTIONDEF
};

// === PROTOTYPES ===
 int	Parse_Buffer(tSpiderScript *Script, const char *Buffer, const char *Filename);
void	Parse_NamespaceContent(tParser *Parser);
void	Parse_ClassDefinition(tParser *Parser);
void	Parse_FunctionDefinition(tScript_Class *Class, tParser *Parser, int Type);
tAST_Node	*Parse_DoCodeBlock(tParser *Parser, tAST_Node *CodeNode);
tAST_Node	*Parse_DoBlockLine(tParser *Parser, tAST_Node *CodeNode);
tAST_Node	*Parse_VarDefList(tParser *Parser, tAST_Node *CodeNode, tScript_Class *Class);
tAST_Node	*Parse_GetVarDef(tParser *Parser, int Type, tScript_Class *Class);

tAST_Node	*Parse_DoExpr0(tParser *Parser);	// Assignment
tAST_Node	*Parse_DoExpr1(tParser *Parser);	// Boolean Operators
tAST_Node	*Parse_DoExpr2(tParser *Parser);	// Comparison Operators
tAST_Node	*Parse_DoExpr3(tParser *Parser);	// Bitwise Operators
tAST_Node	*Parse_DoExpr4(tParser *Parser);	// Bit Shifts
tAST_Node	*Parse_DoExpr5(tParser *Parser);	// Arithmatic
tAST_Node	*Parse_DoExpr6(tParser *Parser);	// Mult & Div
tAST_Node	*Parse_DoExpr7(tParser *Parser);	// Right Unary Operations
tAST_Node	*Parse_DoExpr8(tParser *Parser);	// Left Unary Operations

tAST_Node	*Parse_DoParen(tParser *Parser);	// Parenthesis (Always Last)
tAST_Node	*Parse_DoValue(tParser *Parser);	// Values

tAST_Node	*Parse_GetString(tParser *Parser);
tAST_Node	*Parse_GetNumeric(tParser *Parser);
tAST_Node	*Parse_GetVariable(tParser *Parser);
tAST_Node	*Parse_GetIdent(tParser *Parser, enum eGetIdentMode Mode, tScript_Class *Class);

void	SyntaxAssert_(tParser *Parser, int SourceLine, int Have, int Want);
void	SyntaxError_(tParser *Parser, int SourceLine, int bFatal, const char *Message, ...);
#define SyntaxAssert(p,h,w)	SyntaxAssert_(p,__LINE__,h,w)
#define SyntaxError(p,f,m...)	SyntaxError_(p,__LINE__,f,m)

#if 0
#define SyntaxAssert(_parser, _have, _want)	SyntaxAssertV(_parser, _have, _want, NULL)
#define SyntaxAssertV(_parser, _have, _want, _rv) do { \
	int have = (_have), want = (_want); \
	if( (have) != (want) ) { \
		SyntaxError(Parser, 1, "Unexpected %s(%i), expecting %s(%i)\n", \
			csaTOKEN_NAMES[have], have, csaTOKEN_NAMES[want], want); \
		return _rv; \
	} \
}while(0)
#endif

#define TODO(Parser, message...) do {\
	fprintf(stderr, "TODO: "message);\
	longjmp(Parser->JmpTarget, -1);\
}while(0)

// === CODE ===
/**
 * \brief Parse a buffer into a syntax tree
 */
int Parse_Buffer(tSpiderScript *Script, const char *Buffer, const char *Filename)
{
	tParser	parser = {0};
	tParser *Parser = &parser;	//< Keeps code consistent
	tAST_Node	*mainCode, *node;
	tScript_Function	*fcn;
	
	#if DEBUG >= 2
	printf("Parse_Buffer: (Script=%p, Buffer=%p)\n", Script, Buffer);
	#endif
	
	// Initialise parser
	parser.LastToken = -1;
	parser.NextToken = -1;
	parser.CurLine = 1;
	parser.BufStart = Buffer;
	parser.CurPos = Buffer;
	// hackery to do reference counting
	parser.Filename = malloc(sizeof(int)+strlen(Filename)+1);
	strcpy(parser.Filename + sizeof(int), Filename);
	*(int*)(parser.Filename) = 0;	// Set reference count
	parser.Filename += sizeof(int);	// Move filename
	parser.ErrorHit = 0;
	parser.Script = Script;
	parser.Variant = Script->Variant;
	
	mainCode = AST_NewCodeBlock(&parser);
	
	// Give us an error fallback
	if( setjmp( parser.JmpTarget ) != 0 )
	{
		AST_FreeNode( mainCode );
		
		for(fcn = Script->Functions; fcn; )
		{
			tScript_Function	*nextFcn;
			
			AST_FreeNode( fcn->ASTFcn );
			
			nextFcn = fcn->Next;
			free( fcn );
			fcn = nextFcn;
		}
		return -1;
	}
	
	// Parse the file!
	while(Parser->Token != TOK_EOF)
	{
		switch( GetToken(Parser) )
		{
		case TOK_EOF:
			break;

		case TOK_RWD_CLASS:
			Parse_ClassDefinition(Parser);
			break;
		case TOK_RWD_NAMESPACE:
			Parse_NamespaceContent(Parser);
			break;

		// Ordinary Statement
		default:
			PutBack(Parser);
			node = Parse_DoBlockLine(Parser, mainCode);
			if(node)
				AST_AppendNode( mainCode, node );
			break;
		}
		
		// Jump to error handler on error
		if(Parser->ErrorHit)
			longjmp(Parser->JmpTarget, -1);
	}

	// Return 0 by default
	AST_AppendNode( mainCode, AST_NewUniOp(Parser, NODETYPE_RETURN, AST_NewInteger(Parser, 0)) );
	AST_AppendFunction( Parser, "", SS_DATATYPE_INTEGER, NULL, mainCode );
	
	//printf("---- %p parsed as SpiderScript ----\n", Buffer);
	
	return 0;
}

void Parse_NamespaceContent(tParser *Parser)
{
	char	*name;
	
	SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT);
	name = strndup( Parser->TokenStr, Parser->TokenLen );
	// Within a namespace, only classes and functions can be defined
	SyntaxAssert(Parser, GetToken(Parser), TOK_BRACE_OPEN);

	while( GetToken(Parser) != TOK_BRACE_CLOSE )
	{
		switch( Parser->Token )
		{
		case TOK_RWD_CLASS:
			Parse_ClassDefinition(Parser);
			break;
		case TOK_RWD_NAMESPACE:
			Parse_NamespaceContent(Parser);
			break;
		
		// Static type function
		case TOK_IDENT:
			PutBack(Parser);
			// Setting Class=ERRPTR only allows function definitions
			Parse_GetIdent(Parser, GETIDENTMODE_NAMESPACE, NULL);
			break;
	
		default:
			fprintf(stderr, "Syntax Error: Unexpected %s on line %i, Expected class/namespace/function definition\n",
				csaTOKEN_NAMES[Parser->Token], Parser->CurLine);
			longjmp( Parser->JmpTarget, -1 );
			break;
		}
	}	
}

void Parse_ClassDefinition(tParser *Parser)
{
	char *name;
	tScript_Class	*class;
	
	// Get name of the class and create the definition
	SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT);
	name = strndup( Parser->TokenStr, Parser->TokenLen );
	class = AST_AppendClass(Parser, name);
	free(name);
	
	SyntaxAssert(Parser, GetToken(Parser), TOK_BRACE_OPEN);

	while( GetToken(Parser) != TOK_BRACE_CLOSE )
	{
		switch( Parser->Token )
		{
		case TOK_IDENT:
			PutBack(Parser);
			Parse_GetIdent(Parser, GETIDENTMODE_CLASS, class);
			// TODO: Comma separated attributes?
			break;
#if 0
		case TOK_RWD_FUNCTION:
			if( !Parser->Script->Variant->bDyamicTyped ) {
				SyntaxError(Parser, 1, "Dynamic functions are invalid in static mode");
				longjmp(Parser->JmpTarget, -1);
			}
			
			Parse_FunctionDefinition(class, Parser, SS_DATATYPE_UNDEF);
			break;
#endif
		}
	}
}

void Parse_FunctionDefinition(tScript_Class *Class, tParser *Parser, int Type)
{
	char	*name;
	 int	rv;
	tAST_Node	*first_arg = NULL, *last_arg, *code;
	
	last_arg = (void*)&first_arg;	// HACK
	
	SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT );
	
	name = strndup( Parser->TokenStr, Parser->TokenLen );
	#if DEBUG
	printf("DefFCN %s\n", name);
	#endif
	
	// Get arguments
	SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_OPEN );
	if( LookAhead(Parser) != TOK_PAREN_CLOSE )
	{
		do {
			tAST_Node *def;
#if 0
			if( Parser->Variant->bDyamicTyped ) {
				GetToken(Parser);
				def = Parse_GetVarDef(Parser, SS_DATATYPE_UNDEF, NULL);
			}
			else
#endif
				def = Parse_GetIdent(Parser, GETIDENTMODE_FUNCTIONDEF, NULL);
			last_arg->NextSibling = def;
			last_arg = def;
			last_arg->NextSibling = NULL;
		} while(GetToken(Parser) == TOK_COMMA);
	}
	else
		GetToken(Parser);
	SyntaxAssert(Parser, Parser->Token, TOK_PAREN_CLOSE );

	code = Parse_DoCodeBlock(Parser, NULL);

	if( Class )
		rv = AST_AppendMethod( Parser, Class, name, Type, first_arg, code );
	else
		rv = AST_AppendFunction( Parser, name, Type, first_arg, code );

	// Clean up argument definition nodes
	{
		tAST_Node	*nextarg;
		for( ; first_arg; first_arg = nextarg )
		{
			nextarg = first_arg->NextSibling;
			AST_FreeNode(first_arg);
		}
	}

	free(name);
	
	// Error check after cleaning up, just in case :)
	if( rv ) {
		longjmp(Parser->JmpTarget, -1);
	}
}

/**
 * \brief Parse a block of code surrounded by { }
 */
tAST_Node *Parse_DoCodeBlock(tParser *Parser, tAST_Node *CodeNode)
{
	tAST_Node	*ret;
	
	// Check if we are being called for a one-liner
	if( GetToken(Parser) != TOK_BRACE_OPEN ) {
		PutBack(Parser);
		return Parse_DoBlockLine(Parser, CodeNode);
	}
	
	ret = AST_NewCodeBlock(Parser);
	
	while( LookAhead(Parser) != TOK_BRACE_CLOSE )
	{
		tAST_Node *node = Parse_DoBlockLine(Parser, ret);
		if(node)
			AST_AppendNode( ret, node );
	}
	GetToken(Parser);	// Omnomnom
	return ret;
}

/**
 * \brief Parse a line in a block
 */
tAST_Node *Parse_DoBlockLine(tParser *Parser, tAST_Node *CodeNode)
{
	tAST_Node	*ret;
	
	//printf("Parse_DoBlockLine: Line %i\n", Parser->CurLine);
	
	switch(LookAhead(Parser))
	{
	// New block
	case TOK_BRACE_OPEN:
		return Parse_DoCodeBlock(Parser, CodeNode);
	
	// Empty statement
	case TOK_SEMICOLON:
		GetToken(Parser);
		return NULL;
	
	// Return from a method
	case TOK_RWD_RETURN:
		GetToken(Parser);
		ret = AST_NewUniOp(Parser, NODETYPE_RETURN, Parse_DoExpr0(Parser));
		break;

	case TOK_RWD_DELETE:
		GetToken(Parser);
		ret = AST_NewUniOp(Parser, NODETYPE_DELETE, Parse_GetVariable(Parser));
		break;
	
	// Break / Continue (end a loop / go to next iteration)
	case TOK_RWD_CONTINUE:
	case TOK_RWD_BREAK:
		{
		 int	tok;
		char	*ident = NULL;
		tok = GetToken(Parser);
		// Get the number of nesting levels to break
		if(LookAhead(Parser) == TOK_IDENT)
		{
			GetToken(Parser);
			ident = strndup(Parser->TokenStr, Parser->TokenLen);
		}
		// Get the action
		switch(tok)
		{
		case TOK_RWD_BREAK:	ret = AST_NewBreakout(Parser, NODETYPE_BREAK, ident);	break;
		case TOK_RWD_CONTINUE:	ret = AST_NewBreakout(Parser, NODETYPE_CONTINUE, ident);	break;
		default:
			SyntaxError(Parser, 1, "BUG Unhandled break/continue (%s)",
				csaTOKEN_NAMES[tok]);
			return NULL;
		}
		if(ident)	free(ident);
		}
		break;
	
	// Control Statements
	case TOK_RWD_IF:
		{
		tAST_Node	*cond, *true, *false = NULL;
		GetToken(Parser);	// eat the if
		
		SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_OPEN);
		cond = Parse_DoExpr0(Parser);	// Get condition
		SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_CLOSE);
		true = Parse_DoCodeBlock(Parser, CodeNode);
		if( LookAhead(Parser) == TOK_RWD_ELSE ) {
			GetToken(Parser);
			false = Parse_DoCodeBlock(Parser, CodeNode);
		}
		else
			false = AST_NewNop(Parser);
		ret = AST_NewIf(Parser, cond, true, false);
		}
		return ret;
	
	case TOK_RWD_FOR:
		{
		char	*tag = NULL;
		tAST_Node	*init=NULL, *cond=NULL, *inc=NULL, *code;
		GetToken(Parser);	// Eat 'for'
		
		#if SUPPORT_BREAK_TAGS
		if(LookAhead(Parser) == TOK_LT)
		{
			GetToken(Parser);
			SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT);
			tag = strndup(Parser->TokenStr, Parser->TokenLen);
			SyntaxAssert(Parser, GetToken(Parser), TOK_GT);
		}
		#endif
		
		SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_OPEN);
		
		if(LookAhead(Parser) != TOK_SEMICOLON)
			init = Parse_DoExpr0(Parser);
		
		SyntaxAssert(Parser, GetToken(Parser), TOK_SEMICOLON);
		
		if(LookAhead(Parser) != TOK_SEMICOLON)
			cond = Parse_DoExpr0(Parser);
		
		SyntaxAssert(Parser, GetToken(Parser), TOK_SEMICOLON);
		
		if(LookAhead(Parser) != TOK_PAREN_CLOSE)
			inc = Parse_DoExpr0(Parser);
		
		SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_CLOSE);
		
		code = Parse_DoCodeBlock(Parser, CodeNode);
		ret = AST_NewLoop(Parser, tag, init, 0, cond, inc, code);
		if(tag)	free(tag);
		}
		return ret;
	
	case TOK_RWD_DO:
		{
		const char	*tag = "";
		tAST_Node	*code, *cond;
		GetToken(Parser);	// Eat 'do'
		
		#if SUPPORT_BREAK_TAGS
		if(LookAhead(Parser) == TOK_LT)
		{
			GetToken(Parser);
			SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT);
			tag = strndup(Parser->TokenStr, Parser->TokenLen);
			SyntaxAssert(Parser, GetToken(Parser), TOK_GT);
		}
		#endif
		
		code = Parse_DoCodeBlock(Parser, CodeNode);
		SyntaxAssert( Parser, GetToken(Parser), TOK_RWD_WHILE );
		SyntaxAssert( Parser, GetToken(Parser), TOK_PAREN_OPEN );
		cond = Parse_DoExpr0(Parser);
		SyntaxAssert( Parser, GetToken(Parser), TOK_PAREN_CLOSE );
		ret = AST_NewLoop(Parser, tag, AST_NewNop(Parser), 1, cond, AST_NewNop(Parser), code);
		}
		break;
	case TOK_RWD_WHILE:
		{
		const char	*tag = "";
		tAST_Node	*code, *cond;
		GetToken(Parser);	// Eat 'while'
		
		#if SUPPORT_BREAK_TAGS
		if(LookAhead(Parser) == TOK_LT)
		{
			GetToken(Parser);
			SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT);
			tag = strndup(Parser->TokenStr, Parser->TokenLen);
			SyntaxAssert(Parser, GetToken(Parser), TOK_GT);
		}
		#endif
		
		SyntaxAssert( Parser, GetToken(Parser), TOK_PAREN_OPEN );
		cond = Parse_DoExpr0(Parser);
		SyntaxAssert( Parser, GetToken(Parser), TOK_PAREN_CLOSE );
		code = Parse_DoCodeBlock(Parser, CodeNode);
		ret = AST_NewLoop(Parser, tag, AST_NewNop(Parser), 0, cond, AST_NewNop(Parser), code);
		}
		return ret;
	
	// Define Variables
	case TOK_IDENT:
		ret = Parse_VarDefList(Parser, CodeNode, NULL);
		break;
	// Default
	default:
		//printf("exp0\n");
		ret = Parse_DoExpr0(Parser);
		break;
	}

	if( ret )	
		SyntaxAssert(Parser, GetToken(Parser), TOK_SEMICOLON );
	return ret;
}

tAST_Node *Parse_VarDefList(tParser *Parser, tAST_Node *CodeNode, tScript_Class *Class)
{
	tAST_Node	*ret;
	 int	type;
	
	ret = Parse_GetIdent(Parser, Class ? GETIDENTMODE_CLASS : GETIDENTMODE_EXPRROOT, Class);
	if( !ret || ret->Type != NODETYPE_DEFVAR ) {
		return ret;
	}
	type = ret->DefVar.DataType;
	while(GetToken(Parser) == TOK_COMMA)
	{
		if(CodeNode)	AST_AppendNode( CodeNode, ret );
		SyntaxAssert(Parser, GetToken(Parser), TOK_VARIABLE);
		
		ret = Parse_GetVarDef(Parser, type, Class);
		if(!ret)	longjmp(Parser->JmpTarget, -1);
	}
	if(CodeNode)	AST_AppendNode( CodeNode, ret );
	PutBack(Parser);	// Semicolon is checked by caller
	
	return ret;
}

/**
 * \brief Get a variable definition
 */
tAST_Node *Parse_GetVarDef(tParser *Parser, int Type, tScript_Class *Class)
{
	char	name[Parser->TokenLen];
	tAST_Node	*ret = NULL, *init = NULL;
	 int	level;
	
	SyntaxAssert(Parser, Parser->Token, TOK_VARIABLE);
	
	// copy the name (trimming the $)
	memcpy(name, Parser->TokenStr+1, Parser->TokenLen-1);
	name[Parser->TokenLen-1] = 0;
	
	// Handle arrays
	level = 0;
	if( LookAhead(Parser) == TOK_SQUARE_OPEN )
	{
		GetToken(Parser);
		// Only the highest level can be sized
		if( LookAhead(Parser) != TOK_SQUARE_CLOSE )
		{
			init = Parse_DoExpr0(Parser);
		}
		SyntaxAssert(Parser, GetToken(Parser), TOK_SQUARE_CLOSE);
	
		level ++;
	}
	while( LookAhead(Parser) == TOK_SQUARE_OPEN )
	{
		GetToken(Parser);
		SyntaxAssert(Parser, GetToken(Parser), TOK_SQUARE_CLOSE);
		level ++;
	}

	// Maul the type to denote the dereference level
	#if 0
	if( Parser->Variant->bDyamicTyped )
		Type = SS_DATATYPE_UNDEF;
	else
	#endif
		Type = SS_MAKEARRAYN(Type, level);

	// Initial value
	if( LookAhead(Parser) == TOK_ASSIGN )
	{
		if( init )
			SyntaxError(Parser, 1, "Cannot assign and set array size at the same time");
		GetToken(Parser);
		init = Parse_DoExpr0(Parser);
		if(!init)	return NULL;
	}
	else if( init )
	{
		init = AST_NewCreateArray(Parser, Type, init);
	}
	
	if( Class ) {
		AST_AppendClassProperty(Parser, Class, name, Type);
	}
	else {
		ret = AST_NewDefineVar(Parser, Type, name);
		ret->DefVar.InitialValue = init;
	}
	return ret;
}

/**
 * \brief Assignment Operations
 */
tAST_Node *Parse_DoExpr0(tParser *Parser)
{
	#define _next	Parse_DoExpr1
	tAST_Node	*ret = _next(Parser);
	 int	cont = 1;

	while( cont )
	{
		// Check Assignment
		switch(GetToken(Parser))
		{
		case TOK_ASSIGN:
			ret = AST_NewAssign(Parser, NODETYPE_NOP, ret, _next(Parser));
			break;
		case TOK_ASSIGN_DIV:
			ret = AST_NewAssign(Parser, NODETYPE_DIVIDE, ret, _next(Parser));
			break;
		case TOK_ASSIGN_MUL:
			ret = AST_NewAssign(Parser, NODETYPE_MULTIPLY, ret, _next(Parser));
			break;
		case TOK_ASSIGN_PLUS:
			ret = AST_NewAssign(Parser, NODETYPE_ADD, ret, _next(Parser));
			break;
		case TOK_ASSIGN_MINUS:
			ret = AST_NewAssign(Parser, NODETYPE_SUBTRACT, ret, _next(Parser));
			break;
		default:
			#if DEBUG >= 2
			printf("Parse_DoExpr0: Parser->Token = %s\n", csaTOKEN_NAMES[Parser->Token]);
			#endif
			PutBack(Parser);
			cont = 0;
			break;
		}
	}
	return ret;
	#undef _next
}

/**
 * \brief Logical/Boolean Operators
 */
tAST_Node *Parse_DoExpr1(tParser *Parser)
{
	#define _next	Parse_DoExpr2
	tAST_Node	*ret = _next(Parser);
	 int	cont = 1;

	while( cont )
	{
		switch(GetToken(Parser))
		{
		case TOK_LOGICAND:
			ret = AST_NewBinOp(Parser, NODETYPE_LOGICALAND, ret, _next(Parser));
			break;
		case TOK_LOGICOR:
			ret = AST_NewBinOp(Parser, NODETYPE_LOGICALOR, ret, _next(Parser));
			break;
		case TOK_LOGICXOR:
			ret = AST_NewBinOp(Parser, NODETYPE_LOGICALXOR, ret, _next(Parser));
			break;
		default:
			PutBack(Parser);
			cont = 0;
			break;
		}
	}
	return ret;
	#undef _next
}

// --------------------
// Expression 2 - Comparison Operators
// --------------------
tAST_Node *Parse_DoExpr2(tParser *Parser)
{
	#define _next	Parse_DoExpr3
	tAST_Node	*ret = _next(Parser);
	 int	cont = 1;

	while( cont )
	{
		// Check token
		switch(GetToken(Parser))
		{
		case TOK_EQUALS:
			ret = AST_NewBinOp(Parser, NODETYPE_EQUALS, ret, _next(Parser));
			break;
		case TOK_NOTEQUALS:
			ret = AST_NewBinOp(Parser, NODETYPE_NOTEQUALS, ret, _next(Parser));
			break;
		case TOK_LT:
			ret = AST_NewBinOp(Parser, NODETYPE_LESSTHAN, ret, _next(Parser));
			break;
		case TOK_GT:
			ret = AST_NewBinOp(Parser, NODETYPE_GREATERTHAN, ret, _next(Parser));
			break;
		case TOK_LTE:
			ret = AST_NewBinOp(Parser, NODETYPE_LESSTHANEQUAL, ret, _next(Parser));
			break;
		case TOK_GTE:
			ret = AST_NewBinOp(Parser, NODETYPE_GREATERTHANEQUAL, ret, _next(Parser));
			break;
		default:
			PutBack(Parser);
			cont = 0;
			break;
		}
	}
	return ret;
	#undef _next
}

/**
 * \brief Bitwise Operations
 */
tAST_Node *Parse_DoExpr3(tParser *Parser)
{
	#define _next	Parse_DoExpr4
	tAST_Node	*ret = _next(Parser);
	 int	cont = 1;

	while( cont )
	{
		// Check Token
		switch(GetToken(Parser))
		{
		case TOK_OR:
			ret = AST_NewBinOp(Parser, NODETYPE_BWOR, ret, _next(Parser));
			break;
		case TOK_AND:
			ret = AST_NewBinOp(Parser, NODETYPE_BWAND, ret, _next(Parser));
			break;
		case TOK_XOR:
			ret = AST_NewBinOp(Parser, NODETYPE_BWXOR, ret, _next(Parser));
			break;
		default:
			PutBack(Parser);
			cont = 0;
			break;
		}
	}
	return ret;
	#undef _next
}

// --------------------
// Expression 4 - Shifts
// --------------------
tAST_Node *Parse_DoExpr4(tParser *Parser)
{
	#define _next	Parse_DoExpr5
	tAST_Node *ret = _next(Parser);
	 int	cont = 1;

	while( cont )
	{
		switch(GetToken(Parser))
		{
		case TOK_SHL:
			ret = AST_NewBinOp(Parser, NODETYPE_BITSHIFTLEFT, ret, _next(Parser));
			break;
		case TOK_SHR:
			ret = AST_NewBinOp(Parser, NODETYPE_BITSHIFTRIGHT, ret, _next(Parser));
			break;
		default:
			PutBack(Parser);
			cont = 0;
			break;
		}
	}

	return ret;
	#undef _next
}

// --------------------
// Expression 5 - Arithmatic
// --------------------
tAST_Node *Parse_DoExpr5(tParser *Parser)
{
	#define _next	Parse_DoExpr6
	tAST_Node *ret = _next(Parser);
	 int	cont = 1;
	
	// While loop is added to ensure that the evaluation order ends up as
	// right to left.
	// E.g. a + b + c + d ends up as (((a + b) + c) + d) for casting
	while( cont )
	{
		switch(GetToken(Parser))
		{
		case TOK_PLUS:
			ret = AST_NewBinOp(Parser, NODETYPE_ADD, ret, _next(Parser));
			break;
		case TOK_MINUS:
			ret = AST_NewBinOp(Parser, NODETYPE_SUBTRACT, ret, _next(Parser));
			break;
		default:
			PutBack(Parser);
			cont = 0;
			break;
		}
	}

	return ret;
	#undef _next
}

// --------------------
// Expression 6 - Multiplcation & Division
// --------------------
tAST_Node *Parse_DoExpr6(tParser *Parser)
{
	#define _next	Parse_DoExpr7
	tAST_Node *ret = _next(Parser);
	 int	cont = 1;

	while( cont )
	{
		switch(GetToken(Parser))
		{
		case TOK_MUL:
			ret = AST_NewBinOp(Parser, NODETYPE_MULTIPLY, ret, _next(Parser));
			break;
		case TOK_DIV:
			ret = AST_NewBinOp(Parser, NODETYPE_DIVIDE, ret, _next(Parser));
			break;
		default:
			PutBack(Parser);
			cont = 0;
			break;
		}
	}

	return ret;
	#undef _next
}

// --------------------
// Expression 7 - Right Unary Operations
// --------------------
tAST_Node *Parse_DoExpr7(tParser *Parser)
{
	tAST_Node *ret = Parse_DoExpr8(Parser);
	
	switch(GetToken(Parser))
	{
	case TOK_INCREMENT:
		ret = AST_NewUniOp(Parser, NODETYPE_POSTINC, ret);
		break;
	case TOK_DECREMENT:
		ret = AST_NewUniOp(Parser, NODETYPE_POSTDEC, ret);
		break;
	default:
		PutBack(Parser);
		break;
	}
	return ret;
}

// --------------------
// Expression 8 - Left Unary Operations
// --------------------
tAST_Node *Parse_DoExpr8(tParser *Parser)
{
	switch(GetToken(Parser))
	{
	case TOK_INCREMENT:
		return AST_NewAssign(Parser, NODETYPE_ADD, Parse_DoExpr8(Parser), AST_NewInteger(Parser, 1));
	case TOK_DECREMENT:
		return AST_NewAssign(Parser, NODETYPE_SUBTRACT, Parse_DoExpr8(Parser), AST_NewInteger(Parser, 1));
	case TOK_MINUS:
		return AST_NewUniOp(Parser, NODETYPE_NEGATE, Parse_DoExpr8(Parser));
	case TOK_LOGICNOT:
		return AST_NewUniOp(Parser, NODETYPE_LOGICALNOT, Parse_DoExpr8(Parser));
	case TOK_BWNOT:
		return AST_NewUniOp(Parser, NODETYPE_BWNOT, Parse_DoExpr8(Parser));
	default:
		PutBack(Parser);
		return Parse_DoParen(Parser);
	}
}

// --------------------
// 2nd Last Expression - Parens
// --------------------
tAST_Node *Parse_DoParen(tParser *Parser)
{
	if(LookAhead(Parser) == TOK_PAREN_OPEN)
	{
		tAST_Node	*ret;
		 int	type;
		GetToken(Parser);
		
		switch(LookAhead(Parser))
		{
		// Handle casts if the identifer gives a valid type
		case TOK_IDENT:
			GetToken(Parser);
			type = SpiderScript_GetTypeCodeEx(Parser->Script, Parser->TokenStr, Parser->TokenLen);
			if( type != -1 )
			{
				SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_CLOSE);
				DEBUGS2("Casting to %i", type);
				ret = AST_NewCast(Parser, type, Parse_DoParen(Parser));
				break;
			}
			DEBUGS2("'%.*s' doesn't give a type", Parser->TokenLen, Parser->TokenStr);
			PutBack(Parser);
			// Fall through
		default:		
			ret = Parse_DoExpr0(Parser);
			SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_CLOSE);
			break;
		}
		return ret;
	}
	else
		return Parse_DoValue(Parser);
}

// --------------------
// Last Expression - Value
// --------------------
tAST_Node *Parse_DoValue(tParser *Parser)
{
	 int	tok = LookAhead(Parser);

	DEBUGS2("tok = %i (%s)", tok, csaTOKEN_NAMES[tok]);

	switch(tok)
	{
	case TOK_STR:
		return Parse_GetString(Parser);
	case TOK_INTEGER:
		return Parse_GetNumeric(Parser);
	
	case TOK_REAL:
		GetToken(Parser);
		return AST_NewReal( Parser, atof(Parser->TokenStr) );
	
	case TOK_IDENT:
		return Parse_GetIdent(Parser, GETIDENTMODE_VALUE, NULL);
	case TOK_VARIABLE:
		return Parse_GetVariable(Parser);
	case TOK_RWD_NEW:
		GetToken(Parser);
		return Parse_GetIdent(Parser, GETIDENTMODE_NEW, NULL);

	default:
		SyntaxError(Parser, 1, "Unexpected %s, expected TOK_T_VALUE\n",
			csaTOKEN_NAMES[tok]);
		longjmp( Parser->JmpTarget, -1 );
	}
}

/**
 * \brief Get a string
 */
tAST_Node *Parse_GetString(tParser *Parser)
{
	tAST_Node	*ret;
	 int	i, j;
	GetToken( Parser );
	
	{
		char	data[ Parser->TokenLen - 2 ];
		j = 0;
		
		for( i = 1; i < Parser->TokenLen - 1; i++ )
		{
			if( Parser->TokenStr[i] == '\\' ) {
				i ++;
				switch( Parser->TokenStr[i] )
				{
				case 'n':	data[j++] = '\n';	break;
				case 'r':	data[j++] = '\r';	break;
				default:
					// TODO: Octal Codes
					// TODO: Error/Warning?
					SyntaxError(Parser, 1, "Unknown escape code \\%c", Parser->TokenStr[i]);
					break;
				}
			}
			else {
				data[j++] = Parser->TokenStr[i];
			}
		}
		
		// TODO: Parse Escape Codes
		ret = AST_NewString( Parser, data, j );
	}
	return ret;
}

/**
 * \brief Get a numeric value
 */
tAST_Node *Parse_GetNumeric(tParser *Parser)
{
	uint64_t	value = 0;
	const char	*pos;
	SyntaxAssert( Parser, GetToken( Parser ), TOK_INTEGER );
	pos = Parser->TokenStr;
	//printf("pos = %p, *pos = %c\n", pos, *pos);
		
	if( *pos == '0' )
	{
		pos ++;
		if(*pos == 'x') {
			pos ++;
			for( ;; pos++)
			{
				value *= 16;
				if( '0' <= *pos && *pos <= '9' ) {
					value += *pos - '0';
					continue;
				}
				if( 'A' <= *pos && *pos <= 'F' ) {
					value += *pos - 'A' + 10;
					continue;
				}
				if( 'a' <= *pos && *pos <= 'f' ) {
					value += *pos - 'a' + 10;
					continue;
				}
				break;
			}
		}
		else {
			while( '0' <= *pos && *pos <= '7' ) {
				value = value*8 + *pos - '0';
				pos ++;
			}
		}
	}
	else {
		while( '0' <= *pos && *pos <= '9' ) {
			value = value*10 + *pos - '0';
			pos ++;
		}
	}
	
	return AST_NewInteger( Parser, value );
}

/**
 * \brief Get a variable
 */
tAST_Node *Parse_GetVariable(tParser *Parser)
{
	tAST_Node	*ret;
	SyntaxAssert( Parser, GetToken(Parser), TOK_VARIABLE );
	{
		char	name[Parser->TokenLen];
		memcpy(name, Parser->TokenStr+1, Parser->TokenLen-1);
		name[Parser->TokenLen-1] = 0;
		ret = AST_NewVariable( Parser, name );
		#if DEBUG >= 2
		printf("Parse_GetVariable: name = '%s'\n", name);
		#endif
	}
	for(;;)
	{
		GetToken(Parser);
		if( Parser->Token == TOK_SQUARE_OPEN )
		{
			ret = AST_NewBinOp(Parser, NODETYPE_INDEX, ret, Parse_DoExpr0(Parser));
			SyntaxAssert(Parser, GetToken(Parser), TOK_SQUARE_CLOSE);
			continue ;
		}
		if( Parser->Token == TOK_ELEMENT )
		{
			SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT);
			// Method Call
			if( LookAhead(Parser) == TOK_PAREN_OPEN )
			{
				char	name[Parser->TokenLen+1];
				memcpy(name, Parser->TokenStr, Parser->TokenLen);
				name[Parser->TokenLen] = 0;
				ret = AST_NewMethodCall(Parser, ret, name);
				GetToken(Parser);	// Eat the '('
				// Read arguments
				if( GetToken(Parser) != TOK_PAREN_CLOSE )
				{
					PutBack(Parser);
					do {
						AST_AppendFunctionCallArg( ret, Parse_DoExpr0(Parser) );
					} while(GetToken(Parser) == TOK_COMMA);
					SyntaxAssert( Parser, Parser->Token, TOK_PAREN_CLOSE );
				}
				
			}
			// Attribute
			else
			{
				char	name[Parser->TokenLen+1];
				memcpy(name, Parser->TokenStr, Parser->TokenLen);
				name[Parser->TokenLen] = 0;
				ret = AST_NewClassElement(Parser, ret, name);
			}
			continue ;
		}
		
		break ;
	}
	PutBack(Parser);
	return ret;
}

/**
 * \brief Get an identifier (constant or function call)
 */
tAST_Node *Parse_GetIdent(tParser *Parser, enum eGetIdentMode Mode, tScript_Class *Class)
{
	tAST_Node	*ret = NULL;
	 int	namelen = 0;
	char	*name = NULL;

	#define BC_NS_SEPARATOR	'@'	

	do {
		SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT );
		name = realloc(name, namelen + Parser->TokenLen + 1);
		if(namelen)
			name[namelen-1] = BC_NS_SEPARATOR;
		memcpy(name+namelen, Parser->TokenStr, Parser->TokenLen);
		namelen += Parser->TokenLen + 1;
		name[namelen-1] = '\0';
	}
	#if USE_SCOPE_CHAR
	while( GetToken(Parser) == TOK_SCOPE );
	PutBack(Parser);
	#else
	while( 0 );
	#endif
	
	GetToken(Parser);

	#if DEBUG
	printf("_GetIdent: Mode = %i, Token = %s\n", Mode, csaTOKEN_NAMES[Parser->Token]);
	#endif
		
	if( Parser->Token == TOK_IDENT )
	{
		DEBUGS2("Function definition");
		// Function definition
		int type = SpiderScript_GetTypeCode(Parser->Script, name);
		free(name);
		
		if( Mode != GETIDENTMODE_EXPRROOT
		 && Mode != GETIDENTMODE_CLASS
		 && Mode != GETIDENTMODE_NAMESPACE )
		{
			SyntaxError(Parser, 1, "Function definition within expression");
			return NULL;
		}
		PutBack(Parser);
		Parse_FunctionDefinition(Class, Parser, type);
		return NULL;
	}
	else if( Parser->Token == TOK_VARIABLE )
	{
		DEBUGS2("Variable definition");
		// Variable definition
		int type = SpiderScript_GetTypeCode(Parser->Script, name);
		free(name);
		
		if( Mode != GETIDENTMODE_EXPRROOT
		 && Mode != GETIDENTMODE_CLASS
		 && Mode != GETIDENTMODE_FUNCTIONDEF )
		{
			SyntaxError(Parser, 1, "Variable definition within expression (%i)", Mode);
			return NULL;
		}
		return Parse_GetVarDef(Parser, type, Class);
	}
	else if( Parser->Token == TOK_PAREN_OPEN )
	{
		DEBUGS2("Function call");
		// Function call / object creation
		if( Mode != GETIDENTMODE_VALUE
		 && Mode != GETIDENTMODE_EXPRROOT
		 && Mode != GETIDENTMODE_NEW )
		{
			SyntaxError(Parser, 1, "Function call within class/namespace definition");
			return NULL;
		}		

		#if DEBUG >= 2
		printf("Parse_GetIdent: Calling '%s'\n", name);
		#endif
		// Function Call
		if( Mode == GETIDENTMODE_NEW )
			ret = AST_NewCreateObject( Parser, name );
		else
			ret = AST_NewFunctionCall( Parser, name );
		// Read arguments
		if( GetToken(Parser) != TOK_PAREN_CLOSE )
		{
			PutBack(Parser);
			do {
				#if DEBUG >= 2
				printf(" Parse_GetIdent: Argument\n");
				#endif
				AST_AppendFunctionCallArg( ret, Parse_DoExpr0(Parser) );
			} while(GetToken(Parser) == TOK_COMMA);
			SyntaxAssert( Parser, Parser->Token, TOK_PAREN_CLOSE );
			#if DEBUG >= 2
			printf(" Parse_GetIdent: All arguments parsed\n");
			#endif
		}
	}
	else
	{
		DEBUGS2("Constant");
		// Runtime Constant / Variable (When implemented)
		if( Mode != GETIDENTMODE_VALUE
		 && Mode != GETIDENTMODE_EXPRROOT
		 && Mode != GETIDENTMODE_NEW )
		{
			SyntaxError(Parser, 1, "Code within class/namespace definition");
			return NULL;
		}		

		#if DEBUG >= 2
		printf("Parse_GetIdent: Referencing '%s'\n", name);
		#endif
		PutBack(Parser);
		if( Mode == GETIDENTMODE_NEW )	// Void constructor (TODO: Should this be an error?)
			ret = AST_NewCreateObject( Parser, name );
		else
			ret = AST_NewConstant( Parser, name );
	}
	
	free(name);
	#if DEBUG >= 2
	printf("Parse_GetIdent: Return %p\n", ret);
	#endif
	
	return ret;
}


void SyntaxError_(tParser *Parser, int Line, int bFatal, const char *Message, ...)
{
	va_list	args;
	va_start(args, Message);
	fprintf(stderr, "%s:%i: (parse.c:%i) error: ", Parser->Filename, Parser->CurLine, Line);
	vfprintf(stderr, Message, args);
	fprintf(stderr, "\n");
	va_end(args);
	
	if( bFatal ) {
		longjmp(Parser->JmpTarget, -1);
		Parser->ErrorHit = 1;
	}
}

void SyntaxAssert_(tParser *Parser, int Line, int Have, int Want)
{
	if( Have != Want )
	{
		SyntaxError_(Parser, Line, 1, "Unexpected %s(%i), expecting %s(%i)\n",
			csaTOKEN_NAMES[Have], Have, csaTOKEN_NAMES[Want], Want);
	}
}
