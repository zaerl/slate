/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include <stdio.h>

#include "attractor.h"
#include "../src/slate.h"

#define SPLIT_KEYS 3000

ATT_TEST(split) {
    slate_s map;
    slate(&map);

    char key[24];
    int failed = 0;

    // Enough inserts to push tables past the 1024-slot cap and split.
    for(int i = 0; i < SPLIT_KEYS; ++i) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_set(&map, key, (void*)(uintptr_t)(i + 1)) != 0) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "slate set never fails across splits");
    ATT_ASSERT(slate_length(&map), SPLIT_KEYS, "slate length matches the inserted keys");
    ATT_ASSERT(map.global_depth >= 1, 1, "the map split at least once");
    ATT_ASSERT(map.dir_length, 1 << map.global_depth, "directory length is 2^global_depth");

    // Audit the directory: entries aliasing one table are contiguous and number exactly
    // 2^(global_depth - local_depth), no table exceeds its 1024-slot cap, and the per-table counts
    // add up to the map length.
    uint64_t total = 0;
    int bad_alias = 0;
    int bad_capacity = 0;
    int bad_depth = 0;

    for(int i = 0; i < map.dir_length; ) {
        slate_table_s* table = map.dir[i];
        int aliases = 0;

        while(i + aliases < map.dir_length && map.dir[i + aliases] == table) {
            ++aliases;
        }

        if(table->local_depth > map.global_depth) {
            ++bad_depth;
        }

        if(aliases != 1 << (map.global_depth - table->local_depth)) {
            ++bad_alias;
        }

        if(table->capacity > 1024) {
            ++bad_capacity;
        }

        total += table->used;
        i += aliases;
    }

    ATT_ASSERT(bad_depth, 0, "every local depth is at most the global depth");
    ATT_ASSERT(bad_alias, 0, "every table has 2^(global-local) contiguous aliases");
    ATT_ASSERT(bad_capacity, 0, "no table exceeds 1024 slots");
    ATT_ASSERT(total, SPLIT_KEYS, "per-table counts add up to the map length");

    // Every key must still be findable: replacing never adds an entry.
    failed = 0;

    for(int i = 0; i < SPLIT_KEYS; ++i) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_set(&map, key, (void*)(uintptr_t)i) != 0) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "every key is found again after the splits");
    ATT_ASSERT(slate_length(&map), SPLIT_KEYS, "replacing keys never changes the length");

    ATT_ASSERT_NE(slate_free(&map), SLATE_ERROR_INVALID, "slate free should not fail");
}
