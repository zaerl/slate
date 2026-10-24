/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include "attractor.h"
#include "../src/slate.h"

ATT_TEST(random) {
    slate_s map_1, map_2;
    void* value = NULL;

    slate(&map_1);
    slate(&map_2);

    ATT_ASSERT_NE(map_1.seed, map_2.seed, "slate seeds are different");

    // A freed map is a valid empty map with a fresh seed, never seed 0.
    slate_set(&map_1, "key", (void*)1);
    slate_free(&map_1);

    ATT_ASSERT_NE(map_1.seed, 0, "slate free reseeds instead of zeroing");
    ATT_ASSERT_NE(map_1.seed, map_2.seed, "the reseeded map differs from other maps");

    ATT_ASSERT(slate_set(&map_1, "key", (void*)2), SLATE_OK, "slate set works after free");
    ATT_ASSERT(slate_get(&map_1, "key", &value), SLATE_OK, "the reused map finds its key");
    ATT_ASSERT(value == (void*)2, 1, "the reused map returns the new value");
    ATT_ASSERT(slate_length(&map_1), 1, "the reused map has length 1");

    ATT_ASSERT_NE(slate_free(&map_1), SLATE_ERROR_INVALID, "slate free should not fail");
    ATT_ASSERT_NE(slate_free(&map_2), SLATE_ERROR_INVALID, "slate free is safe on an empty map");
}
