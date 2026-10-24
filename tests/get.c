/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include <stdio.h>

#include "attractor.h"
#include "../src/slate.h"

#define GET_KEYS 3000

ATT_TEST(get) {
    slate_s map;
    void* value = NULL;

    slate(&map);

    ATT_ASSERT(slate_get(NULL, "key", &value), SLATE_ERROR_INVALID, "NULL slate get fails");
    ATT_ASSERT(slate_get(&map, NULL, &value), SLATE_ERROR_INVALID, "NULL key get fails");
    ATT_ASSERT(slate_get(&map, "missing", &value), SLATE_ERROR_NOT_FOUND,
        "an empty slate has no keys");

    slate_set(&map, "key", (void*)42);
    ATT_ASSERT(slate_get(&map, "key", &value), SLATE_OK, "an inserted key is found");
    ATT_ASSERT(value == (void*)42, 1, "the stored value is returned");

    slate_set(&map, "key", (void*)43);
    ATT_ASSERT(slate_get(&map, "key", &value), SLATE_OK, "a replaced key is found");
    ATT_ASSERT(value == (void*)43, 1, "the replaced value is returned");

    ATT_ASSERT(slate_get(&map, "missing", &value), SLATE_ERROR_NOT_FOUND,
        "a missing key is not found");
    ATT_ASSERT(slate_get(&map, "key", NULL), SLATE_OK,
        "a NULL value pointer only checks existence");

    // Every key keeps returning its own value across growth and splits.
    char key[24];
    int failed = 0;

    for(int i = 0; i < GET_KEYS; ++i) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_set(&map, key, (void*)(uintptr_t)(i + 1)) != 0) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "slate set never fails while filling the map");

    failed = 0;

    for(int i = 0; i < GET_KEYS; ++i) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_get(&map, key, &value) != 0 || value != (void*)(uintptr_t)(i + 1)) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "every key returns its value after growth and splits");
    ATT_ASSERT(slate_get(&map, "never-inserted", &value), SLATE_ERROR_NOT_FOUND,
        "a missing key stays missing");

    ATT_ASSERT_NE(slate_free(&map), SLATE_ERROR_INVALID, "slate free should not fail");
}
