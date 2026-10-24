/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include <string.h>

#include "attractor.h"
#include "../src/slate.h"

ATT_TEST(set) {
    slate_s map;
    slate(&map);

    ATT_ASSERT_NE(slate_set(&map, "key", (void*)1), SLATE_ERROR_INVALID, "slate set should not fail");
    ATT_ASSERT(slate_length(&map), 1, "slate length is 1 after first set");

    ATT_ASSERT(slate_set(&map, "key", (void*)2), SLATE_OK,
        "slate set replaces an existing key");
    ATT_ASSERT(slate_length(&map), 1, "slate length is still 1 after replace");

    char buffer[4] = "tmp";

    ATT_ASSERT(slate_set(&map, buffer, (void*)3), SLATE_OK,
        "slate set accepts a stack buffer key");
    memset(buffer, 0, sizeof(buffer));
    ATT_ASSERT(slate_set(&map, "tmp", (void*)4), SLATE_OK, "slate set finds the copied key");
    ATT_ASSERT(slate_length(&map), 2, "slate length is 2 after buffer key replace");

    const char* keys[] = { "k1", "k2", "k3", "k4", "k5", "k6" };

    for(int i = 0; i < 6; ++i) {
        ATT_ASSERT(slate_set(&map, keys[i], (void*)0), SLATE_OK,
            "slate set fills a free slot");
    }

    ATT_ASSERT(slate_length(&map), 8, "slate length is 8 when the group is full");

    // The 9th insert doubles the table instead of failing.
    ATT_ASSERT(slate_set(&map, "overflow", (void*)0), SLATE_OK,
        "slate set grows a full table");
    ATT_ASSERT(slate_length(&map), 9, "slate length is 9 after growth");

    // Keys redistributed by the growth are still found.
    ATT_ASSERT(slate_set(&map, "key", (void*)5), SLATE_OK,
        "slate set replaces a redistributed key");
    ATT_ASSERT(slate_length(&map), 9, "slate length is still 9 after replace");

    ATT_ASSERT_NE(slate_free(&map), SLATE_ERROR_INVALID, "slate free should not fail");
}
