#ifndef MACRO_H
#define MACRO_H

// since the codebase was becoming ugly so
// i decided to make macros that i can use everwhere
// instead of writing the same thing again and again
// i can use these macros

#ifdef DEBUG
#define DBG_LOG(fmt, ...) fprintf(stderr, "[DEBUG](%s) " fmt "\n", __func__, ##__VA_ARGS__)
#else
// writing something to prevent empty-body, i.e only semicolon, without any block of code
#define DBG_LOG(fmt, ...)                                                                          \
	do {                                                                                           \
	} while (0)
#endif

#define DBG_ERR(fmt, ...) fprintf(stderr, "[ERROR](%s) " fmt "\n", __func__, ##__VA_ARGS__)
#define DBG_PERROR(msg)                                                                            \
	do {                                                                                           \
		fprintf(stderr, "[ERROR](%s) ", __func__);                                                 \
		perror(msg);                                                                               \
	} while (0)

#define CRITICAL(fmt, ...)                                                                         \
	do {                                                                                           \
		DBG_ERR(fmt, ##__VA_ARGS__);                                                               \
		abort();                                                                                   \
	} while (0)
#define CRITICAL_PERROR(msg)                                                                       \
	do {                                                                                           \
		DBG_PERROR(msg);                                                                           \
		abort();                                                                                   \
	} while (0)

#define UNUSED __attribute__((unused))

// regular text
#define BLK "\033[0;30m"
#define RED "\033[0;31m"
#define GRN "\033[0;32m"
#define YEL "\033[0;33m"
#define BLU "\033[0;34m"
#define MAG "\033[0;35m"
#define CYN "\033[0;36m"
#define WHT "\033[0;37m"

// regular bold text
#define BBLK "\033[1;30m"
#define BRED "\033[1;31m"
#define BGRN "\033[1;32m"
#define BYEL "\033[1;33m"
#define BBLU "\033[1;34m"
#define BMAG "\033[1;35m"
#define BCYN "\033[1;36m"
#define BWHT "\033[1;37m"

// high intensty text
#define HBLK "\033[0;90m"
#define HRED "\033[0;91m"
#define HGRN "\033[0;92m"
#define HYEL "\033[0;93m"
#define HBLU "\033[0;94m"
#define HMAG "\033[0;95m"
#define HCYN "\033[0;96m"
#define HWHT "\033[0;97m"

// bold high intensity text
#define BHBLK "\033[1;90m"
#define BHRED "\033[1;91m"
#define BHGRN "\033[1;92m"
#define BHYEL "\033[1;93m"
#define BHBLU "\033[1;94m"
#define BHMAG "\033[1;95m"
#define BHCYN "\033[1;96m"
#define BHWHT "\033[1;97m"

// reset
#define RESET "\033[0m"

#endif
