YACC = bison -y
YFLAGS = -d

CC = gcc -Wall
CFLAGS = -O6 -fomit-frame-pointer
LDFLAGS = -O6 -fomit-frame-pointer
LIBS =

EXE = coldmud

OBJS =	grammar.o adminop.o arithop.o cache.o codegen.o data.o dataop.o \
	db.o dbpack.o decode.o dict.o dictop.o dump.o errorop.o execute.o \
	ident.o io.o ioop.o list.o listop.o loc.o log.o main.o match.o \
	memory.o methodop.o miscop.o net.o object.o objectop.o opcodes.o \
	sig.o string.o stringop.o syntaxop.o token.o util.o

all:
	@echo "Please read the file README."

ultrix:
	make $(EXE)

aix:
	make LIBS=-lbsd $(EXE)

solaris:
	make LIBS="-lsocket -lnsl -lelf" $(EXE)

linux:
	make LIBS="-ldbm" $(EXE)

hpux:
	make LIBS="-ldbm" $(EXE)

sysvr4:
	make LIBS="-lsocket -lnsl -lelf -L/usr/ucblib -lucb" LOCFLAGS="-I/usr/ucbinclude" $(EXE)

$(EXE): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $(EXE)

x.tab.h: y.tab.h
	-cmp -s x.tab.h y.tab.h || cp y.tab.h x.tab.h

clean:
	rm -f $(OBJS) [xy].tab.[ch] lex.yy.c $(EXE)

grammar.o : grammar.y grammar.h object.h data.h cmstring.h token.h codegen.h \
  memory.h util.h

loc.o : loc.c
	$(CC) $(CFLAGS) $(LOCFLAGS) -c loc.c

adminop.o : adminop.c x.tab.h operator.h execute.h data.h cmstring.h object.h \
  io.h dump.h log.h cache.h util.h config.h ident.h memory.h net.h
arithop.o : arithop.c x.tab.h operator.h execute.h data.h cmstring.h object.h \
  io.h ident.h util.h
cache.o : cache.c cache.h object.h data.h cmstring.h memory.h db.h loc.h \
  log.h util.h config.h ident.h
codegen.o : codegen.c x.tab.h codegen.h object.h data.h cmstring.h memory.h \
  code_prv.h opcodes.h grammar.h util.h config.h token.h ident.h
data.o : data.c x.tab.h data.h cmstring.h object.h ident.h util.h cache.h \
  memory.h token.h log.h
dataop.o : dataop.c x.tab.h operator.h execute.h data.h cmstring.h object.h \
  io.h ident.h cache.h util.h
db.o : db.c db.h object.h data.h cmstring.h loc.h cache.h log.h util.h \
  dbpack.h memory.h config.h
dbpack.o : dbpack.c x.tab.h dbpack.h object.h data.h cmstring.h memory.h \
  ident.h
decode.o : decode.c x.tab.h decode.h data.h cmstring.h object.h code_prv.h \
  codegen.h memory.h ident.h log.h util.h opcodes.h config.h token.h
dict.o : dict.c x.tab.h data.h cmstring.h memory.h ident.h
dictop.o : dictop.c x.tab.h operator.h execute.h data.h cmstring.h object.h \
  io.h ident.h memory.h
dump.o : dump.c x.tab.h dump.h cache.h object.h data.h cmstring.h log.h \
  config.h util.h execute.h io.h grammar.h db.h ident.h
errorop.o : errorop.c x.tab.h operator.h execute.h data.h cmstring.h object.h \
  io.h ident.h
execute.o : execute.c x.tab.h execute.h data.h cmstring.h object.h io.h \
  memory.h config.h ident.h cache.h util.h opcodes.h log.h decode.h
ident.o : ident.c ident.h memory.h util.h cmstring.h
io.o : io.c x.tab.h io.h cmstring.h net.h execute.h data.h object.h memory.h \
  grammar.h util.h ident.h
ioop.o : ioop.c x.tab.h operator.h execute.h data.h cmstring.h object.h io.h \
  memory.h config.h ident.h util.h
list.o : list.c data.h cmstring.h memory.h
listop.o : listop.c x.tab.h operator.h execute.h data.h cmstring.h object.h \
  io.h ident.h memory.h
log.o : log.c log.h dump.h cmstring.h util.h
main.o : main.c x.tab.h codegen.h object.h data.h cmstring.h memory.h \
  opcodes.h match.h cache.h sig.h db.h util.h io.h log.h dump.h execute.h \
  ident.h token.h
match.o : match.c x.tab.h match.h data.h cmstring.h memory.h util.h
memory.o : memory.c memory.h log.h
methodop.o : methodop.c x.tab.h operator.h execute.h data.h cmstring.h \
  object.h io.h ident.h
miscop.o : miscop.c x.tab.h operator.h execute.h data.h cmstring.h object.h \
  io.h util.h config.h ident.h
net.o : net.c net.h io.h cmstring.h log.h util.h ident.h
object.o : object.c x.tab.h object.h data.h cmstring.h memory.h opcodes.h \
  cache.h io.h ident.h decode.h util.h log.h
objectop.o : objectop.c x.tab.h operator.h execute.h data.h cmstring.h \
  object.h io.h ident.h grammar.h config.h cache.h dbpack.h
opcodes.o : opcodes.c x.tab.h opcodes.h operator.h util.h cmstring.h
sig.o : sig.c sig.h
string.o : string.c cmstring.h memory.h
stringop.o : stringop.c x.tab.h operator.h execute.h data.h cmstring.h \
  object.h io.h util.h match.h ident.h
syntaxop.o : syntaxop.c x.tab.h operator.h execute.h data.h cmstring.h \
  object.h io.h memory.h ident.h cache.h
token.o : token.c x.tab.h token.h data.h cmstring.h memory.h
util.o : util.c x.tab.h util.h cmstring.h data.h config.h ident.h token.h \
  log.h

