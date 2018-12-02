/*
 * trap.s - part of CWDebug, a source-level debugger for AmigaOS
 *
 * Copyright(C) 2018, 2019 Constantin Wiemer
 */


 .text
 .extern _g_ntraps
 .extern _g_target_ctx
 .extern _debug_main
.global _trap_handler


/*
 * trap handler for breakpoints and single-step mode
 */
_trap_handler:
    /*
     * save context in global struct. The current stack frame looks like this:
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
    lea         _g_target_ctx, a0       /* load base address of struct */
    move.l      14(sp), (a0)+           /* PC, offset is 6 + 8 because of saved registers */
    move.l      usp, a1
    move.l      a1, (a0)+               /* SP */
    move.w      12(sp), (a0)+           /* SR, offset is 4 + 8 because of saved registers */
    add.l       #60, a0                 /* move pointer one longword beyond the struct for the pre-decrement mode to work */
    movem.l     d0-d7/a0-a6, -(a0)      /* save registers */
    move.l      (sp)+, 36(a0)           /* finally store saved values of A0 and A1 */
    move.l      (sp)+, 32(a0)

    /* increment trap counter */
    move.l      _g_ntraps, d0
    addq.l      #1, d0
    move.l      d0, _g_ntraps

    /* change return address on the stack so that it points to our stub routine below */
    lea         _debug_stub, a0         /* load address of stub routine */
    move.l      a0, 6(sp)               /* replace return address with address of stub routine */

    /* remove trap number from stack and "return" to stub routine */
    addq.l      #4, sp
    rte

_debug_stub:
    jsr         _debug_main
    /* restore all registers from global struct and return to target */
    lea         _g_target_ctx, a0       /* load base address of struct */
    move.l      (a0), -(sp)             /* push original return address onto stack */
    add.l       #10, a0                 /* move pointer to D0 */
    movem.l     (a0)+, d0-d7            /* restore data registers */
    addq.l      #4, a0                  /* move pointer to A1
    movem.l     (a0)+, a1-a6            /* restore address registers without A0 */
    move.l      -28(a0), a0             /* finally restore A0 */
    rts                                 /* jump to return address by "returning" to it */
