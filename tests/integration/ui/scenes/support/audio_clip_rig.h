/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

/// @file
/// @brief The audio clip scene's spec rig, shared by the specs that drive `AudioClipScene` and legacy's
///        `AudioClipView` side by side: the fixture song and sample, the scene's shell over fake surfaces, legacy's
///        frame read back, and input fed to both sides from one button id.
///
/// @note Each spec file is its own process, so the helpers stay in an anonymous namespace here.

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host/host_ports.h"
#include "support/peak_seed.h"
#include "ui/scenes/audio_clip/audio_clip_scene.h"
#include "ui/scenes/bind/ports/write_sink.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/relevance.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/timeline/timeline.h"

#include "base/notify/dirty_set.h"
#include "ui/shell/input_lowering.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"
#include "ui/widgets/core/metrics.h"

#include "board_config.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/language.h"
#include "gui/ui/ui.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/clip_navigation_timeline_view.h"
#include "gui/views/session_view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/pad_leds.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/edits/audio_input_for.h"
#include "model/edits/setting_edit.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/audio_output.h"
#include "project/setting_group.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "storage/flash_storage.h"

#include "fakes.h"
#include "support/card_access.h" // before <cppspec.hpp> -- see its @warning
#include "support/song_fixture.h"

#include "support/mocks/fake_surfaces.h"
#include "support/printers/project_address_printer.h"
#include "unit/ui/gfx/support/golden.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <format>
#include <ostream>
#include <string>

#include "support/menu_context.h"

/// @brief `PlaybackHandler`'s "the swung-tick answer is zero" latch
///        (`currentlyActioningSwungTickOrResettingPlayPos` in `playback/playback_handler.cpp`, read at
///        `PlaybackHandler::getNumSwungTicksInSinceLastActionedSwungTick`).
///
/// Declared rather than included, exactly as `tests/integration/ui/scenes/timeline_spec.cpp` declares it, and for its
/// reason: `Clip::getLivePos()` adds `getNumSwungTicksInSinceLastActionedSwungTick()` to `lastProcessedPos`, and on a
/// song no transport ever started that reaches `getActualSwungTickCount`'s external-clock swing branch and divides by
/// `(uint32_t)(50 + currentSong->swingAmount)` (`playback/playback_handler.cpp`) -- a **SIGFPE**, not a failure.
/// Holding the latch takes legacy's own early return in `getNumSwungTicksInSinceLastActionedSwungTick`
/// (`playback/playback_handler.cpp`), so `getLivePos()` is exactly `lastProcessedPos`, which is what a spec that sets
/// the play position by hand means.
extern bool currentlyActioningSwungTickOrResettingPlayPos;

/// @name Legacy's own held-button state, reached by bare `extern`
///
/// The arms under test branch on `Buttons::isButtonPressed(...)` and `isShiftButtonPressed()`, whose
/// backing store (`Buttons`' `buttonStates` and `shiftCurrentlyPressed`) is namespace-scope and not in the header.
/// **`Buttons::buttonAction` cannot be used to set it in a spec process**: its very first act after
/// recording the press is `getCurrentUI()->buttonAction(...)` (in `Buttons::buttonAction`), and
/// `getCurrentUI()` (`gui/ui/ui.cpp`) answers `emptyStackUI` here, a UI that acts on nothing; main's answers
/// `lastUIBeforeNullifying`, which is null in a spec process (`origin/main:src/deluge/gui/ui/ui.cpp`).
/// @{
namespace Buttons {
extern bool buttonStates[NUM_BUTTON_COLS + 1][NUM_BUTTON_ROWS];
extern bool shiftCurrentlyPressed;
extern bool shiftCurrentlyStuck;
} // namespace Buttons
/// @}

using namespace deluge::ui::scenes;
using deluge::ui::scenes::audio_clip::audio_clip_scene;
using deluge::ui::scenes::audio_clip::AudioClipScene;
using deluge::ui::scenes::audio_clip::kAudioClipSceneKind;
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

namespace spec_fixture = deluge::ui::scenes::spec;

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

	// Allocated and adopted as `AudioFileManager` adopts a loaded sample, so the holder's lease checks
	// see a real resource. The one lease taken here is never released: the sample is never evicted or
	// freed -- `song_fixture.h`'s own rule, and for its reason.
	void* memory = deluge::memory::alloc_external(sizeof(Sample), 16);
	auto* s = new (memory) Sample();
	audioFileManager.adoptAudioFileObject(s);
	s->addReason();
	s->byteDepth = 2;
	s->numChannels = 1;
	s->sampleRate = 44100;
	s->audioDataStartPosBytes = 0;
	s->audioDataLengthBytes = kSampleLength * kBytesPerFrame;
	s->lengthInSamples = kSampleLength;

	peak_seed::seed_peaks(*s, [&](int32_t i) {
		return std::pair<int8_t, int8_t>{static_cast<int8_t>(static_cast<int8_t>(-cluster_peak(i))),
		                                 static_cast<int8_t>(cluster_peak(i))};
	});
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
		static constexpr const deluge::ui::widgets::core::Style* kStyles[] = {
		    &deluge::ui::widgets::core::kDelugeStyle64};
		BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

/// @brief The minimum `Display` legacy needs: `display` is null in a spec process
///        (`hid/display/display.cpp`) and several arms of `padAction` reach it.
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
	// `l10n::chosenLanguage` (`gui/l10n/l10n.cpp`) is null until the app constructs its `Display`,
	// and legacy's clip-clear arm calls `deluge::l10n::get(...)` unconditionally
	// (`AudioClipView::padAction`'s clip-clear arm) -- which dereferences it. Every other scenes spec that
	// drives a legacy popup sets it the same way (`context_menu_scene_spec.cpp`).
	if (deluge::l10n::chosenLanguage == nullptr) {
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	}
}

/// @brief The starting state BOTH sides read, and neither derives from the other.
///
/// @warning **Only some scroll/zoom pairs can be scanned in a spec process, and the ones that
///          cannot make this whole comparison VACUOUS.** A column whose ends fall inside a 256-frame
///          block reads those frames (`deluge::sample::waveform_columns`), which fails with no card and
///          leaves the column pending. Both sides then draw nothing -- the port because `render_grid`
///          returns, legacy because `renderMainPads`' `!success` arm returns -- and the pad comparison
///          passes on two black grids.
///
///          **`PadExpectation` below is what closes it, and it closes it structurally:**
///          `expect_pads_match` takes one as a required argument and applies it before it compares
///          anything, so there is no spelling of "compare these two grids" that does not also say
///          what makes the comparison non-vacuous. Nothing here relies on a caller remembering to
///          precede the comparison with a guard.
///
///          A guard that is a separate call does not hold, and neither does a docstring claiming
///          every render example applies one: the blink's second phase and the three "no cursor is
///          drawn" examples are exactly the shape where both sides legitimately go black, and
///          forcing `x_zoom = 24` on the count-in example gave 27/27 passing on two black grids.
///          "Every caller does X" is also an absolute claim with only a convention behind it, in a
///          file that gains more pad comparisons: the next unguarded comparison would be written
///          by someone reading a docstring saying the invariant already held. Hence the argument.
///
///          The constraint on this fixture: `x_zoom * (endPos - startPos) / loop_length` -- which
///          is `AudioClip::getSamplesFromTicks` -- must be a
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

	Song* const saved_song = currentSong;
	bool saved_swung_latch = currentlyActioningSwungTickOrResettingPlayPos;
	uint8_t saved_playback_state = playbackHandler.playbackState;
	uint8_t saved_flash_cursor = PadLEDs::flashCursor;

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p), shell(fake, p, reg, deluge::ui::shell::Services{},
	                                        ShellConfig{.frame_ms = deluge::ui::shell::kGraphicsRoutineMs}) {
		ensure_display();
		currentUIMode = UI_MODE_NONE;
		deluge::ui::scenes::spec::make_current(
		    f.song);                    // the app's sink and facts resolve the clip's address in the current song
		f.song->setCurrentClip(f.clip); // a length undo finds its clip as the current one

		// The clip's sample, attached the way `AudioClip::setupPlaybackBounds` leaves it: the whole
		// file, forwards.
		f.clip->sampleHolder.audioFile = sample;
		sample->addReason(); // the lease `SampleHolder::setAudioFile` takes
		f.clip->sampleHolder.startPos = 0;
		f.clip->sampleHolder.endPos = static_cast<int64_t>(kSampleLength);
		// `SampleHolder::setAudioFile` is what normally computes this; the
		// fixture attaches the sample by hand, so it is set by hand too. `kMaxSampleValue` is the
		// identity: `getLengthInSamplesAtSystemSampleRate` returns the raw duration for it and
		// divides by it otherwise (`sample_holder.cpp`) -- a **SIGFPE** at zero, which is what
		// `AudioClipView::setClipLengthEqualToSampleLength` walks into.
		f.clip->sampleHolder.neutralPhaseIncrement = kMaxSampleValue;
		f.clip->sampleControls.reversed = false;
		f.clip->recorder = nullptr;
		// `Clip::getLivePos()`'s SIGFPE guard -- see the extern's docstring.
		currentlyActioningSwungTickOrResettingPlayPos = true;
		// Legacy's `graphicsRoutine` gate and `prepareColour`'s substitution both read this; the
		// factory default is SLOW (`resetSettings` in `storage/flash_storage.cpp`).
		PadLEDs::flashCursor = FLASH_CURSOR_SLOW;
		PadLEDs::clearTickSquares(/*shouldSend=*/false);
		playbackHandler.playbackState = 0;
		currentPlaybackMode = &session;
		// The app's ports, which act on the current song: the fixture's.
		audio_clip_scene().set_ports(&deluge::gui::shell_host::host_ports());
	}

	/// @brief Tear the stack down, so `AudioClipScene` -- a process singleton -- never carries state
	///        into the next example. `slicer_scene_spec.cpp`'s `~Rig`, for its reason.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		audio_clip_scene().open(std::nullopt, nullptr);
		audio_clip_scene().set_ports(nullptr);
		PadLEDs::clearTickSquares(/*shouldSend=*/false);
		PadLEDs::flashCursor = saved_flash_cursor;
		playbackHandler.playbackState = saved_playback_state;
		currentlyActioningSwungTickOrResettingPlayPos = saved_swung_latch;
		currentUIMode = UI_MODE_NONE;
		f.song->setCurrentClip(nullptr);
		deluge::ui::scenes::spec::make_current(saved_song);
	}

	/// @brief The clip's address.
	[[nodiscard]] deluge::project::ClipAddress address() const { return deluge::project::ClipAddress{f.clip->id()}; }

	/// @brief Write @p s into the model. It is the ONE place either side's view comes from.
	void seed(const Sentinel& s) const {
		f.song->persistentView.xScroll[NAVIGATION_CLIP] = s.x_scroll;
		f.song->persistentView.xZoom[NAVIGATION_CLIP] = static_cast<uint32_t>(s.x_zoom);
		f.song->persistentView.tripletsOn = false;
		f.clip->setLoopLength(s.loop_length);
		f.clip->originalLength = s.loop_length;
		f.clip->lastProcessedPos = s.last_processed_pos;
		f.clip->activeIfNoSolo = true;
		f.clip->soloingInSessionMode = false;
		f.clip->armState = ArmState::OFF;
		// Both caches invalidated, so each side's own scan runs from scratch.
	}

	/// @brief Put the model back to EXACTLY what @p s describes -- the clip's length, the sample's
	///        two markers and the undo log -- so a write comparison can run one start state twice.
	///
	/// @warning **This, and not the pad comparison, is what makes the edit examples non-vacuous.**
	///          A write comparison that seeds once, runs legacy, then runs the port over legacy's
	///          own result and compares "the model" to itself passes whatever the port does --
	///          including nothing at all. A delete-the-write check is what
	///          catches it. Every
	///          example below therefore: seeds, runs legacy, snapshots, **re-seeds**, runs the
	///          port, snapshots, and asserts BOTH that the two snapshots agree AND that each
	///          differs from the seed. The second half is the one that fails when a write is
	///          deleted.
	///
	/// @note `actionLogger.deleteAllLogs()` then one throwaway `getNewAction` is deliberate and is itself a finding:
	///       main's `ClipView::shortenClip` and `lengthenClip` (`origin/main:src/deluge/gui/views/clip_view.cpp`) read
	///       `actionLogger.firstAction[BEFORE]->type` **unguarded** in their `PATTERN_PASTE` tests, so driving
	///       main's length edits with an empty undo log segfaults rather than failing. One inert `MISC` action puts
	///       both sides in the state the device is always in by the time a length edit runs, identically, before either
	///       side moves. `ActionType::MISC` is chosen because neither `lengthenClip`'s `undoing` test (which wants
	///       `CLIP_LENGTH_DECREASE`) nor either `PATTERN_PASTE` test matches it.
	void reseed(const Sentinel& s) const {
		seed(s);
		f.clip->sampleHolder.startPos = 0;
		f.clip->sampleHolder.endPos = static_cast<int64_t>(kSampleLength);
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		// A realistic tempo, which `Song::setupDefault()` does not leave behind: a fresh song's
		// `timePerTimerTickBig` makes `getTimePerTimerTickFloat()` answer 2^32
		// (`model/song/song.cpp`), and `SampleHolder::getLoopLengthAtSystemSampleRate`
		// divides by it (`model/sample/sample_holder.cpp`) -- so
		// `setClipLengthEqualToSampleLength` computed a length of **zero** on BOTH sides, and the
		// comparison passed while nothing happened. Caught by `expect_edit_matches`' "each side
		// differs from the seed" half, which is the whole reason that half is there.
		//
		// 120 BPM, through `Song::setBPMInner`'s own arithmetic (`model/song/song.cpp`) rather than
		// by calling it: `setTempoFromNumSamples` reaches `playbackHandler`, and this process has
		// no transport.
		const double bpm = 120.0 * static_cast<double>(1u << f.song->project.transport.inside_world_tick_magnitude);
		f.song->project.transport.samples_per_timer_tick_big = static_cast<uint64_t>((110250.0 / bpm) * 4294967296.0);
	}

	/// @brief Open the port's scene on the fixture's clip.
	void open() {
		audio_clip_scene().open(address(), &f.song->project);
		shell.init(SceneId{kAudioClipSceneKind, 0});
	}

	/// @brief Point the port's scene at the clip and run its entry hook, WITHOUT standing a shell
	///        up over it.
	///
	/// The input examples drive `Scene::handle` directly (see `Input` below) and compare the model,
	/// so there is no frame to render and no gate to arm; `open()`'s `shell.init` would only add a
	/// scene push to tear down again. **`on_enter` is still called**, because that is where the port
	/// puts `AudioClipView::opened`'s `focusRegained()` and
	/// therefore where the two marker latches are reset -- and `AudioClipScene` is a process
	/// singleton, so an example that leaves a marker up otherwise poisons the next one: the
	/// end-marker drag example leaves `end_marker_visible_` true and the start-marker example then
	/// takes the wrong branch of `padAction`.
	void open_scene_only() {
		audio_clip_scene().open(address(), &f.song->project);
		(void)audio_clip_scene().on_enter(SceneId{kAudioClipSceneKind, 0});
	}

	void tick(uint32_t at_ms) { shell.tick(at_ms); }

	AudioClipScene& scene() { return audio_clip_scene(); }

	std::string oled() { return fake.pbm(0); }
	std::string grid() { return fake.state(); }
};

// --- the legacy oracle ------------------------------------------------------------------------

/// @brief The legacy global (`AudioClipView audioClipView`, `gui/views/audio_clip_view.cpp`),
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

/// @brief The 128x48 canvas `AudioClipView::renderOLED` would have
///        shown, as a PBM.
///
/// `renderOLED`'s one call, `view.displayOutputName`, ignores the canvas it is handed and writes
/// into `hid::display::OLED::main` (`gui/views/view.cpp`), so that is what is read back.
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
	const deluge::ui::gfx::grid::PadGrid& g = r.fake.grid().grid();
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
	const deluge::ui::gfx::grid::PadGrid& g = r.fake.grid().grid();
	int32_t lit = 0;
	for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			const RGB& c = g.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
			if (c.r != 0 || c.g != 0 || c.b != 0) {
				lit++;
			}
		}
	}
	to_be_at_least(expect(lit), at_least);
}

/// @brief The colour the pad driver substitutes for a `Normal` tick square.
///
/// `PadLEDs::flashColours[0]` (`hid/led/pad_leds.cpp`), which is `ui::widgets::grid::tick_colour`'s
/// `kPlayheadColour`. No waveform bar can take it: the bars are a scaling of the clip's colour,
/// whose green channel is 26, and grey is `RGB::monochrome(7)`.
constexpr RGB kPlayheadColour{130, 120, 130};

/// @brief How many pads the port painted in the play cursor's colour.
/// @param r The rig.
/// @return The count, over the whole grid including the sidebar.
int32_t cursor_pads(Rig& r) {
	const deluge::ui::gfx::grid::PadGrid& g = r.fake.grid().grid();
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
	/// What the two halves of the blink example are actually about: the end column is `colours::red` on the bright
	/// phase and `colours::red_dull` on the dim one (`AudioClipView::renderMainPads` in `audio_clip_view.cpp`). Without
	/// it either half would pass on any pair of agreeing images.
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
		expect(lit_at_least_).to_be_greater_than(0);
		expect_grid_has_content(self, r, lit_at_least_);
		if (cursor_pads_ >= 0) {
			expect(cursor_pads(r)).to_equal(cursor_pads_);
		}
		if (column_ >= 0) {
			const deluge::ui::gfx::grid::PadGrid& g = r.fake.grid().grid();
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
/// docstring's argument is for. It is applied **before** the
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

/// @brief Prints the three fields by name, so a failed CppSpec comparison shows which one differs.
std::ostream& operator<<(std::ostream& os, const ModelSnapshot& s) {
	return os << std::format("{{loop_length {}, start_pos {}, end_pos {}}}", s.loop_length, s.start_pos, s.end_pos);
}

/// @param r The rig.
/// @return The three fields, read straight off the model both sides share.
ModelSnapshot model_snapshot(const Rig& r) {
	return ModelSnapshot{r.f.clip->loopLength(), r.f.clip->sampleHolder.startPos, r.f.clip->sampleHolder.endPos};
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
	expect(port.start_pos).to_equal(legacy.start_pos);
	expect(port.end_pos).to_equal(legacy.end_pos);
	// ...and then that the comparison was worth making at all.
	expect(legacy).not_().to_equal(seeded);
	expect(port).not_().to_equal(seeded);
}

// --- driving input, both sides from the same button id ------------------------------------------

/// @brief `deluge_profile()`'s button matrix, inverted: the `(x, y)` a `DELUGE_EVENT_BUTTON`
///        carries for @p b.
///
/// The same helper `tests/integration/ui/scenes/global_controls_spec.cpp` carries, and here for its
/// reason: the forward table is `BoardProfile::button_by_matrix` (`ui/shell/profiles/deluge.cpp`'s `kMatrix`)
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
/// See the `extern` block at the top of this file for why that function cannot be called here. This writes only what
/// `isButtonPressed`/`isShiftButtonPressed` read (`hid/buttons.cpp`), which is all the arms under test consult -- it
/// deliberately does NOT run `buttonAction`'s pre-section (the code ahead of its `getCurrentUI()->buttonAction` call),
/// because none of that (the mod-button fan-out, the BACK long-press timer, the sticky-shift bookkeeping) is what these
/// examples are about, and every bit of it reaches a null `getCurrentUI()` or a `uiTimerManager` this port has no
/// counterpart for.
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
		return last;
	}

	/// @brief Turn an encoder by @p delta detents and hand the event to the scene.
	/// @param role  Which encoder.
	/// @param delta Signed detents.
	/// @param at_ms The tick's clock.
	/// @return The `Result` the scene returned.
	Result turn(deluge::ui::widgets::core::EncoderRole role, int8_t delta, uint32_t at_ms = 0) {
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
///        (`gui/views/clip_navigation_timeline_view.h`) is **protected and static**.
///
/// A class derived from it may name it, and a static member function of that class is a legal
/// handle with no cast anywhere. `NavProbe` is never constructed (it is abstract). The identical
/// trick `tests/integration/ui/scenes/timeline_spec.cpp`'s `TickProbe` uses, and for its reason.
struct NavProbe : ClipNavigationTimelineView {
	static void set_remembered_scroll(int32_t value) { xScrollBeforeFollowingAutoExtendingLinearRecording = value; }
	static int32_t remembered_scroll() { return xScrollBeforeFollowingAutoExtendingLinearRecording; }
};

/// @brief Raise legacy's END marker through legacy's own front door, and prove it took.
///
/// `AudioClipView::endMarkerVisible` is **private** and the class is `final`, so there is no probe
/// subclass to reach it with (the trick `timeline_spec.cpp`'s `TickProbe` uses for
/// `ClipView::getTickSquare` needs a protected member). What there IS is `padAction`
/// (`audio_clip_view.cpp`), which sets it, and `getMaxLength`, which is
/// **public** and returns `loopLength + 1` if and only if it is set. So the flag is raised by
/// legacy's own gesture and read back through legacy's own accessor; the caller asserts on the
/// return.
/// @param r The rig.
/// @return True if `endMarkerVisible` is now set.
/// @brief The display column the clip's end falls in, computed from the SENTINEL's own scroll,
///        zoom and length -- `padAction`'s `endSquareDisplay` (`audio_clip_view.cpp`).
///
/// Neither side's answer: it is the number both sides are expected to agree on, so an example that
/// taps this column is tapping the same place on both.
/// @param r The rig.
/// @return The column.
int32_t end_square_of(const Rig& r) {
	return divide_round_negative(r.f.clip->loopLength() - r.f.song->persistentView.xScroll[NAVIGATION_CLIP] - 1,
	                             static_cast<int32_t>(r.f.song->persistentView.xZoom[NAVIGATION_CLIP]));
}

bool raise_legacy_end_marker(Rig& r) {
	// `AudioClipView::padAction`'s `x == endSquareDisplay` arm (`gui/views/audio_clip_view.cpp`). `endSquareDisplay` is
	// computed by legacy itself inside `padAction`; the column handed in is the one the SENTINEL implies, and the
	// return value is what says the two agreed.
	const int32_t end_square =
	    divide_round_negative(r.f.clip->loopLength() - r.f.song->persistentView.xScroll[NAVIGATION_CLIP] - 1,
	                          static_cast<int32_t>(r.f.song->persistentView.xZoom[NAVIGATION_CLIP]));
	(void)legacy_view.padAction(end_square, 0, 1);
	return legacy_view.getMaxLength() == static_cast<uint32_t>(r.f.clip->loopLength()) + 1;
}

/// @brief Put legacy's END marker back down, through the same gesture.
void lower_legacy_end_marker(Rig& r) {
	const int32_t end_square =
	    divide_round_negative(r.f.clip->loopLength() - r.f.song->persistentView.xScroll[NAVIGATION_CLIP] - 1,
	                          static_cast<int32_t>(r.f.song->persistentView.xZoom[NAVIGATION_CLIP]));
	// `AudioClipView::padAction`'s toggle-off arm: tapping the marker's own column toggles it off.
	(void)legacy_view.padAction(end_square, 0, 1);
}

/// @brief Put legacy's two private marker latches down from ANY state, through legacy's own
///        gestures, and confirm it from legacy's own accessor.
///
/// `AudioClipView` is a process singleton, `endMarkerVisible`/`startMarkerVisible` are private and
/// the class is `final`, and **`focusRegained()` -- legacy's own reset (`audio_clip_view.cpp`)
/// -- cannot be called here**: it goes on to `view.focusRegained()`, which segfaults in a spec
/// process (`View::focusRegained`).
///
/// So the reset is legacy's own `padAction`: one tap on the end column lowers whichever marker was up (its end-marker
/// and start-marker toggle-off arms both name that column) or raises the end marker if neither was, and
/// `getMaxLength()` then says which of those happened.
/// @param r The rig.
/// @return True once both latches are down, as far as `getMaxLength()` can see.
/// @brief Name the clip's track @p name through its record, as a rename does.
void name_track(Rig& r, const char* name) {
	deluge::edits::trackSettingFor(*r.f.output, "name", deluge::serialization::Value::makeScalar(name));
}

/// @brief Name the clip @p name through its record, as a rename does.
void name_clip(Rig& r, const char* name) {
	deluge::edits::clipSettingFor(*r.f.clip, "name", deluge::serialization::Value::makeScalar(name));
}

/// @brief How many steps the undo log holds.
int32_t undo_steps() {
	int32_t n = 0;
	for (const Action* a = actionLogger.firstAction[BEFORE]; a != nullptr; a = a->nextAction) {
		n++;
	}
	return n;
}

bool reset_legacy_markers(Rig& r) {
	const int32_t end = end_square_of(r);
	(void)legacy_view.padAction(end, 0, 1);
	if (legacy_view.getMaxLength() == static_cast<uint32_t>(r.f.clip->loopLength()) + 1) {
		(void)legacy_view.padAction(end, 0, 1);
	}
	return legacy_view.getMaxLength() == static_cast<uint32_t>(r.f.clip->loopLength());
}

} // namespace
