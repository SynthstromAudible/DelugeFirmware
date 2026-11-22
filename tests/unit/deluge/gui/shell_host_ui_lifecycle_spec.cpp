#include "support/shell_host_ui_rig.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

using namespace shell_host_ui_rig;

// clang-format off
describe shell_host_ui_lifecycle("gui::ui::ShellHostUI: opening, closing and exiting", $ {

	// ---- lifecycle ----------------------------------------------------------------------------

	it("opens by pushing the scene it was given, and refuses when it was given none", _ {
		Rig r;
		expect(r.guest.canOpen()).to_be_true();
		expect(r.open()).to_be_true();
		expect(r.guest.shell().stack().depth()).to_equal(size_t(1));
		expect(r.base.enters).to_equal(1);

		// A guest nobody called `setInitialScene` on holds a default `SceneId`, whose kind is
		// `SceneKind::Null` -- unregistered, so `SceneStack::push` refuses it and the stack stays
		// empty. An empty guest would swallow every pad while drawing nothing, so that is a refused
		// open, not an open guest.
		Rig r2;
		expect(r2.guest.getInitialScene().kind).to_equal(SceneKind::Null);
		expect(r2.guest.opened()).to_be_false();
		expect(r2.guest.shell().stack().depth()).to_equal(size_t(0));
	});

	it("re-opening starts a fresh stack rather than stacking the same scene twice", _ {
		Rig r;
		expect(r.open()).to_be_true();
		// Something pushed over the menu, and then the guest is opened again without having been
		// unwound -- e.g. a refused rollback, or a caller that opens it twice.
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();
		expect(r.guest.shell().stack().depth()).to_equal(size_t(2));

		expect(r.open()).to_be_true();
		expect(r.guest.shell().stack().depth()).to_equal(size_t(1));
		// Both scenes were exited by the reset before the new push, so neither is stacked twice --
		// which `SceneStack` has no coherent lifecycle for.
		expect(r.over.exits).to_equal(1);
		expect(r.base.exits).to_equal(1);
		expect(r.base.enters).to_equal(2);
	});

	it("resumes the input top on focusRegained, not a passive layer sitting above it", _ {
		Rig r;
		r.over.passive = true; // a popup / console line: drawn over, never taking input
		expect(r.open()).to_be_true();
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();
		r.base.resumes = 0;
		r.over.resumes = 0;

		r.guest.focusRegained();

		// `top()` is the passive layer; `input_top()` is the scene that genuinely gets input back,
		// and `Scene::on_resume`'s contract is about the latter.
		expect(r.guest.shell().stack().top().kind()).to_equal(kOver);
		expect(r.base.resumes).to_equal(1);
		expect(r.over.resumes).to_equal(0);
	});

	it("refuses to open at a panel geometry legacy's canvas cannot hold, and draws nothing there", _ {
		Rig r{board_64(), profile_64()};
		// 128x64 at 1bpp is 1024 bytes; `OLED::main` has 768 and no way to grow.
		expect(r.guest.surfaces().oled()[0].geometry.height).to_equal(uint16_t(64));
		expect(r.guest.canOpen()).to_be_false();
		// The panel has to be the ONLY reason it refuses: `open()` gives it a scene that is really
		// registered, so a guest that skipped the `canOpen()` check would push it and return true.
		expect(r.open()).to_be_false();
		expect(r.guest.shell().stack().depth()).to_equal(size_t(0));
		expect(r.base.enters).to_equal(0);

		// And `renderOLED` must leave the canvas legacy handed it exactly as it found it: a
		// half-copied frame is worse than the stale one legacy would otherwise send.
		OLED::main.clear();
		r.guest.renderOLED(OLED::main);
		expect(any_ink(OLED::main.bytes())).to_be_false();
	});

	it("refuses a panel that is the wrong SHAPE even when it is the right number of bytes", _ {
		// 64x96 is 768 bytes, exactly what `OLED::main` holds, so a byte-count check would say yes
		// and every frame would then be copied in garbled -- checked in the
		// guest's own hook rather than in `blit`'s. This is the only board that separates the shape
		// guard from the size guard, which is why it is here and not folded into the 64-row example.
		static constexpr DelugeDisplayDesc kDisplays96[] = {{64, 96, 0}};
		DelugeBoard b = kDelugeBoard;
		b.display_width = 64;
		b.display_height = 96;
		b.display_count = 1;
		b.displays = kDisplays96;
		Rig r{b, deluge_profile()};

		expect(r.guest.surfaces().oled()[0].canvas.bytes().size()).to_equal(OLED::main.bytes().size());
		expect(r.guest.canOpen()).to_be_false();
		expect(r.open()).to_be_false();

		std::span<uint8_t> legacy = OLED::main.bytes();
		for (size_t i = 0; i < legacy.size(); i++) {
			legacy[i] = static_cast<uint8_t>(0x3C);
		}
		r.guest.renderOLED(OLED::main);
		for (uint8_t byte : OLED::main.bytes()) {
			expect(byte).to_equal(uint8_t(0x3C));
		}
	});

	// ---- exitUI, both directions --------------------------------------------------------------

	it("pops one shell scene on the long-press exit and stays open while the stack still has one", _ {
		Rig r;
		expect(r.open()).to_be_true();
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();
		expect(r.guest.shell().stack().depth()).to_equal(size_t(2));

		const ActionResult result = r.guest.exitUI();

		expect(r.guest.shell().stack().depth()).to_equal(size_t(1));
		expect(r.over.exits).to_equal(1);
		// Consumed, and the guest is still on legacy's hierarchy: closing here would pop it a level
		// early, taking the whole guest down for a Back that only left one scene inside it.
		expect(result).to_equal(ActionResult::DEALT_WITH);
		expect(fakelegacyui::closed.empty()).to_be_true();
	});

	it("leaves a context menu up on a long BACK, as UI::exitUI does", _ {
		// No context menu overrides `exitUI` in main, so the long-press timer's call does nothing there.
		Rig r;
		expect(r.open()).to_be_true();
		expect(r.guest.shell().stack().push(SceneId{deluge::ui::scenes::context_menu::kContextMenuSceneKind, 0}))
		    .to_be_true();
		const ActionResult result = r.guest.exitUI();
		expect(result).to_equal(ActionResult::DEALT_WITH);
		expect(r.guest.shell().stack().input_top().kind())
		    .to_equal(deluge::ui::scenes::context_menu::kContextMenuSceneKind);
		expect(r.guest.shell().stack().depth()).to_equal(size_t(2));
		expect(fakelegacyui::closed.empty()).to_be_true();
	});

	it("closes itself when the pop empties the stack, because nothing else will", _ {
		Rig r;
		expect(r.open()).to_be_true();
		expect(r.guest.shell().stack().depth()).to_equal(size_t(1));

		const ActionResult result = r.guest.exitUI();

		expect(r.guest.shell().stack().depth()).to_equal(size_t(0));
		expect(r.base.exits).to_equal(1);
		// `closeUI(this)`, the same thing `RenameUI::exitUI` and `SampleMarkerEditor::exitUI` do
		// (`gui/ui/rename/rename_ui.cpp`, `origin/main:src/deluge/gui/ui/sample_marker_editor.cpp`) and the
		// same thing `SoundEditor::exitCompletely` does at `origin/main:src/deluge/gui/ui/sound_editor.cpp`. Without
		// it the user is trapped: neither reader of the return value closes anything.
		expect(fakelegacyui::closed.size()).to_equal(size_t(1));
		expect(fakelegacyui::closed[0]).to_equal(static_cast<::UI*>(&r.guest));
		// **The answer is `ACTIONED_AND_CAUSED_CHANGE`, not `NOT_DEALT_WITH`.** `ui.h`'s contract for this hook is
		// "returns whether a UI exited" (`UI::exitUI` in `gui/ui/ui.h`), a UI did, and every legacy self-closing
		// implementation answers this value -- including the tenant the guest stands in for, whose `exitUI` is
		// `exitCompletely()` (`origin/main:src/deluge/gui/ui/sound_editor.h`, returning at
		// `origin/main:src/deluge/gui/ui/sound_editor.cpp`).
		expect(result).to_equal(ActionResult::ACTIONED_AND_CAUSED_CHANGE);
	});

	it("is safe on an already-empty stack: it closes once and pops nothing", _ {
		Rig r;
		expect(r.guest.shell().stack().depth()).to_equal(size_t(0));
		const ActionResult result = r.guest.exitUI();
		expect(result).to_equal(ActionResult::ACTIONED_AND_CAUSED_CHANGE);
		expect(fakelegacyui::closed.size()).to_equal(size_t(1));
	});

	// ---- the SHORT Back, which is the exit a user actually performs -----------------------------
	//
	// `exitUI` above is the LONG press only: `Buttons` arms `TimerName::BACK_MENU_EXIT` on Back
	// down (`Buttons::buttonAction` in `hid/buttons.cpp`) and the manager calls `exitUI` from that arm alone
	// (`UITimerManager::routine` in `gui/ui_timer_manager.cpp`). A short Back is fed into the shell like any other
	// button and routed inside `Shell::tick`, where a scene can take itself off the stack --
	// `MenuScene`'s `Action::Kind::Remove` at depth 0. If nothing closed the guest
	// when that emptied the stack, `shellHostUI` would stay current, drawing nothing and swallowing
	// every later press.

	it("closes itself when a scene removes itself during the tick and empties the stack", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.remove_self_on_event = true;
		fakelegacyui::reset();

		// The button is fed, not actioned: routing happens in `Shell::tick`, which only
		// `timerCallback` calls. So nothing has closed yet at this point...
		(void)r.guest.buttonAction(deluge::hid::button::BACK, true);
		expect(r.guest.shell().stack().depth()).to_equal(size_t(1));
		expect(fakelegacyui::closed.empty()).to_be_true();

		// Reset AFTER the press and before the tick, so the arm assertion below is about the tick
		// alone: `buttonAction` arms `kMinRearmMs` on its way out (`armTickSoon`), which is correct
		// and would otherwise be the thing the last expectation saw.
		fakelegacyui::reset();

		// ...and the tick is what empties the stack and must therefore be what closes.
		expect(r.guest.timerCallback()).to_equal(ActionResult::DEALT_WITH);

		expect(r.guest.shell().stack().depth()).to_equal(size_t(0));
		expect(fakelegacyui::closed.size()).to_equal(size_t(1));
		expect(fakelegacyui::closed[0]).to_equal(static_cast<::UI*>(&r.guest));
		// And no re-arm on the close path: `closeUI` unsets `TimerName::UI_SPECIFIC` itself
		// (`gui/ui/ui.cpp`) before handing focus to the UI beneath, so a timer armed here would
		// either be undone one line later or deliver this guest's `timerCallback` to that UI
		// (`gui/ui_timer_manager.cpp`).
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(-1));
	});

	it("does NOT close on a tick that leaves the stack occupied, and re-arms as usual", _ {
		// The other direction, and the one that makes the example above mean something: a guest
		// that closed on every tick would pass that one too.
		Rig r;
		r.base.timer = 120;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();

		expect(r.guest.timerCallback()).to_equal(ActionResult::DEALT_WITH);

		expect(r.guest.shell().stack().depth()).to_equal(size_t(1));
		expect(fakelegacyui::closed.empty()).to_be_true();
		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(120));
	});

	// ---- the hooks deliberately left to the base ----------------------------------------------

	it("does not override graphicsRoutine, so the base's forward to the root view survives", _ {
		Rig r;
		r.base.occludes = false;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();

		static_cast<::UI&>(r.guest).graphicsRoutine();

		// **What this proves, precisely: non-override.** The fake replaces `UI::graphicsRoutine`'s
		// body with a counter (`tests/unit/deluge/gui/support/fake_legacy_ui.cpp`), because the real body needs
		// `getRootUI()` and a legacy view tree this suite does not build -- so reaching the counter
		// says the call landed on the base, and says nothing about a root view animating. That the
		// base's real body then runs the root's animation while `canSeeViewUnderneath()` is a claim
		// about `UI::graphicsRoutine` (`gui/ui/ui.cpp`), read, not driven here. The example's job is the half that can
		// regress silently: `ShellHostUI` adding an override.
		expect(fakelegacyui::graphics_routines).to_equal(1);
		// And the predicate the base gates on, evaluated by the fake, is the one the guest answers
		// true for on a non-occluding scene -- so if a future override did appear, the thing it
		// would be suppressing is a live `true`.
		expect(r.guest.canSeeViewUnderneath()).to_be_true();
	});

	it("asks the shell for a repaint when the display or language changed, and still tells legacy", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.guest.shell().tick(0);
		// A tick with nothing new leaves the gates shut, so nothing is blitted and legacy's dirty
		// flag stays down.
		(void)fakelegacy::consume_oled_frame();
		r.guest.shell().tick(1);
		expect(fakelegacy::oled_marked()).to_be_false();
		fakelegacyui::reset();

		r.guest.displayOrLanguageChanged();
		// Legacy still hears about it: the base's `renderUIsForOled()` is what sets
		// `doesOLEDNeedRendering` (`gui/ui/ui.cpp`).
		expect(fakelegacyui::oled_redraws).to_equal(1);
		// And the shell will actually re-render on its next tick, rather than serving the frame it
		// happens to be holding.
		r.guest.shell().tick(2);
		expect(fakelegacy::oled_marked()).to_be_true();
	});

	// ---- the close paths: what runs before close(), and the services each open installs ---------

	it("installs the three services on every open, not only the first", _ {
		// `wireShellGuestServices()` is what `Shell::set_services` is called from. This suite never builds the `deluge_ui_scenes` closure,
		// so what it can assert is that `opened()` calls it -- **again on every open**, because the
		// song and `View`'s mod target can both have moved since the last one, and an unseeded
		// `ModKnobs` blanks the mod LEDs and both knob indicator rings rather than doing nothing.
		// What the three installed objects actually are is asserted where they can be built:
		// `sim/scenes_spec/boot_wiring_spec.cpp`.
		Rig r;
		expect(fakelegacyui::service_wirings).to_equal(0);
		expect(r.open()).to_be_true();
		expect(fakelegacyui::service_wirings).to_equal(1);
		(void)r.guest.exitUI();
		expect(r.open()).to_be_true();
		expect(fakelegacyui::service_wirings).to_equal(2);

		// A refused open does not wire: `openUI` rolls the hierarchy back on `false`
		// (`gui/ui/ui.cpp`), so there is no guest to serve.
		Rig refused;
		expect(fakelegacyui::service_wirings).to_equal(0); // the Rig ctor reset the counter
		refused.guest.setInitialScene(SceneId{SceneKind::Null, 0});
		expect(refused.guest.opened()).to_be_false();
		expect(fakelegacyui::service_wirings).to_equal(0);
	});

	it("closes when only an input-transparent overlay is left, running the close hook first", _ {
		static int hook_calls = 0;
		static size_t closed_at_hook = 0;
		hook_calls = 0;
		Rig r;
		expect(r.open(kBase)).to_be_true();
		r.over.passive = true;
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();
		ShellHostUI::setCloseHook([](uint32_t /*now_ms*/) {
			hook_calls++;
			closed_at_hook = fakelegacyui::closed.size();
		});
		expect(r.guest.shell().stack().remove(kBase)).to_be_true();
		(void)r.guest.timerCallback();
		expect(hook_calls).to_equal(1);
		expect(closed_at_hook).to_equal(size_t(0));
		expect(fakelegacyui::closed.size()).to_equal(size_t(1));
		expect(r.guest.shell().stack().depth()).to_equal(size_t(0));
		ShellHostUI::setCloseHook(nullptr);
		r.over.passive = false;
	});

	it("closes on Back when only an input-transparent overlay is left over the popped scene", _ {
		Rig r;
		expect(r.open(kBase)).to_be_true();
		r.over.passive = true;
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();
		expect(r.guest.exitUI()).to_equal(ActionResult::ACTIONED_AND_CAUSED_CHANGE);
		expect(fakelegacyui::closed.size()).to_equal(size_t(1));
		r.over.passive = false;
	});

	it("hands the knob indicator rings back on the close path, and only on the close path", _ {
		// `GuestSurfaceSet::set_indicator` writes the hardware without legacy's
		// `knobIndicatorLevels[]` shadow, so `actuallySetKnobIndicatorLevel`'s early-out
		// (`hid/led/indicator_leds.cpp`) would otherwise suppress the write that puts the
		// rings right when the guest goes away. It matters because of `ModKnobs` -- the one
		// `frame.indicator(...)` declarer in `src/ui/scenes` -- being installed in the guest.
		Rig r;
		expect(r.open(kBase)).to_be_true();
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();

		// The survive path: a UI did not exit, so nothing is handed back.
		expect(r.guest.exitUI()).to_equal(ActionResult::DEALT_WITH);
		expect(fakelegacyui::knob_indicator_clears).to_equal(0);
		expect(fakelegacyui::closed.empty()).to_be_true();

		// The close path: cleared, exactly once, and **before** `close()` -- `closeUI` hands focus
		// to the UI beneath on the same call (`gui/ui/ui.cpp`) and that is what re-fills the
		// rings, so a clear after it would blank rings nothing then refreshes.
		expect(r.guest.exitUI()).to_equal(ActionResult::ACTIONED_AND_CAUSED_CHANGE);
		expect(fakelegacyui::knob_indicator_clears).to_equal(1);
		expect(fakelegacyui::closed.size()).to_equal(size_t(1));
		expect(fakelegacyui::knob_clear_saw_close).to_be_false();
	});

	it("runs the note mode's exit, the settings save, the drum creator's discard and the slicer's release on both "
	   "close paths, after the indicator clear and before close()", _ {
		// `SoundEditor::exitCompletely`'s note arms (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) run before its
		// `close()`; `closeGuest` is the body of both of the guest's close paths, so both
		// must reach it, and neither survive path may.
		{
			Rig r;
			expect(r.open(kBase)).to_be_true();
			expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();

			expect(r.guest.exitUI()).to_equal(ActionResult::DEALT_WITH);
			expect(fakelegacyui::note_mode_exits).to_equal(0);

			expect(r.guest.exitUI()).to_equal(ActionResult::ACTIONED_AND_CAUSED_CHANGE);
			expect(fakelegacyui::note_mode_exits).to_equal(1);
			expect(fakelegacyui::note_exit_saw_knob_clear).to_be_true();
			expect(fakelegacyui::note_exit_saw_close).to_be_false();
			expect(fakelegacyui::settings_saves).to_equal(1);
			expect(fakelegacyui::settings_save_saw_close).to_be_false();
			expect(fakelegacyui::drum_creator_discards).to_equal(1);
			expect(fakelegacyui::drum_creator_discard_saw_close).to_be_false();
			expect(fakelegacyui::slicer_releases).to_equal(1);
			expect(fakelegacyui::slicer_release_saw_close).to_be_false();
			expect(fakelegacyui::closed.size()).to_equal(size_t(1));
		}
		{
			// The short-Back path: the scene removes itself inside the tick (`timerCallback`'s
			// `depth() == 0` close).
			Rig r;
			expect(r.open()).to_be_true();
			r.base.remove_self_on_event = true;
			(void)r.guest.buttonAction(deluge::hid::button::BACK, true);
			fakelegacyui::reset();
			expect(fakelegacyui::note_mode_exits).to_equal(0);

			expect(r.guest.timerCallback()).to_equal(ActionResult::DEALT_WITH);
			expect(fakelegacyui::note_mode_exits).to_equal(1);
			expect(fakelegacyui::note_exit_saw_knob_clear).to_be_true();
			expect(fakelegacyui::note_exit_saw_close).to_be_false();
			expect(fakelegacyui::settings_saves).to_equal(1);
			expect(fakelegacyui::settings_save_saw_close).to_be_false();
			expect(fakelegacyui::drum_creator_discards).to_equal(1);
			expect(fakelegacyui::drum_creator_discard_saw_close).to_be_false();
			expect(fakelegacyui::slicer_releases).to_equal(1);
			expect(fakelegacyui::slicer_release_saw_close).to_be_false();
			expect(fakelegacyui::closed.size()).to_equal(size_t(1));
		}
	});

	it("refuses a long-press Back while a slice is out, and closes once it is not", _ {
		// A slice that is out lands in the kit it was asked for, so the guest must not close under it.
		Rig r;
		expect(r.open(kBase)).to_be_true();
		fakelegacyui::slice_out = true;
		expect(r.guest.exitUI()).to_equal(ActionResult::DEALT_WITH);
		expect(r.guest.shell().stack().depth()).to_equal(size_t(1));
		expect(fakelegacyui::closed.size()).to_equal(size_t(0));

		fakelegacyui::slice_out = false;
		expect(r.guest.exitUI()).to_equal(ActionResult::ACTIONED_AND_CAUSED_CHANGE);
		expect(fakelegacyui::closed.size()).to_equal(size_t(1));
	});
});
// clang-format on

DELUGE_SPEC(shell_host_ui_lifecycle_spec, shell_host_ui_lifecycle)
