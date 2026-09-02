/// @file
/// @brief `MarkerScene` -- the port of `SampleMarkerEditor` -- driven through a real `Shell` over
///        `uitest::FakeSurfaceSet`, against a real `Sample`.
///
/// @par The sample fixture, and why it never touches the card
///
/// `deluge::sample::find_peaks_per_col` reads clusters, and a spec process has no mounted card with
/// the file this `Sample` claims to be. It does **not** have to: the scan's whole-cluster arm
/// (`waveform_scan.cpp`, `investigatingAWholeCluster && sampleCluster->investigated`) reuses the
/// sample's own **overview cache** and never opens a reader at all. So the fixture builds a sample
/// long enough that every display column spans two or more clusters -- `kSampleLength` is exactly
/// `kDisplayWidth` clusters' worth of frames -- and pre-marks every overview entry investigated
/// with a chosen min/max. Every column is then a cache hit, the waveform is exactly the shape the
/// fixture asked for, and no I/O happens.
///
/// That is not a workaround for the harness: it is the same path a zoomed-out render takes on the
/// device once the background pre-scan (issue #4460) has run, which is the common case.
///
/// @par The legacy oracle
///
/// Four examples compare the port against **`SampleMarkerEditor` itself**, driven from its own
/// state: `waveformBasicNavigator` is pointed at the same `Sample`, `soundEditor`'s
/// `currentMultiRange`/`currentSource`/`currentSourceIndex` are set, and the legacy object is asked
/// for the same answer. Each mirrors the state across immediately before it asks, so no comparison
/// can read anything stale.
///
/// @note The spec may include `gui/` (for the legacy editor); the library may not. Same rule
///       `browser_scene_spec.cpp` runs under.

#include "scenes/marker/marker_scene.h"
#include "scenes/menu/context.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "shell/profiles/deluge.h"
#include "shell/shell.h"
#include "ui/core/metrics.h"

#include "gui/ui/sample_marker_editor.h"
#include "gui/waveform/waveform_basic_navigator.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/sample/sample.h"
#include "model/sample/sample_holder_for_voice.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/cluster/cluster.h"
#include "storage/multi_range/multisample_range.h"

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
using deluge::scenes::marker::kMarkerSceneKind;
using deluge::scenes::marker::marker_scene;
using deluge::scenes::marker::MarkerScene;
using deluge::scenes::menu::MenuContext;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

// --- geometry ---------------------------------------------------------------------------------

constexpr size_t kClusterSize = 32768;
constexpr int32_t kBytesPerFrame = 2; // 16-bit mono, so one frame is two bytes
/// Thirty-two clusters' worth of frames, which is **two clusters per display column at max zoom**
/// -- and two is the number that matters. The scan only takes its cache arm when a column spans
/// two or more clusters (`waveform_scan.cpp`'s `numClustersSpan >= 2`); at one cluster per column
/// it takes the two-cluster arm with `investigatingAWholeCluster` left false and opens a reader,
/// which in a spec process fails on every column. Halving this length is enough to turn the whole
/// waveform black -- found by doing exactly that.
constexpr uint64_t kSampleLength = static_cast<uint64_t>(kClusterSize) * 32 / kBytesPerFrame;
constexpr int32_t kNumClusters = 32;
/// `WaveformNav::max_zoom()` for `kSampleLength`: `((len - 1) >> 4) + 1`.
constexpr int32_t kZoomMax = static_cast<int32_t>(((kSampleLength - 1) >> 4) + 1);

/// The per-cluster peak the fixture writes into the overview cache: a widening shape, so a golden
/// shows something recognisable rather than a flat band.
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

	// The normalisation the bar heights are computed against
	// (`Sample::getFoundValueCentrePoint`/`getValueSpan`).
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

// --- the fixture ------------------------------------------------------------------------------

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

/// @brief The minimum `Display` the legacy editor needs. `display` is null in a spec process
///        (`hid/display/display.cpp:4`) and `SampleMarkerEditor::loopUnlock` pops up.
struct MarkerDisplay final : deluge::hid::Display {
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

/// @brief A synth song whose source 0 is a sample source with one multisample range.
struct Rig {
	spec::SongFixture song = spec::make_synth_song();
	Sample* sample = make_sample();
	Sound* sound = nullptr;
	Source* source = nullptr;
	MultisampleRange* range = nullptr;

	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell;

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p), shell(fake, p, reg, deluge::ui::shell::Services{}, ShellConfig{}) {
		sound = static_cast<Sound*>(static_cast<ModControllableAudio*>(song.instrument->toModControllable()));
		source = &sound->sources[0];
		source->oscType = OscType::SAMPLE;
		source->repeatMode = SampleRepeatMode::ONCE;
		source->sampleControls.reversed = false;
		range = static_cast<MultisampleRange*>(source->getOrCreateFirstRange());
		range->sampleHolder.audioFile = sample;
		range->sampleHolder.startPos = 0;
		range->sampleHolder.endPos = kSampleLength;
		range->sampleHolder.loopStartPos = 0;
		range->sampleHolder.loopEndPos = 0;
		range->sampleHolder.loopLocked = false;
		range->sampleHolder.waveformViewZoom = 0; // "no remembered view" -- open zoomed out
		range->sampleHolder.waveformViewScroll = 0;
	}

	MenuContext ctx() {
		MenuContext c = menu::context_for(*song.song, song.clip, 0, false, false, /*kit_global_fx=*/false);
		// `context_for` resolves the range only when the source has exactly one; it does here, so
		// this is an assertion about the fixture rather than an override.
		c.range = range;
		c.source = source;
		c.sample_controls = &source->sampleControls;
		return c;
	}

	/// @brief Open the editor on @p marker, the way `LoopPoint::beginSession` does.
	void open(MarkerType marker) {
		marker_scene().open(ctx());
		shell.init(SceneId{kMarkerSceneKind, static_cast<uint16_t>(static_cast<int16_t>(marker))});
	}

	void tick(uint32_t at_ms) { shell.tick(at_ms); }

	/// @brief One detent of the (detented, 2 edges per click) select encoder, index 3
	///        (`shell/profiles/deluge.cpp`).
	void turn_select(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, 3, 0, static_cast<int16_t>(detents * 2)}, at_ms);
	}
	void pad(int32_t x, int32_t y, bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, static_cast<uint8_t>(x), static_cast<uint8_t>(y),
		                                      static_cast<int16_t>(on ? 255 : 0)},
		                     at_ms);
	}

	std::string oled() { return fake.pbm(0); }
	std::string grid() { return fake.state(); }
};

/// @brief Point the LEGACY editor at the same sample and range, so it can be asked the same
///        question. Mirrored immediately before each comparison, never once at the top.
void mirror_to_legacy(Rig& r, MarkerType marker, int64_t x_scroll, int64_t x_zoom) {
	soundEditor.currentMultiRange = r.range;
	soundEditor.currentSource = r.source;
	soundEditor.currentSourceIndex = 0;
	soundEditor.currentSound = r.sound;
	waveformBasicNavigator.sample = r.sample;
	waveformBasicNavigator.xScroll = x_scroll;
	waveformBasicNavigator.xZoom = x_zoom;
	sampleMarkerEditor.markerType = marker;
}

} // namespace

// clang-format off
describe marker_scene_spec_body("scenes/marker MarkerScene", $ {

	before_each([] {
		static MarkerDisplay d;
		if (display == nullptr) {
			display = &d;
		}
	});

	// --- opening ------------------------------------------------------------------------------

	it("opens zoomed all the way out, on the marker SceneId::arg names", _ {
		Rig r;
		r.open(MarkerType::END);
		r.tick(0);
		expect(static_cast<int32_t>(marker_scene().marker_type())).to_equal(static_cast<int32_t>(MarkerType::END));
		// `WaveformNav::opened` with no remembered view: scroll 0, zoom = max_zoom().
		expect(static_cast<int32_t>(marker_scene().x_scroll())).to_equal(0);
		expect(static_cast<int32_t>(marker_scene().x_zoom())).to_equal(kZoomMax);
	});

	it("adopts the holder's remembered scroll and zoom when it has one", _ {
		Rig r;
		r.range->sampleHolder.waveformViewZoom = kZoomMax / 2;
		r.range->sampleHolder.waveformViewScroll = kZoomMax * 2;
		r.open(MarkerType::START);
		r.tick(0);
		expect(static_cast<int32_t>(marker_scene().x_zoom())).to_equal(kZoomMax / 2);
		expect(static_cast<int32_t>(marker_scene().x_scroll())).to_equal(kZoomMax * 2);
	});

	it("refuses to open with no sample on the holder", _ {
		Rig r;
		r.range->sampleHolder.audioFile = nullptr;
		expect(MarkerScene::can(r.ctx())).to_be_false();
		r.open(MarkerType::START);
		r.tick(0);
		bool on_stack = false;
		for (const deluge::ui::shell::Scene* s : r.shell.stack().scenes()) {
			on_stack = on_stack || s->kind() == kMarkerSceneKind;
		}
		expect(on_stack).to_be_false();
		r.range->sampleHolder.audioFile = r.sample;
	});

	// --- the select encoder, against legacy ---------------------------------------------------

	it("moves the START marker one column per detent and writes it through", _ {
		Rig r;
		r.open(MarkerType::START);
		r.tick(0);
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos)).to_equal(0);

		r.turn_select(1, 10);
		r.tick(10);
		// One column right at max zoom is one column's worth of samples.
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos)).to_equal(kZoomMax);

		r.turn_select(2, 20);
		r.tick(20);
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos)).to_equal(3 * kZoomMax);
	});

	it("writes the same position legacy's selectEncoderAction writes, for the same turns", _ {
		Rig r;
		r.open(MarkerType::START);
		r.tick(0);

		// The port's answer, from its own state.
		r.turn_select(1, 10);
		r.tick(10);
		r.turn_select(1, 20);
		r.tick(20);
		const uint64_t ported = r.range->sampleHolder.startPos;

		// Reset the model and drive LEGACY over the same fixture, from its own state, mirrored
		// immediately before the turns.
		r.range->sampleHolder.startPos = 0;
		mirror_to_legacy(r, MarkerType::START, 0, kZoomMax);
		sampleMarkerEditor.selectEncoderAction(1);
		sampleMarkerEditor.selectEncoderAction(1);
		const uint64_t legacy = r.range->sampleHolder.startPos;

		expect(static_cast<int32_t>(ported)).to_equal(static_cast<int32_t>(legacy));
		expect(static_cast<int32_t>(ported)).to_equal(2 * kZoomMax);
	});

	it("refuses a turn that would drive one marker into another, exactly as legacy does", _ {
		Rig r;
		// START at column 4, END at column 5 (`endPos - 1` floors there): one detent right would
		// land START on END's column, and legacy's "don't drive one marker into the other" loop
		// (`sample_marker_editor.cpp:281-288`) refuses it outright.
		r.range->sampleHolder.startPos = 4 * kZoomMax;
		r.range->sampleHolder.endPos = 6 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		r.turn_select(1, 10);
		r.tick(10);
		const uint64_t ported = r.range->sampleHolder.startPos;

		r.range->sampleHolder.startPos = 4 * kZoomMax;
		mirror_to_legacy(r, MarkerType::START, 0, kZoomMax);
		sampleMarkerEditor.selectEncoderAction(1);
		expect(static_cast<int32_t>(ported)).to_equal(static_cast<int32_t>(r.range->sampleHolder.startPos));
		expect(static_cast<int32_t>(ported)).to_equal(4 * kZoomMax);
	});

	it("allows the same turn once the other marker is a column further away", _ {
		// The other half of the refusal above: without this, an implementation that simply never
		// moved START would pass the previous example.
		Rig r;
		r.range->sampleHolder.startPos = 4 * kZoomMax;
		r.range->sampleHolder.endPos = 8 * kZoomMax; // END now at column 7
		r.open(MarkerType::START);
		r.tick(0);
		r.turn_select(1, 10);
		r.tick(10);
		const uint64_t ported = r.range->sampleHolder.startPos;

		r.range->sampleHolder.startPos = 4 * kZoomMax;
		mirror_to_legacy(r, MarkerType::START, 0, kZoomMax);
		sampleMarkerEditor.selectEncoderAction(1);
		expect(static_cast<int32_t>(ported)).to_equal(static_cast<int32_t>(r.range->sampleHolder.startPos));
		expect(static_cast<int32_t>(ported)).to_equal(5 * kZoomMax);
	});

	it("clamps the END marker at the sample's length", _ {
		Rig r;
		r.range->sampleHolder.endPos = kSampleLength;
		r.open(MarkerType::END);
		r.tick(0);
		r.turn_select(1, 10);
		r.tick(10);
		// `repeatMode` is ONCE and the sample is not reversed, so `shouldAllowExtraScrollRight` is
		// false and the clamp applies.
		expect(static_cast<int32_t>(r.range->sampleHolder.endPos)).to_equal(static_cast<int32_t>(kSampleLength));
	});

	// --- pads ---------------------------------------------------------------------------------

	it("moves the selected marker to a tapped empty column", _ {
		Rig r;
		r.open(MarkerType::START);
		r.tick(0);
		r.pad(6, 3, true, 10);
		r.tick(10);
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos)).to_equal(6 * kZoomMax);
	});

	it("selects a marker rather than moving one when its own column is tapped, and holds it", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 10 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		// Column 9 is END's (endPos - 1 floors there).
		r.pad(9, 3, true, 10);
		r.tick(10);
		expect(static_cast<int32_t>(marker_scene().marker_type())).to_equal(static_cast<int32_t>(MarkerType::END));
		expect(marker_scene().holding_marker()).to_be_true();
		// Nothing moved.
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos)).to_equal(2 * kZoomMax);
		expect(static_cast<int32_t>(r.range->sampleHolder.endPos)).to_equal(10 * kZoomMax);
		r.pad(9, 3, false, 20);
		r.tick(20);
		expect(marker_scene().holding_marker()).to_be_false();
	});

	it("adds a loop start by holding START and tapping to its right", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		r.pad(2, 3, true, 10); // hold START (its column)
		r.tick(10);
		expect(marker_scene().holding_marker()).to_be_true();
		r.pad(6, 3, true, 20); // tap column 6
		r.tick(20);
		expect(static_cast<int32_t>(r.range->sampleHolder.loopStartPos)).to_equal(6 * kZoomMax);
		expect(static_cast<int32_t>(marker_scene().marker_type()))
		    .to_equal(static_cast<int32_t>(MarkerType::LOOP_START));
	});

	it("removes the loop start by holding it and tapping START", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 6 * kZoomMax;
		r.open(MarkerType::LOOP_START);
		r.tick(0);
		r.pad(6, 3, true, 10); // hold LOOP_START
		r.tick(10);
		r.pad(2, 3, true, 20); // tap START
		r.tick(20);
		expect(static_cast<int32_t>(r.range->sampleHolder.loopStartPos)).to_equal(0);
		expect(static_cast<int32_t>(marker_scene().marker_type())).to_equal(static_cast<int32_t>(MarkerType::START));
	});

	// --- the blink ----------------------------------------------------------------------------

	it("asks for a tick every blink period while the selected marker is on screen", _ {
		Rig r;
		r.open(MarkerType::START);
		r.tick(0);
		expect(marker_scene().next_timer().has_value()).to_be_true();
		expect(static_cast<int32_t>(marker_scene().next_timer().value_or(0)))
		    .to_equal(static_cast<int32_t>(marker::kBlinkPhaseMs));
		expect(static_cast<int32_t>(marker_scene().blink_phase())).to_equal(0);

		r.tick(marker::kBlinkPhaseMs);
		expect(static_cast<int32_t>(marker_scene().blink_phase())).to_equal(1);
		r.tick(2 * marker::kBlinkPhaseMs);
		expect(static_cast<int32_t>(marker_scene().blink_phase())).to_equal(2);
		r.tick(4 * marker::kBlinkPhaseMs);
		expect(static_cast<int32_t>(marker_scene().blink_phase())).to_equal(0);
	});

	it("asks for no tick at all when the selected marker is scrolled off screen", _ {
		// `timerCallback`'s early return (`sample_marker_editor.cpp:797-800`). The witness is the
		// TIMER, not the phase: a scene that kept asking would keep the whole shell awake for a
		// blink nobody can see.
		Rig r;
		r.range->sampleHolder.startPos = 0;
		r.range->sampleHolder.endPos = kSampleLength;
		// A remembered view zoomed right in, a long way to the right of the START marker: at zoom
		// 1024 and scroll 100000, START (sample 0) is at column -98.
		r.range->sampleHolder.waveformViewZoom = 1024;
		r.range->sampleHolder.waveformViewScroll = 100000;
		r.open(MarkerType::START);
		r.tick(0);
		expect(static_cast<int32_t>(marker_scene().x_scroll())).to_equal(100000);
		expect(marker_scene().next_timer().has_value()).to_be_false();

		// ...and the phase stays put across a tick that would otherwise have advanced it.
		r.tick(marker::kBlinkPhaseMs * 3);
		expect(static_cast<int32_t>(marker_scene().blink_phase())).to_equal(0);
	});

	it("paints the blink: a solid column on phase 1, the marker gone on phase 3", _ {
		// The visible half of the blink, and the half `timerCallback` (`sample_marker_editor.cpp:806-862`)
		// disagrees with `renderMainPads` about -- see `marker_scene.h`. Phases 0 and 2 must look the
		// same as each other; 1 and 3 must each look different from them and from one another.
		Rig r;
		r.range->sampleHolder.startPos = 4 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		const std::string phase0 = r.fake.state();

		r.tick(marker::kBlinkPhaseMs);
		expect(static_cast<int32_t>(marker_scene().blink_phase())).to_equal(1);
		const std::string phase1 = r.fake.state();

		r.tick(2 * marker::kBlinkPhaseMs);
		expect(static_cast<int32_t>(marker_scene().blink_phase())).to_equal(2);
		const std::string phase2 = r.fake.state();

		r.tick(3 * marker::kBlinkPhaseMs);
		expect(static_cast<int32_t>(marker_scene().blink_phase())).to_equal(3);
		const std::string phase3 = r.fake.state();

		expect(phase0).to_equal(phase2); // phases 0 and 2 both draw normally
		// **Phase 1 is indistinguishable from phase 0 here, and that is legacy's behaviour, not a
		// missing arm.** Phase 1 floods the selected column with the marker colour
		// (`renderMarkerInCol(x, image, markerType, 0, kDisplayHeight, false)`, `:815`); a column
		// with exactly one marker on it is *already* a full-height undimmed bar, because
		// `renderColumn`'s one-marker case takes `yStart = kDisplayHeight / 2` and mirrors it and
		// the selected marker is never dimmed. The flood only shows when the selected marker is
		// SHARING its column, which no legal marker layout on this fixture does.
		expect(phase0).to_equal(phase1);
		expect(phase0 == phase3).to_be_false(); // phase 3 removes the selected marker
		expect(phase1 == phase3).to_be_false();

		// Phase 3 leaves the END marker (which is not the selected one) exactly where phase 0 had
		// it: only the SELECTED marker is suppressed.
		expect(phase3.find("pad 11 0 ") != std::string::npos)
		    .to_equal(phase0.find("pad 11 0 ") != std::string::npos);
	});

	// --- the OLED, against legacy -------------------------------------------------------------

	it("draws the marker's own title, and the loop-lock badge only when the loop is locked", _ {
		Rig r;
		r.range->sampleHolder.startPos = 3 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		const std::string unlocked = r.oled();

		r.range->sampleHolder.loopStartPos = 5 * kZoomMax;
		r.range->sampleHolder.loopEndPos = 9 * kZoomMax;
		r.range->sampleHolder.loopLocked = true;
		r.open(MarkerType::LOOP_START);
		r.tick(10);
		const std::string locked = r.oled();
		expect(unlocked == locked).to_be_false();
	});

	it("labels a reversed sample's START marker 'End point', as reverseRemap does", _ {
		Rig r;
		r.range->sampleHolder.startPos = 3 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		const std::string forwards = r.oled();

		r.source->sampleControls.reversed = true;
		r.open(MarkerType::START);
		r.tick(10);
		const std::string reversed = r.oled();
		expect(forwards == reversed).to_be_false();

		// ...and it is the same picture the END marker draws forwards, which is the actual claim.
		r.source->sampleControls.reversed = false;
		r.range->sampleHolder.endPos = 3 * kZoomMax;
		r.open(MarkerType::END);
		r.tick(20);
		expect(reversed).to_equal(r.oled());
	});

	// --- goldens ------------------------------------------------------------------------------

	it("draws the start point on a 48-row panel", _ {
			Rig r;
			r.range->sampleHolder.startPos = 3 * kZoomMax;
			r.open(MarkerType::START);
			r.tick(0);
			expect_golden(self, "../ui/scenes/marker_start_48.pbm", r.fake.pbm(0));
		});
	it("draws the start point on a 64-row panel", _ {
			Rig r64{board_64(), profile_64()};
			r64.range->sampleHolder.startPos = 3 * kZoomMax;
			r64.open(MarkerType::START);
			r64.tick(0);
			expect_golden(self, "../ui/scenes/marker_start_64.pbm", r64.fake.pbm(0));
		});
		it("draws a locked loop end, badge and all, on a 48-row panel", _ {
			Rig r;
			r.range->sampleHolder.startPos = 0;
			r.range->sampleHolder.loopStartPos = 5 * kZoomMax;
			r.range->sampleHolder.loopEndPos = 9 * kZoomMax;
			r.range->sampleHolder.loopLocked = true;
			r.open(MarkerType::LOOP_END);
			r.tick(0);
			expect_golden(self, "../ui/scenes/marker_loop_locked_48.pbm", r.fake.pbm(0));
		});
		it("draws a locked loop end, badge and all, on a 64-row panel", _ {
			Rig r64{board_64(), profile_64()};
			r64.range->sampleHolder.startPos = 0;
			r64.range->sampleHolder.loopStartPos = 5 * kZoomMax;
			r64.range->sampleHolder.loopEndPos = 9 * kZoomMax;
			r64.range->sampleHolder.loopLocked = true;
			r64.open(MarkerType::LOOP_END);
			r64.tick(0);
			expect_golden(self, "../ui/scenes/marker_loop_locked_64.pbm", r64.fake.pbm(0));
		});

	it("draws the waveform and every marker on the pads", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 6 * kZoomMax;
		r.range->sampleHolder.loopEndPos = 10 * kZoomMax;
		r.range->sampleHolder.endPos = 14 * kZoomMax;
		r.open(MarkerType::LOOP_START);
		r.tick(0);
		expect_golden(self, "../ui/scenes/marker_pads.state", r.fake.state());
	});
});
// clang-format on
DELUGE_SPEC(marker_scene_spec, marker_scene_spec_body)
