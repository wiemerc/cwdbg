/*
 * catch-exc.s - part of cwdbg, a debugger for AmigaOS
 *               This file contains the exception handler that can be linked with an executable to write a
 *               "core dump" and terminate the program in case of an exception. It was inspired by catch.o and the
 *               "tb" utility written by the Software Distillery and distributed with the SAS C compiler.
 *
 * Copyright(C) 2018-2022 Constantin Wiemer
 */


/*
 * definition of TaskContext structure (see debugger.h) */
.set tc_reg_sp,  0
.set tc_exc_num, 4
.set tc_reg_sr,  8
.set tc_reg_pc, 10
.set tc_reg_d,  14
.set tc_reg_a,  46


.text
.extern _dump_core
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
    ori.w       #0x0700, sr                     /* disable interrupts in supervisor mode */

    /* log exception via serial port */
#    movem.l     d0-d1/a0-a1, -(sp)              /* save registers that are modified by _kprintf */
#    move.l      16(sp), -(sp)                   /* exception number */
#    move.l      #msg, -(sp)                     /* format string */
#    jsr         _kprintf
#    add.l       #8, sp
#    movem.l     (sp)+, d0-d1/a0-a1

    move.l      a0, target_tc + tc_reg_a        /* save A0 first so we can use it as base / scratch register */
    move.l      usp, a0                         /* push user SP onto our stack */
    move.l      a0, -(sp)
    /* now pop all items from the stack and save them in the progams's task context */
    lea         target_tc, a0                   /* load base address of struct */
    move.l      (sp)+, (a0)+                    /* SP */
    move.l      (sp)+, (a0)+                    /* exception number */
    move.w      (sp)+, (a0)+                    /* SR */
    move.l      (sp)+, (a0)+                    /* PC (return address) */
    lea         target_tc + tc_reg_d + 32, a0   /* move pointer one longword beyond data registers for the pre-decrement mode to work */
    movem.l     d0-d7, -(a0)                    /* save data registers */
    lea         target_tc + tc_reg_a + 28, a0   /* move pointer one longword beyond address registers */
    movem.l     a1-a6, -(a0)                    /* save address registers without A0 because it has already been saved */
    /* push return address (our stub routine) and a "clean" SR onto stack and "return" to stub routine */
    pea         dump_core_stub
    move.w      #0x0000, -(sp)
    rte


dump_core_stub:
    /* call handle_exception() in dump-core.c with the task context as argument */
    pea         target_tc                       /* push task context address onto stack */
    jsr         _dump_core
    addq.l      #4, sp                          /* remove task context from stack */


.data
    .lcomm target_tc, 74                        /* target context, 74 == sizeof(TaskContext) */
msg:
    .asciz "Exception #%ld occurred\n"
