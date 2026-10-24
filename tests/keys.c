/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include <stdlib.h>
#include <string.h>

#include "attractor.h"
#include "helpers.h"
#include "../src/slate.h"

// Longest key in the length sweep: past xxHash64's 32-byte stripes, so every combination of stripe,
// 8-byte, 4-byte and 1-byte tail is hashed.
#define KEYS_MAX_LENGTH 100

// Fill `buffer` with `length` bytes that depend on `seed`, so keys of one length differ.
static void keys_fill(unsigned char* buffer, uint32_t length, uint64_t seed) {
    uint64_t state = seed;

    for(uint32_t i = 0; i < length; ++i) {
        buffer[i] = (unsigned char)test_random(&state);
    }
}

// Counts how many keys `slate_each` reports per length.
static int keys_count_lengths(const char* key, uint32_t key_length, void* value, void* data) {
    (void)key;
    (void)value;

    if(key_length <= KEYS_MAX_LENGTH) {
        ++((int*)data)[key_length];
    }

    return 0;
}

ATT_TEST(keys) {
    slate_s map;
    unsigned char buffer[KEYS_MAX_LENGTH];
    void* value = NULL;
    int failed = 0;

    slate(&map);

    // Two keys of every length from 0 to 100, each with its own value.
    for(uint32_t length = 0; length <= KEYS_MAX_LENGTH; ++length) {
        for(uint64_t variant = 0; variant < 2; ++variant) {
            keys_fill(buffer, length, (uint64_t)length * 2 + variant);

            void* stored = (void*)(uintptr_t)((uint64_t)length * 2 + variant + 1);

            if(slate_set_bytes(&map, buffer, length, stored) != SLATE_OK) {
                ++failed;
            }
        }
    }

    // A zero-length key has a single identity, so its two variants collapse into one entry.
    ATT_ASSERT(failed, 0, "keys of every length are stored");
    ATT_ASSERT(slate_length(&map), KEYS_MAX_LENGTH * 2 + 1, "every distinct key is an entry");

    failed = 0;

    for(uint32_t length = 1; length <= KEYS_MAX_LENGTH; ++length) {
        for(uint64_t variant = 0; variant < 2; ++variant) {
            keys_fill(buffer, length, (uint64_t)length * 2 + variant);

            if(slate_get_bytes(&map, buffer, length, &value) != SLATE_OK
                || value != (void*)(uintptr_t)((uint64_t)length * 2 + variant + 1)) {
                ++failed;
            }
        }
    }

    ATT_ASSERT(failed, 0, "keys of every length hold their own value");
    ATT_ASSERT(slate_get_bytes(&map, buffer, 0, &value), SLATE_OK, "the empty key is found");
    ATT_ASSERT(value == (void*)2, 1, "the empty key holds its last value");

    int lengths[KEYS_MAX_LENGTH + 1] = { 0 };

    slate_each(&map, keys_count_lengths, lengths);
    failed = lengths[0] != 1;

    for(uint32_t length = 1; length <= KEYS_MAX_LENGTH; ++length) {
        failed += lengths[length] != 2;
    }

    ATT_ASSERT(failed, 0, "iteration reports every key with its exact length");
    ATT_ASSERT(test_check(&map), 0, "the map is sound with keys of every length");

    // One byte more, one byte less, or one byte changed is a different key, wherever the change
    // falls relative to the hash stripes.
    failed = 0;

    for(uint32_t length = 1; length < KEYS_MAX_LENGTH; ++length) {
        keys_fill(buffer, length, (uint64_t)length * 2);

        if(slate_get_bytes(&map, buffer, length - 1, NULL) == SLATE_OK && length - 1 != 0) {
            // A shorter prefix may only match if it is itself a stored key, which these are not.
            ++failed;
        }

        buffer[length - 1] ^= 0x01;

        if(slate_get_bytes(&map, buffer, length, NULL) != SLATE_ERROR_NOT_FOUND) {
            ++failed;
        }

        buffer[length - 1] ^= 0x01;
        buffer[0] ^= 0x80;

        if(slate_get_bytes(&map, buffer, length, NULL) != SLATE_ERROR_NOT_FOUND) {
            ++failed;
        }
    }

    ATT_ASSERT(failed, 0, "a changed first or last byte, or a shorter prefix, is a different key");

    // Deleting every other length leaves the rest intact.
    failed = 0;

    for(uint32_t length = 1; length <= KEYS_MAX_LENGTH; length += 2) {
        keys_fill(buffer, length, (uint64_t)length * 2);
        failed += slate_delete_bytes(&map, buffer, length) != SLATE_OK;
        failed += slate_delete_bytes(&map, buffer, length) != SLATE_ERROR_NOT_FOUND;
    }

    ATT_ASSERT(failed, 0, "keys of odd lengths are deleted once");
    ATT_ASSERT(test_check(&map), 0, "the map is sound after deleting by length");
    slate_free(&map);

    // Long keys: a whole number of stripes, and a large key that also has every tail.
    size_t big_length = 70000 + 7;
    unsigned char* big = (unsigned char*)malloc(big_length);

    ATT_ASSERT(big != NULL, 1, "the large key buffer is allocated");

    if(big) {
        slate(&map);
        keys_fill(big, (uint32_t)big_length, 42);

        ATT_ASSERT(slate_set_bytes(&map, big, 64, (void*)1), SLATE_OK, "a 64-byte key is stored");
        ATT_ASSERT(slate_set_bytes(&map, big, (uint32_t)big_length, (void*)2), SLATE_OK,
            "a 70007-byte key is stored");
        ATT_ASSERT(slate_get_bytes(&map, big, 64, &value), SLATE_OK, "the 64-byte key is found");
        ATT_ASSERT(value == (void*)1, 1, "the 64-byte key is not its longer extension");
        ATT_ASSERT(slate_get_bytes(&map, big, (uint32_t)big_length, &value), SLATE_OK,
            "the large key is found");
        ATT_ASSERT(value == (void*)2, 1, "the large key holds its value");

        big[big_length / 2] ^= 0x10;
        ATT_ASSERT(slate_get_bytes(&map, big, (uint32_t)big_length, NULL), SLATE_ERROR_NOT_FOUND,
            "a large key differing in one middle byte is a different key");
        big[big_length / 2] ^= 0x10;

        ATT_ASSERT(slate_delete_bytes(&map, big, (uint32_t)big_length), SLATE_OK,
            "the large key is deleted");
        ATT_ASSERT(slate_length(&map), 1, "only the 64-byte key remains");
        slate_free(&map);
        free(big);
    }

    // String keys are their bytes without the NUL, and the empty string is the empty key.
    slate(&map);
    ATT_ASSERT(slate_set(&map, "abc", (void*)1), SLATE_OK, "a string key is stored");
    ATT_ASSERT(slate_get_bytes(&map, "abc", 3, &value), SLATE_OK,
        "a string key is found as its bytes");
    ATT_ASSERT(slate_get_bytes(&map, "abc", 4, NULL), SLATE_ERROR_NOT_FOUND,
        "the bytes with the NUL are a different key");
    ATT_ASSERT(slate_set(&map, "", (void*)2), SLATE_OK, "the empty string is a key");
    ATT_ASSERT(slate_get_bytes(&map, "x", 0, &value), SLATE_OK,
        "the empty string is the zero-length key");
    ATT_ASSERT(value == (void*)2, 1, "the empty key holds its value");
    ATT_ASSERT(slate_delete(&map, ""), SLATE_OK, "the empty string key is deleted");

    // Values are opaque: NULL and all-ones pointers round-trip, and a stored NULL is not "missing".
    ATT_ASSERT(slate_set(&map, "null", NULL), SLATE_OK, "a NULL value is stored");
    value = (void*)1;
    ATT_ASSERT(slate_get(&map, "null", &value), SLATE_OK, "a key with a NULL value is found");
    ATT_ASSERT(value == NULL, 1, "the NULL value is returned");
    ATT_ASSERT(slate_set(&map, "ones", (void*)UINTPTR_MAX), SLATE_OK,
        "an all-ones value is stored");
    ATT_ASSERT(slate_get(&map, "ones", &value), SLATE_OK, "the all-ones key is found");
    ATT_ASSERT(value == (void*)UINTPTR_MAX, 1, "the all-ones value is returned intact");

    // A missing key leaves the output untouched.
    value = (void*)3;
    ATT_ASSERT(slate_get(&map, "missing", &value), SLATE_ERROR_NOT_FOUND,
        "a missing key is not found");
    ATT_ASSERT(value == (void*)3, 1, "a failed lookup does not write the value");

    // Keys are copied: the caller's bytes buffer can change right after the call.
    unsigned char reused[3] = { 1, 2, 3 };

    ATT_ASSERT(slate_set_bytes(&map, reused, 3, (void*)4), SLATE_OK, "a buffer key is stored");
    reused[0] = 9;
    ATT_ASSERT(slate_get_bytes(&map, reused, 3, NULL), SLATE_ERROR_NOT_FOUND,
        "the changed buffer is a different key");
    reused[0] = 1;
    ATT_ASSERT(slate_get_bytes(&map, reused, 3, &value), SLATE_OK,
        "the original bytes still find the copy");
    ATT_ASSERT(value == (void*)4, 1, "the copied key holds its value");

    ATT_ASSERT(test_check(&map), 0, "the map is sound with mixed keys");
    ATT_ASSERT_NE(slate_free(&map), SLATE_ERROR_INVALID, "slate free should not fail");
}
