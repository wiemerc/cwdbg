#include <stdio.h>
#include <string.h>

#define MAX_TRAIT_SIZE 100

static void get_num_trait(int i, char *num_trait_buffer, int buffer_size)
{
    if (i % 3 == 0)
        strncpy(num_trait_buffer, "divisible by 3", buffer_size - 1);
    else if (i % 2 == 0)
        strncpy(num_trait_buffer, "even", buffer_size - 1);
    else
        strncpy(num_trait_buffer, "odd", buffer_size - 1);
    num_trait_buffer[buffer_size - 1] = 0;
}

int main()
{
    int i;
    char num_trait[MAX_TRAIT_SIZE];

    for (i = 0; i < 10; i++) {
        get_num_trait(i, num_trait, MAX_TRAIT_SIZE);
        printf("%d is %s\n", i, num_trait);
    }
    return 0;
}