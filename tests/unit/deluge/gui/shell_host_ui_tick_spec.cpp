#include "support/shell_host_ui_rig.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"

#include "support/spec_main.h"

using namespace shell_host_ui_rig;

// clang-format off
describe shell_host_ui_tick("gui::ui::ShellHostUI: the tick: arming, its answer and the LEDs", $ {

	// ---- the tick: arming ------------------------------------------------------------------------

	it("arms UI_SPECIFIC when it opens, and arms nothing when the open is refused", _ {
		Rig r;
		expect(r.open()).to_be_true();
		// Nothing above `opened()` armed a deadline, so the ask is the floor rather than a
		// conversion (`ShellHostUI::armTickSoon`).
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(deluge::gui::ui::ShellRuntime::kMinRearmMs));

		// The same guest shape, varying only the one thing the refusal is about: no initial scene,
		// so `Shell::init`'s push is refused and `opened()` returns false. `openUI` then rolls the
		// hierarchy back to the previous UI (`gui/ui/ui.cpp`) -- an arm left standing here
		// would deliver this guest's `timerCallback` to *that* UI, since the manager dispatches to
		// `getCurrentUI()` (`gui/ui_timer_manager.cpp`).
		Rig refused;
		expect(refused.guest.opened()).to_be_false();
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(-1));
		expect(fakelegacyui::timer_arms.empty()).to_be_true();
	});

	it("ticks the shell on its own clock and re-arms UI_SPECIFIC to the DURATION until next_wake", _ {
		Rig r;
		r.base.timer = 120; // an ordinary animated scene: "wake me in 120 ms"
		expect(r.open()).to_be_true();

		set_clock_ms(500);
		expect(r.guest.timerCallback()).to_equal(ActionResult::DEALT_WITH);

		// The shell reports an ABSOLUTE deadline...
		expect(r.guest.shell().next_wake().has_value()).to_be_true();
		expect(r.guest.shell().next_wake().value_or(0)).to_equal(uint32_t(620));
		// ...and `uiTimerManager` was handed the DURATION. A guest that passed the deadline straight
		// through would have armed 620 ms, and one that passed `now` would have armed 500.
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(120));
		// The tick really ran on `nowMs()`, not on 0: `next_wake` is `now + 120`.
		expect(fakelegacyui::timer_arms.back().which).to_equal(TimerName::UI_SPECIFIC);
	});

	it("wakes for a fact raised between ticks by a legacy callback, after the legacy drain took its own copy", _ {
		Rig r;
		expect(r.open()).to_be_true();
		set_clock_ms(500);
		(void)r.guest.timerCallback();
		uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC); // the timer fired; nothing re-armed it
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(-1));

		// `noteModeReadValueAgain`'s shape: a legacy callback raises a fact while the guest sleeps, and
		// `drain_notifications` runs first, as it does on every pass of the app loop.
		deluge::base::notify::raise(deluge::base::notify::Dirty::Oled);
		(void)deluge::base::notify::dirty().drain(deluge::base::notify::DirtyConsumer::Legacy);
		r.guest.wakeForPendingFacts();
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(deluge::gui::ui::ShellRuntime::kMinRearmMs));

		const int changed = r.base.model_changed;
		set_clock_ms(501);
		(void)r.guest.timerCallback();
		expect(r.base.model_changed).to_equal(changed + 1);
	});

	it("arms NOTHING when it is no longer the current UI, however loudly the shell asks for a wake", _ {
		// **The guard at `ShellHostUI::rearmTimer`'s top**.
		// `TimerName::UI_SPECIFIC` is the *current* UI's timer -- `UITimerManager::routine` dispatches
		// it to `getCurrentUI()->timerCallback()` and never to whatever armed it
		// (`gui/ui_timer_manager.cpp`) -- so arming it while something else is current hands
		// this guest's tick to that something else.
		//
		// This is not hypothetical, and it is not a state the guest puts itself into: three of the
		// dispatcher's arms end in `openUI(...)` (`gui/ui/shell_action_dispatch.cpp`), and legacy's
		// `NO_NAVIGATION` semantics correctly leave the menu on the shell stack when they do --
		// so `timerCallback`'s `depth() == 0` close does not fire and control reaches `rearmTimer`
		// with a foreign UI on top. `dispatchShellActions` is stubbed in this suite, so the *cause*
		// cannot be reproduced here; the guest's response to the resulting state can, and is exactly
		// what this pins. The end-to-end version, with the real dispatcher opening the real
		// `performanceView`, is `tests/integration/ui/scenes/menu_actions_spec.cpp`'s
		// `menu_action_three_verbs`.
		Rig r;
		// A scene that wants a wake on every tick, so "nothing was armed" cannot be the shell simply
		// having nothing to ask for -- `next_wake()` answering `nullopt` takes a different early
		// return in `ShellHostUI::rearmTimer` and would make this example vacuous.
		r.base.timer = 120;
		expect(r.open()).to_be_true();

		// The control leg, and it is what makes the subject mean anything: still current, so the arm
		// happens. Same rig, same scene, same clock -- one thing differs below.
		set_clock_ms(500);
		(void)r.guest.timerCallback();
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(120));

		// Now something else is the current UI -- the state an arm ending in `openUI(...)` leaves.
		fakelegacyui::timer_arms.clear();
		OtherUI someone_else;
		fakelegacyui::current_ui = &someone_else;
		set_clock_ms(600);
		expect(r.guest.timerCallback()).to_equal(ActionResult::DEALT_WITH);

		// The shell still wants a wake -- so the refusal is the guard's doing and not an empty shell.
		expect(r.guest.shell().next_wake().has_value()).to_be_true();
		expect(fakelegacyui::timer_arms.empty()).to_be_true();

		// And it recovers the moment the guest is current again, which is the half that says the
		// guard is not a wedge. In production `closeUI` does this through `focusRegained`
		// (`gui/ui/ui.cpp`); here the topology is declared rather than simulated.
		fakelegacyui::current_ui = &r.guest;
		set_clock_ms(700);
		(void)r.guest.timerCallback();
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(120));
	});

	it("arms the floor -- not zero -- for a deadline that is already now, and does not wedge there", _ {
		Rig r;
		// `Scene::next_timer()` may legally answer 0, meaning "next tick"
		// (`Scene::next_timer`); `AudioClipScene` answers it for the whole of playback.
		// `next_wake()` is then `now_ms` on
		// every tick, for ever.
		r.base.timer = 0;
		expect(r.open()).to_be_true();

		set_clock_ms(500);
		(void)r.guest.timerCallback();
		// The deadline really is `now`, so the clamp is what is being tested and not an accident of
		// the deadline being one millisecond away.
		expect(r.guest.shell().next_wake().value_or(0)).to_equal(uint32_t(500));
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(deluge::gui::ui::ShellRuntime::kMinRearmMs));

		// `setTimer(UI_SPECIFIC, 0)` would put `triggerTime` at exactly the audio sample timer, which
		// the audio engine leaves behind before `UITimerManager::routine` is next called at all -- so
		// the whole timer sweep, and the shell's whole tick, would run on every pass of the main loop
		// for as long as this scene is stacked. 1 ms is the smallest duration `setTimer`'s `ms * 44`
		// can express that leaves `triggerTime` strictly in the future. The floor is what stops that;
		// the chain must still make progress.
		uint32_t now = 500;
		const std::vector<uint32_t> fired = run_quiet(r.guest, now, 4);
		expect(fired.size()).to_equal(size_t(4));
		expect(nth_or(fired, 0, uint32_t(0))).to_equal(uint32_t(501));
		expect(nth_or(fired, 3, uint32_t(0))).to_equal(uint32_t(504));
	});

	it("clamps a far-future deadline, because setTimer computes ms * 44 into an int32_t", _ {
		// A deadline inside the cap is armed exactly, so the clamp is not just "always the cap".
		Rig near;
		near.base.timer = 40'000'000; // ~11 hours
		expect(near.open()).to_be_true();
		set_clock_ms(500);
		(void)near.guest.timerCallback();
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(40'000'000));

		// Past it, the arm saturates. Unclamped, `ms * 44` is 4.4e9 and overflows
		// (`UITimerManager::setTimer` in `gui/ui_timer_manager.cpp`); waking early instead costs one tick, because the
		// next tick recomputes `next_wake()` from scratch.
		Rig far;
		far.base.timer = 100'000'000; // ~27 hours
		expect(far.open()).to_be_true();
		set_clock_ms(500);
		(void)far.guest.timerCallback();
		expect(far.guest.shell().next_wake().value_or(0)).to_equal(uint32_t(100'000'500));
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(deluge::gui::ui::ShellRuntime::kMaxRearmMs));
		to_be_at_most(expect(int64_t(deluge::gui::ui::ShellRuntime::kMaxRearmMs) * deluge::gui::ui::ShellRuntime::kTimerSamplesPerMs), int64_t(2147483647));
	});

	it("arms nothing when the shell wants nothing, and input arms it again", _ {
		Rig r; // no scene timer, no declared LED, nothing animated
		expect(r.open()).to_be_true();

		uint32_t now = 0;
		const std::vector<uint32_t> fired = run_quiet(r.guest, now, 5);
		// One tick ran (the one `opened()` asked for) and then the chain stopped, because there was
		// nothing left to wake for. That is "sleep until the next input", not a freeze.
		expect(fired.size()).to_equal(size_t(1));
		expect(r.guest.shell().next_wake().has_value()).to_be_false();
		expect(uiTimerManager.isTimerSet(TimerName::UI_SPECIFIC)).to_be_false();

		// And the guest is not wedged: a press re-arms it, which is the only reason a press ever
        // reaches a scene -- routing happens in `Shell::pump_input`, which only `tick()` calls.
		expect(r.guest.padAction(3, 4, 200)).to_equal(ActionResult::DEALT_WITH);
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(deluge::gui::ui::ShellRuntime::kMinRearmMs));
		expect(r.base.seen.empty()).to_be_true(); // nothing routed yet: the tick has not run
		const std::vector<uint32_t> after = run_quiet(r.guest, now, 2);
		expect(after.empty()).to_be_false();
		expect(first_of(r.base, UiEvent::Kind::Pad)).not_().to_be_null();
	});

	it("re-arms on focusRegained, because closeUI clears UI_SPECIFIC one line before calling it", _ {
		Rig r;
		expect(r.open()).to_be_true();
		uint32_t now = 0;
		(void)run_quiet(r.guest, now, 3);
		expect(uiTimerManager.isTimerSet(TimerName::UI_SPECIFIC)).to_be_false();

		// What legacy does when a UI opened over the guest closes again: `closeUI` unsets the timer
		// (`gui/ui/ui.cpp`) and then hands focus back. Without the re-arm the guest
		// would tick normally until anything was opened over it and never again.
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, 250); // the guest was mid-animation
		uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
		r.guest.focusRegained();

		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(deluge::gui::ui::ShellRuntime::kMinRearmMs));
	});

	it("asks for a tick on displayOrLanguageChanged, which boot() alone cannot", _ {
		Rig r;
		expect(r.open()).to_be_true();
		uint32_t now = 0;
		(void)run_quiet(r.guest, now, 3);
		expect(uiTimerManager.isTimerSet(TimerName::UI_SPECIFIC)).to_be_false();

		// `Shell::boot()` calls `request_repaint()`, which fires a gate but arms no deadline, so `compute_next_wake` has nothing to report and the repaint
        // would wait for an unrelated wake that never comes.
		r.guest.displayOrLanguageChanged();
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(deluge::gui::ui::ShellRuntime::kMinRearmMs));

		// And that tick really does repaint: legacy's buffer, blanked first, comes back non-blank.
		OLED::main.clear();
		(void)fakelegacy::consume_oled_frame();
		(void)run_quiet(r.guest, now, 1);
		expect(any_ink(OLED::main.bytes())).to_be_true();
	});

	it("arms on exitUI when the guest survives the pop, and not when it closes", _ {
		Rig survive;
		expect(survive.open()).to_be_true();
		expect(survive.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();
		uint32_t now = 0;
		(void)run_quiet(survive.guest, now, 3);
		expect(uiTimerManager.isTimerSet(TimerName::UI_SPECIFIC)).to_be_false();

		// Reached from `TimerName::BACK_MENU_EXIT` (`UITimerManager::routine` in `gui/ui_timer_manager.cpp`), not from
		// `UI_SPECIFIC`, so nothing has re-armed the tick and the stack has just changed.
		expect(survive.guest.exitUI()).to_equal(ActionResult::DEALT_WITH);
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(deluge::gui::ui::ShellRuntime::kMinRearmMs));

		// The close path, varying only the stack depth. `close()` is `closeUI(this)`, which unsets
		// the timer itself (`gui/ui/ui.cpp`) -- so arming here would be undone one line later,
		// or worse, handed to the UI beneath.
		Rig closing;
		expect(closing.open()).to_be_true();
		(void)run_quiet(closing.guest, now, 3);
		fakelegacyui::timer_arms.clear();
		expect(closing.guest.exitUI()).to_equal(ActionResult::ACTIONED_AND_CAUSED_CHANGE);
		expect(fakelegacyui::closed.size()).to_equal(size_t(1));
		expect(fakelegacyui::timer_arms.empty()).to_be_true();
	});

	// ---- the tick: its answer -------------------------------------------------------------------------

	it("answers its timer DEALT_WITH whatever the scheduler says of the card", _ {
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::sd_routine_active = true;
		set_clock_ms(10);
		expect(r.guest.timerCallback()).to_equal(ActionResult::DEALT_WITH);
		fakelegacyui::sd_routine_active = false;
		set_clock_ms(20);
		expect(r.guest.timerCallback()).to_equal(ActionResult::DEALT_WITH);
	});

	// ---- the tick: LEDs, and the ownership rule in both directions --------------------------------

	it("blinks a declared LED on schedule with no input at all", _ {
		Rig r;
		// `LedId`'s values ARE `ButtonId`'s (`ui/shell/profiles/deluge.h`), and the profile maps
		// `Back` to board LED 16 -- cross-checked here against legacy's own enum rather than
		// hardcoded, so a profile that renumbered would fail here rather than pass silently.
		const uint8_t back = static_cast<uint8_t>(IndicatorLED::BACK);
		expect(back).to_equal(uint8_t(16));
		r.base.led_declared = true;
		r.base.led_id = static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Back);
		// The same shape `MenuScene::leds` declares for BACK and LEARN
		// (`MenuScene::leds`, `MenuLearn::leds`): a plain blink, no deadline.
		r.base.led_state = deluge::ui::widgets::core::LedState{deluge::ui::widgets::core::LedState::Kind::Blink, 128, 0};
		expect(r.open()).to_be_true();

		// The whole chain, driven by nothing but the guest's own arms.
		uint32_t now = 0;
		const std::vector<uint32_t> fired = run_quiet(r.guest, now, 5);

		// Each wake lands on the LED's own half-period boundary (`led_next_change`), not on some unrelated cadence that happens to divide it.
		expect(fired.size()).to_equal(size_t(5));
		expect(nth_or(fired, 0, uint32_t(0))).to_equal(uint32_t(1));   // the arm `opened()` asked for
		expect(nth_or(fired, 1, uint32_t(0))).to_equal(uint32_t(128)); // ...and then the blink is the only wake source
		expect(nth_or(fired, 2, uint32_t(0))).to_equal(uint32_t(256));
		expect(nth_or(fired, 3, uint32_t(0))).to_equal(uint32_t(384));
		expect(nth_or(fired, 4, uint32_t(0))).to_equal(uint32_t(512));

		// And the LED actually changed state on those boundaries, through
		// `indicator_leds::setLedState`. The first tick sends it lit (`sent_once` was false), then
		// one write per flip and no writes in between -- `Shell::flush` diffs
		// (`led_send_needed`).
		const std::vector<bool> history = led_history(back);
		expect(history.size()).to_equal(size_t(5));
		expect(nth_or(history, 0, !true)).to_be_true();
		expect(nth_or(history, 1, !false)).to_be_false();
		expect(nth_or(history, 2, !true)).to_be_true();
		expect(nth_or(history, 3, !false)).to_be_false();
		expect(nth_or(history, 4, !true)).to_be_true();
	});

	it("owns only the LEDs a scene declares, in both directions", _ {
		Rig r;
		const uint8_t back = static_cast<uint8_t>(IndicatorLED::BACK);
		r.base.led_declared = true;
		r.base.led_id = static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Back);
		r.base.led_state = deluge::ui::widgets::core::LedState{deluge::ui::widgets::core::LedState::Kind::Blink, 128, 0};
		indicator_leds::ledStates[back] = true; // legacy's state for BACK before the guest takes it
		expect(r.open()).to_be_true();

		uint32_t now = 0;
		(void)run_quiet(r.guest, now, 5);
		expect(led_history(back).size()).to_equal(size_t(5));

		// **Nothing else was touched.** Every other board LED is legacy's while the guest is open,
		// and an undeclared id cannot pass `led_send_needed`'s `desired_declared` test however many
		// ticks run. PLAY is the sharp one: legacy drives it from the
		// transport all the way through a menu.
		for (const fakelegacy::LedWrite& w : fakelegacy::led_writes) {
			expect(w.led).to_equal(back);
		}
		expect(led_history(static_cast<uint8_t>(IndicatorLED::PLAY)).empty()).to_be_true();

		// **And the other direction: an LED the scene stops declaring goes back to legacy**, in the
		// state legacy had it in when the guest took it (`GuestSurfaceSet::release_led`). Only the
		// declaration is varied -- same scene, same clock, same blink. The arm the fifth tick left
		// standing is the blink's own next boundary (640 = 512 + 128), so the next tick lands exactly
		// where a guest that still thought it owned the LED would write the flip. Nothing is re-armed
		// by hand here: that the chain continues at all is part of what is being asserted.
		const size_t before = fakelegacy::led_writes.size();
		r.base.led_declared = false;
		const std::vector<uint32_t> after = run_quiet(r.guest, now, 4);

		// **Exactly ONE further tick runs, and that is the mechanism rather than a limitation.** With
		// nothing declared the composed `LedFrame` is empty, so `compute_next_wake`'s blink loop
		// has no source, `next_wake()` is `std::nullopt` and the chain
		// ends -- which is itself the ownership rule showing through the wake computation. One tick
		// is all this assertion needs: it is the tick on which the flip would have been written.
		expect(after.size()).to_equal(size_t(1));
		expect(nth_or(after, 0, uint32_t(0))).to_equal(uint32_t(640)); // 512 + 128, a phase boundary
		// One write, the hand-back: legacy's own `true`, not the blink's next phase.
		expect(fakelegacy::led_writes.size()).to_equal(before + 1);
		expect(fakelegacy::led_writes.back().led).to_equal(back);
		expect(fakelegacy::led_writes.back().on).to_be_true();
	});

	it("writes a knob indicator only when something declares one", _ {
		Rig r;
		r.base.led_declared = true;
		r.base.led_id = static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Back);
		r.base.led_state = deluge::ui::widgets::core::LedState{deluge::ui::widgets::core::LedState::Kind::Blink, 128, 0};
		expect(r.open()).to_be_true();
		uint32_t now = 0;
		(void)run_quiet(r.guest, now, 5);

		// `Shell::flush` calls `set_indicator` only for an indicator declared this tick, and the
		// only declarer in the tree is `ModKnobs`, which is a *service* -- and this guest is built
		// with `Services{}`. So legacy's `knobIndicatorLevels[]` shadow, which
		// `GuestSurfaceSet::set_indicator` cannot keep true, cannot go stale until that service is
		// installed. This example is what makes that a measurement rather than a
		// reading of the code: the LED writes above prove the tick ran.
		expect(fakelegacy::indicator_writes.empty()).to_be_true();
		expect(fakelegacy::led_writes.empty()).to_be_false();

		// **And the other half, so the claim is about the declaration and not about the guest never
		// writing indicators at all.** Installing a service that declares one -- which is exactly
		// what the application does with `ModKnobs`, through `Shell::set_services` -- makes the very next tick write it. That write is the moment legacy's
		// `knobIndicatorLevels[]` shadow starts diverging from the hardware, and
		// `GuestSurfaceSet::set_indicator` cannot update it, so the guest's close path has to call
		// `indicator_leds::clearKnobIndicatorLevels()`, which "hands the knob indicator rings back on the close path"
		// in `shell_host_ui_lifecycle_spec.cpp` pins (`hid/led/indicator_leds.h`).
		//
		// This example cannot fail when the real installation changes -- it builds its own `Services`
		// and the real installation happens in `src/deluge/deluge.cpp` -- so it is a demonstration of the
		// mechanism and the hand-over, not a guard. The guard belongs in the spec of the real
		// installation, and this is the shape it has to cover.
		IndicatorService svc;
		r.guest.shell().set_services(Services{.mod_knobs = &svc});
		expect(run_quiet(r.guest, now, 1).size()).to_equal(size_t(1));
		expect(fakelegacy::indicator_writes.size()).to_equal(size_t(1));
		// Read through a fallback for the same reason as `nth_or` (`support/shell_host_ui_rig.h`): a mutation that stops the
		// chain leaves this vector empty, and an out-of-range read is undefined behaviour rather than
		// a failed expectation.
		const fakelegacy::IndicatorWrite got =
		    fakelegacy::indicator_writes.empty() ? fakelegacy::IndicatorWrite{} : fakelegacy::indicator_writes[0];
		expect(got.which).to_equal(uint8_t(0)); // IndicatorId::Lower
		expect(got.count).to_equal(uint8_t(4));
		expect(got.levels[2]).to_equal(uint8_t(0x5A));
	});
});
// clang-format on

DELUGE_SPEC(shell_host_ui_tick_spec, shell_host_ui_tick)
