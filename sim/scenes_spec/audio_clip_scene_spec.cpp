/// @file
/// @brief `AudioClipScene` -- the port of `AudioClipView`'s render and navigation half -- driven
///        through a real `Shell` over `uitest::FakeSurfaceSet`, against a real `AudioClip`, a real
///        `Sample`, and against the real legacy `AudioClipView`.
///
/// @par The legacy oracle
///
/// Every comparison here runs the same state twice, each side from its own sentinel:
///
///   * the port, through the `Shell` -- `audio_clip_scene().open(clip, song)` and then
///     `Shell::tick`, rendered into `FakeSurfaceSet`;
///   * legacy, through **`AudioClipView`'s own public entry points** --
///     `renderMainPads`, `renderSidebar`, `renderOLED`, `graphicsRoutine`, `padAction`,
///     `timerCallback`, `getMaxLength`, `getMaxZoom` -- over the same `Song`, whose
///     `xScroll`/`xZoom`/`loopLength` the spec sets and **both** sides then read for themselves.
///
/// Nothing the port computed is ever handed to legacy: the port reads scroll and zoom through
/// `TimelineState::adopt(song)` and legacy reads them through `currentSong->xScroll[NAVIGATION_CLIP]`,
/// so the two derive the same view independently from the one place the spec wrote.
///
/// @par The pad comparison composes legacy the way the DEVICE does, not the way `renderMainPads` does
///
/// `AudioClipView` paints the playhead nowhere near `renderMainPads`: `graphicsRoutine`
/// (`gui/views/audio_clip_view.cpp:262-310`) hands a column to `PadLEDs::setTickSquares`, and the
/// **driver** substitutes the cursor colour on its way out, in `PadLEDs::prepareColour`
/// (`hid/led/pad_leds.cpp:283-298`, called per pad from `sortLedsForCol`, `:260-275`). So the image
/// a pad actually shows is `prepareColour(x, y, image[y][x])`, and that -- not the bare
/// `renderMainPads` output -- is what the port's single immediate-mode `render_grid` is the port of.
/// `legacy_pads()` below therefore runs legacy's own `graphicsRoutine`, legacy's own
/// `renderMainPads`, legacy's own `renderSidebar` and legacy's own `prepareColour`, all four, and
/// composes nothing itself.
///
/// It renders into `PadLEDs::image` rather than a local buffer deliberately: `renderMainPads`'
/// scan-failure arm tests `image == PadLEDs::image` (`:151`) and takes a *different* branch for any
/// other buffer, so a local array would be comparing the port against a path the device never runs.
///
/// @par The sample fixture, and why it never touches the card
///
/// Identical in shape and reason to `marker_scene_spec.cpp`'s and `slicer_scene_spec.cpp`'s:
/// `deluge::sample::find_peaks_per_col` reads clusters, and a spec process has no mounted card. The
/// fixture builds a sample long enough that every display column spans two or more clusters and
/// pre-marks every overview entry investigated, so every column is a cache hit and no I/O happens.
///
/// @par What is NOT covered here, and why
///
///   * **The `UI_MODE_HOLDING_SONG_BUTTON` macro arm of `renderSidebar`** (`:247-249`). Not
///     ported -- see the `@warning` in `audio_clip_scene.h`. Its gate is set by
///     `AudioClipView::buttonAction`'s `SESSION_VIEW` arm, which is E4 task 12's citation, so there
///     is nothing in this half to drive it with either.
///   * **The four `PadLEDs` transition modes** (`UI_MODE_INSTRUMENT_CLIP_COLLAPSING`,
///     `UI_MODE_IMPLODE_ANIMATION`, `UI_MODE_EXPLODE_ANIMATION`, `UI_MODE_AUDIO_CLIP_COLLAPSING`).
///     The port carries no transitions at all, so those early returns have no counterpart to
///     compare.
///   * **`AudioClipView::renderOLED` at 64 rows.** `OLED_MAIN_HEIGHT_PIXELS` is a compile-time 48
///     (`deluge/definitions.h`), so legacy cannot be run at the other geometry; the 64-row coverage
///     is a golden, exactly as `marker_scene_spec`'s and `browser_scene_spec`'s are.
///
/// @note The spec may include `gui/` and `hid/` (for the legacy view and the pad driver); the
///       library may not. Same rule `marker_scene_spec.cpp` and `slicer_scene_spec.cpp` run under.

#include "scenes/audio_clip/audio_clip_scene.h"
#include "scenes/profile.h"
#include "scenes/registry.h"
#include "scenes/timeline/timeline.h"

#include "shell/profiles/deluge.h"
#include "shell/shell.h"
#include "ui/core/metrics.h"

#include "gui/ui/ui.h"
#include "gui/views/audio_clip_view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/pad_leds.h"
#include "model/clip/audio_clip.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/audio_output.h"
#include "storage/cluster/cluster.h"

#include "support/song_fixture.h"

#include "spec/gfx/support/golden.h"
#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

#include "spec/support/spec_main.h"

/// @brief `PlaybackHandler`'s "the swung-tick answer is zero" latch
///        (`playback/playback_handler.cpp:100`, read at
///        `PlaybackHandler::getNumSwungTicksInSinceLastActionedSwungTick`, `:1208-1217`).
///
/// Declared rather than included, exactly as `sim/scenes_spec/timeline_spec.cpp` declares it, and
/// for its reason: `Clip::getLivePos()` (`model/clip/clip.cpp:151-166`) adds
/// `getNumSwungTicksInSinceLastActionedSwungTick()` to `lastProcessedPos`, and on a song no
/// transport ever started that reaches `getActualSwungTickCount`'s external-clock swing branch and
/// divides by `(uint32_t)(50 + currentSong->swingAmount)` (`:1263`) -- a **SIGFPE**, not a failure.
/// Holding the latch takes legacy's own early return (`:1209-1214`), so `getLivePos()` is exactly
/// `lastProcessedPos`, which is what a spec that sets the play position by hand means.
extern bool currentlyActioningSwungTickOrResettingPlayPos;

using namespace deluge::scenes;
using deluge::scenes::audio_clip::audio_clip_scene;
using deluge::scenes::audio_clip::AudioClipScene;
using deluge::scenes::audio_clip::FlashCursor;
using deluge::scenes::audio_clip::kAudioClipSceneKind;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace spec_fixture = deluge::scenes::spec;

namespace {

// --- the sample -------------------------------------------------------------------------------

constexpr size_t kClusterSize = 32768;
constexpr int32_t kBytesPerFrame = 2; // 16-bit mono
/// Thirty-two clusters' worth of frames -- two clusters per display column at the fixture's zoom,
/// which is the number that makes the scan take its cached whole-cluster arm.
constexpr uint64_t kSampleLength = static_cast<uint64_t>(kClusterSize) * 32 / kBytesPerFrame;
constexpr int32_t kNumClusters = 32;

/// @brief A widening per-cluster peak, so no two columns of the waveform are the same height.
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

/// @brief A 128x64 single-panel board, so the same scene can be recorded at the other shipped
///        panel geometry. See the file docstring for why legacy cannot be run at it.
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
///        (`hid/display/display.cpp:4`) and several arms of `padAction` reach it.
struct QuietDisplay final : deluge::hid::Display {
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

void ensure_display() {
	if (display == nullptr) {
		display = new QuietDisplay;
	}
}

/// @brief The starting state BOTH sides read, and neither derives from the other.
///
/// @warning **Only some scroll/zoom pairs can be scanned in a spec process, and the ones that
///          cannot make this whole comparison VACUOUS.** `find_peaks_per_col` takes its cached
///          whole-cluster arm only when a display column spans two or more clusters
///          (`model/sample/waveform_scan.cpp:220-230`); on the one-cluster and part-cluster arms it
///          opens a `SampleFrameReader`, which fails with no card, sets `hadAnyTroubleLoading` and
///          makes the whole scan return **false**. Both sides then draw nothing -- the port because
///          `render_grid` returns, legacy because `renderMainPads`' `:151` arm returns -- and the
///          pad comparison passes on two black grids.
///
///          It was written that way first: `loop_length = 384` at `x_zoom = 48` puts a column
///          boundary 16 bytes from the end of cluster 31, which takes the two-cluster arm with
///          `investigatingAWholeCluster` left false, and the "grey region" example passed with an
///          entirely black grid. `expect_grid_has_content` below is the assertion that closes it,
///          and it is on **every** render example.
///
///          The constraint on this fixture: `x_zoom * (endPos - startPos) / loop_length` -- which
///          is `AudioClip::getSamplesFromTicks` (`model/clip/audio_clip.cpp:979-988`) -- must be a
///          multiple of 32768 frames (two whole clusters), and `x_scroll` must be a whole number of
///          those columns. Hence the zooms below are 48 and 96 and never 12 or 24.
struct Sentinel {
	int32_t x_scroll = 0;
	/// 48 ticks per column, which at `loop_length = 768` is 32768 frames -- exactly two clusters.
	uint32_t x_zoom = 48;
	int32_t loop_length = 768;
	/// The clip's play position, in ticks. Only read once playback is running.
	int32_t last_processed_pos = 0;
};

// --- the port's rig ---------------------------------------------------------------------------

struct Rig {
	spec_fixture::AudioSongFixture f = spec_fixture::make_audio_clip_song();
	Sample* sample = make_sample();

	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell;

	bool saved_swung_latch = currentlyActioningSwungTickOrResettingPlayPos;
	uint8_t saved_playback_state = playbackHandler.playbackState;
	uint8_t saved_flash_cursor = PadLEDs::flashCursor;

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p), shell(fake, p, reg, deluge::ui::shell::Services{}, ShellConfig{}) {
		ensure_display();
		currentUIMode = UI_MODE_NONE;
		// The clip's sample, attached the way `AudioClip::setupPlaybackBounds` leaves it: the whole
		// file, forwards.
		f.clip->sampleHolder.audioFile = sample;
		f.clip->sampleHolder.startPos = 0;
		f.clip->sampleHolder.endPos = static_cast<int64_t>(kSampleLength);
		f.clip->sampleControls.reversed = false;
		f.clip->recorder = nullptr;
		f.clip->renderData.xScroll = -1;
		f.clip->renderData.xZoom = -1;
		// `Clip::getLivePos()`'s SIGFPE guard -- see the extern's docstring.
		currentlyActioningSwungTickOrResettingPlayPos = true;
		// Legacy's `graphicsRoutine` gate and `prepareColour`'s substitution both read this; the
		// factory default is SLOW (`storage/flash_storage.cpp:287`).
		PadLEDs::flashCursor = FLASH_CURSOR_SLOW;
		PadLEDs::clearTickSquares(/*shouldSend=*/false);
		playbackHandler.playbackState = 0;
		currentPlaybackMode = &session;
	}

	/// @brief Tear the stack down, so `AudioClipScene` -- a process singleton -- never carries state
	///        into the next example. `slicer_scene_spec.cpp`'s `~Rig`, for its reason.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		audio_clip_scene().open(nullptr, nullptr);
		audio_clip_scene().set_flash_cursor(FlashCursor::Slow);
		PadLEDs::clearTickSquares(/*shouldSend=*/false);
		PadLEDs::flashCursor = saved_flash_cursor;
		playbackHandler.playbackState = saved_playback_state;
		currentlyActioningSwungTickOrResettingPlayPos = saved_swung_latch;
		currentUIMode = UI_MODE_NONE;
	}

	/// @brief Write @p s into the model. It is the ONE place either side's view comes from.
	void seed(const Sentinel& s) const {
		f.song->xScroll[NAVIGATION_CLIP] = s.x_scroll;
		f.song->xZoom[NAVIGATION_CLIP] = static_cast<uint32_t>(s.x_zoom);
		f.song->tripletsOn = false;
		f.clip->loopLength = s.loop_length;
		f.clip->originalLength = s.loop_length;
		f.clip->lastProcessedPos = s.last_processed_pos;
		f.clip->activeIfNoSolo = true;
		f.clip->soloingInSessionMode = false;
		f.clip->armState = ArmState::OFF;
		// Both caches invalidated, so each side's own scan runs from scratch.
		f.clip->renderData.xScroll = -1;
		f.clip->renderData.xZoom = -1;
	}

	/// @brief Open the port's scene on the fixture's clip.
	void open() {
		audio_clip_scene().open(f.clip, f.song);
		shell.init(SceneId{kAudioClipSceneKind, 0});
	}

	void tick(uint32_t at_ms) { shell.tick(at_ms); }

	AudioClipScene& scene() { return audio_clip_scene(); }

	std::string oled() { return fake.pbm(0); }
	std::string grid() { return fake.state(); }
};

// --- the legacy oracle ------------------------------------------------------------------------

/// @brief The legacy global (`AudioClipView audioClipView`, `gui/views/audio_clip_view.cpp:58`),
///        aliased so every oracle call below says out loud which side it is driving.
AudioClipView& legacy_view = ::audioClipView;

/// @brief One full pad image, sidebar included, as the DEVICE would show it.
struct LegacyPads {
	RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
};

/// @brief Drive legacy's own four functions and compose nothing. See the file docstring.
LegacyPads legacy_pads() {
	std::memset(PadLEDs::image, 0, sizeof(PadLEDs::image));
	legacy_view.graphicsRoutine();
	(void)legacy_view.renderMainPads(0xFFFFFFFFu, PadLEDs::image, nullptr, true);
	(void)legacy_view.renderSidebar(0xFFFFFFFFu, PadLEDs::image, nullptr);

	LegacyPads out;
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
			out.image[y][x] = PadLEDs::prepareColour(x, y, PadLEDs::image[y][x]);
		}
	}
	return out;
}

/// @brief The 128x48 canvas `AudioClipView::renderOLED` (`audio_clip_view.cpp:91-93`) would have
///        shown, as a PBM.
///
/// `renderOLED`'s one call, `view.displayOutputName`, ignores the canvas it is handed and writes
/// into `hid::display::OLED::main` (`gui/views/view.cpp:2088-2098`), so that is what is read back.
std::string legacy_oled() {
	deluge::hid::display::OLED::clearMainImage();
	legacy_view.renderOLED(deluge::hid::display::OLED::main);
	return page_major_to_pbm(deluge::hid::display::OLED::main.bytes().data(), 128, 48);
}

/// @brief Where the port's pads first differ from a legacy image, and how many differ.
///
/// **The whole grid, sidebar included** -- unlike `marker_scene_spec` and `slicer_scene_spec`,
/// whose legacy views have no `renderSidebar` at all. `AudioClipView` has one, so all 18 columns
/// have a counterpart.
struct PadDiff {
	int32_t count = 0;
	int32_t first_x = -1;
	int32_t first_y = -1;
};

PadDiff pad_diff(Rig& r, const LegacyPads& legacy) {
	const deluge::gfx::grid::PadGrid& g = r.fake.grid().grid();
	PadDiff d;
	for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
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

/// @brief Assert the port actually drew something, so a pad comparison of two black grids fails.
///
/// The `Sentinel` docstring has the failure this exists for. `at_least` is the number of non-black
/// pads the example's own geometry implies, asserted by identity of the count's floor rather than
/// "not zero", so a scan that half-fails is caught too.
/// @param self     The enclosing `it(...)` block's lambda parameter.
/// @param r        The rig.
/// @param at_least Minimum non-black pads.
template <typename Self>
void expect_grid_has_content(Self& self, Rig& r, int32_t at_least) {
	const deluge::gfx::grid::PadGrid& g = r.fake.grid().grid();
	int32_t lit = 0;
	for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			const RGB& c = g.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
			if (c.r != 0 || c.g != 0 || c.b != 0) {
				lit++;
			}
		}
	}
	expect(lit >= at_least).to_be_true();
}

/// @brief Assert the port's pads are the legacy image, pad for pad.
/// @param self The enclosing `it(...)` block's lambda parameter -- `expect` expands to `self.expect`.
template <typename Self>
void expect_pads_match(Self& self, Rig& r, const LegacyPads& legacy) {
	const PadDiff d = pad_diff(r, legacy);
	// The coordinates first, so a failure names a pad rather than only a count.
	expect(d.first_x).to_equal(-1);
	expect(d.first_y).to_equal(-1);
	expect(d.count).to_equal(0);
}

/// @brief Raise legacy's END marker through legacy's own front door, and prove it took.
///
/// `AudioClipView::endMarkerVisible` is **private** and the class is `final`, so there is no probe
/// subclass to reach it with (the trick `timeline_spec.cpp`'s `TickProbe` uses for
/// `ClipView::getTickSquare` needs a protected member). What there IS is `padAction`
/// (`audio_clip_view.cpp:548-556`), which sets it, and `getMaxLength` (`:857-862`), which is
/// **public** and returns `loopLength + 1` if and only if it is set. So the flag is raised by
/// legacy's own gesture and read back through legacy's own accessor; the caller asserts on the
/// return.
/// @param r The rig.
/// @return True if `endMarkerVisible` is now set.
bool raise_legacy_end_marker(Rig& r) {
	// `padAction`'s `x == endSquareDisplay` arm (`:550-556`). `endSquareDisplay` is computed by
	// legacy itself inside `padAction`; the column handed in is the one the SENTINEL implies, and
	// the return value is what says the two agreed.
	const int32_t end_square = divide_round_negative(r.f.clip->loopLength - r.f.song->xScroll[NAVIGATION_CLIP] - 1,
	                                                 static_cast<int32_t>(r.f.song->xZoom[NAVIGATION_CLIP]));
	(void)legacy_view.padAction(end_square, 0, 1);
	return legacy_view.getMaxLength() == static_cast<uint32_t>(r.f.clip->loopLength) + 1;
}

/// @brief Put legacy's END marker back down, through the same gesture.
void lower_legacy_end_marker(Rig& r) {
	const int32_t end_square = divide_round_negative(r.f.clip->loopLength - r.f.song->xScroll[NAVIGATION_CLIP] - 1,
	                                                 static_cast<int32_t>(r.f.song->xZoom[NAVIGATION_CLIP]));
	// `:510-516`: tapping the marker's own column toggles it off.
	(void)legacy_view.padAction(end_square, 0, 1);
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)
describe audio_clip_scene_spec_body(
    "scenes/audio_clip AudioClipScene", $ {
	    // --- the scene's identity and predicates ----------------------------------------------------

	    it(
	        "is registered at FirstApp + 9 and reports that kind", _ {
		        expect(static_cast<int32_t>(kAudioClipSceneKind))
		            .to_equal(static_cast<int32_t>(deluge::ui::shell::SceneKind::FirstApp) + 9);
		        expect(scene_registry().find(kAudioClipSceneKind) == &audio_clip_scene()).to_be_true();
		        expect(audio_clip_scene().kind() == kAudioClipSceneKind).to_be_true();
	        });

	    it(
	        "is the FIRST root scene in the registry", _ {
		        // `AudioClipView` -> ... -> `TimelineView : public RootUI`. Every other registered scene is
		        // a plain `UI` in legacy, so this is the only one that may claim it -- asserted over the
		        // whole table so a later scene claiming it wrongly shows up here.
		        const deluge::ui::shell::SceneRegistry reg = scene_registry();
		        int32_t roots = 0;
		        for (deluge::ui::shell::Scene* s : reg.by_kind) {
			        if (s != nullptr && s->is_root()) {
				        roots++;
				        expect(s->kind() == kAudioClipSceneKind).to_be_true();
			        }
		        }
		        expect(roots).to_equal(1);
	        });

	    it(
	        "is the grid base, occludes what is beneath, dims nothing and takes input", _ {
		        AudioClipScene& s = audio_clip_scene();
		        expect(s.is_grid_overlay()).to_be_false();
		        expect(s.occludes_grid()).to_be_true();
		        expect(s.dims_grid()).to_be_false();
		        expect(s.oled_transparent(deluge::ui::shell::SurfaceRole::Main)).to_be_false();
		        expect(s.input_transparent()).to_be_false();
		        // `greyout_mask()` is left at the base's all-zero: `getUIGreyoutColsAndRows`
		        // (`gui/ui/ui.cpp:73-83`) finds no `getGreyoutColsAndRows` anywhere on this chain.
		        expect(s.greyout_mask().rows).to_equal(0u);
		        expect(s.greyout_mask().cols).to_equal(0u);
	        });

	    // --- the navigation answers -----------------------------------------------------------------

	    it(
	        "answers getMaxLength and getMaxZoom exactly as legacy does, in both marker states", _ {
		        Rig r;
		        r.seed(Sentinel{});
		        r.open();
		        r.tick(0);

		        // Markers down on both sides.
		        expect(r.scene().max_length()).to_equal(legacy_view.getMaxLength());
		        expect(r.scene().max_zoom()).to_equal(legacy_view.getMaxZoom());
		        expect(r.scene().max_length()).to_equal(static_cast<uint32_t>(r.f.clip->loopLength));

		        // End marker up on both sides, each through its own front door.
		        expect(raise_legacy_end_marker(r)).to_be_true();
		        r.scene().set_end_marker_visible(true);
		        expect(r.scene().max_length()).to_equal(legacy_view.getMaxLength());
		        expect(r.scene().max_zoom()).to_equal(legacy_view.getMaxZoom());
		        expect(r.scene().max_length()).to_equal(static_cast<uint32_t>(r.f.clip->loopLength) + 1);
		        lower_legacy_end_marker(r);
	        });

	    it(
	        "answers scrollable_and_zoomable from the clip, as setupScroll's guard does", _ {
		        Rig r;
		        r.seed(Sentinel{});
		        r.open();
		        expect(r.scene().scrollable_and_zoomable()).to_equal(r.f.clip->currentlyScrollableAndZoomable());
		        expect(r.scene().scrollable_and_zoomable()).to_be_true();
	        });

	    it(
	        "adopts the song's scroll and zoom, with supportsTriplets false as AudioClipView has it", _ {
		        Rig r;
		        r.seed(Sentinel{.x_scroll = 96, .x_zoom = 24});
		        r.open();
		        expect(r.scene().timeline().x_scroll).to_equal(96);
		        expect(static_cast<int32_t>(r.scene().timeline().x_zoom)).to_equal(24);
		        // `AudioClipView::supportsTriplets()` returns false (`audio_clip_view.h:38`), and `adopt`
		        // must not overwrite it with the song's `tripletsOn`.
		        r.f.song->tripletsOn = true;
		        r.open();
		        expect(r.scene().timeline().supports_triplets).to_be_false();
		        expect(r.scene().timeline().in_triplets_view()).to_be_false();
		        r.f.song->tripletsOn = false;
	        });

	    // --- the render, against legacy, at both geometries ------------------------------------------

	    it(
	        "draws the same waveform and sidebar as legacy, at 48 rows", _ {
		        Rig r;
		        r.seed(Sentinel{});
		        r.open();
		        r.tick(0);
		        // 16 columns of waveform, so well over one bar per column even at the quietest.
		        expect_grid_has_content(self, r, 16);
		        expect_pads_match(self, r, legacy_pads());
	        });

	    it(
	        "draws the same pads as legacy at 64 rows", _ {
		        // The pad grid is 8 rows on both boards -- the 64 is the OLED panel's height -- so legacy's
		        // image is still the whole oracle here, and this example proves the port does not read the
		        // panel geometry while painting pads.
		        Rig r{board_64(), profile_64()};
		        r.seed(Sentinel{});
		        r.open();
		        r.tick(0);
		        expect_grid_has_content(self, r, 16);
		        expect_pads_match(self, r, legacy_pads());
	        });

	    it(
	        "draws the same waveform and grey region as legacy when the clip is scrolled", _ {
		        // Four columns' worth of scroll. The clip's end lands at column 11
		        // (`(768 - 192 - 1) / 48`), so columns 12..15 are the grey "past the end" region --
		        // the arm at `audio_clip_view.cpp:173-180` -- and columns 0..11 are the scrolled waveform.
		        Rig r;
		        r.seed(Sentinel{.x_scroll = 192});
		        r.open();
		        r.tick(0);
		        // The grey region alone is 4 columns x 8 rows.
		        expect_grid_has_content(self, r, 32 + 12);
		        expect_pads_match(self, r, legacy_pads());
	        });

	    it(
	        "draws the same waveform and grey region as legacy when the clip is zoomed out", _ {
		        // 96 ticks per column is 65536 frames, four whole clusters. The whole clip fits in eight
		        // columns, so 8..15 are grey.
		        Rig r;
		        r.seed(Sentinel{.x_zoom = 96});
		        r.open();
		        r.tick(0);
		        expect_grid_has_content(self, r, 64 + 8);
		        expect_pads_match(self, r, legacy_pads());
	        });

	    it(
	        "draws the same pads as legacy for a reversed sample", _ {
		        Rig r;
		        r.seed(Sentinel{});
		        r.f.clip->sampleControls.reversed = true;
		        r.open();
		        r.tick(0);
		        expect_grid_has_content(self, r, 16);
		        expect_pads_match(self, r, legacy_pads());
		        r.f.clip->sampleControls.reversed = false;
	        });

	    it(
	        "draws the same end-marker column as legacy, in both blink phases", _ {
		        Rig r;
		        r.seed(Sentinel{});
		        r.open();

		        // Both sides raise the marker through their own front door, BEFORE the first tick:
		        // `set_end_marker_visible` is a state write, not an event, so it dirties no repaint gate --
		        // legacy's own `uiNeedsRendering(this, 0xFFFFFFFF, 0)` (`audio_clip_view.cpp:555`) is the
		        // half of that arm the shell owns rather than the scene. Found by writing the tick after
		        // the setter and watching the port render the frame from before it.
		        // `raise_legacy_end_marker` asserts legacy's flag took, by reading legacy's own
		        // `getMaxLength`.
		        expect(raise_legacy_end_marker(r)).to_be_true();
		        r.scene().set_end_marker_visible(true);
		        // Both start on the bright half: legacy's `padAction` sets `blinkOn = true`
		        // (`audio_clip_view.cpp:553`), the port's setter does the same.
		        expect(r.scene().blink_on()).to_be_true();
		        r.tick(0);
		        expect_grid_has_content(self, r, 16);
		        expect_pads_match(self, r, legacy_pads());

		        // One blink phase later, both should be on the dim half -- legacy through its own
		        // `timerCallback` (`:223-229`), the port through the deadline `next_timer()` armed on the
		        // tick above, with no input at all.
		        (void)legacy_view.timerCallback();
		        r.tick(deluge::scenes::audio_clip::kMarkerBlinkMs);
		        expect(r.scene().blink_on()).to_be_false();
		        expect_pads_match(self, r, legacy_pads());

		        lower_legacy_end_marker(r);
	        });

	    // --- the playhead ---------------------------------------------------------------------------

	    it(
	        "draws no cursor when playback is stopped, exactly as legacy does not", _ {
		        Rig r;
		        r.seed(Sentinel{});
		        r.open();
		        r.tick(0);
		        // `graphicsRoutine`'s first arm (`audio_clip_view.cpp:269-273`) -> 255.
		        expect(r.scene().last_tick_square()).to_equal(255);
		        expect_grid_has_content(self, r, 16);
		        expect_pads_match(self, r, legacy_pads());
	        });

	    it(
	        "draws the play cursor in the same column as legacy, at three positions", _ {
		        Rig r;
		        playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		        playbackHandler.ticksLeftInCountIn = 0;
		        for (const int32_t pos : {0, 240, 720}) {
			        r.seed(Sentinel{.last_processed_pos = pos});
			        r.open();
			        r.tick(static_cast<uint32_t>(pos));
			        // The expected column, derived independently of BOTH sides from the sentinel: at scroll
			        // 0 and zoom 48, tick `pos` is column `pos / 48`.
			        expect(r.scene().last_tick_square()).to_equal(pos / 48);
			        // The cursor alone is a whole column of `kPlayheadColour`, on top of the waveform.
			        expect_grid_has_content(self, r, 16 + 8);
			        expect_pads_match(self, r, legacy_pads());
		        }
		        playbackHandler.playbackState = 0;
	        });

	    it(
	        "draws no cursor while a count-in is running, exactly as legacy does not", _ {
		        Rig r;
		        playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		        playbackHandler.ticksLeftInCountIn = 4;
		        r.seed(Sentinel{.last_processed_pos = 240});
		        r.open();
		        r.tick(0);
		        expect(r.scene().last_tick_square()).to_equal(255);
		        expect_pads_match(self, r, legacy_pads());
		        playbackHandler.ticksLeftInCountIn = 0;
		        playbackHandler.playbackState = 0;
	        });

	    it(
	        "draws no cursor when the clip is not active, exactly as legacy does not", _ {
		        Rig r;
		        playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		        r.seed(Sentinel{.last_processed_pos = 240});
		        r.f.clip->activeIfNoSolo = false;
		        r.open();
		        r.tick(0);
		        expect(r.scene().last_tick_square()).to_equal(255);
		        expect_pads_match(self, r, legacy_pads());
		        r.f.clip->activeIfNoSolo = true;
		        playbackHandler.playbackState = 0;
	        });

	    it(
	        "draws no cursor at all with the flash cursor OFF, on either side", _ {
		        Rig r;
		        playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		        r.seed(Sentinel{.last_processed_pos = 240});
		        r.open();
		        r.tick(0);
		        const int32_t with_cursor = r.scene().last_tick_square();
		        expect(with_cursor).to_equal(5);

		        // Legacy's setting and the port's copy, each set on its own side.
		        PadLEDs::flashCursor = FLASH_CURSOR_OFF;
		        PadLEDs::clearTickSquares(/*shouldSend=*/false);
		        r.scene().set_flash_cursor(FlashCursor::Off);
		        r.tick(1);
		        expect_pads_match(self, r, legacy_pads());
		        playbackHandler.playbackState = 0;
	        });

	    // --- timers ---------------------------------------------------------------------------------

	    it(
	        "asks for a repaint every tick while the cursor is live, and for nothing when it is not", _ {
		        Rig r;
		        r.seed(Sentinel{});
		        r.open();
		        r.tick(0);
		        // Stopped: no playhead, no marker, no timer at all.
		        expect(r.scene().next_timer().has_value()).to_be_false();

		        playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		        expect(r.scene().next_timer().has_value()).to_be_true();
		        expect(r.scene().next_timer().value()).to_equal(0u);
		        playbackHandler.playbackState = 0;

		        // A visible marker arms the blink instead, at the phase's own interval.
		        r.scene().set_end_marker_visible(true);
		        expect(r.scene().next_timer().value()).to_equal(deluge::scenes::audio_clip::kMarkerBlinkMs);
	        });

	    it(
	        "claims the grid and NOT the OLED, because nothing time-driven reaches the OLED page", _ {
		        // The mask is a claim about surfaces, and an over-claimed OLED is a permanent
		        // unconditional blit (`shell/scene.h`'s `timer_repaints`). Both halves asserted, and the
		        // second half is checked against what the page actually draws in the OLED group below --
		        // the output's name, which no timer here can change.
		        expect(audio_clip_scene().timer_repaints().grid).to_be_true();
		        expect(audio_clip_scene().timer_repaints().oled).to_be_false();
		        expect(audio_clip_scene().timer_repaints().any()).to_be_true();
	        });

	    it(
	        "advances the marker blink on quiet ticks alone, and stops when the marker goes down", _ {
		        Rig r;
		        r.seed(Sentinel{});
		        r.open();
		        r.tick(0);
		        r.scene().set_end_marker_visible(true);
		        expect(r.scene().blink_on()).to_be_true();

		        uint32_t t = 0;
		        for (const bool expected : {false, true, false, true}) {
			        t += deluge::scenes::audio_clip::kMarkerBlinkMs;
			        r.tick(t); // no input at all
			        expect(r.scene().blink_on()).to_equal(expected);
		        }

		        // Marker down: `timerCallback` is unset in legacy (`:514`), so the phase must freeze.
		        r.scene().set_end_marker_visible(false);
		        const bool frozen = r.scene().blink_on();
		        t += 10 * deluge::scenes::audio_clip::kMarkerBlinkMs;
		        r.tick(t);
		        expect(r.scene().blink_on()).to_equal(frozen);
		        expect(r.scene().next_timer().has_value()).to_be_false();
	        });

	    // --- the OLED page --------------------------------------------------------------------------

	    it(
	        "draws the same OLED page as legacy for a named output", _ {
		        Rig r;
		        r.f.output->name = "TAPE";
		        r.f.clip->name.clear();
		        r.f.clip->section = 2;
		        r.seed(Sentinel{});
		        r.open();
		        r.tick(0);
		        expect(r.oled()).to_equal(legacy_oled());
	        });

	    it(
	        "draws the same OLED page as legacy for a named output and a named clip", _ {
		        Rig r;
		        r.f.output->name = "TAPE";
		        r.f.clip->name = "VERSE";
		        r.f.clip->section = 0;
		        r.seed(Sentinel{});
		        r.open();
		        r.tick(0);
		        expect(r.oled()).to_equal(legacy_oled());
		        r.f.clip->name.clear();
	        });

	    it(
	        "draws the same OLED page as legacy for an unnamed output", _ {
		        Rig r;
		        r.f.output->name.clear();
		        r.seed(Sentinel{});
		        r.open();
		        r.tick(0);
		        expect(r.oled()).to_equal(legacy_oled());
	        });

	    it(
	        "draws the same OLED page as legacy in each of the three audio output modes", _ {
		        Rig r;
		        r.f.output->name = "TAPE";
		        r.f.clip->name.clear();
		        r.seed(Sentinel{});
		        for (const AudioOutputMode m :
		             {AudioOutputMode::player, AudioOutputMode::sampler, AudioOutputMode::looper}) {
			        r.f.output->mode = m;
			        r.open();
			        r.tick(0);
			        expect(r.oled()).to_equal(legacy_oled());
		        }
		        r.f.output->mode = AudioOutputMode::player;
	        });

	    // --- goldens --------------------------------------------------------------------------------

	    it(
	        "draws the output name and section on a 48-row panel", _ {
		        Rig r;
		        r.f.output->name = "TAPE";
		        r.f.clip->name.clear();
		        r.f.clip->section = 2;
		        r.seed(Sentinel{});
		        r.open();
		        r.tick(0);
		        expect_golden(self, "../ui/scenes/audio_clip_oled_48.pbm", r.oled());
	        });

	    it(
	        "draws the output name and section on a 64-row panel", _ {
		        Rig r{board_64(), profile_64()};
		        r.f.output->name = "TAPE";
		        r.f.clip->name.clear();
		        r.f.clip->section = 2;
		        r.seed(Sentinel{});
		        r.open();
		        r.tick(0);
		        expect_golden(self, "../ui/scenes/audio_clip_oled_64.pbm", r.oled());
	        });

	    it(
	        "draws the waveform, the grey region and the play cursor on the pads", _ {
		        Rig r;
		        playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		        // Zoomed out to 96 ticks per column: the clip fills columns 0..7, 8..15 are grey, and the
		        // cursor sits in column 2 (`240 / 96`).
		        r.seed(Sentinel{.x_zoom = 96, .last_processed_pos = 240});
		        r.open();
		        r.tick(0);
		        expect_grid_has_content(self, r, 64 + 8);
		        expect_golden(self, "../ui/scenes/audio_clip_pads.state", r.grid());
		        playbackHandler.playbackState = 0;
	        });
    });
// NOLINTEND(readability-function-cognitive-complexity)
// clang-format on
DELUGE_SPEC(audio_clip_scene_spec, audio_clip_scene_spec_body)
