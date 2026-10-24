/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include <stdio.h>

#include "attractor.h"
#include "../src/slate.h"

#define COMPACT_KEYS 3000

// Sum of the distinct tables' capacities.
static uint64_t total_capacity(slate_s* map) {
    uint64_t total = 0;

    for(int d = 0; d < map->dir_length; ++d) {
        if(d > 0 && map->dir[d] == map->dir[d - 1]) {
            continue;
        }

        total += map->dir[d]->capacity;
    }

    return total;
}

ATT_TEST(compact) {
    slate_s map;
    void* value = NULL;

    slate(&map);

    ATT_ASSERT(slate_compact(NULL), SLATE_ERROR_INVALID, "NULL slate compact fails");
    ATT_ASSERT(slate_compact(&map), SLATE_OK, "compacting a fresh map is a no-op");

    // Fill through several splits, then delete most entries.
    char key[24];
    int failed = 0;

    for(int i = 0; i < COMPACT_KEYS; ++i) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_set(&map, key, (void*)(uintptr_t)(i + 1)) != SLATE_OK) {
            ++failed;
        }
    }

    for(int i = 0; i < COMPACT_KEYS; ++i) {
        if(i % 10 == 0) {
            continue;
        }

        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_delete(&map, key) != SLATE_OK) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "the fill and mass deletion succeed");
    ATT_ASSERT(slate_length(&map), COMPACT_KEYS / 10, "a tenth of the entries survive");

    uint64_t capacity_before = total_capacity(&map);
    uint64_t seed_before = map.seed;

    ATT_ASSERT(slate_compact(&map), SLATE_OK, "compacting the shrunken map succeeds");
    ATT_ASSERT(slate_length(&map), COMPACT_KEYS / 10, "compaction preserves the length");
    ATT_ASSERT(total_capacity(&map) < capacity_before, 1, "compaction releases capacity");
    ATT_ASSERT_NE(map.seed, seed_before, "compaction rotates the seed");

    // Every survivor keeps its value; every victim stays gone.
    failed = 0;

    for(int i = 0; i < COMPACT_KEYS; ++i) {
        snprintf(key, sizeof(key), "key-%d", i);

        int rc = slate_get(&map, key, &value);

        if(i % 10 == 0) {
            if(rc != SLATE_OK || value != (void*)(uintptr_t)(i + 1)) {
                ++failed;
            }
        } else if(rc != SLATE_ERROR_NOT_FOUND) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "survivors keep their values and victims stay gone");

    // The compacted map keeps working: inserts, replaces, deletes.
    ATT_ASSERT(slate_set(&map, "key-0", (void*)99), SLATE_OK, "a survivor can be replaced");
    ATT_ASSERT(slate_set(&map, "fresh", (void*)100), SLATE_OK, "a new key can be inserted");
    ATT_ASSERT(slate_delete(&map, "key-10"), SLATE_OK, "a survivor can be deleted");
    ATT_ASSERT(slate_length(&map), COMPACT_KEYS / 10, "the length stays consistent");

    // Deleting everything and compacting returns to the lazy empty state.
    failed = 0;

    for(int i = 0; i < COMPACT_KEYS; i += 10) {
        snprintf(key, sizeof(key), "key-%d", i);
        slate_delete(&map, key);
    }

    slate_delete(&map, "fresh");
    ATT_ASSERT(slate_length(&map), 0, "the map empties completely");
    ATT_ASSERT(slate_compact(&map), SLATE_OK, "compacting an empty map succeeds");
    ATT_ASSERT(map.dir_length, 0, "an empty compaction releases the directory");
    ATT_ASSERT(slate_set(&map, "again", (void*)1), SLATE_OK, "the emptied map still works");

    ATT_ASSERT_NE(slate_free(&map), SLATE_ERROR_INVALID, "slate free should not fail");
}
