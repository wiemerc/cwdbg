/*
 * util.h - part of CWDebug, a source-level debugger for the AmigaOS
 *          contains some utility routines
 *
 * Copyright(C) 2018-2021 Constantin Wiemer
 */


#include <stdarg.h>
#include <stdio.h>

#include "util.h"


UBYTE                g_loglevel;


static char *p_level_name[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "CRIT"
};


void logmsg(const char *p_fname, int lineno, const char *p_func, UBYTE level, const char *p_fmtstr, ...)
{
    va_list args;
    char location[32];

    if (level < g_loglevel)
        return;

    snprintf(location, 32, "%s:%d", p_fname, lineno);
    printf("%-15s | %-25s | %-5s | ", location, p_func, p_level_name[level]);

    va_start(args, p_fmtstr);
    vprintf(p_fmtstr, args);
    va_end(args);
    printf("\n");
}
