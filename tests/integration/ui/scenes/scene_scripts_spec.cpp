/// @file
/// @brief Level-2 acceptance scripts for the three overlay scenes, the two root scenes, and the
///        three **real** shell services.
///
/// The mechanism is `menu_scripts_spec.cpp`'s exactly: a UI script (`tests/ui-scripts/shell/`)
/// driven through the real `Shell` over `uitest::FakeSurfaceSet`, with every `snapshot` step's
/// `.pbm` + `.state` diffed against `tests/golden/ui/shell/<name>/`. The only difference is what
/// gets passed in.
///
// clang-format off
/// | script | subject | how the scene gets on the stack |
/// |---|---|---|
/// | `audio_clip` | `audio_clip::AudioClipScene` | `RunOptions::initial_scene` -- it is a ROOT scene, and a root is what the shell is booted onto |
/// | `slicer` | `slicing::SlicerScene` | `RunOptions::initial_scene`, after the app's slicer host opens it on a sample the preview drum holds -- legacy's `SampleBrowser` opens it the same way, `open()` first and the push second |
/// | `param_popup` | `overlay::ParamPopupScene` **pushed by `services::ModKnobs`** | nothing in the script pushes it: a gold-knob turn reaches the service, and `GuestKnobPresentation::show_value_popup` pushes the scene |
/// | `screensaver` | `overlay::ScreensaverScene` **pushed by `shell::Screensaver`** | nothing in the script pushes it either: the script simply stops touching anything for longer than `ShellConfig::screensaver_idle_ms` |
/// | `count_in` | `overlay::CountInScene` | **the spec pushes it**, from `RunOptions::before_tick`, over an `AudioClipScene` root. Nothing in the tree pushes this kind -- see the honesty note below |
/// | `menu_gold_turn` | `menu::MenuScene` and a real `services::ModKnobs` in one run | `RunOptions::initial_scene` at `nodes::saturation`, exactly as `menu_scripts_spec.cpp` opens a menu. The only recorded frame on this stack with a menu and a real service together |
// clang-format on
///
/// Each script has a `_64` sibling that re-runs the geometry-sensitive frames on the 128x64 panel
/// (`board_64()`/`profile_64()`, the same pair `slicer_scene_spec.cpp` and `count_in_spec.cpp`
/// keep). They are separate script *files* because `expect_script_goldens` derives both the script
/// path and the golden directory from one name; the 64-row files say in their own headers which
/// frames they kept and why the rest would have added nothing.
///
/// @par The real services, and why they are installed after construction
///
/// **Every other script on this stack runs with `Services{}` -- three null services.** Their goldens
/// were recorded against null services, so the scripts here are the only ones that get real ones.
///
/// `services::ModKnobs`, `GlobalControls` and `MidiLearn` each take a `SceneStack&` and (for the
/// first two) an `InputLowering&` at construction, and they *use* them: `ModKnobs`' `GuestKnobPresentation`
/// pushes the param popup onto that stack, and `GuestKnobPresentation::suppress_value_popup` and
/// `ModKnobs::push` read `stack_.input_top()`; `GlobalControls::handle` gates
/// its `View::buttonAction` group on `stack_.input_top().is_root()`; `MidiLearn::handle` reads the
/// same. The only `SceneStack` and `InputLowering` in an application are the ones the `Shell`
/// **owns** (`ui/shell/shell.h`'s `stack_`, `lowering_`), so a real service cannot be built before the
/// shell it serves. `Shell::set_services` (`ui/shell/shell.h`) re-seats all three of the things that
/// hold the slots -- the shell's copy, the `Router`'s, and the deduplicated `service_list_` -- and
/// `RealServices::installer()` calls it from `after_construct`, handing the shell `Services{}` at
/// construction, exactly as an application does.
///
/// @par What passing them changed, visibly -- and what it did not
///
/// **An LED declared `Off` is not written to `.state` at all** (`unit/ui/shell/support/script_runner.h`'s `state_text`:
/// "an LED declared off is not shown at all"), so "the services are wired in" and "the services are
/// visible in a golden" are two different claims and only the second is worth making. What is
/// actually in these 34 snapshots:
///
///   * `led Mod0 on` / `led Mod1 on` and both `indicator` lines, in **every** frame -- `ModKnobs`,
///     and they differ between scripts because the mod target does (`Mod1` and a non-zero lower ring
///     in `slicer`, whose target is a synth clip; `Mod0` in the audio-clip scripts).
///   * `led Record on` and `led Play on`, in `audio_clip`'s last three frames -- `GlobalControls`
///     (`GlobalControls::leds`, the port of `PlaybackHandler::setLedStates`),
///     after the script presses RECORD and PLAY for real. That is reachable in exactly one script
///     here: `GlobalControls::handle` gates its `View::buttonAction` group on
///     `stack_.input_top().is_root()` and `AudioClipScene`
///     is the only root scene that exists.
///   * **Nothing from `MidiLearn`.** LEARN is off unless learn mode is entered, and no script here
///     enters it -- `MidiLearn` is wired in and contributes no observable line. Stated rather than
///     implied: no golden contains a LEARN line.
///
/// And `param_popup` is the script that could not exist at all without the services: the popup is
/// *pushed by a service*, so with `Services{}` a gold-knob turn produces no frame of any kind.
///
/// @par 34 snapshots are 21 distinct panels, and eleven of them are one panel
///
/// Counted, not estimated (`md5sum` over the ten golden directories): the 34 `.pbm` files hold **21
/// distinct panels**. Three groups are repeats.
///
///   * **Eleven**, all the same panel -- `AudioClipScene::render_oled`'s output name (the port of
///     `View::displayOutputName` in `views/view.cpp`). It is
///     `audio_clip/{idle, end_marker, after_play, after_record, after_length_edit}`,
///     `param_popup/{idle, popup_expired}`, `screensaver/{idle, after_wake}` and `count_in/{idle, after_zero}`. Four of
///     the five scripts boot onto `AudioClipScene`, and its OLED half does not move for anything those scripts do: the
///     play cursor, the markers and the length edit are all GRID, and the two overlays that cover it are gone again by
///     the frame after.
///   * **Two**: `slicer/{manual, slice_selected}`.
///   * **Three**: the 64-row idles, `{audio_clip_64, param_popup_64, count_in_64}/idle`.
///
/// **None is vacuous, and the check is per script.** Within any one script no two snapshots are the
/// same frame -- the `.state` half differs at every step, which is what each script's
/// snapshots show. *Across* scripts five are the same frame in both halves, and that is
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
///     scene, and inventing one would be a push no user could perform.
///   * **The count-in digit does not move on its own in a spec process.** The scene is a poll of the host transport
///     port's `count_in_visual()`, which reads `playbackHandler.count_in_visual()`, and the only writer of that number
///     is `PlaybackHandler::actionSwungTick`'s count-in branch, which needs a running `AudioEngine`.
///     `count_in_spec.cpp` writes the field directly for that reason; `count_in.txt` does the same through
///     `RunOptions::before_tick`, which is the level-2 shape of the same stand-in. The script's own header carries the
///     schedule, so the golden is still readable against something written down.
///
/// @note Like `menu_scripts_spec.cpp`, this file may include `gui/` and `hid/`; the library may not.

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host/host_ports.h"
#include "support/peak_seed.h"
#include "support/popup_capture.h"
#include "ui/scenes/audio_clip/audio_clip_scene.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/overlay/count_in_scene.h"
#include "ui/scenes/overlay/param_popup_scene.h"
#include "ui/scenes/overlay/screensaver_scene.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/services/global_controls.h"
#include "ui/scenes/services/midi_learn.h"
#include "ui/scenes/services/mod_knobs.h"
#include "ui/scenes/slicing/slicer_scene.h"

#include "base/notify/dirty_set.h"
#include "base/notify/notice_queue.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

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
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "storage/flash_storage.h"

#include "support/song_fixture.h"

// Before <cppspec.hpp>: `card_access.h` reaches `util/container/ordered_pos_vector.h`, whose
// locals are named `it` -- CppSpec's own DSL keyword. Same placement rule every spec in this
// directory runs under.
#include "support/card_access.h"

#include "support/real_services.h"
#include "support/slicer_host_open.h"
#include "unit/ui/shell/support/script_runner.h"

#include "cppspec.hpp"

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include "support/menu_context.h"
#include "support/spec_main.h"

using namespace deluge;

using namespace deluge::ui::scenes;
using deluge::ui::scenes::audio_clip::audio_clip_scene;
using deluge::ui::scenes::audio_clip::kAudioClipSceneKind;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::slicing::kSlicerSceneKind;
using deluge::ui::scenes::slicing::Mode;
using deluge::ui::scenes::slicing::slicer_scene;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
namespace bindings = deluge::ui::scenes::menu::bindings;
namespace nodes = deluge::ui::scenes::menu::nodes;

/// @brief `PlaybackHandler`'s "we are inside a swung tick" latch, declared rather than included
///        exactly as `audio_clip_scene_spec.cpp` and `timeline_spec.cpp` declare it.
///
/// `audio_clip.txt` starts real playback, and `AudioClipScene::compute_tick_square` then asks
/// `Clip::getLivePos()` where the clip is -- which, on a song whose transport a spec started but
/// whose audio thread never runs, reaches `getActualSwungTickCount`'s external-clock swing branch
/// and divides by `(uint32_t)(50 + currentSong->project.groove.swing_amount)`
/// (`PlaybackHandler::getActualSwungTickCount`): a **SIGFPE**, not a failure. Holding the latch takes legacy's
/// own early return (`PlaybackHandler::getNumSwungTicksInSinceLastActionedSwungTick`).
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
		static constexpr const deluge::ui::widgets::core::Style* kStyles[] = {
		    &deluge::ui::widgets::core::kDelugeStyle64};
		BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

// --- the sample ---------------------------------------------------------------------------------

/// `marker_scene_spec.cpp`'s fixture, for its reason: thirty-two clusters' worth of frames with the sample's peak
/// index seeded (`support/peak_seed.h`), so every column is answered from the index and no spec process ever reaches
/// the card.
constexpr size_t kClusterSize = 32768;
constexpr int32_t kBytesPerFrame = 2; // 16-bit mono
constexpr uint64_t kSampleLength = static_cast<uint64_t>(kClusterSize) * 32 / kBytesPerFrame;
constexpr int32_t kNumClusters = 32;

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

void install_display() {
	if (display == nullptr) {
		display = &deluge::ui::scenes::spec::quiet_display();
	}
}

/// @brief Reset the two process-global channels and the language, exactly as
///        `menu_scripts_spec.cpp`'s `reset_globals` does.
void reset_globals() {
	menu_scene().reset_process_state(); // each example starts with the rows a boot has
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::base::notify::Notice discard;
	while (deluge::base::notify::notices().pop(discard)) {}
	deluge::base::notify::dirty().drain();
}

// --- the services ---------------------------------------------------------------------------------

/// The fixture that installs the real three. It lives in `support/real_services.h` so
/// `menu_scripts_spec.cpp` can install the same three -- see that header for what it is and why.
using deluge::ui::scenes::spec::RealServices;

/// @brief Everything a script run needs put back afterwards, in one guard.
///
/// The scenes are process singletons and `run_script` already tears the *stack* down; what it
/// cannot know about is the model state a fixture pointed them at. `audio_clip_scene().open(std::nullopt,
/// nullptr)` and `set_ports(nullptr)` are `audio_clip_scene_spec.cpp`'s own teardown, and the matrix/pad reset is
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
		audio_clip_scene().open(std::nullopt, nullptr);
		audio_clip_scene().set_ports(nullptr);
		slicer_scene().set_ops(nullptr);
		// The transport half, restored in `global_controls_spec.cpp`'s `reset_transport` order and
		// for its reasons -- `audio_clip.txt` really does press RECORD and PLAY through
		// `services::GlobalControls`, so `playbackHandler` is genuinely running by the end of it.
		// `setForegroundRecording` matters even though no script here starts an output recording:
		// one left standing suppresses the RECORD LED for every later example
		// (`PlaybackHandler::setLedStates`), which is exactly the cross-contamination this file's
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
struct ScriptOps final : deluge::ui::scenes::slicing::SliceOps {
	void stop_previewing() override {}
	void kill_voices() override {}
	void restore_full_range() override {}
	void audition(const deluge::ui::scenes::slicing::PreviewRequest&) override {}
	bool begin_commit(const deluge::ui::scenes::slicing::SliceCommit&) override { return false; }
	std::optional<int32_t> play_position() override { return std::nullopt; }
};

/// @brief Save and restore `FlashStorage::screensaverMode`, which the host device port answers the screensaver
///        scene's mode row from. `screensaver_scene_spec.cpp`'s `set_mode`, as a guard; it installs the host's device
///        port on the scene, because the specs share a process and cannot rely on the boot wiring having run.
struct SavedScreensaverMode {
	::ScreensaverMode saved = FlashStorage::screensaverMode;
	explicit SavedScreensaverMode(::ScreensaverMode mode) {
		FlashStorage::screensaverMode = mode;
		overlay::screensaver_scene().set_device(deluge::gui::shell_host::host_ports().device);
	}
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

/// @brief Open the slicer on @p held's file, the way main's `SampleBrowser::enterKeyPress` does
///        (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`): through the app's slicer host, which the
///        waveforms port's SLICE address then resolves through.
/// @param held The sample in the preview drum; it must outlive the run.
/// @param ops  The slice boundary; borrowed, and unwired again by `SceneReset`.
void open_slicer(const deluge::ui::scenes::spec::SliceHeld& held, deluge::ui::scenes::slicing::SliceOps& ops) {
	slicer_scene().set_ports(&deluge::gui::shell_host::host_ports());
	held.open();
	slicer_scene().set_ops(&ops);
}

/// @brief Place the three manual slices, on the first tick and only then.
///
/// **It has to be a `before_tick` hook and not fixture setup, and the reason is main's.** `SlicerScene::on_enter` is
/// `Slicer::focusRegained` (`origin/main:src/deluge/gui/ui/slicer.cpp`) line for line, and that function *resets the
/// whole table* -- `numManualSlice = 1`, `currentSlice = 0`, every `manualSlicePoints` entry zeroed. The push that
/// opens the scene therefore erases anything placed before it, on the device as much as here. So the slices go in on
/// the first tick, after the open.
///
/// They cannot go in as a *gesture* either: the pad arm of `Slicer::padAction` that adds one walks a sound's voices
/// for a live play position, which needs a running `AudioEngine` a spec process has none of --
/// `slicer_scene_spec.cpp`'s file docstring records that as uncovered and seeds its own table through the same front
/// door, `SlicerScene::add_slice_at` (the port of `Slicer::padAction`'s manual-slice arm in
/// `origin/main:src/deluge/gui/ui/slicer.cpp`).
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
///        the first tick, then walk `playbackHandler.count_in_visual()` down on a schedule. It installs the host's
///        transport port on the scene, which reads that count.
///
/// **Both halves are stand-ins, and each is a stand-in for something different.** The push stands in for wiring that
/// does not exist -- nothing in `src/` pushes `kCountInSceneKind`, which `count_in_scene.h` says outright. The schedule
/// stands in for `PlaybackHandler::actionSwungTick`'s count-in branch, which needs a running `AudioEngine`;
/// `count_in_spec.cpp` writes the same field directly for the same reason. Both are written into
/// `tests/ui-scripts/shell/count_in.txt`'s header so the golden can be read against something.
///
/// The push happens in `before_tick` rather than in `after_construct` because `after_construct` runs
/// *before* `Shell::init`, and an overlay pushed onto an empty stack would sit UNDER the root. The
/// two hooks therefore have to be installed together: `after_construct()` only records the `Shell*`
/// that `before_tick()` then pushes onto.
struct CountInHooks {
	Shell* shell = nullptr;

	CountInHooks() { overlay::count_in_scene().set_transport(deluge::gui::shell_host::host_ports().transport); }
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
///          A column whose ends fall inside a 256-frame block reads those frames
///          (`deluge::sample::waveform_columns`), which fails with no card and leaves the column
///          pending -- so both the pads and the OLED go black and every pad comparison passes on nothing.
///          `audio_clip_scene_spec.cpp`'s `Sentinel` docstring carries the full arithmetic;
///          this is that fixture's own pair.
void attach_audio_sample(spec::AudioSongFixture& f, Sample* sample) {
	f.clip->sampleHolder.audioFile = sample;
	sample->addReason(); // the lease `SampleHolder::setAudioFile` takes
	f.clip->sampleHolder.startPos = 0;
	f.clip->sampleHolder.endPos = static_cast<int64_t>(kSampleLength);
	// `SampleHolder::setAudioFile` normally computes this; `kMaxSampleValue` is the identity, and
	// zero is a SIGFPE in `getLengthInSamplesAtSystemSampleRate` (`sample_holder.cpp`).
	f.clip->sampleHolder.neutralPhaseIncrement = kMaxSampleValue;
	f.clip->sampleControls.reversed = false;
	f.clip->recorder = nullptr;
	f.clip->activeIfNoSolo = true;
	f.clip->soloingInSessionMode = false;
	f.clip->armState = ArmState::OFF;
	f.clip->setLoopLength(768);
	f.clip->originalLength = 768;
	f.clip->lastProcessedPos = 0;
	f.song->persistentView.xScroll[NAVIGATION_CLIP] = 0;
	f.song->persistentView.xZoom[NAVIGATION_CLIP] = 48;
	f.song->persistentView.tripletsOn = false;
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
describe scene_scripts("scenes level-2 acceptance scripts", $ {

	// ---- AudioClipScene ------------------------------------------------------------------------

	it("AudioClipScene: the root scene boots, the end marker goes up, the length edits, and BACK "
	   "over a held X-encoder clears the clip", _ {
		reset_globals();
		install_display();
		SceneReset reset;
		spec::QuietAudio quiet;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		attach_audio_sample(f, make_sample());
		audio_clip_scene().set_ports(&deluge::gui::shell_host::host_ports());
		audio_clip_scene().open(deluge::project::ClipAddress{f.clip->id()}, &f.song->project);

		RealServices svc;
		svc.song = f.song;
		svc.mod_target = f.clip;
		uitest::RunOptions opts = opts_for(SceneId{kAudioClipSceneKind, 0});
		opts.after_construct = svc.installer();

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "audio_clip", ui::scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{7});
		// **The transport really ran**, which is what makes `after_record`/`after_play`'s LED lines
		// evidence rather than decoration: `services::GlobalControls` called
		// `playbackHandler.recordButtonPressed()` and `playButtonPressed()` for real (the fixture
		// holds `spec::QuietAudio`, which keeps the audio routine still, and stubs nothing else). Read back from `playbackHandler`, not from the service.
		expect(playbackHandler.recording).to_equal(RecordingMode::NORMAL);
		expect(playbackHandler.playbackState).not_().to_equal(0);
	});

	it("AudioClipScene: the same scene at 64 rows", _ {
		reset_globals();
		install_display();
		SceneReset reset;
		spec::QuietAudio quiet;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		attach_audio_sample(f, make_sample());
		audio_clip_scene().set_ports(&deluge::gui::shell_host::host_ports());
		audio_clip_scene().open(deluge::project::ClipAddress{f.clip->id()}, &f.song->project);

		RealServices svc;
		svc.song = f.song;
		svc.mod_target = f.clip;
		uitest::RunOptions opts = opts_for_64(SceneId{kAudioClipSceneKind, 0});
		opts.after_construct = svc.installer();

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "audio_clip_64", ui::scenes::scene_registry(), svc.slots(), opts);
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
		const deluge::ui::scenes::spec::SliceHeld held{make_sample(), "SAMPLES/SLICE.WAV"};
		open_slicer(held, ops);
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
		    uitest::expect_script_goldens(self, "slicer", ui::scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{5});
		// The gestures really landed, not merely drew: REGION's four detents, the mode toggle, the
		// slot press and the thirty-detent start move, each read back from the scene's own state.
		expect(slicer_scene().num_clips()).to_equal(int32_t{20});
		expect(slicer_scene().mode()).to_equal(Mode::Manual);
		expect(slicer_scene().current_slice()).to_equal(int32_t{1});
		expect(slicer_scene().slice(1).start_pos).to_equal(kSlice1Start + 30 * 100);
	});

	it("SlicerScene: the same scene at 64 rows", _ {
		reset_globals();
		install_display();
		SceneReset reset;
		spec::QuietAudio quiet;
		ScriptOps ops;
		const deluge::ui::scenes::spec::SliceHeld held{make_sample(), "SAMPLES/SLICE.WAV"};
		open_slicer(held, ops);
		spec::SongFixture song = spec::make_synth_song();
		RealServices svc;
		svc.song = song.song;
		svc.mod_target = song.clip;

		uitest::RunOptions opts = opts_for_64(SceneId{kSlicerSceneKind, 0});
		opts.before_tick = slicer_seed();
		opts.after_construct = svc.installer();
		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "slicer_64", ui::scenes::scene_registry(), svc.slots(), opts);
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
		audio_clip_scene().set_ports(&deluge::gui::shell_host::host_ports());
		audio_clip_scene().open(deluge::project::ClipAddress{f.clip->id()}, &f.song->project);

		RealServices svc;
		svc.song = f.song;
		svc.mod_target = f.clip;
		uitest::RunOptions opts = opts_for(SceneId{kAudioClipSceneKind, 0});
		opts.after_construct = svc.installer();

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "param_popup", ui::scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{4});

		// The popup is a process singleton, and the run's last frame is the one AFTER its deadline
		// passed -- so it must have taken itself down. Asserted rather than cleared: a popup still
		// showing here is a `Remove` that did not fire, which is exactly the defect this script's
		// last frame exists to catch (`ui/shell/scene.h`'s warning on `Pop` vs `Remove`).
		expect(overlay::param_popup_scene().remaining_ms()).to_equal(uint32_t{0});
		expect(overlay::param_popup_scene().has_pending()).to_be_false();
		// It drew something, and it drew the SECOND value. An empty name is the shape a popup pushed
		// over a param the target could not resolve would have; a value of "26" here is the shape
		// a missing repaint leaves (below), and this pair says in words what `popup_moved`'s pixels say.
		expect(std::string(overlay::param_popup_scene().shown_name())).to_equal(std::string("Master level"));
		expect(std::string(overlay::param_popup_scene().shown_value())).to_equal(std::string("45"));
		// ---------------------------------------------------------------------------------------
		// **What `popup_moved` guards.** A second detent on an already-open popup moves the scene's text from "26" to
		// "45"; with nothing repainting the panel, the golden would show "26" while `shown_value()` says "45". Only the
		// arbitration's immediate-commit arm needs its own repaint -- the throttled arm (a turn inside
		// `kMinUpdateIntervalMs`) arms `pending_flush_remaining_ms_`, which `next_timer()` reports and
		// `timer_repaints()` resolves to `{.oled = true}`, so a *fast* turn repaints itself. `ParamPopupScene::show`
		// returns whether it committed and `GuestKnobPresentation::show_value_popup` files `Action::Kind::Repaint` on
		// `SurfaceRole::Main` when it did (`display->displayNotification(...)` in `View::displayModEncoderValuePopup`
		// being a write AND a blit). Mutation-proved from both ends: deleting that `Repaint` turns `popup_moved` back
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
		audio_clip_scene().set_ports(&deluge::gui::shell_host::host_ports());
		audio_clip_scene().open(deluge::project::ClipAddress{f.clip->id()}, &f.song->project);

		RealServices svc;
		svc.song = f.song;
		svc.mod_target = f.clip;
		uitest::RunOptions opts = opts_for_64(SceneId{kAudioClipSceneKind, 0});
		opts.after_construct = svc.installer();

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "param_popup_64", ui::scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{2});
	});

	// ---- a menu and a real ModKnobs in the same run ---------------------------------------------

	it("a gold turn in a MENU with the real services reaches ModKnobs, and pushes a popup", _ {
		// Every menu golden in `menu_scripts_spec.cpp` was
		// recorded with `Services{}` -- three `NullService`s -- so none of them can show what a menu
		// does with a service that also wants gold-knob turns present. This is that frame, on the
		// same node `menu_int.txt` opens.
		//
		// **Why this script pins a popup rather than a menu edit.** Legacy edits
		// a menu value with the *select* encoder; the gold knobs never touch a menu at all --
		// `SoundEditor::modEncoderAction` falls through to `View::modEncoderAction`
		// (`View::modEncoderAction`), which edits the mod-controllable's param instead. `int_value`
		// claims only the select turn (`ui/widgets/oled/values.cpp`'s `select_delta`), so the two
		// `GoldUpper` turns below are unclaimed by `MenuScene` and reach `services::ModKnobs`
		// through the routing chain `View::modEncoderAction` names.
		//
		// **Why a popup, where `menu_int.txt` still says none is pushed there.** `ModKnobs`'s port of "let's see if
		// we're editing the same param in the menu, if so, don't show pop-up" (main's
		// `View::modEncoderAction_existentParam`, `origin/main:src/deluge/gui/views/view.cpp`) is
		// `GuestKnobPresentation::suppress_value_popup` (`services/guest_knob_presentation.h`): it compares the turn's
		// param against `menu_scene().focused()->bind.param`. `nodes::saturation` is built with `int_item()`
		// (`nodes/sound_fx.h`), which never sets `bind.param` at all (`node.h`'s `int_item` builder) -- so
		// `bind.param.valid()` is false, `suppress_value_popup` returns false unconditionally, and the popup goes up. A
		// node built with `param()` instead, showing the exact modulation param GoldUpper points at, would suppress it;
		// this node can't, by construction, which is the case this script now pins.
		reset_globals();
		install_display();
		SceneReset reset;
		spec::QuietAudio quiet;
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::saturation, ctx);
		const int32_t before = bindings::read(nodes::saturation, ctx);
		// **Drain the popup first, and say why.** `ParamPopupScene` is a process singleton and
		// `param_popup_64` above quits at 300 ms with 1.8 s still on its lifetime, so a bare
		// `remaining_ms() == 0` here reads the PREVIOUS example's popup and fails for a reason that
		// has nothing to do with this one. (Otherwise it would see, say, 1850 ms of a
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
		    uitest::expect_script_goldens(self, "menu_gold_turn", ui::scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{3});

		// **`nodes::saturation` itself never moves.** `int_value` claims only the select turn now,
		// so the `+1`/`+2` `GoldUpper` steps below are entirely unclaimed by `MenuScene` -- and
		// `int_item()` never gave this node a `bind.param` for `ModKnobs` to write through even if
		// they did reach it (it has its own `get`/`set` pair instead, in `nodes/sound_fx.h`). So the
		// node this script opens is not, and cannot be, the thing a gold turn here moves.
		expect(bindings::read(nodes::saturation, ctx)).to_equal(before);

		// **`ModKnobs` DOES see the turn now, and shows it.** With `int_value` claiming only the
		// turn, the two `GoldUpper` steps reach `services::ModKnobs::turn_knob`, the counterpart of main's
		// `View::modEncoderAction` (`origin/main:src/deluge/gui/views/view.cpp`) -- the `+1` at 100 ms is swallowed
		// exactly as before, and the `+2` at 150 ms edits whatever real
		// modulation param GoldUpper is currently pointed at (not `nodes::saturation`, per above),
		// moving that param's own knob-indicator ring: `idle`'s `indicator upper` is `16`, and
		// `after_gold_turn`'s is `36`. `GuestKnobPresentation::suppress_value_popup`, the port
		// of main's same-param popup guard (`View::modEncoderAction_existentParam`), compares the turn's param against
		// `menu_scene().focused()->bind.param` -- and `nodes::saturation` has none (see above), so
		// the guard cannot suppress here and the popup goes up: `idle` carries one `scene` line,
		// `after_gold_turn` carries two (the popup pushed over the menu), and `after_back` carries
		// one again -- not zero, and not the menu's: the Back at depth 0 closes `MenuScene`
		// entirely, and the popup outlives it, its 2000 ms lifetime still running when the script's
		// own last tick (`quit` at 400 ms, 250 ms after the 150 ms push) ends the run.
		expect(overlay::param_popup_scene().remaining_ms()).to_equal(uint32_t{1750});
		expect(overlay::param_popup_scene().has_pending()).to_be_false();
		// ---------------------------------------------------------------------------------------
		// **This is NOT a guard on the router's chain order, and that was measured rather than
		// assumed.** The chain runs `top scene -> mod_knobs`, and a recorded frame might be expected
		// to pin that order. Swapping the two steps back inside
		// `Router::run_chain` and re-running leaves all three goldens byte-identical and every
		// assertion here green -- because the ownership check that decides this gesture runs in
		// `Shell::pump_input`, BEFORE `run_chain` is entered, and an owned event never reaches the
		// chain in either order.
		//
		// The order is observable only where a scene and `mod_knobs` could both claim the same
		// event from `handle()`, and **no real scene in the tree claims a gold turn there**:
		// `AudioClipScene` declines it explicitly (`AudioClipScene::handle`), `MenuScene` has
		// no gold arm at all, and `grep -rn "GoldUpper\|GoldLower" src/ui/scenes/` finds nothing
		// outside `ModKnobs` itself. So the order stays pinned by `unit/ui/shell/router_spec.cpp`'s "a scene that
		// claims a gold-knob turn gets it, and mod_knobs never sees it", which uses a spec-local
		// scene that does -- and a recorded frame for it cannot exist until a scene has such
		// an arm.
		//
		// What this DOES pin is still worth its three goldens: a gold turn taken inside a menu now
		// reaches the real `ModKnobs` and moves a real mod-controllable param, exactly as it does
		// everywhere else in the firmware -- and pushes the same popup it would over any other view,
		// because this particular node has nothing to suppress it with. That is a claim no
		// `Services{}` golden can make, and it is the one that will break first if `int_value` ever
		// starts claiming the gold knobs again.
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
		audio_clip_scene().set_ports(&deluge::gui::shell_host::host_ports());
		audio_clip_scene().open(deluge::project::ClipAddress{f.clip->id()}, &f.song->project);

		RealServices svc;
		svc.song = f.song;
		svc.mod_target = f.clip;
		uitest::RunOptions opts = opts_for(SceneId{kAudioClipSceneKind, 0});
		opts.after_construct = svc.installer();
		opts.config.screensaver_idle_ms = 200;
		opts.config.seed = 0x7777; // the seed the per-kind unit goldens were recorded from

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "screensaver", ui::scenes::scene_registry(), svc.slots(), opts);
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
		audio_clip_scene().set_ports(&deluge::gui::shell_host::host_ports());
		audio_clip_scene().open(deluge::project::ClipAddress{f.clip->id()}, &f.song->project);

		RealServices svc;
		svc.song = f.song;
		svc.mod_target = f.clip;
		uitest::RunOptions opts = opts_for_64(SceneId{kAudioClipSceneKind, 0});
		opts.after_construct = svc.installer();
		opts.config.screensaver_idle_ms = 200;
		opts.config.seed = 0x7777; // the seed the per-kind unit goldens were recorded from

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "screensaver_64", ui::scenes::scene_registry(), svc.slots(), opts);
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
		audio_clip_scene().set_ports(&deluge::gui::shell_host::host_ports());
		audio_clip_scene().open(deluge::project::ClipAddress{f.clip->id()}, &f.song->project);
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
		    uitest::expect_script_goldens(self, "count_in", ui::scenes::scene_registry(), svc.slots(), opts);
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
		audio_clip_scene().set_ports(&deluge::gui::shell_host::host_ports());
		audio_clip_scene().open(deluge::project::ClipAddress{f.clip->id()}, &f.song->project);
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
		    uitest::expect_script_goldens(self, "count_in_64", ui::scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{2});
	});
});
// clang-format on

DELUGE_SPEC(scene_scripts_spec, scene_scripts)
