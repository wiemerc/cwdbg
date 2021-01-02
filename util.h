#ifndef CWDEBUG_UTIL_H
#define CWDEBUG_UTIL_H
/*
 * util.h - part of CWDebug, a source-level debugger for the AmigaOS
 *
 * Copyright(C) 2018-2021 Constantin Wiemer
 */


/*
 * included files
 */
#include <dos/dos.h>
#include <exec/types.h>


/*
 * external references
 */
extern UBYTE g_loglevel;


/*
 * constants / macros
 */
#define DEBUG 0
#define INFO  1
#define WARN  2 
#define ERROR 3
#define CRIT  4


/*
 * prototypes
 */
void logmsg(const char *p_fname, int lineno, const char *p_func, UBYTE level, const char *p_fmtstr, ...);


/*
 * macros
 */
#define LOG(level, p_fmtstr, ...) {logmsg(__FILE__, __LINE__, __func__, level, p_fmtstr, ##__VA_ARGS__);}
#define C_TO_BCPL_PTR(ptr) ((BPTR) (((ULONG) (ptr)) >> 2))
#define BCPL_TO_C_PTR(ptr) ((APTR) (((ULONG) (ptr)) << 2))

#endif /* CWDEBUG_UTIL_H */
