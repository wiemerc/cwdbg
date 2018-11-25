/*
 * trap.s - part of CWDebug, a source-level debugger for AmigaOS
 *
 * Copyright(C) 2018, 2019 Constantin Wiemer
 */


 .text
 .extern _SysBase
 .extern _g_ntraps
.global _trap_handler


/*
 * trap handler for breakpoints and single-step mode
 */
_trap_handler:
    /* TODO: Which registers need to be saved?
    /* increment trap counter */
    move.l      d0, -(a7)
    move.l      (_g_ntraps), d0
    addq.l      #1, d0
    move.l      d0, (_g_ntraps)
    move.l      (a7)+, d0

    /* set signal for task */
    movea.l     _SysBase, a6
    move.l      #0x80000000, d0         /* signal value */
    move.l      #0x80000000, d1         /* signal mask */
    jsr         -306(a6)                /* SetSignal() */

    /* remove trap number from stack and return */
    addq.l      #4, a7
    rte
