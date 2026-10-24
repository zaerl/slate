/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include <stdio.h>

#include "attractor.h"
#include "../src/slate.h"

#define CLONE_KEYS 3000

typedef struct clone_check_s {
    slate_s* other;
    int mismatches;
} clone_check_s;

// Verifies that every visited entry exists in the other map with the same value.
static int clone_check(const char* key, uint32_t key_length, void* value, void* data) {
    clone_check_s* check = (clone_check_s*)data;
    void* other_value = NULL;

    if(slate_get_bytes(check->other, key, key_length, &other_value) != SLATE_OK
        || other_value != value) {
        ++check->mismatches;
    }

    return 0;
}

ATT_TEST(clone) {
    slate_s source;
    slate_s copy;
    void* value = NULL;

    slate(&source);

    ATT_ASSERT(slate_clone(NULL, &source), SLATE_ERROR_INVALID, "NULL destination fails");
    ATT_ASSERT(slate_clone(&copy, NULL), SLATE_ERROR_INVALID, "NULL source fails");
    ATT_ASSERT(slate_clone(&source, &source), SLATE_ERROR_INVALID, "self clone fails");

    // Cloning a lazy empty map yields a working empty map.
    ATT_ASSERT(slate_clone(&copy, &source), SLATE_OK, "an empty map clones");
    ATT_ASSERT(slate_length(&copy), 0, "the empty clone is empty");
    ATT_ASSERT(slate_set(&copy, "solo", (void*)1), SLATE_OK, "the empty clone works");
    ATT_ASSERT(slate_length(&source), 0, "the source stays empty");
    slate_free(&copy);

    // Fill through several splits and leave tombstones behind.
    char key[24];
    int failed = 0;

    for(int i = 0; i < CLONE_KEYS; ++i) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_set(&source, key, (void*)(uintptr_t)(i + 1)) != SLATE_OK) {
            ++failed;
        }
    }

    for(int i = 0; i < CLONE_KEYS; i += 5) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_delete(&source, key) != SLATE_OK) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "the fill and deletions succeed");
    ATT_ASSERT(slate_clone(&copy, &source), SLATE_OK, "a split map clones");

    // Structure and content match in both directions.
    ATT_ASSERT(slate_length(&copy), slate_length(&source), "the lengths match");
    ATT_ASSERT(copy.dir_length, source.dir_length, "the directory lengths match");
    ATT_ASSERT(copy.global_depth, source.global_depth, "the depths match");
    ATT_ASSERT(copy.seed == source.seed, 1, "the clone shares the seed");
    ATT_ASSERT(copy.dir != source.dir, 1, "the directories are distinct");
    ATT_ASSERT(copy.dir[0] != source.dir[0], 1, "the tables are distinct");

    clone_check_s check = { &copy, 0 };

    ATT_ASSERT(slate_each(&source, clone_check, &check), SLATE_OK, "the source iterates");
    ATT_ASSERT(check.mismatches, 0, "every source entry is in the clone");

    check.other = &source;
    ATT_ASSERT(slate_each(&copy, clone_check, &check), SLATE_OK, "the clone iterates");
    ATT_ASSERT(check.mismatches, 0, "every clone entry is in the source");
    ATT_ASSERT(slate_get(&copy, "key-0", &value), SLATE_ERROR_NOT_FOUND,
        "deleted entries stay deleted in the clone");

    // Full independence: changes on one side never show on the other.
    ATT_ASSERT(slate_set(&copy, "only-copy", (void*)7), SLATE_OK, "the clone accepts inserts");
    ATT_ASSERT(slate_get(&source, "only-copy", &value), SLATE_ERROR_NOT_FOUND,
        "the source does not see clone inserts");
    ATT_ASSERT(slate_delete(&source, "key-1"), SLATE_OK, "the source accepts deletes");
    ATT_ASSERT(slate_get(&copy, "key-1", &value), SLATE_OK,
        "the clone keeps entries the source deleted");
    ATT_ASSERT(slate_set(&copy, "key-2", (void*)99), SLATE_OK, "the clone replaces a value");
    ATT_ASSERT(slate_get(&source, "key-2", &value), SLATE_OK, "the source keeps its entry");
    ATT_ASSERT(value == (void*)3, 1, "the source value is untouched");

    // Both sides free independently: keys are owned per map.
    ATT_ASSERT_NE(slate_free(&copy), SLATE_ERROR_INVALID, "freeing the clone works");
    ATT_ASSERT(slate_get(&source, "key-2", &value), SLATE_OK,
        "the source survives the clone's free");
    ATT_ASSERT_NE(slate_free(&source), SLATE_ERROR_INVALID, "freeing the source works");
}
