#
# SpiderScript
#

CPPFLAGS +=
CFLAGS   += -Wall -g
LDFLAGS  += -g -Wl,--no-undefined
# -soname libspiderscript.so --no-allow-shlib-undefined

OBJDIR = obj/

OBJ  = main.o lex.o parse.o ast.o exports.o values.o
OBJ += ast_to_bytecode.o bytecode_gen.o bytecode_makefile.o
OBJ += exec.o exec_bytecode.o exec_ast.o
BIN = ../libspiderscript.so

LD = $(CC)

CPPFLAGS+= -DBUILD -I .
CFLAGS	+= $(CPPFLAGS) -fPIC -Werror -O2
LDFLAGS += -shared

OBJ := $(addprefix $(OBJDIR),$(OBJ))

.PHONY:	all clean

all: $(BIN)

clean:
	$(RM) $(OBJ) $(BIN) $(OBJ:%=%.dep)

$(BIN): $(OBJ)
	$(LD) $(LDFLAGS) -o $(BIN) $(OBJ)

$(OBJ): $(OBJDIR)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
	@$(CC) $(CPPFLAGS) -M -MT $@ $< -o $@.dep

-include $(OBJ:%=%.dep)
