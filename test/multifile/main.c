#include <stdio.h>
#include "math_utils.h"

typedef int (*operation) (int, int);

int calculate(operation op, int a, int b) {
    int res = op(a, b);
    return res;
}

int main(void) {
    int x = 10;
    int y = 5;

    int sum = calculate(add, x, y);
    printf("%d + %d = %d\n", x, y, sum);

    int product = calculate(multiply, x, y);
    printf("%d * %d = %d\n", x, y, product);

    return 0;
}
