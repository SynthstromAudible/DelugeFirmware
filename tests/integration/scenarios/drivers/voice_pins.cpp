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

// Which voice each cull, steal and admit takes, run as the voice_pins scenario. Five new synths in a song of the
// driver's own play fixed note scripts; after each step the driver lists every sounding voice (sound, place in its
// list, note, envelope stage, cull rating), and compares the whole listing with voice_pins_records.h. The cases: the
// backstop's kill, terminate and force-release across sounds; the budget's admit; a sound's voice limit; MONO and
// LEGATO reuse; the steal when no voice can be allocated; the newest voice, as the play-position readers pick it.
//
// Gate voice-pins:
//     each step's voices must match the record: which voice each cull, steal and admit takes, across sounds

#include "definitions_cxx.hpp"
#include "extern.h"
#include "instruments/deluge/words.h"
#include "libdeluge/app.h"
#include "memory/general_memory_allocator.h"
#include "memory/heaps.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/setting_edit.h"
#include "model/fx/chain_params.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "model/voice/sound_voice.h"
#include "modulation/params/param_manager.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_instrument.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/drivers/voice_pins_records.h"
#include "serialization/value.h"
#include "storage/storage_manager.h"
#include "support/golden_update.h"

#include <array>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using deluge::scenarios::driver::Io;
using deluge::serialization::Value;
namespace golden = deluge::test::golden;

constexpr std::string_view kHeader = "tests/integration/scenarios/drivers/voice_pins_records.h";
constexpr uint32_t kBlockFrames = 128;
constexpr size_t kNumSynths = 5;

/// One of the driver's synths: its name in the record, the instrument, and the clip it plays through (its arp
/// settings and its param manager).
struct Synth {
	std::string name;
	SoundInstrument* sound{};
	InstrumentClip* clip{};
};

std::array<Synth, kNumSynths> g_synths;
std::vector<std::string> g_lines;

/// A song for the synths to live in.
Song* freshSong() {
	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	deluge::model::initGlobalParams(&song->paramManager);
	return song;
}

void noteOn(size_t s, int32_t note) {
	Synth& synth = g_synths[s];
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* ms = setupModelStackWithThreeMainThingsButNoNoteRow(
	    memory, currentSong, synth.sound, synth.clip, &synth.clip->paramManager);
	synth.sound->Sound::noteOn(ms, &synth.sound->arpeggiator, note, zeroMPEValues);
}

void noteOff(size_t s, int32_t note) {
	Synth& synth = g_synths[s];
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* ms = setupModelStackWithThreeMainThingsButNoNoteRow(
	    memory, currentSong, synth.sound, synth.clip, &synth.clip->paramManager);
	synth.sound->Sound::noteOff(ms, &synth.sound->arpeggiator, note);
}

void renderBlocks(int32_t count) {
	static DelugeStereoSample silence[kBlockFrames]{};
	static DelugeStereoSample output[kBlockFrames]{};
	for (int32_t block = 0; block < count; block++) {
		deluge_app_render(silence, output, kBlockFrames);
	}
}

void setting(size_t s, std::string_view path, std::string_view text) {
	deluge::edits::settingFor(*g_synths[s].sound, path, Value::makeScalar(std::string(text)));
}

void silenceAll() {
	for (Synth& synth : g_synths) {
		synth.sound->killAllVoices();
	}
}

/// Every sounding voice after step @p step of case @p kase, one line each.
void listVoices(std::string_view kase, int32_t step) {
	for (const Synth& synth : g_synths) {
		const auto& voices = synth.sound->voices();
		for (size_t i = 0; i < voices.size(); i++) {
			const SoundVoice& voice = *voices[i];
			g_lines.push_back(
			    std::format("{} {} {} {} {} {} {:08x}", kase, step, synth.name, i, voice.kernel.note_code(),
			                std::to_underlying(voice.kernel.envelope(0).stage()), voice.priority_rating()));
		}
	}
}

/// Give @p instrument a clip of its own, the way the new-track gesture does (InstrumentClip::setAudioInstrument, then
/// setActiveClip), without putting the clip in the song: a synth reads its arp settings from its active clip. Leaked,
/// as the harness fixtures are.
InstrumentClip* attachClip(Instrument* instrument) {
	void* memory = deluge::memory::alloc_fast(sizeof(InstrumentClip));
	if (memory == nullptr) {
		return nullptr;
	}
	auto* clip = new (memory) InstrumentClip(*currentSong);
	if (clip->setAudioInstrument(instrument, currentSong, true, nullptr) != Error::NONE) {
		return nullptr;
	}
	alignas(ModelStackWithAutoParam) char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithSong(modelStackMemory, currentSong)->addTimelineCounter(clip);
	instrument->setActiveClip(modelStack);
	return clip;
}

/// Two notes on each synth, a block apart, so each voice entered at its own time and the counts differ by sound
/// (the third synth plays one).
void playScript() {
	silenceAll();
	for (size_t s = 0; s < kNumSynths; s++) {
		noteOn(s, static_cast<int32_t>(60 + s));
		renderBlocks(1);
		if (s != 2) {
			noteOn(s, static_cast<int32_t>(67 + s));
			renderBlocks(1);
		}
	}
}

void cullCase(std::string_view kase, void (*cull)(size_t), int32_t times) {
	playScript();
	if (kase == "force-release") {
		// One sound's notes already released, so the force-release meets voices in their release stage
		noteOff(4, 64);
		noteOff(4, 71);
		renderBlocks(1);
	}
	listVoices(kase, 0);
	// Two within one block first, while every sound has voices: the second meets the first's voice still fading,
	// which kill takes again and terminate and force-release pass over
	cull(kBlockFrames);
	listVoices(kase, 1);
	cull(kBlockFrames);
	listVoices(kase, 2);
	for (int32_t step = 3; step <= times + 2; step++) {
		renderBlocks(1); // a voice the cull stopped ends in the block after it, and is freed there
		cull(kBlockFrames);
		listVoices(kase, step);
	}
	renderBlocks(1);
}

void admitCase() {
	playScript();
	noteOff(4, 64);
	noteOff(4, 71);
	listVoices("admit", 0);
	// A budget the held load already overfills: the next note-on's admit takes the cull's pick first
	const deluge::audio::VoiceBudget saved = AudioEngine::voiceBudget;
	AudioEngine::voiceBudget.configure(400'000'000, 1);
	AudioEngine::voiceBudget.end_block(AudioEngine::voiceBudget.capacity() + 1);
	noteOn(4, 72);
	AudioEngine::voiceBudget = saved;
	listVoices("admit", 1);
}

void polyLimitCase() {
	silenceAll();
	setting(0, "maxVoices", "3");
	for (int32_t step = 1; step <= 5; step++) {
		noteOn(0, 59 + step);
		renderBlocks(1);
		listVoices("poly-limit", step);
	}
	setting(0, "maxVoices", "8");
}

void reuseCase(std::string_view kase, PolyphonyMode mode) {
	silenceAll();
	setting(1, "polyphonic", deluge::instrument::to_string(mode));
	const int32_t notes[] = {60, 64, 67};
	int32_t step = 0;
	for (const int32_t note : notes) {
		noteOn(1, note);
		renderBlocks(1);
		listVoices(kase, ++step);
	}
	for (int32_t i = 2; i >= 0; i--) {
		noteOff(1, notes[i]);
		renderBlocks(1);
		listVoices(kase, ++step);
	}
	setting(1, "polyphonic", deluge::instrument::to_string(PolyphonyMode::POLY));
}

/// The note-on that finds no memory for a voice starts its note on one of the sound's own voices.
bool outOfMemoryCase(Io io) {
	silenceAll();
	noteOn(3, 60);
	renderBlocks(1);
	noteOn(3, 62);
	renderBlocks(1);
	listVoices("oom", 0);
	std::vector<AudioEngine::VoicePool::pointer_type> held;
	while (AudioEngine::VoicePool::get().size() > 0) {
		held.push_back(AudioEngine::VoicePool::get().acquire(*g_synths[3].sound));
	}
	deluge::memory::failNextAllocationOfSize(sizeof(SoundVoice));
	noteOn(3, 64);
	const bool injected = !deluge::memory::allocationFailurePending();
	deluge::memory::failNextAllocationOfSize(0);
	held.clear();
	listVoices("oom", 1);
	if (!injected) {
		std::fprintf(io.err, "voice_pins: the out-of-memory note-on allocated no voice\n");
	}
	return injected;
}

/// The newest voice, as the slicer's and the shell host's play-position reads pick it, between culls.
void readersCase() {
	playScript();
	for (int32_t step = 0; step <= 3; step++) {
		if (step > 0) {
			AudioEngine::terminateOneVoice(kBlockFrames);
			renderBlocks(1);
		}
		const Synth* newestSynth = nullptr;
		const SoundVoice* newest = nullptr;
		for (const Synth& synth : g_synths) {
			for (const auto& voice : synth.sound->voices()) {
				if (newest == nullptr || voice->order_sounded > newest->order_sounded) {
					newest = voice.get();
					newestSynth = &synth;
				}
			}
		}
		g_lines.push_back(newest == nullptr
		                      ? std::format("readers {} none", step)
		                      : std::format("readers {} {} {}", step, newestSynth->name, newest->kernel.note_code()));
	}
}

int voice_pins_main(int /*argc*/, char** /*argv*/, Io io) {
	Song* song = freshSong();
	currentSong = song;
	for (size_t s = 0; s < kNumSynths; s++) {
		Synth& synth = g_synths[s];
		synth.name = std::format("s{}", s);
		ParamManagerForTimeline params;
		Instrument* instrument = StorageManager::createNewInstrument(OutputType::SYNTH, &params);
		if (instrument == nullptr) {
			std::fprintf(io.err, "voice_pins: no new synth\n");
			return 1;
		}
		synth.sound = static_cast<SoundInstrument*>(instrument);
		song->live.param_backup.backUp(*synth.sound, nullptr, &params, true);
		synth.clip = attachClip(synth.sound);
		if (synth.clip == nullptr) {
			std::fprintf(io.err, "voice_pins: no clip for a synth\n");
			return 1;
		}
		song->addOutput(synth.sound);
	}

	cullCase("kill", &AudioEngine::killOneVoice, 6);
	cullCase("terminate", &AudioEngine::terminateOneVoice, 8);
	cullCase("force-release", &AudioEngine::forceReleaseOneVoice, 6);
	admitCase();
	polyLimitCase();
	reuseCase("mono", PolyphonyMode::MONO);
	reuseCase("legato", PolyphonyMode::LEGATO);
	const bool oom = outOfMemoryCase(io);
	readersCase();
	silenceAll();

	if (golden::updating()) {
		for (const std::string& line : g_lines) {
			golden::record(kHeader, "kRecords", std::format("    \"{}\",", line));
		}
		return oom ? 0 : 1;
	}
	const auto& records = voice_pins_records::kRecords;
	for (size_t i = 0; i < std::max(g_lines.size(), records.size()); i++) {
		const std::string_view got = i < g_lines.size() ? std::string_view{g_lines[i]} : "(none)";
		const std::string_view want = i < records.size() ? records[i] : "(none)";
		if (got != want) {
			std::fprintf(io.err, "voice_pins: line %zu: got \"%.*s\", recorded \"%.*s\"\n", i,
			             static_cast<int>(got.size()), got.data(), static_cast<int>(want.size()), want.data());
			return 1;
		}
	}
	std::fprintf(io.out, "voice_pins: %zu lines match\n", g_lines.size());
	return oom ? 0 : 1;
}

} // namespace

DELUGE_DRIVER_SCENARIO(voice_pins, voice_pins_main)
