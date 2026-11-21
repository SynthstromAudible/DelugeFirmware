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

/// @file
/// @brief Audio-input recording takes, each started from the sound editor's recorder menu item. A take runs while the
/// rest of the app keeps going: DIN MIDI notes and clock delivered during it are handled during it (RT-TAKE), and BACK
/// attaches its sample to the source it was recorded for (RT-ATTACH). No take starts while a load that can replace the
/// clip's output is in flight (RT-REFUSED); a take whose recorder UI is closed under it (RT-CLOSED), or whose synth is
/// replaced under it (RT-REPLACED), is dropped and attached to nothing, and the next take starts (RT-SECOND).
///
/// Every take starts from the menu's recorder item, opened on the clip as the user opens it, and runs on the ported
/// recorder screen. The open ticks the guest until the screen is pushed, so each leg checks its take on the step that
/// started it; after that the guest runs on its own wake, the UI timer the app loop fires.

#include "scenarios/follow_channel.h"
#include "scenarios/guest_drive.h"
#include "scenarios/slot_coherence.h"
#include "scenarios/support/scenario_declare.h"
#include "scenarios/support/scenario_report.h"
#include "support/sound_fixtures.h"

#include "definitions_cxx.hpp"
#include "gui/ui/browser/browser.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/shell_host/shell_host.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/button.h"
#include "hid/display/display.h"
#include "io/file.hpp"
#include "io/midi/midi_engine.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/instrument/instrument.h"
#include "model/output.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_instrument.h"
#include "processing/sound/source_resources.h"
#include "scenes/recorder/recorder_scene.h"
#include "shell/shell.h"
#include "storage/audio/audio_file.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/multi_range/multi_range.h"
#include "storage/preset_acquisition.h"
#include "util/menu_entry.h"
#include "util/misc.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

// See staging_release_scenario.cpp: the app's printf alias has no provider in this harness.
#ifdef printf
#undef printf
#endif

DELUGE_SCENARIO_DECLARE(recorder_take);

// The harness's test input signal and DIN note script (audio/host.rs).
extern "C" {
int32_t deluge_host_midi_script_start(const char* path);
uint32_t deluge_host_midi_script_released(void);
void deluge_host_test_input(uint32_t signal);
}

namespace {

using deluge::scenarios::ScenarioReport;
using deluge::scenarios::Wait;
using deluge::scenarios::WaitState;

/// The legs, in the order they run. Each starts its take from the recorder menu item, the user's gesture.
enum class Stage : uint8_t {
	Loading,
	MidiOpen,       ///< midi_during_take: open the take and arm the DIN script.
	MidiTake,       ///< ...MIDI must be handled while the take runs.
	MidiEnding,     ///< ...BACK ends it; the sample is attached as before.
	RefuseStart,    ///< refused_during_load: a new-synth acquisition is in flight; the recorder must refuse a take.
	RefuseSettle,   ///< ...the acquisition finishes.
	ClosedRun,      ///< closed_mid_take: the UI stack is replaced under a running take, as a cleared song does.
	ClosedWait,     ///< ...the take is dropped: nothing attached, the recorder free.
	SecondRun,      ///< ...a second take starts and runs.
	SecondEnding,   ///< ...and attaches as usual.
	NulledRun,      ///< nullified_mid_take: the UI stack is nullified under a running take, with nothing put back.
	NulledWait,     ///< ...the take is dropped all the same.
	ReplacedRun,    ///< output_replaced_mid_take: the clip's synth is replaced under a running take.
	ReplacedEnding, ///< ...BACK ends the take, which must attach to nothing.
	NextListing,    ///< next_song_mid_take: the load browser lists SONGS, as a song loaded from it leaves it.
	NextRun,        ///< ...with the song playing, MIDI NEXT_SONG arrives while a take runs.
	NextEnding,     ///< ...BACK ends the take, which must attach to its sound in the song it was recorded in.
	NextLoad,       ///< ...then the next song loads.
	Report,
	Done,
};

/// Steps the blank song's preset load is given. A step is about two virtual milliseconds.
constexpr int32_t kMaxLoadSteps = 20000;
/// Steps the take is given to read every scripted byte: the script spans about a second and a half of audio.
constexpr int32_t kMaxTakeSteps = 5000;
/// Steps a take, or a load, is given to settle.
constexpr int32_t kMaxSettleSteps = 5000;
/// Steps a take runs before something is done to it: long enough for its file to exist. Not a wait: it is the
/// stimulus, and nothing signals that the file exists.
constexpr int32_t kRunSteps = 100;

/// MIDI clock at 120 BPM: 24 pulses a quarter note, so one every 919 frames at 44.1 kHz.
constexpr uint64_t kClockFrames = 919;
constexpr int32_t kClockPulses = 48;
constexpr uint64_t kNoteOnFrame = 4410;
constexpr uint64_t kNoteOffFrame = 30000;
constexpr uint64_t kStopFrame = 50000;

/// The song next_song_mid_take's NEXT_SONG loads, and the note its learned NEXT_SONG command listens for.
constexpr const char* kNextSongPath = "SONGS/RTNEXT.XML";
constexpr const char* kNextSongText =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<song\n\tfirmwareVersion=\"c1.3.0\"\n"
    "\tearliestCompatibleFirmware=\"4.1.0-alpha\"\n>\n</song>\n";
constexpr uint8_t kNextSongNote = 0x30;
/// Steps the take runs on after its NEXT_SONG was read: well past a load's STAGE and BEGIN, which is where a load
/// that started at once would close the recorder. Not a wait: the property is that the recorder is still open at the
/// end of this window.
constexpr int32_t kNextRunSteps = 600;

Stage g_stage = Stage::Loading;
bool g_stepDone = false;
ScenarioReport g_report{"RT"};
/// The wait of the stage running; each stage that waits starts its own.
Wait g_wait{g_report, "OPEN the blank song's preset load", kMaxLoadSteps};
/// Steps a take has run since the stage began, against kRunSteps and kNextRunSteps.
int32_t g_steps = 0;

Clip* g_clip = nullptr;
Sound* g_sound = nullptr;
Output* g_outputBefore = nullptr;
MultiRange* g_range = nullptr;
std::string g_recordingPath;
std::string g_scriptPath;
int32_t g_events = 0;
int32_t g_voicesMax = 0;
bool g_externalClockSeen = false;
bool g_recordingShown = false;
bool g_clipSeen = false;
bool g_clipConsumed = false;
/// next_song_mid_take: the song the take was recorded in, and whether the take outlived its NEXT_SONG.
Song* g_songBefore = nullptr;
bool g_nextRead = false;
bool g_loadSeen = false;

/// The failure tag of the leg named by @p marker: the marker without its "RT-"; the first leg's bare "RT" is its OPEN
/// line's.
const char* stageOf(const char* marker) {
	return std::string_view{marker} == "RT" ? "OPEN" : marker + std::string_view{"RT-"}.size();
}

/// Starts a stage's wait, the take's settling or the song load's, with its own budget.
void startWait(const char* what, int32_t budget) {
	g_wait = Wait{g_report, what, budget};
}

/// Polls the stage's wait with whether its condition holds now; true once it does. A wait that runs out ends the run at
/// the report, having recorded its failure.
bool settled(bool met) {
	const WaitState state = g_wait.poll(met);
	if (state == WaitState::Expired) {
		g_stage = Stage::Report;
	}
	return state == WaitState::Met;
}

using deluge::harness::guestTick;
using deluge::harness::guestTop;
using deluge::scenes::recorder::kRecorderSceneKind;
using deluge::scenes::recorder::recorder_scene;

bool inputTake() {
	const AudioInputChannel source = AudioEngine::foregroundRecordingSource();
	return source == AudioInputChannel::LEFT || source == AudioInputChannel::STEREO;
}

bool takeOver() {
	return !recorder_scene().recording() && AudioEngine::foregroundRecordingSource() == AudioInputChannel::NONE;
}

/// The recording's file path, once the recorder has made the file.
void notePath() {
	SampleRecorder* recorder = deluge::gui::shell_host::input_take();
	if (recorder != nullptr && recorder->sample != nullptr && !recorder->sample->filePath.empty()) {
		g_recordingPath = recorder->sample->filePath;
	}
}

/// How many holders keep the recording at @p path loaded, or -1 when no such file is resident.
int64_t leasesOn(const std::string& path) {
	for (AudioFile* file : audioFileManager.audioFiles) {
		if (file != nullptr && file->filePath == path) {
			return file->leaseCount();
		}
	}
	return -1;
}

/// The clip view in front of the song's first clip, with the clip's synth set back to an oscillator.
bool frontClip(const char* marker) {
	g_clip = currentSong->live.clips.session_clips.empty() ? nullptr : currentSong->live.clips.session_clips[0];
	if (g_clip == nullptr || g_clip->output == nullptr || g_clip->output->type != OutputType::SYNTH) {
		g_report.fail("%s the song has no synth clip to record into", stageOf(marker));
		return false;
	}
	currentSong->setCurrentClip(g_clip);
	changeRootUI(&instrumentClipView);
	return true;
}

/// The user's gesture: the menu opened on the first source's recorder item, which pushes the recorder screen on the
/// guest's next tick.
void openRecorder() {
	auto* sound = static_cast<Sound*>(static_cast<SoundInstrument*>(g_clip->output));
	g_sound = sound;
	g_range = nullptr;
	if (!deluge::gui::ui::openMenu(g_clip, /*sourceIndex=*/0, /*performanceView=*/false,
	                               /*sessionOrArrangerRoot=*/false, /*sessionViewRoot=*/false,
	                               /*kitGlobalFx=*/false, deluge::gui::ui::MenuNode::Osc0Recorder)) {
		return;
	}
	for (int32_t t = 0; t < 8 && !guestTop(kRecorderSceneKind) && isUIOpen(&shellHostUI); t++) {
		guestTick(0); // the menu's push of the recorder screen runs from the guest's tick
	}
	// Read once the menu is open: entering it gives a source with no range its first one.
	g_range = !sound->sourceResources[0].ranges.empty() ? sound->sourceResources[0].ranges.getElement(0) : nullptr;
}

/// Starts a take for @p marker's leg; false (with the failure reported) when none is running under the recorder screen.
bool startTake(const char* marker) {
	if (!frontClip(marker)) {
		return false;
	}
	deluge::test::setOscType(*static_cast<SoundInstrument*>(g_clip->output), 0, OscType::SAW);
	g_recordingPath.clear();
	openRecorder();
	const bool onTop = guestTop(kRecorderSceneKind);
	printf("%s-OPEN recorderOnTop=%d source=%d\n", marker, onTop ? 1 : 0,
	       static_cast<int>(AudioEngine::foregroundRecordingSource()));
	if (!onTop || !inputTake() || g_sound == nullptr || g_range == nullptr) {
		g_report.fail("%s the recorder item did not leave an input take running under the recorder screen",
		              stageOf(marker));
		return false;
	}
	g_steps = 0;
	return true;
}

void pressBack() {
	(void)getCurrentUI()->buttonAction(deluge::hid::button::BACK, true);
	(void)getCurrentUI()->buttonAction(deluge::hid::button::BACK, false);
}

/// Waits for the take to be over; false while it runs, and after a take that never settles ends the run.
bool waitOver() {
	return settled(takeOver());
}

/// Starts the wait for a take to settle.
void startSettleWait(const char* what) {
	startWait(what, kMaxSettleSteps);
}

/// Writes the take's DIN script: a MIDI Start, two bars of clock, one note on channel 1 (which MIDI follow sends to
/// the instrument in front of the user), and a Stop. Returns the script's path, or an empty string.
std::string writeScript() {
	std::vector<std::pair<uint64_t, std::string>> events;
	events.emplace_back(0, "FA");
	for (int32_t pulse = 0; pulse < kClockPulses; ++pulse) {
		events.emplace_back(static_cast<uint64_t>(pulse) * kClockFrames, "F8");
	}
	events.emplace_back(kNoteOnFrame, "90 3C 64");
	events.emplace_back(kNoteOffFrame, "80 3C 00");
	events.emplace_back(kStopFrame, "FC");
	std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

	std::error_code error;
	const std::filesystem::path path =
	    std::filesystem::temp_directory_path(error) / ("deluge-recorder-take-" + std::to_string(::getpid()) + ".notes");
	if (error) {
		return {};
	}
	FILE* file = std::fopen(path.c_str(), "w");
	if (file == nullptr) {
		return {};
	}
	for (const auto& [frame, bytes] : events) {
		std::fprintf(file, "%llu %s\n", static_cast<unsigned long long>(frame), bytes.c_str());
	}
	std::fclose(file);
	return path.string();
}

/// Writes next_song_mid_take's DIN script: one press of the note the learned NEXT_SONG command listens for.
std::string writeNextSongScript() {
	std::error_code error;
	const std::filesystem::path path = std::filesystem::temp_directory_path(error)
	                                   / ("deluge-recorder-take-next-" + std::to_string(::getpid()) + ".notes");
	if (error) {
		return {};
	}
	FILE* file = std::fopen(path.c_str(), "w");
	if (file == nullptr) {
		return {};
	}
	std::fprintf(file, "0 90 %02X 64\n2000 80 %02X 00\n", kNextSongNote, kNextSongNote);
	std::fclose(file);
	return path.string();
}

/// The blank song's preset load is over once nothing it asked for is still in flight.
void stepLoading() {
	if (!settled(!currentSong->blankPresetPending && !deluge::gui::instrumentLoad.acquisitionInFlight())) {
		return;
	}
	deluge_host_test_input(1);
	g_stage = Stage::MidiOpen;
}

void stepMidiOpen() {
	if (!startTake("RT")) {
		g_stage = Stage::Report;
		return;
	}
	g_scriptPath = writeScript();
	::scenarios::followChannelOne();
	g_events = g_scriptPath.empty() ? -1 : deluge_host_midi_script_start(g_scriptPath.c_str());
	if (g_events <= 0) {
		g_report.fail("TAKE the DIN script could not be written or armed");
		pressBack();
		startSettleWait("ATTACH the take settling");
		g_stage = Stage::MidiEnding;
		return;
	}
	startWait("TAKE the app reading every scripted byte", kMaxTakeSteps);
	g_stage = Stage::MidiTake;
}

/// While the take runs, MIDI must be handled: every scripted byte read, the note sounding, the clock followed.
void stepMidiTake() {
	if (!guestTop(kRecorderSceneKind) || !inputTake()) {
		g_report.fail("TAKE the take ended before BACK was pressed");
		startSettleWait("ATTACH the take settling");
		g_stage = Stage::MidiEnding;
		return;
	}
	g_voicesMax = std::max(g_voicesMax, static_cast<int32_t>(AudioEngine::getNumVoices()));
	g_externalClockSeen = g_externalClockSeen || playbackHandler.isExternalClockActive();
	g_recordingShown = g_recordingShown || recorder_scene().capturing();
	// The render raises the clipping flag and only the recorder's own step lowers it, so a flag seen raised and then
	// lowered shows that step ran mid-take.
	if (deluge::gui::shell_host::input_take() != nullptr) {
		const bool clipped = deluge::gui::shell_host::input_take()->recordingClippedRecently;
		g_clipConsumed = g_clipConsumed || (g_clipSeen && !clipped);
		g_clipSeen = g_clipSeen || clipped;
	}

	const uint32_t released = deluge_host_midi_script_released();
	const bool allRead = released == static_cast<uint32_t>(g_events);
	if (g_wait.poll(allRead) == WaitState::Pending) {
		return;
	}
	printf("RT-TAKE events=%d released=%u voicesMax=%d externalClock=%d recordingShown=%d clippingConsumed=%d "
	       "steps=%d\n",
	       g_events, released, g_voicesMax, g_externalClockSeen ? 1 : 0, g_recordingShown ? 1 : 0,
	       g_clipConsumed ? 1 : 0, g_wait.ticks());
	if (g_voicesMax == 0) {
		g_report.fail("TAKE a note delivered during the take did not sound");
	}
	if (!g_externalClockSeen) {
		g_report.fail(
		    "TAKE MIDI Start and clock delivered during the take did not start playback on the external clock");
	}
	// The test input is a full-scale sawtooth, so the take clips every block, and only the recorder screen's own
	// step lowers the clipping flag.
	if (!g_clipConsumed) {
		g_report.fail("TAKE the recorder screen's own step did not handle clipping during the take");
	}
	pressBack();
	startSettleWait("ATTACH the take settling");
	g_stage = Stage::MidiEnding;
}

/// BACK ends the take; once it has settled the recorder attaches the sample to the source and closes.
void stepMidiEnding() {
	if (!waitOver()) {
		return;
	}
	const SourceResources& source = g_sound->sourceResources[0];
	AudioFileHolder* holder = g_range->getAudioFileHolder();
	const auto* sample = holder != nullptr ? static_cast<const Sample*>(holder->audioFile) : nullptr;
	const bool isSample = g_sound->oscType(0) == OscType::SAMPLE;
	const bool pathsAgree = sample != nullptr && holder->filePath == sample->filePath;
	const uint64_t frames = sample != nullptr ? sample->lengthInSamples : 0;
	printf("RT-ATTACH osc=%d attached=%d pathsAgree=%d frames=%llu path=%s editorOnTop=%d steps=%d\n",
	       static_cast<int>(g_sound->oscType(0)), sample != nullptr ? 1 : 0, pathsAgree ? 1 : 0,
	       static_cast<unsigned long long>(frames), sample != nullptr ? sample->filePath.c_str() : "(none)",
	       isUIOpen(&shellHostUI) ? 1 : 0, g_wait.ticks());
	if (!isSample || sample == nullptr || !pathsAgree) {
		g_report.fail("ATTACH the take's sample was not attached to the source it was recorded for");
	}
	else if (frames == 0 || sample->filePath.rfind("SAMPLES/RECORD/", 0) != 0) {
		g_report.fail("ATTACH the attached sample is empty or not a recording");
	}
	// The take is the source's sample now, in its record and slot as well as at runtime.
	g_report.count(deluge::harness::expectSongCoherent(*currentSong, "take"));
	if (playbackHandler.isEitherClockActive()) {
		playbackHandler.endPlayback();
	}
	g_stage = Stage::RefuseStart;
}

/// A new-synth gesture on the clip view (an Acquire) swaps the clip's output when it lands, so the recorder must not
/// start a take while it is in flight.
void stepRefuseStart() {
	if (!frontClip("RT-REFUSED")) {
		g_stage = Stage::Report;
		return;
	}
	(void)instrumentClipView.createNewInstrument(OutputType::SYNTH, false);
	const bool loading = deluge::gui::instrumentLoad.inFlight();
	// An undo-log entry the refusal must leave alone: the recorder's other opens wipe the log.
	if (actionLogger.firstAction[BEFORE] == nullptr) {
		(void)actionLogger.getNewAction(ActionType::TEMPO_CHANGE);
	}
	Action* const loggedBefore = actionLogger.firstAction[BEFORE];
	openRecorder();
	const bool onTop = guestTop(kRecorderSceneKind);
	const bool logKept = loggedBefore != nullptr && actionLogger.firstAction[BEFORE] == loggedBefore;
	printf("RT-REFUSED loadInFlight=%d recorderOnTop=%d source=%d undoLogKept=%d\n", loading ? 1 : 0, onTop ? 1 : 0,
	       static_cast<int>(AudioEngine::foregroundRecordingSource()), logKept ? 1 : 0);
	if (loggedBefore == nullptr) {
		g_report.fail("REFUSED no undo-log entry could be made before the recorder was opened, so nothing was tested");
	}
	else if (!logKept) {
		g_report.fail("REFUSED a refused take wiped the undo log");
	}
	if (!loading) {
		g_report.fail("REFUSED the acquisition was not in flight when the recorder was opened, so nothing was tested");
	}
	if (onTop || AudioEngine::foregroundRecordingSource() != AudioInputChannel::NONE) {
		g_report.fail(
		    "REFUSED the recorder started a take while a preset load that can replace its sound was in flight");
		if (onTop) {
			pressBack();
		}
	}
	if (isUIOpen(&shellHostUI)) {
		(void)shellHostUI.exitUI();
	}
	startSettleWait("REFUSED the acquisition and the refused take settling");
	g_stage = Stage::RefuseSettle;
}

void stepRefuseSettle() {
	if (!settled(!deluge::gui::instrumentLoad.inFlight() && takeOver())) {
		return;
	}
	g_stage = startTake("RT-CLOSED") ? Stage::ClosedRun : Stage::Report;
}

/// A cleared song replaces the whole UI stack, the recorder's included, under a running take.
void stepClosedRun() {
	notePath();
	if (++g_steps < kRunSteps) {
		return;
	}
	nullifyUIs();
	changeRootUI(&instrumentClipView);
	startSettleWait("CLOSED the take settling");
	g_stage = Stage::ClosedWait;
}

void stepClosedWait() {
	if (!waitOver()) {
		return;
	}
	const SourceResources& source = g_sound->sourceResources[0];
	const int64_t leases = leasesOn(g_recordingPath);
	printf("RT-CLOSED source=%d osc=%d path=%s leases=%lld steps=%d\n",
	       static_cast<int>(AudioEngine::foregroundRecordingSource()), static_cast<int>(g_sound->oscType(0)),
	       g_recordingPath.c_str(), static_cast<long long>(leases), g_wait.ticks());
	if (g_sound->oscType(0) == OscType::SAMPLE) {
		g_report.fail("CLOSED a take whose recorder screen was closed under it switched the source to SAMPLE");
	}
	if (g_recordingPath.empty() || leases > 0) {
		g_report.fail("CLOSED a take whose recorder screen was closed under it was attached to something");
	}
	g_stage = startTake("RT-SECOND") ? Stage::SecondRun : Stage::Report;
}

void stepSecondRun() {
	if (++g_steps < kRunSteps) {
		return;
	}
	pressBack();
	startSettleWait("SECOND the take settling");
	g_stage = Stage::SecondEnding;
}

void stepSecondEnding() {
	if (!waitOver()) {
		return;
	}
	const bool attached = g_sound->oscType(0) == OscType::SAMPLE && g_range->getAudioFileHolder() != nullptr
	                      && g_range->getAudioFileHolder()->audioFile != nullptr;
	printf("RT-SECOND attached=%d\n", attached ? 1 : 0);
	if (!attached) {
		g_report.fail("SECOND the take after a dropped one did not attach its sample");
	}
	g_stage = startTake("RT-NULLED") ? Stage::NulledRun : Stage::Report;
}

/// The UI stack nullified and nothing opened in its place, as a song load leaves it until the new song's UI is up: the
/// take must not keep running as though the recorder were still on top.
void stepNulledRun() {
	notePath();
	if (++g_steps < kRunSteps) {
		return;
	}
	nullifyUIs();
	startSettleWait("NULLED the take settling");
	g_stage = Stage::NulledWait;
}

void stepNulledWait() {
	if (!waitOver()) {
		return;
	}
	const SourceResources& source = g_sound->sourceResources[0];
	const int64_t leases = leasesOn(g_recordingPath);
	printf("RT-NULLED source=%d osc=%d path=%s leases=%lld steps=%d\n",
	       static_cast<int>(AudioEngine::foregroundRecordingSource()), static_cast<int>(g_sound->oscType(0)),
	       g_recordingPath.c_str(), static_cast<long long>(leases), g_wait.ticks());
	if (g_recordingPath.empty() || leases > 0) {
		g_report.fail("NULLED a take whose UI stack was nullified under it was attached to something");
	}
	changeRootUI(&instrumentClipView);
	g_stage = startTake("RT-REPLACED") ? Stage::ReplacedRun : Stage::Report;
}

/// The synth being recorded for is replaced on its clip mid-take, by the new-synth gesture's own step
/// (InstrumentClipMinder::createNewInstrument(), which an acquisition reaches only on the clip view). No gesture
/// reaches it while the recorder is on top: this test seam stands for any swap the refusal cannot cover once a take
/// runs. The old synth is marked edited so it hibernates rather than being freed: an attach to it is then a wrong
/// attach that the leg can see, not a use after free.
void stepReplacedRun() {
	notePath();
	if (++g_steps < kRunSteps) {
		return;
	}
	g_outputBefore = g_clip->output;
	static_cast<Instrument*>(g_outputBefore)->editedByUser = true;
	deluge::storage::AcquireResult acquired;
	acquired.name = "SYNT999";
	(void)instrumentClipView.InstrumentClipMinder::createNewInstrument(static_cast<InstrumentClip*>(g_clip),
	                                                                   OutputType::SYNTH, false, acquired);
	if (guestTop(kRecorderSceneKind)) {
		pressBack();
	}
	startSettleWait("REPLACED the take settling");
	g_stage = Stage::ReplacedEnding;
}

/// Whether @p output is on the song's hibernation list: alive, and out of the song.
bool hibernating(const Output* output) {
	for (Output* o = currentSong->live.media.first_hibernating_instrument; o != nullptr; o = o->next) {
		if (o == output) {
			return true;
		}
	}
	return false;
}

void stepReplacedEnding() {
	if (!waitOver()) {
		return;
	}
	const bool replaced = g_clip->output != g_outputBefore;
	const bool oldAlive = hibernating(g_outputBefore);
	// Read only while it is known to be alive.
	const OscType oldOsc = oldAlive ? g_sound->oscType(0) : OscType::SAW;
	const int64_t leases = leasesOn(g_recordingPath);
	printf("RT-REPLACED replaced=%d oldHibernating=%d oldOsc=%d path=%s leases=%lld steps=%d\n", replaced ? 1 : 0,
	       oldAlive ? 1 : 0, static_cast<int>(oldOsc), g_recordingPath.c_str(), static_cast<long long>(leases),
	       g_wait.ticks());
	if (!replaced || !oldAlive) {
		g_report.fail("REPLACED the synth was not replaced and kept alive, so nothing was tested");
	}
	if (oldOsc == OscType::SAMPLE) {
		g_report.fail("REPLACED the replaced synth's source was switched to SAMPLE");
	}
	// Attaching takes a lease on the recording; a dropped take holds none.
	if (g_recordingPath.empty() || leases > 0) {
		g_report.fail("REPLACED a take whose sound was replaced under it was attached to something");
	}
	// next_song_mid_take needs a song to move on to, and the load browser's listing that NEXT_SONG walks: a song
	// loaded from the browser leaves it behind, so the leg opens the browser on that song and leaves it the same way.
	if (auto file = deluge::io::File::open(kNextSongPath, DELUGE_FILE_WRITE_CREATE); file.has_value()) {
		const std::string_view text{kNextSongText};
		(void)file->write(std::as_bytes(std::span{text.data(), text.size()}));
	}
	currentSong->setSongFullPath(kNextSongPath);
	(void)openUI(&loadSongUI);
	startSettleWait("NEXT the load browser's listing");
	g_stage = Stage::NextListing;
}

void stepNextListing() {
	const bool listing = Browser::isListingInProgress();
	if (listing) {
		loadSongUI.graphicsRoutine();
	}
	if (!settled(!listing)) {
		return;
	}
	for (int32_t t = 0; currentUIMode == UI_MODE_VERTICAL_SCROLL && t < kDisplayHeight * 4; t++) {
		(void)loadSongUI.timerCallback();
	}
	FileItem* const item = Browser::getCurrentFileItem();
	const bool listed = item != nullptr && !item->isFolder;
	nullifyUIs();
	currentUIMode = UI_MODE_NONE;
	if (!listed) {
		g_report.fail("NEXT the load browser did not list a song for NEXT_SONG to move on to, so nothing was tested");
		g_stage = Stage::Report;
		return;
	}
	auto& command = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::NEXT_SONG)];
	command.cable = nullptr;
	command.channelOrZone = 0;
	command.noteOrCC = kNextSongNote;
	// NEXT_SONG moves on only while the song plays.
	playbackHandler.setupPlaybackUsingInternalClock(0, false);
	if (!startTake("RT-NEXT")) {
		g_stage = Stage::Report;
		return;
	}
	g_songBefore = currentSong;
	if (!g_scriptPath.empty()) {
		std::remove(g_scriptPath.c_str());
	}
	g_scriptPath = writeNextSongScript();
	g_events = g_scriptPath.empty() ? -1 : deluge_host_midi_script_start(g_scriptPath.c_str());
	if (g_events <= 0) {
		g_report.fail("NEXT the DIN script could not be written or armed");
		g_stage = Stage::Report;
		return;
	}
	g_nextRead = false;
	g_loadSeen = false;
	startWait("NEXT the app reading the NEXT_SONG note", kMaxTakeSteps);
	g_stage = Stage::NextRun;
}

/// The take must run on past its NEXT_SONG: a song load closes every UI, the recorder's included.
void stepNextRun() {
	notePath();
	g_loadSeen = g_loadSeen || loadSongUI.loadInFlight();
	if (!guestTop(kRecorderSceneKind) || !inputTake()) {
		printf("RT-NEXT-RUN read=%d loadSeen=%d recorderOnTop=%d steps=%d\n", g_nextRead ? 1 : 0, g_loadSeen ? 1 : 0,
		       guestTop(kRecorderSceneKind) ? 1 : 0, g_nextRead ? g_steps : g_wait.ticks());
		g_report.fail("NEXT the take ended under a MIDI NEXT_SONG, before BACK was pressed");
		startWait("NEXT the next song loading", kMaxLoadSteps);
		g_stage = Stage::NextLoad;
		return;
	}
	if (!g_nextRead) {
		g_nextRead = deluge_host_midi_script_released() == static_cast<uint32_t>(g_events);
		switch (g_wait.poll(g_nextRead)) {
		case WaitState::Pending:
			break;
		case WaitState::Met:
			g_steps = 0;
			break;
		case WaitState::Expired:
			pressBack();
			startSettleWait("NEXT the take settling");
			g_stage = Stage::NextEnding;
			break;
		}
		return;
	}
	if (++g_steps < kNextRunSteps) {
		return;
	}
	printf("RT-NEXT-RUN read=1 loadSeen=%d recorderOnTop=1 steps=%d\n", g_loadSeen ? 1 : 0, g_steps);
	if (g_loadSeen) {
		g_report.fail("NEXT a song load started while the take ran");
	}
	pressBack();
	startSettleWait("NEXT the take settling");
	g_stage = Stage::NextEnding;
}

/// The take ends as BACK asks, in the song it was recorded in, and attaches there.
void stepNextEnding() {
	g_loadSeen = g_loadSeen || loadSongUI.loadInFlight();
	if (!waitOver()) {
		return;
	}
	// The sound is read only while its song is still the current one.
	const bool sameSong = currentSong == g_songBefore;
	const bool attached = sameSong && g_sound->oscType(0) == OscType::SAMPLE && g_range->getAudioFileHolder() != nullptr
	                      && g_range->getAudioFileHolder()->audioFile != nullptr;
	printf("RT-NEXT-ATTACH sameSong=%d attached=%d loadSeen=%d steps=%d\n", sameSong ? 1 : 0, attached ? 1 : 0,
	       g_loadSeen ? 1 : 0, g_wait.ticks());
	if (!attached) {
		g_report.fail("NEXT a take that a MIDI NEXT_SONG arrived during did not attach its sample");
	}
	startWait("NEXT the next song loading", kMaxLoadSteps);
	g_stage = Stage::NextLoad;
}

/// Once the take is over, the NEXT_SONG it held back loads the next song.
void stepNextLoad() {
	g_loadSeen = g_loadSeen || loadSongUI.loadInFlight();
	const bool songChanged = currentSong != g_songBefore;
	if (!settled(songChanged && !loadSongUI.loadInFlight())) {
		return;
	}
	printf("RT-NEXT songChanged=%d loadSeen=%d loadInFlight=%d steps=%d\n", songChanged ? 1 : 0, g_loadSeen ? 1 : 0,
	       loadSongUI.loadInFlight() ? 1 : 0, g_wait.ticks());
	g_stage = Stage::Report;
}

void report() {
	deluge_host_test_input(0);
	if (playbackHandler.isEitherClockActive()) {
		playbackHandler.endPlayback();
	}
	if (!g_scriptPath.empty()) {
		std::remove(g_scriptPath.c_str());
	}
	g_report.printResult();
	g_stage = Stage::Done;
}

} // namespace

extern "C" {

void deluge_scenario_recorder_take_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Loading;
	g_stepDone = false;
	g_report.reset();
	g_wait = Wait{g_report, "OPEN the blank song's preset load", kMaxLoadSteps};
	g_steps = 0;
	g_clip = nullptr;
	g_sound = nullptr;
	g_range = nullptr;
	g_outputBefore = nullptr;
	g_recordingPath.clear();
	g_scriptPath.clear();
	g_events = 0;
	g_voicesMax = 0;
	g_externalClockSeen = false;
	g_recordingShown = false;
	g_clipSeen = false;
	g_clipConsumed = false;
	g_songBefore = nullptr;
	g_nextRead = false;
	g_loadSeen = false;
}

bool deluge_scenario_recorder_take_complete(void) {
	return g_stage == Stage::Done;
}

void deluge_scenario_recorder_take_dispatch_step(void) {
	g_stepDone = false;
	switch (g_stage) {
	case Stage::Loading:
		stepLoading();
		break;
	case Stage::MidiOpen:
		stepMidiOpen();
		break;
	case Stage::MidiTake:
		stepMidiTake();
		break;
	case Stage::MidiEnding:
		stepMidiEnding();
		break;
	case Stage::RefuseStart:
		stepRefuseStart();
		break;
	case Stage::RefuseSettle:
		stepRefuseSettle();
		break;
	case Stage::ClosedRun:
		stepClosedRun();
		break;
	case Stage::ClosedWait:
		stepClosedWait();
		break;
	case Stage::SecondRun:
		stepSecondRun();
		break;
	case Stage::SecondEnding:
		stepSecondEnding();
		break;
	case Stage::NulledRun:
		stepNulledRun();
		break;
	case Stage::NulledWait:
		stepNulledWait();
		break;
	case Stage::ReplacedRun:
		stepReplacedRun();
		break;
	case Stage::ReplacedEnding:
		stepReplacedEnding();
		break;
	case Stage::NextListing:
		stepNextListing();
		break;
	case Stage::NextRun:
		stepNextRun();
		break;
	case Stage::NextEnding:
		stepNextEnding();
		break;
	case Stage::NextLoad:
		stepNextLoad();
		break;
	case Stage::Report:
		report();
		break;
	case Stage::Done:
		break;
	}
	g_stepDone = true;
}

bool deluge_scenario_recorder_take_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_recorder_take_failures(void) {
	return g_report.failures();
}

} // extern "C"
