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

    /* send signal to task */
    movea.l     _SysBase, a6
    suba.l      a1, a1                  /* process name => NULL */
    jsr         -294(a6)                /* FindTask() */
    movea.l     d0, a1                  /* task pointer */
    move.l      #0x80000000, d0         /* signal */
    jsr         -324(a6)                /* Signal() */

    /* remove trap number from stack and return */
    addq.l      #4, a7
    rte
