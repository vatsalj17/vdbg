#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 1. NULL pointer dereference */
void segfault_null_deref(void) {
    int *p = NULL;
    *p = 42;
}

/* 2. Use-after-free */
void segfault_use_after_free(void) {
    int *p = malloc(sizeof(int));
    free(p);
    *p = 10;
}

/* 3. Double free (may abort instead of segfault) */
void segfault_double_free(void) {
    int *p = malloc(sizeof(int));
    free(p);
    free(p);
}

/* 4. Stack overflow (infinite recursion) */
void segfault_stack_overflow(void) {
    segfault_stack_overflow();
}

/* 5. Writing to read-only memory (string literal) */
void segfault_modify_string_literal(void) {
    char *s = "hello";
    s[0] = 'H';
}

/* 6. Invalid pointer arithmetic */
void segfault_invalid_pointer_math(void) {
    int *p = malloc(sizeof(int));
    int *q = p + 1000000000; // way out of range
    *q = 5;
}

/* 7. Accessing uninitialized pointer */
void segfault_uninitialized_pointer(void) {
    int *p;
    *p = 5;
}

/* 8. Buffer overflow (heap) */
void segfault_heap_overflow(void) {
    char *buf = malloc(8);
    strcpy(buf, "this is way too long");
}

/* 9. Buffer overflow (stack) */
void segfault_stack_overflow_buffer(void) {
    char buf[8];
    strcpy(buf, "this is too long");
}

/* 10. Misaligned memory access (on strict architectures) */
void segfault_misaligned_access(void) {
    char *buf = malloc(8);
    int *p = (int *)(buf + 1); // misaligned
    *p = 42;
}

/* 11. Executing non-executable memory */
void segfault_exec_data(void) {
    char data[] = {0xc3}; // ret instruction
    void (*func)(void) = (void (*)())data;
    func(); // NX bit may cause segfault
}

/* 12. Dereferencing wild pointer */
void segfault_wild_pointer(void) {
    int *p = (int *)0x12345678;
    *p = 99;
}

/* 13. Use-after-scope (dangling stack pointer) */
int* get_dangling_ptr(void) {
    int x = 10;
    return &x;
}
void segfault_use_after_scope(void) {
    int *p = get_dangling_ptr();
    *p = 20;
}

/* 14. mmap misuse (access after munmap) */
#include <sys/mman.h>
void segfault_munmap_access(void) {
    int *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    munmap(p, 4096);
    *p = 42;
}

/* 15. Writing to code segment */
void segfault_write_code(void) {
    void (*func)(void) = segfault_write_code;
    unsigned char *p = (unsigned char *)func;
    p[0] = 0x90; // try to modify instruction
}

/* 16. Huge allocation + unchecked NULL */
void segfault_malloc_fail(void) {
    size_t huge = (size_t)-1;
    int *p = malloc(huge);
    *p = 1; // malloc likely returned NULL
}

/* 17. Out-of-bounds on freed region after realloc shrink */
void segfault_realloc_shrink(void) {
    char *p = malloc(100);
    p = realloc(p, 10);
    p[50] = 'A'; // out of new bounds
}

/* 18. Corrupting heap metadata */
void segfault_heap_metadata_corruption(void) {
    char *p = malloc(16);
    memset(p, 'A', 64); // overwrite allocator metadata
    free(p); // may crash inside allocator
}

/* 19. Recursive alloca exhaustion */
#include <alloca.h>
void segfault_alloca_recursion(void) {
    alloca(1024);
    segfault_alloca_recursion();
}

/* 20. Invalid function pointer call */
void segfault_bad_function_pointer(void) {
    void (*func)(void) = (void (*)(void))0xdeadbeef;
    func();
}

typedef void (*test_fn)(void);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <test_number>\n", argv[0]);
        return 1;
    }

    int choice = atoi(argv[1]);

    test_fn tests[] = {
        segfault_null_deref,
        segfault_use_after_free,
        segfault_double_free,
        segfault_stack_overflow,
        segfault_modify_string_literal,
        segfault_invalid_pointer_math,
        segfault_uninitialized_pointer,
        segfault_heap_overflow,
        segfault_stack_overflow_buffer,
        segfault_misaligned_access,
        segfault_exec_data,
        segfault_wild_pointer,
        segfault_use_after_scope,
        segfault_munmap_access,
        segfault_write_code,
        segfault_malloc_fail,
        segfault_realloc_shrink,
        segfault_heap_metadata_corruption,
        segfault_alloca_recursion,
        segfault_bad_function_pointer
    };

    int total = sizeof(tests) / sizeof(tests[0]);

    if (choice < 0 || choice >= total) {
        fprintf(stderr, "Invalid test number. Choose 0–%d\n", total - 1);
        return 1;
    }

    printf("Running test %d...\n", choice);
    tests[choice]();

    return 0;
}
