/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include <stdio.h>

#include "attractor.h"
#include "../src/slate.h"

#define DELETE_KEYS 3000

ATT_TEST(delete) {
    slate_s map;
    void* value = NULL;

    slate(&map);

    ATT_ASSERT(slate_delete(NULL, "key"), SLATE_ERROR_INVALID, "NULL slate delete fails");
    ATT_ASSERT(slate_delete(&map, NULL), SLATE_ERROR_INVALID, "NULL key delete fails");
    ATT_ASSERT(slate_delete(&map, "missing"), SLATE_ERROR_NOT_FOUND,
        "an empty slate has nothing to delete");

    slate_set(&map, "key", (void*)42);
    ATT_ASSERT(slate_delete(&map, "key"), SLATE_OK, "an inserted key is deleted");
    ATT_ASSERT(slate_length(&map), 0, "slate length drops back to 0");
    ATT_ASSERT(slate_get(&map, "key", &value), SLATE_ERROR_NOT_FOUND,
        "a deleted key is no longer found");
    ATT_ASSERT(slate_delete(&map, "key"), SLATE_ERROR_NOT_FOUND, "deleting the same key twice fails");

    // A deleted key can be inserted again with a new value.
    slate_set(&map, "key", (void*)43);
    ATT_ASSERT(slate_get(&map, "key", &value), SLATE_OK, "a re-inserted key is found");
    ATT_ASSERT(value == (void*)43, 1, "the re-inserted value is returned");
    ATT_ASSERT(slate_delete(&map, "key"), SLATE_OK, "the re-inserted key is deleted");

    // Delete every other key from a map that has grown and split, then verify survivors, victims,
    // and the length all agree.
    char key[24];
    int failed = 0;

    for(int i = 0; i < DELETE_KEYS; ++i) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_set(&map, key, (void*)(uintptr_t)(i + 1)) != 0) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "slate set never fails while filling the map");

    failed = 0;

    for(int i = 0; i < DELETE_KEYS; i += 2) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_delete(&map, key) != 0) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "every even key is deleted");
    ATT_ASSERT(slate_length(&map), DELETE_KEYS / 2, "slate length halves after the deletions");

    failed = 0;

    for(int i = 0; i < DELETE_KEYS; ++i) {
        snprintf(key, sizeof(key), "key-%d", i);

        int rc = slate_get(&map, key, &value);

        if(i % 2 == 0) {
            if(rc != 1) {
                ++failed;
            }
        } else if(rc != 0 || value != (void*)(uintptr_t)(i + 1)) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "deleted keys are gone and surviving keys keep their values");

    // Re-setting survivors must replace, never duplicate: deletions may have turned slots before a
    // survivor in its group back to empty.
    failed = 0;

    for(int i = 1; i < DELETE_KEYS; i += 2) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_set(&map, key, (void*)(uintptr_t)(i + 1)) != 0) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "every surviving key can be re-set");
    ATT_ASSERT(slate_length(&map), DELETE_KEYS / 2, "re-setting survivors never duplicates");

    // Deleted slots are reusable: the map absorbs the same keys again.
    failed = 0;

    for(int i = 0; i < DELETE_KEYS; i += 2) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_set(&map, key, (void*)(uintptr_t)(i + 100)) != 0) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "every deleted key can be inserted again");
    ATT_ASSERT(slate_length(&map), DELETE_KEYS, "slate length is restored after re-inserts");

    // Sustained delete/insert churn stays exact: lengths never drift and every key keeps its latest
    // value.
    failed = 0;

    for(int round = 0; round < 5 && failed == 0; ++round) {
        for(int i = 0; i < DELETE_KEYS; i += 7) {
            snprintf(key, sizeof(key), "key-%d", i);

            if(slate_delete(&map, key) != SLATE_OK) {
                ++failed;
            }
        }

        for(int i = 0; i < DELETE_KEYS; i += 7) {
            snprintf(key, sizeof(key), "key-%d", i);

            if(slate_set(&map, key, (void*)(uintptr_t)(i + round)) != SLATE_OK) {
                ++failed;
            }
        }

        if(slate_length(&map) != DELETE_KEYS) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "delete/insert churn keeps the map exact");

    for(int i = 0; i < DELETE_KEYS; i += 7) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_get(&map, key, &value) != SLATE_OK || value != (void*)(uintptr_t)(i + 4)) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "churned keys hold their latest value");

    ATT_ASSERT_NE(slate_free(&map), SLATE_ERROR_INVALID, "slate free should not fail");
}
