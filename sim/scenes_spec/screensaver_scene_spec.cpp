/// @file
/// @brief `ScreensaverScene`, and the two animations E4 task 7 moved out of `hid/display/` into
///        `src/ui/oled/`.
///
/// @par Two oracles, because there are two claims
///
/// **The move did not change the arithmetic.** `sim/scenes_spec/support/oracle_rainfall.{h,cpp}`
/// and `oracle_starfield.{h,cpp}` are the PRE-MOVE `hid/display/` sources, transcribed by a
/// three-substitution `sed` recorded verbatim at the top of each of those files (namespace, include
/// path -- nothing else). They are compiled into this spec under `namespace screensaver_oracle` and
/// run beside `deluge::ui::oled::Rainfall`/`Starfield` frame by frame, from the same seed, for
/// **8000 frames** -- longer than `kLogoIntervalMaxFrames` (7200), so at least one complete logo
/// interval, emission and retirement is inside the window, and this spec asserts that it was. Every
/// drop's length, speed and every cell's `{x, y, size}`, and every logo cell, is compared by
/// identity on every frame.
///
/// **The port draws what legacy draws.** The second oracle is the real, shipped
/// `deluge::hid::display::Screensaver` -- its own `timerEvent()` driving its own canvas through its
/// own `drawBlock`. The scene is run through a real `Shell` in lockstep beside it and the two
/// 128x48 panels are compared **pixel for pixel on every frame**, not at the end.
///
/// @par The lockstep, and the one assumption it rests on
///
/// Both sides hold a `Rainfall`/`Starfield` whose generator seed is a member initialiser
/// (`0x1EAF7A11` / `0xCAFE5678`) and whose constructor calls `scatter()`. So a process-fresh legacy
/// `Screensaver::rainfall_` and a process-fresh `ScreensaverScene::rainfall_` are in identical
/// states, and stay identical as long as every `scatter()`/`advance()` on one is matched on the
/// other -- which is exactly what "the scene ports `timerEvent`" means. **The examples below
/// therefore each require the field they drive to be untouched when they start**, and each asserts
/// `Screensaver::isActive()` is false first. They are safe against each other because the DELUGE
/// examples touch only the two `Rainfall`s and the STARSCAPE ones only the two `Starfield`s; a
/// third example driving the same mode would have to re-establish the lockstep for itself. This is
/// the same process-singleton discipline `count_in_spec.cpp` documents for `count_in_scene()`.
///
/// @par Coverage this spec does NOT have, stated at the site
///
/// Four legacy behaviours have no port and so no comparison here; all four are policy, and all four
/// are `shell::Screensaver`'s half rather than the scene's. They are enumerated with citations in
/// `screensaver_scene.h`'s class doc: `settingsChanged()` (`hid/display/screensaver.cpp:200-241`),
/// the permanent-popup / working-animation / stem-export inhibitors (`:106-118`) and with them the
/// 1000 ms `kInhibitRecheckMS` re-arm that exists only to run them (`:56-58`), the
/// `ScreensaverMode::OFF` early-outs (`:92-94,102-104,171-174`, ported as the service's
/// `idle_ms == 0`), and the `consumeFrameDirty` blit protocol (`screensaver.h:95-109`,
/// `hid/display/oled.cpp:342-362`, whose port is the shell's `RepaintGate`). Counted as uncovered.

#include "scenes/overlay/screensaver_scene.h"
#include "scenes/registry.h"

#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "hid/display/oled.h"
#include "hid/display/screensaver.h"
#include "storage/flash_storage.h"

#include "support/oracle_rainfall.h"
#include "support/oracle_starfield.h"

#include "spec/gfx/support/golden.h"
#include "spec/gfx/support/pbm.h"
#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <cstdint>
#include <string>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::overlay::kFrameIntervalMs;
using deluge::scenes::overlay::kScreensaverSceneKind;
using deluge::scenes::overlay::screensaver_scene;
using deluge::ui::oled::Rainfall;
using deluge::ui::oled::Starfield;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::deluge_profile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

using LegacyScreensaver = deluge::hid::display::Screensaver;

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

/// @brief A `Shell` over `FakeSurfaceSet` with the application's own registry -- which now fills
///        `SceneKind::Screensaver`, the whole point of this task.
struct Rig {
	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell;

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// @brief Tear the stack down, so `on_exit` always runs (E3 task 10's cross-contamination
	///        lesson: every scene in this tree is a process singleton).
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile(),
	             ShellConfig config = ShellConfig{})
	    : profile(p), fake(board, p), shell(fake, p, reg, deluge::ui::shell::Services{}, config) {}

	void open(uint16_t arg = 0) { shell.init(SceneId{kScreensaverSceneKind, arg}); }
	void tick(uint32_t at) { shell.tick(at); }
};

/// @brief The 128x48 canvas legacy's own `Screensaver` last rendered, as a PBM.
///
/// `Screensaver::getCanvas()` is legacy's real drawing surface -- the one `OLED::sendMainImage`
/// substitutes for `main` (`hid/display/oled.cpp:342-348`) -- so this is legacy's own pixels, drawn
/// by legacy's own `render`/`renderRainfall`/`renderStarfield`/`drawBlock`
/// (`hid/display/screensaver.cpp:65-78,243-282`), not a transcription of them.
std::string legacy_canvas_pbm() {
	return page_major_to_pbm(LegacyScreensaver::getCanvas().bytes().data(), 128, 48);
}

/// @brief Is every drop of @p a in exactly the state every drop of @p b is in?
///
/// Compared by identity, cell by cell -- length, speed, and every block's `{x, y, size}` -- with no
/// slack anywhere. Deliberately allocation-free and returning one bool rather than raising an
/// expectation per drop: the loops below run 8000 frames x 34 drops, and one CppSpec expectation
/// object per comparison exhausts the sim's allocation-guard table long before the comparison ends.
/// The caller records the FIRST frame that disagreed, so a failure still names where.
///
/// @warning **What this cannot see, and where that coverage comes from instead.** Both loops here
///          are bounded by the PORT's `kNumDrops` / `kNumStars` / `kLogoCells`, so a divergence in
///          one of those three counts is invisible to the runtime comparison: an oracle with MORE
///          drops than the port passes silently, and one with FEWER aborts on an out-of-range read
///          rather than failing an expectation (which is exactly what the rejected `kNumDrops`
///          34 -> 33 mutation did). The two evidences are complementary, not redundant: those three
///          constants are covered by the *textual* half of the argument -- the reverse-mapped diff
///          of `src/ui/oled/{rainfall,starfield}.{h,cpp}` against
///          `c758fbca5:src/deluge/hid/display/…`, recorded in the task report -- which shows every
///          constant unmodified. Do not read the 8000-frame run as covering them.
bool same_field(const Rainfall& a, const screensaver_oracle::Rainfall& b) {
	for (size_t drop = 0; drop < Rainfall::kNumDrops; drop++) {
		if (a.length_of(drop) != b.lengthOf(drop) || a.speed_of(drop) != b.speedOf(drop)) {
			return false;
		}
		for (size_t cell = 0; cell < a.length_of(drop); cell++) {
			const Rainfall::Block x = a.cell_at(drop, cell);
			const screensaver_oracle::Rainfall::Block y = b.cellAt(drop, cell);
			if (x.x != y.x || x.y != y.y || x.size != y.size) {
				return false;
			}
		}
	}
	if (a.logo_active() != b.logoActive() || a.logo_scale() != b.logoScale() || a.logo_speed() != b.logoSpeed()) {
		return false;
	}
	if (a.logo_active()) {
		for (size_t cell = 0; cell < Rainfall::kLogoCells; cell++) {
			const Rainfall::Block x = a.logo_cell_at(cell);
			const screensaver_oracle::Rainfall::Block y = b.logoCellAt(cell);
			if (x.x != y.x || x.y != y.y || x.size != y.size) {
				return false;
			}
		}
	}
	return true;
}

/// @brief Is every star of @p a projected exactly where every star of @p b is, at the same depth?
bool same_field(const Starfield& a, const screensaver_oracle::Starfield& b) {
	for (size_t i = 0; i < Starfield::kNumStars; i++) {
		const Starfield::Projected x = a.project(i);
		const screensaver_oracle::Starfield::Projected y = b.project(i);
		if (x.x != y.x || x.y != y.y || x.size != y.size || a.depth_of(i) != b.depthOf(i)) {
			return false;
		}
	}
	return true;
}

/// @brief Are two `Rainfall`s in the same state? Used to compare a scene's field against one this
///        spec drove itself from the same seed.
bool same_field(const Rainfall& a, const Rainfall& b) {
	for (size_t drop = 0; drop < Rainfall::kNumDrops; drop++) {
		if (a.length_of(drop) != b.length_of(drop) || a.speed_of(drop) != b.speed_of(drop)) {
			return false;
		}
		for (size_t cell = 0; cell < a.length_of(drop); cell++) {
			const Rainfall::Block x = a.cell_at(drop, cell);
			const Rainfall::Block y = b.cell_at(drop, cell);
			if (x.x != y.x || x.y != y.y || x.size != y.size) {
				return false;
			}
		}
	}
	return a.logo_active() == b.logo_active() && a.logo_scale() == b.logo_scale();
}

/// @brief Does @p pbm have any lit pixel at all?
///
/// The two header lines are skipped deliberately: `"P1\n128 48\n"` contains the character '1'
/// three times, so a bare `pbm.find('1')` answers "lit" for a completely blank panel. (It did, in
/// this spec's first cut -- which made the blank-mode check fail and the "not two blank panels"
/// checks pass for the wrong reason.)
/// @param pbm A P1 PBM, `page_major_to_pbm`'s own format.
/// @return True when at least one pixel row carries a '1'.
bool any_pixel(const std::string& pbm) {
	size_t at = pbm.find('\n');
	at = pbm.find('\n', at + 1) + 1;
	return pbm.find('1', at) != std::string::npos;
}

/// @brief @p count rows of @p pbm starting at row @p first, as a PBM of its own.
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

/// @brief Set the one setting both sides read (`FlashStorage::screensaverMode`,
///        `storage/flash_storage.h:82`) and leave the legacy screensaver deactivated.
void set_mode(::ScreensaverMode mode) {
	LegacyScreensaver::noteActivity(); // `hid/display/screensaver.cpp:85-99`: deactivate if showing.
	FlashStorage::screensaverMode = mode;
}

} // namespace

// clang-format off
describe screensaver_scene_spec_body("scenes::overlay::ScreensaverScene", $ {

	// ---- The move -----------------------------------------------------------------------------

	it("advances identically to the pre-move Rainfall for 8000 frames -- past a whole logo "
	   "interval -- from the same seed", _ {
		// Both sides are constructed from their own class's own seed member initialiser and their
		// own constructor's `scatter()`; neither is computed from the other.
		Rainfall moved;
		screensaver_oracle::Rainfall oracle;

		int32_t first_mismatch = -1;
		int32_t compared = 0;
		int32_t logo_frames = 0;
		int32_t logo_emissions = 0;
		bool was_up = false;

		// 8000 > kLogoIntervalMaxFrames (7200), so a full countdown, emission and retirement is
		// inside the window whatever interval the generator picks.
		expect(Rainfall::kLogoIntervalMaxFrames).to_be_less_than(8000);
		for (int32_t frame = 0; frame < 8000; frame++) {
			if (!same_field(moved, oracle) && first_mismatch < 0) {
				first_mismatch = frame;
			}
			compared++;

			if (moved.logo_active()) {
				logo_frames++;
				if (!was_up) {
					logo_emissions++;
				}
			}
			was_up = moved.logo_active();

			moved.advance();
			oracle.advance();
		}

		// -1 is "no frame disagreed"; any other value is the first frame that did.
		expect(first_mismatch).to_equal(-1);
		expect(compared).to_equal(8000);
		// The logo path is not merely available, it was exercised: at least one whole emission
		// came up and went again inside the window compared above.
		expect(logo_emissions).to_be_greater_than(0);
		expect(logo_frames).to_be_greater_than(0);
	});

	it("re-scatters identically to the pre-move Rainfall, and keeps agreeing afterwards", _ {
		// `scatter()` does not reseed (its own note), so this also proves the generator streams are
		// still in step after a re-activation -- the thing legacy's `timerEvent` does on every push.
		Rainfall moved;
		screensaver_oracle::Rainfall oracle;
		int32_t first_mismatch = -1;
		int32_t compared = 0;
		for (int round = 0; round < 3; round++) {
			moved.scatter();
			oracle.scatter();
			for (int32_t frame = 0; frame < 200; frame++) {
				if (!same_field(moved, oracle) && first_mismatch < 0) {
					first_mismatch = round * 1000 + frame;
				}
				compared++;
				moved.advance();
				oracle.advance();
			}
		}
		expect(first_mismatch).to_equal(-1);
		expect(compared).to_equal(600);
	});

	it("emits a forced logo identically to the pre-move Rainfall, every frame of its fall", _ {
		Rainfall moved;
		screensaver_oracle::Rainfall oracle;
		moved.force_logo();
		oracle.forceLogo();

		int32_t first_mismatch = -1;
		int32_t logo_frames = 0;
		for (int32_t frame = 0; frame < 400; frame++) {
			moved.advance();
			oracle.advance();
			if (!same_field(moved, oracle) && first_mismatch < 0) {
				first_mismatch = frame;
			}
			logo_frames += moved.logo_active() ? 1 : 0;
		}
		expect(first_mismatch).to_equal(-1);
		// A logo really did fall through the window, so the comparison above was not 400 frames of
		// "neither has one".
		expect(logo_frames).to_be_greater_than(0);
	});

	it("advances identically to the pre-move Starfield for 8000 frames, from the same seed", _ {
		Starfield moved;
		screensaver_oracle::Starfield oracle;
		int32_t first_mismatch = -1;
		int32_t compared = 0;
		for (int32_t frame = 0; frame < 8000; frame++) {
			if (!same_field(moved, oracle) && first_mismatch < 0) {
				first_mismatch = frame;
			}
			compared++;
			moved.advance();
			oracle.advance();
		}
		// And across a re-scatter, which is what an activation does.
		for (int round = 0; round < 3; round++) {
			moved.scatter();
			oracle.scatter();
			for (int32_t frame = 0; frame < 200; frame++) {
				if (!same_field(moved, oracle) && first_mismatch < 0) {
					first_mismatch = 100000 + round * 1000 + frame;
				}
				compared++;
				moved.advance();
				oracle.advance();
			}
		}
		expect(first_mismatch).to_equal(-1);
		expect(compared).to_equal(8600);
	});

	// ---- The scene, against the real legacy Screensaver ----------------------------------------

	it("draws pixel for pixel what the real legacy Screensaver draws, every frame, in DELUGE mode", _ {
		// The lockstep's precondition: legacy's `rainfall_` and the scene's are both process-fresh.
		// See this file's header for why that is what makes the two comparable.
		expect(LegacyScreensaver::isActive()).to_be_false();
		set_mode(::ScreensaverMode::DELUGE);

		Rig r;
		r.open();                       // `on_enter` -> `rainfall_.scatter()`
		LegacyScreensaver::timerEvent(); // `!active_` -> `rainfall_.scatter()` + `render()`
		expect(LegacyScreensaver::isActive()).to_be_true();

		uint32_t now = 0;
		r.tick(now); // the activation frame
		expect(r.fake.pbm(0)).to_equal(legacy_canvas_pbm());

		// Every frame, not just the last one: a per-frame comparison is what catches a port that
		// drifts one advance ahead or behind, which an end-state comparison cannot see.
		for (int32_t frame = 0; frame < 300; frame++) {
			LegacyScreensaver::timerEvent(); // one `advance()` + `render()`
			now += kFrameIntervalMs;
			r.tick(now); // quiet-ticked: no pad, no button, no encoder
			expect(r.fake.pbm(0)).to_equal(legacy_canvas_pbm());
		}

		// Not a comparison of two blank panels: the field really is drawing.
		expect(any_pixel(r.fake.pbm(0))).to_be_true();
		LegacyScreensaver::noteActivity();
	});

	it("draws pixel for pixel what the real legacy Screensaver draws, every frame, in STARSCAPE "
	   "mode", _ {
		expect(LegacyScreensaver::isActive()).to_be_false();
		set_mode(::ScreensaverMode::STARSCAPE);

		Rig r;
		r.open();
		LegacyScreensaver::timerEvent();

		uint32_t now = 0;
		r.tick(now);
		expect(r.fake.pbm(0)).to_equal(legacy_canvas_pbm());

		for (int32_t frame = 0; frame < 300; frame++) {
			LegacyScreensaver::timerEvent();
			now += kFrameIntervalMs;
			r.tick(now);
			expect(r.fake.pbm(0)).to_equal(legacy_canvas_pbm());
		}
		expect(any_pixel(r.fake.pbm(0))).to_be_true();
		LegacyScreensaver::noteActivity();
	});

	// ---- Cadence, modes, and the surfaces ------------------------------------------------------

	it("advances one frame per kFrameIntervalMs of quiet ticks, and one -- not several -- after a "
	   "late tick", _ {
		set_mode(::ScreensaverMode::DELUGE);
		Rig r;
		r.open(0x2222);
		auto& scene = screensaver_scene();

		// A seeded push is reproducible, so the expected field can be built independently here.
		Rainfall expected;
		expected.reseed(0x2222);
		expected.scatter();
		expect(same_field(scene.rainfall(), expected)).to_be_true();

		// The shell's very first tick reports `dt == 0` deliberately (`Shell::tick`,
		// `shell.cpp:301-305`: a shell first ticked at a non-zero clock must not fast-forward every
		// animation by the boot time), so the clock starts here.
		r.tick(0);
		expect(scene.since_frame_ms()).to_equal(0u);
		expect(same_field(scene.rainfall(), expected)).to_be_true();

		// Short of the interval: legacy's timer has not fired, so nothing moves.
		r.tick(kFrameIntervalMs - 1);
		expect(scene.since_frame_ms()).to_equal(kFrameIntervalMs - 1);
		expect(same_field(scene.rainfall(), expected)).to_be_true();

		// Reaching it: exactly one frame.
		r.tick(2 * kFrameIntervalMs - 1);
		expected.advance();
		expect(scene.since_frame_ms()).to_equal(0u);
		expect(same_field(scene.rainfall(), expected)).to_be_true();

		// A tick 200 ms late is still ONE frame: legacy re-arms its 50 ms timer from the moment it
		// fired, so a late timer event advances once (`hid/display/screensaver.cpp:174-197`).
		r.tick(2 * kFrameIntervalMs - 1 + 200);
		expected.advance();
		expect(same_field(scene.rainfall(), expected)).to_be_true();
	});

	it("reports a frame deadline only for the animated modes, and repaints only the OLED", _ {
		set_mode(::ScreensaverMode::DELUGE);
		Rig r;
		r.open();
		auto& scene = screensaver_scene();
		expect(scene.timer_repaints().oled).to_be_true();
		expect(scene.timer_repaints().grid).to_be_false();
		expect(scene.next_timer().has_value()).to_be_true();
		expect(*scene.next_timer()).to_equal(kFrameIntervalMs);

		// `isAnimated` (`hid/display/screensaver.cpp:60-63`): STARSCAPE and DELUGE only.
		FlashStorage::screensaverMode = ::ScreensaverMode::STARSCAPE;
		expect(scene.next_timer().has_value()).to_be_true();
		FlashStorage::screensaverMode = ::ScreensaverMode::BLANK;
		expect(scene.next_timer().has_value()).to_be_false();
		FlashStorage::screensaverMode = ::ScreensaverMode::OFF;
		expect(scene.next_timer().has_value()).to_be_false();
	});

	it("blanks the panel in BLANK mode, and leaves the grid and the input focus alone in every "
	   "mode", _ {
		set_mode(::ScreensaverMode::BLANK);
		Rig r;
		r.open();
		auto& scene = screensaver_scene();
		r.tick(0);

		// Legacy's BLANK: "a cleared canvas is the whole picture"
		// (`hid/display/screensaver.cpp:255`).
		expect(any_pixel(r.fake.pbm(0))).to_be_false();

		// Opaque on Main and nowhere else: legacy substitutes its canvas for `main` alone
		// (`hid/display/oled.cpp:342-348`).
		expect(scene.oled_transparent(deluge::ui::shell::SurfaceRole::Main)).to_be_false();
		expect(scene.oled_transparent(deluge::ui::shell::SurfaceRole::Aux)).to_be_true();
		// Legacy never touches `PadLEDs` while showing, so neither dim nor base.
		expect(scene.is_grid_overlay()).to_be_true();
		expect(scene.dims_grid()).to_be_false();
		// And it is not a `UI`: the press that wakes it is never swallowed by waking it.
		expect(scene.input_transparent()).to_be_true();
	});

	it("draws the same animation band at both panel geometries", _ {
		set_mode(::ScreensaverMode::STARSCAPE);
		Rig r48;
		r48.open(0x4444);
		r48.tick(0);

		Rig r64{board_64(), profile_64()};
		r64.open(0x4444); // the same seed, so the same field
		r64.tick(0);

		// The animation's own band is canvas rows `kTopmost`..`kHeight` (5..47), which the frame's
		// origin puts at panel-local rows 0..42 on the 48-row profile (`visible_top` 5) and at
		// 5..47 on the 64-row one (`visible_top` 0). `page_major_to_pbm` is canvas-absolute on
		// both, so the same absolute rows must match.
		expect(rows(r48.fake.pbm(0), 128, Rainfall::kTopmost, Rainfall::kVisibleHeight))
		    .to_equal(rows(r64.fake.pbm(0), 128, Rainfall::kTopmost, Rainfall::kVisibleHeight));

		// And the 64-row panel is blank outside that band -- the animation does not spill into the
		// rows legacy's `drawBlock` clips away.
		expect(any_pixel(rows(r64.fake.pbm(0), 128, Rainfall::kHeight, 64 - Rainfall::kHeight))).to_be_false();
		// ... nor above it, on either panel: `drawBlock` clips rows 0-4 away too.
		expect(any_pixel(rows(r64.fake.pbm(0), 128, 0, Rainfall::kTopmost))).to_be_false();
		expect(any_pixel(rows(r48.fake.pbm(0), 128, 0, Rainfall::kTopmost))).to_be_false();
		// And the band really is drawing something, so the equality above is not two blank bands.
		expect(any_pixel(rows(r48.fake.pbm(0), 128, Rainfall::kTopmost, Rainfall::kVisibleHeight))).to_be_true();
	});

	it("is what shell::Screensaver has been pushing at all along: the service now succeeds", _ {
		// The latch `shell/screensaver.h` documents is a latch on a REFUSED push, and a refusal is
		// what an unregistered `SceneKind::Screensaver` used to produce. With this task's registry
		// entry it never fires.
		set_mode(::ScreensaverMode::DELUGE);
		ShellConfig config;
		config.screensaver_idle_ms = 100;
		config.seed = 0x1234;
		Rig r{deluge::ui::shell::kDelugeBoard, deluge_profile(), config};

		r.tick(0);   // arms the deadline
		r.tick(150); // past it
		expect(r.shell.screensaver_push_refused()).to_equal(0u);
		expect(r.shell.stack().depth()).to_equal(1u);
		expect(r.shell.stack().id_at(0).kind).to_equal(SceneKind::Screensaver);
		// The seed really did reach the scene through `SceneId::arg`.
		expect(r.shell.stack().id_at(0).arg).to_equal(static_cast<uint16_t>(0x1234));

		Rainfall expected;
		expected.reseed(0x1234);
		expected.scatter();
		expect(same_field(screensaver_scene().rainfall(), expected)).to_be_true();

		// And input wakes it, exactly as legacy's `noteActivity` does.
		r.shell.on_encoder_edges(0, 1, 200);
		r.tick(200);
		expect(r.shell.stack().depth()).to_equal(0u);
	});

	it("renders the rainfall golden", _ {
		set_mode(::ScreensaverMode::DELUGE);
		Rig r;
		r.open(0x7777);
		r.tick(0);
		expect_golden(self, "../ui/scenes/screensaver_rainfall_48.pbm", r.fake.pbm(0));
	});

	it("renders the starfield golden", _ {
		set_mode(::ScreensaverMode::STARSCAPE);
		Rig r;
		r.open(0x7777);
		r.tick(0);
		expect_golden(self, "../ui/scenes/screensaver_starfield_48.pbm", r.fake.pbm(0));
	});
});
// clang-format on

DELUGE_SPEC(screensaver_scene_spec, screensaver_scene_spec_body)
