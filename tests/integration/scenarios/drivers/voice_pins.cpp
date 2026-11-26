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

// Which voice each cull, steal and admit takes, run as the voice_pins scenario. The driver builds a song of five new
// synths and plays fixed note scripts on them; after each step the driver lists every sounding voice (sound, place in
// its list, note, envelope stage, cull rating), and compares the whole listing with voice_pins_records.h. The cases:
// the backstop's kill, terminate and force-release across sounds; the budget's admit; a sound's voice limit; MONO and
// LEGATO reuse; the steal when no voice can be allocated; the newest voice, as the play-position readers pick it.
//
// Gate voice-pins:
//     each step's voices must match the record: which voice each cull, steal and admit takes, across sounds

#include "definitions_cxx.hpp"
#include "extern.h"
#include "instruments/classic/words.h"
#include "libdeluge/app.h"
#include "memory/general_memory_allocator.h"
#include "memory/heaps.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/setting_edit.h"
#include "model/fx/chain_params.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "model/voice/voice_part.h"
#include "modulation/params/param_manager.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_instrument.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/drivers/voice_pins_records.h"
#include "scenarios/support/render_block.h"
#include "serialization/value.h"
#include "storage/storage_manager.h"
#include "storage/tree_document.h"
#include "support/golden_update.h"
#include "synth/voice/voice_host.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using deluge::scenarios::driver::Io;
using deluge::serialization::Value;
using deluge::synth::NoteFacts;
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

/// The model stack a note on @p sound through @p clip gets, as an audition builds it: no note row.
ModelStackWithThreeMainThings* stackFor(char* memory, Sound& sound, InstrumentClip& clip) {
	return setupModelStackWithThreeMainThingsButNoNoteRow(memory, currentSong, &sound, &clip, &clip.paramManager);
}

void noteOn(Sound& sound, InstrumentClip& clip, ArpeggiatorBase& arpeggiator, int32_t note) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	sound.Sound::noteOn(stackFor(memory, sound, clip), &arpeggiator, note, zeroMPEValues);
}

void noteOn(size_t s, int32_t note) {
	Synth& synth = g_synths[s];
	noteOn(*synth.sound, *synth.clip, synth.sound->arpeggiator, note);
}

void noteOff(size_t s, int32_t note) {
	Synth& synth = g_synths[s];
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	synth.sound->Sound::noteOff(stackFor(memory, *synth.sound, *synth.clip), &synth.sound->arpeggiator, note);
}

void renderBlocks(int32_t count) {
	static std::array<DelugeStereoSample, kBlockFrames> silence{};
	static std::array<DelugeStereoSample, kBlockFrames> output{};
	for (int32_t block = 0; block < count; block++) {
		scenarios::renderCodecBlock(silence.data(), output.data(), kBlockFrames);
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

/// @p voice's rating in the cull order, for @p sound as it is now.
uint32_t ratingOf(const Sound& sound, const SoundVoice& voice) {
	return voice.kernel.priority_rating(sound.config().voicing.priority, sound.numActiveVoices());
}

/// Every sounding voice after step @p step of case @p kase, one line each.
void listVoices(std::string_view kase, int32_t step) {
	for (const Synth& synth : g_synths) {
		synth.sound->voicePool().read([&](const SoundVoiceHost::List& voices) {
			for (size_t i = 0; i < voices.size(); i++) {
				const SoundVoice& voice = *voices[i];
				g_lines.push_back(
				    std::format("{} {} {} {} {} {} {:08x}", kase, step, synth.name, i, voice.kernel.note_code(),
				                std::to_underlying(voice.kernel.envelope(0).stage()), ratingOf(*synth.sound, voice)));
			}
		});
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

/// One voice on each synth, then two culls within one block: the second passes over the first's still-fading voice,
/// so it takes another sound's (the cull compares sounds by the voices each would give).
void acrossCase(std::string_view kase, void (*cull)(size_t)) {
	silenceAll();
	for (size_t s = 0; s < kNumSynths; s++) {
		noteOn(s, static_cast<int32_t>(60 + s));
		renderBlocks(1);
	}
	listVoices(kase, 0);
	cull(kBlockFrames);
	listVoices(kase, 1);
	cull(kBlockFrames);
	listVoices(kase, 2);
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
	setting(1, "polyphonic", deluge::instruments::classic::to_string(mode));
	constexpr std::array<int32_t, 3> notes{60, 64, 67};
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
	setting(1, "polyphonic", deluge::instruments::classic::to_string(PolyphonyMode::POLY));
}

/// The note-on that finds no memory for a voice starts its note on one of the sound's own voices.
bool outOfMemoryCase(Io io) {
	silenceAll();
	noteOn(3, 60);
	renderBlocks(1);
	noteOn(3, 62);
	renderBlocks(1);
	listVoices("oom", 0);
	const SoundVoiceHost host{g_synths[3].sound};
	std::vector<SoundVoice*> held;
	while (AudioEngine::VoicePool::get().size() > 0) {
		held.push_back(host.acquire());
	}
	deluge::memory::failNextAllocationOfSize(sizeof(SoundVoice));
	noteOn(3, 64);
	const bool injected = !deluge::memory::allocationFailurePending();
	deluge::memory::failNextAllocationOfSize(0);
	for (SoundVoice* voice : held) {
		host.release(*voice);
	}
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
			synth.sound->voicePool().read([&](const SoundVoiceHost::List& voices) {
				for (const SoundVoice* voice : voices) {
					if (newest == nullptr || voice->order > newest->order) {
						newest = voice;
						newestSynth = &synth;
					}
				}
			});
		}
		g_lines.push_back(newest == nullptr
		                      ? std::format("readers {} none", step)
		                      : std::format("readers {} {} {}", step, newestSynth->name, newest->kernel.note_code()));
	}
}

/// The facts a note-on reads of @p sound beside its voices, as it answers them now through a note-on's model stack.
/// Read here from the sound's own queries, not through Sound::noteFacts: the pin is a second reader that the note
/// path's copy has to agree with.
NoteFacts factsOf(Sound& sound, InstrumentClip& clip) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithSoundFlags* modelStack = stackFor(memory, sound, clip)->addSoundFlags();
	const ArpeggiatorSettings* arp = sound.getArpSettings();
	NoteFacts facts{.note_tails_allowed = sound.allowNoteTails(modelStack, true),
	                .fm = sound.getSynthMode() == SynthMode::FM,
	                .note_tails_counting_loops = sound.allowNoteTails(modelStack),
	                .arpeggiating = arp != nullptr && arp->mode != ArpMode::OFF};
	const PolyphonyMode mode = sound.config().voicing.polyphony;
	facts.falls_back = (mode == PolyphonyMode::LEGATO || mode == PolyphonyMode::MONO) && !sound.isDrum()
	                   && facts.note_tails_counting_loops;
	for (int32_t s = 0; s < kNumSources; s++) {
		facts.source_active_now[s] = sound.isSourceActiveCurrently(s, &clip.paramManager);
	}
	return facts;
}

/// Every voice of @p sound as the record lists it, for comparing before and after a read.
std::string voicesOf(const Sound& sound) {
	return sound.voicePool().read([&](const SoundVoiceHost::List& voices) {
		std::string text;
		for (const SoundVoice* voice : voices) {
			text += std::format("{} {} {:08x} {};", voice->kernel.note_code(),
			                    std::to_underlying(voice->kernel.envelope(0).stage()), ratingOf(sound, *voice),
			                    int{voice->just_created});
		}
		return text;
	});
}

/// A note-on reads its facts between its voice walk's edits, and reading them before the walk must answer the same.
/// So, for each render preset in each polyphony mode, across two note-ons: reading them twice gives the same answer
/// and changes neither the record nor any voice, and the note-on's own voice edits leave them as they were. Each preset
/// is loaded once, for all three modes. Returns the presets checked, or -1 after printing the first that breaks this.
int32_t factsCase(Io io) {
	namespace fs = std::filesystem;
	const fs::path root = "tests/golden/render/presets";
	if (!fs::is_directory(root)) {
		std::fprintf(io.err, "voice_pins: no %s here\n", root.c_str());
		return -1;
	}
	std::vector<fs::path> paths;
	for (const auto& entry : fs::recursive_directory_iterator(root)) {
		const std::string ext = entry.path().extension().string();
		if (entry.is_regular_file() && (ext == ".xml" || ext == ".XML" || ext == ".json")) {
			paths.push_back(entry.path());
		}
	}
	std::ranges::sort(paths);
	int32_t checked = 0;
	for (const fs::path& path : paths) {
		std::ifstream file(path, std::ios::binary);
		const std::string bytes{std::istreambuf_iterator<char>(file), {}};
		const char* firstTag = nullptr;
		const char* altTag = nullptr;
		StorageManager::instrumentDocumentTags(OutputType::SYNTH, &firstTag, &altTag);
		deluge::storage::TreeDocument document;
		if (document.open(std::span{reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()},
		                  path.extension() == ".json", firstTag, altTag)
		    != Error::NONE) {
			continue; // not a synth preset
		}
		Instrument* instrument = nullptr;
		std::string name = "PRESET";
		std::string dir = "SYNTHS";
		const Error parsed = StorageManager::parseInstrumentDocument(document, currentSong, nullptr, OutputType::SYNTH,
		                                                             false, &instrument, &name, &dir);
		document.release();
		if (parsed != Error::NONE || instrument == nullptr) {
			continue;
		}
		auto& sound = *static_cast<SoundInstrument*>(instrument);
		InstrumentClip* clip = attachClip(&sound);
		if (clip == nullptr) {
			std::fprintf(io.err, "voice_pins: no clip for %s after %d presets\n", path.c_str(), checked);
			return -1;
		}
		for (const PolyphonyMode mode : {PolyphonyMode::MONO, PolyphonyMode::LEGATO, PolyphonyMode::POLY}) {
			sound.killAllVoices();
			deluge::edits::settingFor(sound, "polyphonic",
			                          Value::makeScalar(std::string(deluge::instruments::classic::to_string(mode))));
			for (const int32_t note : {60, 64}) {
				const deluge::instruments::classic::SoundState record = sound.state();
				const std::string voices = voicesOf(sound);
				const NoteFacts before = factsOf(sound, *clip);
				const NoteFacts again = factsOf(sound, *clip);
				const bool unchanged = record == sound.state() && voices == voicesOf(sound);
				noteOn(sound, *clip, sound.arpeggiator, note);
				const NoteFacts after = factsOf(sound, *clip);
				if (before != again || !unchanged || before != after) {
					std::fprintf(io.err, "voice_pins: %s, %s, note %d: facts %s\n", path.c_str(),
					             std::string(deluge::instruments::classic::to_string(mode)).c_str(), note,
					             before != again ? "differ when read twice"
					             : !unchanged    ? "reading them changed the sound"
					                             : "changed across the note-on's voice edits");
					return -1;
				}
			}
		}
		// The preset's param collections went to its clip, out of the song's backup, so they are freed here, or the
		// presets would not fit the heap; the song frees the instrument at a later block, and the clip, which the
		// instrument still names as its active clip until then, stays allocated
		sound.killAllVoices();
		clip->paramManager.destructAndForgetParamCollections();
		currentSong->deleteOutput(instrument);
		checked++;
	}
	return checked;
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
	acrossCase("terminate-across", &AudioEngine::terminateOneVoice);
	acrossCase("force-release-across", &AudioEngine::forceReleaseOneVoice);
	admitCase();
	polyLimitCase();
	reuseCase("mono", PolyphonyMode::MONO);
	reuseCase("legato", PolyphonyMode::LEGATO);
	const bool oom = outOfMemoryCase(io);
	readersCase();
	silenceAll();
	const int32_t checked = factsCase(io);
	const bool facts = checked > 0;
	g_lines.push_back(std::format("facts mono legato poly presets {}", checked));

	if (golden::updating()) {
		for (const std::string& line : g_lines) {
			golden::record(kHeader, "kRecords", std::format("    \"{}\",", line));
		}
		return oom && facts ? 0 : 1;
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
	return oom && facts ? 0 : 1;
}

} // namespace

DELUGE_DRIVER_SCENARIO(voice_pins, voice_pins_main)
