# Agent Guide

Repository-specific guidance for coding agents working in `berry_c2000`.

## Before editing

- Inspect `git status --short --branch` in the parent repo and `git -C components/berry/berry status --short --branch` in the Berry submodule. Preserve staged and unstaged user changes; do not reset, clean, or checkout over them.
- Read the relevant application documentation before changing target behavior: `applications/console_uart/BERRY_C28_PORT_ASSESSMENT.md` and `BERRY_INTEGRATION.md`.
- Keep changes scoped. Do not stage, commit, or push unless explicitly asked.

## Repository and build

- `applications/console_uart/` is a CCS/SYS/BIOS project for the F28335. CCS builds the firmware; `make -C applications/console_uart defconfig` only regenerates Kconfig headers.
- Berry is a nested Git submodule at `components/berry/berry`. For Berry source changes, review and commit inside that repo first; then separately update the parent submodule pointer if requested.
- The tested toolchain is CCS 10.2, C2000 CGT 22.6.1.LTS, SYS/BIOS 6.83, XDCtools 3.62, C28 float, COFF ABI.
- Regenerate Berry headers from the Berry submodule directory with Python 3 and the app-owned config: `python tools/coc/coc -o generate src default -c ../../../applications/console_uart/berry_conf.h`.
- Build Debug in CCS or, with the standard Windows install, run `C:\ti\ccs1020\ccs\utils\bin\gmake.exe -C applications/console_uart/Debug all` from the repository root.

## C28 invariants

- The target has `CHAR_BIT=16`; one addressable C `char` stores one logical Berry octet. Keep `bbyte` values in `0..255`; use explicit masks/conversions and correctly sized integer types. Never add a project-wide `uint8_t` typedef.
- C28 `int` is 16 bits; pointers and `size_t` are 32 bits. Widen shifts, masks, counters, indices, and constants before operations that exceed 16 bits.
- Keep generated Berry headers and every translation unit on the same `applications/console_uart/berry_conf.h`. Do not modify Berry's default config for this app.
- Berry VM calls belong to the single `berryConsole` task. Do not call the VM from UART/HWI context or share one VM across tasks.
- `be_readstring()` must return newline-terminated chunks as specified by `berry.h`; preserve CR/LF, backspace, NUL termination, and bounded overlong-line behavior. SCI values are 8-bit wire octets carried in 16-bit C storage.
- UART-A uses 115200 8N1, GPIO29 TX / GPIO28 RX. The ring buffer overwrites the oldest byte when full; the default RX and TX queues are 512 entries. Avoid shrinking RX without repeating burst tests.

## Diagnostics and target verification

- Normal `defconfig` keeps `CONFIG_BERRY_STARTUP_DIAGNOSTICS` off and boots directly into the REPL. Enable it through Kconfig for Phase 5 regression, C-API lexer/VM probes, allocator stress, and heap/stack telemetry. Restore the default before producing a normal image.
- Run host Berry regressions when changing the core. For target-dependent behavior, build with TI CGT and verify on the F28335; a successful link is not runtime proof.
- COM22 is the XDS100 serial port. Close CCS/TeraTerm before opening it directly. Use 115200 8N1 and close any `SerialPort` in a `finally` block.
- The last accepted target workload covered Phase 5 cycles, REPL result/error recovery, multiline input, 240-character `input()`, CR/LF/CRLF/backspace/overlong input, and a 1,000-command production-mode GC soak. Diagnostics-mode telemetry and a 200-command UART/GC burst also passed. Re-run relevant hardware checks after changing the UART port, task lifecycle, allocator, or token/parser code.
- Keep filesystem and bytecode persistence disabled unless a separate logical-octet I/O/serialization contract is designed and tested.

## Lexer token table

`components/berry/berry/src/be_lexer.h` and the `token_strings` array in `src/be_lexer.c` must have exactly matching token order. `OptWalrus` (`:=`) appears before the keyword tokens. A mismatch shifts keyword recognition; verify `return`, `false`, and exception behavior after editing the table.