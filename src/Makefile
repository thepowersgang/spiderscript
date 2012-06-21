#
# SpiderScript
#

CPPFLAGS +=
CFLAGS   += -Wall -g
LDFLAGS  += -g -Wl,--no-undefined
# -soname libspiderscript.so --no-allow-shlib-undefined

OBJDIR = obj/

OBJ  = main.o lex.o parse.o ast.o values.o
OBJ += exports.gen.o
OBJ += ast_to_bytecode.o bytecode_gen.o bytecode_makefile.o
OBJ += exec.o exec_bytecode.o exec_ast.o types.o
BIN = ../libspiderscript.so

LD = $(CC)

CPPFLAGS+= -DBUILD -I .
CFLAGS	+= $(CPPFLAGS) -fPIC -Werror -O2 -std=gnu99
LDFLAGS += -shared

OBJ := $(addprefix $(OBJDIR),$(OBJ))

.PHONY:	all clean

all: $(BIN)

clean:
	$(RM) $(OBJ) $(BIN) $(OBJ:%=%.dep) $(OBJ:obj/%.gen.o=%.gen.c)

$(BIN): $(OBJ)
	$(LD) $(LDFLAGS) -o $(BIN) $(OBJ)

%.gen.c: %.ssf ../tools/gen_scriptfcns.pl
	perl ../tools/gen_scriptfcns.pl $< $@

$(OBJ): $(OBJDIR)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
	@$(CC) $(CPPFLAGS) -M -MT $@ $< -o $@.dep

-include $(OBJ:%=%.dep)
