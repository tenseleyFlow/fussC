/*
 * libFuzzer harness for the path parser. Feeds an arbitrary byte string as a
 * repo-relative path into tree_add, which splits on '/', builds cumulative
 * paths, and copies components into bounded buffers - exactly the kind of code
 * that breaks on long paths, empty/doubled slashes, and multi-byte sequences.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tree.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *path = malloc(size + 1);
	if (size)
		memcpy(path, data, size);
	path[size] = '\0';

	tree t;
	tree_init(&t);
	tree_add(&t, path, 0);
	/* A second add exercises the find-or-create merge path. */
	tree_add(&t, path, 1);
	tree_free(&t);

	free(path);
	return 0;
}
