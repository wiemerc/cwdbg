#include "proto/dos.h"

// The declaration below is a workaround for a bug in GCC which causes it to create a
// corrupt executable (additional null word at the end of HUNK_DATA) if there are no
// relocations for the data block. By referencing a string constant, which is placed at
// the beginning of the code block, we make sure there is at least one relocation.
static const char *dummy = "bla";

// We must not name the function main, otherwise GCC will insert a call to __main which does apparently some
// initializations for the standard library.
int entry(int argc, char **argv)
{
    asm("trap #0");
    PutStr("hello, amiga\n");
    asm("trap #0");
    return 1;
}
