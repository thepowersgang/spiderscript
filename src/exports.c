/*
 * Acess2 - SpiderScript
 * - Script Exports (Lang. Namespace)
 */
#define _GNU_SOURCE	// HACK!
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <spiderscript.h>

#define SS_FCN(name)	int name(tSpiderScript*Script,void*RetData,int NArgs,const int*ArgTypes, void*const Args[])

// === PROTOTYPES ===
SS_FCN(Exports_sizeof);
SS_FCN(Exports_array);
SS_FCN(Exports_Lang_Strings_Split);
SS_FCN(Exports_Lang_Struct);

// === GLOBALS ===
tSpiderFunction	gExports_Lang_Strings_Split = {
	.Name = "Lang@Strings@Split",
	.Handler = Exports_Lang_Strings_Split,
	.ReturnType = SS_MAKEARRAY(SS_DATATYPE_STRING),
	.ArgTypes = {SS_DATATYPE_STRING, SS_DATATYPE_STRING, 0}
};

tSpiderFunction	gExports_Lang_Struct = {
	.Next = &gExports_Lang_Strings_Split,
	.Name = "Lang@Struct",
	.Handler = Exports_Lang_Struct,
	.ReturnType = SS_DATATYPE_STRING,
	.ArgTypes = {SS_DATATYPE_STRING, -1}
};
// -- Global Functions
tSpiderFunction	gExports_array = {
	.Next = &gExports_Lang_Struct,
	.Name = "array",
	.Handler = Exports_array,
	.ReturnType = SS_DATATYPE_NOVALUE,
	.ArgTypes = {SS_DATATYPE_INTEGER, -1}
};
tSpiderFunction	gExports_sizeof = {
	.Next = &gExports_array,
	.Name = "sizeof",
	.Handler = Exports_sizeof,
	.ReturnType = SS_DATATYPE_INTEGER,
	.ArgTypes = {-1}
};
tSpiderFunction	*gpExports_First = &gExports_sizeof;

// === CODE ===
SS_FCN(Exports_sizeof)
{
	tSpiderInteger	*ret = RetData;
	if(NArgs != 1 || ArgTypes[0])	return -1;

	if( SS_GETARRAYDEPTH(ArgTypes[0]) ) {
		*ret = ((tSpiderArray*)Args[0])->Length;
		return SS_DATATYPE_INTEGER;
	}
	switch( ArgTypes[0] )
	{
	case SS_DATATYPE_STRING:
		*ret = ((tSpiderString*)Args[0])->Length;
		return SS_DATATYPE_INTEGER;
	default:
		*ret = 0;
		return SS_DATATYPE_NOVALUE;
	}
}

SS_FCN(Exports_array)
{
	tSpiderArray	**ret = RetData;
	if(NArgs != 2)	return -1;
	
	if(ArgTypes[0] != SS_DATATYPE_INTEGER || ArgTypes[1] != SS_DATATYPE_INTEGER)
		return -1;

	 int	type = *(tSpiderInteger*)Args[1];
	 int	size = *(tSpiderInteger*)Args[0];

	if( !SS_GETARRAYDEPTH(type) ) {
		// ERROR - This should never happen
		return -1;
	}
	type = SS_DOWNARRAY(type);

	*ret = SpiderScript_CreateArray(type, size);
	return type;
}

SS_FCN(Exports_Lang_Strings_Split)
{
	 int	haystack_len, needle_len, ofs, slen;
	const void	*haystack, *needle, *end;
	 int	nSubStrs = 0;
	tSpiderString	**strings = NULL;
	tSpiderArray	*ret, **ret_ptr = RetData;

	// Error checking
	if( NArgs != 2 )
		return -1;
	if( !Args[0] || !Args[1] )
		return -1;
	if( ArgTypes[0] != SS_DATATYPE_STRING )
		return -1;
	if( ArgTypes[1] != SS_DATATYPE_STRING )
		return -1;

	// Split the string
	haystack_len = ((tSpiderString*)Args[0])->Length;
	haystack     = ((tSpiderString*)Args[0])->Data;
	needle_len = ((tSpiderString*)Args[1])->Length;
	needle     = ((tSpiderString*)Args[1])->Data;
	ofs = 0;
	do {
		end = memmem(haystack + ofs, haystack_len - ofs, needle, needle_len);
		if( end )
			slen = end - (haystack + ofs);
		else
			slen = haystack_len - ofs;
		
		strings = realloc(strings, (nSubStrs+1)*sizeof(tSpiderString*));
		strings[nSubStrs] = SpiderScript_CreateString(slen, haystack + ofs);
		nSubStrs ++;

		ofs += slen + needle_len;
	} while(end);

	// Create output array
	ret = SpiderScript_CreateArray(SS_DATATYPE_STRING, nSubStrs);
	memcpy(ret->Strings, strings, nSubStrs*sizeof(tSpiderString*));
	free(strings);

	*ret_ptr = ret;
	return SS_MAKEARRAY(SS_DATATYPE_STRING);
}

SS_FCN(Exports_Lang_Struct)
{
	 int	i;
	printf("Exports_Lang_Struct: (Script=%p, NArgs=%i, Args=%p)\n", Script, NArgs, Args);
	
	for( i = 0; i < NArgs; i ++ )
	{
		printf(" Args[%i] = {Type: %i, ", i, ArgTypes[i]);
		switch(ArgTypes[i])
		{
		case SS_DATATYPE_INTEGER:
			printf(" Integer: 0x%lx", *(tSpiderInteger*)Args[i]);
			break;
		case SS_DATATYPE_REAL:
			printf(" Real: %f", *(tSpiderReal*)Args[i]);
			break;
//		case SS_DATATYPE_STRING:
//			printf(" Length: %i, Data = '%s'", Args[i]->String.Length, Args[i]->String.Data);
//			break;
		default:
			break;
		}
		printf("}\n");
	}
	
	return -1;
}
