#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int global_counter = 0;
char global_message[64] = "hello from global";
unsigned long secret_value = 0xDEADBEEFCAFEBABE;

void increment_counter(void) {
	global_counter++;
	printf("[counter] value is now %d\n", global_counter);
}

int factorial(int n) {
	if (n <= 1) return 1;
	return n * factorial(n - 1);
}

void print_array(int *arr, int len) {
	printf("[array] ");
	for (int i = 0; i < len; i++) {
		printf("%d ", arr[i]);
	}
	printf("\n");
}

void modify_memory_demo(void) {
	int *buf = malloc(8 * sizeof(int));
	if (!buf) {
		perror("malloc");
		return;
	}

	for (int i = 0; i < 8; i++)
		buf[i] = (i + 1) * 10;

	printf("[heap] before: ");
	print_array(buf, 8);

    printf("buf: %p\n", (void*)buf);
	printf("[heap] set a breakpoint here and try: memory write <addr> <val>\n");
	sleep(1);

	printf("[heap] after:  ");
	print_array(buf, 8);

	free(buf);
}

void register_demo(void) {
	volatile int limit = 5;
	printf("[register_demo] counting to %d (try 'register write rcx <val>')\n", limit);
	for (volatile int i = 0; i < limit; i++) {
		printf("  tick %d\n", i);
		sleep(0);
	}
}

void string_demo(void) {
	char buf[32];
	strncpy(buf, "initial string", sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	printf("[string] local buf = \"%s\"\n", buf);
	printf("[string] global_message = \"%s\"\n", global_message);

	printf("[string] try corrupting global_message via 'memory write'\n");
	sleep(1);

	printf("[string] global_message is now: \"%s\"\n", global_message);
}

void segfault_demo(int trigger) {
	if (!trigger) return;
	printf("[segfault] about to dereference NULL — hope you have a handler!\n");
	int *p = NULL;
	*p = 42;
}

int main(int argc, char *argv[]) {
	int trigger_segfault = 0;
	if (argc > 1 && strcmp(argv[1], "segfault") == 0) {
		trigger_segfault = 1;
	}

	printf("=== debugger test target (pid %d) ===\n", (int)getpid());
	printf("secret_value @ %p = 0x%lx\n", (void *)&secret_value, secret_value);
	printf("global_counter @ %p\n\n", (void *)&global_counter);

	increment_counter();
	increment_counter();
	increment_counter();

	int n = 6;
	printf("[factorial] %d! = %d\n", n, factorial(n));

	modify_memory_demo();
	register_demo();
	string_demo();
	segfault_demo(trigger_segfault);

	return 0;
}
