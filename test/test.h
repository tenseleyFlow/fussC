#ifndef FUSSY_TEST_H
#define FUSSY_TEST_H

#include <stdio.h>

/*
 * Bespoke test harness - no external framework. Each test is a void function
 * registered in test_main.c. CHECK records a failure but keeps going so one
 * run reports every problem.
 */

extern int fussy_checks;
extern int fussy_failures;

#define CHECK(cond)                                                          \
	do {                                                                 \
		fussy_checks++;                                              \
		if (!(cond)) {                                               \
			fussy_failures++;                                   \
			fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__,     \
				__LINE__, #cond);                           \
		}                                                           \
	} while (0)

#define CHECK_STR_EQ(a, b)                                                   \
	do {                                                                 \
		fussy_checks++;                                              \
		if (strcmp((a), (b)) != 0) {                                 \
			fussy_failures++;                                   \
			fprintf(stderr,                                     \
				"  FAIL %s:%d: \"%s\" != \"%s\"\n",         \
				__FILE__, __LINE__, (a), (b));              \
		}                                                           \
	} while (0)

#define RUN(fn)                                                              \
	do {                                                                 \
		fprintf(stderr, "test: %s\n", #fn);                         \
		fn();                                                       \
	} while (0)

#endif /* FUSSY_TEST_H */
