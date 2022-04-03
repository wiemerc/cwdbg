#include <stdio.h>

int main()
{
    int i;
    for (i = 1; i <= 10; i++) {
        printf("%d", i);
        if ((i % 2) == 0)
            printf(" is even\n");
        else if ((i % 3) == 0)
            printf(" is dividable by 3\n");
        else
            printf("\n");
    }
    return 0;
}
