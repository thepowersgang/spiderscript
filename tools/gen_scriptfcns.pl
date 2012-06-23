#!/usr/bin/perl
use strict;
use warnings;

my $gIdentRegex = qr/[A-Za-z_]([A-Za-z_]+)/;

my $infile = $ARGV[0];
my $outfile = $ARGV[1];
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
		return ("-1", "void*");
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
		if( $code !~ $gIdentRegex ) {
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

# Open output and append header
open(OUTFILE, ">", $outfile) or die "Couldn't open ", $outfile, " for writing";
print OUTFILE "#define __SFCN_PROTO(n) int n(tSpiderScript*Script,void*RetData,int NArgs,const int*ArgTypes,const void*const Args[])\n";
print OUTFILE "#define __SFCN_DEF(i,p,r,n,a...)	tSpiderFunction gExports_##i = {.Next=p,.Name=n,.Handler=Export_fcn_##i,.ReturnType=r,.ArgTypes={a}}\n";

# Quick intial pass to turn classes into numbers
open(INFILE, "<", $infile) or die "Couldn't open ", $infile, " for reading";
my $classcount = 0;
my %gClassIndicies;
while(<INFILE>)
{
	if( /^\@NAMESPACE / )
	{
		/^\@NAMESPACE ([A-Za-z]+)$/ or die "Syntax error on line $line when parsing \@NAMESPACE";
		push @namespace_stack, $1;
		next;
	}
	elsif( /^\@CLASS / )
	{
		/^\@CLASS ($gIdentRegex)$/ or die "Syntax error on line $line when parsing \@CLASS";
		my $name = "TYPE_";
		foreach my $ns (@namespace_stack) {
			$name .= $ns."_z_";
		}
		$name .= $1;
		$gClassIndicies{$name} = $classcount;
		$classcount ++;
		next;
	}
}
@namespace_stack = ();
close INFILE;
foreach my $name (keys %gClassIndicies) {
	print OUTFILE "#define $name (0x1000|".($classcount-1-$gClassIndicies{$name}).")\n";
}

open(INFILE, "<", $infile) or die "Couldn't open ", $infile, " for reading";

my $has_been_meta = 1;
while(<INFILE>)
{
	$line ++;

	my $indent = "";	
	my $orig_line = $_;
	
	s#//.*$##;	# Kill comments
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

	
	if( /^\@NAMESPACE / )
	{
		$has_been_meta = 1;
		/^\@NAMESPACE ($gIdentRegex)$/ or die "Syntax error on line $line when parsing \@NAMESPACE";
		push @namespace_stack, $1;
#		print "Namespace level is ".join('@', @namespace_stack)."\n";
		next;
	}
	elsif( /^\@CLASS / )
	{
		$has_been_meta = 1;
		/^\@CLASS ($gIdentRegex)$/ or die "Syntax error on line $line when parsing \@CLASS";		

		# TODO: Classes	
		if( $gCurClass ne "" ) {
			die "Defining a class within a class at line $line";
		}
		$gCurClass = join('@', @namespace_stack)."@".$1;
		$gCurClass_V = "TYPE_".$gCurClass;
		$gCurClass_V =~ s/@/_z_/g;
		my $classsym = "gExports_class_".$gCurClass;
		$classsym =~ s/@/_/g;
		print OUTFILE $indent,"extern tSpiderClass $classsym;\n";
		
		$gClassLastFunction = "NULL";
		$gClassConstructor = "NULL";
		$gClassDestructor = "NULL";
#		print $gCurClass, " - ", $gCurClass_V, "\n";
	
		next;
	}
	elsif( /^\@CONSTRUCTOR/ )
	{
		$has_been_meta = 1;
		if( $gCurClass eq "" ) {
			die "Constructor not in class";
		}
		
		/^\@CONSTRUCTOR \s*\(([^\)]*)\)$/
			or die "Syntax error on line $line when parsing \@CONSTRUCTOR\n";
		
		my $argspecs = $1;
		$argspecs =~ s/^\s+//;
		$argspecs =~ s/\s+$//;
		
		my $symbol = $gCurClass."\@__construct";
		$symbol =~ s/@/_/g;

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
			$gFcnArgs_C{$argname} = $cast;
			$gFcnArgs_V{$argname} = $code;
			$gFcnArgs_I{$argname} = $argc;
			$argc++;
		}

		$bInFunction = 1;
		
		$gFcnRetType_V = $gCurClass_V;
		$gFcnRetType_C = "tSpiderObject*";
		$gClassConstructor = "&gExports_$symbol";		

		print OUTFILE $indent,"__SFCN_PROTO(Export_fcn_$symbol);\n";
		print OUTFILE $indent,"__SFCN_DEF($symbol, NULL, $gFcnRetType_V, \"__construct\"";
		foreach my $argcode (values %gFcnArgs_V) {
			print OUTFILE ", $argcode";
		}
		print OUTFILE ");\n";
		print OUTFILE $indent,"__SFCN_PROTO(Export_fcn_$symbol)\n";
		print OUTFILE $indent,"{\n";
	

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

		$gClassDestructor = "Export_fcn_$symbol";	

		print OUTFILE $indent,"void Export_fcn_$symbol(tSpiderObject *this)\n";
		print OUTFILE $indent,"{\n";
		
		next ;
	}
	elsif( /^\@FUNCTION / )
	{
		$has_been_meta = 1;
		/^\@FUNCTION ([^\s]+)\s+([A-Za-z_0-9]+)\s*\(([^\)]*)\)$/
			or die "Syntax error on line $line when parsing \@FUNCTION\n";
		
		my $rettype = $1;
		my $fcnname = $2;
		my $argspecs = $3;

		$argspecs =~ s/^\s+//;
		$argspecs =~ s/\s+$//;

		$bInFunction = 1;

		# Create the path and symbol name of the function
		my $fcnpath = "";
		foreach my $ns (@namespace_stack) {
			$fcnpath .= $ns."@";
		}
		if($gCurClass ne "") {
			$fcnpath .= $gCurClass."__";
		}
		$fcnpath .= $fcnname;
		
		# Mangle the path into a symbol name
		my $symbol = $fcnpath;
		$symbol =~ s/@/_/g;
		my $def_symbol = "gExports_".$symbol;

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
		
		print OUTFILE $indent,"__SFCN_PROTO(Export_fcn_$symbol);\n";
		print OUTFILE $indent,"__SFCN_DEF($symbol, ";
		if( $gCurClass eq "" ) {
			print OUTFILE $gLastFunction;
		} else {
			print OUTFILE $gClassLastFunction;
		}
		print OUTFILE ", $gFcnRetType_V, \"$fcnpath\"";
		foreach my $arg (@gFcnArgs) {
			print OUTFILE ", $gFcnArgs_V{$arg}";
		}
		print OUTFILE ");\n";
		print OUTFILE $indent,"__SFCN_PROTO(Export_fcn_$symbol)\n";
		print OUTFILE $indent,"{\n";
		
		# TODO: Do argument validation
		foreach my $arg (keys %gFcnArgs_C) {
			$gFcnArgs_V{$arg} eq "-1" and next;
			my $cast = $gFcnArgs_C{$arg};
			if( $cast =~ /\*$/ ) {
				print OUTFILE $indent,"\t$cast $arg = Args[$gFcnArgs_I{$arg}];\n";
			} else {
				print OUTFILE $indent,"\t$cast $arg = *(const $cast*)Args[$gFcnArgs_I{$arg}];\n";
			}
		}

		if( $gCurClass eq "" )
		{
			$gLastFunction = "&".$def_symbol;
		}
		else
		{
			$gClassLastFunction = "&".$def_symbol;
		}

		next;
	}
	elsif( /^\@{/ )
	{
		$has_been_meta = 1;
		# TODO: Ensure that namespaces/classes start with one of these
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
			die "Closing when nothing to close\n";
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
		if( ! exists $gFcnArgs_V{$_[0]} ) {
			die "Argument $_[0] does not exist";
		}
		if( $gFcnArgs_V{$_[0]} eq "-1" ) {
			return "(*($_[1]*)Args[$gFcnArgs_I{$_[0]}])";
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
#			if( $gFcnArgs_C{$arg} =~ /\*$/ ) {
#				return "(($gFcnArgs_C{$arg})Args[$gFcnArgs_I{$arg}])";
#			}
#			else {
#				return "(*(($gFcnArgs_C{$arg}*)Args[$gFcnArgs_I{$arg}]))";
#			}
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

print OUTFILE "tSpiderFunction *gpExports_First = $gLastFunction;\n";
print OUTFILE "tSpiderClass *gpExports_FirstClass = $gLastClass;\n";

