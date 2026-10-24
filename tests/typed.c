/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 */

#include <string.h>

#include "attractor.h"
#include "../src/slate-generic.h"

ATT_TEST(typed) {
    slate_s map;
    void* value = NULL;

    slate(&map);

    // Byte keys: embedded NULs are ordinary key bytes.
    const char binary_a[] = { 'k', '\0', '1' };
    const char binary_b[] = { 'k', '\0', '2' };

    ATT_ASSERT(slate_set_bytes(&map, binary_a, sizeof(binary_a), (void*)1), SLATE_OK,
        "a key with an embedded NUL is stored");
    ATT_ASSERT(slate_set_bytes(&map, binary_b, sizeof(binary_b), (void*)2), SLATE_OK,
        "a second key differing after the NUL is stored");
    ATT_ASSERT(slate_length(&map), 2, "the binary keys are distinct entries");
    ATT_ASSERT(slate_get_bytes(&map, binary_a, sizeof(binary_a), &value), SLATE_OK,
        "the first binary key is found");
    ATT_ASSERT(value == (void*)1, 1, "the first binary key holds its value");
    ATT_ASSERT(slate_get_bytes(&map, binary_a, 2, &value), SLATE_ERROR_NOT_FOUND,
        "a prefix of a key is not the key");
    ATT_ASSERT(slate_delete_bytes(&map, binary_b, sizeof(binary_b)), SLATE_OK,
        "a binary key is deleted");

    // Integer keys: signed, unsigned and string keyspaces never collide.
    ATT_ASSERT(slate_set_i64(&map, 42, (void*)3), SLATE_OK, "a signed key is stored");
    ATT_ASSERT(slate_set_u64(&map, 42, (void*)4), SLATE_OK, "an unsigned key is stored");
    ATT_ASSERT(slate_set(&map, "42", (void*)5), SLATE_OK, "a string key is stored");
    ATT_ASSERT(slate_get_i64(&map, 42, &value), SLATE_OK, "the signed key is found");
    ATT_ASSERT(value == (void*)3, 1, "the signed key holds its value");
    ATT_ASSERT(slate_get_u64(&map, 42, &value), SLATE_OK, "the unsigned key is found");
    ATT_ASSERT(value == (void*)4, 1, "the unsigned key holds its value");
    ATT_ASSERT(slate_get(&map, "42", &value), SLATE_OK, "the string key is found");
    ATT_ASSERT(value == (void*)5, 1, "the string key holds its value");

    // The leading NUL in the integer encoding makes it unreachable from any string key: the crafted
    // 10-char lookalike is a different key even when its bytes match the tag and value of an
    // integer key.
    ATT_ASSERT(slate_set_i64(&map, 0x4141414141414141, (void*)30), SLATE_OK,
        "an all-printable-bytes integer key is stored");
    ATT_ASSERT(slate_get(&map, "iAAAAAAAA", &value), SLATE_ERROR_NOT_FOUND,
        "no string can alias an integer key");
    ATT_ASSERT(slate_set(&map, "iAAAAAAAA", (void*)31), SLATE_OK,
        "the lookalike string is its own key");
    ATT_ASSERT(slate_get_i64(&map, 0x4141414141414141, &value), SLATE_OK,
        "the integer key is unaffected by the lookalike");
    ATT_ASSERT(value == (void*)30, 1, "the integer key holds its own value");

    ATT_ASSERT(slate_set_i64(&map, -7, (void*)6), SLATE_OK, "a negative key is stored");
    ATT_ASSERT(slate_get_i64(&map, -7, &value), SLATE_OK, "the negative key is found");
    ATT_ASSERT(value == (void*)6, 1, "the negative key holds its value");
    ATT_ASSERT(slate_delete_i64(&map, -7), SLATE_OK, "the negative key is deleted");
    ATT_ASSERT(slate_get_i64(&map, -7, &value), SLATE_ERROR_NOT_FOUND,
        "the deleted negative key is gone");

    // Generic macros: one call site per type, dispatched at compile time (_Generic in C, the
    // template in C++; this file is built as both).
    ATT_ASSERT(SLATE_SET(&map, "name", (void*)7), SLATE_OK, "generic set accepts a string");
    ATT_ASSERT(SLATE_SET(&map, 1000, (void*)8), SLATE_OK, "generic set accepts an int");
    ATT_ASSERT(SLATE_SET(&map, 1000u, (void*)9), SLATE_OK, "generic set accepts an unsigned");
    ATT_ASSERT(SLATE_GET(&map, "name", &value), SLATE_OK, "generic get accepts a string");
    ATT_ASSERT(value == (void*)7, 1, "the generic string key holds its value");
    ATT_ASSERT(SLATE_GET(&map, 1000, &value), SLATE_OK, "generic get accepts an int");
    ATT_ASSERT(value == (void*)8, 1, "the generic int key holds its value");
    ATT_ASSERT(SLATE_GET(&map, 1000u, &value), SLATE_OK, "generic get accepts an unsigned");
    ATT_ASSERT(value == (void*)9, 1, "the generic unsigned key holds its value");
    ATT_ASSERT(SLATE_DELETE(&map, 1000), SLATE_OK, "generic delete accepts an int");
    ATT_ASSERT(SLATE_GET(&map, 1000, &value), SLATE_ERROR_NOT_FOUND,
        "the generic-deleted key is gone");

    ATT_ASSERT_NE(slate_free(&map), SLATE_ERROR_INVALID, "slate free should not fail");
}
