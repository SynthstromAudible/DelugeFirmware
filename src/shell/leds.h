#pragma once
#include "shell/scene.h"
#include "shell/service.h"
#include "shell/surfaces.h"
#include "ui/core/led_frame.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace deluge::ui::shell {

/// @brief Is a declared LED lit at \p now_ms?
///
/// The whole of the legacy `indicator_leds` blinker machinery, reduced to a pure function of the
/// declaration and the clock. Legacy kept a `LedBlinker` table (`hid/led/indicator_leds.h:71-79`)
/// whose phase was advanced by a `TimerName::LED_BLINK`/`LED_BLINK_TYPE_1` callback
/// (`indicator_leds.cpp:94,108-112`, at `kFlashTime` 110 ms / `kFastFlashTime` 60 ms,
/// `definitions_cxx.hpp:159-160`) and whose "blink N times then go back" case counted
/// `blinksLeft` down before calling `setLedState(..., returnToState)`
/// (`indicator_leds.cpp:115-130`). Here the phase is `now_ms / period_ms` -- so two LEDs declared
/// with the same period blink *together* whatever tick they were first declared on, which the
/// per-blinker timers could not guarantee -- and the countdown is an absolute deadline,
/// `until_ms`, after which the LED is simply on. That is the "blink N times, then an alert folds
/// back to On" shape without any per-LED state for the shell to keep.
///
/// @param s      The declared state.
/// @param now_ms The tick's wall clock.
/// @return `true` for `On`; `false` for `Off`; for `Blink`, `true` once \p now_ms has reached a
///         non-zero `until_ms`, otherwise the even half of the `period_ms` phase. A `Blink` with a
///         zero `period_ms` has no phase to be in and reads as lit, rather than dividing by zero.
bool led_is_on(const LedState& s, uint32_t now_ms);

/// @brief When will `led_is_on(s, …)` next answer differently?
///
/// **A declared blink is a wake source, and until E4 task 4 nothing treated it as one.**
/// `led_is_on` derives a blink's phase from `now_ms` alone, and `Shell::compute_next_wake`
/// considered gate deadlines, scene timers and service timers -- so on an otherwise idle system a
/// declared `Kind::Blink` simply froze at whatever phase the last tick left it in. That was live:
/// `MenuScene::leds` declares `Back` and `Learn` as `Blink(kFlashTime)` unconditionally
/// (`scenes/menu/menu_scene.cpp:931`, `:938`) while `MenuScene::next_timer`
/// (`scenes/menu/menu_scene.cpp:1593-1607`) returns `std::nullopt` for any focused node with
/// neither source pads nor a shortcut position -- which is most of the tree. `Shell::tick`'s
/// wake loop now asks this for every entry in the composed frame, so the phase moves on its own.
///
/// @param s      The declared state.
/// @param now_ms The tick's wall clock.
/// @return Milliseconds from now until the answer flips, or `std::nullopt` for a state that never
///         changes on its own (`On`, `Off`, a zero-period blink, and a blink whose `until_ms` has
///         already latched it solid).
std::optional<uint32_t> led_next_change(const LedState& s, uint32_t now_ms);

/// @brief Composes one tick's LED declarations from the services and the scene stack.
///
/// A struct with one static rather than a free function, so the composition order has a name to
/// cite: **services first, then the stack bottom to top**, each participant calling
/// `Scene::leds`/`Service::leds` on the same frame. `LedFrame::set` overwrites an already-declared
/// id, so the *last* writer wins -- which makes the top scene the authority for every LED it
/// mentions while the scenes and services beneath it supply the rest. That is the legacy
/// arrangement (`indicator_leds::setLedState` was called from whichever UI last ran) turned into a
/// rule rather than an accident of call order.
struct LedCompose {
	/// @brief Declare every participant's LEDs into \p out.
	///
	/// Does not clear \p out first: the caller owns the frame's lifetime and clears it once per
	/// tick (`Shell::derive_leds`), which keeps this composable with a frame that already carries
	/// board-level declarations.
	/// @param out                   The frame to declare into.
	/// @param services              The services, consulted first and therefore outranked by
	///                              every scene.
	/// @param stack_bottom_to_top   The scene stack, root first.
	static void compose(LedFrame& out, std::span<Service* const> services, std::span<Scene* const> stack_bottom_to_top);
};

/// @brief Does board LED \p board_led's desired state differ from what was last sent?
///
/// The predicate `Shell::flush` (Task 8) drives `SurfaceSet::set_led` from, factored out here so
/// this task's specs can pin the diff without a flush to run. An LED nothing declared this tick is
/// never sent: the shell drives the LEDs the stack asked for, not every LED the board has.
/// @param s         The LED surface, with `desired_*` filled in by `Shell::derive_leds`.
/// @param board_led The board LED index (`LedId`'s underlying value).
/// @return True when \p board_led was declared this tick and either has never been sent or has
///         changed since it was.
bool led_send_needed(const LedSurface& s, size_t board_led);

} // namespace deluge::ui::shell
