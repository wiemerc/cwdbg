/*
 * main.c - part of CWDebug, a source-level debugger for AmigaOS
 *
 * Copyright(C) 2018, 2019 Constantin Wiemer
 */


/*
 * included files
 */
#include <exec/types.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "util.h"
#include "m68k.h"


/*
 * constants
 */
#define TRAP_NUM        0
#define TRAP_OPCODE     0x4e40
#define STACK_SIZE      8192
#define MODE_RUN        0
#define MODE_TRAP       1
#define MODE_STEP       2
#define MODE_RESTORE    3


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

typedef struct {
    struct Node  bp_node;
    APTR         bp_addr;
    USHORT       bp_opcode;
    ULONG        bp_count;
} BreakPoint;


/*
 * global variables
 */
BPTR                 g_logfh;            // for the LOG() macro
UBYTE                g_loglevel;
char                 g_logmsg[256];


extern void trap_handler();
extern int run_target(int (*)(), APTR, ULONG);


static void print_instr(const TaskContext *ctx)
{
    ULONG nbytes;
    UWORD *sp;
    char instr[128], dump[64], *dp;

    nbytes = m68k_disassemble(instr, (ULONG) ctx->tc_reg_pc, M68K_CPU_TYPE_68030);
    for (sp = ctx->tc_reg_pc, dp = dump; nbytes > 0 && dp < dump + 64; nbytes -= 2, ++sp, dp += 5)
        sprintf(dp, "%04x ", *sp);
    printf("PC=0x%08lx: %-20s: %s\n", (ULONG) ctx->tc_reg_pc, dump, instr);
}


static void print_registers(const TaskContext *ctx)
{
    UBYTE i;

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


static void print_stack(const TaskContext *ctx)
{
    // TODO
}


static BreakPoint *find_bpoint_by_addr(struct List *bpoints, APTR baddr)
{
    BreakPoint *bpoint;

    if (IsListEmpty(bpoints))
        return NULL;
    for (bpoint = (BreakPoint *) bpoints->lh_Head;
         bpoint != (BreakPoint *) bpoints->lh_Tail;
         bpoint = (BreakPoint *) bpoint->bp_node.ln_Succ) {
        if (bpoint->bp_addr == baddr)
            return bpoint;
    }
    return NULL;
}


int debug_main(int mode, APTR data)
{
    int                 status = RETURN_OK;         // exit status
    static int          running = 0;
    BPTR                seglist;                    // segment list of target
    static int          (*entry)();                 // entry point of target
    APTR                stack;                      // stack for target
    static struct List  bpoints;                    // list of breakpoints
    BreakPoint          *bpoint;                    // current breakpoint
    APTR                baddr;                      // address of current breakpoint


    int command_loop()
    {
        UBYTE               cmd[64];                    // command buffer
        UBYTE               *args;                      // pointer to command arguments
        ULONG               boffset;                    // offset from entry point

        while(1) {
            Write(Output(), "> ", 2);
            WaitForChar(Input(), 0xffffffff);
            Read(Input(), cmd, 64);
            args = cmd + 1;
            switch (cmd[0]) {
                case 'r':
                    if (running) {
                        LOG(ERROR, "target is already running");
                        break;
                    }
                    // TODO: reset breakpoint count
                    LOG(INFO, "starting target at address 0x%08lx with stack pointer at 0x%08lx", (ULONG) entry, (ULONG) stack + STACK_SIZE);
                    running = 1;
                    status = run_target(entry, stack, STACK_SIZE);
                    running = 0;
                    LOG(INFO, "target terminated with exit code %d", status);
                    break;

                case 'b':
                    if (sscanf(args, "%lx", &boffset) == 0) {
                        LOG(ERROR, "invalid format for breakpoint offset");
                        break;
                    }
                    if ((bpoint = AllocVec(sizeof(BreakPoint), 0)) == NULL) {
                        LOG(ERROR, "could not allocate memory for breakpoint");
                        break;
                    }
                    baddr = (APTR) ((ULONG) entry) + boffset;
                    bpoint->bp_addr         = baddr;
                    bpoint->bp_opcode       = *((USHORT *) baddr);
                    bpoint->bp_count        = 0;
                    bpoint->bp_node.ln_Type = ++bpoints.lh_Type;
                    AddTail(&bpoints, (struct Node *) bpoint);
                    *((USHORT *) baddr) = TRAP_OPCODE;
                    LOG(INFO, "breakpoint set at entry + 0x%08lx", boffset);
                    break;

                case 'q':
                    if (running) {
                        // TODO: handle that case as well
                        LOG(ERROR, "target is still running");
                        break;
                    }
                    LOG(INFO, "exiting...");
                    // TODO: free breakpoints
                    FreeVec(stack);
                    UnLoadSeg(seglist);
                    return RETURN_OK;

                case 'c':
                    if (!running) {
                        LOG(ERROR, "target is not yet running");
                        break;
                    }
                    return 0;

                case 's':
                    if (!running) {
                        LOG(ERROR, "target is not yet running");
                        break;
                    }
                    // TODO
                    LOG(ERROR, "single-stepping not yet implemented");
                    break;

                case 'i':
                    if (!running) {
                        LOG(ERROR, "target is not yet running");
                        break;
                    }
                    // TODO: implement separate commands 'ir' and 'is'
                    print_registers(data);
                    print_stack(data);
                    break;

                default:
                    LOG(ERROR, "unknown command '%c'", cmd[0]);
                    break;
            }
        }
    }


    if (mode == MODE_RUN) {
        // target is not yet running (called by main())
        // load target
        if ((seglist = LoadSeg(data)) == NULL) {
            LOG(ERROR, "could not load target: %ld", IoErr());
            return RETURN_ERROR;
        }
        // seglist points to (first) code segment, code starts one long word behind pointer
        entry = BCPL_TO_C_PTR(seglist + 1);

        // allocate stack for target
        if ((stack = AllocVec(STACK_SIZE, 0)) == NULL) {
            LOG(ERROR, "could not allocate stack for target");
            UnLoadSeg(seglist);
            return RETURN_ERROR;
        }

        // initialize list of breakpoints, lh_Type is used as number of breakpoints
        NewList(&bpoints);
        bpoints.lh_Type = 0;

        return command_loop();
    }
    else if (mode == MODE_TRAP) {
        // target has hit breakpoint or an exception occurred (called by trap handler)
        baddr = ((TaskContext *) data)->tc_reg_pc - 2;
        if ((bpoint = find_bpoint_by_addr(&bpoints, baddr)) != NULL) {
            // rewind PC by 2 bytes and replace trap instruction with original instruction
            ((TaskContext *) data)->tc_reg_pc = baddr;
            *((USHORT *) baddr) = bpoint->bp_opcode;
            ++bpoint->bp_count;
            LOG(INFO, "target has hit breakpoint #%d at entry + 0x%08lx, hit count = %ld", 
                bpoint->bp_node.ln_Type, ((ULONG) baddr - (ULONG) entry), bpoint->bp_count);
        }
        else {
            LOG(INFO, "unhandled exception occurred at entry + 0x%08lx", ((ULONG) baddr - (ULONG) entry));
            // TODO: use different return code in this case to prevent the exception handler from restoring the "breakpoint"
        }

        print_instr(data);
        return command_loop();
    }
    else if (mode == MODE_STEP) {
        print_instr(data);
        return command_loop();
    }
    return 0;
}


int main(int argc, char **argv)
{
    int                 status = RETURN_OK;         // exit status
    struct Task         *self = FindTask(NULL);     // pointer to this task

    // setup logging
    g_logfh = Output();
    g_loglevel = DEBUG;

    if (argc == 1) {
        LOG(ERROR, "no target specified - usage: cwdebug <target> [args]");
        status = RETURN_ERROR;
        goto ERROR_WRONG_USAGE;
    }

    LOG(INFO, "initializing...");

    // allocate trap for breakpoints and install trap handler
    self->tc_TrapCode = trap_handler;
    if (AllocTrap(TRAP_NUM) == -1) {
        LOG(ERROR, "could not allocate trap");
        status = RETURN_ERROR;
        goto ERROR_NO_TRAP;
    }

    // initialize disassembler routines
    m68k_build_opcode_table();

    // hand over control to debug_main() which does all the work
    status = debug_main(MODE_RUN, argv[1]);

    FreeTrap(TRAP_NUM);
ERROR_NO_TRAP:
ERROR_WRONG_USAGE:
    return status;
}
