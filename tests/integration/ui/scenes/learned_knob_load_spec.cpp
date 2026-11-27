/// @file
/// @brief A sound's learned MIDI knobs as a load reads them (through the model's codec, into the song's table) and as
///        the pitch-adjust zeroing reads them: a pitch adjust a learned knob turns keeps its preset value, whether the
///        knob is in the table, still on its owner before the table takes it, or handed over by a preset change.

#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/learn_mapping.h"
#include "model/fx/chain_params.h"
#include "model/model_stack.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/song.h"
#include "modulation/knob_target.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound_instrument.h"
#include "project/mapping_table.h"
#include "storage/storage_manager.h"
#include "synth/modulation/cable.h"

#include "integration/scenarios/drivers/driver.h"
#include "integration/scenarios/drivers/tree_load.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
#include "gui/ui/shell_host/host_ports.h"
#include "support/indexed_knobs.h"
#include "support/printers/param_target_printer.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "support/song_load.h"

#include "support/spec_main.h"

namespace spec = deluge::ui::scenes::spec;
namespace params = deluge::modulation::params;

using deluge::project::ControlsAddress;
using deluge::project::Mapping;
using deluge::project::OwnerParamTarget;
using Command = deluge::project::MidiCommand;

namespace {

/// A preset value no pitch adjust holds by default.
constexpr int32_t kSeeded = 1 << 26;

/// @brief A patched param of a sound, as the table names it.
deluge::project::ParamTarget patched(int32_t p) {
	return deluge::project::engine_target(
	    deluge::bridge::controlTargetOf(deluge::modulation::knobParam(p), params::Kind::UNPATCHED_SOUND).value);
}

/// @brief A knob on the sound pitch adjust, as a file reads it: its owner a placeholder.
Mapping pitchKnobRead() {
	return Mapping{.source = Command{.channel_or_zone = 1, .note_or_cc = 20},
	               .target = OwnerParamTarget{{}, patched(params::LOCAL_PITCH_ADJUST)}};
}

/// @brief Point every mod knob of @p sound away from the pitch adjusts, so only a learned MIDI knob can keep one.
void noPitchModKnobs(Sound& sound) {
	for (auto& button : sound.modKnobs) {
		for (auto& knob : button) {
			knob.target = deluge::modulation::knobParam(params::LOCAL_PAN);
		}
	}
}

/// @brief Give @p manager's sound and oscillator A pitch adjusts a preset value.
void seedPitch(ParamManager& manager) {
	PatchedParamSet* set = manager.getPatchedParamSet();
	for (const int32_t p : {params::LOCAL_PITCH_ADJUST, params::LOCAL_OSC_A_PITCH_ADJUST}) {
		set->params[p].setBase(*set->docForParam(p), kSeeded);
	}
}

/// @brief @p manager's preset value of patched param @p p.
int32_t valueOf(ParamManager& manager, int32_t p) {
	return manager.getPatchedParamSet()->getValue(p);
}

void activate(spec::SongFixture& f) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	f.instrument->setActiveClip(setupModelStackWithTimelineCounter(memory, f.song, f.clip));
}

/// @brief A synth built the way a preset load builds one, its params backed up with the song, not yet in it.
SoundInstrument* makeLooseSynth(::Song& song) {
	ParamManagerForTimeline params;
	auto* synth = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
	synth->setupAsDefaultSynth(&params);
	song.live.param_backup.backUp(synth, nullptr, &params, true);
	return synth;
}

/// @brief A song with one synth, on one clip, whose element holds @p midiKnobs.
std::string synth_song_xml(std::string_view midiKnobs) {
	return std::string{R"(<?xml version="1.0" encoding="UTF-8"?>
<song firmwareVersion="c1.3.0" earliestCompatibleFirmware="4.1.0-alpha" timePerTimerTick="229" timerTickFraction="-1342177280" inputTickMagnitude="2">
	<instruments>
		<sound presetName="LK1" presetFolder="SYNTHS" polyphonic="poly" voicePriority="1" mode="subtractive" lpfMode="24dB" modFXType="none">
			<osc1 type="square" transpose="0" cents="0" retrigPhase="-1" />
			<osc2 type="square" transpose="0" cents="0" retrigPhase="-1" />
			<unison num="1" detune="8" />
			<midiKnobs>
)"} + std::string{midiKnobs}
	       + R"(
			</midiKnobs>
		</sound>
	</instruments>
	<sessionClips>
		<instrumentClip instrumentPresetName="LK1" instrumentPresetFolder="SYNTHS" length="1536" section="0">
			<soundParams volume="0x4CCCCCA8" pan="0x00000000" lpfFrequency="0x7FFFFFFF">
				<equalizer bass="0x00000000" treble="0x00000000" bassFrequency="0x00000000" trebleFrequency="0x00000000" />
			</soundParams>
		</instrumentClip>
	</sessionClips>
</song>
)";
}

/// @brief @p song as it saves, as text.
std::string saved(Song& song) {
	std::vector<char> buffer(1U << 20);
	const long length = deluge::scenarios::driver::saveSongCapture(song, buffer.data(), buffer.size());
	return length > 0 ? std::string(buffer.data(), static_cast<size_t>(length)) : std::string{};
}

} // namespace

// clang-format off
describe learned_knob_load("a sound's learned knobs as a load reads them", ${
	it("puts a song's knobs in its table on the volume the runtime turns, without a cable's range, and saves them so", _ {
		Song* song = spec::load_song(synth_song_xml(R"(
				<midiKnob channel="1" ccNumber="10" relative="0" controlsParam="volume" />
				<midiKnob channel="1" ccNumber="11" relative="1" controlsParam="range" />
				<midiKnob channel="1" ccNumber="12" relative="0" controlsParam="volumePostReverbSend" patchAmountFromSource="compressor" />
				<midiKnob channel="1" ccNumber="13" relative="1" controlsParam="pitch" />)"));
		expect(song).not_().to_be_null("the loaded song");
		SoundInstrument* synth = song != nullptr ? spec::first_synth(*song) : nullptr;
		expect(synth).not_().to_be_null("the song's synth");
		if (synth != nullptr) {
			expect(synth->id().has_value()).to_be_true("the synth's id");
		}
		if (synth == nullptr || !synth->id()) {
			return;
		}
		const ControlsAddress owner{ControlsAddress::Kind::OUTPUT, *synth->id()};
		const std::vector<Mapping> knobs = song->project.mappings.params_of(owner);
		expect(static_cast<int>(knobs.size())).to_equal(3);
		if (knobs.size() != 3) {
			return;
		}
		const auto paramOf = [](const Mapping& knob) { return std::get<OwnerParamTarget>(knob.target).param; };
		expect(paramOf(knobs[0])).to_equal(patched(params::GLOBAL_VOLUME_POST_FX), "the first knob's param");
		expect(paramOf(knobs[1]))
		    .to_equal(deluge::project::engine_target(
		                  deluge::bridge::controlTargetOf(
		                      deluge::modulation::knobTargetOf(deluge::synth::CableTarget::depth_of(
		                          params::GLOBAL_VOLUME_POST_REVERB_SEND, PatchSource::SIDECHAIN)),
		                      params::Kind::UNPATCHED_SOUND)
		                      .value),
		              "the second knob's param");
		expect(paramOf(knobs[2])).to_equal(patched(params::LOCAL_PITCH_ADJUST), "the third knob's param");
		expect(knobs[2].relative).to_be_true();
		expect(synth->effectsOwner()->knobs->loaded.empty()).to_be_true(); // the table holds them now
		// Incoming MIDI finds each, as its own param, once the song is the current one.
		::Song* const before = currentSong;
		spec::make_current(song);
		for (const Mapping& knob : knobs) {
			expect(spec::indexPlays(*song, std::get<OwnerParamTarget>(knob.target), knob.source.channel_or_zone,
			                        knob.source.note_or_cc))
			    .to_be_true();
		}
		spec::make_current(before);
		const std::string text = saved(*song); // a song saves as JSON
		expect(text).to_contain(R"("controlsParam": "volumePostFX")", "the saved song");
		expect(text).not_().to_contain(R"("controlsParam": "volume")", "the saved song");
		expect(text).not_().to_contain(R"("ccNumber": 11)", "the saved song");
	});

	it("keeps a pitch adjust a learned knob in the table turns, and zeroes one no knob turns", _ {
		spec::SongFixture f = spec::make_synth_song();
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		noPitchModKnobs(sound);
		deluge::edits::learnMappingFor(pitchKnobRead().source,
		                               OwnerParamTarget{{ControlsAddress::Kind::OUTPUT, *sound.id()},
		                                                patched(params::LOCAL_PITCH_ADJUST)});
		// The zeroing that tells a knobbed pitch adjust from one without a knob is the minimal-details pass (a clip's
		// own pass asks its cable set, which holds no pitch adjust, and zeroes nothing).
		seedPitch(f.clip->paramManager);
		sound.ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(&f.clip->paramManager, f.song);
		expect(valueOf(f.clip->paramManager, params::LOCAL_PITCH_ADJUST)).to_equal(kSeeded);
		expect(valueOf(f.clip->paramManager, params::LOCAL_OSC_A_PITCH_ADJUST)).to_equal(0);
	});

	it("keeps a pitch adjust a knob its preset's file read turns, before and after the song takes the preset", _ {
		spec::SongFixture f = spec::make_synth_song();
		SoundInstrument* preset = makeLooseSynth(*f.song);
		noPitchModKnobs(*preset);
		ParamManagerForTimeline presetParams;
		expect(int(presetParams.setupWithPatching())).to_equal(int(Error::NONE), "the preset's param setup");
		Sound::initParams(&presetParams);
		seedPitch(presetParams);
		preset->ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(&presetParams, f.song);
		expect(valueOf(presetParams, params::LOCAL_PITCH_ADJUST)).to_equal(0); // no knob turns it yet

		preset->effectsOwner()->knobs->loaded.push_back(pitchKnobRead());
		seedPitch(presetParams);
		preset->ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(&presetParams, f.song);
		expect(valueOf(presetParams, params::LOCAL_PITCH_ADJUST)).to_equal(kSeeded);
		expect(valueOf(presetParams, params::LOCAL_OSC_A_PITCH_ADJUST)).to_equal(0);

		f.song->addOutput(preset);
		expect(preset->effectsOwner()->knobs->loaded.empty()).to_be_true();
		seedPitch(presetParams);
		preset->ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(&presetParams, f.song);
		expect(valueOf(presetParams, params::LOCAL_PITCH_ADJUST)).to_equal(kSeeded);
	});

	it("keeps the pitch adjust of a preset a preset change hands a learned knob on it to", _ {
		spec::SongFixture f = spec::make_synth_song();
		activate(f);
		deluge::edits::learnMappingFor(pitchKnobRead().source,
		                               OwnerParamTarget{{ControlsAddress::Kind::OUTPUT, *f.instrument->id()},
		                                                patched(params::LOCAL_PITCH_ADJUST)});
		SoundInstrument* preset = makeLooseSynth(*f.song);
		noPitchModKnobs(*preset);
		ParamManagerForTimeline presetParams;
		expect(int(presetParams.setupWithPatching())).to_equal(int(Error::NONE), "the preset's param setup");
		Sound::initParams(&presetParams);

		// A preset change hands the knob over in the table before the preset joins the song's lists, and the clips'
		// change of instrument sets the preset up meanwhile: the knob at its id keeps its pitch adjust then.
		constexpr uint32_t kPresetId = 0xBEEF;
		preset->setId(kPresetId);
		deluge::bridge::handOverInstrument(*f.song, *f.instrument->id(), kPresetId, true);
		seedPitch(presetParams);
		preset->ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(&presetParams, f.song);
		expect(valueOf(presetParams, params::LOCAL_PITCH_ADJUST)).to_equal(kSeeded);
		expect(valueOf(presetParams, params::LOCAL_OSC_A_PITCH_ADJUST)).to_equal(0);
	});

	it("keeps the pitch adjust of the preset a whole preset change hands a learned knob to", _ {
		spec::SongFixture f = spec::make_synth_song();
		activate(f);
		auto& old = *static_cast<SoundInstrument*>(f.instrument);
		noPitchModKnobs(old);
		deluge::edits::learnMappingFor(pitchKnobRead().source,
		                               OwnerParamTarget{{ControlsAddress::Kind::OUTPUT, *old.id()},
		                                                patched(params::LOCAL_PITCH_ADJUST)});
		SoundInstrument* preset = makeLooseSynth(*f.song);
		noPitchModKnobs(*preset);
		f.song->replaceInstrument(&old, preset); // the replaced synth, never edited, is deleted
		ParamManagerForTimeline sounds;
		expect(int(sounds.setupWithPatching())).to_equal(int(Error::NONE), "the sounds' param setup");
		Sound::initParams(&sounds);
		seedPitch(sounds);
		preset->ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(&sounds, f.song);
		expect(valueOf(sounds, params::LOCAL_PITCH_ADJUST)).to_equal(kSeeded);
		expect(valueOf(sounds, params::LOCAL_OSC_A_PITCH_ADJUST)).to_equal(0);
	});

	it("leaves a pitch adjust as it is when its learned knob is unlearned, as next does and main does not", _ {
		// Accepted difference (tests/ui-scripts/parity/DIFFERENCES.md): main's `ModControllableAudio::unlearnKnobs`
		// (`origin/main:src/deluge/model/mod_controllable/mod_controllable_audio.cpp`) zeroes a pitch adjust no knob
		// turns any more, in the sound's params parked without a clip; the menu's unlearn here is a table edit alone.
		spec::SongFixture f = spec::make_synth_song();
		activate(f);
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		noPitchModKnobs(sound);
		const OwnerParamTarget knob{{ControlsAddress::Kind::OUTPUT, *sound.id()}, patched(params::LOCAL_PITCH_ADJUST)};
		deluge::edits::learnMappingFor(pitchKnobRead().source, knob);
		ParamManagerForTimeline parked;
		expect(int(parked.setupWithPatching())).to_equal(int(Error::NONE), "the parked params' setup");
		Sound::initParams(&parked);
		seedPitch(parked);
		f.song->live.param_backup.backUp(f.instrument, nullptr, &parked, true);
		ParamManager* backed = nullptr;
		f.song->live.param_backup.forEachOf(f.instrument, [&](std::optional<uint32_t> clip, ParamManager& manager) {
			if (!clip) {
				backed = &manager;
			}
		});
		expect(backed).not_().to_be_null("the backed-up params");
		if (backed == nullptr) {
			return;
		}
		expect(deluge::gui::shell_host::host_ports().learn->learn(knob, std::nullopt, std::nullopt)).to_be_true();
		expect(f.song->project.mappings.find(knob)).to_be_null("the unlearned knob's mapping");
		expect(valueOf(*backed, params::LOCAL_PITCH_ADJUST)).to_equal(kSeeded);
		// The zeroing main's unlearn runs does reach those params: with no knob left, it zeroes the pitch adjust.
		sound.ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(f.song);
		expect(valueOf(*backed, params::LOCAL_PITCH_ADJUST)).to_equal(0);
	});
});
// clang-format on

DELUGE_SPEC(learned_knob_load_spec, learned_knob_load)
