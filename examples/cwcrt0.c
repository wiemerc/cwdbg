//
// minimalistic startup code to be used with some of the examples
//


#include <dos/dos.h>
#include <proto/exec.h>


// The library name needs to be defined as global *array* (not pointer), otherwise GCC
// will treat it as a constant and put it at the *beginning* of the code block, which is
// a problem because there is no start address in the Hunk format (unlike ELF or PE).
static char libname[] = "dos.library";

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;

extern int entry();


int start()
{
    // initialize library symbols
    SysBase = *((struct ExecBase **) 0x00000004);
    if ((DOSBase = (struct DosLibrary *) OpenLibrary(libname, 0L)) == NULL)
        return RETURN_ERROR;

    // call main program
    return entry();
}
