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

#include "scenarios/render_song_scenario.h"

#include "definitions_cxx.hpp"
#include "dsp/granular/GranularProcessor.h"
#include "engines/effects/words.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/session_view.h"
#include "harness/streaming_underrun.h"
#include "libdeluge/tier.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/change_effect.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/output.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// The app's printf.h aliases printf to an implementation that writes through putchar_, which the Rust BSP builds do not
// define; this harness links the Rust BSP, so the alias would leave every line below an undefined symbol.
#ifdef printf
#undef printf
#endif

// The harness's capture sink and render count (audio/host.rs).
extern "C" {
void deluge_host_capture_start(void);
uint64_t deluge_host_capture_stop(void);
uint64_t deluge_host_render_count(void);
uint64_t deluge_sim_card_reads(void);
void deluge_host_headphones(bool on);
// The fill task's modelled latencies (streaming_loader.rs).
void deluge_streaming_set_fill_latency_ms(uint32_t ms);
void deluge_streaming_set_fill_read_latency_us(uint32_t us);
}

// The app's input routine (deluge.cpp): reads the sockets, among them headphone detect, into the audio engine.
void inputRoutine();

namespace {

enum class Stage : uint8_t { Open, Listing, Loading, Play, Playing, Report, Done };

/// Ticks each wait is given before it counts as a failure. A tick is about two virtual milliseconds.
constexpr int32_t kMaxWaitTicks = 20000;

Stage g_stage = Stage::Open;
int32_t g_failures = 0;
int32_t g_ticks = 0;

std::string g_songPath;
uint64_t g_blocksWanted = 3000;
bool g_stereo = false;

/// The storage model while playing (RENDER_FILL_DELAY_MS, RENDER_FILL_READ_US, RENDER_FILL_BURST): the fill task's
/// wait to be scheduled, each chunk's read time, and a window of blocks, counted from playback's start, in which the
/// wait is the burst's instead. All zero by default: a card that answers at once, and a fill task that runs at once.
struct StorageModel {
	uint32_t delayMs = 0;
	uint32_t readUs = 0;
	uint64_t burstStart = 0;
	uint64_t burstBlocks = 0;
	uint32_t burstMs = 0;
};
StorageModel g_model;
bool g_inBurst = false;

uint32_t envU32(const char* name) {
	const char* value = std::getenv(name);
	return value != nullptr ? static_cast<uint32_t>(std::strtoul(value, nullptr, 10)) : 0;
}

/// RENDER_FILL_BURST is `<start block>:<blocks>:<ms>`; anything else leaves no burst.
void readBurst(StorageModel& model) {
	const char* burst = std::getenv("RENDER_FILL_BURST");
	unsigned long long start = 0;
	unsigned long long blocks = 0;
	unsigned ms = 0;
	if (burst != nullptr && std::sscanf(burst, "%llu:%llu:%u", &start, &blocks, &ms) == 3) {
		model.burstStart = start;
		model.burstBlocks = blocks;
		model.burstMs = ms;
	}
}

/// RENDER_DELAY_SWITCH: the blocks, counted from playback's start, at which every audio track's delay switches between
/// analog and digital, ascending and comma-separated; empty by default. Each switch is the Edit the gold knob's press
/// on the delay makes (ModControllableAudio::switchDelayAnalog), so the same delay is switched while it rings.
std::vector<uint64_t> g_switchAt;
std::size_t g_switchesDone = 0;
int32_t g_tracksSwitched = 0;

void readSwitches() {
	g_switchAt.clear();
	const char* list = std::getenv("RENDER_DELAY_SWITCH");
	for (const char* at = list; at != nullptr && *at != '\0';) {
		char* end = nullptr;
		g_switchAt.push_back(std::strtoull(at, &end, 10));
		at = (end != nullptr && *end == ',') ? end + 1 : nullptr;
	}
}

/// Switch every audio track's delay between analog and digital, as a press of the gold knob on its delay does, at
/// render block @p block.
void switchDelays(uint64_t block) {
	for (Output* output = currentSong->live.media.first_output; output != nullptr; output = output->next) {
		if (output->type != OutputType::AUDIO) {
			continue;
		}
		auto* owner = static_cast<ModControllableAudio*>(output->toModControllable());
		if (owner == nullptr) {
			continue;
		}
		deluge::edits::changeEffect(*owner, "delay/analog",
		                            deluge::serialization::Value::makeNumber(owner->effects().delay.analog ? 0 : 1));
		g_tracksSwitched++;
		printf("RS-DELAY-SWITCH block=%llu analog=%d\n", static_cast<unsigned long long>(block),
		       owner->effects().delay.analog ? 1 : 0);
	}
}

/// RENDER_MODFX_SWITCH: `<block>:<type>,...`, ascending, each type as a file writes it (`chorus`, `TapeWarble`); empty
/// by default. At each block, counted from playback's start, every audio track's mod FX type is set to that type by the
/// Edit the mod FX type menu makes, so the same mod FX switches type while it plays.
struct ModFxSwitch {
	uint64_t block;
	std::string type;
};
std::vector<ModFxSwitch> g_modFxSwitchAt;
std::size_t g_modFxSwitchesDone = 0;
int32_t g_tracksModFxSwitched = 0;
/// The type each track switch set, as a file writes it, comma-separated in the order they were made.
std::string g_modFxSwitchedTypes;
/// Track switches after which the track's mod FX holds a ring, and after which it holds none.
int32_t g_modFxSwitchedRing = 0;
int32_t g_modFxSwitchedNoRing = 0;

void readModFxSwitches() {
	g_modFxSwitchAt.clear();
	const char* list = std::getenv("RENDER_MODFX_SWITCH");
	for (const char* at = list; at != nullptr && *at != '\0';) {
		char* end = nullptr;
		const uint64_t block = std::strtoull(at, &end, 10);
		if (end == nullptr || *end != ':') {
			break;
		}
		const char* type = end + 1;
		const char* comma = std::strchr(type, ',');
		g_modFxSwitchAt.push_back({block, comma != nullptr ? std::string(type, comma) : std::string(type)});
		at = comma != nullptr ? comma + 1 : nullptr;
	}
}

/// Set every audio track's mod FX type to @p type, as the mod FX type menu does, at render block @p block.
void switchModFx(uint64_t block, const std::string& type) {
	for (Output* output = currentSong->live.media.first_output; output != nullptr; output = output->next) {
		if (output->type != OutputType::AUDIO) {
			continue;
		}
		auto* owner = static_cast<ModControllableAudio*>(output->toModControllable());
		if (owner == nullptr) {
			continue;
		}
		deluge::edits::changeEffect(*owner, "modFXType", deluge::serialization::Value::makeScalar(type));
		g_tracksModFxSwitched++;
		const std::string now(deluge::effects::to_string(owner->effects().mod_fx.type));
		const bool ring = owner->modfx.holdsRing();
		g_modFxSwitchedTypes += (g_modFxSwitchedTypes.empty() ? "" : ",") + now;
		(ring ? g_modFxSwitchedRing : g_modFxSwitchedNoRing)++;
		printf("RS-MODFX-SWITCH block=%llu type=%s ring=%d\n", static_cast<unsigned long long>(block), now.c_str(),
		       ring ? 1 : 0);
	}
}

/// RENDER_MODFX_CLEAR: the blocks, counted from playback's start, at which every audio track's mod FX memory is
/// cleared, ascending and comma-separated; empty by default. Each clear is what ModControllableAudio::clearModFXMemory,
/// which a sound's cut tail calls, does for the track's type: for a comb type the mod FX component's clearMemory, which
/// zeroes the ring when one is held and the phaser's memories otherwise; for the grain the granular processor's
/// clearGrainFXBuffer, which stops its grains and starts its ring over. A grain track whose processor has not yet been
/// made (the render makes it on its first sounding block) is not cleared.
std::vector<uint64_t> g_modFxClearAt;
std::size_t g_modFxClearsDone = 0;
int32_t g_tracksModFxCleared = 0;
/// Comb track clears that zeroed a ring, and those of a track holding none, which zero the phaser's memories.
int32_t g_modFxClearedRing = 0;
int32_t g_modFxClearedNoRing = 0;
/// Grain track clears.
int32_t g_modFxClearedGrain = 0;

void readModFxClears() {
	g_modFxClearAt.clear();
	const char* list = std::getenv("RENDER_MODFX_CLEAR");
	for (const char* at = list; at != nullptr && *at != '\0';) {
		char* end = nullptr;
		g_modFxClearAt.push_back(std::strtoull(at, &end, 10));
		at = (end != nullptr && *end == ',') ? end + 1 : nullptr;
	}
}

/// Clear every audio track's mod FX memory at render block @p block.
void clearModFx(uint64_t block) {
	for (Output* output = currentSong->live.media.first_output; output != nullptr; output = output->next) {
		if (output->type != OutputType::AUDIO) {
			continue;
		}
		auto* owner = static_cast<ModControllableAudio*>(output->toModControllable());
		const ModFXType type = owner != nullptr ? owner->effects().mod_fx.type : ModFXType::NONE;
		if (type == ModFXType::NONE) {
			continue;
		}
		if (type == ModFXType::GRAIN) {
			if (owner->grainFX == nullptr) {
				continue;
			}
			owner->grainFX->clearGrainFXBuffer();
			g_tracksModFxCleared++;
			g_modFxClearedGrain++;
			printf("RS-MODFX-CLEAR block=%llu type=%s grain=1\n", static_cast<unsigned long long>(block),
			       std::string(deluge::effects::to_string(type)).c_str());
			continue;
		}
		const bool ring = owner->modfx.holdsRing();
		owner->modfx.clearMemory();
		g_tracksModFxCleared++;
		(ring ? g_modFxClearedRing : g_modFxClearedNoRing)++;
		printf("RS-MODFX-CLEAR block=%llu type=%s ring=%d\n", static_cast<unsigned long long>(block),
		       std::string(deluge::effects::to_string(owner->effects().mod_fx.type)).c_str(), ring ? 1 : 0);
	}
}

uint64_t g_renderBase = 0;
uint64_t g_voiceBase[3] = {};
uint64_t g_waitBase = 0;
uint64_t g_unassignBase = 0;
uint32_t g_audioSyncBase = 0;
uint64_t g_readsBase = 0;
uint64_t g_reads = 0;

uint64_t g_blocksRendered = 0;
int32_t g_stops = 0;
int64_t g_firstStop = -1;
bool g_wasPlaying = false;
int32_t g_direnessMax = 0;
int32_t g_voicesMax = 0;
/// Playing ticks whose output was not in the channel count the run asked for.
int32_t g_wrongWidthTicks = 0;
uint64_t g_captured = 0;

void fail(const char* marker, const char* what) {
	printf("%s FAIL %s\n", marker, what);
	g_failures++;
}

/// The song's name as the load gives it: the file name without its folder or extension.
std::string expectedName(const std::string& path) {
	std::string name = path.substr(path.find_last_of('/') + 1);
	if (const auto dot = name.find_last_of('.'); dot != std::string::npos) {
		name.resize(dot);
	}
	return name;
}

bool waitedTooLong(const char* marker, const char* what) {
	if (++g_ticks <= kMaxWaitTicks) {
		return false;
	}
	fail(marker, what);
	g_stage = Stage::Report;
	return true;
}

void stepOpen() {
	// Also renames the current song to the file name WITH its extension, while a completed load names the new song
	// without it: so a load that gives up cannot leave behind a song whose name matches.
	currentSong->setSongFullPath(g_songPath.c_str());
	if (!openUI(&loadSongUI)) {
		fail("RS-LOADED", "the load browser would not open");
		g_stage = Stage::Report;
		return;
	}
	g_ticks = 0;
	g_stage = Stage::Listing;
}

void stepListing() {
	// Nothing else ticks the browser on this headless harness: each step is its tick.
	if (Browser::isListingInProgress()) {
		loadSongUI.graphicsRoutine();
		waitedTooLong("RS-LOADED", "the load browser's listing never finished");
		return;
	}
	loadSongUI.beginLoad();
	g_ticks = 0;
	g_stage = Stage::Loading;
}

void stepLoading() {
	if (loadSongUI.loadInFlight()) {
		waitedTooLong("RS-LOADED", "the load never finished");
		return;
	}
	const std::string want = expectedName(g_songPath);
	const std::string got = currentSong->project.metadata.name.c_str();
	const char* view = getRootUI() == &sessionView ? "session" : getRootUI() == &arrangerView ? "arranger" : "other";
	// The session clips set to play, and the notes they hold: a song whose clips hold nothing plays silence.
	int32_t playingClips = 0;
	int32_t notes = 0;
	for (int32_t c = 0; c < std::ssize(currentSong->live.clips.session_clips); c++) {
		Clip* clip = currentSong->live.clips.session_clips[c];
		if (!clip->activeIfNoSolo) {
			continue;
		}
		playingClips++;
		if (clip->type == ClipType::INSTRUMENT) {
			for (const NoteRow& row : static_cast<InstrumentClip*>(clip)->noteRows) {
				notes += static_cast<int32_t>(std::ssize(row.notes));
			}
		}
	}
	printf("RS-LOADED name=%s outputs=%d view=%s playing_clips=%d notes=%d\n", got.c_str(),
	       static_cast<int>(currentSong->getNumOutputs()), view, playingClips, notes);
	if (got != want) {
		printf("RS-LOADED listing_failures=%d last_listing_error=%d\n", static_cast<int>(Browser::listingFailures()),
		       static_cast<int>(Browser::lastListingFailure()));
		// Either the load gave up (the name still carries the extension stepOpen gave it), or the browser did not
		// find the file and loaded the nearest one instead.
		fail("RS-LOADED", "the song asked for is not the song loaded");
		g_stage = Stage::Report;
		return;
	}
	g_stage = Stage::Play;
}

void stepPlay() {
	g_renderBase = deluge_host_render_count();
	for (uint8_t tier = 0; tier < 3; tier++) {
		g_voiceBase[tier] = deluge_sim_voice_render_count(tier);
	}
	g_waitBase = deluge_sim_underrun_wait_count();
	g_unassignBase = deluge_sim_underrun_unassign_count();
	g_audioSyncBase = deluge_tier_sync_audio_total();
	g_readsBase = deluge_sim_card_reads();
	deluge_streaming_set_fill_latency_ms(g_model.delayMs);
	deluge_streaming_set_fill_read_latency_us(g_model.readUs);
	g_inBurst = false;
	if (g_stereo) {
		// The app reads its sockets in its input routine, on a UI timer the harness does not tick; read them now, so
		// the headphones plugged in at the start count from the first captured block.
		inputRoutine();
	}
	deluge_host_capture_start();
	playbackHandler.playButtonPressed(kInternalButtonPressLatency);
	g_wasPlaying = false;
	g_ticks = 0;
	g_stage = Stage::Playing;
}

void stepPlaying() {
	const bool playing = playbackHandler.isEitherClockActive();
	if (g_wasPlaying && !playing) {
		if (g_stops++ == 0) {
			g_firstStop = static_cast<int64_t>(deluge_host_render_count() - g_renderBase);
		}
	}
	g_wasPlaying = playing;
	g_direnessMax = std::max(g_direnessMax, static_cast<int32_t>(AudioEngine::cpuDireness));
	g_voicesMax = std::max(g_voicesMax, static_cast<int32_t>(AudioEngine::getNumVoices()));
	if (AudioEngine::renderInStereo != g_stereo) {
		g_wrongWidthTicks++;
	}
	g_blocksRendered = deluge_host_render_count() - g_renderBase;
	while (g_switchesDone < g_switchAt.size() && g_blocksRendered >= g_switchAt[g_switchesDone]) {
		switchDelays(g_blocksRendered);
		g_switchesDone++;
	}
	while (g_modFxSwitchesDone < g_modFxSwitchAt.size()
	       && g_blocksRendered >= g_modFxSwitchAt[g_modFxSwitchesDone].block) {
		switchModFx(g_blocksRendered, g_modFxSwitchAt[g_modFxSwitchesDone].type);
		g_modFxSwitchesDone++;
	}
	while (g_modFxClearsDone < g_modFxClearAt.size() && g_blocksRendered >= g_modFxClearAt[g_modFxClearsDone]) {
		clearModFx(g_blocksRendered);
		g_modFxClearsDone++;
	}
	const bool burst = g_model.burstBlocks != 0 && g_blocksRendered >= g_model.burstStart
	                   && g_blocksRendered < g_model.burstStart + g_model.burstBlocks;
	if (burst != g_inBurst) {
		deluge_streaming_set_fill_latency_ms(burst ? g_model.burstMs : g_model.delayMs);
		g_inBurst = burst;
	}
	if (g_blocksRendered >= g_blocksWanted) {
		g_reads = deluge_sim_card_reads() - g_readsBase;
		g_captured = deluge_host_capture_stop();
		if (playing) {
			playbackHandler.playButtonPressed(kInternalButtonPressLatency);
		}
		g_stage = Stage::Report;
		return;
	}
	// A block takes about 2.9 virtual milliseconds and a tick about two, so twice the blocks in ticks is ample.
	if (++g_ticks > static_cast<int64_t>(g_blocksWanted) * 2 + 1000) {
		fail("RS-RENDER", "the render stopped short of the blocks asked for");
		g_captured = deluge_host_capture_stop();
		g_stage = Stage::Report;
	}
}

void report() {
	deluge_streaming_set_fill_latency_ms(0);
	deluge_streaming_set_fill_read_latency_us(0);
	printf("RS-MODEL fill_delay_ms=%u fill_read_us=%u burst=%llu:%llu:%u\n", g_model.delayMs, g_model.readUs,
	       static_cast<unsigned long long>(g_model.burstStart), static_cast<unsigned long long>(g_model.burstBlocks),
	       g_model.burstMs);
	printf("RS-RENDER blocks=%llu stops=%d first_stop=%lld voices_max=%d direness_max=%d\n",
	       static_cast<unsigned long long>(g_blocksRendered), g_stops, static_cast<long long>(g_firstStop), g_voicesMax,
	       g_direnessMax);
	if (g_stops != 0) {
		fail("RS-RENDER", "playback stopped before the blocks asked for were rendered");
	}
	uint64_t voices[3];
	for (uint8_t tier = 0; tier < 3; tier++) {
		voices[tier] = deluge_sim_voice_render_count(tier) - g_voiceBase[tier];
	}
	printf("RS-VOICES audio=%llu storage=%llu interaction=%llu\n",
	       static_cast<unsigned long long>(voices[DELUGE_TIER_AUDIO]),
	       static_cast<unsigned long long>(voices[DELUGE_TIER_STORAGE]),
	       static_cast<unsigned long long>(voices[DELUGE_TIER_INTERACTION]));
	const uint32_t audioSync = deluge_tier_sync_audio_total() - g_audioSyncBase;
	printf("RS-TIER-SYNC-AUDIO total=%u\n", audioSync);
	if (audioSync != 0) {
		fail("RS-TIER-SYNC-AUDIO", "the render made a synchronous card call");
	}
	// Card reads while playing: sample data streamed from the card, rather than found resident.
	printf("RS-STREAM reads=%llu\n", static_cast<unsigned long long>(g_reads));
	printf("RS-UNDERRUN wait=%llu unassign=%llu\n",
	       static_cast<unsigned long long>(deluge_sim_underrun_wait_count() - g_waitBase),
	       static_cast<unsigned long long>(deluge_sim_underrun_unassign_count() - g_unassignBase));
	if (!g_switchAt.empty()) {
		printf("RS-DELAY-SWITCH switches=%zu asked=%zu tracks=%d\n", g_switchesDone, g_switchAt.size(),
		       g_tracksSwitched);
		if (g_switchesDone != g_switchAt.size() || g_tracksSwitched != static_cast<int32_t>(g_switchAt.size())) {
			fail("RS-DELAY-SWITCH", "a switch asked for did not switch exactly one audio track's delay");
		}
	}
	if (!g_modFxSwitchAt.empty()) {
		printf("RS-MODFX-SWITCH applied=%d asked=%zu types=%s ring=%d noring=%d\n", g_tracksModFxSwitched,
		       g_modFxSwitchAt.size(), g_modFxSwitchedTypes.empty() ? "-" : g_modFxSwitchedTypes.c_str(),
		       g_modFxSwitchedRing, g_modFxSwitchedNoRing);
		if (g_modFxSwitchesDone != g_modFxSwitchAt.size()
		    || g_tracksModFxSwitched != static_cast<int32_t>(g_modFxSwitchAt.size())) {
			fail("RS-MODFX-SWITCH", "a switch asked for did not switch exactly one audio track's mod FX");
		}
	}
	if (!g_modFxClearAt.empty()) {
		printf("RS-MODFX-CLEAR applied=%d asked=%zu ring=%d noring=%d grain=%d\n", g_tracksModFxCleared,
		       g_modFxClearAt.size(), g_modFxClearedRing, g_modFxClearedNoRing, g_modFxClearedGrain);
		if (g_modFxClearsDone != g_modFxClearAt.size()
		    || g_tracksModFxCleared != static_cast<int32_t>(g_modFxClearAt.size())) {
			fail("RS-MODFX-CLEAR", "a clear asked for did not clear exactly one audio track's mod FX");
		}
	}
	printf("RS-CAPTURE frames=%llu stereo=%d\n", static_cast<unsigned long long>(g_captured), g_stereo ? 1 : 0);
	if (g_wrongWidthTicks != 0) {
		fail("RS-CAPTURE", g_stereo ? "the app rendered in mono with headphones plugged in"
		                            : "the app rendered in stereo with nothing plugged in");
	}
	printf("RS-RESULT failures=%d\n", g_failures);
	g_stage = Stage::Done;
}

} // namespace

extern "C" {

void deluge_scenario_render_song_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Open;
	g_failures = 0;
	g_ticks = 0;
	g_stops = 0;
	g_firstStop = -1;
	g_direnessMax = 0;
	g_voicesMax = 0;
	g_wrongWidthTicks = 0;
	g_captured = 0;
	// Headphones plugged in from boot make the app render in stereo, as the device does through them; otherwise it
	// renders in mono, as through the speaker.
	const char* stereo = std::getenv("RENDER_STEREO");
	g_stereo = stereo != nullptr && std::strcmp(stereo, "1") == 0;
	deluge_host_headphones(g_stereo);
	const char* song = std::getenv("RENDER_SONG");
	g_songPath = song != nullptr ? song : "";
	g_model = StorageModel{};
	g_model.delayMs = envU32("RENDER_FILL_DELAY_MS");
	g_model.readUs = envU32("RENDER_FILL_READ_US");
	readBurst(g_model);
	readSwitches();
	g_switchesDone = 0;
	g_tracksSwitched = 0;
	readModFxSwitches();
	g_modFxSwitchesDone = 0;
	g_tracksModFxSwitched = 0;
	g_modFxSwitchedTypes.clear();
	g_modFxSwitchedRing = 0;
	g_modFxSwitchedNoRing = 0;
	readModFxClears();
	g_modFxClearsDone = 0;
	g_tracksModFxCleared = 0;
	g_modFxClearedRing = 0;
	g_modFxClearedNoRing = 0;
	g_modFxClearedGrain = 0;
	if (const char* blocks = std::getenv("RENDER_BLOCKS")) {
		g_blocksWanted = std::strtoull(blocks, nullptr, 10);
	}
	if (g_songPath.empty()) {
		fail("RS-LOADED", "RENDER_SONG names no song");
		g_stage = Stage::Report;
	}
}

bool deluge_scenario_render_song_complete(void) {
	return g_stage == Stage::Done;
}

void deluge_scenario_render_song_dispatch_step(void) {
	switch (g_stage) {
	case Stage::Open:
		stepOpen();
		break;
	case Stage::Listing:
		stepListing();
		break;
	case Stage::Loading:
		stepLoading();
		break;
	case Stage::Play:
		stepPlay();
		break;
	case Stage::Playing:
		stepPlaying();
		break;
	case Stage::Report:
		report();
		break;
	case Stage::Done:
		break;
	}
}

bool deluge_scenario_render_song_step_done(void) {
	return true;
}

int32_t deluge_scenario_render_song_failures(void) {
	return g_failures;
}

} // extern "C"
