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
/// @brief Play one synth preset as the device plays it: loaded by the blank boot song from `SYNTHS/PRESET.XML`,
/// played from a DIN MIDI note script, and rendered in real time on the audio tier. One case of the synth golden
/// masters.
///
/// Configured from the environment:
///   - `RENDER_TREE`: the card tree holding `SYNTHS/PRESET.XML` (golden_vt_render fills the card from it).
///   - `RENDER_MIDI`: the note script, `<frame> <hex bytes>` per line, frames counted from the start of capture.
///   - `RENDER_SECONDS`: how long to capture (default 2). Whole 128-frame blocks are captured, so the capture can run
///     up to a block past it.
///   - `RENDER_INPUT`: feeds the audio input a harness test signal from the start of capture (see `audio/host.rs`),
///     for presets whose sources read the input: `1` the sawtooth, `2` the percussive signal. Silence otherwise.
///   - `RENDER_STEREO`: `1` plugs headphones in from the scenario's start, so the app renders in stereo as the device
///     does through them (voices with a stereo source render and filter both channels, ping-pong delay alternates).
///     Mono otherwise, as through the device's speaker.
///   - `RENDER_PCM_OUT`: where the harness writes the captured output (see `audio/host.rs`).
///   - `RENDER_ZONE_MOVE`: `<block> <marker> <frame>` moves one marker of osc 1's first zone -- `start`, `loop_start`,
///     `loop_end` or `end` -- to `<frame>` once `<block>` captured blocks have rendered, as the sample marker editor
///     does mid-note. `RENDER_ZONE_END`: `<block> <frame>` is the same for `end`. Unmoved otherwise.
///   - `RENDER_SETTING`: `<block> <path> <value>` changes the synth's setting at `<path>` (a settings-schema path, e.g.
///     `osc1/type`) to `<value>` once `<block>` captured blocks have rendered, as its sound editor menu does.
///
/// Lines:
///   - `RP-LOADED name=<n> swapped=<0|1>`: the synth the blank song holds once its preset load has finished. A
///     failure unless the preset replaced the built-in default synth: a failed load leaves that synth, named `0`, in
///     place and renders just as well.
///   - `RP-MIDI events=<n> released=<n>`: the script's events, and how many of them the app read before capture
///     ended. A failure unless it read them all.
///   - `RP-RENDER blocks=<n> voices_max=<v> direness_max=<d>`: blocks rendered while capturing, the most voices
///     sounding at once, and the highest CPU direness seen. A failure if direness rose at all: direness culls voices
///     and lowers quality, and the golden masters record full-quality renders.
///   - `RP-TIER-SYNC-AUDIO total=<n>`: synchronous card calls the render made; the audio tier must make none.
///   - `RP-CAPTURE frames=<n> input=<0|1> stereo=<0|1>`: frames captured, whether the test input signal was fed, and
///     whether headphones were plugged in. A failure if the app rendered any captured block in the other width.
///   - `RP-ZONE block=<n> marker=<m> frame=<frame>`: the zone's marker moved (`RENDER_ZONE_MOVE` or `RENDER_ZONE_END`
///     only); a failure if the marker is not one of the four.
///   - `RP-SETTING block=<n> path=<p> value=<v>`: the setting changed (`RENDER_SETTING` only); a failure if refused.
///   - `RP-RESULT failures=<n>`.

#include "scenarios/follow_channel.h"
#include "scenarios/support/scenario_declare.h"
#include "scenarios/support/scenario_report.h"

#include "gui/ui/load/instrument_load.h"
#include "libdeluge/tier.h"
#include "memory/heaps.h"
#include "model/clip/clip.h"
#include "model/edits/setting_edit.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/project_bridge/setting_apply.h"
#include "model/project_bridge/source_bridge.h"
#include "model/song/song.h"
#include "modulation/automation/record_events.h"
#include "modulation/params/lane_publication.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_instrument.h"
#include "util/structural_edit_probe.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

// The app's printf.h aliases printf to an implementation that writes through putchar_, which the Rust BSP builds do not
// define; this harness links the Rust BSP, so the alias would leave every line below an undefined symbol.
#ifdef printf
#undef printf
#endif

DELUGE_SCENARIO_DECLARE(render_preset);

// The harness's capture sink, render count and note script (audio/host.rs).
extern "C" {
void deluge_host_capture_start(void);
uint64_t deluge_host_capture_stop(void);
uint64_t deluge_host_render_count(void);
int32_t deluge_host_midi_script_start(const char* path);
void deluge_host_test_input(uint32_t signal);
void deluge_host_headphones(bool on);
uint32_t deluge_host_midi_script_released(void);
}

// The app's input routine (deluge.cpp): reads the sockets, among them headphone detect, into the audio engine.
void inputRoutine();

namespace {

using deluge::scenarios::ScenarioReport;
using deluge::scenarios::Wait;
using deluge::scenarios::WaitState;

enum class Stage : uint8_t { Loading, Start, Capturing, Report, Done };

/// Ticks the preset load is given before it counts as a failure. A tick is about two virtual milliseconds.
constexpr int32_t kMaxLoadTicks = 20000;
constexpr uint64_t kBlockFrames = 128;
constexpr double kFramesPerSecond = 44100.0;

Stage g_stage = Stage::Loading;
ScenarioReport g_report{"RP"};
/// The load's wait, then the capture's. The capture's budget depends on the blocks asked for and is an int32_t, which
/// holds any RENDER_SECONDS below about 8 hours of audio (2 hours is about 5.6 million ticks).
Wait g_wait{g_report, "the blank song's preset load", kMaxLoadTicks};

std::string g_midiPath;
/// The harness test signal the audio input is fed (RENDER_INPUT), 0 for silence.
uint32_t g_testInput = 0;
bool g_stereo = false;
uint64_t g_blocksWanted = 0;
int32_t g_midiEvents = 0;

uint64_t g_renderBase = 0;
/// The render's heap allocations when capture starts: the render must make none.
uint32_t g_allocationBase = 0;
uint32_t g_audioSyncBase = 0;
uint64_t g_blocksRendered = 0;
int32_t g_direnessMax = 0;
int32_t g_voicesMax = 0;
/// Captured ticks whose output was not in the channel count the case asked for.
int32_t g_wrongWidthTicks = 0;
uint64_t g_captured = 0;
/// The captured block at which one of osc 1's first zone's markers moves, as the sample marker editor moves it; 0 for
/// none. The marker and the frame it moves to.
uint64_t g_zoneMoveBlock = 0;
MarkerType g_zoneMoveMarker = MarkerType::END;
uint32_t g_zoneMoveFrame = 0;
bool g_zoneMoved = false;
/// The captured block at which a setting changes, as its menu changes it; 0 for none.
uint64_t g_settingBlock = 0;
std::string g_settingPath;
std::string g_settingValue;
bool g_settingApplied = false;

/// The load is over once the blank song has asked for its preset and nothing it asked for is still in flight.
void stepLoading() {
	const bool loaded = !currentSong->blankPresetPending && !deluge::gui::instrumentLoad.acquisitionInFlight();
	switch (g_wait.poll(loaded)) {
	case WaitState::Pending:
		return;
	case WaitState::Expired:
		g_stage = Stage::Report;
		return;
	case WaitState::Met:
		break;
	}
	const Output* output =
	    currentSong->live.clips.session_clips.empty() ? nullptr : currentSong->live.clips.session_clips[0]->output;
	// completeBlankPreset() clears blankPresetInstrument when it swaps the preset in, and leaves it set when the card
	// had nothing to swap in.
	const bool swapped = currentSong->blankPresetInstrument == nullptr;
	printf("RP-LOADED name=%s swapped=%d\n", output != nullptr ? output->name.c_str() : "(none)", swapped ? 1 : 0);
	if (output == nullptr || output->type != OutputType::SYNTH || !swapped) {
		g_report.fail("LOADED the preset did not replace the built-in default synth");
		g_stage = Stage::Report;
		return;
	}
	g_stage = Stage::Start;
}

/// Capture and the note script start on the same frame, so the script's frames count from the capture's first.
void stepStart() {
	if (!g_midiPath.empty()) {
		::scenarios::followChannelOne();
		g_midiEvents = deluge_host_midi_script_start(g_midiPath.c_str());
		if (g_midiEvents < 0) {
			g_report.fail("MIDI the note script could not be read");
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
	g_allocationBase = deluge::memory::audioTierAllocations();
	deluge_host_capture_start();
	// A block takes about 2.9 virtual milliseconds and a tick about two, so twice the blocks in ticks is ample.
	g_wait = Wait{g_report, "RENDER the render reaching the blocks asked for",
	              static_cast<int32_t>(g_blocksWanted * 2 + 1000)};
	g_stage = Stage::Capturing;
}

/// The marker a `RENDER_ZONE_MOVE` names, or NONE for a name that is not one.
MarkerType markerNamed(std::string_view name) {
	if (name == "start") {
		return MarkerType::START;
	}
	if (name == "loop_start") {
		return MarkerType::LOOP_START;
	}
	if (name == "loop_end") {
		return MarkerType::LOOP_END;
	}
	if (name == "end") {
		return MarkerType::END;
	}
	return MarkerType::NONE;
}

const char* markerName(MarkerType marker) {
	switch (marker) {
	case MarkerType::START:
		return "start";
	case MarkerType::LOOP_START:
		return "loop_start";
	case MarkerType::LOOP_END:
		return "loop_end";
	default:
		return "end";
	}
}

/// Moves one marker of osc 1's first zone mid-note, as the sample marker editor does: the new ranges as one edit, then
/// the sound's voices told which marker moved.
void moveZoneMarker() {
	Clip* clip = currentSong->live.clips.session_clips[0];
	auto& sound = *static_cast<SoundInstrument*>(clip->output);
	std::vector<deluge::synth::SourceRange> ranges = deluge::bridge::rangesOf(sound.sourceResources[0]);
	deluge::synth::SampleZone& zone = ranges[0].zone;
	switch (g_zoneMoveMarker) {
	case MarkerType::START:
		zone.start = g_zoneMoveFrame;
		break;
	case MarkerType::LOOP_START:
		zone.loop_start = g_zoneMoveFrame;
		break;
	case MarkerType::LOOP_END:
		zone.loop_end = g_zoneMoveFrame;
		break;
	default:
		zone.end = g_zoneMoveFrame;
		break;
	}
	deluge::edits::rangesFor(sound, 0, std::move(ranges));
	alignas(ModelStackWithAutoParam) char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithSoundFlags* modelStack = setupModelStackWithSong(modelStackMemory, currentSong)
	                                           ->addTimelineCounter(clip)
	                                           ->addModControllableButNoNoteRow(&sound)
	                                           ->addParamManager(&clip->paramManager)
	                                           ->addSoundFlags();
	sound.sampleZoneChanged(g_zoneMoveMarker, 0, modelStack);
	printf("RP-ZONE block=%llu marker=%s frame=%u\n", static_cast<unsigned long long>(g_blocksRendered),
	       markerName(g_zoneMoveMarker), g_zoneMoveFrame);
}

/// Changes the synth's setting at g_settingPath mid-render, as its sound editor menu does (side effects included).
void applySettingNow() {
	Clip* clip = currentSong->live.clips.session_clips[0];
	auto& sound = *static_cast<SoundInstrument*>(clip->output);
	const std::optional<deluge::project::SlotAddress> slot = deluge::bridge::slotAddressOf(sound);
	deluge::edits::Context ctx{*currentSong, currentSong->project};
	deluge::edits::SetEngineSetting edit{slot.value_or(deluge::project::SlotAddress{}), g_settingPath,
	                                     deluge::serialization::Value::makeScalar(g_settingValue)};
	const Error error = slot ? deluge::edits::apply(ctx, edit) : Error::UNSPECIFIED;
	if (error != Error::NONE) {
		g_report.fail("SETTING the setting was refused");
		return;
	}
	printf("RP-SETTING block=%llu path=%s value=%s\n", static_cast<unsigned long long>(g_blocksRendered),
	       g_settingPath.c_str(), g_settingValue.c_str());
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
		moveZoneMarker();
	}
	if (g_settingBlock != 0 && !g_settingApplied && g_blocksRendered >= g_settingBlock) {
		g_settingApplied = true;
		applySettingNow();
	}
	switch (g_wait.poll(g_blocksRendered >= g_blocksWanted)) {
	case WaitState::Pending:
		return;
	case WaitState::Met:
	case WaitState::Expired:
		g_captured = deluge_host_capture_stop();
		deluge_host_test_input(0);
		g_stage = Stage::Report;
		return;
	}
}

void report() {
	const uint32_t released = deluge_host_midi_script_released();
	printf("RP-MIDI events=%d released=%u\n", std::max(g_midiEvents, 0), released);
	if (g_midiEvents > 0 && released != static_cast<uint32_t>(g_midiEvents)) {
		g_report.fail("MIDI the app did not read every scripted event before capture ended");
	}
	printf("RP-RENDER blocks=%llu voices_max=%d direness_max=%d\n", static_cast<unsigned long long>(g_blocksRendered),
	       g_voicesMax, g_direnessMax);
	if (g_direnessMax != 0) {
		g_report.fail("RENDER CPU direness rose, so the render took reduced-quality paths");
	}
	const uint32_t allocations = deluge::memory::audioTierAllocations() - g_allocationBase;
#if ALPHA_OR_BETA_VERSION
	const uint32_t storeEdits = deluge::renderStructuralEdits(deluge::StructuralContainer::PARAM_STORE);
	printf("RP-STORE render_edits=%u\n", storeEdits);
	if (storeEdits != 0) {
		g_report.fail("STORE the render changed a lane store");
	}
#endif
	// Reported, not gated: the live-input pitch shift, the sample timestretch and the voice cache paths allocate on
	// the render (TODO.md).
	printf("RP-ALLOC render_allocations=%u\n", allocations);
	const uint32_t audioSync = deluge_tier_sync_audio_total() - g_audioSyncBase;
	printf("RP-TIER-SYNC-AUDIO total=%u\n", audioSync);
	if (audioSync != 0) {
		g_report.fail("TIER-SYNC-AUDIO the render made a synchronous card call");
	}
	printf("RP-CAPTURE frames=%llu input=%u stereo=%d\n", static_cast<unsigned long long>(g_captured), g_testInput,
	       g_stereo ? 1 : 0);
	if (g_wrongWidthTicks != 0) {
		g_report.fail("CAPTURE %s", g_stereo ? "the app rendered in mono with headphones plugged in"
		                                     : "the app rendered in stereo with nothing plugged in");
	}
#if ALPHA_OR_BETA_VERSION
	printf("RP-SNAPSHOT model_reads=%u render_writes=%u requests_dropped=%u\n", deluge::modulation::renderModelReads,
	       deluge::modulation::renderModelWrites, deluge::modulation::laneRequestOverflows);
	if (deluge::modulation::renderModelReads != 0 || deluge::modulation::renderModelWrites != 0) {
		g_report.fail("SNAPSHOT the render read or wrote a lane in the model");
	}
	if (deluge::modulation::laneRequestOverflows != 0) {
		g_report.fail("SNAPSHOT a lane request the render made found the queue full");
	}
	printf("RP-LANES unheld_writes=%u splits=%u\n", deluge::modulation::unheldLaneWrites,
	       deluge::modulation::laneSplits);
	if (deluge::modulation::laneSplits != 0) {
		g_report.fail("LANES the interaction tier resolved a lane the render does not read");
	}
	if (deluge::modulation::unheldLaneWrites != 0) {
		g_report.fail("LANES the render's unheld lane was written");
	}
#endif
	g_report.printResult();
	g_stage = Stage::Done;
}

} // namespace

extern "C" {

void deluge_scenario_render_preset_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Loading;
	g_report.reset();
	g_wait = Wait{g_report, "the blank song's preset load", kMaxLoadTicks};
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
	g_testInput = input != nullptr ? static_cast<uint32_t>(std::strtoul(input, nullptr, 10)) : 0;
	const char* midi = std::getenv("RENDER_MIDI");
	g_midiPath = midi != nullptr ? midi : "";
	g_zoneMoved = false;
	g_zoneMoveBlock = 0;
	g_zoneMoveMarker = MarkerType::END;
	if (const char* move = std::getenv("RENDER_ZONE_MOVE"); move != nullptr) {
		char marker[16] = {};
		unsigned long long block = 0;
		unsigned frame = 0;
		if (std::sscanf(move, "%llu %15s %u", &block, marker, &frame) != 3 || markerNamed(marker) == MarkerType::NONE) {
			g_report.fail("ZONE RENDER_ZONE_MOVE names no marker of the four");
		}
		else {
			g_zoneMoveBlock = block;
			g_zoneMoveMarker = markerNamed(marker);
			g_zoneMoveFrame = frame;
		}
	}
	else if (const char* end = std::getenv("RENDER_ZONE_END"); end != nullptr) {
		char* rest = nullptr;
		g_zoneMoveBlock = std::strtoull(end, &rest, 10);
		g_zoneMoveFrame = static_cast<uint32_t>(std::strtoul(rest, nullptr, 10));
	}
	g_settingApplied = false;
	g_settingBlock = 0;
	if (const char* setting = std::getenv("RENDER_SETTING"); setting != nullptr) {
		char* rest = nullptr;
		g_settingBlock = std::strtoull(setting, &rest, 10);
		const std::string pair = rest != nullptr ? rest : "";
		const size_t path_start = pair.find_first_not_of(' ');
		if (path_start == std::string::npos) {
			g_report.fail("SETTING RENDER_SETTING names no setting");
			g_settingBlock = 0;
		}
		else {
			const size_t space = pair.find(' ', path_start);
			g_settingPath = pair.substr(path_start, space - path_start);
			g_settingValue = space == std::string::npos ? "" : pair.substr(space + 1);
		}
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
	return g_report.failures();
}

} // extern "C"
