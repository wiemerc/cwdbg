#ifndef CWDBG_STDINT_H
#define CWDBG_STDINT_H
/*
 * stdint.h - part of cwdbg, a debugger for the AmigaOS
 *            This file contains some of the C99 type definitions that the old GCC I use lacks.
 *
 * Copyright(C) 2018-2022 Constantin Wiemer
 */


// to get the types int*_t
#include <sys/types.h>

// the missing types
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

#endif /* CWDBG_STDINT_H */
