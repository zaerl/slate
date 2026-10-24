/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

// The test build routes every slate allocation through the counting allocator in helpers.c, for
// leak detection and failure injection. helpers.h only pulls in <stddef.h> and <stdint.h>, which
// slate.h itself includes before its feature-test macros.
#include "helpers.h"

#define SLATE_MALLOC(size) test_malloc(size)
#define SLATE_CALLOC(count, size) test_calloc(count, size)
#define SLATE_FREE(pointer) test_free(pointer)

// slate.h must precede other libc includes: its implementation defines feature-test macros that
// must come before the first libc header.
#define SLATE_IMPLEMENTATION
#include "../src/slate.h"

#define ATT_IMPLEMENTATION
#include "attractor.h"

#include <inttypes.h>
#include <stdio.h>

int main(void) {
    att_set_verbose(1);
    att_run_tests(NULL);

    int all_valid = att_report() == 0;

    // Every test frees its maps, so anything still allocated is a leak.
    if(test_alloc_live() != 0) {
        printf("Leak: %" PRId64 " allocation(s) never freed\n", test_alloc_live());
        all_valid = 0;
    }

    return all_valid ? 0 : -1;
}
