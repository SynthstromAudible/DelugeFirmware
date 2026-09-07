#pragma once
#include "shell/scene.h"

#include <cstdint>
#include <optional>

namespace deluge::ui::shell {

/// @brief One thing outside the scene stack that also wants first (or last) look at routed
///        input: the mod-knob bar, the global transport/shift/record handling, MIDI learn.
///
/// The `Service` interface is deliberately the same shape as `Scene::handle`/`leds`/`tick` --
/// it is a scene-shaped participant in the chain that is not itself a scene, because it is not
/// pushed or popped and has no render surface of its own. A minimal service overrides nothing at
/// all: every hook already does nothing and returns "unhandled".
///
/// Split out of `router.h` (rather than defined alongside `Router`/`Services`) so that a class
/// that only needs the `Service` interface -- `Notices` (`shell/notices.h`), which is a `Service`
/// but neither builds nor calls a `Router` -- does not have to pull in `Router`'s own machinery
/// just to get its base class.
class Service {
public:
	virtual ~Service() = default;

	/// @brief React to one routed event, exactly as `Scene::handle` does.
	/// @param event The event.
	/// @param ctx   The world around it.
	/// @return Whether the event was handled, and what should happen next.
	virtual Result handle(const UiEvent& event, const UiContext& ctx) { return {}; }
	/// @brief Declare this service's LED and knob-indicator states for the tick.
	/// @param frame The tick's LED declarations, composed alongside every scene's.
	virtual void leds(LedFrame& frame) {}
	/// @brief Advance any time-based state.
	/// @param now_ms The tick's wall clock.
	/// @param dt_ms   Milliseconds since this service was last ticked.
	virtual void tick(uint32_t now_ms, uint32_t dt_ms) {}
	/// @brief When this service next needs a `tick()` even with no input.
	/// @return Milliseconds from now, or `std::nullopt` for "no timer".
	/// @see timer_repaints, for which surfaces (if any) that deadline arms.
	virtual std::optional<uint32_t> next_timer() const { return std::nullopt; }

	/// @brief Which surfaces `next_timer()`'s deadline should repaint.
	///
	/// The `Service` half of `Scene::timer_repaints`. **The two hooks share one return type and one
	/// member default -- `RepaintSurfaces{}` is `{false, false}` for both -- and differ only in what
	/// their un-overridden bodies return:** `Scene`'s returns `{true, true}`, this one returns
	/// nothing armed. The reason is history, not taste: until E4 task 5 a service timer was *only* a
	/// wake for `Shell::compute_next_wake`, and every existing service was written on that
	/// understanding, so returning `{true, true}` here would have silently started arming both gates
	/// for `ModKnobs`, `GlobalControls`, `Notices` and `Screensaver` -- a behaviour change none of
	/// them asked for.
	///
	/// **What this docstring used to say, and why it was wrong** (E4 final review, I1): it said the
	/// default was `{false, false}` and left the reader to conclude that an override naming one
	/// field got `false` for the other. That was true of *this body* and false of the *type*, whose
	/// members then defaulted to `true`, so the natural override
	/// `return {.grid = true};` arrived with `oled == true` and armed an unconditional per-timer
	/// blit on every OLED panel. The type's members are now `false` (`Scene::RepaintSurfaces`), so
	/// the sentence is finally true of both, and a designated initializer here can only arm what it
	/// names.
	///
	/// **A service has no stack position, so there is no "can it paint" narrowing.**
	/// `Shell::tick`'s step 8b skips a *scene* whose surface is covered
	/// (`Shell::oled_paintable`/`grid_paintable`), because a covered scene arming a gate buys a
	/// repaint it can never appear in. A service draws nothing at all -- what it arms is the
	/// repaint of whichever scene reads its state -- so the claim is honoured unconditionally.
	/// That makes the OLED half the dangerous one, exactly as it is for a scene: the grid's flush
	/// diffs against `last_sent` and blits nothing when a repaint changed no pixel, while an OLED
	/// surface is blitted unconditionally once it has rendered. Claim `oled` only for a timer
	/// whose deadline really does change OLED content.
	///
	/// @warning That last sentence had nothing behind it until E4 task 14, which is the reason
	///          this warning exists: the paragraph above is advice, and the next service to
	///          override this was one docstring away from an unconditional periodic blit on every
	///          panel. Three things stand behind it now, and the order matters because the first
	///          two are what make the third a tripwire rather than the whole defence.
	///          1. **The type.** `Scene::RepaintSurfaces`' members are `false`, so an override can
	///             only arm what it names. Until E4's final review they were `true`, and
	///             `return {.grid = true};` silently claimed the OLED as well.
	///          2. **`tests/spec/shell/flush_spec.cpp`**, whose two service legs are the only
	///             machine check on step 8c: "Claiming the OLED: the other branch, and the one
	///             that had only a docstring" (the first cut set only `{false, false}` and
	///             `{false, true}`, so deleting step 8c's whole `if (surfaces.oled)` block left all
	///             98 host tests green), and "a service claiming `{.grid = true}` arms no OLED
	///             gate", which is written in the dangerous spelling itself.
	///          3. **`check_layer_rules.py`'s `services-no-oled-timer-claim` row**, zero-baseline
	///             over `src/shell/**` *and* `src/scenes/services/**` -- it was scoped to the
	///             latter alone until E4's final review, which left `Notices` and `Screensaver`,
	///             two live services in this very directory, outside it. `audit_service_scope()`
	///             now fails the checker if a `Service` is ever declared where the row cannot see
	///             it.
	///
	///          It is a ratchet and not a ban: if your service really does need the OLED armed,
	///          make the case in the commit message, run `--update`, and extend flush_spec's OLED
	///          leg to match.
	///
	/// @return `{false, false}` by default: the timer is a wake and nothing more.
	virtual Scene::RepaintSurfaces timer_repaints() const { return {.oled = false, .grid = false}; }
};

/// @brief The "does nothing, claims nothing" `Service` -- the default for every unfilled slot in
///        `Services` until a real implementation exists.
class NullService final : public Service {};

/// @brief The single, shared `NullService` instance every unfilled `Services` slot points at.
///
/// An inline function so the function-local `static` is one instance across every translation
/// unit that includes this header, exactly as an `inline` variable would be, without needing
/// C++17's inline-variable syntax for a class that must stay hidden behind a reference.
/// @return The shared instance.
inline Service& null_service() {
	static NullService instance;
	return instance;
}

} // namespace deluge::ui::shell
