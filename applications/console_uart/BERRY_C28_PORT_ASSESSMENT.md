# Berry C28 Core Port Assessment

**Assessment date:** 2026-10-02
**Target:** TMS320F28335, TI C2000 compiler 22.6.1.LTS, `ti.targets.C28_float`, COFF ABI
**Related plan:** [BERRY_INTEGRATION.md](BERRY_INTEGRATION.md)

## Decision

The vendored Berry core is **not buildable or runnable unchanged** on this target. The hardware report confirms `CHAR_BIT=16`, `sizeof(char)=1`, `sizeof(int)=1`, `sizeof(void *)=2`, and `sizeof(size_t)=2`. Thus a C byte on this compiler is a 16-bit unit; pointers and `size_t` are 32-bit values, while `int` is 16-bit.

A direct compile of a translation unit containing only `#include "berry.h"` fails at `berry.h:51` because the TI compiler does not define `uint8_t`. The same toolchain does not define `int8_t`; it does provide `uint16_t`, `int16_t`, `uint32_t`, `int32_t`, `uint64_t`, and `int64_t`. The absence of 8-bit exact-width types is expected when `CHAR_BIT` is 16.

The assessment is **no-go for the existing Berry source**, but not a claim that a C28 port is impossible. Proceeding means a core portability project, not a UART-only adaptation or a configuration-only change. The recommended first experiment is an unpacked logical-octet representation: store each value in the low 8 bits of one 16-bit C `char` storage unit. This keeps array indexing and C string lengths expressed in C storage units aligned with logical-octet counts, but consumes about twice the memory of packed 8-bit storage and requires explicit range/sign semantics throughout the core. Do not create a fake global `uint8_t` typedef and assume that it restores 8-bit arithmetic.

## Evidence And Risks

| Area | Repository evidence | Port risk |
|---|---|---|
| Public type/API | `src/berry.h` includes `<stdint.h>` and declares `bbyte` as `uint8_t`. | Berry's public header fails to compile before VM sources are reached. Introduce a Berry-owned octet type instead of depending on an unavailable exact-width type. |
| Byte-buffer semantics | `src/be_byteslib.c` stores and returns `uint8_t`, serializes values into 1/2/3/4-octet fields, and casts to `int8_t` for signed reads. | A 16-bit `unsigned char` does not truncate to 0..255. Stores, shifts, sign extension, masks, and wrap behavior need explicit, tested helpers. |
| Allocator pools | `src/be_mem.c` defines `mem16`/`mem32` as arrays of 16/32 `uint8_t` values and computes pool offsets in `uint8_t *` units. | Pool dimensions must be defined in logical octets and checked after the storage representation changes. Per-octet 16-bit storage increases pool RAM use. |
| Compiler intrinsics | `src/be_mem.c` has GCC and MSVC branches for `ffs`/`popcount`; the other compiler branch ends in `#error`. | Add TI-supported implementations and test edge cases; merely fixing `uint8_t` still leaves a known compile blocker. |
| Strings and source text | `src/be_string.c` copies with `memcpy`/`strlen`/`strncmp`; strings and lengths are based on C `char` arrays. | One logical octet per C storage unit may preserve indexing, but every input/generated string must contain values 0..255, and UTF-8, embedded NUL, hashing, and comparison need target tests. |
| Generated constants | `src/be_constobj.h` creates constant byte arrays using `uint8_t[]`; Berry's `tools/coc` emits headers included from `src`. | The generator and every generated header must agree on the target octet type and representation. Regeneration must be deterministic. |
| VM pointer operations | `src/be_vm.c` uses `uint8_t *` for pointer arithmetic on compact pointers; `src/be_constobj.h` and `src/be_map.h` store compact 24-bit links/bitfields. | Audit pointer-unit arithmetic, address truncation, bitfield layout, and linker address range under the C28 COFF ABI. The measured 32-bit pointer size alone does not validate these encodings. |
| Binary files and I/O | `src/be_bytecode.c` writes/reads `uint8_t` arrays through `be_fread`/`be_fwrite`; other file APIs count `size_t` units. | Defer filesystem and bytecode persistence in the first target milestone. Any later file/wire port must explicitly translate logical octets to the host/SCI representation. |
| Memory budget | C28 logical octets take a 16-bit storage unit. The earlier baseline map was built before the current user changes to the application config. | Rebuild the current app and measure Berry with the final representation; do not reuse the old map as a budget. Include pools, generated constants, task stack, SYS/BIOS heap, and allocator overhead. |

## Recommended Port Design

1. Keep host Berry unchanged. Put target adaptations behind a C28-specific configuration/port layer and explicit source changes, so host tests remain the reference behavior.
2. Define a Berry-owned `bbyte` storage type whose invariant is `0 <= value <= 255`. On C28, one element occupies one 16-bit C storage unit. Keep `uint16_t`/`uint32_t` for numeric widths; do not alias `uint8_t` to a 16-bit type across the project.
3. Centralize conversion helpers for octet truncation (`value & 0xFF`), signed-octet interpretation (`0x80..0xFF` maps to `-128..-1`), and construction from character/input values. Use them wherever a byte value is stored or sign-extended.
4. Initially disable bytecode save/load, filesystem, shared libraries, and OS-dependent modules. This narrows I/O obligations but does not remove the public `bbyte` and core VM fixes.
5. Start with one C storage unit per logical octet rather than packed two-octet words. It is simpler to index and bridge to `char *` APIs. Measure the resulting memory cost before optimizing representation.
6. Treat pointer-compressed constants and bitfields as ABI gates. Keep precompiled objects disabled for the first bring-up if the generated layout cannot be proven correct; do not mask/truncate pointers until the target address range and relocations have been tested.

## Execution Plan

Each phase has a concrete pass condition. Stop at a failed gate; do not layer later failures on top of a broken representation.

### Phase 0: Freeze A Host Reference

**Actions:** Record the Berry revision/configuration and run its existing host test suite before target changes. Preserve the logs and host test command. Create a separate port branch/worktree so target adaptations do not silently alter host behavior.

**Pass criteria:** The unmodified host build and tests pass, the exact Berry revision/config is recorded, and the new port work is isolated from the user's application edits.

### Phase 1: Introduce The Octet Abstraction

**Actions:** Add a Berry-owned octet type and conversion helpers. Replace core `uint8_t`/`int8_t` dependencies by intent: logical-octet storage, numeric 16/32/64-bit quantities, or signed-octet interpretation. Update `berry.h` and internal headers first. Do not add a project-wide `#define uint8_t`.

**Pass criteria:** The target compiler builds a small API/type test that includes `berry.h`; test values 0, 1, 127, 128, 254, and 255 round-trip unchanged, while 256 truncates only where an API explicitly specifies octet wrapping. Signed conversions map 0x7F to 127, 0x80 to -128, and 0xFF to -1. Host builds remain unchanged and pass.

### Phase 2: Port Core Storage And Generated Objects

**Actions:** Update allocator pool declarations/offsets, VM pointer operations, constant byte/object macros, and the `tools/coc` generator to use the new octet abstraction. Audit `memcpy`, `memset`, `memcmp`, `strlen`, and `strncmp` use: document which lengths count C storage units and which count logical octets. Add target compile-time/runtime checks for compact pointer links and map bitfields. Regenerate all Berry headers from the target config.

**Pass criteria:** A clean generation produces byte-identical output on repeated runs; all required generated headers and selected core sources compile; allocator tests pass at 15/16/17 and 31/32/33 logical-octet sizes; pool allocation/free returns the same slot and survives repeated GC. Runtime checks prove compact pointers round-trip without truncation and map key/value operations preserve contents.

### Phase 3: Port Compiler And ABI Dependencies

**Actions:** Implement TI-compatible least-significant-set-bit and population-count helpers in place of the unsupported `be_mem.c` branch. Audit C28 behavior for bitfields, enum/integer promotion, `long`/`long long`, variadic formatting, and all pointer-to-integer casts. Keep numeric serialization code disabled until its octet writer/reader is explicit.

**Pass criteria:** The complete selected Berry core compiles with the exact CCS flags and COFF ABI. Bit-scan tests cover zero plus every pool bitmap boundary; population-count tests cover zero, single bits, alternating bits, and all ones. ABI tests validate object/bitfield layout and pointer encoding from the actual linker map.

### Phase 4: Validate Strings, Parser, And VM Semantics

**Actions:** Add target C/API tests for strings containing ASCII and logical octets 0x00, 0x7F, 0x80, and 0xFF; include embedded NUL through length-explicit APIs. Test ASCII and multibyte UTF-8 parsing, hashing/equality, global names, and output conversion. Run a minimal script such as `return 1 + 2`, then exceptions and repeated VM create/run/delete cycles.

**Pass criteria:** String lengths and byte values match expected logical-octet counts; comparisons/hashes agree for independently constructed equal strings; parser/VM tests produce expected values and recover after exceptions; repeated runs show no corruption or monotonic memory growth. Any C library routine that assumes an 8-bit execution character set is either proven compatible or replaced by a Berry helper.

### Phase 5: Port Byte Buffers And Reintroduce Features

**Actions:** Test byte-buffer append/read/set operations, signed reads, endian conversion, hex conversion, slicing, equality, and boundary behavior. Keep filesystem and bytecode persistence disabled initially. Add a logical-octet serializer only if persistent scripts are required, with a documented on-disk format and explicit host/target conversion.

**Pass criteria:** Byte-buffer tests cover all 256 logical values, lengths and offsets at 0/1/end/end+1, signed boundaries, and 16/24/32-bit endian cases. Serialization round-trips a host-generated fixture only after the file API is separately ported; otherwise the feature remains disabled and the build proves no file/bytecode dependency is linked.

### Phase 6: Integrate Into SYS/BIOS And Measure

**Actions:** Add Berry to the application-owned C28 port/config, start one VM in one SYS/BIOS task, and route output/input through UART-A in task context. Configure and test a thread-safe allocator and dedicated task stack. Build cleanly and inspect the link map after each feature is enabled.

**Pass criteria:** Target prints the expected script results on COM22; invalid scripts recover; sustained commands, GC, and UART traffic do not corrupt data or hang. Record peak heap, task stack high-water mark, flash/RAM section sizes, and margins. Require explicit review of the 16-bit-storage memory overhead before enabling byte buffers or larger scripts.

## Stop/Go Gates

- **Stop the unmodified integration now:** Direct `berry.h` compilation fails because `uint8_t` is absent.
- **Proceed with the core port only if:** the per-octet representation passes phases 1-4 on hardware, all Berry host tests remain green, pointer/layout assumptions are verified, and the current application map has adequate margin.
- **Do not enable binary persistence or OS modules until:** their logical-octet I/O and target filesystem contracts have independent round-trip tests.
- **Fallback:** If the core representation or memory budget fails, keep the C2000 firmware native and run Berry on an 8-bit-byte host connected over a documented UART protocol.
