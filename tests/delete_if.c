/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include <stdio.h>
#include <string.h>

#include "attractor.h"
#include "../src/slate.h"

#define DELETE_IF_KEYS 3000

// Deletes entries with an even value, verifying key/value pairing.
static int delete_even(const char* key, uint32_t key_length, void* value, void* data) {
    int* bad = (int*)data;
    uintptr_t index = (uintptr_t)value;
    char expected[24];

    snprintf(expected, sizeof(expected), "key-%d", (int)(index - 1));

    if(strcmp(expected, key) != 0 || key_length != (uint32_t)strlen(key)) {
        ++*bad;
    }

    return (index - 1) % 2 == 0;
}

static int keep_all(const char* key, uint32_t key_length, void* value, void* data) {
    (void)key;
    (void)key_length;
    (void)value;
    (void)data;

    return 0;
}

static int delete_all(const char* key, uint32_t key_length, void* value, void* data) {
    (void)key;
    (void)key_length;
    (void)value;
    (void)data;

    return 1;
}

ATT_TEST(delete_if) {
    slate_s map;
    void* value = NULL;
    int bad = 0;

    slate(&map);

    ATT_ASSERT(slate_delete_if(NULL, keep_all, NULL), SLATE_ERROR_INVALID,
        "NULL slate delete_if fails");
    ATT_ASSERT(slate_delete_if(&map, NULL, NULL), SLATE_ERROR_INVALID,
        "NULL predicate fails");
    ATT_ASSERT(slate_delete_if(&map, delete_all, NULL), SLATE_OK,
        "an empty slate deletes nothing cleanly");

    // Fill through several splits.
    char key[24];
    int failed = 0;

    for(int i = 0; i < DELETE_IF_KEYS; ++i) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_set(&map, key, (void*)(uintptr_t)(i + 1)) != SLATE_OK) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "the fill succeeds");

    // A keep-everything predicate deletes nothing.
    ATT_ASSERT(slate_delete_if(&map, keep_all, NULL), SLATE_OK, "keep-all succeeds");
    ATT_ASSERT(slate_length(&map), DELETE_IF_KEYS, "keep-all deletes nothing");

    // Deleting the even-indexed half in one pass.
    ATT_ASSERT(slate_delete_if(&map, delete_even, &bad), SLATE_OK, "delete-even succeeds");
    ATT_ASSERT(bad, 0, "the predicate saw consistent key/value pairs");
    ATT_ASSERT(slate_length(&map), DELETE_IF_KEYS / 2, "half the entries are deleted");

    // Victims are gone, survivors keep their values, and the map works.
    failed = 0;

    for(int i = 0; i < DELETE_IF_KEYS; ++i) {
        snprintf(key, sizeof(key), "key-%d", i);

        int rc = slate_get(&map, key, &value);

        if(i % 2 == 0) {
            if(rc != SLATE_ERROR_NOT_FOUND) {
                ++failed;
            }
        } else if(rc != SLATE_OK || value != (void*)(uintptr_t)(i + 1)) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "victims are gone and survivors keep their values");
    ATT_ASSERT(slate_set(&map, "key-0", (void*)9), SLATE_OK, "a deleted key can return");
    ATT_ASSERT(slate_length(&map), DELETE_IF_KEYS / 2 + 1, "the length stays consistent");

    // Delete-all empties the map and rotates the seed.
    uint64_t seed_before = map.seed;

    ATT_ASSERT(slate_delete_if(&map, delete_all, NULL), SLATE_OK, "delete-all succeeds");
    ATT_ASSERT(slate_length(&map), 0, "delete-all empties the map");
    ATT_ASSERT_NE(map.seed, seed_before, "emptying the map rotates the seed");
    ATT_ASSERT(slate_set(&map, "again", (void*)1), SLATE_OK, "the emptied map still works");

    ATT_ASSERT_NE(slate_free(&map), SLATE_ERROR_INVALID, "slate free should not fail");
}
