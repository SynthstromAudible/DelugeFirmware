/// @file
/// @brief Level-2 acceptance scripts for what E4 shipped -- the three overlay scenes, the two root
///        scenes, and (for the first time on this stack) the three **real** shell services.
///
/// The mechanism is `menu_scripts_spec.cpp`'s exactly: a UI script (`tests/ui-scripts/shell/`)
/// driven through the real `Shell` over `uitest::FakeSurfaceSet`, with every `snapshot` step's
/// `.pbm` + `.state` diffed against `tests/golden/ui/shell/<name>/`. What is new here is only what
/// gets passed in.
///
// clang-format off
/// | script | subject | how the scene gets on the stack |
/// |---|---|---|
/// | `audio_clip` | `audio_clip::AudioClipScene` (E4 tasks 11-12) | `RunOptions::initial_scene` -- it is a ROOT scene, and a root is what the shell is booted onto |
/// | `slicer` | `slicing::SlicerScene` (E4 tasks 9-10) | `RunOptions::initial_scene`, after `slicer_scene().open(sample)` -- legacy's `SampleBrowser` opens it the same way, `open()` first and the push second |
/// | `param_popup` | `overlay::ParamPopupScene` (E4 task 1) **pushed by `services::ModKnobs`** (task 2) | nothing in the script pushes it: a gold-knob turn reaches the service, and `ModKnobs::show_popup` pushes the scene (`services/mod_knobs.cpp:399`) |
/// | `screensaver` | `overlay::ScreensaverScene` (E4 task 7) **pushed by `shell::Screensaver`** | nothing in the script pushes it either: the script simply stops touching anything for longer than `ShellConfig::screensaver_idle_ms` |
/// | `count_in` | `overlay::CountInScene` (E4 task 6) | **the spec pushes it**, from `RunOptions::before_tick`, over an `AudioClipScene` root. Nothing in the tree pushes this kind -- see the honesty note below |
/// | `menu_gold_turn` | `menu::MenuScene` and a real `services::ModKnobs` in one run (E4 final review) | `RunOptions::initial_scene` at `nodes::saturation`, exactly as `menu_scripts_spec.cpp` opens a menu. The only recorded frame on this stack with a menu and a real service together |
// clang-format on
///
/// Each script has a `_64` sibling that re-runs the geometry-sensitive frames on the 128x64 panel
/// (`board_64()`/`profile_64()`, the same pair `slicer_scene_spec.cpp` and `count_in_spec.cpp`
/// keep). They are separate script *files* because `expect_script_goldens` derives both the script
/// path and the golden directory from one name; the 64-row files say in their own headers which
/// frames they kept and why the rest would have added nothing.
///
/// @par The real services, and the wiring gap this file found
///
/// **Every script on this stack before this one runs with `Services{}` -- three null services --
/// and they all still do.** That is a controller ruling and not an oversight: three plans' goldens
/// were recorded against null services, and re-recording forty-seven of them as a side effect of
/// this task would be a diff nobody could review. The five scripts here are the ones that get real
/// ones.
///
/// Passing them was not as simple as filling in the struct, and the reason was a real gap in the
/// application's own boot path, not a test-harness quirk:
///
///   * `services::ModKnobs`, `GlobalControls` and `MidiLearn` each take a `SceneStack&` and (for
///     the first two) an `InputLowering&` at construction, and they *use* them --
///     `ModKnobs::show_popup` pushes the param popup onto that stack
///     (`scenes/services/mod_knobs.cpp:413`), and `ModKnobs::popup_suppressed` (`:351`) and
///     `ModKnobs::mod_encoder_button_action` (`:564`) read `stack_.input_top()`;
///     `GlobalControls::handle` gates its `View::buttonAction` group on
///     `stack_.input_top().is_root()` (`scenes/services/global_controls.cpp:580`);
///     `MidiLearn::handle` reads the same (`scenes/services/midi_learn.cpp:466`).
///   * The only `SceneStack` and `InputLowering` in an application are the ones the `Shell` **owns**
///     (`shell/shell.h`'s `stack_`, `lowering_`), and `Services` was fixed at the shell's
///     construction. There was no `Shell::set_services`.
///
/// So a real service could not be built before the shell it serves, and could not be installed
/// after it -- which meant **no application could install a service at all**, and no gold-knob
/// turn, transport button or MIDI-learn press reached anything outside the scene stack. This file
/// worked around it with a `LateService` forwarding shim, and reported the gap rather than fixing
/// it, because adding a setter to `src/shell` was spec F's wiring decision and not a side effect of
/// writing acceptance scripts.
///
/// **F1 task 2 fixed it.** `Shell::set_services` (`shell/shell.h`) re-seats all three of the things
/// that hold the slots -- the shell's copy, the `Router`'s, and the deduplicated `service_list_` --
/// and `RealServices::installer()` below now calls it from `after_construct` and hands the shell
/// `Services{}` at construction, exactly as an application does. The shim is gone; the 34 golden
/// snapshots did not move.
///
/// @par What passing them changed, visibly -- and what it did not
///
/// **An LED declared `Off` is not written to `.state` at all** (`script_runner.h`'s `state_text`:
/// "an LED declared off is not shown at all"), so "the services are wired in" and "the services are
/// visible in a golden" are two different claims and only the second is worth making. What is
/// actually in these 34 snapshots:
///
///   * `led Mod0 on` / `led Mod1 on` and both `indicator` lines, in **every** frame -- `ModKnobs`,
///     and they differ between scripts because the mod target does (`Mod1` and a non-zero lower ring
///     in `slicer`, whose target is a synth clip; `Mod0` in the audio-clip scripts).
///   * `led Record on` and `led Play on`, in `audio_clip`'s last three frames -- `GlobalControls`
///     (`services/global_controls.cpp:601-616`, the port of `PlaybackHandler::setLedStates`),
///     after the script presses RECORD and PLAY for real. That is the brief's own motivating
///     gesture, and it is reachable in exactly one script here: `GlobalControls::handle` gates its
///     `View::buttonAction` group on `stack_.input_top().is_root()` (`:580`) and `AudioClipScene`
///     is the only root scene that exists.
///   * **Nothing from `MidiLearn`.** LEARN is off unless learn mode is entered, and no script here
///     enters it -- `MidiLearn` is wired in and contributes no observable line. Stated rather than
///     implied: an earlier version of this docstring claimed seven `GlobalControls` LEDs and a
///     LEARN line that no golden contains, which the review caught by counting them.
///
/// And `param_popup` is the script that could not exist at all without the services: the popup is
/// *pushed by a service*, so with `Services{}` a gold-knob turn produces no frame of any kind.
///
/// @par 34 snapshots are 21 distinct panels, and eleven of them are one panel
///
/// Counted, not estimated (`md5sum` over the ten golden directories): the 34 `.pbm` files hold **21
/// distinct panels**. Three groups are repeats.
///
///   * **Eleven**, all the same panel -- `AudioClipScene::render_oled`'s output name (`:1410`, the
///     port of `view.displayOutputName`). It is `audio_clip/{idle, end_marker, after_play,
///     after_record, after_length_edit}`, `param_popup/{idle, popup_expired}`,
///     `screensaver/{idle, after_wake}` and `count_in/{idle, after_zero}`. Four of the five scripts
///     boot onto `AudioClipScene`, and its OLED half does not move for anything those scripts do:
///     the play cursor, the markers and the length edit are all GRID, and the two overlays that
///     cover it are gone again by the frame after.
///   * **Two**: `slicer/{manual, slice_selected}`.
///   * **Three**: the 64-row idles, `{audio_clip_64, param_popup_64, count_in_64}/idle`.
///
/// **None is vacuous, and the check is per script.** Within any one script no two snapshots are the
/// same frame -- the `.state` half differs at every step, which is what the review that asked for
/// this paragraph verified. *Across* scripts five are the same frame in both halves, and that is
/// the point rather than a defect: `audio_clip/idle`, `param_popup/idle`, `screensaver/idle` and
/// `count_in/after_zero` are literally the same boot state reached by four routes, as are
/// `audio_clip/end_marker` and `screensaver/after_wake`.
///
/// Recorded because the count misleads in one specific way: **a regression in `AudioClipScene`'s
/// OLED reports eleven times**, in four scripts, and a reader counting "34 goldens" is counting
/// eleven copies of one panel. If you are triaging a wide golden diff here, check that panel first.
///
/// @par Honesty notes -- the two things a script here cannot do by itself
///
///   * **`CountInScene` has no pusher in the tree.** `count_in_scene.h` says so plainly ("fires
///     `Action::Kind::Remove` the tick this scene stops showing, so whatever pushes it can
///     push-and-forget"), and `grep -rn kCountInSceneKind src/` finds only the registry entry and
///     the scene's own `Remove`. So `count_in.txt` boots onto `AudioClipScene` and the spec pushes
///     the overlay over it on the first tick (`CountInHooks`), which is what the missing wiring
///     will do. Written down rather than dressed up as a gesture: no user gesture reaches this
///     scene today, and inventing one would be a push no user could perform.
///   * **The count-in digit does not move on its own in a spec process.** The scene is a poll of
///     `playbackHandler.count_in_visual()`, and the only writer of that number is
///     `PlaybackHandler::considerPlaybackTiming`'s swung-tick tree (`playback_handler.cpp:1044-1051`),
///     which needs a running `AudioEngine`. `count_in_spec.cpp` writes the field directly for that
///     reason; `count_in.txt` does the same through `RunOptions::before_tick`, which is the level-2
///     shape of the same stand-in. The script's own header carries the schedule, so the golden is
///     still readable against something written down.
///
/// @note Like `menu_scripts_spec.cpp`, this file may include `gui/` and `hid/`; the library may not.

#include "scenes/audio_clip/audio_clip_scene.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/overlay/count_in_scene.h"
#include "scenes/overlay/param_popup_scene.h"
#include "scenes/overlay/screensaver_scene.h"
#include "scenes/profile.h"
#include "scenes/registry.h"
#include "scenes/services/global_controls.h"
#include "scenes/services/midi_learn.h"
#include "scenes/services/mod_knobs.h"
#include "scenes/slicing/slicer_scene.h"

#include "notify/dirty_set.h"
#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "model/clip/audio_clip.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/cluster/cluster.h"
#include "storage/flash_storage.h"

#include "support/song_fixture.h"

// Before <cppspec.hpp>: `card_access.h` reaches `util/container/ordered_pos_vector.h`, whose
// locals are named `it` -- CppSpec's own DSL keyword. Same placement rule every spec in this
// directory runs under.
#include "support/card_access.h"

#include "spec/shell/support/script_runner.h"

#include "cppspec.hpp"

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::audio_clip::audio_clip_scene;
using deluge::scenes::audio_clip::kAudioClipSceneKind;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::slicing::kSlicerSceneKind;
using deluge::scenes::slicing::Mode;
using deluge::scenes::slicing::slicer_scene;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
namespace bindings = deluge::scenes::menu::bindings;
namespace nodes = deluge::scenes::menu::nodes;

/// @brief `PlaybackHandler`'s "we are inside a swung tick" latch, declared rather than included
///        exactly as `audio_clip_scene_spec.cpp:122` and `timeline_spec.cpp` declare it.
///
/// `audio_clip.txt` starts real playback, and `AudioClipScene::compute_tick_square` then asks
/// `Clip::getLivePos()` where the clip is -- which, on a song whose transport a spec started but
/// whose audio thread never runs, reaches `getActualSwungTickCount`'s external-clock swing branch
/// and divides by `(uint32_t)(50 + currentSong->swingAmount)` (`playback_handler.cpp:1263`): a
/// **SIGFPE**, not a failure. Holding the latch takes legacy's own early return (`:1209-1214`).
extern bool currentlyActioningSwungTickOrResettingPlayPos;

namespace {

// --- the second geometry ------------------------------------------------------------------------

/// @brief A 128x64 single-panel board. `slicer_scene_spec.cpp`'s, verbatim and for its reason.
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

// --- the sample ---------------------------------------------------------------------------------

/// `marker_scene_spec.cpp`'s fixture, for its reason: thirty-two clusters' worth of frames with
/// every overview entry pre-marked investigated, so `find_peaks_per_col` takes its cached
/// whole-cluster arm and no spec process ever reaches the card. Halving the length turns the
/// waveform black, which is exactly the vacuous golden this file is required not to record.
constexpr size_t kClusterSize = 32768;
constexpr int32_t kBytesPerFrame = 2; // 16-bit mono
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

/// @brief The minimum `Display` the model reaches for; `display` is null in a spec process
///        (`hid/display/display.cpp:4`). `menu_scripts_spec.cpp`'s `ScriptDisplay`.
struct ScriptDisplay final : deluge::hid::Display {
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

void install_display() {
	static ScriptDisplay d;
	if (display == nullptr) {
		display = &d;
	}
}

/// @brief Reset the two process-global channels and the language, exactly as
///        `menu_scripts_spec.cpp`'s `reset_globals` does.
void reset_globals() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
	deluge::notify::dirty().drain();
}

// --- the services ---------------------------------------------------------------------------------

/// @brief The three real services, built over the run's own `Shell` from `after_construct` and
///        installed into it with `Shell::set_services`.
///
/// Example-scoped: the services themselves are ordinary objects (unlike every *scene* in this tree,
/// which is a process singleton), so nothing here leaks into the next block. The `Shell` is
/// constructed and destroyed inside `expect_script_goldens`, which this outlives, so nothing here
/// is ever reached through a shell that has gone away.
///
/// @note This used to hold three `LateService` shims -- `Service`s that forwarded every hook to a
///       real one bound in afterwards -- because `Services` was fixed at the `Shell`'s
///       construction and the three real services each need the `SceneStack&` (and, for two of
///       them, the `InputLowering&`) that only exists once that shell does. `Shell::set_services`
///       (F1 task 2) closed that loop for the application, and this file is the reason it was
///       found; the shim is gone and the 34 golden snapshots below did not move, which is the
///       check that the setter installs exactly what the shim forwarded to.
struct RealServices {
	std::optional<services::MidiLearn> learn;
	std::optional<services::ModKnobs> knobs;
	std::optional<services::GlobalControls> controls;

	/// The song every service is told about, and the timeline counter `ModKnobs` points at. Both
	/// are set by the caller before the run; a null `mod_target` leaves the knobs unpointed, which
	/// is `set_target_with_timeline`'s own "no target" state.
	Song* song = nullptr;
	TimelineCounter* mod_target = nullptr;

	RealServices() = default;
	RealServices(const RealServices&) = delete;
	RealServices& operator=(const RealServices&) = delete;

	/// @return What to hand `expect_script_goldens` as its `Services`: nothing. The shell is built
	///         with empty slots, exactly as an application's is, and `installer()` fills them.
	deluge::ui::shell::Services slots() { return {}; }

	/// @return The `RunOptions::after_construct` hook that builds the real three over @p shell and
	///         installs them into it.
	std::function<void(Shell&)> installer() {
		return [this](Shell& shell) {
			// `MidiLearn` first: `GlobalControls` takes it by reference as its `LearnMode`.
			learn.emplace(shell.stack());
			knobs.emplace(overlay::param_popup_scene(), shell.stack(), shell.lowering());
			controls.emplace(shell.stack(), shell.lowering(), *learn);
			learn->set_song(song);
			knobs->set_song(song);
			controls->set_song(song);
			if (mod_target != nullptr) {
				knobs->set_target_with_timeline(mod_target, false);
				knobs->set_mod_region();
			}
			shell.set_services({&*knobs, &*controls, &*learn});
		};
	}
};

/// @brief Everything a script run needs put back afterwards, in one guard.
///
/// The scenes are process singletons and `run_script` already tears the *stack* down; what it
/// cannot know about is the model state a fixture pointed them at. `audio_clip_scene().open(nullptr,
/// nullptr)` is `audio_clip_scene_spec.cpp`'s own teardown, and the matrix/pad reset is
/// `slicer_scene_spec.cpp`'s.
struct SceneReset {
	uint8_t saved_playback_state = playbackHandler.playbackState;
	int32_t saved_count_in = playbackHandler.currentVisualCountForCountIn;
	RecordingMode saved_recording = playbackHandler.recording;
	bool saved_metronome = playbackHandler.metronomeOn;
	bool saved_swung_latch = currentlyActioningSwungTickOrResettingPlayPos;

	SceneReset() = default;
	SceneReset(const SceneReset&) = delete;
	SceneReset& operator=(const SceneReset&) = delete;
	~SceneReset() {
		audio_clip_scene().open(nullptr, nullptr);
		slicer_scene().set_ops(nullptr);
		// The transport half, restored in `global_controls_spec.cpp`'s `reset_transport` order and
		// for its reasons -- `audio_clip.txt` really does press RECORD and PLAY through
		// `services::GlobalControls`, so `playbackHandler` is genuinely running by the end of it.
		// `setForegroundRecording` matters even though no script here starts an output recording:
		// one left standing suppresses the RECORD LED for every later example
		// (`playback_handler.cpp:2503-2506`), which is exactly the cross-contamination this file's
		// own LED assertions would then be reading.
		playbackHandler.playbackState = saved_playback_state;
		playbackHandler.currentVisualCountForCountIn = saved_count_in;
		playbackHandler.recording = saved_recording;
		playbackHandler.metronomeOn = saved_metronome;
		Buttons::recordButtonPressUsedUp = false;
		AudioEngine::setForegroundRecording(nullptr, AudioInputChannel::NONE);
		currentlyActioningSwungTickOrResettingPlayPos = saved_swung_latch;
		std::memset(matrixDriver.padStates, 0, sizeof(matrixDriver.padStates));
	}
};

/// @brief A `SliceOps` that records nothing and performs no I/O.
///
/// `slicer_scene_spec.cpp`'s `FakeOps`, minus the recording -- what this file asserts about the
/// slicer is its pixels, and the boundary's *calls* are that spec's subject. It is still needed:
/// `SlicerScene::preview` and the mode toggle both reach `ops_`, and leaving it null would make a
/// pad tap a different code path from the one a device takes.
struct ScriptOps final : deluge::scenes::slicing::SliceOps {
	void stop_previewing() override {}
	void kill_voices() override {}
	void restore_full_range() override {}
	void audition(const deluge::scenes::slicing::PreviewRequest&) override {}
	void commit(const deluge::scenes::slicing::SliceCommit&) override {}
	std::optional<int32_t> play_position() override { return std::nullopt; }
};

/// @brief Save and restore `FlashStorage::screensaverMode`, the one setting the screensaver scene
///        reads. `screensaver_scene_spec.cpp`'s `set_mode`, as a guard.
struct SavedScreensaverMode {
	::ScreensaverMode saved = FlashStorage::screensaverMode;
	explicit SavedScreensaverMode(::ScreensaverMode mode) { FlashStorage::screensaverMode = mode; }
	~SavedScreensaverMode() { FlashStorage::screensaverMode = saved; }
	SavedScreensaverMode(const SavedScreensaverMode&) = delete;
	SavedScreensaverMode& operator=(const SavedScreensaverMode&) = delete;
};

/// @brief The three manual slice positions `slicer.txt` and `slicer_64.txt` are seeded with.
///
/// Placed a quarter, a half and three quarters of the way through the sample, so the three marks
/// land on distinct columns of the 16-wide mark row (`start_pos / lengthInSamples * 16`).
/// @{
constexpr int32_t kSlice1Start = static_cast<int32_t>(kSampleLength / 4);
constexpr int32_t kSlice2Start = static_cast<int32_t>(kSampleLength / 2);
constexpr int32_t kSlice3Start = static_cast<int32_t>(kSampleLength * 3 / 4);
/// @}

/// @brief Open the slicer on @p sample, the way `SampleBrowser::renderPreviewForTarget` does.
/// @param sample The sample the scene is opened on.
/// @param ops    The slice boundary; borrowed, and unwired again by `SceneReset`.
void open_slicer(Sample* sample, deluge::scenes::slicing::SliceOps& ops) {
	slicer_scene().open(sample);
	slicer_scene().set_ops(&ops);
}

/// @brief Place the three manual slices, on the first tick and only then.
///
/// **It has to be a `before_tick` hook and not fixture setup, and the reason is legacy's.**
/// `SlicerScene::on_enter` is `Slicer::focusRegained` (`slicer.cpp:59-72`) line for line, and that
/// function *resets the whole table* -- `numManualSlice = 1`, `currentSlice = 0`, every
/// `manualSlicePoints` entry zeroed. The push that opens the scene therefore erases anything placed
/// before it, on the device as much as here. So the slices go in on the first tick, after the open.
///
/// They cannot go in as a *gesture* either: the pad arm that adds one (`slicer.cpp:506-534`) walks
/// `Sound::voices()` for a live play position, which needs a running `AudioEngine` a spec process
/// has none of -- `slicer_scene_spec.cpp`'s file docstring records that as uncovered and seeds its
/// own table through the same front door, `SlicerScene::add_slice_at` (the port of `:535-551`).
/// @return The hook; `tests/ui-scripts/shell/slicer.txt`'s header carries the same three numbers.
std::function<void(uint32_t)> slicer_seed() {
	return [](uint32_t now_ms) {
		if (now_ms != 0) {
			return;
		}
		for (const int32_t p : {kSlice1Start, kSlice2Start, kSlice3Start}) {
			(void)slicer_scene().add_slice_at(p);
		}
	};
}

/// @brief The `RunOptions::before_tick` hook both count-in scripts run under: push the overlay on
///        the first tick, then walk `playbackHandler.count_in_visual()` down on a schedule.
///
/// **Both halves are stand-ins, and each is a stand-in for something different.** The push stands
/// in for wiring that does not exist -- nothing in `src/` pushes `kCountInSceneKind`, which
/// `count_in_scene.h` says outright and which is spec F's to supply. The schedule stands in for
/// `PlaybackHandler::considerPlaybackTiming`'s swung-tick tree (`playback_handler.cpp:1044-1051`),
/// which needs a running `AudioEngine`; `count_in_spec.cpp` writes the same field directly for the
/// same reason. Both are written into `tests/ui-scripts/shell/count_in.txt`'s header so the golden
/// can be read against something.
///
/// The push happens in `before_tick` rather than in `after_construct` because `after_construct` runs
/// *before* `Shell::init`, and an overlay pushed onto an empty stack would sit UNDER the root. The
/// two hooks therefore have to be installed together: `after_construct()` only records the `Shell*`
/// that `before_tick()` then pushes onto.
struct CountInHooks {
	Shell* shell = nullptr;

	CountInHooks() = default;
	CountInHooks(const CountInHooks&) = delete;
	CountInHooks& operator=(const CountInHooks&) = delete;

	std::function<void(Shell&)> after_construct() {
		return [this](Shell& s) { shell = &s; };
	}
	std::function<void(uint32_t)> before_tick() {
		return [this](uint32_t now_ms) {
			if (now_ms == 0 && shell != nullptr) {
				(void)shell->stack().push(SceneId{overlay::kCountInSceneKind, 0});
			}
			playbackHandler.currentVisualCountForCountIn =
			    now_ms < 200 ? 0 : (now_ms < 500 ? 4 : (now_ms < 800 ? 3 : 0));
		};
	}
};

/// @brief `RunOptions` pointed at @p id on the shipped 128x48 board.
uitest::RunOptions opts_for(SceneId id) {
	return uitest::RunOptions{
	    .tick_ms = 10,
	    .board = &deluge::ui::shell::kDelugeBoard,
	    .profile = &deluge_profile(),
	    .initial_scene = id,
	};
}

/// @brief `opts_for`, on the 128x64 panel.
uitest::RunOptions opts_for_64(SceneId id) {
	static const DelugeBoard board = board_64();
	return uitest::RunOptions{
	    .tick_ms = 10,
	    .board = &board,
	    .profile = &profile_64(),
	    .initial_scene = id,
	};
}

/// @brief Attach @p sample to @p f's audio clip the way `AudioClip::setupPlaybackBounds` leaves it,
///        at the one scroll/zoom/length triple a spec process can actually scan.
///
/// @warning **The 48/768 pair is load-bearing, and getting it wrong makes the goldens vacuous.**
///          `find_peaks_per_col` takes its cached whole-cluster arm only when a display column spans
///          two or more clusters (`model/sample/waveform_scan.cpp:220-230`); anywhere else it opens
///          a `SampleFrameReader`, which fails with no card and makes the whole scan return false --
///          so both the pads and the OLED go black and every pad comparison passes on nothing.
///          `audio_clip_scene_spec.cpp`'s `Sentinel` docstring carries the full arithmetic and the
///          two review rounds that produced it; this is that fixture's own pair.
void attach_audio_sample(spec::AudioSongFixture& f, Sample* sample) {
	f.clip->sampleHolder.audioFile = sample;
	f.clip->sampleHolder.startPos = 0;
	f.clip->sampleHolder.endPos = static_cast<int64_t>(kSampleLength);
	// `SampleHolder::setAudioFile` normally computes this; `kMaxSampleValue` is the identity, and
	// zero is a SIGFPE in `getLengthInSamplesAtSystemSampleRate` (`sample_holder.cpp:94-101`).
	f.clip->sampleHolder.neutralPhaseIncrement = kMaxSampleValue;
	f.clip->sampleControls.reversed = false;
	f.clip->recorder = nullptr;
	f.clip->renderData.xScroll = -1;
	f.clip->renderData.xZoom = -1;
	f.clip->activeIfNoSolo = true;
	f.clip->soloingInSessionMode = false;
	f.clip->armState = ArmState::OFF;
	f.clip->loopLength = 768;
	f.clip->originalLength = 768;
	f.clip->lastProcessedPos = 0;
	f.song->xScroll[NAVIGATION_CLIP] = 0;
	f.song->xZoom[NAVIGATION_CLIP] = 48;
	f.song->tripletsOn = false;
	playbackHandler.playbackState = 0;
	playbackHandler.recording = RecordingMode::OFF;
	playbackHandler.metronomeOn = false;
	Buttons::recordButtonPressUsedUp = false;
	// `Clip::getLivePos()`'s SIGFPE guard -- `audio_clip_scene_spec.cpp`'s `Rig` sets the same
	// latch for the same reason, and `audio_clip.txt` needs it because that script starts real
	// playback and `AudioClipScene::compute_tick_square` then asks the clip where it is.
	currentlyActioningSwungTickOrResettingPlayPos = true;
	PadLEDs::flashCursor = FLASH_CURSOR_SLOW;
	PadLEDs::clearTickSquares(/*shouldSend=*/false);
}

} // namespace

// clang-format off
describe scene_scripts("scenes E4 level-2 acceptance scripts", $ {

	// ---- AudioClipScene ------------------------------------------------------------------------

	it("AudioClipScene: the root scene boots, the end marker goes up, the length edits, and BACK "
	   "over a held X-encoder clears the clip", _ {
		reset_globals();
		install_display();
		SceneReset reset;
		spec::QuietAudio quiet;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		attach_audio_sample(f, make_sample());
		audio_clip_scene().open(f.clip, f.song);

		RealServices svc;
		svc.song = f.song;
		svc.mod_target = f.clip;
		uitest::RunOptions opts = opts_for(SceneId{kAudioClipSceneKind, 0});
		opts.after_construct = svc.installer();

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "audio_clip", scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{7});
		// **The transport really ran**, which is what makes `after_record`/`after_play`'s LED lines
		// evidence rather than decoration: `services::GlobalControls` called
		// `playbackHandler.recordButtonPressed()` and `playButtonPressed()` for real (the fixture
		// holds `spec::QuietAudio`, which disarms the audio routine those reach, and stubs nothing
		// else). Read back from `playbackHandler`, not from the service.
		expect(playbackHandler.recording == RecordingMode::NORMAL).to_be_true();
		expect(playbackHandler.playbackState != 0).to_be_true();
	});

	it("AudioClipScene: the same scene at 64 rows", _ {
		reset_globals();
		install_display();
		SceneReset reset;
		spec::QuietAudio quiet;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		attach_audio_sample(f, make_sample());
		audio_clip_scene().open(f.clip, f.song);

		RealServices svc;
		svc.song = f.song;
		svc.mod_target = f.clip;
		uitest::RunOptions opts = opts_for_64(SceneId{kAudioClipSceneKind, 0});
		opts.after_construct = svc.installer();

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "audio_clip_64", scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{2});
	});

	// ---- SlicerScene ---------------------------------------------------------------------------

	it("SlicerScene: the count edits in REGION, X_ENC switches to MANUAL, a slot selects a slice, "
	   "and the horizontal encoder moves its start", _ {
		reset_globals();
		install_display();
		SceneReset reset;
		spec::QuietAudio quiet;
		ScriptOps ops;
		open_slicer(make_sample(), ops);
		// A song for the services to be told about. The slicer is not a root scene and is not
		// showing this clip -- but on the device `View`'s mod target is whatever clip the slicer was
		// opened *from*, so a service pointed at one is what the hardware actually has here.
		spec::SongFixture song = spec::make_synth_song();
		RealServices svc;
		svc.song = song.song;
		svc.mod_target = song.clip;

		uitest::RunOptions opts = opts_for(SceneId{kSlicerSceneKind, 0});
		opts.before_tick = slicer_seed();
		opts.after_construct = svc.installer();
		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "slicer", scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{5});
		// The gestures really landed, not merely drew: REGION's four detents, the mode toggle, the
		// slot press and the thirty-detent start move, each read back from the scene's own state.
		expect(slicer_scene().num_clips()).to_equal(int32_t{20});
		expect(slicer_scene().mode() == Mode::Manual).to_be_true();
		expect(slicer_scene().current_slice()).to_equal(int32_t{1});
		expect(slicer_scene().slice(1).start_pos).to_equal(kSlice1Start + 30 * 100);
	});

	it("SlicerScene: the same scene at 64 rows", _ {
		reset_globals();
		install_display();
		SceneReset reset;
		spec::QuietAudio quiet;
		ScriptOps ops;
		open_slicer(make_sample(), ops);
		spec::SongFixture song = spec::make_synth_song();
		RealServices svc;
		svc.song = song.song;
		svc.mod_target = song.clip;

		uitest::RunOptions opts = opts_for_64(SceneId{kSlicerSceneKind, 0});
		opts.before_tick = slicer_seed();
		opts.after_construct = svc.installer();
		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "slicer_64", scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{2});
	});

	// ---- ParamPopupScene, pushed by services::ModKnobs ------------------------------------------

	it("ParamPopupScene: a gold-knob turn reaches ModKnobs, which writes the param and pushes the "
	   "popup; the popup then times itself out", _ {
		reset_globals();
		install_display();
		SceneReset reset;
		spec::QuietAudio quiet;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		attach_audio_sample(f, make_sample());
		audio_clip_scene().open(f.clip, f.song);

		RealServices svc;
		svc.song = f.song;
		svc.mod_target = f.clip;
		uitest::RunOptions opts = opts_for(SceneId{kAudioClipSceneKind, 0});
		opts.after_construct = svc.installer();

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "param_popup", scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{4});

		// The popup is a process singleton, and the run's last frame is the one AFTER its deadline
		// passed -- so it must have taken itself down. Asserted rather than cleared: a popup still
		// showing here is a `Remove` that did not fire, which is exactly the defect this script's
		// last frame exists to catch (`shell/scene.h`'s warning on `Pop` vs `Remove`).
		expect(overlay::param_popup_scene().remaining_ms()).to_equal(uint32_t{0});
		expect(overlay::param_popup_scene().has_pending()).to_be_false();
		// It drew something, and it drew the SECOND value. An empty name is the shape a popup pushed
		// over a param the target could not resolve would have; a value of "26" here is the shape
		// the defect below had, and this pair says in words what `popup_moved`'s pixels say.
		expect(std::string(overlay::param_popup_scene().shown_name())).to_equal(std::string("Master level"));
		expect(std::string(overlay::param_popup_scene().shown_value())).to_equal(std::string("45"));
		// ---------------------------------------------------------------------------------------
		// **What `popup_moved` guards, and how it was found.** Task 13's first cut of this script
		// had no such frame: a second detent on an already-open popup moved the scene's text from
		// "26" to "45" and **nothing repainted the panel**, so the golden showed "26" while
		// `shown_value()` said "45". The narrow cause was the arbitration's immediate-commit arm
		// alone -- the throttled arm (a turn inside `kMinUpdateIntervalMs`) arms
		// `pending_flush_remaining_ms_`, which `next_timer()` reports and `timer_repaints()`
		// resolves to `{.oled = true}`, so a *fast* turn repainted itself and only a slow one did
		// not. `ParamPopupScene::show` now returns whether it committed and
		// `ModKnobs::mod_encoder_action` files `Action::Kind::Repaint` on `SurfaceRole::Main` when
		// it did (legacy's `display->displayNotification(...)`, `view.cpp:1245`, being a write AND
		// a blit). Mutation-proved from both ends: deleting that `Repaint` turns `popup_moved` back
		// into a copy of `popup_shown` and this script fails, and `mod_knobs_spec.cpp`'s
		// "files an OLED repaint for every committed popup update" example fails with it.
	});

	it("ParamPopupScene: the same popup over the same base scene at 64 rows", _ {
		reset_globals();
		install_display();
		SceneReset reset;
		spec::QuietAudio quiet;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		attach_audio_sample(f, make_sample());
		audio_clip_scene().open(f.clip, f.song);

		RealServices svc;
		svc.song = f.song;
		svc.mod_target = f.clip;
		uitest::RunOptions opts = opts_for_64(SceneId{kAudioClipSceneKind, 0});
		opts.after_construct = svc.installer();

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "param_popup_64", scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{2});
	});

	// ---- a menu and a real ModKnobs in the same run ---------------------------------------------

	it("a gold turn in a MENU with the real services: the scene claims it, ModKnobs never sees it, "
	   "and no popup is pushed", _ {
		// **E4 final-review fix wave, the ruling's step 1.** Every menu golden in
		// `menu_scripts_spec.cpp` was recorded with `Services{}` -- three `NullService`s -- so none
		// of them can show what a menu does with a service that also wants gold-knob turns present.
		// This is that frame, on the same node `menu_int.txt` opens.
		reset_globals();
		install_display();
		SceneReset reset;
		spec::QuietAudio quiet;
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::saturation, ctx);
		const int32_t before = bindings::read(nodes::saturation, ctx);
		// **Drain the popup first, and say why.** `ParamPopupScene` is a process singleton and
		// `param_popup_64` above quits at 300 ms with 1.8 s still on its lifetime, so a bare
		// `remaining_ms() == 0` here reads the PREVIOUS example's popup and fails for a reason that
		// has nothing to do with this one. (It did, on the first run of this block -- 1850 ms of a
		// popup this script never raised.) Two ticks longer than `kPopupLifetimeMs` puts it in the
		// state a fresh process would have, and the same assertion after the run is then about this
		// run alone.
		overlay::param_popup_scene().tick(60'000);
		expect(overlay::param_popup_scene().remaining_ms()).to_equal(uint32_t{0});

		RealServices svc;
		svc.song = song.song;
		svc.mod_target = song.clip;
		uitest::RunOptions opts = opts_for(SceneId{SceneKind::FirstApp, menu::node_id(nodes::saturation)});
		opts.after_construct = svc.installer();

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "menu_gold_turn", scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{3});

		// **The scene got the turn.** `+2` detents through `MenuScene`'s `int_value` widget is the
		// same arithmetic `menu_int`'s block derives: `edges_per_detent`(2) * 2 = 4 raw edges, and
		// `int_value`'s coarse step is `range.step` (1), clamped to `IntRange{0,15,1}`. The `+1` at
		// 100 ms is swallowed by `pretend_knobs_untouched()`, so it contributes nothing -- if that
		// debounce ever stops applying, this total is what notices.
		expect(bindings::read(nodes::saturation, ctx)).to_equal(std::min(before + 4, 15));

		// **`ModKnobs` never saw it, so nothing was pushed.** The gold turn is one of the six
		// widget-owned inputs the shell holds back and delivers after render, skipping any input
		// `ctx().claims` shows a widget already took (`shell/README.md`, `Router::route_post_render`)
		// -- and `MenuScene`'s `int_value` widget takes this one. So the service is never reached,
		// and `ParamPopupScene` stays off the stack: `idle` and `after_gold_turn` carry exactly one
		// `scene` line and `after_back` carries none, which is the recorded half of this claim (a
		// popup pushed by the turn would still be stacked at `after_back`, since its 2000 ms
		// lifetime outlasts the run).
		expect(overlay::param_popup_scene().remaining_ms()).to_equal(uint32_t{0});
		expect(overlay::param_popup_scene().has_pending()).to_be_false();
		// ---------------------------------------------------------------------------------------
		// **This is NOT a guard on the router's chain order, and that was measured rather than
		// assumed.** It was written to be one: E4 task 3 corrected the chain from
		// `mod_knobs -> top scene` to `top scene -> mod_knobs` (shell spec §15.4), and the ruling
		// asked for a recorded frame behind it. Swapping the two steps back inside
		// `Router::run_chain` and re-running leaves all three goldens byte-identical and every
		// assertion here green -- because the claim check that decides this gesture runs in
		// `Router::route_post_render`, BEFORE `run_chain` is entered, and a claimed event never
		// reaches the chain in either order.
		//
		// The order is observable only where a scene and `mod_knobs` could both claim the same
		// event from `handle()`, and **no real scene in the tree claims a gold turn there**:
		// `AudioClipScene` declines it explicitly (`audio_clip_scene.cpp:247-251`), `MenuScene` has
		// no gold arm at all, and `grep -rn "GoldUpper\|GoldLower" src/scenes/` finds nothing
		// outside `ModKnobs` itself. So the order stays pinned by `router_spec.cpp`'s "a scene that
		// claims a gold-knob turn gets it, and mod_knobs never sees it", which uses a spec-local
		// scene that does -- and a recorded frame for it cannot exist until a ported scene has such
		// an arm. That is the finding, and it is in this fix wave's report.
		//
		// What this DOES pin is still worth its three goldens: installing the real services does
		// not silently steal a menu's edit encoder. That is a claim no `Services{}` golden can
		// make, and it is the one that will break first if the widget-claim path changes.
	});

	// ---- ScreensaverScene, pushed by shell::Screensaver -----------------------------------------

	it("ScreensaverScene: 200 ms of nothing pushes it, quiet ticks animate it, and the pad that "
	   "wakes it still reaches the scene underneath", _ {
		reset_globals();
		install_display();
		SceneReset reset;
		spec::QuietAudio quiet;
		SavedScreensaverMode mode{::ScreensaverMode::DELUGE};
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		attach_audio_sample(f, make_sample());
		audio_clip_scene().open(f.clip, f.song);

		RealServices svc;
		svc.song = f.song;
		svc.mod_target = f.clip;
		uitest::RunOptions opts = opts_for(SceneId{kAudioClipSceneKind, 0});
		opts.after_construct = svc.installer();
		opts.config.screensaver_idle_ms = 200;
		opts.config.seed = 0x7777; // the seed the per-kind unit goldens were recorded from

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "screensaver", scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{4});
		// The end marker went up, which is the whole claim of the last frame: the waking press was
		// not swallowed by the wake.
		expect(audio_clip_scene().end_marker_visible()).to_be_true();
	});

	it("ScreensaverScene: the animation at 64 rows", _ {
		reset_globals();
		install_display();
		SceneReset reset;
		spec::QuietAudio quiet;
		SavedScreensaverMode mode{::ScreensaverMode::DELUGE};
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		attach_audio_sample(f, make_sample());
		audio_clip_scene().open(f.clip, f.song);

		RealServices svc;
		svc.song = f.song;
		svc.mod_target = f.clip;
		uitest::RunOptions opts = opts_for_64(SceneId{kAudioClipSceneKind, 0});
		opts.after_construct = svc.installer();
		opts.config.screensaver_idle_ms = 200;
		opts.config.seed = 0x7777; // the seed the per-kind unit goldens were recorded from

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "screensaver_64", scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{2});
	});

	// ---- CountInScene ---------------------------------------------------------------------------

	it("CountInScene: the poll notices the digit change and the end of the count-in on quiet ticks "
	   "alone, and removes itself when the count reaches zero", _ {
		reset_globals();
		install_display();
		SceneReset reset;
		spec::QuietAudio quiet;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		attach_audio_sample(f, make_sample());
		audio_clip_scene().open(f.clip, f.song);
		playbackHandler.currentVisualCountForCountIn = 0;

		CountInHooks hooks;
		RealServices svc;
		svc.song = f.song;
		svc.mod_target = f.clip;
		uitest::RunOptions opts = opts_for(SceneId{kAudioClipSceneKind, 0});
		opts.after_construct = [&](Shell& shell) {
			hooks.after_construct()(shell);
			svc.installer()(shell);
		};
		opts.before_tick = hooks.before_tick();

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "count_in", scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{4});
		expect(overlay::count_in_scene().showing()).to_be_false();
	});

	it("CountInScene: the digit box over the same base scene at 64 rows", _ {
		reset_globals();
		install_display();
		SceneReset reset;
		spec::QuietAudio quiet;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		attach_audio_sample(f, make_sample());
		audio_clip_scene().open(f.clip, f.song);
		playbackHandler.currentVisualCountForCountIn = 0;

		CountInHooks hooks;
		RealServices svc;
		svc.song = f.song;
		svc.mod_target = f.clip;
		uitest::RunOptions opts = opts_for_64(SceneId{kAudioClipSceneKind, 0});
		opts.after_construct = [&](Shell& shell) {
			hooks.after_construct()(shell);
			svc.installer()(shell);
		};
		opts.before_tick = hooks.before_tick();

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "count_in_64", scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{2});
	});
});
// clang-format on

DELUGE_SPEC(scene_scripts_spec, scene_scripts)
