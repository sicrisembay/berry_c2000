# berry_c2000

C2000 firmware projects and the Berry interpreter port for the TMS320F28335. The current integration is the SYS/BIOS `console_uart` application, which runs Berry in a single-owner task and exposes a UART-A REPL.

## Status

The F28335 port has passed the Phase 1-6 checks documented in [the C28 assessment](applications/console_uart/BERRY_C28_PORT_ASSESSMENT.md). The tested target workload includes byte/string semantics, REPL input and error recovery, allocator concurrency, and UART/GC bursts. Normal startup goes directly to the REPL; diagnostics are opt-in. A longer product-duration soak remains advisable.

## Repository layout

- `applications/console_uart/`: CCS/SYS/BIOS application, target Berry config and UART port.
- `components/berry/berry/`: Berry source, maintained as a Git submodule.
- `components/drivers/`: C2000 device headers and UART driver.
- `components/utility/`: shared firmware utilities.

## Tested target

- TMS320F28335, C28 float target, COFF ABI
- CCS 10.2, TI C2000 CGT 22.6.1.LTS
- SYS/BIOS 6.83.00.18 and XDCtools 3.62.00.08
- UART-A on COM22 at 115200 baud, 8N1; GPIO29 TX and GPIO28 RX

The target has 16-bit C `char` storage (`CHAR_BIT=16`). Berry represents each logical octet in one C storage unit and masks values explicitly. Filesystem and bytecode persistence, shared libraries, OS/time modules, solidification, and precompiled objects are disabled in the application config.

## Setup and build

Clone with submodules initialized:

```sh
git clone --recurse-submodules <repository-url>
```

Prerequisites are CCS with the versions above, Python 3, and the repository's Kconfig `defconfig`/`genconfig` tools. Generate the application config:

```sh
make -C applications/console_uart defconfig
```

Regenerate Berry's generated headers using the application-owned target config:

```sh
cd components/berry/berry
python tools/coc/coc -o generate src default -c ../../../applications/console_uart/berry_conf.h
```

Build the `console_uart` **Debug** configuration in CCS. The project is CCS-managed; the application Makefile only generates Kconfig/build-info files. With the standard CCS installation path, the equivalent PowerShell build is:

```powershell
& 'C:\ti\ccs1020\ccs\utils\bin\gmake.exe' -C applications/console_uart/Debug all
```

Load `applications/console_uart/Debug/console_uart.out` through the configured XDS100v2 target. The default `configs/defconfig` sets UART-A RX/TX queues to 512 entries and leaves `CONFIG_BERRY_STARTUP_DIAGNOSTICS` disabled.

To build the startup regression/allocator harness, enable `BERRY_STARTUP_DIAGNOSTICS` in Kconfig (`make -C applications/console_uart menuconfig`) and rebuild. The normal image starts the VM, prints `Berry ready`, then enters the REPL. The diagnostic image additionally runs Phase 5 checks and reports allocator and heap/stack telemetry before entering the REPL.

## Documentation

- [Berry C28 port assessment and target results](applications/console_uart/BERRY_C28_PORT_ASSESSMENT.md)
- [Berry integration and build guide](applications/console_uart/BERRY_INTEGRATION.md)
- [Berry upstream README](components/berry/berry/README.md)