//! The functions bindgen binds besides the `deluge_*` ones.

/// The functions `libdeluge/scheduler.h` declares, which do not share the `deluge_` prefix.
pub const SCHEDULER_API_FUNCTIONS: &str = "getSystemTime|isSDRoutineActive";

/// The other declared functions without the `deluge_` prefix: `system.h`'s critical-section pair,
/// and the MIDI/gate timer callback `signals.h` declares for the app to implement.
pub const UNPREFIXED_FUNCTIONS: &str =
    "ENTER_CRITICAL_SECTION|EXIT_CRITICAL_SECTION|midiAndGateTimerGoneOff";
