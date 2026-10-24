# AGENTS.md

Slate is a single-header C hash map that ports Go's Swiss-table map (directory + extendible
hashing + 8-slot groups with SWAR control bytes). The same source must compile cleanly as **C11 and
C++17**. `README.md` is the user-facing spec: its API table, key/value semantics and Design section
are a contract, so keep them in sync with any behavior change.

## Layout

| Path | What it is |
| --- | --- |
| `src/slate.h` | The whole library: public API on top, implementation under `#ifdef SLATE_IMPLEMENTATION` |
| `src/slate-generic.h` | Optional `SLATE_SET/GET/DELETE` dispatch (`_Generic` in C, templates in C++) |
| `tests/*.c` | One test file per feature, built into a single runner |
| `tests/test.c` | Test `main`; the only test file that defines `SLATE_IMPLEMENTATION` and `ATT_IMPLEMENTATION` |
| `tests/helpers.c` | Counting allocator, failure injection, invariant checker (no tests of its own) |
| `tests/attractor.h` | Vendored test framework ([attractor](https://github.com/zaerl/attractor)); do not edit |
| `cli/cli-test.c` | Interactive visualizer that dumps tables/groups and live-checks invariants |
| `bin/` | Build output (gitignored); never edit or commit |

## Commands

```sh
make test         # build and run the suite as C11   (bin/test)
make test-cpp     # same sources compiled as C++17   (bin/test-cpp)
make test-docker  # both, on Alpine/gcc inside Docker
make cli          # build bin/cli-test (interactive; needs a TTY, so don't run it unattended)
make clean
```

Always run **both** `make test` and `make test-cpp` after a change; a change is not done until both
report all test cases valid with no compiler warnings. Flags are `-Wall -Wextra -Wshadow
-Wpointer-arith -Wvla -Wwrite-strings` with strict `-std=c11` / `-std=c++17` (no GNU extensions).
On macOS `gcc` is Apple clang; Docker covers real gcc.

## Writing tests

- Add `tests/<feature>.c`; the Makefile picks it up by wildcard, and `ATT_TEST(name)` self-registers
  via a constructor, so no runner edits are needed.
- Include `"attractor.h"` then `"../src/slate.h"` (or `"../src/slate-generic.h"`). Never define
  `SLATE_IMPLEMENTATION` outside `tests/test.c`.
- Assertions are `ATT_ASSERT(value, expected, "message")`, plus `_NE`, `_LT`, `_GE`, `_TRUE`, etc.
  Messages are lowercase descriptions of the expected behavior ("a missing key is not found").
- For bulk loops, count failures into an `int failed` and assert it once rather than asserting per
  iteration.
- Use enough keys (the suites use 3000) to cross growth and table splits when testing structural
  behavior; tables split past 1024 slots / 896 entries.
- Cover the `NULL` argument → `SLATE_ERROR_INVALID` paths for new public functions.
- End every test by freeing each map: `ATT_ASSERT_NE(slate_free(&map), SLATE_ERROR_INVALID, ...)`.
  The test build routes allocations through a counting allocator, and the runner fails on any
  allocation still alive at exit.
- `tests/helpers.h` has the shared tools: `test_check(&map)` re-derives every invariant and returns
  the number of violations (assert it is 0 after structural changes), `test_alloc_fail_at(n)`
  injects an allocation failure, `test_fill_until_alias` builds a map with an aliased directory, and
  `test_random` is a seeded generator. Use a fixed seed so failures reproduce.
- Callbacks mark unused parameters with `(void)name;` so `make tidy` passes.
- Code must also be valid C++: cast `void*` results explicitly, and pass integers as values via
  `(void*)(uintptr_t)n`.

## Code style

Match the existing code exactly:

- 4-space indents, no tabs, lines ≤ 100 columns, wrapped continuation lines indented 4 spaces.
- `if(`, `for(`, `while(` with no space before the paren; braces always, even for one-liners; a
  blank line before `return` and between logical blocks.
- `snake_case` everywhere. Public symbols `slate_*`, types end in `_s` (structs), `_e` (enums),
  `_fn` (callbacks); macros `SLATE_*`. Internal helpers are `static` with the `slate_` prefix.
- Every public function is declared `SLATE_API`, returns `int` (`slate_error_e`) except
  `slate_length`, and validates its pointer arguments first.
- `//` comments, full sentences, explaining *why* or the invariant, not restating code. Each public
  declaration gets a comment; Go equivalents are named where they exist ("like Go's `maps.Clone`").
- Every file starts with the MIT license block comment used in the existing files.
- No VLAs, no SIMD intrinsics, no compiler-specific code without an `#if defined(_MSC_VER)` /
  fallback pair. Allocate only through `SLATE_MALLOC` / `SLATE_CALLOC` / `SLATE_FREE`.

## Invariants not to break

- **Error contract**: `SLATE_OK` is 0, real errors are negative, `SLATE_ERROR_NOT_FOUND` is +1 (a
  normal outcome). Failed operations leave the map unchanged.
- **Ownership**: keys are copied and owned by the map; values are borrowed `void*` and never freed.
- **Key encoding**: integer keys are a 10-byte form `\0`, tag (`'i'`/`'u'`), 8 value bytes, so they
  never collide with string keys. Changing it breaks `cli-test.c`, which mirrors it.
- **Hash split**: xxHash64 → h1 = top 57 bits (directory, group, probe), h2 = low 7 bits
  (fingerprint). Control bytes: `0x80` empty, `0xFE` deleted, `0x00–0x7F` occupied.
- **Tables**: power-of-two group counts, max 128 groups; growth at 7/8 load doubles groups, a full
  table splits by the next h1 bit. `dir_length == 1 << global_depth`, each table has exactly
  `2^(global_depth - local_depth)` contiguous directory aliases, and per-table `used` sums to
  `map->used`. `tests/split.c` audits these; `cli_check_invariants` in `cli/cli-test.c` does too.
- **Aliasing**: anything walking `map->dir` must skip repeated entries (`dir[d] == dir[d - 1]`) to
  visit or free each table once.
- **Lifecycle**: `slate()` allocates nothing; `slate_free` leaves a reusable empty map with a fresh
  nonzero seed. Deletion never shrinks memory; only `slate_compact` / `slate_free` do.
- **Iteration**: `slate_each` / `slate_delete_if` callbacks must not modify the map being iterated.
  This is a deliberate divergence from Go; don't add snapshot iteration.
- **Implementation preamble**: feature-test macros (`_GNU_SOURCE` for WASM, `_CRT_RAND_S` for
  Windows) sit before any libc include; keep it that way.
- Not thread-safe by design, except seed generation (atomics), which must stay thread-safe.

## Portability

Targets Linux, macOS, Windows (MSVC), BSD and WebAssembly (Emscripten/WASI), 64-bit,
little-endian oriented. Platform-specific code (entropy source, `ctz`, atomics) lives in small
`#if` blocks near the top of the implementation; extend those rather than scattering `#ifdef`s.

## When changing behavior

1. Update `src/slate.h` (declaration comment + implementation).
2. Add or update a test in `tests/`, then run `make test && make test-cpp`.
3. Update `README.md` (API table, semantics, Design) and, if it touches internals the visualizer
   reads, `cli/cli-test.c`.
4. If a new macro or function belongs in the generic header, add both the `_Generic` and C++
   template branches in `src/slate-generic.h`.
