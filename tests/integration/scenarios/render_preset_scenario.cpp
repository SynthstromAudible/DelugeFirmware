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

#include "scenarios/render_preset_scenario.h"

#include "gui/menu_item/setting_edit.h"
#include "gui/ui/load/instrument_load.h"
#include "libdeluge/tier.h"
#include "model/clip/clip.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/project_bridge/source_bridge.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_instrument.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// The app's printf.h aliases printf to an implementation that writes through putchar_, which the Rust BSP builds do not
// define; this harness links the Rust BSP, so the alias would leave every line below an undefined symbol.
#ifdef printf
#undef printf
#endif

// The harness's capture sink, render count and note script (audio/host.rs).
extern "C" {
void deluge_host_capture_start(void);
uint64_t deluge_host_capture_stop(void);
uint64_t deluge_host_render_count(void);
int32_t deluge_host_midi_script_start(const char* path);
void deluge_host_test_input(bool on);
void deluge_host_headphones(bool on);
uint32_t deluge_host_midi_script_released(void);
}

// The app's input routine (deluge.cpp): reads the sockets, among them headphone detect, into the audio engine.
void inputRoutine();

namespace {

enum class Stage : uint8_t { Loading, Start, Capturing, Report, Done };

/// Ticks the preset load is given before it counts as a failure. A tick is about two virtual milliseconds.
constexpr int32_t kMaxLoadTicks = 20000;
constexpr uint64_t kBlockFrames = 128;
constexpr double kFramesPerSecond = 44100.0;

Stage g_stage = Stage::Loading;
int32_t g_failures = 0;
int32_t g_ticks = 0;

std::string g_midiPath;
bool g_testInput = false;
bool g_stereo = false;
uint64_t g_blocksWanted = 0;
int32_t g_midiEvents = 0;

uint64_t g_renderBase = 0;
uint32_t g_audioSyncBase = 0;
uint64_t g_blocksRendered = 0;
int32_t g_direnessMax = 0;
int32_t g_voicesMax = 0;
/// Captured ticks whose output was not in the channel count the case asked for.
int32_t g_wrongWidthTicks = 0;
uint64_t g_captured = 0;
/// The captured block at which osc 1's first zone takes a new end, as the sample marker editor sets it; 0 for none.
uint64_t g_zoneMoveBlock = 0;
uint32_t g_zoneMoveEnd = 0;
bool g_zoneMoved = false;

void fail(const char* marker, const char* what) {
	printf("%s FAIL %s\n", marker, what);
	g_failures++;
}

/// The load is over once the blank song has asked for its preset and nothing it asked for is still in flight.
void stepLoading() {
	if (currentSong->blankPresetPending || deluge::gui::instrumentLoad.acquisitionInFlight()) {
		if (++g_ticks > kMaxLoadTicks) {
			fail("RP-LOADED", "the blank song's preset load never finished");
			g_stage = Stage::Report;
		}
		return;
	}
	const Output* output =
	    currentSong->live.clips.session_clips.empty() ? nullptr : currentSong->live.clips.session_clips[0]->output;
	// completeBlankPreset() clears blankPresetInstrument when it swaps the preset in, and leaves it set when the card
	// had nothing to swap in.
	const bool swapped = currentSong->blankPresetInstrument == nullptr;
	printf("RP-LOADED name=%s swapped=%d\n", output != nullptr ? output->name.c_str() : "(none)", swapped ? 1 : 0);
	if (output == nullptr || output->type != OutputType::SYNTH || !swapped) {
		fail("RP-LOADED", "the preset did not replace the built-in default synth");
		g_stage = Stage::Report;
		return;
	}
	g_stage = Stage::Start;
}

/// Capture and the note script start on the same frame, so the script's frames count from the capture's first.
void stepStart() {
	if (!g_midiPath.empty()) {
		g_midiEvents = deluge_host_midi_script_start(g_midiPath.c_str());
		if (g_midiEvents < 0) {
			fail("RP-MIDI", "the note script could not be read");
			g_stage = Stage::Report;
			return;
		}
	}
	deluge_host_test_input(g_testInput);
	if (g_stereo) {
		// The app reads its sockets in its input routine, on a UI timer the harness does not tick; read them now, so
		// the headphones plugged in at the start count from the first captured block.
		inputRoutine();
	}
	g_renderBase = deluge_host_render_count();
	g_audioSyncBase = deluge_tier_sync_audio_total();
	deluge_host_capture_start();
	g_ticks = 0;
	g_stage = Stage::Capturing;
}

/// Moves osc 1's first zone's end mid-note, as the sample marker editor does: the new ranges as one edit, then the
/// sound's voices told which marker moved.
void moveZoneEnd() {
	Clip* clip = currentSong->live.clips.session_clips[0];
	auto& sound = *static_cast<SoundInstrument*>(clip->output);
	std::vector<deluge::instrument::RangeState> ranges = deluge::bridge::rangesOf(sound.sources[0]);
	ranges[0].end_pos = g_zoneMoveEnd;
	deluge::gui::menu_item::rangesFor(sound, 0, std::move(ranges));
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithSoundFlags* modelStack = setupModelStackWithSong(modelStackMemory, currentSong)
	                                           ->addTimelineCounter(clip)
	                                           ->addModControllableButNoNoteRow(&sound)
	                                           ->addParamManager(&clip->paramManager)
	                                           ->addSoundFlags();
	sound.sampleZoneChanged(MarkerType::END, 0, modelStack);
	printf("RP-ZONE block=%llu end=%u\n", static_cast<unsigned long long>(g_blocksRendered), g_zoneMoveEnd);
}

void stepCapturing() {
	g_direnessMax = std::max(g_direnessMax, static_cast<int32_t>(AudioEngine::cpuDireness));
	g_voicesMax = std::max(g_voicesMax, static_cast<int32_t>(AudioEngine::getNumVoices()));
	if (AudioEngine::renderInStereo != g_stereo) {
		g_wrongWidthTicks++;
	}
	g_blocksRendered = deluge_host_render_count() - g_renderBase;
	if (g_zoneMoveBlock != 0 && !g_zoneMoved && g_blocksRendered >= g_zoneMoveBlock) {
		g_zoneMoved = true;
		moveZoneEnd();
	}
	if (g_blocksRendered >= g_blocksWanted) {
		g_captured = deluge_host_capture_stop();
		deluge_host_test_input(false);
		g_stage = Stage::Report;
		return;
	}
	// A block takes about 2.9 virtual milliseconds and a tick about two, so twice the blocks in ticks is ample.
	if (++g_ticks > static_cast<int64_t>(g_blocksWanted) * 2 + 1000) {
		fail("RP-RENDER", "the render stopped short of the blocks asked for");
		g_captured = deluge_host_capture_stop();
		deluge_host_test_input(false);
		g_stage = Stage::Report;
	}
}

void report() {
	const uint32_t released = deluge_host_midi_script_released();
	printf("RP-MIDI events=%d released=%u\n", std::max(g_midiEvents, 0), released);
	if (g_midiEvents > 0 && released != static_cast<uint32_t>(g_midiEvents)) {
		fail("RP-MIDI", "the app did not read every scripted event before capture ended");
	}
	printf("RP-RENDER blocks=%llu voices_max=%d direness_max=%d\n", static_cast<unsigned long long>(g_blocksRendered),
	       g_voicesMax, g_direnessMax);
	if (g_direnessMax != 0) {
		fail("RP-RENDER", "CPU direness rose, so the render took reduced-quality paths");
	}
	const uint32_t audioSync = deluge_tier_sync_audio_total() - g_audioSyncBase;
	printf("RP-TIER-SYNC-AUDIO total=%u\n", audioSync);
	if (audioSync != 0) {
		fail("RP-TIER-SYNC-AUDIO", "the render made a synchronous card call");
	}
	printf("RP-CAPTURE frames=%llu input=%d stereo=%d\n", static_cast<unsigned long long>(g_captured),
	       g_testInput ? 1 : 0, g_stereo ? 1 : 0);
	if (g_wrongWidthTicks != 0) {
		fail("RP-CAPTURE", g_stereo ? "the app rendered in mono with headphones plugged in"
		                            : "the app rendered in stereo with nothing plugged in");
	}
	printf("RP-RESULT failures=%d\n", g_failures);
	g_stage = Stage::Done;
}

} // namespace

extern "C" {

void deluge_scenario_render_preset_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Loading;
	g_failures = 0;
	g_ticks = 0;
	g_midiEvents = 0;
	g_direnessMax = 0;
	g_voicesMax = 0;
	g_captured = 0;
	g_wrongWidthTicks = 0;
	// Headphones plugged in from boot make the app render in stereo, as the device does through them; otherwise it
	// renders in mono, as through the speaker.
	const char* stereo = std::getenv("RENDER_STEREO");
	g_stereo = stereo != nullptr && std::strcmp(stereo, "1") == 0;
	deluge_host_headphones(g_stereo);
	const char* input = std::getenv("RENDER_INPUT");
	g_testInput = input != nullptr && std::strcmp(input, "1") == 0;
	const char* midi = std::getenv("RENDER_MIDI");
	g_midiPath = midi != nullptr ? midi : "";
	g_zoneMoved = false;
	const char* zone = std::getenv("RENDER_ZONE_END");
	g_zoneMoveBlock = 0;
	if (zone != nullptr) {
		char* rest = nullptr;
		g_zoneMoveBlock = std::strtoull(zone, &rest, 10);
		g_zoneMoveEnd = static_cast<uint32_t>(std::strtoul(rest, nullptr, 10));
	}
	const char* seconds = std::getenv("RENDER_SECONDS");
	const double wanted = std::ceil((seconds != nullptr ? std::strtod(seconds, nullptr) : 2.0) * kFramesPerSecond);
	g_blocksWanted = (static_cast<uint64_t>(wanted) + kBlockFrames - 1) / kBlockFrames;
}

bool deluge_scenario_render_preset_complete(void) {
	return g_stage == Stage::Done;
}

void deluge_scenario_render_preset_dispatch_step(void) {
	switch (g_stage) {
	case Stage::Loading:
		stepLoading();
		break;
	case Stage::Start:
		stepStart();
		break;
	case Stage::Capturing:
		stepCapturing();
		break;
	case Stage::Report:
		report();
		break;
	case Stage::Done:
		break;
	}
}

bool deluge_scenario_render_preset_step_done(void) {
	return true;
}

int32_t deluge_scenario_render_preset_failures(void) {
	return g_failures;
}

} // extern "C"
