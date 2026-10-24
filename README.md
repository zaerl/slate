# Slate

[![Test](https://github.com/zaerl/slate/actions/workflows/tests.yml/badge.svg)](https://github.com/zaerl/slate/actions/workflows/tests.yml)

**A fast, single-header hash map for C, built the way Go builds its maps.**

Slate brings Go's [Swiss-table](https://abseil.io/about/design/swisstables) map, the design behind
Go 1.24's built-in `map`, to C. You get one header you drop into your project, a small API that reads
like Go's `maps` package, and the same engineering choices that keep Go's maps fast and predictable at
any size.

```c
#define SLATE_IMPLEMENTATION
#include "slate.h"

slate_s map;
slate(&map);
slate_set(&map, "answer", (void*)42);
slate_set_u64(&map, 1234567890, (void*)"also works");
slate_free(&map);
```

## Why Slate

- **Drop-in.** One header, no build system, no dependencies beyond libc. Copy `slate.h` and go.
- **Fast lookups without SIMD.** Each group of 8 slots keeps a 7-bit fingerprint per slot in a single
  `uint64_t`, and lookups check all 8 at once with portable bit tricks. Most misses are rejected
  without touching a single key.
- **No latency spikes as it grows.** Tables are capped at 1024 slots and split in two instead of
  rehashing the whole map, so a growth pause is bounded by 1024 entries whether the map holds a
  thousand entries or a hundred million.
- **Hardened by default.** Every map gets its own unpredictable seed from OS entropy, so crafted keys
  can't force collisions, and the cost is one entropy read per process.
- **Any key you need.** Strings, arbitrary byte buffers (embedded NULs and empty keys included), and
  signed or unsigned 64-bit integers, kept in separate keyspaces that can never collide.
- **Familiar API.** `clear`, `clone`, `equal`, `delete_if`, a reserve hint: if you know Go's maps,
  you already know Slate.
- **Portable and strict.** Compiles as C11 and as C++17 from the same source with `-Wall -Wextra`
  clean, and runs on Linux, macOS, Windows, BSD and WebAssembly (Emscripten and WASI).
- **Yours to configure.** Plug in a custom allocator, or make the whole library `static inline` with
  one macro.
- **Tested.** 250+ assertions covering growth, splits, tombstones, compaction and cloning, run as both
  C and C++.

## Quick start

1. Copy [`src/slate.h`](src/slate.h) into your project.
2. In **exactly one** source file, define `SLATE_IMPLEMENTATION` before including it. Include it
   before any libc header there, since the implementation sets platform feature-test macros.
3. Every other file just includes `slate.h`.

```c
#define SLATE_IMPLEMENTATION
#include "slate.h"

#include <stdio.h>

int main(void) {
    slate_s map;
    void* value = NULL;

    // Initialize the map. Nothing is allocated until the first insert.
    slate(&map);

    // Insert or replace by string key. Values are any void*.
    slate_set(&map, "answer", (void*)42);

    // Look up by string key.
    if(slate_get(&map, "answer", &value) == SLATE_OK) {
        printf("answer: %d\n", (int)(uintptr_t)value);
    }

    // Integer keys live in their own keyspace.
    slate_set_i64(&map, -7, (void*)"negative seven");
    slate_set_u64(&map, 7, (void*)"seven");

    // Any byte sequence works as a key, embedded NULs included.
    const unsigned char id[] = { 0x01, 0x00, 0xFF };

    slate_set_bytes(&map, id, sizeof(id), (void*)"binary");

    if(slate_get_bytes(&map, id, sizeof(id), &value) == SLATE_OK) {
        printf("id: %s\n", (const char*)value);
    }

    printf("%llu entries\n", (unsigned long long)slate_length(&map));

    // Remove by key.
    slate_delete(&map, "answer");

    // Free everything. The map is left empty and can be reused right away.
    slate_free(&map);

    return 0;
}
```

### Type-generic keys

Include the optional [`slate-generic.h`](src/slate-generic.h) to let the key's type pick the function
at compile time (C11 `_Generic` in C, a template in C++). Unsupported key types fail to compile.

```c
#include "slate-generic.h"

SLATE_SET(&map, "name", value); // slate_set
SLATE_SET(&map, 42, value);     // slate_set_i64
SLATE_SET(&map, 42u, value);    // slate_set_u64
SLATE_GET(&map, 42, &value);
SLATE_DELETE(&map, "name");
```

## API

Functions return a `slate_error_e` as an `int`: `SLATE_OK` (0) on success, `SLATE_ERROR_NOT_FOUND`
(1) when a key simply isn't there, and a negative code on failure (`SLATE_ERROR_INVALID`,
`SLATE_ERROR_ALLOC`, `SLATE_ERROR_FULL`). A failed operation leaves the map unchanged.

| Function | Purpose | Go equivalent |
| --- | --- | --- |
| `slate(&map)` | Initialize a map (no allocation until the first insert) | `var m map[K]V` |
| `slate_reserve(&map, n)` | Pre-size an empty map for `n` entries | `make(map[K]V, n)` |
| `slate_set(&map, "key", value)` | Insert or replace by string key | `m[k] = v` |
| `slate_get(&map, "key", &value)` | Look up by string key (`&value` may be `NULL` to test presence) | `v, ok := m[k]` |
| `slate_delete(&map, "key")` | Remove by string key | `delete(m, k)` |
| `slate_set_bytes / get_bytes / delete_bytes` | The same, keyed by a byte buffer + length | |
| `slate_set_i64 / get_i64 / delete_i64` | The same, keyed by a signed 64-bit integer | |
| `slate_set_u64 / get_u64 / delete_u64` | The same, keyed by an unsigned 64-bit integer | |
| `slate_each(&map, callback, data)` | Visit every entry; the callback can stop early | `for k, v := range m` |
| `slate_delete_if(&map, predicate, data)` | Delete every entry the predicate selects | `maps.DeleteFunc` |
| `slate_equal(&a, &b)` | 1 when both maps hold the same entries | `maps.Equal` |
| `slate_length(&map)` | Number of entries | `len(m)` |
| `slate_clear(&map)` | Remove every entry but keep the capacity | `clear(m)` |
| `slate_compact(&map)` | Rebuild at the current size, releasing capacity left by deletions | |
| `slate_clone(&copy, &map)` | Independent copy without rehashing (values copied shallowly) | `maps.Clone` |
| `slate_free(&map)` | Free everything; the map stays usable as an empty map | |

`slate_clone` initializes its destination: pass a fresh or freed map, not one holding entries.

### Recipes

**Merge one map into another** (Go's `maps.Copy`). A `slate_each` callback may not modify the map
it iterates, but writing into a *different* map is fine. Existing keys in the destination are
replaced, and an insert failure stops the iteration and becomes `slate_each`'s return value:

```c
static int merge_entry(const char* key, uint32_t key_length, void* value, void* data) {
    return slate_set_bytes((slate_s*)data, key, key_length, value);
}

int result = slate_each(&source, merge_entry, &destination);
```

**Free owned values before freeing the map.** Slate never frees your values, so release them first:

```c
static int free_value(const char* key, uint32_t key_length, void* value, void* data) {
    free(value);

    return 0;
}

slate_each(&map, free_value, NULL);
slate_free(&map);
```

### Key and value semantics

- **Keys are copied.** The map owns its copies, so you can reuse your buffer right after the call.
- **Values are borrowed.** The map stores the `void*` as-is and never frees it.
- **Keys are byte sequences.** A string key is its bytes without the NUL; the bytes API accepts
  embedded NULs and empty keys. Integer keys use a 10-byte encoding that starts with a NUL, so the
  string, signed and unsigned keyspaces never collide (only raw `slate_set_bytes` keys can reproduce
  those encodings).
- **Iteration order is unspecified** and changes between runs by design, since each map's seed
  randomizes placement. Don't modify a map from its own `slate_each` callback; use
  `slate_delete_if` to delete while iterating.

## Configuration

Define these before the include that carries `SLATE_IMPLEMENTATION`:

- `SLATE_API`: the linkage prefix for every public function (default: empty). For example,
  `#define SLATE_API static inline` in a single-file program keeps the library private to that file
  without unused-function warnings.
- `SLATE_MALLOC(size)`, `SLATE_CALLOC(count, size)`, `SLATE_FREE(pointer)`: plug in a custom
  allocator. Define all three or none.

## How it works

Slate follows Go's map internals closely. Each choice below matches Go unless noted.

- **Hashing** is xxHash64, seeded per map. Seeds come from one lazy OS entropy read per process
  (`/dev/urandom`, `rand_s` on Windows, `getentropy` on WASM), stretched through splitmix64, so every
  map gets a distinct, unpredictable seed with no per-map system call. A map that becomes empty picks
  a fresh seed for free.
- **Tables** hold up to 128 groups of 8 slots. Each group packs 8 control bytes into one `uint64_t`:
  a 7-bit fingerprint (h2) for occupied slots, `0x80` for empty, `0xFE` for deleted. Lookups scan
  all 8 bytes at once with SWAR bit tricks, no SIMD intrinsics required.
- **Probing** follows the triangular sequence (+1, +2, +3, ... groups), which visits every group
  exactly once for power-of-two group counts. The 64-bit hash splits into h1 (57 bits: table, group
  and probe selection) and h2 (7 bits: the in-group fingerprint).
- **Growth** doubles a table's groups at 7/8 load. At 128 groups (1024 slots) the table instead splits
  in two by the next h1 bit, which bounds every growth pause.
- **The directory** maps the top `global_depth` bits of h1 to tables. Splitting a table whose
  `local_depth` equals the global depth first doubles the directory, which is a pointer copy, not a
  rehash.
- **Deletion** marks a slot empty when its group still has an empty slot, and leaves a tombstone
  otherwise. Inserts reuse the first tombstone on the probe once the key is known to be absent, and
  growth and splits drop tombstones entirely.
- **One deliberate difference from Go:** Go allows modifying a map while ranging over it, which costs
  its runtime snapshot iteration and per-entry revalidation. Slate forbids modification from the
  `slate_each` callback and skips that machinery.

To watch all of this happen, see [the test CLI](#the-test-cli).

## Portability and thread safety

- Strict `-std=c11` and `-std=c++17`, 64-bit, little-endian oriented (WASM included). Big-endian
  platforms work but hash to different, equally valid values.
- Map operations are **not thread-safe**; guard a shared map with your own lock. Seed generation is
  thread-safe.
- Like Go, memory isn't returned on deletion: tables never merge and the directory never shrinks.
  Call `slate_compact` to release it, or `slate_free` to release everything.

## Building the tests

```sh
make test         # C11
make test-cpp     # the same sources and tests, compiled as C++17
make test-docker  # both, inside an Alpine container
```

## The test CLI

[`cli/cli-test.c`](cli/cli-test.c) is an interactive visualizer: every keypress changes the map and
redraws its internals, so you can watch groups fill up, tables grow and split, and tombstones appear
and disappear.

```sh
make cli
./bin/cli-test
```

Each key you press inserts a random unsigned 64-bit key. A few keys do more:

| Key | Action |
| --- | --- |
| `1`–`9` | Bulk-insert 10^n random keys (capped at a million) |
| Backspace | Delete a random entry |
| `p` | Compact the map (`slate_compact`) |
| `c` | Clear the map but keep its capacity (`slate_clear`) |
| `k` | Clone the map and check the copy with `slate_equal` |
| `v` | Look up every stored key and confirm it returns its value |
| `f` | Toggle between slot markers and fingerprints |
| `q` or Esc | Quit |

After each key the screen shows:

- **The status line:** what the key did, and the entry count.
- **The map header:** the seed, the global depth, and a live invariant check. It recomputes every
  counter from the raw control bytes and prints `OK` or the exact counter that's wrong.
- **A fill histogram:** how many groups hold 0 to 8 entries, which shows clustering at a glance.
- **Every table:** its directory range, `used`, `capacity`, `grow_left` and `local_depth`, then one
  row per group: `.` for empty, `x` for deleted, `#` for occupied. In fingerprint mode, occupied
  slots show their 7-bit h2 in hex instead. The last inserted key is highlighted in green.

```text
seed 96dcd6fbe1d00ab8  global_depth 0  invariants: OK
groups by fill 0..8: 0 0 0 0 3 3 2 3 5

dir 0  (used 100, capacity 128, grow_left 12, local_depth 0)
group   0  ####....
group   1  ######..
group   2  ########
group   3  #######.
...
```

Try `3` (1,000 keys) to see the first table split, then press Backspace a few times. A deleted slot
only turns into a tombstone (`x`) when its group is full; otherwise it goes straight back to empty.
Then press `p` to watch compaction rebuild everything. The CLI also accepts piped input, so
`printf '3pv' | ./bin/cli-test` prints a plain transcript you can script.

## Credits

- The design follows [Go's Swiss table map](https://go.dev/src/internal/runtime/maps/), explained
  in depth by the excellent [VictoriaMetrics write-up](https://victoriametrics.com/blog/go-swiss-table-map/).
- Hashing is [xxHash64](https://xxhash.com) (BSD 2-Clause, Yann Collet).
- Seed stretching is Sebastiano Vigna's public-domain [splitmix64](https://prng.di.unimi.it/splitmix64.c).
- Tests use [attractor](https://github.com/zaerl/attractor).

## License

MIT. See [LICENSE](LICENSE).
