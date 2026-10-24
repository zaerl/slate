/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include <stdio.h>
#include <string.h>

#include "attractor.h"
#include "helpers.h"
#include "../src/slate.h"

static int lifecycle_count(const char* key, uint32_t key_length, void* value, void* data) {
    (void)key;
    (void)key_length;
    (void)value;

    ++*(int*)data;

    return 0;
}

static int lifecycle_never(const char* key, uint32_t key_length, void* value, void* data) {
    (void)key;
    (void)key_length;
    (void)value;
    (void)data;

    return 0;
}

// Returns the value stored in `data` on the first call, counting calls in the following int.
static int lifecycle_stop_with(const char* key, uint32_t key_length, void* value, void* data) {
    int* state = (int*)data;

    (void)key;
    (void)key_length;
    (void)value;

    ++state[1];

    return state[0];
}

ATT_TEST(lifecycle) {
    int64_t live = test_alloc_live();
    slate_s map;
    slate_s other;
    void* value = NULL;
    int visits = 0;

    // Every entry point rejects NULL pointers without touching anything.
    slate(&map);
    slate_set(&map, "kept", (void*)1);

    int invalid = 0;

    invalid += slate(NULL) != SLATE_ERROR_INVALID;
    invalid += slate_reserve(NULL, 1) != SLATE_ERROR_INVALID;
    invalid += slate_clear(NULL) != SLATE_ERROR_INVALID;
    invalid += slate_compact(NULL) != SLATE_ERROR_INVALID;
    invalid += slate_set_bytes(NULL, "k", 1, NULL) != SLATE_ERROR_INVALID;
    invalid += slate_set_bytes(&map, NULL, 0, NULL) != SLATE_ERROR_INVALID;
    invalid += slate_get_bytes(NULL, "k", 1, &value) != SLATE_ERROR_INVALID;
    invalid += slate_get_bytes(&map, NULL, 0, &value) != SLATE_ERROR_INVALID;
    invalid += slate_delete_bytes(NULL, "k", 1) != SLATE_ERROR_INVALID;
    invalid += slate_delete_bytes(&map, NULL, 0) != SLATE_ERROR_INVALID;
    invalid += slate_set(NULL, "k", NULL) != SLATE_ERROR_INVALID;
    invalid += slate_set(&map, NULL, NULL) != SLATE_ERROR_INVALID;
    invalid += slate_set_i64(NULL, 1, NULL) != SLATE_ERROR_INVALID;
    invalid += slate_get_i64(NULL, 1, &value) != SLATE_ERROR_INVALID;
    invalid += slate_delete_i64(NULL, 1) != SLATE_ERROR_INVALID;
    invalid += slate_set_u64(NULL, 1, NULL) != SLATE_ERROR_INVALID;
    invalid += slate_get_u64(NULL, 1, &value) != SLATE_ERROR_INVALID;
    invalid += slate_delete_u64(NULL, 1) != SLATE_ERROR_INVALID;
    invalid += slate_each(NULL, lifecycle_count, &visits) != SLATE_ERROR_INVALID;
    invalid += slate_each(&map, NULL, NULL) != SLATE_ERROR_INVALID;
    invalid += slate_delete_if(NULL, lifecycle_never, NULL) != SLATE_ERROR_INVALID;
    invalid += slate_delete_if(&map, NULL, NULL) != SLATE_ERROR_INVALID;
    invalid += slate_free(NULL) != SLATE_ERROR_INVALID;

    ATT_ASSERT(invalid, 0, "every function rejects NULL pointers");
    ATT_ASSERT(visits, 0, "a rejected call never runs the callback");
    ATT_ASSERT(slate_length(&map), 1, "rejected calls leave the map alone");
    ATT_ASSERT(slate_get(&map, "kept", &value), SLATE_OK, "the entry survives rejected calls");

    // An absurd reservation is refused and the map stays usable.
    slate_free(&map);
    ATT_ASSERT(slate_reserve(&map, UINT64_MAX), SLATE_ERROR_INVALID,
        "reserving 2^64 entries is refused");
    ATT_ASSERT(slate_reserve(&map, UINT64_MAX - 100), SLATE_ERROR_INVALID,
        "a count that would wrap the sizing arithmetic is refused");
    ATT_ASSERT(slate_reserve(&map, ((uint64_t)1 << 30) * 896 + 1), SLATE_ERROR_INVALID,
        "one entry past 2^30 full tables is refused");
    ATT_ASSERT(map.dir == NULL, 1, "a refused reservation allocates nothing");
    ATT_ASSERT(slate_set(&map, "after", (void*)1), SLATE_OK,
        "the map works after a refused reservation");
    slate_free(&map);

    // A freed map behaves as an empty map everywhere, and freeing twice is harmless.
    ATT_ASSERT(slate_free(&map), SLATE_OK, "freeing a freed map succeeds");
    ATT_ASSERT(slate_length(&map), 0, "a freed map is empty");
    ATT_ASSERT(slate_get(&map, "after", &value), SLATE_ERROR_NOT_FOUND,
        "a freed map finds nothing");
    ATT_ASSERT(slate_delete(&map, "after"), SLATE_ERROR_NOT_FOUND,
        "a freed map deletes nothing");
    ATT_ASSERT(slate_each(&map, lifecycle_count, &visits), SLATE_OK, "a freed map iterates");
    ATT_ASSERT(visits, 0, "a freed map visits nothing");
    ATT_ASSERT(slate_delete_if(&map, lifecycle_count, &visits), SLATE_OK,
        "delete_if works on a freed map");
    ATT_ASSERT(slate_clear(&map), SLATE_OK, "clearing a freed map succeeds");
    ATT_ASSERT(slate_compact(&map), SLATE_OK, "compacting a freed map succeeds");
    ATT_ASSERT(slate_clone(&other, &map), SLATE_OK, "a freed map clones");
    ATT_ASSERT(slate_length(&other), 0, "the clone of a freed map is empty");
    ATT_ASSERT(slate_equal(&map, &other), 1, "a freed map equals an empty map");
    ATT_ASSERT(slate_free(&other), SLATE_OK, "freeing the empty clone succeeds");
    ATT_ASSERT(slate_reserve(&map, 100), SLATE_OK, "a freed map can be reserved");
    ATT_ASSERT(test_check(&map), 0, "the reserved map is sound");

    // A map emptied by deletions keeps its tables and stays fully usable.
    slate_set(&map, "x", (void*)1);
    slate_delete(&map, "x");
    ATT_ASSERT(map.dir != NULL, 1, "a drained map keeps its directory");
    ATT_ASSERT(slate_clone(&other, &map), SLATE_OK, "a drained map clones");
    ATT_ASSERT(slate_equal(&map, &other), 1, "a drained map equals its clone");
    ATT_ASSERT(test_check(&other), 0, "the drained clone is sound");
    slate_free(&other);
    ATT_ASSERT(slate_compact(&map), SLATE_OK, "a drained map compacts");
    ATT_ASSERT(map.dir == NULL, 1, "compacting a drained map returns it to the lazy state");

    // The iteration callback's value is returned as-is, whatever its sign, after a single call.
    int stop[2] = { -7, 0 };

    slate_set(&map, "a", (void*)1);
    slate_set(&map, "b", (void*)2);
    ATT_ASSERT(slate_each(&map, lifecycle_stop_with, stop), -7,
        "a negative callback value stops the iteration and is returned");
    ATT_ASSERT(stop[1], 1, "a negative value stops after one call");

    stop[0] = 3;
    stop[1] = 0;
    ATT_ASSERT(slate_each(&map, lifecycle_stop_with, stop), 3,
        "a positive callback value stops the iteration and is returned");
    ATT_ASSERT(stop[1], 1, "a positive value stops after one call");

    // A predicate that keeps everything changes nothing.
    ATT_ASSERT(slate_delete_if(&map, lifecycle_never, NULL), SLATE_OK,
        "a keep-all predicate works");
    ATT_ASSERT(slate_length(&map), 2, "a keep-all predicate deletes nothing");

    // Maps of equal length but different keys, or one differing NULL value, are not equal.
    slate(&other);
    slate_set(&other, "a", (void*)1);
    slate_set(&other, "c", (void*)2);
    ATT_ASSERT(slate_equal(&map, &other), 0, "maps with different keys are not equal");
    slate_delete(&other, "c");
    slate_set(&other, "b", NULL);
    ATT_ASSERT(slate_equal(&map, &other), 0, "a NULL value differs from a non-NULL one");
    slate_set(&other, "b", (void*)2);
    ATT_ASSERT(slate_equal(&map, &other), 1, "matching the value restores equality");

    // A destination that was used and freed can receive a clone.
    slate_free(&other);
    ATT_ASSERT(slate_clone(&other, &map), SLATE_OK, "a freed map can be a clone destination");
    ATT_ASSERT(slate_equal(&map, &other), 1, "the clone into a freed map is equal");
    slate_free(&other);

    ATT_ASSERT_NE(slate_free(&map), SLATE_ERROR_INVALID, "slate free should not fail");
    ATT_ASSERT(test_alloc_live(), live, "the lifecycle leaks nothing");

    // The version string spells out its components.
    char version[32];

    snprintf(version, sizeof(version), "%d.%d.%d", SLATE_VERSION_MAJOR, SLATE_VERSION_MINOR,
        SLATE_VERSION_REVISION);
    ATT_ASSERT(strcmp(version, SLATE_VERSION), 0, "SLATE_VERSION matches its components");
}
