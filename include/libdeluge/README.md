# `libdeluge`: the platform boundary

These headers are the C ABI between the portable Deluge application (`src/deluge`) and the platform that runs
it. One platform implements them: the Rust BSP (`src/bsp/rust`, on deluge-sdk's `deluge-bsp`), on the device and,
under the `host_app` feature, on the Embassy host that `dbt sim` and the gates run.

## Rules

1. **C, and only C, crosses.** Every declaration is inside `extern "C"`. Only fixed-width integers, `bool`, and
   POD structs and enums with an explicit underlying type cross. No STL, no exceptions, no allocation across the
   boundary, and no unwinding: every function is `DELUGE_NOEXCEPT`.
2. **The header is the contract.** bindgen reads every header (`src/bsp/rust/wrapper.h` and
   `src/bsp/rust/harness/golden_vt_render/wrapper.h`, both just `#include <libdeluge/libdeluge.h>`), and every
   Rust export is pinned to its generated declaration by `abi_pin!` (`src/bsp/rust/src/abi_pins.rs`), including
   its `in path:` and rename forms. A crate-defined type that crosses the boundary is re-exported into `sys`
   under the header's name and checked field-for-field, or variant-for-variant, by `abi_layout!`. A copied
   constant is pinned by `abi_const!`. The pre-commit hook `python3 scripts/check_abi_surface.py --check --pins`
   fails when a declared function has no definition or no pin. Change a signature in the header, and the build
   tells you every place that disagrees.
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
- answers are stamped with the card generation, so after an ejection an old answer is never collected;
- a request whose buffer the platform may still touch is reported by the channel's `holds()`.

`file_prefix.h`, `file_directory.h`, `file_bytes.h`, `file_write.h`, `audio_load.h` and `streaming_fill.h` are
the channels. Before boot finishes (`deluge::boot::finished()`), the app uses blocking calls instead.

## Host-only declarations

Declarations inside `#ifdef DELUGE_HOST` (the `*_set_latency_ms` knobs, `deluge_trigger_clock_host_edge`) exist
only in the Embassy host build, for the gates. A device image never defines them.
