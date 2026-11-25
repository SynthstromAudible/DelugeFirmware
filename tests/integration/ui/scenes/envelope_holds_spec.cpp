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
/// @brief Whether a sound's envelope holds its note, by the envelope's mode: an AHD is a one-shot whatever its params
/// say, AR and MPE hold while the note does, and an ADSR's sustain, decay and release decide. The modes come from a
/// song file's `envelopeNSettings`.

#include "model/song/song.h"
#include "processing/sound/sound_instrument.h"

#include "cppspec.hpp"
#include "support/song_load.h"
#include "support/spec_main.h"

#include <string>
#include <string_view>

namespace spec = deluge::ui::scenes::spec;

namespace {

/// @brief A song of one synth whose envelope 1 has @p settings (an `envelope1Settings` element, or nothing) and
/// @p sustain, its decay no longer than its release.
std::string song_xml(std::string_view settings, std::string_view sustain) {
	return std::string{R"(<?xml version="1.0" encoding="UTF-8"?>
<song firmwareVersion="c1.3.0" earliestCompatibleFirmware="4.1.0-alpha" timePerTimerTick="229" timerTickFraction="-1342177280" inputTickMagnitude="2">
	<instruments>
		<sound presetName="ENV1" presetFolder="SYNTHS" polyphonic="poly" voicePriority="1" mode="subtractive" lpfMode="24dB" modFXType="none">
			<osc1 type="saw" transpose="0" cents="0" retrigPhase="-1" />
			<osc2 type="square" transpose="0" cents="0" retrigPhase="-1" />
			)"}
	       + std::string{settings} + R"(
			<unison num="1" detune="8" />
		</sound>
	</instruments>
	<sessionClips>
		<instrumentClip instrumentPresetName="ENV1" instrumentPresetFolder="SYNTHS" length="1536" section="0">
			<soundParams volume="0x4CCCCCA8" pan="0x00000000" oscAVolume="0x7FFFFFFF" noiseVolume="0x80000000">
				<envelope1 attack="0x80000000" decay="0x80000000" sustain=")"
	       + std::string{sustain} + R"(" release="0x10000000" />
			</soundParams>
		</instrumentClip>
	</sessionClips>
</song>
)";
}

/// @brief What the loaded song's synth says of its envelope 1: whether it sustains now, whether it ever does, and
/// whether its notes have tails. "no synth" where the song does not load.
std::string holds(std::string_view settings, std::string_view sustain) {
	Song* song = spec::load_song(song_xml(settings, sustain));
	SoundInstrument* synth = song != nullptr ? spec::first_synth(*song) : nullptr;
	Clip* clip = synth != nullptr ? song->getClipWithOutput(synth) : nullptr;
	if (clip == nullptr) {
		return "no synth";
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* stack = setupModelStackWithTimelineCounter(memory, song, clip)
	                                           ->addOtherTwoThingsButNoNoteRow(synth, &clip->paramManager);
	std::string out;
	out += synth->envelopeHasSustainCurrently(0, &clip->paramManager) ? "now " : "- ";
	out += synth->envelopeHasSustainEver(0, &clip->paramManager) ? "ever " : "- ";
	out += synth->allowNoteTails(stack->addSoundFlags(), true) ? "tails" : "-";
	return out;
}

constexpr std::string_view kNoSustain = "0x80000000";
constexpr std::string_view kFullSustain = "0x7FFFFFFF";

} // namespace

// clang-format off
describe envelope_holds("whether an envelope holds its note", ${
	it("asks a classic ADSR's sustain", _ {
		expect(holds("", kFullSustain)).to_equal(std::string{"now ever tails"});
		expect(holds("", kNoSustain)).to_equal(std::string{"- - -"});
	});

	it("never holds an AHD, however high its sustain", _ {
		expect(holds(R"(<envelope1Settings mode="ahd" curve="32" />)", kFullSustain)).to_equal(std::string{"- - -"});
		expect(holds(R"(<envelope1Settings mode="ahd" curve="32" />)", kNoSustain)).to_equal(std::string{"- - -"});
	});

	it("holds an AR and an MPE envelope whose level is 0, so a note-off releases them", _ {
		for (const std::string_view mode : {"ar", "mpe"}) {
			const std::string settings = std::string{R"(<envelope1Settings mode=")"} + std::string{mode} + R"(" curve="32" />)";
			expect(holds(settings, kNoSustain)).to_equal(std::string{"now ever tails"});
			expect(holds(settings, kFullSustain)).to_equal(std::string{"now ever tails"});
		}
	});
});
// clang-format on

DELUGE_SPEC(envelope_holds_spec, envelope_holds)
