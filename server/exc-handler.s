/*
 * exc-handler.s - part of CWDebug, a source-level debugger for AmigaOS
 *                 This file contains the exception handler that does the "low level" work.
 *
 * Copyright(C) 2018-2022 Constantin Wiemer
 */


/*
 * constants
 */
.set TRAP_NUM_BP,           0
.set TRAP_NUM_RESTORE,      1
.set EXC_NUM_TRAP_BP,       0x00000020
.set EXC_NUM_TRAP_RESTORE,  0x00000021
.set EXC_NUM_TRACE,         0x00000009

/* keep in sync with debugger.h */
.set TS_STOPPED_BY_BREAKPOINT,   16
.set TS_STOPPED_BY_SINGLE_STEP,  64
.set TS_STOPPED_BY_EXCEPTION,    128

/* see TaskContext structure in debugger.h */
.set tc_reg_sp,  0
.set tc_exc_num, 4
.set tc_reg_sr,  8
.set tc_reg_pc, 10
.set tc_reg_d,  14
.set tc_reg_a,  46


.text
.extern _handle_stopped_target
.global _exc_handler


/*
 * general exception handler
 *
 * An exception stack frame looks like this:
 *
 * 15                             0
 * --------------------------------
 * | exception number (high word) |     +0
 * --------------------------------
 * | exception number (low word)  |     +2
 * --------------------------------
 * | status register              |     +4
 * --------------------------------
 * | return address (high word)   |     +6
 * --------------------------------
 * | return address (low word)    |     +8
 * --------------------------------
 */
_exc_handler:
    ori.w       #0x0700, sr                                 /* disable interrupts in supervisor mode */

                                                            /* log exception via serial port */
#    movem.l     d0-d1/a0-a1, -(sp)                         /* save registers that are modified by _kprintf */
#    move.l      16(sp), -(sp)                              /* exception number */
#    move.l      #msg, -(sp)                                /* format string */
#    jsr         _kprintf
#    add.l       #8, sp
#    movem.l     (sp)+, d0-d1/a0-a1

    /* default stop reason, changed later if necessary */
    move.l      #TS_STOPPED_BY_BREAKPOINT, stop_reason

    /* branch depending on the exception number */
    /* TODO: If we hit a breakpoint while single-stepping, we get a crash because we don't disable the trace mode before returning to user mode */
    cmp.l       #EXC_NUM_TRAP_BP, (sp)
    beq.s       exc_main
    cmp.l       #EXC_NUM_TRAP_RESTORE, (sp)
    beq.s       exc_restore
    cmp.l       #EXC_NUM_TRACE, (sp)
    beq.s       exc_trace
    bra.w       exc_exc                                     /* any other exception */


exc_restore:
    /* restore all registers and resume target */
    add.l       #10, sp                                     /* remove trap number, status register and return address from stack */
    lea         g_target_task_ctx, a0                       /* load base address of struct */
    move.l      tc_reg_pc(a0), -(sp)                        /* push saved target PC and status register onto stack */
    move.w      tc_reg_sr(a0), -(sp)
    add.l       #tc_reg_d, a0                               /* move pointer to D0 */
    movem.l     (a0)+, d0-d7                                /* restore data registers */
    movem.l     (a0)+, a0-a6                                /* restore address registers without A0 (is skipped automatically because it contains the base address) */
    lea         g_target_task_ctx + tc_reg_a, a0            /* finally restore A0 */
    move.l      (a0), a0
    rte


exc_main:
    move.l      a0, g_target_task_ctx + tc_reg_a            /* save A0 first so we can use it as base / scratch register */
    move.l      usp, a0                                     /* push user SP onto our stack */
    move.l      a0, -(sp)
    /* now pop all items from the stack and save them in the target's task context */
    lea         g_target_task_ctx, a0                       /* load base address of struct */
    move.l      (sp)+, (a0)+                                /* SP */
    move.l      (sp)+, (a0)+                                /* exception number */
    move.w      (sp)+, (a0)+                                /* SR */
    move.l      (sp)+, (a0)+                                /* PC (return address) */
    lea         g_target_task_ctx + tc_reg_d + 32, a0       /* move pointer one longword beyond data registers for the pre-decrement mode to work */
    movem.l     d0-d7, -(a0)                                /* save data registers */
    lea         g_target_task_ctx + tc_reg_a + 28, a0       /* move pointer one longword beyond address registers */
    movem.l     a1-a6, -(a0)                                /* save address registers without A0 because it has already been saved */
    /* push again return address (our stub routine) and a "clean" SR onto stack and "return" to stub routine */
    pea         debugger_stub
    move.w      #0x0000, -(sp)
    rte


exc_trace:
    /* trace exception */
    andi.w      #0x78ff, 4(sp)                              /* disable trace mode and re-enable interrupts in user mode */
    move.l      #TS_STOPPED_BY_SINGLE_STEP, stop_reason
    bra.s       exc_main                                    /* call debugger in the same way as with a breakpoint */


exc_exc:
    /* another exception => just call debugger */
    move.l      #TS_STOPPED_BY_EXCEPTION, stop_reason
    bra.s       exc_main


debugger_stub:
    /* call the entry point into the debugger */
    pea         g_target_task_ctx                           /* push target context address and stop reason onto stack */
    move.l      stop_reason, -(sp)
    jsr         _handle_stopped_target
    addq.l      #8, sp                                      /* remove args from stack */

    /* trap again to restore all registers, including the SR, which can only be done in supervisor mode, and resume target */
    trap        #TRAP_NUM_RESTORE


.data
    .lcomm stop_reason, 4                                   /* stop reason */
    .lcomm g_target_task_ctx, 74                            /* target context, 74 == sizeof(TaskContext) */
msg:
    .asciz "Exception #%ld occurred\n"
