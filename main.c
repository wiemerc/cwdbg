/*
 * main.c - part of CWDebug, a source-level debugger for AmigaOS
 *
 * Copyright(C) 2018, 2019 Constantin Wiemer
 */


/*
 * included files
 */
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "util.h"


/*
 * constants
 */
#define TRAP_NUM    0
#define SIGNAL_NUM  31


/*
 * global variables
 */
BPTR                 g_logfh;                       /* for the LOG() macro */
UBYTE                g_loglevel;
char                 g_logmsg[256];
ULONG                g_ntraps   = 0;                /* trap counter */
ULONG                g_nsignals = 0;                /* signal counter */


extern void trap_handler();


ULONG sig_handler()
{
//    LOG(INFO, "signal handler has been called");
    ++g_nsignals;
    return 1 << SIGNAL_NUM;
}


int main(int argc, char **argv)
{
    int                 status = RETURN_OK;         /* exit status */
    BPTR                seglist;                    /* segment list of loaded program */
    struct Task         *self = FindTask(NULL);     /* pointer to this task */
    int (*entry)();                                 /* entry point of target */

    /* setup logging */
//    if ((g_logfh = Open("CON:0/0/800/200/CWDebug Console", MODE_NEWFILE)) == 0)
//        return RETURN_ERROR;
    g_logfh = Output();
    g_loglevel = DEBUG;

    if (argc == 1) {
        LOG(ERROR, "no target specified - usage: cwdebug <target> [args]");
        status = RETURN_ERROR;
        goto ERROR_WRONG_USAGE;
    }

    LOG(INFO, "debugger is starting target '%s'", argv[1]);
    if ((seglist = LoadSeg(argv[1])) == NULL) {
        LOG(ERROR, "could not load target: %ld", IoErr());
        status = RETURN_ERROR;
        goto ERROR_LOAD_SEG_FAILED;
    }

    self->tc_TrapCode = trap_handler;
    if (AllocTrap(TRAP_NUM) == -1) {
        LOG(ERROR, "could not allocate trap");
        status = RETURN_ERROR;
        goto ERROR_NO_TRAP;
    }

    self->tc_ExceptCode = sig_handler;
    if (AllocSignal(SIGNAL_NUM) == -1) {
        LOG(ERROR, "could not allocate signal");
        status = RETURN_ERROR;
        goto ERROR_NO_SIGNAL;
    }
    SetSignal(0, 1 << SIGNAL_NUM);
    SetExcept(1 << SIGNAL_NUM, 1 << SIGNAL_NUM);

    /* seglist points to (first) code segment, code starts one long word behind pointer */
    entry = BCPL_TO_C_PTR(seglist + 1);
    status = entry();
    LOG(INFO, "target terminated with exit code %ld", status);
    LOG(INFO, "trap handler was called %ld times", g_ntraps);
    LOG(INFO, "signal handler was called %ld times", g_nsignals);

    FreeSignal(SIGNAL_NUM);
ERROR_NO_SIGNAL:
    FreeTrap(TRAP_NUM);
ERROR_NO_TRAP:
    UnLoadSeg(seglist);
ERROR_LOAD_SEG_FAILED:
ERROR_WRONG_USAGE:
//    Delay(250);
//    Close(g_logfh);
    return RETURN_OK;
}
