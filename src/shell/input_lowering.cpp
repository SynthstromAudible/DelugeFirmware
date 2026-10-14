#include "shell/input_lowering.h"

#include <algorithm>
#include <bit>
#include <span>

namespace deluge::ui::shell {

namespace {

/// @brief Is this encoder's turn one the toolkit's widgets own?
///
/// Everything except the tempo encoder, which no widget reads and which the shell therefore
/// routes like any other event.
bool is_held_back_encoder(EncoderRole role) {
	switch (role) {
	case EncoderRole::Select:
	case EncoderRole::GoldUpper:
	case EncoderRole::GoldLower:
	case EncoderRole::Horizontal:
	case EncoderRole::Vertical:
		return true;
	default:
		return false;
	}
}

/// @brief Is this button's edge one the toolkit's widgets own?
///
/// The select encoder's push (`f.select_pressed()`) and back (`f.back_pressed()`); every other
/// button, the other five encoder pushes included, is routed.
bool is_held_back_button(ButtonId b) {
	return b == ButtonId::SelectEnc || b == ButtonId::Back;
}

/// @brief Add \p delta to \p acc without wrapping past `int8_t`'s range.
int8_t saturating_add(int8_t acc, int delta) {
	return static_cast<int8_t>(std::clamp(static_cast<int>(acc) + delta, -128, 127));
}

} // namespace

bool is_encoder_button(ButtonId b) {
	switch (b) {
	case ButtonId::SelectEnc:
	case ButtonId::TempoEnc:
	case ButtonId::XEnc:
	case ButtonId::YEnc:
	case ButtonId::ModEnc0:
	case ButtonId::ModEnc1:
		return true;
	default:
		return false;
	}
}

InputLowering::InputLowering(const BoardProfile& profile, LoweringConfig config) : profile_(profile), cfg_(config) {
}

// -- The tick ---------------------------------------------------------------------------------

void InputLowering::begin_tick(uint32_t now_ms) {
	now_ms_ = now_ms;
	had_input_ = false;
	select_edge_ = false;
	back_edge_ = false;
	for (int8_t& d : tick_delta_) {
		d = 0;
	}
	pad_events_.clear();
	held_back_.clear();
	ctx_.begin_tick(now_ms);
}

void InputLowering::end_tick(uint32_t now_ms) {
	// The tick's clock, not the clock of whichever feed happened to open the window.
	now_ms_ = now_ms;
	ctx_.tick_ms = now_ms;
	snap_.turn = tick_delta_[static_cast<size_t>(EncoderRole::Select)];
	snap_.edit[0] = tick_delta_[static_cast<size_t>(EncoderRole::GoldUpper)];
	snap_.edit[1] = tick_delta_[static_cast<size_t>(EncoderRole::GoldLower)];
	snap_.horizontal = tick_delta_[static_cast<size_t>(EncoderRole::Horizontal)];
	snap_.vertical = tick_delta_[static_cast<size_t>(EncoderRole::Vertical)];
	snap_.select_pressed = select_edge_;
	snap_.back_pressed = back_edge_;
	snap_.shift = shift();
	snap_.pads = std::span<const ui::PadEvent>(pad_events_.begin(), pad_events_.size());
	snap_.held_pads = held_pads_;
	snap_.now_ms = now_ms_;
}

// -- Feeding ----------------------------------------------------------------------------------

void InputLowering::on_board_event(const DelugeInputEvent& ev, uint32_t now_ms) {
	if (sd_busy_) {
		defer(ev, now_ms);
		return;
	}
	// The lock stops the UI acting on input, but never on the release path: an all-released report
	// dropped here strands whatever the shell still believes is held.
	if (input_locked_ && ev.kind != DELUGE_EVENT_NO_PRESSES) {
		return;
	}
	dispatch(ev, now_ms);
}

void InputLowering::dispatch(const DelugeInputEvent& ev, uint32_t now_ms) {
	switch (ev.kind) {
	case DELUGE_EVENT_PAD:
		on_pad(ev.x, ev.y, static_cast<uint8_t>(ev.value));
		break;
	case DELUGE_EVENT_BUTTON:
		on_matrix(ev.x, ev.y, ev.value != 0, now_ms);
		break;
	case DELUGE_EVENT_ENCODER:
		apply_encoder_edges(ev.x, static_cast<int8_t>(ev.value), now_ms);
		break;
	case DELUGE_EVENT_NO_PRESSES:
		clear_all_held(now_ms);
		break;
	}
}

void InputLowering::defer(const DelugeInputEvent& ev, uint32_t now_ms) {
	if (deferred_count_ == kDeferredCapacity) {
		deferred_head_ = static_cast<uint8_t>((deferred_head_ + 1) % kDeferredCapacity);
		deferred_count_--;
		deferred_dropped_++;
	}
	deferred_[(deferred_head_ + deferred_count_) % kDeferredCapacity] = DeferredEvent{ev, now_ms};
	deferred_count_++;
}

void InputLowering::set_sd_busy(bool busy) {
	if (busy == sd_busy_) {
		return;
	}
	sd_busy_ = busy;
	if (busy) {
		return;
	}
	// Replay in arrival order, each with the clock it arrived at rather than the clock the routine
	// happened to end at, so a press's `held_since` still says when the user pressed it. The queue
	// is emptied first: nothing below can re-enter the deferral path now that the lock is clear.
	const uint8_t head = deferred_head_;
	const uint8_t count = deferred_count_;
	deferred_head_ = 0;
	deferred_count_ = 0;
	for (uint8_t i = 0; i < count; i++) {
		const DeferredEvent& d = deferred_[(head + i) % kDeferredCapacity];
		if (input_locked_ && d.ev.kind != DELUGE_EVENT_NO_PRESSES) {
			continue;
		}
		dispatch(d.ev, d.now_ms);
	}
}

void InputLowering::on_encoder_edges(uint8_t index, int8_t edges, uint32_t now_ms) {
	if (input_locked_) {
		return;
	}
	apply_encoder_edges(index, edges, now_ms);
}

void InputLowering::apply_encoder_edges(uint8_t index, int8_t edges, uint32_t now_ms) {
	if (index >= profile_.encoder_by_index.size()) {
		return;
	}
	const EncoderRole role = profile_.encoder_by_index[index];
	const size_t r = static_cast<size_t>(role);
	if (r >= kEncoderCount) {
		return;
	}
	had_input_ = true;

	const EncoderKind kind = r < profile_.encoder_kind.size() ? profile_.encoder_kind[r] : EncoderKind::Detented;
	int8_t delta = 0;
	if (kind == EncoderKind::Detented) {
		// `DetentedEncoder::applyEdges`: accumulate edges, hand over whole detents, keep the
		// remainder for next time.
		const int per = cfg_.edges_per_detent > 0 ? cfg_.edges_per_detent : 1;
		edge_accumulator_[r] += edges;
		const int detents = edge_accumulator_[r] / per;
		edge_accumulator_[r] -= detents * per;
		delta = static_cast<int8_t>(std::clamp(detents, -128, 127));
	}
	else {
		// `ContinuousEncoder::applyEdges`: the raw edge count is the magnitude; acceleration is
		// applied by whoever consumes it, through `calc_next_knob_speed`.
		delta = edges;
		if (delta != 0 && !action_gold_turn(r, delta, now_ms)) {
			return;
		}
	}
	if (delta == 0) {
		return;
	}
	tick_delta_[r] = saturating_add(tick_delta_[r], delta);

	UiEvent e{UiEvent::Kind::Encoder};
	e.encoder = role;
	e.delta = delta;
	emit(e);
}

bool InputLowering::action_gold_turn(size_t role_index, int8_t delta, uint32_t now_ms) {
	// The mod-knob block of `interpretEncoders` as it stood before the encoder overhaul
	// (`hid/encoders.cpp` lines 242-292 as of `e460593c8^` -- pinned to that revision and
	// therefore deliberately NOT written in the live `file:line` form, since `check_citations.py`
	// resolves against the current tree, where that file is 107 lines). The overhaul kept the
	// `modEncoderInitialTurnDirection` test but dropped the `turnedRecently` branch that is the
	// only thing that ever sets the field, so the surviving code can no longer reach its own
	// wiggle case (`hid/encoder_input.cpp:178-205`); this restores the working shape.
	const bool turned_recently = (now_ms - last_turn_ms_[role_index]) < cfg_.short_press_ms;
	last_turn_ms_[role_index] = now_ms;
	if (!turned_recently) {
		// The first turn after a pause is not acted on: remember which way it went, so that going
		// straight back the other way can be written off as an accidental wiggle.
		//
		// Legacy also closes the action logger's pending value change here when the *other* gold
		// knob has been still too; that is an undo-history concern rather than a lowering one, and
		// belongs to whoever owns the mod knobs.
		initial_turn_direction_[role_index] = delta;
		return false;
	}
	if (delta + initial_turn_direction_[role_index] == 0) {
		// Exactly cancels the remembered turn: an accidental wiggle.
		initial_turn_direction_[role_index] = delta;
		return false;
	}
	initial_turn_direction_[role_index] = 0;
	return true;
}

void InputLowering::on_pad(uint8_t x, uint8_t y, uint8_t velocity) {
	if (x >= PadMask::kCols || y >= PadMask::kRows) {
		return;
	}
	had_input_ = true;
	if (velocity != 0) {
		// `deluge.cpp:245-247` calls `Buttons::ignoreCurrentShiftForSticky()` on every pad press,
		// so shift held over a pad press does not latch when shift is then tapped off.
		consider_shift_release_for_sticky_ = false;
	}
	const bool down = velocity != 0;
	held_pads_.set(x, y, down);
	pad_events_.push_back(ui::PadEvent{x, y, velocity, down});

	UiEvent e{UiEvent::Kind::Pad};
	e.x = x;
	e.y = y;
	e.velocity = velocity;
	e.on = down;
	emit(e);
}

void InputLowering::on_matrix(uint8_t x, uint8_t y, bool on, uint32_t now_ms) {
	if (x >= profile_.button_cols || y >= profile_.button_rows) {
		return;
	}
	const size_t i = static_cast<size_t>(y) * profile_.button_cols + x;
	if (i >= profile_.button_by_matrix.size()) {
		return;
	}
	const ButtonId b = profile_.button_by_matrix[i];
	if (b == ButtonId::None) {
		return;
	}
	on_button(b, on, now_ms);
}

void InputLowering::on_button(ButtonId b, bool on, uint32_t now_ms) {
	const size_t i = static_cast<size_t>(b);
	if (i >= kButtonCount) {
		return;
	}
	had_input_ = true;
	if (on) {
		// `buttons.cpp:89-91`: any press disarms the pending sticky-shift toggle; the shift press
		// itself re-arms it in `toggle_shift` immediately below.
		consider_shift_release_for_sticky_ = false;
		held_.set(b, true);
		press_time_[i] = now_ms;
		ever_pressed_.set(b, true);
		used_up_.set(b, false);
	}
	else {
		held_.set(b, false);
	}
	if (b == ButtonId::Shift) {
		toggle_shift(on, now_ms);
	}
	if (on && b == ButtonId::SelectEnc) {
		select_edge_ = true;
	}
	if (on && b == ButtonId::Back) {
		back_edge_ = true;
	}

	UiEvent e{is_encoder_button(b) ? UiEvent::Kind::EncoderButton : UiEvent::Kind::Button};
	e.button = b;
	e.on = on;
	emit(e);
}

void InputLowering::toggle_shift(bool on, uint32_t now_ms) {
	// `Buttons::commandToggleShift` (`buttons.cpp:237-278`).
	if (on) {
		shift_pressed_at_ = now_ms;
		shift_pressed_ = true;
		consider_shift_release_for_sticky_ = true;
		return;
	}
	if (cfg_.shift_is_sticky) {
		// The legacy version un-wraps an overflowed audio-sample timer by hand; unsigned
		// subtraction already gives the same answer for any interval short of a full wrap.
		const uint32_t delta = now_ms - shift_pressed_at_;
		if (delta < cfg_.hold_time_ms) {
			// A tap latches shift, or -- if it was already latched -- releases it.
			shift_stuck_ = consider_shift_release_for_sticky_ && !shift_stuck_;
		}
	}
	if (!shift_stuck_) {
		shift_pressed_ = false;
	}
}

void InputLowering::clear_shift_sticky() {
	// `Buttons::clearShiftSticky` (`buttons.cpp:302-306`).
	shift_stuck_ = false;
	shift_pressed_ = false;
}

void InputLowering::set_shift_is_sticky(bool sticky) {
	cfg_.shift_is_sticky = sticky;
	if (!sticky) {
		// `ShiftIsSticky::writeCurrentValue` (`shift_is_sticky.cpp:25-27`): turning the feature off
		// must also release a shift that is latched under it.
		clear_shift_sticky();
	}
}

void InputLowering::clear_all_held(uint32_t now_ms) {
	had_input_ = true;
	for (size_t i = 1; i < kButtonCount; i++) {
		const ButtonId b = static_cast<ButtonId>(i);
		if (held_.has(b)) {
			on_button(b, false, now_ms);
		}
	}
	for (uint8_t x = 0; x < PadMask::kCols; x++) {
		// Copied out first: `on_pad` clears the bit it is standing on.
		uint32_t column = held_pads_.cols[x];
		while (column != 0) {
			const uint8_t y = static_cast<uint8_t>(std::countr_zero(column));
			column &= column - 1;
			on_pad(x, y, 0);
		}
	}
	push_out(UiEvent{UiEvent::Kind::AllReleased});
}

// -- Queues -----------------------------------------------------------------------------------

void InputLowering::emit(const UiEvent& e) {
	const bool held_back = (e.kind == UiEvent::Kind::Encoder && is_held_back_encoder(e.encoder))
	                       || ((e.kind == UiEvent::Kind::Button || e.kind == UiEvent::Kind::EncoderButton)
	                           && is_held_back_button(e.button));
	if (held_back) {
		if (!held_back_.push_back(e)) {
			// Read whole rather than popped, so the prefix -- the part with the press edges in it
			// -- is what to keep. `kHeldBackCapacity` is sized past what a tick can produce, so
			// this should never fire.
			held_back_dropped_++;
		}
		return;
	}
	push_out(e);
}

void InputLowering::push_out(const UiEvent& e) {
	if (queue_count_ == kQueueCapacity) {
		// Nothing drained the FIFO in time. Drop the oldest rather than the newest, so what
		// survives is the most recent gesture instead of a stale prefix of it.
		queue_head_ = static_cast<uint8_t>((queue_head_ + 1) % kQueueCapacity);
		queue_count_--;
		queue_dropped_++;
	}
	queue_[(queue_head_ + queue_count_) % kQueueCapacity] = e;
	queue_count_++;
}

bool InputLowering::next(UiEvent& out) {
	if (queue_count_ == 0) {
		return false;
	}
	out = queue_[queue_head_];
	queue_head_ = static_cast<uint8_t>((queue_head_ + 1) % kQueueCapacity);
	queue_count_--;
	return true;
}

// -- Queries ----------------------------------------------------------------------------------

uint32_t InputLowering::held_since(ButtonId b) const {
	const size_t i = static_cast<size_t>(b);
	return (i < kButtonCount && held_.has(b)) ? press_time_[i] : 0;
}

bool InputLowering::was_short_press(ButtonId b, uint32_t now_ms) const {
	const size_t i = static_cast<size_t>(b);
	return i < kButtonCount && ever_pressed_.has(b) && (now_ms - press_time_[i]) < cfg_.short_press_ms;
}

double InputLowering::calc_next_knob_speed(EncoderRole role, int8_t delta, uint32_t now_ms) {
	// `ContinuousEncoder::calcNextKnobSpeed` (`hid/encoders.cpp:57-75`), verbatim but for the
	// per-role state and the millisecond clock. The comment there: inertia and acceleration
	// control how fast the knob speeds up, speed_scale turns the raw speed into the offset
	// multiplier, and min/max clamp the effective speed. Tuned to feel controllable while still
	// covering 0..50 in one fast turn.
	constexpr double acceleration = 0.1;
	constexpr double inertia = 1.0 - acceleration;
	constexpr double speed_scale = 0.15;
	constexpr double min_speed = 1.0;
	constexpr double max_speed = 3.0;
	constexpr double reset_speed_time_threshold = 0.3;

	const size_t r = static_cast<size_t>(role);
	if (r >= kEncoderCount) {
		return min_speed;
	}
	const double time = static_cast<double>(now_ms) / 1000.0;
	const double last_time = static_cast<double>(last_knob_ms_[r]) / 1000.0;

	if (time - last_time >= reset_speed_time_threshold || delta != last_knob_offset_[r]) {
		// Too much time passed, or the knob changed direction: start again.
		knob_speed_[r] = 0.0;
	}
	else {
		// Legacy's clock is a sub-millisecond `double`, so it never divides by zero here. This one
		// can, and an infinite speed would then latch for the rest of the gesture, so a zero
		// interval is read as the smallest this clock represents.
		constexpr double smallest_interval = 0.001;
		knob_speed_[r] = knob_speed_[r] * inertia + 1.0 / std::max(time - last_time, smallest_interval) * acceleration;
	}
	last_knob_ms_[r] = now_ms;
	last_knob_offset_[r] = delta;
	return std::clamp(knob_speed_[r] * speed_scale, min_speed, max_speed);
}

void InputLowering::pretend_knob_untouched(EncoderRole role, uint32_t now_ms) {
	// `View::pretendModKnobsUntouchedForAWhile` (`gui/views/view.cpp:1968-1972`):
	// `timeModEncoderLastTurned[i] = AudioEngine::audioSampleTimer - kSampleRate`. Unsigned wrap is
	// the same wrap `turned_recently`'s own `now_ms - last_turn_ms_` does, so backdating past zero
	// is well defined and still reads as "a second ago".
	const size_t r = static_cast<size_t>(role);
	if (r >= kEncoderCount) {
		return;
	}
	last_turn_ms_[r] = now_ms - kPretendUntouchedMs;
}

// -- UiContext's lowering-backed queries (declared in scene.h) --------------------------------

bool UiContext::press_used_up(ButtonId b) const {
	return lowering != nullptr && lowering->press_used_up(b);
}

uint32_t UiContext::held_since(ButtonId b) const {
	return lowering != nullptr ? lowering->held_since(b) : 0;
}

} // namespace deluge::ui::shell
