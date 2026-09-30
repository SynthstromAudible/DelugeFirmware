# `libdeluge`: the platform boundary

These headers are the C ABI between the portable Deluge application (`src/deluge`) and the platform that runs
it. One platform implements them: the Rust BSP (`src/bsp/rust`, on deluge-sdk's `deluge-bsp`), on the device and,
under the `host_app` feature, on the Embassy host that `dbt sim` and the gates run.

## Rules

1. **C, and only C, crosses.** Every declaration is inside `extern "C"`. Only fixed-width integers, `bool`, and
   POD structs and enums with an explicit underlying type cross. No STL, no exceptions, no allocation across the
   boundary. No function behind the ABI may throw or unwind: the Rust side aborts on panic, and the ABI requires a
   C++ callback to catch at the boundary. `DELUGE_NOEXCEPT` (`abi.h`) marks this on the declarations that already
   carry it; the rest are not yet held to it by the compiler.
2. **The header is the contract.** bindgen reads every header (`src/bsp/rust/wrapper.h` and
   `src/bsp/rust/harness/golden_vt_render/wrapper.h`, both just `#include <libdeluge/libdeluge.h>`), and every
   Rust export is pinned to its generated declaration by `abi_pin!` — beside the definition it pins (including the
   one rename form, `abi_pin!(yield_ => r#yield)` in `scheduler.rs`), and for another crate's own exports and
   imports, via its `in path:` form, in `src/bsp/rust/src/abi_pins.rs`. That file also holds the `abi_layout!`
   and `abi_const!` checks, except for `sys_host.rs`'s, which sit at the bottom of that file. A crate-defined
   type that crosses the boundary is re-exported into `sys` under the header's name and checked exhaustively,
   field-for-field or variant-for-variant, by `abi_layout!`. A copied constant is pinned by `abi_const!`. The
   pre-commit hook `python3 scripts/check_abi_surface.py --check --pins` fails when a declared function has no
   definition, or when a Rust definition or `extern "C"` import of one has no pin that reaches it. Change a signature in the header, and the build tells you every place that disagrees.
3. **Direction is one-way.** The app includes `<libdeluge/…>`. The platform implements it. `app.h` is the other
   direction: callbacks the app implements and the platform calls. `scripts/check_platform_boundary.py` keeps the
   app from including platform headers.
4. **Call context is documented per function**, as one of:
   - `[isr]`: may be called from interrupt context.
   - `[audio]`: may be called from the audio tier. It must not block, allocate or lock.
   - `[task]`: called from task context only.

## Channels

Storage is reached through request/collect channels: a request returns at once, and a collect is polled once per
tick for the answer (`channel.h`'s `DelugeChannelStatus`). Every channel follows the same rules:
- answers are keyed, and a collect with another key answers UNKNOWN and leaves the answer for its owner;
- answers are stamped with the card generation (`card.h`'s `deluge_card_flush` bumps it for every channel at
  once), so after an ejection an old answer is never collected.

`file_write.h` also holds a caller-owned buffer across ticks, and its `deluge_file_write_holds()` reports
whether the platform may still touch it; the other channels hand bytes back synchronously at the collect that
reports READY, so they need no such call.

`file_prefix.h`, `file_directory.h`, `file_bytes.h`, `file_write.h` and `audio_load.h` are the channels. Before
boot finishes (`deluge::boot::finished()`), the app uses blocking calls instead.

`streaming_fill.h` is not one of these: it is the sample-streaming fill queue (enqueue/drain of the loader
queue, plus synchronous efatfs handle calls and fill-context registration), not a keyed request/collect channel.

## Host-only declarations

Declarations inside `#ifdef DELUGE_HOST` (the `*_set_latency_ms` knobs, `deluge_trigger_clock_host_edge`) exist
only in the Embassy host build, for the gates. A device image never defines them.
