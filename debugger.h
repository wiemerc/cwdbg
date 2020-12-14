#ifndef CWNET_DEBUGGER_H
#define CWNET_DEBUGGER_H
/*
 * debugger.h - part of CWDebug, a source-level debugger for the AmigaOS
 *
 * Copyright(C) 2018-2021 Constantin Wiemer
 */


/*
 * included files
 */
#include <exec/types.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "util.h"
#include "serio.h"
#include "m68k.h"


/*
 * constants
 */
#define TRAP_NUM        0
#define TRAP_OPCODE     0x4e40
#define STACK_SIZE      8192
#define MODE_BREAKPOINT 0
#define MODE_RUN        1
#define MODE_STEP       2
#define MODE_EXCEPTION  3
#define MODE_CONTINUE   4
#define MODE_RESTORE    5
#define MODE_KILL       6


/*
 * type definitions
 */
typedef struct {
    APTR   tc_reg_sp;
    ULONG  tc_exc_num;
    USHORT tc_reg_sr;
    APTR   tc_reg_pc;
    ULONG  tc_reg_d[8];
    ULONG  tc_reg_a[7];                  // without A7 = SP
} TaskContext;

typedef struct {
    struct Node  bp_node;
    ULONG        bp_num;
    APTR         bp_addr;
    USHORT       bp_opcode;
    ULONG        bp_count;
} BreakPoint;


/*
 * exported functions
 */
int debug_main(int mode, APTR data);

#endif /* CWNET_DEBUGGER_H */
