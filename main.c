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
#include "m68k.h"


/*
 * constants
 */
#define TRAP_NUM    0
#define STACK_SIZE  8192


/*
 * type definitions
 */
typedef struct {
    APTR   tc_reg_pc;
    APTR   tc_reg_sp;
    USHORT tc_reg_sr;
    ULONG  tc_reg_d[8];
    ULONG  tc_reg_a[7];                  // without A7 = SP
} TaskContext;


/*
 * global variables
 */
BPTR                 g_logfh;            // for the LOG() macro
UBYTE                g_loglevel;
char                 g_logmsg[256];
TaskContext          g_target_ctx;       // task context of target


extern void trap_handler();
extern int run_target(int (*)(), APTR, ULONG);


void print_task_context(const TaskContext *ctx)
{
    UBYTE i;
    ULONG nbytes;
    UWORD *sp;
    char instr[128], dump[64], *dp;

    nbytes = m68k_disassemble(instr, (ULONG) ctx->tc_reg_pc, M68K_CPU_TYPE_68030);
    for (sp = ctx->tc_reg_pc, dp = dump; nbytes > 0 && dp < dump + 64; nbytes -= 2, ++sp, dp += 5)
        sprintf(dp, "%04x ", *sp);
    printf("PC=0x%08lx: %-20s: %s\n", (ULONG) ctx->tc_reg_pc, dump, instr);

    // TODO: pretty-print status register
    for (i = 0; i < 4; i++)
        printf("D%d=0x%08lx  ", i, ctx->tc_reg_d[i]);
    puts("");
    for (i = 4; i < 8; i++)
        printf("D%d=0x%08lx  ", i, ctx->tc_reg_d[i]);
    puts("");
    for (i = 0; i < 4; i++)
        printf("A%d=0x%08lx  ", i, ctx->tc_reg_a[i]);
    puts("");
    for (i = 4; i < 7; i++)
        printf("A%d=0x%08lx  ", i, ctx->tc_reg_a[i]);
    printf("A7(SP)=0x%08lx\n", (ULONG) ctx->tc_reg_sp);
}


void debug_main()
{
    LOG(INFO, "target has hit breakpoint");
    print_task_context(&g_target_ctx);
    Wait(SIGBREAKF_CTRL_D);
}


int main(int argc, char **argv)
{
    int                 status = RETURN_OK;         // exit status
    struct Task         *self = FindTask(NULL);     // pointer to this task
    BPTR                seglist;                    // segment list of target
    int                 (*entry)();                 // entry point of target
    APTR                stack;                      // stack for target

    // setup logging
//    if ((g_logfh = Open("CON:0/0/800/200/CWDebug Console", MODE_NEWFILE)) == 0)
//        return RETURN_ERROR;
    g_logfh = Output();
    g_loglevel = DEBUG;

    if (argc == 1) {
        LOG(ERROR, "no target specified - usage: cwdebug <target> [args]");
        status = RETURN_ERROR;
        goto ERROR_WRONG_USAGE;
    }

    // load target
    LOG(INFO, "initializing...");
    if ((seglist = LoadSeg(argv[1])) == NULL) {
        LOG(ERROR, "could not load target: %ld", IoErr());
        status = RETURN_ERROR;
        goto ERROR_LOAD_SEG_FAILED;
    }

    // allocate trap for breakpoints and install trap handler
    self->tc_TrapCode = trap_handler;
    if (AllocTrap(TRAP_NUM) == -1) {
        LOG(ERROR, "could not allocate trap");
        status = RETURN_ERROR;
        goto ERROR_NO_TRAP;
    }

    // allocate stack for target
    if ((stack = AllocVec(STACK_SIZE, 0)) == NULL) {
        LOG(ERROR, "could not allocate stack for target");
        status = RETURN_ERROR;
        goto ERROR_NO_STACK;
    }

    // initialize disassembler routines
    m68k_build_opcode_table();

    // start target, seglist points to (first) code segment, code starts one long word behind pointer
    entry = BCPL_TO_C_PTR(seglist + 1);
    LOG(INFO, "starting target at address 0x%08lx with stack pointer at 0x%08lx", (ULONG) entry, (ULONG) stack + STACK_SIZE);
    status = run_target(entry, stack, STACK_SIZE);
    LOG(INFO, "target terminated with exit code %d", status);

    FreeVec(stack);
ERROR_NO_STACK:
    FreeTrap(TRAP_NUM);
ERROR_NO_TRAP:
    UnLoadSeg(seglist);
ERROR_LOAD_SEG_FAILED:
ERROR_WRONG_USAGE:
//    Delay(250);
//    Close(g_logfh);
    return status;
}
