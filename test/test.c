#include <stdio.h>
#include <unistd.h>

int main(void) {
    int i = 0;
    printf("main: %p\n", main);
    while (1) {
        printf("%d\n", i++);
        sleep(1);
    }
}
