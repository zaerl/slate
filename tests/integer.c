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

// Checks that every key `slate_each` reports for an integer entry has the 10-byte encoding: a NUL,
// then 'i' or 'u'.
static int integer_check_encoding(const char* key, uint32_t key_length, void* value, void* data) {
    (void)value;

    if(key_length == 10 && key[0] == '\0' && (key[1] == 'i' || key[1] == 'u')) {
        ++*(int*)data;
    }

    return 0;
}

ATT_TEST(integer_keys) {
    slate_s map;
    void* value = NULL;

    slate(&map);

    // Boundary values in both keyspaces.
    const int64_t signed_keys[] = { INT64_MIN, INT64_MIN + 1, -1, 0, 1, INT64_MAX - 1, INT64_MAX };
    const uint64_t unsigned_keys[] = { 0, 1, (uint64_t)1 << 63, UINT64_MAX - 1, UINT64_MAX };
    const int signed_count = (int)(sizeof(signed_keys) / sizeof(signed_keys[0]));
    const int unsigned_count = (int)(sizeof(unsigned_keys) / sizeof(unsigned_keys[0]));
    int failed = 0;

    for(int i = 0; i < signed_count; ++i) {
        failed += slate_set_i64(&map, signed_keys[i], (void*)(uintptr_t)(100 + i)) != SLATE_OK;
    }

    for(int i = 0; i < unsigned_count; ++i) {
        failed += slate_set_u64(&map, unsigned_keys[i], (void*)(uintptr_t)(200 + i)) != SLATE_OK;
    }

    ATT_ASSERT(failed, 0, "boundary integer keys are stored");

    // Signed -1 and unsigned UINT64_MAX share a bit pattern, as do signed and unsigned 0 and 1, yet
    // the tag keeps them apart.
    ATT_ASSERT(slate_length(&map), signed_count + unsigned_count,
        "equal bit patterns in the two keyspaces are distinct entries");

    failed = 0;

    for(int i = 0; i < signed_count; ++i) {
        failed += slate_get_i64(&map, signed_keys[i], &value) != SLATE_OK
            || value != (void*)(uintptr_t)(100 + i);
    }

    for(int i = 0; i < unsigned_count; ++i) {
        failed += slate_get_u64(&map, unsigned_keys[i], &value) != SLATE_OK
            || value != (void*)(uintptr_t)(200 + i);
    }

    ATT_ASSERT(failed, 0, "every boundary key holds its own value");

    int encoded = 0;

    slate_each(&map, integer_check_encoding, &encoded);
    ATT_ASSERT(encoded, signed_count + unsigned_count,
        "iteration reports integer keys in their 10-byte encoding");

    // Deleting from one keyspace never touches the other.
    ATT_ASSERT(slate_delete_u64(&map, UINT64_MAX), SLATE_OK, "an unsigned key is deleted");
    ATT_ASSERT(slate_delete_u64(&map, UINT64_MAX), SLATE_ERROR_NOT_FOUND,
        "a deleted unsigned key is gone");
    ATT_ASSERT(slate_get_i64(&map, -1, &value), SLATE_OK,
        "the signed key with the same bits survives");
    ATT_ASSERT(slate_delete_i64(&map, 0), SLATE_OK, "signed zero is deleted");
    ATT_ASSERT(slate_get_u64(&map, 0, &value), SLATE_OK, "unsigned zero survives");
    ATT_ASSERT(slate_delete_u64(&map, 12345), SLATE_ERROR_NOT_FOUND,
        "deleting a missing unsigned key is not found");
    ATT_ASSERT(slate_delete_i64(&map, 12345), SLATE_ERROR_NOT_FOUND,
        "deleting a missing signed key is not found");

    // String keys can never collide with integer keys: strings have no NUL byte.
    char text[24];

    failed = 0;
    slate_free(&map);

    for(int i = 0; i < 1000; ++i) {
        snprintf(text, sizeof(text), "%d", i);
        failed += slate_set(&map, text, (void*)1) != SLATE_OK;
        failed += slate_set_i64(&map, i, (void*)2) != SLATE_OK;
        failed += slate_set_u64(&map, (uint64_t)i, (void*)3) != SLATE_OK;
    }

    ATT_ASSERT(failed, 0, "the same number in three keyspaces is stored");
    ATT_ASSERT(slate_length(&map), 3000, "the three keyspaces never collide");

    // The encoding is documented, so raw bytes can address an integer key on purpose.
    unsigned char raw[10] = { 0x00, 'u' };
    uint64_t bits = 7;

    memcpy(raw + 2, &bits, sizeof(bits));
    ATT_ASSERT(slate_get_bytes(&map, raw, sizeof(raw), &value), SLATE_OK,
        "the documented encoding finds an unsigned key");
    ATT_ASSERT(value == (void*)3, 1, "the encoded key is the unsigned key");

    raw[1] = 'i';
    ATT_ASSERT(slate_get_bytes(&map, raw, sizeof(raw), &value), SLATE_OK,
        "the documented encoding finds a signed key");
    ATT_ASSERT(value == (void*)2, 1, "the encoded key is the signed key");

    ATT_ASSERT(test_check(&map), 0, "the map is sound with mixed keyspaces");
    ATT_ASSERT_NE(slate_free(&map), SLATE_ERROR_INVALID, "slate free should not fail");
}
