/**
 * The Slate library
 *
 * This file is distributed under the MIT License. See LICENSE for details.
 *
 * Single-header library. In exactly one source file define SLATE_IMPLEMENTATION before including
 * this header:
 *
 * #define SLATE_IMPLEMENTATION
 * #include "slate.h"
 *
 * Optional configuration, defined before the include:
 *
 * SLATE_API linkage prefix of the public functions, default: nothing
 * SLATE_MALLOC / SLATE_CALLOC / SLATE_FREE custom allocator (define all three)
 */

#ifndef SLATE_H
#define SLATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#ifndef SLATE_API
#define SLATE_API
#endif

typedef struct slate_slot_s {
    // Keys are byte sequences of key_length bytes.
    char *key;
    void *value;
    uint32_t key_length;
} slate_slot_s;

typedef struct slate_group_s {
    uint64_t ctrl;
    slate_slot_s slots[8];
} slate_group_s;

typedef struct slate_table_s {
    uint8_t local_depth;
    uint16_t used;
    uint16_t capacity;
    uint16_t grow_left;
    slate_group_s *groups;
} slate_table_s;

// The main map structure. Created with `slate()`.
typedef struct slate_s {
    slate_table_s **dir;  // Array of table pointers, indexed by the top global_depth bits of H1.
    uint64_t used;        // Number of entries in the map.
    uint64_t seed;        // Hash seed for the map.
    int dir_length;       // Number of entries in the directory array.
    uint8_t global_depth; // Number of high hash bits used to select one directory entry.
} slate_s;

// Result of every slate operation. SLATE_OK is 0, real errors are negative, and
// SLATE_ERROR_NOT_FOUND is positive: an ordinary outcome of get and delete, not a failure.
typedef enum slate_error_e {
    // The map ran out of space and could not grow or split.
    SLATE_ERROR_FULL = -3,
    // A memory allocation failed; the map is unchanged.
    SLATE_ERROR_ALLOC = -2,
    // An argument was NULL or otherwise invalid.
    SLATE_ERROR_INVALID = -1,
    // The operation succeeded.
    SLATE_OK = 0,
    // The key is not in the map.
    SLATE_ERROR_NOT_FOUND = 1
} slate_error_e;

// Initialize a map. Returns SLATE_OK on success or a negative error code on failure.
SLATE_API int slate(slate_s* map);

// Pre-size an map so that `count` entries fit without growing or splitting, like Go's
// `make(map, hint)`
SLATE_API int slate_reserve(slate_s* map, uint64_t count);

// Remove every entry but keep all allocated capacity, like Go's `clear(m)`.
SLATE_API int slate_clear(slate_s* map);

// Initialize destination as an independent copy of source, like Go's `maps.Clone`: keys are
// duplicated, values are copied shallowly (the void* itself).
// No rehashing happens. `slate_compact` the clone if you want an independent seed.
SLATE_API int slate_clone(slate_s* destination, slate_s* source);

// Rebuild the map sized for its current number of entries, releasing the capacity accumulated by
// growth and splits (deletion alone never returns memory, matching Go).
// O(n): every entry is rehashed under a fresh seed.
SLATE_API int slate_compact(slate_s* map);

// Add a map element mapped at `key`. An arbitrary byte sequence of `key_length` bytes (embedded
// NULs allowed, `key_length` 0 allowed). The string and integer functions below are wrappers around
// these.
SLATE_API int slate_set_bytes(slate_s* map, const void* key, uint32_t key_length, void* value);

// Writes `*value` (when value is not NULL) and returns SLATE_OK if the key exists, an error code
// otherwise.
SLATE_API int slate_get_bytes(slate_s* map, const void* key, uint32_t key_length, void** value);

// Delete the entry mapped at `key`. Returns SLATE_OK if the key was removed.
SLATE_API int slate_delete_bytes(slate_s* map, const void* key, uint32_t key_length);

// Add/get/delete a map element mapped at a NUL-terminated string key.
SLATE_API int slate_set(slate_s* map, const char* key, void* value);
SLATE_API int slate_get(slate_s* map, const char* key, void** value);
SLATE_API int slate_delete(slate_s* map, const char* key);

// Integer keys, stored as a canonical 10-byte encoding.
SLATE_API int slate_set_i64(slate_s* map, int64_t key, void* value);
SLATE_API int slate_get_i64(slate_s* map, int64_t key, void** value);
SLATE_API int slate_delete_i64(slate_s* map, int64_t key);

// Unsigned integer keys, stored as a canonical 10-byte encoding.
SLATE_API int slate_set_u64(slate_s* map, uint64_t key, void* value);
SLATE_API int slate_get_u64(slate_s* map, uint64_t key, void** value);
SLATE_API int slate_delete_u64(slate_s* map, uint64_t key);

// Callback for `slate_each`: receives each key with its length, its value, and the caller's data
// pointer. Keys set through the string functions are NUL-terminated. Keys set through the bytes or
// integer functions may contain any byte.
// Return 0 to continue the iteration, any other value to stop it.
typedef int (*slate_each_fn)(const char* key, uint32_t key_length, void* value, void* data);

// Call `callback` for every entry, in no guaranteed order. Returns SLATE_OK after visiting every
// entry, or the callback's value if one stopped the iteration.
// The callback must not modify the map.
SLATE_API int slate_each(slate_s* map, slate_each_fn callback, void* data);

// Callback for `slate_delete_if`: return non-zero to delete the entry, 0 to keep it. The function
// must not modify the map.
typedef int (*slate_delete_if_fn)(const char* key, uint32_t key_length, void* value, void* data);

// Delete every entry the predicate selects, like Go's `maps.DeleteFunc`.
SLATE_API int slate_delete_if(slate_s* map, slate_delete_if_fn callback, void* data);

// Returns 1 when both maps hold the same keys with the same values, 0 otherwise, like Go's
// `maps.Equal`.
SLATE_API int slate_equal(slate_s* first, slate_s* second);

// Returns the number of entries in the map.
SLATE_API uint64_t slate_length(slate_s* map);

// Frees the map and its resources. Afterwards the map is a valid empty map with a fresh seed, ready
// to be reused without calling `slate()` again.
SLATE_API int slate_free(slate_s* map);

#ifdef __cplusplus
}
#endif

#endif // SLATE_H

#ifdef SLATE_IMPLEMENTATION

#ifndef SLATE_IMPLEMENTATION_INCLUDED
#define SLATE_IMPLEMENTATION_INCLUDED

#if defined(__wasi__) || defined(__EMSCRIPTEN__)
    // Expose getentropy in the (musl-derived) libc headers under strict C11.
    #define _GNU_SOURCE
#endif

#if defined(_WIN32) || defined(_WIN64)
    // Must be defined before <stdlib.h> to expose rand_s.
    #define _CRT_RAND_S
    #include <stdlib.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

#if defined(_MSC_VER)
    #include <intrin.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <time.h>

// Overridable allocators. Define all three before the include that carries SLATE_IMPLEMENTATION to
// plug a custom allocator into the whole library.
#if defined(SLATE_MALLOC) || defined(SLATE_CALLOC) || defined(SLATE_FREE)
    #if !defined(SLATE_MALLOC) || !defined(SLATE_CALLOC) || !defined(SLATE_FREE)
        #error "Define all of SLATE_MALLOC, SLATE_CALLOC and SLATE_FREE, or none."
    #endif
#else
    #define SLATE_MALLOC(size) malloc(size)
    #define SLATE_CALLOC(count, size) calloc(count, size)
    #define SLATE_FREE(pointer) free(pointer)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Control byte indicating an empty slot.
// 1|0000000
#define SLATE_EMPTY 0x80

// All 8 control bytes of a group set to SLATE_EMPTY.
#define SLATE_EMPTY_GROUP 0x8080808080808080u

// Control byte indicating a deleted slot.
//  1|1111110
#define SLATE_DELETED 0xFE

// Width of H1: the 64-bit hash minus the 7 bits taken by h2. The directory is indexed by the top
// global_depth of these bits.
#define SLATE_H1_BITS 57

// One table grows up to 128 groups (128 * 8 = 1024 slots) and then splits in two, as in Go.
#define SLATE_TABLE_MAX_GROUPS 128

// Entries a maximum-size table absorbs before splitting: its 1024 slots times the 7/8 load factor.
#define SLATE_TABLE_MAX_ENTRIES (SLATE_TABLE_MAX_GROUPS * 7)

// SWAR group scans: examine all 8 control bytes of a ctrl word at once. The masks select the high
// and low bit of every byte.
#define SLATE_CTRL_MSBS 0x8080808080808080u
#define SLATE_CTRL_LSBS 0x0101010101010101u

// Count trailing zeros of a non-zero value: the caller guarantees it.
static int slate_ctz64(uint64_t value) {
#if defined(_MSC_VER)
    unsigned long index;

    _BitScanForward64(&index, value);

    return (int)index;
#else
    return __builtin_ctzll(value);
#endif
}

// Bitmask with the high bit of every byte possibly equal to h2 set.
static uint64_t slate_group_match(uint64_t ctrl, uint8_t h2) {
    uint64_t x = ctrl ^ ((uint64_t)h2 * SLATE_CTRL_LSBS);

    return (x - SLATE_CTRL_LSBS) & ~x & SLATE_CTRL_MSBS;
}

// Bitmask with the high bit of every SLATE_EMPTY byte set.
static uint64_t slate_group_match_empty(uint64_t ctrl) {
    return ctrl & ~(ctrl << 1) & SLATE_CTRL_MSBS;
}

// Bitmask with the high bit of every SLATE_DELETED byte set.
static uint64_t slate_group_match_deleted(uint64_t ctrl) {
    return ctrl & (ctrl << 1) & SLATE_CTRL_MSBS;
}

// Write one slot: replace its control byte (a fingerprint or a marker) and store the key, its
// length, and the value.
static inline void slate_slot_write(slate_group_s* group, int slot, uint8_t ctrl,
    char* key, uint32_t key_length, void* value) {
    group->ctrl &= ~((uint64_t)0xFF << (slot * 8));
    group->ctrl |= (uint64_t)ctrl << (slot * 8);
    group->slots[slot].key = key;
    group->slots[slot].key_length = key_length;
    group->slots[slot].value = value;
}

static uint64_t slate_atomic_load(volatile uint64_t* target) {
#if defined(_MSC_VER)
    return (uint64_t)_InterlockedCompareExchange64((volatile long long*)target, 0, 0);
#else
    return __atomic_load_n(target, __ATOMIC_ACQUIRE);
#endif
}

static uint64_t slate_atomic_cas(volatile uint64_t* target, uint64_t expected, uint64_t desired) {
#if defined(_MSC_VER)
    return (uint64_t)_InterlockedCompareExchange64((volatile long long*)target,
        (long long)desired, (long long)expected);
#else
    __atomic_compare_exchange_n(target, &expected, desired, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);

    return expected;
#endif
}

static uint64_t slate_atomic_increment(volatile uint64_t* target) {
#if defined(_MSC_VER)
    return (uint64_t)_InterlockedIncrement64((volatile long long*)target);
#else
    return __atomic_add_fetch(target, 1, __ATOMIC_RELAXED);
#endif
}

// Read 64 bits from the OS entropy source. Returns SLATE_ERROR_INVALID on failure.
static int slate_os_entropy(uint64_t* value) {
#if defined(_WIN32) || defined(_WIN64)
    unsigned int low = 0;
    unsigned int high = 0;

    if(rand_s(&low) != 0 || rand_s(&high) != 0) {
        return SLATE_ERROR_INVALID;
    }

    *value = ((uint64_t)high << 32) | (uint64_t)low;

    return SLATE_OK;
#elif defined(__wasi__) || defined(__EMSCRIPTEN__)
    // WASM has no /dev/urandom (and no ASLR to fall back on, so the fallback seed would be
    // predictable): getentropy wraps the runtime's native randomness, `random_get` on WASI and
    // crypto.getRandomValues on Emscripten.
    unsigned char buffer[sizeof(*value)];

    if(getentropy(buffer, sizeof(buffer)) != 0) {
        return SLATE_ERROR_INVALID;
    }

    *value = 0;

    for(size_t i = 0; i < sizeof(buffer); ++i) {
        *value = (*value << 8) | buffer[i];
    }

    return SLATE_OK;
#else
    unsigned char buffer[sizeof(*value)];
    size_t total = 0;
    int fd = open("/dev/urandom", O_RDONLY);

    if(fd == -1) {
        return SLATE_ERROR_INVALID;
    }

    while(total < sizeof(buffer)) {
        ssize_t count = read(fd, buffer + total, sizeof(buffer) - total);

        if(count <= 0) {
            close(fd);

            return SLATE_ERROR_INVALID;
        }

        total += (size_t)count;
    }

    close(fd);

    *value = 0;

    for(total = 0; total < sizeof(buffer); ++total) {
        *value = (*value << 8) | buffer[total];
    }

    return SLATE_OK;
#endif
}

// `splitmix64` finalizer (public domain, Sebastiano Vigna)
// See https://prng.di.unimi.it/splitmix64.c
static uint64_t slate_splitmix64(uint64_t value) {
    // Stafford's Mix13.
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9u;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBu;

    return value ^ (value >> 31);
}

// Per-map seed generator: one lazy OS entropy read per process, stretched into a stream of distinct
// unpredictable seeds.
// `base` holds the process entropy (0 means "not fetched yet") and `counter` makes every call
// return a different seed derived from it. Thread-safe via the atomics.
static uint64_t slate_random_seed(void) {
    static volatile uint64_t base = 0;
    static volatile uint64_t counter = 0;
    uint64_t entropy = slate_atomic_load(&base);

    // Zero only before the first successful initialization: `base` starts zero by static
    // initialization, and once a value is installed nothing ever writes zero back.
    if(entropy == 0) {
        uint64_t fresh = 0;

        if(slate_os_entropy(&fresh) != SLATE_OK) {
            // Fallback: ASLR-randomized address mixed with the clock.
            fresh = (uint64_t)(uintptr_t)&base ^ (uint64_t)time(NULL);
        }

        // Zero is reserved as the "not fetched yet" sentinel, but both sources above can
        // legitimately produce it.
        if(fresh == 0) {
            // The 64-bit "golden gamma": ⌊2⁶⁴ / φ⌋, φ is the golden ratio (1+√5)/2 ≈ 1.618
            // 11,400,714,819,323,198,485 — 2⁶⁴ divided by φ, rounded to the nearest odd integer.
            fresh = 0x9E3779B97F4A7C15u;
        }

        // Concurrent first calls race here.
        entropy = slate_atomic_cas(&base, 0, fresh);

        if(entropy == 0) {
            entropy = fresh;
        }
    }

    // The splitmix64 sequence: advance by the golden gamma per call and finalize. Distinct counter
    // values give distinct, well-mixed seeds, unpredictable without knowing the process entropy.
    return slate_splitmix64(entropy + slate_atomic_increment(&counter) * 0x9E3779B97F4A7C15u);
}

// Multiple tool functions.
static uint64_t slate_rotl64(uint64_t value, int amount) {
    return (value << amount) | (value >> (64 - amount));
}

static uint64_t slate_read64(const unsigned char* data) {
    uint64_t value;
    memcpy(&value, data, sizeof(value));

    return value;
}

static uint32_t slate_read32(const unsigned char* data) {
    uint32_t value;
    memcpy(&value, data, sizeof(value));

    return value;
}

// xxHash64 (BSD 2-Clause, Yann Collet). See https://xxhash.com

// See PRIME64_1/_5 definitions
#define SLATE_PRIME64_1 0x9E3779B185EBCA87u
#define SLATE_PRIME64_2 0xC2B2AE3D27D4EB4Fu
#define SLATE_PRIME64_3 0x165667B19E3779F9u
#define SLATE_PRIME64_4 0x85EBCA77C2B2AE63u
#define SLATE_PRIME64_5 0x27D4EB2F165667C5u

// See `XXH64_round` function.
static uint64_t slate_hash_round(uint64_t accumulator, uint64_t input) {
    accumulator += input * SLATE_PRIME64_2;
    accumulator = slate_rotl64(accumulator, 31);

    return accumulator * SLATE_PRIME64_1;
}

// See `XXH64_mergeRound` function.
static uint64_t slate_hash_merge_round(uint64_t hash, uint64_t value) {
    hash ^= slate_hash_round(0, value);

    return hash * SLATE_PRIME64_1 + SLATE_PRIME64_4;
}

// See `XXH64` function.
static uint64_t slate_hash(const void* input, size_t length, uint64_t seed) {
    const unsigned char* data = (const unsigned char*)input;
    const unsigned char* end = data + length;
    uint64_t hash;

    if(length >= 32) {
        uint64_t v1 = seed + SLATE_PRIME64_1 + SLATE_PRIME64_2;
        uint64_t v2 = seed + SLATE_PRIME64_2;
        uint64_t v3 = seed;
        uint64_t v4 = seed - SLATE_PRIME64_1;

        do {
            v1 = slate_hash_round(v1, slate_read64(data));
            v2 = slate_hash_round(v2, slate_read64(data + 8));
            v3 = slate_hash_round(v3, slate_read64(data + 16));
            v4 = slate_hash_round(v4, slate_read64(data + 24));
            data += 32;
        } while(data <= end - 32);

        hash = slate_rotl64(v1, 1) + slate_rotl64(v2, 7) + slate_rotl64(v3, 12) +
            slate_rotl64(v4, 18);
        hash = slate_hash_merge_round(hash, v1);
        hash = slate_hash_merge_round(hash, v2);
        hash = slate_hash_merge_round(hash, v3);
        hash = slate_hash_merge_round(hash, v4);
    } else {
        hash = seed + SLATE_PRIME64_5;
    }

    hash += (uint64_t)length;

    while(data + 8 <= end) {
        hash ^= slate_hash_round(0, slate_read64(data));
        hash = slate_rotl64(hash, 27) * SLATE_PRIME64_1 + SLATE_PRIME64_4;
        data += 8;
    }

    if(data + 4 <= end) {
        hash ^= (uint64_t)slate_read32(data) * SLATE_PRIME64_1;
        hash = slate_rotl64(hash, 23) * SLATE_PRIME64_2 + SLATE_PRIME64_3;
        data += 4;
    }

    while(data < end) {
        hash ^= (uint64_t)(*data) * SLATE_PRIME64_5;
        hash = slate_rotl64(hash, 11) * SLATE_PRIME64_1;
        ++data;
    }

    hash ^= hash >> 33;
    hash *= SLATE_PRIME64_2;
    hash ^= hash >> 29;
    hash *= SLATE_PRIME64_3;
    hash ^= hash >> 32;

    return hash;
}

// Duplicate an owned copy of a key. This is similar to `strdup`.
static char* slate_key_dup(const void* key, uint32_t key_length) {
    char* copy = (char*)SLATE_MALLOC((size_t)key_length + 1);

    if(!copy) {
        return NULL;
    }

    memcpy(copy, key, key_length);
    // Add a terminating NUL byte, so string keys stay "printable".
    copy[key_length] = '\0';

    return copy;
}

// True when a slot holds exactly this key.
static inline int slate_key_equal(const slate_slot_s* slot, const void* key, uint32_t key_length) {
    return slot->key_length == key_length && memcmp(slot->key, key, key_length) == 0;
}

// Free one table with all its owned resources.
static int slate_table_free(slate_table_s* table) {
    if(!table) {
        return SLATE_ERROR_INVALID;
    }

    if(table->groups) {
        for(int j = 0; j < table->capacity / 8; ++j) {
            slate_group_s* group = &table->groups[j];

            for(int k = 0; k < 8; ++k) {
                uint8_t ctrl = (uint8_t)(group->ctrl >> (k * 8));

                // Occupied slots (high bit clear) own their key copy.
                if((ctrl & SLATE_EMPTY) == 0) {
                    SLATE_FREE(group->slots[k].key);
                }
            }
        }

        SLATE_FREE(table->groups);
    }

    SLATE_FREE(table);

    return SLATE_OK;
}

// Allocate one table with `groups` groups of 8 slots. `groups` must be a power of two within the
// table cap: no table is ever born larger than it could grow.
static slate_table_s* slate_table_alloc(int groups, uint8_t local_depth) {
    if(groups < 1 || groups > SLATE_TABLE_MAX_GROUPS || (groups & (groups - 1)) != 0) {
        return NULL;
    }

    slate_table_s* table = (slate_table_s*)SLATE_CALLOC(1, sizeof(slate_table_s));

    if(!table) {
        return NULL;
    }

    table->groups = (slate_group_s*)SLATE_CALLOC((size_t)groups, sizeof(slate_group_s));

    if(!table->groups) {
        SLATE_FREE(table);

        return NULL;
    }

    for(int i = 0; i < groups; ++i) {
        // Replicate SLATE_EMPTY into all 8 control bytes of the group.
        table->groups[i].ctrl = SLATE_EMPTY_GROUP;
    }

    table->local_depth = local_depth;
    table->capacity = (uint16_t)(groups * 8);

    // Inserts left before the table reaches the 7/8 load factor.
    table->grow_left = (uint16_t)(table->capacity / 8 * 7);

    return table;
}

// Clone one table: struct copy, groups memcpy, then an owned copy of every key.
static slate_table_s* slate_table_clone(const slate_table_s* source) {
    size_t groups_size = (size_t)(source->capacity / 8) * sizeof(slate_group_s);
    slate_table_s* table = (slate_table_s*)SLATE_MALLOC(sizeof(slate_table_s));

    if(!table) {
        return NULL;
    }

    *table = *source;
    table->groups = (slate_group_s*)SLATE_MALLOC(groups_size);

    if(!table->groups) {
        SLATE_FREE(table);

        return NULL;
    }

    memcpy(table->groups, source->groups, groups_size);

    for(int g = 0; g < table->capacity / 8; ++g) {
        slate_group_s* group = &table->groups[g];
        uint64_t occupied = ~group->ctrl & SLATE_CTRL_MSBS;

        while(occupied) {
            int i = slate_ctz64(occupied) >> 3;
            char* copy = slate_key_dup(group->slots[i].key, group->slots[i].key_length);

            if(!copy) {
                // The slots still holding source key pointers become empty, so freeing the partial
                // clone frees only duplicated keys.
                while(occupied) {
                    int k = slate_ctz64(occupied) >> 3;

                    group->ctrl &= ~((uint64_t)0xFF << (k * 8));
                    group->ctrl |= (uint64_t)SLATE_EMPTY << (k * 8);
                    occupied &= occupied - 1;
                }

                for(int rest = g + 1; rest < table->capacity / 8; ++rest) {
                    table->groups[rest].ctrl = SLATE_EMPTY_GROUP;
                }

                slate_table_free(table);

                return NULL;
            }

            group->slots[i].key = copy;
            occupied &= occupied - 1;
        }
    }

    return table;
}

// Free the directory and each distinct table exactly once. Directory entries that alias one table
// share their top local_depth bits, so they are contiguous: a table is freed only at its first
// entry.
static int slate_dir_free(slate_table_s** dir, int length) {
    if(!dir) {
        return SLATE_ERROR_INVALID;
    }

    for(int i = 0; i < length; ++i) {
        if(dir[i] && (i == 0 || dir[i] != dir[i - 1])) {
            slate_table_free(dir[i]);
        }
    }

    SLATE_FREE(dir);

    return SLATE_OK;
}

// Allocate a directory of `length` pointers, each to its own table with `groups` groups of 8 slots.
// `length` must be a power of two: the directory is indexed by the top global_depth bits of H1.
static int slate_dir_alloc(slate_s* map, int length, int groups) {
    if(!map || length < 1 || (length & (length - 1)) != 0) {
        return SLATE_ERROR_INVALID;
    }

    slate_table_s** dir = (slate_table_s**)SLATE_CALLOC((size_t)length, sizeof(slate_table_s*));

    if(!dir) {
        return SLATE_ERROR_INVALID;
    }

    // Every entry gets a distinct table, so local depth == global depth.
    uint8_t depth = 0;

    while((1 << depth) < length) {
        ++depth;
    }

    for(int i = 0; i < length; ++i) {
        dir[i] = slate_table_alloc(groups, depth);

        if(!dir[i]) {
            slate_dir_free(dir, length);

            return SLATE_ERROR_ALLOC;
        }
    }

    map->dir = dir;
    map->dir_length = length;
    map->global_depth = depth;

    return SLATE_OK;
}

// Place an already-owned key into a group array by its hash, with no existence check.
static void slate_place_slot(slate_group_s* groups, uint64_t group_count, uint64_t hash,
    char* key, uint32_t key_length, void* value) {
    uint64_t mask = group_count - 1;
    uint64_t position = (hash >> (64 - SLATE_H1_BITS)) & mask;
    uint8_t h2 = (uint8_t)(hash & 0x7F);

    for(uint64_t probe = 0; ; ++probe) {
        slate_group_s* group = &groups[position];
        uint64_t empty = slate_group_match_empty(group->ctrl);

        if(empty) {
            slate_slot_write(group, slate_ctz64(empty) >> 3, h2, key, key_length, value);

            // Found an empty slot and placed the key there.
            return;
        }

        position = (position + probe + 1) & mask;
    }
}

// Double the directory so one more H1 bit indexes it. Old entry i is duplicated into new entries 2i
// and 2i+1 (its index extended by one low bit), so both alias the table it pointed to.
static int slate_dir_grow(slate_s* map) {
    if(!map || !map->dir || map->dir_length < 1) {
        return SLATE_ERROR_INVALID;
    }

    // 2^30 pointers is already 8GB. Not a good idea.
    if(map->global_depth >= 30) {
        return SLATE_ERROR_INVALID;
    }

    int new_length = map->dir_length * 2;
    slate_table_s** dir = (slate_table_s**)SLATE_CALLOC((size_t)new_length, sizeof(slate_table_s*));

    if(!dir) {
        return SLATE_ERROR_ALLOC;
    }

    for(int i = 0; i < map->dir_length; ++i) {
        dir[i * 2] = map->dir[i];
        dir[i * 2 + 1] = map->dir[i];
    }

    SLATE_FREE(map->dir);

    map->dir = dir;
    map->dir_length = new_length;
    ++map->global_depth;

    return SLATE_OK;
}

// Split a full table into two, dividing its entries by the next H1 bit.
static int slate_table_split(slate_s* map, slate_table_s* table) {
    // When the table's local depth equals the global depth, double it first.
    if(table->local_depth == map->global_depth) {
        if(slate_dir_grow(map) != SLATE_OK) {
            return SLATE_ERROR_ALLOC;
        }
    }

    // The directory entries aliasing the parent form one contiguous range.
    int start = -1;
    int count = 0;

    for(int i = 0; i < map->dir_length; ++i) {
        if(map->dir[i] == table) {
            if(start < 0) {
                start = i;
            }

            ++count;
        } else if(start >= 0) {
            break;
        }
    }

    // After the doubling above the parent always has an even number of aliases. Anything else means
    // a corrupted directory.
    if(start < 0 || count < 2 || (count & 1) != 0) {
        return SLATE_ERROR_INVALID;
    }

    uint8_t new_depth = (uint8_t)(table->local_depth + 1);
    int groups = table->capacity / 8;

    // Both halves keep the parent's capacity, so each starts about half full instead of immediately
    // needing to grow again.
    slate_table_s* left = slate_table_alloc(groups, new_depth);
    slate_table_s* right = slate_table_alloc(groups, new_depth);

    if(!left || !right) {
        slate_table_free(left);
        slate_table_free(right);

        return SLATE_ERROR_ALLOC;
    }

    // Redistribute by the first H1 bit the children disagree on: 0 goes left, 1 goes right. Keys
    // move by pointer; deleted slots are dropped.
    for(int j = 0; j < groups; ++j) {
        slate_group_s* group = &table->groups[j];

        for(int k = 0; k < 8; ++k) {
            uint8_t ctrl = (uint8_t)(group->ctrl >> (k * 8));

            if((ctrl & SLATE_EMPTY) != 0) {
                continue;
            }

            char* slot_key = group->slots[k].key;
            uint32_t slot_key_length = group->slots[k].key_length;
            uint64_t hash = slate_hash(slot_key, slot_key_length, map->seed);
            uint64_t h1 = hash >> (64 - SLATE_H1_BITS);
            slate_table_s* target = (h1 >> (SLATE_H1_BITS - new_depth)) & 1 ? right : left;

            slate_place_slot(target->groups, (uint64_t)groups, hash, slot_key,
                slot_key_length, group->slots[k].value);
            ++target->used;
        }
    }

    left->grow_left = (uint16_t)(left->capacity / 8 * 7 - left->used);
    right->grow_left = (uint16_t)(right->capacity / 8 * 7 - right->used);

    // Rewire the parent's alias range: the first half of the entries has a 0 in the children's
    // distinguishing bit, the second half a 1.
    for(int i = 0; i < count; ++i) {
        map->dir[start + i] = i < count / 2 ? left : right;
    }

    // The keys moved by pointer, so only the parent's shell is freed.
    SLATE_FREE(table->groups);
    SLATE_FREE(table);

    return SLATE_OK;
}

// Double the table's group count and redistribute every occupied slot by its recomputed hash.
static int slate_table_grow(slate_s* map, slate_table_s* table) {
    int old_group_count = table->capacity / 8;
    int new_group_count = old_group_count * 2;

    // A table at the cap splits in two instead of growing further.
    if(new_group_count > SLATE_TABLE_MAX_GROUPS) {
        return slate_table_split(map, table);
    }

    // Double the memory for the new group array.
    slate_group_s* groups = (slate_group_s*)SLATE_CALLOC((size_t)new_group_count,
        sizeof(slate_group_s));

    if(!groups) {
        return SLATE_ERROR_ALLOC;
    }

    for(int i = 0; i < new_group_count; ++i) {
        // Initialize each new group as empty.
        groups[i].ctrl = SLATE_EMPTY_GROUP;
    }

    uint16_t moved = 0;

    for(int j = 0; j < old_group_count; ++j) {
        slate_group_s* old_group = &table->groups[j];

        for(int k = 0; k < 8; ++k) {
            uint8_t ctrl = (uint8_t)(old_group->ctrl >> (k * 8));

            // Only occupied slots move.
            if((ctrl & SLATE_EMPTY) != 0) {
                continue;
            }

            char* slot_key = old_group->slots[k].key;
            uint32_t slot_key_length = old_group->slots[k].key_length;
            uint64_t hash = slate_hash(slot_key, slot_key_length, map->seed);

            slate_place_slot(groups, (uint64_t)new_group_count, hash, slot_key,
                slot_key_length, old_group->slots[k].value);

            ++moved;
        }
    }

    // Remove the old group array.
    SLATE_FREE(table->groups);

    table->groups = groups;
    table->capacity = (uint16_t)(new_group_count * 8);
    table->used = moved;

    // 7/8 of the new capacity minus the entries already in place.
    table->grow_left = (uint16_t)(table->capacity / 8 * 7 - moved);

    return SLATE_OK;
}

// Delete one occupied slot: free the key, write the right marker, and fix the counters.
static void slate_slot_delete(slate_s* map, slate_table_s* table, slate_group_s* group, int slot) {
    uint64_t empty = slate_group_match_empty(group->ctrl);

    SLATE_FREE(group->slots[slot].key);
    slate_slot_write(group, slot, empty ? SLATE_EMPTY : SLATE_DELETED, NULL, 0, NULL);

    if(empty) {
        ++table->grow_left;
    }

    --table->used;
    --map->used;

    if(map->used == 0) {
        map->seed = slate_random_seed();
    }
}

// Smallest power of two >= value (value >= 1).
static uint64_t slate_next_pow2(uint64_t value) {
    uint64_t pow2 = 1;

    while(pow2 < value) {
        pow2 <<= 1;
    }

    return pow2;
}

// Directory and table sizes fitting `count` entries under the 7/8 load factor: a full table holds
// SLATE_TABLE_MAX_ENTRIES (896) before splitting, a group holds 8 * 7/8 = 7. The sizing assumes an
// even hash distribution, exactly like Go's make(map, hint): skewed keys can still grow or split a
// table early. Returns -1 when count is absurd.
static int slate_size_for(uint64_t count, int* tables, int* groups) {
    uint64_t t = slate_next_pow2((count + SLATE_TABLE_MAX_ENTRIES - 1) / SLATE_TABLE_MAX_ENTRIES);

    // Matches the directory depth guard in slate_dir_grow.
    if(t > ((uint64_t)1 << 30)) {
        return SLATE_ERROR_INVALID;
    }

    uint64_t per_table = (count + t - 1) / t;

    *tables = (int)t;
    *groups = (int)slate_next_pow2((per_table + 6) / 7);

    return SLATE_OK;
}

// Free a directory and its distinct tables without touching the keys the slots reference: the
// compaction migration copies key pointers into the new tables, so the shells on either side never
// own them alone.
static void slate_dir_free_shells(slate_table_s** dir, int length) {
    for(int i = 0; i < length; ++i) {
        if(dir[i] && (i == 0 || dir[i] != dir[i - 1])) {
            SLATE_FREE(dir[i]->groups);
            SLATE_FREE(dir[i]);
        }
    }

    SLATE_FREE(dir);
}

// Canonical 10-byte integer key: a NUL, one type tag byte, the 8 value bytes, so signed, unsigned
// and string keys can never collide with each other.
static uint32_t slate_key_encode(unsigned char* encoded, char tag, uint64_t bits) {
    // The leading NUL makes integer keys structurally disjoint from every string key: `slate_set`
    // keys go through `strlen` and can never contain a 0x00 byte. The tag then separates the signed
    // and unsigned spaces.
    encoded[0] = 0x00;
    encoded[1] = (unsigned char)tag;
    memcpy(encoded + 2, &bits, sizeof(bits));

    return 10;
}

SLATE_API int slate(slate_s* map) {
    if(!map) {
        return SLATE_ERROR_INVALID;
    }

    map->used = 0;
    map->seed = slate_random_seed();
    map->global_depth = 0;
    map->dir = NULL;
    map->dir_length = 0;

    return SLATE_OK;
}

SLATE_API int slate_reserve(slate_s* map, uint64_t count) {
    if(!map || map->used > 0) {
        return SLATE_ERROR_INVALID;
    }

    if(count == 0) {
        return SLATE_OK;
    }

    int tables = 0;
    int groups = 0;

    if(slate_size_for(count, &tables, &groups) != SLATE_OK) {
        return SLATE_ERROR_INVALID;
    }

    // The current directory is empty: replace it with the sized one.
    if(map->dir) {
        slate_dir_free(map->dir, map->dir_length);
        map->dir = NULL;
        map->dir_length = 0;
        map->global_depth = 0;
    }

    if(slate_dir_alloc(map, tables, groups) != SLATE_OK) {
        return SLATE_ERROR_ALLOC;
    }

    return SLATE_OK;
}

SLATE_API int slate_clear(slate_s* map) {
    if(!map) {
        return SLATE_ERROR_INVALID;
    }

    for(int d = 0; d < map->dir_length; ++d) {
        if(d > 0 && map->dir[d] == map->dir[d - 1]) {
            continue;
        }

        slate_table_s* table = map->dir[d];

        for(int g = 0; g < table->capacity / 8; ++g) {
            slate_group_s* group = &table->groups[g];
            uint64_t occupied = ~group->ctrl & SLATE_CTRL_MSBS;

            while(occupied) {
                SLATE_FREE(group->slots[slate_ctz64(occupied) >> 3].key);
                occupied &= occupied - 1;
            }

            group->ctrl = SLATE_EMPTY_GROUP;
        }

        table->used = 0;
        table->grow_left = (uint16_t)(table->capacity / 8 * 7);
    }

    map->used = 0;

    // As on delete-to-empty and free: an empty map reseeds for free.
    map->seed = slate_random_seed();

    return SLATE_OK;
}

SLATE_API int slate_compact(slate_s* map) {
    if(!map) {
        return SLATE_ERROR_INVALID;
    }

    // An empty map compacts back to the unallocated lazy state.
    if(map->used == 0) {
        if(map->dir) {
            slate_dir_free(map->dir, map->dir_length);
            map->dir = NULL;
            map->dir_length = 0;
            map->global_depth = 0;
        }

        return SLATE_OK;
    }

    int tables = 0;
    int groups = 0;

    if(slate_size_for(map->used, &tables, &groups) != SLATE_OK) {
        return SLATE_ERROR_INVALID;
    }

    // Keep the old layout aside. The migration below only reads it and copies key pointers out, so
    // restoring these fields undoes everything if an allocation fails mid-way.
    slate_table_s** old_dir = map->dir;
    int old_dir_length = map->dir_length;
    uint8_t old_global_depth = map->global_depth;
    uint64_t old_seed = map->seed;

    map->dir = NULL;
    map->dir_length = 0;
    map->global_depth = 0;

    if(slate_dir_alloc(map, tables, groups) != SLATE_OK) {
        map->dir = old_dir;
        map->dir_length = old_dir_length;
        map->global_depth = old_global_depth;

        return SLATE_ERROR_ALLOC;
    }

    // Rehashing everything anyway makes a seed rotation free.
    map->seed = slate_random_seed();

    for(int d = 0; d < old_dir_length; ++d) {
        if(d > 0 && old_dir[d] == old_dir[d - 1]) {
            continue;
        }

        slate_table_s* source = old_dir[d];

        for(int g = 0; g < source->capacity / 8; ++g) {
            slate_group_s* group = &source->groups[g];
            uint64_t occupied = ~group->ctrl & SLATE_CTRL_MSBS;

            while(occupied) {
                int i = slate_ctz64(occupied) >> 3;
                slate_slot_s* slot = &group->slots[i];
                uint64_t hash = slate_hash(slot->key, slot->key_length, map->seed);
                uint64_t h1 = hash >> (64 - SLATE_H1_BITS);
                slate_table_s* target = map->dir[h1 >> (SLATE_H1_BITS - map->global_depth)];

                // Extreme skew can overfill a target.
                if(target->used == target->capacity) {
                    if(slate_table_grow(map, target) != SLATE_OK) {
                        slate_dir_free_shells(map->dir, map->dir_length);
                        map->dir = old_dir;
                        map->dir_length = old_dir_length;
                        map->global_depth = old_global_depth;
                        map->seed = old_seed;

                        return SLATE_ERROR_ALLOC;
                    }

                    target = map->dir[h1 >> (SLATE_H1_BITS - map->global_depth)];
                }

                slate_place_slot(target->groups, (uint64_t)(target->capacity / 8), hash,
                    slot->key, slot->key_length, slot->value);
                ++target->used;
                occupied &= occupied - 1;
            }
        }
    }

    // Restore the load-factor budget invariant on every distinct table.
    for(int d = 0; d < map->dir_length; ++d) {
        if(d > 0 && map->dir[d] == map->dir[d - 1]) {
            continue;
        }

        slate_table_s* table = map->dir[d];
        uint16_t budget = (uint16_t)(table->capacity / 8 * 7);

        table->grow_left = table->used >= budget ? 0 : (uint16_t)(budget - table->used);
    }

    // The keys moved by pointer: free only the old shells.
    slate_dir_free_shells(old_dir, old_dir_length);

    return SLATE_OK;
}

SLATE_API int slate_clone(slate_s* destination, slate_s* source) {
    if(!destination || !source || destination == source) {
        return SLATE_ERROR_INVALID;
    }

    destination->used = source->used;
    destination->seed = source->seed;
    destination->global_depth = source->global_depth;
    destination->dir = NULL;
    destination->dir_length = 0;

    if(!source->dir || source->dir_length == 0) {
        return SLATE_OK;
    }

    slate_table_s** dir = (slate_table_s**)SLATE_CALLOC((size_t)source->dir_length,
        sizeof(slate_table_s*));

    if(!dir) {
        // Leave the destination as a valid empty map.
        slate(destination);

        return SLATE_ERROR_ALLOC;
    }

    // Clone each distinct table once.
    for(int d = 0; d < source->dir_length; ) {
        int run = 1;

        while(d + run < source->dir_length && source->dir[d + run] == source->dir[d]) {
            ++run;
        }

        slate_table_s* clone = slate_table_clone(source->dir[d]);

        if(!clone) {
            // Entries beyond d are still NULL from calloc and are skipped.
            slate_dir_free(dir, source->dir_length);
            slate(destination);

            return SLATE_ERROR_ALLOC;
        }

        for(int i = 0; i < run; ++i) {
            dir[d + i] = clone;
        }

        d += run;
    }

    destination->dir = dir;
    destination->dir_length = source->dir_length;

    return SLATE_OK;
}

SLATE_API int slate_set_bytes(slate_s* map, const void* key, uint32_t key_length, void* value) {
    if(!map || !key) {
        return SLATE_ERROR_INVALID;
    }

    if(map->dir_length == 0 || !map->dir) {
        if(slate_dir_alloc(map, 1, 1) != SLATE_OK) {
            return SLATE_ERROR_ALLOC;
        }
    }

    uint64_t hash = slate_hash(key, key_length, map->seed);
    uint64_t h1 = hash >> (64 - SLATE_H1_BITS);
    uint8_t h2 = (uint8_t)(hash & 0x7F);
    int grown = 0;

retry:;
    // The top global_depth bits of H1 select the directory entry.
    uint64_t dir_index = h1 >> (SLATE_H1_BITS - map->global_depth);
    slate_table_s* table = map->dir[dir_index];

    uint64_t group_count = (uint64_t)(table->capacity / 8);
    uint64_t mask = group_count - 1;

    // Starting group = H1 % number of groups. Triangular probe sequence advances by 1, 2, 3, etc.
    uint64_t starting_group = h1 & mask;
    uint64_t position = starting_group;

    // First tombstone seen along the probe.
    slate_group_s* deleted_group = NULL;
    int deleted_slot = 0;

    for(uint64_t probe = 0; probe < group_count; ++probe) {
        slate_group_s* group = &table->groups[position];

        // Fingerprint candidates first.
        uint64_t match = slate_group_match(group->ctrl, h2);

        while(match) {
            int i = slate_ctz64(match) >> 3;

            if(slate_key_equal(&group->slots[i], key, key_length)) {
                group->slots[i].value = value;

                return SLATE_OK;
            }

            match &= match - 1;
        }

        if(!deleted_group) {
            uint64_t deleted = slate_group_match_deleted(group->ctrl);

            if(deleted) {
                deleted_group = group;
                deleted_slot = slate_ctz64(deleted) >> 3;
            }
        }

        uint64_t empty = slate_group_match_empty(group->ctrl);

        if(empty) {
            // The key is confirmed absent.
            if(deleted_group) {
                break;
            }

            // The table grows only when a new key claims a fresh slot, so replacements never
            // trigger it.
            if(table->grow_left == 0 && !grown) {
                grown = 1;

                if(slate_table_grow(map, table) != SLATE_OK) {
                    return SLATE_ERROR_FULL;
                }

                // The layout changed: probe again from the top.
                goto retry;
            }

            char* copy = slate_key_dup(key, key_length);

            if(!copy) {
                return SLATE_ERROR_ALLOC;
            }

            slate_slot_write(group, slate_ctz64(empty) >> 3, h2, copy, key_length, value);

            ++table->used;
            ++map->used;

            // This protects against wrap if growth was refused.
            if(table->grow_left > 0) {
                --table->grow_left;
            }

            return SLATE_OK;
        }

        position = (position + probe + 1) & mask;
    }

    // The full probe found no empty slot (or broke out early to reuse a tombstone).
    if(deleted_group) {
        char* copy = slate_key_dup(key, key_length);

        if(!copy) {
            return SLATE_ERROR_ALLOC;
        }

        slate_slot_write(deleted_group, deleted_slot, h2, copy, key_length, value);

        ++table->used;
        ++map->used;

        return SLATE_OK;
    }

    // Every group is full of occupied slots.
    return SLATE_ERROR_FULL;
}

SLATE_API int slate_get_bytes(slate_s* map, const void* key, uint32_t key_length, void** value) {
    if(!map || !key) {
        return SLATE_ERROR_INVALID;
    }

    if(map->dir_length == 0 || !map->dir) {
        return SLATE_ERROR_NOT_FOUND;
    }

    uint64_t hash = slate_hash(key, key_length, map->seed);
    uint64_t h1 = hash >> (64 - SLATE_H1_BITS);
    uint8_t h2 = (uint8_t)(hash & 0x7F);

    // Route to the table, then probe exactly like slate_set does.
    uint64_t dir_index = h1 >> (SLATE_H1_BITS - map->global_depth);
    slate_table_s* table = map->dir[dir_index];

    uint64_t group_count = (uint64_t)(table->capacity / 8);
    uint64_t mask = group_count - 1;
    uint64_t position = h1 & mask;

    for(uint64_t probe = 0; probe < group_count; ++probe) {
        slate_group_s* group = &table->groups[position];
        uint64_t match = slate_group_match(group->ctrl, h2);

        while(match) {
            int i = slate_ctz64(match) >> 3;

            if(slate_key_equal(&group->slots[i], key, key_length)) {
                if(value) {
                    *value = group->slots[i].value;
                }

                return SLATE_OK;
            }

            match &= match - 1;
        }

        // An empty slot ends the search. Deleted slots do not stop the probe.
        if(slate_group_match_empty(group->ctrl)) {
            return SLATE_ERROR_NOT_FOUND;
        }

        position = (position + probe + 1) & mask;
    }

    return SLATE_ERROR_NOT_FOUND;
}

SLATE_API int slate_delete_bytes(slate_s* map, const void* key, uint32_t key_length) {
    if(!map || !key) {
        return SLATE_ERROR_INVALID;
    }

    if(map->dir_length == 0 || !map->dir) {
        return SLATE_ERROR_NOT_FOUND;
    }

    uint64_t hash = slate_hash(key, key_length, map->seed);
    uint64_t h1 = hash >> (64 - SLATE_H1_BITS);
    uint8_t h2 = (uint8_t)(hash & 0x7F);

    uint64_t dir_index = h1 >> (SLATE_H1_BITS - map->global_depth);
    slate_table_s* table = map->dir[dir_index];

    uint64_t group_count = (uint64_t)(table->capacity / 8);
    uint64_t mask = group_count - 1;
    uint64_t position = h1 & mask;

    for(uint64_t probe = 0; probe < group_count; ++probe) {
        slate_group_s* group = &table->groups[position];
        uint64_t match = slate_group_match(group->ctrl, h2);
        uint64_t empty = slate_group_match_empty(group->ctrl);
        int found = -1;

        while(match) {
            int i = slate_ctz64(match) >> 3;

            if(slate_key_equal(&group->slots[i], key, key_length)) {
                found = i;

                break;
            }

            match &= match - 1;
        }

        if(found >= 0) {
            slate_slot_delete(map, table, group, found);

            return SLATE_OK;
        }

        // An empty slot ends the search, as in slate_get.
        if(empty) {
            return SLATE_ERROR_NOT_FOUND;
        }

        position = (position + probe + 1) & mask;
    }

    return SLATE_ERROR_NOT_FOUND;
}

SLATE_API int slate_set(slate_s* map, const char* key, void* value) {
    if(!map || !key) {
        return SLATE_ERROR_INVALID;
    }

    return slate_set_bytes(map, key, (uint32_t)strlen(key), value);
}

SLATE_API int slate_get(slate_s* map, const char* key, void** value) {
    if(!map || !key) {
        return SLATE_ERROR_INVALID;
    }

    return slate_get_bytes(map, key, (uint32_t)strlen(key), value);
}

SLATE_API int slate_delete(slate_s* map, const char* key) {
    if(!map || !key) {
        return SLATE_ERROR_INVALID;
    }

    return slate_delete_bytes(map, key, (uint32_t)strlen(key));
}

SLATE_API int slate_set_i64(slate_s* map, int64_t key, void* value) {
    unsigned char encoded[10];
    uint32_t length = slate_key_encode(encoded, 'i', (uint64_t)key);

    return slate_set_bytes(map, encoded, length, value);
}

SLATE_API int slate_get_i64(slate_s* map, int64_t key, void** value) {
    unsigned char encoded[10];
    uint32_t length = slate_key_encode(encoded, 'i', (uint64_t)key);

    return slate_get_bytes(map, encoded, length, value);
}

SLATE_API int slate_delete_i64(slate_s* map, int64_t key) {
    unsigned char encoded[10];
    uint32_t length = slate_key_encode(encoded, 'i', (uint64_t)key);

    return slate_delete_bytes(map, encoded, length);
}

SLATE_API int slate_set_u64(slate_s* map, uint64_t key, void* value) {
    unsigned char encoded[10];
    uint32_t length = slate_key_encode(encoded, 'u', key);

    return slate_set_bytes(map, encoded, length, value);
}

SLATE_API int slate_get_u64(slate_s* map, uint64_t key, void** value) {
    unsigned char encoded[10];
    uint32_t length = slate_key_encode(encoded, 'u', key);

    return slate_get_bytes(map, encoded, length, value);
}

SLATE_API int slate_delete_u64(slate_s* map, uint64_t key) {
    unsigned char encoded[10];
    uint32_t length = slate_key_encode(encoded, 'u', key);

    return slate_delete_bytes(map, encoded, length);
}

SLATE_API int slate_delete_if(slate_s* map, slate_delete_if_fn callback, void* data) {
    if(!map || !callback) {
        return SLATE_ERROR_INVALID;
    }

    for(int d = 0; d < map->dir_length; ++d) {
        // Aliased directory entries are contiguous: visit each table once.
        if(d > 0 && map->dir[d] == map->dir[d - 1]) {
            continue;
        }

        slate_table_s* table = map->dir[d];

        for(int g = 0; g < table->capacity / 8; ++g) {
            slate_group_s* group = &table->groups[g];

            // The snapshot makes the walk immune to the ctrl rewrites the deletions perform;
            // deleting never moves other entries.
            uint64_t occupied = ~group->ctrl & SLATE_CTRL_MSBS;

            while(occupied) {
                int i = slate_ctz64(occupied) >> 3;
                slate_slot_s* slot = &group->slots[i];

                if(callback(slot->key, slot->key_length, slot->value, data) != 0) {
                    slate_slot_delete(map, table, group, i);
                }

                occupied &= occupied - 1;
            }
        }
    }

    return SLATE_OK;
}

SLATE_API int slate_equal(slate_s* first, slate_s* second) {
    // Covers comparing a map with itself and, like Go's two nil maps, two NULLs.
    if(first == second) {
        return 1;
    }

    if(!first || !second || first->used != second->used) {
        return 0;
    }

    // Equal lengths plus every entry of first present in second with the same value imply equality.
    for(int d = 0; d < first->dir_length; ++d) {
        if(d > 0 && first->dir[d] == first->dir[d - 1]) {
            continue;
        }

        slate_table_s* table = first->dir[d];

        for(int g = 0; g < table->capacity / 8; ++g) {
            slate_group_s* group = &table->groups[g];
            uint64_t occupied = ~group->ctrl & SLATE_CTRL_MSBS;

            while(occupied) {
                int i = slate_ctz64(occupied) >> 3;
                slate_slot_s* slot = &group->slots[i];
                void* value = NULL;

                if(slate_get_bytes(second, slot->key, slot->key_length, &value) != SLATE_OK
                    || value != slot->value) {
                    return 0;
                }

                occupied &= occupied - 1;
            }
        }
    }

    return 1;
}

SLATE_API int slate_each(slate_s* map, slate_each_fn callback, void* data) {
    if(!map || !callback) {
        return SLATE_ERROR_INVALID;
    }

    for(int d = 0; d < map->dir_length; ++d) {
        // Aliased directory entries are contiguous: visit each table once.
        if(d > 0 && map->dir[d] == map->dir[d - 1]) {
            continue;
        }

        slate_table_s* table = map->dir[d];

        for(int g = 0; g < table->capacity / 8; ++g) {
            slate_group_s* group = &table->groups[g];

            // Occupied slots have the high bit of their control byte clear.
            uint64_t occupied = ~group->ctrl & SLATE_CTRL_MSBS;

            while(occupied) {
                int i = slate_ctz64(occupied) >> 3;
                int stop = callback(group->slots[i].key, group->slots[i].key_length,
                    group->slots[i].value, data);

                // If the callback returns a non-zero value, stop iteration and return that value.
                if(stop != 0) {
                    return stop;
                }

                occupied &= occupied - 1;
            }
        }
    }

    return SLATE_OK;
}

SLATE_API uint64_t slate_length(slate_s* map) {
    if(!map) {
        // An invalid map has length 0.
        return 0;
    }

    return map->used;
}

SLATE_API int slate_free(slate_s* map) {
    if(!map) {
        return SLATE_ERROR_INVALID;
    }

    map->used = 0;
    map->global_depth = 0;

    // The freed struct is a valid empty map that slate_set can bootstrap again, so it needs a real
    // seed.
    map->seed = slate_random_seed();

    if(map->dir_length > 0 && map->dir) {
        slate_dir_free(map->dir, map->dir_length);
        map->dir = NULL;
        map->dir_length = 0;
    }

    return SLATE_OK;
}

#ifdef __cplusplus
}
#endif

#endif // SLATE_IMPLEMENTATION_INCLUDED
#endif // SLATE_IMPLEMENTATION
