/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 *
 * Shared test helpers: a counting allocator plugged into slate.h through SLATE_MALLOC,
 * SLATE_CALLOC and SLATE_FREE (see test.c), allocation failure injection, an invariant checker and
 * a deterministic random generator.
 */

#ifndef SLATE_TEST_HELPERS_H
#define SLATE_TEST_HELPERS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct slate_s;

// The allocator slate.h uses in the test build. Every successful allocation is counted until freed,
// so `test_alloc_live` exposes leaks.
void* test_malloc(size_t size);
void* test_calloc(size_t count, size_t size);
void test_free(void* pointer);

// Number of allocations currently alive.
int64_t test_alloc_live(void);

// Make the `nth` allocation from now (1-based) fail, once. 0 disarms.
void test_alloc_fail_at(uint64_t nth);

// 1 when the armed failure has fired since the last `test_alloc_fail_at` call.
int test_alloc_failed(void);

// Number of allocations attempted since the last `test_alloc_fail_at` call.
uint64_t test_alloc_attempts(void);

// Walk every table and re-derive the bookkeeping from the raw control bytes: directory length and
// aliasing, table sizes and depths, used counts, the grow_left budget, and that every stored key is
// found by lookup with its own value. Returns the number of violations (0 when the map is sound).
int test_check(struct slate_s* map);

// 1 when at least two directory entries point at the same table.
int test_has_alias(struct slate_s* map);

// Insert keys from `next_key` on until the directory has an alias. Returns the number inserted, or
// -1 on failure.
int test_fill_until_alias(struct slate_s* map, uint64_t* next_key);

// Deterministic splitmix64 stream, so randomized tests are reproducible.
uint64_t test_random(uint64_t* state);

#ifdef __cplusplus
}
#endif

#endif // SLATE_TEST_HELPERS_H
