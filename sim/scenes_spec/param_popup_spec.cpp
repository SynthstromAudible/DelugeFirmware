/// @file
/// @brief `ParamPopupScene` -- driven directly (this scene has no caller yet; `ModKnobs`, E4 task
///        2, is the one spec E names for that) and compared against the real legacy it ports:
///        `View::displayModEncoderValuePopup` / `flushPendingModEncoderValuePopup`
///        (`gui/views/view.cpp`) for the arbitration and text, and the real
///        `OLED::displayNotification` (`hid/display/oled.cpp:744-794`) for the pixels the new
///        `ui::oled::popup::notification` widget draws -- reached end to end through a real
///        `Shell`, so the comparison is the assembled scene's own pixels, not the widget in
///        isolation.
///
/// @par What this spec counts as uncovered (oracle discipline)
///
/// Three of legacy's arms need model context, and this spec drives none of them: it calls `show()`
/// with a default `PopupContext`, which is the "no context" case those arms fall back from --
///
///   * the `ModControllableAudio*`-aware LPF/HPF-morph name and the stutter-quantization bucket
///     text (`isParamQuantizedStutter` has no context-free overload at all); and
///   * the MIDI-clip CC/expression-axis naming (`isClipContext() && getCurrentOutputType() ==
///     OutputType::MIDI_OUT`).
///
/// They are no longer *unreachable*, which is what this note said when E4 task 1 wrote it: task 2
/// widened `show()` with `overlay::PopupContext` precisely so the class that has the context --
/// `services::ModKnobs`, the popup's first real caller -- can fire them. The arm that is now
/// covered is covered where that context lives, in `sim/scenes_spec/mod_knobs_spec.cpp`'s
/// "gives the popup the model context legacy reads off View" example, which drives the LPF-morph
/// rename against the real `View::displayModEncoderValuePopup` on a real `Sound`. What stays
/// uncovered is `PopupContext::stuttering` (nothing in the port owns the stutter gesture, so no
/// caller can set it true) and the MIDI-clip naming arm (reachable, but not yet driven: a MIDI
/// clip's mod knobs resolve through `MIDIInstrument::getParamFromModEncoder`, whose default CC
/// assignment on a bare fixture takes the non-existent-param arm before any popup is shown).
///
/// No `SongFixture` (unlike every other file in this directory): every code path this spec
/// reaches is one `View::activeModControllableModelStack` never touches for it -- its
/// `modControllable` is null by construction (`ModelStackWithThreeMainThings
/// activeModControllableModelStack{}`, `view.h:120`), which is exactly what makes
/// `isClipContext()` false and every `ModControllableAudio*`-taking call fall back -- so nothing
/// here reads `currentSong`.

#include "scenes/overlay/param_popup_scene.h"
#include "scenes/registry.h"

#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/views/view.h"
#include "hid/display/oled.h"
#include "modulation/params/param.h"
#include "processing/engines/audio_engine.h"

#include "spec/gfx/support/golden.h"
#include "spec/gfx/support/pbm.h"
#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::overlay::kParamPopupSceneKind;
using deluge::scenes::overlay::param_popup_scene;
using deluge::scenes::overlay::ParamPopupScene;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::deluge_profile;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
using deluge::ui::shell::SurfaceRole;

namespace {

/// `Action` unqualified is the model's own undo `Action`, reachable transitively through
/// `gui/views/view.h`'s `model/model_stack.h`; the shell's is a different type with the same name
/// (`context_menu_scene_spec.cpp`/`rename_scene_spec.cpp` make the same alias for the same reason).
using ShellAction = deluge::ui::shell::Action;

using Kind = deluge::modulation::params::Kind;
// `Local`/`UnpatchedGlobal` are legacy's own unscoped enums (`modulation/params/param.h`), so
// their enumerators (`LOCAL_VOLUME`, `UNPATCHED_VOLUME`, ...) are already unqualified names inside
// `deluge::modulation::params` -- this just brings them into scope here too.
using namespace deluge::modulation::params;

/// @brief A 128x64 single-panel board, so the same scene can be recorded at the other shipped
///        panel geometry.
constexpr DelugeDisplayDesc kDisplays64[] = {{128, 64, 0}};

DelugeBoard board_64() {
	DelugeBoard b = deluge::ui::shell::kDelugeBoard;
	b.display_height = 64;
	b.display_count = 1;
	b.displays = kDisplays64;
	return b;
}

const BoardProfile& profile_64() {
	static const BoardProfile p = [] {
		static constexpr const deluge::ui::Style* kStyles[] = {&deluge::ui::kDelugeStyle64};
		BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

/// @brief The minimum `Display` `View::displayModEncoderValuePopup` needs to run its arbitration
///        without touching pixels -- `display` is null in a spec process
///        (`hid/display/display.cpp:4`).
///
/// Captures exactly what `displayNotification`/`hasPopupOfType` are for: what legacy decided to
/// show, and whether it currently considers a notification up. This is the oracle for the
/// *arbitration narrative* below; the *pixel* comparison further down drives a real
/// `deluge::hid::display::OLED` instead, so the rendered strip is compared too.
struct SpyDisplay final : deluge::hid::Display {
	std::string last_title;
	std::string last_value;
	int calls = 0;
	bool showing = false;

	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const*, int8_t, bool, uint8_t, int32_t, PopupType) override {}
	void popupText(char const*, PopupType) override {}
	void popupTextTemporary(char const*, PopupType) override {}
	void cancelPopup() override {}
	void freezeWithError(char const*) override {}
	void displayError(Error) override {}
	void removeWorkingAnimation() override {}
	void displayLoadingAnimationText(char const*, bool, bool) override {}
	void removeLoadingAnimation() override {}
	bool hasPopup() override { return showing; }
	bool hasPopupOfType(PopupType type) override { return showing && type == PopupType::NOTIFICATION; }
	void consoleText(char const*) override {}
	void timerRoutine() override {}

	void displayNotification(std::string_view title, std::optional<std::string_view> value) override {
		last_title = title;
		last_value = value.has_value() ? std::string{*value} : std::string{};
		showing = true;
		calls++;
	}
};

/// @brief A `Shell` over `FakeSurfaceSet` with the application's own scene registry installed,
///        holding nothing but `ParamPopupScene` -- there is no base scene beneath it in this spec,
///        exactly as `deluge::hid::display::OLED::popup` (the legacy oracle's canvas) has no base
///        image composited under it either (`sendMainImage` is never called here): both sides are
///        blank outside the strip the popup actually draws, which is what makes a *whole-panel*
///        comparison meaningful without a base scene to also port.
struct Rig {
	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// @brief Tear the stack down, so `ParamPopupScene::on_exit` always runs (E3 task 10's
	///        cross-contamination lesson: this scene is a process singleton, like every other
	///        scene in this tree).
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p) {}

	void open() { shell.init(SceneId{kParamPopupSceneKind, 0}); }
	void tick(uint32_t at) { shell.tick(at); }
};

/// @brief The 128x48 canvas `OLED::displayNotification` would have shown, as a PBM.
///
/// Not a transcription: `deluge::hid::display::OLED::displayNotification` is the shipped
/// override, called here exactly as `View::displayModEncoderValuePopup` calls it (through
/// `display->displayNotification`), reading the shipped popup canvas back afterward.
///
/// Always 48 rows, never 64: `deluge::hid::display::OLED::popup` is a
/// `gfx::oled::OwnedCanvas<kDelugeOled>`, and `kDelugeOled` (`gfx/oled/geometry.h:21`) is a fixed
/// `{128, 48, 5}` -- the real board's own panel. Legacy has no 64-row mode to compare against; a
/// wider read past its 6 pages is simply past the buffer.
/// @return The popup canvas as a PBM, 48 rows tall.
std::string legacy_popup_pbm() {
	return page_major_to_pbm(deluge::hid::display::OLED::popup.bytes().data(), 128, 48);
}

/// @brief Set the *real* legacy clock `View::displayModEncoderValuePopup` reads.
///
/// Legacy takes no `now_ms` parameter at all -- it reads `AudioEngine::audioSampleTimer` directly
/// (`view.cpp:1103`), so this is the only way to drive its arbitration deterministically in a spec
/// process where nothing else advances that clock. `kSampleRate` (44100) converts the same
/// millisecond timeline `ParamPopupScene::show()` takes explicitly.
/// @param ms The port-side millisecond timeline this spec is otherwise using.
void set_legacy_time_ms(uint64_t ms) {
	AudioEngine::audioSampleTimer = static_cast<uint32_t>(ms * static_cast<uint64_t>(kSampleRate) / 1000);
}

/// @brief @p count rows of @p pbm starting at row @p first, as a PBM of its own.
///
/// This popup is pinned to the *visible* area's top (frame-local row 0, exactly as
/// `title()`/`title_badge()` are -- see `ui/oled/popup.h`'s `notification()` doc), not to a fixed
/// canvas row: on the shipped 48-row profile that lands on canvas row `visible_top` (5), but the
/// 64-row profile this spec also records at has `visible_top == 0` (`board_64()`, matching
/// `marker_scene_spec.cpp`'s/`context_menu_scene_spec.cpp`'s own), so the same content legitimately
/// sits at canvas row 0 there. Comparing the two profiles' raw canvases directly would fail on
/// that offset alone; this extracts each side's own visible-area-relative rows instead, so what is
/// actually being compared is "the same content", not "the same absolute canvas address".
/// @param pbm   A P1 PBM, `page_major_to_pbm`'s own format, at least `first + count` rows tall.
/// @param width The PBM's width (echoed into the new header).
/// @param first The first row to keep (that profile's own `visible_top`).
/// @param count How many rows to keep from there.
/// @return The extracted pixels, `count` rows tall.
std::string rows(const std::string& pbm, int width, int first, int count) {
	std::string out = "P1\n" + std::to_string(width) + " " + std::to_string(count) + "\n";
	size_t at = pbm.find('\n');
	at = pbm.find('\n', at + 1) + 1;
	for (int i = 0; i < first; i++) {
		at = pbm.find('\n', at) + 1;
	}
	for (int i = 0; i < count; i++) {
		const size_t nl = pbm.find('\n', at);
		out += pbm.substr(at, nl - at + 1);
		at = nl + 1;
	}
	return out;
}

} // namespace

// clang-format off
describe param_popup_spec_body("scenes::overlay::ParamPopupScene", $ {

	it("draws exactly what the real legacy OLED::displayNotification draws, at both panel geometries", _ {
		// Fresh legacy state: the first-ever call to `displayModEncoderValuePopup` in this process,
		// so its function-local arbitration statics start from their initial values and every
		// timing threshold is trivially satisfied (`!hasPopupOfType(NOTIFICATION)` alone grants
		// ownership).
		deluge::hid::display::OLED oled_display;
		deluge::hid::display::OLED::popup.clear();
		display = &oled_display;
		set_legacy_time_ms(0);

		view.displayModEncoderValuePopup(Kind::UNPATCHED_GLOBAL, UNPATCHED_VOLUME, 0, PatchSource::NONE,
		                                 PatchSource::NONE);
		expect(display->hasPopupOfType(PopupType::NOTIFICATION)).to_be_true();
		const std::string legacy_pbm = legacy_popup_pbm();

		{
			Rig r;
			r.open();
			param_popup_scene().show(Kind::UNPATCHED_GLOBAL, UNPATCHED_VOLUME, 0, PatchSource::NONE,
			                         PatchSource::NONE, 0);
			r.tick(0);
			// Both at `visible_top == 5` (the default profile matches legacy's own), so the raw
			// canvases already agree row for row.
			expect(r.fake.pbm(0)).to_equal(legacy_pbm);
		}
		{
			Rig r64{board_64(), profile_64()};
			r64.open();
			// A fresh `show()` on the *same* singleton: enough time has passed (both arbitration
			// thresholds are under 1050ms) that the ownership handoff succeeds unconditionally,
			// matching legacy's own untimed first call above -- same title/value, since nothing
			// about this popup's own strip depends on panel height beyond its width (128 on both
			// shipped profiles).
			param_popup_scene().show(Kind::UNPATCHED_GLOBAL, UNPATCHED_VOLUME, 0, PatchSource::NONE,
			                         PatchSource::NONE, 1'000'000);
			r64.tick(1'000'000);
			// `board_64()` sets `visible_top == 0` (no hidden bezel on this hypothetical wider
			// panel, matching `marker_scene_spec.cpp`'s/`context_menu_scene_spec.cpp`'s own), so the
			// same content legitimately lands 5 rows higher on the raw canvas than legacy's (whose
			// `visible_top` is 5) -- `rows()` compares each side's own visible-area-relative rows
			// instead of the raw canvas address. Legacy also has no 64-row mode
			// (`legacy_popup_pbm`'s own note), so 16 rows -- comfortably past the strip's own
			// 11 -- is as far as either side has an opinion.
			expect(rows(r64.fake.pbm(0), 128, 0, 16)).to_equal(rows(legacy_pbm, 128, 5, 16));
		}
	});

	it("ports the patch-cable name form, source1->source2->name, against the real getPatchedParamShortName", _ {
		deluge::hid::display::OLED oled_display;
		deluge::hid::display::OLED::popup.clear();
		display = &oled_display;
		set_legacy_time_ms(2'000'000); // Far past both prior tests' thresholds either way.

		view.displayModEncoderValuePopup(Kind::PATCH_CABLE, LOCAL_VOLUME, 64, PatchSource::LFO_GLOBAL_1,
		                                 PatchSource::ENVELOPE_0);
		const std::string legacy_pbm = legacy_popup_pbm();

		Rig r;
		r.open();
		param_popup_scene().show(Kind::PATCH_CABLE, LOCAL_VOLUME, 64, PatchSource::LFO_GLOBAL_1,
		                         PatchSource::ENVELOPE_0, 2'000'000);
		r.tick(2'000'000);
		expect(r.fake.pbm(0)).to_equal(legacy_pbm);
	});

	it("ports the arbitration in View::displayModEncoderValuePopup: immediate display, refresh, "
	   "ownership hold-off, and throttled/pending flush", _ {
		// A base far past every prior example's own legacy clock reading, so the ownership-timeout
		// arithmetic below is never accidentally satisfied by leftover state.
		constexpr uint64_t kBase = 4'000'000;

		SpyDisplay spy;
		display = &spy;
		ParamPopupScene& scene = param_popup_scene();

		// --- t=0: nothing owns the display yet -- immediate arm. ---------------------------------
		set_legacy_time_ms(kBase + 0);
		view.displayModEncoderValuePopup(Kind::UNPATCHED_GLOBAL, UNPATCHED_VOLUME, 0, PatchSource::NONE,
		                                 PatchSource::NONE);
		scene.show(Kind::UNPATCHED_GLOBAL, UNPATCHED_VOLUME, 0, PatchSource::NONE, PatchSource::NONE, 0);
		expect(spy.calls).to_equal(1);
		expect(std::string{scene.shown_name()}).to_equal(spy.last_title);
		expect(std::string{scene.shown_value()}).to_equal(spy.last_value);
		expect(scene.remaining_ms()).to_equal(2000u);

		// --- t=+20: same param, same value -- refresh only, no redraw. --------------------------
		scene.tick(20);
		set_legacy_time_ms(kBase + 20);
		view.displayModEncoderValuePopup(Kind::UNPATCHED_GLOBAL, UNPATCHED_VOLUME, 0, PatchSource::NONE,
		                                 PatchSource::NONE);
		scene.show(Kind::UNPATCHED_GLOBAL, UNPATCHED_VOLUME, 0, PatchSource::NONE, PatchSource::NONE, 20);
		expect(spy.calls).to_equal(1); // unchanged: legacy did not redraw either.
		expect(scene.remaining_ms()).to_equal(2000u); // re-armed, not merely "still counting down".

		// --- t=+120: a different param, too soon (well under both ownership thresholds -- 1000ms
		//     since ownership began, 250ms since the last update) -- rejected. --------------------
		scene.tick(100);
		set_legacy_time_ms(kBase + 120);
		view.displayModEncoderValuePopup(Kind::UNPATCHED_GLOBAL, UNPATCHED_PAN, 64, PatchSource::NONE,
		                                 PatchSource::NONE);
		scene.show(Kind::UNPATCHED_GLOBAL, UNPATCHED_PAN, 64, PatchSource::NONE, PatchSource::NONE, 120);
		expect(spy.calls).to_equal(1); // legacy's arbitration refused the switch too.
		expect(scene.remaining_ms()).to_equal(1900u); // only ticked down, never refreshed.

		// --- t=+1100: past kMinDisplayOwnershipMs (1000ms since the first param took ownership at
		//     t=0) -- the switch now succeeds. ----------------------------------------------------
		scene.tick(980);
		set_legacy_time_ms(kBase + 1100);
		view.displayModEncoderValuePopup(Kind::UNPATCHED_GLOBAL, UNPATCHED_PAN, 64, PatchSource::NONE,
		                                 PatchSource::NONE);
		scene.show(Kind::UNPATCHED_GLOBAL, UNPATCHED_PAN, 64, PatchSource::NONE, PatchSource::NONE, 1100);
		expect(spy.calls).to_equal(2);
		expect(std::string{scene.shown_name()}).to_equal(spy.last_title);
		expect(std::string{scene.shown_value()}).to_equal(spy.last_value);
		expect(scene.remaining_ms()).to_equal(2000u);
		const std::string pan_first_value = std::string{scene.shown_value()};

		// --- t=+1120: same (new) owner, a genuinely different value, comfortably inside the ~45ms
		//     throttle window (legacy's own `kSampleRate/22` is ~45.44ms; the port's is a flat
		//     45ms -- 20ms elapsed clears neither) -- deferred, not drawn yet. --------------------
		scene.tick(20);
		set_legacy_time_ms(kBase + 1120);
		view.displayModEncoderValuePopup(Kind::UNPATCHED_GLOBAL, UNPATCHED_PAN, -64, PatchSource::NONE,
		                                 PatchSource::NONE);
		scene.show(Kind::UNPATCHED_GLOBAL, UNPATCHED_PAN, -64, PatchSource::NONE, PatchSource::NONE, 1120);
		expect(spy.calls).to_equal(2); // legacy queued it (`hasPendingModEncoderValuePopup`), not drawn.
		expect(scene.has_pending()).to_be_true();
		expect(std::string{scene.shown_value()}).to_equal(pan_first_value); // unchanged so far.

		// --- t=+1200: comfortably past the throttle window (80ms since the last actual display at
		//     +1100, past both 45ms and 45.44ms) -- the flush fires (port: inside `tick()`; legacy:
		//     `flushPendingModEncoderValuePopup`, replayed here explicitly since nothing drives
		//     legacy's `TimerName::MOD_ENCODER_POPUP_FLUSH` in this harness). -----------------------
		set_legacy_time_ms(kBase + 1200);
		view.flushPendingModEncoderValuePopup();
		scene.tick(80);
		expect(spy.calls).to_equal(3);
		expect(scene.has_pending()).to_be_false();
		expect(std::string{scene.shown_name()}).to_equal(spy.last_title);
		expect(std::string{scene.shown_value()}).to_equal(spy.last_value);
		expect(std::string{scene.shown_value()}).not_().to_equal(pan_first_value);
		expect(scene.remaining_ms()).to_equal(2000u);
	});

	it("expires on its own after a quiet tick and removes itself with Action::Kind::Remove, never Pop", _ {
		ParamPopupScene& scene = param_popup_scene();
		scene.show(Kind::UNPATCHED_GLOBAL, UNPATCHED_TEMPO, 0, PatchSource::NONE, PatchSource::NONE, 5'000'000);
		expect(scene.remaining_ms()).to_equal(2000u);
		expect(scene.next_timer().has_value()).to_be_true();
		expect(*scene.next_timer()).to_equal(2000u);

		// A quiet tick that does not reach the deadline: nothing to remove yet.
		scene.tick(1000);
		expect(scene.remaining_ms()).to_equal(1000u);
		Result mid = scene.after_render();
		expect(mid.actions.size()).to_equal(0u);

		// The deadline itself: `remaining_ms()` reaches zero and `after_render()` fires exactly
		// once, with `Remove` -- never `Pop`, which would also take down anything legitimately
		// stacked above a passive overlay like this one (`shell/scene.h`'s warning on `Action`).
		scene.tick(1000);
		expect(scene.remaining_ms()).to_equal(0u);
		Result r = scene.after_render();
		expect(r.actions.size()).to_equal(1u);
		expect(r.actions[0].kind).to_equal(ShellAction::Kind::Remove);
		expect(r.actions[0].pop_to).to_equal(kParamPopupSceneKind);
		for (const ShellAction& a : r.actions) {
			expect(a.kind).not_().to_equal(ShellAction::Kind::Pop);
		}

		// Fires only once: the same tick's `after_render()` called again reports nothing more.
		Result again = scene.after_render();
		expect(again.actions.size()).to_equal(0u);
	});

	it("removes only itself from a real stack, leaving what is above it exactly where it was", _ {
		Rig r;
		r.open();
		param_popup_scene().show(Kind::UNPATCHED_GLOBAL, UNPATCHED_DELAY_RATE, 0, PatchSource::NONE,
		                         PatchSource::NONE, 9'000'000);
		r.tick(9'000'000);
		expect(r.shell.stack().depth()).to_equal(1u);

		// Something the popup did not push stacks above it -- the menu interpreter, entered
		// directly on the stack rather than through the router, so this is not exercising
		// `MenuScene` itself, only that `ParamPopupScene` tolerates a scene above it.
		expect(r.shell.stack().push(SceneId{SceneKind::FirstApp, 0})).to_be_true();
		expect(r.shell.stack().depth()).to_equal(2u);

		// Quiet-tick the popup past its deadline; the menu scene on top must survive.
		r.tick(9'000'000 + 2000);
		expect(r.shell.stack().depth()).to_equal(1u);
		expect(r.shell.stack().id_at(0).kind).to_equal(SceneKind::FirstApp);
	});
});
// clang-format on

DELUGE_SPEC(param_popup_spec, param_popup_spec_body)
