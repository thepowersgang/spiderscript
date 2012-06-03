/*
 * 
 */
#ifndef _SPIDERSCRIPT_H_
#define _SPIDERSCRIPT_H_

#include <stdint.h>

#define ERRPTR	((void*)((intptr_t)0-1))

/**
 * \brief Opaque script handle
 */
typedef struct sSpiderScript	tSpiderScript;

typedef struct sSpiderVariant	tSpiderVariant;
typedef struct sSpiderNamespace	tSpiderNamespace;
typedef struct sSpiderFunction	tSpiderFunction;
typedef struct sSpiderValue	tSpiderValue;
typedef struct sSpiderClass	tSpiderClass;
typedef struct sSpiderObject	tSpiderObject;

typedef int	tSpiderScript_DataType;

/**
 * \brief SpiderScript Variable Datatypes
 * \todo Expand the descriptions
 */
enum eSpiderScript_InternalTypes
{
	SS_DATATYPE_NOVALUE,	// "void" - No value stored
	SS_DATATYPE_UNDEF,	// "Variable" - Used for dynamic typing
	SS_DATATYPE_BOOLEAN,	// "Boolean" - true/false
	SS_DATATYPE_INTEGER,	// "Integer" - 64-bit signed integer
	SS_DATATYPE_REAL,	// "Real" - 64-bit floating point
	SS_DATATYPE_STRING,	// "String" - Byte sequence
	NUM_SS_DATATYPES
};

extern const char *SpiderScript_GetTypeName(tSpiderScript *Script, int Type);
extern int	SpiderScript_GetTypeCode(tSpiderScript *Script, const char *Name);
extern int	SpiderScript_GetTypeCodeEx(tSpiderScript *Script, const char *Name, int Length);

#define SS_MAKEARRAYN(_type, _lvl)	((_type) + 0x10000*(_lvl))
#define SS_MAKEARRAY(_type)	((_type) + 0x10000)
#define SS_DOWNARRAY(_type)	((_type) - 0x10000)
#define SS_GETARRAYDEPTH(_type)	((_type) >> 16)
#define SS_ISTYPEOBJECT(_type)	((_type) & 0xF000)

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
	
	 int	bDyamicTyped;	//!< Use dynamic typing
	 int	bImplicitCasts;	//!< Allow implicit casts (casts to lefthand side)
	
	tSpiderFunction	*Functions;	//!< Functions (Flat list)
	tSpiderClass	*Classes;	//!< Classes (Flat list)
	
	tSpiderValue	(*GetConstant)(int Index);
	 int	NConstants;	//!< Number of constants
	struct {
		const char *Name;
		 int	Type;
	}	Constants[];	//!< Number of constants
};

/**
 * \brief SpiderScript data object
 */
struct sSpiderValue
{
	tSpiderScript_DataType	Type;	//!< Variable type
	 int	ReferenceCount;	//!< Reference count
	
	union {
		int64_t	Integer;	//!< Integer data
		double	Real;	//!< Real Number data
		/**
		 * \brief String data
		 */
		struct {
			 int	Length;	//!< Length
			char	Data[];	//!< Actual string (\a Length bytes)
		}	String;
		/**
		 * \brief Variable data
		 */
		struct {
			 int	Length;	//!< Length of the array
			tSpiderValue	*Items[];	//!< Array elements (\a Length long)
		}	Array;
		
		/**
		 * \brief Opaque data
		 */
		struct {
			void	*Data;	//!< Data (can be anywhere)
			void	(*Destroy)(void *Data);	//!< Called on GC
		}	Opaque;
		
		/**
		 * \brief Object Instance
		 */
		tSpiderObject	*Object;
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
	 * \brief Construct an instance of the object
	 * \param NArgs	Number of arguments
	 * \param Args	Argument array
	 * \return Pointer to an object instance (which must be fully valid)
	 * \retval NULL	Invalid parameter (usually, actually just a NULL value)
	 * \retval ERRPTR	Invalid parameter count
	 */
	tSpiderObject	*(*Constructor)(tSpiderScript *Script, int NArgs, tSpiderValue **Args);
	
	/**
	 * \brief Clean up and destroy the object
	 * \param This	Object instace
	 * \note The object pointer (\a This) should be invalidated and freed
	 *       by this function.
	 */
	void	(*Destructor)(tSpiderObject *This);


	/**
	 * \brief Get/Set an attribute's value
	 */
	tSpiderValue	*(*GetSetAttribute)(tSpiderObject *This, int AttibuteID, tSpiderValue *NewValue);
	
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
		 int	Type;	//!< Datatype
		char	bReadOnly;	//!< Allow writes to the attribute?
		char	bMethod;	//!< IO Goes via GetSetAttribute function
	}	AttributeDefs[];
};

/**
 * \brief Object Instance
 */
struct sSpiderObject
{
	 int	TypeCode;
	tSpiderScript	*Script;
	 int	ReferenceCount;	//!< Number of references
	void	*OpaqueData;	//!< Pointer to the end of the \a Attributes array
	tSpiderValue	*Attributes[];	//!< Attribute Array
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
	 */
	tSpiderValue	*(*Handler)(tSpiderScript *Script, int nParams, tSpiderValue **Parameters);

	/**
	 * \brief What type is returned
	 */
	 int	ReturnType;	

	/**
	 * \brief Argument types
	 * 
	 * Zero or -1 terminated array of \a eSpiderScript_DataTypes.
	 * If the final entry is zero, the function has a fixed number of
	 * parameters, if the final entry is -1, the function has a variable
	 * number of arguments.
	 */
	 int	ArgTypes[];	// Zero (or -1) terminated array of parameter types
};


// === FUNCTIONS ===
/**
 * \brief Parse a file into a script
 * \param Variant	Variant structure
 * \param Filename	File to parse
 * \return Script suitable for execution
 */
extern tSpiderScript	*SpiderScript_ParseFile(tSpiderVariant *Variant, const char *Filename);
/**
 * \brief Execute a function from a script
 * \param Script	Script to run
 * \param Function	Name of function to run ("" for the 'main')
 * \return Return value
 */
extern tSpiderValue	*SpiderScript_ExecuteFunction(tSpiderScript *Script,
	const char *Function, const char *DefaultNamespaces[],
	int NArguments, tSpiderValue **Arguments,
	void **FunctionIdent, int bExecute
	);
/**
 * \brief Execute an object method
 */
extern tSpiderValue	*SpiderScript_ExecuteMethod(tSpiderScript *Script,
	tSpiderObject *Object, const char *MethodName,
	int NArguments, tSpiderValue **Arguments
	);
/**
 * \brief Creates an object instance
 */
extern tSpiderValue	*SpiderScript_CreateObject(tSpiderScript *Script,
	const char *ClassName, const char *DefaultNamespaces[],
	int NArguments, tSpiderValue **Arguments,
	void **Ident, int bExecute
	);

/**
 * \brief Convert a script to bytecode and save to a file
 */
extern int	SpiderScript_SaveBytecode(tSpiderScript *Script, const char *DestFile);
/**
 * \brief Load a script from bytecode
 */
extern tSpiderScript	*SpiderScript_LoadBytecode(tSpiderVariant *Variant, const char *Filename);
/**
 * \brief Save the AST of a script to a file
 */
extern int	SpiderScript_SaveAST(tSpiderScript *Script, const char *Filename);

/**
 * \brief Free a script
 * \param Script	Script structure to free
 */
extern void	SpiderScript_Free(tSpiderScript *Script);

extern tSpiderObject	*SpiderScript_AllocateObject(tSpiderScript *Script, tSpiderClass *Class, int ExtraBytes);
extern void	SpiderScript_ReferenceObject(tSpiderObject *Object);
extern void	SpiderScript_DereferenceObject(tSpiderObject *Object);

/**
 * \name tSpiderValue Manipulation functions
 * \{
 */
extern void	SpiderScript_DereferenceValue(tSpiderValue *Object);
extern void	SpiderScript_ReferenceValue(tSpiderValue *Object);
extern tSpiderValue	*SpiderScript_CreateInteger(uint64_t Value);
extern tSpiderValue	*SpiderScript_CreateReal(double Value);
extern tSpiderValue	*SpiderScript_CreateString(int Length, const char *Data);
extern tSpiderValue	*SpiderScript_CreateArray(int InnerType, int ItemCount);
extern tSpiderValue	*SpiderScript_StringConcat(const tSpiderValue *Str1, const tSpiderValue *Str2);
extern tSpiderValue	*SpiderScript_CastValueTo(int Type, tSpiderValue *Source);
extern int	SpiderScript_IsValueTrue(tSpiderValue *Value);
extern void	SpiderScript_FreeValue(tSpiderValue *Value);
extern char	*SpiderScript_DumpValue(tSpiderValue *Value);

extern tSpiderValue	*SpiderScript_DoOp(tSpiderValue *Left, enum eSpiderValueOps Op, int bCanCast, tSpiderValue *Right);
/**
 * \}
 */

#endif
