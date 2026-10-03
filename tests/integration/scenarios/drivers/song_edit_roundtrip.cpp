// Edit-aware round-trip driver (Phase 2 / DOM-live-authoritative automation): load a song, apply a DOM-first
// edit to a song-global unpatched param, save, reload, and assert the edit persisted through the DOM path.
//
//   song_edit_roundtrip <song.XML>
//
// This exercises the whole DOM-authoritative edit loop off-hardware — the piece the golden load->save harness
// structurally cannot see (it never edits). We:
//   1. load(original)                                             -> song + populated DOM store
//   2. setUnpatchedValueDomFirst(store, p, SENTINEL, engineParam) -> write DOM store lane + reproject engine
//   3. save                                                       -> gen1
//   4. load(gen1)                                                 -> reload
//   5. require reloaded store[p] == SENTINEL AND reloaded engine[p] == SENTINEL
//
// It passes today with save reading the engine (the DOM-first edit reprojects into it) and will keep passing
// after 5c-3 flips save to read the store — so it is the safety net for that flip.
//
// Exit: 0 = edit persisted; 1 = load error; 2 = usage/IO; 3 = edit not applied / not persisted.

#include "deluge-project/param_automation.h"
#include "deluge-project/param_edit.h"
#include "deluge-project/serialization.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/clip.h"
#include "model/clip/clip_array.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/global_effectable/global_effectable.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"
#include "util/phase_increment_fine_tuner.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "scenarios/drivers/driver.h"
#include "scenarios/drivers/tree_load.h"
#include "storage/parse_xml.h"
#include "storage/value_tree_deserializer.h"
#include <optional>
#include <string_view>

namespace {

using deluge::scenarios::driver::Io;

/// Where this driver's prints go: what its process wrote to stdout and stderr.
Io io{};

namespace params = deluge::modulation::params;

/// A tuner set up for @p cents the way Sound does (a cent is 42949672 in the tuner's detune scale).
PhaseIncrementFineTuner centsTuner(int32_t cents) {
	PhaseIncrementFineTuner tuner;
	tuner.setup(cents * 42949672);
	return tuner;
}

/// Whether two tuners detune a phase increment identically.
bool sameTuning(PhaseIncrementFineTuner a, PhaseIncrementFineTuner b) {
	constexpr int32_t kProbe = 1 << 24;
	return a.detune(kProbe) == b.detune(kProbe);
}

/// Each bound Sound of @p song: a synth instrument's, or a kit's sound drum's, live or hibernating.
template <typename F>
void forEachBoundSound(Song& song, F&& f) {
	for (Output* first :
	     {song.project.media.first_output, static_cast<Output*>(song.project.media.first_hibernating_instrument)}) {
		for (Output* o = first; o != nullptr; o = o->next) {
			if (o->type == OutputType::SYNTH) {
				Sound* sound = static_cast<SoundInstrument*>(o);
				if (sound->docSound_ != nullptr) {
					f(*sound);
				}
			}
			else if (o->type == OutputType::KIT) {
				for (Drum* d = static_cast<Kit*>(o)->firstDrum; d != nullptr; d = d->next) {
					if (d->type == DrumType::SOUND) {
						Sound* sound = static_cast<SoundDrum*>(d);
						if (sound->docSound_ != nullptr) {
							f(*sound);
						}
					}
				}
			}
		}
	}
}

/// M4b: every bound Sound's snapshot must equal the projection of its DocSound record, and each snapshot setter must
/// write the record and reproject. @return the number of mismatches, each printed under @p label.
int32_t checkSoundSnapshots(Song& song, const char* label) {
	int32_t sounds = 0, bad = 0;
	Sound* probe = nullptr;
	forEachBoundSound(song, [&](Sound& sound) {
		const deluge::project::DocSound& d = *sound.docSound_;
		sounds++;
		probe = (probe != nullptr) ? probe : &sound;
		if (sound.getTranspose() != d.transpose) {
			std::fprintf(io.err, "M4b FAIL %s: transpose snapshot=%d record=%d\n", label, sound.getTranspose(),
			             d.transpose);
			bad++;
		}
		const deluge::snapshot::SoundSnapshot& snap = sound.snapshot();
		auto expect = [&](bool ok, const char* field) {
			if (!ok) {
				std::fprintf(io.err, "M4b FAIL %s: %s snapshot != record\n", label, field);
				bad++;
			}
		};
		expect(snap.oscillatorSync == d.oscillator_sync, "oscillatorSync");
		expect(snap.voicePriority == static_cast<VoicePriority>(d.voice_priority), "voicePriority");
		expect(snap.modulator1ToModulator0 == d.modulator1_to_modulator0, "modulator1ToModulator0");
		if (sound.docModControllable_ != nullptr) {
			const deluge::project::DocModControllable& m = *sound.docModControllable_;
			const deluge::snapshot::ModControllableSnapshot& ms = sound.modControllableSnapshot();
			expect(ms.clippingAmount == m.clipping_amount, "clippingAmount");
			expect(ms.lpfMode == static_cast<FilterMode>(m.lpf_mode), "lpfMode");
			expect(ms.hpfMode == static_cast<FilterMode>(m.hpf_mode), "hpfMode");
			expect(ms.filterRoute == static_cast<FilterRoute>(m.filter_route), "filterRoute");
			expect(ms.modFXType == static_cast<ModFXType>(m.mod_fx_type), "modFXType");
		}
		expect(snap.numUnison == d.num_unison, "numUnison");
		expect(snap.unisonDetune == d.unison_detune, "unisonDetune");
		expect(snap.unisonStereoSpread == d.unison_stereo_spread, "unisonStereoSpread");
		{
			// The unison tables are derived; the live ones must match a fresh projection from the record.
			const deluge::snapshot::SoundSnapshot live = snap;
			sound.projectSnapshot();
			for (int32_t u = 0; u < live.numUnison && live.numUnison != 1; u++) {
				expect(sameTuning(live.unisonDetuners[u], sound.snapshot().unisonDetuners[u]), "unisonDetuners");
				expect(live.unisonPan[u] == sound.snapshot().unisonPan[u], "unisonPan");
			}
		}
		expect(snap.cents == d.cents, "cents");
		expect(sameTuning(snap.fineTuner, centsTuner(d.cents)), "fineTuner");
		for (int32_t l = 0; l < LFO_COUNT; l++) {
			expect(snap.lfoConfig[l].waveType == static_cast<LFOType>(d.lfo_wave_type[l]), "lfoConfig.waveType");
			expect(snap.lfoConfig[l].syncType == static_cast<SyncType>(d.lfo_sync_type[l]), "lfoConfig.syncType");
			expect(snap.lfoConfig[l].syncLevel == static_cast<SyncLevel>(d.lfo_sync_level[l]), "lfoConfig.syncLevel");
		}
		for (int32_t i = 0; i < 2; i++) {
			expect(snap.modulatorCents[i] == d.modulator_cents[i], "modulatorCents");
			expect(sameTuning(snap.modulatorTransposers[i], centsTuner(d.modulator_cents[i])), "modulatorTransposers");
			expect(snap.modulatorTranspose[i] == d.modulator_transpose[i], "modulatorTranspose");
			expect(snap.oscRetriggerPhase[i] == d.osc_retrigger_phase[i], "oscRetriggerPhase");
			expect(snap.modulatorRetriggerPhase[i] == d.modulator_retrigger_phase[i], "modulatorRetriggerPhase");
		}
		if (sound.getSynthMode() != static_cast<SynthMode>(d.synth_mode)) {
			std::fprintf(io.err, "M4b FAIL %s: synthMode snapshot=%d record=%d\n", label,
			             static_cast<int>(sound.getSynthMode()), d.synth_mode);
			bad++;
		}
	});
	if (probe != nullptr) {
		Sound& sound = *probe;
		const deluge::project::DocSound& d = *sound.docSound_;
		const int16_t transpose = sound.getTranspose();
		sound.setTranspose(static_cast<int16_t>(transpose + 7));
		if (sound.getTranspose() != transpose + 7 || d.transpose != transpose + 7) {
			std::fprintf(io.err, "M4b FAIL %s: setTranspose did not write the record and reproject\n", label);
			bad++;
		}
		sound.setTranspose(transpose);
		const SynthMode mode = sound.getSynthMode();
		const SynthMode other = (mode == SynthMode::FM) ? SynthMode::RINGMOD : SynthMode::FM;
		sound.assignSynthMode(other);
		if (sound.getSynthMode() != other || static_cast<SynthMode>(d.synth_mode) != other) {
			std::fprintf(io.err, "M4b FAIL %s: assignSynthMode did not write the record and reproject\n", label);
			bad++;
		}
		sound.assignSynthMode(mode);
		auto roundTrip = [&](bool ok, const char* setter) {
			if (!ok) {
				std::fprintf(io.err, "M4b FAIL %s: %s did not write the record and reproject\n", label, setter);
				bad++;
			}
		};
		const deluge::snapshot::SoundSnapshot before = sound.snapshot();
		sound.setOscillatorSync(!before.oscillatorSync);
		roundTrip(sound.snapshot().oscillatorSync == d.oscillator_sync && d.oscillator_sync != before.oscillatorSync,
		          "setOscillatorSync");
		sound.setOscillatorSync(before.oscillatorSync);
		const VoicePriority otherPriority =
		    before.voicePriority == VoicePriority::HIGH ? VoicePriority::LOW : VoicePriority::HIGH;
		sound.setVoicePriority(otherPriority);
		roundTrip(sound.snapshot().voicePriority == otherPriority
		              && static_cast<VoicePriority>(d.voice_priority) == otherPriority,
		          "setVoicePriority");
		sound.setVoicePriority(before.voicePriority);
		sound.setModulator1ToModulator0(!before.modulator1ToModulator0);
		roundTrip(sound.snapshot().modulator1ToModulator0 == d.modulator1_to_modulator0
		              && d.modulator1_to_modulator0 != before.modulator1ToModulator0,
		          "setModulator1ToModulator0");
		sound.setModulator1ToModulator0(before.modulator1ToModulator0);
		sound.setOscRetriggerPhase(1, before.oscRetriggerPhase[1] + 5);
		roundTrip(sound.snapshot().oscRetriggerPhase[1] == d.osc_retrigger_phase[1]
		              && d.osc_retrigger_phase[1] == before.oscRetriggerPhase[1] + 5,
		          "setOscRetriggerPhase");
		sound.setOscRetriggerPhase(1, before.oscRetriggerPhase[1]);
		sound.setModulatorRetriggerPhase(1, before.modulatorRetriggerPhase[1] + 5);
		roundTrip(sound.snapshot().modulatorRetriggerPhase[1] == d.modulator_retrigger_phase[1]
		              && d.modulator_retrigger_phase[1] == before.modulatorRetriggerPhase[1] + 5,
		          "setModulatorRetriggerPhase");
		sound.setModulatorRetriggerPhase(1, before.modulatorRetriggerPhase[1]);
		const LFOConfig lfo = before.lfoConfig[LFO2_ID];
		const LFOType otherWave = lfo.waveType == LFOType::SQUARE ? LFOType::SAW : LFOType::SQUARE;
		sound.setLfoWaveType(LFO2_ID, otherWave);
		roundTrip(sound.snapshot().lfoConfig[LFO2_ID].waveType == otherWave
		              && static_cast<LFOType>(d.lfo_wave_type[LFO2_ID]) == otherWave,
		          "setLfoWaveType");
		sound.setLfoWaveType(LFO2_ID, lfo.waveType);
		const SyncLevel otherLevel = lfo.syncLevel == SYNC_LEVEL_8TH ? SYNC_LEVEL_16TH : SYNC_LEVEL_8TH;
		sound.setLfoSync(LFO2_ID, SYNC_TYPE_TRIPLET, otherLevel);
		roundTrip(sound.snapshot().lfoConfig[LFO2_ID].syncType == SYNC_TYPE_TRIPLET
		              && sound.snapshot().lfoConfig[LFO2_ID].syncLevel == otherLevel
		              && static_cast<SyncType>(d.lfo_sync_type[LFO2_ID]) == SYNC_TYPE_TRIPLET
		              && static_cast<SyncLevel>(d.lfo_sync_level[LFO2_ID]) == otherLevel,
		          "setLfoSync");
		sound.setLfoSync(LFO2_ID, lfo.syncType, lfo.syncLevel);
		const int8_t otherCents = static_cast<int8_t>(before.cents == 25 ? -25 : 25);
		sound.set_cents(otherCents);
		roundTrip(sound.snapshot().cents == otherCents && d.cents == otherCents
		              && sameTuning(sound.snapshot().fineTuner, centsTuner(otherCents)),
		          "set_cents");
		sound.set_cents(before.cents);
		const int8_t otherModCents = static_cast<int8_t>(before.modulatorCents[1] == 30 ? -30 : 30);
		sound.setModulatorCents(1, otherModCents, nullptr);
		roundTrip(sound.snapshot().modulatorCents[1] == otherModCents && d.modulator_cents[1] == otherModCents
		              && sameTuning(sound.snapshot().modulatorTransposers[1], centsTuner(otherModCents)),
		          "setModulatorCents");
		sound.setModulatorCents(1, before.modulatorCents[1], nullptr);
		const uint8_t otherUnison = before.numUnison == 4 ? 3 : 4;
		sound.setNumUnison(otherUnison, nullptr);
		sound.setUnisonDetune(before.unisonDetune == 20 ? 30 : 20, nullptr);
		sound.setUnisonStereoSpread(before.unisonStereoSpread == 10 ? 15 : 10);
		{
			const deluge::snapshot::SoundSnapshot edited = sound.snapshot();
			roundTrip(edited.numUnison == otherUnison && d.num_unison == otherUnison
			              && edited.unisonDetune == d.unison_detune
			              && edited.unisonStereoSpread == d.unison_stereo_spread,
			          "setNumUnison/setUnisonDetune/setUnisonStereoSpread");
			sound.projectSnapshot();
			for (int32_t u = 0; u < otherUnison; u++) {
				roundTrip(sameTuning(edited.unisonDetuners[u], sound.snapshot().unisonDetuners[u])
				              && edited.unisonPan[u] == sound.snapshot().unisonPan[u],
				          "unison tables after an edit");
			}
		}
		sound.setUnisonStereoSpread(before.unisonStereoSpread);
		sound.setUnisonDetune(before.unisonDetune, nullptr);
		sound.setNumUnison(before.numUnison, nullptr);
		if (sound.docModControllable_ != nullptr) {
			const deluge::project::DocModControllable& m = *sound.docModControllable_;
			const deluge::snapshot::ModControllableSnapshot mBefore = sound.modControllableSnapshot();
			sound.setClippingAmount(static_cast<uint8_t>(mBefore.clippingAmount == 3 ? 4 : 3));
			sound.setLpfMode(mBefore.lpfMode == FilterMode::SVF_BAND ? FilterMode::SVF_NOTCH : FilterMode::SVF_BAND);
			sound.setHpfMode(mBefore.hpfMode == FilterMode::HPLADDER ? FilterMode::SVF_BAND : FilterMode::HPLADDER);
			sound.setFilterRoute(mBefore.filterRoute == FilterRoute::PARALLEL ? FilterRoute::HIGH_TO_LOW
			                                                                  : FilterRoute::PARALLEL);
			const deluge::snapshot::ModControllableSnapshot& ms = sound.modControllableSnapshot();
			roundTrip(ms.clippingAmount == m.clipping_amount && ms.clippingAmount != mBefore.clippingAmount
			              && ms.lpfMode == static_cast<FilterMode>(m.lpf_mode) && ms.lpfMode != mBefore.lpfMode
			              && ms.hpfMode == static_cast<FilterMode>(m.hpf_mode) && ms.hpfMode != mBefore.hpfMode
			              && ms.filterRoute == static_cast<FilterRoute>(m.filter_route)
			              && ms.filterRoute != mBefore.filterRoute,
			          "setClippingAmount/setLpfMode/setHpfMode/setFilterRoute");
			sound.setFilterRoute(mBefore.filterRoute);
			sound.setHpfMode(mBefore.hpfMode);
			sound.setLpfMode(mBefore.lpfMode);
			sound.setClippingAmount(mBefore.clippingAmount);
		}
	}
	std::fprintf(io.err, "M4b snapshot coherence %s: %d bound sounds, %d mismatches\n", label, sounds, bad);
	return bad;
}

// load(xml) -> fully-loaded Song (currentSong set, view cleared), or nullptr on load error (code in *loadErr).
// `xml` must be writable (the reader null-terminates tokens in place) and outlive all uses of the Song.
static Song* load_song(char* xml, size_t xmlLen, Error* loadErr) {
	void* mem = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (mem) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);

	// Through the Value tree, as a song load is: parse the whole XML, then replay it. The tree reader's
	// shouldCallRoutines() is false, which gates the AudioEngine yields off for the load.
	std::optional<deluge::storage::Value> tree =
	    deluge::storage::parseXml(std::string_view(reinterpret_cast<const char*>(xml), xmlLen));
	Error e = Error::FILE_CORRUPTED;
	if (tree) {
		deluge::storage::ValueTreeDeserializer adapter(*tree, deluge::storage::SourceFormat::Xml);
		activeReader = &adapter;
		e = openTreeFile(adapter, "song");
		if (e == Error::NONE) {
			e = deluge::project::loadSong(*song, adapter);
		}
		activeReader = &smDeserializer;
	}
	*loadErr = e;
	if (e != Error::NONE) {
		return nullptr;
	}
	currentSong = song;
	song->setCurrentClip(nullptr); // drop transient view state (see song_roundtrip.cpp)
	return song;
}

int song_edit_roundtrip_main(int argc, char** argv, Io driverIo) {
	io = driverIo;
	if (argc < 2) {
		std::fprintf(io.err, "usage: %s <song.XML>\n", argv[0]);
		return 2;
	}
	const char* songPath = argv[1];

	// Two song-global unpatched params: one gets a distinctive scalar value, the other gets automation nodes.
	// Distinctive non-zero values so they're always serialized and survive the hex round-trip verbatim.
	const int32_t pVal = params::UNPATCHED_REVERB_SEND_AMOUNT;
	const int32_t kSentinel = 0x12345678;
	const int32_t pAuto = params::UNPATCHED_VOLUME;

	FILE* f = std::fopen(songPath, "rb");
	if (!f) {
		std::fprintf(io.err, "cannot open %s\n", songPath);
		return 2;
	}
	std::fseek(f, 0, SEEK_END);
	long fileLen = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);

	const uint32_t kCap = 16u * 1024 * 1024;
	char* orig = (char*)std::malloc(fileLen + 1);
	char* gen1 = (char*)std::malloc(kCap);
	char* gen1copy = (char*)std::malloc(kCap);
	char* gen2 = (char*)std::malloc(kCap);     // S6-fix regression: 2nd-generation save buffer
	char* gen2copy = (char*)std::malloc(kCap); // ...and its reload copy
	fileLen = (long)std::fread(orig, 1, fileLen, f);
	orig[fileLen] = 0;
	std::fclose(f);

	// 1+2. Load, then DOM-first edit the song-global param.
	Error e1;
	Song* song = load_song(orig, fileLen, &e1);
	if (!song) {
		std::fprintf(io.err, "LOADFAIL original -> Error %d\n", (int)e1);
		return 1;
	}
	if (checkSoundSnapshots(*song, "load") != 0) {
		return 3;
	}

	// Phase C gate: every loaded AudioClip's unpatched set must be BOUND to a DOM store (bindDomStore ran in
	// the post-load loop), and the borrow must be live — engine nodes() aliases the store lane. Run against a
	// song with audio clips (Sampling.XML) to exercise this; songs without audio clips just report seen=0.
	{
		auto isLiveBound = [](ParamSet* s) {
			return s->domStore() != nullptr && s->docForParam(0) == &s->domStore()->params[0];
		};
		auto bothBound = [&](ParamManagerForTimeline& pm) {
			return isLiveBound(pm.getUnpatchedParamSet()) && isLiveBound(pm.getPatchedParamSet());
		};
		// M2-F: patch cables are NOT bound to a shared positional store (they reorder by index at runtime); the
		// cable owns its node store inline (amount_) and owner-resolution returns it.
		auto cablesBound = [](ParamManagerForTimeline& pm) {
			PatchCableSet* pcs = pm.getPatchCableSet();
			// A cable's node store is DOM-resident either INLINE (M2-F unbound: docForParam == amount_) or bound to
			// the identity-keyed tree store (S4b id / S5-b.3 session relocate: domCables() != nullptr). Both OK.
			return pcs->domCables() != nullptr || pcs->docForParam(0) == &pcs->patchCables[0].amount_;
		};
		// Option I (read-authority, Slice C): each cable's store current_value must equal the engine cursor — the
		// read now sources the persisted scalar from the store (docForParam), not the engine.
		auto cableScalarOk = [](ParamManagerForTimeline& pm) {
			PatchCableSet* pcs = pm.getPatchCableSet();
			for (int32_t c = 0; c < pcs->numPatchCables; c++) {
				if (pcs->docForParam(c)->current_value != pcs->patchCables[c].param.currentValue) {
					return false;
				}
			}
			return true;
		};
		// Option I (read-authority): the DOM-first read makes the store's current_value authoritative — it must
		// equal the engine cursor for every param. On a no-id song (e.g. Sampling for audio) the post-load
		// engine-seed is skipped, so the store scalar comes ONLY from the read, isolating read-authority; on
		// id songs it also guards the store<->engine consistency the seed/relocate must preserve.
		auto scalarOkSet = [](ParamSet* s) {
			if (s->domStore() == nullptr) {
				return false;
			}
			for (int32_t p = 0; p < s->getNumParams() && p < (int32_t)s->domStore()->params.size(); p++) {
				if (s->domStore()->params[p].current_value != s->params[p].currentValue) {
					return false;
				}
			}
			return true;
		};
		auto bothScalarOk = [&](ParamManagerForTimeline& pm) {
			return scalarOkSet(pm.getUnpatchedParamSet()) && scalarOkSet(pm.getPatchedParamSet());
		};
		int32_t audioSeen = 0, audioBound = 0, audioScalarOk = 0, audioIdSeen = 0, audioIdOk = 0;
		int32_t synthSeen = 0, synthBound = 0, synthScalarOk = 0;
		int32_t kitSeen = 0, kitBound = 0, kitScalarOk = 0, drumSeen = 0, drumBound = 0, drumScalarOk = 0;
		int32_t cableSeen = 0, cableBound = 0, cableScalarOkCount = 0;
		// S5-b.3: synth/kit clip-level (INSTRUMENT) + per-drum (DRUM) owners must resolve by durable id
		// (resolveParam == docForParam), i.e. doc() id-resolves them, not the fallback.
		int32_t instIdSeen = 0, instIdOk = 0, drumIdSeen = 0, drumIdOk = 0;
		int32_t midiIdSeen = 0, midiIdOk = 0; // S5-b.2: MIDI clip CC automation
		auto instIdResolves = [&](Clip* clip, ParamSet* set) {
			if (!clip->output->id()) {
				return; // an output not yet given its id has no address to resolve
			}
			instIdSeen++;
			deluge::project::ParamAddress addr{deluge::project::OwnerId::instrument(*clip->output->id()),
			                                   set->getParamKind(), 0, clip->id()};
			instIdOk += (song->project.resolveParam(addr) == set->docForParam(0)) ? 1 : 0;
		};
		std::array<ClipArray*, 2> clipArrays{&song->project.clips.session_clips,
		                                     &song->project.clips.arrangement_only_clips};
		for (ClipArray* arr : clipArrays) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (!clip->paramManager.containsAnyMainParamCollections()) {
					continue;
				}
				if (clip->type == ClipType::AUDIO) {
					audioSeen++;
					ParamSet* u = clip->paramManager.getUnpatchedParamSet();
					audioBound += isLiveBound(u) ? 1 : 0;
					audioScalarOk += scalarOkSet(u) ? 1 : 0;
					// S5-b.1: an audio clip must be id-addressed — resolveParam(clip id) must return the SAME
					// store lane as docForParam, proving doc() id-resolves it (not the fallback).
					audioIdSeen++;
					deluge::project::ParamAddress addr{deluge::project::OwnerId::clip(clip->id()), u->getParamKind(), 0,
					                                   std::nullopt};
					audioIdOk += (song->project.resolveParam(addr) == u->docForParam(0)) ? 1 : 0;
				}
				else if (clip->type == ClipType::INSTRUMENT && clip->output != nullptr
				         && clip->output->type == OutputType::SYNTH) {
					synthSeen++; // a synth clip binds BOTH its unpatched and patched sets
					synthBound += bothBound(clip->paramManager) ? 1 : 0;
					synthScalarOk += bothScalarOk(clip->paramManager) ? 1 : 0;
					instIdResolves(clip, clip->paramManager.getUnpatchedParamSet());
					if (clip->paramManager.getPatchCableSet()->numPatchCables > 0) {
						cableSeen++; // ...and its patch cables
						cableBound += cablesBound(clip->paramManager) ? 1 : 0;
						cableScalarOkCount += cableScalarOk(clip->paramManager) ? 1 : 0;
					}
				}
				else if (clip->type == ClipType::INSTRUMENT && clip->output != nullptr
				         && clip->output->type == OutputType::KIT) {
					kitSeen++; // kit clip-level global FX = unpatched only
					ParamSet* ku = clip->paramManager.getUnpatchedParamSet();
					kitBound += isLiveBound(ku) ? 1 : 0;
					kitScalarOk += scalarOkSet(ku) ? 1 : 0;
					instIdResolves(clip, ku);
					InstrumentClip* ic = static_cast<InstrumentClip*>(clip);
					for (int32_t n = 0; n < std::ssize(ic->noteRows); n++) {
						NoteRow& nr = ic->noteRows[n];
						if (nr.drum != nullptr && nr.drum->type == DrumType::SOUND
						    && nr.paramManager.containsAnyMainParamCollections()) {
							drumSeen++; // a SoundDrum row binds unpatched + patched
							drumBound += bothBound(nr.paramManager) ? 1 : 0;
							drumScalarOk += bothScalarOk(nr.paramManager) ? 1 : 0;
							if (nr.drum->id()) {
								drumIdSeen++;
								ParamSet* du = nr.paramManager.getUnpatchedParamSet();
								deluge::project::ParamAddress addr{deluge::project::OwnerId::drum(*nr.drum->id()),
								                                   du->getParamKind(), 0, clip->id()};
								drumIdOk += (song->project.resolveParam(addr) == du->docForParam(0)) ? 1 : 0;
							}
							if (nr.paramManager.getPatchCableSet()->numPatchCables > 0) {
								cableSeen++; // ...and its patch cables
								cableBound += cablesBound(nr.paramManager) ? 1 : 0;
								cableScalarOkCount += cableScalarOk(nr.paramManager) ? 1 : 0;
							}
						}
					}
				}
				else if (clip->output != nullptr && clip->output->type == OutputType::MIDI_OUT) {
					// S5-b.2: a MIDI clip's CC automation must id-resolve (Kind::MIDI, by cc).
					MIDIParamCollection* midi = clip->paramManager.getMIDIParamCollection();
					if (midi != nullptr) {
						for (int32_t i = 0; i < std::ssize(midi->params); i++) {
							midiIdSeen++;
							deluge::project::ParamAddress addr{deluge::project::OwnerId::clip(clip->id()),
							                                   params::Kind::MIDI, midi->params[i].cc, std::nullopt};
							midiIdOk += (song->project.resolveParam(addr) == midi->docForParam(i)) ? 1 : 0;
						}
					}
				}
			}
		}
		std::fprintf(io.err,
		             "audio: %d/%d (scalarOk %d) | synth: %d/%d (scalarOk %d) | kit: %d/%d (scalarOk %d) | "
		             "drums: %d/%d (scalarOk %d) | patchCables: %d/%d (scalarOk %d)\n",
		             audioBound, audioSeen, audioScalarOk, synthBound, synthSeen, synthScalarOk, kitBound, kitSeen,
		             kitScalarOk, drumBound, drumSeen, drumScalarOk, cableBound, cableSeen, cableScalarOkCount);
		if (audioSeen != audioBound || synthSeen != synthBound || kitSeen != kitBound || drumSeen != drumBound
		    || cableSeen != cableBound) {
			std::fprintf(io.err, "PHASE C: a clip/drum loaded UNBOUND (borrow not live)\n");
			return 3;
		}
		if (audioScalarOk != audioSeen) {
			std::fprintf(io.err, "Flip Option I FAIL: an audio clip's store current_value != engine (read not "
			                     "DOM-authoritative)\n");
			return 3;
		}
		std::fprintf(io.err, "audio id-addressed (S5-b.1): %d/%d\n", audioIdOk, audioIdSeen);
		if (audioIdOk != audioIdSeen) {
			std::fprintf(io.err, "S5-b.1 FAIL: an audio clip did not resolve by its id "
			                     "(resolveParam != docForParam) — doc() would hit the engine fallback\n");
			return 3;
		}
		std::fprintf(io.err, "instrument/drum id-addressed (S5-b.3): inst %d/%d, drum %d/%d\n", instIdOk, instIdSeen,
		             drumIdOk, drumIdSeen);
		if (instIdOk != instIdSeen || drumIdOk != drumIdSeen) {
			std::fprintf(io.err, "S5-b.3 FAIL: an instrument/drum owner did not resolve by its id "
			                     "(resolveParam != docForParam) — doc() would hit the engine fallback\n");
			return 3;
		}
		std::fprintf(io.err, "MIDI-clip id-addressed (S5-b.2): %d/%d\n", midiIdOk, midiIdSeen);
		if (midiIdOk != midiIdSeen) {
			std::fprintf(io.err, "S5-b.2 FAIL: a MIDI clip CC did not resolve by its clip's id\n");
			return 3;
		}
		if (kitScalarOk != kitSeen) {
			std::fprintf(io.err, "Flip Option I FAIL: a kit clip's store current_value != engine (read not "
			                     "DOM-authoritative)\n");
			return 3;
		}
		if (synthScalarOk != synthSeen || drumScalarOk != drumSeen) {
			std::fprintf(io.err, "Flip Option I FAIL: a synth/drum clip's store current_value != engine (read not "
			                     "DOM-authoritative)\n");
			return 3;
		}
		if (cableScalarOkCount != cableSeen) {
			std::fprintf(io.err, "Flip Option I FAIL: a patch cable's store current_value != engine (read not "
			                     "DOM-authoritative)\n");
			return 3;
		}
	}

	// Option I (save-flip gate): golden only LOADS songs, so it never exercises new-instrument creation
	// (setupAsDefaultSynth / setupAsSample), which sets PERSISTENT params via the setup overload. With save now
	// reading the DOM store (toDocParamSet), those setters MUST write the store or a freshly-created synth/sample
	// would save wrong. Build fresh Sounds exactly as createNewInstrument does (setupWithPatching + initParams) and
	// assert store == engine for every param (+ the setup()-created patch cable).
	{
		auto setScalarOk = [](ParamManagerForTimeline& pm) {
			auto ok = [](ParamSet* s) {
				for (int32_t p = 0; p < s->getNumParams(); p++) {
					if (s->docForParam(p)->current_value != s->params[p].currentValue) {
						return false;
					}
				}
				return true;
			};
			return ok(pm.getUnpatchedParamSet()) && ok(pm.getPatchedParamSet());
		};
		auto cableScalarOk = [](ParamManagerForTimeline& pm) {
			PatchCableSet* pcs = pm.getPatchCableSet();
			for (int32_t c = 0; c < pcs->numPatchCables; c++) {
				if (pcs->docForParam(c)->current_value != pcs->patchCables[c].param.currentValue) {
					return false;
				}
			}
			return true;
		};
		// setupAsDefaultSynth — pure param setters.
		{
			auto* si = new SoundInstrument();
			ParamManagerForTimeline pm;
			if (pm.setupWithPatching() != Error::NONE) {
				std::fprintf(io.err, "new-synth pm setup fail\n");
				return 3;
			}
			Sound::initParams(&pm);
			static_cast<Sound*>(si)->setupAsDefaultSynth(&pm);
			if (!setScalarOk(pm)) {
				std::fprintf(io.err, "Flip Option I FAIL: setupAsDefaultSynth store != engine (new-synth save "
				                     "would persist wrong values)\n");
				return 3;
			}
		}
		// setupAsSample — param setters + a velocity->volume patch cable created via PatchCable::setup().
		{
			auto* si = new SoundInstrument();
			ParamManagerForTimeline pm;
			if (pm.setupWithPatching() != Error::NONE) {
				std::fprintf(io.err, "new-sample pm setup fail\n");
				return 3;
			}
			Sound::initParams(&pm);
			static_cast<Sound*>(si)->setupAsSample(&pm);
			if (!setScalarOk(pm) || !cableScalarOk(pm)) {
				std::fprintf(io.err, "Flip Option I FAIL: setupAsSample store != engine (new-sample save would "
				                     "persist wrong values)\n");
				return 3;
			}
		}
		std::fprintf(io.err, "new-instrument setup scalarOk: OK\n");
	}

	// The store is populated by the <songParams> read; seed it from the engine if the song had none.
	if (!song->project.global_fx.params.unpatched) {
		song->project.global_fx.params.unpatched =
		    deluge::project::toDocParamSet(*song->paramManager.getUnpatchedParamSet());
	}
	deluge::project::DocParamSet& store = *song->project.global_fx.params.unpatched;
	if (pVal >= (int32_t)store.params.size() || pAuto >= (int32_t)store.params.size()) {
		std::fprintf(io.err, "STORE TOO SMALL (%zu params)\n", store.params.size());
		return 3;
	}
	// DOM-first edits on the store: a scalar value + an automation lane (two nodes).
	store.params[pVal].current_value = kSentinel;
	deluge::project::setNodeAtPos(store.params[pAuto], 0, 111, true);
	deluge::project::setNodeAtPos(store.params[pAuto], 48, -222, false);
	// Reproject the store into the engine (+ refresh automation summary flags) so save serializes the edits.
	deluge::project::fromDocParamManager(song->project.global_fx.params, song->paramManager);

	// The edits must be visible in the engine immediately.
	AutoParam& engVal = song->paramManager.getUnpatchedParamSet()->params[pVal];
	AutoParam& engAuto = song->paramManager.getUnpatchedParamSet()->params[pAuto];
	if (engVal.currentValue != kSentinel
	    || song->paramManager.getUnpatchedParamSet()->docForParam(pAuto)->nodes.size() != 2) {
		std::fprintf(io.err, "EDIT DID NOT LAND engVal=%d autoNodes=%zu\n", engVal.currentValue,
		             song->paramManager.getUnpatchedParamSet()->docForParam(pAuto)->nodes.size());
		return 3;
	}

	// Flip Phase 2 guard: DIVERGE the engine scalar from the store scalar for pVal, so that persistence PROVES the
	// song-global save read the STORE (project.global_fx.params.unpatched, holding kSentinel) — NOT the engine
	// cursor. If save still projected the engine, the file would carry this distinct value and the reload assert
	// below (reloaded store == engine == kSentinel) would fail.
	engVal.currentValue = 0x0BADF00D;

	// 3. Save.
	deluge::scenarios::driver::beginCapture(smSerializer);
	deluge::project::saveSong(*song, smSerializer);
	long n1 = deluge::scenarios::driver::endCapture(smSerializer, gen1, kCap);

	// 4. Reload the saved bytes.
	std::memcpy(gen1copy, gen1, n1);
	Error e2;
	Song* reloaded = load_song(gen1copy, n1, &e2);
	if (!reloaded) {
		std::fprintf(io.err, "LOADFAIL gen1 -> Error %d\n", (int)e2);
		return 1;
	}
	if (checkSoundSnapshots(*reloaded, "reload") != 0) {
		return 3;
	}

	// M2-G S2b: the save (gen1) assigns ids, so on reload every audio clip's unpatched params must be
	// TREE-resident — its store is the project.clip_params entry keyed by id, not an engine-owned domStore_.
	// (The corpus files carry no ids on first load, so this only fires post-save — proving the tree path is live.)
	{
		int32_t audioClips = 0, treeResident = 0;
		std::array<ClipArray*, 2> arrs{&reloaded->project.clips.session_clips,
		                               &reloaded->project.clips.arrangement_only_clips};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::AUDIO || !clip->paramManager.containsAnyMainParamCollections()) {
					continue;
				}
				audioClips++;
				if (std::optional<uint32_t> id = clip->id()) {
					deluge::project::DocParamManager* mgr = reloaded->project.clip_params.find(*id);
					if (mgr != nullptr && mgr->unpatched
					    && clip->paramManager.getUnpatchedParamSet()->domStore() == &*mgr->unpatched) {
						treeResident++;
					}
				}
			}
		}
		std::fprintf(io.err, "audio clips tree-resident: %d/%d\n", treeResident, audioClips);
		if (audioClips > 0 && treeResident != audioClips) {
			std::fprintf(io.err, "S2b FAIL: an audio clip is not tree-resident after reload\n");
			return 3;
		}
	}

	// M2-G S3c/S3d: every reloaded instrument-shared clip (synth or kit clip-level FX) must have its unpatched
	// automation TREE-resident — its store is the project.backed_params entry keyed by
	// (OwnerId::instrument(output id), clip id), not an engine-owned domStore_. Proves the
	// instrument-shared relocation is live (again only post-save, once ids exist).
	{
		int32_t sharedClips = 0, treeResident = 0;
		std::array<ClipArray*, 2> arrs{&reloaded->project.clips.session_clips,
		                               &reloaded->project.clips.arrangement_only_clips};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::INSTRUMENT || clip->output == nullptr
				    || (clip->output->type != OutputType::SYNTH && clip->output->type != OutputType::KIT)
				    || !clip->paramManager.containsAnyMainParamCollections()) {
					continue;
				}
				sharedClips++;
				std::optional<uint32_t> clipId = clip->id();
				std::optional<uint32_t> outId = clip->output->id();
				if (clipId.has_value() && outId.has_value()) {
					deluge::project::DocParamManager* mgr =
					    reloaded->project.backed_params.find(deluge::project::OwnerId::instrument(*outId), *clipId);
					if (mgr != nullptr && mgr->unpatched
					    && clip->paramManager.getUnpatchedParamSet()->domStore() == &*mgr->unpatched) {
						treeResident++;
					}
				}
			}
		}
		std::fprintf(io.err, "instrument-shared clips tree-resident: %d/%d\n", treeResident, sharedClips);
		if (sharedClips > 0 && treeResident != sharedClips) {
			std::fprintf(io.err, "S3c/S3d FAIL: a synth/kit clip is not tree-resident after reload\n");
			return 3;
		}
	}

	// M2-G S3e: every reloaded SoundDrum NoteRow's unpatched automation must be TREE-resident — keyed by
	// (OwnerId::drum(drum id), clip id) in project.backed_params. Per-drum params live per (drum, clip)
	// pair (each kit clip's NoteRow has its own manager).
	{
		int32_t drumRows = 0, treeResident = 0;
		std::array<ClipArray*, 2> arrs{&reloaded->project.clips.session_clips,
		                               &reloaded->project.clips.arrangement_only_clips};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::INSTRUMENT || clip->output == nullptr
				    || clip->output->type != OutputType::KIT) {
					continue;
				}
				std::optional<uint32_t> clipId = clip->id();
				InstrumentClip* ic = static_cast<InstrumentClip*>(clip);
				for (int32_t n = 0; n < std::ssize(ic->noteRows); n++) {
					NoteRow& nr = ic->noteRows[n];
					if (nr.drum == nullptr || nr.drum->type != DrumType::SOUND
					    || !nr.paramManager.containsAnyMainParamCollections()) {
						continue;
					}
					drumRows++;
					std::optional<uint32_t> drumId = nr.drum->id();
					if (clipId.has_value() && drumId.has_value()) {
						deluge::project::DocParamManager* mgr =
						    reloaded->project.backed_params.find(deluge::project::OwnerId::drum(*drumId), *clipId);
						if (mgr != nullptr && mgr->unpatched
						    && nr.paramManager.getUnpatchedParamSet()->domStore() == &*mgr->unpatched) {
							treeResident++;
						}
					}
				}
			}
		}
		std::fprintf(io.err, "drum rows tree-resident: %d/%d\n", treeResident, drumRows);
		if (drumRows > 0 && treeResident != drumRows) {
			std::fprintf(io.err, "S3e FAIL: a SoundDrum row is not tree-resident after reload\n");
			return 3;
		}
	}

	// M2-G S4a: every reloaded manager that holds an expression (MPE) set — a synth/kit clip-level manager (keyed
	// (instrument(output), clip)) or a SoundDrum NoteRow (keyed (drum, clip)) — must have that set TREE-resident,
	// proving the expression relocation binds at the same (owner, clip) key as unpatched/patched. Counted across
	// both owner tiers; 0/0 on a song with no MPE data is a pass (nothing to relocate).
	{
		int32_t mpeSets = 0, treeResident = 0;
		std::array<ClipArray*, 2> arrs{&reloaded->project.clips.session_clips,
		                               &reloaded->project.clips.arrangement_only_clips};
		auto checkExpression = [&](ParamManager& pm, const deluge::project::OwnerId& owner,
		                           std::optional<uint32_t> clipId) {
			ExpressionParamSet* expression = pm.getExpressionParamSet();
			if (expression == nullptr || !clipId.has_value()) {
				return;
			}
			mpeSets++;
			deluge::project::DocParamManager* mgr = reloaded->project.backed_params.find(owner, clipId);
			if (mgr != nullptr && mgr->expression && expression->domStore() == &*mgr->expression) {
				treeResident++;
			}
		};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::INSTRUMENT || clip->output == nullptr
				    || (clip->output->type != OutputType::SYNTH && clip->output->type != OutputType::KIT)) {
					continue;
				}
				std::optional<uint32_t> clipId = clip->id();
				std::optional<uint32_t> outId = clip->output->id();
				if (outId.has_value()) {
					checkExpression(clip->paramManager, deluge::project::OwnerId::instrument(*outId), clipId);
				}
				if (clip->output->type == OutputType::KIT) {
					InstrumentClip* ic = static_cast<InstrumentClip*>(clip);
					for (int32_t n = 0; n < std::ssize(ic->noteRows); n++) {
						NoteRow& nr = ic->noteRows[n];
						if (nr.drum == nullptr || nr.drum->type != DrumType::SOUND) {
							continue;
						}
						if (std::optional<uint32_t> drumId = nr.drum->id()) {
							checkExpression(nr.paramManager, deluge::project::OwnerId::drum(*drumId), clipId);
						}
					}
				}
			}
		}
		std::fprintf(io.err, "expression (MPE) sets tree-resident: %d/%d\n", treeResident, mpeSets);
		if (mpeSets > 0 && treeResident != mpeSets) {
			std::fprintf(io.err, "S4a FAIL: an expression set is not tree-resident after reload\n");
			return 3;
		}
	}

	// M2-G S3g: doc() must id-address kit clip-level FX and per-drum params to the SAME DocAutoParam the bound-store
	// path (docForParam) returns — i.e. doc()'s owner classification (noteRow set ⇒ drum(OwnerId::drum); else
	// synth/kit ⇒ OwnerId::instrument) is correct. Golden/save can't see this (they never resolve via doc()); a
	// misclassification would silently resolve a drum param against the kit-FX entry. Build the REAL ModelStacks the
	// engine uses and assert doc() == docForParam for a param of each kit clip-level manager and each SoundDrum row.
	{
		int32_t checked = 0;
		std::array<ClipArray*, 2> arrs{&reloaded->project.clips.session_clips,
		                               &reloaded->project.clips.arrangement_only_clips};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::INSTRUMENT || clip->output == nullptr
				    || clip->output->type != OutputType::KIT) {
					continue;
				}
				auto consistent = [&](ParamManagerForTimeline& pm, ModelStackWithAutoParam* msap) -> bool {
					if (msap == nullptr || msap->autoParam == nullptr) {
						return true; // no param at this id → nothing to check
					}
					checked++;
					return msap->doc() == pm.getUnpatchedParamSet()->docForParam(0);
				};
				// Kit clip-level FX (no noteRow): expect an OwnerId::instrument(kit output) address.
				char buf[MODEL_STACK_MAX_SIZE]{};
				ModelStackWithThreeMainThings* m3 = setupModelStackWithThreeMainThingsButNoNoteRow(
				    buf, reloaded, static_cast<Kit*>(clip->output)->toModControllable(), clip, &clip->paramManager);
				if (!consistent(clip->paramManager, m3->getUnpatchedAutoParamFromId(0))) {
					std::fprintf(io.err, "S3g FAIL: kit clip-level doc() != bound store (misclassified)\n");
					return 3;
				}
				// Each SoundDrum NoteRow (noteRow set): expect an OwnerId::drum(drum id) address.
				InstrumentClip* ic = static_cast<InstrumentClip*>(clip);
				for (int32_t n = 0; n < std::ssize(ic->noteRows); n++) {
					NoteRow& nr = ic->noteRows[n];
					if (nr.drum == nullptr || nr.drum->type != DrumType::SOUND
					    || !nr.paramManager.containsAnyMainParamCollections()) {
						continue;
					}
					char dbuf[MODEL_STACK_MAX_SIZE]{};
					ModelStackWithThreeMainThings* dm3 = setupModelStackWithThreeMainThingsIncludingNoteRow(
					    dbuf, reloaded, clip, n, &nr, static_cast<SoundDrum*>(nr.drum)->toModControllable(),
					    &nr.paramManager);
					if (!consistent(nr.paramManager, dm3->getUnpatchedAutoParamFromId(0))) {
						std::fprintf(io.err, "S3g FAIL: drum doc() != bound store (misclassified as kit-level)\n");
						return 3;
					}
				}
			}
		}
		std::fprintf(io.err, "kit/drum doc()==bound-store checks: %d\n", checked);
	}

	// M2-G S4b: every reloaded synth/drum manager's patch cables must be (a) tree-resident — the engine cable set
	// bound to the owner's project-tree `patch_cables` — and (b) id-addressable: the address doc() builds for each
	// cable (owner + PATCH_CABLE + the packed (dest,source) paramId) must resolve to the SAME DocAutoParam the
	// engine bound-store path (docForParam) returns. Identity-matched, so this catches a mis-encoded/mis-owned
	// cable that save (which never resolves via doc()) would silently miss.
	{
		int32_t cableSets = 0, treeResident = 0, addrChecks = 0;
		bool misOwned = false;
		std::array<ClipArray*, 2> arrs{&reloaded->project.clips.session_clips,
		                               &reloaded->project.clips.arrangement_only_clips};
		auto checkCables = [&](ParamManagerForTimeline& pm, const deluge::project::OwnerId& owner,
		                       std::optional<uint32_t> clipId) {
			PatchCableSet* pcs = pm.getPatchCableSet();
			if (pcs == nullptr || !clipId.has_value()) {
				return;
			}
			cableSets++;
			deluge::project::DocParamManager* mgr = reloaded->project.backed_params.find(owner, clipId);
			if (mgr != nullptr && pcs->domCables() == &mgr->patch_cables) {
				treeResident++;
			}
			for (int32_t c = 0; c < pcs->numPatchCables; c++) {
				int32_t packedId =
				    PatchCableSet::getParamId(pcs->patchCables[c].destinationParamDescriptor, pcs->patchCables[c].from);
				deluge::project::ParamAddress addr{owner, deluge::modulation::params::Kind::PATCH_CABLE, packedId,
				                                   clipId};
				if (reloaded->project.resolveParam(addr) != pcs->docForParam(c)) {
					misOwned = true;
					return;
				}
				addrChecks++;
			}
		};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::INSTRUMENT || clip->output == nullptr) {
					continue;
				}
				std::optional<uint32_t> clipId = clip->id();
				if (clip->output->type == OutputType::SYNTH) {
					if (std::optional<uint32_t> outId = clip->output->id()) {
						checkCables(clip->paramManager, deluge::project::OwnerId::instrument(*outId), clipId);
					}
				}
				else if (clip->output->type == OutputType::KIT) {
					InstrumentClip* ic = static_cast<InstrumentClip*>(clip);
					for (int32_t n = 0; n < std::ssize(ic->noteRows); n++) {
						NoteRow& nr = ic->noteRows[n];
						if (nr.drum == nullptr || nr.drum->type != DrumType::SOUND) {
							continue;
						}
						if (std::optional<uint32_t> drumId = nr.drum->id()) {
							checkCables(nr.paramManager, deluge::project::OwnerId::drum(*drumId), clipId);
						}
					}
				}
			}
		}
		if (misOwned) {
			std::fprintf(io.err, "S4b FAIL: patch-cable doc() != bound store (mis-encoded/mis-owned)\n");
			return 3;
		}
		std::fprintf(io.err, "patch-cable sets tree-resident: %d/%d (doc()==bound-store checks: %d)\n", treeResident,
		             cableSets, addrChecks);
		if (cableSets > 0 && treeResident != cableSets) {
			std::fprintf(io.err, "S4b FAIL: a synth/drum patch-cable set is not tree-resident after reload\n");
			return 3;
		}
	}

	// M2-G S4c: every reloaded MIDI_OUT clip's CC automation must be (a) tree-resident — the engine
	// MIDIParamCollection bound to the clip's per-clip project-tree midi_params (keyed by Clip::id, in
	// clip_params like audio clips, since MIDI CC automation is per-clip) — and (b) id-addressable: the address
	// doc() builds (OwnerId::clip + Kind::MIDI + cc) must resolve to the SAME DocAutoParam the engine bound-store
	// path (docForParam) returns. Catches a mis-keyed cc that save (never via doc()) would silently miss.
	{
		int32_t midiSets = 0, treeResident = 0, addrChecks = 0;
		std::array<ClipArray*, 2> arrs{&reloaded->project.clips.session_clips,
		                               &reloaded->project.clips.arrangement_only_clips};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::INSTRUMENT || clip->output == nullptr
				    || clip->output->type != OutputType::MIDI_OUT
				    || !clip->paramManager.containsAnyParamCollectionsIncludingExpression()) {
					continue;
				}
				std::optional<uint32_t> clipId = clip->id();
				if (!clipId.has_value()) {
					continue;
				}
				MIDIParamCollection* midi = clip->paramManager.getMIDIParamCollection();
				midiSets++;
				deluge::project::DocParamManager* mgr = reloaded->project.clip_params.find(*clipId);
				if (mgr != nullptr && midi->domMidi() == &mgr->midi_params) {
					treeResident++;
				}
				for (int32_t i = 0; i < std::ssize(midi->params); i++) {
					int32_t cc = midi->params[i].cc;
					deluge::project::ParamAddress addr{deluge::project::OwnerId::clip(*clipId),
					                                   deluge::modulation::params::Kind::MIDI, cc, std::nullopt};
					if (reloaded->project.resolveParam(addr) != midi->docForParam(i)) {
						std::fprintf(io.err, "S4c FAIL: MIDI doc() != bound store (mis-keyed cc)\n");
						return 3;
					}
					addrChecks++;
				}
			}
		}
		std::fprintf(io.err, "MIDI CC sets tree-resident: %d/%d (doc()==bound-store checks: %d)\n", treeResident,
		             midiSets, addrChecks);
		if (midiSets > 0 && treeResident != midiSets) {
			std::fprintf(io.err, "S4c FAIL: a MIDI_OUT CC set is not tree-resident after reload\n");
			return 3;
		}
	}

	// M2-G S6-fix (data-loss REGRESSION guard): a runtime automation edit routes through doc() -> the TREE store,
	// leaving the engine's inline patch-cable `amount_` / MIDI `nodes_` STALE. The SAVE projection must read the
	// tree (docForParam), not the inline store, or the edit is silently lost. Corpus files carry no ids on
	// first load (nothing is bound then), so the bound tree exists only on the RELOADED song — plant a distinctive
	// node in a bound patch cable's and a bound MIDI CC's tree store here (via docForParam, exactly where doc()
	// writes), leave the inline stale, then save+reload again and require it survived.
	{
		constexpr int32_t kMarkPos = 13;
		constexpr int32_t kMarkVal = 0x5A5A5A;
		bool plantedCable = false, plantedMidi = false;
		std::optional<uint32_t> cableClipId, midiClipId;

		auto scanCableForMark = [](Clip* clip) -> bool {
			PatchCableSet* pcs = clip->paramManager.getPatchCableSet();
			for (int32_t c = 0; pcs != nullptr && c < pcs->numPatchCables; c++) {
				const auto& nodes = pcs->docForParam(c)->nodes;
				for (int32_t n = 0; n < std::ssize(nodes); n++) {
					if (nodes[n].pos == kMarkPos && nodes[n].value == kMarkVal) {
						return true;
					}
				}
			}
			return false;
		};
		auto scanMidiForMark = [](Clip* clip) -> bool {
			MIDIParamCollection* midi = clip->paramManager.getMIDIParamCollection();
			for (int32_t i = 0; midi != nullptr && i < std::ssize(midi->params); i++) {
				const auto& nodes = midi->docForParam(i)->nodes;
				for (int32_t n = 0; n < std::ssize(nodes); n++) {
					if (nodes[n].pos == kMarkPos && nodes[n].value == kMarkVal) {
						return true;
					}
				}
			}
			return false;
		};

		std::array<ClipArray*, 2> arrs{&reloaded->project.clips.session_clips,
		                               &reloaded->project.clips.arrangement_only_clips};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::INSTRUMENT || clip->output == nullptr) {
					continue;
				}
				if (!plantedCable && clip->output->type == OutputType::SYNTH) {
					PatchCableSet* pcs = clip->paramManager.getPatchCableSet();
					if (pcs != nullptr && pcs->numPatchCables > 0 && pcs->domCables() != nullptr) {
						deluge::project::setNodeAtPos(*pcs->docForParam(0), kMarkPos, kMarkVal, false);
						cableClipId = clip->id();
						plantedCable = true;
					}
				}
				if (!plantedMidi && clip->output->type == OutputType::MIDI_OUT
				    && clip->paramManager.containsAnyParamCollectionsIncludingExpression()) {
					MIDIParamCollection* midi = clip->paramManager.getMIDIParamCollection();
					if (midi != nullptr && !midi->params.empty() && midi->domMidi() != nullptr) {
						deluge::project::setNodeAtPos(*midi->docForParam(0), kMarkPos, kMarkVal, false);
						midiClipId = clip->id();
						plantedMidi = true;
					}
				}
			}
		}

		if (plantedCable || plantedMidi) {
			deluge::scenarios::driver::beginCapture(smSerializer);
			deluge::project::saveSong(*reloaded, smSerializer);
			long n2 = deluge::scenarios::driver::endCapture(smSerializer, gen2, kCap);
			std::memcpy(gen2copy, gen2, n2);
			Error e3;
			Song* reloaded2 = load_song(gen2copy, n2, &e3);
			if (reloaded2 == nullptr) {
				std::fprintf(io.err, "S6-fix: LOADFAIL gen2 -> Error %d\n", (int)e3);
				return 1;
			}
			auto findClip = [&](std::optional<uint32_t> id) -> Clip* {
				if (!id.has_value()) {
					return nullptr;
				}
				std::array<ClipArray*, 2> a{&reloaded2->project.clips.session_clips,
				                            &reloaded2->project.clips.arrangement_only_clips};
				for (ClipArray* arr : a) {
					for (int32_t c = 0; c < std::ssize(*arr); c++) {
						if ((*arr)[c]->id() == id) {
							return (*arr)[c];
						}
					}
				}
				return nullptr;
			};
			if (plantedCable) {
				Clip* rc = findClip(cableClipId);
				if (rc == nullptr || !scanCableForMark(rc)) {
					std::fprintf(io.err, "S6-fix FAIL: patch-cable automation edit LOST on save (read stale inline "
					                     "amount_, not the tree)\n");
					return 3;
				}
			}
			if (plantedMidi) {
				Clip* rc = findClip(midiClipId);
				if (rc == nullptr || !scanMidiForMark(rc)) {
					std::fprintf(io.err, "S6-fix FAIL: MIDI CC automation edit LOST on save (writeToFile read stale "
					                     "nodes_, not the tree)\n");
					return 3;
				}
			}
			std::fprintf(io.err, "S6-fix: tree-authoritative save OK (cable=%d midi=%d)\n", plantedCable ? 1 : 0,
			             plantedMidi ? 1 : 0);
		}
	}

	// M2-G S6-fix (ops REGRESSION guard): MIDIParamCollection's automation ops (stripAutomation/generateRepeats/
	// shiftHorizontally/...) must operate on the tree store (docForParam) when bound, not the stale inline nodes_.
	// Representative check: plant a node in a bound MIDI CC's tree, call stripAutomation(), and require the TREE
	// lane is cleared. Pre-fix stripAutomation cleared the inline nodes_ and left the tree untouched.
	{
		std::array<ClipArray*, 2> arrs{&reloaded->project.clips.session_clips,
		                               &reloaded->project.clips.arrangement_only_clips};
		for (ClipArray* arr : arrs) {
			for (int32_t c = 0; c < std::ssize(*arr); c++) {
				Clip* clip = (*arr)[c];
				if (clip->type != ClipType::INSTRUMENT || clip->output == nullptr
				    || clip->output->type != OutputType::MIDI_OUT
				    || !clip->paramManager.containsAnyParamCollectionsIncludingExpression()) {
					continue;
				}
				MIDIParamCollection* midi = clip->paramManager.getMIDIParamCollection();
				if (midi == nullptr || midi->params.empty() || midi->domMidi() == nullptr) {
					continue;
				}
				deluge::project::setNodeAtPos(*midi->docForParam(0), 21, 0x0B0B0B, false);
				if (midi->docForParam(0)->nodes.size() == 0) {
					std::fprintf(io.err, "S6-fix ops: planted node missing (setup error)\n");
					return 3;
				}
				midi->stripAutomation();
				if (midi->docForParam(0)->nodes.size() != 0) {
					std::fprintf(io.err, "S6-fix FAIL: MIDI stripAutomation did not clear the TREE lane (op read "
					                     "stale inline nodes_)\n");
					return 3;
				}
				std::fprintf(io.err, "S6-fix ops: MIDI stripAutomation hit the tree OK\n");
				goto opsChecked;
			}
		}
opsChecked:;
	}

	// 5. Both edits must have persisted into the reloaded DOM store (the authority) and the reloaded engine.
	if (!reloaded->project.global_fx.params.unpatched) {
		std::fprintf(io.err, "reloaded store EMPTY\n");
		return 3;
	}
	deluge::project::DocParamSet& rstore = *reloaded->project.global_fx.params.unpatched;
	int32_t storeVal = rstore.params[pVal].current_value;
	int32_t engineVal = reloaded->paramManager.getUnpatchedParamSet()->params[pVal].currentValue;

	const auto& storeNodes = rstore.params[pAuto].nodes;
	const auto& engNodes = reloaded->paramManager.getUnpatchedParamSet()->docForParam(pAuto)->nodes;

	bool valOk = (storeVal == kSentinel) && (engineVal == kSentinel);
	bool autoStoreOk = storeNodes.size() == 2 && storeNodes[0].pos == 0 && storeNodes[0].value == 111
	                   && storeNodes[0].interpolated && storeNodes[1].pos == 48 && storeNodes[1].value == -222
	                   && !storeNodes[1].interpolated;
	bool autoEngOk = engNodes.size() == 2 && engNodes[0].value == 111 && engNodes[1].value == -222;

	bool persisted = valOk && autoStoreOk && autoEngOk;
	std::fprintf(io.err,
	             "value p=%d sentinel=0x%08X reloaded store=0x%08X engine=0x%08X | automation p=%d storeNodes=%zu "
	             "engNodes=%zu | %s\n",
	             pVal, kSentinel, storeVal, engineVal, pAuto, storeNodes.size(), engNodes.size(),
	             persisted ? "PERSISTED" : "LOST");
	return persisted ? 0 : 3;
}

} // namespace

DELUGE_DRIVER_SCENARIO(song_edit_roundtrip, song_edit_roundtrip_main)
