/// @file
/// @brief An FM sound saved before community 1.2.0 can carry filter modes that never rendered then: a load turns them
/// off, in the record and in what the sound plays; a later save's filter modes are kept.

#include "instruments/deluge/state_access.h"
#include "integration/scenarios/drivers/tree_load.h"
#include "memory/general_memory_allocator.h"
#include "model/fx/chain_params.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/song.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "support/spec_main.h"

#include <optional>
#include <string>
#include <string_view>

using deluge::effects::FilterMode;

namespace {

/// @brief A song saved by @p firmware with one FM synth whose element names both filters' modes.
std::string fm_song_xml(std::string_view firmware) {
	return std::string{R"(<?xml version="1.0" encoding="UTF-8"?>
<song firmwareVersion=")"}
	       + std::string{firmware}
	       + R"(" earliestCompatibleFirmware="4.1.0-alpha" timePerTimerTick="229" timerTickFraction="-1342177280" inputTickMagnitude="2">
	<instruments>
		<sound presetName="FM1" presetFolder="SYNTHS" polyphonic="poly" voicePriority="1" mode="fm" lpfMode="24dB" hpfMode="HPLadder" modFXType="none">
			<osc1 type="sine" transpose="0" cents="0" retrigPhase="-1" />
			<osc2 type="sine" transpose="0" cents="0" retrigPhase="-1" />
			<unison num="1" detune="8" />
		</sound>
	</instruments>
	<sessionClips>
		<instrumentClip instrumentPresetName="FM1" instrumentPresetFolder="SYNTHS" length="1536" section="0">
			<soundParams volume="0x4CCCCCA8" pan="0x00000000" lpfFrequency="0x7FFFFFFF" />
		</instrumentClip>
	</sessionClips>
</song>
)";
}

/// @brief Load @p xml into a new song, as a song file is read; null if the read failed.
Song* load_song(std::string_view xml) {
	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	deluge::model::initGlobalParams(&song->paramManager);
	std::optional<deluge::storage::Value> tree = parseDocument(xml, deluge::storage::DocumentFormat::XML);
	if (!tree) {
		return nullptr;
	}
	deluge::storage::ValueTreeDeserializer adapter(*tree, sourceFormatOf(deluge::storage::DocumentFormat::XML));
	Error error = openTreeFile(adapter, "song", deluge::storage::DocumentFormat::XML);
	if (error == Error::NONE) {
		error = deluge::bridge::loadSong(*song, adapter);
	}
	return error == Error::NONE ? song : nullptr;
}

/// @brief The first synth of @p song, playing or hibernating.
SoundInstrument* synth_of(Song& song) {
	for (Output* first :
	     {song.live.media.first_output, static_cast<Output*>(song.live.media.first_hibernating_instrument)}) {
		for (Output* output = first; output != nullptr; output = output->next) {
			if (output->type == OutputType::SYNTH) {
				return static_cast<SoundInstrument*>(output);
			}
		}
	}
	return nullptr;
}

} // namespace

// clang-format off
describe old_fm_filter_load("an old FM sound's filter modes as a load reads them", ${
	it("turns both filters off for an FM sound saved before community 1.2.0, in its record and in what it plays", _ {
		Song* song = load_song(fm_song_xml("c1.1.0"));
		expect(song).not_().to_be_null("the loaded song");
		SoundInstrument* synth = song != nullptr ? synth_of(*song) : nullptr;
		expect(synth).not_().to_be_null("the song's synth");
		if (synth == nullptr) {
			return;
		}
		const deluge::effects::EffectStates& recorded = deluge::instrument::sound_effects(synth->state());
		expect(recorded.filter.lpf_mode).to_equal(FilterMode::OFF);
		expect(recorded.filter.hpf_mode).to_equal(FilterMode::OFF);
		expect(synth->effects().filter.lpf_mode).to_equal(FilterMode::OFF);
		expect(synth->effects().filter.hpf_mode).to_equal(FilterMode::OFF);
	});

	it("keeps the filter modes of an FM sound saved by community 1.2.0", _ {
		Song* song = load_song(fm_song_xml("c1.2.0"));
		SoundInstrument* synth = song != nullptr ? synth_of(*song) : nullptr;
		expect(synth).not_().to_be_null("the song's synth");
		if (synth == nullptr) {
			return;
		}
		expect(deluge::instrument::sound_effects(synth->state()).filter.lpf_mode).to_equal(FilterMode::TRANSISTOR_24DB);
		expect(synth->effects().filter.lpf_mode).to_equal(FilterMode::TRANSISTOR_24DB);
	});
});
// clang-format on

DELUGE_SPEC(old_fm_filter_load_spec, old_fm_filter_load)
