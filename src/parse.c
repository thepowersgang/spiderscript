/*
 * Acess2 - SpiderScript
 * - Parser
 */
#define DEBUG	0
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <spiderscript.h>
#define WANT_TOKEN_STRINGS	1
#include "tokens.h"
#include "ast.h"
#include "common.h"
#include <assert.h>
#include <errno.h>

#define	SUPPORT_BREAK_TAGS	1
#define MAX_INCLUDE_DEPTH	5

enum eGetIdentMode
{
	GETIDENTMODE_VALUE,	// 0
	GETIDENTMODE_NEW,	// 1
	GETIDENTMODE_EXPR,	// 2
	GETIDENTMODE_ROOT,	// 2
	GETIDENTMODE_CLASS,	// 3
	GETIDENTMODE_NAMESPACE,	// 4
	GETIDENTMODE_FUNCTIONDEF, // 5
};

// === PROTOTYPES ===
 int	Parse_IncludeFile(tParser *Parser, const char *NewFile, int NewFileLen, tAST_Node *RootCode, int Depth);
 int 	Parse_BufferInt(tSpiderScript *Script, const char *Buffer, const char *Filename, tAST_Node *MainCode, int Depth);
 int	Parse_Buffer(tSpiderScript *Script, const char *Buffer, const char *Filename);
 int	Parse_NamespaceContent(tParser *Parser);
 int	Parse_ClassDefinition(tParser *Parser);
 int	Parse_FunctionDefinition(tScript_Class *Class, tParser *Parser, tSpiderTypeRef Type, const char *Name);
tAST_Node	*Parse_DoCodeBlock(tParser *Parser, tAST_Node *CodeNode);
tAST_Node	*Parse_DoBlockLine(tParser *Parser, tAST_Node *CodeNode);
tAST_Node	*Parse_VarDefList(tParser *Parser, tAST_Node *CodeNode, tScript_Class *Class);
tAST_Node	*Parse_GetVarDef(tParser *Parser, tSpiderTypeRef Type, tScript_Class *Class);

tAST_Node	*Parse_DoExpr0(tParser *Parser);	// Assignment
tAST_Node	*Parse_DoExprTernary(tParser *Parser);	// Ternary + Null-Coalescing
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
tAST_Node	*Parse_GetDynValue(tParser *Parser, tAST_Node *RootValue);
char	*Parse_ReadIdent(tParser *Parser);
 int	Parse_int_GetArrayDepth(tParser *Parser);
tAST_Node	*Parse_DoNew(tParser *Parser, const tSpiderScript_TypeDef *type, int level);
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
		SyntaxError(Parser, "Maximum include depth of %i exceeded", MAX_INCLUDE_DEPTH);
		return -1;
	}

	if( !Parser->Filename ) {
		// Including disabled because the variant didn't give us a path
		SyntaxError(Parser, "Include disabled");
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

	// TODO: Detect duplicate includes?

	FILE	*fp = fopen(path, "r");
	if( !fp ) {
		SyntaxError(Parser, "Can't open '%s': %s", path, strerror(errno));
		return -1;
	}
	fseek(fp, 0, SEEK_END);
	 int	flen = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	
	char	*data = malloc(flen+1);
	if( fread(data, 1, flen, fp) != flen ) {
		SyntaxError(Parser, "Can't load '%s': %s", path, strerror(errno));
		free(data);
		free(path);
		fclose(fp);
		return -1;
	}
	data[flen] = 0;
	fclose(fp);

	// TODO: Hash 'data' and check against list of loaded files	

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
	parser.Cur.Line = 1;
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
	while(Parser->Cur.Token != TOK_EOF)
	{
		switch( GetToken(Parser) )
		{
		case TOK_EOF:
			break;

		case TOK_RWD_INCLUDE: {
			if( SyntaxAssert(Parser, GetToken(Parser), TOK_STR) )
				goto error_return;
			int rv = Parse_IncludeFile(Parser,
				Parser->Cur.TokenStr+1, Parser->Cur.TokenLen-2,
				MainCode, Depth);
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
		
		case TOK_IDENT:
			PutBack(Parser);
			node = Parse_VarDefList(Parser, MainCode, SS_ERRPTR);
			if( !node )
				goto error_return;
			if(node != SS_ERRPTR) {
				AST_AppendNode( MainCode, node );
				if( SyntaxAssert(Parser, GetToken(Parser), TOK_SEMICOLON) )
					goto error_return;
			}
			break;

		// Ordinary Statement
		default:
			PutBack(Parser);
			node = Parse_DoBlockLine(Parser, MainCode);
			if(!node)
				goto error_return;
			if(node != SS_ERRPTR)
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
		const tSpiderTypeRef rettype = {.Def=&gSpiderScript_IntegerType, .ArrayDepth = 0};
		AST_AppendNode( MainCode, AST_NewUniOp(Parser, NODETYPE_RETURN, AST_NewInteger(Parser, 0)) );
		AST_AppendFunction( Parser, "", rettype, NULL, MainCode, false );
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
	#if WANT_TOKEN_STRINGS
	assert( sizeof(csaTOKEN_NAMES)/sizeof(csaTOKEN_NAMES[0]) == TOK_LAST+1 );
	#endif
	return Parse_BufferInt(Script, Buffer, Filename, NULL, 0);
}

int Parse_NamespaceContent(tParser *Parser)
{
	char	*name;
	
	SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT);
	name = strndup( Parser->Cur.TokenStr, Parser->Cur.TokenLen );
	SyntaxAssert(Parser, GetToken(Parser), TOK_BRACE_OPEN);

	// Within a namespace, only classes and functions can be defined
	while( GetToken(Parser) != TOK_BRACE_CLOSE )
	{
		switch( Parser->Cur.Token )
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
				csaTOKEN_NAMES[Parser->Cur.Token]);
			free(name);
			return -1;
		}
	}
	return 0;
}

int Parse_ClassDefinition(tParser *Parser)
{
	// Get name of the class and create the definition
	SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT);
	char *name = strndup( Parser->Cur.TokenStr, Parser->Cur.TokenLen );
	tScript_Class *class = AST_AppendClass(Parser, name);

	if( GetToken(Parser) == TOK_SEMICOLON )
	{
		// Forward definition
		DEBUGS1("DefCLASS %s Forward", name);
		free(name);
		return 0;
	}

	DEBUGS1("DefCLASS %s Full", name);
	free(name);
	
	// TODO: Support 'Extends/Implements'
	
	if( SyntaxAssert(Parser, Parser->Cur.Token, TOK_BRACE_OPEN) )
	{
		// No need to free the class, as it's in-tree now
		return -1;
	}

	if( class == NULL || AST_IsClassFinal(class) )
	{
		SyntaxError(Parser, "Redefinition of class '%s'", class->Name);
		return -1;
	}

	while( GetToken(Parser) != TOK_BRACE_CLOSE )
	{
		switch( Parser->Cur.Token )
		{
		case TOK_IDENT:
			PutBack(Parser);
			if( Parse_GetIdent(Parser, GETIDENTMODE_CLASS, class) == NULL )
				return -1;
			// TODO: Comma separated attributes?
			break;
		default:
			SyntaxError(Parser, "Unexpected %s in class", csaTOKEN_NAMES[Parser->Cur.Token]);
			return -1;
		}
	}
	
	AST_FinaliseClass(Parser, class);
	return 0;
}

/**
 * \brief Function
 */
int Parse_FunctionDefinition(tScript_Class *Class, tParser *Parser, tSpiderTypeRef Type, const char *Name)
{
	 int	rv = 0;
	tAST_Node	*first_arg = NULL;
	tAST_Node	**arg_next_ptr = &first_arg;
	tAST_Node	*code;
	bool	bVariable = false;
	DEBUGS1("DefFCN %s", Name);
	
	// Get arguments
	if( SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_OPEN ) ) {
		rv = -1;
		goto _return;
	}
	if( LookAhead(Parser) != TOK_PAREN_CLOSE )
	{
		do {
			tAST_Node *def;
			if( LookAhead(Parser) == TOK_ELIPSIS ) {
				GetToken(Parser);
				break;
			}
			def = Parse_GetIdent(Parser, GETIDENTMODE_FUNCTIONDEF, NULL);
			if( !def ) {
				rv = -1;
				goto _return;
			}
			DEBUGS1("Arg '%s'", def->DefVar.Name);
			def->NextSibling = NULL;
			*arg_next_ptr = def;
			arg_next_ptr = &def->NextSibling;
		} while(GetToken(Parser) == TOK_COMMA);
		
		if( Parser->Cur.Token == TOK_ELIPSIS )
		{
			// Function takes a variable argument count
			bVariable = true;
			GetToken(Parser);
		}
	}
	else
		GetToken(Parser);
	
	if( SyntaxAssert(Parser, Parser->Cur.Token, TOK_PAREN_CLOSE)  ) {
		rv = -1;
		goto _return;
	}

	// Function attributes (after definition)
	for( bool loop = true; loop; )
	{
		switch(GetToken(Parser))
		{
		case TOK_RWD_CONSTANT:
			loop = true;
			//function_is_const = true;
			break;
		default:
			loop = false;
			PutBack(Parser);
			break;
		}
	}

	DEBUGS2("-- Parsing function '%s' code", Name);
	code = Parse_DoCodeBlock(Parser, NULL);
	if( !code ) {
		rv = -1;
		goto _return;
	}
	DEBUGS2("-- Done '%s' code (%p)", Name, code);

	DEBUGS1("- first_arg=%p", first_arg);
	if( Class && Class != SS_ERRPTR )
		rv = AST_AppendMethod( Parser, Class, Name, Type, first_arg, code, bVariable );
	else
		rv = AST_AppendFunction( Parser, Name, Type, first_arg, code, bVariable );

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
	DEBUGS2_DOWN();
	DEBUGS2("DoCodeBlock");
	
	// Check if we are being called for a one-liner
	if( GetToken(Parser) != TOK_BRACE_OPEN ) {
		PutBack(Parser);
		ret = Parse_DoBlockLine(Parser, CodeNode);
		DEBUGS2_UP();
		return ret;
	}
	
	ret = AST_NewCodeBlock(Parser);
	
	while( LookAhead(Parser) != TOK_BRACE_CLOSE )
	{
		tAST_Node *node = Parse_DoBlockLine(Parser, ret);
		if(!node) {
			AST_FreeNode(ret);
			return NULL;
		}
		if( node && node != SS_ERRPTR )
			AST_AppendNode( ret, node );
	}
	GetToken(Parser);	// Omnomnom
	DEBUGS2_UP();
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

	//case TOK_RWD_DELETE:
	//	GetToken(Parser);
	//	ret = AST_NewUniOp(Parser, NODETYPE_DELETE, Parse_GetVariable(Parser));
	//	break;
	
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
			ident = strndup(Parser->Cur.TokenStr, Parser->Cur.TokenLen);
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
		tAST_Node	*cond = NULL, *truecode = NULL, *falsecode = NULL;
		GetToken(Parser);	// eat the if

		DEBUGS2("for statement");		
		
		if( SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_OPEN) )
			goto _if_err_ret;
		if( !(cond = Parse_DoExpr0(Parser)) )
			goto _if_err_ret;
		if( SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_CLOSE) )
			goto _if_err_ret;
		if( !(truecode = Parse_DoCodeBlock(Parser, CodeNode)) )
			goto _if_err_ret;

#if 0
		while( LookAhead(Parser) == TOK_RWD_ELSEIF ) {
			GetToken();
			if( SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_OPEN) )
				goto _if_err_ret;
			if( !(cond2 = Parse_DoExpr0(Parser)) )
				goto _if_err_ret;
			if( SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_CLOSE) )
				goto _if_err_ret;
			if( !(eliftrue = Parse_DoCodeBlock(Parser, CodeNode)) )
				goto _if_err_ret;
		}
#endif
	
		if( LookAhead(Parser) == TOK_RWD_ELSE ) {
			GetToken(Parser);
			falsecode = Parse_DoCodeBlock(Parser, CodeNode);
		}
		else
			falsecode = AST_NewNop(Parser);
		
		if(!falsecode)
			goto _if_err_ret;
		
		ret = AST_NewIf(Parser, cond, truecode, falsecode);
		return ret;
	_if_err_ret:
		if(cond)	AST_FreeNode(cond);
		if(truecode)	AST_FreeNode(truecode);
		if(falsecode)	AST_FreeNode(falsecode);
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
			tag = strndup(Parser->Cur.TokenStr, Parser->Cur.TokenLen);
			if( SyntaxAssert(Parser, GetToken(Parser), TOK_GT) )
				goto _for_err_ret;
		}
		#endif
		
		if( SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_OPEN) )
			goto _for_err_ret;
		
		// Initialiser (special handling for definitions)
		if(LookAhead(Parser) != TOK_SEMICOLON) {
			if(LookAhead(Parser) == TOK_IDENT)
				// Expecting a variable definition
				init = Parse_GetIdent(Parser, GETIDENTMODE_EXPR, NULL);
			else
				init = Parse_DoExpr0(Parser);
			if( !init )
				goto _for_err_ret;
		}

		if( LookAhead(Parser) == TOK_COLON )
		{
			DEBUGS2("actually iterator");
			GetToken(Parser);
			
			// "for( $array : $index, $value)"
			// "for( $array : $value)"
			//               ^ -- here
			if( SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT) )
				goto _for_err_ret;
			char *it_name = NULL;
			char *val_name = strndup(Parser->Cur.TokenStr, Parser->Cur.TokenLen);
			
			if( LookAhead(Parser) == TOK_COMMA )
			{
				// Listed both index and value names
				GetToken(Parser);
				if( SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT) ) {
					free(val_name);
					goto _for_err_ret;
				}
				it_name = val_name;
				val_name = strndup(Parser->Cur.TokenStr, Parser->Cur.TokenLen);
			}
			DEBUGS2("it_name=%s,val_name=%s", it_name, val_name);
			
			if( SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_CLOSE) ) {
				free(it_name);
				free(val_name);
				goto _for_err_ret;
			}
			
			// Code
			if( !(code = Parse_DoCodeBlock(Parser, CodeNode)) ) {
				free(it_name);
				free(val_name);
				goto _for_err_ret;
			}
			
			ret = AST_NewIterator(Parser, tag, init, it_name, val_name, code);
			free(it_name);
			free(val_name);
		}
		else
		{

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
		}
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
			tag = strndup(Parser->Cur.TokenStr, Parser->Cur.TokenLen);
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
			tag = strndup(Parser->Cur.TokenStr, Parser->Cur.TokenLen);
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

	// Switch statement
	case TOK_RWD_SWITCH:
		{
		GetToken(Parser);	// Eat the 'switch'
		tAST_Node	*val = NULL;
		if( SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_OPEN) )
			return NULL;
		if( !(val = Parse_DoExpr0(Parser)) )
			return NULL;
		ret = AST_NewBinOpN(Parser, NODETYPE_SWITCH, val, NULL);
		if( SyntaxAssert( Parser, GetToken(Parser), TOK_PAREN_CLOSE ) )
			goto _switch_err_ret;
		if( SyntaxAssert( Parser, GetToken(Parser), TOK_BRACE_OPEN ) )
			goto _switch_err_ret;

		while( LookAhead(Parser) != TOK_BRACE_CLOSE )
		{
			GetToken(Parser);
			tAST_Node *caseval = NULL;
			if( Parser->Cur.Token == TOK_RWD_CASE ) {
				caseval = Parse_DoValue(Parser);
				if( !caseval )
					goto _switch_err_ret;
			}
			else if( Parser->Cur.Token == TOK_RWD_DEFAULT )
				caseval = NULL;
			else {
				SyntaxAssert(Parser, Parser->Cur.Token, TOK_RWD_CASE);
				goto _switch_err_ret;
			}
			SyntaxAssert(Parser, GetToken(Parser), TOK_COLON);
			tAST_Node *code = AST_NewCodeBlock(Parser);
			while( LookAhead(Parser) != TOK_RWD_CASE
			    && LookAhead(Parser) != TOK_BRACE_CLOSE
			    && LookAhead(Parser) != TOK_RWD_DEFAULT
				)
			{
				tAST_Node *node = Parse_DoBlockLine(Parser, code);
				if(!node) {
					AST_FreeNode(code);
					AST_FreeNode(caseval);
					goto _switch_err_ret;
				}
				AST_AppendNode(code, node);
			}
			AST_AppendNode(ret, AST_NewBinOpN(Parser, NODETYPE_CASE, caseval, code));
		}
		GetToken(Parser);

		return ret;
	_switch_err_ret:
		if(ret)	AST_FreeNode(ret);
		return NULL;
		}

	// Global variable
	case TOK_RWD_GLOBAL:
		GetToken(Parser);	// Eat the 'global'
		if( SyntaxAssert(Parser, LookAhead(Parser), TOK_IDENT) )
			return NULL;
		// Expecting variable definition
		ret = Parse_GetIdent(Parser, GETIDENTMODE_EXPR, NULL);
		if( !ret || ret == SS_ERRPTR )
			return ret;
		if( ret->Type != NODETYPE_DEFVAR ) {
			// Oops?
			AST_FreeNode(ret);
			return NULL;
		}
		ret->Type = NODETYPE_DEFGLOBAL;
		if( ret->DefVar.InitialValue ) {
			// Nope?
		}
		break;

	// Auto-detected variable types
	case TOK_RWD_AUTO:
		GetToken(Parser);	// eat 'auto'
		GetToken(Parser);	// get variable
		ret = Parse_GetVarDef(Parser, (tSpiderTypeRef){NULL,0}, NULL);
		if(ret == SS_ERRPTR)
			return ret;	// Early return to avoid the semicolon check
		break;
	// Define Variables / Functions / Call functions
	case TOK_IDENT:
		ret = Parse_VarDefList(Parser, CodeNode, NULL);
		if(ret == SS_ERRPTR)
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
	tSpiderTypeRef	type;

	enum eGetIdentMode	mode;
	if( Class == NULL )
		mode = GETIDENTMODE_EXPR;	// Vardef only
	else if( Class == SS_ERRPTR )
		mode = GETIDENTMODE_ROOT;	// Allows functions
	else
		mode = GETIDENTMODE_CLASS;	// Same, but handles other member stuff

	ret = Parse_GetIdent(Parser, mode, Class);
	if( !ret || ret == SS_ERRPTR ) {
		// Either an error or a  class attribute
		return ret;
	}
	
	// Success, but was a call/definition
	if( ret->Type != NODETYPE_DEFVAR )
		return ret;
	
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
		
		if( SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT) )
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
tAST_Node *Parse_GetVarDef(tParser *Parser, tSpiderTypeRef Type, tScript_Class *Class)
{
	char	name[Parser->Cur.TokenLen];
	tAST_Node	*ret = NULL, *init = NULL;
	
	SyntaxAssert(Parser, Parser->Cur.Token, TOK_IDENT);
	
	// copy the name (trimming the $)
	memcpy(name, Parser->Cur.TokenStr, Parser->Cur.TokenLen);
	name[Parser->Cur.TokenLen] = 0;
	
	// Initial value
	if( LookAhead(Parser) == TOK_ASSIGN )
	{
		GetToken(Parser);
		
		if( LookAhead(Parser) == TOK_RWD_NEW )
		{
			GetToken(Parser);
			// Handle new specially, as typename is now optional
			if( GetToken(Parser) == TOK_IDENT )
			{
				PutBack(Parser);
				// Oh, you're providing a type name... ok
				init = Parse_GetIdent(Parser, GETIDENTMODE_NEW, NULL);
			}
			else if( Parser->Cur.Token == TOK_PAREN_OPEN )
			{
				init = Parse_DoNew(Parser, Type.Def, Type.ArrayDepth);
			}
			else
			{
				// Bail
				SyntaxAssert(Parser, Parser->Cur.Token, TOK_PAREN_OPEN);
				return NULL;
			}
		}
		else
		{
			init = Parse_DoExpr0(Parser);
			if(!init)	return NULL;
		}
	}
	#if 0
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
			init = AST_NewCreateObject( Parser, Type );
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
	#endif
	
	if( Class && Class != SS_ERRPTR ) {
		if( init ) {
			SyntaxError(Parser, "TODO: Impliment initialisation of class properties");
			AST_FreeNode(init);
			return NULL;
		}
		AST_AppendClassProperty(Parser, Class, name, Type);
		if( SyntaxAssert(Parser, GetToken(Parser), TOK_SEMICOLON) )
			return NULL;
		ret = SS_ERRPTR;	// Not an error, but used to avoid returning a non-error NULL
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
	#define _next	Parse_DoExprTernary
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
			DEBUGS2("Hit %s, putback", csaTOKEN_NAMES[Parser->Cur.Token]);
			PutBack(Parser);
			cont = 0;
			break;
		}
	}
	DEBUGS2("back to %p", __builtin_return_address(0));
	return ret;
	#undef _next
}

tAST_Node *Parse_DoExprTernary(tParser *Parser)
{
	#define _cur	Parse_DoExprTernary
	#define _next	Parse_DoExpr1
	tAST_Node	*ret = _next(Parser);
	
	switch( GetToken(Parser) )
	{
	case TOK_QUESTIONMARK: {
		tAST_Node	*trueval = _next(Parser);
		SyntaxAssert(Parser, GetToken(Parser), TOK_COLON);
		tAST_Node	*falseval = _cur(Parser);
		ret = AST_NewTernary(Parser, ret, trueval, falseval);
		break; }
	case TOK_QMARKCOLON: {
		tAST_Node	*nullval = _cur(Parser);
		ret = AST_NewTernary(Parser, ret, NULL, nullval);
		break; }
	default:
		PutBack(Parser);
		break;
	}
	return ret;
	#undef _next
	#undef _cur
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
		case TOK_MODULO:
			ret = AST_NewBinOp(Parser, NODETYPE_MODULO, ret, _next(Parser));
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
	tAST_Node	*ret;
	DEBUGS2_DOWN();
	if(LookAhead(Parser) == TOK_PAREN_OPEN)
	{
		GetToken(Parser);	// eat the '('
		
		if(LookAhead(Parser) == TOK_IDENT)
		{
			tParser	saved = *Parser;
			
			DEBUGS2("trying cast");
			char *name = Parse_ReadIdent(Parser);
			if(!name)	return NULL;
			const tSpiderScript_TypeDef *type = SpiderScript_GetType(Parser->Script, name);
			free(name);
			if( type != SS_ERRPTR )
			{
				int level = Parse_int_GetArrayDepth(Parser);
				if( SyntaxAssert(Parser, Parser->Cur.Token, TOK_PAREN_CLOSE) ) {
					return NULL;
				}
				tSpiderTypeRef	ref = {type,level};
				DEBUGS2("Casting to %s", SpiderScript_GetTypeName(Parser->Script, ref));
				ret = AST_NewCast(Parser, ref, Parse_DoParen(Parser));
			}
			else
			{
				DEBUGS2("fallback Expr0");
				*Parser = saved;
				goto _expr0;
			}
		}
		else
		{
		_expr0:
			ret = Parse_DoExpr0(Parser);
			DEBUGS2("Paren got %p", ret);
			if( ret && SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_CLOSE) ) {
				AST_FreeNode(ret);
				return NULL;
			}
		}
	}
	else
		ret = Parse_DoValue(Parser);
	DEBUGS2_UP();
	return ret;
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
		return AST_NewReal( Parser, strtod(Parser->Cur.TokenStr, NULL) );

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
	case TOK_RWD_NEW:
		GetToken(Parser);
		return Parse_GetIdent(Parser, GETIDENTMODE_NEW, NULL);
	
	#if 0
	// Elipsis - Variable argument access
	case TOK_ELIPSIS:
		GetToken(Parser);
		// "...[index]" Access an individual argument
		if( LookAhead(Parser) == TOK_SQUARE_OPEN ) {
			GetToken(Parser);
			ret = AST_NewVArgInd(Parser, Parse_DoExpr0(Parser));
			SyntaxAssert(Parser, GetToken(Parser), TOK_SQUARE_CLOSE);
			return ret;
		}
		// Used to pass through arguments
		return AST_NewVariable(Parser, "...");
	#endif

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
	GetToken( Parser );
	
	{
		char	data[ Parser->Cur.TokenLen - 2 ];
		 int	j = 0;
		
		for( int i = 1; i < Parser->Cur.TokenLen - 1; i++ )
		{
			if( Parser->Cur.TokenStr[i] == '\\' ) {
				i ++;
				if( i == Parser->Cur.TokenLen - 1 ) {
					SyntaxError(Parser, "\\ at end of string");
					return NULL;
				}
				switch( Parser->Cur.TokenStr[i] )
				{
				case 'n':	data[j++] = '\n';	break;
				case 'r':	data[j++] = '\r';	break;
				case '"':	data[j++] = '"';	break;
				default:
					// TODO: Octal Codes
					// TODO: Error/Warning?
					SyntaxError(Parser, "Unknown escape code \\%c", Parser->Cur.TokenStr[i]);
					return NULL;
				}
			}
			else {
				data[j++] = Parser->Cur.TokenStr[i];
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
	pos = Parser->Cur.TokenStr;
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

tAST_Node *Parse_DoFunctionArgs(tParser *Parser, tAST_Node *FcnNode)
{
	if( GetToken(Parser) != TOK_PAREN_CLOSE )
	{
		DEBUGS2("Call has args");
		PutBack(Parser);
		do {
			// If the last argument is '...', mark the call as a varg parssthrough
			if( LookAhead(Parser) == TOK_ELIPSIS ) {
				GetToken(Parser);	// eat ...
				GetToken(Parser);	// Should eat the )
				AST_SetCallVArgPassthrough(FcnNode);
				break;
			}
			DEBUGS2_DOWN();
			tAST_Node *arg = Parse_DoExpr0(Parser);
			DEBUGS2_UP();
			if( !arg ) {
				AST_FreeNode(FcnNode);
				return NULL;
			}
			DEBUGS2("--");
			AST_AppendFunctionCallArg( FcnNode, arg );
		} while(GetToken(Parser) == TOK_COMMA && Parser->Cur.Token != TOK_INVAL);
		
		if( SyntaxAssert( Parser, Parser->Cur.Token, TOK_PAREN_CLOSE ) ) {
			AST_FreeNode(FcnNode);
			return NULL;
		}
		DEBUGS2("Done");
	}
	else
		DEBUGS2("No args for call");
	return FcnNode;
}

/**
 * \brief Handle arrays/elements
 * \note Frees RootValue if an error is encountered
 */
tAST_Node *Parse_GetDynValue(tParser *Parser, tAST_Node *RootValue)
{
	tAST_Node *ret = RootValue;
	for(;;)
	{
		GetToken(Parser);
		DEBUGS2("Token = %s", csaTOKEN_NAMES[Parser->Cur.Token]);
		if( Parser->Cur.Token == TOK_SQUARE_OPEN )
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
		if( Parser->Cur.Token == TOK_ELEMENT )
		{
			if( SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT) ) {
				AST_FreeNode(ret);
				return NULL;
			}
			
			char	name[Parser->Cur.TokenLen+1];
			memcpy(name, Parser->Cur.TokenStr, Parser->Cur.TokenLen);
			name[Parser->Cur.TokenLen] = 0;
			
			// Method Call
			if( LookAhead(Parser) == TOK_PAREN_OPEN )
			{
				GetToken(Parser);	// Eat the '('
				ret = AST_NewMethodCall(Parser, ret, name);
				
				// Read arguments
				ret = Parse_DoFunctionArgs(Parser, ret);
				if( !ret )
					return NULL;
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

char *Parse_ReadIdent(tParser *Parser)
{
	size_t	namelen = 0;
	char	*tname = NULL;
	
	do {
		if( SyntaxAssert(Parser, GetToken(Parser), TOK_IDENT ) ) {
			if(tname)	free(tname);
			return NULL;
		}
		tname = realloc(tname, namelen + Parser->Cur.TokenLen + 1);
		if(namelen)
			tname[namelen-1] = BC_NS_SEPARATOR;
		memcpy(tname+namelen, Parser->Cur.TokenStr, Parser->Cur.TokenLen);
		namelen += Parser->Cur.TokenLen + 1;
		tname[namelen-1] = '\0';
	}
	#if USE_SCOPE_CHAR
	while( GetToken(Parser) == TOK_SCOPE );
	#else
	while( 0 );
	GetToken(Parser);
	#endif

	return tname;
}

int Parse_int_GetArrayDepth(tParser *Parser)
{
	 int	level = 0;
	if( Parser->Cur.Token == TOK_SQUARE_OPEN )
	{
		do {
			if( SyntaxAssert(Parser, GetToken(Parser), TOK_SQUARE_CLOSE) )
				return -1;
			level ++;
		} while(GetToken(Parser) == TOK_SQUARE_OPEN);
	}
	return level;
}

tAST_Node *Parse_DoNew(tParser *Parser, const tSpiderScript_TypeDef *type, int level)
{
	tAST_Node *ret;
	if( level == 0 )
	{
		// New Object
		DEBUGS2("Parse_DoNew: Create '%s'",
			SpiderScript_GetTypeName(Parser->Script, (tSpiderTypeRef){.Def=type}));
		ret = AST_NewCreateObject( Parser, type );
		// Read arguments
		return Parse_DoFunctionArgs(Parser, ret);
	}
	else
	{
		// Reallocate array
		tSpiderTypeRef	ref = {.Def = type, .ArrayDepth = level};
		DEBUGS2("Parse_DoNew: Create array '%s'",
			SpiderScript_GetTypeName(Parser->Script, ref));
		
		ret = AST_NewCreateArray(Parser, ref, Parse_DoExpr0(Parser));
		SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_CLOSE);
		return ret;
	}

}

/**
 * \brief Get an identifier (constant or function call)
 */
tAST_Node *Parse_GetIdent(tParser *Parser, enum eGetIdentMode Mode, tScript_Class *Class)
{
	tAST_Node	*ret = NULL;
	 int	level = 0;
	tParser	saved_parser = *Parser;

	DEBUGS2_DOWN();
	DEBUGS1("Mode = %i", Mode);

	char *tname = Parse_ReadIdent(Parser);
	if( !tname ) {
		DEBUGS2_UP();
		return NULL;
	}
	
	// Create a stack-allocated copy of tname to avoid having to free it later
	char name[strlen(tname)+1];
	strcpy(name, tname);
	free(tname);
	
	// Resolve the type, but don't check yet
	const tSpiderScript_TypeDef *type = SpiderScript_GetType(Parser->Script, name);
	if( type != SS_ERRPTR ) {
		level = Parse_int_GetArrayDepth(Parser);
		if( level < 0 ) {
			DEBUGS2_UP();
			return NULL;
		}
	}
	
	// If the type is invalid, and we're at the root of a function
	if( type == SS_ERRPTR && (Mode == GETIDENTMODE_EXPR || Mode == GETIDENTMODE_ROOT) )
	{
		DEBUGS2("type (%s) == SS_ERRPTR, DoExpr0", name);
		// We actually hit a function call/variable deref, and should do Expr0
		*Parser = saved_parser;
		ret = Parse_DoExpr0(Parser);
		DEBUGS2_UP();
		return ret;
	}

	DEBUGS1("Mode = %i, Token = %s", Mode, csaTOKEN_NAMES[Parser->Cur.Token]);

	if( Parser->Cur.Token == TOK_RWD_OPERATOR )
	{
		DEBUGS2("Operator override");
		// Function definition
		if( type == SS_ERRPTR ) {
			SyntaxError(Parser, "Unknown type '%s'", name);
			return NULL;
		}
		tSpiderTypeRef	ref = {.Def = type, .ArrayDepth = level};

		if( Mode != GETIDENTMODE_CLASS ) {
			SyntaxError(Parser, "Operator override outside of class (Mode %i)", Mode);
			return NULL;
		}
		const char *fcnname = NULL;
		bool	is_heap = false;
		switch(GetToken(Parser))
		{
		case TOK_MODULO:	fcnname = "operator %";	break;
		case TOK_PAREN_OPEN:
			SyntaxAssert(Parser, GetToken(Parser), TOK_PAREN_CLOSE);
			fcnname = SpiderScript_FormatTypeStr1(Parser->Script, "operator (%s)", ref);
			is_heap = true;
			break;
		default:
			SyntaxError(Parser, "Unknown operator token %s", csaTOKEN_NAMES[Parser->Cur.Token]);
			return NULL;
		}

		// If rv != 0, return NULL (error)
		ret = (Parse_FunctionDefinition(Class, Parser, ref, fcnname) ? NULL : SS_ERRPTR);
		if(is_heap)	free((char*)fcnname);
	}
	else if( Parser->Cur.Token == TOK_IDENT )
	{
		DEBUGS2("Function/variable definition");
		// Function definition
		if( type == SS_ERRPTR ) {
			SyntaxError(Parser, "Unknown type '%s'", name);
			return NULL;
		}

		tSpiderTypeRef	ref = {.Def = type, .ArrayDepth = level};
		
		if( LookAhead(Parser) == TOK_PAREN_OPEN )
		{
			if( Mode != GETIDENTMODE_ROOT
			 && Mode != GETIDENTMODE_CLASS
			 && Mode != GETIDENTMODE_NAMESPACE)
			{
				SyntaxError(Parser, "Function definition not expected (Mode %i)", Mode);
				return NULL;
			}
			DEBUGS2("- Function definition");
			
			char fcnname[Parser->Cur.TokenLen+1];
			memcpy(fcnname, Parser->Cur.TokenStr, Parser->Cur.TokenLen);
			fcnname[Parser->Cur.TokenLen] = 0;
			if( Parse_FunctionDefinition(Class, Parser, ref, fcnname) )
				return NULL;
			ret = SS_ERRPTR;	// Not an error
		}
		else
		{
			if( Mode != GETIDENTMODE_ROOT
			 && Mode != GETIDENTMODE_EXPR
			 && Mode != GETIDENTMODE_CLASS
			 && Mode != GETIDENTMODE_NAMESPACE
			 && Mode != GETIDENTMODE_FUNCTIONDEF)
			{
				SyntaxError(Parser, "Variable definition within expression (Mode %i)", Mode);
				return NULL;
			}
			DEBUGS2("- Variable definition");
			ret = Parse_GetVarDef(Parser, ref, Class);
		}
	}
	else if( Parser->Cur.Token == TOK_PAREN_OPEN )
	{
		DEBUGS2("Function call");
		// Function call / object creation
		if( Mode != GETIDENTMODE_VALUE
		 && Mode != GETIDENTMODE_NEW )
		{
			SyntaxError(Parser, "Function call within class/namespace definition");
			return NULL;
		}

		if( Mode != GETIDENTMODE_NEW )
		{
			// Function Call
			DEBUGS2("Calling '%s'", name);
			if( level )
			{
				SyntaxError(Parser, "Array definition unexpected");
				return NULL;
			}

			ret = AST_NewFunctionCall( Parser, name );
			// Read arguments
			ret = Parse_DoFunctionArgs(Parser, ret);
			if(!ret)
				return NULL;
		}
		else
		{
			if( type == SS_ERRPTR ) {
				SyntaxError(Parser, "Unknown type '%s'", name);
				return NULL;
			}
			ret = Parse_DoNew(Parser, type, level);
			if(!ret)
				return NULL;
		}
	}
	else
	{
		DEBUGS2("Constant / Variable");
		if( Mode != GETIDENTMODE_VALUE
		 && Mode != GETIDENTMODE_EXPR
		 && Mode != GETIDENTMODE_ROOT
		  )
		{
			SyntaxError(Parser, "Code within class/namespace definition");
			return NULL;
		}
		
		if( type != SS_ERRPTR )
		{
			// Type is valid, has to be a cast
			SyntaxError(Parser, "Invalid use of type name");
			return NULL;
		}

		DEBUGS2("Referencing '%s'", name);
		PutBack(Parser);
		
		ret = Parse_GetDynValue(Parser, AST_NewVariable( Parser, name ) );
	}
	
	DEBUGS2("Return %p", ret);	
	DEBUGS2_UP();
	return ret;
}


void SyntaxError_(tParser *Parser, int Line, const char *Message, ...)
{
	va_list	args;
	 int	len = 0;
	va_start(args, Message);
	len += snprintf(NULL, 0, "%s:%i: (parse.c:%i) error: ", Parser->Filename, Parser->Cur.Line, Line);
	len += vsnprintf(NULL, 0, Message, args);
	va_end(args);
	
	char buffer[len+1];
	char *buf = buffer;

	va_start(args, Message);
	buf += sprintf(buf, "%s:%i: (parse.c:%i) error: ", Parser->Filename, Parser->Cur.Line, Line);
	buf += vsprintf(buf, Message, args);
	va_end(args);
		
	if( Parser->Variant->HandleError ) {
		Parser->Variant->HandleError(Parser->Script, buffer);
	}
	else {
		fprintf(stderr, "%s\n", buffer);
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
