/*
 * SpiderScript Library Header
 */
#ifndef _SPIDERSCRIPT_H_
#define _SPIDERSCRIPT_H_

#define EXPORT	__attribute__((__visibility__("default")))

#include <stdint.h>
#include <stddef.h>
#include <setjmp.h>	// Used for exception handling

#define ERRPTR	((void*)((intptr_t)0-1))

/**
 * \brief SpiderScript Variable Datatypes
 * \todo Expand the descriptions
 */
enum eSpiderScript_InternalTypes
{
	SS_DATATYPE_NOVALUE,	// "void" - No value stored
	SS_DATATYPE_UNDEF,	// "undefined" - Can be any type (functions only)
	SS_DATATYPE_BOOLEAN,	// "Boolean" - true/false
	SS_DATATYPE_INTEGER,	// "Integer" - 64-bit signed integer
	SS_DATATYPE_REAL,	// "Real" - 64-bit floating point
	SS_DATATYPE_STRING,	// "String" - Byte sequence
	NUM_SS_DATATYPES
};

/**
 * \brief Opaque script handle
 */
typedef struct sSpiderScript	tSpiderScript;

typedef struct sSpiderVariant	tSpiderVariant;
typedef struct sSpiderNamespace	tSpiderNamespace;
typedef struct sSpiderFcnProto	tSpiderFcnProto;
typedef struct sSpiderFunction	tSpiderFunction;
typedef struct sSpiderClass	tSpiderClass;

typedef char	tSpiderBool;
typedef int64_t	tSpiderInteger;
typedef double	tSpiderReal;
typedef struct sSpiderString	tSpiderString;
typedef struct sSpiderObject	tSpiderObject;
typedef struct sSpiderArray	tSpiderArray;

typedef int	tSpiderScript_DataType;

typedef enum eSpiderScript_InternalTypes	tSpiderScript_CoreType;
typedef struct sSpiderScript_TypeRef	tSpiderTypeRef;
typedef struct sSpiderScript_TypeDef	tSpiderScript_TypeDef;

struct sSpiderScript_TypeDef
{
	enum {
		SS_TYPECLASS_CORE,
		SS_TYPECLASS_NCLASS,
		SS_TYPECLASS_SCLASS,
		SS_TYPECLASS_FCNPTR,
	} Class;
	union {
		tSpiderScript_CoreType	Core;	
		tSpiderClass	*NClass;
		struct sScript_Class	*SClass;
		tSpiderFcnProto	*FcnPtr;
	};
};

EXPORT extern const tSpiderScript_TypeDef	gSpiderScript_AnyType;
EXPORT extern const tSpiderScript_TypeDef	gSpiderScript_IntegerType;
EXPORT extern const tSpiderScript_TypeDef	gSpiderScript_BoolType;
EXPORT extern const tSpiderScript_TypeDef	gSpiderScript_RealType;
EXPORT extern const tSpiderScript_TypeDef	gSpiderScript_StringType;

struct sSpiderScript_TypeRef
{
	const tSpiderScript_TypeDef	*Def;
	 int	ArrayDepth;
};

EXPORT extern const char *SpiderScript_GetTypeName(tSpiderScript *Script, tSpiderTypeRef Type);
EXPORT extern const tSpiderScript_TypeDef	*SpiderScript_GetCoreType(tSpiderScript_CoreType Type);
EXPORT extern const tSpiderScript_TypeDef	*SpiderScript_GetType(tSpiderScript *Script, const char *Name);
EXPORT extern const tSpiderScript_TypeDef	*SpiderScript_GetTypeEx(tSpiderScript *Script, const char *Name, int Length);

//#define SS_MAKEARRAYN(_type, _lvl)	((_type) + 0x10000*(_lvl))
#define SS_MAKEARRAY(_type)	((_type) + 0x10000)
#define SS_DOWNARRAY(_type)	((_type) - 0x10000)
#define SS_GETARRAYDEPTH(_type)	((_type).ArrayDepth)
#define SS_ISCORETYPE(_t,_c)	((_t).ArrayDepth == 0 && (_t).Def && (_t).Def->Class == SS_TYPECLASS_CORE && (_t).Def->Core == _c)
#define SS_ISTYPEOBJECT(_type)	((_type).Def && (_type).Def->Class != SS_TYPECLASS_CORE)
#define SS_ISTYPEREFERENCE(_t)	((SS_ISTYPEOBJECT(_t)) || SS_ISCORETYPE(_t,SS_DATATYPE_STRING))

#define SS_TYPESEQUAL(_t1,_t2)	((_t1).ArrayDepth == (_t2).ArrayDepth && (_t1).Def == (_t2).Def)

enum eSpiderValueOps
{
	SS_VALUEOP_NOP,

	SS_VALUEOP_ADD,
	SS_VALUEOP_SUBTRACT,
	SS_VALUEOP_NEGATE,
	SS_VALUEOP_MULIPLY,
	SS_VALUEOP_DIVIDE,
	SS_VALUEOP_MODULO,

	SS_VALUEOP_BITNOT,
	SS_VALUEOP_BITAND,
	SS_VALUEOP_BITOR,
	SS_VALUEOP_BITXOR,

	SS_VALUEOP_SHIFTLEFT,
	SS_VALUEOP_SHIFTRIGHT,
	SS_VALUEOP_ROTATELEFT
};

/**
 * \brief Variant of SpiderScript
 */
struct sSpiderVariant
{
	const char	*Name;	// Just for debug
	
	 int	bImplicitCasts;	//!< Allow implicit casts (casts to lefthand side)
	
	void	(*HandleError)(tSpiderScript *Script, const char *Message);
	
	 int	nFunctions;
	tSpiderFunction	**Functions;	//!< Functions (Pointer to array)
	 int	nClasses;
	tSpiderClass	**Classes;	//!< Classes (Pointer to array)
	
	 int	(*GetConstant)(void **Dest, int Index);
	 int	NConstants;	//!< Number of constants
	struct {
		const char *Name;
		tSpiderTypeRef	Type;
	}	Constants[];	//!< Number of constants
};

struct sSpiderString
{
	 int	RefCount;
	size_t	Length;
	char	Data[];
};

struct sSpiderArray
{
	 int	RefCount;
	tSpiderTypeRef	Type;
	size_t	Length;
//	char	Data[];
	union {
		tSpiderBool	Bools[0];
		tSpiderInteger	Integers[0];
		tSpiderReal	Reals[0];
		tSpiderArray	*Arrays[0];
		tSpiderString	*Strings[0];
		tSpiderObject	*Objects[0];
	};
};

/**
 * \brief Object Definition
 * 
 * Internal representation of an arbitary object.
 */
struct sSpiderClass
{
	/**
	 */
	struct sSpiderClass	*Next;	//!< Internal linked list
	/**
	 * \brief Object type name
	 */
	const char * const	Name;

	/**
	 * Type for this class
	 */
	tSpiderScript_TypeDef	TypeDef;

	/**
	 * \brief Create a new instance of the object
	 * \return Allocated tSpiderObject in \a RetData
	 */	
	tSpiderFunction	*Constructor;
	
	/**
	 * \brief Clean up and destroy the object
	 * \param This	Object instace
	 * \note The object pointer (\a This) should not be free'd by this function
	 */
	void	(*Destructor)(tSpiderObject *This);

	/**
	 * \brief Method Definitions (linked list)
	 */
	tSpiderFunction	*Methods;
	
	/**
	 * \brief Number of attributes
	 */
	 int	NAttributes;
	
	//! Attribute definitions
	struct {
		const char	*Name;	//!< Attribute Name
		tSpiderTypeRef	Type;	//!< Datatype
		char	bReadOnly;	//!< Allow writes to the attribute?
		char	bMethod;	//!< IO Goes via GetSetAttribute function
	}	AttributeDefs[];
};

/**
 * \brief Object Instance
 */
struct sSpiderObject
{
	const tSpiderScript_TypeDef	*TypeDef;
	tSpiderScript	*Script;
	 int	ReferenceCount;	//!< Number of references
	void	*OpaqueData;	//!< Pointer to the end of the \a Attributes array
	void	*Attributes[];	//!< Attribute Array (with attribute data afterwards)
};

struct sSpiderFcnProto
{
	/**
	 * \brief What type is returned
	 */
	tSpiderTypeRef	ReturnType;

	 int	bVariableArgs;

	/**
	 * \brief Argument types (NULL terminated)
	 * 
	 */
	tSpiderTypeRef	Args[];
};

/**
 * \brief Represents a function avaliable to a script
 */
struct sSpiderFunction
{
	/**
	 * \brief Next function in list
	 */
	struct sSpiderFunction	*Next;
	
	/**
	 * \brief Function name
	 */
	const char	*Name;
	
	/**
	 * \brief Function handler
	 * \param Script	Script instance
	 * \param RetData	Pointer the storage location (pointer to pointer for
	 *               	objects/arrays/strings, pointer to data for bools/integers/reals)
	 * \param nArgs 	Number of arguments passed
	 * \param ArgTypes	Types of each argument
	 * \param Args  	Pointers to each argument (all point to actual data, unlike \a RetData)
	 */
	 int	(*Handler)(tSpiderScript *Script, void *RetData, int nArgs, const tSpiderTypeRef *ArgTypes, const void * const Args[]);
	
	tSpiderFcnProto	*Prototype;
};


// === FUNCTIONS ===
/**
 * \brief Parse a file into a script
 * \param Variant	Variant structure
 * \param Filename	File to parse
 * \return Script suitable for execution
 */
EXPORT extern tSpiderScript	*SpiderScript_ParseFile(tSpiderVariant *Variant, const char *Filename);

EXPORT extern int	SpiderScript_ExecuteFunction(tSpiderScript *Script, const char *Function,
	tSpiderTypeRef *RetType, void *RetData,
	int NArguments, const tSpiderTypeRef *ArgTypes, const void * const Arguments[],
	void **Ident
	);
/**
 * \brief Execute an object method
 */
EXPORT extern int	SpiderScript_ExecuteMethod(tSpiderScript *Script, const char *MethodName,
	tSpiderTypeRef *RetType, void *RetData,
	int NArguments, const tSpiderTypeRef *ArgTypes, const void * const Arguments[],
	void **Ident
	);
/**
 * \brief Creates an object instance
 */
EXPORT extern int	SpiderScript_CreateObject(tSpiderScript *Script, const char *ClassName,
	tSpiderObject **RetData, int NArguments, const tSpiderTypeRef *ArgTypes, const void * const Arguments[],
	void **Ident
	);

EXPORT extern int	SpiderScript_CreateObject_Type(tSpiderScript *Script, tSpiderScript_TypeDef *TypeCode,
	tSpiderObject **RetData, int NArguments, const tSpiderTypeRef *ArgTypes, const void * const Arguments[],
	void **Ident
	);
/**
 * \brief Convert a script to bytecode and save to a file
 */
EXPORT extern int	SpiderScript_SaveBytecode(tSpiderScript *Script, const char *DestFile);
/**
 * \brief Load a script from bytecode
 */
EXPORT extern tSpiderScript	*SpiderScript_LoadBytecode(tSpiderVariant *Variant, const char *Filename);
/**
 * \brief Save the AST of a script to a file
 */
EXPORT extern int	SpiderScript_SaveAST(tSpiderScript *Script, const char *Filename);

/**
 * \brief Free a script
 * \param Script	Script structure to free
 */
EXPORT extern void	SpiderScript_Free(tSpiderScript *Script);

/**
 * \name Exception Handling
 * \{
 */
enum eSpiderScript_Exceptions
{
	SS_EXCEPTION_NONE = 0,	// No exception
	SS_EXCEPTION_GENERIC,	// Generic error
	SS_EXCEPTION_BUG,	// Bug in the engine
	SS_EXCEPTION_NULLDEREF,	// Derefence of a NULL array/string/object
	SS_EXCEPTION_INDEX_OOB,	// Array index out of bounds
	SS_EXCEPTION_BADELEMENT,	// TODO: Is this needed?
	SS_EXCEPTION_ARGUMENT,	// Invalid argument
	SS_EXCEPTION_TYPEMISMATCH,	// Type mismatch
	SS_EXCEPTION_NAMEERROR	// Invalid name/ID
};

EXPORT extern int	SpiderScript_ThrowException(tSpiderScript *Script, int ExceptionID, char *Message, ...);
EXPORT extern int	SpiderScript_GetException(tSpiderScript *Script, const char **Message);
EXPORT extern void	SpiderScript_SetCatchTarget(tSpiderScript *Script, jmp_buf *Target, jmp_buf *OldTargetSaved);
EXPORT extern void	SpiderScript_ClearException(tSpiderScript *Script);
/**
 * \}
 */

/**
 * \name Object Manipulation
 * \{
 */
EXPORT extern tSpiderObject	*SpiderScript_AllocateObject(tSpiderScript *Script, tSpiderClass *Class, int ExtraBytes);
EXPORT extern void	SpiderScript_ReferenceObject(const tSpiderObject *Object);
EXPORT extern void	SpiderScript_DereferenceObject(const tSpiderObject *Object);
/**
 * \}
 */

/**
 * \name Array Manipulation
 * \{
 */
EXPORT extern tSpiderArray	*SpiderScript_CreateArray(tSpiderTypeRef InnerType, int ItemCount);
EXPORT extern const void	*SpiderScript_GetArrayPtr(const tSpiderArray *Array, int Item);
EXPORT extern void	SpiderScript_ReferenceArray(const tSpiderArray *Array);
EXPORT extern void	SpiderScript_DereferenceArray(const tSpiderArray *Array);
/**
 * \}
 */

/**
 * \name String Manipulation
 * \{
 */
EXPORT extern tSpiderString	*SpiderScript_CreateString(int Length, const char *Data);
EXPORT extern void	SpiderScript_ReferenceString(const tSpiderString *String);
EXPORT extern void	SpiderScript_DereferenceString(const tSpiderString *String);

EXPORT extern tSpiderString	*SpiderScript_StringConcat(const tSpiderString *Str1, const tSpiderString *Str2);
EXPORT extern int        	SpiderScript_StringCompare(const tSpiderString *Str1, const tSpiderString *Str2);
EXPORT extern tSpiderString	*SpiderScript_CastValueToString(tSpiderTypeRef SourceType, const void *Source);
/**
 * \}
 */

/**
 * \name Scalar Casts
 * \{
 */
EXPORT extern tSpiderBool	SpiderScript_CastValueToBool(tSpiderTypeRef SourceType, const void *Source);
EXPORT extern tSpiderInteger	SpiderScript_CastValueToInteger(tSpiderTypeRef SourceType, const void *Source);
EXPORT extern tSpiderReal	SpiderScript_CastValueToReal(tSpiderTypeRef SourceType, const void *Source);
/**
 * \}
 */

#endif
