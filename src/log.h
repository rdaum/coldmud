/* log.h: Declarations for logging and panic routines. */

#ifndef LOG_H
#define LOG_H

void panic(char *s);
void fail_to_start(char *s);
void write_log(char *s, ...);

#endif

