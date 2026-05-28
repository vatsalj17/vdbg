#include <stdio.h>
#include "math_utils.h"

int main(void) {
    int x = 10;
    int y = 5;

    int sum = add(x, y);
    printf("%d + %d = %d\n", x, y, sum);

    int product = multiply(x, y);
    printf("%d * %d = %d\n", x, y, product);

    return 0;
}
