import re

gsRegexFragIdent = '[A-Za-z_][A-Za-z0-9_]*'
gsRegexFragArgList = '\(([^\)]*)\)'
gRegexIdent = re.compile('('+gsRegexFragIdent+')')
gRegexNamespace = re.compile('^@NAMESPACE ('+gsRegexFragIdent+')$')
gRegexClass = re.compile('^@CLASS ('+gsRegexFragIdent+')$')
gRegexConstructor = re.compile('^@CONSTRUCTOR\s+'+gsRegexFragArgList+'$')
gRegexFunction = re.compile('^@FUNCTION\s+('+gsRegexFragIdent+'(?:\[\])*)\s+('+gsRegexFragIdent+')'+gsRegexFragArgList+'$')

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

def MakeCSymbol(path):
	return path.replace('@', '_')

class SSType():
	def __init__(self, desc, cast):
		self.V = desc
		self.desc = desc
		self.C = cast
		self.cast = cast

def GetType(ident):
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
	elif ident == "Real":
		code = "&gSpiderScript_StringType"
		cast = "const tSpiderString *"
	else:
		if not gRegexIdent.match(ident):
			raise SSSyntaxError("Invalid identifier")
		code = "TYPE_" + ident.replace(".", "_z_")
		cast = "const tSpiderObject *";
	
	if array_level > 0:
		cast = "const tSpiderArray*"
	
	return SSType("{%s,%i}" % (code, array_level), cast)

def parse_args( arglist ):
	is_var = False
	args = {}

	if len(arglist) == 1 and arglist[0] == "":
		return (is_var, args)

	idx = 0
	for arg in arglist:
		if is_var:
			raise SSSyntaxError("arguments after ...")
		if arg == "...":
			is_var = True
		else:
			split_arg = arg.split()
			if len(split_arg) != 2:	raise SSSyntaxError("badly formatted argument '%s'" % (arg))
			typesym,name = split_arg
			args[name] = (idx, GetType(typesym))
			idx += 1
	return (is_var, args)

# Initial pass
class FileScanner():
	def __init__(self):
		self._reset()
	
	def _reset(self):
		self.function_list = []
		self.class_list = []
		self.namespace_stack = []

		self.last_function_global = "NULL"	

		self.in_class = False
		self.in_function = False
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
		
		elif self.expect_brace:
			raise SSSyntaxError("Expect '@{'");

		elif firstword == "@}":
			if line != firstword:
				raise SSSyntaxError("Chaff after '@}'");
			if self.in_function:
				self.in_function = False
				if output_code:
					# Allow the compiler to complain if a non-void doesn't return
					if self.fcn_ret.desc == "{NULL,0}":
						print >> self.outfile, indent+"\treturn 0;"
					print >> self.outfile, "}"
			elif self.in_class:
				self.in_class = False
				if output_code:
					# Print class definition
					sym = self.current_class.replace('@', '_')
					constructor = "&gExports_fcn_"+sym+"_"+"__construct" if self.class_has_constructor else "NULL"
					destructor = "&gExports_fcn_"+sym+"_"+"__destruct" if self.class_has_destructor else "NULL"
					classsym = "gExports_class_"+sym
					print >> self.outfile, indent+"tSpiderSclass "+classsym+" = {"
					print >> self.outfile, indent+"\t.Next="+self.last_class+","
					print >> self.outfile, indent+"\t.Name=\""+self.current_class+"\","
					print >> self.outfile, indent+"\t.TypeDef={.Class=SS_TYPECLASS_NCLASS,{.NClass=&"+classsym+"}},"
					print >> self.outfile, indent+"\t.Constructor="+constructor+","
					print >> self.outfile, indent+"\t.Destructor="+destructor+","
					print >> self.outfile, indent+"\t.Methods="+self.last_function_class+","
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
			self.current_class = MakeSymbolPath(self.namespace_stack, m.group(1))
			self.current_class_v = "TYPE_"+self.current_class.replace('@','_z_')
			self.class_list.append( self.current_class )
			self.expect_brace = True
			self.in_class = True
			self.class_has_constructor = False
			self.class_has_destructor = False
			self.last_function_class = "NULL"
			
			if output_code:
				print >> self.outfile, "extern tSpiderClass gExports_class_"+self.current_class.replace("@","_")
		
		elif firstword == "@CONSTRUCTOR":
			m = gRegexConstructor.match(line)
			if not m:	raise SSSyntaxError("Bad @CONSTRUCTOR")
			if self.in_function:	raise SSSyntaxError("Nested function")
			if not self.in_class:	raise SSSyntaxError("Constructor not in class");
			self.expect_brace = True
			self.in_function = True
			
			self.fcn_is_varg, self.arguments = parse_args( m.group(1).split(',') )
			self.fcn_ret = GetType(self.current_class)
			self.class_has_constructor = True
			
			if output_code:
				symbol = self.current_class.replace('@', '_')+'_'+'__construct'
				self.printFunctionHeader(symbol, "NULL", '__construct')
		
		elif firstword == "@DESTRUCTOR":
			if line != "@DESTRUCTOR ()":	raise SSSyntaxError("Bad @DESTRUCTOR")
			if self.in_function:	raise SSSyntaxError("Nested function")
			if not self.in_class:	raise SSSyntaxError("Destructor not in class");
			self.expect_brace = True
			self.in_function = True

			self.fcn_is_varg, self.arguments = False, []
			self.fcn_reg = ("-1","void")

			if output_code:
				symbol = self.current_class.replace('@', '_')+'_'+'__destructor'
				print >> self.outfile, "void Exports_fcn_"+symbol+"(tSpiderObject *this)\n"
				print >> self.outfile, "{\n"
		
		elif firstword == "@FUNCTION":
			m = gRegexFunction.match(line)
			if not m:	raise SSSyntaxError("Bad @FUNCTION")
			if self.in_function:	raise SSSyntaxError("Nested function")

			self.fcn_is_varg, self.arguments = parse_args( m.group(3).split(',') )
			self.fcn_ret = GetType( m.group(1) )
			name = m.group(2)

			if self.in_class:
				path = self.self.current_class + "@" + m.group(2)
				hdrname = name
				self.arguments.insert( ("{"+self.current_class_v+",0}","const tSpiderObject*") )
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
				self.printFunctionHeader(symbol, prev_function, path)
		
		else:
			line = re.sub('@TYPEOF\(\s*('+gsRegexFragIdent+')\s*\)', self.macro_TYPEOF, line)
			line = re.sub('@TYPE\(\s*([^\)]+)\s*\)', self.macro_TYPE, line)
			line = re.sub('@RETURN\s*([^;]*)', self.macro_RETURN, line)
			line = re.sub('@(ARRAY)\s*\(\s*('+gsRegexFragIdent+')\s*\)', self.macro_CAST, line)
			line = re.sub('@(STRING)\s*\(\s*('+gsRegexFragIdent+')\s*\)', self.macro_CAST, line)
			if output_code:
				line = re.sub('@('+gsRegexFragIdent+')', self.bad_meta, line)
				print >> self.outfile, indent+line
			else:
				if line[0] == '@':
					raise SSSyntaxError("Unknown meta-operator " + firstword);
			
			# Ignored
			pass
	
	# ---------- Functions Header -----------
	def printFunctionHeader(self, symbol, previous, name, indent=""):
		print >> self.outfile, indent+"__SFCN_PROTO(Exports_fcn_%s);" % (symbol)
		# - Prototype
		print >> self.outfile, indent+"tSpiderFcnProto gExports_fcnp_%s = {" % (symbol)
		print >> self.outfile, indent+"\t.ReturnType=%s,.Args={" % (self.fcn_ret.desc)
		for idx,arg in self.arguments.items():
			print >> self.outfile, indent+"\t\t%s," % (arg[1].desc)
		if self.fcn_is_varg:
			print >> self.outfile, indent+"\t\t{NULL,0}"
		else:
			print >> self.outfile, indent+"\t\t{NULL,0}"
		print >> self.outfile, indent+"\t}"
		print >> self.outfile, indent+"};"
		# - Descriptor
		print >> self.outfile, indent+"tSpiderFunction gExports_fcn_%s" % (symbol)
		print >> self.outfile, indent+"\t.Next=%s, .Name=\"%s\"," % (previous, name)
		print >> self.outfile, indent+"\t.Handler=Exports_fcn_%s, .Prototype=&gExports_fcnp_%s," % (symbol, symbol)
		print >> self.outfile, indent+"};"
		# - Code header
		print >> self.outfile, indent+"__SFCN_PROTO(Exports_fcn_%s)" % (symbol)
		print >> self.outfile, indent+"{"
		if self.fcn_is_varg:
			print >> self.outfile, indent+"\tassert(NArgs >= %i)" % (len(self.arguments))
			print >> self.outfile, indent+"\tconst int VArgC = NArgs - %i" % (len(self.arguments))
			print >> self.outfile, indent+"\tconst void **VArgV = &Args[%i]" % (len(self.arguments))
		else:
			print >> self.outfile, indent+"\tassert(NArgs == %i)" % (len(self.arguments))
		for name,arg in self.arguments.items():
			cast = arg[1].cast
			idx = arg[0]
			if cast[-1:] == "*":
				print >> self.outfile, indent+"\t%s %s = Args[%i];" % (cast, name, idx)
			else:
				print >> self.outfile, indent+"\t%s %s = *(const %s*)Args[%i];" % (cast, name, cast, idx)
				
	
	# ---------- Inline Operators -----------
	def macro_TYPE(self, m):
		return "(tSpiderTypeRef)%s" % ( GetType(m.group(1)).desc )
	def macro_TYPEOF(self, m):
		name = m.group(1)
		if not name in self.arguments:
			raise SSSyntaxError("Argument '%s' does not exist (TYPEOF)" % (name))
		if self.arguments[name][1].desc == "gSpiderScript_AnyType":
			return "ArgTypes[%i]" % ( self.arguments[name][0] )
		else:
			return "(tSpiderTypeRef)%s" % ( self.arguments[name][1].desc )
	def macro_RETURN(self, m):
		val = m.group(1).strip()
		if self.fcn_ret.desc == "{NULL,0}":
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
		if not name in self.arguments:
			raise SSSyntaxError("Argument '%s' does not exist (cast)" % (name))
		argtype = self.arguments[name][1]
		if not "gSpiderScript_AnyType" in argtype.desc:
			raise SSSyntaxError("Can't cast strictly typed argument '%s'" % (name))
		
		if c_cast[-1:0] == "*":
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
	def ScanFile(self, filename):
		global gRegexNamespace
		with open(filename) as fh:
			lineno = 0
			for line in fh:
				lineno += 1
				try:
					self.ProcessLine(line)
				except SSSyntaxError as e:
					print "%s:%i: Syntax Error: %s" % (filename, lineno, e)
					print line
					return
	
	def ConvertFile(self, infilename, outfilename):
		self._reset()
		with open(outfilename, "w") as self.outfile:
			print >> self.outfile, "// Auto-generated from '%s'" % (infilename)
			print >> self.outfile, "#include <spiderscript.h>";
			print >> self.outfile, "#include <$gHeaderFile>";
			print >> self.outfile, "#define __SFCN_PROTO(n) int n(tSpiderScript*Script,void*RetData,int NArgs,const tSpiderTypeRef*ArgTypes,const void*const Args[])"
			print >> self.outfile, "#define __SS_BUCHECK(cnd)	do{if(!(cnd)){return SpiderScript_ThrowException(Script,SS_EXCEPTION_BUG,\"Assertion failure '\"#cnd\"'\");}}while(0)"
			#print >> self.outfile, "#define __SFCN_DEF(i,p,r,n,a...)	tSpiderFunction gExports_fcn_##i = {.Next=p,.Name=n,.Handler=Exports_fcn_##i,.Prototype={.ReturnType=r,.ArgTypes={a}}}"
			with open(infilename, "r") as fh:
				lineno = 0
				for line in fh:
					lineno += 1
					try:
						self.ProcessLine(line, True)
					except SSSyntaxError as e:
						print "%s:%i: Syntax Error: %s" % (infilename, lineno, e)
						print line
						return
				pass # for
			pass # with infile
		pass # with outfile
				

if __name__ == "__main__":
	# TODO: Parse arguments
	fs = FileScanner()
	fs.ConvertFile("libspiderscript/src/exports.ssf", "1.c")
	#fs.ScanFile("libspiderscript/src/exports.ssf")
	
	print fs.function_list
