#include "io/midi/device_registry.h"
#include "support/shell_host_ui_rig.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

using namespace shell_host_ui_rig;

// clang-format off
describe shell_host_ui_buttons("gui::ui::ShellHostUI: buttons, MIDI learn, the note modes and a busy card", $ {

	// ---- MIDI learn: the four `UI` learn offers --------------------------------------------------
	//
	// Each override routes the message through the guest's chain as one `UiEvent` and answers with the chain's
	// answer, falling back to the base `UI` answer (legacy's learn mode reaching `View`) when nothing in the chain
	// used it. What the menu and `services::MidiLearn` do with the event is `tests/integration/ui/scenes`'s.

	it("each learn offer routes one event carrying the message, and falls back to the base answer", _ {
		Rig r;
		expect(r.open()).to_be_true();
		struct Case {
			UiEvent::Kind kind;
			uint8_t data1;
			uint8_t data2;
		};
		for (const Case c : {Case{UiEvent::Kind::MidiNoteOn, 64, 100}, Case{UiEvent::Kind::MidiProgramChange, 12, 0},
		                     Case{UiEvent::Kind::MidiCc, 74, 64}, Case{UiEvent::Kind::MidiPitchBend, 5, 70}}) {
			fakelegacyui::reset();
			r.base.seen.clear();
			bool handled = true;
			switch (c.kind) {
			case UiEvent::Kind::MidiNoteOn:
				handled = r.guest.noteOnReceivedForMidiLearn(fake_cable(), 9, c.data1, c.data2);
				break;
			case UiEvent::Kind::MidiProgramChange:
				handled = r.guest.pcReceivedForMidiLearn(fake_cable(), 9, c.data1);
				break;
			case UiEvent::Kind::MidiCc:
				handled = r.guest.ccReceivedForMidiLearn(fake_cable(), 9, c.data1, c.data2);
				break;
			default:
				handled = r.guest.pitchBendReceivedForMidiLearn(fake_cable(), 9, c.data1, c.data2);
				break;
			}
			expect(r.base.seen.size()).to_equal(size_t(1));
			expect(r.base.seen[0].ev.kind).to_equal(c.kind);
			expect(r.base.seen[0].ev.channel).to_equal(uint8_t(9));
			expect(r.base.seen[0].ev.data1).to_equal(c.data1);
			expect(r.base.seen[0].ev.data2).to_equal(c.data2);
			expect(deluge::midi::cable_of(r.base.seen[0].ev.device)).to_equal(&fake_cable());
			// `PatternScene` declines and this rig installs no services, so the base answer decides.
			expect(fakelegacyui::base_learn_offers).to_equal(1);
			expect(handled).to_be_false();
		}
	});

	it("a learn offer the chain uses goes no further", _ {
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		r.base.handle_midi = true;
		expect(r.guest.ccReceivedForMidiLearn(fake_cable(), 9, 74, 64)).to_be_true();
		expect(r.base.seen.size()).to_equal(size_t(1));
		expect(fakelegacyui::base_learn_offers).to_equal(0);
	});

	// ---- input: the note modes (`gui/ui/shell_note_mode.h`) --------------------------------------
	//
	// `SoundEditor` forwards a button, a turn and its own exit to `instrumentClipView` while a note mode is on
	// (`SoundEditor::buttonAction`, `SoundEditor::horizontalEncoderAction`, `SoundEditor::verticalEncoderAction` and
	// `SoundEditor::exitCompletely` in `origin/main:src/deluge/gui/ui/sound_editor.cpp`). What the forward decides is
	// `tests/integration/ui/scenes/note_mode_spec.cpp`'s; what these pin is the guest's half, against fakes that answer whatever an
	// example sets: a value is returned and nothing is fed, `std::nullopt` feeds exactly as before, and the tick is
	// armed only while the guest is current.

	it("consults the note mode for a button, and feeds the shell as before when it answers nothing", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();
		fakelegacyui::reset();

		const ActionResult result = r.guest.buttonAction(deluge::hid::button::LOAD, true);
		r.guest.shell().tick(1);

		expect(fakelegacyui::note_button_forwards).to_equal(1);
		expect(fakelegacyui::last_note_button).to_equal(fakelegacyui::NoteButtonCall{deluge::hid::button::LOAD, true});
		expect(r.base.seen.size()).to_equal(size_t(1));
		expect(result).to_equal(ActionResult::NOT_DEALT_WITH);
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(deluge::gui::ui::ShellRuntime::kMinRearmMs));
	});

	it("returns the note mode's own answer for a button and feeds nothing", _ {
		// The re-entrancy rule: the handler's own `ActionResult`, whichever it is. The same
		// press as the example above, varying only the answer.
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();
		fakelegacyui::reset();

		fakelegacyui::note_button_answer = ActionResult::DEALT_WITH;
		expect(r.guest.buttonAction(deluge::hid::button::LOAD, true)).to_equal(ActionResult::DEALT_WITH);
		fakelegacyui::note_button_answer = ActionResult::ACTIONED_AND_CAUSED_CHANGE;
		expect(r.guest.buttonAction(deluge::hid::button::LOAD, false))
		    .to_equal(ActionResult::ACTIONED_AND_CAUSED_CHANGE);
		r.guest.shell().tick(1);
		expect(r.base.seen.empty()).to_be_true();
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(deluge::gui::ui::ShellRuntime::kMinRearmMs));

		expect(fakelegacyui::note_button_forwards).to_equal(2);
		expect(fakelegacyui::last_note_button).to_equal(fakelegacyui::NoteButtonCall{deluge::hid::button::LOAD, false});
	});

	it("hands a button to the menu's SAVE arm before the shell, and takes its answer", _ {
		// `SoundEditor::buttonAction`'s SAVE arm is legacy's, as the note mode's buttons are (`forwardMenuSaveButton`).
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();
		fakelegacyui::reset();
		fakelegacyui::menu_save_answer = ActionResult::DEALT_WITH;
		expect(r.guest.buttonAction(deluge::hid::button::SAVE, true)).to_equal(ActionResult::DEALT_WITH);
		r.guest.shell().tick(1);
		expect(r.base.seen.empty()).to_be_true();
		expect(fakelegacyui::menu_save_forwards).to_equal(1);
		// Declined, the button goes on to the shell as before.
		fakelegacyui::menu_save_answer = std::nullopt;
		(void)r.guest.buttonAction(deluge::hid::button::SAVE, false);
		expect(fakelegacyui::menu_save_forwards).to_equal(2);
	});

	it("asks the menu-view forward after SAVE's and returns its answer without feeding", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();
		fakelegacyui::reset();
		fakelegacyui::menu_view_answer = ActionResult::DEALT_WITH;
		expect(r.guest.buttonAction(deluge::hid::button::SESSION_VIEW, true)).to_equal(ActionResult::DEALT_WITH);
		r.guest.shell().tick(1);
		expect(fakelegacyui::menu_view_forwards).to_equal(1);
		expect(r.base.seen.empty()).to_be_true();
	});

	it("does not consult the note mode for the two gold-knob pushes, whose early return comes first", _ {
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		fakelegacyui::note_button_answer = ActionResult::DEALT_WITH;

		expect(r.guest.buttonAction(deluge::hid::button::MOD_ENCODER_0, true)).to_equal(ActionResult::NOT_DEALT_WITH);
		expect(r.guest.buttonAction(deluge::hid::button::MOD_ENCODER_1, false)).to_equal(ActionResult::NOT_DEALT_WITH);
		expect(fakelegacyui::note_button_forwards).to_equal(0);
	});

	it("consults the note mode for a horizontal turn: feeds when it answers nothing, returns its "
	   "answer otherwise", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();
		fakelegacyui::reset();

		expect(r.guest.horizontalEncoderAction(3)).to_equal(ActionResult::DEALT_WITH);
		r.guest.shell().tick(1);
		expect(fakelegacyui::note_horizontal_forwards).to_equal(1);
		expect(fakelegacyui::last_note_horizontal_offset).to_equal(int32_t(3));
		const PatternScene::Seen* turn = first_of(r.base, UiEvent::Kind::Encoder);
		expect(turn).not_().to_be_null();
		if (turn != nullptr) {
			expect(turn->ev.encoder).to_equal(deluge::ui::widgets::core::EncoderRole::Horizontal);
		}

		r.base.seen.clear();
		fakelegacyui::note_horizontal_answer = ActionResult::ACTIONED_AND_CAUSED_CHANGE;
		expect(r.guest.horizontalEncoderAction(-2)).to_equal(ActionResult::ACTIONED_AND_CAUSED_CHANGE);
		r.guest.shell().tick(2);
		expect(fakelegacyui::note_horizontal_forwards).to_equal(2);
		expect(fakelegacyui::last_note_horizontal_offset).to_equal(int32_t(-2));
		expect(r.base.seen.empty()).to_be_true();
	});

	it("consults the note mode for a vertical turn the same way", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();
		fakelegacyui::reset();

		expect(r.guest.verticalEncoderAction(-2)).to_equal(ActionResult::DEALT_WITH);
		r.guest.shell().tick(1);
		expect(fakelegacyui::last_note_vertical).to_equal(fakelegacyui::NoteVerticalCall{-2});
		const PatternScene::Seen* turn = first_of(r.base, UiEvent::Kind::Encoder);
		expect(turn).not_().to_be_null();
		if (turn != nullptr) {
			expect(turn->ev.encoder).to_equal(deluge::ui::widgets::core::EncoderRole::Vertical);
		}

		r.base.seen.clear();
		fakelegacyui::note_vertical_answer = ActionResult::DEALT_WITH;
		expect(r.guest.verticalEncoderAction(4)).to_equal(ActionResult::DEALT_WITH);
		expect(fakelegacyui::last_note_vertical).to_equal(fakelegacyui::NoteVerticalCall{4});
		fakelegacyui::note_vertical_answer = ActionResult::ACTIONED_AND_CAUSED_CHANGE;
		expect(r.guest.verticalEncoderAction(5)).to_equal(ActionResult::ACTIONED_AND_CAUSED_CHANGE);
		r.guest.shell().tick(2);
		expect(fakelegacyui::note_vertical_forwards).to_equal(3);
		expect(r.base.seen.empty()).to_be_true();
	});

	it("arms the tick after a forward only while the guest is still current, on all three hooks", _ {
		// The legacy handlers are called from inside the guest's hooks and can open or close UIs
		// (re-entrancy), and `TimerName::UI_SPECIFIC` is delivered to whoever is current
		// (`gui/ui_timer_manager.cpp`) -- `padAction`'s guard, for the same reason, is stood in
		// for the same way.
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		fakelegacyui::note_button_answer = ActionResult::DEALT_WITH;
		fakelegacyui::note_horizontal_answer = ActionResult::DEALT_WITH;
		fakelegacyui::note_vertical_answer = ActionResult::DEALT_WITH;

		OtherUI someone_else;
		fakelegacyui::current_ui = &someone_else;
		(void)r.guest.buttonAction(deluge::hid::button::LOAD, true);
		(void)r.guest.horizontalEncoderAction(1);
		(void)r.guest.verticalEncoderAction(1);
		expect(fakelegacyui::note_button_forwards).to_equal(1);
		expect(fakelegacyui::note_horizontal_forwards).to_equal(1);
		expect(fakelegacyui::note_vertical_forwards).to_equal(1);
		expect(fakelegacyui::timer_arms.empty()).to_be_true();

		// Control: the guest current again, the same three calls, one arm each.
		fakelegacyui::current_ui = &r.guest;
		(void)r.guest.buttonAction(deluge::hid::button::LOAD, false);
		expect(fakelegacyui::timer_arms.size()).to_equal(size_t(1));
		(void)r.guest.horizontalEncoderAction(1);
		expect(fakelegacyui::timer_arms.size()).to_equal(size_t(2));
		(void)r.guest.verticalEncoderAction(1);
		expect(fakelegacyui::timer_arms.size()).to_equal(size_t(3));
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(deluge::gui::ui::ShellRuntime::kMinRearmMs));
	});

	// ---- input: a busy card ----------------------------------------------------------------------

	it("feeds and forwards input as it arrives, whatever the scheduler says of the card", _ {
		// No card routine runs re-entrantly under an input handler, so nothing is deferred: a press
		// that arrives while the scheduler reports the card busy is fed, and one a claim forwards is
		// forwarded, both straight away.
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();
		fakelegacyui::reset();
		fakelegacyui::sd_routine_active = true;

		expect(r.guest.padAction(4, 2, 255)).to_equal(ActionResult::DEALT_WITH);
		r.guest.shell().tick(10);
		expect(r.base.seen.size()).to_equal(size_t(1));
		expect(static_cast<int>(r.base.seen[0].ev.x)).to_equal(4);

		r.base.claim = GridPadClaim::None;
		(void)r.guest.padAction(3, 4, 64);
		expect(fakelegacyui::pad_forwards).to_equal(1);
		fakelegacyui::sd_routine_active = false;
	});

	// ---- input: buttons ------------------------------------------------------------------------

	it("decodes a legacy Button back into matrix coordinates, one id per button", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		// LOAD is matrix (6, 1) and SAVE is (6, 3): the same column, so a decode that dropped the
		// row would give both the same id.
		const ActionResult result = r.guest.buttonAction(deluge::hid::button::LOAD, true);
		(void)r.guest.buttonAction(deluge::hid::button::SAVE, true);
		r.guest.shell().tick(1);

		expect(r.base.seen.size()).to_equal(size_t(2));
		expect(r.base.seen[0].ev.kind).to_equal(UiEvent::Kind::Button);
		expect(r.base.seen[0].ev.button).to_equal(ButtonId::Load);
		expect(r.base.seen[1].ev.button).to_equal(ButtonId::Save);

		// **`NOT_DEALT_WITH`, and it is what keeps `Buttons::buttonAction`'s tail running** -- `commandToggleShift`
		// (`hid/buttons.cpp`), the transport, and the mod-encoder button dispatch at the end of the function.
		// `DEALT_WITH` would `goto dealtWith` straight after the current UI's `buttonAction` call.
		expect(result).to_equal(ActionResult::NOT_DEALT_WITH);
	});

	it("delivers an encoder button as an EncoderButton, not a Button", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		(void)r.guest.buttonAction(deluge::hid::button::SELECT_ENC, true);
		r.guest.shell().tick(1);

		// `InputLowering::on_button` splits on `is_encoder_button`,
		// which is a fact about the id the matrix decode produced -- so this also proves the decode
		// landed on the right cell rather than merely on *a* cell.
		expect(r.base.seen.size()).to_equal(size_t(1));
		expect(r.base.seen[0].ev.kind).to_equal(UiEvent::Kind::EncoderButton);
		expect(r.base.seen[0].ev.button).to_equal(ButtonId::SelectEnc);
	});

	it("seeds the shell's sticky-shift setting from runtimeFeatureSettings when it opens", _ {
		// Legacy reads `ShiftIsSticky` at shift-release time (`commandToggleShift` in `hid/buttons.cpp`); the shell
		// reads its own `LoweringConfig` field, which defaults to false. Unseeded, a sticky-shift user's latched shift
		// is invisible to `ctx.shift` -- the whole gate on the menu's shortcut-pad jump (`MenuPads::handle_pad`,
		// `ui/scenes/menu/pads.cpp`).
		Rig on;
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, RuntimeFeatureStateToggle::On);
		expect(on.open()).to_be_true();
		expect(on.guest.shell().lowering().shift_is_sticky()).to_be_true();

		// Vary only the setting, on an otherwise identical guest.
		Rig off;
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, RuntimeFeatureStateToggle::Off);
		expect(off.open()).to_be_true();
		expect(off.guest.shell().lowering().shift_is_sticky()).to_be_false();
	});

	it("latches shift on a tap once the setting is seeded, and does not without it", _ {
		// The seed is only worth having if it changes what a scene sees, so this drives the whole
		// gesture: a tap shorter than `hold_time_ms` (100), then a pad, and asks whether `ctx.shift`
		// survived the release.
		Rig sticky;
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, RuntimeFeatureStateToggle::On);
		expect(sticky.open()).to_be_true();
		AudioEngine::audioSampleTimer = 1000 * 44;
		(void)sticky.guest.buttonAction(deluge::hid::button::SHIFT, true);
		AudioEngine::audioSampleTimer = 1010 * 44; // a 10 ms tap
		(void)sticky.guest.buttonAction(deluge::hid::button::SHIFT, false);
		sticky.guest.shell().tick(10);
		sticky.base.seen.clear();
		(void)sticky.guest.padAction(3, 3, 255);
		sticky.guest.shell().tick(20);
		const PatternScene::Seen* latched = first_of(sticky.base, UiEvent::Kind::Pad);
		expect(latched).not_().to_be_null();
		expect(latched->shift).to_be_true();
		expect(latched->sticky).to_be_true();

		// Vary only the setting; the identical gesture must not latch.
		Rig plain;
		// The `Rig` constructor has already put the feature back to Off; naming it is what makes the
		// contrast with the block above explicit rather than incidental.
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, RuntimeFeatureStateToggle::Off);
		expect(plain.open()).to_be_true();
		AudioEngine::audioSampleTimer = 1000 * 44;
		(void)plain.guest.buttonAction(deluge::hid::button::SHIFT, true);
		AudioEngine::audioSampleTimer = 1010 * 44;
		(void)plain.guest.buttonAction(deluge::hid::button::SHIFT, false);
		plain.guest.shell().tick(10);
		plain.base.seen.clear();
		(void)plain.guest.padAction(3, 3, 255);
		plain.guest.shell().tick(20);
		const PatternScene::Seen* unlatched = first_of(plain.base, UiEvent::Kind::Pad);
		expect(unlatched).not_().to_be_null();
		expect(unlatched->shift).to_be_false();
	});

	it("re-reads hold_time_ms from FlashStorage::holdTime on every event, out of audio samples", _ {
		// The other half of the sticky-shift divergence: the threshold a tap is measured against is `FlashStorage::holdTime` in legacy
		// (`hid/buttons.cpp`) and a fixed 100 ms in the shell. `holdTime` is in **samples**, so
		// this is a unit conversion as well as a copy -- a seed that forwarded the number unchanged
		// would give 4410 ms for legacy's default.
		//
		// Asserted after a *feed*, not after `open()`: the read is in `feed()`, which is where
		// legacy's own read is. See the next example for why that placement is load-bearing.
		const auto seeded_by_a_press = [](int32_t hold_samples) {
			FlashStorage::holdTime = hold_samples;
			Rig r;
			(void)r.open();
			(void)r.guest.padAction(1, 1, 255);
			return r.guest.shell().lowering().hold_time_ms();
		};
		expect(seeded_by_a_press((2 * kSampleRate) / 20)).to_equal(uint16_t(100));  // legacy's default
		expect(seeded_by_a_press((20 * kSampleRate) / 20)).to_equal(uint16_t(1000)); // the item's max
		expect(seeded_by_a_press((1 * kSampleRate) / 20)).to_equal(uint16_t(50));    // and its min
		FlashStorage::holdTime = (2 * kSampleRate) / 20;
	});

	it("follows a change made while the guest is open, which is the only way the settings item can", _ {
		// **The placement is the point.** Seeding once, in `opened()`, would assume that a guest that is not open
		// cannot be observing the threshold. That is wrong for the one writer reachable *only while the guest is
		// open*: the menu's hold-time item (`nodes::default_hold_time`), whose DEVICE row's store writes
		// `FlashStorage::holdTime` from inside the shell, so a per-open seed would lag it by a whole guest session.
		//
		// The guest is opened ONCE and the flash global is moved underneath it, which is what the item does; only
		// then is the gesture driven. An `opened()`-only seed fails here and passes the example above, which is why
		// both exist.
		Rig r;
		FlashStorage::holdTime = (2 * kSampleRate) / 20; // 100 ms at open
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, RuntimeFeatureStateToggle::On);
		expect(r.open()).to_be_true();

		FlashStorage::holdTime = (10 * kSampleRate) / 20; // the item writes 500 ms
		AudioEngine::audioSampleTimer = 1000 * 44;
		(void)r.guest.buttonAction(deluge::hid::button::SHIFT, true);
		AudioEngine::audioSampleTimer = 1200 * 44; // a 200 ms tap: a hold at 100 ms, a tap at 500
		(void)r.guest.buttonAction(deluge::hid::button::SHIFT, false);
		expect(r.guest.shell().lowering().hold_time_ms()).to_equal(uint16_t(500));
		r.guest.shell().tick(200);
		r.base.seen.clear();
		(void)r.guest.padAction(3, 3, 255);
		r.guest.shell().tick(210);
		const PatternScene::Seen* pad = first_of(r.base, UiEvent::Kind::Pad);
		expect(pad).not_().to_be_null();
		expect(pad->sticky).to_be_true();

		FlashStorage::holdTime = (2 * kSampleRate) / 20;
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, RuntimeFeatureStateToggle::Off);
	});

	it("a tap that legacy would latch latches here too, once the threshold is seeded", _ {
		// The seed made visible where it matters: the same 200 ms shift tap, with only
		// `FlashStorage::holdTime` varied, must reach a scene as a latch on one setting and not on
		// the other. With the shell's fixed 100 ms this gesture could never latch at all, which is
		// what the divergence cost a user who had raised the setting.
		const auto latches_with = [](int32_t hold_samples) {
			FlashStorage::holdTime = hold_samples;
			Rig r;
			runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, RuntimeFeatureStateToggle::On);
			(void)r.open();
			AudioEngine::audioSampleTimer = 1000 * 44;
			(void)r.guest.buttonAction(deluge::hid::button::SHIFT, true);
			AudioEngine::audioSampleTimer = 1200 * 44; // a 200 ms tap, the same every time
			(void)r.guest.buttonAction(deluge::hid::button::SHIFT, false);
			r.guest.shell().tick(200);
			r.base.seen.clear();
			(void)r.guest.padAction(3, 3, 255);
			r.guest.shell().tick(210);
			const PatternScene::Seen* pad = first_of(r.base, UiEvent::Kind::Pad);
			return pad != nullptr && pad->sticky;
		};
		expect(latches_with((2 * kSampleRate) / 20)).to_be_false(); // 100 ms: 200 is a hold
		expect(latches_with((10 * kSampleRate) / 20)).to_be_true(); // 500 ms: 200 is a tap
		expect(latches_with((20 * kSampleRate) / 20)).to_be_true(); // 1 s
		expect(latches_with((1 * kSampleRate) / 20)).to_be_false(); // 50 ms
		FlashStorage::holdTime = (2 * kSampleRate) / 20;
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, RuntimeFeatureStateToggle::Off);
	});

	it("feeds each gold-knob push once, though legacy delivers it twice", _ {
		// `Buttons::buttonAction` calls this hook for MOD_ENCODER_0/1 at `hid/buttons.cpp` and then calls
		// `modEncoderButtonAction` for the same press in its tail. The first feeds the shell, whose router hands
		// `EncoderButton{ModEnc0/1}` to the knobs' service; the second is the guest's and does nothing.
		Rig r;
		expect(r.open()).to_be_true();
		KnobRecorder knobs;
		r.guest.shell().set_services(Services{.mod_knobs = &knobs});
		fakelegacyui::reset();
		fakelegacyui::current_ui = &r.guest;
		// `NOT_DEALT_WITH` keeps legacy's tail, and the shift and transport handling above it, running.
		expect(r.guest.buttonAction(deluge::hid::button::MOD_ENCODER_1, true)).to_equal(ActionResult::NOT_DEALT_WITH);
		static_cast<::UI&>(r.guest).modEncoderButtonAction(1, true);
		r.guest.shell().tick(10);
		expect(knobs.count(UiEvent::Kind::EncoderButton)).to_equal(size_t(1));
		expect(fakelegacyui::mod_encoder_button_actions).to_equal(0);
		r.guest.shell().set_services(Services{});
	});

	it("reads shift out of the events it feeds, in both directions", _ {
		Rig r;
		expect(r.open()).to_be_true();

		(void)r.guest.buttonAction(deluge::hid::button::SHIFT, true);
		r.guest.shell().tick(1);
		r.base.seen.clear();
		(void)r.guest.padAction(2, 2, 255);
		r.guest.shell().tick(2);

		const PatternScene::Seen* withShift = first_of(r.base, UiEvent::Kind::Pad);
		expect(withShift).not_().to_be_null();
		expect(withShift->shift).to_be_true();
		// Not the sticky latch: `LoweringConfig::shift_is_sticky` is false here, which is the
		// divergence from legacy recorded on the class (`@par Shift`, point 1).
		expect(withShift->sticky).to_be_false();

		// Vary only the shift state, on the same pad.
		(void)r.guest.buttonAction(deluge::hid::button::SHIFT, false);
		r.guest.shell().tick(3);
		r.base.seen.clear();
		(void)r.guest.padAction(2, 2, 255);
		r.guest.shell().tick(4);

		const PatternScene::Seen* withoutShift = first_of(r.base, UiEvent::Kind::Pad);
		expect(withoutShift).not_().to_be_null();
		expect(withoutShift->shift).to_be_false();
	});
});
// clang-format on

DELUGE_SPEC(shell_host_ui_buttons_spec, shell_host_ui_buttons)
