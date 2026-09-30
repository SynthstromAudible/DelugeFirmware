//! The functions bindgen binds besides the `deluge_*` ones. Shared by `build.rs` and
//! `harness/golden_vt_render/build.rs`, so both `sys` modules declare the same set.

/// The functions `libdeluge/scheduler.h` declares, which do not share the `deluge_` prefix
/// (`scripts/check_abi_surface.py` lists the same set). bindgen matches the allowlist against the
/// Rust name, and `yield` is a Rust keyword it renames `yield_`.
pub const SCHEDULER_API_FUNCTIONS: &str = "addRepeatingTask|addConditionalTask|ignoreForStats|\
    getAverageRunTimeForTask|getSystemTime|runTask|yield_|blockTask|unblockTask|isSDRoutineActive";

/// The other declared functions without the `deluge_` prefix: `system.h`'s critical-section pair,
/// and the MIDI/gate timer callback `signals.h` declares for the app to implement.
pub const UNPREFIXED_FUNCTIONS: &str =
    "ENTER_CRITICAL_SECTION|EXIT_CRITICAL_SECTION|midiAndGateTimerGoneOff";
