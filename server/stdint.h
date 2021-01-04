#ifndef CWDEBUG_STDINT_H
#define CWDEBUG_STDINT_H
/*
 * stdint.h - part of CWDebug, a source-level debugger for the AmigaOS
 *            This file contains some of the C99 type definitions that the old GCC I use lacks.
 *
 * Copyright(C) 2018-2021 Constantin Wiemer
 */


// to get the types int*_t
#include <sys/types.h>

// the missing types
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

#endif /* CWDEBUG_STDINT_H */
