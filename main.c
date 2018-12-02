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


/*
 * type definitions
 */
typedef struct {
    APTR   tc_reg_pc;
    APTR   tc_reg_sp;
    USHORT tc_reg_sr;
    ULONG  tc_reg_d[8];
    ULONG  tc_reg_a[7];                             /* without A7 = SP */
} TaskContext;


/*
 * global variables
 */
BPTR                 g_logfh;                       /* for the LOG() macro */
UBYTE                g_loglevel;
char                 g_logmsg[256];
ULONG                g_ntraps = 0;                  /* trap counter */
TaskContext          g_target_ctx;                  /* task context of target */


extern void trap_handler();


void print_task_context(const TaskContext *ctx)
{
    int i;

    /* TODO: print disassembled instruction */
    printf("PC=0x%08lx, instruction = 0x%04lx\n", ctx->tc_reg_pc, *((USHORT *) ctx->tc_reg_pc));
    /* TODO: pretty-print status register */
    for (i = 0; i < 4; i++)
        printf("D%ld=0x%08lx  ", i, ctx->tc_reg_d[i]);
    puts("");
    for (i = 4; i < 8; i++)
        printf("D%ld=0x%08lx  ", i, ctx->tc_reg_d[i]);
    puts("");
    for (i = 0; i < 4; i++)
        printf("A%ld=0x%08lx  ", i, ctx->tc_reg_a[i]);
    puts("");
    for (i = 4; i < 7; i++)
        printf("A%ld=0x%08lx  ", i, ctx->tc_reg_a[i]);
    printf("A7(SP)=0x%08lx\n", ctx->tc_reg_sp);
}


void debug_main()
{
    LOG(INFO, "target has hit breakpoint");
    print_task_context(&g_target_ctx);
    Wait(SIGBREAKF_CTRL_D);
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

    /* seglist points to (first) code segment, code starts one long word behind pointer */
    /* TODO: save / restore all registers */
    /* TODO: use separate stack for target */
    entry = BCPL_TO_C_PTR(seglist + 1);
    LOG(INFO, "starting target at address 0x%08lx", entry);
    status = entry();
    LOG(INFO, "target terminated with exit code %ld", status);
    LOG(INFO, "trap handler was called %ld times", g_ntraps);

    FreeTrap(TRAP_NUM);
ERROR_NO_TRAP:
    UnLoadSeg(seglist);
ERROR_LOAD_SEG_FAILED:
ERROR_WRONG_USAGE:
//    Delay(250);
//    Close(g_logfh);
    return RETURN_OK;
}
