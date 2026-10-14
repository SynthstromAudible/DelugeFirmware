/// @file
/// @brief `CountInScene` -- driven directly by mutating `playbackHandler.currentVisualCountForCountIn`
///        (its own sentinel, public: `playback_handler.h:169`) and quiet-ticking the scene's own
///        poll, exactly the way a real count-in tick would leave it (`playback_handler.cpp:1051`).
///        Compared against the real legacy it ports -- `gui/ui/notify_drain.cpp`'s count-in block
///        (`:92-113`) -- reached end to end through a real `deluge::hid::display::OLED` and a real
///        `Shell`, so the comparison is the assembled scene's own pixels, not the widget in
///        isolation.
///
/// @par The oracle, and its one genuine gap
///
/// Both sides read the same sentinel: `playbackHandler.currentVisualCountForCountIn`, set directly
/// here exactly as a real count-in tick would leave it. Legacy is driven through
/// `display->displayPopup(buffer, 0, true, 255, 2)` -- `notify_drain.cpp:100`'s own call, with
/// `buffer` formatted from the same sentinel via `intToString` (`:98-99`) -- and the port through
/// its own `tick()`, which reads `playbackHandler.count_in_visual()` itself; neither side is
/// computed from the other's state.
///
/// The one gap: `notify_drain.cpp:104-111`'s masked `UI_MODE_RECORD_COUNT_IN` clear has no port to
/// compare against at all -- `currentUIMode` lives in `gui/ui/ui.h`, which `src/scenes` may never
/// include (Global Constraint 3; see `count_in_scene.h`'s class doc). Counted here as uncovered,
/// not silently assumed.
///
/// @par Both panel geometries
///
/// The 48-row profile is compared whole-panel against the real `OLED` singleton, which is the only
/// live oracle there is (`OLED_MAIN_HEIGHT_PIXELS` is a compile-time 48; there is no 64-row legacy
/// singleton to ask). The widget this scene calls, `ui::oled::popup_text`, is already proven byte
/// for byte against a *height-parameterised* transcription of `OLED::popupText` at both 48 and 64
/// rows in `tests/spec/ui/popup_spec.cpp`'s "matches popupText byte for byte, at both geometries"
/// example -- so the 64-row example here does not re-derive that; it instead confirms this scene
/// invokes that already-proven widget consistently across geometries, by checking the box's own
/// interior is byte-identical at its own (geometry-correct) vertical offset in each profile. Box
/// height for a single-line digit is `9 + kPopupMargin(12) = 21` regardless of panel height (only
/// the *centring*, not the content, depends on panel height), so the canvas-absolute top is
/// `(panel_h - 21) / 2`: row 13 at 48 rows, row 21 at 64 rows -- both computed the same way
/// `popup_rect_for`'s own doc derives them.
///
/// @par Why every render is quiet-ticked at `kCountInPollMs`, not driven by a `show()` call
///
/// `CountInScene` has no caller in the input pipeline (see the class doc) -- its `tick()` polls
/// `playbackHandler.count_in_visual()` itself, and `next_timer()` is what makes the shell call
/// `tick()` (and, when the poll finds a change, re-render) with no input at all. An early version
/// of this scene and this spec drove a `show(int32_t)` parameter directly, the way
/// `ParamPopupScene::show()` is driven; that version's own spec caught the mistake, because
/// `Shell::tick`'s OLED step only renders a surface whose gate reports `needs_frame()`, and calling
/// `show()` from outside the input pipeline, with no repaint request of any kind, left that gate
/// unarmed after the first render -- every subsequent call rendered the *previous* digit's stale
/// pixels. Every example below therefore advances the mock clock by at least `kCountInPollMs`
/// between a sentinel change and the render it expects to see, exactly as
/// `recorder_scene_spec.cpp`'s own quiet-tick examples advance by `kRecorderPollMs`.

#include "scenes/overlay/count_in_scene.h"
#include "scenes/registry.h"

#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "hid/display/oled.h"
#include "playback/playback_handler.h"
#include "util/c_string.h"

#include "spec/gfx/support/golden.h"
#include "spec/gfx/support/pbm.h"
#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <cstdint>
#include <string>
#include <string_view>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::overlay::count_in_scene;
using deluge::scenes::overlay::kCountInPollMs;
using deluge::scenes::overlay::kCountInSceneKind;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::deluge_profile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

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

/// @brief A `Shell` over `FakeSurfaceSet` with the application's own scene registry installed,
///        holding nothing but `CountInScene` -- there is no base scene beneath it in this spec,
///        exactly as `deluge::hid::display::OLED::popup` (the legacy oracle's canvas) has no base
///        image composited under it either.
struct Rig {
	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// @brief Tear the stack down, so `CountInScene::on_exit` always runs (E3 task 10's
	///        cross-contamination lesson: this scene is a process singleton, like every other
	///        scene in this tree).
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p) {}

	void open() { shell.init(SceneId{kCountInSceneKind, 0}); }
	void tick(uint32_t at) { shell.tick(at); }
};

/// @brief The 128x48 canvas `OLED::displayPopup`/`cancelPopup` would have left, as a PBM.
///
/// Always 48 rows: `deluge::hid::display::OLED::popup` is a fixed `kDelugeOled` canvas
/// (`gfx/oled/geometry.h:21`, `{128, 48, 5}`) -- the real board's own panel. Legacy has no 64-row
/// mode to compare against.
std::string legacy_popup_pbm() {
	return page_major_to_pbm(deluge::hid::display::OLED::popup.bytes().data(), 128, 48);
}

/// @brief Drive the real legacy call site, `notify_drain.cpp:97-100`, from @p count.
/// @param count The sentinel value (mirrors `playbackHandler.count_in_visual()`).
void legacy_show_count_in(int32_t count) {
	char buffer[12];
	intToString(count, buffer);
	display->displayPopup(buffer, 0, true, 255, 2);
}

/// @brief @p count rows of @p pbm starting at row @p first, as a PBM of its own.
/// @param pbm   A P1 PBM, `page_major_to_pbm`'s own format, at least `first + count` rows tall.
/// @param width The PBM's width (echoed into the new header).
/// @param first The first row to keep.
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
describe count_in_spec_body("scenes::overlay::CountInScene", $ {

	it("draws exactly what the real legacy displayPopup call draws, for counts 4, 3, 2, 1 and the "
	   "clear at 0", _ {
		deluge::hid::display::OLED oled_display;
		deluge::hid::display::OLED::popup.clear();
		display = &oled_display;

		Rig r;
		r.open();
		uint32_t now = 0;

		for (int32_t count : {4, 3, 2, 1}) {
			// Each side reads its own sentinel: `playbackHandler`'s real field, which legacy's
			// formatting and the port's `tick()` poll both read independently -- neither computed
			// from the other.
			playbackHandler.currentVisualCountForCountIn = count;
			legacy_show_count_in(playbackHandler.count_in_visual());
			now += kCountInPollMs;
			r.tick(now); // quiet-ticked: no pad, no button, no encoder -- only the poll.

			expect(count_in_scene().showing()).to_be_true();
			expect(r.fake.pbm(0)).to_equal(legacy_popup_pbm());
		}

		// The clear at 0: `notify_drain.cpp:102-103`.
		playbackHandler.currentVisualCountForCountIn = 0;
		display->cancelPopup();
		now += kCountInPollMs;
		r.tick(now);
		expect(count_in_scene().showing()).to_be_false();
		expect(display->hasPopup()).to_be_false();
	});

	it("centres the same box content at both panel geometries, at the geometry-correct offset", _ {
		// The widget itself (`ui::oled::popup_text`) is already proven byte for byte against a
		// height-parameterised legacy transcription at both 48 and 64 rows
		// (`tests/spec/ui/popup_spec.cpp`), so this confirms the *scene* invokes it consistently,
		// not the widget's own arithmetic again.
		Rig r48;
		r48.open();
		playbackHandler.currentVisualCountForCountIn = 4;
		r48.tick(kCountInPollMs);

		Rig r64{board_64(), profile_64()};
		r64.open();
		// A fresh scene singleton read on a fresh Rig: `playbackHandler`'s sentinel is already 4
		// (left there by the example above), which differs from `count_in_scene()`'s own
		// `shown_count_` -- still 0 at process start, and `Rig`'s destructor does not reset it --
		// so the very next poll redraws it, exactly the edge-detector `notify_drain.cpp:96` has.
		r64.tick(2 * kCountInPollMs);

		// A single-line digit's box is `{9 + kPopupMargin} = 21` rows regardless of panel height;
		// only its centring changes: `(48 - 21) / 2 = 13`, `(64 - 21) / 2 = 21`.
		expect(rows(r48.fake.pbm(0), 128, 13, 22)).to_equal(rows(r64.fake.pbm(0), 128, 21, 22));

		// And the two canvases really do differ outside that band -- this is not a trivially
		// blank match.
		expect(r48.fake.pbm(0) == r64.fake.pbm(0)).to_be_false();

		// Leave the singleton quiescent for later examples.
		playbackHandler.currentVisualCountForCountIn = 0;
		r64.tick(3 * kCountInPollMs);
	});

	it("expires only when the model says so, holds steady across quiet ticks, and removes itself "
	   "with Action::Kind::Remove, never Pop", _ {
		Rig r;
		r.open();
		uint32_t now = 0;

		playbackHandler.currentVisualCountForCountIn = 4;
		now += kCountInPollMs;
		r.tick(now);
		expect(count_in_scene().showing()).to_be_true();
		expect(std::string{count_in_scene().shown_text()}).to_equal(std::string{"4"});

		// Quiet ticks -- the sentinel unchanged, no input at all -- must not change anything: there
		// is no blink or lifetime deadline here (see the class doc's finding that the brief's
		// "blink speed 2" is a dead legacy parameter), so the popup simply holds steady.
		for (int i = 0; i < 4; i++) {
			now += kCountInPollMs;
			r.tick(now);
			expect(count_in_scene().showing()).to_be_true();
			expect(std::string{count_in_scene().shown_text()}).to_equal(std::string{"4"});
		}

		// The cancel: `after_render()` fires exactly once, with `Remove` -- never `Pop`. The scene
		// itself pushed nothing above it, so a bare stack of depth 1 is enough to observe the
		// removal by depth alone.
		playbackHandler.currentVisualCountForCountIn = 0;
		now += kCountInPollMs;
		r.tick(now);
		expect(count_in_scene().showing()).to_be_false();
		expect(r.shell.stack().depth()).to_equal(0u);
	});

	it("removes only itself from a real stack, leaving what is above it exactly where it was", _ {
		Rig r;
		r.open();
		uint32_t now = 0;

		playbackHandler.currentVisualCountForCountIn = 4;
		now += kCountInPollMs;
		r.tick(now);
		expect(r.shell.stack().depth()).to_equal(1u);

		// Something the popup did not push stacks above it -- the menu interpreter, entered
		// directly on the stack rather than through the router.
		expect(r.shell.stack().push(SceneId{SceneKind::FirstApp, 0})).to_be_true();
		expect(r.shell.stack().depth()).to_equal(2u);

		playbackHandler.currentVisualCountForCountIn = 0;
		now += kCountInPollMs;
		r.tick(now);
		expect(r.shell.stack().depth()).to_equal(1u);
		expect(r.shell.stack().id_at(0).kind).to_equal(SceneKind::FirstApp);
	});

	it("renders the count-in golden", _ {
		Rig r;
		r.open();
		playbackHandler.currentVisualCountForCountIn = 4;
		r.tick(kCountInPollMs);
		expect_golden(self, "../ui/scenes/count_in_48.pbm", r.fake.pbm(0));

		// Leave the singleton quiescent for later examples.
		playbackHandler.currentVisualCountForCountIn = 0;
		r.tick(2 * kCountInPollMs);
	});
});
// clang-format on

DELUGE_SPEC(count_in_spec, count_in_spec_body)
