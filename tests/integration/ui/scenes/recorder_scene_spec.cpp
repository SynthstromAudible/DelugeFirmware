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
/// `SampleRecorder::setup` (`model/sample/sample_recorder.cpp`) allocates a `Sample` and one
/// capture buffer and returns; the file is created in `cardRoutine()`, which only
/// `AudioEngine::doRecorderCardRoutines()` calls on the device. The one example that pumps it does
/// so on an aborted take that never created a file, whose teardown is memory only.
/// `endSyncedRecording` is arithmetic over `numSamplesCaptured`, and `abort()` is
/// a single status store -- the file deletion that follows an abort is, again, in `cardRoutine()`.
/// The file runs with no card installed (`NoSdRoot`), and the card is in memory, so a future change
/// that *did* create a file could reach no committed fixture; `QuietAudio` keeps the audio routine
/// from running inside an example.
///
/// @par The reference
///
/// Main's `AudioRecorder::renderOLED` is the whole of what this UI draws, so it is compared
/// pixel-for-pixel: the port renders from its own recorder's `numSamplesCaptured`, and is compared
/// with the panel main draws for the same take, as `tests/golden/ui/menu_oracle/pixels/recorder_*.pbm`
/// holds it.
///
/// @note The spec may include `gui/`; the library may not. Same rule `browser_scene_spec.cpp` and
///       `marker_scene_spec.cpp` run under.

#include "fakes.h"
#include "gui/ui/menu_view_of.h"
#include "gui/ui/notify_drain.h"
#include "gui/ui/shell_drum_creator.h"
#include "gui/ui/shell_host/host_ports.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "model/fx/chain_params.h"
#include "support/global_guest_menu.h"
#include "ui/scenes/bind/ports/write_sink.h"
#include "ui/scenes/bind/target.h"
#include "ui/scenes/browser/browser_scene.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/recorder/recorder_scene.h"
#include "ui/scenes/registry.h"
#include "util/menu_entry.h"

#include "ui/shell/leds.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"
#include "ui/widgets/core/led_frame.h"
#include "ui/widgets/core/metrics.h"

#include "gui/l10n/l10n.h"
#include "hid/button.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "integration/scenarios/slot_coherence.h"
#include "memory/heaps.h"
#include "model/action/action_logger.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/settings/runtime_feature_settings.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/recording/foreground_recording.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/sound/source_resources.h"
#include "project/setting_group.h"
#include "runtime/app_loop.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/storage_manager.h"

#include "base/notify/dirty_set.h"
#include "base/notify/notice_queue.h"

#include "support/fake_ports.h"
#include "support/menu_engine.h"
#include "support/oracle_files.h"
#include "support/recorder_scene_fixtures.h"
#include "support/song_fixture.h"
#include "support/torn_down_take.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with every local named `it` in the project
// headers this drags in.
#include "support/card_access.h"

#include "support/mocks/fake_surfaces.h"
#include "support/printers/project_address_printer.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/gfx/support/pbm.h"
#include "unit/ui/widgets/support/frames.h"

#include "cppspec.hpp"
#include "instruments/classic/state_access.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/menu_context.h"
#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::recorder::kRecorderSceneKind;
using deluge::ui::scenes::recorder::recorder_scene;
using deluge::ui::scenes::recorder::RecorderScene;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
namespace rf = deluge::ui::scenes::spec::recorder_fx;
using rf::AudioClockAt;
using rf::Guards;
using rf::RecorderDisplay;
using rf::Rig;
using rf::spec_bail;

namespace PadLEDs {
/// The tick squares as last set; 255 is none. Defined in `hid/led/pad_leds.cpp`, not in its header.
extern uint8_t slowFlashSquares[];
} // namespace PadLEDs

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
		static constexpr const deluge::ui::widgets::core::Style* kStyles[] = {
		    &deluge::ui::widgets::core::kDelugeStyle64};
		BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

/// @brief `display` is @p d for the scope, and put back after.
struct DisplayAs {
	deluge::hid::Display* saved = display;
	explicit DisplayAs(deluge::hid::Display& d) { display = &d; }
	~DisplayAs() { display = saved; }
	DisplayAs(const DisplayAs&) = delete;
	DisplayAs& operator=(const DisplayAs&) = delete;
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

/// @brief `playbackHandler.slowJob`, registered if this binary never ran the app's setup, counting its runs.
int32_t g_slow_runs = 0;
void count_slow_run() {
	++g_slow_runs;
}
struct SlowJob {
	SlowJob() {
		if (playbackHandler.slowJob == deluge::runtime::kNoJob) {
			playbackHandler.slowJob =
			    deluge::runtime::appLoop.add("spec slow", &count_slow_run, deluge::runtime::kNever, 0);
		}
		g_slow_runs = 0;
	}
	/// @brief Run the app loop's passes until none is in progress.
	static void drain() {
		const deluge::runtime::Micros now = deluge::runtime::nowUs();
		(void)deluge::runtime::appLoop.service(now, 0);
		while (deluge::runtime::appLoop.inPass()) {
			(void)deluge::runtime::appLoop.service(deluge::runtime::nowUs(), 0);
		}
	}
	~SlowJob() { playbackHandler.nextSongPending_ = false; }
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
		(void)drum->getOrCreateFirstRange(0);
	}
	~KitRig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(deluge::ui::shell::SceneKind::Null);
		}
		spec::settle_torn_down_take(); // a take ended with its screen keeps the next one out until it is freed
		currentSong = saved_song;
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}
	KitRig(const KitRig&) = delete;
	KitRig& operator=(const KitRig&) = delete;

	MenuContext ctx() {
		return spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
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

/// @brief What main's `AudioRecorder::renderOLED` draws for one take's state, the whole 128x48 panel as
///        `pixels/recorder_<name>.pbm` holds it; empty when there is no such file.
std::string main_panel(std::string_view name) {
	return spec::OracleFiles::pixels("recorder_" + std::string{name});
}

/// @brief The state @p b was declared with this frame, or `Off` when it was not declared at all.
deluge::ui::widgets::core::LedState::Kind led_kind(const deluge::ui::widgets::core::LedFrame& f, ButtonId b) {
	for (const auto& [id, state] : f.entries()) {
		if (id == static_cast<deluge::ui::widgets::core::LedId>(b)) {
			return state.kind;
		}
	}
	return deluge::ui::widgets::core::LedState::Kind::Off;
}

/// @brief The shared blink rate @p b was declared on this frame, or `Normal` if it was not declared.
deluge::ui::widgets::core::BlinkRate led_rate(const deluge::ui::widgets::core::LedFrame& f, ButtonId b) {
	for (const auto& [id, state] : f.entries()) {
		if (id == static_cast<deluge::ui::widgets::core::LedId>(b)) {
			return state.rate;
		}
	}
	return deluge::ui::widgets::core::BlinkRate::Normal;
}

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
/// A hand-written save/restore pair inside one
/// example is the one shape that stops being a save/restore the moment an assertion between
/// them fails.
struct MonitoringOn {
	InputMonitoringMode saved = AudioEngine::inputMonitoringMode;
	MonitoringOn() { AudioEngine::inputMonitoringMode = InputMonitoringMode::ON; }
	~MonitoringOn() { AudioEngine::inputMonitoringMode = saved; }
	MonitoringOn(const MonitoringOn&) = delete;
	MonitoringOn& operator=(const MonitoringOn&) = delete;
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

/// @brief Whether the recorder is on @p shell's stack.
bool recorder_on_stack(deluge::ui::shell::Shell& shell) {
	return std::ranges::any_of(shell.stack().scenes(),
	                           [](const deluge::ui::shell::Scene* s) { return s->kind() == kRecorderSceneKind; });
}

/// @brief Whether the working animation is on @p shell's stack.
bool working_on_stack(deluge::ui::shell::Shell& shell) {
	return std::ranges::any_of(shell.stack().scenes(), [](const deluge::ui::shell::Scene* s) {
		return s->kind() == deluge::ui::shell::SceneKind::Working;
	});
}

/// @brief Whether a notice of @p kind, saying @p text when one is given, is queued; empties the queue.
bool drained_notice(deluge::base::notify::Notice::Kind kind, std::string_view text = {}) {
	bool seen = false;
	deluge::base::notify::Notice notice;
	while (deluge::base::notify::notices().pop(notice)) {
		seen = seen || (notice.kind == kind && (text.empty() || std::string_view{notice.text} == text));
	}
	return seen;
}

/// @brief A kit built the way a preset load builds one, its params backed up with the song, not yet in it.
Kit* loose_kit(::Song& song) {
	ParamManagerForTimeline params;
	params.setupUnpatched();
	deluge::model::initGlobalParams(&params);
	auto* kit = static_cast<Kit*>(StorageManager::createNewInstrument(OutputType::KIT, &params));
	song.live.param_backup.backUp(kit, nullptr, &params, true);
	return kit;
}

/// @brief A synth built the way a preset load builds one, its params backed up with the song, not yet in it.
SoundInstrument* loose_synth(::Song& song) {
	ParamManagerForTimeline params;
	auto* synth = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
	synth->setupAsDefaultSynth(&params);
	song.live.param_backup.backUp(synth, nullptr, &params, true);
	return synth;
}

} // namespace

// clang-format off
describe recorder_scene_spec_body("scenes/recorder RecorderScene", $ {

	// The ONLY thing left in `before_each`: installing a `Display`, which is a one-time static and
	// has nothing to leak. Every process global this spec touches is held by `Guards` instead --
	// see the comment above `spec_bail`. `before_each` cannot `expect`, so a defensive reset here
	// could only ever repair silently, which would hide a leak from the assertions that should see it.
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
		expect(deluge::gui::shell_host::input_take()).not_().to_be_null();
		// `AudioRecorder::opened` leaves `updatedRecordingStatus` false;
		// the port reads `numSamplesCaptured`, which a recorder nothing has fed is zero.
		expect(recorder_scene().capturing()).to_be_false();
		expect(recorder_scene().stopping()).to_be_false();
	});

	it("refuses to open with nowhere to put the finished sample", _ {
		// The port's own gate: `AudioRecorder::process`'s success arm dereferences the range with no null test
		// (`origin/main:src/deluge/gui/ui/audio_recorder.cpp`), and main is saved from that by
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

	// --- the screen, against main ---------------------------------------------------------------

	it("draws exactly what main's AudioRecorder::renderOLED draws, in both of its states", _ {
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		const std::string ported_waiting = r.oled();

		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		rec->numSamplesCaptured = 1024;
		r.tick(10);
		const std::string ported_recording = r.oled();

		expect(ported_waiting).to_equal(main_panel("waiting"));
		expect(ported_recording).to_equal(main_panel("captured"));
		// ...and the two really are different pictures, so the comparison above is not vacuous.
		expect(ported_waiting).not_().to_equal(ported_recording);

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

		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		rec->numSamplesCaptured = 1;
		// Two quiet ticks, one poll period apart. No pad, no button, no encoder.
		r.tick(deluge::ui::scenes::recorder::kRecorderPollMs);
		r.tick(2 * deluge::ui::scenes::recorder::kRecorderPollMs);
		expect(waiting).not_().to_equal(r.oled());
		expect(r.oled()).to_equal(main_panel("captured"));

		rec->numSamplesCaptured = 0;
	});

	it("asks to be woken while a recorder is armed, and stops asking once it is gone", _ {
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		expect(recorder_scene().next_timer().has_value()).to_be_true();
		expect(static_cast<int32_t>(recorder_scene().next_timer().value_or(0)))
		    .to_equal(static_cast<int32_t>(deluge::ui::scenes::recorder::kRecorderPollMs));

		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		rec->numSamplesCaptured = 4096;
		rec->status = RecorderStatus::COMPLETE;
		r.tick(10);
		expect(recorder_scene().next_timer().has_value()).to_be_false();
	});

	// --- stopping -----------------------------------------------------------------------------

	it("ends the recording on BACK, on SELECT_ENC and on RECORD", _ {
		// `AudioRecorder::buttonAction` (`origin/main:src/deluge/gui/ui/audio_recorder.cpp`): the three buttons that
		// mean "stop".
		const ButtonId kStopButtons[] = {ButtonId::Back, ButtonId::SelectEnc, ButtonId::Record};
		for (ButtonId b : kStopButtons) {
			Guards g;
			Rig r;
			r.open();
			r.tick(0);
			SampleRecorder* rec = deluge::gui::shell_host::input_take();
			// A recorder that captured nothing is aborted by `endSyncedRecording`,
			// which is a different arm; give it something so the ordinary one runs.
			rec->numSamplesCaptured = 44100;
			r.press(b, 10);
			r.tick(10);
			expect(recorder_scene().stopping()).to_be_true();
			// `endSyncedRecording` left `CAPTURING_DATA` behind, whichever of its two arms it took.
			expect(rec->status.load()).not_().to_equal(RecorderStatus::CAPTURING_DATA);

			rec->status = RecorderStatus::COMPLETE;
			r.tick(20);
		}
	});

	it("leaves every other button to whatever is beneath", _ {
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		rec->numSamplesCaptured = 44100;
		r.press(ButtonId::Play, 10);
		r.tick(10);
		expect(recorder_scene().stopping()).to_be_false();
		expect(rec->status.load()).to_equal(RecorderStatus::CAPTURING_DATA);

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
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		const Take take{*rec, "SAMPLES/RECORD/REC000.WAV"};
		Sample* captured = take.sample;
		rec->status = RecorderStatus::COMPLETE;

		r.tick(10);

		// `AudioRecorder::process`'s success arm, every statement of it.
		expect(static_cast<int32_t>(r.sound->oscType(0))).to_equal(static_cast<int32_t>(OscType::SAMPLE));
		AudioFileHolder* holder = r.range->getAudioFileHolder();
		expect(holder->audioFile).to_equal(static_cast<AudioFile*>(captured));
		expect(holder->filePath).to_equal(std::string("SAMPLES/RECORD/REC000.WAV"));
		// `deluge::recording`'s `finishRecording`, and the `close()` of main's `AudioRecorder::process`
		// (`origin/main:src/deluge/gui/ui/audio_recorder.cpp`).
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
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		const Take take{*rec, "SAMPLES/RECORD/REC001.WAV"};
		rec->status = RecorderStatus::COMPLETE;

		r.tick(10);

		expect(static_cast<int32_t>(r.sound->oscType(0))).to_equal(static_cast<int32_t>(OscType::SAMPLE));
		expect(deluge::harness::soundIncoherence(*r.sound, *r.song.song)).to_equal(std::string{});
	});

	it("attaches through one request, and the record names the take", _ {
		Guards g;
		{
			Rig r;
			bind::test::RecordingSoundWrites recording;
			r.open(&recording);
			r.tick(0);
			SampleRecorder* rec = deluge::gui::shell_host::input_take();
			const Take take{*rec, "SAMPLES/RECORD/REC002.WAV"};
			rec->status = RecorderStatus::COMPLETE;
			r.tick(10);
			expect(recording.calls.size()).to_equal(size_t{1});
			if (!recording.calls.empty()) {
				const bind::test::RecordingSoundWrites::Call& attach = recording.calls[0];
				expect(attach.kind).to_equal(bind::test::RecordingSoundWrites::Call::Kind::ATTACH);
				expect(attach.slot).to_equal(r.slot());
				expect(attach.source).to_equal(0);
				expect(attach.range).to_equal(std::optional<uint16_t>{0});
				expect(attach.text).to_equal(std::string{"SAMPLES/RECORD/REC002.WAV"});
			}
			// Only the request: the port took it and nothing else wrote.
			expect(static_cast<int32_t>(r.sound->oscType(0))).to_equal(static_cast<int32_t>(OscType::SQUARE));
			expect(r.on_stack()).to_be_false();
		}
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		const Take take{*rec, "SAMPLES/RECORD/REC003.WAV"};
		rec->status = RecorderStatus::COMPLETE;
		r.tick(10);
		const deluge::instruments::classic::SoundState* state = deluge::gui::shell_host::host_ports().sound->sound_state(r.slot());
		expect(state).not_().to_be_null();
		if (state != nullptr) {
			const std::vector<deluge::synth::SourceRange> ranges =
			    deluge::instruments::classic::source_ranges(*state, deluge::instruments::classic::SourceId::A);
			expect(deluge::instruments::classic::source_type(*state, deluge::instruments::classic::SourceId::A)).to_equal(OscType::SAMPLE);
			expect(ranges.empty()).to_be_false();
			if (!ranges.empty()) {
				expect(ranges[0].file_path).to_equal("SAMPLES/RECORD/REC003.WAV");
			}
		}
	});

	it("refuses an attach whose path names no take in memory, and nothing changes", _ {
		Guards g;
		Rig r;
		deluge::bridge::recordLoadedSound(*r.sound, *r.song.song);
		expect(deluge::gui::shell_host::host_ports().sound_writes->attach_recording(r.slot(), 0, 0, "SAMPLES/RECORD/NOWHERE.WAV")).to_be_false();
		expect(static_cast<int32_t>(r.sound->oscType(0))).to_equal(static_cast<int32_t>(OscType::SQUARE));
		expect(r.range->getAudioFileHolder()->audioFile).to_be_null();
		const deluge::instruments::classic::SoundState* state = deluge::gui::shell_host::host_ports().sound->sound_state(r.slot());
		expect(state).not_().to_be_null();
		if (state != nullptr) {
			expect(deluge::instruments::classic::source_type(*state, deluge::instruments::classic::SourceId::A)).to_equal(OscType::SQUARE);
		}
	});

	it("reports an attach the app refuses", _ {
		Guards g;
		Rig r;
		bind::test::RecordingSoundWrites refusing;
		refusing.accept = false;
		r.open(&refusing);
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		rec->numSamplesCaptured = 44100;
		rec->status = RecorderStatus::COMPLETE;
		r.tick(10);
		bool error_shown = false;
		deluge::base::notify::Notice notice;
		while (deluge::base::notify::notices().pop(notice)) {
			error_shown = error_shown || notice.kind == deluge::base::notify::Notice::Kind::Error;
		}
		expect(error_shown).to_be_true();
		expect(r.range->getAudioFileHolder()->audioFile).to_be_null();
		expect(r.on_stack()).to_be_false();
	});

	it("attaches the recorder's own take where another sample in memory has its path", _ {
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		const Take take{*rec, "SAMPLES/RECORD/REC005.WAV"};
		void* memory = deluge::memory::alloc_fast(sizeof(Sample));
		auto* stale = new (memory) Sample();
		stale->filePath = "SAMPLES/RECORD/REC005.WAV";
		(void)audioFileManager.audioFiles.insertElement(stale);
		rec->status = RecorderStatus::COMPLETE;
		r.tick(10);
		expect(r.range->getAudioFileHolder()->audioFile).to_equal(static_cast<AudioFile*>(take.sample));
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
			bind::test::RecordingWrites recording;
			MenuContext c = k.ctx();
			c.ports.writes = &recording;
			recorder_scene().open(c);
			k.shell.init(SceneId{kRecorderSceneKind, 0});
			k.shell.tick(0);
			expect(recording.gestures.size()).to_equal(size_t{1});
			if (!recording.gestures.empty()) {
				expect(recording.gestures[0].kind).to_equal(bind::test::RecordingWrites::Gesture::Kind::TEXT);
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

	it("opens the drum creator's take in the guest on source 0's recorder item alone", _ {
		// `enterDrumCreator`'s `setup(clip, &file0SelectorMenu, 0)` and `openUI(&audioRecorder)`: no page above the
		// recorder, so the take's end leaves the menu and the clip view is in front again.
		Guards g;
		KitRig k;
		expect(deluge::gui::ui::openShellDrumRecorder(k.song.clip)).to_be_true();
		expect(menu_scene().state().depth).to_equal(0);
		expect(menu_scene().state().path[0]).to_equal(&menu::nodes::sample0_recorder);
		nullifyUIs();
	});

	it("lands the drum creator's refused take on source 0's file selector, which asks for the browser", _ {
		// `enterDrumCreator`'s fallback: `openUI(&audioRecorder)` refused, so legacy opens the editor on
		// `file0SelectorMenu`, whose `FileSelector::beginSession` opens the sample browser. A take already running
		// (here an output recording) is the refusal `RecorderScene::can` makes.
		Guards g;
		deluge::ui::scenes::spec::UiStackGuard ui_guard;
		deluge::ui::scenes::spec::GlobalGuestGuard guest_guard;
		KitRig k;
		// The clip view under the guest, as on the device: the guest's close needs a UI to close back to.
		setRootUILowLevel(&instrumentClipView);
		{
			PublishedOutputRecording output;
			expect(deluge::gui::ui::openShellDrumRecorder(k.song.clip)).to_be_true();
			// The menu's render files the recorder's push; the router applies it, the recorder refuses, the menu resumes.
			for (int32_t i = 0; i < 4 && menu_scene().state().path[0] == &menu::nodes::sample0_recorder; i++) {
				(void)shellHostUI.timerCallback();
			}
		}
		expect(recorder_scene().last_enter_refused()).to_be_true();
		expect(isUIOpen(&shellHostUI)).to_be_true();
		expect(menu_scene().state().depth).to_equal(0);
		expect(menu_scene().state().path[0]).to_equal(&menu::nodes::file0_selector);
		// The leaf's hook asked for the browser, which the menu's next render pushes.
		expect(menu_scene().state().walk.pending_scene.kind).to_equal(browser::kBrowserSceneKind);
		nullifyUIs();
	});

	it("leaves the root view's animation stopped under the recorder, as main's base UI does", _ {
		// `UI::canSeeViewUnderneath` is false and `AudioRecorder` does not override it; the menu under it says true.
		Guards g;
		deluge::ui::scenes::spec::UiStackGuard ui_guard;
		deluge::ui::scenes::spec::GlobalGuestGuard guest_guard;
		KitRig k;
		setRootUILowLevel(&instrumentClipView);
		expect(deluge::gui::ui::openShellDrumRecorder(k.song.clip)).to_be_true();
		expect(shellHostUI.canSeeViewUnderneath()).to_be_true();
		for (int32_t i = 0; i < 4 && !recorder_on_stack(shellHostUI.shell()); i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(recorder_on_stack(shellHostUI.shell())).to_be_true();
		expect(shellHostUI.canSeeViewUnderneath()).to_be_false();
		nullifyUIs();
	});

	it("passes a SYNC_SCALING release to the root view under the recorder while fill mode is on", _ {
		// `AudioRecorder::buttonAction`: "allow turning off fill mode if it was active while entering audio recorder".
		Guards g;
		deluge::ui::scenes::spec::UiStackGuard ui_guard;
		deluge::ui::scenes::spec::GlobalGuestGuard guest_guard;
		KitRig k;
		const uint32_t saved_action = runtimeFeatureSettings.get(RuntimeFeatureSettingType::SyncScalingAction);
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::SyncScalingAction,
		                           RuntimeFeatureStateSyncScalingAction::Fill);
		setRootUILowLevel(&instrumentClipView);
		expect(deluge::gui::ui::openShellDrumRecorder(k.song.clip)).to_be_true();
		for (int32_t i = 0; i < 4 && !recorder_on_stack(shellHostUI.shell()); i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(recorder_on_stack(shellHostUI.shell())).to_be_true();
		currentSong->changeFillMode(true);
		expect(currentSong->isFillModeActive()).to_be_true();
		// The press is not the recorder's to pass on: fill stays on.
		(void)shellHostUI.buttonAction(deluge::hid::button::SYNC_SCALING, true);
		expect(currentSong->isFillModeActive()).to_be_true();
		expect(shellHostUI.buttonAction(deluge::hid::button::SYNC_SCALING, false)).to_equal(ActionResult::DEALT_WITH);
		expect(currentSong->isFillModeActive()).to_be_false();
		expect(recorder_on_stack(shellHostUI.shell())).to_be_true();
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::SyncScalingAction, saved_action);
		nullifyUIs();
	});

	it("leaves the menu when the creator's take that did begin closes, as legacy's recorder leaves the clip view", _ {
		Guards g;
		deluge::ui::scenes::spec::UiStackGuard ui_guard;
		deluge::ui::scenes::spec::GlobalGuestGuard guest_guard;
		KitRig k;
		// The clip view under the guest, as on the device: the guest's close needs a UI to close back to.
		setRootUILowLevel(&instrumentClipView);
		expect(deluge::gui::ui::openShellDrumRecorder(k.song.clip)).to_be_true();
		for (int32_t i = 0; i < 4 && !recorder_on_stack(shellHostUI.shell()); i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(recorder_on_stack(shellHostUI.shell())).to_be_true();
		expect(recorder_scene().last_enter_refused()).to_be_false();
		// The take ends from outside (its `on_exit` ends it and keeps its file); the menu resumes and goes up, out.
		(void)shellHostUI.shell().stack().remove(kRecorderSceneKind);
		for (int32_t i = 0; i < 8 && isUIOpen(&shellHostUI); i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(menu_scene().state().path[0]).not_().to_equal(&menu::nodes::file0_selector);
		nullifyUIs();
	});

	it("ends a take whose guest a UI teardown took down, and frees it once it has settled", _ {
		// A song load or a clear takes every UI down (`nullifyUIs`) with the recorder on top, and nothing ticks a closed
		// guest: the teardown itself ends the screen's take, as main's recorder does once its UI is gone.
		Guards g;
		deluge::ui::scenes::spec::UiStackGuard ui_guard;
		deluge::ui::scenes::spec::GlobalGuestGuard guest_guard;
		KitRig k;
		setRootUILowLevel(&instrumentClipView);
		expect(deluge::gui::ui::openShellDrumRecorder(k.song.clip)).to_be_true();
		for (int32_t i = 0; i < 4 && !recorder_on_stack(shellHostUI.shell()); i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(recorder_scene().recording()).to_be_true();
		nullifyUIs();
		expect(recorder_on_stack(shellHostUI.shell())).to_be_false();
		expect(recorder_scene().recording()).to_be_false();
		// Still published until its file is closed, then freed by the foreground recording, attached to nothing.
		expect(deluge::recording::inputTakeInProgress()).to_be_true();
		deluge::ui::scenes::spec::settle_torn_down_take();
		expect(AudioEngine::foregroundRecorder()).to_be_null();
		expect(static_cast<int32_t>(AudioEngine::foregroundRecordingSource()))
		    .to_equal(static_cast<int32_t>(AudioInputChannel::NONE));
	});

	it("leaves the song load's animation up when a UI teardown takes a running take's guest down", _ {
		// `LoadSongUI` shows "Loading" and then takes every UI down: the take that ends with its screen showed no
		// animation, so the one up is the load's, and it stays until the load takes it down.
		Guards g;
		RecorderDisplay shown;
		DisplayAs as{shown};
		deluge::ui::scenes::spec::UiStackGuard ui_guard;
		deluge::ui::scenes::spec::GlobalGuestGuard guest_guard;
		KitRig k;
		setRootUILowLevel(&instrumentClipView);
		expect(deluge::gui::ui::openShellDrumRecorder(k.song.clip)).to_be_true();
		for (int32_t i = 0; i < 4 && !recorder_on_stack(shellHostUI.shell()); i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(recorder_scene().recording()).to_be_true();
		display->displayLoadingAnimationText("Loading");
		nullifyUIs();
		drain_notifications();
		expect(recorder_scene().recording()).to_be_false();
		expect(shown.loading).to_be_true();
	});

	it("leaves no working animation behind when a UI teardown takes a stopping take's guest down", _ {
		// The stop showed the guest's own "Working"; the teardown takes that down with the guest and leaves the song
		// load's animation alone. The next guest's popups expire on time: an animation still armed would hold them
		// up (`Notices::next_timer`).
		Guards g;
		RecorderDisplay shown;
		DisplayAs as{shown};
		deluge::ui::scenes::spec::UiStackGuard ui_guard;
		deluge::ui::scenes::spec::GlobalGuestGuard guest_guard;
		KitRig k;
		setRootUILowLevel(&instrumentClipView);
		expect(deluge::gui::ui::openShellDrumRecorder(k.song.clip)).to_be_true();
		for (int32_t i = 0; i < 4 && !recorder_on_stack(shellHostUI.shell()); i++) {
			(void)shellHostUI.timerCallback();
		}
		deluge::gui::shell_host::input_take()->numSamplesCaptured = 44100;
		(void)shellHostUI.buttonAction(deluge::hid::button::BACK, true);
		// One tick routes the press, which posts the "Working"; the next shows it.
		(void)shellHostUI.timerCallback();
		(void)shellHostUI.timerCallback();
		expect(recorder_scene().stopping()).to_be_true();
		expect(working_on_stack(shellHostUI.shell())).to_be_true();

		display->displayLoadingAnimationText("Loading");
		nullifyUIs();
		drain_notifications();
		expect(shown.loading).to_be_true();

		deluge::ui::scenes::spec::settle_torn_down_take();
		expect(deluge::gui::ui::openShellDrumRecorder(k.song.clip)).to_be_true();
		for (int32_t i = 0; i < 4 && !recorder_on_stack(shellHostUI.shell()); i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(working_on_stack(shellHostUI.shell())).to_be_false();
		deluge::base::notify::notice_text("POPUP");
		(void)shellHostUI.timerCallback();
		expect(shellHostUI.shell().notices().next_timer().has_value()).to_be_true();
		nullifyUIs();
	});

	it("goes up from a refused recorder no creator opened, as legacy's editor does from its recorder item", _ {
		Guards g;
		deluge::ui::scenes::spec::UiStackGuard ui_guard;
		deluge::ui::scenes::spec::GlobalGuestGuard guest_guard;
		KitRig k;
		// The clip view under the guest, as on the device: the guest's close needs a UI to close back to.
		setRootUILowLevel(&instrumentClipView);
		{
			PublishedOutputRecording output;
			expect(deluge::gui::ui::openMenu(k.song.clip, 0, false, false, false, false,
			                                      deluge::gui::ui::MenuNode::Osc0Recorder))
			    .to_be_true();
			for (int32_t i = 0; i < 8 && isUIOpen(&shellHostUI); i++) {
				(void)shellHostUI.timerCallback();
			}
		}
		expect(recorder_scene().last_enter_refused()).to_be_true();
		expect(isUIOpen(&shellHostUI)).to_be_false();
		nullifyUIs();
	});

	it("ends the take on a card error and frees it only once the recorder has settled", _ {
		// `AudioRecorder::slowRoutine`'s card-error arm: stop capturing, then wait for the
		// recorder to close its file and for the driver to answer the last write it was given --
		// `finish_recording()` frees the buffer that write borrows.
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		const Take take{*rec, "SAMPLES/RECORD/REC000.WAV"};
		rec->numSamplesCaptured = 1024;
		rec->hadCardError = true;

		r.tick(10);
		expect(recorder_scene().stopping()).to_be_true();
		expect(recorder_scene().recording()).to_be_true();
		expect(deluge::gui::shell_host::input_take()).to_equal(rec);
		expect(r.on_stack()).to_be_true();

		// Settled: the card routine has closed the take. The next poll frees it, attaching nothing.
		rec->status = RecorderStatus::COMPLETE;
		r.tick(20);
		expect(recorder_scene().recording()).to_be_false();
		expect(r.range->getAudioFileHolder()->audioFile).to_be_null();
		expect(r.on_stack()).to_be_false();
	});

	it("attaches nothing when the recording aborted, but still closes", _ {
		// `AudioRecorder::process`'s empty `if (status == ABORTED || hadCardError) {}` arm.
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		rec->status = RecorderStatus::ABORTED;
		// Not settled until its card routine has torn the take down, which the app loop does on the
		// device. No file was ever created, so the teardown is memory only.
		r.tick(10);
		expect(recorder_scene().recording()).to_be_true();
		for (int32_t pass = 0; pass < 8 && !rec->settled(); ++pass) {
			(void)rec->cardRoutine();
		}
		expect(rec->settled()).to_be_true();
		r.tick(20);
		expect(r.range->getAudioFileHolder()->audioFile).to_be_null();
		expect(static_cast<int32_t>(r.sound->oscType(0))).to_equal(static_cast<int32_t>(OscType::SQUARE));
		expect(recorder_scene().recording()).to_be_false();
		expect(r.on_stack()).to_be_false();
	});

	it("attaches nothing when the card errored, even though the status says complete", _ {
		// The other half of the same condition: `hadCardError` alone is enough to refuse the
		// attach, and it also ends the take: the scene stops the capture, then frees the recorder once it has settled.
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		rec->status = RecorderStatus::COMPLETE;
		rec->hadCardError = true;
		r.tick(10);
		expect(r.range->getAudioFileHolder()->audioFile).to_be_null();
		expect(recorder_scene().recording()).to_be_false();
		expect(r.on_stack()).to_be_false();
	});

	it("raises CLIPPING OCCURRED only when no popup is up, and clears the flag either way", _ {
		// `process()`'s `!display->hasPopup()` gate.
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		deluge::base::notify::notice_text("A KNOB", deluge::base::notify::Notice::Kind::Text);
		r.tick(10);
		expect(std::string{r.shell.notices().last_popup_text()}).to_equal(std::string{"A KNOB"});
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		rec->recordingClippedRecently = true;
		r.tick(20);
		r.tick(30);
		expect(rec->recordingClippedRecently).to_be_false();
		expect(std::string{r.shell.notices().last_popup_text()}).to_equal(std::string{"A KNOB"});
	});

	it("raises CLIPPING OCCURRED when no popup is up", _ {
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		rec->recordingClippedRecently = true;
		r.tick(10);
		r.tick(20);
		expect(std::string{r.shell.notices().last_popup_text()}).to_equal(std::string{"Clipping occurred"});
	});

	it("clears the playhead squares as the recorder opens", _ {
		// `AudioRecorder::opened`'s `PadLEDs::clearTickSquares(true)`.
		Guards g;
		Rig r;
		const uint8_t saved_cursor = PadLEDs::flashCursor;
		PadLEDs::flashCursor = FLASH_CURSOR_FAST; // nothing to send to the pads
		PadLEDs::slowFlashSquares[2] = 3;
		r.open();
		expect(static_cast<int32_t>(PadLEDs::slowFlashSquares[2])).to_equal(255);
		PadLEDs::flashCursor = saved_cursor;
	});

	it("clears the clipping flag when it reports it, so one clip is one popup", _ {
		// `AudioRecorder::process`'s clipping arm. The popup itself is a `Notices` matter; what
		// this scene owns is the flag, which legacy clears before showing anything.
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
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

	it("ends a take torn down under it and keeps its file, attached to nothing, freed once it has settled", _ {
		// `AudioRecorder::tick`'s `!isUIOpen(this)` arm: the screen is gone, so what the take was for may be too.
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		rec->numSamplesCaptured = 1024;

		r.shell.stack().pop(deluge::ui::shell::SceneKind::Null);
		r.tick(10);
		expect(recorder_scene().recording()).to_be_false();
		// Ended, not aborted: an abort deletes the part-written file.
		expect(rec->status.load()).not_().to_equal(RecorderStatus::ABORTED);
		expect(rec->status.load()).not_().to_equal(RecorderStatus::CAPTURING_DATA);
		// Still the foreground recording until it settles: freed any earlier, a write could still be reading it.
		expect(AudioEngine::foregroundRecorder()).to_equal(rec);
		deluge::recording::slowRoutine();
		expect(AudioEngine::foregroundRecorder()).to_equal(rec);

		rec->status = RecorderStatus::COMPLETE; // what the card routine leaves once the file is closed
		deluge::recording::slowRoutine();
		expect(static_cast<int32_t>(AudioEngine::foregroundRecordingSource()))
		    .to_equal(static_cast<int32_t>(AudioInputChannel::NONE));
		expect(AudioEngine::foregroundRecorder()).to_be_null();
		expect(r.range->getAudioFileHolder()->audioFile).to_be_null();
		expect(static_cast<int32_t>(r.sound->oscType(0))).to_equal(static_cast<int32_t>(OscType::SQUARE));
	});

	it("leaves up an animation it did not show when it frees a torn-down take", _ {
		// The take ended with no "Working" of its own, so an animation up is another's: the song load that took the
		// screen down shows one, and takes it down itself.
		Guards g;
		RecorderDisplay shown;
		DisplayAs as{shown};
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		rec->numSamplesCaptured = 1024;
		display->displayLoadingAnimationText("Loading");
		r.shell.stack().pop(deluge::ui::shell::SceneKind::Null);
		r.tick(10);
		rec->status = RecorderStatus::COMPLETE;
		deluge::recording::slowRoutine();
		expect(AudioEngine::foregroundRecorder()).to_be_null();
		expect(shown.loading).to_be_true();
	});

	it("attaches a kit row's take to its row", _ {
		Guards g;
		KitRig k;
		recorder_scene().open(k.ctx());
		k.shell.init(SceneId{kRecorderSceneKind, 0});
		k.shell.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		expect(rec).not_().to_be_null();
		const Take take{*rec, "SAMPLES/RECORD/REC024.WAV"};
		rec->status = RecorderStatus::COMPLETE;
		k.shell.tick(10);
		expect(recorder_scene().recording()).to_be_false();
		expect(k.drum->sourceResources[0].ranges.getElement(0)->getAudioFileHolder()->audioFile)
		    .to_equal(static_cast<AudioFile*>(take.sample));
	});

	it("keeps a kit row's take unattached, silently, when its edited kit is replaced under it and only sleeps", _ {
		Guards g;
		KitRig k;
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		k.song.instrument->setActiveClip(setupModelStackWithTimelineCounter(memory, k.song.song, k.song.clip));
		recorder_scene().open(k.ctx());
		k.shell.init(SceneId{kRecorderSceneKind, 0});
		k.shell.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		expect(rec).not_().to_be_null();
		const Take take{*rec, "SAMPLES/RECORD/REC025.WAV"};
		// A kit preset change under the take: the edited kit hibernates with its rows, under their own ids.
		k.song.instrument->editedByUser = true;
		k.song.song->replaceInstrument(k.song.instrument, loose_kit(*k.song.song));
		expect(k.song.song->live.media.first_hibernating_instrument).to_equal(k.song.instrument);
		rec->status = RecorderStatus::COMPLETE;
		k.shell.tick(10);
		expect(recorder_scene().recording()).to_be_false();
		// A kit row is a sample source already: the range holding no file is what says nothing was attached.
		expect(k.drum->sourceResources[0].ranges.getElement(0)->getAudioFileHolder()->audioFile).to_be_null();
		expect(drained_notice(deluge::base::notify::Notice::Kind::Error)).to_be_false();
	});

	it("refuses a take with a Loading popup while a load may replace its sound, and keeps the undo log", _ {
		Guards g;
		deluge::ui::scenes::spec::UiStackGuard ui_guard;
		Rig r;
		setRootUILowLevel(&instrumentClipView); // `getNewAction` logs only with a view on top
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		expect(actionLogger.firstAction[BEFORE]).not_().to_be_null();

		MenuContext c = r.ctx();
		deluge::ui::scenes::spec::HeldPorts loading{c};
		loading.card.replacing = true;
		r.open_on(c); // `on_enter` runs as the scene is pushed
		expect(drained_notice(deluge::base::notify::Notice::Kind::Text, "Loading")).to_be_true();
		r.tick(0);
		expect(r.on_stack()).to_be_false();
		expect(recorder_scene().recording()).to_be_false();
		expect(recorder_scene().last_enter_refused()).to_be_true();
		expect(actionLogger.firstAction[BEFORE]).not_().to_be_null();
		actionLogger.deleteAllLogs();
	});

	it("attaches the take to the range it began on after a range is inserted before it", _ {
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		const Take take{*rec, "SAMPLES/RECORD/REC020.WAV"};
		// A range below the one the take is for: that range is now the source's second.
		expect(deluge::gui::shell_host::host_ports().sound_writes->insert_range(r.slot(), 0, 0, 40, std::nullopt)).to_be_true();
		rec->status = RecorderStatus::COMPLETE;
		r.tick(10);
		expect(r.on_stack()).to_be_false();
		expect(r.source->ranges.size()).to_equal(size_t{2});
		expect(r.source->ranges.getElement(1)->getAudioFileHolder()->audioFile)
		    .to_equal(static_cast<AudioFile*>(take.sample));
		expect(r.source->ranges.getElement(0)->getAudioFileHolder()->audioFile).to_be_null();
	});

	it("keeps the take unattached, silently, when its range was replaced by another at the same place", _ {
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		const Take take{*rec, "SAMPLES/RECORD/REC021.WAV"};
		expect(deluge::gui::shell_host::host_ports().sound_writes->insert_range(r.slot(), 0, 0, 40, std::nullopt)).to_be_true();
		expect(deluge::gui::shell_host::host_ports().sound_writes->delete_range(r.slot(), 0, 1, std::nullopt)).to_be_true();
		(void)drained_notice(deluge::base::notify::Notice::Kind::Error);
		rec->status = RecorderStatus::COMPLETE;
		r.tick(10);
		expect(r.on_stack()).to_be_false();
		expect(r.source->ranges.getElement(0)->getAudioFileHolder()->audioFile).to_be_null();
		expect(static_cast<int32_t>(r.sound->oscType(0))).to_equal(static_cast<int32_t>(OscType::SQUARE));
		expect(drained_notice(deluge::base::notify::Notice::Kind::Error)).to_be_false();
	});

	it("keeps the take unattached, silently, when its edited synth is replaced under it and only sleeps in the song", _ {
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		const Take take{*rec, "SAMPLES/RECORD/REC022.WAV"};
		// A preset change under the take: the edited synth hibernates under its own output id. The clip plays it first,
		// as a loaded song's does: `Song::replaceInstrument` reads its params through the active clip.
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		r.song.instrument->setActiveClip(setupModelStackWithTimelineCounter(memory, r.song.song, r.song.clip));
		r.song.instrument->editedByUser = true;
		SoundInstrument* replacement = loose_synth(*r.song.song);
		r.song.song->replaceInstrument(r.song.instrument, replacement);
		expect(r.song.song->live.media.first_hibernating_instrument).to_equal(r.song.instrument);
		rec->status = RecorderStatus::COMPLETE;
		r.tick(10);
		expect(r.on_stack()).to_be_false();
		expect(static_cast<int32_t>(r.sound->oscType(0))).to_equal(static_cast<int32_t>(OscType::SQUARE));
		expect(replacement->oscType(0)).not_().to_equal(OscType::SAMPLE);
		expect(drained_notice(deluge::base::notify::Notice::Kind::Error)).to_be_false();
	});

	it("keeps the take unattached, silently, in another song, even one with a sound at its slot", _ {
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		const Take take{*rec, "SAMPLES/RECORD/REC023.WAV"};
		spec::SongFixture other = spec::make_synth_song();
		other.instrument->setId(r.slot().id); // another song's first track can have the same output id
		auto* other_sound = deluge::model::effects_owner_of(deluge::model::owner_of(*other.instrument))->sound;
		(void)other_sound->getOrCreateFirstRange(0);
		const OscType before = other_sound->oscType(0);
		currentSong = other.song;
		rec->status = RecorderStatus::COMPLETE;
		r.tick(10);
		currentSong = r.song.song;
		expect(r.on_stack()).to_be_false();
		expect(static_cast<int32_t>(other_sound->oscType(0))).to_equal(static_cast<int32_t>(before));
		expect(static_cast<int32_t>(r.sound->oscType(0))).to_equal(static_cast<int32_t>(OscType::SQUARE));
		expect(drained_notice(deluge::base::notify::Notice::Kind::Error)).to_be_false();
	});

	it("greys out every column of the grid, exactly as getGreyoutColsAndRows does", _ {
		// `AudioRecorder::getGreyoutColsAndRows` (`origin/main:src/deluge/gui/ui/audio_recorder.cpp`):
		// `*cols = 0xFFFFFFFF`, rows untouched.
		expect(static_cast<int64_t>(recorder_scene().greyout_mask().cols)).to_equal(static_cast<int64_t>(0xFFFFFFFF));
		expect(static_cast<int64_t>(recorder_scene().greyout_mask().rows)).to_equal(static_cast<int64_t>(0));
	});

	it("leaves every pad to the view beneath: a grid overlay that draws no region", _ {
		// `AudioRecorder` overrides neither `renderMainPads` nor `renderSidebar`: `main` shows the view's mute and
		// audition columns under the recorder, greyed by `getGreyoutColsAndRows`.
		expect(recorder_scene().is_grid_overlay()).to_be_true();
		expect(recorder_scene().pad_regions()).to_equal(deluge::ui::shell::PadRegions::None);
		expect(recorder_scene().draws_pads()).to_be_false();
	});

	it("flashes its source's recorder shortcut pad once every blink cycle while armed", _ {
		// `opened()`'s `setupShortcutBlink(currentSourceIndex, 4, 0)` and `blinkShortcut()`: frequency 0 flashes the
		// param pad on every cycle's param step, 20 ms into each 200 ms cycle.
		Guards g;
		Rig r;
		r.open();
		const size_t from = r.fake.flashes_sent.size();
		for (uint32_t at = 0; at <= 400; at += 10) {
			r.tick(at);
		}
		const std::vector<deluge::ui::widgets::grid::PadFlash> flashes{
		    r.fake.flashes_sent.begin() + static_cast<std::ptrdiff_t>(from), r.fake.flashes_sent.end()};
		expect(flashes.size()).to_equal(size_t{2}); // at 20 ms and 220 ms
		for (const deluge::ui::widgets::grid::PadFlash& flash : flashes) {
			expect(static_cast<int32_t>(flash.x)).to_equal(0);
			expect(static_cast<int32_t>(flash.y)).to_equal(4);
			expect(flash.colour).to_equal(deluge::ui::widgets::grid::FlashColour::Default);
		}
	});

	it("restarts both shared blinks as the recording begins", _ {
		// `beginRecording`'s `blinkLed(BACK)` and `blinkLed(RECORD, 255, 1)` (`AudioRecorder::opened`).
		Guards g;
		Rig r;
		r.open_over_menu();
		deluge::ui::widgets::core::LedFrame f;
		recorder_scene().leds(f);
		expect(f.restart_requested(deluge::ui::widgets::core::BlinkRate::Normal).value_or(false)).to_be_true();
		expect(f.restart_requested(deluge::ui::widgets::core::BlinkRate::Fast).value_or(false)).to_be_true();
		deluge::ui::widgets::core::LedFrame again;
		recorder_scene().leds(again);
		expect(again.restart_requested(deluge::ui::widgets::core::BlinkRate::Normal).has_value()).to_be_false();
	});

	it("declares the LEDs opened() sets, RECORD blinking at the fast rate", _ {
		// `AudioRecorder::opened`'s LED tail. SYNTH/KIT are the
		// interesting pair: `SoundEditor::setLedStates` turns BOTH off
		// and this UI lights exactly one, so a scene that left them to `MenuScene` beneath would
		// show neither.
		Guards g;
		Rig r;
		r.open_over_menu();
		r.tick(0);
		expect(menu_scene().focused()).not_().to_be_null(); // else the menu declares nothing

		// Composed over the REAL stack -- `MenuScene` beneath, recorder on top -- rather than by
		// calling `leds()` directly, because the SYNTH/KIT claim is precisely that this scene's
		// declaration outranks the menu's. `MenuScene::leds` declares both
		// OFF, mirroring `SoundEditor::setLedStates`.
		deluge::ui::widgets::core::LedFrame f;
		deluge::ui::shell::LedCompose::compose(f, {}, r.shell.stack().scenes());
		expect(static_cast<int32_t>(led_kind(f, ButtonId::Record)))
		    .to_equal(static_cast<int32_t>(deluge::ui::widgets::core::LedState::Kind::Shared));
		expect(led_rate(f, ButtonId::Record)).to_equal(deluge::ui::widgets::core::BlinkRate::Fast); // kFastFlashTime
		expect(static_cast<int32_t>(led_kind(f, ButtonId::Back)))
		    .to_equal(static_cast<int32_t>(deluge::ui::widgets::core::LedState::Kind::Shared));
		expect(led_rate(f, ButtonId::Back)).to_equal(deluge::ui::widgets::core::BlinkRate::Normal); // kFlashTime
		// The fixture is a synth, so `editingKit()` is false: SYNTH on, KIT off.
		expect(static_cast<int32_t>(led_kind(f, ButtonId::Synth)))
		    .to_equal(static_cast<int32_t>(deluge::ui::widgets::core::LedState::Kind::On));
		expect(static_cast<int32_t>(led_kind(f, ButtonId::Kit)))
		    .to_equal(static_cast<int32_t>(deluge::ui::widgets::core::LedState::Kind::Off));
		expect(static_cast<int32_t>(led_kind(f, ButtonId::Scale)))
		    .to_equal(static_cast<int32_t>(deluge::ui::widgets::core::LedState::Kind::Off));
	});

	// --- the foreground-recording publication ---------------------------------------------------

	it("publishes the recording where the nine subsystems that gate on it can see it", _ {
		// `AudioRecorder::opened` publishes the take's channel and recorder, and those are not
		// private bookkeeping: the subsystems that gate on a live recording read them through
		// `AudioEngine::foregroundRecordingSource()` / `AudioEngine::foregroundRecorder()`. The port
		// publishes through `AudioEngine::setForegroundRecording`, the one writer of that storage --
		// so this example reads them back the way every reader does.
		Guards g;
		NothingPluggedIn unplugged;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();

		// Nothing plugged in, so `AudioRecorder::opened`'s stereo test is false and the channel is
		// LEFT.
		expect(static_cast<int32_t>(AudioEngine::foregroundRecordingSource()))
		    .to_equal(static_cast<int32_t>(AudioInputChannel::LEFT));
		expect(AudioEngine::foregroundRecorder()).to_equal(rec);

		// ...and cleared once the recording is over.
		rec->status = RecorderStatus::COMPLETE;
		rec->hadCardError = true; // refuse the attach; this example is about the publication
		r.tick(10);
		expect(static_cast<int32_t>(AudioEngine::foregroundRecordingSource()))
		    .to_equal(static_cast<int32_t>(AudioInputChannel::NONE));
		expect(AudioEngine::foregroundRecorder()).to_be_null();
	});

	it("keeps input monitoring alive for the whole recording", _ {
		// The consequence the state assertion above exists for, asserted as BEHAVIOUR:
		// `AudioEngine::setMonitoringMode` turns `inputMonitor` on
		// only for `STEREO`/`LEFT` and double-checks the recorder pointer. A port that armed a
		// recorder without publishing it records correctly and leaves the user hearing nothing.
		Guards g;
		MonitoringOn monitoring;

		AudioEngine::setMonitoringMode();
		expect(AudioEngine::inputMonitor.on()).to_be_false(); // nothing armed yet

		Rig r;
		r.open();
		r.tick(0);
		AudioEngine::setMonitoringMode();
		expect(AudioEngine::inputMonitor.on()).to_be_true();

		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		rec->status = RecorderStatus::COMPLETE;
		rec->hadCardError = true;
		r.tick(10);
		AudioEngine::setMonitoringMode();
		expect(AudioEngine::inputMonitor.on()).to_be_false(); // ...and off again once it is over
	});

	it("refuses to open while any foreground recording is already live", _ {
		// `AudioRecorder::opened`'s first refusal, which is `recordingSource > NONE` -- and which
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
		// (`Shell::render_oled`) and this scene is still not drawn on the panel. It is,
		// however, the grid **base** under a popup -- an overlay that composites nothing -- and
		// `Shell::tick`'s step 8b arms EVERY surface a scene is on at its `next_timer()` deadline,
		// so the grid gate fires at `kRecorderPollMs`, `render_grid` appends this scene to
		// `rendered_this_tick_`, and `after_render()`'s completion poll runs with the popup up.
		//
		// Completion is therefore noticed at `kRecorderPollMs`, not after the popup's timeout
		// (`kPopupTimeoutMs`, 800 ms). **This example is the mutation guard on step 8b**: delete it
		// and completion goes unnoticed until the popup closes.
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
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
		expect_golden(self, "ui/scenes/recorder_waiting_48.pbm", r.fake.pbm(0));
	});

	it("draws the recording screen on a 48-row panel", _ {
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		deluge::gui::shell_host::input_take()->numSamplesCaptured = 1;
		r.tick(deluge::ui::scenes::recorder::kRecorderPollMs);
		expect_golden(self, "ui/scenes/recorder_recording_48.pbm", r.fake.pbm(0));
		deluge::gui::shell_host::input_take()->numSamplesCaptured = 0;
	});

	it("draws the recording screen on a 64-row panel", _ {
		Guards g;
		Rig r64{board_64(), profile_64()};
		r64.open();
		r64.tick(0);
		deluge::gui::shell_host::input_take()->numSamplesCaptured = 1;
		r64.tick(deluge::ui::scenes::recorder::kRecorderPollMs);
		expect_golden(self, "ui/scenes/recorder_recording_64.pbm", r64.fake.pbm(0));
		deluge::gui::shell_host::input_take()->numSamplesCaptured = 0;
	});

	// --- the SAMPLE RECORDER page ---------------------------------------------------------------------

	it("lands Shift + its shortcut pad on the SAMPLE RECORDER page, focused on source 0's item, and opens nothing", _ {
		// `SoundEditor::setup` asks `maybeGetParentMenu` for the chain page that lists the item and begins that page,
		// focused on it (`recorderMenu`); only a select there opens the recorder.
		Guards g;
		Rig r;
		r.open_menu(menu::nodes::source0);
		r.tick(0);
		r.press(ButtonId::Shift, 10);
		r.tick(10);
		r.pad(0, 4, true, 20);
		r.tick(20);
		r.pad(0, 4, false, 30);
		r.tick(30);
		r.release(ButtonId::Shift, 40);
		for (uint32_t at = 40; at <= 80; at += 10) {
			r.tick(at);
		}
		expect(menu::node_id(menu::nodes::recorder)).not_().to_equal(0);
		expect(menu_scene().state().path[0]).to_equal(&menu::nodes::recorder);
		expect(menu_scene().state().depth).to_equal(0);
		expect(static_cast<int32_t>(menu_scene().state().columns[0].focused)).to_equal(0);
		expect(r.on_stack()).to_be_false();
		expect(recorder_scene().recording()).to_be_false();
	});

	it("opens from a select on that page, over the page", _ {
		Guards g;
		Rig r;
		r.open_menu(menu::nodes::recorder);
		r.tick(0);
		r.press(ButtonId::SelectEnc, 10);
		r.tick(10);
		r.release(ButtonId::SelectEnc, 20);
		for (uint32_t at = 20; at <= 50; at += 10) {
			r.tick(at);
		}
		expect(r.on_stack()).to_be_true();
		expect(menu_scene().state().path[0]).to_equal(&menu::nodes::recorder);
		expect(menu_scene().state().depth).to_equal(1);
		expect(menu_scene().state().path[1]).to_equal(&menu::nodes::sample0_recorder);
	});

	it("opens REC 2 from that page on oscillator 2's own range, with oscillator 1 multisampled and none picked", _ {
		// `osc::AudioRecorder::checkPermissionToBeginSession`
		// (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`) asks about its own `source_id_`: oscillator 2
		// gets its first range and, having one, needs no picker, whatever oscillator 1 holds.
		Guards g;
		Rig r;
		r.source->ranges.insertMultiRange(1);
		deluge::edits::recordRanges(*r.sound, 0);
		MenuContext c = r.ctx();
		c.horizontal_menus = true;
		c.range_index = -1;
		menu_scene().open(menu::nodes::recorder, c);
		r.shell.init(SceneId{deluge::ui::shell::SceneKind::FirstApp, menu::node_id(menu::nodes::recorder)});
		r.tick(0);
		// The third slot is REC 2's, each recorder being two slots wide; past the page buttons' 140 ms debounce.
		r.press(ButtonId::Midi, 300);
		r.tick(300);
		r.release(ButtonId::Midi, 310);
		r.tick(310);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(1);
		r.press(ButtonId::SelectEnc, 320);
		r.tick(320);
		r.release(ButtonId::SelectEnc, 330);
		for (uint32_t at = 330; at <= 360; at += 10) {
			r.tick(at);
		}
		expect(menu_scene().state().path[1]).to_equal(&menu::nodes::sample1_recorder);
		expect(r.on_stack()).to_be_true();
		expect(r.sound->sourceResources[1].ranges.size()).to_equal(size_t{1});
		expect(r.sound->sourceResources[0].ranges.size()).to_equal(size_t{2});
	});

	it("returns a take begun on the SAMPLE RECORDER page to its source page, the file selector focused", _ {
		// `osc::AudioRecorder::beginSession` re-roots onto `sourceMenuGroup`, focused on the file selector, before it
		// opens the recorder, so the take lands there.
		Guards g;
		Rig r;
		r.open_menu(menu::nodes::recorder);
		r.tick(0);
		r.press(ButtonId::SelectEnc, 10);
		r.tick(10);
		r.release(ButtonId::SelectEnc, 20);
		for (uint32_t at = 20; at <= 50; at += 10) {
			r.tick(at);
		}
		expect(r.on_stack()).to_be_true();
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		const Take take{*rec, "SAMPLES/RECORD/REC004.WAV"};
		rec->status = RecorderStatus::COMPLETE;
		for (uint32_t at = 60; at <= 90; at += 10) {
			r.tick(at);
		}
		expect(r.on_stack()).to_be_false();
		expect(menu_scene().state().path[0]).to_equal(&menu::nodes::source_group);
		expect(menu_scene().state().depth).to_equal(0);
		// The take made source 0 a sample source, whose page lists the type first and the file selector second.
		expect(static_cast<int32_t>(menu_scene().state().columns[0].focused)).to_equal(1);

		// The landing blinks the focused file selector's own pad, (0, 5). `main` goes on flashing the recorder's
		// (0, 4) there, which its blocking recorder loop re-plans after the take: accepted as `main`'s bug (D128).
		const size_t from = r.fake.flashes_sent.size();
		for (uint32_t at = 100; at <= 500; at += 10) {
			r.tick(at);
		}
		bool own_pad = false;
		bool recorder_pad = false;
		for (size_t i = from; i < r.fake.flashes_sent.size(); i++) {
			own_pad = own_pad || (r.fake.flashes_sent[i].x == 0 && r.fake.flashes_sent[i].y == 5);
			recorder_pad = recorder_pad || (r.fake.flashes_sent[i].x == 0 && r.fake.flashes_sent[i].y == 4);
		}
		expect(own_pad).to_be_true();
		expect(recorder_pad).to_be_false();
	});

	it("leaves the menu after a take opened on the item alone, as the drum creator's take leaves the clip view", _ {
		Guards g;
		Rig r;
		r.open_menu(menu::nodes::sample0_recorder);
		for (uint32_t at = 0; at <= 30; at += 10) {
			r.tick(at);
		}
		expect(r.on_stack()).to_be_true();
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		rec->status = RecorderStatus::COMPLETE;
		rec->hadCardError = true; // attach nothing; this example is about where the menu goes
		for (uint32_t at = 40; at <= 70; at += 10) {
			r.tick(at);
		}
		expect(r.on_stack()).to_be_false();
		expect(r.has(deluge::ui::shell::SceneKind::FirstApp)).to_be_false();
	});

	// --- the threshold take ---------------------------------------------------------------------------

	it("draws main's AudioRecorder::renderOLED threshold screen while waiting, its threshold line lit and dark", _ {
		Guards g;
		AudioClockAt clock{0};
		Rig r;
		r.song.song->thresholdRecordingMode = ThresholdRecordingMode::LOW;
		r.open();
		r.tick(0);
		expect(recorder_scene().threshold_take()).to_be_true();
		const std::string lit = r.oled();
		clock.set(400); // the line is dark from 290 ms to 570 ms of its 750 ms cycle
		r.tick(10);
		const std::string dark = r.oled();
		clock.set(0);
		expect(lit).to_equal(main_panel("threshold_lit"));
		expect(dark).to_equal(main_panel("threshold_dark"));
		expect(lit).not_().to_equal(dark);
	});

	it("starts RECORDING once the take passes its threshold margin, with the solid meter and the record dot", _ {
		Guards g;
		AudioClockAt clock{0};
		Rig r;
		r.song.song->thresholdRecordingMode = ThresholdRecordingMode::LOW;
		r.open();
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		expect(rec->minThresholdMargin).to_be_greater_than(0u);
		rec->numSamplesCaptured = rec->minThresholdMargin; // the pre-roll a threshold take holds before it starts
		r.tick(10);
		expect(recorder_scene().capturing()).to_be_false();
		expect(r.oled()).to_equal(main_panel("threshold_margin"));
		rec->numSamplesCaptured = rec->minThresholdMargin + 1;
		r.tick(20);
		expect(recorder_scene().capturing()).to_be_true();
		expect(r.oled()).to_equal(main_panel("threshold_recording"));
		rec->numSamplesCaptured = 0;
	});

	it("asks the render to measure the input's levels for a threshold take, and only for one", _ {
		// `opened()`'s `recorder->monitorInputLevels = display->haveOLED() && isThresholdRecording()`.
		Guards g;
		{
			Rig r;
			r.song.song->thresholdRecordingMode = ThresholdRecordingMode::LOW;
			r.open();
			r.tick(0);
			expect(deluge::gui::shell_host::input_take()->monitorInputLevels.load()).to_be_true();
		}
		Rig r;
		r.song.song->thresholdRecordingMode = ThresholdRecordingMode::OFF;
		r.open();
		r.tick(0);
		expect(recorder_scene().threshold_take()).to_be_false();
		// Armed only once the first take is freed: a take still live refuses the second.
		expect(deluge::gui::shell_host::input_take()).not_().to_be_null();
		expect(deluge::gui::shell_host::input_take()->monitorInputLevels.load()).to_be_false();
	});

	it("releases a next song held for the take as the take ends, as finishRecording does", _ {
		Guards g;
		SlowJob slow;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		expect(playbackHandler.slowJob).not_().to_equal(deluge::runtime::kNoJob);
		const deluge::runtime::JobStats before = deluge::runtime::appLoop.stats(playbackHandler.slowJob);
		playbackHandler.nextSongPending_ = true; // what a MIDI NEXT_SONG during the take leaves
		const Take take{*rec, "SAMPLES/RECORD/REC010.WAV"};
		rec->status = RecorderStatus::COMPLETE;

		r.tick(10);
		expect(r.on_stack()).to_be_false();
		SlowJob::drain();
		expect(deluge::runtime::appLoop.stats(playbackHandler.slowJob).runs).to_be_greater_than(before.runs);
	});

	it("stops a take that hit a card error and frees it only once it has settled", _ {
		Guards g;
		Rig r;
		r.open();
		r.tick(0);
		SampleRecorder* rec = deluge::gui::shell_host::input_take();
		rec->numSamplesCaptured = 44100;
		rec->hadCardError = true; // mid-capture: status still CAPTURING_DATA
		rec->status = RecorderStatus::CAPTURING_DATA;

		r.tick(10);
		expect(r.on_stack()).to_be_true(); // not freed under the capture
		expect(recorder_scene().stopping()).to_be_true();
	});
});

// clang-format on

namespace recorder_fakes {

using deluge::ui::scenes::bind::AuditionEnd;
using deluge::ui::scenes::bind::ForegroundRecording;
using deluge::ui::scenes::bind::InputTake;
using deluge::ui::scenes::bind::TakeChannels;
using deluge::ui::scenes::bind::test::FakeHost;
using deluge::ui::scenes::bind::test::RecordingSoundWrites;
using deluge::ui::scenes::bind::test::RecordingTakeActions;
using deluge::ui::shell::Action;
using deluge::ui::shell::Result;
using deluge::ui::shell::SurfaceRole;
using Kind = RecordingTakeActions::Kind;

/// The synth's durable output id; any id works, since the rig's song holds no other output.
constexpr uint32_t kSynthOutputId = 4;
/// The synth's slot: its output's own instrument.
constexpr deluge::project::SlotAddress kSynthSlot{deluge::project::SlotAddress::Kind::OUTPUT, kSynthOutputId};
/// A kit row's sound, and the row it plays on.
constexpr deluge::project::SlotAddress kDrumSlot{deluge::project::SlotAddress::Kind::DRUM, 9};
constexpr deluge::project::LaneAddress kDrumLane{.kit_id = 40, .drum_id = 9};
/// The take's file, as the host names it.
constexpr std::string_view kTakePath = "SAMPLES/RECORD/REC00001.WAV";

/// @brief Every notice the queue holds, drained.
std::vector<deluge::base::notify::Notice::Kind> drain_notices() {
	std::vector<deluge::base::notify::Notice::Kind> kinds;
	deluge::base::notify::Notice n;
	while (deluge::base::notify::notices().pop(n)) {
		kinds.push_back(n.kind);
	}
	return kinds;
}

/// @brief How many of @p r's actions are of @p kind.
size_t count_actions(const Result& r, Action::Kind kind) {
	size_t n = 0;
	for (const Action& a : r.actions) {
		n += a.kind == kind ? 1 : 0;
	}
	return n;
}

/// @brief A song record holding one synth, a fake host whose sound port answers the take's one range, and a
///        recorder over them.
struct FakesRig {
	FakeHost host;
	deluge::project::Song model;
	RecorderScene scene;

	FakesRig() {
		deluge::project::DocOutput& output = model.output_structure.getOrCreate(kSynthOutputId);
		output.instrument = deluge::project::InstrumentSlot{.plugin = "deluge.instrument"};
		host.sound.take = 0; // the range the take began on is still there
		(void)drain_notices();
	}
	FakesRig(const FakesRig&) = delete;
	FakesRig& operator=(const FakesRig&) = delete;

	/// @brief The synth's source 0, range 0, through this rig's fakes.
	[[nodiscard]] MenuContext ctx() {
		MenuContext c{};
		c.model = &model;
		c.ports = host.ports();
		c.addresses.slot = kSynthSlot;
		c.range_index = 0;
		c.source_index = 0;
		return c;
	}
	/// @brief The kit row's sound, source 0, range 0.
	[[nodiscard]] MenuContext kit_ctx() {
		MenuContext c = ctx();
		c.is_kit = true;
		c.addresses.slot = kDrumSlot;
		c.addresses.lane = kDrumLane;
		return c;
	}
	/// @brief Open and enter the recorder on @p c.
	bool enter(const MenuContext& c) {
		scene.open(c);
		return scene.on_enter(SceneId{kRecorderSceneKind, 0});
	}
	/// @brief The take the audio fake holds; the case has begun one.
	InputTake& take() { return *host.audio.take; }
};

} // namespace recorder_fakes

using TakeCall = recorder_fakes::Kind;
using ActionKind = deluge::ui::shell::Action::Kind;

// clang-format off
describe recorder_scene_fakes("scenes/recorder RecorderScene over the ports' fakes", ${
	it("refuses while the foreground recording names an input or the device's output, and opens over none", _ {
		recorder_fakes::FakesRig r;
		MenuContext c = r.ctx();
		r.host.audio.foreground = bind::ForegroundRecording::EXTERNAL;
		expect(RecorderScene::can(c)).to_be_false();
		r.host.audio.foreground = bind::ForegroundRecording::INTERNAL;
		expect(RecorderScene::can(c)).to_be_false();
		r.host.audio.foreground = bind::ForegroundRecording::UNSET;
		expect(RecorderScene::can(c)).to_be_true();
		r.host.audio.foreground = bind::ForegroundRecording::NONE;
		expect(RecorderScene::can(c)).to_be_true();
	});

	it("wipes the undo log, names the drum, clears the tick squares, begins and ends the audition, in that order", _ {
		recorder_fakes::FakesRig r;
		expect(r.enter(r.kit_ctx())).to_be_true();
		expect(r.host.order).to_equal(std::vector<std::string_view>{"wipe", "name", "ticks", "begin", "audition"});
		expect(r.host.view_actions.ends).to_equal(std::vector<bind::AuditionEnd>{bind::AuditionEnd::ClipViewWhenAuditioning});
		expect(r.scene.recording()).to_be_true();
		expect(r.scene.last_enter_refused()).to_be_false();
		r.scene.on_exit();
	});

	it("refuses before the wipe while a load may replace what the take attaches to", _ {
		recorder_fakes::FakesRig r;
		r.host.card.replacing = true;
		expect(r.enter(r.kit_ctx())).to_be_false();
		expect(r.host.order).to_equal(std::vector<std::string_view>{});
		expect(r.scene.recording()).to_be_false();
	});

	it("still ends the audition when the take cannot begin, and refuses the entry", _ {
		recorder_fakes::FakesRig r;
		r.host.take_actions.begins = false;
		expect(r.enter(r.ctx())).to_be_false();
		expect(r.host.view_actions.ends).to_equal(std::vector<bind::AuditionEnd>{bind::AuditionEnd::ClipViewWhenAuditioning});
		expect(r.scene.recording()).to_be_false();
		expect(r.scene.last_enter_refused()).to_be_true();
		expect(r.scene.next_timer().has_value()).to_be_false();
		const std::vector<deluge::base::notify::Notice::Kind> notices = recorder_fakes::drain_notices();
		expect(notices).to_equal(std::vector<deluge::base::notify::Notice::Kind>{deluge::base::notify::Notice::Kind::Error});
	});

	it("begins in stereo when an input is plugged in, and in mono otherwise", _ {
		recorder_fakes::FakesRig r;
		r.host.audio.inputs = true;
		expect(r.enter(r.ctx())).to_be_true();
		expect(r.host.take_actions.channels).to_equal(bind::TakeChannels::STEREO);
		r.scene.on_exit();
		r.host.audio.inputs = false;
		expect(r.enter(r.ctx())).to_be_true();
		expect(r.host.take_actions.channels).to_equal(bind::TakeChannels::MONO);
		r.scene.on_exit();
	});

	it("attaches a settled take's file to its range, then finishes it, and closes", _ {
		recorder_fakes::FakesRig r;
		expect(r.enter(r.ctx())).to_be_true();
		r.host.audio.take_path = std::string{recorder_fakes::kTakePath};
		r.take().settled = true;
		const deluge::ui::shell::Result result = r.scene.after_render();
		expect(r.host.sound_writes.calls.size()).to_equal(size_t{1});
		if (r.host.sound_writes.calls.size() == 1) {
			const bind::test::RecordingSoundWrites::Call& attach = r.host.sound_writes.calls[0];
			expect(attach.kind).to_equal(bind::test::RecordingSoundWrites::Call::Kind::ATTACH);
			expect(attach.slot).to_equal(recorder_fakes::kSynthSlot);
			expect(attach.range).to_equal(std::optional<uint16_t>{0});
			expect(attach.text).to_equal(std::string{recorder_fakes::kTakePath});
		}
		expect(r.host.take_actions.calls).to_equal(std::vector<TakeCall>{TakeCall::BEGIN, TakeCall::FINISH});
		expect(recorder_fakes::count_actions(result, ActionKind::Remove)).to_equal(size_t{1});
		expect(r.scene.recording()).to_be_false();
	});

	it("attaches nothing from an aborted take and still finishes it", _ {
		recorder_fakes::FakesRig r;
		expect(r.enter(r.ctx())).to_be_true();
		r.host.audio.take_path = std::string{recorder_fakes::kTakePath};
		r.take().settled = true;
		r.take().aborted = true;
		const deluge::ui::shell::Result result = r.scene.after_render();
		expect(r.host.sound_writes.calls.empty()).to_be_true();
		expect(r.host.take_actions.calls).to_equal(std::vector<TakeCall>{TakeCall::BEGIN, TakeCall::FINISH});
		expect(recorder_fakes::count_actions(result, ActionKind::Remove)).to_equal(size_t{1});
	});

	it("ends a take the card failed under, attaches nothing from it and still finishes it", _ {
		recorder_fakes::FakesRig r;
		expect(r.enter(r.ctx())).to_be_true();
		r.host.audio.take_path = std::string{recorder_fakes::kTakePath};
		r.take().capturing_data = true;
		r.take().card_error = true;
		r.take().settled = true;
		const deluge::ui::shell::Result result = r.scene.after_render();
		expect(r.host.sound_writes.calls.empty()).to_be_true();
		expect(r.host.take_actions.calls)
		    .to_equal(std::vector<TakeCall>{TakeCall::BEGIN, TakeCall::END_SOON, TakeCall::FINISH});
		expect(recorder_fakes::count_actions(result, ActionKind::Remove)).to_equal(size_t{1});
	});

	it("shows one clipping popup for a clip, and none on the next frame", _ {
		recorder_fakes::FakesRig r;
		expect(r.enter(r.ctx())).to_be_true();
		r.host.take_actions.clipped = true;
		const deluge::ui::shell::Result first = r.scene.after_render();
		expect(recorder_fakes::count_actions(first, ActionKind::Popup)).to_equal(size_t{1});
		if (!first.actions.empty()) {
			expect(first.actions[0].popup_string)
			    .to_equal(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_CLIPPING_OCCURRED));
		}
		const deluge::ui::shell::Result second = r.scene.after_render();
		expect(recorder_fakes::count_actions(second, ActionKind::Popup)).to_equal(size_t{0});
		r.scene.on_exit();
	});

	it("orphans a running take on exit and on being taken down, and only exit clears the Working it showed", _ {
		using Notice = deluge::base::notify::Notice;
		recorder_fakes::FakesRig r;
		expect(r.enter(r.ctx())).to_be_true();
		r.take().capturing_data = true;
		r.take().card_error = true; // asks for the end, so the scene shows Working
		(void)r.scene.after_render();
		expect(r.scene.stopping()).to_be_true();
		(void)recorder_fakes::drain_notices();
		r.scene.on_exit();
		expect(r.host.take_actions.calls)
		    .to_equal(std::vector<TakeCall>{TakeCall::BEGIN, TakeCall::END_SOON, TakeCall::TAKE_CLIPPED, TakeCall::ORPHAN});
		expect(recorder_fakes::drain_notices()).to_equal(std::vector<Notice::Kind>{Notice::Kind::ClearWorking});
		expect(r.scene.recording()).to_be_false();

		r.host.take_actions.calls.clear();
		expect(r.enter(r.ctx())).to_be_true();
		r.take().capturing_data = true;
		r.take().card_error = true;
		(void)r.scene.after_render();
		expect(r.scene.stopping()).to_be_true();
		(void)recorder_fakes::drain_notices();
		r.scene.taken_down();
		expect(r.host.take_actions.calls)
		    .to_equal(std::vector<TakeCall>{TakeCall::BEGIN, TakeCall::END_SOON, TakeCall::TAKE_CLIPPED, TakeCall::ORPHAN});
		expect(recorder_fakes::drain_notices()).to_equal(std::vector<Notice::Kind>{});
		expect(r.scene.recording()).to_be_false();
	});

	it("blinks the record dot by the audio clock while recording", _ {
		recorder_fakes::FakesRig r;
		expect(r.enter(r.ctx())).to_be_true();
		r.take().threshold = true;
		r.take().threshold_level = 0.5F;
		r.take().capturing = true;
		const auto frame_at = [&](uint32_t frames) {
			r.host.audio.sample_timer = frames;
			deluge::ui::test::Rig48 oled;
			r.scene.render_oled(deluge::ui::shell::SurfaceRole::Main, oled.frame);
			return oled.pbm();
		};
		const std::string lit = frame_at(0);
		const std::string unlit = frame_at(static_cast<uint32_t>(kSampleRate * 480 / 1000));
		const std::string lit_again = frame_at(static_cast<uint32_t>(kSampleRate * 960 / 1000));
		expect(unlit).not_().to_equal(lit);
		expect(lit_again).to_equal(lit);
		r.scene.on_exit();
	});

	it("presents the status word, or the threshold meter's state", _ {
		recorder_fakes::FakesRig r;
		expect(r.enter(r.ctx())).to_be_true();
		const deluge::ui::views::oled::RecorderStatus waiting = r.scene.present_status(128);
		expect(static_cast<int>(waiting.mode)).to_equal(static_cast<int>(deluge::ui::views::oled::RecorderStatus::Mode::Word));
		expect(waiting.word).to_equal(std::string_view{"Waiting"});
		r.take().capturing = true;
		expect(r.scene.present_status(128).word).to_equal(std::string_view{"Recording"});
		r.take().threshold = true;
		r.take().threshold_level = 0.5F;
		r.host.audio.sample_timer = 0;
		const deluge::ui::views::oled::RecorderStatus meter = r.scene.present_status(128);
		expect(static_cast<int>(meter.mode)).to_equal(static_cast<int>(deluge::ui::views::oled::RecorderStatus::Mode::Meter));
		expect(meter.meter.recording).to_be_true();
		expect(meter.meter.blink_on).to_be_true();
		expect(meter.meter.threshold_x).to_equal(deluge::ui::widgets::oled::threshold_meter_x(0.5F, 128));
		r.scene.on_exit();
	});

	it("presents the shortcut blink: its pad, its step, and whether the step has played", _ {
		recorder_fakes::FakesRig r;
		expect(r.enter(r.ctx())).to_be_true();
		const deluge::ui::views::grid::BlinkPads first = r.scene.present_blink(0);
		expect(first.step).to_equal(0u);
		expect(first.fire).to_be_true();
		expect(first.plan).to_equal(deluge::ui::widgets::grid::exclusive_blink(0, recorder::kRecordShortcutY, 0));
		r.scene.on_exit();
	});

	it("refuses with no ports at all, asking nothing of the fakes", _ {
		recorder_fakes::FakesRig r;
		MenuContext c = r.ctx();
		c.ports = {};
		expect(r.enter(c)).to_be_false();
		expect(r.scene.recording()).to_be_false();
		expect(recorder_fakes::drain_notices()).to_equal(std::vector<deluge::base::notify::Notice::Kind>{});
	});

	it("with no take port, wipes, names, clears and ends the audition, then refuses with no error", _ {
		recorder_fakes::FakesRig r;
		MenuContext c = r.kit_ctx();
		c.ports.take_actions = nullptr;
		expect(r.enter(c)).to_be_false();
		expect(r.host.order).to_equal(std::vector<std::string_view>{"wipe", "name", "ticks", "audition"});
		expect(r.scene.recording()).to_be_false();
		expect(r.scene.next_timer().has_value()).to_be_false();
		expect(recorder_fakes::drain_notices()).to_equal(std::vector<deluge::base::notify::Notice::Kind>{});
	});

	it("with no audio port to read a take back, begins none, and refuses with no error", _ {
		recorder_fakes::FakesRig r;
		MenuContext c = r.ctx();
		c.ports.audio = nullptr;
		expect(r.enter(c)).to_be_false();
		expect(r.host.take_actions.calls).to_equal(std::vector<TakeCall>{});
		expect(r.scene.recording()).to_be_false();
		expect(r.scene.next_timer().has_value()).to_be_false();
		expect(recorder_fakes::drain_notices()).to_equal(std::vector<deluge::base::notify::Notice::Kind>{});
	});
});

describe recorder_scene_reference_panels("recorder_scene: the reference panels", $ {
	it("reads every panel frozen beside the tables", _ {
		expect(spec::OracleFiles::unread("recorder_")).to_equal(std::string{});
	});
});
// clang-format on
DELUGE_SPEC(recorder_scene_spec, recorder_scene_spec_body, recorder_scene_fakes, recorder_scene_reference_panels)
