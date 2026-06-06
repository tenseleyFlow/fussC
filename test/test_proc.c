#include "proc.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

void test_proc_stdout(void)
{
	char *argv[] = {"echo", "hello", NULL};
	char *out = NULL, *err = NULL;
	int rc = proc_run(argv, &out, &err);
	CHECK(rc == 0);
	CHECK_STR_EQ(out, "hello\n");
	CHECK_STR_EQ(err, "");
	free(out);
	free(err);
}

void test_proc_stderr_and_exit(void)
{
	char *argv[] = {"sh", "-c", "echo out; echo problem >&2; exit 3", NULL};
	char *out = NULL, *err = NULL;
	int rc = proc_run(argv, &out, &err);
	CHECK(rc == 3); /* exit code captured */
	CHECK_STR_EQ(out, "out\n");
	CHECK_STR_EQ(err, "problem\n");
	free(out);
	free(err);
}

void test_proc_large_output(void)
{
	/* Far more than a pipe buffer, to prove the concurrent drain. */
	char *argv[] = {"sh", "-c", "yes x | head -c 200000", NULL};
	char *out = NULL, *err = NULL;
	int rc = proc_run(argv, &out, &err);
	CHECK(rc == 0);
	CHECK(strlen(out) == 200000);
	free(out);
	free(err);
}

void test_proc_spawn_failure(void)
{
	char *argv[] = {"fussy_no_such_program_xyz", NULL};
	/* Pre-seed with garbage to prove proc_run overwrites both regardless.
	 */
	char *out = (char *)1, *err = (char *)1;
	int rc = proc_run(argv, &out, &err);
	/* Either posix_spawnp fails (-1) or the child execs and exits 127. */
	CHECK(rc != 0);
	/* Contract: out/err are always assigned. On a spawn failure (-1) they
	 * are NULL; if the shell exec'd and exited 127 they are heap strings.
	 */
	if (rc < 0) {
		CHECK(out == NULL);
		CHECK(err == NULL);
	}
	free(out);
	free(err);
}
