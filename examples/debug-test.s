/*
 * some constants
 */
.set AbsExecBase, 4
.set OpenLibrary, -552 
.set CloseLibrary, -414
.set PutStr, -948


.text
    /* open DOS library */
    movea.l     AbsExecBase, a6
    lea         libname, a1
    clr.l       d0
    jsr         OpenLibrary(a6)
    tst.l       d0
    beq.w       error_no_dos
    move.l      d0, DOSBase

    /* set registers to defined values */
    /* address registers */
    moveq.l     #7, d0                  /* loop counter = number of registers */
    move.l      #0xa6a6a6a6, d1         /* defined value, first register is A6 */
set_loop_start:
    move.l      d1, -(sp)               /* push register */
    subq.l      #1, d0                  /* decrement loop counter */
    beq.s       set_loop_end
    subi.l      #0x1010101, d1          /* decrement expected value */
    bra.s       set_loop_start
set_loop_end:
    cmp.l       #0xd0d0d0d0, d1         /* check if we set address or data registers */
    beq.s       set_loop_end_d
set_loop_end_a:
    movem.l     (sp)+, a0-a6            /* pop all address registers */
    /* data registers */
    moveq.l     #8, d0                  /* loop counter = number of registers */
    move.l      #0xd7d7d7d7, d1         /* defined value, first register is D7 */
    bra.s       set_loop_start
set_loop_end_d:
    movem.l     (sp)+, d0-d7            /* pop all data registers */

    /* trap to debugger, push defined value onto stack before */
    move.l      #0xdeadbeef, -(sp)
    trap        #0

    /* check if all registers contain the previous values */
    movem.l     d0-d7/a0-a6, -(sp)      /* push all registers onto stack */
    /* data registers */
    moveq.l     #8, d0                  /* loop counter = number of registers */
    move.l      #0xd0d0d0d0, d1         /* expected value, first register is D0 */
check_loop_start:
    move.l      (sp)+, d2               /* pop register */
    cmp.l       d1, d2                  /* compare with expected value */
    bne.s       wrong_value
next_value:
    subq.l      #1, d0                  /* decrement loop counter */
    beq.s       check_loop_end
    addi.l      #0x1010101, d1          /* increment expected value */
    bra.s       check_loop_start
check_loop_end:
    cmp.l       #0xa6a6a6a6, d1         /* check if we set address or data registers */
    beq.s       check_loop_end_a
check_loop_end_d:
    /* address registers */
    moveq.l     #7, d0                  /* loop counter = number of registers */
    move.l      #0xa0a0a0a0, d1         /* expected value, first register is A0 */
    bra.s       check_loop_start
check_loop_end_a:
    bra.s       normal_exit
wrong_value:
    /* print error message */
    movem.l     d0-d1, -(sp)            /* save D0 and D1 because they're used in the loop */
    movea.l     DOSBase, a6
    move.l      #msg, d1
    jsr         PutStr(a6)
    movem.l     (sp)+, d0-d1
    bra.s       next_value
    
normal_exit:
    /* close DOS library */
    movea.l     AbsExecBase, a6
    movea.l     DOSBase, a1
    jsr         CloseLibrary(a6)

    /* check if defined value is still on the stack */
    clr.l       d0
    move.l      (sp)+, d1
    cmp.l       #0xdeadbeef, d1
    sne.b       d0                      /* exit code, 0 if defined value was still on the stack, 0xff otherwise */
    rts

error_no_dos:
    moveq.l     #1, d0                  /* exit code */
    rts


.data
    .comm DOSBase, 4

    libname:    .asciz "dos.library"
    msg:        .asciz "register contains different value after trap\n"
