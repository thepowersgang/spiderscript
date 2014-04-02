/*
 * SpiderScript
 * - Script Lexer
 */
#include "tokens.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define DEBUG	0

#define ARRAY_SIZE(x)	((sizeof(x))/(sizeof((x)[0])))

// === IMPORTS ===
extern void	SyntaxError_(tParser *Parser, int Line, const char *Message, ...);

// === PROTOTYPES ===
 int	is_ident(char ch);
 int	isdigit(int ch);
 int	isspace(int ch);
 int	GetToken(tParser *File);

// === CONSTANTS ===
const struct {
	const  int	Value;
	const char	*Name;
} csaReservedWords[] = {
	{TOK_RWD_INCLUDE, "include"},
//	{TOK_RWD_INCLUDE, "include_once"},
	
	{TOK_RWD_FUNCTION, "function"},
	{TOK_RWD_CLASS, "class"},
	{TOK_RWD_NAMESPACE, "namespace"},
	{TOK_RWD_AUTO, "auto"},
	{TOK_RWD_OPERATOR, "operator"},

	// Storage class
	{TOK_RWD_GLOBAL, "global"},
	{TOK_RWD_STATIC, "static"},
	{TOK_RWD_CONSTANT, "const"},

	{TOK_RWD_RETURN, "return"},
	{TOK_RWD_BREAK, "break"},
	{TOK_RWD_CONTINUE, "continue"},
	{TOK_RWD_NEW, "new"},
	{TOK_RWD_DELETE, "delete"},
	
	{TOK_RWD_IF, "if"},
	{TOK_RWD_ELSE, "else"},
	{TOK_RWD_ELSEIF, "elseif"},
	{TOK_RWD_DO, "do"},
	{TOK_RWD_WHILE, "while"},
	{TOK_RWD_FOR, "for"},
	{TOK_RWD_SWITCH, "switch"},
	{TOK_RWD_CASE, "case"},
	{TOK_RWD_DEFAULT, "default"},
	
	{TOK_RWD_NULL, "null"},
	{TOK_RWD_TRUE, "true"},
	{TOK_RWD_FALSE, "false"},
};

// === CODE ===
/**
 * \brief Read a token from a buffer
 * \param File	Parser state
 */
int GetToken(tParser *File)
{
	 int	ret;

	if( File->ErrorHit ) {
		return TOK_INVAL;
	}
	
	if( File->NextState.Token != TOK_INVAL ) {
		// Save Last
		File->PrevState = File->Cur;
		// Restore Next
		File->Cur = File->NextState;
		// Set State
		File->CurPos = File->Cur.TokenStr + File->Cur.TokenLen;
		File->NextState.Token = TOK_INVAL;
		#if DEBUG
		printf(" GetToken: FAST Return %i (%i long) (%.*s)\n", File->Cur.Token, File->Cur.TokenLen,
			File->Cur.TokenLen, File->Cur.TokenStr);
		#endif
		return File->Cur.Token;
	}
	
	//printf("  GetToken: File=%p, File->CurPos = %p\n", File, File->CurPos);
	
	// Clear whitespace (including comments)
	for( ;; )
	{
		// Whitespace
		while( isspace( *File->CurPos ) )
		{
			//printf("whitespace 0x%x, line = %i\n", *File->CurPos, File->CurLine);
			if( *File->CurPos == '\n' )
				File->Cur.Line ++;
			File->CurPos ++;
		}
		
		// # Line Comments
		if( *File->CurPos == '#' ) {
			while( *File->CurPos && *File->CurPos != '\n' )
				File->CurPos ++;
			continue ;
		}
		
		// C-Style Line Comments
		if( *File->CurPos == '/' && File->CurPos[1] == '/' ) {
			while( *File->CurPos && *File->CurPos != '\n' )
				File->CurPos ++;
			continue ;
		}
		
		// C-Style Block Comments
		if( *File->CurPos == '/' && File->CurPos[1] == '*' ) {
			File->CurPos += 2;	// Eat the '/*'
			while( *File->CurPos && !(File->CurPos[-1] == '*' && *File->CurPos == '/') )
			{
				if( *File->CurPos == '\n' )
					File->Cur.Line ++;
				File->CurPos ++;
			}
			File->CurPos ++;	// Eat the '/'
			continue ;
		}
		
		// No more "whitespace"
		break;
	}
	
	// Save previous tokens (speeds up PutBack and LookAhead)
	File->PrevState = File->Cur;
	
	// Read token
	File->Cur.TokenStr = File->CurPos;
	switch( *File->CurPos++ )
	{
	case '\0':	ret = TOK_EOF;	break;
	
	// Operations
	case '^':
		if( *File->CurPos == '^' ) {
			File->CurPos ++;
			ret = TOK_LOGICXOR;
			break;
		}
		ret = TOK_XOR;
		break;
	
	case '|':
		if( *File->CurPos == '|' ) {
			File->CurPos ++;
			ret = TOK_LOGICOR;
			break;
		}
		ret = TOK_OR;
		break;
	
	case '&':
		if( *File->CurPos == '&' ) {
			File->CurPos ++;
			ret = TOK_LOGICAND;
			break;
		}
		ret = TOK_AND;
		break;
	
	case '/':
		if( *File->CurPos == '=' ) {
			File->CurPos ++;
			ret = TOK_ASSIGN_DIV;
			break;
		}
		ret = TOK_DIV;
		break;
	case '%':
		if( *File->CurPos == '=' ) {
			File->CurPos ++;
			ret = TOK_ASSIGN_MODULO;
			break;
		}
		ret = TOK_MODULO;
		break;
	case '*':
		if( *File->CurPos == '=' ) {
			File->CurPos ++;
			ret = TOK_ASSIGN_MUL;
			break;
		}
		ret = TOK_MUL;
		break;
	case '+':
		if( *File->CurPos == '+' ) {
			File->CurPos ++;
			ret = TOK_INCREMENT;
			break;
		}
		if( *File->CurPos == '=' ) {
			File->CurPos ++;
			ret = TOK_ASSIGN_PLUS;
			break;
		}
		ret = TOK_PLUS;
		break;
	case '-':
		if( *File->CurPos == '-' ) {
			File->CurPos ++;
			ret = TOK_DECREMENT;
			break;
		}
		if( *File->CurPos == '=' ) {
			File->CurPos ++;
			ret = TOK_ASSIGN_MINUS;
			break;
		}
		if( *File->CurPos == '>' ) {
			File->CurPos ++;
			ret = TOK_ELEMENT;
			break;
		}
		ret = TOK_MINUS;
		break;
	
	// Strings
	case '"':
		while( *File->CurPos && !(*File->CurPos == '"' && File->CurPos[-1] != '\\') )
			File->CurPos ++;
		if( *File->CurPos )
		{
			File->CurPos ++;
			ret = TOK_STR;
		}
		else
			ret = TOK_EOF;
		break;
	
	// Brackets
	case '(':	ret = TOK_PAREN_OPEN;	break;
	case ')':	ret = TOK_PAREN_CLOSE;	break;
	case '{':	ret = TOK_BRACE_OPEN;	break;
	case '}':	ret = TOK_BRACE_CLOSE;	break;
	case '[':	ret = TOK_SQUARE_OPEN;	break;
	case ']':	ret = TOK_SQUARE_CLOSE;	break;
	
	// Core symbols
	case ';':	ret = TOK_SEMICOLON;	break;
	case ',':	ret = TOK_COMMA;	break;
	case ':':	ret = TOK_COLON;	break;
	case '?':
		if( *File->CurPos == ':' ) {
			File->CurPos ++;
			ret = TOK_QMARKCOLON;
		}
		else
			ret = TOK_QUESTIONMARK;
		break;
	case '.':
		if( *File->CurPos == '.' && File->CurPos[1] == '.' ) {
			File->CurPos += 2;
			ret = TOK_ELIPSIS;
		}
		else
			#if USE_SCOPE_CHAR
			ret = TOK_SCOPE;
			#else
			goto default;
			#endif
		break;
	
	// Equals
	case '=':
		if( *File->CurPos != '=' ) {
			// Assignment Equals
			ret = TOK_ASSIGN;
			break;
		}
		File->CurPos ++;
		if( *File->CurPos != '=' ) {
			// Comparison Equals
			ret = TOK_EQUALS;
			break;
		}
		File->CurPos ++;
		ret = TOK_REFEQUALS;
		break;
	
	// Less-Than
	case '<':
		// Less-Than or Equal
		if( *File->CurPos == '=' ) {
			File->CurPos ++;
			ret = TOK_LTE;
			break;
		}
		ret = TOK_LT;
		break;
	
	// Greater-Than
	case '>':
		// Greater-Than or Equal
		if( *File->CurPos == '=' ) {
			File->CurPos ++;
			ret = TOK_GTE;
			break;
		}
		ret = TOK_GT;
		break;
	
	// Logical NOT
	case '!':
		if( *File->CurPos != '=' ) {
			ret = TOK_LOGICNOT;
			break;
		}
		File->CurPos ++;
		if( *File->CurPos != '=' ) {
			ret = TOK_NOTEQUALS;
			break;
		}
		File->CurPos ++;
		ret = TOK_REFNOTEQUALS;
		break;
	// Bitwise NOT
	case '~':
		ret = TOK_BWNOT;
		break;
	
	case '0' ... '9':
		File->CurPos --;
		
		ret = TOK_INTEGER;
		if( *File->CurPos == '0' && File->CurPos[1] == 'x' )
		{
			File->CurPos += 2;
			while(('0' <= *File->CurPos && *File->CurPos <= '9')
			   || ('A' <= *File->CurPos && *File->CurPos <= 'F')
			   || ('a' <= *File->CurPos && *File->CurPos <= 'f') )
			{
				File->CurPos ++;
			}
		}
		else
		{
			while( isdigit(*File->CurPos) )
				File->CurPos ++;
			
//				printf("*File->CurPos = '%c'\n", *File->CurPos);
			
			// Decimal
			if( *File->CurPos == '.' )
			{
				ret = TOK_REAL;
				File->CurPos ++;
				while( isdigit(*File->CurPos) )
					File->CurPos ++;
			}
			// Exponent
			if( *File->CurPos == 'e' || *File->CurPos == 'E' )
			{
				ret = TOK_REAL;
				File->CurPos ++;
				if(*File->CurPos == '-' || *File->CurPos == '+')
					File->CurPos ++;
				while( isdigit(*File->CurPos) )
					File->CurPos ++;
			}
			
//				printf(" ret = %i\n", ret);
		}
		break;
	// Variables
	case '$':
	// Default (Numbers and Identifiers)
	default:
		File->CurPos --;
	
		// Identifier
		if( is_ident(*File->CurPos) )
		{
			ret = TOK_IDENT;
			
			// Identifier
			while( is_ident(*File->CurPos) || isdigit(*File->CurPos) )
				File->CurPos ++;
			
			// This is set later too, but we use it below
			const char *tokstr = File->Cur.TokenStr;
			size_t	len = File->CurPos - tokstr;
			
			// Check if it's a reserved word
			for( int i = 0; i < ARRAY_SIZE(csaReservedWords); i ++ )
			{
				if( strncmp(csaReservedWords[i].Name, tokstr, len) != 0 )
					continue ;
				if( csaReservedWords[i].Name[len] != '\0' )
					continue ;
				ret = csaReservedWords[i].Value;
				break ;
			}
			// If there's no match, just keep ret as TOK_IDENT
			
			break;
		}
		// Syntax Error
		ret = TOK_INVAL;
		File->ErrorHit = 1;
		SyntaxError_(File, -1, "Unknown symbol '%c'", *File->CurPos);
		break;
	}
	// Return
	File->Cur.Token = ret;
	File->Cur.TokenLen = File->CurPos - File->Cur.TokenStr;
	
	#if DEBUG
	printf(" GetToken: Return %i (%i long) (%.*s)\n", ret, File->Cur.TokenLen,
		File->Cur.TokenLen, File->Cur.TokenStr);
	#endif
	return ret;
}

void PutBack(tParser *File)
{
	if( File->PrevState.Token == -1 ) {
		// ERROR:
		fprintf(stderr, "INTERNAL ERROR: Putback when LastToken==-1\n");
		longjmp( File->JmpTarget, -1 );
		return ;
	}
	#if DEBUG
	printf(" PutBack: Was on %i\n", File->Cur.Token);
	#endif
	// Save
	File->NextState = File->Cur;
	// Restore
	File->Cur = File->PrevState;
	// Invalidate
	File->PrevState.Token = TOK_INVAL;
}

int LookAhead(tParser *File)
{
	if( File->NextState.Token != TOK_INVAL )
		return File->NextState.Token;
	// TODO: Should I save the entire state here?
	 int	ret = GetToken(File);
	PutBack(File);
	return ret;
}

// --- Helpers ---
/**
 * \brief Check for ident characters
 * \note Matches Regex [a-zA-Z_]
 */
int is_ident(char ch)
{
	if('a' <= ch && ch <= 'z')	return 1;
	if('A' <= ch && ch <= 'Z')	return 1;
	if(ch == '_')	return 1;
	if(ch == '$')	return 1;
	#if !USE_SCOPE_CHAR
	if(ch == '.')	return 1;
	#endif
	if(ch < 0)	return 1;
	return 0;
}

int isdigit(int ch)
{
	if('0' <= ch && ch <= '9')	return 1;
	return 0;
}

int isspace(int ch)
{
	if(' ' == ch)	return 1;
	if('\t' == ch)	return 1;
	if('\b' == ch)	return 1;
	if('\n' == ch)	return 1;
	if('\r' == ch)	return 1;
	return 0;
}
