#!/usr/bin/perl
use strict;
use warnings;

my $gIdentRegex = qr/[A-Za-z_]([A-Za-z_]+)/;
my $cRegexNamespace = qr/^\@NAMESPACE ([A-Za-z]+)$/;
my $cRegexClass     = qr/^\@CLASS ($gIdentRegex)$/;
my $cRegexArgList   = qr/\(([^\)]*)\)/;
my $cRegexFunction  = qr/^\@FUNCTION ([^\s]+)\s+([A-Za-z_0-9]+)\s*$cRegexArgList$/;
my $cRegexConstructor = qr/^\@CONSTRUCTOR \s*$cRegexArgList$/;

sub make_path
{
	my @namespace_stack = @{$_[0]};
	my $name = $_[1];
	my $rv = "";
	foreach my $ns (@namespace_stack) {
		$rv .= $ns."@";
	}
	$rv .= $name;
	return $rv;
}

sub make_sym
{
	my $ret = $_[0];
	$ret =~ s/@/_/g;
	return $ret;
}

sub scan_file
{
	my @namespace_stack;
	my $infile = $_[0];
	my $line = 0;
	my @classnames;
	my @funcnames;
	my $bInClass = 0;
	my $bInFunction = 0;
	
	# Quick intial pass to turn classes into numbers
	open(INFILE, "<", $infile) or die "Couldn't open ", $infile, " for reading";
	while(<INFILE>)
	{
		$line ++;
		s#//.*$##;	# Kill comments
		s/^\s+//g;
		s/\s+$//g;
		if( /^[^\@]/ || /^$/ )
		{
			# ignore!
		}
		elsif( /^\@NAMESPACE / )
		{
			/$cRegexNamespace/ or die "Syntax error on line $line after \@NAMESPACE";
			push @namespace_stack, $1;
			next;
		}
		elsif( /^\@CLASS / )
		{
			/$cRegexClass/ or die "Syntax error on line $line after \@CLASS";
			my $name = make_path(\@namespace_stack, $1);
			push @classnames, $name;
			$bInClass = 1;
			next;
		}
		elsif( /^\@CONSTRUCTOR/ )
		{
			/$cRegexConstructor/ or die "Syntax error on line $line after \@CONSTRUCTOR";
			$bInFunction = 1;
			next;
		}
		elsif( /^\@DESTRUCTOR/ )
		{
			/^\@DESTRUCTOR$/ or die "Syntax error on line $line after \@DESTRUCTOR";
			$bInFunction = 1;
			next;
		}
		elsif( /^\@FUNCTION / )
		{
			/$cRegexFunction/ or die "Syntax error on line $line after \@FUNCTION";

			if( !$bInClass ) {
				my $name = make_path(\@namespace_stack, $2);
				push @funcnames, $name;
			}		

			$bInFunction = 1;
			next;
		}
		elsif( /^\@{/ )
		{
		}
		elsif( /^\@}/ )
		{
			if( $bInFunction ) {
				$bInFunction = 0;
			}
			elsif( $bInClass ) {
				$bInClass = 0;
			}
			else {
				pop @namespace_stack;
			}
		}
		elsif( /^\@RETURN/ )
		{
			# Ignored
		}
		else
		{
			die "Huh? - ", $_, "\n";
		}
	}
	close INFILE;

	return (\@classnames, \@funcnames);
}

my @infiles;
my $gOutFilePath;
my $gHeaderFile = "types.gen.h";
my $gMode = "convert";

for( my $i = 0; $i < (scalar @ARGV); $i ++ )
{
	my $arg = $ARGV[$i];

	if( $arg =~ /^\-[a-zA-Z]/ ) {
		($arg eq "-H") and $gHeaderFile = $ARGV[++$i];
		next ;
	}
	
	if( $arg =~ /^\-\-/ ) {
		($arg eq "--mkhdr")      and $gMode = "mkhdr";
		($arg eq "--mkhdr-lang") and $gMode = "mkhdr-lang";
		($arg eq "--mkidx")      and $gMode = "mkidx";
		next;
	}

	if( !defined $gOutFilePath ) {
		$gOutFilePath = $arg;
	}
	else {
		push @infiles, $arg;
	}
}


if( $gMode =~ /^mkhdr/ )
{
	my @gClasses;

	for my $i (0 .. scalar @infiles-1)
	{
		my @refl = scan_file $infiles[$i];
		push @gClasses, @{$refl[0]};
	}

	open(OUTFILE, ">", $gOutFilePath) or die "Couldn't open ", $gOutFilePath, " for writing";

	my $flag = "0x1000";
	if( $gMode =~ /lang$/ ) {
		$flag = "0x3000";
	}
	else {
		# Child header
		use File::Basename;
		print OUTFILE "#include \"".dirname(__FILE__)."/../src/export_types.gen.h\"\n";
	}

	my $classcount = scalar @gClasses;
	for my $index (0 .. $classcount-1) {
		my $name = "TYPE_".$gClasses[$index];
		$name =~ s/@/_z_/g;
		print OUTFILE "#define $name ($flag|".$index.")\n";
	}
	close(OUTFILE);
	exit ;
}

if( $gMode eq "mkidx" )
{
	my @gClasses;
	my @gFunctions;

	for my $i (0 .. scalar @infiles-1)
	{
		my @refl = scan_file $infiles[$i];
		push @gClasses,   @{$refl[0]};
		push @gFunctions, @{$refl[1]};
	}
	
	open(OUTFILE, ">", $gOutFilePath) or die "Couldn't open ", $gOutFilePath, " for writing";
	
	print OUTFILE "#include <spiderscript.h>\n";
	print OUTFILE "#include \"$gHeaderFile\"\n";
	
	for my $index (0 .. scalar @gClasses-1) {
		print OUTFILE "extern tSpiderClass gExports_class_".make_sym($gClasses[$index]).";\n";
	}
	for my $index (0 .. scalar @gFunctions-1) {
		print OUTFILE "extern tSpiderFunction gExports_fcn_".make_sym($gFunctions[$index]).";\n";
	}
	print OUTFILE "int giNumExportedClasses = ",scalar @gClasses,";\n";
	print OUTFILE "int giNumExportedFunctions = ",scalar @gFunctions,";\n";
	
	print OUTFILE "tSpiderClass *gapExportedClasses[] = {\n";
	my $classcount = scalar @gClasses;
	for my $index (0 .. $classcount-1) {
		print OUTFILE "\t&gExports_class_".make_sym($gClasses[$index]).",\n";
	}
	print OUTFILE "\tNULL\n";
	print OUTFILE "};\n";
	
	print OUTFILE "tSpiderFunction *gapExportedFunctions[] = {\n";
	for my $index (0 .. scalar @gFunctions - 1) {
		print OUTFILE "\t&gExports_fcn_".make_sym($gFunctions[$index]).",\n";
	}
	print OUTFILE "\tNULL\n";
	print OUTFILE "};\n";
	
	print OUTFILE "\n";
	close(OUTFILE);
	exit ;
}

my $line = 0;
my @namespace_stack = ();

sub gettype
{
	my $code;
	my $cast;
	my $ident = $_[0];
	my $array_level = 0;
	
	while( $ident =~ s/\[\]$// )
	{
		$array_level ++;
	}
	
	if( $ident eq "*" ) {
		$array_level and die "Can't have an undef array";
		return ("-1", "const void*");
	}
	elsif( $ident eq "void" ) {
		$array_level and die "Can't have an void array";
		return ("0", "void");
	}
	elsif( $ident eq "Boolean" ) {
		$code = "SS_DATATYPE_BOOLEAN";
		$cast = "tSpiderBool";
	}
	elsif( $ident eq "Integer" ) {
		$code = "SS_DATATYPE_INTEGER";
		$cast = "tSpiderInteger";
	}
	elsif( $ident eq "Real" ) {
		$code = "SS_DATATYPE_REAL";
		$cast = "tSpiderReal";
	}
	elsif( $ident eq "String" ) {
		$code = "SS_DATATYPE_STRING";
		$cast = "const tSpiderString *";
	}
	else {
		if( $ident !~ $gIdentRegex ) {
			die "$code is not an ident";
		}
		$code = "TYPE_".$ident;
		$code =~ s/\./_z_/g;
		$cast = "const tSpiderObject*";
	}
	
	if( $array_level )
	{
		$code = "SS_MAKEARRAYN($code, $array_level)";
		$cast = "const tSpiderArray*";
	}

	return ($code,$cast);
}

my $gLastFunction = "NULL";
my $gLastClass = "NULL";

my $gCurClass = "";
my $gCurClass_V = "";
my $gClassLastFunction;
my $gClassConstructor;
my $gClassDestructor;
my %gClassProperties;	# Name->TypeCode

my $bInFunction = 0;
my @gFcnArgs;	# Names
my %gFcnArgs_C;	# Name->Cast
my %gFcnArgs_V;	# Name->Value
my %gFcnArgs_I;	# Name->Index
my $gFcnRetType_C;	# C Datatype
my $gFcnRetType_V;	# Integral type code
my $indent;

sub function_header
{
	my $symbol = $_[0];
	my $prev = $_[1];
	my $fcnname = $_[2];
	print OUTFILE $indent,"__SFCN_PROTO(Exports_fcn_$symbol);\n";
	print OUTFILE $indent,"__SFCN_DEF($symbol, $prev, $gFcnRetType_V, \"$fcnname\"";
	foreach my $arg (@gFcnArgs) {
		print OUTFILE ", $gFcnArgs_V{$arg}";
	}
	print OUTFILE ");\n";
	print OUTFILE $indent,"__SFCN_PROTO(Exports_fcn_$symbol)\n";
	print OUTFILE $indent,"{\n";
	
	# TODO: Do argument validation
	foreach my $arg (keys %gFcnArgs_C)
	{
#		$gFcnArgs_V{$arg} eq "-1" and next;
		my $cast = $gFcnArgs_C{$arg};
		if( $cast =~ /\*$/ ) {
			print OUTFILE $indent,"\t$cast $arg = Args[$gFcnArgs_I{$arg}];\n";
		} else {
			print OUTFILE $indent,"\t$cast $arg = *(const $cast*)Args[$gFcnArgs_I{$arg}];\n";
		}
	}
}

if( scalar @infiles != 1 ) {
	die "Only one input file allowed for code transformation";
}
my $infile = $infiles[0];
open(INFILE, "<", $infile) or die "Couldn't open ", $infile, " for reading";

# Open output and append header
open(OUTFILE, ">", $gOutFilePath) or die "Couldn't open ", $gOutFilePath, " for writing";
print OUTFILE "#define __SFCN_PROTO(n) int n(tSpiderScript*Script,void*RetData,int NArgs,const int*ArgTypes,const void*const Args[])\n";
print OUTFILE "#define __SFCN_DEF(i,p,r,n,a...)	tSpiderFunction gExports_fcn_##i = {.Next=p,.Name=n,.Handler=Exports_fcn_##i,.ReturnType=r,.ArgTypes={a}}\n";
print OUTFILE "#include <spiderscript.h>\n";
print OUTFILE "#include <$gHeaderFile>\n";

my $has_been_meta = 1;
my $expecting_open_brace = 0;
while(<INFILE>)
{
	$line ++;

	my $orig_line = $_;
	
	s#//.*$##;	# Kill comments
	$indent = "";	
	while(s/^(\s)//)	# Trim off blanks
	{
		$indent .= $1;
	}
	s/\s+$//;	# Trim off blanks

	if( /^$/ )
	{
		print OUTFILE "\n";
		next ;
	}

	if( /^\@{/ )
	{
		$has_been_meta = 1;
		if( !$expecting_open_brace ) {
			die "Unepxected \@{";
		}
		$expecting_open_brace = 0;
		next;
	}
	
	if( $expecting_open_brace ) {
		die "Expected \@{ before content";
	}
	
	if( /^\@NAMESPACE / )
	{
		$has_been_meta = 1;
		/$cRegexNamespace/ or die "Syntax error on line $line when parsing \@NAMESPACE";
		push @namespace_stack, $1;
#		print "Namespace level is ".join('@', @namespace_stack)."\n";
		$expecting_open_brace = 1;
		next;
	}
	elsif( /^\@CLASS / )
	{
		$has_been_meta = 1;
		/$cRegexClass/ or die "Syntax error on line $line when parsing \@CLASS";		

		# TODO: Classes	
		if( $gCurClass ne "" ) {
			die "Defining a class within a class at line $line";
		}
		$gCurClass = make_path(\@namespace_stack, $1);
		$gCurClass_V = "TYPE_".$gCurClass;
		$gCurClass_V =~ s/@/_z_/g;
		my $classsym = "gExports_class_".$gCurClass;
		$classsym =~ s/@/_/g;
		print OUTFILE $indent,"extern tSpiderClass $classsym;\n";
		
		$gClassLastFunction = "NULL";
		$gClassConstructor = "NULL";
		$gClassDestructor = "NULL";
#		print $gCurClass, " - ", $gCurClass_V, "\n";
	
		$expecting_open_brace = 1;
		next;
	}
	elsif( /^\@CONSTRUCTOR/ )
	{
		$has_been_meta = 1;
		if( $gCurClass eq "" ) {
			die "Constructor not in class";
		}
		
		/$cRegexConstructor/
			or die "Syntax error on line $line when parsing \@CONSTRUCTOR\n";
		
		my $argspecs = $1;
		$argspecs =~ s/^\s+//;
		$argspecs =~ s/\s+$//;
		
		my $symbol = $gCurClass."\@__construct";
		$symbol =~ s/@/_/g;

		# Nuke any old parameters
		@gFcnArgs = ();
		%gFcnArgs_C = ();
		%gFcnArgs_V = ();
		%gFcnArgs_I = ();

		# Parse parameters
		my @args = split(/\s*,\s*/, $argspecs);
		my $argc = 0;
		foreach my $arg (@args) {
			my $argname;
			my $typesym;
			my $code;
			my $cast;
			($typesym, $argname) = split(/\s+/, $arg, 2);
			($code, $cast) = gettype($typesym);
			push @gFcnArgs, $argname;
			$gFcnArgs_C{$argname} = $cast;
			$gFcnArgs_V{$argname} = $code;
			$gFcnArgs_I{$argname} = $argc;
			$argc++;
		}

		$bInFunction = 1;
		
		$gFcnRetType_V = $gCurClass_V;
		$gFcnRetType_C = "tSpiderObject*";
		$gClassConstructor = "&gExports_fcn_$symbol";		

		function_header($symbol, "NULL", "__construct");

		$expecting_open_brace = 1;
		next ;
	}
	elsif( /^\@DESTRUCTOR/ )
	{
		$has_been_meta = 1;
		if( $gCurClass eq "" ) {
			die "Constructor not in class";
		}
		
		# No Arguments
		
		my $symbol = $gCurClass."\@__destruct";
		$symbol =~ s/@/_/g;

		$bInFunction = 1;

		$gFcnRetType_V = -1;
		$gFcnRetType_C = "void";

		$gClassDestructor = "&Exports_fcn_$symbol";	

		print OUTFILE $indent,"void Exports_fcn_$symbol(tSpiderObject *this)\n";
		print OUTFILE $indent,"{\n";
		
		$expecting_open_brace = 1;
		next ;
	}
	elsif( /^\@FUNCTION / )
	{
		$has_been_meta = 1;
		/$cRegexFunction/ or die "Syntax error on line $line when parsing \@FUNCTION\n";
		
		my $rettype = $1;
		my $fcnname = $2;
		my $argspecs = $3;

		$argspecs =~ s/^\s+//;
		$argspecs =~ s/\s+$//;

		$bInFunction = 1;

		# Create the path and symbol name of the function
		my $fcnpath;
		if($gCurClass ne "") {
			$fcnpath = $gCurClass."__".$fcnname;
		}
		else {
			$fcnpath = make_path(\@namespace_stack, $fcnname);
		}
		
		# Mangle the path into a symbol name
		my $symbol = $fcnpath;
		$symbol =~ s/@/_/g;
		my $def_symbol = "gExports_fcn_".$symbol;

		# If it's a class method, the name should be the actual name, not the path
		if($gCurClass ne "") {
			$fcnpath = $fcnname;
		}
		
#		print "Function '$symbol' returning $rettype with args ($argspecs)\n";

		# Parse parameters
		my @args = split(/\s*,\s*/, $argspecs);
		my $argc = 0;

		@gFcnArgs = ();
		%gFcnArgs_C = ();
		%gFcnArgs_V = ();
		%gFcnArgs_I = ();

		if( $gCurClass ne "" )
		{
			push @gFcnArgs, "this";
			$gFcnArgs_C{"this"} = "const tSpiderObject*";
			$gFcnArgs_V{"this"} = $gCurClass_V;
			$gFcnArgs_I{"this"} = $argc;
			$argc ++;
		}

		foreach my $arg (@args) {
			my $argname;
			my $typesym;
			my $code;
			my $cast;
			($typesym, $argname) = split(/\s+/, $arg, 2);
			($code, $cast) = gettype($typesym);
			push @gFcnArgs, $argname;
			$gFcnArgs_C{$argname} = $cast;
			$gFcnArgs_V{$argname} = $code;
			$gFcnArgs_I{$argname} = $argc;
			$argc++;
		}
	
		($gFcnRetType_V,$gFcnRetType_C) = gettype($rettype);

		if( $gCurClass eq "" ) {
			function_header($symbol, $gLastFunction, $fcnpath);
		} else {
			function_header($symbol, $gClassLastFunction, $fcnpath);
		}

		if( $gCurClass eq "" )
		{
			$gLastFunction = "&".$def_symbol;
		}
		else
		{
			$gClassLastFunction = "&".$def_symbol;
		}

		$expecting_open_brace = 1;
		next;
	}
	elsif( /^\@}/ )
	{
		$has_been_meta = 1;
		if( $bInFunction )
		{
			if( $gFcnRetType_V eq "0" )
			{
				print OUTFILE $indent,"\treturn $gFcnRetType_V;\n";
			}
			print OUTFILE $indent,"}\n";
			$bInFunction = 0;
		}
		elsif( $gCurClass ne "" )
		{
			my $classpath = $gCurClass;
			my $classsym = "gExports_class_".$gCurClass;
			$classsym =~ s/@/_/g;
			print OUTFILE $indent,"tSpiderClass $classsym = {";
			print OUTFILE ".Next=$gLastClass,";
			print OUTFILE ".Name=\"$gCurClass\",";
			print OUTFILE ".Constructor=$gClassConstructor,";
			print OUTFILE ".Destructor=$gClassDestructor,";
			print OUTFILE ".Methods=$gClassLastFunction,";
			print OUTFILE ".NAttributes=".scalar(%gClassProperties).",";
			print OUTFILE ".AttributeDefs={";
			foreach my $pname (keys %gClassProperties) {
				print OUTFILE "{\"$pname\",$gClassProperties{$pname},0,0},";
			}
			print OUTFILE "{NULL,0,0,0}}";
			print OUTFILE "};\n";
			$gLastClass = "&$classsym";
			$gCurClass = "";
			# Write out class definition
		}
		elsif( defined $namespace_stack[0] )
		{
			my $closed = pop @namespace_stack;
#			print "Closed namespace $closed\n";
		}
		else
		{
			die "Closing when nothing to close on line $line\n";
		}
		next;
	}	

	# Code?
	$_ = $orig_line;

	sub macro_TYPEOF
	{
		if( ! exists $gFcnArgs_V{$_[0]} ) {
			die "Argument $_[0] does not exist";
		}
		if( $gFcnArgs_V{$_[0]} eq "-1" ) {
			return "ArgTypes[$gFcnArgs_I{$_[0]}]";
		}
		else {
			return $gFcnArgs_V{$_[0]};
		}
	}
	
	sub macro_CAST
	{
		my $var = $_[0];
		my $dest = $_[1];
		if( ! exists $gFcnArgs_V{$var} ) {
			die "Argument $var does not exist";
		}
		if( $gFcnArgs_V{$var} eq "-1" )
		{
			if( $dest =~ /\*$/ ) {
				return "(($dest)$var)";
			}
			else {
				return "(*($dest*)$var)";
			}
		}
		else {
			die "Can't cast strictly typed argument $_[0]";
		}
	}
	
	sub macro_RETURN
	{
		if( $gFcnRetType_V eq "0" ) {
			if( $_[0] !~ /\s*/ ) {
				die "Can't return a value from a void function";
			}
			return "return 0;";
		}
		else {
			return "do{*($gFcnRetType_C*)RetData = ($_[0]);return $gFcnRetType_V;}while(0);"
		}
	}
	
	sub getvalue
	{
		my $arg = $_[0];
		if( ! exists $gFcnArgs_V{$arg} ) {
			die "Argument $arg does not exist";
		}
		
		if( $gFcnArgs_V{$arg} eq "-1" ) {
			die "Can't directly access loosely typed argument $arg";
		}
		else {
			return $arg;
		}
	}

	if( $has_been_meta )
	{
		print OUTFILE "#line $line \"$infile\"\n";
	}

	if( $bInFunction && $gCurClass ne "" )
	{
		my $classsym = "gExports_class_".$gCurClass;
		$classsym =~ s/@/_/g;
		s/\@CLASSPTR\b/&$classsym/g;
	}
	s/\@TYPECODE\(\s*([^\)]+)\s*\)/(gettype($1))[0]/ge;
	if( $bInFunction )
	{
		s/\@TYPEOF\(\s*($gIdentRegex)\s*\)/macro_TYPEOF($1)/ge;
		s/\@RETURN\b([^;]*);/macro_RETURN($1)/ge;
		s/\@BOOLEAN\(\s*($gIdentRegex)\s*\)/macro_CAST($1, "const tSpiderBool")/ge;
		s/\@INTEGER\(\s*($gIdentRegex)\s*\)/macro_CAST($1, "const tSpiderInteger")/ge;
		s/\@REAL\(\s*($gIdentRegex)\s*\)/macro_CAST($1, "const tSpiderReal")/ge;
		s/\@STRING\(\s*($gIdentRegex)\s*\)/macro_CAST($1, "const tSpiderString*")/ge;
		s/\@ARRAY\(\s*($gIdentRegex)\s*\)/macro_CAST($1, "const tSpiderArray*")/ge;
		s/\@($gIdentRegex)/getvalue($1)/ge;
	}
	
	print OUTFILE $_;
	
	$has_been_meta = 0;
}

