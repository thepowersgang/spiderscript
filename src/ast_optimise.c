/*
 * SpiderScript Library
 * by John Hodge (thePowersGang)
 * 
 * bytecode_gen.c
 * - Generate bytecode
 */
#include <stdlib.h>
#include "ast.h"
#include <string.h>

#define MAX_ADD_CHAIN	32

// === CODE ===
tAST_Node *AST_Optimise_MakeString(tAST_Node **ents, int len, int First, int Cur)
{
	tAST_Node *ns = AST_NewString(NULL, NULL, len);
	len = 0;
	for( int j = First; j < Cur; j++ )
	{
		memcpy(ns->ConstString->Data + len,
			ents[j]->ConstString->Data,
			ents[j]->ConstString->Length
			);
		len += ents[j]->ConstString->Length;
		AST_FreeNode(ents[j]);
	}
	return ns;
}

tAST_Node *AST_Optimise(tAST_Node *Node)
{
	tAST_Node	*l, *r;
	switch(Node->Type)
	{
	case NODETYPE_BLOCK:
		break;
	
	case NODETYPE_ADD:
		// Detect a chain of string additions
		l = Node->BinOp.Left = AST_Optimise(Node->BinOp.Left);

		if( l->Type == NODETYPE_STRING )
		{
			 int	len = l->ConstString->Length, nStr = 1;
			 int	nEnts = 1;
			tAST_Node	*ents[MAX_ADD_CHAIN];
			
			ents[0] = l;
			r = Node->BinOp.Right;
			while( r->Type == NODETYPE_ADD )
			{
				if(r->BinOp.Left->Type == NODETYPE_STRING )
					nStr ++, len += r->BinOp.Left->ConstString->Length;
				if( nEnts < MAX_ADD_CHAIN ) {
					ents[nEnts] = r->BinOp.Left;
					r->BinOp.Left = NULL;
				}
				nEnts ++;
				r = r->BinOp.Right;
			}
			if( nEnts == 1 ) {
				// Nothing!
				return Node;
			}
			if( nEnts == nStr ) {
				// Single output string
				tAST_Node *ns = AST_Optimise_MakeString(ents, len, 0, nEnts);
				AST_FreeNode(Node);
				return ns;
			}

			// Complex string - Translate into function call
			tAST_Node *fc = AST_NewFunctionCall(NULL, "Lang.Strings.Concat");
			
			 int	first = 0;
			for( int i = 0; i < nEnts; i ++ )
			{
				if( ents[i]->Type == NODETYPE_STRING )
					len += ents[i]->ConstString->Length;
				else {
					if( first < i )
					{
						tAST_Node *ns = AST_Optimise_MakeString(ents, len, first, i);
						AST_AppendFunctionCallArg(fc, ns);
						len = 0;
					}
					AST_AppendFunctionCallArg(fc, ents[i]);
					
					first = i + 1;
				}
			}
			if( first < nEnts )
			{
				tAST_Node *ns = AST_Optimise_MakeString(ents, len, first, nEnts);
				AST_AppendFunctionCallArg(fc, ns);
			}
			AST_FreeNode(Node);
			return fc;
		}
		else
		{
			Node->BinOp.Right = AST_Optimise(Node->BinOp.Right);
		}
		break;
	default:
		break;
	}
	return Node;
}
