/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include <stdio.h>
#include <string.h>

#include "attractor.h"
#include "../src/slate.h"

#define EACH_KEYS 3000

typedef struct each_state_s {
    unsigned char seen[EACH_KEYS];
    int visits;
    int duplicates;
    int bad;
} each_state_s;

// Marks each visited entry and checks the key text matches its value.
static int each_visit(const char* key, uint32_t key_length, void* value, void* data) {
    each_state_s* state = (each_state_s*)data;
    uintptr_t index = (uintptr_t)value;
    char expected[24];

    ++state->visits;

    if(index < 1 || index > EACH_KEYS) {
        ++state->bad;

        return 0;
    }

    if(state->seen[index - 1]) {
        ++state->duplicates;
    }

    state->seen[index - 1] = 1;
    snprintf(expected, sizeof(expected), "key-%d", (int)(index - 1));

    if(strcmp(expected, key) != 0 || key_length != (uint32_t)strlen(key)) {
        ++state->bad;
    }

    return 0;
}

// Stops the iteration with value 7 on the third visit.
static int each_stop(const char* key, uint32_t key_length, void* value, void* data) {
    int* count = (int*)data;

    (void)key;
    (void)key_length;
    (void)value;

    ++*count;

    return *count == 3 ? 7 : 0;
}

ATT_TEST(each) {
    slate_s map;
    each_state_s state = { { 0 }, 0, 0, 0 };

    slate(&map);

    ATT_ASSERT(slate_each(NULL, each_visit, &state), SLATE_ERROR_INVALID, "NULL slate each fails");
    ATT_ASSERT(slate_each(&map, NULL, &state), SLATE_ERROR_INVALID, "NULL callback fails");

    ATT_ASSERT(slate_each(&map, each_visit, &state), SLATE_OK, "an empty slate iterates cleanly");
    ATT_ASSERT(state.visits, 0, "an empty slate visits nothing");

    // Fill across growth and splits, then visit everything exactly once.
    char key[24];
    int failed = 0;

    for(int i = 0; i < EACH_KEYS; ++i) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_set(&map, key, (void*)(uintptr_t)(i + 1)) != SLATE_OK) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "slate set never fails while filling the map");
    ATT_ASSERT(slate_each(&map, each_visit, &state), SLATE_OK, "a full iteration returns SLATE_OK");
    ATT_ASSERT(state.visits, EACH_KEYS, "every entry is visited");
    ATT_ASSERT(state.duplicates, 0, "no entry is visited twice");
    ATT_ASSERT(state.bad, 0, "every visited key matches its value");

    int seen_all = 1;

    for(int i = 0; i < EACH_KEYS; ++i) {
        if(!state.seen[i]) {
            seen_all = 0;
        }
    }

    ATT_ASSERT(seen_all, 1, "no entry is skipped");

    // A non-zero callback return stops the iteration and is passed back.
    int stop_count = 0;

    ATT_ASSERT(slate_each(&map, each_stop, &stop_count), 7, "the callback's value is returned");
    ATT_ASSERT(stop_count, 3, "the iteration stops immediately");

    // Deleted entries are not visited.
    failed = 0;

    for(int i = 0; i < 100; ++i) {
        snprintf(key, sizeof(key), "key-%d", i);

        if(slate_delete(&map, key) != SLATE_OK) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "the first 100 keys are deleted");

    memset(&state, 0, sizeof(state));
    ATT_ASSERT(slate_each(&map, each_visit, &state), SLATE_OK, "iteration works after deletions");
    ATT_ASSERT(state.visits, EACH_KEYS - 100, "deleted entries are not visited");
    ATT_ASSERT(state.bad, 0, "surviving keys still match their values");

    int deleted_seen = 0;

    for(int i = 0; i < 100; ++i) {
        if(state.seen[i]) {
            deleted_seen = 1;
        }
    }

    ATT_ASSERT(deleted_seen, 0, "no deleted entry is visited");

    ATT_ASSERT_NE(slate_free(&map), SLATE_ERROR_INVALID, "slate free should not fail");
}
