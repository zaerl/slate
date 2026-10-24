/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

// slate.h must precede other includes: its implementation defines feature-test macros that must come
// before the first libc header.
#define SLATE_IMPLEMENTATION
#include "../src/slate.h"

#define ATT_IMPLEMENTATION
#include "attractor.h"

int main(int argc, char *const argv[]) {
    att_set_verbose(1);
    att_run_tests(NULL);

    int all_valid = att_report() == 0;

    return all_valid ? 0 : -1;
}
