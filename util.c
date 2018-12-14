/*
 * util.h - part of CWDebug, a source-level debugger for AmigaOS
 *
 * Copyright(C) 2018, 2019 Constantin Wiemer
 */


/*
 * included files
 */
#include "util.h"


/*
 * create hexdump of a memory region
 */
void dump_memory(const UBYTE *addr, ULONG size)
{
    ULONG pos = 0, i, nchars;
    char line[256], *p;

    while (pos < size) {
        printf("%04lx: ", pos);
        for (i = pos, p = line, nchars = 0; (i < pos + 16) && (i < size); ++i, ++p, ++nchars) {
            printf("%02x ", addr[i]);
            if (addr[i] >= 0x20 && addr[i] <= 0x7e) {
                sprintf(p, "%c", addr[i]);
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
