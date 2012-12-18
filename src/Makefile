#
# SpiderScript
#

CPPFLAGS +=
CFLAGS   += -Wall -g
LDFLAGS  += -g -Wl,--no-undefined
# -soname libspiderscript.so --no-allow-shlib-undefined

OBJDIR = obj/

OBJ  = main.o lex.o parse.o ast.o values.o
OBJ += ast_to_bytecode.o bytecode_gen.o bytecode_makefile.o
OBJ += exec.o exec_bytecode.o exec_ast.o types.o ast_optimise.o
OBJ += exceptions.o
EXPORT_FILES := exports.ssf
BIN = ../libspiderscript.so

LD = $(CC)

CPPFLAGS+= -DBUILD -I .
CFLAGS	+= $(CPPFLAGS) -fPIC -Werror -O2 -std=gnu99 -fvisibility=hidden
LDFLAGS += -shared

OBJ += $(patsubst %.c,%.o,.exports/_index.c $(EXPORT_FILES:%.ssf=.exports/%.c))
OBJ := $(addprefix $(OBJDIR),$(OBJ))

.PHONY:	all clean

all: $(BIN)

clean:
	$(RM) $(OBJ) $(BIN) $(OBJ:%=%.dep) $(OBJ:obj/%.gen.o=%.gen.c)
	$(RM) -r obj .exports/

$(BIN): $(OBJ)
	$(LD) $(LDFLAGS) -o $(BIN) $(OBJ)

export_types.gen.h: $(EXPORT_FILES) ../tools/gen_scriptfcns.pl
	@mkdir -p $(dir $@)
	perl ../tools/gen_scriptfcns.pl --mkhdr-lang $@ $(EXPORT_FILES)

.exports/_index.c: $(EXPORT_FILES) ../tools/gen_scriptfcns.pl
	@mkdir -p $(dir $@)
	perl ../tools/gen_scriptfcns.pl -H export_types.gen.h --mkidx $@ $(EXPORT_FILES)

.exports/%.c: %.ssf export_types.gen.h ../tools/gen_scriptfcns.pl
	@mkdir -p $(dir $@)
	perl ../tools/gen_scriptfcns.pl -H export_types.gen.h $@ $<

$(OBJ): $(OBJDIR)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
	@$(CC) $(CPPFLAGS) -M -MT $@ $< -o $@.dep

-include $(OBJ:%=%.dep)
