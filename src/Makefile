YACC = bison -y
YFLAGS = -d

CXX = g++
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

.cc.o:
	$(CXX) $(CFLAGS) -c $< -o $@

ultrix:
	$(MAKE) $(MFLAGS) $(EXE)

aix:
	$(MAKE) $(MFLAGS) LIBS=-lbsd $(EXE)

solaris:
	$(MAKE) $(MFLAGS) LIBS="-lsocket -lnsl -lelf" $(EXE)

linux:
	$(MAKE) $(MFLAGS) LIBS="-ldbm" $(EXE)

hpux:
	$(MAKE) $(MFLAGS) LIBS="-ldbm" $(EXE)

svr4:
	$(MAKE) $(MFLAGS) LIBS="-lsocket -lnsl -lelf -L/usr/ucblib -lucb" LOOKUPFLAGS="-I/usr/ucbinclude" $(EXE)

$(EXE): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $(EXE)

x.tab.h: y.tab.h
	-cmp -s x.tab.h y.tab.h || cp y.tab.h x.tab.h

clean:
	rm -f $(OBJS) [xy].tab.[ch] lex.yy.c grammar.c $(EXE)

grammar.o : grammar.y grammar.h object.h data.h cmstring.h regexp.h list.h \
  dict.h buffer.h ident.h token.h codegen.h memory.h util.h

lookup.o :
	$(CC) $(CFLAGS) $(LOOKUPFLAGS) -c lookup.cc

