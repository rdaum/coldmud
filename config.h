/* config.h: Compile-time values administrator may want to change. */

#ifndef CONFIG_H
#define CONFIG_H

/* Uses vfork() instead of fork() in adminop.c. */
/* #define BSD_FEATURES */

/* The version number. */
#define VERSION_MAJOR	0
#define VERSION_MINOR	9
#define VERSION_BUGFIX	0

/* Number of ticks a method gets before dying with an E_TICKS. */
#define METHOD_TICKS		20000

/* Maximum depth of method calls. */
#define MAX_CALL_DEPTH		128

/* Width and depth of object cache. */
#define CACHE_WIDTH	7
#define CACHE_DEPTH	23

/* Default indent for decompiled code. */
#define DEFAULT_INDENT	4

/* Maximum number of characters of a data value to display using format(). */
#define MAX_DATA_DISPLAY 15

#endif

