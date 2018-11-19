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
extern BPTR  g_logfh;
extern UBYTE g_level;
extern char  g_logmsg[256];


/*
 * constants / macros
 */
#define DEBUG 10
#define INFO  20
#define WARN  30
#define ERROR 40
#define CRIT  50
#define LOG(level, fmt, ...)                                 \
{                                                            \
    if (level >= g_loglevel) {                               \
        switch (level) {                                     \
            case DEBUG: Write(g_logfh, "DEBUG: ", 7); break; \
            case INFO:  Write(g_logfh, "INFO: ", 6);  break; \
            case WARN:  Write(g_logfh, "WARN: ", 6);  break; \
            case ERROR: Write(g_logfh, "ERROR: ", 7); break; \
            case CRIT:  Write(g_logfh, "CRIT: ", 5);  break; \
        }                                                    \
        sprintf(g_logmsg, fmt, ##__VA_ARGS__);               \
        Write(g_logfh, g_logmsg, strlen(g_logmsg));          \
        Write(g_logfh, "\n", 1);                             \
    }                                                        \
}
#define C_TO_BCPL_PTR(ptr) ((BPTR) (((ULONG) (ptr)) >> 2))
#define BCPL_TO_C_PTR(ptr) ((APTR) (((ULONG) (ptr)) << 2))

#endif /* CWDEBUG_UTIL_H */
