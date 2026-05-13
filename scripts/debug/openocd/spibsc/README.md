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

1. `git clone --recurse-submodules --depth 1 --branch v0.12.0` from
   <https://github.com/openocd-org/openocd> into `build/openocd-spibsc/`.
   Pinned to `v0.12.0` because the patch is against that tag's layout;
   override with `OPENOCD_TAG=<tag>` to try a newer release (you'll likely
   need to refresh the patch).
2. Applies [`openocd-0.12.0-spibsc.patch`](openocd-0.12.0-spibsc.patch) — adds the
   driver source, the in-RAM mailbox handler under
   `contrib/loaders/flash/renesas_spibsc/`, the RZ/A1L target config, and the
   two-line `Makefile.am`/`drivers.c` registration.
3. `./bootstrap && ./configure --prefix=$OPENOCD_PREFIX && make && make install`.
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
| `OPENOCD_REPO` | `https://github.com/openocd-org/openocd.git` | Upstream remote |
| `OPENOCD_TAG` | `v0.12.0` | Tag/branch to clone |
| `OPENOCD_BUILD_DIR` | `build/openocd-spibsc` | Source/build directory |
| `OPENOCD_PREFIX` | `toolchain/v<N>/<sys>-<arch>/openocd` | `make install` destination |
| `OPENOCD_CONFIGURE_FLAGS` | `--enable-cmsis-dap --disable-werror` | Passed to `./configure` |

System prerequisites for the build (Debian/Ubuntu names):

```
build-essential autoconf automake libtool pkg-config texinfo
libhidapi-dev libusb-1.0-0-dev libjim-dev
```

(jim-dev is needed because we disable bundled jimtcl via the upstream defaults.)

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
