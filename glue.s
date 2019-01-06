/*
 * glue.s - part of CWDebug, a source-level debugger for AmigaOS
 *          This file contain two assembly routines that "glue" the debugger to the target.
 *
 * Copyright(C) 2018, 2019 Constantin Wiemer
 */


/*
 * constants
 */
.set TRAP_NUM,          0
.set TRAP_OPCODE,       0x4e40
.set EXC_NUM_TRAP,      0x00000020
.set EXC_NUM_TRACE,     0x00000009
.set MODE_BREAKPOINT,   0
.set MODE_RUN,          1
.set MODE_STEP,         2
.set MODE_EXCEPTION,    3
.set MODE_CONTINUE,     4
.set MODE_RESTORE,      5
.set MODE_KILL,         6

/* see TaskContext structure in main.c */
.set tc_reg_pc,  0
.set tc_reg_sp,  4
.set tc_reg_sr,  8
.set tc_reg_d,  10
.set tc_reg_a,  42


.text
.extern _debug_main
.global _exc_handler
.global _run_target
.global _g_dummy


/*
 * start target with its own stack and return exit code
 * Contrary to several documents / articles on the Internet, the GNU tool chain for AmigaOS
 * uses register A5 as frame pointer and *not* A6 (probably because of the use of A6 as
 * library base address on the Amiga).
 * TODO: pass command line
 */
_run_target:
    link.w      fp, #-4                 /* set up new stack frame with room for the old stack pointer */
    movem.l     d1-d7/a0-a4/a6, -(sp)   /* save all registers on the old stack except D0 (used for the return value) and the frame pointer A5 */
    move.l      sp, -4(fp)              /* save old stack pointer */
    move.l      12(fp), sp              /* load new stack pointer */
    add.l       16(fp), sp              /* add stack size so it points to end of stack */
    move.l      fp, -(sp)               /* save frame pointer on new stack (so that we can restore the old stack pointer later) */
    move.l      16(fp), -(sp)           /* push stack size onto new stack (AmigaDOS calling convention) */
    move.l      8(fp), a0               /* call entry point of target */
    jsr         (a0)
    addq.l      #4, sp                  /* remove stack size */
    move.l      (sp)+, fp               /* restore frame pointer */
    move.l      -4(fp), sp              /* restore old stack pointer */
    movem.l     (sp)+, d1-d7/a0-a4/a6   /* restore all registers */
    unlk        fp                      /* restore old frame pointer and return */
    rts


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
    /* branch depending on the exception number */
    cmp.l       #EXC_NUM_TRAP, (sp)
    beq.s       exc_trap
    cmp.l       #EXC_NUM_TRACE, (sp)
    beq.s       exc_trace
    bra.w       exc_exc                 /* any other exception */


exc_trap:
    cmp.l       #MODE_BREAKPOINT, mode
    beq.s       exc_call_main
    /* fall through */


exc_restore:
    /* restore all registers and resume target */
    addq.l      #4, sp                  /* remove trap number from stack */
    lea         target_ctx, a0          /* load base address of struct */
    move.l      tc_reg_pc(a0), 2(sp)    /* replace return address on the stack with target PC */
    move.w      tc_reg_sr(a0), (sp)     /* restore status register */
    add.l       #10, a0                 /* move pointer to D0 */
    movem.l     (a0)+, d0-d7            /* restore data registers */
    movem.l     (a0)+, a0-a6            /* restore address registers without A0 (is skipped automatically because it contains the base address) */
    move.l      -28(a0), a0             /* finally restore A0 */
    move.l      #MODE_BREAKPOINT, mode  /* restore mode for next breakpoint */
    rte


exc_call_main:
    /* TODO: save exception number as well */
    move.l      a0, -(sp)               /* save A0 and A1 because we use them */
    move.l      a1, -(sp)
    lea         target_ctx, a0          /* load base address of struct */
    move.l      14(sp), (a0)+           /* PC, offset is 6 + 8 because of saved registers */
    move.l      usp, a1
    move.l      a1, (a0)+               /* SP */
    move.w      12(sp), (a0)+           /* SR, offset is 4 + 8 because of saved registers */
    add.l       #60, a0                 /* move pointer one longword beyond the struct for the pre-decrement mode to work */
    movem.l     d0-d7/a0-a6, -(a0)      /* save registers */
    move.l      (sp)+, 36(a0)           /* finally store saved values of A0 and A1 */
    move.l      (sp)+, 32(a0)

    /* change return address on the stack so that it points to our stub routine below */
    lea         debug_stub, a0          /* load address of stub routine */
    move.l      a0, 6(sp)               /* replace return address with address of stub routine */

    /* remove trap number from stack and "return" to stub routine */
    addq.l      #4, sp
    rte


exc_trace:
    /* trace exception */
    andi.w       #0x78ff, 4(sp)          /* disable trace mode and re-enable interrupts */ 
    move.l       #MODE_STEP, mode
    bra.s        exc_call_main           /* call debug_main() in the same way as with a breakpoint */


exc_exc:
    /* another exception => just call debug_main() */
    move.l      #MODE_EXCEPTION, mode
    bra.s       exc_call_main


debug_stub:
    /* call debug_main() */
    /* TODO: abort target if mode == MODE_KILL */
    pea         target_ctx              /* push target context address and mode onto stack */
    move.l      mode, -(sp)
    jsr         _debug_main
    addq.l      #8, sp                  /* remove target context and mode from stack */

    /* trap again to restore all registers, including the SR which can only be done in
     * supervisor mode, and resume target */
    move.l      #MODE_RESTORE, mode
    trap        #TRAP_NUM


.data
    .lcomm mode, 4                      /* mode, initialized with 0 == MODE_BREAKPOINT */
    .lcomm target_ctx, 70               /* target context, 70 == sizeof(TaskContext) */
    .comm _g_dummy, 4
