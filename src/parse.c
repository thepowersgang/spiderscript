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
#define MAX_INCLUDE_DEPTH	5

#if DEBUG >= 2
# define DEBUGS2(s, v...)	printf("%s: "s"\n", __func__, ## v)
#else
# define DEBUGS2(...)	do{}while(0)
#endif
#if DEBUG >= 1
# define DEBUGS1(s, v...)	printf("%s: "s"\n", __func__, ## v)
#else
# define DEBUGS1(...)	do{}while(0)
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
 int	Parse_IncludeFile(tParser *Parser, const char *NewFile, int NewFileLen, tAST_Node *RootCode, int Depth);
 int 	Parse_BufferInt(tSpiderScript *Script, const char *Buffer, const char *Filename, tAST_Node *MainCode, int Depth);
 int	Parse_Buffer(tSpiderScript *Script, const char *Buffer, const char *Filename);
 int	Parse_NamespaceContent(tParser *Parser);
 int	Parse_ClassDefinition(tParser *Parser);
 int	Parse_FunctionDefinition(tScript_Class *Class, tParser *Parser, int Type);
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

int	SyntaxAssert_(tParser *Parser, int SourceLine, int Have, int Want);
void	SyntaxError_(tParser *Parser, int SourceLine, const char *Message, ...);
#define SyntaxAssert(p,h,w)	SyntaxAssert_(p,__LINE__,h,w)
#define SyntaxError(p,m...)	SyntaxError_(p,__LINE__,m)

#define TODO(Parser, message...) do {\
	fprintf(stderr, "TODO: "message);\
	longjmp(Parser->JmpTarget, -1);\
}while(0)

// === CODE ===
int Parse_IncludeFile(tParser *Parser, const char *NewFile, int NewFileLen, tAST_Node *RootCode, int Depth)
{
	char *path;

	if( Depth == MAX_INCLUDE_DEPTH ) {
		// Include depth exceeded
		return -1;
	}

	if( !Parser->Filename ) {
		// Including disabled because the variant didn't give us a path
		return -1;
	}	

	if( NewFile[0] == '/' ) {
		path = strndup(NewFile, NewFileLen);
	}
	else {
		int len = strlen(Parser->Filename);
		while( len && Parser->Filename[len-1] != '/' )
			len --;
		
		path = malloc( len + NewFileLen + 1 );
		memcpy(path, Parser->Filename, len);
		memcpy(path+len, NewFile, NewFileLen);
		path[len + NewFileLen] = 0;
	}

	FILE	*fp = fopen(path, "r");
	fseek(fp, 0, SEEK_END);
	 int	flen = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	
	char	*data = malloc(flen+1);
	fread(data, 1, flen, fp);
	data[flen] = 0;
	fclose(fp);
	
	int rv = Parse_BufferInt(Parser->Script, data, path, RootCode, Depth+1);

	free(data);
	free(path);
	return rv;
}

/**
 * \brief Parse a buffer into a syntax tree
 */
int Parse_BufferInt(tSpiderScript *Script, const char *Buffer, const char *Filename, tAST_Node *MainCode, int Depth)
{
	tParser	parser = {0};
	tParser *Parser = &parser;	//< Keeps code consistent
	tAST_Node	*node;
	 int	bRootFile = 0;
	
	DEBUGS2("(Script=%p, Buffer=%p)", Script, Buffer);
	
	// Initialise parser
	parser.LastToken = -1;
	parser.NextToken = -1;
	parser.CurLine = 1;
	parser.BufStart = Buffer;
	parser.CurPos = Buffer;
	// hackery to do reference counting
	parser.Filename = malloc(sizeof(int)+strlen(Filename)+1);
	strcpy(parser.Filename + sizeof(int), Filename);
	*(int*)(parser.Filename) = 0;	// Set reference count (zero so it's free'd by AST_FreeNode)
	parser.Filename += sizeof(int);	// Move filename
	parser.ErrorHit = 0;
	parser.Script = Script;
	parser.Variant = Script->Variant;
	
	bRootFile = !MainCode;
	if( !MainCode )
		MainCode = AST_NewCodeBlock(Parser);
	
	// Give us an error fallback
	if( setjmp( parser.JmpTarget ) != 0 ) {
		goto error_return;
	}
	
	// Parse the file!
	while(Parser->Token != TOK_EOF)
	{
		switch( GetToken(Parser) )
		{
		case TOK_EOF:
			break;

		case TOK_RWD_INCLUDE: {
			if( SyntaxAssert(Parser, GetToken(Parser), TOK_STR) )
				goto error_return;
			int rv = Parse_IncludeFile(Parser, Parser->TokenStr+1, Parser->TokenLen-2, MainCode, Depth);
			if( rv < 0 )
				goto error_return;
			if( SyntaxAssert(Parser, GetToken(Parser), TOK_SEMICOLON) )
				goto error_return;
			break; }

		case TOK_RWD_CLASS:
			if( Parse_ClassDefinition(Parser) )
				goto error_return;
			break;
		case TOK_RWD_NAMESPACE:
			if( Parse_NamespaceContent(Parser) )
				goto error_return;
			break;

		// Ordinary Statement
		default:
			PutBack(Parser);
			node = Parse_DoBlockLine(Parser, MainCode);
			if(!node)
				goto error_return;
			if(node != ERRPTR)
				AST_AppendNode( MainCode, node );
			break;
		}
		
		// Jump to error handler on error
		if(Parser->ErrorHit)
			longjmp(Parser->JmpTarget, -1);
	}

	// Return 0 by default
	if( bRootFile )
	{
		AST_AppendNode( MainCode, AST_NewUniOp(Parser, NODETYPE_RETURN, AST_NewInteger(Parser, 0)) );
		AST_AppendFunction( Parser, "", SS_DATATYPE_INTEGER, NULL, MainCode );
	}
	
	//printf("---- %p parsed as SpiderScript ----\n", Buffer);
	return 0;
	
error_return:
	if( bRootFile ) {
		AST_FreeNode( MainCode );
	}
	return -1;
}

int Parse_Buffer(tSpiderScript *Script, const char *Buffer, const char *Filename)
{
	return Parse_BufferInt(Script, Buffer, Filename, NULL, 0);
}

int Parse_NamespaceContent(tParser *Parser)
{
	char	*name;
	
	SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT);
	name = strndup( Parser->TokenStr, Parser->TokenLen );
	SyntaxAssert(Parser, GetToken(Parser), TOK_BRACE_OPEN);

	// Within a namespace, only classes and functions can be defined
	while( GetToken(Parser) != TOK_BRACE_CLOSE )
	{
		switch( Parser->Token )
		{
		case TOK_RWD_CLASS:
			if( Parse_ClassDefinition(Parser) ) {
				free(name);
				return -1;
			}
			break;
		case TOK_RWD_NAMESPACE:
			if( Parse_NamespaceContent(Parser) ) {
				free(name);
				return -1;
			}
			break;
		
		// Static type function
		case TOK_IDENT:
			PutBack(Parser);
			if( !Parse_GetIdent(Parser, GETIDENTMODE_NAMESPACE, NULL) ) {
				free(name);
				return -1;
			}
			break;
	
		default:
			SyntaxError(Parser, "Unexpected %s, Expected class/namespace/function definition\n",
				csaTOKEN_NAMES[Parser->Token]);
			free(name);
			return -1;
		}
	}
	return 0;
}

int Parse_ClassDefinition(tParser *Parser)
{
	char *name;
	tScript_Class	*class;
	
	// Get name of the class and create the definition
	SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT);
	name = strndup( Parser->TokenStr, Parser->TokenLen );
	class = AST_AppendClass(Parser, name);
	free(name);
	
	if( SyntaxAssert(Parser, GetToken(Parser), TOK_BRACE_OPEN) ) {
		// No need to free the class, as it's in-tree now
		return -1;
	}

	while( GetToken(Parser) != TOK_BRACE_CLOSE )
	{
		switch( Parser->Token )
		{
		case TOK_IDENT:
			PutBack(Parser);
			if( Parse_GetIdent(Parser, GETIDENTMODE_CLASS, class) == NULL )
				return -1;
			// TODO: Comma separated attributes?
			break;
		default:
			SyntaxError(Parser, "Unexpected %s in class", csaTOKEN_NAMES[Parser->Token]);
			return -1;
		}
	}
	return 0;
}

int Parse_FunctionDefinition(tScript_Class *Class, tParser *Parser, int Type)
{
	 int	rv = 0;
	tAST_Node	*first_arg = NULL, *last_arg, *code;
	
	last_arg = (void*)&first_arg;	// HACK
	
	SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT );
	
	char name[Parser->TokenLen+1];
	memcpy(name, Parser->TokenStr, Parser->TokenLen);
	name[Parser->TokenLen] = 0;
	DEBUGS1("DefFCN %s", name);
	
	// Get arguments
	if( SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_OPEN ) ) {
		rv = -1;
		goto _return;
	}
	if( LookAhead(Parser) != TOK_PAREN_CLOSE )
	{
		do {
			tAST_Node *def;
			def = Parse_GetIdent(Parser, GETIDENTMODE_FUNCTIONDEF, NULL);
			if( !def ) { rv = -1; goto _return; }
			last_arg->NextSibling = def;
			last_arg = def;
			last_arg->NextSibling = NULL;
		} while(GetToken(Parser) == TOK_COMMA);
	}
	else
		GetToken(Parser);
	
	if( SyntaxAssert(Parser, Parser->Token, TOK_PAREN_CLOSE)  ) {
		rv = -1;
		goto _return;
	}

	DEBUGS2("-- Parsing function '%s' code", name);
	if( !(code = Parse_DoCodeBlock(Parser, NULL)) ) {
		rv = -1;
		goto _return;
	}
	DEBUGS2("-- Done '%s' code", name);

	if( Class )
		rv = AST_AppendMethod( Parser, Class, name, Type, first_arg, code );
	else
		rv = AST_AppendFunction( Parser, name, Type, first_arg, code );

_return:
	// Clean up argument definition nodes
	{
		tAST_Node	*nextarg;
		for( ; first_arg; first_arg = nextarg )
		{
			nextarg = first_arg->NextSibling;
			AST_FreeNode(first_arg);
		}
	}

	// Error check after cleaning up, just in case :)
	return rv;
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
		if(!node) {
			AST_FreeNode(ret);
			return NULL;
		}
		if( node && node != ERRPTR )
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
	
	DEBUGS2("First token %s", csaTOKEN_NAMES[LookAhead(Parser)]);
	switch(LookAhead(Parser))
	{
	// New block
	case TOK_BRACE_OPEN:
		return Parse_DoCodeBlock(Parser, CodeNode);
	
	// Empty statement
	case TOK_SEMICOLON:
		GetToken(Parser);
		return AST_NewNop(Parser);
	
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
		default:	SyntaxError(Parser, "BUG!!!");	ret = NULL;	break;
		}
		if(ident)	free(ident);
		}
		break;
	
	// Control Statements
	case TOK_RWD_IF:
		{
		tAST_Node	*cond = NULL, *true = NULL, *false = NULL;
		GetToken(Parser);	// eat the if

		DEBUGS2("for statement");		
		
		if( SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_OPEN) )
			goto _if_err_ret;
		if( !(cond = Parse_DoExpr0(Parser)) )
			goto _if_err_ret;
		if( SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_CLOSE) )
			goto _if_err_ret;
		if( !(true = Parse_DoCodeBlock(Parser, CodeNode)) )
			goto _if_err_ret;
		
		if( LookAhead(Parser) == TOK_RWD_ELSE ) {
			GetToken(Parser);
			false = Parse_DoCodeBlock(Parser, CodeNode);
		}
		else
			false = AST_NewNop(Parser);
		
		if(!false)
			goto _if_err_ret;
		
		ret = AST_NewIf(Parser, cond, true, false);
		return ret;
	_if_err_ret:
		if(cond)	AST_FreeNode(cond);
		if(true)	AST_FreeNode(true);
		if(false)	AST_FreeNode(false);
		return NULL;
		}
	
	case TOK_RWD_FOR:
		{
		char	*tag = NULL;
		tAST_Node	*init=NULL, *cond=NULL, *inc=NULL, *code = NULL;
		GetToken(Parser);	// Eat 'for'
		
		DEBUGS2("for loop");
		
		#if SUPPORT_BREAK_TAGS
		if(LookAhead(Parser) == TOK_LT)
		{
			GetToken(Parser);
			if( SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT) )
				goto _for_err_ret;
			tag = strndup(Parser->TokenStr, Parser->TokenLen);
			if( SyntaxAssert(Parser, GetToken(Parser), TOK_GT) )
				goto _for_err_ret;
		}
		#endif
		
		if( SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_OPEN) )
			goto _for_err_ret;
		
		// Initialiser (special handling for definitions)
		if(LookAhead(Parser) != TOK_SEMICOLON) {
			// TODO: When will this go out of scope?
			if(LookAhead(Parser) == TOK_IDENT)
				init = Parse_GetIdent(Parser, GETIDENTMODE_EXPRROOT, NULL);
			else
				init = Parse_DoExpr0(Parser);
			if( !init )
				goto _for_err_ret;
		}
		
		// SEPARATOR
		if( SyntaxAssert(Parser, GetToken(Parser), TOK_SEMICOLON) )
			goto _for_err_ret;
		
		// Condition
		if( LookAhead(Parser) != TOK_SEMICOLON && !(cond = Parse_DoExpr0(Parser)) )
			goto _for_err_ret;
		
		// SEPARATOR
		if( SyntaxAssert(Parser, GetToken(Parser), TOK_SEMICOLON) )
			goto _for_err_ret;
		
		// Increment
		if( LookAhead(Parser) != TOK_PAREN_CLOSE && !(inc = Parse_DoExpr0(Parser)) )
			goto _for_err_ret;
		
		// CLOSE
		if( SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_CLOSE) )
			goto _for_err_ret;
		
		// Code
		if( !(code = Parse_DoCodeBlock(Parser, CodeNode)) )
			goto _for_err_ret;
		
		ret = AST_NewLoop(Parser, tag, init, 0, cond, inc, code);
		if(tag)	free(tag);
		return ret;	// No break, because no semicolon is needed
	_for_err_ret:
		if(tag)	free(tag);
		if(init)	AST_FreeNode(init);
		if(cond)	AST_FreeNode(cond);
		if(inc) 	AST_FreeNode(inc);
		if(code)	AST_FreeNode(code);
		return NULL;
		}
	
	case TOK_RWD_DO:
		{
		char	*tag = NULL;
		tAST_Node	*code = NULL, *cond = NULL;
		GetToken(Parser);	// Eat 'do'
		
		DEBUGS2("do-while loop");

		#if SUPPORT_BREAK_TAGS
		if(LookAhead(Parser) == TOK_LT)
		{
			GetToken(Parser);
			if( SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT) )
				goto _do_err_ret;
			tag = strndup(Parser->TokenStr, Parser->TokenLen);
			if( SyntaxAssert(Parser, GetToken(Parser), TOK_GT) )
				goto _do_err_ret;
		}
		#endif
		
		// Code
		if( !(code = Parse_DoCodeBlock(Parser, CodeNode)) )
			goto _do_err_ret;
		// "while("
		if( SyntaxAssert( Parser, GetToken(Parser), TOK_RWD_WHILE ) )
			goto _do_err_ret;
		if( SyntaxAssert( Parser, GetToken(Parser), TOK_PAREN_OPEN ) )
			goto _do_err_ret;
		// Condition 
		if( !(cond = Parse_DoExpr0(Parser)) )
			goto _do_err_ret;
		if( SyntaxAssert( Parser, GetToken(Parser), TOK_PAREN_CLOSE ) )
			goto _do_err_ret;
		ret = AST_NewLoop(Parser, tag, AST_NewNop(Parser), 1, cond, AST_NewNop(Parser), code);
		if(tag)	free(tag);
		break;	// Break because do-while needs a semicolon
	_do_err_ret:
		if(tag)	free(tag);
		if(code)	AST_FreeNode(code);
		if(cond)	AST_FreeNode(cond);
		return NULL;
		}
	
	case TOK_RWD_WHILE:
		{
		char	*tag = NULL;
		tAST_Node	*code = NULL, *cond = NULL;
		GetToken(Parser);	// Eat 'while'
		
		DEBUGS2("While loop");		

		#if SUPPORT_BREAK_TAGS
		if(LookAhead(Parser) == TOK_LT)
		{
			GetToken(Parser);
			if( SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT) )
				goto _while_err_ret;
			tag = strndup(Parser->TokenStr, Parser->TokenLen);
			if( SyntaxAssert(Parser, GetToken(Parser), TOK_GT) )
				goto _while_err_ret;
		}
		#endif

		if( SyntaxAssert( Parser, GetToken(Parser), TOK_PAREN_OPEN ) )
			goto _while_err_ret;
		if( !(cond = Parse_DoExpr0(Parser)) )
			goto _while_err_ret;
		if( SyntaxAssert( Parser, GetToken(Parser), TOK_PAREN_CLOSE ) )
			goto _while_err_ret;
		if( !(code = Parse_DoCodeBlock(Parser, CodeNode)) )
			goto _while_err_ret;
		ret = AST_NewLoop(Parser, tag, AST_NewNop(Parser), 0, cond, AST_NewNop(Parser), code);
		if(tag)	free(tag);
		return ret;
	_while_err_ret:
		if(tag)	free(tag);
		if(cond)	AST_FreeNode(cond);
		if(code)	AST_FreeNode(code);
		return NULL;
		}
	
	// Define Variables / Functions / Call functions
	case TOK_IDENT:
		ret = Parse_VarDefList(Parser, CodeNode, NULL);
		if(ret == ERRPTR)
			return ret;	// Early return to avoid the semicolon check
		break;
	// Default
	default:
		ret = Parse_DoExpr0(Parser);
		break;
	}

	DEBUGS2("ret = %p", ret);
	if( ret && SyntaxAssert(Parser, GetToken(Parser), TOK_SEMICOLON ) ) {
		AST_FreeNode(ret);
		ret = NULL;
	}
	return ret;
}

tAST_Node *Parse_VarDefList(tParser *Parser, tAST_Node *CodeNode, tScript_Class *Class)
{
	tAST_Node	*ret;
	 int	type;
	
	ret = Parse_GetIdent(Parser, Class ? GETIDENTMODE_CLASS : GETIDENTMODE_EXPRROOT, Class);
	if( !ret || ret == ERRPTR || ret->Type != NODETYPE_DEFVAR ) {
		// Either an error, class attribute, or a function call/definition
		return ret;
	}
	
	// Make sure that CodeNode is set if there's a definition list
	if( LookAhead(Parser) == TOK_COMMA && !CodeNode ) {
		SyntaxError(Parser, "Unexpected TOK_COMMA");
		AST_FreeNode(ret);
		return NULL;
	}
	
	type = ret->DefVar.DataType;
	while(GetToken(Parser) == TOK_COMMA)
	{
		AST_AppendNode( CodeNode, ret );
		
		if( SyntaxAssert(Parser, GetToken(Parser), TOK_VARIABLE) )
			return NULL;	// No free needed as ret is now in CodeNode
		
		ret = Parse_GetVarDef(Parser, type, Class);
		if(!ret)	break;	// Return the NULL
	}
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
	
	SyntaxAssert(Parser, Parser->Token, TOK_VARIABLE);
	
	// copy the name (trimming the $)
	memcpy(name, Parser->TokenStr+1, Parser->TokenLen-1);
	name[Parser->TokenLen-1] = 0;
	
	// Initial value
	if( LookAhead(Parser) == TOK_ASSIGN )
	{
		GetToken(Parser);
		init = Parse_DoExpr0(Parser);
		if(!init)	return NULL;
	}
	else if( LookAhead(Parser) == TOK_PAREN_OPEN )
	{
		GetToken(Parser);	// Eat '('
		
		// "String[] $string_array($size);"
		// or
		// "Object $object(...);"
		if( SS_GETARRAYDEPTH(Type) )
		{
			init = AST_NewCreateArray(Parser, Type, Parse_DoExpr0(Parser));
			if(!init)	return NULL;
		}
		else
		{
			init = AST_NewCreateObject( Parser, SpiderScript_GetTypeName(Parser->Script, Type) );
			// Read arguments
			if( LookAhead(Parser) != TOK_PAREN_CLOSE )
			{
				do {
					DEBUGS2(" Parse_GetIdent: Argument");
					tAST_Node	*arg = Parse_DoExpr0(Parser);
					if( !arg ) {
						AST_FreeNode(init);
						return NULL;
					}
					AST_AppendFunctionCallArg( init, arg );
				} while(GetToken(Parser) == TOK_COMMA);
				PutBack(Parser);
			}
		}
		if( SyntaxAssert( Parser, GetToken(Parser), TOK_PAREN_CLOSE ) ) {
			AST_FreeNode(init);
			return NULL;
		}
	}
	
	if( Class ) {
		if( init ) {
			SyntaxError(Parser, "TODO: Impliment initialisation of class properties");
			AST_FreeNode(init);
			return NULL;
		}
		AST_AppendClassProperty(Parser, Class, name, Type);
		if( SyntaxAssert(Parser, GetToken(Parser), TOK_SEMICOLON) )
			return NULL;
		ret = ERRPTR;	// Not an error, but used to avoid returning a non-error NULL
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
			DEBUGS2("Parser->Token = %s", csaTOKEN_NAMES[Parser->Token]);
			PutBack(Parser);
			cont = 0;
			break;
		}
	}
	DEBUGS2("back to %p", __builtin_return_address(0));
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
	 int	count = 0;

	while(cont)
	{
		if( count == 2 ) {
			SyntaxError(Parser, "Chaining comparisons doesn't do what you think it does");
			AST_FreeNode(ret);
			return NULL;
		}
		// Check token
		switch(GetToken(Parser))
		{
		case TOK_REFEQUALS:
			ret = AST_NewBinOp(Parser, NODETYPE_REFEQUALS, ret, _next(Parser));
			break;
		case TOK_REFNOTEQUALS:
			ret = AST_NewBinOp(Parser, NODETYPE_REFNOTEQUALS, ret, _next(Parser));
			break;
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
		count ++;
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
		GetToken(Parser);	// eat the '('
		
		if(LookAhead(Parser) == TOK_IDENT)
		{
			 int	type;
			GetToken(Parser);
			// Handle casts if the identifer gives a valid type
			type = SpiderScript_GetTypeCodeEx(Parser->Script, Parser->TokenStr, Parser->TokenLen);
			if( type != -1 )
			{
				if( SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_CLOSE) )
					return NULL;
				DEBUGS2("Casting to %i", type);
				ret = AST_NewCast(Parser, type, Parse_DoParen(Parser));
				return ret;
			}
			DEBUGS2("'%.*s' doesn't give a type", Parser->TokenLen, Parser->TokenStr);
			PutBack(Parser);
			// Fall through
		}
	
		DEBUGS2("Paren calling Expr0");	
		ret = Parse_DoExpr0(Parser);
		DEBUGS2("Paren got %p from Expr0", ret);	
		if( ret && SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_CLOSE) ) {
			AST_FreeNode(ret);
			return NULL;
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

	case TOK_RWD_TRUE:
		GetToken(Parser);
		return AST_NewBoolean( Parser, 1 );
	case TOK_RWD_FALSE:
		GetToken(Parser);
		return AST_NewBoolean( Parser, 0 );
	case TOK_RWD_NULL:
		GetToken(Parser);
		return AST_NewNullReference( Parser );

	case TOK_IDENT:
		return Parse_GetIdent(Parser, GETIDENTMODE_VALUE, NULL);
	case TOK_VARIABLE:
		return Parse_GetVariable(Parser);
	case TOK_RWD_NEW:
		GetToken(Parser);
		return Parse_GetIdent(Parser, GETIDENTMODE_NEW, NULL);

	default:
		SyntaxError(Parser, "Unexpected %s, expected TOK_T_VALUE",
			csaTOKEN_NAMES[tok]);
		return NULL;
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
					SyntaxError(Parser, "Unknown escape code \\%c", Parser->TokenStr[i]);
					return NULL;
				}
			}
			else {
				data[j++] = Parser->TokenStr[i];
			}
		}
		
		// TODO: Parse Escape Codes
		ret = AST_NewString( Parser, data, j );
		DEBUGS2("%i byte string", j);
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
	GetToken( Parser );
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

	if( SyntaxAssert( Parser, GetToken(Parser), TOK_VARIABLE ) )
		return NULL;
	
	{
		char	name[Parser->TokenLen];
		memcpy(name, Parser->TokenStr+1, Parser->TokenLen-1);
		name[Parser->TokenLen-1] = 0;
		ret = AST_NewVariable( Parser, name );
		DEBUGS2("name = '%s'", name);
	}
	
	for(;;)
	{
		GetToken(Parser);
		DEBUGS2("Token = %s", csaTOKEN_NAMES[Parser->Token]);
		if( Parser->Token == TOK_SQUARE_OPEN )
		{
			tAST_Node	*tmp;
			tmp = AST_NewBinOp(Parser, NODETYPE_INDEX, ret, Parse_DoExpr0(Parser));
			if( !tmp ) {
				AST_FreeNode(ret);
				return NULL;
			}
			ret = tmp;
			if( SyntaxAssert(Parser, GetToken(Parser), TOK_SQUARE_CLOSE) ) {
				AST_FreeNode(ret);
				return NULL;
			}
			continue ;
		}
		if( Parser->Token == TOK_ELEMENT )
		{
			if( SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT) ) {
				AST_FreeNode(ret);
				return NULL;
			}
			
			char	name[Parser->TokenLen+1];
			memcpy(name, Parser->TokenStr, Parser->TokenLen);
			name[Parser->TokenLen] = 0;
			
			// Method Call
			if( LookAhead(Parser) == TOK_PAREN_OPEN )
			{
				GetToken(Parser);	// Eat the '('
				ret = AST_NewMethodCall(Parser, ret, name);
				
				// Read arguments
				if( GetToken(Parser) != TOK_PAREN_CLOSE )
				{
					DEBUGS2("Method call %s has args", name);
					PutBack(Parser);
					do {
						tAST_Node *arg = Parse_DoExpr0(Parser);
						if( !arg ) {
							AST_FreeNode(ret);
							return NULL;
						}
						AST_AppendFunctionCallArg( ret, arg );
					} while(GetToken(Parser) == TOK_COMMA);
					
					if( SyntaxAssert( Parser, Parser->Token, TOK_PAREN_CLOSE ) ) {
						AST_FreeNode(ret);
						return NULL;
					}
				}
				else
					DEBUGS2("No args for method call %s", name);
				
			}
			// Attribute
			else
			{
				ret = AST_NewClassElement(Parser, ret, name);
			}
			continue ;
		}
		
		break ;
	}
	PutBack(Parser);
	DEBUGS2("Back to %p", __builtin_return_address(0));
	return ret;
}

/**
 * \brief Get an identifier (constant or function call)
 */
tAST_Node *Parse_GetIdent(tParser *Parser, enum eGetIdentMode Mode, tScript_Class *Class)
{
	tAST_Node	*ret = NULL;
	 int	namelen = 0;
	char	*tname = NULL;
	 int	level = 0;

	do {
		if( SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT ) ) {
			if(tname)	free(tname);
			return NULL;
		}
		tname = realloc(tname, namelen + Parser->TokenLen + 1);
		if(namelen)
			tname[namelen-1] = BC_NS_SEPARATOR;
		memcpy(tname+namelen, Parser->TokenStr, Parser->TokenLen);
		namelen += Parser->TokenLen + 1;
		tname[namelen-1] = '\0';
	}
	#if USE_SCOPE_CHAR
	while( GetToken(Parser) == TOK_SCOPE );
	PutBack(Parser);
	#else
	while( 0 );
	#endif
	
	// Create a stack-allocated copy of tname to avoid having to free it later
	char name[strlen(tname)+1];
	strcpy(name, tname);
	free(tname);
	
	GetToken(Parser);

	DEBUGS1("Mode = %i, Token = %s", Mode, csaTOKEN_NAMES[Parser->Token]);

	if( Parser->Token == TOK_SQUARE_OPEN )
	{
		do {
			if( SyntaxAssert(Parser, GetToken(Parser), TOK_SQUARE_CLOSE) )
				return NULL;
			level ++;
		} while(GetToken(Parser) == TOK_SQUARE_OPEN);
	}	

	if( Parser->Token == TOK_IDENT )
	{
		DEBUGS2("Function definition");
		// Function definition
		int type = SpiderScript_GetTypeCode(Parser->Script, name);
		if( type == -1 ) {
			SyntaxError(Parser, "Unknown type '%s'", name);
			return NULL;
		}

		type = SS_MAKEARRAYN(type, level);
		
		if( Mode != GETIDENTMODE_EXPRROOT
		 && Mode != GETIDENTMODE_CLASS
		 && Mode != GETIDENTMODE_NAMESPACE )
		{
			SyntaxError(Parser, "Function definition within expression");
			return NULL;
		}
		PutBack(Parser);
		
		if( Parse_FunctionDefinition(Class, Parser, type) )
			return NULL;
		
		ret = ERRPTR;	// Not an error
	}
	else if( Parser->Token == TOK_VARIABLE )
	{
		DEBUGS2("Variable definition");
		// Variable definition
		int type = SpiderScript_GetTypeCode(Parser->Script, name);
		if( type == -1 ) {
			SyntaxError(Parser, "Unknown type '%s'", name);
			return NULL;
		}
		// Apply level
		type = SS_MAKEARRAYN(type, level);
		
		if( Mode != GETIDENTMODE_EXPRROOT
		 && Mode != GETIDENTMODE_CLASS
		 && Mode != GETIDENTMODE_FUNCTIONDEF )
		{
			SyntaxError(Parser, "Variable definition within expression (%i)", Mode);
			return NULL;
		}

		ret = Parse_GetVarDef(Parser, type, Class);
	}
	else if( Parser->Token == TOK_PAREN_OPEN )
	{
		DEBUGS2("Function call");
		// Function call / object creation
		if( Mode != GETIDENTMODE_VALUE
		 && Mode != GETIDENTMODE_EXPRROOT
		 && Mode != GETIDENTMODE_NEW )
		{
			SyntaxError(Parser, "Function call within class/namespace definition");
			return NULL;
		}

		if( level )
		{
			SyntaxError(Parser, "Array definition unexpected");
			return NULL;
		}

		DEBUGS2("Parse_GetIdent: Calling '%s'", name);
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
				DEBUGS2(" Parse_GetIdent: Argument");
				tAST_Node	*arg = Parse_DoExpr0(Parser);
				if( !arg ) {
					AST_FreeNode(ret);
					return NULL;
				}
				AST_AppendFunctionCallArg( ret, arg );
			} while(GetToken(Parser) == TOK_COMMA);
			if( SyntaxAssert( Parser, Parser->Token, TOK_PAREN_CLOSE ) ) {
				AST_FreeNode(ret);
				return NULL;
			}
			DEBUGS2(" Parse_GetIdent: All arguments parsed");
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
			SyntaxError(Parser, "Code within class/namespace definition");
			return NULL;
		}
		
		if( level )
		{
			SyntaxError(Parser, "Constants cannot be arrays");
			return NULL;
		}

		DEBUGS2("Parse_GetIdent: Referencing '%s'", name);
		PutBack(Parser);
		if( Mode == GETIDENTMODE_NEW )	// Void constructor (TODO: Should this be an error?)
			ret = AST_NewCreateObject( Parser, name );
		else
			ret = AST_NewConstant( Parser, name );
	}
	
	DEBUGS2("Return %p", ret);	
	return ret;
}


void SyntaxError_(tParser *Parser, int Line, const char *Message, ...)
{
	va_list	args;
	 int	len = 0;
	va_start(args, Message);
	len += snprintf(NULL, 0, "%s:%i: (parse.c:%i) error: ", Parser->Filename, Parser->CurLine, Line);
	len += vsnprintf(NULL, 0, Message, args);
	len += snprintf(NULL, 0, "\n");
	va_end(args);
	char buffer[len+1];
	char	*buf = buffer;
	
	va_start(args, Message);
	buf += sprintf(buf, "%s:%i: (parse.c:%i) error: ", Parser->Filename, Parser->CurLine, Line);
	buf += vsprintf(buf, Message, args);
	buf += sprintf(buf, "\n");
	va_end(args);
		
	if( Parser->Variant->HandleError ) {
		Parser->Variant->HandleError(Parser->Script, buf);
	}
	else {
		fprintf(stderr, "%s", buf);
	}
}

int SyntaxAssert_(tParser *Parser, int Line, int Have, int Want)
{
	if( Have != Want )
	{
		SyntaxError_(Parser, Line, "Unexpected %s(%i), expecting %s(%i)",
			csaTOKEN_NAMES[Have], Have, csaTOKEN_NAMES[Want], Want);
		return 1;
	}
	return 0;
}
