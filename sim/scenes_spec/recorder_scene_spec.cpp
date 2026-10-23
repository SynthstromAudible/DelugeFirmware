/// @file
/// @brief `RecorderScene` -- the port of `AudioRecorder`'s menu-driven half -- driven through a
///        real `Shell` over `uitest::FakeSurfaceSet`, against a real `SampleRecorder`.
///
/// @par The recorder is real, and it never touches the card
///
/// These examples let the scene allocate a genuine `SampleRecorder` through
/// `AudioEngine::getNewRecorder`, exactly as it does on the device, and then drive its state machine
/// by writing the same fields the audio and card threads write: `numSamplesCaptured`, `status`,
/// `hadCardError`, `recordingClippedRecently`. That is the only way to test this scene honestly --
/// the whole of `process()` is a poll of those four fields.
///
/// **Nothing here writes to the SD card, and that is a property of the code rather than of a guard.**
/// `SampleRecorder::setup` (`model/sample/sample_recorder.cpp:121`) allocates a `Sample` and one
/// capture buffer and returns; the file is created in `cardRoutine()`, which only
/// `AudioEngine::doRecorderCardRoutines()` calls and which nothing in this spec pumps.
/// `endSyncedRecording` (`:1195`) is arithmetic over `numSamplesCaptured`, and `abort()` (`:367`) is
/// a single status store -- the file deletion that follows an abort is, again, in `cardRoutine()`.
/// The file runs with no card installed (`NoSdRoot`), and the card is in memory, so a future change
/// that *did* create a file could reach no committed fixture; `QuietAudio` keeps the audio routine
/// from running inside an example.
///
/// @par The legacy oracle
///
/// `AudioRecorder::renderOLED` (`gui/ui/audio_recorder.cpp:116-119`) is the whole of what this UI
/// draws, so it is compared pixel-for-pixel: the **shipped `audioRecorder` object** is asked to
/// render into `OLED::main` from its own `updatedRecordingStatus`, and the port is asked to render
/// from its own recorder's `numSamplesCaptured`. Neither side reads the other's state.
///
/// @note The spec may include `gui/` (for the legacy recorder); the library may not. Same rule
///       `browser_scene_spec.cpp` and `marker_scene_spec.cpp` run under.

#include "gui/ui/menu_view_of.h"
#include "scenes/bind/target.h"
#include "scenes/bind/write_sink.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/profile.h"
#include "scenes/recorder/recorder_scene.h"
#include "scenes/registry.h"

#include "shell/leds.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"
#include "ui/core/led_frame.h"
#include "ui/core/metrics.h"

#include "deluge-project/setting_group.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/audio_recorder.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "integration/scenarios/slot_coherence.h"
#include "memory/heaps.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/source.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/multi_range/multisample_range.h"

#include "notify/dirty_set.h"
#include "notify/notice_queue.h"

#include "support/menu_engine.h"
#include "support/song_fixture.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with every local named `it` in the project
// headers this drags in.
#include "support/card_access.h"

#include "spec/gfx/support/golden.h"
#include "spec/gfx/support/pbm.h"
#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <utility>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::recorder::kRecorderSceneKind;
using deluge::scenes::recorder::recorder_scene;
using deluge::scenes::recorder::RecorderScene;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

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

/// @brief The minimum `Display` the model reaches for. `display` is null in a spec process
///        (`hid/display/display.cpp:4`), and `AudioEngine::getNewRecorder`'s failure paths and
///        `Kit::makeDrumNameUnique` both go through it.
struct RecorderDisplay final : deluge::hid::Display {
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

/// @brief A synth song whose source 0 has one multisample range for the recording to land in.
struct Rig {
	spec::SongFixture song = spec::make_synth_song();
	Sound* sound = nullptr;
	Source* source = nullptr;
	MultisampleRange* range = nullptr;

	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell;

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// @brief Tear the stack down, so the scene's `on_exit` always runs.
	///
	/// **Scenes are process singletons.** An example that left the recorder armed would make the
	/// NEXT example's `on_enter` refuse (`recorder_ != nullptr`) and cascade into a wall of
	/// unrelated failures -- which is exactly what the first run of mutant M1 produced. Popping
	/// here means every example starts from the same place whatever the one before it did, so a
	/// mutation kills the assertions that actually see it and no others.
	~Rig() {
		while (on_stack()) {
			shell.stack().pop(deluge::ui::shell::SceneKind::Null);
		}
		currentSong = saved_song;
	}

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p), shell(fake, p, reg, deluge::ui::shell::Services{}, ShellConfig{}) {
		currentSong = song.song; // the app's sink resolves addresses in the current song
		sound = static_cast<Sound*>(static_cast<ModControllableAudio*>(song.instrument->toModControllable()));
		source = &sound->sources[0];
		source->oscType = OscType::SQUARE; // so "became SAMPLE" is a real observation
		source->sampleControls.reversed = false;
		range = static_cast<MultisampleRange*>(source->getOrCreateFirstRange());
	}

	MenuContext ctx() {
		MenuContext c = menu::context_for(*song.song, deluge::gui::menuViewOf(*song.song, song.clip), 0, false, false,
		                                  false, /*kit_global_fx=*/false);
		c.range_index = 0;
		return c;
	}

	/// @brief Open the recorder, the way `osc::AudioRecorder::beginSession` does.
	/// @param sink Where the scene's writes go; the app's sink when null.
	void open(bind::WriteSink* sink = nullptr) {
		MenuContext c = ctx();
		if (sink != nullptr) {
			c.sink = sink;
		}
		recorder_scene().open(c);
		shell.init(SceneId{kRecorderSceneKind, 0});
	}

	/// @brief The sound's slot.
	deluge::project::SlotAddress slot() { return *ctx().addresses.slot; }

	/// @brief Open the recorder ON TOP of the sound menu, which is where a node actually opens it.
	///
	/// The stack the LED example needs: `LedCompose::compose` runs bottom to top and the last
	/// writer wins, so "the recorder's SYNTH/KIT answer beats the menu's" is only a claim about a
	/// stack with both scenes on it.
	void open_over_menu() {
		const MenuContext c = ctx();
		menu_scene().open(*menu::MenuScene::roots()[static_cast<size_t>(MenuContext::Root::Sound)], c);
		shell.init(SceneId{deluge::ui::shell::SceneKind::FirstApp, 0});
		recorder_scene().open(c);
		(void)shell.stack().push(SceneId{kRecorderSceneKind, 0});
	}

	void tick(uint32_t at_ms) { shell.tick(at_ms); }

	/// @brief Press a button down. `ButtonId` -> board matrix coordinate, so the shell's own
	///        lowering runs rather than a synthesised `UiEvent`.
	///
	/// Press only, no release: `AudioRecorder::buttonAction` acts on the press and refuses the
	/// release outright (`audio_recorder.cpp:279-285 buttonAction`).
	void press(ButtonId b, uint32_t at_ms) {
		const std::span<const ButtonId> matrix = profile.button_by_matrix;
		for (size_t i = 0; i < matrix.size(); i++) {
			if (matrix[i] != b) {
				continue;
			}
			const auto x = static_cast<uint8_t>(i % profile.button_cols);
			const auto y = static_cast<uint8_t>(i / profile.button_cols);
			shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, 255}, at_ms);
			return;
		}
	}

	bool on_stack() {
		for (const deluge::ui::shell::Scene* s : shell.stack().scenes()) {
			if (s->kind() == kRecorderSceneKind) {
				return true;
			}
		}
		return false;
	}

	std::string oled() { return fake.pbm(0); }

	Song* const saved_song = currentSong;
};

/// @brief A sink that refuses every request.
struct RefusingSink final : bind::WriteSink {
	bool write(const bind::WriteRequest&) override { return false; }
	bool write_param(const deluge::project::ParamAddress&, int32_t, int32_t, int32_t) override { return false; }
	bool delete_automation(const deluge::project::ParamAddress&) override { return false; }
	bool write_text(const bind::Target&, const bind::Addresses&, std::string_view) override { return false; }
	bool write_ranges(deluge::project::SlotAddress, uint8_t, std::span<const deluge::instrument::RangeState>,
	                  bind::RangesCause) override {
		return false;
	}
	bool recolour_clip(deluge::project::ClipAddress, int32_t) override { return false; }
	bool clear_clip(deluge::project::ClipAddress, bool, bool) override { return false; }
	bool move_audio_clip_marker(deluge::project::ClipAddress, std::string_view, int64_t) override { return false; }
	void set_default_audio_input_channel(int32_t) override {}
	bool attach_recording(deluge::project::SlotAddress, uint8_t, uint16_t, std::string_view) override { return false; }
	bool resize_audio_clip(deluge::project::ClipAddress, bind::ResizeKind, int32_t, int32_t) override { return false; }
	void set_undo_scroll_after(deluge::project::ClipAddress, int32_t) override {}
	bool learn(deluge::project::MappingTarget, std::optional<deluge::project::MidiCommand>,
	           std::optional<bind::LearnInput>) override {
		return false;
	}
	bool write_source_transpose(const bind::Addresses&, uint8_t, std::optional<uint16_t>, int32_t) override {
		return false;
	}
	bool knob_mode(deluge::project::KnobModeOwner, uint8_t) override { return false; }
	bool chain_view(deluge::project::ChainAddress, std::string_view, std::string_view) override { return false; }
	bool write_controls(deluge::project::ControlsAddress, std::vector<deluge::project::ControllerMapping>) override {
		return false;
	}
	bool stutter(const bind::KnobFocus&, bool) override { return false; }
	bool grab_region(const bind::KnobFocus&, uint32_t) override { return false; }
	bool write_effect(const bind::EffectOwner&, std::string_view, const deluge::serialization::Value&) override {
		return false;
	}
	bool midi_cc_select(const bind::KnobFocus&, int32_t, bool) override { return false; }
};

/// @brief A finished take: the recorder's sample with a file, resident in the audio file manager at its path as the
///        recorder's file creation leaves it, and out of it again when the scope ends.
struct Take {
	Sample* sample;
	Take(SampleRecorder& rec, const char* path) : sample(rec.sample) {
		sample->filePath = path;
		sample->lengthInSamples = 44100;
		sample->audioDataLengthBytes = 44100 * 3;
		rec.numSamplesCaptured = 44100;
		(void)audioFileManager.audioFiles.insertElement(sample);
	}
	~Take() {
		const int32_t i = audioFileManager.audioFiles.searchForExactObject(sample);
		if (i >= 0) {
			audioFileManager.audioFiles.erase(audioFileManager.audioFiles.begin() + i);
		}
	}
	Take(const Take&) = delete;
	Take& operator=(const Take&) = delete;
};

/// @brief A kit with one sound row whose source 0 has a range, with a `Shell` for the recorder over it.
struct KitRig {
	spec::SongFixture song = spec::make_kit_song();
	SoundDrum* drum = nullptr;
	const BoardProfile& profile = deluge_profile();
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, profile};
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};
	Song* const saved_song = currentSong;

	KitRig() {
		currentSong = song.song;
		drum = spec::add_drum<SoundDrum>(song);
		(void)drum->sources[0].getOrCreateFirstRange();
	}
	~KitRig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(deluge::ui::shell::SceneKind::Null);
		}
		currentSong = saved_song;
	}
	KitRig(const KitRig&) = delete;
	KitRig& operator=(const KitRig&) = delete;

	MenuContext ctx() {
		return menu::context_for(*song.song, deluge::gui::menuViewOf(*song.song, song.clip), 0, false, false, false,
		                         false);
	}

	/// @brief The lane's name, as the record holds it.
	std::string recorded_name() {
		const std::optional<deluge::project::SettingAddress> address = bind::setting_address(
		    {.kind = bind::Target::Kind::MODEL, .path = "name", .group = deluge::project::SettingGroupId::LANE},
		    ctx().addresses);
		const std::optional<deluge::serialization::Value> value =
		    address ? deluge::project::read_setting(song.song->project, *address, "name") : std::nullopt;
		return value ? value->str() : std::string{};
	}
};

/// @brief What LEGACY draws, from LEGACY's own sentinel -- `AudioRecorder::updatedRecordingStatus`.
/// @param captured Legacy's own flag: has the first sample been captured?
/// @return The whole 128x48 panel as a PBM, the same shape `FakeSurfaceSet::pbm` produces.
std::string legacy_oled(bool captured) {
	using deluge::hid::display::OLED;
	OLED::clearMainImage();
	audioRecorder.updatedRecordingStatus = captured;
	audioRecorder.renderOLED(OLED::main);
	return page_major_to_pbm(&OLED::main.hackGetImageStore()[0][0], OLED_MAIN_WIDTH_PIXELS, OLED_MAIN_HEIGHT_PIXELS);
}

/// @brief The state @p b was declared with this frame, or `Off` when it was not declared at all.
deluge::ui::LedState::Kind led_kind(const deluge::ui::LedFrame& f, ButtonId b) {
	for (const auto& [id, state] : f.entries()) {
		if (id == static_cast<deluge::ui::LedId>(b)) {
			return state.kind;
		}
	}
	return deluge::ui::LedState::Kind::Off;
}

/// @brief The blink period @p b was declared with this frame, or 0.
uint16_t led_period(const deluge::ui::LedFrame& f, ButtonId b) {
	for (const auto& [id, state] : f.entries()) {
		if (id == static_cast<deluge::ui::LedId>(b)) {
			return state.period_ms;
		}
	}
	return 0;
}

// --- process-global guards -----------------------------------------------------------------
//
// **Every process global this spec touches is held in an RAII guard, and the guards REFUSE rather
// than repair.** `QuietAudio` (`support/card_access.h:82-88`) is the shape; the addition here is
// the refusal, and it is not fastidiousness -- a silently-repairing reset has propped up a broken
// assertion three times in this branch stack: task 3's quiet-tick example (covered for by an
// unrelated repaint), fix round 1's `~Rig` cross-contamination, and fix round 1's mutant F5, which
// survived because `before_each` cleared the very publication the missing code should have cleared.
//
// `before_each`/`after_each` take a `std::function<void()>` with no `self`
// (`cppspec/description.hpp:23,95-97`), so `expect` is not available in them. A named reason on
// stderr plus `std::abort()` is what "loudly" can mean there, and it fails the suite non-zero.

[[noreturn]] void spec_bail(const char* what) {
	std::fprintf(stderr, "recorder_scene_spec: %s\n", what);
	std::abort();
}

/// @brief Hold the foreground-recording publication clear for the scope -- and refuse to run, or to
///        finish, if it is not.
///
/// The guard fix round 1's F5 needed. `AudioRecorder::recordingSource`/`::recorder` are process
/// globals nine subsystems read, so an example that leaks one poisons the next; clearing it
/// silently is what hid the leak in the first place.
struct NoForegroundRecording {
	NoForegroundRecording() { check("a foreground recording was already published when an example started"); }
	~NoForegroundRecording() { check("an example finished with a foreground recording still published"); }
	NoForegroundRecording(const NoForegroundRecording&) = delete;
	NoForegroundRecording& operator=(const NoForegroundRecording&) = delete;
	static void check(const char* what) {
		if (audioRecorder.recordingSource != AudioInputChannel::NONE || audioRecorder.recorder != nullptr) {
			spec_bail(what);
		}
	}
};

/// @brief Publish an OUTPUT recording (a resample, as far as every reader is concerned) for the
///        scope, and take it away again.
///
/// So the one example that needs a live foreground recording cannot leave one behind by forgetting
/// a line: `NoForegroundRecording` above would abort, but only after the damage was already in the
/// habit.
struct PublishedOutputRecording {
	PublishedOutputRecording() { AudioEngine::setForegroundRecording(nullptr, AudioInputChannel::OUTPUT); }
	~PublishedOutputRecording() { AudioEngine::setForegroundRecording(nullptr, AudioInputChannel::NONE); }
	PublishedOutputRecording(const PublishedOutputRecording&) = delete;
	PublishedOutputRecording& operator=(const PublishedOutputRecording&) = delete;
};

/// @brief Force input monitoring ON for the scope, and put `inputMonitoringMode` back.
///
/// The exposure the fix-round-2 review named: this was a hand-written save/restore pair inside one
/// example, which is the one shape that stops being a save/restore the moment an assertion between
/// them fails.
struct MonitoringOn {
	InputMonitoringMode saved = AudioEngine::inputMonitoringMode;
	MonitoringOn() { AudioEngine::inputMonitoringMode = InputMonitoringMode::ON; }
	~MonitoringOn() { AudioEngine::inputMonitoringMode = saved; }
	MonitoringOn(const MonitoringOn&) = delete;
	MonitoringOn& operator=(const MonitoringOn&) = delete;
};

/// @brief Empty the notice queue and the dirty set on the way in.
///
/// @note **Deliberately not symmetric, and this is the convention rather than an oversight.** The
///       queue cannot be asserted empty on the way out: `~Rig` pops the stack, `RecorderScene::on_exit`
///       legitimately raises `ClearWorking`, and that notice is still queued when the example ends.
///       Draining on entry is isolation (it decides what the NEXT example starts from); asserting on
///       exit would be asserting something untrue.
struct DrainedNotices {
	DrainedNotices() {
		deluge::notify::Notice discard;
		while (deluge::notify::notices().pop(discard)) {}
		deluge::notify::dirty().drain();
	}
	~DrainedNotices() = default;
	DrainedNotices(const DrainedNotices&) = delete;
	DrainedNotices& operator=(const DrainedNotices&) = delete;
};

/// @brief Select the built-in English strings for the scope, and put the pointer back.
///
/// `Action::Kind::Popup` resolves through `scenes::text_lookup` -> `l10n::getView`, which
/// dereferences `chosenLanguage` unguarded (`gui/l10n/l10n.cpp:9`); a spec process leaves it null.
struct EnglishL10n {
	const deluge::l10n::Language* saved = deluge::l10n::chosenLanguage;
	EnglishL10n() { deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english; }
	~EnglishL10n() { deluge::l10n::chosenLanguage = saved; }
	EnglishL10n(const EnglishL10n&) = delete;
	EnglishL10n& operator=(const EnglishL10n&) = delete;
};

/// @brief No mic and no line-in for the scope: the booted host app's jack state put back after.
struct NothingPluggedIn {
	bool mic = AudioEngine::micPluggedIn;
	bool line_in = AudioEngine::lineInPluggedIn;
	NothingPluggedIn() {
		AudioEngine::micPluggedIn = false;
		AudioEngine::lineInPluggedIn = false;
	}
	~NothingPluggedIn() {
		AudioEngine::micPluggedIn = mic;
		AudioEngine::lineInPluggedIn = line_in;
	}
	NothingPluggedIn(const NothingPluggedIn&) = delete;
	NothingPluggedIn& operator=(const NothingPluggedIn&) = delete;
};

/// @brief Every guard an example needs, in one declaration.
///
/// Declare it FIRST in an example, before the `Rig`: members are destroyed in reverse declaration
/// order and the `Rig` (declared after) tears the scene stack down, so `RecorderScene::on_exit` has
/// already run by the time `NoForegroundRecording` checks.
struct Guards {
	deluge::scenes::spec::QuietAudio quiet;
	deluge::scenes::spec::NoSdRoot no_card;
	DrainedNotices notices;
	EnglishL10n l10n;
	NoForegroundRecording publication;
};

extern "C" bool deluge_host_storage_hold_fs(bool hold);

/// @brief Hold the filesystem lock for the scope: `AudioEngine::sdRoutineBusy()` genuinely true.
///
/// **Not a fake flag.** `sdRoutineBusy()` is `deluge::sync::sd_busy()`, which is true while the
/// filesystem lock is held -- by a card operation in flight, or here by the host's
/// `deluge_host_storage_hold_fs`, which takes that same lock on this thread and releases it however
/// the scope ends. Nothing in the scope may touch a file: a filesystem call on this thread would wait
/// on the lock it holds.
struct CardBusy {
	CardBusy() {
		if (!deluge_host_storage_hold_fs(true)) {
			spec_bail("CardBusy: the filesystem lock is already held");
		}
	}
	~CardBusy() { (void)deluge_host_storage_hold_fs(false); }
	CardBusy(const CardBusy&) = delete;
	CardBusy& operator=(const CardBusy&) = delete;
};

/// @brief Run @p body while the card is busy (`CardBusy`).
void in_sd_routine(const std::function<void()>& body) {
	const CardBusy busy;
	body();
}

} // namespace

// clang-format off
describe recorder_scene_spec_body("scenes/recorder RecorderScene", $ {

	// The ONLY thing left in `before_each`: installing a `Display`, which is a one-time static and
	// has nothing to leak. Every process global this spec touches is held by `Guards` instead --
	// see the comment above `spec_bail`. `before_each` cannot `expect`, so a defensive reset here
	// could only ever repair silently, which is precisely the habit that hid mutant F5.
	before_each([] {
		static RecorderDisplay d;
		if (display == nullptr) {
			display = &d;
		}
	});

	// --- arming -------------------------------------------------------------------------------

	it("arms a recorder when it opens, and reports it as waiting", _ {
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		expect(r.on_stack()).to_be_true();
		expect(recorder_scene().recording()).to_be_true();
		expect(recorder_scene().recorder() != nullptr).to_be_true();
		// `AudioRecorder::opened` leaves `updatedRecordingStatus` false (`audio_recorder.cpp:64`);
		// the port reads `numSamplesCaptured`, which a recorder nothing has fed is zero.
		expect(recorder_scene().capturing()).to_be_false();
		expect(recorder_scene().stopping()).to_be_false();
	});

	it("refuses to open with nowhere to put the finished sample", _ {
		// The port's own gate: `process()`'s success arm dereferences the range with no null test
		// (`:215-217`), and legacy is saved from that by
		// `checkPermissionToBeginSessionForRangeSpecificParam` refusing first.
		Guards g;
		Rig r;
		MenuContext c = r.ctx();
		c.range_index = -1;
		expect(RecorderScene::can(c)).to_be_false();
		recorder_scene().open(c);
		r.shell.init(SceneId{kRecorderSceneKind, 0});
		r.tick(0);
		expect(r.on_stack()).to_be_false();
		expect(recorder_scene().recording()).to_be_false();
	});

	// --- the screen, against legacy -------------------------------------------------------------

	it("draws exactly what AudioRecorder::renderOLED draws, in both of its states", _ {
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		const std::string ported_waiting = r.oled();

		SampleRecorder* rec = recorder_scene().recorder();
		rec->numSamplesCaptured = 1024;
		r.tick(10);
		const std::string ported_recording = r.oled();

		// LEGACY, from its own sentinel, after the port has finished with its own.
		expect(ported_waiting).to_equal(legacy_oled(false));
		expect(ported_recording).to_equal(legacy_oled(true));
		// ...and the two really are different pictures, so the comparison above is not vacuous.
		expect(ported_waiting == ported_recording).to_be_false();

		rec->numSamplesCaptured = 0;
	});

	it("flips from Waiting to Recording with no input at all", _ {
		// **The gate-arm regression test.** `tick()` alone changes nothing anyone can see:
		// `next_timer()` wakes the shell, but only `request_repaint_after` marks the panel dirty
		// (`RepaintGate::needs_frame`). Delete the arm in `render_oled` and this example fails,
		// because the second frame is byte-identical to the first. Nothing here presses anything.
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		const std::string waiting = r.oled();

		SampleRecorder* rec = recorder_scene().recorder();
		rec->numSamplesCaptured = 1;
		// Two quiet ticks, one poll period apart. No pad, no button, no encoder.
		r.tick(deluge::scenes::recorder::kRecorderPollMs);
		r.tick(2 * deluge::scenes::recorder::kRecorderPollMs);
		expect(waiting == r.oled()).to_be_false();
		expect(r.oled()).to_equal(legacy_oled(true));

		rec->numSamplesCaptured = 0;
	});

	it("asks to be woken while a recorder is armed, and stops asking once it is gone", _ {
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		expect(recorder_scene().next_timer().has_value()).to_be_true();
		expect(static_cast<int32_t>(recorder_scene().next_timer().value_or(0)))
		    .to_equal(static_cast<int32_t>(deluge::scenes::recorder::kRecorderPollMs));

		SampleRecorder* rec = recorder_scene().recorder();
		rec->numSamplesCaptured = 4096;
		rec->status = RecorderStatus::COMPLETE;
		r.tick(10);
		expect(recorder_scene().next_timer().has_value()).to_be_false();
	});

	// --- stopping -----------------------------------------------------------------------------

	it("ends the recording on BACK, on SELECT_ENC and on RECORD", _ {
		// `AudioRecorder::buttonAction` (`:268-274`): the three buttons that mean "stop".
		const ButtonId kStopButtons[] = {ButtonId::Back, ButtonId::SelectEnc, ButtonId::Record};
		for (ButtonId b : kStopButtons) {
			Guards g;
			Rig r;
			r.open();
			r.tick(0);
			SampleRecorder* rec = recorder_scene().recorder();
			// A recorder that captured nothing is aborted by `endSyncedRecording` (`:1243-1245`),
			// which is a different arm; give it something so the ordinary one runs.
			rec->numSamplesCaptured = 44100;
			r.press(b, 10);
			r.tick(10);
			expect(recorder_scene().stopping()).to_be_true();
			// `endSyncedRecording` left `CAPTURING_DATA` behind, whichever of its two arms it took.
			expect(rec->status.load() != RecorderStatus::CAPTURING_DATA).to_be_true();

			rec->status = RecorderStatus::COMPLETE;
			r.tick(20);
		}
	});

	it("leaves every other button to whatever is beneath", _ {
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = recorder_scene().recorder();
		rec->numSamplesCaptured = 44100;
		r.press(ButtonId::Play, 10);
		r.tick(10);
		expect(recorder_scene().stopping()).to_be_false();
		expect(rec->status.load() == RecorderStatus::CAPTURING_DATA).to_be_true();

		rec->status = RecorderStatus::COMPLETE;
		rec->numSamplesCaptured = 0;
		r.tick(20);
	});

	// --- the sample reaching the model ----------------------------------------------------------

	it("hands the finished sample to the source and closes", _ {
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = recorder_scene().recorder();
		const Take take{*rec, "SAMPLES/RECORD/REC000.WAV"};
		Sample* captured = take.sample;
		rec->status = RecorderStatus::COMPLETE;

		r.tick(10);

		// `process()`'s success arm (`:213-217`), every statement of it.
		expect(static_cast<int32_t>(r.source->oscType)).to_equal(static_cast<int32_t>(OscType::SAMPLE));
		AudioFileHolder* holder = r.range->getAudioFileHolder();
		expect(holder->audioFile == static_cast<AudioFile*>(captured)).to_be_true();
		expect(holder->filePath).to_equal(std::string("SAMPLES/RECORD/REC000.WAV"));
		// `finishRecording()` (`:245-253`), and `close()` (`:221`).
		expect(recorder_scene().recording()).to_be_false();
		expect(r.on_stack()).to_be_false();
	});

	it("keeps the sound's record current: the attached sample is in the record a save writes", _ {
		Guards g;
		Rig r;
		deluge::bridge::recordLoadedSound(*r.sound, *r.song.song); // the square source, as a load records it
		expect(deluge::harness::soundIncoherence(*r.sound, *r.song.song)).to_equal(std::string{});
		r.open();
		r.tick(0);
		SampleRecorder* rec = recorder_scene().recorder();
		const Take take{*rec, "SAMPLES/RECORD/REC001.WAV"};
		rec->status = RecorderStatus::COMPLETE;

		r.tick(10);

		expect(static_cast<int32_t>(r.source->oscType)).to_equal(static_cast<int32_t>(OscType::SAMPLE));
		expect(deluge::harness::soundIncoherence(*r.sound, *r.song.song)).to_equal(std::string{});
	});

	it("attaches through one request, and the record names the take", _ {
		Guards g;
		{
			Rig r;
			bind::RecordingSink recording;
			r.open(&recording);
			r.tick(0);
			SampleRecorder* rec = recorder_scene().recorder();
			const Take take{*rec, "SAMPLES/RECORD/REC002.WAV"};
			rec->status = RecorderStatus::COMPLETE;
			r.tick(10);
			expect(recording.gestures.size()).to_equal(size_t{1});
			if (!recording.gestures.empty()) {
				const bind::GestureRequest& attach = recording.gestures[0];
				expect(attach.kind == bind::GestureRequest::Kind::ATTACH).to_be_true();
				expect(attach.slot == r.slot() && attach.source == 0 && attach.range == std::optional<uint16_t>{0}).to_be_true();
				expect(attach.text).to_equal(std::string{"SAMPLES/RECORD/REC002.WAV"});
			}
			// Only the request: the sink took it and nothing else wrote.
			expect(static_cast<int32_t>(r.source->oscType)).to_equal(static_cast<int32_t>(OscType::SQUARE));
			expect(r.on_stack()).to_be_false();
		}
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = recorder_scene().recorder();
		const Take take{*rec, "SAMPLES/RECORD/REC003.WAV"};
		rec->status = RecorderStatus::COMPLETE;
		r.tick(10);
		const deluge::instrument::SoundState* state = menu::host_runtime_facts().sound_state(r.slot());
		expect(state != nullptr && state->sources[0].osc_type == OscType::SAMPLE).to_be_true();
		expect(state != nullptr && !state->sources[0].ranges.empty()
		       && state->sources[0].ranges[0].file_path == "SAMPLES/RECORD/REC003.WAV")
		    .to_be_true();
	});

	it("refuses an attach whose path names no take in memory, and nothing changes", _ {
		Guards g;
		Rig r;
		deluge::bridge::recordLoadedSound(*r.sound, *r.song.song);
		expect(menu::host_write_sink().attach_recording(r.slot(), 0, 0, "SAMPLES/RECORD/NOWHERE.WAV")).to_be_false();
		expect(static_cast<int32_t>(r.source->oscType)).to_equal(static_cast<int32_t>(OscType::SQUARE));
		expect(r.range->getAudioFileHolder()->audioFile == nullptr).to_be_true();
		const deluge::instrument::SoundState* state = menu::host_runtime_facts().sound_state(r.slot());
		expect(state != nullptr && state->sources[0].osc_type == OscType::SQUARE).to_be_true();
	});

	it("reports an attach the app refuses", _ {
		Guards g;
		Rig r;
		RefusingSink refusing;
		r.open(&refusing);
		r.tick(0);
		SampleRecorder* rec = recorder_scene().recorder();
		rec->numSamplesCaptured = 44100;
		rec->status = RecorderStatus::COMPLETE;
		r.tick(10);
		bool error_shown = false;
		deluge::notify::Notice notice;
		while (deluge::notify::notices().pop(notice)) {
			error_shown = error_shown || notice.kind == deluge::notify::Notice::Kind::Error;
		}
		expect(error_shown).to_be_true();
		expect(r.range->getAudioFileHolder()->audioFile == nullptr).to_be_true();
		expect(r.on_stack()).to_be_false();
	});

	it("attaches the recorder's own take where another sample in memory has its path", _ {
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = recorder_scene().recorder();
		const Take take{*rec, "SAMPLES/RECORD/REC005.WAV"};
		void* memory = deluge::memory::alloc_fast(sizeof(Sample));
		auto* stale = new (memory) Sample();
		stale->filePath = "SAMPLES/RECORD/REC005.WAV";
		(void)audioFileManager.audioFiles.insertElement(stale);
		rec->status = RecorderStatus::COMPLETE;
		r.tick(10);
		expect(r.range->getAudioFileHolder()->audioFile == static_cast<AudioFile*>(take.sample)).to_be_true();
		const int32_t i = audioFileManager.audioFiles.searchForExactObject(stale);
		if (i >= 0) {
			audioFileManager.audioFiles.erase(audioFileManager.audioFiles.begin() + i);
		}
	});

	it("renames a kit row already called REC1, as legacy's unique-name search counts the row's own name", _ {
		Guards g;
		KitRig k;
		deluge::edits::laneSettingFor(*k.drum, "name", deluge::serialization::Value::makeScalar("REC1"));
		recorder_scene().open(k.ctx());
		k.shell.init(SceneId{kRecorderSceneKind, 0});
		k.shell.tick(0);
		expect(k.recorded_name()).to_equal(std::string{"REC2"});
	});

	it("names a kit row's drum REC with the next free number, as a text request", _ {
		Guards g;
		{
			KitRig k;
			bind::RecordingSink recording;
			MenuContext c = k.ctx();
			c.sink = &recording;
			recorder_scene().open(c);
			k.shell.init(SceneId{kRecorderSceneKind, 0});
			k.shell.tick(0);
			expect(recording.gestures.size()).to_equal(size_t{1});
			if (!recording.gestures.empty()) {
				expect(recording.gestures[0].kind == bind::GestureRequest::Kind::TEXT).to_be_true();
				expect(recording.gestures[0].text).to_equal(std::string{"REC1"});
			}
		}
		KitRig k;
		SoundDrum* other = spec::add_drum<SoundDrum>(k.song);
		deluge::edits::laneSettingFor(*other, "name", deluge::serialization::Value::makeScalar("REC1"));
		static_cast<Kit*>(k.song.instrument)->selectedDrum = k.drum; // the recorder opens on the first row
		recorder_scene().open(k.ctx());
		k.shell.init(SceneId{kRecorderSceneKind, 0});
		k.shell.tick(0);
		expect(k.recorded_name()).to_equal(std::string{"REC2"});
	});

	it("defers the whole completion arm while the SD routine is running", _ {
		// `AudioRecorder::slowRoutine`'s guard (`:168-176`), which `after_render()` needs for the
		// same reason: `finish_recording()` frees the recorder, and freeing one that is suspended
		// part-way through its own `cardRoutine()` leaves that routine running on freed memory.
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = recorder_scene().recorder();
		const Take take{*rec, "SAMPLES/RECORD/REC000.WAV"};
		rec->status = RecorderStatus::COMPLETE;

		// The poll tick that would finish it, run from inside a real card routine.
		in_sd_routine([&r] { r.tick(10); });

		// Nothing freed, nothing attached, nothing closed.
		expect(recorder_scene().recording()).to_be_true();
		expect(recorder_scene().recorder() == rec).to_be_true();
		expect(static_cast<int32_t>(r.source->oscType)).to_equal(static_cast<int32_t>(OscType::SQUARE));
		expect(r.on_stack()).to_be_true();

		// And the cost of the deferral is exactly one poll: the next tick, outside the routine,
		// runs the completion arm in full.
		r.tick(20);
		expect(recorder_scene().recording()).to_be_false();
		expect(static_cast<int32_t>(r.source->oscType)).to_equal(static_cast<int32_t>(OscType::SAMPLE));
		expect(r.on_stack()).to_be_false();
	});

	it("attaches nothing when the recording aborted, but still closes", _ {
		// `process()`'s empty `if (status == ABORTED || hadCardError) {}` arm (`:209`).
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = recorder_scene().recorder();
		rec->status = RecorderStatus::ABORTED;
		r.tick(10);
		expect(r.range->getAudioFileHolder()->audioFile == nullptr).to_be_true();
		expect(static_cast<int32_t>(r.source->oscType)).to_equal(static_cast<int32_t>(OscType::SQUARE));
		expect(recorder_scene().recording()).to_be_false();
		expect(r.on_stack()).to_be_false();
	});

	it("attaches nothing when the card errored, even though the status says complete", _ {
		// The other half of the same condition: `hadCardError` alone is enough to refuse the
		// attach, and it is also enough on its own to end the recording (`:207`).
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = recorder_scene().recorder();
		rec->status = RecorderStatus::COMPLETE;
		rec->hadCardError = true;
		r.tick(10);
		expect(r.range->getAudioFileHolder()->audioFile == nullptr).to_be_true();
		expect(recorder_scene().recording()).to_be_false();
		expect(r.on_stack()).to_be_false();
	});

	it("clears the clipping flag when it reports it, so one clip is one popup", _ {
		// `process()`'s clipping arm (`:227-233`). The popup itself is a `Notices` matter; what
		// this scene owns is the flag, which legacy clears before showing anything.
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = recorder_scene().recorder();
		rec->numSamplesCaptured = 1024;
		rec->recordingClippedRecently = true;
		r.tick(10);
		expect(rec->recordingClippedRecently).to_be_false();
		// ...and the recording is still going: clipping is not a stop.
		expect(recorder_scene().recording()).to_be_true();
		expect(r.on_stack()).to_be_true();

		rec->status = RecorderStatus::COMPLETE;
		rec->numSamplesCaptured = 0;
		rec->hadCardError = true; // refuse the attach; this example is not about the sample
		r.tick(20);
	});

	it("throws an armed recorder away when it is removed from outside", _ {
		// The divergence `on_exit` exists for: legacy's `process()` blocks until it has closed
		// itself, so legacy never reaches this. A scene can be popped by a song load.
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = recorder_scene().recorder();
		rec->numSamplesCaptured = 1024;

		r.shell.stack().pop(deluge::ui::shell::SceneKind::Null);
		r.tick(10);
		expect(recorder_scene().recording()).to_be_false();
		expect(static_cast<int32_t>(rec->status.load())).to_equal(static_cast<int32_t>(RecorderStatus::ABORTED));
		expect(rec->pointerHeldElsewhere).to_be_false();
		// ...and the foreground-recording publication is cleared, so the nine subsystems that gate
		// on it do not keep seeing a recording that is over. Without this the leak is invisible:
		// `before_each` clears the pair defensively, so the next example would not notice either
		// (fix round 1, mutant F5 -- which survived until this assertion was added).
		expect(static_cast<int32_t>(audioRecorder.recordingSource))
		    .to_equal(static_cast<int32_t>(AudioInputChannel::NONE));
		expect(audioRecorder.recorder == nullptr).to_be_true();
		// Nothing was attached on the way out.
		expect(r.range->getAudioFileHolder()->audioFile == nullptr).to_be_true();
	});

	it("greys out every column of the grid, exactly as getGreyoutColsAndRows does", _ {
		// `AudioRecorder::getGreyoutColsAndRows` (`:58-61`): `*cols = 0xFFFFFFFF`, rows untouched.
		expect(static_cast<int64_t>(recorder_scene().greyout_mask().cols)).to_equal(static_cast<int64_t>(0xFFFFFFFF));
		expect(static_cast<int64_t>(recorder_scene().greyout_mask().rows)).to_equal(static_cast<int64_t>(0));
	});

	it("declares the LEDs opened() sets, RECORD blinking at the fast rate", _ {
		// `AudioRecorder::opened`'s LED tail (`audio_recorder.cpp:100-106`). SYNTH/KIT are the
		// interesting pair: `SoundEditor::setLedStates` turns BOTH off (`sound_editor.cpp:344-345`)
		// and this UI lights exactly one, so a scene that left them to `MenuScene` beneath would
		// show neither.
		Guards g;
		Rig r;
		r.open_over_menu();
		r.tick(0);
		expect(menu_scene().focused() != nullptr).to_be_true(); // else the menu declares nothing

		// Composed over the REAL stack -- `MenuScene` beneath, recorder on top -- rather than by
		// calling `leds()` directly, because the SYNTH/KIT claim is precisely that this scene's
		// declaration outranks the menu's (fix round 1, Minor 1). `MenuScene::leds` declares both
		// OFF, mirroring `SoundEditor::setLedStates` (`sound_editor.cpp:344-345`).
		deluge::ui::LedFrame f;
		deluge::ui::shell::LedCompose::compose(f, {}, r.shell.stack().scenes());
		expect(static_cast<int32_t>(led_kind(f, ButtonId::Record)))
		    .to_equal(static_cast<int32_t>(deluge::ui::LedState::Kind::Blink));
		expect(static_cast<int32_t>(led_period(f, ButtonId::Record))).to_equal(60); // kFastFlashTime
		expect(static_cast<int32_t>(led_kind(f, ButtonId::Back)))
		    .to_equal(static_cast<int32_t>(deluge::ui::LedState::Kind::Blink));
		expect(static_cast<int32_t>(led_period(f, ButtonId::Back))).to_equal(110); // kFlashTime
		// The fixture is a synth, so `editingKit()` is false: SYNTH on, KIT off.
		expect(static_cast<int32_t>(led_kind(f, ButtonId::Synth)))
		    .to_equal(static_cast<int32_t>(deluge::ui::LedState::Kind::On));
		expect(static_cast<int32_t>(led_kind(f, ButtonId::Kit)))
		    .to_equal(static_cast<int32_t>(deluge::ui::LedState::Kind::Off));
		expect(static_cast<int32_t>(led_kind(f, ButtonId::Scale)))
		    .to_equal(static_cast<int32_t>(deluge::ui::LedState::Kind::Off));
	});

	// --- the foreground-recording publication ---------------------------------------------------

	it("publishes the recording where the nine subsystems that gate on it can see it", _ {
		// **Fix round 1's critical finding.** `opened()` sets `AudioRecorder::recordingSource` and
		// `::recorder` (`audio_recorder.cpp:135-138`), and those are not private bookkeeping: nine
		// subsystems read them. The port publishes them through
		// `AudioEngine::setForegroundRecording`, which writes the same storage -- so this example
		// reads the LEGACY members, which is the path all nine readers take.
		Guards g;
		NothingPluggedIn unplugged;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = recorder_scene().recorder();

		// Nothing plugged in, so `opened()`'s stereo test is false and the channel is LEFT
		// (`audio_recorder.cpp:92-95`).
		expect(static_cast<int32_t>(audioRecorder.recordingSource))
		    .to_equal(static_cast<int32_t>(AudioInputChannel::LEFT));
		expect(audioRecorder.recorder == rec).to_be_true();

		// ...and cleared once the recording is over.
		rec->status = RecorderStatus::COMPLETE;
		rec->hadCardError = true; // refuse the attach; this example is about the publication
		r.tick(10);
		expect(static_cast<int32_t>(audioRecorder.recordingSource))
		    .to_equal(static_cast<int32_t>(AudioInputChannel::NONE));
		expect(audioRecorder.recorder == nullptr).to_be_true();
	});

	it("keeps input monitoring alive for the whole recording", _ {
		// The consequence the state assertion above exists for, asserted as BEHAVIOUR:
		// `AudioEngine::setMonitoringMode` (`audio_engine.cpp:1024-1055`) enables `doMonitoring`
		// only for `STEREO`/`LEFT` and double-checks the recorder pointer. A port that armed a
		// recorder without publishing it records correctly and leaves the user hearing nothing.
		Guards g;
		MonitoringOn monitoring;

		AudioEngine::setMonitoringMode();
		expect(AudioEngine::doMonitoring).to_be_false(); // nothing armed yet

		Rig r;
		r.open();
		r.tick(0);
		AudioEngine::setMonitoringMode();
		expect(AudioEngine::doMonitoring).to_be_true();

		SampleRecorder* rec = recorder_scene().recorder();
		rec->status = RecorderStatus::COMPLETE;
		rec->hadCardError = true;
		r.tick(10);
		AudioEngine::setMonitoringMode();
		expect(AudioEngine::doMonitoring).to_be_false(); // ...and off again once it is over
	});

	it("refuses to open while any foreground recording is already live", _ {
		// `opened()`'s first refusal (`:69-71`), which is `recordingSource > NONE` -- and which
		// covers an OUTPUT recording (resampling, stem export) as well as this UI's own.
		Guards g;
		Rig r;
		PublishedOutputRecording resampling;
		expect(RecorderScene::can(r.ctx())).to_be_false();
		r.open();
		r.tick(0);
		expect(r.on_stack()).to_be_false();
		expect(recorder_scene().recording()).to_be_false();
	});

	// --- completion under this scene's own popup --------------------------------------------------

	it("notices completion while its own clipping popup is up, through the grid pass", _ {
		// `PopupScene` is not `oled_transparent`, so `Shell::render_oled`'s walk stops at it
		// (`shell/shell.cpp:629-633`) and this scene is still not drawn on the panel. It is,
		// however, the grid **base** under a popup -- an overlay that composites nothing -- and
		// `Shell::tick`'s step 8b arms EVERY surface a scene is on at its `next_timer()` deadline,
		// so the grid gate fires at `kRecorderPollMs`, `render_grid` appends this scene to
		// `rendered_this_tick_`, and `after_render()`'s completion poll runs with the popup up.
		//
		// E3 task 5 recorded the opposite as an accepted, bounded stall (up to `kPopupTimeoutMs`,
		// 800 ms) and this example asserted it; E3 task 10's fold ended it. **This example is the
		// mutation guard on that fold**: delete step 8b and completion goes unnoticed at 30 ms
		// again, exactly as it used to be.
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = recorder_scene().recorder();
		rec->numSamplesCaptured = 1024;
		rec->recordingClippedRecently = true;
		r.tick(10);  // files the popup
		r.tick(20);  // Notices drains it and pushes SceneKind::Popup

		const auto popup_on_stack = [&r] {
			bool up = false;
			for (const deluge::ui::shell::Scene* s : r.shell.stack().scenes()) {
				up = up || s->kind() == deluge::ui::shell::SceneKind::Popup;
			}
			return up;
		};
		expect(popup_on_stack()).to_be_true();

		// The recording finishes underneath it, and IS noticed -- on the very next poll tick, not
		// 800 ms later.
		rec->status = RecorderStatus::COMPLETE;
		rec->hadCardError = true; // refuse the attach; this example is about the poll, not the sample
		r.tick(30);
		expect(recorder_scene().recording()).to_be_false();
		expect(r.on_stack()).to_be_false();
		// And it really was noticed *under* the popup: the popup is what `Shell::render_oled` still
		// stops at, and it is still on the stack now, with 770 ms of its timeout left to run.
		expect(popup_on_stack()).to_be_true();
	});

	// --- goldens ------------------------------------------------------------------------------

	it("draws the waiting screen on a 48-row panel", _ {
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		expect_golden(self, "../ui/scenes/recorder_waiting_48.pbm", r.fake.pbm(0));
	});

	it("draws the recording screen on a 48-row panel", _ {
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		recorder_scene().recorder()->numSamplesCaptured = 1;
		r.tick(deluge::scenes::recorder::kRecorderPollMs);
		expect_golden(self, "../ui/scenes/recorder_recording_48.pbm", r.fake.pbm(0));
		recorder_scene().recorder()->numSamplesCaptured = 0;
	});

	it("draws the recording screen on a 64-row panel", _ {
		Guards g;
		Rig r64{board_64(), profile_64()};
		r64.open();
		r64.tick(0);
		recorder_scene().recorder()->numSamplesCaptured = 1;
		r64.tick(deluge::scenes::recorder::kRecorderPollMs);
		expect_golden(self, "../ui/scenes/recorder_recording_64.pbm", r64.fake.pbm(0));
		recorder_scene().recorder()->numSamplesCaptured = 0;
	});
});
// clang-format on
DELUGE_SPEC(recorder_scene_spec, recorder_scene_spec_body)
