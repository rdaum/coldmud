YACC = bison -y
YFLAGS = -d

CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -g
LIBS =

EXE = coldmud

OBJS =	grammar.o adminop.o arithop.o buffer.o bufferop.o cache.o codegen.o \
	data.o dataop.o db.o dbpack.o decode.o dict.o dictop.o dump.o \
	errorop.o execute.o ident.o io.o ioop.o list.o listop.o lookup.o \
	log.o main.o match.o memory.o methodop.o miscop.o net.o object.o \
	objectop.o opcodes.o regexp.o sig.o string.o stringop.o syntaxop.o \
	token.o util.o

all:
	@echo "Please read the file README."

ultrix:
	$(MAKE) $(EXE)

aix:
	$(MAKE) LIBS=-lbsd $(EXE)

solaris:
	$(MAKE) LIBS="-lsocket -lnsl -lelf" $(EXE)

linux:
	$(MAKE) LIBS="-ldbm" $(EXE)

hpux:
	$(MAKE) LIBS="-ldbm" $(EXE)

svr4:
	$(MAKE) LIBS="-lsocket -lnsl -lelf -L/usr/ucblib -lucb" LOOKUPFLAGS="-I/usr/ucbinclude" $(EXE)

$(EXE): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $(EXE)

x.tab.h: y.tab.h
	-cmp -s x.tab.h y.tab.h || cp y.tab.h x.tab.h

clean:
	rm -f $(OBJS) [xy].tab.[ch] lex.yy.c grammar.c $(EXE)

grammar.o : grammar.c grammar.h object.h data.h cmstring.h regexp.h list.h \
  dict.h buffer.h ident.h token.h codegen.h memory.h util.h

lookup.o :
	$(CC) $(CFLAGS) $(LOCFLAGS) -c lookup.c

adminop.o : adminop.c x.tab.h operator.h execute.h data.h cmstring.h regexp.h \
  list.h dict.h buffer.h ident.h object.h io.h dump.h log.h cache.h util.h \
  config.h memory.h net.h lookup.h
arithop.o : arithop.c x.tab.h operator.h execute.h data.h cmstring.h regexp.h \
  list.h dict.h buffer.h ident.h object.h io.h util.h
buffer.o : buffer.c x.tab.h buffer.h list.h data.h cmstring.h regexp.h dict.h \
  ident.h object.h memory.h
bufferop.o : bufferop.c x.tab.h execute.h data.h cmstring.h regexp.h list.h \
  dict.h buffer.h ident.h object.h io.h
cache.o : cache.c cache.h object.h data.h cmstring.h regexp.h list.h dict.h \
  buffer.h ident.h memory.h db.h lookup.h log.h util.h config.h
codegen.o : codegen.c x.tab.h codegen.h object.h data.h cmstring.h regexp.h \
  list.h dict.h buffer.h ident.h memory.h code_prv.h opcodes.h grammar.h \
  util.h config.h token.h
data.o : data.c x.tab.h data.h cmstring.h regexp.h list.h dict.h buffer.h \
  ident.h object.h util.h cache.h memory.h token.h log.h lookup.h
dataop.o : dataop.c x.tab.h operator.h execute.h data.h cmstring.h regexp.h \
  list.h dict.h buffer.h ident.h object.h io.h cache.h util.h
db.o : db.c db.h object.h data.h cmstring.h regexp.h list.h dict.h buffer.h \
  ident.h lookup.h cache.h log.h util.h dbpack.h memory.h config.h
dbpack.o : dbpack.c x.tab.h dbpack.h object.h data.h cmstring.h regexp.h \
  list.h dict.h buffer.h ident.h memory.h
decode.o : decode.c x.tab.h decode.h data.h cmstring.h regexp.h list.h dict.h \
  buffer.h ident.h object.h code_prv.h codegen.h memory.h log.h util.h \
  opcodes.h config.h token.h
dict.o : dict.c x.tab.h dict.h data.h cmstring.h regexp.h list.h buffer.h \
  ident.h object.h memory.h
dictop.o : dictop.c x.tab.h operator.h execute.h data.h cmstring.h regexp.h \
  list.h dict.h buffer.h ident.h object.h io.h memory.h
dump.o : dump.c x.tab.h dump.h cache.h object.h data.h cmstring.h regexp.h \
  list.h dict.h buffer.h ident.h log.h config.h util.h execute.h io.h \
  grammar.h db.h lookup.h
errorop.o : errorop.c x.tab.h operator.h execute.h data.h cmstring.h regexp.h \
  list.h dict.h buffer.h ident.h object.h io.h
execute.o : execute.c x.tab.h execute.h data.h cmstring.h regexp.h list.h \
  dict.h buffer.h ident.h object.h io.h memory.h config.h cache.h util.h \
  opcodes.h log.h decode.h
ident.o : ident.c ident.h memory.h util.h cmstring.h regexp.h
io.o : io.c x.tab.h io.h cmstring.h regexp.h data.h list.h dict.h buffer.h \
  ident.h object.h net.h execute.h memory.h grammar.h util.h
ioop.o : ioop.c x.tab.h operator.h execute.h data.h cmstring.h regexp.h \
  list.h dict.h buffer.h ident.h object.h io.h memory.h config.h util.h
list.o : list.c x.tab.h list.h data.h cmstring.h regexp.h dict.h buffer.h \
  ident.h object.h memory.h
listop.o : listop.c x.tab.h operator.h execute.h data.h cmstring.h regexp.h \
  list.h dict.h buffer.h ident.h object.h io.h memory.h
log.o : log.c log.h dump.h cmstring.h regexp.h util.h
lookup.o : lookup.c lookup.h ident.h log.h util.h cmstring.h regexp.h
main.o : main.c x.tab.h codegen.h object.h data.h cmstring.h regexp.h list.h \
  dict.h buffer.h ident.h memory.h opcodes.h match.h cache.h sig.h db.h \
  util.h io.h log.h dump.h execute.h token.h config.h
match.o : match.c x.tab.h match.h data.h cmstring.h regexp.h list.h dict.h \
  buffer.h ident.h object.h memory.h util.h
memory.o : memory.c memory.h log.h
methodop.o : methodop.c x.tab.h operator.h execute.h data.h cmstring.h \
  regexp.h list.h dict.h buffer.h ident.h object.h io.h
miscop.o : miscop.c x.tab.h operator.h execute.h data.h cmstring.h regexp.h \
  list.h dict.h buffer.h ident.h object.h io.h util.h config.h lookup.h
net.o : net.c net.h io.h cmstring.h regexp.h data.h list.h dict.h buffer.h \
  ident.h object.h log.h util.h
object.o : object.c x.tab.h object.h data.h cmstring.h regexp.h list.h dict.h \
  buffer.h ident.h memory.h opcodes.h cache.h io.h decode.h util.h log.h
objectop.o : objectop.c x.tab.h operator.h execute.h data.h cmstring.h \
  regexp.h list.h dict.h buffer.h ident.h object.h io.h grammar.h config.h \
  cache.h dbpack.h
opcodes.o : opcodes.c x.tab.h opcodes.h ident.h operator.h util.h cmstring.h \
  regexp.h
regexp.o : regexp.c regexp.h regmagic.h util.h cmstring.h
sig.o : sig.c sig.h
string.o : string.c cmstring.h regexp.h memory.h dbpack.h object.h data.h \
  list.h dict.h buffer.h ident.h util.h
stringop.o : stringop.c x.tab.h operator.h execute.h data.h cmstring.h \
  regexp.h list.h dict.h buffer.h ident.h object.h io.h match.h util.h
syntaxop.o : syntaxop.c x.tab.h operator.h execute.h data.h cmstring.h \
  regexp.h list.h dict.h buffer.h ident.h object.h io.h memory.h cache.h \
  lookup.h
token.o : token.c x.tab.h token.h data.h cmstring.h regexp.h list.h dict.h \
  buffer.h ident.h object.h memory.h
util.o : util.c x.tab.h util.h cmstring.h regexp.h data.h list.h dict.h \
  buffer.h ident.h object.h config.h token.h log.h

