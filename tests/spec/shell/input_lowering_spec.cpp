#include "shell/input_lowering.h"
#include "support/fake_surfaces.h"

#include "cppspec.hpp"

#include "../support/spec_main.h"
using namespace deluge::ui::shell;
using deluge::ui::EncoderRole;

namespace {
/// A BUTTON event at matrix (x, y).
DelugeInputEvent button_ev(uint8_t x, uint8_t y, bool on) {
	return DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)};
}
/// A PAD event at (x, y); velocity 0 is a release.
DelugeInputEvent pad_ev(uint8_t x, uint8_t y, uint8_t velocity) {
	return DelugeInputEvent{DELUGE_EVENT_PAD, x, y, static_cast<int16_t>(velocity)};
}
/// The one held-back event, or a default-constructed one if there is not exactly one.
UiEvent only_held_back(const InputLowering& in) {
	return in.held_back().size() == 1 ? in.held_back()[0] : UiEvent{UiEvent::Kind::AllReleased};
}
} // namespace

// clang-format off
describe shell_input_lowering("shell::InputLowering", $ {
	it("lowers a matrix press to a ButtonId and tracks held/since", _ {
		InputLowering in(uitest::test_profile_input(), {});
		in.begin_tick(1000);
		in.on_board_event(button_ev(8, 3, true), 1000);   // Play, definitions_cxx.hpp:68
		in.end_tick();
		UiEvent e;
		expect(in.next(e)).to_be_true();
		expect(e.kind == UiEvent::Kind::Button).to_be_true();
		expect(e.button == ButtonId::Play).to_be_true();
		expect(e.on).to_be_true();
		expect(in.next(e)).to_be_false();
		expect(in.held().has(ButtonId::Play)).to_be_true();
		expect(in.held_since(ButtonId::Play)).to_equal(uint32_t(1000));
		expect(in.had_input_this_tick()).to_be_true();
	});

	it("holds widget-owned inputs back from next() and routes the rest", _ {
		InputLowering in(uitest::test_profile_input(), {});
		in.begin_tick(10);
		in.on_board_event(button_ev(8, 3, true), 10);     // Play -> next()
		in.on_board_event(button_ev(7, 1, true), 10);     // Back -> held_back()
		in.on_board_event(button_ev(4, 3, true), 10);     // SelectEnc -> held_back()
		in.on_board_event(button_ev(4, 1, true), 10);     // TempoEnc -> next()
		in.on_encoder_edges(3, 2, 10);                    // Select turn -> held_back()
		in.on_encoder_edges(2, 2, 10);                    // Tempo turn -> next()
		in.end_tick();

		expect(in.held_back().size()).to_equal(size_t(3));
		expect(in.held_back()[0].button == ButtonId::Back).to_be_true();
		expect(in.held_back()[1].kind == UiEvent::Kind::EncoderButton).to_be_true();
		expect(in.held_back()[1].button == ButtonId::SelectEnc).to_be_true();
		expect(in.held_back()[2].encoder == EncoderRole::Select).to_be_true();

		UiEvent e;
		expect(in.next(e)).to_be_true(); expect(e.button == ButtonId::Play).to_be_true();
		expect(in.next(e)).to_be_true(); expect(e.button == ButtonId::TempoEnc).to_be_true();
		expect(in.next(e)).to_be_true(); expect(e.kind == UiEvent::Kind::Encoder).to_be_true();
		expect(e.encoder == EncoderRole::Tempo).to_be_true();
		expect(in.next(e)).to_be_false();
	});

	it("shift press+release under hold time becomes sticky when enabled", _ {
		InputLowering in(uitest::test_profile_input(), LoweringConfig{.shift_is_sticky = true});
		in.begin_tick(0);
		in.on_board_event(button_ev(8, 0, true), 0);      // Shift, definitions_cxx.hpp:67
		expect(in.shift()).to_be_true();
		in.on_board_event(button_ev(8, 0, false), 50);    // released inside hold_time_ms (100)
		expect(in.sticky()).to_be_true();
		expect(in.shift()).to_be_true();                  // buttons.cpp:273-276: stays on while stuck
		// A second short press clears it (buttons.cpp:267).
		in.on_board_event(button_ev(8, 0, true), 200);
		in.on_board_event(button_ev(8, 0, false), 220);
		expect(in.sticky()).to_be_false();
		expect(in.shift()).to_be_false();
	});

	it("does not stick when the shift press outlasts hold_time_ms, or when sticky is off", _ {
		InputLowering in(uitest::test_profile_input(), LoweringConfig{.shift_is_sticky = true});
		in.begin_tick(0);
		in.on_board_event(button_ev(8, 0, true), 0);
		in.on_board_event(button_ev(8, 0, false), 400);   // flash_storage.cpp:349-350: 100 ms
		expect(in.sticky()).to_be_false();
		expect(in.shift()).to_be_false();

		// Another button pressed while shift was held disarms the toggle (buttons.cpp:89-91).
		InputLowering armed(uitest::test_profile_input(), LoweringConfig{.shift_is_sticky = true});
		armed.begin_tick(0);
		armed.on_board_event(button_ev(8, 0, true), 0);
		armed.on_board_event(button_ev(8, 3, true), 10);
		armed.on_board_event(button_ev(8, 0, false), 20);
		expect(armed.sticky()).to_be_false();

		InputLowering off(uitest::test_profile_input(), {});
		off.begin_tick(0);
		off.on_board_event(button_ev(8, 0, true), 0);
		off.on_board_event(button_ev(8, 0, false), 10);
		expect(off.sticky()).to_be_false();
	});

	it("two edges make one detent; accelerating encoders pass edges through", _ {
		InputLowering in(uitest::test_profile_input(), {});
		in.begin_tick(5);
		in.on_encoder_edges(3, 2, 5);                     // select, encoders.cpp:28-34
		in.end_tick();
		UiEvent e = only_held_back(in);
		expect(e.kind == UiEvent::Kind::Encoder).to_be_true();
		expect(e.encoder == EncoderRole::Select).to_be_true();
		expect(e.delta).to_equal(int8_t(1));
		expect(in.snapshot().turn).to_equal(int8_t(1));

		// Index 4 is mod1, the upper gold knob (encoders.h:80-81, encoder_input.cpp:57).
		in.begin_tick(6);
		in.on_encoder_edges(4, 3, 6);                     // gold knob: raw edges through
		in.end_tick();
		e = only_held_back(in);
		expect(e.encoder == EncoderRole::GoldUpper).to_be_true();
		expect(e.delta).to_equal(int8_t(3));
		expect(in.snapshot().edit[0]).to_equal(int8_t(3));

		// Index 5 is mod0, the lower gold knob, and lands in edit[1].
		in.begin_tick(7);
		in.on_encoder_edges(5, -2, 7);
		in.end_tick();
		e = only_held_back(in);
		expect(e.encoder == EncoderRole::GoldLower).to_be_true();
		expect(in.snapshot().edit[1]).to_equal(int8_t(-2));
	});

	it("carries a partial detent across ticks", _ {
		InputLowering in(uitest::test_profile_input(), {});
		in.begin_tick(1);
		in.on_encoder_edges(3, 3, 1);                     // 3 edges -> 1 detent, 1 carried
		in.end_tick();
		expect(in.snapshot().turn).to_equal(int8_t(1));
		expect(in.held_back().size()).to_equal(size_t(1));

		in.begin_tick(2);
		in.end_tick();
		expect(in.snapshot().turn).to_equal(int8_t(0));
		expect(in.held_back().empty()).to_be_true();

		in.begin_tick(3);
		in.on_encoder_edges(3, 1, 3);                     // the carried edge completes a detent
		in.end_tick();
		expect(in.snapshot().turn).to_equal(int8_t(1));
	});

	it("reproduces the legacy gold-knob acceleration curve", _ {
		InputLowering in(uitest::test_profile_input(), {});
		// encoders.cpp:57-75. First call: 1.0 s since the zero-initialised last time, past the
		// 0.3 s reset threshold, so the speed resets and clamps up to min_speed.
		expect(in.calc_next_knob_speed(EncoderRole::GoldUpper, 1, 1000)).to_be_within(1e-9).of(1.0);
		// 10 ms later, same direction: speed = 0*0.9 + (1/0.01)*0.1 = 10; 10*0.15 = 1.5.
		expect(in.calc_next_knob_speed(EncoderRole::GoldUpper, 1, 1010)).to_be_within(1e-9).of(1.5);
		// 10 ms later again: speed = 10*0.9 + 100*0.1 = 19; 19*0.15 = 2.85.
		expect(in.calc_next_knob_speed(EncoderRole::GoldUpper, 1, 1020)).to_be_within(1e-9).of(2.85);
		// A direction change resets, and clamps back up to min_speed.
		expect(in.calc_next_knob_speed(EncoderRole::GoldUpper, -1, 1030)).to_be_within(1e-9).of(1.0);
		// The other gold knob keeps its own state, so it starts from the reset value too.
		expect(in.calc_next_knob_speed(EncoderRole::GoldLower, 1, 1040)).to_be_within(1e-9).of(1.0);
	});

	it("defers events while sd_busy and replays them in order; drops oldest past 8", _ {
		InputLowering in(uitest::test_profile_input(), {});
		in.begin_tick(100);
		in.set_sd_busy(true);
		for (uint8_t x = 0; x < 10; x++) {
			in.on_board_event(pad_ev(x, 0, 64), 100);
		}
		UiEvent e;
		expect(in.next(e)).to_be_false();
		expect(in.deferred_dropped()).to_equal(uint32_t(2));

		in.set_sd_busy(false);
		for (uint8_t x = 2; x < 10; x++) {
			expect(in.next(e)).to_be_true();
			expect(e.kind == UiEvent::Kind::Pad).to_be_true();
			expect(e.x).to_equal(x);
		}
		expect(in.next(e)).to_be_false();
		expect(in.queue_dropped()).to_equal(uint32_t(0));
	});

	it("AllReleased releases everything held and reaches the queue even when locked", _ {
		InputLowering in(uitest::test_profile_input(), {});
		in.begin_tick(1);
		in.on_board_event(button_ev(8, 3, true), 1);      // Play
		in.on_board_event(pad_ev(3, 5, 90), 1);
		UiEvent e;
		while (in.next(e)) {}                             // drain the presses

		in.set_input_lock(true);
		in.on_board_event(button_ev(8, 2, true), 2);      // Record: dropped by the lock
		in.on_board_event(DelugeInputEvent{DELUGE_EVENT_NO_PRESSES, 0, 0, 0}, 2);

		expect(in.next(e)).to_be_true();
		expect(e.kind == UiEvent::Kind::Button).to_be_true();
		expect(e.button == ButtonId::Play).to_be_true();
		expect(e.on).to_be_false();
		expect(in.next(e)).to_be_true();
		expect(e.kind == UiEvent::Kind::Pad).to_be_true();
		expect(e.x).to_equal(uint8_t(3)); expect(e.y).to_equal(uint8_t(5));
		expect(e.velocity).to_equal(uint8_t(0));
		expect(in.next(e)).to_be_true();
		expect(e.kind == UiEvent::Kind::AllReleased).to_be_true();
		expect(in.next(e)).to_be_false();
		expect(in.held().any()).to_be_false();
		expect(in.held_pads().test(3, 5)).to_be_false();
		expect(in.held().has(ButtonId::Record)).to_be_false();
	});

	it("builds the toolkit InputSnapshot from this tick's events", _ {
		InputLowering in(uitest::test_profile_input(), {});
		in.begin_tick(4242);
		in.on_encoder_edges(3, 2, 4242);                  // one Select detent
		in.on_board_event(button_ev(4, 3, true), 4242);   // SelectEnc press edge
		in.on_board_event(button_ev(7, 1, true), 4242);   // Back press edge
		in.on_board_event(button_ev(8, 0, true), 4242);   // Shift held
		in.on_board_event(pad_ev(3, 5, 90), 4242);
		in.end_tick();

		const deluge::ui::InputSnapshot& s = in.snapshot();
		expect(s.turn).to_equal(int8_t(1));
		expect(s.select_pressed).to_be_true();
		expect(s.back_pressed).to_be_true();
		expect(s.shift).to_be_true();
		expect(s.pads.size()).to_equal(size_t(1));
		expect(s.pads[0].velocity).to_equal(uint8_t(90));
		expect(s.held_pads.test(3, 5)).to_be_true();
		expect(s.now_ms).to_equal(uint32_t(4242));
	});

	it("select_pressed is an edge: false on the next tick while still held", _ {
		InputLowering in(uitest::test_profile_input(), {});
		in.begin_tick(2);
		in.on_board_event(button_ev(4, 3, true), 2);
		in.end_tick();
		expect(in.snapshot().select_pressed).to_be_true();

		in.begin_tick(3);
		in.end_tick();
		expect(in.snapshot().select_pressed).to_be_false();
		expect(in.snapshot().pads.empty()).to_be_true();
		expect(in.held().has(ButtonId::SelectEnc)).to_be_true();
	});

	it("drops matrix cells that are unmapped or out of range", _ {
		InputLowering in(uitest::test_profile_input(), {});
		in.begin_tick(1);
		in.on_board_event(button_ev(4, 0, true), 1);      // ButtonId::None cell
		in.on_board_event(button_ev(9, 0, true), 1);      // past button_cols
		in.on_board_event(button_ev(0, 4, true), 1);      // past button_rows
		in.on_board_event(pad_ev(40, 0, 64), 1);          // past PadMask bounds
		in.end_tick();
		UiEvent e;
		expect(in.next(e)).to_be_false();
		expect(in.held().any()).to_be_false();
		expect(in.had_input_this_tick()).to_be_false();
		expect(in.snapshot().pads.empty()).to_be_true();
	});

	it("clears the frame context's claims once per tick", _ {
		InputLowering in(uitest::test_profile_input(), {});
		in.begin_tick(1);
		in.end_tick();
		const uint32_t first = in.ctx().tick;
		in.ctx().claims.turn = true;
		in.ctx().claims.back = true;
		expect(in.ctx().claims.any()).to_be_true();

		in.begin_tick(2);
		expect(in.ctx().claims.any()).to_be_false();
		expect(in.ctx().claims.double_claim).to_be_false();
		expect(in.ctx().tick).to_equal(first + 1);
		expect(in.ctx().tick_ms).to_equal(uint32_t(2));
	});

	it("held_since follows the current press only", _ {
		InputLowering in(uitest::test_profile_input(), {});
		in.begin_tick(100);
		in.on_board_event(button_ev(8, 3, true), 100);
		expect(in.held_since(ButtonId::Play)).to_equal(uint32_t(100));
		in.on_board_event(button_ev(8, 3, false), 150);
		expect(in.held_since(ButtonId::Play)).to_equal(uint32_t(0));
		expect(in.was_short_press(ButtonId::Play, 150)).to_be_true();
		in.on_board_event(button_ev(8, 3, true), 900);
		expect(in.held_since(ButtonId::Play)).to_equal(uint32_t(900));
		expect(in.was_short_press(ButtonId::Play, 1500)).to_be_false();
	});

	it("tracks press-used-up per button and clears it on the next press", _ {
		InputLowering in(uitest::test_profile_input(), {});
		in.begin_tick(1);
		in.on_board_event(button_ev(8, 2, true), 1);      // Record
		expect(in.press_used_up(ButtonId::Record)).to_be_false();
		in.mark_press_used_up(ButtonId::Record);
		expect(in.press_used_up(ButtonId::Record)).to_be_true();
		in.on_board_event(button_ev(8, 2, false), 2);
		expect(in.press_used_up(ButtonId::Record)).to_be_true();  // buttons.cpp:171: cleared on press
		in.on_board_event(button_ev(8, 2, true), 3);
		expect(in.press_used_up(ButtonId::Record)).to_be_false();
	});

	it("a pad press disarms the pending sticky-shift toggle", _ {
		InputLowering in(uitest::test_profile_input(), LoweringConfig{.shift_is_sticky = true});
		in.begin_tick(0);
		in.on_board_event(button_ev(8, 0, true), 0);      // shift down
		in.on_board_event(pad_ev(3, 5, 90), 10);          // deluge.cpp:241-243
		in.on_board_event(button_ev(8, 0, false), 20);    // released well inside hold_time_ms
		expect(in.sticky()).to_be_false();
		expect(in.shift()).to_be_false();

		// A pad *release* is not a press and leaves the toggle armed.
		InputLowering armed(uitest::test_profile_input(), LoweringConfig{.shift_is_sticky = true});
		armed.begin_tick(0);
		armed.on_board_event(button_ev(8, 0, true), 0);
		armed.on_board_event(pad_ev(3, 5, 0), 10);
		armed.on_board_event(button_ev(8, 0, false), 20);
		expect(armed.sticky()).to_be_true();
	});

	it("clears a latched shift on demand and when the feature is switched off", _ {
		InputLowering in(uitest::test_profile_input(), LoweringConfig{.shift_is_sticky = true});
		in.begin_tick(0);
		in.on_board_event(button_ev(8, 0, true), 0);
		in.on_board_event(button_ev(8, 0, false), 20);
		expect(in.sticky()).to_be_true();
		in.clear_shift_sticky();                          // buttons.cpp:302-306
		expect(in.sticky()).to_be_false();
		expect(in.shift()).to_be_false();

		// Turning the feature off clears a latch made while it was on.
		in.on_board_event(button_ev(8, 0, true), 100);
		in.on_board_event(button_ev(8, 0, false), 120);
		expect(in.sticky()).to_be_true();
		in.set_shift_is_sticky(false);                    // shift_is_sticky.cpp:25-27
		expect(in.shift_is_sticky()).to_be_false();
		expect(in.sticky()).to_be_false();
		expect(in.shift()).to_be_false();

		// ...and a tap no longer latches once it is off.
		in.on_board_event(button_ev(8, 0, true), 200);
		in.on_board_event(button_ev(8, 0, false), 220);
		expect(in.sticky()).to_be_false();

		// Switching it back on latches again, without clearing anything.
		in.set_shift_is_sticky(true);
		in.on_board_event(button_ev(8, 0, true), 300);
		in.on_board_event(button_ev(8, 0, false), 320);
		expect(in.sticky()).to_be_true();
	});

	it("debounces the first turn and an immediate reversal of a gold knob", _ {
		InputLowering in(uitest::test_profile_input(), {});
		UiEvent e;

		// First turn after a pause longer than short_press_ms (500): recorded, not actioned.
		in.begin_tick(1000);
		in.on_encoder_edges(4, 1, 1000);
		in.end_tick();
		expect(in.held_back().empty()).to_be_true();
		expect(in.snapshot().edit[0]).to_equal(int8_t(0));
		expect(in.had_input_this_tick()).to_be_true();    // legacy still sets `anything`
		expect(in.next(e)).to_be_false();

		// Straight back the other way, inside the window: an accidental wiggle, also swallowed.
		in.begin_tick(1100);
		in.on_encoder_edges(4, -1, 1100);
		in.end_tick();
		expect(in.held_back().empty()).to_be_true();
		expect(in.snapshot().edit[0]).to_equal(int8_t(0));

		// Carrying on that way is a real turn.
		in.begin_tick(1200);
		in.on_encoder_edges(4, -1, 1200);
		in.end_tick();
		expect(in.held_back().size()).to_equal(size_t(1));
		expect(in.snapshot().edit[0]).to_equal(int8_t(-1));

		// And so is the next one, in either direction, now that the record is cleared.
		in.begin_tick(1300);
		in.on_encoder_edges(4, 1, 1300);
		in.end_tick();
		expect(in.snapshot().edit[0]).to_equal(int8_t(1));

		// Pausing past the window arms the debounce again.
		in.begin_tick(2000);
		in.on_encoder_edges(4, 1, 2000);
		in.end_tick();
		expect(in.snapshot().edit[0]).to_equal(int8_t(0));

		// The other gold knob keeps its own record.
		in.begin_tick(2010);
		in.on_encoder_edges(5, 1, 2010);
		in.end_tick();
		expect(in.snapshot().edit[1]).to_equal(int8_t(0));

		// Detented encoders are not debounced.
		in.begin_tick(3000);
		in.on_encoder_edges(3, 2, 3000);
		in.end_tick();
		expect(in.snapshot().turn).to_equal(int8_t(1));
	});

	it("counts held-back events it had no room for", _ {
		InputLowering in(uitest::test_profile_input(), {});
		in.begin_tick(1);
		for (int i = 0; i < InputLowering::kHeldBackCapacity + 2; i++) {
			in.on_board_event(button_ev(7, 1, i % 2 == 0), 1);   // Back, pressed and released
		}
		in.end_tick();
		expect(in.held_back().size()).to_equal(size_t(InputLowering::kHeldBackCapacity));
		expect(in.held_back_dropped()).to_equal(uint32_t(2));
	});

	it("does not call a button that was never pressed a short press", _ {
		InputLowering in(uitest::test_profile_input(), {});
		expect(in.was_short_press(ButtonId::Record, 0)).to_be_false();
		expect(in.was_short_press(ButtonId::Record, 10)).to_be_false();
	});
});
// clang-format on

DELUGE_SPEC(shell_input_lowering_spec, shell_input_lowering)
