#!/usr/bin/python
import re
import argparse
import os
import sys

global LANGHEADER
global MANGLED_OPS
LANGHEADER = os.path.normpath(os.path.dirname('./'+__file__)+"/../src/export_types.gen.h")

MANGLED_OPS = {
	"[]": "$index"
}

gsRegexFragIdent = '[A-Za-z_][A-Za-z0-9_\.]*'
gsRegexFragArgList = '[^\)]*'
gsRegexFragVArg = '\[[^\]]+\]'
gsRegexFragArg = '(?:%s|%s)' % (gsRegexFragIdent, gsRegexFragVArg)
gRegexIdent = re.compile('('+gsRegexFragIdent+')')
gRegexNamespace = re.compile('^@NAMESPACE ('+gsRegexFragIdent+')$')
gRegexClass = re.compile('^@CLASS ('+gsRegexFragIdent+')(?:<('+gsRegexFragIdent+')>)?$')
gRegexConstructor = re.compile('^@CONSTRUCTOR\s+\(('+gsRegexFragArgList+')\)$')
gRegexFunction = re.compile('^@FUNCTION\s+('+gsRegexFragIdent+'(?:\[\])*)\s+('+gsRegexFragIdent+')\s*\(('+gsRegexFragArgList+')\)$')
gRegexOperator = re.compile('^@OPERATOR\s+('+gsRegexFragIdent+'(?:\[\])*)\s+"([^"]+)"\s*\(('+gsRegexFragArgList+')\)$')


class SSSyntaxError(Exception):
	def __init__(self, reason):
		self.reason = reason
	def __str__(self):
		return self.reason

def MakeSymbolPath(namespaces,name):
	ret = ""
	for ns in namespaces:
		ret += ns + "@"
	return ret + name

def class_get_type_macro(name):
	return "TYPE_"+name.replace('@','_z_')
def mangle_sym(name):
	return name.replace("@","_")
def class_get_type_sym(name):
	return "gExports_class_"+mangle_sym(name)

def MakeCSymbol(path):
	return path.replace('@', '_')

class SSType():
	def __init__(self, desc, cast):
		self.V = desc
		self.desc = desc
		self.C = cast
		self.cast = cast
	def __repr__(self):
		return "SSType(%r,%r)" % (self.desc, self.cast)

# Initial pass
class FileScanner():
	def __init__(self):
		self._reset()
	
	def _reset(self):
		self.function_list = []
		self.class_list = []
		self.namespace_stack = []

		self.last_function_global = "NULL"	
		self.last_class = "NULL";

		self.in_class = False
		self.in_function = False
		self.in_constructor = False
		self.expect_brace = False
	
	def ProcessLine(self, line, output_code=False):
		line = line.split('//')[0].rstrip()
		stripped_line = line.lstrip()
		indent = line[0:len(line)-len(stripped_line)]
		line = stripped_line

		if len(line) == 0:
			if output_code:
				print >> self.outfile, indent+""
			return
		
		firstword = line.split()[0]	# split on whitespace
		
		if firstword == "@{":
			if line != firstword:
				raise SSSyntaxError("Chaff after '@{'");
			if not self.expect_brace:
				raise SSSyntaxError("Unexpected '@{'")
			self.expect_brace = False
			if output_code:
				print >> self.outfile, "# %i \"%s\"" % (self.lineno+1, self.filename)
		
		elif self.expect_brace:
			raise SSSyntaxError("Expect '@{'");

		elif firstword == "@}":
			if line != firstword:
				raise SSSyntaxError("Chaff after '@}'");
			if self.in_function:
				self.in_function = False
				self.in_constructor = False
				if output_code:
					# Allow the compiler to complain if a non-void doesn't return
					if self.fcn_ret.desc == "{NULL,0}":
						print >> self.outfile, indent+"\treturn 0;"
					print >> self.outfile, "}"
					print >> self.outfile, "\t#undef SS_ERRRET"
			elif self.in_class:
				self.in_class = False
				if output_code:
					# Print class definition
					sym = self.current_class.replace('@', '_')
					if self.class_has_constructor:
						constructor = "Exports_fcn_%s_%s" % (sym,"__construct")
						constructorp = "&gExports_fcnp_%s_%s" % (sym,"__construct")
					else:
						constructor,constructorp = "NULL","NULL"
					destructor = "Exports_fcn_%s_%s" % (sym,"__destructor") if self.class_has_destructor else "NULL"
					classsym = "gExports_class_"+sym
					print >> self.outfile, indent+"tSpiderClass "+classsym+" = {"
					print >> self.outfile, indent+"\t.Next="+self.last_class+","
					print >> self.outfile, indent+"\t.Name=\""+self.current_class+"\","
					print >> self.outfile, indent+"\t.TypeDef={.Class=SS_TYPECLASS_NCLASS,{.NClass=&"+classsym+"}},"
					print >> self.outfile, indent+"\t.ConstructorProto="+constructorp+","
					print >> self.outfile, indent+"\t.Constructor="+constructor+","
					print >> self.outfile, indent+"\t.Destructor="+destructor+","
					print >> self.outfile, indent+"\t.Methods="+self.last_function_class+","
					print >> self.outfile, indent+"\t.NMetaArgs=%i," % (len(self.template_args))
					print >> self.outfile, indent+"\t.NAttributes=0,"
					print >> self.outfile, indent+"\t.AttributeDefs={"
					print >> self.outfile, indent+"\t\t{NULL,{NULL,0},0,0}"
					print >> self.outfile, indent+"\t},"
					print >> self.outfile, indent+"};"
					self.last_class = "&"+classsym
			elif len(self.namespace_stack) > 0:
				self.namespace_stack.pop()	# remove last item
			else:
				raise SSSyntaxError("Unbalanced @}")

		elif firstword == "@NAMESPACE":
			m = gRegexNamespace.match(line)
			if not m:	raise SSSyntaxError("Bad @NAMESPACE")
			self.namespace_stack.append( m.group(1) )
			self.expect_brace = True
		
		elif firstword == "@CLASS":
			m = gRegexClass.match(line)
			if not m:	raise SSSyntaxError("Bad @CLASS")
			if m.group(2) != None:
				# Templated
				self.template_args = [m.group(2)]
			else:
				self.template_args = []
			self.current_class = MakeSymbolPath(self.namespace_stack, m.group(1))
			self.current_class_v = class_get_type_macro(self.current_class)
			self.class_list.append( self.current_class )
			self.expect_brace = True
			self.in_class = True
			self.class_has_constructor = False
			self.class_has_destructor = False
			self.last_function_class = "NULL"
			
			if output_code:
				print >> self.outfile, "extern tSpiderClass %s;" % (class_get_type_sym(self.current_class))
				print >> self.outfile, "extern tSpiderScript_TypeDef	gSpiderScript_TemplateInst;"
				print >> self.outfile, "extern tSpiderScript_TypeDef	gSpiderScript_TemplateArg0;"
		
		elif firstword == "@CONSTRUCTOR":
			m = gRegexConstructor.match(line)
			if not m:	raise SSSyntaxError("Bad @CONSTRUCTOR")
			if self.in_function:	raise SSSyntaxError("Nested function")
			if not self.in_class:	raise SSSyntaxError("Constructor not in class");
			self.expect_brace = True
			self.in_function = True
			self.in_constructor = True
			
			self.fcn_is_varg, self.arguments = self.parse_args( m.group(1).split(',') )
			self.fcn_ret = self.GetType(self.current_class.replace('@', '.'))
			self.class_has_constructor = True
			
			if output_code:
				print >> self.outfile, "#define SS_ERRRET NULL"
				symbol = mangle_sym(self.current_class)+'_'+'__construct'
				prototype = """
tSpiderObject *Exports_fcn_%s(tSpiderScript *Script, const tSpiderScript_TypeDef *ClassTypeDef,
	int NArgs, const tSpiderTypeRef *ArgTypes, const void * const Args[])
""" % (symbol)
				self.printFunctionHeader(symbol, "NULL", fcnname=None, prototype=prototype)
		
		elif firstword == "@DESTRUCTOR":
			if line != "@DESTRUCTOR":	raise SSSyntaxError("Bad @DESTRUCTOR")
			if self.in_function:	raise SSSyntaxError("Nested function")
			if not self.in_class:	raise SSSyntaxError("Destructor not in class");
			self.expect_brace = True
			self.in_function = True

			self.fcn_is_varg, self.arguments = False, []
			self.fcn_ret = SSType("", "void")
			self.class_has_destructor = True

			if output_code:
				print >> self.outfile, "#define SS_ERRRET ((void)0)"
				symbol = self.current_class.replace('@', '_')+'_'+'__destructor'
				print >> self.outfile, "void Exports_fcn_"+symbol+"(tSpiderObject *this)\n"
				print >> self.outfile, "{\n"
		
		elif firstword == "@OPERATOR":
			m = gRegexOperator.match(line);
			if not m:	raise SSSyntaxError("Bad @OPERATOR")
			if not self.in_class:	raise SSSyntaxError("Operator override outside of class");
			if self.in_function:	raise SSSyntaxError("Nested function")
			
			self.fcn_is_varg, self.arguments = self.parse_args( m.group(3).split(','), idx=self.in_class)
			self.fcn_ret = self.GetType( m.group(1) )
			op = m.group(2)
			
			path = self.current_class + "@operator%s" % (MANGLED_OPS[op])
			hdrname = "operator %s" % (op)
			
			this_type = self.current_class_v if len(self.template_args) == 0 else "&gSpiderScript_TemplateInst"
			self.arguments['this'] = (0, SSType("{%s,0}" %(this_type),"const tSpiderObject*"))
			symbol = path.replace('@', '_')
			defsym = "&gExports_fcn_"+symbol

			prev_function = self.last_function_class
			self.last_function_class = defsym
			
			self.expect_brace = True
			self.in_function = True
			
			if output_code:
				print >> self.outfile, "#define SS_ERRRET -1"
				self.printFunctionHeader(symbol, prev_function, hdrname, indent="")
			
		elif firstword == "@FUNCTION":
			m = gRegexFunction.match(line)
			if not m:	raise SSSyntaxError("Bad @FUNCTION")
			if self.in_function:	raise SSSyntaxError("Nested function")
			
			#print m.groups()
			self.fcn_is_varg, self.arguments = self.parse_args( m.group(3).split(','), idx=(1 if self.in_class else 0) )
			self.fcn_ret = self.GetType( m.group(1) )
			name = m.group(2)

			if self.in_class:
				path = self.current_class + "@" + m.group(2)
				hdrname = name
				this_type = self.current_class_v if len(self.template_args) == 0 else "&gSpiderScript_TemplateInst"
				self.arguments['this'] = (0, SSType("{%s,0}" %(this_type),"const tSpiderObject*"))
			else:
				path = MakeSymbolPath(self.namespace_stack, m.group(2))
				self.function_list.append( path )
				hdrname = path
			
			symbol = path.replace('@', '_')
			defsym = "&gExports_fcn_"+symbol
			
			if self.in_class:
				prev_function = self.last_function_class
				self.last_function_class = defsym
			else:
				prev_function = self.last_function_global
				self.last_function_global = defsym
			
			self.expect_brace = True
			self.in_function = True
			
			if output_code:
				print >> self.outfile, "#define SS_ERRRET -1"
				self.printFunctionHeader(symbol, prev_function, hdrname, indent="")
		
		else:
			if self.in_class:
				line = line.replace('@CLASSPTR', '&%s' % (class_get_type_sym(self.current_class)))
			line = re.sub('@TYPEOF\(\s*('+gsRegexFragIdent+')\s*\)', self.macro_TYPEOF, line)
			line = re.sub('@TYPEOF\(\s*\[(.+?)\]\s*\)', self.macro_TYPEOF_idx, line)
			line = re.sub('@TYPECODE\(\s*([^\)]+)\s*\)', self.macro_TYPE, line)
			line = re.sub('@TYPE\(\s*([^\)]+)\s*\)', self.macro_TYPE, line)
			line = re.sub('@RETURN\s*([^;]*)', self.macro_RETURN, line)
			line = re.sub('@(ARRAY)\s*\(\s*('+gsRegexFragArg+')\s*\)', self.macro_CAST, line)
			line = re.sub('@(STRING)\s*\(\s*('+gsRegexFragArg+')\s*\)', self.macro_CAST, line)
			line = re.sub('@(INTEGER)\s*\(\s*('+gsRegexFragArg+')\s*\)', self.macro_CAST, line)
			line = re.sub('@(REAL)\s*\(\s*('+gsRegexFragArg+')\s*\)', self.macro_CAST, line)
			if output_code:
				line = re.sub('@('+gsRegexFragIdent+')', self.bad_meta, line)
				print >> self.outfile, indent+line
			else:
				if line[0] == '@':
					raise SSSyntaxError("Unknown meta-operator " + firstword);
			
			# Ignored
			pass
	# ---------- Functions Header -----------
	def printFunctionProtoDef(self, indent, symbol, ret, args, is_varg):
		print >> self.outfile, indent+"tSpiderFcnProto gExports_fcnp_%s = {" % (symbol)
		print >> self.outfile, indent+"\t.ReturnType=%s,.Args={" % (ret.desc)
		_args = ["NULL"] * len(args)
		for name,arg in args.items():
			_args[arg[0]] = arg[1].desc
		for arg in _args:
			print >> self.outfile, indent+"\t\t%s," % (arg)
		print >> self.outfile, indent+"\t\t{NULL,0}"
		print >> self.outfile, indent+"\t},"
		print >> self.outfile, indent+"\t.bVariableArgs=%s," % ("1" if is_varg else "0")
		print >> self.outfile, indent+"};"
	def printFunctionHeader(self, symbol, previous, fcnname, indent="", prototype=None):
		if prototype == None:
			prototype = "__SFCN_PROTO(Exports_fcn_%s)" % (symbol)
		print >> self.outfile, indent+"%s;" % (prototype)
		# - Prototype
		self.printFunctionProtoDef(indent, symbol, self.fcn_ret, self.arguments, self.fcn_is_varg)
		# - Descriptor
		if fcnname != None:
			print >> self.outfile, indent+"tSpiderFunction gExports_fcn_%s = {" % (symbol)
			print >> self.outfile, indent+"\t.Next=%s, .Name=\"%s\"," % (previous, fcnname)
			print >> self.outfile, indent+"\t.Handler=Exports_fcn_%s, .Prototype=&gExports_fcnp_%s," % (symbol, symbol)
			print >> self.outfile, indent+"};"
		# - Code header
		print >> self.outfile, indent+"%s" % (prototype)
		print >> self.outfile, indent+"{"
		if self.fcn_is_varg:
			print >> self.outfile, indent+"\t__SS_BUGCHECK(NArgs >= %i);" % (len(self.arguments))
			print >> self.outfile, indent+"\tconst int VArgC = NArgs - %i;" % (len(self.arguments))
			print >> self.outfile, indent+"\tconst tSpiderTypeRef *const VArgT = &ArgTypes[%i];" % (len(self.arguments))
			print >> self.outfile, indent+"\tconst void *const*const VArgV = &Args[%i];" % (len(self.arguments))
			print >> self.outfile, indent+"\t(void)VArgC;(void)VArgT;(void)VArgV;"
		else:
			print >> self.outfile, indent+"\t__SS_BUGCHECK(NArgs == %i);" % (len(self.arguments))
		for name,arg in self.arguments.items():
			cast = arg[1].cast
			idx = arg[0]
			if cast[-1:] == "*":
				print >> self.outfile, indent+"\t%s %s = Args[%i];" % (cast, name, idx)
			else:
				print >> self.outfile, indent+"\t%s %s = *(%s*const)Args[%i];" % (cast, name, cast, idx)
			print >> self.outfile, indent+"\t(void)%s;" % (name)
		print >> self.outfile, "# %i \"%s\"" % (self.lineno+1, self.filename)

	def GetType(self,ident):
		array_level = 0
		while len(ident) > 2 and ident[-2:] == "[]":
			array_level += 1
			ident = ident[0:-2]
		
		if ident == "*":
			if array_level:	raise SSSyntaxError("Untyped arrays are not allowed")
			code = "&gSpiderScript_AnyType"
			cast = "const void*"
		elif ident == "void":
			if array_level:	raise SSSyntaxError("void arrays are not allowed")
			code = "NULL"
			cast = "void"
		elif ident == "Boolean":
			code = "&gSpiderScript_BoolType"
			cast = "tSpiderBool"
		elif ident == "Integer":
			code = "&gSpiderScript_IntegerType"
			cast = "tSpiderInteger"
		elif ident == "Real":
			code = "&gSpiderScript_RealType"
			cast = "tSpiderReal"
		elif ident == "String":
			code = "&gSpiderScript_StringType"
			cast = "const tSpiderString *"
		elif ident in self.template_args:
			code = "&gSpiderScript_TemplateArg%i" % (self.template_args.index(ident))
			cast = "const void*"
		else:
			if not gRegexIdent.match(ident):
				raise SSSyntaxError("Invalid identifier")
			code = "TYPE_" + ident.replace(".", "_z_")
			cast = "const tSpiderObject *";
		
		if array_level > 0:
			cast = "const tSpiderArray*"
		
		ret = SSType("{%s,%i}" % (code, array_level), cast)
		return ret

	def parse_args(self, arglist, idx=0):
		is_var = False
		args = {}

		if len(arglist) == 1 and arglist[0] == "":
			return (is_var, args)

		for arg in arglist:
			if is_var:
				raise SSSyntaxError("arguments after ...")
			arg = arg.strip()
			if arg == "...":
				is_var = True
			else:
				split_arg = arg.split()
				if len(split_arg) != 2:	raise SSSyntaxError("badly formatted argument '%s'" % (arg))
				typesym,name = split_arg
				args[name] = (idx, self.GetType(typesym))
				idx += 1
		return (is_var, args)
				
	
	# ---------- Inline Operators -----------
	def macro_TYPE(self, m):
		return "((tSpiderTypeRef)%s)" % ( self.GetType(m.group(1)).desc )
	def macro_TYPEOF(self, m):
		name = m.group(1)
		if not name in self.arguments:
			raise SSSyntaxError("Argument '%s' does not exist (TYPEOF)" % (name))
		if "gSpiderScript_AnyType" in self.arguments[name][1].desc:
			return "ArgTypes[%i]" % ( self.arguments[name][0] )
		else:
			return "((tSpiderTypeRef)%s)" % ( self.arguments[name][1].desc )
	def macro_TYPEOF_idx(self, m):
		name = m.group(1)
		return "ArgTypes[%i+(%s)]" % (len(self.arguments), name)
	def macro_RETURN(self, m):
		val = m.group(1).strip()
		if self.in_class and self.in_constructor:
			return "return %s" % (val)
		elif self.fcn_ret.desc == "{NULL,0}":
			if val != "":
				raise SSSyntaxError("Returning non-void from void")
			return "return 0"
		else:
			return "do{*(%s*)RetData = (%s);return 0;}while(0)" % (self.fcn_ret.cast, val)
	def macro_CAST(self,m):
		op_type = m.group(1)
		name = m.group(2)
		c_cast = {
			"BOOL": 	"const tSpiderBool",
			"INTEGER":	"const tSpiderInteger",
			"REAL": 	"const tSpiderReal",
			"STRING":	"const tSpiderString*",
			"ARRAY":	"const tSpiderArray*",
			}[op_type]
		if name[0] == '[':
			name = name[1:-1]
			name = "({int idx=(%s);__SS_BUGCHECK(%i+idx<NArgs);VArgV[idx];})" % (name, len(self.arguments))
		else:
			if not name in self.arguments:
				raise SSSyntaxError("Argument '%s' does not exist (cast)" % (name))
			argtype = self.arguments[name][1]
			if not "gSpiderScript_AnyType" in argtype.desc:
				raise SSSyntaxError("Can't cast strictly typed argument '%s'" % (name))
		
		if c_cast[-1:] == "*":
			return "((%s)%s)" % (c_cast, name)
		else:
			return "(*(%s*)%s)" % (c_cast, name)
	
	def getvalue(self, m):
		name = m.group(1)
		if not name in self.arguments:
			raise SSSyntaxError("")
		if self.arguments[name][1] == "gSpiderScript_AnyType":
			raise SSSyntaxError("")
		return name
	def bad_meta(self, m):
		raise SSSyntaxError("Unknown meta-operator '@%s'" % m.group(1))

	# ---------- Root Functions ----------
	def ProcessFile(self, infilename, output_code=False):
		with open(infilename, "r") as fh:
			self.filename = infilename
			self.lineno = 0
			for line in fh:
				self.lineno += 1
				try:
					self.ProcessLine(line, output_code)
				except SSSyntaxError as e:
					print "%s:%i: Syntax Error: %s" % (infilename, self.lineno, e)
					print line
					sys.exit(1)
			pass # for
		print "Processed %s" % (infilename)
	
	def ConvertFile(self, infilename, outfilename, headerfile):
		self._reset()
		with open(outfilename, "w") as self.outfile:
			print >> self.outfile, "// Auto-generated from '%s'" % (infilename)
			print >> self.outfile, "#include <spiderscript.h>";
			print >> self.outfile, "#include <assert.h>";
			print >> self.outfile, "#include <%s>" % (headerfile);
			print >> self.outfile, "#define __SFCN_PROTO(n) int n(tSpiderScript*Script,void*RetData,int NArgs,const tSpiderTypeRef*ArgTypes,const void*const Args[])"
			print >> self.outfile, "#define __SS_BUGCHECK(cnd)	do{if(!(cnd)){return SpiderScript_ThrowException(Script,SS_EXCEPTION_BUG,\"Assertion failure '\"#cnd\"'\"),SS_ERRRET;}}while(0)"
			#print >> self.outfile, "#define __SFCN_DEF(i,p,r,n,a...)	tSpiderFunction gExports_fcn_##i = {.Next=p,.Name=n,.Handler=Exports_fcn_##i,.Prototype={.ReturnType=r,.ArgTypes={a}}}"
			self.ProcessFile(infilename, True)
		pass # with outfile
	def GenerateHeader(self, outfilename):
		with open(outfilename, "w") as self.outfile:
			if self.is_lang:
				print "Language header"
				flag = "0x3000"
				prefix = "SS_EXPORT "
			else:
				print "Provider header"
				flag = "0x1000"
				prefix = ""
				global LANGHEADER
				print >> self.outfile, "#include \"%s\"" % (LANGHEADER)
			
			for index, sclass in enumerate(self.class_list):
				typename = class_get_type_macro(sclass)
				symname = class_get_type_sym(sclass)
				print >> self.outfile, "%sextern tSpiderClass %s;" % (prefix, symname)
				print >> self.outfile, "#define %s &%s.TypeDef" % (typename, symname)
	def GenerateIndex(self, outfilename, headerfile):
		with open(outfilename, "w") as self.outfile:
			print >> self.outfile, "#include <spiderscript.h>"
			print >> self.outfile, "#include \"%s\"" % (headerfile)
			
			for sclass in self.class_list:
				symname = class_get_type_sym(sclass)
				print >> self.outfile, "extern tSpiderClass %s;" % (symname)
			for fcnname in self.function_list:
				print >> self.outfile, "extern tSpiderFunction gExports_fcn_%s;" % (mangle_sym(fcnname))
			print >> self.outfile, "int giNumExportedClasses = %i;" % (len(self.class_list))
			print >> self.outfile, "int giNumExportedFunctions = %i;" % (len(self.function_list))
			
			print >> self.outfile, "tSpiderClass *gapExportedClasses[] = {"
			for sclass in self.class_list:
				print >> self.outfile, "\t&%s," % (class_get_type_sym(sclass))
			print >> self.outfile, "\tNULL"
			print >> self.outfile, "};"
			
			print >> self.outfile, "tSpiderFunction *gapExportedFunctions[] = {"
			for fcnname in self.function_list:
				print >> self.outfile, "\t&gExports_fcn_%s," % (mangle_sym(fcnname))
			print >> self.outfile, "\tNULL"
			print >> self.outfile, "};"
			print >> self.outfile, ""

if __name__ == "__main__":
	parser = argparse.ArgumentParser(description='Convert SpiderScript meta-files into C source')
	parser.add_argument('-H', '--header', dest='header', metavar='header', help='Header filename')
	parser.add_argument('-M', '--mode', dest='mode', choices=['code','mkhdr','index'], default='code', help='Operation mode')
	parser.add_argument('--lang', dest='is_lang', action='store_true', default=False, help='Generate language-provided header')
	parser.add_argument('-o', '--output', dest='outfile', help='Output file', required=True)
	parser.add_argument('files', nargs='+', metavar='files')
	args = parser.parse_args()
	
	# TODO: Parse arguments
	fs = FileScanner()
	fs.is_lang = args.is_lang
	if args.mode == 'code':
		if len(args.files) != 1:
			raise exception("")
		fs.ConvertFile(args.files[0], args.outfile, args.header)
	elif args.mode == 'mkhdr':
		for filename in args.files:
			fs.ProcessFile(filename)
		fs.GenerateHeader( args.outfile )
	elif args.mode == 'index':
		for file in args.files:
			fs.ProcessFile(file)
		fs.GenerateIndex( args.outfile, args.header )
