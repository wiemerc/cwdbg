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
#define MODE_BREAKPOINT 0
#define MODE_RUN        1
#define MODE_STEP       2
#define MODE_EXCEPTION  3
#define MODE_CONTINUE   4
#define MODE_RESTORE    5
#define MODE_KILL       6


/*
 * type definitions
 */
typedef struct {
    APTR   tc_reg_sp;
    ULONG  tc_exc_num;
    USHORT tc_reg_sr;
    APTR   tc_reg_pc;
    ULONG  tc_reg_d[8];
    ULONG  tc_reg_a[7];                  // without A7 = SP
} TaskContext;

typedef struct {
    struct Node  bp_node;
    ULONG        bp_num;
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


extern int run_target(int (*)(), APTR, ULONG);
extern void exc_handler();
extern ULONG g_dummy;


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


static void print_memory(const UBYTE *addr, ULONG size)
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
    // regular local variables
    int                 status;                     // exit status
    BPTR                seglist;                    // segment list of target
    APTR                stack;                      // stack for target
    BreakPoint          *bpoint;                    // current breakpoint
    APTR                baddr;                      // address of current breakpoint

    // variables that need to survive accross calls
    static int          (*entry)();                 // entry point of target
    static struct List  bpoints;                    // list of breakpoints
    static BreakPoint   *prev_bpoint = NULL;        // previous breakpoint that needs to be restored
    static int          running = 0;                // target running?
    static int          stepping = 0;               // in single-step mode?


    // nested function to save passing state (has access to all local variables in debug_main())
    int command_loop()
    {
        char                cmd[64];                // command buffer
        char                *args[5];               // argument vector
        char                *token;                 // pointer to one token
        UBYTE               nargs;                  // number of arguments
        ULONG               boffset;                // offset from entry point
        APTR                maddr;                  // address of memory block to print
        ULONG               msize;                  // size of memory block

        while(1) {
            // read command from standard input (and ignore errors and commands >= 64 characters)
            Write(Output(), "> ", 2);
            WaitForChar(Input(), 0xffffffff);
            cmd[Read(Input(), cmd, 64)] = 0;

            // split command into tokens, up to 3
            token = strtok(cmd, " \t");
            nargs = 0;
            while ((nargs < 3) && (token != NULL)) {
                args[nargs] = token;
                token = strtok(NULL, " \t");
                ++nargs;
            }
            args[nargs] = NULL;

            // commands are very similar to the ones in GDB
            switch (args[0][0]) {
                case 'r':
                    if (running) {
                        LOG(ERROR, "target is already running");
                        break;
                    }
                    // TODO: reset breakpoint count for each run
                    LOG(INFO, "starting target at address 0x%08lx with stack pointer at 0x%08lx", (ULONG) entry, (ULONG) stack + STACK_SIZE);
                    running = 1;
                    status = run_target(entry, stack, STACK_SIZE);
                    running = 0;
                    LOG(INFO, "target terminated with exit code %d", status);
//                    LOG(DEBUG, "value of dummy: 0x%08lx", g_dummy);
                    break;

                case 'b':
                    if (nargs != 2) {
                        LOG(ERROR, "command 'b' requires an address");
                        break;
                    }
                    if (sscanf(args[1], "%lx", &boffset) == 0) {
                        LOG(ERROR, "invalid format for breakpoint offset");
                        break;
                    }
                    if ((bpoint = AllocVec(sizeof(BreakPoint), 0)) == NULL) {
                        LOG(ERROR, "could not allocate memory for breakpoint");
                        break;
                    }
                    baddr = (APTR) ((ULONG) entry) + boffset;
                    bpoint->bp_num          = ++bpoints.lh_Type;
                    bpoint->bp_addr         = baddr;
                    bpoint->bp_opcode       = *((USHORT *) baddr);
                    bpoint->bp_count        = 0;
                    AddTail(&bpoints, (struct Node *) bpoint);
                    *((USHORT *) baddr) = TRAP_OPCODE;
                    LOG(INFO, "breakpoint set at entry + 0x%08lx", boffset);
                    break;

                case 'k':
                    // kill target
                    stepping = 0;
                    return MODE_KILL;

                case 'q':
                    if (running) {
                        LOG(ERROR, "target is still running");
                        break;
                    }
                    LOG(INFO, "exiting...");
                    while ((bpoint = (BreakPoint *) RemHead(&bpoints)))
                        FreeVec(bpoint);
                    FreeVec(stack);
                    UnLoadSeg(seglist);
                    return RETURN_OK;

                case 'c':
                    // continue execution
                    if (!running) {
                        LOG(ERROR, "target is not yet running");
                        break;
                    }
                    // If we continue from a breakpoint, it has to be restored first, so we
                    // single-step the original instruction at the breakpoint and remember
                    // to restore the breakpoint afterwards (see code for MODE_STEP below).
                    // TODO: just continue in case of a deleted breakpoint
                    stepping = 0;
                    if (mode == MODE_BREAKPOINT) {
                        ((TaskContext *) data)->tc_reg_sr &= 0xbfff;    // clear T0
                        ((TaskContext *) data)->tc_reg_sr |= 0x8700;    // set T1 and interrupt mask
                    }
                    return MODE_CONTINUE;

                case 's':
                case '\n':
                    if (!running) {
                        LOG(ERROR, "target is not yet running");
                        break;
                    }
                    stepping = 1;
                    // In trace mode, *all* interrupts must be disabled (except for the NMI),
                    // otherwise OS code could be executed while the trace bit is still set,
                    // which would cause the OS exception handler (an alert) to be executed instead
                    // of ours => value 0x8700 is ORed with the SR.
                    ((TaskContext *) data)->tc_reg_sr &= 0xbfff;    // clear T0
                    ((TaskContext *) data)->tc_reg_sr |= 0x8700;    // set T1 and interrupt mask
                    return MODE_CONTINUE;

                case 'i':
                    if (!running) {
                        LOG(ERROR, "target is not yet running");
                        break;
                    }
                    if (nargs != 2) {
                        LOG(ERROR, "command 'i' requires a subcommand, either 'r' or 's'");
                        break;
                    }
                    switch (args[1][0]) {
                        case 'r':
                            print_registers(data);
                            break;
                        case 's':
                            print_stack(data);
                            break;
                        default:
                            LOG(ERROR, "unknown command 'i %c'", args[1][0]);
                    }
                    break;

                case 'p':
                    if (nargs != 3) {
                        LOG(ERROR, "command 'p' requires address and size");
                        break;
                    }
                    if ((sscanf(args[1], "%p", &maddr) == 0) || (sscanf(args[2], "%ld", &msize) == 0)) {
                        LOG(ERROR, "invalid format for address / size");
                        break;
                    }
                    print_memory(maddr, msize);
                    break;

                case 'd':
                    // TODO
                    break;

                default:
                    LOG(ERROR, "unknown command '%c'", args[0][0]);
                    break;
            }
        }
    }


    switch (mode) {
        case MODE_RUN:
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

        case MODE_BREAKPOINT:
            // target has hit breakpoint (called by exception handler)
            baddr = ((TaskContext *) data)->tc_reg_pc - 2;
            if ((bpoint = find_bpoint_by_addr(&bpoints, baddr)) != NULL) {
                prev_bpoint = bpoint;
                // rewind PC by 2 bytes and replace trap instruction with original instruction
                ((TaskContext *) data)->tc_reg_pc = baddr;
                *((USHORT *) baddr) = bpoint->bp_opcode;
                ++bpoint->bp_count;
                LOG(INFO, "target has hit breakpoint #%ld at entry + 0x%08lx, hit count = %ld", 
                    bpoint->bp_num, ((ULONG) baddr - (ULONG) entry), bpoint->bp_count);
            }
            else {
                LOG(CRIT, "INTERNAL ERROR: target has hit unknown breakpoint at entry + 0x%08lx", ((ULONG) baddr - (ULONG) entry));
                return MODE_KILL;
            }

            print_instr(data);
            return command_loop();

        case MODE_STEP:
            if (prev_bpoint) {
                // previous breakpoint needs to be restored
                LOG(DEBUG, "restoring breakpoint #%ld at entry + 0x%08lx", prev_bpoint->bp_num, ((ULONG) prev_bpoint->bp_addr - (ULONG) entry));
                *((USHORT *) prev_bpoint->bp_addr) = TRAP_OPCODE;
                prev_bpoint = NULL;
            }
            if (stepping) {
                // in single-step mode
                print_instr(data);
                return command_loop();
            }
            else
                return MODE_CONTINUE;

        case MODE_EXCEPTION:
            // unhandled exception occurred (called by exception handler)
            LOG(INFO, "unhandled exception #%ld occurred at entry + 0x%08lx",
                ((TaskContext *) data)->tc_exc_num,
                ((ULONG) ((TaskContext *) data)->tc_reg_pc - (ULONG) entry));
            print_instr(data);
            return command_loop();

        default:
            LOG(CRIT, "INTERNAL ERROR: unknown mode %d returned by exception handler", mode);
            return MODE_KILL;
    }
}


int main(int argc, char **argv)
{
    int                 status = RETURN_OK;         // exit status
    struct Task         *self = FindTask(NULL);     // pointer to this task
    APTR                old_exc_handler;            // previous execption handler

    // setup logging
    g_logfh = Output();
    g_loglevel = DEBUG;

    if (argc == 1) {
        LOG(ERROR, "no target specified - usage: cwdebug <target> [args]");
        status = RETURN_ERROR;
        goto ERROR_WRONG_USAGE;
    }

    LOG(INFO, "initializing...");

    // allocate trap and install exception handler
    old_exc_handler   = self->tc_TrapCode;
    self->tc_TrapCode = exc_handler;
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
    self->tc_TrapCode = old_exc_handler;
ERROR_WRONG_USAGE:
    return status;
}
