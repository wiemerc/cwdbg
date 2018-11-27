/*
 * trap.s - part of CWDebug, a source-level debugger for AmigaOS
 *
 * Copyright(C) 2018, 2019 Constantin Wiemer
 */


 .text
 .extern _g_ntraps
 .extern _g_target_pc
 .extern _debug_main
.global _trap_handler


/*
 * trap handler for breakpoints and single-step mode
 */
_trap_handler:
    /* save registers */
    /* TODO: save all registers in global struct */
    move.l      d0, -(sp)
    move.l      a0, -(sp)

    /* increment trap counter */
    move.l      _g_ntraps, d0
    addq.l      #1, d0
    move.l      d0, _g_ntraps

    /*
     * now the tricky part... manipulate return address on the stack so that it
     * points to our stub routine below. The stack frame looks like this:
     *
     * 15                             0
     * --------------------------------
     * | A0 (high word)               |     +0
     * --------------------------------
     * | A0 (low word)                |     +2
     * --------------------------------
     * | D0 (high word)               |     +4
     * --------------------------------
     * | D0 (low word)                |     +6
     * --------------------------------
     * | exception number (high word) |     +8
     * --------------------------------
     * | exception number (low word)  |     +10
     * --------------------------------
     * | status register              |     +12
     * --------------------------------
     * | return address (high word)   |     +14
     * --------------------------------
     * | return address (low word)    |     +16
     * --------------------------------
     */
    move.l      usp, a0                 /* load user stack pointer */
    move.l      14(sp), _g_target_pc    /* save original return address */
    lea         _debug_stub, a0         /* load address of stub routine */
    move.l      a0, 14(sp)              /* replace return address with address of stub routine */

    /* restore registers */
    move.l      (sp)+, a0
    move.l      (sp)+, d0
    
    /* remove trap number from stack and return */
    addq.l      #4, sp
    rte

_debug_stub:
    jsr         _debug_main
    /* TODO: restore all registers from global struct */
    movea.l     _g_target_pc, a0
    jmp         (a0)
