#pragma once
#include "shell/scene_stack.h"
#include "shell/service.h"

#include <cstdint>
#include <optional>

namespace deluge::ui::shell {

/// @brief Pushes and pops the idle-animation scene after a configurable period with no input.
///
/// Ports the *policy* half of legacy's `hid::display::Screensaver`
/// (`hid/display/screensaver.h:35-106`): when to show and hide the screensaver, not what it
/// draws. The real animation (`rainfall`/`starfield`) is spec E3's `ScreensaverScene`, registered
/// by the application under `SceneKind::Screensaver` exactly like any other scene; this service
/// only knows the kind and pushes/pops it by name, so it neither owns nor constructs one -- a spec
/// short of the real scene supplies its own stand-in at that slot.
///
/// @note `idle_ms == 0` disables the service outright, the port of legacy's `ScreensaverMode::OFF`
///       (`screensaver.cpp:92-94,102-104,171-174`): `tick()` never pushes and `note_activity()`
///       never re-arms the deadline.
class Screensaver final : public Service {
public:
	/// @param stack   The stack this service pushes/pops `SceneKind::Screensaver` on.
	/// @param idle_ms How long the shell may sit with no `note_activity()` call before the scene
	///                 is pushed; 0 disables the service (see the class note).
	/// @param seed    Passed through to the pushed scene's `SceneId::arg` (the low 16 bits) for
	///                 the real animation's own use (`Scene::on_enter`'s parameter); this service
	///                 never reads it back itself.
	Screensaver(SceneStack& stack, uint32_t idle_ms, uint32_t seed);

	/// @brief Register that a physical control was touched: wake the screensaver if it is
	///        showing, and re-arm the idle deadline either way.
	///
	/// Ported from `Screensaver::noteActivity` (`hid/display/screensaver.cpp:83-97`): the wake
	/// check runs first and unconditionally, exactly as legacy's own comment on that ordering
	/// warns ("sits on the input hot path... so a mode that somehow reached OFF... can never
	/// strand the panel"), and the deadline is re-armed afterwards regardless of whether the
	/// screensaver had been showing -- legacy's unconditional trailing `arm()` call.
	///
	/// `Shell::on_board_event`/`on_encoder_edges` call this before lowering the event, never
	/// instead of it: legacy's `dispatchInputEvent` calls `noteActivity()` first and then
	/// unconditionally continues into `padAction`/`buttonAction` for the very same event
	/// (`deluge.cpp:238,252`; the encoder path is the same shape, `hid/encoder_input.cpp:208`), so
	/// the press that wakes the screensaver is never swallowed by waking it.
	/// @param now_ms The wall clock the input arrived at.
	void note_activity(uint32_t now_ms);

	/// @brief Push the screensaver once idle and not already showing.
	/// @param now_ms The tick's wall clock.
	/// @param dt_ms  Unused: idle time is measured against an absolute deadline recorded by
	///               `note_activity()`/the first `tick()`, not accumulated from `dt_ms`.
	void tick(uint32_t now_ms, uint32_t dt_ms) override;

	/// @brief When the idle deadline (or nothing, while active or disabled) falls.
	std::optional<uint32_t> next_timer() const override;

	/// @brief Is the screensaver scene currently pushed?
	bool active() const { return active_; }

private:
	SceneStack& stack_;
	uint32_t idle_ms_;
	uint32_t seed_;
	/// The absolute clock the screensaver should next show at; valid only while `armed_`.
	uint32_t deadline_ms_ = 0;
	/// The clock of the last `tick()`, so `next_timer()` -- which takes no clock of its own --
	/// can report `deadline_ms_` relative to "now" the same way a scene's own timer does.
	uint32_t now_ms_ = 0;
	/// Whether `deadline_ms_` holds a real deadline yet: false only before the first `tick()` or
	/// `note_activity()` call, or whenever `idle_ms_` is 0.
	bool armed_ = false;
	bool active_ = false;
};

} // namespace deluge::ui::shell
