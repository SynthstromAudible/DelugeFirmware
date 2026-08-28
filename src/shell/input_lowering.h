#pragma once
#include "libdeluge/control_surface.h"
#include "shell/fixed_vector.h"
#include "shell/profile.h"
#include "shell/scene.h"
#include "ui/core/frame_context.h"
#include "ui/core/input.h"
#include "ui/core/repaint_gate.h"

#include <cstdint>

namespace deluge::ui::shell {

/// @brief The timing and feature facts the lowering needs, all of them settings the firmware
///        reads out of flash rather than constants.
///
/// The defaults are the legacy defaults, converted to milliseconds: `FlashStorage::holdTime` is
/// `defaultHoldTime (2) * kSampleRate / 20` audio samples (`storage/flash_storage.cpp:349-350`),
/// which is 100 ms at 44.1 kHz; `kShortPressTime` is `kSampleRate / 2`
/// (`definitions_cxx.hpp:1028`), 500 ms; and `DetentedEncoder::applyEdges` takes two A-pin edges
/// -- one quadrature cycle -- per detent click (`hid/encoders.cpp:28-34`).
struct LoweringConfig {
	/// A press shorter than this counts as a tap for the sticky-shift rule.
	uint16_t hold_time_ms = 100;
	/// A press shorter than this counts as short for `was_short_press()`.
	uint16_t short_press_ms = 500;
	/// A-pin edges per detent click on a `EncoderKind::Detented` encoder; 0 is read as 1.
	uint8_t edges_per_detent = 2;
	/// Whether a tap on shift latches it (the `ShiftIsSticky` runtime feature). Settable after
	/// construction through `set_shift_is_sticky`, because the settings menu toggles it live.
	bool shift_is_sticky = false;
};

/// @brief The one object between the board's raw input and the shell's `UiEvent`s.
///
/// It owns every piece of input state the legacy firmware scattered across `Buttons::`
/// (`hid/buttons.cpp`), `MatrixDriver::padStates` (`hid/matrix/matrix_driver.cpp:65-84`) and the
/// encoder accumulators (`hid/encoders.cpp`): which buttons and pads are down, when each press
/// began, whether a press has been consumed, the sticky-shift latch, and each encoder's partial
/// detent. From that it produces three things per tick -- the routed `UiEvent` queue (`next()`),
/// the held-back queue (`held_back()`), and the toolkit's `ui::InputSnapshot` (`snapshot()`).
///
/// # The tick
///
/// `begin_tick(now_ms)` -> feed events -> `end_tick()` -> render -> drain `next()` and, for what
/// nothing claimed, `held_back()`. `begin_tick` clears the per-tick state (detent sums, the pad
/// event buffer, the press edges, the held-back queue) and calls `ui::FrameContext::begin_tick`
/// so claims never survive a tick; `end_tick` assembles the snapshot the frames read. The routed
/// queue is deliberately *not* cleared by `begin_tick`: it is a FIFO drained by `next()`, so an
/// event that arrived while the shell was busy is still delivered on a later tick.
///
/// # The held-back split
///
/// Five encoder turns (`Select`, `GoldUpper`, `GoldLower`, `Horizontal`, `Vertical`), the select
/// encoder's own push (`EncoderButton{SelectEnc}`) and `Button{Back}` are the inputs the toolkit's
/// widgets own: a widget claims them while it is being drawn, so routing them to a scene before
/// the frame has been built would consume them twice. Those go to `held_back()`, which the shell
/// delivers *after* render and only for the inputs `ctx().claims` shows nothing owned. Everything
/// else -- pads, every other button (`TempoEnc`, `XEnc`, `YEnc`, `ModEnc0`, `ModEnc1` included),
/// the `Tempo` encoder and `AllReleased` -- comes out of `next()` in arrival order.
///
/// They are held back from *routing* only. Their bookkeeping runs exactly as every other event's
/// does, and they still reach the snapshot every tick, so `f.turn()`, `f.select_pressed()` and
/// `f.back_pressed()` read them the same way regardless.
///
/// # The gold-knob wiggle debounce
///
/// An `EncoderKind::Accelerating` encoder does not act on every turn. The first turn after it has
/// been still for `LoweringConfig::short_press_ms` is recorded but swallowed, and a turn that
/// exactly cancels the recorded one is written off as an accidental wiggle and swallowed too; only
/// a turn that carries on past the recorded direction is lowered. Ported from the mod-knob block
/// of `interpretEncoders` as it stood before the encoder overhaul (`hid/encoders.cpp:242-292` at
/// `e460593c8^`) -- the overhaul dropped the `turnedRecently` half and left the surviving
/// `modEncoderInitialTurnDirection` test unreachable (`hid/encoder_input.cpp:178-205`). A swallowed
/// turn produces no `UiEvent` and contributes nothing to the snapshot, but still counts as input.
///
/// # What is dropped
///
/// A matrix cell mapped to `ButtonId::None`, a matrix coordinate at or past
/// `BoardProfile::button_cols`/`button_rows`, an encoder index past
/// `BoardProfile::encoder_by_index`, and a pad coordinate outside `ui::PadMask`'s 32x32 bounds are
/// all discarded without touching any state -- a board that reports something its profile does not
/// describe is not a reason to write off the end of a table.
///
/// @note No heap, no globals: every queue is a fixed-capacity member.
class InputLowering {
public:
	/// @brief How many lowered events the routed FIFO holds before the oldest is dropped.
	static constexpr uint8_t kQueueCapacity = 16;
	/// @brief How many board events are held while the SD routine has the lock.
	static constexpr uint8_t kDeferredCapacity = 8;
	/// @brief How many pad events one tick's snapshot can carry.
	///
	/// A 33rd pad event in one tick is missing from `snapshot().pads` but still reaches
	/// `held_pads()` and `next()`, so only a widget reading the tick's edges loses it. Every pad on
	/// the Deluge's grid would have to change state in a single tick to reach that.
	static constexpr uint8_t kPadEventCapacity = 32;
	/// @brief How many widget-owned events one tick can hold back.
	///
	/// Ten is one more than a tick can actually produce: five held-back encoder roles turning once
	/// each, plus press and release of both `SelectEnc` and `Back`.
	static constexpr uint8_t kHeldBackCapacity = 10;

	/// @brief Build a lowering for one board.
	/// @param profile The board's tables. Copied in -- a `BoardProfile` is a handful of spans over
	///                board-owned storage, so copying it costs nothing and removes any question of
	///                the caller's temporary outliving the lowering.
	/// @param config  Timing and feature settings; the defaults are the legacy defaults.
	explicit InputLowering(const BoardProfile& profile, LoweringConfig config = {});

	/// @name Feeding input
	/// @{
	/// @brief Lower one event from `deluge_control_poll_event`.
	///
	/// `DELUGE_EVENT_PAD`, `DELUGE_EVENT_BUTTON` and `DELUGE_EVENT_NO_PRESSES` are the device's
	/// three shapes. `DELUGE_EVENT_ENCODER` never arrives on this channel on the Deluge
	/// (`deluge.cpp:276`) but the host injector uses it; its `x` is the encoder index and its
	/// `value` is a signed count of A-pin *edges*, the same unit `deluge_encoder_take_edges`
	/// reports, so it is handed straight to the encoder path.
	/// @param ev     The event.
	/// @param now_ms The wall clock at which it arrived.
	void on_board_event(const DelugeInputEvent& ev, uint32_t now_ms);
	/// @brief Lower the A-pin edges one encoder has accumulated -- the device path
	///        (`deluge_encoder_take_edges`, folded in by `hid/encoder_input.cpp:56-63`).
	///
	/// @note Unlike `on_board_event`, this is never deferred by `sd_busy()`: the legacy
	///       `applyEdges` runs unconditionally and leaves the detents pending in the encoder's own
	///       accumulator for whenever the routine ends, and the routed FIFO plays that part here.
	///       `input_locked()` does drop it, matching the stem-export bail-out at
	///       `encoder_input.cpp:66-68`.
	/// @param index  The board's encoder index, looked up in `BoardProfile::encoder_by_index`.
	/// @param edges  Signed A-pin edge count since the last call.
	/// @param now_ms The wall clock.
	void on_encoder_edges(uint8_t index, int8_t edges, uint32_t now_ms);
	/// @}

	/// @name The tick
	/// @{
	/// @brief Open a tick: clear the per-tick state and the frame context's claims.
	///
	/// \p now_ms is provisional. The shell opens the window on the first *feed* since the previous
	/// tick (input arrives between ticks, and this call is what clears the held-back queue and
	/// `had_input_this_tick`), so the clock passed here is the clock that feed arrived at, not the
	/// clock of the tick the frames will be built on. `end_tick` is what stamps the final one.
	/// @param now_ms The clock the window is being opened at.
	void begin_tick(uint32_t now_ms);
	/// @brief Close a tick: stamp \p now_ms as *the* tick's clock and assemble the
	///        `ui::InputSnapshot` the frames read.
	///
	/// The snapshot's `now_ms` and `FrameContext::tick_ms` both come from here rather than from
	/// `begin_tick`, so `f.now_ms()` -- which every widget's animation, gesture threshold and
	/// `request_repaint_after` deadline is measured against -- is the clock the frame is being
	/// built on and never a feed's arrival time one tick period earlier.
	///
	/// @note Does not re-run `FrameContext::begin_tick`: the claims are cleared and the tick
	///       counter bumped exactly once per tick, by `begin_tick`.
	/// @param now_ms This tick's wall clock.
	void end_tick(uint32_t now_ms);
	/// @}

	/// @name Draining
	/// @{
	/// @brief Take the next routed event.
	/// @param out Filled in with the event when one is available.
	/// @return False when the queue is empty (\p out is untouched).
	bool next(UiEvent& out);
	/// @brief This tick's widget-owned events, in arrival order.
	///
	/// The shell delivers these after render, skipping the ones `ctx().claims` shows a widget
	/// owned. Cleared by the next `begin_tick()`.
	/// @return The held-back queue.
	const FixedVector<UiEvent, kHeldBackCapacity>& held_back() const { return held_back_; }
	/// @brief How many widget-owned events have been dropped for want of room.
	///
	/// Unlike the two FIFOs, this queue drops the *newest*: it is read whole rather than popped,
	/// and an over-full tick means more widget-owned events arrived than a tick can hold, so the
	/// prefix is the part with the press edges in it. Should stay at zero (`kHeldBackCapacity` is
	/// sized past what a tick can produce); non-zero means that reasoning is wrong.
	uint32_t held_back_dropped() const { return held_back_dropped_; }
	/// @}

	/// @name Input state
	/// @{
	/// @brief Which buttons are down right now.
	const ButtonSet& held() const { return held_; }
	/// @brief Which pads are down right now, in absolute grid coordinates.
	const PadMask& held_pads() const { return held_pads_; }
	/// @brief Is shift active, physically held or latched?
	bool shift() const { return shift_pressed_ || shift_stuck_; }
	/// @brief Is the sticky-shift latch engaged?
	bool sticky() const { return shift_stuck_; }
	/// @brief Drop the sticky-shift latch and the pressed flag together.
	///
	/// `Buttons::clearShiftSticky` (`buttons.cpp:302-306`): the way out of a latch that nothing is
	/// going to release, since the physical button is long since up.
	void clear_shift_sticky();
	/// @brief Turn the sticky-shift feature on or off at runtime.
	///
	/// The `ShiftIsSticky` settings toggle (`menu_item/runtime_feature/shift_is_sticky.cpp:25-27`),
	/// which calls `clearShiftSticky()` when it is switched off -- otherwise a shift latched under
	/// the old setting would stay on with nothing left to clear it.
	/// @param sticky The new setting.
	void set_shift_is_sticky(bool sticky);
	/// @brief Is the sticky-shift feature enabled?
	bool shift_is_sticky() const { return cfg_.shift_is_sticky; }
	/// @brief Has \p b's current press already been consumed?
	///
	/// The generalisation of the legacy `recordButtonPressUsedUp` / `selectButtonPressUsedUp`
	/// pair (`hid/buttons.cpp:39-43`): one bit per button, cleared when the button goes down.
	/// @param b The button.
	/// @return True if something has claimed the press.
	bool press_used_up(ButtonId b) const { return used_up_.has(b); }
	/// @brief Mark \p b's current press consumed, so its release does not act as well.
	///
	/// @note The bit belongs to the button whose *release* should be suppressed, which is not
	///       always the button that was pressed: legacy's single `recordButtonPressUsedUp` is
	///       written on a **Play** press (`buttons.cpp:172`) to stop the following Record release
	///       acting, so that caller must pass `ButtonId::Record`, not `ButtonId::Play`.
	/// @param b The button whose current press is consumed.
	void mark_press_used_up(ButtonId b) { used_up_.set(b, true); }
	/// @brief When \p b's current press began.
	/// @param b The button.
	/// @return The `now_ms` of the press, or 0 if \p b is not held.
	uint32_t held_since(ButtonId b) const;
	/// @brief Was \p b's most recent press shorter than `LoweringConfig::short_press_ms`?
	///
	/// The legacy record-release test (`buttons.cpp:193-194`), which reads the press time *after*
	/// the release, so this deliberately does not require \p b to still be held.
	/// @param b      The button.
	/// @param now_ms The clock to measure against.
	/// @return True if the press began less than `short_press_ms` ago; false for a button that has
	///         never been pressed, which a bare timestamp compare would otherwise call a short
	///         press for the first `short_press_ms` after construction.
	bool was_short_press(ButtonId b, uint32_t now_ms) const;
	/// @brief Did anything the lowering accepted arrive this tick?
	///
	/// What the shell passes to `ui::RepaintGate::needs_frame`'s `had_input`.
	bool had_input_this_tick() const { return had_input_; }
	/// @brief The board tables this lowering was built with.
	const BoardProfile& profile() const { return profile_; }
	/// @}

	/// @name The toolkit's view
	/// @{
	/// @brief This tick's input as the widgets see it, assembled by `end_tick()`.
	///
	/// @warning Only valid between `end_tick()` and the next `begin_tick()`. Read mid-tick it
	///          carries the *previous* tick's scalars over a `pads` span into a buffer that is
	///          being refilled in place -- `begin_tick` resets the buffer's length without zeroing
	///          it. `pads` also views this object's own storage, so the snapshot must not outlive
	///          the lowering.
	const ui::InputSnapshot& snapshot() const { return snap_; }
	/// @brief The tick's one shared frame context: this snapshot, the widgets' gate, and the
	///        claims every frame built this tick records into.
	///
	/// There is exactly one per tick rather than one per surface, because a claim is shell-wide:
	/// there is one turn detent, one pair of gold knobs, one select encoder and one back button
	/// per tick however many surfaces exist, so a claim made deep inside one surface's frame tree
	/// has to land where the post-render delivery reads it back.
	ui::FrameContext& ctx() { return ctx_; }
	/// @brief The gate a widget arms through `f.gate()`.
	///
	/// @note The shell evaluates `needs_frame()` on this once per tick and folds the answer into
	///       every surface's own gate check, rather than trying to attribute one widget's
	///       animation to the surface it happened to be drawn on. Nothing else may consume it.
	ui::RepaintGate& widgets_gate() { return widgets_gate_; }
	/// @}

	/// @name Deferral and locking
	/// @{
	/// @brief Take or release the SD-routine lock.
	///
	/// While set, `on_board_event` queues instead of lowering; releasing it replays the queue in
	/// arrival order, each event with the clock it arrived at. `DELUGE_EVENT_NO_PRESSES` is
	/// deferred like everything else rather than dropped -- dropping it strands whatever the
	/// firmware still believes is held (issue #3168, `deluge.cpp:262-271`).
	///
	/// @note Dropping the oldest can separate a press from its release, so a scene that pairs the
	///       two may see a release for a button it never saw pressed. `held()` stays correct
	///       either way, and the alternative -- dropping the newest -- would strand the *held*
	///       state instead, which is the worse failure.
	/// @param busy True to take the lock.
	void set_sd_busy(bool busy);
	/// @brief Is the SD-routine lock taken?
	bool sd_busy() const { return sd_busy_; }
	/// @brief How many deferred events have been dropped to make room for newer ones.
	uint32_t deferred_dropped() const { return deferred_dropped_; }
	/// @brief Take or release the input lock (stem export, song load).
	///
	/// Everything is dropped while it is set except `DELUGE_EVENT_NO_PRESSES`, which still runs:
	/// the release path must not be the thing a lock strands.
	/// @param locked True to take the lock.
	void set_input_lock(bool locked) { input_locked_ = locked; }
	/// @brief Is the input lock taken?
	bool input_locked() const { return input_locked_; }
	/// @brief How many routed events have been dropped because nothing drained the FIFO.
	///
	/// Should stay at zero; a non-zero count means the shell missed ticks.
	uint32_t queue_dropped() const { return queue_dropped_; }
	/// @brief Release everything held: a synthetic release per held button and pad, then
	///        `AllReleased`.
	///
	/// The safety net behind `DELUGE_EVENT_NO_PRESSES` (`matrix_driver.cpp:53-64`,
	/// `buttons.cpp:315-323`). The synthetic releases run through the same path a real release
	/// takes, so the sticky-shift rule sees a released shift exactly as it would have, and each
	/// lands in whichever queue that event would normally have gone to.
	/// @param now_ms The clock to record the releases at.
	void clear_all_held(uint32_t now_ms);
	/// @}

	/// @brief The gold-knob acceleration multiplier for one turn, ported from
	///        `ContinuousEncoder::calcNextKnobSpeed` (`hid/encoders.cpp:46-77`).
	///
	/// Speed builds while the knob keeps turning the same way and resets when it stops matching the
	/// previous turn or pauses for longer than the threshold. Unlike the legacy version, whose
	/// direction and timestamp were function-level `static`s shared by both gold knobs, the state
	/// here is per encoder role.
	///
	/// @note The reset test compares \p delta to the previous turn for *equality*, as legacy does,
	///       so one detent followed by two also resets the ramp -- it is not only a sign change.
	/// @note Two calls in the same millisecond would divide by a zero interval and latch the speed
	///       at infinity for the rest of the gesture, so a zero interval is read as one
	///       millisecond, the smallest this clock can represent.
	/// @param role   Which encoder turned.
	/// @param delta  The signed turn; compared against the previous turn, not scaled.
	/// @param now_ms The wall clock.
	/// @return A multiplier in [1.0, 3.0].
	double calc_next_knob_speed(EncoderRole role, int8_t delta, uint32_t now_ms);

private:
	/// @brief One board event waiting out the SD routine, with the clock it arrived at.
	struct DeferredEvent {
		DelugeInputEvent ev{};
		uint32_t now_ms = 0;
	};

	void dispatch(const DelugeInputEvent& ev, uint32_t now_ms);
	void defer(const DelugeInputEvent& ev, uint32_t now_ms);
	void apply_encoder_edges(uint8_t index, int8_t edges, uint32_t now_ms);
	void on_pad(uint8_t x, uint8_t y, uint8_t velocity);
	void on_matrix(uint8_t x, uint8_t y, bool on, uint32_t now_ms);
	void on_button(ButtonId b, bool on, uint32_t now_ms);
	bool action_gold_turn(size_t role_index, int8_t delta, uint32_t now_ms);
	void toggle_shift(bool on, uint32_t now_ms);
	void emit(const UiEvent& e);
	void push_out(const UiEvent& e);

	static constexpr size_t kButtonCount = static_cast<size_t>(ButtonId::Count);
	static constexpr size_t kEncoderCount = static_cast<size_t>(EncoderRole::Count);

	BoardProfile profile_;
	LoweringConfig cfg_;

	/// @name Button and pad state (the legacy `Buttons::` globals and `MatrixDriver::padStates`)
	/// @{
	ButtonSet held_;
	ButtonSet used_up_;
	ButtonSet ever_pressed_; ///< Whether `press_time_` has ever been written, for `was_short_press`.
	PadMask held_pads_{};
	uint32_t press_time_[kButtonCount]{}; ///< The clock of each button's most recent press.
	bool shift_pressed_ = false;
	bool shift_stuck_ = false;
	bool consider_shift_release_for_sticky_ = false;
	uint32_t shift_pressed_at_ = 0;
	/// @}

	/// @name Encoder state
	/// @{
	int32_t edge_accumulator_[kEncoderCount]{};      ///< Partial detents, carried between ticks.
	int8_t tick_delta_[kEncoderCount]{};             ///< What each encoder contributed to this tick.
	double knob_speed_[kEncoderCount]{};             ///< `ContinuousEncoder::currentKnobSpeed`, per role.
	int8_t last_knob_offset_[kEncoderCount]{};       ///< `calcNextKnobSpeed`'s `last_offset`, per role.
	uint32_t last_knob_ms_[kEncoderCount]{};         ///< `calcNextKnobSpeed`'s `last_encoder_time`, per role.
	int8_t initial_turn_direction_[kEncoderCount]{}; ///< `modEncoderInitialTurnDirection`, per role.
	uint32_t last_turn_ms_[kEncoderCount]{};         ///< `timeModEncoderLastTurned`, per role.
	/// @}

	/// @name Queues
	/// @{
	UiEvent queue_[kQueueCapacity]{};
	uint8_t queue_head_ = 0;
	uint8_t queue_count_ = 0;
	uint32_t queue_dropped_ = 0;
	FixedVector<UiEvent, kHeldBackCapacity> held_back_;
	uint32_t held_back_dropped_ = 0;
	DeferredEvent deferred_[kDeferredCapacity]{};
	uint8_t deferred_head_ = 0;
	uint8_t deferred_count_ = 0;
	uint32_t deferred_dropped_ = 0;
	/// @}

	/// @name Per-tick state, reset by `begin_tick`
	/// @{
	FixedVector<ui::PadEvent, kPadEventCapacity> pad_events_;
	uint32_t now_ms_ = 0;
	bool had_input_ = false;
	bool select_edge_ = false;
	bool back_edge_ = false;
	/// @}

	bool sd_busy_ = false;
	bool input_locked_ = false;

	ui::InputSnapshot snap_{};
	ui::RepaintGate widgets_gate_;
	ui::FrameContext ctx_{snap_, widgets_gate_};
};

/// @brief Is \p b an encoder's own push rather than a plain matrix button?
///
/// Decides between `UiEvent::Kind::EncoderButton` and `UiEvent::Kind::Button`.
/// @param b The button.
/// @return True for the six `*Enc` ids.
bool is_encoder_button(ButtonId b);

} // namespace deluge::ui::shell
