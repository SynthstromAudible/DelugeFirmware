#include "support/shell_host_ui_rig.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

using namespace shell_host_ui_rig;

// clang-format off
describe shell_host_ui_pad_input("gui::ui::ShellHostUI: pad input and the grid-pad claim", $ {

	// ---- input: pads ---------------------------------------------------------------------------

	it("routes a pad to the top scene with its coordinates and velocity intact, on the next tick", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		AudioEngine::audioSampleTimer = 250 * 44; // nowMs() == 250
		const ActionResult result = r.guest.padAction(5, 3, 137);

		// **Nothing is routed by the hook itself.** `Shell::on_board_event` feeds the lowering;
		// `Shell::pump_input` is what routes, and only `tick` calls it.
		expect(r.base.seen.empty()).to_be_true();

		r.guest.shell().tick(250);
		expect(r.base.seen.size()).to_equal(size_t(1));
		expect(r.base.seen[0].ev.kind).to_equal(UiEvent::Kind::Pad);
		expect(static_cast<int>(r.base.seen[0].ev.x)).to_equal(5);
		expect(static_cast<int>(r.base.seen[0].ev.y)).to_equal(3);
		expect(static_cast<int>(r.base.seen[0].ev.velocity)).to_equal(137);
		expect(r.base.seen[0].ev.on).to_be_true();
		// A fed pad is dealt with: the shell routes it on its next tick.
		expect(result).to_equal(ActionResult::DEALT_WITH);
	});

	it("carries a pad release through as a release rather than a second press", _ {
		Rig r;
		expect(r.open()).to_be_true();
		(void)r.guest.padAction(5, 3, 255);
		r.guest.shell().tick(1);
		r.base.seen.clear();

		(void)r.guest.padAction(5, 3, 0);
		r.guest.shell().tick(2);

		expect(r.base.seen.size()).to_equal(size_t(1));
		expect(r.base.seen[0].ev.on).to_be_false();
		expect(static_cast<int>(r.base.seen[0].ev.velocity)).to_equal(0);
	});

	it("drops a pad coordinate that would fold onto a real one when narrowed", _ {
		Rig r;
		expect(r.open()).to_be_true();
		r.base.seen.clear();

		// 256 is 0 in a `uint8_t`, and `InputLowering::on_pad`'s own bounds check runs on the narrowed value -- so
		// without the guard in `padAction` this arrives as a press on column 0, a pad nobody touched. `padAction` is a
		// public virtual legacy UIs call on each other (`SoundEditor::potentialShortcutPadAction` in
		// `origin/main:src/deluge/gui/ui/sound_editor.cpp`), so its domain is not `MatrixDriver`'s alone.
		(void)r.guest.padAction(256, 0, 255);
		(void)r.guest.padAction(-1, 0, 255);
		r.guest.shell().tick(1);
		expect(r.base.seen.empty()).to_be_true();

		// Vary only the coordinate: column 0 itself still gets through, so the refusal above is
		// about the fold and not about the guest refusing pads generally.
		(void)r.guest.padAction(0, 0, 255);
		r.guest.shell().tick(2);
		expect(r.base.seen.size()).to_equal(size_t(1));
		expect(static_cast<int>(r.base.seen[0].ev.x)).to_equal(0);
	});

	// ---- input: the seam with `Scene::grid_pad_claim()` ----------------------------------------

	it("feeds a pad the scene claims, and does not forward it", _ {
		// `GridPadClaim::All` is the default, so this is every scene that existed before this seam.
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		r.base.claim = GridPadClaim::All;

		(void)r.guest.padAction(3, 4, 64);
		r.guest.shell().tick(1);

		expect(r.base.seen.size()).to_equal(size_t(1));
		expect(fakelegacyui::pad_forwards).to_equal(0);
	});

	it("forwards a pad the scene does not claim, and does not feed it", _ {
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		r.base.claim = GridPadClaim::None;

		(void)r.guest.padAction(3, 4, 64);
		r.guest.shell().tick(1);

		expect(r.base.seen.empty()).to_be_true();
		expect(fakelegacyui::pad_forwards).to_equal(1);
		expect(fakelegacyui::last_pad_forward.x).to_equal(int32_t(3));
		expect(fakelegacyui::last_pad_forward.y).to_equal(int32_t(4));
		expect(fakelegacyui::last_pad_forward.velocity).to_equal(int32_t(64));
	});

	it("under ShortcutsOnly feeds a shift-held main-grid press and forwards everything else", _ {
		// Main's `SoundEditor::potentialShortcutPadAction` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) returns
		// `NOT_DEALT_WITH` for a release (`!on`), for a sidebar pad (`x >= kDisplayWidth`) and, with Shift up, for
		// anything but an audition-held press in the instrument clip view; past that gate it claims
		// unconditionally (its closing `return ActionResult::DEALT_WITH`).
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		r.base.claim = GridPadClaim::ShortcutsOnly;

		fakelegacyui::shift_button_pressed = true;
		(void)r.guest.padAction(3, 4, 64);
		r.guest.shell().tick(1);
		expect(r.base.seen.size()).to_equal(size_t(1));

		(void)r.guest.padAction(kDisplayWidth + 1, 4, 64); // sidebar: never a shortcut
		(void)r.guest.padAction(5, 4, 0);                  // the release of a press it never fed
		fakelegacyui::shift_button_pressed = false;
		(void)r.guest.padAction(3, 4, 64); // no shift
		expect(fakelegacyui::pad_forwards).to_equal(3);
	});

	it("under ShortcutsOnly feeds an audition-held press marked as one, and swallows a refused one", _ {
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		r.base.claim = GridPadClaim::ShortcutsOnly;

		fakelegacyui::shortcut_gate = deluge::gui::ui::ShortcutPadGate::Audition;
		(void)r.guest.padAction(3, 4, 64);
		r.guest.shell().tick(1);
		expect(r.base.seen.size()).to_equal(size_t(1));
		expect(r.base.seen.back().ev.shortcut_without_shift).to_be_true();

		fakelegacyui::shortcut_gate = deluge::gui::ui::ShortcutPadGate::Shift;
		(void)r.guest.padAction(6, 4, 64);
		r.guest.shell().tick(2);
		expect(r.base.seen.size()).to_equal(size_t(2));
		expect(r.base.seen.back().ev.shortcut_without_shift).to_be_false();

		fakelegacyui::shortcut_gate = deluge::gui::ui::ShortcutPadGate::Swallow;
		(void)r.guest.padAction(5, 4, 64);
		r.guest.shell().tick(3);
		expect(r.base.seen.size()).to_equal(size_t(2)); // not fed
		expect(fakelegacyui::pad_forwards).to_equal(0); // and not forwarded

		fakelegacyui::shortcut_gate = deluge::gui::ui::ShortcutPadGate::Shift;
		fakelegacyui::sequence_direction_answer = true;
		(void)r.guest.padAction(4, 7, 64);
		r.guest.shell().tick(4);
		expect(r.base.seen.size()).to_equal(size_t(2)); // the note-row editor took it
		expect(fakelegacyui::pad_forwards).to_equal(0);
	});

	it("hands the audition column to the clip audition forward under AllButClipAudition, press and release", _ {
		// `SampleMarkerEditor::padAction`'s audition arm: whatever the root, the host decides on the clip's type.
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		r.base.claim = GridPadClaim::AllButClipAudition;
		fakelegacyui::root_takes_audition = false;
		(void)r.guest.padAction(kDisplayWidth + 1, 4, 64);
		(void)r.guest.padAction(kDisplayWidth + 1, 4, 0);
		expect(fakelegacyui::clip_audition_forwards).to_equal(2);
		expect(fakelegacyui::last_clip_audition.velocity).to_equal(int32_t(0));
		expect(fakelegacyui::pad_forwards).to_equal(0);
		(void)r.guest.padAction(3, 4, 64);
		(void)r.guest.padAction(kDisplayWidth, 4, 64);
		r.guest.shell().tick(1);
		expect(r.base.seen.size()).to_equal(size_t(2));
		expect(fakelegacyui::clip_audition_forwards).to_equal(2);
	});

	it("forwards the audition column and keeps the rest under AllButAudition", _ {
		// `SampleBrowser::padAction`'s first arm: the audition column is the instrument clip view's when that is the
		// root, and the browser's (which swallows it) otherwise.
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		r.base.claim = GridPadClaim::AllButAudition;

		fakelegacyui::root_takes_audition = true;
		(void)r.guest.padAction(kDisplayWidth + 1, 4, 64);
		expect(fakelegacyui::pad_forwards).to_equal(1);
		(void)r.guest.padAction(3, 4, 64);
		(void)r.guest.padAction(kDisplayWidth, 4, 64);
		r.guest.shell().tick(1);
		expect(r.base.seen.size()).to_equal(size_t(2));
		expect(fakelegacyui::pad_forwards).to_equal(1);

		fakelegacyui::root_takes_audition = false;
		(void)r.guest.padAction(kDisplayWidth + 1, 5, 64);
		r.guest.shell().tick(2);
		expect(fakelegacyui::pad_forwards).to_equal(1);
		expect(r.base.seen.size()).to_equal(size_t(3));
	});

	it("takes the main columns back when legacy's scroll finishes", _ {
		// `SampleBrowser::scrollFinished` leaves the scroll's UI mode; the guest then repaints what it owns.
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		r.guest.surfaces().hold_main_columns(true);
		fakelegacy::column_sends.clear();
		r.guest.scrollFinished();
		expect(r.guest.surfaces().main_columns_held()).to_be_false();
		expect(fakelegacyui::modes_exited.size()).to_equal(size_t(1));
		expect(fakelegacyui::modes_exited.empty() ? 0u : fakelegacyui::modes_exited[0])
		    .to_equal(static_cast<uint32_t>(UI_MODE_HORIZONTAL_SCROLL));
		expect(fakelegacy::column_sends.empty()).to_be_false();
	});

	it("reopens with no pad held when the guest closed between a fed press and its release", _ {
		// SHIFT + a pad fed to the menu, the menu closed with the pad still down, the release then legacy's: the next
		// visit's shell must not still hold the pad (LEARN refuses while one is held).
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		r.base.claim = GridPadClaim::ShortcutsOnly;
		fakelegacyui::shift_button_pressed = true;
		(void)r.guest.padAction(3, 4, 64);
		r.guest.shell().tick(1);
		expect(r.guest.shell().lowering().held_pads().test(3, 4)).to_be_true();
		(void)r.guest.exitUI(); // the stack empties: the guest closes
		fakelegacyui::shift_button_pressed = false;
		expect(r.open()).to_be_true();
		expect(r.guest.shell().lowering().held_pads().test(3, 4)).to_be_false();
	});

	it("opens knowing a SHIFT legacy saw go down, and loses it on the release", _ {
		// SHIFT pressed in the clip view, then SHIFT + a shortcut pad opening the menu: the guest's shell never saw
		// the press, but the press is still down and the next shortcut press is a SHIFT one.
		Rig r;
		fakelegacyui::reset();
		fakelegacyui::shift_button_pressed = true;
		expect(r.open()).to_be_true();
		expect(r.guest.shell().lowering().shift()).to_be_true();
		fakelegacyui::shift_button_pressed = false;
		(void)r.guest.buttonAction(deluge::hid::button::SHIFT, false);
		expect(r.guest.shell().lowering().shift()).to_be_false();
	});

	it("opens with a latched SHIFT still latched", _ {
		Rig r;
		fakelegacyui::reset();
		fakelegacyui::shift_button_pressed = true;
		fakelegacyui::shift_stuck = true;
		expect(r.open()).to_be_true();
		expect(r.guest.shell().lowering().shift()).to_be_true();
		expect(r.guest.shell().lowering().sticky()).to_be_true();
		fakelegacyui::reset();
	});

	it("feeds the release of a pad whose press it fed, and only that release", _ {
		// The menu claims a Shift + pad press; the release comes with Shift up and fails the claim's gate, but
		// leaving it with legacy would leave the shell holding the pad for good.
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		r.base.claim = GridPadClaim::ShortcutsOnly;

		fakelegacyui::shift_button_pressed = true;
		(void)r.guest.padAction(3, 4, 64);
		fakelegacyui::shift_button_pressed = false;
		(void)r.guest.padAction(3, 4, 0);
		r.guest.shell().tick(1);
		expect(r.base.seen.size()).to_equal(size_t(2));
		expect(fakelegacyui::pad_forwards).to_equal(0);
		expect(r.guest.shell().lowering().held_pads().test(3, 4)).to_be_false(); // the LEARN arm's "no pad held"

		(void)r.guest.padAction(6, 2, 64); // no shift: legacy's
		(void)r.guest.padAction(6, 2, 0);  // and so is its release
		(void)r.guest.padAction(3, 4, 0);  // a second release of the fed pad is legacy's
		r.guest.shell().tick(2);
		expect(r.base.seen.size()).to_equal(size_t(2));
		expect(fakelegacyui::pad_forwards).to_equal(3);
	});

	it("asks the input top, not the top", _ {
		// A popup is `input_transparent()`, so it is not the scene whose grid this is. Asking
		// `top()` would let a popup's default `All` swallow a pad the menu beneath would forward.
		Rig r;
		r.base.claim = GridPadClaim::None;
		expect(r.open()).to_be_true();
		r.over.passive = true; // a popup / console line: drawn over, never taking input
		expect(r.guest.shell().stack().push(SceneId{kOver, 0})).to_be_true();
		fakelegacyui::reset();

		(void)r.guest.padAction(3, 4, 64);

		expect(fakelegacyui::pad_forwards).to_equal(1);
	});

	it("still arms the tick after a forward", _ {
		// Load-bearing, not housekeeping: the `Exit` arm empties the guest's shell stack, and
		// `ShellHostUI::timerCallback` is what then closes the guest.
		// With no tick armed, the guest would sit open over an empty stack.
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		r.base.claim = GridPadClaim::None;

		(void)r.guest.padAction(3, 4, 64);

		expect(fakelegacyui::ui_specific_armed_ms()).to_equal(int32_t(deluge::gui::ui::ShellRuntime::kMinRearmMs));
	});

	it("does NOT arm the tick after a forward that left some other UI current", _ {
		// A forwarded pad runs a legacy view's own pad handler, which may open a UI over the guest and leave
		// `getCurrentUI() != &guest` by the time `forwardGridPad` returns. Arming `TimerName::UI_SPECIFIC` there would
		// hand this guest's tick to whatever just opened (`UITimerManager::routine`, `gui/ui_timer_manager.cpp`) -- the
		// same hazard `rearmTimer`'s own guard exists for. `forwardGridPad` is faked in this suite (see
		// `tests/unit/deluge/gui/support/fake_legacy_ui.h`), so the test stands in for "the forward opened another UI"
		// the same way `shell_host_ui_tick_spec.cpp`'s `rearmTimer` example does: by setting `current_ui` directly
		// rather than reproducing a real `openUI`.
		Rig r;
		expect(r.open()).to_be_true();
		fakelegacyui::reset();
		r.base.claim = GridPadClaim::None;

		OtherUI someone_else;
		fakelegacyui::current_ui = &someone_else;

		(void)r.guest.padAction(3, 4, 64);

		expect(fakelegacyui::pad_forwards).to_equal(1); // the forward itself still happened
		expect(fakelegacyui::timer_arms.empty()).to_be_true();

		fakelegacyui::current_ui = &r.guest; // never leave a dangling pointer behind (see `Rig`)
	});
});
// clang-format on

DELUGE_SPEC(shell_host_ui_pad_input_spec, shell_host_ui_pad_input)
