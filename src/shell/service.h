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
	virtual std::optional<uint32_t> next_timer() const { return std::nullopt; }
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
