#pragma once

/// @brief Runtime contract check for the leaf drawing libraries (`src/gfx`, and anything else that
///        may only include the standard library): traps immediately if @p cond is false.
///
/// Why not `assert`: the libraries' only contract checks used to be `assert()`, which `-DNDEBUG`
/// compiles out. Every shipped build defines `NDEBUG`, so on the device the checks did not exist —
/// and the conditions they guard are not recoverable runtime conditions but memory-safety
/// preconditions. A `Canvas` built over a span shorter than its geometry writes off the end of
/// caller memory on the *first* draw call; a `PadGrid` indexed out of range reads or writes past
/// its buffer. Compiling those checks out is exactly backwards.
///
/// Why not the firmware's failure path: the app has `FREEZE_WITH_ERROR` (`io/debug/log.h` ->
/// `display->freezeWithError()`), which parks the display with a code and is the right thing for
/// an app-layer invariant. `src/gfx` is a leaf — it depends on nothing but the C/C++ standard
/// library (enforced by `scripts/check_gfx_layer.py`), so `display` is not reachable from here and
/// making it reachable would invert the layering. A leaf library therefore cannot report; it can
/// only stop.
///
/// So: an unconditional trap. `__builtin_trap()` is GCC/Clang, compiles to `udf` on the target and
/// `ud2` on the host sim, is `[[noreturn]]`, and costs 2-4 bytes plus a compare at each site.
/// The compiler also gets the postcondition for free — code after the check may assume `cond`.
/// It survives `-DNDEBUG` by construction: there is no `NDEBUG` in this file.
///
/// The cost is that a violation is a halt with no message. That is the correct trade for a
/// precondition whose alternative is silent memory corruption: a trap at the violating frame is
/// far easier to debug (the PC and backtrace are exact) than a heap smash discovered later.
///
/// @param cond Condition that must hold. Traps immediately (never returns) if false.
///
/// @warning Do NOT use this for conditions a caller can legitimately produce at runtime --
///          clipping, out-of-range values arriving from the model, a full queue. Those are
///          return values, not contract violations.
#define DELUGE_GFX_CONTRACT(cond)                                                                                      \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			__builtin_trap();                                                                                          \
		}                                                                                                              \
	} while (0)
