/// @file
/// @brief `SlicerScene` -- the port of `Slicer` -- driven through a real `Shell` over
///        `uitest::FakeSurfaceSet`, against a real `Sample` and against the real legacy `Slicer`.
///
/// @par The sample fixture, and why it never touches the card
///
/// Identical in shape and reason to `marker_scene_spec.cpp`'s: `deluge::sample::find_peaks_per_col`
/// reads clusters, and a spec process has no mounted card. The fixture builds a sample long enough
/// that every display column spans two or more clusters and pre-marks every overview entry
/// investigated, so every column is a cache hit and no I/O happens. Halving `kSampleLength` turns
/// the whole waveform black -- the scan only takes its cache arm at `numClustersSpan >= 2`.
///
/// @par The legacy oracle
///
/// Every comparison here runs the same gesture twice, each side from its own sentinel:
///
///   * the port, through the `Shell` -- real board events into `Shell::on_board_event`, rendered
///     into `FakeSurfaceSet`;
///   * legacy, through **`Slicer`'s own public entry points** -- `focusRegained`,
///     `selectEncoderAction`, `horizontalEncoderAction`, `verticalEncoderAction`, `renderOLED`,
///     `renderMainPads` -- over a `waveformBasicNavigator` that `open_legacy` sets up with the two
///     lines `SampleBrowser::renderPreviewForTarget` uses (`sample_browser.cpp:617-619`), so even the
///     scroll and zoom legacy renders from are ones IT derived.
///
/// Nothing the port computed is ever handed to legacy. The one thing both sides are *told* is the
/// mode, and each is told it by the spec: `legacy_slicer.slicerMode = SLICER_MODE_MANUAL` on one side,
/// `slicer_scene().set_mode(Mode::Manual)` on the other. That is the same shape
/// `marker_scene_spec`'s `open_legacy` uses for `sampleMarkerEditor.markerType`.
///
/// @par What is NOT covered here, and why
///
///   * **`Slicer::graphicsRoutine` (`slicer.cpp:156-203`), the playhead.** Its only interesting
///     branch needs `Sound::voices()` to report a live voice, which needs a running
///     `AudioEngine`; `support/song_fixture.h` builds a `Song` by direct construction and never
///     calls `AudioEngine::init()`, so the only branch a spec can reach is "no voices, no tick
///     square", which asserts nothing. The scene does not port it -- see `slicer_scene.h`.
///   * **`Slicer::padAction` and the rest of `Slicer::buttonAction`** -- Task 10's.
///   * **`Slicer::redraw` (`slicer.cpp:100-101`)**, which is empty: a 7SEG-only hook with no body.
///
/// @note The spec may include `gui/` (for the legacy slicer); the library may not. Same rule
///       `marker_scene_spec.cpp` runs under.

#include "scenes/profile.h"
#include "scenes/registry.h"
#include "scenes/slicing/slicer_scene.h"

#include "shell/profiles/deluge.h"
#include "shell/shell.h"
#include "ui/core/metrics.h"

#include "gui/ui/slicer.h"
#include "gui/ui/ui.h"
#include "gui/waveform/waveform_basic_navigator.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "storage/cluster/cluster.h"

#include "support/song_fixture.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with every local named `it` in the project
// headers this drags in.
#include "support/card_access.h"

#include "spec/gfx/support/golden.h"
#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <string>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::slicing::kSlicerSceneKind;
using deluge::scenes::slicing::Mode;
using deluge::scenes::slicing::slicer_scene;
using deluge::scenes::slicing::SlicerScene;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

// --- the sample -------------------------------------------------------------------------------

constexpr size_t kClusterSize = 32768;
constexpr int32_t kBytesPerFrame = 2; // 16-bit mono
/// Thirty-two clusters' worth of frames -- two clusters per display column at max zoom, which is
/// the number that makes the scan take its cached whole-cluster arm. See the file docstring.
constexpr uint64_t kSampleLength = static_cast<uint64_t>(kClusterSize) * 32 / kBytesPerFrame;
constexpr int32_t kNumClusters = 32;

int8_t cluster_peak(int32_t i) {
	return static_cast<int8_t>(16 + 3 * i);
}

Sample* make_sample() {
	Cluster::set_size(kClusterSize);

	// Placed in the deluge heap and never freed -- `song_fixture.h`'s own rule, and for its reason.
	void* memory = deluge::memory::alloc_fast(sizeof(Sample));
	auto* s = new (memory) Sample();
	s->byteDepth = 2;
	s->numChannels = 1;
	s->sampleRate = 44100;
	s->audioDataStartPosBytes = 0;
	s->audioDataLengthBytes = kSampleLength * kBytesPerFrame;
	s->lengthInSamples = kSampleLength;

	s->maxValueFound = 127 << 24;
	s->minValueFound = -128 << 24;

	s->resizeOverviewCache(kNumClusters);
	for (int32_t i = 0; i < kNumClusters; i++) {
		OverviewCacheEntry& e = s->overviewCacheEntry(static_cast<uint32_t>(i));
		e.min = static_cast<int8_t>(-cluster_peak(i));
		e.max = cluster_peak(i);
		e.investigated = true;
	}
	return s;
}

// --- the second geometry ----------------------------------------------------------------------

/// @brief A 128x64 single-panel board, so the same scene can be recorded at the other shipped panel
///        geometry. Legacy's own `renderOLED` cannot be run at it -- `OLED_MAIN_HEIGHT_PIXELS` is a
///        compile-time 48 -- so the 64-row coverage is a golden, exactly as `marker_scene_spec`'s is.
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

/// @brief The minimum `Display` legacy needs: `display` is null in a spec process
///        (`hid/display/display.cpp:4`) and both encoder arms call `popupTextTemporary`.
struct SlicerDisplay final : deluge::hid::Display {
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
	bool hasPopup() override { return false; }
	bool hasPopupOfType(PopupType) override { return false; }
	void consoleText(char const*) override {}
	void timerRoutine() override {}
};

// --- the port's rig ---------------------------------------------------------------------------

struct Rig {
	spec::SongFixture song = spec::make_synth_song();
	Sample* sample = make_sample();

	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell;

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// @brief Tear the stack down, so `SlicerScene` -- a process singleton -- never carries state
	///        into the next example. `recorder_scene_spec.cpp`'s `~Rig`, for its reason.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		Buttons::clearShiftSticky();
	}

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p), shell(fake, p, reg, deluge::ui::shell::Services{}, ShellConfig{}) {}

	/// @brief Open the slicer on the fixture's sample, the way `SampleBrowser` does.
	void open() {
		slicer_scene().open(sample);
		shell.init(SceneId{kSlicerSceneKind, 0});
	}

	void tick(uint32_t at_ms) { shell.tick(at_ms); }

	/// @brief One tick to deliver the turn, one to draw its consequence.
	///
	/// `Shell::tick` delivers encoder events at **step 8**, after step 5/6 have already built this
	/// tick's frames (`shell/shell.cpp:370-371`), and the `Repaint` the arm files is therefore for
	/// the tick after (`shell.cpp:379-386`). So a pixel comparison taken one tick after a turn shows
	/// the value from *before* it -- found by writing exactly that and watching the panel read 17
	/// where legacy read 20. Every gesture below therefore settles before anything is read.
	void settle(uint32_t at_ms) {
		shell.tick(at_ms);
		shell.tick(at_ms + 1);
	}

	/// @brief One detent of the (detented, 2 edges per click) select encoder, index 3
	///        (`shell/profiles/deluge.cpp`).
	///
	/// @warning **At most 63 detents per call.** `InputLowering::apply_encoder_edges` takes its
	///          edge count as an `int8_t` (`shell/input_lowering.cpp:165`), so 64 detents is 128
	///          edges and wraps negative -- 120 detents arrived as -16 and turned a count of 136
	///          into 0, which is how this was found. Legacy has no such limit
	///          (`selectEncoderAction(int8_t offset)` is handed whole detents), so every multi-turn
	///          gesture below is split into <= 63-detent turns on BOTH sides.
	void turn_select(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, 3, 0, static_cast<int16_t>(detents * 2)}, at_ms);
	}
	/// @brief One detent of the horizontal encoder, index 1.
	void turn_horizontal(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, 1, 0, static_cast<int16_t>(detents * 2)}, at_ms);
	}
	/// @brief One detent of the vertical encoder, index 0.
	void turn_vertical(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, 0, 0, static_cast<int16_t>(detents * 2)}, at_ms);
	}
	/// @brief Press or release SHIFT, at matrix (8, 0) (`shell/profiles/deluge.cpp:14`).
	void shift(bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 8, 0, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
	}

	std::string oled() { return fake.pbm(0); }
	std::string grid() { return fake.state(); }
};

// --- the legacy oracle ------------------------------------------------------------------------

/// @brief The legacy global (`Slicer slicer`, `gui/ui/slicer.cpp:55`), aliased so every oracle call
///        below says out loud which side it is driving.
Slicer& legacy_slicer = ::slicer;

/// @brief Open the LEGACY slicer on the same sample, letting it derive its OWN view and its own
///        state.
///
/// `waveformBasicNavigator.sample = sample; waveformBasicNavigator.opened();` is
/// `SampleBrowser::renderPreviewForTarget`'s own pair
/// (`gui/ui/browser/sample_browser.cpp:617-619`) -- the lines the preview path runs before the
/// slicer is opened over it -- and `focusRegained()` is what
/// `UI::opened()` then calls (`gui/ui/ui.h:111-113`). So legacy's scroll, zoom, mode, counts and
/// slice table are all ones IT computed, from the sample alone.
void open_legacy(Rig& r) {
	waveformBasicNavigator.sample = r.sample;
	waveformBasicNavigator.opened();
	legacy_slicer.focusRegained();
	currentUIMode = UI_MODE_NONE;
}

/// @brief The pad image `Slicer::renderMainPads` (`slicer.cpp:103-152`) writes, in legacy's shape.
struct LegacyPads {
	RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
};

LegacyPads legacy_pads() {
	LegacyPads out;
	legacy_slicer.renderMainPads(0xFFFFFFFFu, out.image, nullptr, true);
	return out;
}

/// @brief The 128x48 canvas `Slicer::renderOLED` (`slicer.cpp:74-98`) would have shown, as a PBM.
std::string legacy_oled() {
	static deluge::hid::display::oled_canvas::Canvas canvas;
	canvas.clear();
	legacy_slicer.renderOLED(canvas);
	return page_major_to_pbm(canvas.bytes().data(), 128, 48);
}

/// @brief Where the port's main pads first differ from a legacy image, and how many differ.
///
/// Main columns only. Legacy's `renderMainPads` never writes the two sidebar columns (its clear is
/// `kDisplayWidth * 3` bytes per row, `waveform_renderer.cpp:57-59`) and `Slicer` has no
/// `renderSidebar` at all, so the sidebar has no counterpart to compare -- exactly
/// `marker_scene_spec`'s situation.
struct PadDiff {
	int32_t count = 0;
	int32_t first_x = -1;
	int32_t first_y = -1;
};

PadDiff pad_diff(Rig& r, const LegacyPads& legacy) {
	const deluge::gfx::grid::PadGrid& g = r.fake.grid().grid();
	PadDiff d;
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (!(g.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y)) == legacy.image[y][x])) {
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

/// @brief Assert the port's main pads are the legacy image, pad for pad.
/// @param self The enclosing `it(...)` block's lambda parameter -- `expect` expands to `self.expect`.
template <typename Self>
void expect_pads_match(Self& self, Rig& r, const LegacyPads& legacy) {
	const PadDiff d = pad_diff(r, legacy);
	// The coordinates first, so a failure names a pad rather than only a count.
	expect(d.first_x).to_equal(-1);
	expect(d.first_y).to_equal(-1);
	expect(d.count).to_equal(0);
}

} // namespace

// clang-format off
describe slicer_scene_spec_body("scenes/slicer SlicerScene", $ {

	before_each([] {
		static SlicerDisplay d;
		if (display == nullptr) {
			display = &d;
		}
	});

	// --- opening ------------------------------------------------------------------------------

	it("opens exactly where focusRegained leaves legacy", _ {
		Rig r;
		r.open();
		r.tick(0);

		open_legacy(r);

		expect(static_cast<int32_t>(slicer_scene().mode())).to_equal(legacy_slicer.slicerMode);
		expect(slicer_scene().num_clips()).to_equal(static_cast<int32_t>(legacy_slicer.numClips));
		expect(slicer_scene().num_manual_slices()).to_equal(legacy_slicer.numManualSlice);
		expect(slicer_scene().current_slice()).to_equal(legacy_slicer.currentSlice);
		// `:68-71`: every one of the 64 slice slots reset, not just the first.
		for (int32_t i = 0; i < deluge::scenes::slicing::kMaxManualSlices; i++) {
			expect(slicer_scene().slice(i).start_pos).to_equal(legacy_slicer.manualSlicePoints[i].startPos);
			expect(slicer_scene().slice(i).transpose).to_equal(legacy_slicer.manualSlicePoints[i].transpose);
		}
	});

	it("opens zoomed all the way out, on the navigator the browser would have left", _ {
		Rig r;
		r.open();
		r.tick(0);

		open_legacy(r);
		expect(static_cast<int32_t>(slicer_scene().x_scroll())).to_equal(static_cast<int32_t>(waveformBasicNavigator.xScroll));
		expect(static_cast<int32_t>(slicer_scene().x_zoom())).to_equal(static_cast<int32_t>(waveformBasicNavigator.xZoom));
	});

	// --- the OLED page, against legacy --------------------------------------------------------

	it("draws the REGION page legacy's renderOLED draws", _ {
		Rig r;
		r.open();
		r.tick(0);
		const std::string ported = r.oled();

		open_legacy(r);
		expect(ported).to_equal(legacy_oled());
	});

	it("draws the MANUAL page legacy's renderOLED draws", _ {
		Rig r;
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		const std::string ported = r.oled();

		open_legacy(r);
		legacy_slicer.slicerMode = SLICER_MODE_MANUAL; // the spec's own sentinel, not the port's answer
		expect(ported).to_equal(legacy_oled());
	});

	// --- the select encoder, against legacy ---------------------------------------------------

	it("counts REGION clips the way legacy's selectEncoderAction counts them", _ {
		Rig r;
		r.open();
		r.tick(0);
		r.turn_select(1, 10);
		r.settle(10);
		r.turn_select(3, 20);
		r.settle(20);
		const int32_t ported = slicer_scene().num_clips();
		const std::string ported_oled = r.oled();

		open_legacy(r);
		legacy_slicer.selectEncoderAction(1);
		legacy_slicer.selectEncoderAction(3);
		expect(ported).to_equal(static_cast<int32_t>(legacy_slicer.numClips));
		expect(ported_oled).to_equal(legacy_oled());
	});

	it("wraps 257 back to 2, exactly as legacy does", _ {
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
		const std::string ported_oled = r.oled();

		open_legacy(r);
		for (int32_t i = 0; i < 4; i++) {
			legacy_slicer.selectEncoderAction(60);
		}
		legacy_slicer.selectEncoderAction(1);
		expect(ported).to_equal(2);
		expect(ported).to_equal(static_cast<int32_t>(legacy_slicer.numClips));
		expect(ported_oled).to_equal(legacy_oled());
	});

	it("wraps 1 back to 256, exactly as legacy does", _ {
		Rig r;
		r.open();
		r.tick(0);
		r.turn_select(-15, 10);
		r.settle(10);
		const int32_t ported = slicer_scene().num_clips();
		const std::string ported_oled = r.oled();

		open_legacy(r);
		legacy_slicer.selectEncoderAction(-15);
		expect(ported).to_equal(256);
		expect(ported).to_equal(static_cast<int32_t>(legacy_slicer.numClips));
		expect(ported_oled).to_equal(legacy_oled());
	});

	it("steps past the wrap on a multi-detent turn, exactly as legacy does", _ {
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

		open_legacy(r);
		for (int32_t i = 0; i < 4; i++) {
			legacy_slicer.selectEncoderAction(60);
		}
		legacy_slicer.selectEncoderAction(2);
		expect(ported).to_equal(258);
		expect(ported).to_equal(static_cast<int32_t>(legacy_slicer.numClips));
	});

	it("lets a fast turn drive the REGION count negative, exactly as legacy does", _ {
		// The floor wrap tests for equality with 1, so a turn that STEPS OVER 1 misses it: 16 - 20
		// is -4, and -4 is what both sides keep. Recorded in `src/scenes/README.md`'s defect ledger
		// as a legacy defect the port reproduces rather than fixes -- a one-sided fix would be a
		// silent divergence, which is what every other example here exists to catch.
		Rig r;
		r.open();
		r.tick(0);
		r.turn_select(-20, 10);
		r.settle(10);
		const int32_t ported = slicer_scene().num_clips();
		const std::string ported_oled = r.oled();

		open_legacy(r);
		legacy_slicer.selectEncoderAction(-20);
		expect(ported).to_equal(-4);
		expect(ported).to_equal(static_cast<int32_t>(legacy_slicer.numClips));
		// And the page shows it: `intToString` renders the sign, so the window reads "-4".
		expect(ported_oled).to_equal(legacy_oled());
	});

	it("only ever removes manual slices from the select encoder, as legacy does", _ {
		Rig r;
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		// A positive turn adds nothing (adding is `padAction`'s, Task 10) ...
		r.turn_select(4, 10);
		r.settle(10);
		const int32_t after_up = slicer_scene().num_manual_slices();
		// ... and a negative turn floors at one, resetting slot 0.
		r.turn_select(-4, 20);
		r.settle(20);
		const int32_t after_down = slicer_scene().num_manual_slices();
		const int32_t cursor = slicer_scene().current_slice();

		open_legacy(r);
		legacy_slicer.slicerMode = SLICER_MODE_MANUAL;
		legacy_slicer.selectEncoderAction(4);
		expect(after_up).to_equal(legacy_slicer.numManualSlice);
		legacy_slicer.selectEncoderAction(-4);
		expect(after_down).to_equal(legacy_slicer.numManualSlice);
		expect(cursor).to_equal(legacy_slicer.currentSlice);
	});

	// --- the horizontal encoder, against legacy -----------------------------------------------

	it("does nothing in REGION mode, as legacy's horizontalEncoderAction does", _ {
		Rig r;
		r.open();
		r.tick(0);
		r.turn_horizontal(5, 10);
		r.settle(10);
		const int32_t ported = slicer_scene().slice(0).start_pos;

		open_legacy(r);
		(void)legacy_slicer.horizontalEncoderAction(5);
		expect(ported).to_equal(0);
		expect(ported).to_equal(legacy_slicer.manualSlicePoints[0].startPos);
	});

	it("moves the current slice 100 samples per detent, as legacy does", _ {
		Rig r;
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		r.turn_horizontal(3, 10);
		r.settle(10);
		r.turn_horizontal(-1, 20);
		r.settle(20);
		const int32_t ported = slicer_scene().slice(0).start_pos;

		open_legacy(r);
		legacy_slicer.slicerMode = SLICER_MODE_MANUAL;
		(void)legacy_slicer.horizontalEncoderAction(3);
		(void)legacy_slicer.horizontalEncoderAction(-1);
		expect(ported).to_equal(200);
		expect(ported).to_equal(legacy_slicer.manualSlicePoints[0].startPos);
	});

	it("moves 10 samples per detent with shift held, as legacy does", _ {
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

		open_legacy(r);
		legacy_slicer.slicerMode = SLICER_MODE_MANUAL;
		// `Buttons::isShiftButtonPressed()` (`slicer.cpp:209`) reads `shiftCurrentlyPressed`, which
		// `Buttons::commandToggleShift` is the public way to set (`hid/buttons.cpp:236-245`).
		Buttons::commandToggleShift(true);
		(void)legacy_slicer.horizontalEncoderAction(3);
		Buttons::clearShiftSticky();
		expect(ported).to_equal(30);
		expect(ported).to_equal(legacy_slicer.manualSlicePoints[0].startPos);
	});

	it("clamps the start position to the sample, as legacy does", _ {
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

		open_legacy(r);
		legacy_slicer.slicerMode = SLICER_MODE_MANUAL;
		(void)legacy_slicer.horizontalEncoderAction(-5);
		expect(at_floor).to_equal(0);
		expect(at_floor).to_equal(legacy_slicer.manualSlicePoints[0].startPos);
		for (int32_t i = 0; i < 90; i++) {
			(void)legacy_slicer.horizontalEncoderAction(60);
		}
		expect(at_ceiling).to_equal(legacy_slicer.manualSlicePoints[0].startPos);
	});

	// --- the vertical encoder, against legacy -------------------------------------------------

	it("transposes the current slice and clamps at +-24, as legacy does", _ {
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

		open_legacy(r);
		legacy_slicer.slicerMode = SLICER_MODE_MANUAL;
		legacy_slicer.verticalEncoderAction(5, false);
		expect(after_up).to_equal(5);
		expect(after_up).to_equal(legacy_slicer.manualSlicePoints[0].transpose);
		legacy_slicer.verticalEncoderAction(60, false);
		expect(at_ceiling).to_equal(24);
		expect(at_ceiling).to_equal(legacy_slicer.manualSlicePoints[0].transpose);
		legacy_slicer.verticalEncoderAction(-60, false);
		legacy_slicer.verticalEncoderAction(-60, false);
		expect(at_floor).to_equal(-24);
		expect(at_floor).to_equal(legacy_slicer.manualSlicePoints[0].transpose);
	});

	it("does nothing in REGION mode, as legacy's verticalEncoderAction does", _ {
		Rig r;
		r.open();
		r.tick(0);
		r.turn_vertical(5, 10);
		r.settle(10);
		const int32_t ported = slicer_scene().slice(0).transpose;

		open_legacy(r);
		legacy_slicer.verticalEncoderAction(5, false);
		expect(ported).to_equal(0);
		expect(ported).to_equal(legacy_slicer.manualSlicePoints[0].transpose);
	});

	// --- the pads, against legacy -------------------------------------------------------------

	it("draws the REGION waveform legacy's renderMainPads draws", _ {
		Rig r;
		r.open();
		r.tick(0);

		open_legacy(r);
		expect_pads_match(self, r, legacy_pads());
	});

	it("draws the MANUAL squashed waveform, slice marks and slot grid legacy draws", _ {
		Rig r;
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);

		open_legacy(r);
		legacy_slicer.slicerMode = SLICER_MODE_MANUAL;
		expect_pads_match(self, r, legacy_pads());
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

		open_legacy(r);
		legacy_slicer.slicerMode = SLICER_MODE_MANUAL;
		for (int32_t i = 0; i < 8; i++) {
			(void)legacy_slicer.horizontalEncoderAction(50);
		}
		expect(legacy_slicer.manualSlicePoints[0].startPos).to_equal(40000);
		expect_pads_match(self, r, legacy_pads());
	});

	// --- goldens, both geometries -------------------------------------------------------------

	it("draws the REGION page on a 48-row panel", _ {
			Rig r;
			r.open();
			r.tick(0);
			expect_golden(self, "../ui/scenes/slicer_region_48.pbm", r.fake.pbm(0));
		});
	it("draws the REGION page on a 64-row panel", _ {
			Rig r64{board_64(), profile_64()};
			r64.open();
			r64.tick(0);
			expect_golden(self, "../ui/scenes/slicer_region_64.pbm", r64.fake.pbm(0));
		});
	it("draws the MANUAL page on a 48-row panel", _ {
			Rig r;
			r.open();
			slicer_scene().set_mode(Mode::Manual);
			r.tick(0);
			expect_golden(self, "../ui/scenes/slicer_manual_48.pbm", r.fake.pbm(0));
		});
	it("draws the MANUAL page on a 64-row panel", _ {
			Rig r64{board_64(), profile_64()};
			r64.open();
			slicer_scene().set_mode(Mode::Manual);
			r64.tick(0);
			expect_golden(self, "../ui/scenes/slicer_manual_64.pbm", r64.fake.pbm(0));
		});

	it("draws the REGION waveform on the pads", _ {
		Rig r;
		r.open();
		r.tick(0);
		expect_golden(self, "../ui/scenes/slicer_region_pads.state", r.fake.state());
	});
	it("draws the MANUAL waveform and slot grid on the pads", _ {
		Rig r;
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		expect_golden(self, "../ui/scenes/slicer_manual_pads.state", r.fake.state());
	});
});
// clang-format on
DELUGE_SPEC(slicer_scene_spec, slicer_scene_spec_body)
