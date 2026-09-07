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

#include "notify/dirty_set.h"
#include "shell/input_lowering.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"
#include "ui/core/metrics.h"

#include "board_config.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/language.h"
#include "gui/ui/ui.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/clip_navigation_timeline_view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/pad_leds.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/sample/sample.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/audio_output.h"
#include "storage/cluster/cluster.h"
#include "storage/flash_storage.h"

#include "support/card_access.h" // before <cppspec.hpp> -- see its @warning
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

/// @name Legacy's own held-button state, reached by bare `extern`
///
/// The arms under test branch on `Buttons::isButtonPressed(...)` and `isShiftButtonPressed()`, whose
/// backing store (`hid/buttons.cpp:73`, `:52`, `:56`) is namespace-scope and not in the header.
/// **`Buttons::buttonAction` cannot be used to set it in a spec process**: its very first act after
/// recording the press is `getCurrentUI()->buttonAction(...)` (`hid/buttons.cpp:134`), and
/// `getCurrentUI()` is null here (`numUIsOpen == 0` -> `lastUIBeforeNullifying`,
/// `gui/ui/ui.cpp:136-141`). Found by segfault, not by reading.
/// @{
namespace Buttons {
extern bool buttonStates[NUM_BUTTON_COLS + 1][NUM_BUTTON_ROWS];
extern bool shiftCurrentlyPressed;
extern bool shiftCurrentlyStuck;
} // namespace Buttons
/// @}

using namespace deluge::scenes;
using deluge::scenes::audio_clip::audio_clip_scene;
using deluge::scenes::audio_clip::AudioClipScene;
using deluge::scenes::audio_clip::FlashCursor;
using deluge::scenes::audio_clip::kAudioClipSceneKind;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::InputLowering;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;
using ShellAction = deluge::ui::shell::Action;

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
	// `l10n::chosenLanguage` is null until the app constructs its `Display` (`gui/l10n/l10n.cpp:6`),
	// and legacy's clip-clear arm calls `deluge::l10n::get(...)` unconditionally
	// (`audio_clip_view.cpp:438`, `:441`) -- which dereferences it. Every other scenes spec that
	// drives a legacy popup sets it the same way (`context_menu_scene_spec.cpp:91`).
	if (deluge::l10n::chosenLanguage == nullptr) {
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
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
///          entirely black grid.
///
///          **`PadExpectation` below is what closes it, and it closes it structurally:**
///          `expect_pads_match` takes one as a required argument and applies it before it compares
///          anything, so there is no spelling of "compare these two grids" that does not also say
///          what makes the comparison non-vacuous. Nothing here relies on a caller remembering to
///          precede the comparison with a guard.
///
///          It took two rounds to get there, and both are worth knowing because the second is the
///          first one level up. Fix round 1: the guard existed but was a separate call, and the
///          docstring claimed it was on every render example while four of the twelve comparisons
///          had none -- the blink's second phase and the three "no cursor is drawn" examples, which
///          are exactly the shape where both sides legitimately go black. Review demonstrated it
///          rather than inferring it: forcing `x_zoom = 24` on the count-in example gave 27/27
///          passing on two black grids. Fix round 2: the four were fixed, but the claim was then
///          "every caller does X" backed by a one-off script -- an absolute claim with a
///          convention behind it, in a file **task 12 extends with more pad comparisons**. The next
///          unguarded comparison would have been written by someone reading a docstring saying the
///          invariant already held. Hence the argument.
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
		// `SampleHolder::setAudioFile` is what normally computes this (`sample_holder.cpp:110`); the
		// fixture attaches the sample by hand, so it is set by hand too. `kMaxSampleValue` is the
		// identity: `getLengthInSamplesAtSystemSampleRate` returns the raw duration for it and
		// divides by it otherwise (`sample_holder.cpp:94-101`) -- a **SIGFPE** at zero, which is what
		// `setClipLengthEqualToSampleLength` walks into (`audio_clip_view.cpp:745`).
		f.clip->sampleHolder.neutralPhaseIncrement = kMaxSampleValue;
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

	/// @brief Put the model back to EXACTLY what @p s describes -- the clip's length, the sample's
	///        two markers and the undo log -- so a write comparison can run one start state twice.
	///
	/// @warning **This, and not the pad comparison, is what makes the edit examples non-vacuous.**
	///          A write comparison that seeds once, runs legacy, then runs the port over legacy's
	///          own result and compares "the model" to itself passes whatever the port does --
	///          including nothing at all. That is the defect plan E2 shipped and only a
	///          delete-the-write check caught (`docs/.../ui-migration-design.md` §10.6). Every
	///          example below therefore: seeds, runs legacy, snapshots, **re-seeds**, runs the
	///          port, snapshots, and asserts BOTH that the two snapshots agree AND that each
	///          differs from the seed. The second half is the one that fails when a write is
	///          deleted.
	///
	/// @note `actionLogger.deleteAllLogs()` then one throwaway `getNewAction` is deliberate and is
	///       itself a finding: `ClipView::shortenClip` opens with an **unguarded**
	///       `actionLogger.firstAction[BEFORE]->type` (`gui/views/clip_view.cpp:129`), as does
	///       `lengthenClip` at `:98`, so driving legacy's length edits with an empty undo log
	///       segfaults rather than failing. One inert `MISC` action puts both sides in the state
	///       the device is always in by the time a length edit runs, identically, before either
	///       side moves. `ActionType::MISC` is chosen because neither `lengthenClip`'s `undoing`
	///       test (which wants `CLIP_LENGTH_DECREASE`) nor either `PATTERN_PASTE` test matches it.
	void reseed(const Sentinel& s) const {
		seed(s);
		f.clip->sampleHolder.startPos = 0;
		f.clip->sampleHolder.endPos = static_cast<int64_t>(kSampleLength);
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		// A realistic tempo, which `Song::setupDefault()` does not leave behind: a fresh song's
		// `timePerTimerTickBig` makes `getTimePerTimerTickFloat()` answer 2^32
		// (`model/song/song.cpp:3213-3215`), and `SampleHolder::getLoopLengthAtSystemSampleRate`
		// divides by it (`model/sample/sample_holder.cpp:105-111`) -- so
		// `setClipLengthEqualToSampleLength` computed a length of **zero** on BOTH sides, and the
		// comparison passed while nothing happened. Caught by `expect_edit_matches`' "each side
		// differs from the seed" half, which is the whole reason that half is there.
		//
		// 120 BPM, through `Song::setBPMInner`'s own arithmetic (`song.cpp:3072-3081`) rather than
		// by calling it: `setTempoFromNumSamples` reaches `playbackHandler`, and this process has
		// no transport.
		const double bpm = 120.0 * static_cast<double>(1u << f.song->insideWorldTickMagnitude);
		f.song->timePerTimerTickBig = static_cast<uint64_t>((110250.0 / bpm) * 4294967296.0);
	}

	/// @brief Open the port's scene on the fixture's clip.
	void open() {
		audio_clip_scene().open(f.clip, f.song);
		shell.init(SceneId{kAudioClipSceneKind, 0});
	}

	/// @brief Point the port's scene at the clip and run its entry hook, WITHOUT standing a shell
	///        up over it.
	///
	/// The input examples drive `Scene::handle` directly (see `Input` below) and compare the model,
	/// so there is no frame to render and no gate to arm; `open()`'s `shell.init` would only add a
	/// scene push to tear down again. **`on_enter` is still called**, because that is where the port
	/// puts `AudioClipView::opened`'s `focusRegained()` (`audio_clip_view.cpp:74`, `:80-81`) and
	/// therefore where the two marker latches are reset -- and `AudioClipScene` is a process
	/// singleton, so an example that leaves a marker up otherwise poisons the next one. Found
	/// exactly that way: the end-marker drag example left `end_marker_visible_` true and the
	/// start-marker example then took the wrong branch of `padAction`.
	void open_scene_only() {
		audio_clip_scene().open(f.clip, f.song);
		(void)audio_clip_scene().on_enter(SceneId{kAudioClipSceneKind, 0});
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

/// @brief The colour the pad driver substitutes for a `Normal` tick square.
///
/// `PadLEDs::flashColours[0]` (`hid/led/pad_leds.cpp:277-281`), which is `ui::grid::tick_colour`'s
/// `kPlayheadColour`. No waveform bar can take it: the bars are a scaling of the clip's colour,
/// whose green channel is 26, and grey is `RGB::monochrome(7)`.
constexpr RGB kPlayheadColour{130, 120, 130};

/// @brief How many pads the port painted in the play cursor's colour.
/// @param r The rig.
/// @return The count, over the whole grid including the sidebar.
int32_t cursor_pads(Rig& r) {
	const deluge::gfx::grid::PadGrid& g = r.fake.grid().grid();
	int32_t n = 0;
	for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (g.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y)) == kPlayheadColour) {
				n++;
			}
		}
	}
	return n;
}

/// @brief What makes one pad comparison non-vacuous. A **required argument** to
///        `expect_pads_match`, not a convention its callers are trusted to follow.
///
/// See the `Sentinel` docstring for the failure this exists for, and for why it is an argument
/// rather than a separate call that callers are asked to make first. Every instance must claim a
/// positive floor on the number of non-black pads (`expect_pads_match` asserts that too, so
/// `waveform(0)` is not a way out); the cursor and column claims are the ones an example adds when
/// its own subject is sharper than "something was drawn".
class PadExpectation {
public:
	/// @brief The waveform is on screen; this comparison says nothing about the cursor.
	/// @param lit_at_least Minimum non-black pads the example's own geometry implies; must be > 0.
	static PadExpectation waveform(int32_t lit_at_least) { return PadExpectation{lit_at_least, -1}; }
	/// @brief The waveform is on screen **and** the cursor is not -- the "no cursor is drawn"
	///        examples, whose whole subject is a `graphicsRoutine` gate suppressing the cursor while
	///        the rest of the view keeps drawing. Two claims, and both have to hold, or the example
	///        cannot fail for the reason it exists.
	/// @param lit_at_least Minimum non-black pads; must be > 0.
	static PadExpectation waveform_without_cursor(int32_t lit_at_least) { return PadExpectation{lit_at_least, 0}; }
	/// @brief The waveform is on screen **and** the cursor is a whole column of it.
	/// @param lit_at_least Minimum non-black pads; must be > 0.
	static PadExpectation waveform_with_cursor(int32_t lit_at_least) {
		return PadExpectation{lit_at_least, static_cast<int32_t>(kDisplayHeight)};
	}

	/// @brief Also require that column @p x is @p colour on every row.
	///
	/// What the two halves of the blink example are actually about: the end column is
	/// `colours::red` on the bright phase and `colours::red_dull` on the dim one
	/// (`audio_clip_view.cpp:164-170`). Without it either half would pass on any pair of agreeing
	/// images.
	/// @param x      The column.
	/// @param colour The colour every row of it must be.
	/// @return This expectation, for chaining onto a factory.
	PadExpectation and_column(int32_t x, RGB colour) const {
		PadExpectation out = *this;
		out.column_ = x;
		out.column_colour_ = colour;
		return out;
	}

	/// @brief Assert everything this expectation claims about @p r's grid.
	/// @param self The enclosing `it(...)` block's lambda parameter.
	/// @param r    The rig.
	template <typename Self>
	void apply(Self& self, Rig& r) const {
		// A floor of zero would be "compare these two grids and claim nothing", which is the thing
		// this type exists to make unspellable. Asserted rather than `static_assert`ed because the
		// factories take a runtime int.
		expect(lit_at_least_ > 0).to_be_true();
		expect_grid_has_content(self, r, lit_at_least_);
		if (cursor_pads_ >= 0) {
			expect(cursor_pads(r)).to_equal(cursor_pads_);
		}
		if (column_ >= 0) {
			const deluge::gfx::grid::PadGrid& g = r.fake.grid().grid();
			int32_t matching = 0;
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				if (g.at(static_cast<uint8_t>(column_), static_cast<uint8_t>(y)) == column_colour_) {
					matching++;
				}
			}
			expect(matching).to_equal(static_cast<int32_t>(kDisplayHeight));
		}
	}

private:
	PadExpectation(int32_t lit_at_least, int32_t cursor_pads_expected)
	    : lit_at_least_(lit_at_least), cursor_pads_(cursor_pads_expected) {}

	int32_t lit_at_least_;
	/// Expected count of pads in the cursor's colour, or -1 for "this example does not say".
	int32_t cursor_pads_;
	/// A column that must be uniformly `column_colour_`, or -1 for "this example does not say".
	int32_t column_ = -1;
	RGB column_colour_{};
};

/// @brief Assert the port's pads are the legacy image, pad for pad -- **and** that the comparison
///        is worth making.
///
/// @p expected has no default and no "nothing" value: a caller physically cannot compare two grids
/// here without saying what makes the comparison non-vacuous, which is the property the `Sentinel`
/// docstring's account of the two fix rounds is the argument for. It is applied **before** the
/// comparison so a failure names the vacuity rather than a pad.
/// @param self     The enclosing `it(...)` block's lambda parameter -- `expect` expands to `self.expect`.
/// @param r        The rig.
/// @param legacy   The image legacy's own four functions produced.
/// @param expected What makes this comparison non-vacuous.
template <typename Self>
void expect_pads_match(Self& self, Rig& r, const LegacyPads& legacy, const PadExpectation& expected) {
	expected.apply(self, r);
	const PadDiff d = pad_diff(r, legacy);
	// The coordinates first, so a failure names a pad rather than only a count.
	expect(d.first_x).to_equal(-1);
	expect(d.first_y).to_equal(-1);
	expect(d.count).to_equal(0);
}

// --- the write oracle -------------------------------------------------------------------------

/// @brief Everything an edit writes to the model, and nothing else.
///
/// `loopLength` is `Song::setClipLength`'s target; the two `sampleHolder` positions are what
/// `changeUnderlyingSampleLength`/`Start` move so the audio keeps its pitch. Compared by value, so a
/// failure names the field.
struct ModelSnapshot {
	int32_t loop_length = 0;
	uint64_t start_pos = 0;
	uint64_t end_pos = 0;
	bool operator==(const ModelSnapshot&) const = default;
};

/// @param r The rig.
/// @return The three fields, read straight off the model both sides share.
ModelSnapshot snapshot(const Rig& r) {
	return ModelSnapshot{r.f.clip->loopLength, r.f.clip->sampleHolder.startPos, r.f.clip->sampleHolder.endPos};
}

/// @brief Assert @p port and @p legacy agree AND that both actually moved off @p seeded.
///
/// The second half is the blindness check, made a property of the helper rather than a habit: see
/// `Rig::reseed`'s `@warning`. A port that writes nothing produces `port == seeded`, which fails
/// here even though `legacy == port` would have been trivially arranged by running legacy first.
/// @param self   The enclosing `it(...)` block's lambda parameter.
/// @param legacy What legacy's own method left in the model.
/// @param port   What the port left in the model, from the same re-seeded start.
/// @param seeded What both started from.
template <typename Self>
void expect_edit_matches(Self& self, const ModelSnapshot& legacy, const ModelSnapshot& port,
                         const ModelSnapshot& seeded) {
	// Field by field first, so a failure says which one.
	expect(port.loop_length).to_equal(legacy.loop_length);
	expect(port.start_pos == legacy.start_pos).to_be_true();
	expect(port.end_pos == legacy.end_pos).to_be_true();
	// ...and then that the comparison was worth making at all.
	expect(legacy == seeded).to_be_false();
	expect(port == seeded).to_be_false();
}

// --- driving input, both sides from the same button id ------------------------------------------

/// @brief `deluge_profile()`'s button matrix, inverted: the `(x, y)` a `DELUGE_EVENT_BUTTON`
///        carries for @p b.
///
/// The same helper `sim/scenes_spec/global_controls_spec.cpp:229-240` carries, and here for its
/// reason: the forward table is `BoardProfile::button_by_matrix` (`shell/profiles/deluge.cpp:12-18`)
/// and hard-coding coordinates would be a second copy of the layout to drift.
/// @param b The button.
/// @return `{x, y}`, or `{255, 255}` if the profile has no cell for it.
std::pair<uint8_t, uint8_t> matrix_xy(ButtonId b) {
	const BoardProfile& p = deluge_profile();
	for (uint8_t y = 0; y < p.button_rows; y++) {
		for (uint8_t x = 0; x < p.button_cols; x++) {
			const size_t i = static_cast<size_t>(y) * p.button_cols + x;
			if (i < p.button_by_matrix.size() && p.button_by_matrix[i] == b) {
				return {x, y};
			}
		}
	}
	return {255, 255};
}

/// @brief The legacy `deluge::hid::Button` for @p b, through the same matrix cell.
///
/// **This is what keeps the two sides driven from one sentinel on the input side too**: an example
/// names a `ButtonId`, and both the port's `UiEvent` and legacy's `buttonAction` argument are
/// derived from it -- neither from the other. `deluge::hid::button::fromXY` (`hid/button.h`) is
/// legacy's own constructor, so this is a mapping and not a second table.
/// @param b The port's button id.
/// @return Legacy's.
deluge::hid::Button legacy_button(ButtonId b) {
	const auto [x, y] = matrix_xy(b);
	return deluge::hid::button::fromXY(x, y);
}

/// @brief Hold or release @p b on LEGACY's side, without going through `Buttons::buttonAction`.
///
/// See the `extern` block at the top of this file for why that function cannot be called here. This
/// writes only what `isButtonPressed`/`isShiftButtonPressed` read (`hid/buttons.cpp:280-296`), which
/// is all the arms under test consult -- it deliberately does NOT run `buttonAction`'s pre-section
/// (`:88-133`), because none of that (the mod-button fan-out, the BACK long-press timer, the
/// sticky-shift bookkeeping) is what these examples are about, and every bit of it reaches a null
/// `getCurrentUI()` or a `uiTimerManager` this port has no counterpart for.
/// @param b  The button.
/// @param on True to hold.
void legacy_hold(ButtonId b, bool on) {
	const auto xy = deluge::hid::button::toXY(legacy_button(b));
	::Buttons::buttonStates[xy.x][xy.y] = on;
	if (b == ButtonId::Shift) {
		::Buttons::shiftCurrentlyPressed = on;
		::Buttons::shiftCurrentlyStuck = false;
	}
}

/// @brief A real `InputLowering`, so `UiContext::held` and `::shift` are the shell's own answers
///        rather than a struct the example filled in.
///
/// `global_controls_spec.cpp`'s `GcRig::press` in miniature, and for its reason: the arms under
/// test branch on which OTHER buttons are down, and a hand-built `ButtonSet` would be the example
/// asserting its own premise. Events are handed to `AudioClipScene::handle` directly rather than
/// through a `Router` because the question these examples ask is what that function returns --
/// specifically, whether it claims `Back` (see the BACK-defect examples).
struct Input {
	InputLowering lowering{deluge_profile(), {}};
	uint32_t now_ms = 0;

	UiContext ctx() const {
		return UiContext{lowering.held(), lowering.held_pads(), lowering.shift(), lowering.sticky(),
		                 now_ms,          lowering.profile(),   &lowering};
	}

	/// @brief Press or release @p b at @p at_ms and hand every lowered event to the scene.
	/// @param b     The button.
	/// @param on    True to press.
	/// @param at_ms The tick's clock.
	/// @return The last `Result` the scene returned.
	Result press(ButtonId b, bool on, uint32_t at_ms = 0) {
		now_ms = at_ms;
		const auto [x, y] = matrix_xy(b);
		lowering.begin_tick(at_ms);
		lowering.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
		Result last;
		UiEvent e{UiEvent::Kind::AllReleased};
		while (lowering.next(e)) {
			last = audio_clip_scene().handle(e, ctx());
		}
		lowering.end_tick(at_ms);
		// **`Button{Back}` never comes out of `next()`.** It is one of the widget-owned inputs the
		// lowering HOLDS BACK from routing so a widget can claim it while it is being drawn
		// (`shell/input_lowering.h`'s "the held-back split"), and the shell delivers those after
		// render, skipping whatever a widget claimed. This rig builds no frame, so nothing can have
		// claimed anything and all of them are delivered. Without this the BACK examples below did
		// not fail loudly -- `handle` was simply never called with a `Back` event, and the "not
		// claimed" assertion passed for entirely the wrong reason.
		for (const UiEvent& held : lowering.held_back()) {
			last = audio_clip_scene().handle(held, ctx());
		}
		return last;
	}

	/// @brief Turn an encoder by @p delta detents and hand the event to the scene.
	/// @param role  Which encoder.
	/// @param delta Signed detents.
	/// @param at_ms The tick's clock.
	/// @return The `Result` the scene returned.
	Result turn(deluge::ui::EncoderRole role, int8_t delta, uint32_t at_ms = 0) {
		now_ms = at_ms;
		return audio_clip_scene().handle(UiEvent{.kind = UiEvent::Kind::Encoder, .encoder = role, .delta = delta},
		                                 ctx());
	}

	/// @brief Strike or release a pad and hand the event to the scene.
	/// @param x  Column, absolute.
	/// @param y  Row.
	/// @param on True to strike.
	/// @return The `Result` the scene returned.
	Result pad(int32_t x, int32_t y, bool on) {
		return audio_clip_scene().handle(UiEvent{.kind = UiEvent::Kind::Pad,
		                                         .x = static_cast<uint8_t>(x),
		                                         .y = static_cast<uint8_t>(y),
		                                         .velocity = static_cast<uint8_t>(on ? 64 : 0),
		                                         .on = on},
		                                 ctx());
	}

	/// @brief Whether @p r asked for a popup carrying @p string_id.
	/// @param r         The result to inspect.
	/// @param string_id The l10n id.
	/// @return True if one of the actions is that popup.
	static bool has_popup(const Result& r, uint16_t string_id) {
		for (const ShellAction& a : r.actions) {
			if (a.kind == ShellAction::Kind::Popup && a.popup_string == string_id) {
				return true;
			}
		}
		return false;
	}
};

/// @brief `ClipNavigationTimelineView::xScrollBeforeFollowingAutoExtendingLinearRecording`
///        (`gui/views/clip_navigation_timeline_view.h:32`) is **protected and static**.
///
/// A class derived from it may name it, and a static member function of that class is a legal
/// handle with no cast anywhere. `NavProbe` is never constructed (it is abstract). The identical
/// trick `sim/scenes_spec/timeline_spec.cpp`'s `TickProbe` uses, and for its reason.
struct NavProbe : ClipNavigationTimelineView {
	static void set_remembered_scroll(int32_t value) { xScrollBeforeFollowingAutoExtendingLinearRecording = value; }
	static int32_t remembered_scroll() { return xScrollBeforeFollowingAutoExtendingLinearRecording; }
};

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
/// @brief The display column the clip's end falls in, computed from the SENTINEL's own scroll,
///        zoom and length -- `padAction`'s `endSquareDisplay` (`audio_clip_view.cpp:502-504`).
///
/// Neither side's answer: it is the number both sides are expected to agree on, so an example that
/// taps this column is tapping the same place on both.
/// @param r The rig.
/// @return The column.
int32_t end_square_of(const Rig& r) {
	return divide_round_negative(r.f.clip->loopLength - r.f.song->xScroll[NAVIGATION_CLIP] - 1,
	                             static_cast<int32_t>(r.f.song->xZoom[NAVIGATION_CLIP]));
}

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

/// @brief Put legacy's two private marker latches down from ANY state, through legacy's own
///        gestures, and confirm it from legacy's own accessor.
///
/// `AudioClipView` is a process singleton, `endMarkerVisible`/`startMarkerVisible` are private and
/// the class is `final`, and **`focusRegained()` -- legacy's own reset (`audio_clip_view.cpp:80-81`)
/// -- cannot be called here**: it goes on to `view.focusRegained()`, which segfaults in a spec
/// process (`gui/views/view.cpp:122`). Found by calling it.
///
/// So the reset is legacy's own `padAction`: one tap on the end column lowers whichever marker was
/// up (`:512` and `:531` both name that column) or raises the end marker if neither was, and
/// `getMaxLength()` (`:857-862`) then says which of those happened.
/// @param r The rig.
/// @return True once both latches are down, as far as `getMaxLength()` can see.
bool reset_legacy_markers(Rig& r) {
	const int32_t end = end_square_of(r);
	(void)legacy_view.padAction(end, 0, 1);
	if (legacy_view.getMaxLength() == static_cast<uint32_t>(r.f.clip->loopLength) + 1) {
		(void)legacy_view.padAction(end, 0, 1);
	}
	return legacy_view.getMaxLength() == static_cast<uint32_t>(r.f.clip->loopLength);
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
		        expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform(16));
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
		        expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform(16));
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
		        expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform(32 + 12));
	        });

	    it(
	        "draws the same waveform and grey region as legacy when the clip is zoomed out", _ {
		        // 96 ticks per column is 65536 frames, four whole clusters. The whole clip fits in eight
		        // columns, so 8..15 are grey.
		        Rig r;
		        r.seed(Sentinel{.x_zoom = 96});
		        r.open();
		        r.tick(0);
		        expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform(64 + 8));
	        });

	    it(
	        "draws the same pads as legacy for a reversed sample", _ {
		        Rig r;
		        r.seed(Sentinel{});
		        r.f.clip->sampleControls.reversed = true;
		        r.open();
		        r.tick(0);
		        expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform(16));
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
		        // The end column at scroll 0, zoom 48 and length 768 is `(768 - 1) / 48` = 15, and on
		        // the bright phase it is `colours::red` (`audio_clip_view.cpp:164-167`).
		        expect_pads_match(self, r, legacy_pads(),
		                          PadExpectation::waveform(16).and_column(15, deluge::gui::colours::red));

		        // One blink phase later, both should be on the dim half -- legacy through its own
		        // `timerCallback` (`:223-229`), the port through the deadline `next_timer()` armed on the
		        // tick above, with no input at all.
		        (void)legacy_view.timerCallback();
		        r.tick(deluge::scenes::audio_clip::kMarkerBlinkMs);
		        expect(r.scene().blink_on()).to_be_false();
		        // The subject of this half: on the dim phase the end column is `colours::red_dull`
		        // (`audio_clip_view.cpp:168-170`), where the bright phase drew `colours::red`. The
		        // end column at scroll 0, zoom 48 and length 768 is `(768 - 1) / 48` = 15.
		        expect_pads_match(self, r, legacy_pads(),
		                          PadExpectation::waveform(16).and_column(15, deluge::gui::colours::red_dull));

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
		        expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform_without_cursor(16));
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
			        expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform_with_cursor(16 + 8));
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
		        expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform_without_cursor(16));
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
		        expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform_without_cursor(16));
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
		        // The cursor really is on screen before the setting is changed -- eight pads, one
		        // whole column -- so the assertion after it is a change and not a tautology.
		        expect(cursor_pads(r)).to_equal(static_cast<int32_t>(kDisplayHeight));

		        // Legacy's setting and the port's copy, each set on its own side.
		        PadLEDs::flashCursor = FLASH_CURSOR_OFF;
		        PadLEDs::clearTickSquares(/*shouldSend=*/false);
		        r.scene().set_flash_cursor(FlashCursor::Off);
		        r.tick(1);
		        expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform_without_cursor(16));
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
	        "keeps the cursor tracking across twelve quiet ticks, with no input at all", _ {
		        // Fix round 1, Minor 3. `next_timer() == 0` is the first zero any scene in this tree
		        // returns, so the thing worth driving is not the value but what a long run of ticks
		        // does with it: twelve consecutive ticks, no board event of any kind, the model's
		        // play position advanced between them the way the audio engine would advance it.
		        // Each tick must re-render (the arm is `request_repaint_after(0, now_ms)`, whose
		        // deadline is due on the very next tick) and must land on legacy's own image.
		        Rig r;
		        playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		        r.seed(Sentinel{});
		        r.open();
		        for (int32_t step = 0; step < 12; step++) {
			        // 48 ticks is exactly one column at this zoom, so the expected column is the
			        // step number -- derived from the sentinel, not from either side.
			        r.f.clip->lastProcessedPos = step * 48;
			        r.tick(static_cast<uint32_t>(step));
			        expect(r.scene().last_tick_square()).to_equal(step);
			        expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform_with_cursor(16 + 8));
			        // And the timer stays armed for the next one, rather than being a one-shot.
			        expect(r.scene().next_timer().has_value()).to_be_true();
			        expect(r.scene().next_timer().value()).to_equal(0u);
		        }
		        playbackHandler.playbackState = 0;
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

	    // --- the UI-mode latch, which three arms below branch on -------------------------------------
	    it(
	        "enters and leaves the horizontal-encoder UI mode exactly when legacy does", _ {
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        Input in;
		        r.reseed(Sentinel{});
		        r.open_scene_only();
		        currentUIMode = UI_MODE_NONE;

		        // Press: both sides reach `TimelineView::buttonAction`'s X_ENC arm
		        // (`gui/views/timeline_view.cpp:73-90`) and enter the mode.
		        (void)in.press(ButtonId::XEnc, true);
		        (void)legacy_view.buttonAction(legacy_button(ButtonId::XEnc), true, false);
		        expect(r.scene().ui_mode()).to_equal(currentUIMode);
		        expect(r.scene().ui_mode()).to_equal(deluge::scenes::audio_clip::kUiModeHoldingHorizontalEncoder);

		        // Release: both leave it.
		        (void)in.press(ButtonId::XEnc, false);
		        (void)legacy_view.buttonAction(legacy_button(ButtonId::XEnc), false, false);
		        expect(r.scene().ui_mode()).to_equal(currentUIMode);
		        expect(r.scene().ui_mode()).to_equal(deluge::scenes::audio_clip::kUiModeNone);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "does NOT enter that mode when shift is held, which is what frees the length-edit chord", _ {
		        spec_fixture::QuietAudio quiet;
		        // `audio_clip_view.cpp:392-396`: with shift down and Y_ENC up, the X_ENC arm matches
		        // neither branch, does nothing, and -- the part that matters -- never delegates to
		        // `ClipView`, so the mode is never entered. That is exactly what leaves
		        // `horizontalEncoderAction`'s `isNoUIModeActive()` first arm (`:789`) reachable.
		        Rig r;
		        Input in;
		        r.reseed(Sentinel{});
		        r.open_scene_only();
		        currentUIMode = UI_MODE_NONE;

		        (void)in.press(ButtonId::Shift, true);
		        legacy_hold(ButtonId::Shift, true);
		        (void)in.press(ButtonId::XEnc, true);
		        (void)legacy_view.buttonAction(legacy_button(ButtonId::XEnc), true, false);

		        expect(r.scene().ui_mode()).to_equal(currentUIMode);
		        expect(r.scene().ui_mode()).to_equal(deluge::scenes::audio_clip::kUiModeNone);

		        (void)in.press(ButtonId::XEnc, false);
		        (void)in.press(ButtonId::Shift, false);
		        legacy_hold(ButtonId::Shift, false);
		        currentUIMode = UI_MODE_NONE;
	        });

	    // --- the BACK defect Task 11 handed over ------------------------------------------------------

	    it(
	        "claims BACK while the horizontal encoder is held, so GlobalControls never undoes", _ {
		        spec_fixture::QuietAudio quiet;
		        // The routing half of the defect. `services::GlobalControls` gates `View::buttonAction`'s
		        // undo/redo arm on `stack_.input_top().is_root()` (`services/global_controls.cpp:580`),
		        // which this scene answers true; the ONLY thing that keeps the undo from happening is
		        // this scene claiming the event first, which is what `handled` says.
		        Rig r;
		        Input in;
		        r.reseed(Sentinel{});
		        r.open_scene_only();

		        // With no mode set, BACK is not this scene's: it must fall through, which is how
		        // undo/redo keeps working everywhere else.
		        expect(in.press(ButtonId::Back, true, 1).handled).to_be_false();
		        // Released before the next press: `InputLowering` reports EDGES, so a second press of
		        // a button already down produces no event at all and the assertion below would be
		        // reading the previous result. Found by that assertion failing.
		        (void)in.press(ButtonId::Back, false, 2);

		        // With the horizontal encoder held it IS this scene's, and the claim is what closes
		        // the defect.
		        (void)in.press(ButtonId::XEnc, true, 3);
		        const Result claimed = in.press(ButtonId::Back, true, 4);
		        expect(claimed.handled).to_be_true();
		        // The release is claimed too -- legacy's `if (on)` at `:418` guards the BODY, not the
		        // arm, so a release with the mode set also returns DEALT_WITH rather than reaching
		        // `View::buttonAction`.
		        expect(in.press(ButtonId::Back, false, 5).handled).to_be_true();
		        (void)in.press(ButtonId::XEnc, false, 6);
	        });

	    it(
	        "clears the clip on BACK + horizontal encoder, exactly as legacy does", _ {
		        // The behavioural half. `AudioClip::clear` (`model/clip/audio_clip.cpp:1295-1334`)
		        // drops the sample and the clip's name, which is what "cleared" means in the model --
		        // and is emphatically not what an undo would have done.
		        //
		        // **Every example in this group holds `QuietAudio`, and none of the render examples
		        // above does.** Driving legacy's own gestures reaches cluster loading and
		        // `AudioEngine::routineWithClusterLoading`, and a spec process never booted the engine
		        // -- so without the latch these segfault rather than fail (`support/card_access.h`'s
		        // `@warning`; found here by exactly that segfault, in `AudioClip::unassignVoiceSample`
		        // at `:1311` and again in legacy's `setupScroll` pre-render). It must NOT spread to a
		        // pad comparison: there the same pump is what makes the waveform scan succeed, and a
		        // failed scan is the vacuity the `Sentinel` docstring is about.
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        Input in;

		        // --- legacy, from its own sentinel ---
		        r.reseed(Sentinel{});
		        r.f.clip->name = "TAKE";
		        currentUIMode = UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON;
		        (void)legacy_view.buttonAction(legacy_button(ButtonId::Back), true, false);
		        currentUIMode = UI_MODE_NONE;
		        const bool legacy_sample_gone = r.f.clip->sampleHolder.audioFile == nullptr;
		        const bool legacy_name_gone = r.f.clip->name.empty();
		        expect(legacy_sample_gone).to_be_true();
		        expect(legacy_name_gone).to_be_true();

		        // --- the port, from the same sentinel, re-seeded ---
		        r.f.clip->sampleHolder.setAudioFile(r.sample);
		        r.reseed(Sentinel{});
		        r.f.clip->name = "TAKE";
		        r.open_scene_only();
		        expect(r.f.clip->sampleHolder.audioFile != nullptr).to_be_true(); // the re-seed took
		        (void)in.press(ButtonId::XEnc, true);
		        const Result cleared = in.press(ButtonId::Back, true);
		        expect(r.f.clip->sampleHolder.audioFile == nullptr).to_equal(legacy_sample_gone);
		        expect(r.f.clip->name.empty()).to_equal(legacy_name_gone);
		        // And the popup legacy shows through `display->displayPopup` (`:437-442`), which this
		        // layer raises as an `Action::Kind::Popup`.
		        expect(Input::has_popup(cleared,
		                                static_cast<uint16_t>(FlashStorage::automationClear
		                                                          ? deluge::l10n::String::STRING_FOR_SAMPLE_CLEARED
		                                                          : deluge::l10n::String::STRING_FOR_CLIP_CLEARED)))
		            .to_be_true();
		        (void)in.press(ButtonId::XEnc, false);

		        r.f.clip->sampleHolder.setAudioFile(r.sample);
		        r.f.clip->name.clear();
	        });

	    it(
	        "gives BACK back to the global handler when the song button is ALSO held, as legacy does", _ {
		        spec_fixture::QuietAudio quiet;
		        // `:417`'s condition is `currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON`,
		        // an EXACT comparison against the whole word -- so the song button's exclusive value
		        // ORed in takes the arm away. Asserted against legacy's own answer, by driving legacy's
		        // `buttonAction` in the same state and watching the clip survive.
		        Rig r;
		        Input in;

		        r.reseed(Sentinel{});
		        currentUIMode = UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON | UI_MODE_HOLDING_SONG_BUTTON;
		        (void)legacy_view.buttonAction(legacy_button(ButtonId::Back), true, false);
		        currentUIMode = UI_MODE_NONE;
		        expect(r.f.clip->sampleHolder.audioFile != nullptr).to_be_true(); // legacy did NOT clear

		        r.reseed(Sentinel{});
		        r.open_scene_only();
		        (void)in.press(ButtonId::Session, true);
		        (void)in.press(ButtonId::XEnc, true);
		        expect(r.scene().ui_mode())
		            .to_equal(deluge::scenes::audio_clip::kUiModeHoldingSongButton
		                      | deluge::scenes::audio_clip::kUiModeHoldingHorizontalEncoder);
		        expect(in.press(ButtonId::Back, true).handled).to_be_false();
		        expect(r.f.clip->sampleHolder.audioFile != nullptr).to_be_true();
		        (void)in.press(ButtonId::XEnc, false);
		        (void)in.press(ButtonId::Session, false);
	        });

	    // --- stop_following()'s refused turn ----------------------------------------------------------

	    it(
	        "forgets the auto-extend restore point on a turn the UI-mode gate then refuses", _ {
		        spec_fixture::QuietAudio quiet;
		        // The residual Task 11 recorded and this task closes. Legacy clears
		        // `xScrollBeforeFollowingAutoExtendingLinearRecording` as the FIRST statement of
		        // `ClipNavigationTimelineView::horizontalEncoderAction`
		        // (`clip_navigation_timeline_view.cpp:32`), above `TimelineView`'s gates -- so a turn
		        // those gates refuse still forgets it. With `UI_MODE_HOLDING_SONG_BUTTON` set, neither
		        // `timeline_view.cpp:161` (wants the horizontal-encoder flag) nor `:203` (wants no mode
		        // at all) matches, so nothing moves.
		        Rig r;
		        Input in;

		        // --- legacy ---
		        r.reseed(Sentinel{});
		        NavProbe::set_remembered_scroll(480);
		        currentUIMode = UI_MODE_HOLDING_SONG_BUTTON;
		        (void)legacy_view.horizontalEncoderAction(1);
		        currentUIMode = UI_MODE_NONE;
		        const int32_t legacy_follow = NavProbe::remembered_scroll();
		        const int32_t legacy_scroll = r.f.song->xScroll[NAVIGATION_CLIP];
		        const uint32_t legacy_zoom = r.f.song->xZoom[NAVIGATION_CLIP];

		        // --- the port ---
		        r.reseed(Sentinel{});
		        r.open_scene_only();
		        r.scene().timeline().scroll_before_following = 480;
		        (void)in.press(ButtonId::Session, true);
		        (void)in.turn(deluge::ui::EncoderRole::Horizontal, 1);
		        (void)in.press(ButtonId::Session, false);

		        expect(r.scene().timeline().scroll_before_following).to_equal(legacy_follow);
		        expect(r.scene().timeline().scroll_before_following).to_equal(deluge::scenes::timeline::kNotFollowing);
		        // ...and the turn really was REFUSED on both sides, which is what makes this example
		        // about the clear rather than about the gesture.
		        expect(r.scene().timeline().x_scroll).to_equal(legacy_scroll);
		        expect(r.scene().timeline().x_scroll).to_equal(0);
		        expect(r.scene().timeline().x_zoom).to_equal(legacy_zoom);
		        expect(r.scene().timeline().x_zoom).to_equal(48u);
	        });

	    it(
	        "KEEPS the restore point on a shift length edit, which is one arm above the clear", _ {
		        spec_fixture::QuietAudio quiet;
		        // The other half of the placement, and the reason the brief's "first statement of the
		        // horizontal-encoder arm" was one function too high: legacy's clear lives in
		        // `ClipNavigationTimelineView`, reached only through `ClipView`'s THIRD arm
		        // (`gui/views/clip_view.cpp:253-262`). A shift length edit takes the FIRST
		        // (`clip_view.cpp:149-181`) and never reaches it.
		        Rig r;
		        Input in;

		        // --- legacy ---
		        r.reseed(Sentinel{});
		        NavProbe::set_remembered_scroll(480);
		        currentUIMode = UI_MODE_NONE;
		        legacy_hold(ButtonId::Shift, true);
		        (void)legacy_view.horizontalEncoderAction(1);
		        legacy_hold(ButtonId::Shift, false);
		        const int32_t legacy_follow = NavProbe::remembered_scroll();
		        const int32_t legacy_length = r.f.clip->loopLength;
		        expect(legacy_follow).to_equal(480); // legacy really does keep it

		        // --- the port ---
		        r.reseed(Sentinel{});
		        r.open_scene_only();
		        r.scene().timeline().scroll_before_following = 480;
		        (void)in.press(ButtonId::Shift, true);
		        (void)in.turn(deluge::ui::EncoderRole::Horizontal, 1);
		        (void)in.press(ButtonId::Shift, false);

		        expect(r.scene().timeline().scroll_before_following).to_equal(legacy_follow);
		        // ...and the edit itself happened, so this is not passing because the turn did nothing.
		        expect(r.f.clip->loopLength).to_equal(legacy_length);
		        expect(r.f.clip->loopLength > 768).to_be_true();
	        });

	    // --- the edits ---------------------------------------------------------------------------------

	    it(
	        "writes the same loop length and end marker as legacy when the end marker is dragged", _ {
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        Input in;
		        const Sentinel s{};
		        // Column 9 at zoom 48 means a new length of `(9 + 1) * 48 = 480` ticks -- derived from
		        // the sentinel alone, and shorter than the seeded 768 so the edit is a real change.
		        constexpr int32_t kColumn = 9;

		        // --- legacy: raise its marker through its own gesture, then drag through it too ---
		        r.reseed(s);
		        expect(reset_legacy_markers(r)).to_be_true();
		        const ModelSnapshot seeded = snapshot(r);
		        expect(raise_legacy_end_marker(r)).to_be_true();
		        (void)legacy_view.padAction(kColumn, 0, 1);
		        const ModelSnapshot legacy = snapshot(r);
		        lower_legacy_end_marker(r);

		        // --- the port, from the same re-seeded start ---
		        r.reseed(s);
		        r.open_scene_only();
		        expect(snapshot(r) == seeded).to_be_true(); // the re-seed really put it back
		        (void)in.pad(end_square_of(r), 0, true);    // raise, through the port's own gesture
		        expect(r.scene().end_marker_visible()).to_be_true();
		        (void)in.pad(kColumn, 0, true);
		        const ModelSnapshot port = snapshot(r);

		        expect_edit_matches(self, legacy, port, seeded);
		        expect(port.loop_length).to_equal(480);
	        });

	    it(
	        "writes the same start marker as legacy when the start marker is dragged", _ {
		        spec_fixture::QuietAudio quiet;
		        // The start-marker gesture is behind `RuntimeFeatureSettingType::TrimFromStartOfAudioClip`
		        // (`audio_clip_view.cpp:560`), which is off by default; both sides read the same global,
		        // so it is turned on once, for both, and put back.
		        const uint32_t saved = runtimeFeatureSettings.get(RuntimeFeatureSettingType::TrimFromStartOfAudioClip);
		        runtimeFeatureSettings.set(RuntimeFeatureSettingType::TrimFromStartOfAudioClip,
		                                   RuntimeFeatureStateToggle::On);

		        Rig r;
		        Input in;
		        const Sentinel s{};
		        constexpr int32_t kColumn = 4; // a new start of `4 * 48 = 192` ticks

		        // --- legacy ---
		        r.reseed(s);
		        expect(reset_legacy_markers(r)).to_be_true();
		        const ModelSnapshot seeded = snapshot(r);
		        (void)legacy_view.padAction(0, 0, 1); // start square at scroll 0 is column 0
		        (void)legacy_view.padAction(kColumn, 0, 1);
		        const ModelSnapshot legacy = snapshot(r);

		        // --- the port ---
		        r.reseed(s);
		        r.open_scene_only();
		        (void)in.pad(0, 0, true);
		        expect(r.scene().start_marker_visible()).to_be_true();
		        (void)in.pad(kColumn, 0, true);
		        const ModelSnapshot port = snapshot(r);

		        expect_edit_matches(self, legacy, port, seeded);
		        // `oldEndTick - newStartTicks` = `768 - 192` (`audio_clip_view.cpp:652-653`).
		        expect(port.loop_length).to_equal(576);

		        // Legacy's start latch is private and nothing else puts it down; every example that
		        // drives legacy's `padAction` opens with `reset_legacy_markers`, but put it down here
		        // too rather than leaving the next example to clean up after this one.
		        expect(reset_legacy_markers(r)).to_be_true();

		        runtimeFeatureSettings.set(RuntimeFeatureSettingType::TrimFromStartOfAudioClip, saved);
	        });

	    it(
	        "writes the same length and end marker as legacy for a REVERSED sample", _ {
		        spec_fixture::QuietAudio quiet;
		        // The other arm of `changeUnderlyingSampleLength` (`audio_clip_view.cpp:601-607`),
		        // which moves `startPos` rather than `endPos` and can clamp it at zero.
		        Rig r;
		        Input in;
		        const Sentinel s{};
		        constexpr int32_t kColumn = 9;

		        r.reseed(s);
		        r.f.clip->sampleControls.reversed = true;
		        expect(reset_legacy_markers(r)).to_be_true();
		        const ModelSnapshot seeded = snapshot(r);
		        expect(raise_legacy_end_marker(r)).to_be_true();
		        (void)legacy_view.padAction(kColumn, 0, 1);
		        const ModelSnapshot legacy = snapshot(r);
		        lower_legacy_end_marker(r);

		        r.reseed(s);
		        r.f.clip->sampleControls.reversed = true;
		        r.open_scene_only();
		        (void)in.pad(end_square_of(r), 0, true);
		        (void)in.pad(kColumn, 0, true);
		        const ModelSnapshot port = snapshot(r);

		        expect_edit_matches(self, legacy, port, seeded);
		        // Reversed, it is the START pos that moved and the END pos that did not.
		        expect(port.end_pos == seeded.end_pos).to_be_true();
		        expect(port.start_pos != seeded.start_pos).to_be_true();

		        r.f.clip->sampleControls.reversed = false;
	        });

	    it(
	        "edits the clip length without timestretching exactly as legacy does, both directions", _ {
		        spec_fixture::QuietAudio quiet;
		        // `AudioClipView::editClipLengthWithoutTimestretching` (`audio_clip_view.cpp:798-835`),
		        // a DISTINCT arm from `adjustLoopLength`: it moves the sample's marker with the clip.
		        // Reached by shift + the horizontal encoder's own button, which -- per the UI-mode
		        // example above -- deliberately enters no mode.
		        for (const int32_t offset : {1, -1}) {
			        Rig r;
			        Input in;
			        const Sentinel s{};

			        r.reseed(s);
			        const ModelSnapshot seeded = snapshot(r);
			        currentUIMode = UI_MODE_NONE;
			        legacy_hold(ButtonId::Shift, true);
			        legacy_hold(ButtonId::XEnc, true);
			        (void)legacy_view.horizontalEncoderAction(offset);
			        legacy_hold(ButtonId::XEnc, false);
			        legacy_hold(ButtonId::Shift, false);
			        currentUIMode = UI_MODE_NONE;
			        const ModelSnapshot legacy = snapshot(r);

			        r.reseed(s);
			        r.open_scene_only();
			        (void)in.press(ButtonId::Shift, true);
			        (void)in.press(ButtonId::XEnc, true);
			        expect(r.scene().ui_mode()).to_equal(deluge::scenes::audio_clip::kUiModeNone);
			        (void)in.turn(deluge::ui::EncoderRole::Horizontal, static_cast<int8_t>(offset));
			        (void)in.press(ButtonId::XEnc, false);
			        (void)in.press(ButtonId::Shift, false);
			        const ModelSnapshot port = snapshot(r);

			        expect_edit_matches(self, legacy, port, seeded);
			        // `ClipView::changeClipLength` moves by ONE SQUARE, not one screen
			        // (`gui/views/clip_view.cpp:272`, `:292`), and a square at zoom 48 is 48 ticks --
			        // so the two directions land on 816 and 720. Derived from the sentinel, not from
			        // either side; the first cut of this expectation said 1536/384 and was wrong.
			        expect(port.loop_length).to_equal(offset > 0 ? 816 : 720);
		        }
	        });

	    // --- the first parked MenuAction gets a real callee -------------------------------------------

	    it(
	        "sets the clip length equal to the sample length exactly as legacy does", _ {
		        spec_fixture::QuietAudio quiet;
		        // `MenuAction::SetClipLengthToSampleLength`'s callee (`scenes/menu/action.h`'s table).
		        // **Driven directly**, because nothing consumes `MenuScene::take_action()` -- the
		        // dispatcher is spec F's, and building half of one here would be a second seam nobody
		        // owns. This example is what says the callee is real.
		        Rig r;
		        const Sentinel s{};

		        r.reseed(s);
		        const ModelSnapshot seeded = snapshot(r);
		        legacy_view.setClipLengthEqualToSampleLength();
		        const ModelSnapshot legacy = snapshot(r);

		        r.reseed(s);
		        r.open_scene_only();
		        expect(snapshot(r) == seeded).to_be_true();
		        r.scene().set_clip_length_equal_to_sample_length();
		        const ModelSnapshot port = snapshot(r);

		        expect_edit_matches(self, legacy, port, seeded);
	        });

	    it(
	        "reaches setClipLengthEqualToSampleLength from the X_ENC + Y_ENC chord, as legacy does", _ {
		        spec_fixture::QuietAudio quiet;
		        // The other caller (`audio_clip_view.cpp:383-390`), and the one this library can
		        // actually reach today. Same model comparison, driven through the buttons.
		        Rig r;
		        Input in;
		        const Sentinel s{};

		        r.reseed(s);
		        const ModelSnapshot seeded = snapshot(r);
		        currentUIMode = UI_MODE_NONE;
		        legacy_hold(ButtonId::YEnc, true);
		        (void)legacy_view.buttonAction(legacy_button(ButtonId::XEnc), true, false);
		        legacy_hold(ButtonId::YEnc, false);
		        currentUIMode = UI_MODE_NONE;
		        const ModelSnapshot legacy = snapshot(r);

		        r.reseed(s);
		        r.open_scene_only();
		        (void)in.press(ButtonId::YEnc, true);
		        (void)in.press(ButtonId::XEnc, true);
		        (void)in.press(ButtonId::XEnc, false);
		        (void)in.press(ButtonId::YEnc, false);
		        const ModelSnapshot port = snapshot(r);

		        expect_edit_matches(self, legacy, port, seeded);
	        });

	    // --- the remaining encoder arms ---------------------------------------------------------------

	    it(
	        "scrolls the audio output's mode on a select turn, exactly as legacy does", _ {
		        spec_fixture::QuietAudio quiet;
		        // `AudioClipView::selectEncoderAction`'s non-shift arm (`audio_clip_view.cpp:735-736`).
		        Rig r;
		        Input in;
		        r.reseed(Sentinel{});
		        currentUIMode = UI_MODE_NONE;

		        const AudioOutputMode start = r.f.output->mode;
		        legacy_view.selectEncoderAction(1);
		        const AudioOutputMode after_legacy = r.f.output->mode;
		        expect(after_legacy == start).to_be_false(); // legacy really moved it

		        r.f.output->mode = start;
		        r.open_scene_only();
		        (void)in.turn(deluge::ui::EncoderRole::Select, 1);
		        expect(r.f.output->mode == after_legacy).to_be_true();

		        r.f.output->mode = AudioOutputMode::player;
	        });

	    it(
	        "shifts the clip's colour on shift + a vertical turn, exactly as legacy does", _ {
		        spec_fixture::QuietAudio quiet;
		        // `AudioClipView::verticalEncoderAction` (`audio_clip_view.cpp:837-848`).
		        Rig r;
		        Input in;
		        r.reseed(Sentinel{});
		        currentUIMode = UI_MODE_NONE;

		        const int16_t start = r.f.clip->colourOffset;
		        legacy_hold(ButtonId::Shift, true);
		        (void)legacy_view.verticalEncoderAction(3, false);
		        legacy_hold(ButtonId::Shift, false);
		        const int16_t after_legacy = r.f.clip->colourOffset;
		        expect(after_legacy == start).to_be_false();

		        r.f.clip->colourOffset = start;
		        r.open_scene_only();
		        (void)in.press(ButtonId::Shift, true);
		        (void)in.turn(deluge::ui::EncoderRole::Vertical, 3);
		        (void)in.press(ButtonId::Shift, false);
		        expect(r.f.clip->colourOffset).to_equal(after_legacy);

		        r.f.clip->colourOffset = start;
	        });

	    it(
	        "scrolls and zooms on a plain horizontal turn, landing where legacy lands", _ {
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        Input in;

		        // --- scroll: no mode, no shift (`timeline_view.cpp:203-222`) ---
		        // A 4096-tick clip, so one screen right (48 * 16 = 768 ticks) is genuinely inside
		        // `getMaxLength()` and the turn is ALLOWED. At the sentinel's own 768 it would be
		        // refused, which is the other example's subject rather than this one's.
		        r.reseed(Sentinel{.loop_length = 4096});
		        currentUIMode = UI_MODE_NONE;
		        (void)legacy_view.horizontalEncoderAction(1);
		        const int32_t legacy_scroll_long = r.f.song->xScroll[NAVIGATION_CLIP];

		        r.reseed(Sentinel{.loop_length = 4096});
		        r.open_scene_only();
		        (void)in.turn(deluge::ui::EncoderRole::Horizontal, 1);
		        expect(r.scene().timeline().x_scroll).to_equal(legacy_scroll_long);
		        expect(r.scene().timeline().x_scroll).to_equal(48 * kDisplayWidth); // one screen

		        // --- zoom: the horizontal encoder's button held (`timeline_view.cpp:161-199`) ---
		        r.reseed(Sentinel{.loop_length = 4096});
		        currentUIMode = UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON;
		        (void)legacy_view.horizontalEncoderAction(-1);
		        currentUIMode = UI_MODE_NONE;
		        const uint32_t legacy_zoom = r.f.song->xZoom[NAVIGATION_CLIP];
		        expect(legacy_zoom).to_equal(96u); // zoomed OUT one step

		        r.reseed(Sentinel{.loop_length = 4096});
		        r.open_scene_only();
		        (void)in.press(ButtonId::XEnc, true);
		        (void)in.turn(deluge::ui::EncoderRole::Horizontal, -1);
		        expect(r.scene().timeline().x_zoom).to_equal(legacy_zoom);
		        (void)in.press(ButtonId::XEnc, false);
	        });

	    // --- notifications -----------------------------------------------------------------------------

	    it(
	        "scrolls back to the left when a clip-content change leaves the view past the end", _ {
		        spec_fixture::QuietAudio quiet;
		        // `clipNeedsReRendering`'s own half (`audio_clip_view.cpp:711-713`), which legacy's
		        // comment calls "a bit hacky that I put this in this function". The redraw half is the
		        // shell's (`Shell::apply_dirty`), so this is all that is left for the scene.
		        Rig r;
		        r.reseed(Sentinel{.x_scroll = 768});
		        r.open_scene_only();
		        expect(r.scene().timeline().x_scroll).to_equal(768);

		        // Legacy's own answer, from legacy's own function, over the same model.
		        r.f.song->xScroll[NAVIGATION_CLIP] = 768;
		        legacy_view.clipNeedsReRendering(r.f.clip);
		        const int32_t legacy_scroll = r.f.song->xScroll[NAVIGATION_CLIP];
		        expect(legacy_scroll).to_equal(0);

		        deluge::notify::DirtySet dirty;
		        dirty.raise(deluge::notify::Dirty::ClipContent);
		        audio_clip_scene().on_model_changed(dirty);
		        expect(r.scene().timeline().x_scroll).to_equal(legacy_scroll);
	        });

	    it(
	        "leaves the scroll alone when the view is still inside the clip", _ {
		        spec_fixture::QuietAudio quiet;
		        // The `else` of the same test (`:714-716`), so the example above is a branch and not a
		        // function that always scrolls to zero.
		        Rig r;
		        r.reseed(Sentinel{.x_scroll = 384, .loop_length = 4096});
		        r.open_scene_only();

		        r.f.song->xScroll[NAVIGATION_CLIP] = 384;
		        legacy_view.clipNeedsReRendering(r.f.clip);
		        expect(r.f.song->xScroll[NAVIGATION_CLIP]).to_equal(384);

		        deluge::notify::DirtySet dirty;
		        dirty.raise(deluge::notify::Dirty::ClipContent);
		        audio_clip_scene().on_model_changed(dirty);
		        expect(r.scene().timeline().x_scroll).to_equal(384);
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
		        // The one place a bare content assertion is still right: this compares against a
		        // recorded golden rather than against legacy, so there is no `PadExpectation` to
		        // carry -- but a golden of a black grid would have recorded nothing and passed
		        // forever, which is the same hazard, so the floor is asserted here too.
		        expect_grid_has_content(self, r, 64 + 8);
		        expect_golden(self, "../ui/scenes/audio_clip_pads.state", r.grid());
		        playbackHandler.playbackState = 0;
	        });
    });
// NOLINTEND(readability-function-cognitive-complexity)
// clang-format on
DELUGE_SPEC(audio_clip_scene_spec, audio_clip_scene_spec_body)
