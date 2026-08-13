#include <stdio.h>

int add(int a, int b) {
	return a + b;
}

static inline int sub(int a, int b) {
    return a - b;
}

int main(void) {
	printf("%d\n", add(4, 2));
	printf("%d\n", sub(4, 2));
}
