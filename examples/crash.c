#include <proto/exec.h>
#include <stdio.h>


extern void exc_handler();


int main()
{
    // TODO: Install exception handler in startup code
    FindTask(NULL)->tc_TrapCode = exc_handler;

    printf("Address of main() = %p\n", main);
    asm("trap #3");
    return 0;
}
