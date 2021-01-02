#ifndef CWDEBUG_UTIL_H
#define CWDEBUG_UTIL_H
/*
 * util.h - part of CWDebug, a source-level debugger for AmigaOS
 *
 * Copyright(C) 2018, 2019 Constantin Wiemer
 */


/*
 * included files
 */
#include <exec/types.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>


/*
 * external references
 */
extern UBYTE g_loglevel;
extern char  g_logmsg[256];


/*
 * constants / macros
 */
#define DEBUG 10
#define INFO  20
#define WARN  30
#define ERROR 40
#define CRIT  50
// TODO: rewrite as function with vsprintf()
#define LOG(level, fmt, ...)                                 \
{                                                            \
    if (level >= g_loglevel) {                               \
        switch (level) {                                     \
            case DEBUG: Write(Output(), "DEBUG: ", 7); break; \
            case INFO:  Write(Output(), "INFO: ", 6);  break; \
            case WARN:  Write(Output(), "WARN: ", 6);  break; \
            case ERROR: Write(Output(), "ERROR: ", 7); break; \
            case CRIT:  Write(Output(), "CRIT: ", 5);  break; \
        }                                                    \
        sprintf(g_logmsg, fmt, ##__VA_ARGS__);               \
        Write(Output(), g_logmsg, strlen(g_logmsg));          \
        Write(Output(), "\n", 1);                             \
    }                                                        \
}
#define C_TO_BCPL_PTR(ptr) ((BPTR) (((ULONG) (ptr)) >> 2))
#define BCPL_TO_C_PTR(ptr) ((APTR) (((ULONG) (ptr)) << 2))


/*
 * prototypes
 */

#endif /* CWDEBUG_UTIL_H */
