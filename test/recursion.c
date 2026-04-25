#include <stdio.h>

int fib(int n) {
    printf("Entering fib(%d)\n", n);

    if (n <= 1) {
        printf("Returning %d\n", n);
        return n;
    }

    int result = fib(n - 1) + fib(n - 2);
    printf("Returning %d for fib(%d)\n", result, n);
    return result;
}

int main(void) {
    fib(4);
    return 0;
}
