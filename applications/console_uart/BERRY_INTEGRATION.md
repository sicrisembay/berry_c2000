# Integrating Berry with console_uart

This is an implementation guide, not a description of an integration that already builds. `console_uart` is a CCS/SYS/BIOS project for the TMS320F28335; its current `main()` initializes UART and starts BIOS. Berry is vendored at `components/berry/berry`. Keep the interpreter on a BIOS task, with UART interrupts restricted to the existing driver.

## Actionable Execution Plan

Complete these steps in order. Record the compiler/tool versions, test output, and measured memory figures in the change or project notes. Do not treat a successful compile as proof of runtime compatibility. Step 1 is a go/no-go gate: if the Berry core cannot correctly represent its required byte-oriented data on C28, stop the on-target interpreter effort and choose a host-side Berry process with a UART protocol instead.

### 1. Establish a clean baseline and target facts

**Action:** In CCS, clean-build the existing Debug `console_uart` project and save the build log and link map. Record the active compiler, ABI, CPU, linker command file, UART-A settings, and current RAM section usage. Build and flash the unchanged application, then verify the existing UART path with a loopback or known-good terminal test. Add a temporary target probe that reports `CHAR_BIT`, `sizeof(char)`, `sizeof(int)`, `sizeof(void *)`, and `sizeof(size_t)` over UART; remove it after recording the output.

**Pass criteria:** Baseline builds without errors, existing UART-A loopback passes, and the target facts are captured from the actual compiler/device. If the baseline cannot build or UART cannot be verified, fix that independently before attributing failures to Berry.

**Execution status (2026-10-01): Partial.** The checked-in Debug configuration builds successfully with CCS 10.2.0, C2000 compiler 22.6.1.LTS, SYS/BIOS 6.83.0.18, XDCtools 3.62.0.08, C28 float target, and COFF ABI. The baseline was imported and built headlessly in a temporary CCS workspace after running `make -C applications/console_uart defconfig`; the generated `configs/generated/autoconf.h` and CCS outputs are ignored build artifacts. The output and map are under `applications/console_uart/Debug/`.

Map baseline (sizes are C28 address units): `.text` `0x2610`, `.econst` `0x191c`, `.ebss` `0x741`, `.data` `0x8c`, and `.stack` `0x200`. The map reports FLASH used `0x4428` of `0x3ff80`, M01SARAM used `0x7cd` of `0x800`, and L07SARAM used `0x44d` of `0x8000`. There is no `.esysmem` allocation in this baseline. The build completed with two existing duplicate-typedef warnings from `DSP2833x_Device.h`; Kconfig emitted its existing quote-style warning for the `rsource` line.

**Hardware follow-up (2026-10-02): Complete.** The user loaded and ran the application in CCS; Tera Term on COM22 at 115200 8N1 showed `Hello World` and the type report. The captured values were `CHAR_BIT=16`, `sizeof(char)=1`, `sizeof(int)=1`, `sizeof(void *)=2`, and `sizeof(size_t)=2`. This establishes that the target's addressable C byte is 16 bits, not 8.

The temporary type-report code has been removed from `console_uart.c`, and the restored application rebuilt successfully. The screenshot verifies target execution and UART-A transmit/output; a UART receive loopback was not part of this check. External DSS attempts failed while the interactive CCS session was in use, but CCS itself successfully loaded and ran the application.

### 2. Decide whether Berry can run on this data model

**Action:** Audit Berry's assumptions about 8-bit octets, including string storage/lengths, UTF-8, byte buffers, parser input, generated constant strings, bytecode, and hashing. Write a small target test that checks byte round-trips through Berry strings and verifies string lengths and equality for ASCII plus a multibyte UTF-8 sequence. Include a byte-buffer round-trip if that module will be enabled. Do not use a `char *` cast as the UART conversion test.

**Pass criteria:** The interpreter can represent and process the tested octets with documented, bounded conversion rules on the C28 data model, and tests pass on the actual target. If core representation or compiler semantics make this impractical, mark on-device Berry **no-go** and switch to the host-side alternative. Do not continue to feature integration on the assumption that a successful link proves correctness.

**Execution status (2026-10-02): NO-GO for unmodified Berry.** The target report measured `CHAR_BIT=16`; `sizeof(char)=1` therefore means one 16-bit C byte, not an 8-bit octet. A direct compile of a file including Berry's public `berry.h` with the configured TI C2000 compiler fails at `berry.h:51`: `uint8_t` is undefined. The compiler's `<stdint.h>` does not provide an exact 8-bit integer type for this target, and Berry declares `bbyte` as `uint8_t` in that public header. `uint8_t` is also used by core VM, memory-pool, bytecode, and byte-buffer code, so disabling optional modules alone does not make the unmodified interpreter compile. Step 2 cannot pass without a C28-specific port.

This is a no-go for the current Berry source on this data model, not proof that no interpreter could ever be ported to C28. A Berry port would need an explicit logical-octet representation and a systematic audit of every `uint8_t`/`int8_t` use, 0..255 arithmetic/masking, string/byte lengths and C library calls, generated constant data, and binary I/O. Simply typedefing `uint8_t` to 16-bit `unsigned char` would not restore 8-bit wraparound or exact-width semantics. The UART conversion by itself is insufficient. The next decision is whether to fund that core port or use Berry on an 8-bit-byte host and communicate with the C2000 over UART.

### 3. Resolve TI compiler portability

**Action:** Compile Berry's `src/be_mem.c` with the project's TI compiler and replace its unsupported-compiler `ffs()` `#error` path with a TI-compatible implementation. Add focused checks for zero and representative nonzero bitmasks, including the bit positions used by Berry's allocation pools. Build any other Berry translation units that expose compiler-specific errors and fix only those portability issues.

**Pass criteria:** `be_mem.c` and the selected Berry source set compile with the project's exact compiler/flags; the bit-scan tests return the expected least-significant-set-bit index, and no compiler-specific branch is left untested. If this compiler work fails, stop and reassess compiler support before proceeding.

### 4. Make the Berry source set reproducible

**Action:** Copy `default/berry_conf.h` into an application-owned port/config directory. For the initial in-memory console, disable filesystem, bytecode save/load, shared-library loading, and the OS module; keep the script compiler enabled. From `components/berry/berry`, run `python tools/coc/coc -o generate src default -c <path-to-target-berry_conf.h>` using Python 3. Preserve generated headers as build inputs or document a repeatable generation step. In CCS, remove the Berry source exclusion but keep `default/berry.c`, `default/be_port.c`, tests, examples, tools, modules, and generated headers out of C source compilation. Add the Berry `src` and target-config include paths.

**Pass criteria:** Generated `generate/be_const_strtab.h` and required `generate/be_fixed_*.h` files exist; a CCS clean build compiles each selected `src/*.c` and application port exactly once, selects the target config in every translation unit, and does not compile the desktop `main()` or default host port. The build log and project configuration make those facts verifiable after a clean rebuild.

### 5. Provide and test Berry allocation

**Action:** Determine which C RTS allocation functions are linked by the target and whether they are safe in the intended BIOS task context. `BIOS.heapSize = 0x0` does not configure the C RTS heap. Configure the linker heap and `.esysmem`, or implement an application allocator, then map Berry's `BE_EXPLICIT_MALLOC`, `BE_EXPLICIT_FREE`, and `BE_EXPLICIT_REALLOC` consistently. Add a small allocator test covering allocate, grow, preserve contents, free, and exhaustion. Measure heap use for VM creation, a representative script, and VM deletion.

**Pass criteria:** All allocator tests pass on target; forced exhaustion returns a controlled Berry allocation failure without corrupting neighboring data or hanging; repeated VM create/run/delete cycles return to the expected baseline. Record peak heap use and retain sufficient headroom for SYS/BIOS, UART queues, and application tasks.

### 6. Implement and test the UART port

**Action:** Add an application-owned port implementing Berry's `be_writebuffer()` and `be_readstring()`; do not compile `default/be_port.c`. Bridge to `UART_send()`/`UART_receive()` in task context. Handle short TX writes by retrying unsent data without blocking an ISR. Implement bounded line input, CR/LF handling, backspace, NUL termination, overlong-line policy, and the explicit C28-to-8-bit-wire conversion established in Step 2. Use UART-A's configured 115200 baud, GPIO29 TX, and GPIO28 RX.

**Pass criteria:** A host terminal can send and receive every byte in the supported test set; tests cover empty input, CR, LF, CRLF, backspace, maximum-length and overlong lines, TX queue saturation/short writes, and repeated lines. No Berry, line editing, or blocking work runs in an interrupt handler. If disabled features still leave unresolved `be_sys.h` symbols, identify the referencing module and either provide an explicit unsupported-operation stub or remove the dependency; never link the desktop port as a workaround.

### 7. Run a Berry smoke test in a BIOS task

**Action:** Create one SYS/BIOS task with a dedicated, measured stack. Keep `UART_init()` before `BIOS_start()`. In the task, create a VM, execute `be_dostring(vm, "print('Berry ready')")`, report exceptions, and delete the VM. Give the VM a single task owner; do not invoke it from UART ISRs or concurrently from multiple tasks.

**Pass criteria:** On the target, the console prints exactly `Berry ready`, VM creation and deletion succeed, and the task completes without stack overflow, watchdog reset, or memory corruption. Run the smoke test repeatedly. Capture the task stack high-water mark and increase its configured size with documented margin.

### 8. Add the interactive REPL and exercise errors

**Action:** Keep the VM alive in its task and call `be_repl(vm, get_line, free_line)` using `src/be_repl.h`. Ensure `get_line` returns writable NUL-terminated input and a defined EOF result; pair heap-backed input with `free_line`, or use `NULL` only for a reusable buffer with a lifetime that satisfies the REPL. Exercise arithmetic, strings, multi-line input, an invalid script, repeated commands, and line overflow behavior.

**Pass criteria:** Each valid command produces the expected output; invalid input reports an exception and the REPL accepts the next command; multi-line input completes; boundary/overlong input follows the documented policy; no leak or memory growth is observed over a fixed repeated-command run. Verify both UART queues remain responsive while Berry executes.

### 9. Perform final clean-build and resource acceptance

**Action:** Delete only generated build artifacts through CCS clean, regenerate Kconfig output if needed (`make defconfig` from `applications/console_uart`) and Berry headers from the target config, then rebuild Debug from scratch. Inspect the linker map for `.text`, `.econst`, `.ebss`, `.esysmem`, and stack allocations. Flash with the configured XDS100v2 and repeat UART/REPL tests under sustained input and output.

**Pass criteria:** A clean checkout/build procedure reproduces the firmware; all Berry sources and generated headers are accounted for; the map fits the device with recorded RAM/flash margin; sustained console tests pass without dropped or corrupted data, resets, stack overflow, or allocation failure. Archive the exact tool versions, commands, test results, and resource measurements. Do not call the integration complete until this target acceptance passes.

## 1. Check target compatibility first

The CCS project uses C2000 compiler 22.6.1.LTS, the C28 COFF ABI, and the `ti.targets.C28_float` target. Before adding sources, compile a small target-side check for `CHAR_BIT`, `sizeof(char)`, `sizeof(int)`, pointer width, and `sizeof(size_t)`. C28 targets commonly have 16-bit `char` (`CHAR_BIT == 16`). Berry's strings, bytecode, byte buffers, UTF-8 handling, and allocator pools assume 8-bit octets in several places. A successful link does **not** establish that the VM works on this data model. Audit and adapt those assumptions throughout Berry before claiming F28335 support; choosing `BE_USE_SINGLE_FLOAT` or reducing modules does not solve this. In particular, test string literals, parsing, bytecode decoding, hash/string operations, and the UART byte bridge on hardware. Berry's `src/be_mem.c` also has an unsupported-compiler `ffs()` branch ending in `#error`; supply and test a TI-compatible implementation instead of assuming the GCC branch applies. If that porting work is out of scope, run Berry on an 8-bit-byte host instead and use the C2000 UART protocol as a remote endpoint.

## 2. Generate Berry's headers on the host

Berry's `src/*.c` includes files under `../generate/`; these are not checked in. On a machine with Python 3 and the tools required by Berry's generator, run from `components/berry/berry`:

```sh
python tools/coc/coc -o generate src default -c default/berry_conf.h
```

The upstream `make prebuild` target invokes the same generator (and uses `python` on Windows). Confirm that `generate/be_const_strtab.h` and the `generate/be_fixed_*.h` files exist before building in CCS. Regenerate after changing the Berry configuration or its source/module definitions. Do not compile `default/berry.c`: it defines the desktop `main()`.

## 3. Add only the intended sources to CCS

Import `applications/console_uart` as an existing CCS project with its repository-relative location intact. Its `.project` links the shared `components` directory from two parents above the project. In CCS project properties, inspect the **Debug** source exclusions: `.cproject` currently excludes `components/berry|src` (the project-local `src` is also excluded). Remove the Berry exclusion, then exclude `components/berry/berry/default/berry.c`, `components/berry/berry/default/be_port.c`, Berry's `tools`, `examples`, `tests`, `modules`, and generated files from C compilation. Compile `components/berry/berry/src/*.c` and an application-owned Berry port source exactly once; verify the CCS build log rather than assuming linked folders are compiled automatically. Keep the existing driver and utility sources enabled. If CCS builds Berry sources that your chosen feature flags do not need, trim them deliberately and check the link for unresolved symbols.

Add include search paths for `components/berry/berry/src` and the directory containing your target `berry_conf.h`; retain the existing `components` and `configs/generated` paths. `berry.h` includes `berry_conf.h` by name, so ensure every translation unit sees the same target configuration. Generated includes use relative paths from `src`, not just an include-path entry. The top-level application `Makefile` only drives Kconfig/build-info generation; the firmware build is CCS-managed, not `make` in the application directory.

## 4. Provide a target configuration and memory budget

Start from `components/berry/berry/default/berry_conf.h` in an application-owned port directory. For a first in-memory script, disable unsupported or unnecessary features there: `BE_USE_FILE_SYSTEM`, `BE_USE_BYTECODE_SAVER`, `BE_USE_BYTECODE_LOADER`, `BE_USE_SHARED_LIB`, and `BE_USE_OS_MODULE`; leave `BE_USE_SCRIPT_COMPILER` enabled for interactive input. Disable optional modules only after checking their dependencies and regenerating the headers with this **same** config file. Do not edit the vendored default config just for the application.

Berry defaults to `malloc`, `free`, and `realloc` through `BE_EXPLICIT_MALLOC`, `BE_EXPLICIT_FREE`, and `BE_EXPLICIT_REALLOC`. Provide a working, thread-safe allocation source (or suitable application allocator) and point these macros at it. `BIOS.heapSize = 0x0` in `console_uart.cfg` is the BIOS heap setting, not proof that the C RTS heap is available. Check the CCS linker heap setting, `.esysmem` in `TMS320F28335.cmd`, and the available RAM. Reserve room for the VM, parser, UART queues, BIOS, and task stacks; measure actual high-water marks and allocation failures on target. `Program.stack = 512` and the CCS linker `--stack_size=0x300` are not a Berry task-stack budget.

## 5. Connect Berry to UART-A

Use an application-owned port file instead of `default/be_port.c`, which routes I/O through host `stdin`/`stdout` and supplies desktop file functions. Implement `be_writebuffer(const char *, size_t)` and `be_readstring(char *, size_t)` declared in `src/berry.h`. Use the existing `UART_send(UART_A, ..., UInt16)` and `UART_receive(UART_A, ..., UInt16)` from `components/drivers/uart/uart.h`. The default `configs/defconfig` enables UART-A on GPIO29 (TX) / GPIO28 (RX) at 115200 baud, 8-bit data; connect an appropriate voltage-level serial adapter and matching terminal settings.

`UART_send` can accept fewer characters than requested when its queue is full; send in `UInt16`-sized chunks and retry unsent data from task context, yielding appropriately. `UART_receive` is nonblocking and returns the available count; accumulate a bounded line, handle CR/LF and backspace as needed, NUL-terminate it, and return `NULL` only for a real end-of-input. Check `size > 0` and define a policy for overlong lines. On a 16-bit-`char` C28 build, **explicitly** convert between SCI's 8-bit wire values and the VM's character representation; a `char *` cast is not a byte protocol. Do not run the VM or block in an ISR. If the linker still requests `be_fopen`/other `be_sys.h` functions after disabling filesystem features, identify the referencing module and supply a clear unsupported-operation port or remove that feature; do not link the desktop port merely to silence errors.

## 6. Start Berry in a BIOS task

Keep `UART_init()` in `console_uart.c` before `BIOS_start()`. Create a SYS/BIOS `Task` (in `console_uart.cfg` or with `Task_create` before starting BIOS) with a measured, adequate stack and a single owner for the VM. In that task, a minimal in-memory test is:

```c
#include "berry.h"

bvm *vm = be_vm_new();
if (vm != NULL) {
    int result = be_dostring(vm, "print('Berry ready')");
    if (result != BE_OK) {
        be_dumpexcept(vm);
    }
    be_vm_delete(vm);
}
```

This is the task body, not a replacement for `main()` or a complete port. Once that test works, keep the VM alive and use `be_repl(vm, get_line, free_line)` from `src/be_repl.h` for an interactive console. The `get_line` callback returns a writable, NUL-terminated line or `NULL` on EOF; `free_line` releases it (or is `NULL` for a reusable static buffer). Match the buffer lifetime and line-size policy to the implementation in `src/be_repl.c`. Berry's REPL uses `be_writebuffer` for output; it needs the script compiler enabled. Keep UART and Berry calls in task context and avoid concurrent use of one VM.

## 7. Build and verify

1. From `applications/console_uart`, run `make defconfig` if `configs/generated/autoconf.h` is absent; this requires the Kconfig `defconfig` and `genconfig` commands used by `configs/Makefile`. Generate Berry headers separately as in step 2.
2. Build the **Debug** CCS configuration; inspect the compile log for exactly one copy of each selected Berry source and the application port, and inspect the link map for `.text`, `.econst`, `.ebss`, `.esysmem`, and task-stack pressure. Resolve any compiler/data-model diagnostics before flashing.
3. Load through the configured XDS100v2 target, connect UART-A at 115200 8N1, and check that the in-memory test prints `Berry ready`. Then test arithmetic, strings, an invalid script/error path, repeated commands, RX overflow behavior, and allocation failure/recovery.

This repository does not contain a completed C28 Berry port or an automated target build, so these steps require hardware/toolchain verification; the 16-bit-`char` compatibility work is the main feasibility gate.