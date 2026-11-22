/// @file
/// @brief An FM sound saved before community 1.2.0 can carry filter modes that never rendered then: a load turns them
/// off, in the record and in what the sound plays; a later save's filter modes are kept.

#include "instruments/classic/state_access.h"
#include "model/song/song.h"
#include "processing/sound/sound_instrument.h"

#include "cppspec.hpp"
#include "support/song_load.h"
#include "support/spec_main.h"

#include <optional>
#include <string>
#include <string_view>

namespace spec = deluge::scenes::spec;
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

} // namespace

// clang-format off
describe old_fm_filter_load("an old FM sound's filter modes as a load reads them", ${
	it("turns both filters off for an FM sound saved before community 1.2.0, in its record and in what it plays", _ {
		Song* song = spec::load_song(fm_song_xml("c1.1.0"));
		expect(song).not_().to_be_null("the loaded song");
		SoundInstrument* synth = song != nullptr ? spec::first_synth(*song) : nullptr;
		expect(synth).not_().to_be_null("the song's synth");
		if (synth == nullptr) {
			return;
		}
		const deluge::effects::EffectStates& recorded = deluge::instruments::classic::sound_effects(synth->state());
		expect(recorded.filter.lpf_mode).to_equal(FilterMode::OFF);
		expect(recorded.filter.hpf_mode).to_equal(FilterMode::OFF);
		expect(synth->effects().filter.lpf_mode).to_equal(FilterMode::OFF);
		expect(synth->effects().filter.hpf_mode).to_equal(FilterMode::OFF);
	});

	it("keeps the filter modes of an FM sound saved by community 1.2.0", _ {
		Song* song = spec::load_song(fm_song_xml("c1.2.0"));
		SoundInstrument* synth = song != nullptr ? spec::first_synth(*song) : nullptr;
		expect(synth).not_().to_be_null("the song's synth");
		if (synth == nullptr) {
			return;
		}
		expect(deluge::instruments::classic::sound_effects(synth->state()).filter.lpf_mode).to_equal(FilterMode::TRANSISTOR_24DB);
		expect(synth->effects().filter.lpf_mode).to_equal(FilterMode::TRANSISTOR_24DB);
	});
});
// clang-format on

DELUGE_SPEC(old_fm_filter_load_spec, old_fm_filter_load)
