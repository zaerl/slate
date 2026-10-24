/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include <stdio.h>

#include "attractor.h"
#include "../src/slate.h"

#define CLEAR_KEYS 3000

ATT_TEST(clear) {
    slate_s map;
    void* value = NULL;

    slate(&map);

    ATT_ASSERT(slate_clear(NULL), SLATE_ERROR_INVALID, "NULL slate clear fails");
    ATT_ASSERT(slate_clear(&map), SLATE_OK, "clearing a fresh map is a no-op");

    // Fill through several splits.
    char key[24];
    int failed = 0;

    for(int i = 0; i < CLEAR_KEYS; ++i) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_set(&map, key, (void*)(uintptr_t)(i + 1)) != SLATE_OK) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "the fill succeeds");

    int dir_length_before = map.dir_length;
    uint8_t depth_before = map.global_depth;
    uint16_t capacity_before = map.dir[0]->capacity;
    uint64_t seed_before = map.seed;

    // Clearing empties the map but keeps the whole structure.
    ATT_ASSERT(slate_clear(&map), SLATE_OK, "clearing a full map succeeds");
    ATT_ASSERT(slate_length(&map), 0, "a cleared map is empty");
    ATT_ASSERT(map.dir_length, dir_length_before, "the directory is kept");
    ATT_ASSERT(map.global_depth, depth_before, "the depth is kept");
    ATT_ASSERT(map.dir[0]->capacity, capacity_before, "table capacity is kept");
    ATT_ASSERT_NE(map.seed, seed_before, "clearing rotates the seed");
    ATT_ASSERT(slate_get(&map, "key-0", &value), SLATE_ERROR_NOT_FOUND,
        "a cleared key is no longer found");
    ATT_ASSERT(slate_delete(&map, "key-1"), SLATE_ERROR_NOT_FOUND,
        "a cleared key cannot be deleted");

    // The retained capacity absorbs a refill, and every value comes back.
    failed = 0;

    for(int i = 0; i < CLEAR_KEYS; ++i) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_set(&map, key, (void*)(uintptr_t)(i + 7)) != SLATE_OK) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "the refill succeeds");
    ATT_ASSERT(slate_length(&map), CLEAR_KEYS, "the refill restores the length");

    failed = 0;

    for(int i = 0; i < CLEAR_KEYS; ++i) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_get(&map, key, &value) != SLATE_OK || value != (void*)(uintptr_t)(i + 7)) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "every refilled key holds its new value");

    ATT_ASSERT_NE(slate_free(&map), SLATE_ERROR_INVALID, "slate free should not fail");
}
