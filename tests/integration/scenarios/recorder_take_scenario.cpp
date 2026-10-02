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

#include "scenarios/recorder_take_scenario.h"

#include "definitions_cxx.hpp"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/menus.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/button.h"
#include "hid/display/display.h"
#include "model/clip/clip.h"
#include "model/output.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/multi_range/multi_range.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

// See staging_release_scenario.cpp: the app's printf alias has no provider in this harness.
#ifdef printf
#undef printf
#endif

// The harness's test input signal and DIN note script (audio/host.rs).
extern "C" {
int32_t deluge_host_midi_script_start(const char* path);
uint32_t deluge_host_midi_script_released(void);
void deluge_host_test_input(bool on);
}

namespace {

enum class Stage : uint8_t { Loading, Open, Take, Ending, Report, Done };

/// Steps the blank song's preset load is given. A step is about two virtual milliseconds.
constexpr int32_t kMaxLoadSteps = 20000;
/// Steps the take is given to read every scripted byte: the script spans about a second and a half of audio.
constexpr int32_t kMaxTakeSteps = 5000;
/// Steps the take is given to settle once BACK has ended it.
constexpr int32_t kMaxEndingSteps = 5000;

/// MIDI clock at 120 BPM: 24 pulses a quarter note, so one every 919 frames at 44.1 kHz.
constexpr uint64_t kClockFrames = 919;
constexpr int32_t kClockPulses = 48;
constexpr uint64_t kNoteOnFrame = 4410;
constexpr uint64_t kNoteOffFrame = 30000;
constexpr uint64_t kStopFrame = 50000;

Stage g_stage = Stage::Loading;
bool g_stepDone = false;
int32_t g_failures = 0;
int32_t g_steps = 0;

Sound* g_sound = nullptr;
MultiRange* g_range = nullptr;
int32_t g_events = 0;
int32_t g_voicesMax = 0;
bool g_externalClockSeen = false;
bool g_recordingShown = false;
bool g_popupShown = false;

void fail(const char* marker, const char* what) {
	printf("%s FAIL %s\n", marker, what);
	g_failures++;
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

/// The blank song's preset load is over once nothing it asked for is still in flight.
void stepLoading() {
	if (currentSong->blankPresetPending || deluge::gui::instrumentLoad.acquisitionInFlight()) {
		if (++g_steps > kMaxLoadSteps) {
			fail("RT-OPEN", "the blank song's preset load never finished");
			g_stage = Stage::Report;
		}
		return;
	}
	g_steps = 0;
	g_stage = Stage::Open;
}

/// The user's gesture: the synth's clip in front, the sound editor opened on the first source's recorder item. The
/// item's session opens the recorder, which starts the take; the gesture must then return, with the take running.
void stepOpen() {
	Clip* clip = currentSong->sessionClips.empty() ? nullptr : currentSong->sessionClips[0];
	if (clip == nullptr || clip->output == nullptr || clip->output->type != OutputType::SYNTH) {
		fail("RT-OPEN", "the blank song has no synth clip to record into");
		g_stage = Stage::Report;
		return;
	}
	currentSong->setCurrentClip(clip);
	changeRootUI(&instrumentClipView);
	deluge_host_test_input(true);

	// The item sits in a horizontal menu: setup() opens that menu focused on it, and the user's Select enters it.
	const bool setup = soundEditor.setup(clip, &sample0RecorderMenu, 0);
	g_sound = soundEditor.currentSound;
	g_range = soundEditor.currentMultiRange;
	const bool opened = setup && openUI(&soundEditor);
	if (opened && getCurrentUI() == &soundEditor) {
		soundEditor.enterSubmenu(&sample0RecorderMenu);
	}
	const bool recorderOnTop = getCurrentUI() == &audioRecorder;
	const bool inputTake = audioRecorder.recordingSource == AudioInputChannel::LEFT
	                       || audioRecorder.recordingSource == AudioInputChannel::STEREO;
	printf("RT-OPEN setup=%d opened=%d recorderOnTop=%d source=%d\n", setup ? 1 : 0, opened ? 1 : 0,
	       recorderOnTop ? 1 : 0, static_cast<int>(audioRecorder.recordingSource));
	if (!recorderOnTop || !inputTake || g_sound == nullptr || g_range == nullptr) {
		fail("RT-OPEN", "the recorder item did not leave an input take running under the recorder UI");
		g_stage = Stage::Report;
		return;
	}

	const std::string script = writeScript();
	g_events = script.empty() ? -1 : deluge_host_midi_script_start(script.c_str());
	if (g_events <= 0) {
		fail("RT-TAKE", "the DIN script could not be written or armed");
		g_stage = Stage::Ending;
		(void)getCurrentUI()->buttonAction(deluge::hid::button::BACK, true, false);
		return;
	}
	g_steps = 0;
	g_stage = Stage::Take;
}

/// While the take runs, MIDI must be handled: every scripted byte read, the note sounding, the clock followed.
void stepTake() {
	const bool takeRunning =
	    getCurrentUI() == &audioRecorder && audioRecorder.recordingSource > AudioInputChannel::NONE;
	if (!takeRunning) {
		fail("RT-TAKE", "the take ended before BACK was pressed");
		g_stage = Stage::Ending;
		return;
	}
	g_voicesMax = std::max(g_voicesMax, static_cast<int32_t>(AudioEngine::getNumVoices()));
	g_externalClockSeen = g_externalClockSeen || playbackHandler.isExternalClockActive();
	g_recordingShown = g_recordingShown || audioRecorder.updatedRecordingStatus;
	g_popupShown = g_popupShown || display->hasPopup();

	const uint32_t released = deluge_host_midi_script_released();
	const bool allRead = released == static_cast<uint32_t>(g_events);
	if (!allRead && ++g_steps <= kMaxTakeSteps) {
		return;
	}
	printf("RT-TAKE events=%d released=%u voicesMax=%d externalClock=%d recordingShown=%d popupShown=%d steps=%d\n",
	       g_events, released, g_voicesMax, g_externalClockSeen ? 1 : 0, g_recordingShown ? 1 : 0, g_popupShown ? 1 : 0,
	       g_steps);
	if (!allRead) {
		fail("RT-TAKE", "the app did not read the DIN bytes delivered during the take");
	}
	if (g_voicesMax == 0) {
		fail("RT-TAKE", "a note delivered during the take did not sound");
	}
	if (!g_externalClockSeen) {
		fail("RT-TAKE", "MIDI Start and clock delivered during the take did not start playback on the external clock");
	}
	// The test input is a full-scale sawtooth, so the take clips every block, and the recorder shows the clipping
	// popup in preference to the Waiting to Recording change. Either shows the recorder's own step ran mid-take.
	if (!g_recordingShown && !g_popupShown) {
		fail("RT-TAKE", "the recorder showed neither clipping nor the Waiting to Recording change during the take");
	}

	(void)getCurrentUI()->buttonAction(deluge::hid::button::BACK, true, false);
	(void)getCurrentUI()->buttonAction(deluge::hid::button::BACK, false, false);
	g_steps = 0;
	g_stage = Stage::Ending;
}

/// BACK ends the take; once it has settled the recorder attaches the sample to the source and closes.
void stepEnding() {
	const bool over = !isUIOpen(&audioRecorder) && audioRecorder.recordingSource == AudioInputChannel::NONE;
	if (!over) {
		if (++g_steps > kMaxEndingSteps) {
			fail("RT-ATTACH", "the take never settled after BACK");
			g_stage = Stage::Report;
		}
		return;
	}
	const Source& source = g_sound->sources[0];
	AudioFileHolder* holder = g_range->getAudioFileHolder();
	const auto* sample = holder != nullptr ? static_cast<const Sample*>(holder->audioFile) : nullptr;
	const bool isSample = source.oscType == OscType::SAMPLE;
	const bool pathsAgree = sample != nullptr && holder->filePath == sample->filePath;
	const uint64_t frames = sample != nullptr ? sample->lengthInSamples : 0;
	printf("RT-ATTACH osc=%d attached=%d pathsAgree=%d frames=%llu path=%s editorOnTop=%d steps=%d\n",
	       static_cast<int>(source.oscType), sample != nullptr ? 1 : 0, pathsAgree ? 1 : 0,
	       static_cast<unsigned long long>(frames), sample != nullptr ? sample->filePath.c_str() : "(none)",
	       getCurrentUI() == &soundEditor ? 1 : 0, g_steps);
	if (!isSample || sample == nullptr || !pathsAgree) {
		fail("RT-ATTACH", "the take's sample was not attached to the source it was recorded for");
	}
	else if (frames == 0 || sample->filePath.rfind("SAMPLES/RECORD/", 0) != 0) {
		fail("RT-ATTACH", "the attached sample is empty or not a recording");
	}
	g_stage = Stage::Report;
}

void report() {
	deluge_host_test_input(false);
	if (playbackHandler.isEitherClockActive()) {
		playbackHandler.endPlayback();
	}
	printf("RT-RESULT failures=%d\n", g_failures);
	g_stage = Stage::Done;
}

} // namespace

extern "C" {

void deluge_scenario_recorder_take_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Loading;
	g_stepDone = false;
	g_failures = 0;
	g_steps = 0;
	g_sound = nullptr;
	g_range = nullptr;
	g_events = 0;
	g_voicesMax = 0;
	g_externalClockSeen = false;
	g_recordingShown = false;
	g_popupShown = false;
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
	case Stage::Open:
		stepOpen();
		break;
	case Stage::Take:
		stepTake();
		break;
	case Stage::Ending:
		stepEnding();
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
	return g_failures;
}

} // extern "C"
