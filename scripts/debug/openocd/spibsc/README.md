# Deluge SPIBSC OpenOCD driver

`renesas_spibsc` is a custom OpenOCD flash driver for the Renesas RZ/A1L's SPIBSC0
controller as wired on the Deluge (Winbond W25Q32JV via channel 0, XIP window at
`0x18000000`). It enables `program`, `flash erase_sector`, and a custom
`renesas_spibsc smart_program` command that uploads the image to SDRAM, CRC-sweeps
the destination range, and only re-programs 4 KiB sub-sectors whose hash differs
(typically ~10x faster than `program` for incremental rebuilds).

`make flash` (or `./dbt flash`) uses this driver via `openocd-0.12.0/src/openocd`.

## Building

```sh
./scripts/debug/openocd/spibsc/build-openocd.sh
```

The script:

1. `git clone --recurse-submodules --depth 1 --branch deluge-spibsc-v0.12.0` from
   <https://github.com/heeen/openocd>. That branch is an openocd v0.12.0 +
   SPIBSC patch checkout — no separate patch step. The patch itself
   ([`openocd-0.12.0-spibsc.patch`](openocd-0.12.0-spibsc.patch)) is kept in
   this directory as documentation of what changed vs. upstream; the build
   script will fall back to clone-upstream-and-apply-patch if you point it
   at upstream via `OPENOCD_REPO=https://github.com/openocd-org/openocd.git
   OPENOCD_TAG=v0.12.0`.
2. `./bootstrap && ./configure --prefix=$OPENOCD_PREFIX && make && make install`.
   The default prefix is the DBT-toolchain openocd directory
   (`toolchain/v<N>/<sys>-<arch>/openocd`), so the patched binary
   **replaces the stock xPack openocd in place**. After this, you have one
   openocd binary on the system; it has our `renesas_spibsc` driver; and
   it's already on `$PATH` because `scripts/toolchain/dbtenv.sh` injects
   `toolchain/.../openocd/bin` for every dbt task.

If DBT re-fetches the toolchain (which would overwrite our binary with
the stock xPack one), just re-run `build-openocd.sh`.

Environment overrides:

| Variable | Default | Purpose |
|---|---|---|
| `OPENOCD_REPO` | `https://github.com/heeen/openocd.git` | openocd remote (fork with patch pre-applied) |
| `OPENOCD_TAG` | `deluge-spibsc-v0.12.0` | Branch/tag to clone |
| `OPENOCD_BUILD_DIR` | `build/openocd-spibsc` | Source/build directory |
| `OPENOCD_PREFIX` | `toolchain/v<N>/<sys>-<arch>/openocd` | `make install` destination |
| `OPENOCD_CONFIGURE_FLAGS` | `--enable-cmsis-dap --disable-werror` | Passed to `./configure` |

System prerequisites:

| Platform | Status | Setup |
|---|---|---|
| Linux (x86_64 / arm64) | Supported | `apt install build-essential autoconf automake libtool pkg-config texinfo libhidapi-dev libusb-1.0-0-dev libjim-dev` |
| macOS (x86_64 / arm64) | Supported | `brew install autoconf automake libtool pkg-config hidapi libusb` |
| Windows | Not yet | Use Linux/macOS to produce `openocd.exe`, copy into `toolchain/win32-x64/openocd/bin/` |

(`libjim-dev` / homebrew `jim` is only needed if you disable bundled jimtcl; the
default `--recurse-submodules` clone pulls it in so the system package isn't
strictly required.)

The script bails with a clear message on Windows shells (MSYS/Cygwin) rather
than failing inside `./configure` — building openocd-msvc with our driver is a
separate Visual-Studio + msys2 effort we haven't tackled.

## What's in the patch

- `src/flash/nor/renesas_spibsc.c` — driver implementation (~1500 lines).
- `contrib/loaders/flash/renesas_spibsc/` — ARM source for two on-chip helpers:
  - `handler.{c,h,ld}` — the mailbox handler that lives at `0x20200100`. OpenOCD
    uploads it, then issues SPI transactions by writing commands to a fixed
    mailbox in RAM and reading status back.
  - `renesas_spibsc.{S,ld}` — the trampoline that sets up CP15 / cache / MMU
    state and jumps to the C handler.
- `src/flash/nor/Makefile.am` and `drivers.c` — register the new driver.
- `tcl/target/renesas_rza1l.cfg` — generic Renesas RZ/A1L target config.

## Flash bank declaration

The driver is wired up in this repo's openocd cfgs (`scripts/debug/openocd/deluge-*.cfg`):

```
flash bank deluge.flash renesas_spibsc 0x18000000 0 0 0 $_CHIPNAME.cpu 0x3fefa000 0
```

`0x18000000` is the SPI XIP window base; `0x3fefa000` is the SPIBSC controller's
register base. Sectors 0–6 (the bootloader region) are software-write-protected
by the driver as a safety guardrail. Use `flash protect 0 0 6 off` first if you
need to overwrite them.
