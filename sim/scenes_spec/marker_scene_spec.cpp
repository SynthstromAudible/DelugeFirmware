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
/// The "against the real legacy editor" group compares the port against **`SampleMarkerEditor`
/// itself**, driven from its own inputs. Each example runs the same gesture twice: once through the
/// shell into `MarkerScene`, then -- on a model `restore()`d to exactly what the port started from
/// -- through legacy's own public entry points (`selectEncoderAction`, `padAction`,
/// `horizontalEncoderAction`, `renderMainPads`, `renderOLED`). `open_legacy` calls
/// `waveformBasicNavigator.opened(&holder)`, which is `SampleMarkerEditor::opened`'s own line, so
/// even the scroll and zoom legacy renders from are ones IT derived; nothing the port computed
/// reaches the oracle. This replaced a `mirror_to_legacy` that handed legacy the port's own view
/// (E3 final review, B3).
///
/// The pad overlay in particular is compared against the real `renderColumn`/`renderMarkerInCol`,
/// reached through the public `renderMainPads` because both are private
/// (`sample_marker_editor.h:90-94`). That comparison used to be a transcription of those two bodies
/// living in `tests/spec/ui/markers_grid_spec.cpp`, whose host target cannot link `deluge_app`
/// (E3 final review, B2).
///
/// Two divergences are **measured** here rather than only written down: `renderMainPads`'
/// phase-2 suppression, which the scene deliberately does not follow (see `marker_scene.h`), and
/// legacy's zoom transition holding `UI_MODE_HORIZONTAL_ZOOM` across a second detent. Both are
/// asserted in both directions at their examples.
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
#include "gui/ui/ui.h"
#include "gui/waveform/waveform_basic_navigator.h"
#include "hid/button.h"
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
using deluge::ui::shell::SceneKind;
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

	/// @brief Tear the stack down, so every scene's `on_exit` always runs (E3 task 10).
	///
	/// `MarkerScene` is a process singleton (`shell/scene.h`'s `SceneState` cache): an example that
	/// left it -- or anything pushed above it -- on the stack would make the NEXT example's
	/// `on_enter` see stale state instead of a fresh one, the cross-contamination
	/// `recorder_scene_spec.cpp`'s `~Rig` was written to stop (a leaked recorder there made the next
	/// `on_enter` refuse outright and killed 13 of 16 examples on one mutant's first run). Popping
	/// unconditionally means every example starts from the same place whatever the one before it did.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}

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
		MenuContext c = menu::context_for(*song.song, song.clip, 0, false, false, false, /*kit_global_fx=*/false);
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
	/// @brief Press or release the horizontal encoder's own button, at matrix (0, 1)
	///        (`shell/profiles/deluge.cpp`). Legacy's `UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON`.
	void x_enc_button(bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 0, 1, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
	}
	/// @brief One detent of the (detented) horizontal encoder, index 1 (`shell/profiles/deluge.cpp`).
	void turn_horizontal(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, 1, 0, static_cast<int16_t>(detents * 2)}, at_ms);
	}

	std::string oled() { return fake.pbm(0); }
	std::string grid() { return fake.state(); }
};

/// @brief Everything about the holder either side can move, so a legacy run can start from exactly
///        the model the port started from rather than from the model the port left behind.
struct HolderState {
	uint64_t start{};
	uint64_t end{};
	uint32_t loop_start{};
	uint32_t loop_end{};
	bool locked{};
	int32_t view_scroll{};
	int32_t view_zoom{};
};

HolderState snapshot(Rig& r) {
	const SampleHolderForVoice& h = r.range->sampleHolder;
	return HolderState{h.startPos,           h.endPos,          h.loopStartPos, h.loopEndPos, h.loopLocked,
	                   h.waveformViewScroll, h.waveformViewZoom};
}

void restore(Rig& r, const HolderState& s) {
	SampleHolderForVoice& h = r.range->sampleHolder;
	h.startPos = s.start;
	h.endPos = s.end;
	h.loopStartPos = s.loop_start;
	h.loopEndPos = s.loop_end;
	h.loopLocked = s.locked;
	h.waveformViewScroll = s.view_scroll;
	h.waveformViewZoom = s.view_zoom;
}

/// @brief Open the LEGACY editor on the same model, letting it derive its OWN view.
///
/// Not a mirror of the port's scroll and zoom: `waveformBasicNavigator.opened(&holder)` is
/// `SampleMarkerEditor::opened`'s own line (`sample_marker_editor.cpp:119-128`), so the view legacy
/// navigates and renders from is the one IT computes from the holder. The caller `restore()`s the
/// holder first, so not even the remembered-view fields carry the port's answer across (E3 final
/// review, B1/B3: each side from its own sentinel).
void open_legacy(Rig& r, MarkerType marker) {
	soundEditor.currentMultiRange = r.range;
	soundEditor.currentSource = r.source;
	soundEditor.currentSourceIndex = 0;
	soundEditor.currentSound = r.sound;
	waveformBasicNavigator.sample = static_cast<Sample*>(r.range->sampleHolder.audioFile); // `:119`
	waveformBasicNavigator.opened(&r.range->sampleHolder);                                 // `:126`
	sampleMarkerEditor.markerType = marker;
	sampleMarkerEditor.blinkPhase = 0; // `:128`
	currentUIMode = UI_MODE_NONE;
}

/// @brief The pad image `SampleMarkerEditor::renderMainPads` writes, in legacy's own shape.
struct LegacyPads {
	RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
};

/// @brief Ask the REAL `SampleMarkerEditor::renderMainPads` (`sample_marker_editor.cpp:1362`) for
///        its pads, over the model legacy has just opened on.
///
/// This is the whole overlay -- `renderColumn` (`:1268`) and `renderMarkerInCol` (`:1058`), which
/// are private and so unreachable except through this public entry -- on top of
/// `waveformRenderer.renderFullScreen`. It replaces the transcription of those two bodies that used
/// to live in `tests/spec/ui/markers_grid_spec.cpp`, which the host target cannot link
/// `deluge_app` to reach (E3 final review, B2).
/// @param blink_phase `SampleMarkerEditor::blinkPhase`, which decides the suppression mask.
LegacyPads legacy_pads(int8_t blink_phase) {
	sampleMarkerEditor.blinkPhase = blink_phase;
	LegacyPads out;
	sampleMarkerEditor.renderMainPads(0xFFFFFFFFu, out.image, nullptr, true);
	return out;
}

/// @brief Where the port's main pads first differ from a legacy image, and how many differ.
///
/// Main columns only: the sidebar is `renderSidebar`'s, which this scene does not port (see the
/// class docstring), so legacy's two sidebar columns have no counterpart to compare.
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

/// @brief The 128x48 canvas `SampleMarkerEditor::renderOLED` (`sample_marker_editor.cpp:1098`)
///        would have shown, as a PBM.
std::string legacy_oled() {
	static deluge::hid::display::oled_canvas::Canvas canvas;
	canvas.clear();
	sampleMarkerEditor.renderOLED(canvas);
	return page_major_to_pbm(canvas.bytes().data(), 128, 48);
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
		const HolderState before = snapshot(r);
		r.open(MarkerType::START);
		r.tick(0);

		// The port's answer, from its own state.
		r.turn_select(1, 10);
		r.tick(10);
		r.turn_select(1, 20);
		r.tick(20);
		const uint64_t ported = r.range->sampleHolder.startPos;

		// Put the model back exactly as the port found it and drive LEGACY over it, letting the
		// legacy navigator derive its own scroll and zoom.
		restore(r, before);
		open_legacy(r, MarkerType::START);
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
		const HolderState before = snapshot(r);
		r.open(MarkerType::START);
		r.tick(0);
		r.turn_select(1, 10);
		r.tick(10);
		const uint64_t ported = r.range->sampleHolder.startPos;

		restore(r, before);
		open_legacy(r, MarkerType::START);
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
		const HolderState before = snapshot(r);
		r.open(MarkerType::START);
		r.tick(0);
		r.turn_select(1, 10);
		r.tick(10);
		const uint64_t ported = r.range->sampleHolder.startPos;

		restore(r, before);
		open_legacy(r, MarkerType::START);
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

	it("paints the blink: phases 0, 1 and 2 alike, the selected marker gone on phase 3", _ {
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

	// --- against the real legacy editor -------------------------------------------------------
	//
	// Every example below runs the same gesture twice: once through the shell into `MarkerScene`,
	// and once through `SampleMarkerEditor`'s own public entry points over a model `restore()`d to
	// what the port started from, with `waveformBasicNavigator.opened()` deriving legacy's view for
	// itself. Neither side reads anything the other computed.
	//
	// **Uncovered, and deliberately so.** `SampleMarkerEditor::renderSidebar` has no counterpart --
	// the scene does not port the sidebar (see the class docstring) -- and neither does
	// `verticalEncoderAction`, which falls through to the UI beneath. The audition-column and
	// mute-column arms of `padAction` (`sample_marker_editor.cpp:353-365`) reach
	// `instrumentClipView` and `exitUI()`, neither of which this layer performs. Those are recorded
	// as uncovered in `docs/superpowers/specs/2026-08-26-ui-migration-design.md` §10.6, not counted here.

	it("draws the same pads as SampleMarkerEditor::renderMainPads, four markers apart", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 6 * kZoomMax;
		r.range->sampleHolder.loopEndPos = 10 * kZoomMax;
		r.range->sampleHolder.endPos = 14 * kZoomMax;
		const HolderState before = snapshot(r);
		r.open(MarkerType::LOOP_START);
		r.tick(0);

		restore(r, before);
		open_legacy(r, MarkerType::LOOP_START);
		// `renderMainPads` is the only public way into `renderColumn` and `renderMarkerInCol`, which
		// are private (`sample_marker_editor.h:90-94`) -- and it draws the waveform underneath them
		// too, so this compares the blend rather than the overlay on black.
		expect_pads_match(self, r, legacy_pads(0));
	});

	it("stacks a shared column the same way SampleMarkerEditor::renderColumn does", _ {
		// All four markers land on column 4: START floors `pos / zoom`, the two end-ish markers floor
		// `(pos - 1) / zoom`, so 4z, 4z+1, 4z+2 and 4z+3 are one column between them. This is the
		// stacking arithmetic -- `numMarkers`, `baseIncrement`/`selectedIncrement`, and the mirror
		// about the middle row -- which a column with one marker on it cannot exercise at all.
		Rig r;
		r.range->sampleHolder.startPos = 4 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 4 * kZoomMax + 1;
		r.range->sampleHolder.loopEndPos = 4 * kZoomMax + 2;
		r.range->sampleHolder.endPos = 4 * kZoomMax + 3;
		const HolderState before = snapshot(r);
		r.open(MarkerType::LOOP_END);
		r.tick(0);

		restore(r, before);
		open_legacy(r, MarkerType::LOOP_END);
		expect_pads_match(self, r, legacy_pads(0));
	});

	it("gives the selected marker the wider slice when three share a column, as legacy does", _ {
		// `numMarkers == 3` is the ONE case where `baseIncrement` (1) and `selectedIncrement` (2)
		// differ (`sample_marker_editor.cpp:1310-1313`); with two or four sharing they are equal and
		// swapping them changes nothing. Without this example a port that swapped the two passes.
		Rig r;
		r.range->sampleHolder.startPos = 4 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 4 * kZoomMax + 1;
		r.range->sampleHolder.loopEndPos = 4 * kZoomMax + 2;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		const HolderState before = snapshot(r);
		r.open(MarkerType::LOOP_START);
		r.tick(0);

		restore(r, before);
		open_legacy(r, MarkerType::LOOP_START);
		expect_pads_match(self, r, legacy_pads(0));
	});

	it("suppresses the same markers as renderMainPads does at every blink phase", _ {
		// The suppression mask is `blinkPhase >= 1` and the phase-1 flood is `blinkPhase == 1`
		// (`sample_marker_editor.cpp:1376-1398`). With the selected marker SHARING a column the two
		// are distinguishable, which is exactly what the port-only blink example above says a
		// one-marker column cannot show.
		Rig r;
		r.range->sampleHolder.startPos = 4 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 4 * kZoomMax + 1;
		r.range->sampleHolder.loopEndPos = 9 * kZoomMax;
		r.range->sampleHolder.endPos = 13 * kZoomMax;
		const HolderState before = snapshot(r);
		r.open(MarkerType::LOOP_START);
		for (int32_t phase = 0; phase < 4; phase++) {
			r.tick(static_cast<uint32_t>(phase) * marker::kBlinkPhaseMs);
			expect(static_cast<int32_t>(marker_scene().blink_phase())).to_equal(phase);
			restore(r, before);
			open_legacy(r, MarkerType::LOOP_START);
			// **Phase 2 is the documented divergence, and this is where it is measured rather than
			// merely asserted.** Legacy renders the blink two ways: `timerCallback` (`:771`) draws
			// phases 0 and 2 normally, while `renderMainPads` (`:1362`) suppresses whenever
			// `blinkPhase >= 1`. The scene follows `timerCallback` on purpose (`marker_scene.h`), so
			// at phase 2 it must match `renderMainPads`' PHASE-0 output and must not match its
			// phase-2 output. Both halves are asserted, so a port that quietly started following
			// `renderMainPads` would fail here.
			expect_pads_match(self, r, legacy_pads(static_cast<int8_t>(phase == 2 ? 0 : phase)));
			if (phase == 2) {
				expect(pad_diff(r, legacy_pads(2)).count > 0).to_be_true();
			}
		}
	});

	it("moves the marker to the column legacy's padAction moves it to", _ {
		Rig r;
		const HolderState before = snapshot(r);
		r.open(MarkerType::START);
		r.tick(0);
		r.pad(6, 3, true, 10);
		r.tick(10);
		const uint64_t ported = r.range->sampleHolder.startPos;

		restore(r, before);
		open_legacy(r, MarkerType::START);
		sampleMarkerEditor.padAction(6, 3, 127);
		expect(static_cast<int32_t>(ported)).to_equal(static_cast<int32_t>(r.range->sampleHolder.startPos));
		expect(static_cast<int32_t>(ported)).to_equal(6 * kZoomMax);
	});

	it("selects the marker legacy's padAction selects, and holds it the same way", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 10 * kZoomMax;
		const HolderState before = snapshot(r);
		r.open(MarkerType::START);
		r.tick(0);
		r.pad(9, 3, true, 10); // column 9 is END's
		r.tick(10);
		const MarkerType ported = marker_scene().marker_type();
		const bool ported_holding = marker_scene().holding_marker();

		restore(r, before);
		open_legacy(r, MarkerType::START);
		sampleMarkerEditor.padAction(9, 3, 127);
		expect(static_cast<int32_t>(ported)).to_equal(static_cast<int32_t>(sampleMarkerEditor.markerType));
		expect(static_cast<int32_t>(ported)).to_equal(static_cast<int32_t>(MarkerType::END));
		// `UI_MODE_HOLDING_SAMPLE_MARKER` (`:536`) is what the scene's `holding_marker()` replaces.
		expect(ported_holding).to_equal(isUIModeActive(UI_MODE_HOLDING_SAMPLE_MARKER));
		expect(ported_holding).to_be_true();
		currentUIMode = UI_MODE_NONE;
	});

	it("adds the loop start where legacy's hold-and-tap adds it", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		const HolderState before = snapshot(r);
		r.open(MarkerType::START);
		r.tick(0);
		r.pad(2, 3, true, 10); // hold START's own column
		r.tick(10);
		r.pad(6, 3, true, 20); // tap column 6
		r.tick(20);
		const uint32_t ported_loop = r.range->sampleHolder.loopStartPos;
		const MarkerType ported_marker = marker_scene().marker_type();

		restore(r, before);
		open_legacy(r, MarkerType::START);
		sampleMarkerEditor.padAction(2, 3, 127);
		sampleMarkerEditor.padAction(6, 3, 127);
		expect(static_cast<int32_t>(ported_loop))
		    .to_equal(static_cast<int32_t>(r.range->sampleHolder.loopStartPos));
		expect(static_cast<int32_t>(ported_marker)).to_equal(static_cast<int32_t>(sampleMarkerEditor.markerType));
		expect(static_cast<int32_t>(ported_marker)).to_equal(static_cast<int32_t>(MarkerType::LOOP_START));
		currentUIMode = UI_MODE_NONE;
	});

	it("removes the loop start where legacy's hold-and-tap removes it", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 6 * kZoomMax;
		const HolderState before = snapshot(r);
		r.open(MarkerType::LOOP_START);
		r.tick(0);
		r.pad(6, 3, true, 10); // hold LOOP_START
		r.tick(10);
		r.pad(2, 3, true, 20); // tap START
		r.tick(20);
		const uint32_t ported_loop = r.range->sampleHolder.loopStartPos;
		const MarkerType ported_marker = marker_scene().marker_type();

		restore(r, before);
		open_legacy(r, MarkerType::LOOP_START);
		sampleMarkerEditor.padAction(6, 3, 127);
		sampleMarkerEditor.padAction(2, 3, 127);
		expect(static_cast<int32_t>(ported_loop))
		    .to_equal(static_cast<int32_t>(r.range->sampleHolder.loopStartPos));
		expect(static_cast<int32_t>(ported_loop)).to_equal(0);
		expect(static_cast<int32_t>(ported_marker)).to_equal(static_cast<int32_t>(sampleMarkerEditor.markerType));
		expect(static_cast<int32_t>(ported_marker)).to_equal(static_cast<int32_t>(MarkerType::START));
		currentUIMode = UI_MODE_NONE;
	});

	it("removes the loop end where legacy's hold-and-tap removes it", _ {
		// The LOOP_END arm (`sample_marker_editor.cpp:509-522`), which the LOOP_START one above does
		// not reach: a different marker held, a different column tapped, and a different write.
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 4 * kZoomMax;
		r.range->sampleHolder.loopEndPos = 8 * kZoomMax;
		const HolderState before = snapshot(r);
		r.open(MarkerType::LOOP_END);
		r.tick(0);
		r.pad(7, 3, true, 10); // hold LOOP_END (`(pos - 1) / zoom` floors to 7)
		r.tick(10);
		r.pad(11, 3, true, 20); // tap END's own column
		r.tick(20);
		const uint32_t ported_loop = r.range->sampleHolder.loopEndPos;
		const MarkerType ported_marker = marker_scene().marker_type();

		restore(r, before);
		open_legacy(r, MarkerType::LOOP_END);
		sampleMarkerEditor.padAction(7, 3, 127);
		sampleMarkerEditor.padAction(11, 3, 127);
		expect(static_cast<int32_t>(ported_loop)).to_equal(static_cast<int32_t>(r.range->sampleHolder.loopEndPos));
		expect(static_cast<int32_t>(ported_loop)).to_equal(0);
		expect(static_cast<int32_t>(ported_marker)).to_equal(static_cast<int32_t>(sampleMarkerEditor.markerType));
		expect(static_cast<int32_t>(ported_marker)).to_equal(static_cast<int32_t>(MarkerType::END));
		currentUIMode = UI_MODE_NONE;
	});

	it("toggles the loop lock where legacy's hold-and-tap toggles it", _ {
		// `markerHeld == LOOP_START && markerPressed == LOOP_END` (`:480-489`).
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 4 * kZoomMax;
		r.range->sampleHolder.loopEndPos = 8 * kZoomMax;
		const HolderState before = snapshot(r);
		r.open(MarkerType::LOOP_START);
		r.tick(0);
		r.pad(4, 3, true, 10); // hold LOOP_START
		r.tick(10);
		r.pad(7, 3, true, 20); // tap LOOP_END
		r.tick(20);
		const bool ported_locked = r.range->sampleHolder.loopLocked;

		restore(r, before);
		open_legacy(r, MarkerType::LOOP_START);
		sampleMarkerEditor.padAction(4, 3, 127);
		sampleMarkerEditor.padAction(7, 3, 127);
		expect(ported_locked).to_equal(r.range->sampleHolder.loopLocked);
		expect(ported_locked).to_be_true();
		currentUIMode = UI_MODE_NONE;
	});

	it("zooms to the same scroll and zoom legacy's horizontalEncoderAction reaches", _ {
		// Legacy zooms only while the horizontal encoder's BUTTON is held
		// (`UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON`, `sample_marker_editor.cpp:661-671`), and
		// both sides are driven through that gate rather than around it.
		// `WaveformBasicNavigator::zoom` runs `PadLEDs::renderZoom()` on success
		// (`waveform_basic_navigator.cpp:67`) -- the transition animation the port deliberately does
		// not carry -- and that pumps `AudioEngine::routineWithClusterLoading()`, which segfaults in
		// a spec process on the input ring. `QuietAudio` is the disarm (`support/card_access.h`);
		// without it this example does not fail, it takes the process down.
		spec::QuietAudio quiet;
		Rig r;
		r.range->sampleHolder.startPos = 4 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		const HolderState before = snapshot(r);
		r.open(MarkerType::START);
		r.tick(0);
		r.x_enc_button(true, 10);
		r.tick(10);
		r.turn_horizontal(1, 20);
		r.tick(20);
		const int32_t ported_zoom = static_cast<int32_t>(marker_scene().x_zoom());
		const int32_t ported_scroll = static_cast<int32_t>(marker_scene().x_scroll());

		restore(r, before);
		open_legacy(r, MarkerType::START);
		sampleMarkerEditor.buttonAction(deluge::hid::button::X_ENC, true, false);
		sampleMarkerEditor.horizontalEncoderAction(1);
		expect(ported_zoom).to_equal(static_cast<int32_t>(waveformBasicNavigator.xZoom));
		expect(ported_scroll).to_equal(static_cast<int32_t>(waveformBasicNavigator.xScroll));
		// ...and it really did zoom, so a port that refused the turn could not pass this.
		expect(ported_zoom).to_equal(kZoomMax / 2);

		// **One detent, and that is the ceiling, not an oversight.** A successful legacy zoom leaves
		// `currentUIMode == UI_MODE_HORIZONTAL_ZOOM` until the `PadLEDs` transition finishes, and
		// `horizontalEncoderAction`'s `isUIModeWithinRange(zoomUIModes)` gate then refuses the next
		// detent (`sample_marker_editor.cpp:745-752`). Nothing runs that animation to completion in
		// a spec process, so a second detent compares the port's second zoom against legacy's
		// refusal. Measured: port 8192, legacy 16384. The animation is the one part of
		// `WaveformBasicNavigator::zoom` the port deliberately does not carry
		// (`waveform_basic_navigator.h`), so this is the divergence working as recorded, not a bug.
		expect((currentUIMode & UI_MODE_HORIZONTAL_ZOOM) != 0).to_be_true();
		sampleMarkerEditor.buttonAction(deluge::hid::button::X_ENC, false, false);
		currentUIMode = UI_MODE_NONE;
	});

	it("scrolls to the same position legacy's horizontalEncoderAction scrolls to", _ {
		// The same entry point's OTHER arm: no button held, so `waveformBasicNavigator.scroll`.
		Rig r;
		r.range->sampleHolder.startPos = 0;
		r.range->sampleHolder.endPos = kSampleLength;
		r.range->sampleHolder.waveformViewZoom = kZoomMax / 4;
		r.range->sampleHolder.waveformViewScroll = 8 * (kZoomMax / 4);
		const HolderState before = snapshot(r);
		r.open(MarkerType::START);
		r.tick(0);
		r.turn_horizontal(1, 10);
		r.tick(10);
		const int32_t ported_scroll = static_cast<int32_t>(marker_scene().x_scroll());

		restore(r, before);
		open_legacy(r, MarkerType::START);
		sampleMarkerEditor.horizontalEncoderAction(1);
		expect(ported_scroll).to_equal(static_cast<int32_t>(waveformBasicNavigator.xScroll));
		expect(ported_scroll).to_equal(9 * (kZoomMax / 4));
	});

	it("draws the same OLED page as SampleMarkerEditor::renderOLED", _ {
		Rig r;
		r.range->sampleHolder.startPos = 3 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		const HolderState before = snapshot(r);
		r.open(MarkerType::START);
		r.tick(0);
		const std::string ported = r.oled();

		restore(r, before);
		open_legacy(r, MarkerType::START);
		// The full 128x48 panel, title rows included: `OLED_MAIN_TOPMOST_PIXEL` is 5
		// (`board_config.h:64`) and legacy offsets its own drawing by it, so the faceplate rows are
		// blank on both sides and there is nothing to normalise away.
		expect(ported).to_equal(legacy_oled());
	});

	it("draws the same OLED page for a locked loop, badge and all", _ {
		// The loop-lock badge and the position readout's other units -- `renderOLED`'s arms the
		// START page never reaches.
		Rig r;
		r.range->sampleHolder.startPos = 0;
		r.range->sampleHolder.endPos = 14 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 5 * kZoomMax;
		r.range->sampleHolder.loopEndPos = 9 * kZoomMax;
		r.range->sampleHolder.loopLocked = true;
		const HolderState before = snapshot(r);
		r.open(MarkerType::LOOP_END);
		r.tick(0);
		const std::string ported = r.oled();

		restore(r, before);
		open_legacy(r, MarkerType::LOOP_END);
		expect(ported).to_equal(legacy_oled());
	});

	it("labels a reversed sample's marker the same way reverseRemap does", _ {
		// `reverseRemap` (`sample_marker_editor.cpp:83-104`) is read by BOTH renderers; with the
		// sample reversed, START must draw legacy's "End point" page.
		Rig r;
		r.source->sampleControls.reversed = true;
		r.range->sampleHolder.startPos = 3 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		const HolderState before = snapshot(r);
		r.open(MarkerType::START);
		r.tick(0);
		const std::string ported = r.oled();

		restore(r, before);
		open_legacy(r, MarkerType::START);
		expect(ported).to_equal(legacy_oled());
		r.source->sampleControls.reversed = false;
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
