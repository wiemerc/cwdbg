#ifndef CWDEBUG_UTIL_H
#define CWDEBUG_UTIL_H
//
// util.h - part of CWDebug, a source-level debugger for the AmigaOS
//
// Copyright(C) 2018-2022 Constantin Wiemer
//


#include <dos/dos.h>
#include <exec/types.h>


//
// constants
//

// log levels
#define DEBUG 0
#define INFO  1
#define WARN  2 
#define ERROR 3
#define CRIT  4


//
// exported functions
//
void logmsg(const char *p_fname, int lineno, const char *p_func, UBYTE level, const char *p_fmtstr, ...);
void dump_memory(const uint8_t *p_addr, uint32_t size);
int pack_data(uint8_t *p_buffer, size_t buf_size, const char *p_fmt_str, ...);
int unpack_data(const uint8_t *p_buffer, size_t buf_size, const char *p_fmt_str, ...);


//
// macros
//
#define LOG(level, p_fmtstr, ...) {logmsg(__FILE__, __LINE__, __func__, level, p_fmtstr, ##__VA_ARGS__);}
#define C_TO_BCPL_PTR(ptr) ((BPTR) (((ULONG) (ptr)) >> 2))
#define BCPL_TO_C_PTR(ptr) ((APTR) (((ULONG) (ptr)) << 2))


//
// external references
//
extern UBYTE g_loglevel;

#endif // CWDEBUG_UTIL_H
