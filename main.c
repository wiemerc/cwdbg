/*
 * main.c - part of CWDebug, a source-level debugger for AmigaOS
 *
 * Copyright(C) 2018, 2019 Constantin Wiemer
 */


/*
 * included files
 */
#include "util.h"


/*
 * global variables
 */
BPTR                 g_logfh;                               /* for the LOG() macro */
UBYTE                g_loglevel;
char                 g_logmsg[256];


int main(int argc, char **argv)
{
    /* setup logging */
    if ((g_logfh = Open("CON:0/0/800/200/CWDebug Console", MODE_NEWFILE)) == 0)
        return 1;
    g_loglevel = DEBUG;

    LOG(INFO, "debugger is starting target '%s'", argv[1]);
    /*
     * LoadSeg()
     * CreateProc()
     */
    Delay(500);
    Close(g_logfh);
    return 0;
}
