// A param is set, and its automation cleared, by its address: the Edit finds the engine's param itself, on every owner
// a menu edits (a synth, a kit row, a kit's own effects, an audio clip, the song), including an owner never saved. The
// address the bridge resolves is the address the engine's own stack names, a write lands in the engine and in the
// record, a turn while recording records automation, and an address naming nothing is refused. Their undo steps are
// edit-substrate's (an Action opens only with a UI on the hierarchy).

#include "deluge-project/param_address.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/delete_param_automation.h"
#include "model/edits/edit.h"
#include "model/edits/param_address_for.h"
#include "model/edits/set_param_value.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/param_stack.h"
#include "model/project_bridge/patch_cable_words.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <optional>

using namespace deluge::scenes;
using deluge::project::OwnerId;
using deluge::project::ParamAddress;
using deluge::project::ParamSpace;
namespace params = deluge::modulation::params;

namespace {

ParamAddress address_of(OwnerId owner, params::Kind kind, int32_t index, std::optional<uint32_t> clip_id) {
	return {
	    .owner = owner, .space = ParamSpace::ENGINE, .id = deluge::bridge::paramIdOf(kind, index), .clip_id = clip_id};
}

/// The value the engine's param at @p address plays, or nullopt where the bridge finds none.
std::optional<int32_t> engine_value(Song& song, const ParamAddress& address) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(song, address, memory);
	return stack != nullptr && stack->autoParam != nullptr ? std::optional{stack->autoParam->getCurrentValue()}
	                                                       : std::nullopt;
}

/// Whether the stack the bridge builds for @p address names that address again.
bool round_trips(Song& song, const ParamAddress& address) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(song, address, memory);
	if (stack == nullptr) {
		return false;
	}
	const std::optional<ParamAddress> named = deluge::edits::addressFor(*stack);
	return named && named->owner == address.owner && named->space == address.space && named->id == address.id
	       && named->clip_id == address.clip_id;
}

Error set(Song& song, const ParamAddress& address, int32_t value, int32_t pos = 0, int32_t length = 0) {
	deluge::edits::Context ctx{song, song.project};
	deluge::edits::SetParamValue edit{address, value, pos, length};
	return deluge::edits::apply(ctx, edit);
}

/// Whether a write of a value away from the current one lands in the engine and the record at @p address.
bool lands(Song& song, const ParamAddress& address) {
	const std::optional<int32_t> before = engine_value(song, address);
	if (!before) {
		return false;
	}
	const int32_t wanted = *before == 0x10000000 ? 0x20000000 : 0x10000000;
	if (set(song, address, wanted) != Error::NONE || engine_value(song, address) != wanted) {
		return false;
	}
	const deluge::project::DocAutoParam* record = song.project.resolveParam(address);
	return record == nullptr || record->current_value == wanted;
}

/// The stack naming the cable from @p source to @p destination on @p owner, found (or, with @p create, made) the way
/// legacy's menu reaches it.
ModelStackWithAutoParam* legacy_cable(Song& song, Clip* clip, ModControllable* owner, ParamManagerForTimeline* manager,
                                      int32_t note_row_index, NoteRow* row, PatchSource source, int32_t destination,
                                      bool create, void* memory) {
	ModelStackWithThreeMainThings* stack =
	    setupModelStackWithThreeMainThingsIncludingNoteRow(memory, &song, clip, note_row_index, row, owner, manager);
	ParamCollectionSummary* summary = manager->getPatchCableSetSummary();
	deluge::synth::CableTarget descriptor = deluge::synth::CableTarget::param(destination);
	ModelStackWithParamId* with_id =
	    stack->addParamCollectionSummary(summary)->addParamId(PatchCableSet::getParamId(descriptor, source));
	return summary->paramCollection->getAutoParamFromId(with_id, create);
}

/// The address of @p owner's cable from @p source to @p destination in @p clip.
ParamAddress cable_address(OwnerId owner, PatchSource source, int32_t destination, uint32_t clip) {
	deluge::synth::CableTarget descriptor = deluge::synth::CableTarget::param(destination);
	return {.owner = owner,
	        .space = ParamSpace::ENGINE,
	        .id = deluge::bridge::cable_depth(source, descriptor),
	        .clip_id = clip};
}

/// Whether the bridge finds, at @p stack's own address, the AutoParam @p stack names.
bool finds_again(Song& song, const ModelStackWithAutoParam& stack) {
	const std::optional<ParamAddress> address = deluge::edits::addressFor(stack);
	if (!address) {
		return false;
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* found = deluge::bridge::paramStackAt(song, *address, memory);
	return found != nullptr && found->autoParam == stack.autoParam;
}

} // namespace

// clang-format off
describe param_by_address("a param set by its address", ${
	it("resolves and writes a synth's patched param and its sound's unpatched one", _ {
		spec::SongFixture f = spec::make_synth_song();
		const OwnerId synth = OwnerId::instrument(*f.instrument->id());
		const ParamAddress lpf = address_of(synth, params::Kind::PATCHED, params::LOCAL_LPF_FREQ, f.clip->id());
		const ParamAddress bass = address_of(synth, params::Kind::UNPATCHED_SOUND, params::UNPATCHED_BASS, f.clip->id());
		expect(round_trips(*f.song, lpf)).to_be_true();
		expect(round_trips(*f.song, bass)).to_be_true();
		expect(lands(*f.song, lpf)).to_be_true();
		expect(lands(*f.song, bass)).to_be_true();
	});

	it("resolves and writes a kit row's param on its note row", _ {
		spec::SongFixture f = spec::make_kit_song();
		auto* drum = spec::add_drum<SoundDrum>(f);
		const ParamAddress lpf =
		    address_of(OwnerId::drum(*drum->id()), params::Kind::PATCHED, params::LOCAL_LPF_FREQ, f.clip->id());
		expect(round_trips(*f.song, lpf)).to_be_true();
		expect(lands(*f.song, lpf)).to_be_true();
	});

	it("resolves and writes a kit's own effects param, an audio clip's and the song's", _ {
		spec::SongFixture kit = spec::make_kit_song();
		const ParamAddress kit_lpf = address_of(OwnerId::instrument(*kit.instrument->id()),
		                                        params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_FREQ, kit.clip->id());
		expect(round_trips(*kit.song, kit_lpf)).to_be_true();
		expect(lands(*kit.song, kit_lpf)).to_be_true();

		spec::AudioSongFixture audio = spec::make_audio_clip_song();
		const ParamAddress audio_lpf =
		    address_of(OwnerId::clip(audio.clip->id()), params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_FREQ,
		               std::nullopt);
		expect(round_trips(*audio.song, audio_lpf)).to_be_true();
		expect(lands(*audio.song, audio_lpf)).to_be_true();

		const ParamAddress song_lpf =
		    address_of(OwnerId::song(), params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_FREQ, std::nullopt);
		expect(round_trips(*kit.song, song_lpf)).to_be_true();
		expect(lands(*kit.song, song_lpf)).to_be_true();
	});

	it("writes an owner made this session and never saved, without the bridge's fallback", _ {
		spec::SongFixture f = spec::make_synth_song();
#if ALPHA_OR_BETA_VERSION
		const uint32_t fallbacks = deluge::edits::docFallbackHits;
#endif
		const ParamAddress lpf = address_of(OwnerId::instrument(*f.instrument->id()), params::Kind::PATCHED,
		                                    params::LOCAL_LPF_FREQ, f.clip->id());
		expect(lands(*f.song, lpf)).to_be_true();
#if ALPHA_OR_BETA_VERSION
		expect(deluge::edits::docFallbackHits).to_equal(fallbacks);
#endif
	});

	it("raises the reverb's recompute after a sidechain shape write", _ {
		spec::SongFixture f = spec::make_synth_song();
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = false;
		const ParamAddress shape = address_of(OwnerId::instrument(*f.instrument->id()), params::Kind::UNPATCHED_SOUND,
		                                      params::UNPATCHED_SIDECHAIN_SHAPE, f.clip->id());
		expect(set(*f.song, shape, 0x30000000)).to_equal(Error::NONE);
		expect(AudioEngine::mustUpdateReverbParamsBeforeNextRender).to_be_true();
	});

	it("records automation for a turn while recording", _ {
		spec::SongFixture f = spec::make_synth_song();
		actionLogger.deleteAllLogs();
		const ParamAddress lpf = address_of(OwnerId::instrument(*f.instrument->id()), params::Kind::PATCHED,
		                                    params::LOCAL_LPF_FREQ, f.clip->id());
		const auto recording = playbackHandler.recording;
		const auto state = playbackHandler.playbackState;
		const bool armed = f.clip->armedForRecording;
		playbackHandler.recording = RecordingMode::NORMAL;
		playbackHandler.playbackState = PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		f.clip->armedForRecording = true;
		f.song->setCurrentClip(f.clip);
		const Error error = set(*f.song, lpf, 0x18000000);
		playbackHandler.recording = recording;
		playbackHandler.playbackState = state;
		f.clip->armedForRecording = armed;
		expect(error).to_equal(Error::NONE);
		const deluge::project::DocAutoParam* record = f.song->project.resolveParam(lpf);
		expect(record != nullptr && !record->nodes.empty()).to_be_true();
	});

	it("clears a param's automation by its address", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ParamAddress lpf = address_of(OwnerId::instrument(*f.instrument->id()), params::Kind::PATCHED,
		                                    params::LOCAL_LPF_FREQ, f.clip->id());
		expect(set(*f.song, lpf, 0x18000000, 0, 48)).to_equal(Error::NONE); // a region: automation
		const deluge::project::DocAutoParam* record = f.song->project.resolveParam(lpf);
		expect(record != nullptr && !record->nodes.empty()).to_be_true();
		deluge::edits::Context ctx{*f.song, f.song->project};
		deluge::edits::DeleteParamAutomation clear{lpf};
		expect(deluge::edits::apply(ctx, clear)).to_equal(Error::NONE);
		expect(f.song->project.resolveParam(lpf)->nodes.empty()).to_be_true();
	});

	it("finds a cable's depth on a synth and on a kit row", _ {
		spec::SongFixture synth = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(synth.instrument);
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithAutoParam* cable =
		    legacy_cable(*synth.song, synth.clip, sound, &synth.clip->paramManager, 0, nullptr, PatchSource::LFO_GLOBAL_1,
		                 params::LOCAL_PITCH_ADJUST, true, memory);
		expect(cable != nullptr && cable->autoParam != nullptr).to_be_true();
		expect(finds_again(*synth.song, *cable)).to_be_true();

		spec::SongFixture kit = spec::make_kit_song();
		auto* drum = spec::add_drum<SoundDrum>(kit);
		int32_t row_index = 0;
		NoteRow* row = kit.clip->getNoteRowForDrum(drum, &row_index);
		ModelStackWithAutoParam* drum_cable = legacy_cable(*kit.song, kit.clip, drum, &row->paramManager, row_index, row,
		                                                   PatchSource::ENVELOPE_0, params::LOCAL_LPF_FREQ, true, memory);
		expect(drum_cable != nullptr && drum_cable->autoParam != nullptr).to_be_true();
		expect(finds_again(*kit.song, *drum_cable)).to_be_true();
	});

	it("finds a MIDI clip's CC lane and its pitch bend", _ {
		spec::SongFixture f = spec::make_midi_song();
		auto* midi = static_cast<MIDIInstrument*>(f.instrument);
		for (const int32_t cc : {74, static_cast<int32_t>(CC_NUMBER_PITCH_BEND)}) {
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* stack = setupModelStackWithThreeMainThingsIncludingNoteRow(
			    memory, f.song, f.clip, 0, nullptr, midi, &f.clip->paramManager);
			ModelStackWithAutoParam* lane = midi->getParamToControlFromInputMIDIChannel(cc, stack);
			expect(lane != nullptr && lane->autoParam != nullptr).to_be_true();
			expect(finds_again(*f.song, *lane)).to_be_true();
		}
	});

	it("does not create a cable it cannot find", _ {
		spec::SongFixture f = spec::make_synth_song();
		PatchCableSet* set = f.clip->paramManager.getPatchCableSet();
		const uint8_t cables = set->numPatchCables;
		const ParamAddress absent = cable_address(OwnerId::instrument(*f.instrument->id()), PatchSource::LFO_GLOBAL_2,
		                                          params::LOCAL_OSC_A_VOLUME, f.clip->id());
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		expect(deluge::bridge::paramStackAt(*f.song, absent, memory) == nullptr).to_be_true();
		expect(set->numPatchCables).to_equal(cables);
	});

	it("creates a cable with the polarity asked", _ {
		spec::SongFixture f = spec::make_synth_song();
		const PatchSource source = PatchSource::ENVELOPE_1;
		const Polarity asked =
		    PatchCable::getDefaultPolarity(source) == Polarity::BIPOLAR ? Polarity::UNIPOLAR : Polarity::BIPOLAR;
		const ParamAddress address =
		    cable_address(OwnerId::instrument(*f.instrument->id()), source, params::LOCAL_LPF_RESONANCE, f.clip->id());
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithAutoParam* made = deluge::bridge::cableStackAt(*f.song, address, asked, memory);
		expect(made != nullptr && made->autoParam != nullptr).to_be_true();
		PatchCableSet* set = f.clip->paramManager.getPatchCableSet();
		deluge::synth::CableTarget descriptor = deluge::synth::CableTarget::param(params::LOCAL_LPF_RESONANCE);
		const uint8_t index = set->getPatchCableIndex(source, descriptor);
		expect(index != 255).to_be_true();
		expect(set->patchCables[index].polarity == asked).to_be_true();
		AutoParam* first = made->autoParam;
		alignas(ModelStackWithAutoParam) char again_memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithAutoParam* again = deluge::bridge::cableStackAt(*f.song, address, asked, again_memory);
		expect(again != nullptr && again->autoParam == first).to_be_true();
	});

	it("refuses a cable on a full set", _ {
		spec::SongFixture f = spec::make_synth_song();
		PatchCableSet* set = f.clip->paramManager.getPatchCableSet();
		const OwnerId owner = OwnerId::instrument(*f.instrument->id());
		// Fill the set with distinct cables, from a few sources to the patched params.
		for (const PatchSource source : {PatchSource::RANDOM, PatchSource::VELOCITY, PatchSource::NOTE}) {
			for (int32_t p = 0; set->numPatchCables < kMaxNumPatchCables && p < params::kNumParams; ++p) {
				alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
				(void)deluge::bridge::cableStackAt(*f.song, cable_address(owner, source, p, f.clip->id()),
				                                   Polarity::UNIPOLAR, memory);
			}
		}
		expect(set->numPatchCables).to_equal(static_cast<uint8_t>(kMaxNumPatchCables));
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		const ParamAddress one_more =
		    cable_address(owner, PatchSource::AFTERTOUCH, params::LOCAL_LPF_FREQ, f.clip->id());
		expect(deluge::bridge::cableStackAt(*f.song, one_more, Polarity::UNIPOLAR, memory) == nullptr).to_be_true();
		expect(set->numPatchCables).to_equal(static_cast<uint8_t>(kMaxNumPatchCables));
	});

	it("refuses an address that names nothing", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ParamAddress nowhere =
		    address_of(OwnerId::instrument(*f.instrument->id() + 1000), params::Kind::PATCHED, params::LOCAL_LPF_FREQ,
		               f.clip->id());
		expect(set(*f.song, nowhere, 0x10000000)).to_equal(Error::UNSPECIFIED);
	});
});

CPPSPEC_SPEC(param_by_address)
