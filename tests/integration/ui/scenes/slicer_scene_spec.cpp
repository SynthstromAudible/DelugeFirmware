/// @file
/// @brief `SlicerScene` -- the port of `Slicer` -- driven through a real `Shell` over
///        `uitest::FakeSurfaceSet`, against a real `Sample` and against what main's `Slicer` answers.
///
/// @par The sample fixture, and why it never touches the card
///
/// Identical in shape and reason to `marker_scene_spec.cpp`'s (`support/slicer_scene_fixtures.h`):
/// every display column of the fixture's sample is answered from its seeded peak index, so no I/O happens.
///
/// @par The reference
///
/// Every comparison here runs a gesture through the `Shell` -- real board events into `Shell::on_board_event`, rendered
/// into `FakeSurfaceSet` -- and compares what the scene is left with against what main's `Slicer` was left with after
/// the same gesture through its own public entry points (`focusRegained`, `selectEncoderAction`,
/// `horizontalEncoderAction`, `verticalEncoderAction`, `buttonAction`, `renderOLED`, `renderMainPads`), over a
/// navigator opened the way `SampleBrowser::previewIfPossible`
/// (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`) opens it, as `slicer_scene.tsv` and its `pads/slicer_*`
/// and `pixels/slicer_*` files hold it. The one thing main's run was *told* is the mode, as the port is told it by
/// `slicer_scene().set_mode(Mode::Manual)`.
///
/// @par What is NOT covered here, and why
///
///   * **`Slicer::graphicsRoutine`, the playhead.** Its only interesting
///     branch needs a sound's voice pool to report a live voice, which needs a running
///     `AudioEngine`; `support/song_fixture.h` builds a `Song` by direct construction and never
///     calls `AudioEngine::init()`, so the only branch a spec can reach is "no voices, no tick
///     square", which asserts nothing. The scene does not port it -- see `slicer_scene.h`.
///   * **`Slicer::redraw`**, which is empty: a 7SEG-only hook with no body.
///   * **`Slicer::padAction`'s "do slice" branch** -- the voice walk, which is
///     `graphicsRoutine`'s walk again, needs a live voice for the same reason and is unreachable for
///     the same reason. A sound's voices (`Sound::voicePool()`, `processing/sound/sound.h`) come
///     only from `AudioEngine`'s voice pool, and nothing
///     in a spec process boots one. The *arithmetic* it feeds is ported as
///     `SlicerScene::add_slice_at` and is compared against main's table for the same taps
///     (below); only the source of the number is uncovered.
///   * **The slice body, `SliceCore::do_slice` (`gui/ui/slice_core.cpp`).** It reads the sound it
///     slices, recolours the clip view and ends the slice in the sample browser -- `gui/` globals
///     `src/ui/scenes/**` may never name, and a card read. So the commit's *body* is behind
///     `SliceOps::commit` and the spec drives a fake; what IS compared, and mutation-proved, is
///     everything around it: the snapshot, the gate, the dispatch and the release.
///
/// @par The seeded slice table
///
/// Several arms need a slice table with more than one entry in it, and neither the port nor main
/// can build one from its own `padAction`: main's adder is behind the voice walk above. So the
/// port's table is built through its own ported adder (`SlicerScene::add_slice_at`, the port of
/// `Slicer::padAction`'s slice-adding arm), main's had its own public members assigned the same
/// taps sorted, and each example **asserts the two tables agree before the gesture under test
/// runs** (`expect_tables_match`). The seeding is therefore an assertion, not an assumption.
///
/// @note The spec may include `gui/`; the library may not. Same rule `marker_scene_spec.cpp` runs under.

#include "gui/ui/shell_host/host_ports.h"
#include "gui/ui/shell_host/shell_host.h"
#include "gui/ui/shell_slice_ops.h"
#include "ui/scenes/browser/browser_scene.h"
#include "ui/scenes/context_menu/context_menu_scene.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/slicing/slicer_scene.h"

#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"
#include "ui/widgets/core/metrics.h"
#include "ui/widgets/grid/columns.h"

#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "storage/cluster/cluster.h"

#include "support/browser_scene_fixtures.h"
#include "support/fake_ports.h"
#include "support/oracle_files.h"
#include "support/oracle_table.h"
#include "support/popup_capture.h"
#include "support/slicer_scene_fixtures.h"
#include "support/song_fixture.h"

// NOT `support/card_access.h`; the omission is deliberate. Nothing here drives a listing, and the
// card-touching half of `SliceCore::do_slice` is behind `SliceOps::commit`, whose fake performs no
// I/O. `SdRoot` would assert a card touch that does not happen, as `QuietAudio` would a hold on the
// audio routine nothing here needs.

#include "support/mocks/fake_surfaces.h"
#include "support/printers/rgb_printer.h"
#include "unit/ui/gfx/support/golden.h"

#include "cppspec.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::ui::scenes::slicing::kSlicerSceneKind;
using deluge::ui::scenes::slicing::Mode;
using deluge::ui::scenes::slicing::slicer_scene;
using deluge::ui::scenes::slicing::SlicerScene;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
namespace sf = deluge::ui::scenes::spec::slicer_fx;
using sf::FakeOps;
using sf::kNumClusters;
using sf::kSampleLength;
using sf::Rig;
using sf::seed_port;

namespace {

// --- the second geometry ----------------------------------------------------------------------

/// @brief A 128x64 single-panel board, so the same scene can be recorded at the other shipped panel
///        geometry. Main's own `renderOLED` draws at a compile-time 48 rows, so the 64-row coverage is a
///        golden, exactly as `marker_scene_spec`'s is.
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
		static constexpr const deluge::ui::widgets::core::Style* kStyles[] = {
		    &deluge::ui::widgets::core::kDelugeStyle64};
		BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

/// @brief `slicer_scene.tsv`: where main's slicer is left after each gesture below.
spec::OracleTable& table() {
	return spec::OracleTable::named("slicer_scene");
}

/// @brief The values of row (`slicer/<path>`, @p column), or `<no row>` alone when there is none.
std::vector<std::string> frozen(std::string_view path, std::string_view column) {
	const std::vector<std::string>* v = table().find("slicer/" + std::string{path}, column);
	return v != nullptr ? *v : std::vector<std::string>{"<no row>"};
}

/// @brief The one value of row (`slicer/<path>`, @p column), as a number; `INT64_MIN` when there is no such row.
int64_t frozen_number(std::string_view path, std::string_view column) {
	const std::vector<std::string> v = frozen(path, column);
	return v.size() == 1 && v[0] != "<no row>" ? std::stoll(v[0]) : INT64_MIN;
}

/// @brief Where the port's main pads first differ from a reference pad image, and how many differ.
///
/// Main columns only. Main's `renderMainPads` never writes the two sidebar columns and `Slicer` has no
/// `renderSidebar` at all, so the sidebar has no counterpart to compare -- exactly
/// `marker_scene_spec`'s situation. A missing image differs everywhere.
struct PadDiff {
	int32_t count = 0;
	int32_t first_x = -1;
	int32_t first_y = -1;
};

template <typename AnyRig>
PadDiff pad_diff_excusing(int32_t excused_column, AnyRig& r, const std::optional<spec::OraclePads>& frozen_pads) {
	const deluge::ui::gfx::grid::PadGrid& g = r.fake.grid().grid();
	PadDiff d;
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		if (x == excused_column) {
			continue;
		}
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (!frozen_pads || !(g.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y)) == (*frozen_pads)[y][x])) {
				if (d.count == 0) {
					d.first_x = x;
					d.first_y = y;
				}
				d.count++;
			}
		}
	}
	return d;
}

template <typename AnyRig>
PadDiff pad_diff(AnyRig& r, const std::optional<spec::OraclePads>& frozen_pads) {
	return pad_diff_excusing(-1, r, frozen_pads);
}

/// @brief Q30's column: the fixture's first column starts at frame 0, where main's scan reads only the first whole
///        cluster (`origin/main:src/deluge/gui/waveform/waveform_renderer.cpp` `WaveformRenderer::findPeaksPerCol`'s
///        initial-transient arm), and ours takes the peak of every frame the column covers.
constexpr int32_t kQ30Column = 0;

/// @brief Assert the port's main pads are `pads/<name>.txt`, pad for pad, but for Q30's column, which must differ.
/// @param self The enclosing `it(...)` block's lambda parameter -- `expect` expands to `self.expect`.
template <typename Self, typename AnyRig>
void expect_pads_match(Self& self, AnyRig& r, std::string_view name) {
	const std::optional<spec::OraclePads> main_pads = spec::OracleFiles::pads(name);
	const PadDiff d = pad_diff_excusing(kQ30Column, r, main_pads);
	// The coordinates first, so a failure names a pad rather than only a count.
	expect(d.first_x).to_equal(-1);
	expect(d.first_y).to_equal(-1);
	expect(d.count).to_equal(0);
	// The excused column differs from main's: a port that matched it there would have lost Q30.
	expect(pad_diff(r, main_pads).count).to_be_greater_than(0);
}

/// @brief Assert the port's slice table is main's, as `slicer/<path>`'s `num_slices`, `current`, `starts` and
///        `transposes` rows hold it, entry for entry over all 64 slots.
template <typename Self>
void expect_tables_match(Self& self, std::string_view path) {
	expect(int64_t{slicer_scene().num_manual_slices()}).to_equal(frozen_number(path, "num_slices"));
	expect(int64_t{slicer_scene().current_slice()}).to_equal(frozen_number(path, "current"));
	std::vector<std::string> starts;
	std::vector<std::string> transposes;
	for (int32_t i = 0; i < deluge::ui::scenes::slicing::kMaxManualSlices; i++) {
		starts.push_back(std::to_string(slicer_scene().slice(i).start_pos));
		transposes.push_back(std::to_string(slicer_scene().slice(i).transpose));
	}
	expect(starts).to_equal(frozen(path, "starts"));
	expect(transposes).to_equal(frozen(path, "transposes"));
}

} // namespace

// clang-format off
describe slicer_scene_spec_body("scenes/slicer SlicerScene", $ {

	before_each([] {
		if (display == nullptr) {
			display = &deluge::ui::scenes::spec::quiet_display();
		}
	});

	// --- composition --------------------------------------------------------------------------

	it("draws the main pads only, leaving the sidebar to the view beneath", _ {
		// `Slicer` overrides `renderMainPads` but not `renderSidebar`, nor `getGreyoutColsAndRows`.
		const deluge::ui::shell::Scene& s = slicer_scene();
		expect(s.pad_regions()).to_equal(deluge::ui::shell::PadRegions::Main);
		expect(static_cast<uint64_t>(s.greyout_mask().cols)).to_equal(uint64_t{0});
		expect(static_cast<uint64_t>(s.greyout_mask().rows)).to_equal(uint64_t{0});
	});

	// --- opening ------------------------------------------------------------------------------

	it("opens exactly where main's focusRegained leaves its slicer", _ {
		Rig r;
		r.open();
		r.tick(0);
		expect(static_cast<int64_t>(slicer_scene().mode())).to_equal(frozen_number("open", "mode"));
		expect(int64_t{slicer_scene().num_clips()}).to_equal(frozen_number("open", "clips"));
		// Every one of the 64 slice slots reset, not just the first.
		expect_tables_match(self, "open");
	});

	it("opens zoomed all the way out, on the navigator the browser would have left", _ {
		Rig r;
		r.open();
		r.tick(0);

		expect(std::vector<std::string>{std::to_string(slicer_scene().x_scroll()), std::to_string(slicer_scene().x_zoom())})
		    .to_equal(frozen("open", "view"));
	});

	// --- the OLED page, against main ---------------------------------------------------------

	it("draws the REGION page main's renderOLED draws", _ {
		Rig r;
		r.open();
		r.tick(0);
		expect(r.oled()).to_equal(spec::OracleFiles::pixels("slicer_region"));
	});

	it("draws the MANUAL page main's renderOLED draws", _ {
		Rig r;
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		expect(r.oled()).to_equal(spec::OracleFiles::pixels("slicer_manual"));
	});

	// --- the select encoder, against main ----------------------------------------------------

	it("counts REGION clips the way main's selectEncoderAction counts them", _ {
		Rig r;
		r.open();
		r.tick(0);
		r.turn_select(1, 10);
		r.settle(10);
		r.turn_select(3, 20);
		r.settle(20);
		expect(int64_t{slicer_scene().num_clips()}).to_equal(frozen_number("count_clips", "clips"));
		expect(r.oled()).to_equal(spec::OracleFiles::pixels("slicer_count_clips"));
	});

	it("wraps 257 back to 2, exactly as main does", _ {
		// The wrap tests for equality with 257, so it is only reachable one detent at a time from
		// 256 -- 240 + 1 lands on 257, 240 + 2 lands on 258 and sails past. Both sides take the same
		// route from their own 16.
		Rig r;
		r.open();
		r.tick(0);
		for (int32_t i = 0; i < 4; i++) {
			const uint32_t at = 10 + static_cast<uint32_t>(i) * 2;
			r.turn_select(60, at);
			r.settle(at);
		}
		expect(slicer_scene().num_clips()).to_equal(256);
		r.turn_select(1, 30);
		r.settle(30);
		const int32_t ported = slicer_scene().num_clips();
		expect(ported).to_equal(2);
		expect(int64_t{ported}).to_equal(frozen_number("wrap_up", "clips"));
		expect(r.oled()).to_equal(spec::OracleFiles::pixels("slicer_wrap_up"));
	});

	it("wraps 1 back to 256, exactly as main does", _ {
		Rig r;
		r.open();
		r.tick(0);
		r.turn_select(-15, 10);
		r.settle(10);
		const int32_t ported = slicer_scene().num_clips();
		expect(ported).to_equal(256);
		expect(int64_t{ported}).to_equal(frozen_number("wrap_down", "clips"));
		expect(r.oled()).to_equal(spec::OracleFiles::pixels("slicer_wrap_down"));
	});

	it("steps past the wrap on a multi-detent turn, exactly as main does", _ {
		// 255 + 2 == 257 wraps; 256 + 2 == 258 does not. The bug-shaped half of the arm above, and
		// the reason `num_clips_` has to be an `int16_t` rather than an `int32_t`.
		Rig r;
		r.open();
		r.tick(0);
		for (int32_t i = 0; i < 4; i++) {
			const uint32_t at = 10 + static_cast<uint32_t>(i) * 2;
			r.turn_select(60, at);
			r.settle(at);
		}
		r.turn_select(2, 30);
		r.settle(30);
		const int32_t ported = slicer_scene().num_clips();
		expect(ported).to_equal(258);
		expect(int64_t{ported}).to_equal(frozen_number("past_wrap", "clips"));
	});

	it("lets a fast turn drive the REGION count negative, exactly as main does", _ {
		// The floor wrap tests for equality with 1, so a turn that STEPS OVER 1 misses it: 16 - 20
		// is -4, and -4 is what both sides keep. Recorded in `src/ui/scenes/README.md`'s "Defects this
		// port found in shipping firmware" as a legacy defect the port reproduces rather than fixes -- a one-sided fix would be a
		// silent divergence, which is what every other example here exists to catch.
		Rig r;
		r.open();
		r.tick(0);
		r.turn_select(-20, 10);
		r.settle(10);
		const int32_t ported = slicer_scene().num_clips();
		expect(ported).to_equal(-4);
		expect(int64_t{ported}).to_equal(frozen_number("negative", "clips"));
		// And the page shows it: `intToString` renders the sign, so the window reads "-4".
		expect(r.oled()).to_equal(spec::OracleFiles::pixels("slicer_negative"));
	});

	it("only ever removes manual slices from the select encoder, as main does", _ {
		Rig r;
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		// A positive turn adds nothing (adding is `padAction`'s) ...
		r.turn_select(4, 10);
		r.settle(10);
		const int32_t after_up = slicer_scene().num_manual_slices();
		// ... and a negative turn floors at one, resetting slot 0.
		r.turn_select(-4, 20);
		r.settle(20);
		const int32_t after_down = slicer_scene().num_manual_slices();
		const int32_t cursor = slicer_scene().current_slice();
		expect(int64_t{after_up}).to_equal(frozen_number("select_manual", "num_after_up"));
		expect(int64_t{after_down}).to_equal(frozen_number("select_manual", "num_after_down"));
		expect(int64_t{cursor}).to_equal(frozen_number("select_manual", "current_after_down"));
	});

	// --- the horizontal encoder, against main ------------------------------------------------

	it("does nothing in REGION mode, as main's horizontalEncoderAction does", _ {
		Rig r;
		r.open();
		r.tick(0);
		r.turn_horizontal(5, 10);
		r.settle(10);
		const int32_t ported = slicer_scene().slice(0).start_pos;
		expect(ported).to_equal(0);
		expect(int64_t{ported}).to_equal(frozen_number("horizontal_region", "start0"));
	});

	it("moves the current slice 100 samples per detent, as main does", _ {
		Rig r;
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		r.turn_horizontal(3, 10);
		r.settle(10);
		r.turn_horizontal(-1, 20);
		r.settle(20);
		const int32_t ported = slicer_scene().slice(0).start_pos;
		expect(ported).to_equal(200);
		expect(int64_t{ported}).to_equal(frozen_number("horizontal_manual", "start0"));
	});

	it("moves 10 samples per detent with shift held, as main does", _ {
		Rig r;
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		r.shift(true, 10);
		r.settle(10);
		r.turn_horizontal(3, 20);
		r.settle(20);
		r.shift(false, 30);
		r.settle(30);
		const int32_t ported = slicer_scene().slice(0).start_pos;
		expect(ported).to_equal(30);
		expect(int64_t{ported}).to_equal(frozen_number("horizontal_shift", "start0"));
	});

	it("clamps the start position to the sample, as main does", _ {
		Rig r;
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		// Below zero first: legacy clamps negative BEFORE comparing against the unsigned length.
		r.turn_horizontal(-5, 10);
		r.settle(10);
		const int32_t at_floor = slicer_scene().slice(0).start_pos;
		// Then far past the end. One detent is 100 samples and the sample is 524,288 frames long,
		// so 90 turns of 60 detents (540,000 samples) overshoots it and the clamp has to bite.
		// One turn per settle, 60 detents each: see `turn_select`'s warning about the `int8_t`
		// edge count.
		for (int32_t i = 0; i < 90; i++) {
			const uint32_t at = 20 + static_cast<uint32_t>(i) * 2;
			r.turn_horizontal(60, at);
			r.settle(at);
		}
		const int32_t at_ceiling = slicer_scene().slice(0).start_pos;
		expect(at_floor).to_equal(0);
		expect(int64_t{at_floor}).to_equal(frozen_number("horizontal_clamp", "start0_floor"));
		expect(int64_t{at_ceiling}).to_equal(frozen_number("horizontal_clamp", "start0_ceiling"));
	});

	// --- the vertical encoder, against main --------------------------------------------------

	it("transposes the current slice and clamps at +-24, as main does", _ {
		Rig r;
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		r.turn_vertical(5, 10);
		r.settle(10);
		const int32_t after_up = slicer_scene().slice(0).transpose;
		r.turn_vertical(60, 20);
		r.settle(20);
		const int32_t at_ceiling = slicer_scene().slice(0).transpose;
		r.turn_vertical(-60, 30);
		r.settle(30);
		r.turn_vertical(-60, 40);
		r.settle(40);
		const int32_t at_floor = slicer_scene().slice(0).transpose;
		expect(after_up).to_equal(5);
		expect(int64_t{after_up}).to_equal(frozen_number("vertical_manual", "transpose0_up"));
		expect(at_ceiling).to_equal(24);
		expect(int64_t{at_ceiling}).to_equal(frozen_number("vertical_manual", "transpose0_ceiling"));
		expect(at_floor).to_equal(-24);
		expect(int64_t{at_floor}).to_equal(frozen_number("vertical_manual", "transpose0_floor"));
	});

	it("does nothing in REGION mode, as main's verticalEncoderAction does", _ {
		Rig r;
		r.open();
		r.tick(0);
		r.turn_vertical(5, 10);
		r.settle(10);
		const int32_t ported = slicer_scene().slice(0).transpose;
		expect(ported).to_equal(0);
		expect(int64_t{ported}).to_equal(frozen_number("vertical_region", "transpose0"));
	});

	// --- the pads, against main --------------------------------------------------------------

	it("draws the REGION waveform main's renderMainPads draws", _ {
		Rig r;
		r.open();
		r.tick(0);
		expect_pads_match(self, r, "slicer_region");
	});

	it("keeps its waveform when reopened on the same sample, as legacy's navigator does", _ {
		// Legacy's slicer reads `waveformBasicNavigator`, which nothing resets between two opens on the same file, so
		// the second open draws the first open's peaks. Ours reads the sample's peak index, which outlives both opens.
		Rig r;
		r.open();
		r.tick(0);
		const deluge::ui::gfx::grid::PadGrid first = r.fake.grid().grid();

		r.shell.stack().pop(SceneKind::Null);
		r.open();
		r.tick(10);

		int32_t differ = 0;
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				const auto at = [&](const deluge::ui::gfx::grid::PadGrid& g) {
					return g.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
				};
				differ += at(first) == at(r.fake.grid().grid()) ? 0 : 1;
			}
		}
		expect(differ).to_equal(0);
	});

	it("opens on the navigator the browser's preview drew, with the columns the preview read", _ {
		// `Slicer::renderMainPads` renders through `waveformBasicNavigator`, which `SampleBrowser::previewIfPossible`
		// (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`) opened and scanned for this sample: the columns
		// are already read, so the slicer draws the preview's peaks. The browser hands the slicer its view, and the
		// slicer reads the same sample's peak index, so the slicer's first frame is the browser's last.
		namespace bfx = deluge::ui::scenes::spec::browser_fx;
		bfx::CleanBrowser clean;
		spec::SongFixture f = spec::make_synth_song();
		Sample* const sample = sf::make_sample();
		deluge::ui::scenes::spec::SliceHeld held{sample, sf::kSlicePath};
		slicer_scene().set_ports(&deluge::gui::shell_host::host_ports());
		bfx::Rig r;
		menu::MenuContext ctx = bfx::context_of(f);
		(void)menu::ensure_first_range(ctx);
		r.open_and_land(ctx, browser::BrowserId::Sample);
		browser::browser_scene().preview_settled(true);
		while (r.clock < 50) {
			r.step();
		}
		const deluge::ui::views::waveform::WaveformView view = browser::browser_scene().waveform_view();
		const deluge::ui::gfx::grid::PadGrid preview = r.fake.grid().grid();
		held.open();
		expect(r.shell.stack().push(SceneId{kSlicerSceneKind, 0})).to_be_true();
		r.step();
		expect(slicer_scene().x_scroll()).to_equal(view.scroll());
		expect(slicer_scene().x_zoom()).to_equal(view.zoom());
		int32_t differ = 0;
		int32_t lit = 0;
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				const RGB was = preview.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
				differ += was == r.fake.grid().grid().at(static_cast<uint8_t>(x), static_cast<uint8_t>(y)) ? 0 : 1;
				lit += was == RGB{} ? 0 : 1;
			}
		}
		expect(lit).to_be_greater_than(0);
		expect(differ).to_equal(0);
		expect_pads_match(self, r, "slicer_preview_nav");
	});

	it("opens on the file the host bound: the sample's identity and its path", _ {
		// The host takes the preview drum's file here, as the browser's held-preview door does; the open a card load
		// binds needs the loader, which a spec process does not run, and is checked in the sample-claim scenario's
		// slicer legs (`tests/integration/scenarios/sample_claim_scenario.cpp`).
		Rig r;
		slicer_scene().open(std::nullopt);
		r.open();
		const std::optional<deluge::ui::scenes::slicing::SliceFile>& file = slicer_scene().file();
		expect(file.has_value()).to_be_true();
		if (file) {
			expect(file->path).to_equal(r.sample->filePath);
			expect(file->identity).to_equal(deluge::gui::shell_host::waveform_identity(*r.sample));
		}
		// A guest close releases the host, and the scene holds no file the host has let go.
		deluge::gui::ui::releaseShellSlicerBeforeClose();
		expect(slicer_scene().has_sample()).to_be_false();
	});

	it("names nothing at SLICE once BACK closes it on the app's host, and keeps its file for a reopen", _ {
		Rig r;
		deluge::gui::ui::wireShellSliceOps();
		r.open();
		r.tick(0);
		const bind::WaveformAddress slice{.kind = bind::WaveformAddress::Kind::SLICE};
		expect(deluge::gui::shell_host::host_ports().waveforms->info(slice).has_value()).to_be_true();
		r.back(true, 10);
		r.settle(10);
		expect(r.on_stack()).to_be_false();
		expect(deluge::gui::shell_host::host_ports().waveforms->info(slice).has_value()).to_be_false();
		expect(slicer_scene().has_sample()).to_be_true();
	});

	it("opens zoomed out on a fresh navigator when the browser shows no preview of the sample", _ {
		Rig r;
		slicer_scene().open(std::nullopt);
		r.open_host();
		r.shell.init(SceneId{kSlicerSceneKind, 0});
		r.tick(0);
		expect_pads_match(self, r, "slicer_fresh_nav");
	});

	it("draws the MANUAL squashed waveform, slice marks and slot grid main draws", _ {
		Rig r;
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		expect_pads_match(self, r, "slicer_manual");
	});

	it("moves the slice mark across the row as the horizontal encoder moves the slice", _ {
		// One detent is 100 samples and one mark column is `lengthInSamples / 16` = 32768 samples,
		// so the mark only moves once the slice has been driven a long way. 8 turns of 50 detents
		// is 400 detents, 40,000 samples -- past the first column boundary.
		Rig r;
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		for (int32_t i = 0; i < 8; i++) {
			const uint32_t at = 10 + static_cast<uint32_t>(i) * 2;
			r.turn_horizontal(50, at);
			r.settle(at);
		}

		expect(int64_t{slicer_scene().slice(0).start_pos}).to_equal(frozen_number("mark_moves", "start0"));
		expect(slicer_scene().slice(0).start_pos).to_equal(40000);
		expect_pads_match(self, r, "slicer_mark_moves");
	});

	// --- the slice table, against main -------------------------------------------------------

	it("places slices in sorted order, as main's padAction does", _ {
		// `Slicer::padAction`'s slice-adding arm. The taps go in unsorted deliberately: its bubble
		// sort is what puts them in order, and a table that arrived sorted would not exercise it.
		Rig r;
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		const std::vector<int32_t> taps{300000, 100000, 200000, 50000};
		seed_port(taps);
		expect_tables_match(self, "sorted");
		// Five: `focusRegained`'s slice 0 plus the four taps.
		expect(slicer_scene().num_manual_slices()).to_equal(5);
	});

	it("refuses a slice at or past the end of the sample, as legacy does", _ {
		// Main's `Slicer::padAction` (`origin/main:src/deluge/gui/ui/slicer.cpp`):
		// `samplePos < waveformBasicNavigator.sample->lengthInSamples`.
		Rig r;
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		const int64_t length = static_cast<int64_t>(kSampleLength);
		expect(slicer_scene().add_slice_at(static_cast<int32_t>(length) - 1)).to_be_true();
		expect(slicer_scene().add_slice_at(static_cast<int32_t>(length))).to_be_false();
		expect(slicer_scene().num_manual_slices()).to_equal(2);
	});

	it("stops placing slices at the 64-slot ceiling, as legacy does", _ {
		// The other half of that test in `Slicer::padAction`: `numManualSlice < MAX_MANUAL_SLICES`. `focusRegained`
		// leaves one slice, so 63 taps fill the table and the 64th is refused.
		Rig r;
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		for (int32_t i = 0; i < 63; i++) {
			expect(slicer_scene().add_slice_at(1000 + i)).to_be_true();
		}
		expect(slicer_scene().num_manual_slices()).to_equal(deluge::ui::scenes::slicing::kMaxManualSlices);
		expect(slicer_scene().add_slice_at(99999)).to_be_false();
		expect(slicer_scene().num_manual_slices()).to_equal(deluge::ui::scenes::slicing::kMaxManualSlices);
	});

	// --- the pads, against main's padAction ---------------------------------------------------

	it("maps every slot pad to the slice main's own slot grid draws there", _ {
		// Main's `padAction` slot index is the INVERSE of its `renderMainPads` slot layout, and main's
		// own renderer is what says where slot `i` lives. So the pad coordinate for each index is the
		// `slicer/slots slot/<i>` row, read off a real `Slicer::renderMainPads` image (the one slot it
		// draws bright) -- not out of arithmetic this file wrote. Then the port is pressed there and
		// asked which slice it thinks that was.
		//
		// Main's `padAction` itself was NOT driven: its select branch reaches `preview()` on every index
		// that maps to a placed slice, and `previewForTarget` needs a kit clip, a resolved model stack
		// and a booted `AudioEngine`. See the file docstring.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		std::vector<int32_t> taps;
		for (int32_t i = 0; i < 63; i++) {
			taps.push_back(1000 * (i + 1));
		}
		seed_port(taps);
		expect(slicer_scene().num_manual_slices()).to_equal(deluge::ui::scenes::slicing::kMaxManualSlices);

		for (int32_t i = 0; i < deluge::ui::scenes::slicing::kMaxManualSlices; i++) {
			const std::vector<std::string> slot = frozen("slots", "slot/" + std::to_string(i));
			expect(slot.size()).to_equal(size_t{2});
			if (slot.size() != 2) {
				continue;
			}
			const int32_t x = std::stoi(slot[0]);
			const int32_t y = std::stoi(slot[1]);
			expect(x).to_be_greater_than(-1);
			const uint32_t at = 100 + static_cast<uint32_t>(i) * 4;
			r.pad(x, y, 64, at);
			r.settle(at);
			r.pad(x, y, 0, at + 2);
			r.settle(at + 2);
			expect(slicer_scene().current_slice()).to_equal(i);
		}
	});

	it("auditions the slice under the pad, from its start to the next slice's start", _ {
		// `Slicer::padAction`'s interior arm (`origin/main:src/deluge/gui/ui/slicer.cpp`).
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		seed_port({100000, 200000});
		// Pad (1, 0) is slot 1 -- the middle slice, whose end is slot 2's start.
		r.pad(1, 0, 64, 10);
		r.settle(10);
		expect(static_cast<int32_t>(ops.auditions.size())).to_equal(1);
		expect(static_cast<int32_t>(ops.audition_at(0).start_point)).to_equal(100000);
		expect(static_cast<int32_t>(ops.audition_at(0).end_point)).to_equal(200000);
		expect(ops.audition_at(0).on).to_equal(64);
	});

	it("auditions the last slice to the end of the sample", _ {
		// `Slicer::padAction`'s tail arm (`origin/main:src/deluge/gui/ui/slicer.cpp`).
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		seed_port({100000});
		r.pad(1, 0, 64, 10); // slot 1, the last slice
		r.settle(10);
		expect(static_cast<int32_t>(ops.auditions.size())).to_equal(1);
		expect(static_cast<int32_t>(ops.audition_at(0).start_point)).to_equal(100000);
		expect(static_cast<int64_t>(ops.audition_at(0).end_point)).to_equal(static_cast<int64_t>(kSampleLength));
	});

	it("silences the audition when the pad is released", _ {
		// `Slicer::padAction` (`origin/main:src/deluge/gui/ui/slicer.cpp`): a release inside the window is
		// `preview(0, 0, 0, 0)`.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		r.pad(0, 0, 64, 10);
		r.settle(10);
		r.pad(0, 0, 0, 20);
		r.settle(20);
		expect(static_cast<int32_t>(ops.auditions.size())).to_equal(2);
		expect(ops.audition_at(1).on).to_equal(0);
		expect(static_cast<int32_t>(ops.audition_at(1).start_point)).to_equal(0);
	});

	it("takes the popup down only when the pad moves the cursor, as main does", _ {
		// Main's `padAction`: `closePopup` is `currentSlice != slicePadIndex`, so re-tapping the pad
		// the cursor is already on leaves a popup up.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		seed_port({100000});

		// Put a real popup on screen first, through the arm that raises one.
		r.y_enc(true, 4);
		r.settle(4);
		expect(r.popup()).to_equal(std::string("Transpose: 0"));
		expect(r.popup_showing()).to_be_true();

		// Main's own `padAction` could not be driven (see the file docstring), so its `closePopup`
		// expression was read with its own cursor on slot 0: `slicer/popup_close same_pad` for a tap
		// on slot 0, `other_pad` for a tap on slot 1, 1 where it takes the popup down.
		//
		// Slot 0 is already the cursor, so tapping it cancels nothing and the popup survives ...
		r.pad(0, 0, 64, 10);
		r.settle(10);
		expect(r.popup_showing()).to_be_true();
		expect(int64_t{r.popup_showing() ? 0 : 1}).to_equal(frozen_number("popup_close", "same_pad"));
		expect(slicer_scene().current_slice()).to_equal(0);

		// ... and moving to slot 1 takes it down.
		r.pad(1, 0, 64, 20);
		r.settle(20);
		expect(slicer_scene().current_slice()).to_equal(1);
		expect(r.popup_showing()).to_be_false();
		expect(int64_t{r.popup_showing() ? 0 : 1}).to_equal(frozen_number("popup_close", "other_pad"));
	});

	it("keeps its slice table when a popup it raised itself expires", _ {
		// `on_resume()` must not re-run `focusRegained`: a popup is a scene in the shell
		// (`SceneKind::Popup`), so the removal `Notices` performs 800 ms later calls `on_resume()`. Forty hand-placed slices vanished because the
		// user tapped a pad. See `SlicerScene::on_resume` for the whole argument.
		//
		// The popup here is raised by the scene's own Y_ENC arm (`Slicer::buttonAction`), and the 800 ms
		// is `kPopupTimeoutMs` (`ui/shell/notices.cpp`), returned by `Notices::timeout_for`; the push is
		// `Notices::show_popup_with_timeout` and the removal `Notices::tick`.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		seed_port({100000, 200000, 300000});
		expect(slicer_scene().num_manual_slices()).to_equal(4);

		r.y_enc(true, 10);
		r.settle(10);
		expect(r.popup()).to_equal(std::string("Transpose: 0"));
		// Past the popup's own timeout, so `Notices::tick` removes the scene.
		r.tick(1200);
		r.tick(1210);

		expect(slicer_scene().num_manual_slices()).to_equal(4);
		expect(slicer_scene().slice(3).start_pos).to_equal(300000);
	});

	it("resets its state when a real scene above it closes", _ {
		// The other half of the pair above, and the half `SlicerScene::on_resume`'s empty-body
		// placeholder could not make: `closeUI` calls `focusRegained()` on whatever it leaves on
		// top (`gui/ui/ui.cpp`), so an opaque scene closing over the slicer DOES reset it.
		// `ContextMenuScene` is the stand-in for the context menus and MIDI-learn overlays wiring
		// will put here; unlike a popup it is not `input_transparent()`, so it really does take the
		// input top and really does hand it back.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		seed_port({100000, 200000, 300000});
		expect(slicer_scene().num_manual_slices()).to_equal(4);

		const SceneId menu{deluge::ui::scenes::context_menu::kContextMenuSceneKind,
		                   static_cast<uint16_t>(deluge::ui::scenes::context_menu::DescriptorId::ResetFlash)};
		expect(r.shell.stack().push(menu)).to_be_true();
		expect(&r.shell.stack().input_top()).not_().to_equal(&slicer_scene());
		r.shell.stack().pop();
		expect(&r.shell.stack().input_top()).to_equal(&slicer_scene());

		// `Slicer::focusRegained` (`origin/main:src/deluge/gui/ui/slicer.cpp`) again: one slice, slot 0, REGION.
		expect(slicer_scene().num_manual_slices()).to_equal(1);
		expect(slicer_scene().current_slice()).to_equal(0);
		expect(slicer_scene().mode()).to_equal(Mode::Region);
	});

	it("swallows a REGION main pad, which main hands to the sample browser beneath", _ {
		// Main's `Slicer::padAction` hands it to `SampleBrowser::padAction`, whose keyboard would move the file under a
		// slicer that is slicing another: swallowed here (DIFFERENCES D57, accepted for the main pads).
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		r.tick(0);
		const uint32_t dropped = r.shell.router().dropped_events();
		r.pad(3, 2, 64, 10);
		r.settle(10);
		expect(r.shell.router().dropped_events()).to_equal(dropped);
		expect(static_cast<int32_t>(ops.auditions.size())).to_equal(0);
		expect(slicer_scene().current_slice()).to_equal(0);
		expect(r.on_stack()).to_be_true();
	});

	it("leaves the audition column to the view beneath in REGION mode, as SampleBrowser::padAction does", _ {
		// `SampleBrowser::padAction`'s audition arm hands x = 17 to the instrument clip view; MANUAL deals with it.
		Rig r;
		r.open();
		expect(slicer_scene().grid_pad_claim()).to_equal(deluge::ui::shell::GridPadClaim::AllButAudition);
		slicer_scene().set_mode(Mode::Manual);
		expect(slicer_scene().grid_pad_claim()).to_equal(deluge::ui::shell::GridPadClaim::All);
	});

	it("exits the slicer and the browser beneath on a REGION mute-column press, as SampleBrowser::padAction does", _ {
		// `possiblyExit`: a press with no UI mode stops the preview and runs `exitAction`, which closes the browser and
		// the slicer over it. A release does nothing.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		r.tick(0);
		const auto pad = [&](int16_t velocity) {
			deluge::ui::shell::ButtonSet held{};
			deluge::ui::widgets::core::PadMask pads{};
			const deluge::ui::shell::UiContext ctx{held, pads, false, false, 0, r.profile};
			deluge::ui::shell::UiEvent e{deluge::ui::shell::UiEvent::Kind::Pad};
			e.x = kDisplayWidth;
			e.y = 3;
			e.velocity = velocity;
			e.on = velocity != 0;
			return slicer_scene().handle(e, ctx);
		};
		const deluge::ui::shell::Result released = pad(0);
		expect(released.handled).to_be_true();
		expect(released.actions.empty()).to_be_true();
		const deluge::ui::shell::Result pressed = pad(64);
		expect(pressed.handled).to_be_true();
		const auto removes = [&](deluge::ui::shell::SceneKind kind) {
			return std::ranges::any_of(pressed.actions, [&](const deluge::ui::shell::Action& a) {
				return a.kind == deluge::ui::shell::Action::Kind::Remove && a.pop_to == kind;
			});
		};
		expect(removes(deluge::ui::scenes::slicing::kSlicerSceneKind)).to_be_true();
		expect(removes(deluge::ui::scenes::browser::kBrowserSceneKind)).to_be_true();
		expect(ops.stop_previewing_calls).to_equal(1);
		// Through the shell: the slicer leaves.
		r.pad(kDisplayWidth, 3, 64, 10);
		r.settle(10);
		expect(r.on_stack()).to_be_false();
	});

	it("keeps a REGION mute-column press while an audition is held, as SampleBrowser::padAction's !currentUIMode", _ {
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		deluge::ui::scenes::spec::HeldPorts auditioning;
		auditioning.ui.auditioning_now = true;
		const bind::Ports held = auditioning.ports();
		slicer_scene().set_ports(&held);
		r.open();
		r.tick(0);
		r.pad(kDisplayWidth, 3, 64, 10);
		r.settle(10);
		slicer_scene().set_ports(&deluge::gui::shell_host::host_ports());
		expect(r.on_stack()).to_be_true();
		expect(ops.stop_previewing_calls).to_equal(0);
	});

	it("keeps a MANUAL mute-column press, as main's MANUAL arm deals with every pad", _ {
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		r.pad(kDisplayWidth, 3, 64, 10);
		r.settle(10);
		expect(r.on_stack()).to_be_true();
		expect(ops.stop_previewing_calls).to_equal(0);
	});

	it("swallows a MANUAL pad outside the slot grid, as legacy does", _ {
		// `Slicer::padAction` (`origin/main:src/deluge/gui/ui/slicer.cpp`): in MANUAL every pad is DEALT_WITH -- the
		// bottom half and the sidebar included.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		r.pad(3, 6, 64, 10); // bottom half: outside `y < kDisplayHeight / 2`
		r.settle(10);
		r.pad(kDisplayWidth, 0, 64, 20); // the sidebar
		r.settle(20);
		expect(static_cast<int32_t>(ops.auditions.size())).to_equal(0);
		expect(slicer_scene().current_slice()).to_equal(0);
	});

	// --- the buttons, against main's buttonAction ----------------------------------------------

	it("switches mode on the X encoder button, as main's buttonAction does", _ {
		// Main's X_ENC arm of `Slicer::buttonAction`.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		r.tick(0);
		r.x_enc(true, 10);
		r.settle(10);
		const int32_t after_one = static_cast<int32_t>(slicer_scene().mode());
		r.x_enc(false, 20);
		r.x_enc(true, 30);
		r.settle(30);
		const int32_t after_two = static_cast<int32_t>(slicer_scene().mode());
		// Main's own arm, from main's own state, as `slicer/x_enc modes` holds it. Its
		// `AudioEngine::stopAnyPreviewing()` and `getCurrentKit()->firstDrum->killAllVoices()` both need
		// a booted engine and a kit clip, so the row is the arm's mode arithmetic run exactly as main
		// writes it -- see `SliceOps` for why the two calls are behind the boundary rather than here.
		expect(std::vector<std::string>{std::to_string(after_one), std::to_string(after_two)})
		    .to_equal(frozen("x_enc", "modes"));
		// And the two audio calls the arm makes: `stop_previewing` only on the way INTO manual.
		expect(ops.stop_previewing_calls).to_equal(1);
		expect(ops.kill_voices_calls).to_equal(2);
	});

	it("pops up the transpose on the Y encoder button, with main's own text", _ {
		// Main's Y_ENC arm. The port's string, from the shell's popup service, is compared character
		// for character with the text main's arm asked its display for.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		r.turn_vertical(-7, 10);
		r.settle(10);
		r.y_enc(true, 20);
		r.settle(20);
		const std::string ported = r.popup();
		expect(ported).to_equal(std::string("Transpose: -7"));
		expect(ported).to_equal(table().value("slicer/y_enc_manual", "popup"));
	});

	it("says nothing on the Y encoder button in REGION mode, as main does", _ {
		// The mode half of main's Y_ENC condition.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		r.tick(0);
		r.y_enc(true, 10);
		r.settle(10);
		const std::string ported = r.popup();
		expect(ported).to_equal(std::string(""));
		expect(ported).to_equal(table().value("slicer/y_enc_region", "popup"));
	});

	it("deletes the slice under a held pad on SAVE, exactly as main does", _ {
		// Main's SAVE arm, over the whole 64-entry table. Main asks `matrixDriver.isPadPressed(xx, yy)`;
		// the port asks `UiContext::held_pads`, which is the same fact lowered by the shell, so each
		// side reads its own.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		const std::vector<int32_t> taps{100000, 200000, 300000};
		seed_port(taps);

		// Put the cursor on slice 2 (slot (2, 0)) and hold that pad, as main's run had it.
		r.pad(2, 0, 64, 10);
		r.settle(10);
		expect_tables_match(self, "save_held/before");

		r.save(true, 20);
		r.settle(20);
		expect_tables_match(self, "save_held/after");
		expect(slicer_scene().num_manual_slices()).to_equal(3);
	});

	it("ignores SAVE when the slice's own pad is not held, exactly as main does", _ {
		// The other side of main's held-pad test. **The braces matter:** the whole delete is inside
		// that test, and when it fails the arm falls past SELECT_ENC and BACK to its final `else`,
		// which answers `NOT_DEALT_WITH` (`slicer/save_released result`).
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		const std::vector<int32_t> taps{100000, 200000, 300000};
		seed_port(taps);
		r.pad(2, 0, 64, 10);
		r.settle(10);
		r.pad(2, 0, 0, 12); // released again -- nothing is held now
		r.settle(12);
		expect_tables_match(self, "save_released/before");

		r.save(true, 20);
		r.settle(20);
		expect(frozen_number("save_released", "result")).to_equal(static_cast<int64_t>(ActionResult::NOT_DEALT_WITH));
		expect_tables_match(self, "save_released/after");
		expect(slicer_scene().num_manual_slices()).to_equal(4);
	});

	it("floors the count at one and re-clamps the cursor when the last slice is deleted", _ {
		// Main's SAVE arm floors and re-clamps the way its `selectEncoderAction`'s MANUAL arm does.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		seed_port({100000});
		r.pad(1, 0, 64, 10);
		r.settle(10);
		expect_tables_match(self, "save_last/before");

		r.save(true, 20);
		r.settle(20);
		expect_tables_match(self, "save_last/after");
		expect(slicer_scene().num_manual_slices()).to_equal(1);
		expect(slicer_scene().current_slice()).to_equal(0);
	});

	// --- the commit -----------------------------------------------------------------------------

	it("begins a commit with the REGION count the select encoder left, and stays committing until the host ends it", _ {
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		r.tick(0);
		r.turn_select(4, 10);
		r.settle(10);
		r.select_enc(true, 20);
		r.settle(20);
		expect(static_cast<int32_t>(ops.commits.size())).to_equal(1);
		expect(ops.commit_at(0).is_manual).to_be_false();
		expect(ops.commit_at(0).slice_count).to_equal(20);
		expect(ops.kill_voices_calls).to_equal(0); // REGION kills no voices
		expect(slicer_scene().committing()).to_be_true();
		expect(r.on_stack()).to_be_true();
	});

	it("commits the manual slice table, killing the drum's voices first", _ {
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		seed_port({100000, 200000});
		r.select_enc(true, 20);
		r.settle(20);
		expect(static_cast<int32_t>(ops.commits.size())).to_equal(1);
		expect(ops.commit_at(0).is_manual).to_be_true();
		expect(ops.commit_at(0).slice_count).to_equal(3);
		expect(ops.kill_voices_calls).to_equal(1);
		// By value: the snapshot carries the table, not a pointer into it.
		expect(ops.commit_at(0).points[1].start_pos).to_equal(100000);
		expect(ops.commit_at(0).points[2].start_pos).to_equal(200000);
	});

	it("leaves the REGION count as it was after a manual commit", _ {
		// Main's MANUAL Select writes `numClips = numManualSlice`: not copied, recorded in DIFFERENCES.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		r.tick(0);
		r.turn_select(4, 10);
		r.settle(10);
		r.x_enc(true, 20);
		r.settle(20);
		seed_port({100000});
		r.select_enc(true, 30);
		r.settle(30);
		expect(ops.commit_at(0).slice_count).to_equal(2);
		expect(slicer_scene().num_clips()).to_equal(20);
	});

	it("removes itself on the after_render after a landed commit", _ {
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		r.tick(0);
		r.select_enc(true, 10);
		r.settle(10);
		slicer_scene().commit_done(true);
		expect(slicer_scene().next_timer()).to_equal(std::optional<uint32_t>{0});
		r.settle(20);
		expect(r.on_stack()).to_be_false();
		expect(slicer_scene().committing()).to_be_false();
	});

	it("stays open and takes input again after a failed commit", _ {
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		r.tick(0);
		r.select_enc(true, 10);
		r.settle(10);
		slicer_scene().commit_done(false);
		r.settle(20);
		expect(r.on_stack()).to_be_true();
		expect(slicer_scene().committing()).to_be_false();
		r.x_enc(true, 30);
		r.settle(30);
		expect(slicer_scene().mode()).to_equal(Mode::Manual);
	});

	it("refuses buttons and pads while a commit is out", _ {
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		seed_port({100000});
		r.select_enc(true, 10);
		r.settle(10);
		r.back(true, 20);
		r.settle(20);
		r.x_enc(true, 30);
		r.settle(30);
		r.pad(1, 0, 64, 40); // the second slot
		r.settle(40);
		expect(r.on_stack()).to_be_true();
		expect(slicer_scene().mode()).to_equal(Mode::Manual);
		expect(static_cast<int32_t>(ops.auditions.size())).to_equal(0);
		expect(slicer_scene().current_slice()).to_equal(0);
		expect(static_cast<int32_t>(ops.commits.size())).to_equal(1);
	});

	it("keeps the committed count when the select encoder turns while the commit is out", _ {
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		r.tick(0);
		r.select_enc(true, 10);
		r.settle(10);
		r.turn_select(3, 20);
		r.settle(20);
		expect(ops.commit_at(0).slice_count).to_equal(16);
		expect(static_cast<int32_t>(ops.commits.size())).to_equal(1);
	});

	it("ignores a commit_done with no commit out", _ {
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		r.tick(0);
		slicer_scene().commit_done(true);
		r.settle(10);
		expect(r.on_stack()).to_be_true();
	});

	it("does not commit when the host refuses", _ {
		Rig r;
		FakeOps ops;
		ops.accept_commit = false;
		slicer_scene().set_ops(&ops);
		r.open();
		r.tick(0);
		r.select_enc(true, 10);
		r.settle(10);
		expect(slicer_scene().committing()).to_be_false();
	});

	it("cancels the held audition on enter, X_ENC, Select and BACK", _ {
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		r.tick(0);
		expect(ops.cancel_audition_calls).to_equal(1); // the enter
		r.x_enc(true, 10);
		r.settle(10);
		expect(ops.cancel_audition_calls).to_equal(2);
		ops.accept_commit = false;
		r.select_enc(true, 20);
		r.settle(20);
		expect(ops.cancel_audition_calls).to_equal(3);
		r.back(true, 30);
		r.settle(30);
		expect(ops.cancel_audition_calls).to_equal(4);
	});

	it("draws the playhead on rows 7 to 4 at the playing column, in both modes", _ {
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		ops.next_play_position =
		    static_cast<int32_t>(slicer_scene().x_scroll() + 5 * slicer_scene().x_zoom());
		const RGB tick = deluge::ui::widgets::grid::tick_colour(deluge::ui::widgets::grid::TickColour::Normal, RGB{});
		uint32_t at = 0;
		for (const Mode mode : {Mode::Region, Mode::Manual}) {
			slicer_scene().set_mode(mode);
			r.settle(at);
			at += 10;
			const deluge::ui::gfx::grid::PadGrid& g = r.fake.grid().grid();
			for (int32_t y = 4; y < 8; y++) {
				expect(g.at(5, y)).to_equal(tick);
			}
			expect(g.at(5, 3)).not_().to_equal(tick);
		}
	});

	it("draws the playhead only with the flash cursor Slow and flashes it Fast, the setting the host answers", _ {
		// `PadLEDs::flashCursor`: Off draws none, Fast only flashes the pad (no pixel), Slow substitutes one.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		ops.next_play_position = static_cast<int32_t>(slicer_scene().x_scroll() + 5 * slicer_scene().x_zoom());
		const RGB tick = deluge::ui::widgets::grid::tick_colour(deluge::ui::widgets::grid::TickColour::Normal, RGB{});
		const uint8_t saved = PadLEDs::flashCursor;
		slicer_scene().set_mode(Mode::Region);
		// Each leg waits out the 15 ms playhead tick, so each reads a frame drawn under its own setting.
		uint32_t at = 0;
		for (const auto [setting, drawn] : {std::pair{uint8_t{FLASH_CURSOR_SLOW}, true},
		                                    std::pair{uint8_t{FLASH_CURSOR_FAST}, false},
		                                    std::pair{uint8_t{FLASH_CURSOR_SLOW}, true},
		                                    std::pair{uint8_t{FLASH_CURSOR_OFF}, false}}) {
			PadLEDs::flashCursor = setting;
			r.settle(at);
			at += 20;
			expect(r.fake.grid().grid().at(5, 7) == tick).to_equal(drawn);
		}
		// Fast: the PIC flashes rows 7 to 4 when the square moves, and paints nothing.
		PadLEDs::flashCursor = FLASH_CURSOR_FAST;
		ops.next_play_position = static_cast<int32_t>(slicer_scene().x_scroll() + 9 * slicer_scene().x_zoom());
		const size_t from = r.fake.flashes_sent.size();
		r.settle(at);
		std::vector<int32_t> rows;
		for (size_t i = from; i < r.fake.flashes_sent.size(); i++) {
			if (r.fake.flashes_sent[i].x == 9) {
				rows.push_back(r.fake.flashes_sent[i].y);
			}
		}
		expect(rows).to_equal(std::vector<int32_t>{4, 5, 6, 7});
		expect(r.fake.grid().grid().at(9, 7)).not_().to_equal(tick);
		PadLEDs::flashCursor = saved;
	});

	it("asks for a tick every 15 ms while something plays, and for none when nothing does", _ {
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		r.tick(0);
		expect(slicer_scene().next_timer().has_value()).to_be_false();
		ops.next_play_position = 1000;
		expect(slicer_scene().next_timer()).to_equal(std::optional<uint32_t>{15});
		r.settle(20);
		r.settle(40);
		// The voice ends: one more frame, the one that clears the playhead it left, and then none.
		ops.next_play_position.reset();
		expect(slicer_scene().next_timer().value_or(999)).to_equal(15u);
		r.settle(60);
		expect(slicer_scene().next_timer().value_or(999)).to_equal(999u);
	});

	// --- BACK -----------------------------------------------------------------------------------

	it("closes on BACK, as legacy's buttonAction does", _ {
		// `Slicer::buttonAction`'s BACK arm (`origin/main:src/deluge/gui/ui/slicer.cpp`). `close()` is
		// `Action::Kind::Remove` here, not `Pop` -- see the note at the definition.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		r.tick(0);
		expect(r.on_stack()).to_be_true();
		r.back(true, 10);
		r.settle(10);
		expect(r.on_stack()).to_be_false();
		// REGION mode restores nothing (`Slicer::buttonAction`'s BACK arm).
		expect(ops.restore_calls).to_equal(0);
		expect(ops.kill_voices_calls).to_equal(0);
	});

	it("puts the drum's range back to the whole sample before closing, in MANUAL mode", _ {
		// `Slicer::buttonAction`'s MANUAL arm (`origin/main:src/deluge/gui/ui/slicer.cpp`).
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		r.back(true, 10);
		r.settle(10);
		expect(ops.kill_voices_calls).to_equal(1);
		expect(ops.restore_calls).to_equal(1);
		expect(r.on_stack()).to_be_false();
	});

	it("does nothing on a button RELEASE, as legacy's !on guard does", _ {
		// The other half of `Slicer::buttonAction`'s `!on` guard (`origin/main:src/deluge/gui/ui/slicer.cpp`).
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		r.tick(0);
		r.back(false, 10);
		r.settle(10);
		r.x_enc(false, 20);
		r.settle(20);
		expect(r.on_stack()).to_be_true();
		expect(slicer_scene().mode()).to_equal(Mode::Region);
	});

	// --- goldens, both geometries -------------------------------------------------------------

	it("draws the REGION page on a 48-row panel", _ {
			Rig r;
			r.open();
			r.tick(0);
			expect_golden(self, "ui/scenes/slicer_region_48.pbm", r.fake.pbm(0));
		});
	it("draws the REGION page on a 64-row panel", _ {
			Rig r64{board_64(), profile_64()};
			r64.open();
			r64.tick(0);
			expect_golden(self, "ui/scenes/slicer_region_64.pbm", r64.fake.pbm(0));
		});
	it("draws the MANUAL page on a 48-row panel", _ {
			Rig r;
			r.open();
			slicer_scene().set_mode(Mode::Manual);
			r.tick(0);
			expect_golden(self, "ui/scenes/slicer_manual_48.pbm", r.fake.pbm(0));
		});
	it("draws the MANUAL page on a 64-row panel", _ {
			Rig r64{board_64(), profile_64()};
			r64.open();
			slicer_scene().set_mode(Mode::Manual);
			r64.tick(0);
			expect_golden(self, "ui/scenes/slicer_manual_64.pbm", r64.fake.pbm(0));
		});

	it("draws the REGION waveform on the pads", _ {
		Rig r;
		r.open();
		r.tick(0);
		expect_golden(self, "ui/scenes/slicer_region_pads.state", r.fake.state());
	});
	it("draws the MANUAL waveform and slot grid on the pads", _ {
		Rig r;
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		expect_golden(self, "ui/scenes/slicer_manual_pads.state", r.fake.state());
	});
});

describe slicer_scene_reference_rows("slicer_scene: the reference rows", $ {
	it("parses slicer_scene.tsv clean and asks every row of it", _ {
		spec::OracleTable& t = spec::OracleTable::named("slicer_scene");
		expect(t.problems().size()).to_equal(size_t{0});
		expect(t.size()).to_equal(size_t{123});
		expect(t.unasked()).to_equal(std::string{});
	});

	it("reads every pad image and panel frozen beside it", _ {
		expect(spec::OracleFiles::unread("slicer_")).to_equal(std::string{});
	});
});
// clang-format on
DELUGE_SPEC(slicer_scene_spec, slicer_scene_spec_body, slicer_scene_reference_rows)
