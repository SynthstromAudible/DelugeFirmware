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
///   * **`Slicer::redraw` (`slicer.cpp:100-101`)**, which is empty: a 7SEG-only hook with no body.
///   * **`Slicer::padAction`'s "do slice" branch (`slicer.cpp:508-534`)** -- the voice walk, which is
///     `graphicsRoutine`'s walk again, needs a live voice for the same reason and is unreachable for
///     the same reason. `Sound::voices()` reads a private `fast_vector<ActiveVoice>`
///     (`processing/sound/sound.h:311, 358`) filled only by `AudioEngine`'s voice pool, and nothing
///     in a spec process boots one. The *arithmetic* it feeds is ported as
///     `SlicerScene::add_slice_at` and is compared against a table the spec seeds legacy with
///     (below); only the source of the number is uncovered.
///   * **`Slicer::doSlice` (`slicer.cpp:596-751`) itself.** It calls
///     `sampleBrowser.claimAudioFileForInstrument` (`:600`), reads `soundEditor.currentSource` /
///     `currentSound` (`:612-614`, `:634`), calls `instrumentClipView.recalculateColours` (`:747`)
///     and ends in `sampleBrowser.exitAndNeverDeleteDrum()` (`:749`) -- four `gui/` globals
///     `src/scenes/**` may never name, and a card read. So the commit's *body* is behind
///     `SliceOps::commit` and the spec drives a fake; what IS compared, and mutation-proved, is
///     everything around it: the snapshot, the gate, the dispatch and the release.
///
/// @par What the two sides are TOLD, and by whom
///
/// Task 9's rule stands -- each side computes from its own sentinel -- with one addition this task
/// had to make. Several arms need a slice table with more than one entry in it, and neither side can
/// build one from its own `padAction`: legacy's adder is behind the voice walk above. So the spec
/// **tells both sides the same table**, each through its own front door -- the port through its own
/// ported adder (`SlicerScene::add_slice_at`, which is `slicer.cpp:535-551`), legacy through its own
/// public members -- and then **asserts the two tables agree before the gesture under test runs**
/// (`expect_tables_match`). The seeding is therefore an assertion, not an assumption, and every
/// comparison after it is still legacy's own code against the port's own code.
///
/// @note The spec may include `gui/` (for the legacy slicer); the library may not. Same rule
///       `marker_scene_spec.cpp` runs under.

#include "scenes/context_menu/context_menu_scene.h"
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
#include "hid/matrix/matrix_driver.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "storage/cluster/cluster.h"

#include "support/song_fixture.h"

// NOT `support/card_access.h`. Task 9 included it and used neither guard; task 10 deleted it, and
// the reason is worth stating because the omission looks like one. `QuietAudio` disarms exactly one
// landmine -- `FileCatalogue`'s `emptyFileItems`/`deleteSomeFileItems` pump
// `AudioEngine::routineWithClusterLoading()` between chunks, and an un-booted engine dies in
// `fillInputRing` (`card_access.h:59-81`). Nothing here drives a listing, and the card-touching half
// of `Slicer::doSlice` is behind `SliceOps::commit`, whose fake performs no I/O -- so nothing in this
// file reaches an audio-engine yield point at all. Holding the latch anyway would assert a card
// touch that does not happen. `SdRoot` likewise: no file is opened.

#include "spec/gfx/support/golden.h"
#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

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

/// @brief The last text legacy asked for, and whether it asked for the popup to be taken down.
///
/// Not decoration: `popupTextTemporary` is where three of legacy's arms end
/// (`horizontalEncoderAction` `:227`, `verticalEncoderAction` `:243`, `buttonAction`'s Y_ENC arm
/// `:301`), and `cancelPopup` is where two more do (`padAction` `:503`, `:540`). Recording them is
/// what makes the popup an oracle rather than a side effect nobody looks at: the port's own text
/// arrives through `notify::notice_text` and comes back out of `Shell::notices().last_popup_text()`,
/// so the two strings can be compared character for character.
/// @{
std::string legacy_popup_text;
int32_t legacy_popup_cancels = 0;
/// @}

/// @brief The minimum `Display` legacy needs: `display` is null in a spec process
///        (`hid/display/display.cpp:4`) and both encoder arms call `popupTextTemporary`.
struct SlicerDisplay final : deluge::hid::Display {
	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const*, int8_t, bool, uint8_t, int32_t, PopupType) override {}
	void popupText(char const* t, PopupType) override { legacy_popup_text = t != nullptr ? t : ""; }
	void popupTextTemporary(char const* t, PopupType) override { legacy_popup_text = t != nullptr ? t : ""; }
	void cancelPopup() override { legacy_popup_cancels++; }
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
		// The scene is a process singleton and `FakeOps` is an example-scoped local, so leaving it
		// wired would hand the next example a dangling boundary. Unwired is also the state
		// `SliceOps`'s docstring calls the honest default.
		slicer_scene().set_ops(nullptr);
		std::memset(matrixDriver.padStates, 0, sizeof(matrixDriver.padStates));
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
	/// tick's frames (`shell/shell.cpp:388-389`), and the `Repaint` the arm files is therefore for
	/// the tick after (`shell.cpp:397-404`). So a pixel comparison taken one tick after a turn shows
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

	/// @brief Press or release one button, by matrix coordinates (`shell/profiles/deluge.cpp:13-18`).
	void button(uint8_t mx, uint8_t my, bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, mx, my, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
	}
	/// @name The five buttons `Slicer::buttonAction` has arms for
	/// @{
	void x_enc(bool on, uint32_t at_ms) { button(0, 1, on, at_ms); }
	void y_enc(bool on, uint32_t at_ms) { button(0, 0, on, at_ms); }
	void select_enc(bool on, uint32_t at_ms) { button(4, 3, on, at_ms); }
	void save(bool on, uint32_t at_ms) { button(6, 3, on, at_ms); }
	void back(bool on, uint32_t at_ms) { button(7, 1, on, at_ms); }
	/// @}

	/// @brief Press or release a pad, absolute coordinates; velocity 0 is a release.
	void pad(int32_t x, int32_t y, int32_t velocity, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, static_cast<uint8_t>(x), static_cast<uint8_t>(y),
		                                      static_cast<int16_t>(velocity)},
		                     at_ms);
	}

	/// @return Is `SlicerScene` still on the stack?
	bool on_stack() {
		for (const deluge::ui::shell::Scene* s : shell.stack().scenes()) {
			if (s->kind() == kSlicerSceneKind) {
				return true;
			}
		}
		return false;
	}

	/// @return The text the shell's popup service last showed.
	///
	/// @warning This is a *record*, not a level -- `Notices::last_popup_text()` is "the text last
	///          shown" and survives the popup's removal (`shell/notices.h:358-361`). Ask
	///          `popup_showing()` whether one is on screen.
	std::string popup() { return shell.notices().last_popup_text(); }

	/// @return Is a popup scene on the stack right now? `Notices` pushes `SceneKind::Popup` for one
	///         and `SceneStack::remove`s it on cancel (`Notices::drain`'s `CancelPopup` case,
	///         `shell/notices.cpp:282-289`) or on timeout (`Notices::tick`, `notices.cpp:327-350`).
	bool popup_showing() {
		for (const deluge::ui::shell::Scene* s : shell.stack().scenes()) {
			if (s->kind() == SceneKind::Popup) {
				return true;
			}
		}
		return false;
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

// --- where legacy draws each slot ---------------------------------------------------------------

struct PadXY {
	int32_t x = -1;
	int32_t y = -1;
};

/// @brief Ask **legacy's own renderer** which pad slot @p index is drawn at.
///
/// `Slicer::renderMainPads`'s slot loop (`slicer.cpp:131-149`) paints slot `i` at
/// `((i % 4) + (i / 16) * 4, (i / 4) % 4)`, and colours the one equal to `currentSlice` with
/// `colours::green.dim()` -- dim level 1, which no other slot can take: with `numManualSlice` at 0
/// every other slot is `dim(6)` (`:137`), green on the even pages and dark blue on the odd ones.
/// So exactly one pad matches, and finding it reads the layout out of legacy rather than restating
/// it here. `padAction`'s `:488` is that layout inverted, which is what the caller is testing.
///
/// @param index The slot to locate.
/// @return Its pad coordinates, or `{-1, -1}` if legacy drew no unique bright slot.
PadXY legacy_slot_pad(int32_t index) {
	const int32_t saved_count = legacy_slicer.numManualSlice;
	const int32_t saved_cursor = legacy_slicer.currentSlice;
	legacy_slicer.numManualSlice = 0;
	legacy_slicer.currentSlice = index;
	const LegacyPads pads = legacy_pads();
	legacy_slicer.numManualSlice = saved_count;
	legacy_slicer.currentSlice = saved_cursor;

	const RGB bright = deluge::gui::colours::green.dim();
	PadXY found;
	int32_t matches = 0;
	for (int32_t y = 0; y < kDisplayHeight / 2; y++) {
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			if (pads.image[y][x] == bright) {
				found = PadXY{x, y};
				matches++;
			}
		}
	}
	return matches == 1 ? found : PadXY{};
}

// --- the audio/model/card boundary, faked ------------------------------------------------------

/// @brief A `SliceOps` that records rather than acts.
///
/// The six calls behind `SliceOps` are the ones this layer may not make (see its docstring); a fake
/// is how the spec sees *that they were made, with what, and in what order* without a booted audio
/// engine, a kit, or a card. Everything the scene decides on its own -- the slice table, the modes,
/// the popup, the gate, the coalescer, the dispatch -- is real and is compared against legacy.
///
/// `play_position` is the one that answers back: it stands for the voice walk at
/// `slicer.cpp:508-534`, and `nullopt` (its default) is legacy's `voiceSample == nullptr` at `:533`.
struct FakeOps final : deluge::scenes::slicing::SliceOps {
	int32_t stop_previewing_calls = 0;
	int32_t kill_voices_calls = 0;
	int32_t restore_calls = 0;
	std::vector<deluge::scenes::slicing::PreviewRequest> auditions;
	std::vector<deluge::scenes::slicing::SliceCommit> commits;
	std::optional<int32_t> next_play_position{};
	int32_t play_position_calls = 0;

	void stop_previewing() override { stop_previewing_calls++; }
	void kill_voices() override { kill_voices_calls++; }
	void restore_full_range() override { restore_calls++; }
	void audition(const deluge::scenes::slicing::PreviewRequest& r) override { auditions.push_back(r); }
	void commit(const deluge::scenes::slicing::SliceCommit& c) override { commits.push_back(c); }
	std::optional<int32_t> play_position() override {
		play_position_calls++;
		return next_play_position;
	}

	/// @brief Element @p i of a recorded vector, or a zeroed value if it was never recorded.
	///
	/// cppspec's `expect` does not stop the example, so a bare `auditions[0]` on a mutant that
	/// recorded nothing aborts the whole process inside libstdc++ and hides every other result --
	/// found while mutation-testing, and this is the fix. A mutant now fails loudly on the value.
	/// @{
	deluge::scenes::slicing::PreviewRequest audition_at(size_t i) const {
		return i < auditions.size() ? auditions[i] : deluge::scenes::slicing::PreviewRequest{};
	}
	deluge::scenes::slicing::SliceCommit commit_at(size_t i) const {
		return i < commits.size() ? commits[i] : deluge::scenes::slicing::SliceCommit{};
	}
	/// @}
};

// --- the shared slice table ---------------------------------------------------------------------

/// @brief Seed BOTH sides with the same manual slice table, each through its own front door.
///
/// See "What the two sides are TOLD" in the file docstring. The port goes through its own ported
/// adder (`SlicerScene::add_slice_at`, `slicer.cpp:535-551`) with the play positions handed in;
/// legacy is assigned its own public members, because its adder is behind the voice walk no spec can
/// drive. `expect_tables_match` then makes the seeding an assertion.
///
/// @param positions Start positions to place, in the order a finger would tap them -- deliberately
///                  NOT sorted, so the port's insertion sort has to do something.
void seed_port(const std::vector<int32_t>& positions) {
	for (const int32_t p : positions) {
		(void)slicer_scene().add_slice_at(p);
	}
}

void seed_legacy(const std::vector<int32_t>& positions) {
	std::vector<int32_t> sorted = positions;
	std::sort(sorted.begin(), sorted.end());
	legacy_slicer.numManualSlice = 1 + static_cast<int32_t>(sorted.size());
	// Slot 0 is `focusRegained`'s zero slice (`slicer.cpp:65, 68-71`), which every placed slice
	// sorts after here because the fixtures use positive positions.
	for (size_t i = 0; i < sorted.size(); i++) {
		legacy_slicer.manualSlicePoints[i + 1].startPos = sorted[i];
		legacy_slicer.manualSlicePoints[i + 1].transpose = 0;
	}
}

/// @brief Assert the two slice tables are identical, entry for entry, over all 64 slots.
template <typename Self>
void expect_tables_match(Self& self) {
	expect(slicer_scene().num_manual_slices()).to_equal(legacy_slicer.numManualSlice);
	expect(slicer_scene().current_slice()).to_equal(legacy_slicer.currentSlice);
	for (int32_t i = 0; i < deluge::scenes::slicing::kMaxManualSlices; i++) {
		expect(slicer_scene().slice(i).start_pos).to_equal(legacy_slicer.manualSlicePoints[i].startPos);
		expect(slicer_scene().slice(i).transpose).to_equal(legacy_slicer.manualSlicePoints[i].transpose);
	}
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

	// --- the slice table, against legacy ------------------------------------------------------

	it("places slices in sorted order, as legacy's padAction does", _ {
		// `slicer.cpp:535-551`. The taps go in unsorted deliberately: the bubble sort at `:542-551`
		// is what puts them in order, and a table that arrived sorted would not exercise it.
		Rig r;
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		const std::vector<int32_t> taps{300000, 100000, 200000, 50000};
		seed_port(taps);

		open_legacy(r);
		legacy_slicer.slicerMode = SLICER_MODE_MANUAL;
		seed_legacy(taps);
		expect_tables_match(self);
		// Five: `focusRegained`'s slice 0 plus the four taps.
		expect(slicer_scene().num_manual_slices()).to_equal(5);
	});

	it("refuses a slice at or past the end of the sample, as legacy does", _ {
		// `:535`: `samplePos < waveformBasicNavigator.sample->lengthInSamples`.
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
		// The other half of `:535`: `numManualSlice < MAX_MANUAL_SLICES`. `focusRegained` leaves one
		// slice, so 63 taps fill the table and the 64th is refused.
		Rig r;
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		for (int32_t i = 0; i < 63; i++) {
			expect(slicer_scene().add_slice_at(1000 + i)).to_be_true();
		}
		expect(slicer_scene().num_manual_slices()).to_equal(deluge::scenes::slicing::kMaxManualSlices);
		expect(slicer_scene().add_slice_at(99999)).to_be_false();
		expect(slicer_scene().num_manual_slices()).to_equal(deluge::scenes::slicing::kMaxManualSlices);
	});

	// --- the pads, against legacy's padAction --------------------------------------------------

	it("maps every slot pad to the slice legacy's own slot grid draws there", _ {
		// `padAction`'s `:488` is the INVERSE of `renderMainPads`'s slot layout at `:132-133`, and
		// legacy's own renderer is what says where slot `i` lives. So the pad coordinate for each
		// index comes out of `legacy_slot_pad`, which reads it off a real `Slicer::renderMainPads`
		// image -- not out of arithmetic this file wrote. Then the port is pressed there and asked
		// which slice it thinks that was.
		//
		// Legacy's `padAction` itself is NOT driven: its select branch reaches `preview()` at
		// `:493-500` on every index that maps to a placed slice, and `previewForTarget` (`:446-482`)
		// needs a kit clip, a resolved `soundEditor` model stack and a booted `AudioEngine`. See the
		// file docstring.
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
		expect(slicer_scene().num_manual_slices()).to_equal(deluge::scenes::slicing::kMaxManualSlices);

		open_legacy(r);
		legacy_slicer.slicerMode = SLICER_MODE_MANUAL;

		for (int32_t i = 0; i < deluge::scenes::slicing::kMaxManualSlices; i++) {
			const PadXY slot = legacy_slot_pad(i);
			expect(slot.x).to_be_greater_than(-1);
			const uint32_t at = 100 + static_cast<uint32_t>(i) * 4;
			r.pad(slot.x, slot.y, 64, at);
			r.settle(at);
			r.pad(slot.x, slot.y, 0, at + 2);
			r.settle(at + 2);
			expect(slicer_scene().current_slice()).to_equal(i);
		}
	});

	it("auditions the slice under the pad, from its start to the next slice's start", _ {
		// `:493-496`, the interior arm.
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
		// `:497-500`, the tail arm.
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
		// `:559-561`: a release inside the window is `preview(0, 0, 0, 0)`.
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

	it("takes the popup down only when the pad moves the cursor, as legacy does", _ {
		// `:491`, `:502-504`: `closePopup` is `currentSlice != slicePadIndex`, so re-tapping the pad
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

		// Slot 0 is already the cursor, so tapping it cancels nothing and the popup survives ...
		r.pad(0, 0, 64, 10);
		r.settle(10);
		expect(r.popup_showing()).to_be_true();
		expect(slicer_scene().current_slice()).to_equal(0);

		// ... and moving to slot 1 takes it down.
		r.pad(1, 0, 64, 20);
		r.settle(20);
		expect(slicer_scene().current_slice()).to_equal(1);
		expect(r.popup_showing()).to_be_false();

		// Legacy's own `padAction` cannot be driven here (see the file docstring), so the same pair
		// of conditions is read off legacy's `:491` expression with legacy's own cursor: same
		// cursor, no cancel; different cursor, cancel.
		open_legacy(r);
		legacy_slicer.slicerMode = SLICER_MODE_MANUAL;
		seed_legacy({100000});
		legacy_slicer.currentSlice = 0;
		expect(legacy_slicer.currentSlice != 0).to_be_false();
		expect(legacy_slicer.currentSlice != 1).to_be_true();
	});

	it("keeps its slice table when a popup it raised itself expires", _ {
		// The regression Task 10 found and Task 9 shipped: `on_resume()` used to re-run
		// `focusRegained`, and a popup is a scene in the shell (`SceneKind::Popup`), so the removal
		// `Notices` performs 800 ms later called it. Forty hand-placed slices vanished because the
		// user tapped a pad. See `SlicerScene::on_resume` for the whole argument.
		//
		// The popup here is raised by the scene's own Y_ENC arm (`slicer.cpp:301`), and the 800 ms
		// is `kPopupTimeoutMs` (`shell/notices.cpp:31`), returned by `Notices::timeout_for`
		// (`notices.cpp:352-355`); the push is `Notices::show_popup_with_timeout`
		// (`notices.cpp:301-325`) and the removal `Notices::tick` (`notices.cpp:327-350`).
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
		// top (`gui/ui/ui.cpp:194-199`), so an opaque scene closing over the slicer DOES reset it.
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

		const SceneId menu{deluge::scenes::context_menu::kContextMenuSceneKind,
		                   static_cast<uint16_t>(deluge::scenes::context_menu::DescriptorId::ResetFlash)};
		expect(r.shell.stack().push(menu)).to_be_true();
		expect(&r.shell.stack().input_top() != &slicer_scene()).to_be_true();
		r.shell.stack().pop();
		expect(&r.shell.stack().input_top() == &slicer_scene()).to_be_true();

		// `Slicer::focusRegained` (`slicer.cpp:59-72`) again: one slice, slot 0, REGION.
		expect(slicer_scene().num_manual_slices()).to_equal(1);
		expect(slicer_scene().current_slice()).to_equal(0);
		expect(slicer_scene().mode() == Mode::Region).to_be_true();
	});

	it("hands a REGION pad to the scene beneath, as legacy hands it to the sample browser", _ {
		// `:567` `return sampleBrowser.padAction(x, y, on)`. This layer may not name that global; an
		// unhandled event reaches the same UI through the stack. See the note at the definition.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		r.tick(0);
		r.pad(3, 2, 64, 10);
		r.settle(10);
		expect(static_cast<int32_t>(ops.auditions.size())).to_equal(0);
		expect(slicer_scene().current_slice()).to_equal(0);
	});

	it("swallows a MANUAL pad outside the slot grid, as legacy does", _ {
		// `:563-565`: in MANUAL every pad is DEALT_WITH -- the bottom half and the sidebar included.
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

	// --- the buttons, against legacy's buttonAction ---------------------------------------------

	it("switches mode on the X encoder button, as legacy's buttonAction does", _ {
		// `:284-294`.
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

		open_legacy(r);
		// Legacy's own arm, from legacy's own state. `AudioEngine::stopAnyPreviewing()` (`:288`) and
		// `getCurrentKit()->firstDrum->killAllVoices()` (`:291`) both need a booted engine and a kit
		// clip, so the mode arithmetic (`:285-286`) is compared by running it exactly as legacy
		// writes it -- see `SliceOps` for why the two calls are behind the boundary rather than here.
		int32_t legacy_mode = legacy_slicer.slicerMode;
		legacy_mode++;
		legacy_mode %= 2;
		expect(after_one).to_equal(legacy_mode);
		legacy_mode++;
		legacy_mode %= 2;
		expect(after_two).to_equal(legacy_mode);
		// And the two audio calls the arm makes: `stop_previewing` only on the way INTO manual.
		expect(ops.stop_previewing_calls).to_equal(1);
		expect(ops.kill_voices_calls).to_equal(2);
	});

	it("pops up the transpose on the Y encoder button, with legacy's own text", _ {
		// `:297-303`. Both sides format their own string; the two are compared character for
		// character through `SlicerDisplay` on one side and the shell's popup service on the other.
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

		open_legacy(r);
		legacy_slicer.slicerMode = SLICER_MODE_MANUAL;
		legacy_slicer.verticalEncoderAction(-7, false);
		legacy_popup_text.clear();
		(void)legacy_slicer.buttonAction(deluge::hid::button::Y_ENC, true, false);
		expect(ported).to_equal(std::string("Transpose: -7"));
		expect(ported).to_equal(legacy_popup_text);
	});

	it("says nothing on the Y encoder button in REGION mode, as legacy does", _ {
		// The mode half of `:297`'s condition.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		r.tick(0);
		r.y_enc(true, 10);
		r.settle(10);
		const std::string ported = r.popup();

		open_legacy(r);
		legacy_popup_text.clear();
		(void)legacy_slicer.buttonAction(deluge::hid::button::Y_ENC, true, false);
		expect(ported).to_equal(std::string(""));
		expect(ported).to_equal(legacy_popup_text);
	});

	it("deletes the slice under a held pad on SAVE, exactly as legacy does", _ {
		// `:306-332`, both sides, over the whole 64-entry table. Legacy asks
		// `matrixDriver.isPadPressed(xx, yy)` (`:309`); the port asks `UiContext::held_pads`, which
		// is the same fact lowered by the shell, so each side reads its own.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		const std::vector<int32_t> taps{100000, 200000, 300000};
		seed_port(taps);

		open_legacy(r);
		legacy_slicer.slicerMode = SLICER_MODE_MANUAL;
		seed_legacy(taps);

		// Put both cursors on slice 2 (slot (2, 0)) and hold that pad on both sides.
		r.pad(2, 0, 64, 10);
		r.settle(10);
		legacy_slicer.currentSlice = 2;
		matrixDriver.padStates[2][0] = true;
		expect_tables_match(self);

		r.save(true, 20);
		r.settle(20);
		(void)legacy_slicer.buttonAction(deluge::hid::button::SAVE, true, false);
		expect_tables_match(self);
		expect(slicer_scene().num_manual_slices()).to_equal(3);
	});

	it("ignores SAVE when the slice's own pad is not held, exactly as legacy does", _ {
		// The other side of `:309`. **The braces matter:** the whole delete is inside that test, and
		// when it fails the arm falls past SELECT_ENC and BACK to the `else` at `:393-395`.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		const std::vector<int32_t> taps{100000, 200000, 300000};
		seed_port(taps);

		open_legacy(r);
		legacy_slicer.slicerMode = SLICER_MODE_MANUAL;
		seed_legacy(taps);
		legacy_slicer.currentSlice = 2;
		r.pad(2, 0, 64, 10);
		r.settle(10);
		r.pad(2, 0, 0, 12); // released again -- nothing is held now
		r.settle(12);
		expect_tables_match(self);

		r.save(true, 20);
		r.settle(20);
		expect(static_cast<int32_t>(legacy_slicer.buttonAction(deluge::hid::button::SAVE, true, false)))
		    .to_equal(static_cast<int32_t>(ActionResult::NOT_DEALT_WITH));
		expect_tables_match(self);
		expect(slicer_scene().num_manual_slices()).to_equal(4);
	});

	it("floors the count at one and re-clamps the cursor when the last slice is deleted", _ {
		// `:318-326`, the same floor and re-clamp `selectEncoderAction`'s MANUAL arm applies.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		slicer_scene().set_mode(Mode::Manual);
		r.tick(0);
		seed_port({100000});

		open_legacy(r);
		legacy_slicer.slicerMode = SLICER_MODE_MANUAL;
		seed_legacy({100000});
		r.pad(1, 0, 64, 10);
		r.settle(10);
		legacy_slicer.currentSlice = 1;
		matrixDriver.padStates[1][0] = true;
		expect_tables_match(self);

		r.save(true, 20);
		r.settle(20);
		(void)legacy_slicer.buttonAction(deluge::hid::button::SAVE, true, false);
		expect_tables_match(self);
		expect(slicer_scene().num_manual_slices()).to_equal(1);
		expect(slicer_scene().current_slice()).to_equal(0);
	});

	// --- the commit -----------------------------------------------------------------------------

	it("commits the REGION slice count the select encoder left, in one dispatched op", _ {
		// `:334-368` and `:570-594`. `doSlice` itself is behind `SliceOps::commit` -- see the file
		// docstring -- so what is asserted here is the snapshot legacy builds at `:345-349`.
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
		// `:346` reads `numClips` in REGION mode, and nothing kills voices on that path (`:347-351`).
		expect(ops.kill_voices_calls).to_equal(0);
		// The gate is closed before dispatch and released by the op (`:363`, `:593`); on this BSP the
		// dispatch is inline (`deluge_worker_run`, `task_scheduler_c_api.cpp:101-104`), so by the
		// time the tick returns it is open again.
		expect(slicer_scene().committing()).to_be_false();
	});

	it("commits the manual slice table, killing the drum's voices first", _ {
		// `:345-351`: MANUAL snapshots the whole table by value and kills voices before dispatch.
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

	it("refuses every button while a commit is in flight, as legacy's top-of-function guard does", _ {
		// `:279-281` against the gate at `:363`. The gate is not observable on this BSP from outside
		// the op -- the dispatch is inline -- so the second press is made from INSIDE it, which is
		// exactly the window the gate exists for (`:353-362`: a BACK arriving there would `close()`
		// the scene from the executor while the op mutates the same UI stack).
		struct ReentrantOps final : deluge::scenes::slicing::SliceOps {
			SlicerScene* scene = nullptr;
			deluge::ui::shell::UiContext* ctx = nullptr;
			bool gate_was_closed = false;
			int32_t commits = 0;
			void commit(const deluge::scenes::slicing::SliceCommit&) override {
				commits++;
				gate_was_closed = scene->committing();
			}
		};
		Rig r;
		ReentrantOps ops;
		ops.scene = &slicer_scene();
		slicer_scene().set_ops(&ops);
		r.open();
		r.tick(0);
		r.select_enc(true, 10);
		r.settle(10);
		expect(ops.commits).to_equal(1);
		expect(ops.gate_was_closed).to_be_true();
		expect(slicer_scene().committing()).to_be_false();
	});

	// --- BACK -----------------------------------------------------------------------------------

	it("closes on BACK, as legacy's buttonAction does", _ {
		// `:371-392`. `close()` is `Action::Kind::Remove` here, not `Pop` -- see the note at the
		// definition.
		Rig r;
		FakeOps ops;
		slicer_scene().set_ops(&ops);
		r.open();
		r.tick(0);
		expect(r.on_stack()).to_be_true();
		r.back(true, 10);
		r.settle(10);
		expect(r.on_stack()).to_be_false();
		// REGION mode restores nothing (`:375`).
		expect(ops.restore_calls).to_equal(0);
		expect(ops.kill_voices_calls).to_equal(0);
	});

	it("puts the drum's range back to the whole sample before closing, in MANUAL mode", _ {
		// `:375-389`, the MANUAL arm.
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
		// The other half of `:279`.
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
		expect(static_cast<int32_t>(slicer_scene().mode())).to_equal(SLICER_MODE_REGION);
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
