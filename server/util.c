/*
 * util.h - part of CWDebug, a source-level debugger for the AmigaOS
 *          contains some utility routines
 *
 * Copyright(C) 2018-2022 Constantin Wiemer
 */


#include <proto/exec.h>
#include <stdarg.h>
#include <stdio.h>

#include "stdint.h"
#include "util.h"


uint8_t g_loglevel;


static char *p_level_name[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "CRIT"
};


void logmsg(const char *p_fname, int lineno, const char *p_func, uint8_t level, const char *p_fmtstr, ...)
{
    va_list args;
    char location[32];

    Forbid();
    if (level < g_loglevel)
        return;

    snprintf(location, 32, "%s:%d", p_fname, lineno);
    printf("0x%08x | %-15s | %-25s | %-5s | ", (uint32_t) FindTask(NULL), location, p_func, p_level_name[level]);

    va_start(args, p_fmtstr);
    vprintf(p_fmtstr, args);
    va_end(args);
    printf("\n");
    Permit();
}


void dump_memory(const uint8_t *p_addr, uint32_t size)
{
    uint32_t pos = 0, i, nchars;
    char line[256], *p;

    while (pos < size) {
        printf("%04x: ", pos);
        for (i = pos, p = line, nchars = 0; (i < pos + 16) && (i < size); ++i, ++p, ++nchars) {
            printf("%02x ", p_addr[i]);
            if (p_addr[i] >= 0x20 && p_addr[i] <= 0x7e) {
                sprintf(p, "%c", p_addr[i]);
            }
            else {
                sprintf(p, ".");
            }
        }
        if (nchars < 16) {
            for (i = 1; i <= (3 * (16 - nchars)); ++i, ++p, ++nchars) {
                sprintf(p, " ");
            }
        }
        *p = '\0';

        printf("\t%s\n", line);
        pos += 16;
    }
}
