/*
 */
#ifndef _TOKENS_H_
#define _TOKENS_H_

#include <setjmp.h>

// Make the scope character ('.') be a symbol, otherwise it's just
// a ident character
#define USE_SCOPE_CHAR	1

// === TYPES ===
typedef struct
{	
	// Lexer State
	const char	*BufStart;
	const char	*CurPos;
	
	char	*Filename;
	
	 int	LastLine;
	 int	LastToken, LastTokenLen;
	const char	*LastTokenStr;
	
	 int	NextLine;
	 int	NextToken, NextTokenLen;
	const char	*NextTokenStr;
	
	 int	CurLine;
	 int	Token, TokenLen;
	const char	*TokenStr;
	
	jmp_buf	JmpTarget;
	 int	ErrorHit;
	
	struct sSpiderScript	*Script;
	struct sSpiderVariant	*Variant;
}	tParser;

// === FUNCTIONS ===
 int	GetToken(tParser *File);
void	PutBack(tParser *File);
 int	LookAhead(tParser *File);

// === CONSTANTS ===
enum eTokens
{
	TOK_INVAL,
	TOK_EOF,
	
	// Primitives
	TOK_STR,
	TOK_INTEGER,
	TOK_REAL,
	TOK_VARIABLE,
	TOK_IDENT,
	
	// Reserved Words
	// - Meta-ops
	TOK_RWD_INCLUDE,
	// - Definitions
	TOK_RWD_FUNCTION,
	TOK_RWD_CLASS,
	TOK_RWD_NAMESPACE,
	// - Classes
	TOK_RWD_GLOBAL,
	TOK_RWD_CONSTANT,
	TOK_RWD_STATIC,
	// - Control Flow
	TOK_RWD_NEW,
	TOK_RWD_DELETE,
	TOK_RWD_RETURN,
	TOK_RWD_BREAK,
	TOK_RWD_CONTINUE,
	// - Blocks
	TOK_RWD_IF,
	TOK_RWD_ELSE,
	TOK_RWD_DO,
	TOK_RWD_WHILE,
	TOK_RWD_FOR,
	// - Value
	TOK_RWD_NULL,
	TOK_RWD_TRUE,
	TOK_RWD_FALSE,
	
	// 
	TOK_ASSIGN,
	TOK_SEMICOLON,
	TOK_COMMA,
	TOK_SCOPE,
	TOK_ELEMENT,
	TOK_QUESTIONMARK,
	TOK_COLON,
	
	// Comparisons
	TOK_REFEQUALS, TOK_REFNOTEQUALS,
	TOK_EQUALS, TOK_NOTEQUALS,
	TOK_LT,	TOK_LTE,
	TOK_GT,	TOK_GTE,
	
	// Operations
	TOK_BWNOT,	TOK_LOGICNOT,
	TOK_DIV,	TOK_MUL,
	TOK_PLUS,	TOK_MINUS,
	TOK_SHL,	TOK_SHR,
	TOK_LOGICAND,	TOK_LOGICOR,	TOK_LOGICXOR,
	TOK_AND,	TOK_OR,	TOK_XOR,
	
	// Assignment Operations
	TOK_INCREMENT,  	TOK_DECREMENT,
	TOK_ASSIGN_DIV, 	TOK_ASSIGN_MUL,
	TOK_ASSIGN_PLUS,	TOK_ASSIGN_MINUS,
	TOK_ASSIGN_SHL, 	TOK_ASSIGN_SHR,
	TOK_ASSIGN_LOGICAND, 	TOK_ASSIGN_LOGICOR,	TOK_ASSIGN_LOGXICOR,
	TOK_ASSIGN_AND, 	TOK_ASSIGN_OR,	TOK_ASSIGN_XOR,
	
	TOK_PAREN_OPEN, 	TOK_PAREN_CLOSE,
	TOK_BRACE_OPEN, 	TOK_BRACE_CLOSE,
	TOK_SQUARE_OPEN,	TOK_SQUARE_CLOSE,
	
	TOK_LAST
};

# if WANT_TOKEN_STRINGS
const char * const csaTOKEN_NAMES[] = {
	"TOK_INVAL",
	"TOK_EOF",
	
	"TOK_STR",
	"TOK_INTEGER",
	"TOK_REAL",
	"TOK_VARIABLE",
	"TOK_IDENT",

	"TOK_RWD_INCLUDE",
	
	"TOK_RWD_FUNCTION",
	"TOK_RWD_CLASS",
	"TOK_RWD_NAMESPACE",

	"TOK_RWD_GLOBAL",
	"TOK_RWD_CONSTANT",
	"TOK_RWD_STATIC",
	
	"TOK_RWD_NEW",
	"TOK_RWD_DELETE",
	"TOK_RWD_RETURN",
	"TOK_RWD_BREAK",
	"TOK_RWD_CONTINUE",
	
	"TOK_RWD_IF",
	"TOK_RWD_ELSE",
	"TOK_RWD_DO",
	"TOK_RWD_WHILE",
	"TOK_RWD_FOR",
	
	"TOK_RWD_NULL",
	"TOK_RWD_TRUE",
	"TOK_RWD_FALSE",

	"TOK_ASSIGN",
	"TOK_SEMICOLON",
	"TOK_COMMA",
	"TOK_SCOPE",
	"TOK_ELEMENT",
	
	"TOK_REFEQUALS", "TOK_REFNOTEQUALS",
	"TOK_EQUALS",	"TOK_NOTEQUALS",
	"TOK_LT",	"TOK_LTE",
	"TOK_GT",	"TOK_GTE",
	
	"TOK_BWNOT",	"TOK_LOGICNOT",
	"TOK_DIV",	"TOK_MUL",
	"TOK_PLUS",	"TOK_MINUS",
	"TOK_SHL",	"TOK_SHR",
	"TOK_LOGICAND",	"TOK_LOGICOR",	"TOK_LOGICXOR",
	"TOK_AND",	"TOK_OR",	"TOK_XOR",
	
	"TOK_INCREMENT",  	"TOK_DECREMENT",
	"TOK_ASSIGN_DIV",	"TOK_ASSIGN_MUL",
	"TOK_ASSIGN_PLUS",	"TOK_ASSIGN_MINUS",
	"TOK_ASSIGN_SHL",	"TOK_ASSIGN_SHR",
	"TOK_ASSIGN_LOGICAND",	"TOK_ASSIGN_LOGICOR",	"TOK_ASSIGN_LOGICXOR",
	"TOK_ASSIGN_AND",	"TOK_ASSIGN_OR",	"TOK_ASSIGN_XOR",
	
	"TOK_PAREN_OPEN",	"TOK_PAREN_CLOSE",
	"TOK_BRACE_OPEN",	"TOK_BRACE_CLOSE",
	"TOK_SQUARE_OPEN",	"TOK_SQUARE_CLOSE",
	
	"TOK_LAST"
};
# endif

#endif
