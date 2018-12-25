/*
 * glue.s - part of CWDebug, a source-level debugger for AmigaOS
 *          This file contain two assembly routines that "glue" the debugger to the target.
 *
 * Copyright(C) 2018, 2019 Constantin Wiemer
 */


 .text
 .extern _debug_main
.global _trap_handler
.global _run_target


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
    move.l      fp, -(sp)               /* save frame pointer on new stack */
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
 * trap handler for breakpoints
 */
_trap_handler:
    /* branch depending on the exception number */
    cmp.l       #0x00000021, (sp)
    beq.s       trace
    cmp.l       #0x00000009, (sp)
    beq.s       step
    bra.s       main                    /* let debug_main() handle breakpoints and any other exceptions */

trace:
    addq.l      #4, sp                  /* remove trap number from stack  */
    move.l      #0, ninstr              /* initialize instruction counter */
    ori.w       #0x8000, (sp)           /* enable trace mode in *user* mode */
    rte

step:
    addq.l      #4, sp                  /* remove trap number from stack */
    addq.l      #1, ninstr              /* increment number of instructions */
    cmp.l       #2, ninstr
    beq.s       step_restore
    /* fall through */
step_store:
    /* number of instructions = 1: PC = instruction at breakpoint => store address */
    move.l      2(sp), bp_addr
    rte
step_restore:
    /* number of instructions == 2: PC = instruction past breakpoint => disable trace mode and restore breakpoint */
    andi.w      #0x7fff, (sp)           /* disable trace mode */
    move.l      a0, -(sp)               /* save A0 */
    move.l      bp_addr, a0             /* load address of breakpoint */
    move.w      #0x4e40, (a0)           /* restore breakpoint */
    move.l      (sp)+, a0               /* restore A0 */
    rte

main:
    /*
     * save target context - the current stack frame looks like this:
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
    lea         _debug_stub, a0         /* load address of stub routine */
    move.l      a0, 6(sp)               /* replace return address with address of stub routine */

    /* remove trap number from stack and "return" to stub routine */
    addq.l      #4, sp
    rte

_debug_stub:
    /* call debug_main() */
    pea         target_ctx              /* push target context address and mode onto stack */
    move.l      #1, -(sp)
    jsr         _debug_main
    addq.l      #8, sp                  /* remove target context and mode from stack */

    /* restore all registers */
    lea         target_ctx, a0          /* load base address of struct */
    move.l      (a0), -(sp)             /* push original return address onto stack */
    add.l       #10, a0                 /* move pointer to D0 */
    movem.l     (a0)+, d0-d7            /* restore data registers */
    movem.l     (a0)+, a0-a6            /* restore address registers without A0 (is skipped automatically because it contains the base address) */
    move.l      -28(a0), a0             /* finally restore A0 */

    /*
     * We trap again to get into supervisor mode so we can set the trace bit and single-step
     * the next two instructions - the RTS and the original instruction at the breakpoint
     * address. This necessary to restore the breakpoint.
     */
    /* TODO: return flag in D0 to indicate that breakpoint should *not* be restored */
    trap #1
    rts                                 /* jump to return address by "returning" to it */

.data
    .lcomm target_ctx, 70               /* 70 == sizeof(TaskContext) */
    .lcomm ninstr, 4
    .lcomm bp_addr, 4
