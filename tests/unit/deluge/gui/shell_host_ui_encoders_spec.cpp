#include "support/shell_host_ui_rig.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

using namespace shell_host_ui_rig;

// clang-format off
describe shell_host_ui_encoders("gui::ui::ShellHostUI: encoders, and the input hooks it leaves to legacy", $ {

	// ---- input: encoders -----------------------------------------------------------------------

	it("multiplies detents back up into edges, so a turn arrives at the size legacy handed over", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		// Legacy hands a `UI` whole **detents** (`hid/encoder_input.cpp`'s `interpretEncoders`); the shell's
		// entry point takes **edges** and divides by `edges_per_detent` itself. Feeding the detent count straight through would
		// halve this to 1.
		const ActionResult result = r.guest.horizontalEncoderAction(3);
		r.guest.shell().tick(1);

		const PatternScene::Seen* turn = first_of(r.base, UiEvent::Kind::Encoder);
		expect(turn).not_().to_be_null();
		expect(turn->ev.encoder).to_equal(deluge::ui::widgets::core::EncoderRole::Horizontal);
		expect(static_cast<int>(turn->ev.delta)).to_equal(3);
		expect(result).to_equal(ActionResult::DEALT_WITH);
	});

	it("keeps the three function encoders on their own roles", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		(void)r.guest.verticalEncoderAction(-2);
		r.guest.shell().tick(1);
		const PatternScene::Seen* vertical = first_of(r.base, UiEvent::Kind::Encoder);
		expect(vertical).not_().to_be_null();
		expect(vertical->ev.encoder).to_equal(deluge::ui::widgets::core::EncoderRole::Vertical);
		expect(static_cast<int>(vertical->ev.delta)).to_equal(-2);

		r.base.seen.clear();
		r.guest.selectEncoderAction(4);
		r.guest.shell().tick(2);
		const PatternScene::Seen* select = first_of(r.base, UiEvent::Kind::Encoder);
		expect(select).not_().to_be_null();
		expect(select->ev.encoder).to_equal(deluge::ui::widgets::core::EncoderRole::Select);
		expect(static_cast<int>(select->ev.delta)).to_equal(4);
	});

	it("saturates a turn past the int8_t edge ceiling instead of wrapping it negative", _ {
		Rig r;
		expect(r.open()).to_be_true();

		// The ceiling, with the arithmetic: `Shell::on_encoder_edges` takes an `int8_t`
		// and the Deluge is two edges per detent, so 63 detents (126 edges)
		// is the largest that fits and 64 (128 edges) narrows to -128, i.e. **-64 detents**.
		// The guest can be handed more than 63: legacy's `detentDelta` accumulates in
		// `DetentedEncoder::pos` across every pass `interpretEncoders` skips during the SD routine
		// (`hid/encoder_input.cpp`'s `interpretEncoders`) or a stem export.
		r.base.seen.clear();
		(void)r.guest.verticalEncoderAction(63);
		r.guest.shell().tick(1);
		const PatternScene::Seen* atCeiling = first_of(r.base, UiEvent::Kind::Encoder);
		expect(atCeiling).not_().to_be_null();
		expect(static_cast<int>(atCeiling->ev.delta)).to_equal(63);

		r.base.seen.clear();
		(void)r.guest.verticalEncoderAction(64);
		r.guest.shell().tick(2);
		const PatternScene::Seen* past = first_of(r.base, UiEvent::Kind::Encoder);
		expect(past).not_().to_be_null();
		// Not -64, and not 0 either: saturated, the choice legacy makes for its own two narrowing
		// encoders (`hid/encoder_input.cpp`'s `interpretEncoders`).
		expect(static_cast<int>(past->ev.delta)).to_equal(63);

		r.base.seen.clear();
		(void)r.guest.verticalEncoderAction(-500);
		r.guest.shell().tick(3);
		const PatternScene::Seen* negative = first_of(r.base, UiEvent::Kind::Encoder);
		expect(negative).not_().to_be_null();
		expect(static_cast<int>(negative->ev.delta)).to_equal(-63);
	});

	it("resolves an encoder role through the profile's table, not a fixed board index", _ {
		// Only the encoder order differs from the Deluge's, so the panel, the grid and every other
		// answer are the ones the examples above used.
		Rig r{kDelugeBoard, profile_permuted_encoders()};
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		(void)r.guest.horizontalEncoderAction(2);
		r.guest.shell().tick(1);

		// Index 1 on this profile is Tempo, and index 2 is Horizontal.
		const PatternScene::Seen* turn = first_of(r.base, UiEvent::Kind::Encoder);
		expect(turn).not_().to_be_null();
		expect(turn->ev.encoder).to_equal(deluge::ui::widgets::core::EncoderRole::Horizontal);
		expect(static_cast<int>(turn->ev.delta)).to_equal(2);
	});

	it("drops a turn for a role the board has no encoder for, and only that role", _ {
		Rig r{kDelugeBoard, profile_no_horizontal()};
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		(void)r.guest.horizontalEncoderAction(5);
		r.guest.shell().tick(1);
		expect(first_of(r.base, UiEvent::Kind::Encoder)).to_be_null();

		// Vary the role, not the board: vertical is in this table and still arrives.
		(void)r.guest.verticalEncoderAction(5);
		r.guest.shell().tick(2);
		const PatternScene::Seen* vertical = first_of(r.base, UiEvent::Kind::Encoder);
		expect(vertical).not_().to_be_null();
		expect(vertical->ev.encoder).to_equal(deluge::ui::widgets::core::EncoderRole::Vertical);
	});

	it("feeds nothing for a zero turn", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();
		(void)r.guest.horizontalEncoderAction(0);
		r.guest.shell().tick(1);
		expect(r.base.seen.empty()).to_be_true();
		expect(r.guest.shell().lowering().had_input_this_tick()).to_be_false();
	});

	it("times encoder acceleration on the scheduler's clock, as legacy does, apart from the UI clock", _ {
		Rig r;
		expect(r.open()).to_be_true();
		AudioEngine::audioSampleTimer = 100 * 44;
		fakelegacyui::system_time_s = 1.5;
		(void)r.guest.timerCallback();
		expect(r.guest.shell().lowering().acceleration_clock_ms()).to_equal(uint32_t(1500));
		expect(r.guest.shell().lowering().snapshot().acceleration_clock_ms).to_equal(uint32_t(1500));
		expect(r.guest.shell().lowering().snapshot().now_ms).to_equal(uint32_t(100));
	});

	it("hands a routed event both clocks: the UI's and the one acceleration is timed on", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();
		AudioEngine::audioSampleTimer = 250 * 44;
		fakelegacyui::system_time_s = 2.0;
		(void)r.guest.buttonAction(deluge::hid::button::LOAD, true);
		(void)r.guest.timerCallback();
		expect(r.base.seen.empty()).to_be_false();
		expect(r.base.seen.back().now_ms).to_equal(uint32_t(250));
		expect(r.base.seen.back().acceleration_clock_ms).to_equal(uint32_t(2000));
	});

	it("stamps events with the clock nowMs() reads, not the tick's", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		AudioEngine::audioSampleTimer = 1500 * 44; // nowMs() == 1500
		(void)r.guest.buttonAction(deluge::hid::button::LOAD, true);
		// `end_tick` re-stamps the snapshot with the tick's own clock, which is what a scene reads
		// as `ctx.now_ms` (`Shell::make_context`). The **feed's** clock is what the
		// press keeps, and that one comes from `nowMs()` -- so the two numbers being different is
		// what proves the hook stamped its own rather than borrowing the tick's.
		r.guest.shell().tick(1700);

		expect(r.base.seen.size()).to_equal(size_t(1));
		expect(r.base.seen[0].now_ms).to_equal(uint32_t(1700));
		expect(r.guest.shell().lowering().held_since(ButtonId::Load)).to_equal(uint32_t(1500));
	});

	it("reads nowMs() from legacy's audio-sample counter, so a different count gives a different stamp", _ {
		// The companion to the example above: vary only the clock. Without this, a `nowMs()` that
		// returned a constant would pass everything above it.
		Rig r;
		expect(r.open()).to_be_true();
		AudioEngine::audioSampleTimer = 37 * 44; // 37 ms
		(void)r.guest.buttonAction(deluge::hid::button::LOAD, true);
		expect(r.guest.shell().lowering().held_since(ButtonId::Load)).to_equal(uint32_t(37));

		(void)r.guest.buttonAction(deluge::hid::button::LOAD, false);
		AudioEngine::audioSampleTimer = 9001 * 44; // 9001 ms
		(void)r.guest.buttonAction(deluge::hid::button::LOAD, true);
		expect(r.guest.shell().lowering().held_since(ButtonId::Load)).to_equal(uint32_t(9001));
	});

	it("runs on legacy's UI clock, not the scheduler's: the audio-sample counter at 44 a millisecond", _ {
		Rig r;
		expect(r.open()).to_be_true();
		AudioEngine::audioSampleTimer = 4400;
		fakelegacyui::system_time_s = 7.0; // the scheduler's clock, which nowMs() does not read
		expect(r.runtime.nowMs()).to_equal(uint32_t(100));
		fakelegacyui::system_time_s = 9.0;
		expect(r.runtime.nowMs()).to_equal(uint32_t(100));
		AudioEngine::audioSampleTimer = 4400 + 43;
		expect(r.runtime.nowMs()).to_equal(uint32_t(100));
		AudioEngine::audioSampleTimer = 4400 + 44;
		expect(r.runtime.nowMs()).to_equal(uint32_t(101));
	});

	// ---- input: the hooks deliberately NOT wired ------------------------------------------------

	it("hands a gold turn, a mod button and a gold push to the knobs' service, and none to legacy's `view`", _ {
		Rig r;
		expect(r.open()).to_be_true();
		KnobRecorder knobs;
		r.guest.shell().set_services(Services{.mod_knobs = &knobs});
		fakelegacyui::reset();
		fakelegacyui::current_ui = &r.guest;

		// The turn as legacy lowered it -- its wiggle check and acceleration applied -- reaches the service as it is.
		set_clock_ms(1000);
		static_cast<::UI&>(r.guest).modEncoderAction(0, 3);
		static_cast<::UI&>(r.guest).modButtonAction(2, true);
		static_cast<::UI&>(r.guest).modButtonAction(2, false);
		(void)r.guest.buttonAction(deluge::hid::button::MOD_ENCODER_0, true);
		static_cast<::UI&>(r.guest).modEncoderButtonAction(0, true);
		r.guest.shell().tick(1001);

		expect(knobs.count(UiEvent::Kind::Encoder)).to_equal(size_t(1));
		for (const UiEvent& e : knobs.seen) {
			if (e.kind == UiEvent::Kind::Encoder) {
				expect(e.encoder).to_equal(deluge::ui::widgets::core::EncoderRole::GoldLower);
				expect(e.delta).to_equal(3);
				expect(e.lowered).to_be_true();
			}
		}
		expect(knobs.count(UiEvent::Kind::Button)).to_equal(size_t(2));
		expect(knobs.count(UiEvent::Kind::EncoderButton)).to_equal(size_t(1));
		expect(fakelegacyui::mod_encoder_actions).to_equal(0);
		expect(fakelegacyui::mod_button_actions).to_equal(0);
		expect(fakelegacyui::mod_encoder_button_actions).to_equal(0);
		r.guest.shell().set_services(Services{});
	});
});
// clang-format on

DELUGE_SPEC(shell_host_ui_encoders_spec, shell_host_ui_encoders)
