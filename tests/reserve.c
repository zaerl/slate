/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include <stdio.h>

#include "attractor.h"
#include "../src/slate.h"

ATT_TEST(reserve) {
    slate_s map;

    slate(&map);

    ATT_ASSERT(slate_reserve(NULL, 10), SLATE_ERROR_INVALID, "NULL slate reserve fails");
    ATT_ASSERT(slate_reserve(&map, 0), SLATE_OK, "reserving zero is a no-op");

    // A small reservation builds one right-sized table.
    ATT_ASSERT(slate_reserve(&map, 10), SLATE_OK, "a small reservation succeeds");
    ATT_ASSERT(map.dir_length, 1, "10 entries fit one table");
    ATT_ASSERT(map.dir[0]->capacity, 16, "the table holds 2 groups");
    ATT_ASSERT(map.dir[0]->grow_left >= 10, 1, "the insert budget covers the reservation");

    // Reserving again on a still-empty map replaces the layout.
    ATT_ASSERT(slate_reserve(&map, 1000), SLATE_OK, "re-reserving an empty map succeeds");
    ATT_ASSERT(map.dir_length, 2, "1000 entries need two tables");
    ATT_ASSERT(map.global_depth, 1, "the directory depth matches its length");
    ATT_ASSERT(map.dir[0]->capacity, 1024, "each table is sized for its share");

    // The reservation holds: 1000 inserts cause no split (the directory stays at depth 1) and no
    // growth (capacities are unchanged).
    char key[24];
    int failed = 0;

    for(int i = 0; i < 1000; ++i) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_set(&map, key, (void*)(uintptr_t)(i + 1)) != SLATE_OK) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "every reserved insert succeeds");
    ATT_ASSERT(slate_length(&map), 1000, "slate length matches the inserts");
    ATT_ASSERT(map.global_depth, 1, "no table split during the reserved inserts");
    ATT_ASSERT(map.dir[0]->capacity, 1024, "no table grew during the reserved inserts");
    ATT_ASSERT(map.dir[1]->capacity, 1024, "the second table did not grow either");

    // A populated map cannot be re-reserved.
    ATT_ASSERT(slate_reserve(&map, 5000), SLATE_ERROR_INVALID, "reserving a populated map fails");

    // Every key is still findable after the reserved fill.
    void* value = NULL;

    failed = 0;

    for(int i = 0; i < 1000; ++i) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_get(&map, key, &value) != SLATE_OK || value != (void*)(uintptr_t)(i + 1)) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "every key holds its value in the reserved map");

    ATT_ASSERT_NE(slate_free(&map), SLATE_ERROR_INVALID, "slate free should not fail");

    // After free the map is empty again and can be reserved.
    ATT_ASSERT(slate_reserve(&map, 50), SLATE_OK, "a freed map can be reserved");
    ATT_ASSERT_NE(slate_free(&map), SLATE_ERROR_INVALID, "freeing the reservation works");
}
