/* cmstring.c: Declarations for string handling. */

#ifndef STRING_H
#define STRING_H

class String;

#include <stdio.h>
#include "regexp.h"

struct String_rep {
    int start;		/* Offset from s where text of string starts. */
    int len;		/* Length of string. */
    int size;		/* Maximum length of string without resizing. */
    int refs;		/* Number of strings referencing this rep. */
    regexp* reg;	/* Cached regexp, if any. */
    char s[1];		/* Text of string. */
};

extern String_rep empty_rep;

class String {

  private:
    String_rep *rep;

  public:
    /* Constructors and destructor. */
    String() { rep = &empty_rep; rep->refs++; }
    String(int size);
    String(char* s);
    String(char* s, int len);
    String(const String& str) { rep = str.rep; rep->refs++; }
    ~String() { if (!--rep->refs) rep_free(rep); }

    /* Observers. */
    char* chars() { return rep->s + rep->start; }
    int length() { return rep->len; }
    regexp* regexp();

    /* Mutators. */
    void append(char* s) { append(s, strlen(s)); }
    void append(char* s, int len);
    void append(char c);
    void append(char c, int len);
    void append(String str);

    void substring(int start, int len = -1);

    void uppercase();
    void lowercase();

    /* Operators. */
    operator +=(char* s) { append(s); }
    operator +=(char c) { append(c); }
    operator +=(const String &str) { append(str); }
};

void rep_free(String_rep* rep);
char *regerror(char *msg);

#endif

