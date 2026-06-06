#ifndef FUSSY_PROC_H
#define FUSSY_PROC_H

/*
 * Run a child process and capture its output through pipes (never /tmp). Used
 * for the network git ops and anything else that shells out.
 */

/*
 * Spawn argv[0] (PATH-searched), wait for it, and capture stdout into *out and
 * stderr into *err. Both are always assigned: a heap string the caller frees on
 * success (possibly empty), or NULL when the child could not be spawned (e.g.
 * the binary is missing). Returns the child's exit status (0..255), or -1 if it
 * could not be spawned. argv is NULL-terminated. free(NULL) makes the caller's
 * cleanup uniform.
 */
int proc_run(char *const argv[], char **out, char **err);

#endif /* FUSSY_PROC_H */
