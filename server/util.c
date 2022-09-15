//
// util.h - part of CWDebug, a source-level debugger for the AmigaOS
//          This file contains some utility routines.
//
// Copyright(C) 2018-2022 Constantin Wiemer
//


#include <assert.h>
#ifdef TEST
    #include <clib/exec_protos.h>
#else
    #include <proto/exec.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#ifdef TEST
    #include <setjmp.h>
    #include <cmocka.h>
#endif

#include "stdint.h"
#include "util.h"


#ifdef TEST
    #undef assert
    #define assert(expression) mock_assert((int) (expression), #expression, __FILE__, __LINE__);
#endif


uint8_t g_loglevel;


static char *p_level_name[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "CRIT"
};


void logmsg(const char *p_fname, int lineno, const char *p_func, uint8_t level, const char *p_fmtstr, ...)
{
    va_list args;
    char location[32];

    if (level < g_loglevel)
        return;

    snprintf(location, 32, "%s:%d", p_fname, lineno);
    // TODO: The correct way would be to mock all system calls instead of just not calling them when we run the tests.
    #ifdef TEST
        printf("0x%08x | %-15s | %-25s | %-5s | ", 0xdeadbeef, location, p_func, p_level_name[level]);
    #else
        printf("0x%08x | %-15s | %-25s | %-5s | ", (uint32_t) FindTask(NULL), location, p_func, p_level_name[level]);
    #endif

    va_start(args, p_fmtstr);
    vprintf(p_fmtstr, args);
    va_end(args);
    printf("\n");
}


void dump_memory(const uint8_t *p_addr, uint32_t size)
{
    uint32_t pos = 0, i, nchars;
    char line[256], *p;

    while (pos < size) {
        printf("%04x: ", pos);
        for (i = pos, p = line, nchars = 0; (i < pos + 16) && (i < size); ++i, ++p, ++nchars) {
            printf("%02x ", p_addr[i]);
            if (p_addr[i] >= 0x20 && p_addr[i] <= 0x7e) {
                sprintf(p, "%c", p_addr[i]);
            }
            else {
                sprintf(p, ".");
            }
        }
        if (nchars < 16) {
            for (i = 1; i <= (3 * (16 - nchars)); ++i, ++p, ++nchars) {
                sprintf(p, " ");
            }
        }
        *p = '\0';

        printf("\t%s\n", line);
        pos += 16;
    }
}


int pack_data(uint8_t *p_buffer, uint32_t buf_size, const char *p_format_str, ...)
{
    va_list args;
    const char *p_fmt;
    uint8_t *p_buf_pos = p_buffer;
    uint32_t buf_size_left = buf_size;
    int rc = DOSTRUE;

    // input variables of the respective types, passed in as variadic args
    uint8_t i8;
    uint16_t i16;
    uint32_t i32;

    assert((p_buffer != NULL) && (buf_size > 0) && (p_format_str != NULL));
    va_start(args, p_format_str);
    for (p_fmt = p_format_str; *p_fmt != '\0'; p_fmt++) {
        switch (*p_fmt) {
            case '!':
                // We ignore the byte order indicator as we only support network byte order (big endian),
                // but we want to be compatible with the format strings we use on the host side.
                break;

            case 'B':
                if (buf_size_left >= 1) {
                    i8 = va_arg(args, uint32_t);
                    *p_buf_pos++ = i8;
                    --buf_size_left;
                }
                else {
                    LOG(ERROR, "Not enough bytes in buffer to pack byte")
                    rc = DOSFALSE;
                    goto exit;
                }
                break;

            case 'H':
                if (buf_size_left >= 2) {
                    // Variadic args are always extended to an (unsigned) int, so we can't use the actual type in va_arg().
                    i16 = va_arg(args, uint32_t);
                    *p_buf_pos++ = (uint8_t) ((i16 >> 8) & 0xff);
                    *p_buf_pos++ = (uint8_t) (i16 & 0xff);
                    buf_size_left -= 2;
                }
                else {
                    LOG(ERROR, "Not enough bytes in buffer to pack word")
                    rc = DOSFALSE;
                    goto exit;
                }
                break;

            case 'I':
                if (buf_size_left >= 4) {
                    i32 = va_arg(args, uint32_t);
                    *p_buf_pos++ = (uint8_t) ((i32 >> 24) & 0xff);
                    *p_buf_pos++ = (uint8_t) ((i32 >> 16) & 0xff);
                    *p_buf_pos++ = (uint8_t) ((i32 >> 8) & 0xff);
                    *p_buf_pos++ = (uint8_t) (i32 & 0xff);
                    buf_size_left -= 4;
                }
                else {
                    LOG(ERROR, "Not enough bytes in buffer to pack double word")
                    rc = DOSFALSE;
                    goto exit;
                }
                break;

            default:
                LOG(ERROR, "Unknown format specifier '%c'", *p_fmt);
                rc = DOSFALSE;
                goto exit;
        }
    }

    exit:
        va_end(args);
        return rc;
}


int unpack_data(uint8_t *p_buffer, uint32_t buf_size, const char *p_format_str, ...)
{
    va_list args;
    const char *p_fmt;
    uint8_t *p_buf_pos = p_buffer;
    uint32_t buf_size_left = buf_size;
    int rc = DOSTRUE;

    // output variables of the respective types, passed in as variadic args
    uint8_t *p8;
    uint16_t *p16;
    uint32_t *p32;

    assert((p_buffer != NULL) && (buf_size > 0) && (p_format_str != NULL));
    va_start(args, p_format_str);
    for (p_fmt = p_format_str; *p_fmt != '\0'; p_fmt++) {
        switch (*p_fmt) {
            case '!':
                // We ignore the byte order indicator as we only support network byte order (big endian),
                // but we want to be compatible with the format strings we use on the host side.
                break;

            case 'B':
                if (buf_size_left >= 1) {
                    p8 = va_arg(args, uint8_t *);
                    *p8 = *p_buf_pos++;
                    --buf_size_left;
                }
                else {
                    LOG(ERROR, "Not enough bytes in buffer to unpack byte")
                    rc = DOSFALSE;
                    goto exit;
                }
                break;

            case 'H':
                if (buf_size_left >= 2) {
                    p16 = va_arg(args, uint16_t *);
                    *p16 = *p_buf_pos++ << 8;
                    *p16 |= *p_buf_pos++;
                    buf_size_left -= 2;
                }
                else {
                    LOG(ERROR, "Not enough bytes in buffer to unpack word")
                    rc = DOSFALSE;
                    goto exit;
                }
                break;

            case 'I':
                if (buf_size_left >= 4) {
                    p32 = va_arg(args, uint32_t *);
                    *p32 = *p_buf_pos++ << 24;
                    *p32 |= *p_buf_pos++ << 16;
                    *p32 |= *p_buf_pos++ << 8;
                    *p32 |= *p_buf_pos++;
                    buf_size_left -= 4;
                }
                else {
                    LOG(ERROR, "Not enough bytes in buffer to unpack double word")
                    rc = DOSFALSE;
                    goto exit;
                }
                break;

            default:
                LOG(ERROR, "Unknown format specifier '%c'", *p_fmt);
                rc = DOSFALSE;
                goto exit;
        }
    }

    exit:
        va_end(args);
        return rc;
}


//
// unit tests for pack / unpack
//
#ifdef TEST
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static void test_pack_byte(void **state)
{
    uint8_t buffer[1];
    pack_data(buffer, sizeof(buffer), "!B", 0x42);
    assert_memory_equal(buffer, ((uint8_t[]) {0x42}), 1);
}


static void test_pack_word(void **state)
{
    uint8_t buffer[2], expected_buffer[] = {0xca, 0xfe};
    pack_data(buffer, sizeof(buffer), "!H", 0xcafe);
    assert_memory_equal(buffer, expected_buffer, 2);
}


static void test_pack_dword(void **state)
{
    uint8_t buffer[4], expected_buffer[] = {0xca, 0xfe, 0xba, 0xbe};
    pack_data(buffer, sizeof(buffer), "!I", 0xcafebabe);
    assert_memory_equal(buffer, expected_buffer, 4);
}


static void test_pack_wrong_size(void **state)
{
    uint8_t buffer[2];
    int rc = pack_data(buffer, sizeof(buffer), "!I", 0xcafebabe);
    assert_int_equal(rc, DOSFALSE);
}


static void test_pack_wrong_format(void **state)
{
    uint8_t buffer[4];
    int rc = pack_data(buffer, sizeof(buffer), ">I", 0xcafebabe);
    assert_int_equal(rc, DOSFALSE);
}


static void test_pack_null_args(void **state)
{
    expect_assert_failure(pack_data(NULL, 0, NULL));
}


static void test_unpack_byte(void **state)
{
    uint8_t buffer[] = {0x42};
    uint8_t result;
    unpack_data(buffer, sizeof(buffer), "!B", &result);
    assert_int_equal(result, 0x42);
}


static void test_unpack_word(void **state)
{
    uint8_t buffer[] = {0xca, 0xfe};
    uint16_t result;
    unpack_data(buffer, sizeof(buffer), "!H", &result);
    assert_int_equal(result, 0xcafe);
}


static void test_unpack_dword(void **state)
{
    uint8_t buffer[] = {0xca, 0xfe, 0xba, 0xbe};
    uint32_t result;
    unpack_data(buffer, sizeof(buffer), "!I", &result);
    assert_int_equal(result, 0xcafebabe);
}


static void test_unpack_wrong_size(void **state)
{
    uint8_t buffer[] = {0xca, 0xfe};
    uint16_t result;
    int rc = unpack_data(buffer, sizeof(buffer), "!I", &result);
    assert_int_equal(rc, DOSFALSE);
}


static void test_unpack_wrong_format(void **state)
{
    uint8_t buffer[] = {0xca, 0xfe};
    uint16_t result;
    int rc = unpack_data(buffer, sizeof(buffer), ">H", &result);
    assert_int_equal(rc, DOSFALSE);
}


static void test_unpack_null_args(void **state)
{
    expect_assert_failure(unpack_data(NULL, 0, NULL));
}


int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pack_byte),
        cmocka_unit_test(test_pack_word),
        cmocka_unit_test(test_pack_dword),
        cmocka_unit_test(test_pack_wrong_size),
        cmocka_unit_test(test_pack_wrong_format),
        cmocka_unit_test(test_pack_null_args),
        cmocka_unit_test(test_unpack_byte),
        cmocka_unit_test(test_unpack_word),
        cmocka_unit_test(test_unpack_dword),
        cmocka_unit_test(test_unpack_wrong_size),
        cmocka_unit_test(test_unpack_wrong_format),
        cmocka_unit_test(test_unpack_null_args),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
#pragma GCC diagnostic pop
#endif
