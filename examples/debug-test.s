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
    beq.s       error_no_dos
    move.l      d0, DOSBase

    /* print string */
    trap        #0
    movea.l     DOSBase, a6
    move.l      #msg, d1
    jsr         PutStr(a6)
    
    /* close DOS library */
    movea.l     AbsExecBase, a6
    movea.l     DOSBase, a1
    jsr         CloseLibrary(a6)

    clr.l       d0
    rts

error_no_dos:
    moveq.l     #1, d0
    rts


.data
    .comm DOSBase, 4

    libname:    .asciz "dos.library"
    msg:        .asciz "hello, Amiga\n"
