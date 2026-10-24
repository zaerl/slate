/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include "attractor.h"
#include "../src/slate.h"

ATT_TEST(length) {
    slate_s map;

    ATT_ASSERT(slate_length(NULL), 0, "NULL slate length is 0");

    slate(&map);
    ATT_ASSERT(slate_length(&map), 0, "slate length is initially 0");
    ATT_ASSERT(map.dir_length, 0, "slate dirs length is initially 0");
    ATT_ASSERT(map.global_depth, 0, "slate global depth is initially 0");
}
