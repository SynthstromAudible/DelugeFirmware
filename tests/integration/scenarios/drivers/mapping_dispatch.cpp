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
/// @brief The mapping index against the scans incoming MIDI matched learned bindings with: a seeded property.
///
/// A small song (two synths, a kit of sound, MIDI and gate rows, a hibernating kit of a MIDI and a gate row, a MIDI
/// track, an audio track, session clips and an arrangement-only clip) is given random mapping tables: every target
/// kind, channels and MPE zones with their CC and program-change encodings, sources from any device, from DIN, from
/// upstream USB or from a hosted device the Deluge does not know. Each table is projected onto the engine fields and
/// the index rebuilt. Each random message (from DIN or upstream USB, with inputs told apart by device or not, and
/// random MPE zones on both cables) is then matched by the index and by a transcription of the scans the dispatch makes
/// over the engine fields, and the two sequences of hits (target and position, in order) must be equal.
///
/// The transcription gives a source naming a device the Deluge does not know a cable of its own that no message
/// arrives on, as a song's load gives such a device a placeholder entry.
///
/// No arguments. Prints `MAPPING-DISPATCH cases=<n> mismatches=<m> ...` and exits 0 when no message's hits differ and
/// every kind of hit was seen.

#include "definitions_cxx.hpp"
#include "deluge-project/arrangement.h"
#include "deluge-project/controller_mapping.h"
#include "deluge-project/mapping_table.h"
#include "deluge-project/midi_command.h"
#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/learned_midi.h"
#include "io/midi/mapping_index.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/clip.h"
#include "model/drum/drum.h"
#include "model/global_effectable/global_effectable.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/output.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/project_bridge/midi_command.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "processing/engines/audio_engine.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/drivers/tree_load.h"
#include "storage/document_format.h"
#include "storage/storage_manager.h"
#include "storage/value_tree_deserializer.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

using deluge::project::ControlsAddress;
using deluge::project::Mapping;
using deluge::project::MappingTable;
using deluge::project::MappingTarget;
using deluge::project::MidiCommand;
using deluge::project::MidiDeviceRef;
namespace params = deluge::modulation::params;

constexpr uint32_t kSeed = 0x5eed1c00;
constexpr int32_t kTables = 300;
constexpr int32_t kMessagesPerTable = 40;

constexpr const char* kSongText = R"(<?xml version="1.0" encoding="UTF-8"?>
<song firmwareVersion="c1.3.0" earliestCompatibleFirmware="4.1.0-alpha" timePerTimerTick="229" timerTickFraction="-1342177280" inputTickMagnitude="2">
	<instruments>
		<sound presetName="MDS1" presetFolder="SYNTHS" polyphonic="poly" voicePriority="1" mode="subtractive" lpfMode="24dB" modFXType="none">
			<osc1 type="square" transpose="0" cents="0" retrigPhase="-1" />
			<osc2 type="square" transpose="0" cents="0" retrigPhase="-1" />
			<unison num="1" detune="8" />
		</sound>
		<sound presetName="MDS2" presetFolder="SYNTHS" clipInstances="0x000000000000060080000000" polyphonic="poly" voicePriority="1" mode="subtractive" lpfMode="24dB" modFXType="none">
			<osc1 type="saw" transpose="0" cents="0" retrigPhase="-1" />
			<osc2 type="square" transpose="0" cents="0" retrigPhase="-1" />
			<unison num="1" detune="8" />
		</sound>
		<kit presetName="MDK1" presetFolder="KITS" defaultVelocity="100" modFXCurrentParam="feedback" modFXType="none" lpfMode="24dB" hpfMode="HPLadder" filterRoute="H2L">
			<delay pingPong="1" analog="0" syncLevel="7" syncType="0" />
			<audioCompressor attack="83886080" release="83886080" thresh="0" ratio="1073741824" compHPF="0" compBlend="2147483647" />
			<soundSources>
				<sound name="MDD1" polyphonic="poly" voicePriority="1" mode="subtractive" lpfMode="24dB" modFXType="none">
					<osc1 type="square" transpose="0" cents="0" retrigPhase="-1" />
					<osc2 type="square" transpose="0" cents="0" retrigPhase="-1" />
					<unison num="1" detune="8" />
				</sound>
				<sound name="MDD2" polyphonic="poly" voicePriority="1" mode="subtractive" lpfMode="24dB" modFXType="none">
					<osc1 type="saw" transpose="0" cents="0" retrigPhase="-1" />
					<osc2 type="square" transpose="0" cents="0" retrigPhase="-1" />
					<unison num="1" detune="8" />
				</sound>
				<midiOutput channel="1" note="40" />
				<gateOutput channel="0" />
				<sound name="MDD3" polyphonic="poly" voicePriority="1" mode="subtractive" lpfMode="24dB" modFXType="none">
					<osc1 type="sine" transpose="0" cents="0" retrigPhase="-1" />
					<osc2 type="square" transpose="0" cents="0" retrigPhase="-1" />
					<unison num="1" detune="8" />
				</sound>
			</soundSources>
		</kit>
		<midiChannel channel="2" />
		<audioTrack name="MDAUD" modFXCurrentParam="feedback" modFXType="none" lpfMode="24dB" hpfMode="HPLadder" filterRoute="H2L">
			<delay pingPong="1" analog="0" syncLevel="7" syncType="0" />
			<audioCompressor attack="83886080" release="83886080" thresh="0" ratio="1073741824" compHPF="0" compBlend="2147483647" />
		</audioTrack>
	</instruments>
	<sessionClips>
		<instrumentClip instrumentPresetName="MDS1" instrumentPresetFolder="SYNTHS" isPlaying="1" length="1536" section="0">
			<soundParams arpeggiatorGate="0x00000000" portamento="0x80000000" compressorShape="0xDC28F5B2" oscAVolume="0x7FFFFFFF" oscAPulseWidth="0x00000000" oscBVolume="0x80000000" oscBPulseWidth="0x00000000" noiseVolume="0x80000000" volume="0x4CCCCCA8" pan="0x00000000" lpfFrequency="0x7FFFFFFF" lpfResonance="0x80000000" hpfFrequency="0x80000000" hpfResonance="0x80000000" lfo1Rate="0x1999997E" lfo2Rate="0x00000000" modulator1Amount="0x80000000" modulator1Feedback="0x80000000" modulator2Amount="0x80000000" modulator2Feedback="0x80000000" carrier1Feedback="0x80000000" carrier2Feedback="0x80000000" modFXRate="0x00000000" modFXDepth="0x00000000" delayRate="0x00000000" delayFeedback="0x80000000" reverbAmount="0x80000000" arpeggiatorRate="0x00000000" stutterRate="0x00000000" sampleRateReduction="0x80000000" bitCrush="0x80000000" modFXOffset="0x00000000" modFXFeedback="0x00000000" waveFold="0x00000000">
				<envelope1 attack="0x80000000" decay="0xE6666654" sustain="0x7FFFFFFF" release="0x80000000" />
				<envelope2 attack="0xE6666654" decay="0xE6666654" sustain="0xFFFFFFE9" release="0xE6666654" />
				<patchCables>
					<patchCable source="velocity" destination="volume" amount="0x3FFFFFE8" />
				</patchCables>
				<equalizer bass="0x00000000" treble="0x00000000" bassFrequency="0x00000000" trebleFrequency="0x00000000" />
			</soundParams>
		</instrumentClip>
		<instrumentClip instrumentPresetName="MDS1" instrumentPresetFolder="SYNTHS" length="1536" section="1">
			<soundParams arpeggiatorGate="0x00000000" portamento="0x80000000" compressorShape="0xDC28F5B2" oscAVolume="0x7FFFFFFF" oscAPulseWidth="0x00000000" oscBVolume="0x80000000" oscBPulseWidth="0x00000000" noiseVolume="0x80000000" volume="0x4CCCCCA8" pan="0x00000000" lpfFrequency="0x7FFFFFFF" lpfResonance="0x80000000" hpfFrequency="0x80000000" hpfResonance="0x80000000" lfo1Rate="0x1999997E" lfo2Rate="0x00000000" modulator1Amount="0x80000000" modulator1Feedback="0x80000000" modulator2Amount="0x80000000" modulator2Feedback="0x80000000" carrier1Feedback="0x80000000" carrier2Feedback="0x80000000" modFXRate="0x00000000" modFXDepth="0x00000000" delayRate="0x00000000" delayFeedback="0x80000000" reverbAmount="0x80000000" arpeggiatorRate="0x00000000" stutterRate="0x00000000" sampleRateReduction="0x80000000" bitCrush="0x80000000" modFXOffset="0x00000000" modFXFeedback="0x00000000" waveFold="0x00000000">
				<envelope1 attack="0x80000000" decay="0xE6666654" sustain="0x7FFFFFFF" release="0x80000000" />
				<envelope2 attack="0xE6666654" decay="0xE6666654" sustain="0xFFFFFFE9" release="0xE6666654" />
				<patchCables>
					<patchCable source="velocity" destination="volume" amount="0x3FFFFFE8" />
				</patchCables>
				<equalizer bass="0x00000000" treble="0x00000000" bassFrequency="0x00000000" trebleFrequency="0x00000000" />
			</soundParams>
		</instrumentClip>
		<instrumentClip instrumentPresetName="MDS2" instrumentPresetFolder="SYNTHS" length="1536" section="0">
			<soundParams arpeggiatorGate="0x00000000" portamento="0x80000000" compressorShape="0xDC28F5B2" oscAVolume="0x7FFFFFFF" oscAPulseWidth="0x00000000" oscBVolume="0x80000000" oscBPulseWidth="0x00000000" noiseVolume="0x80000000" volume="0x4CCCCCA8" pan="0x00000000" lpfFrequency="0x7FFFFFFF" lpfResonance="0x80000000" hpfFrequency="0x80000000" hpfResonance="0x80000000" lfo1Rate="0x1999997E" lfo2Rate="0x00000000" modulator1Amount="0x80000000" modulator1Feedback="0x80000000" modulator2Amount="0x80000000" modulator2Feedback="0x80000000" carrier1Feedback="0x80000000" carrier2Feedback="0x80000000" modFXRate="0x00000000" modFXDepth="0x00000000" delayRate="0x00000000" delayFeedback="0x80000000" reverbAmount="0x80000000" arpeggiatorRate="0x00000000" stutterRate="0x00000000" sampleRateReduction="0x80000000" bitCrush="0x80000000" modFXOffset="0x00000000" modFXFeedback="0x00000000" waveFold="0x00000000">
				<envelope1 attack="0x80000000" decay="0xE6666654" sustain="0x7FFFFFFF" release="0x80000000" />
				<envelope2 attack="0xE6666654" decay="0xE6666654" sustain="0xFFFFFFE9" release="0xE6666654" />
				<patchCables>
					<patchCable source="velocity" destination="volume" amount="0x3FFFFFE8" />
				</patchCables>
				<equalizer bass="0x00000000" treble="0x00000000" bassFrequency="0x00000000" trebleFrequency="0x00000000" />
			</soundParams>
		</instrumentClip>
		<instrumentClip instrumentPresetName="MDK1" instrumentPresetFolder="KITS" isPlaying="1" length="1536" section="2">
			<kitParams reverbAmount="0x80000000" volume="0x00000000" pan="0x00000000" modFXDepth="0x00000000" modFXRate="0xE0000000" sampleRateReduction="0x80000000" bitCrush="0x80000000" compressorThreshold="0x00000000"><delay rate="0x00000000" feedback="0x80000000" /><lpf frequency="0x7FFFFFFF" resonance="0x80000000" /><hpf frequency="0x80000000" resonance="0x80000000" /><equalizer bass="0x00000000" treble="0x00000000" bassFrequency="0x00000000" trebleFrequency="0x00000000" /></kitParams>
			<noteRows>
				<noteRow drumIndex="0">
					<soundParams arpeggiatorGate="0x00000000" portamento="0x80000000" compressorShape="0xDC28F5B2" oscAVolume="0x7FFFFFFF" oscAPulseWidth="0x00000000" oscBVolume="0x80000000" oscBPulseWidth="0x00000000" noiseVolume="0x80000000" volume="0x4CCCCCA8" pan="0x00000000" lpfFrequency="0x7FFFFFFF" lpfResonance="0x80000000" hpfFrequency="0x80000000" hpfResonance="0x80000000" lfo1Rate="0x1999997E" lfo2Rate="0x00000000" modulator1Amount="0x80000000" modulator1Feedback="0x80000000" modulator2Amount="0x80000000" modulator2Feedback="0x80000000" carrier1Feedback="0x80000000" carrier2Feedback="0x80000000" modFXRate="0x00000000" modFXDepth="0x00000000" delayRate="0x00000000" delayFeedback="0x80000000" reverbAmount="0x80000000" arpeggiatorRate="0x00000000" stutterRate="0x00000000" sampleRateReduction="0x80000000" bitCrush="0x80000000" modFXOffset="0x00000000" modFXFeedback="0x00000000" waveFold="0x00000000">
						<envelope1 attack="0x80000000" decay="0xE6666654" sustain="0x7FFFFFFF" release="0x80000000" />
						<envelope2 attack="0xE6666654" decay="0xE6666654" sustain="0xFFFFFFE9" release="0xE6666654" />
						<patchCables>
							<patchCable source="velocity" destination="volume" amount="0x3FFFFFE8" />
						</patchCables>
						<equalizer bass="0x00000000" treble="0x00000000" bassFrequency="0x00000000" trebleFrequency="0x00000000" />
					</soundParams>
				</noteRow>
				<noteRow drumIndex="1">
					<soundParams arpeggiatorGate="0x00000000" portamento="0x80000000" compressorShape="0xDC28F5B2" oscAVolume="0x7FFFFFFF" oscAPulseWidth="0x00000000" oscBVolume="0x80000000" oscBPulseWidth="0x00000000" noiseVolume="0x80000000" volume="0x4CCCCCA8" pan="0x00000000" lpfFrequency="0x7FFFFFFF" lpfResonance="0x80000000" hpfFrequency="0x80000000" hpfResonance="0x80000000" lfo1Rate="0x1999997E" lfo2Rate="0x00000000" modulator1Amount="0x80000000" modulator1Feedback="0x80000000" modulator2Amount="0x80000000" modulator2Feedback="0x80000000" carrier1Feedback="0x80000000" carrier2Feedback="0x80000000" modFXRate="0x00000000" modFXDepth="0x00000000" delayRate="0x00000000" delayFeedback="0x80000000" reverbAmount="0x80000000" arpeggiatorRate="0x00000000" stutterRate="0x00000000" sampleRateReduction="0x80000000" bitCrush="0x80000000" modFXOffset="0x00000000" modFXFeedback="0x00000000" waveFold="0x00000000">
						<envelope1 attack="0x80000000" decay="0xE6666654" sustain="0x7FFFFFFF" release="0x80000000" />
						<envelope2 attack="0xE6666654" decay="0xE6666654" sustain="0xFFFFFFE9" release="0xE6666654" />
						<patchCables>
							<patchCable source="velocity" destination="volume" amount="0x3FFFFFE8" />
						</patchCables>
						<equalizer bass="0x00000000" treble="0x00000000" bassFrequency="0x00000000" trebleFrequency="0x00000000" />
					</soundParams>
				</noteRow>
				<noteRow drumIndex="2" />
				<noteRow drumIndex="3" />
				<noteRow drumIndex="4">
					<soundParams arpeggiatorGate="0x00000000" portamento="0x80000000" compressorShape="0xDC28F5B2" oscAVolume="0x7FFFFFFF" oscAPulseWidth="0x00000000" oscBVolume="0x80000000" oscBPulseWidth="0x00000000" noiseVolume="0x80000000" volume="0x4CCCCCA8" pan="0x00000000" lpfFrequency="0x7FFFFFFF" lpfResonance="0x80000000" hpfFrequency="0x80000000" hpfResonance="0x80000000" lfo1Rate="0x1999997E" lfo2Rate="0x00000000" modulator1Amount="0x80000000" modulator1Feedback="0x80000000" modulator2Amount="0x80000000" modulator2Feedback="0x80000000" carrier1Feedback="0x80000000" carrier2Feedback="0x80000000" modFXRate="0x00000000" modFXDepth="0x00000000" delayRate="0x00000000" delayFeedback="0x80000000" reverbAmount="0x80000000" arpeggiatorRate="0x00000000" stutterRate="0x00000000" sampleRateReduction="0x80000000" bitCrush="0x80000000" modFXOffset="0x00000000" modFXFeedback="0x00000000" waveFold="0x00000000">
						<envelope1 attack="0x80000000" decay="0xE6666654" sustain="0x7FFFFFFF" release="0x80000000" />
						<envelope2 attack="0xE6666654" decay="0xE6666654" sustain="0xFFFFFFE9" release="0xE6666654" />
						<patchCables>
							<patchCable source="velocity" destination="volume" amount="0x3FFFFFE8" />
						</patchCables>
						<equalizer bass="0x00000000" treble="0x00000000" bassFrequency="0x00000000" trebleFrequency="0x00000000" />
					</soundParams>
				</noteRow>
			</noteRows>
		</instrumentClip>
		<instrumentClip midiChannel="2" length="1536" section="1" />
		<audioClip trackName="MDAUD" length="1536" section="0">
			<params reverbAmount="0x80000000" volume="0x00000000" pan="0x00000000" modFXDepth="0x00000000" modFXRate="0xE0000000" sampleRateReduction="0x80000000" bitCrush="0x80000000" compressorThreshold="0x00000000"><delay rate="0x00000000" feedback="0x80000000" /><lpf frequency="0x7FFFFFFF" resonance="0x80000000" /><hpf frequency="0x80000000" resonance="0x80000000" /><equalizer bass="0x00000000" treble="0x00000000" bassFrequency="0x00000000" trebleFrequency="0x00000000" /></params>
		</audioClip>
	</sessionClips>
	<arrangementOnlyTracks>
		<instrumentClip instrumentPresetName="MDS2" instrumentPresetFolder="SYNTHS" length="1536" section="0">
			<soundParams arpeggiatorGate="0x00000000" portamento="0x80000000" compressorShape="0xDC28F5B2" oscAVolume="0x7FFFFFFF" oscAPulseWidth="0x00000000" oscBVolume="0x80000000" oscBPulseWidth="0x00000000" noiseVolume="0x80000000" volume="0x4CCCCCA8" pan="0x00000000" lpfFrequency="0x7FFFFFFF" lpfResonance="0x80000000" hpfFrequency="0x80000000" hpfResonance="0x80000000" lfo1Rate="0x1999997E" lfo2Rate="0x00000000" modulator1Amount="0x80000000" modulator1Feedback="0x80000000" modulator2Amount="0x80000000" modulator2Feedback="0x80000000" carrier1Feedback="0x80000000" carrier2Feedback="0x80000000" modFXRate="0x00000000" modFXDepth="0x00000000" delayRate="0x00000000" delayFeedback="0x80000000" reverbAmount="0x80000000" arpeggiatorRate="0x00000000" stutterRate="0x00000000" sampleRateReduction="0x80000000" bitCrush="0x80000000" modFXOffset="0x00000000" modFXFeedback="0x00000000" waveFold="0x00000000">
				<envelope1 attack="0x80000000" decay="0xE6666654" sustain="0x7FFFFFFF" release="0x80000000" />
				<envelope2 attack="0xE6666654" decay="0xE6666654" sustain="0xFFFFFFE9" release="0xE6666654" />
				<patchCables>
					<patchCable source="velocity" destination="volume" amount="0x3FFFFFE8" />
				</patchCables>
				<equalizer bass="0x00000000" treble="0x00000000" bassFrequency="0x00000000" trebleFrequency="0x00000000" />
			</soundParams>
		</instrumentClip>
	</arrangementOnlyTracks>
</song>
)";

/// The song in @p text, loaded as the app loads a song and made current; nullptr on a load failure, which is printed to
/// @p err.
Song* loadSong(std::string_view text, FILE* err) {
	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);
	std::optional<deluge::storage::Value> tree = parseDocument(text, deluge::storage::DocumentFormat::XML);
	if (!tree) {
		std::fprintf(err, "MAPPING-DISPATCH FAIL the song does not parse\n");
		return nullptr;
	}
	deluge::storage::ValueTreeDeserializer adapter(*tree, deluge::storage::SourceFormat::Xml);
	activeReader = &adapter;
	Error e = openTreeFile(adapter, "song");
	if (e == Error::NONE) {
		e = deluge::bridge::loadSong(*song, adapter);
	}
	activeReader = &smDeserializer;
	if (e != Error::NONE) {
		std::fprintf(err, "MAPPING-DISPATCH FAIL the song's load failed: error %d\n", static_cast<int>(e));
		return nullptr;
	}
	currentSong = song;
	song->setCurrentClip(nullptr);
	AudioEngine::getReverbParamsFromSong(song);
	return song;
}

/// The table's name for patched param @p p of a sound, or (when @p unpatchedKind is given) unpatched param @p p of that
/// kind.
deluge::engine::ParamId knobParam(int32_t p, std::optional<params::Kind> unpatchedKind = std::nullopt) {
	ParamDescriptor descriptor;
	descriptor.setToHaveParamOnly(unpatchedKind ? params::UNPATCHED_START + p : p);
	return deluge::bridge::knobTargetOf(descriptor, unpatchedKind.value_or(params::Kind::UNPATCHED_SOUND));
}

/// One match: what it reaches, and its position (a section's number, a clip's session index, a knob's place in its
/// owner's list, a row's place in its kit).
struct Hit {
	MappingTarget target;
	uint32_t ordinal;
	bool operator==(const Hit&) const = default;
};

std::string describe(const Hit& hit) {
	char buf[64];
	std::visit(
	    [&]<typename T>(const T& t) {
		    if constexpr (std::is_same_v<T, deluge::project::ParamTarget>) {
			    std::snprintf(buf, sizeof buf, "knob(%d:%u)#%u", static_cast<int>(t.owner.kind), t.owner.id,
			                  hit.ordinal);
		    }
		    else if constexpr (std::is_same_v<T, deluge::project::ClipMuteTarget>) {
			    std::snprintf(buf, sizeof buf, "clip(%u)#%u", t.clip_id, hit.ordinal);
		    }
		    else if constexpr (std::is_same_v<T, deluge::project::DrumMuteTarget>) {
			    std::snprintf(buf, sizeof buf, "rowmute(%u)#%u", t.drum_id, hit.ordinal);
		    }
		    else if constexpr (std::is_same_v<T, deluge::project::SectionLaunchTarget>) {
			    std::snprintf(buf, sizeof buf, "section(%u)", static_cast<unsigned>(t.section));
		    }
		    else if constexpr (std::is_same_v<T, deluge::project::TrackInputTarget>) {
			    std::snprintf(buf, sizeof buf, "track(%u)", t.output_id);
		    }
		    else {
			    std::snprintf(buf, sizeof buf, "rowinput(%u)#%u", t.drum_id, hit.ordinal);
		    }
	    },
	    hit.target);
	return buf;
}

std::string describe(const std::vector<Hit>& hits) {
	std::string out = "[";
	for (const Hit& hit : hits) {
		out += (out.size() > 1 ? " " : "") + describe(hit);
	}
	return out + "]";
}

/// The cable the transcription gives a source naming a device the Deluge does not know: no message arrives on it.
MIDICableUSBHosted g_placeholder;

/// @p field as a legacy load leaves it: when the table's entry @p entry names a device the Deluge has no cable for, the
/// field listens to that device's placeholder.
LearnedMIDI legacyField(LearnedMIDI field, const Mapping* entry) {
	if (entry != nullptr && entry->source.device && deluge::bridge::find_cable(*entry->source.device) == nullptr) {
		field.cable = &g_placeholder;
	}
	return field;
}

/// What the song holds that bindings can reach.
struct Fixture {
	Song* song{};
	std::vector<uint32_t> clipIds;   ///< session and arrangement-only
	std::vector<uint32_t> outputIds; ///< playing and hibernating
	std::vector<uint32_t> kitIds;    ///< playing and hibernating
	std::vector<uint32_t> drumIds;   ///< every kit's
	std::vector<ControlsAddress> owners;
	std::vector<bool> ownerIsSound;
};

bool playing(Song& song, const Output* output) {
	for (Output* o = song.live.media.first_output; o != nullptr; o = o->next) {
		if (o == output) {
			return true;
		}
	}
	return false;
}

/// The oracle: today's scan of sections and session clips (PlaybackHandler::offerNoteToLearnedThings).
std::vector<Hit> legacyCommands(Song& song, MIDICable& cable, int32_t channel, int32_t note) {
	std::vector<Hit> hits;
	for (int32_t s = 0; s < kMaxNumSections; s++) {
		if (deluge::bridge::matches(deluge::bridge::sectionCommand(song.project.mappings, s), cable, channel, note)) {
			hits.push_back({deluge::project::SectionLaunchTarget{static_cast<uint8_t>(s)}, static_cast<uint32_t>(s)});
		}
	}
	for (int32_t c = std::ssize(song.live.clips.session_clips) - 1; c >= 0; c--) {
		Clip* clip = song.live.clips.session_clips[c];
		const MappingTarget target = deluge::project::ClipMuteTarget{clip->id()};
		if (legacyField(clip->muteMIDICommand, song.project.mappings.find(target))
		        .equalsNoteOrCC(&cable, channel, note)) {
			hits.push_back({target, static_cast<uint32_t>(c)});
		}
	}
	return hits;
}

/// The oracle: today's scan of one owner's learned knobs (ModControllableAudio::offerReceivedCCToLearnedParams*); a
/// hibernating owner is never offered MIDI.
std::vector<Hit> legacyKnobs(Song& song, ControlsAddress owner, MIDICable& cable, int32_t channel, int32_t cc,
                             std::string& broken) {
	std::vector<Hit> hits;
	bool isSound = false;
	ModControllableAudio* knobs = deluge::bridge::controlsOwnerAt(song, owner, &isSound);
	if (knobs == nullptr) {
		return hits;
	}
	if (owner.kind != ControlsAddress::Kind::SONG) {
		bool awake = false;
		for (Output* o = song.live.media.first_output; o != nullptr && !awake; o = o->next) {
			if (owner.kind == ControlsAddress::Kind::OUTPUT) {
				awake = o->id() == owner.id;
			}
			else if (o->type == OutputType::KIT) {
				for (Drum* d = static_cast<Kit*>(o)->firstDrum; d != nullptr && !awake; d = d->next) {
					awake = d->id() == owner.id;
				}
			}
		}
		if (!awake) {
			return hits;
		}
	}
	const std::vector<Mapping> entries = song.project.mappings.params_of(owner);
	if (entries.size() != knobs->midi_knobs.size()) {
		broken = "an owner's knob list is not its table entries";
		return hits;
	}
	for (size_t i = 0; i < entries.size(); i++) {
		if (legacyField(knobs->midi_knobs[i].midiInput, &entries[i]).equalsNoteOrCC(&cable, channel, cc)) {
			hits.push_back({entries[i].target, static_cast<uint32_t>(i)});
		}
	}
	return hits;
}

/// The oracle: today's scan of a kit's rows after its own input (Kit::offerReceivedNote): a row's input, or failing
/// that its mute. A hibernating kit is never offered MIDI.
std::vector<Hit> legacyLanes(Song& song, uint32_t kitId, MIDICable& cable, int32_t channel, int32_t note) {
	std::vector<Hit> hits;
	Kit* kit = nullptr;
	for (Output* o = song.live.media.first_output; o != nullptr; o = o->next) {
		if (o->type == OutputType::KIT && o->id() == kitId) {
			kit = static_cast<Kit*>(o);
		}
	}
	if (kit == nullptr) {
		return hits;
	}
	uint32_t ordinal = 0;
	for (Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next, ordinal++) {
		const MappingTarget input = deluge::project::LaneInputTarget{*drum->id()};
		const MappingTarget mute = deluge::project::DrumMuteTarget{*drum->id()};
		if (legacyField(drum->midiInput, song.project.mappings.find(input))
		        .equalsNoteOrCCAllowMPE(&cable, channel, note)) {
			hits.push_back({input, ordinal});
		}
		else if (legacyField(drum->muteMIDICommand, song.project.mappings.find(mute))
		             .equalsNoteOrCC(&cable, channel, note)) {
			hits.push_back({mute, ordinal});
		}
	}
	return hits;
}

Hit hitOf(const deluge::midi::IndexedEntry& entry) {
	return {entry.target, entry.ordinal};
}

/// A random source: a channel, MPE zone or their CC or program-change encodings, a small note or CC (pitch bend among
/// them), and any device, DIN, upstream USB or an unknown hosted device.
MidiCommand randomSource(std::mt19937& rng) {
	constexpr std::array<uint8_t, 7> kChannels{
	    0, 1, 2, 14, 15, MIDI_CHANNEL_MPE_LOWER_ZONE, MIDI_CHANNEL_MPE_UPPER_ZONE};
	MidiCommand source;
	source.channel_or_zone = kChannels[rng() % kChannels.size()];
	switch (rng() % 4) {
	case 0:
		source.channel_or_zone += IS_A_CC;
		break;
	case 1:
		if (source.channel_or_zone < 16) {
			source.channel_or_zone += IS_A_PC;
		}
		break;
	default:
		break;
	}
	source.note_or_cc = rng() % 8 == 0 ? 128 : rng() % 4;
	switch (rng() % 4) {
	case 0:
		break;
	case 1:
		source.device = MidiDeviceRef{.port = MidiDeviceRef::Port::DIN};
		break;
	case 2:
		source.device = MidiDeviceRef{.port = MidiDeviceRef::Port::UPSTREAM_USB_1};
		break;
	default:
		source.device = MidiDeviceRef{.port = MidiDeviceRef::Port::HOSTED,
		                              .name = "MAPPING DISPATCH ABSENT",
		                              .vendor_id = 0x1209,
		                              .product_id = 1};
		break;
	}
	return source;
}

MappingTable randomTable(std::mt19937& rng, const Fixture& f) {
	MappingTable table;
	const int32_t n = static_cast<int32_t>(rng() % 40);
	for (int32_t i = 0; i < n; i++) {
		Mapping mapping{.source = randomSource(rng)};
		switch (rng() % 6) {
		case 0: {
			const size_t o = rng() % f.owners.size();
			const deluge::engine::ParamId param =
			    f.ownerIsSound[o] ? (rng() % 2 ? knobParam(params::LOCAL_PAN) : knobParam(params::LOCAL_LPF_FREQ))
			                      : knobParam(rng() % 2 ? params::UNPATCHED_DELAY_AMOUNT : params::UNPATCHED_LPF_FREQ,
			                                  params::Kind::UNPATCHED_GLOBAL);
			mapping.target = deluge::project::ParamTarget{f.owners[o], param};
			break;
		}
		case 1:
			mapping.target = deluge::project::ClipMuteTarget{f.clipIds[rng() % f.clipIds.size()]};
			break;
		case 2:
			mapping.target = deluge::project::DrumMuteTarget{f.drumIds[rng() % f.drumIds.size()]};
			break;
		case 3:
			mapping.target = deluge::project::LaneInputTarget{f.drumIds[rng() % f.drumIds.size()]};
			break;
		case 4:
			mapping.target = deluge::project::SectionLaunchTarget{static_cast<uint8_t>(rng() % kMaxNumSections)};
			break;
		default:
			mapping.target = deluge::project::TrackInputTarget{f.outputIds[rng() % f.outputIds.size()]};
			break;
		}
		table.learn(mapping);
	}
	return table;
}

/// Make @p f's song's table @p table and its engine fields the projection of it.
void install(const Fixture& f, MappingTable table) {
	Song& song = *f.song;
	song.project.mappings = std::move(table);
	for (uint32_t id : f.clipIds) {
		(void)deluge::bridge::projectMapping(song, deluge::project::ClipMuteTarget{id});
	}
	for (uint32_t id : f.drumIds) {
		(void)deluge::bridge::projectMapping(song, deluge::project::DrumMuteTarget{id});
		(void)deluge::bridge::projectMapping(song, deluge::project::LaneInputTarget{id});
	}
	for (uint32_t id : f.outputIds) {
		(void)deluge::bridge::projectMapping(song, deluge::project::TrackInputTarget{id});
	}
	for (const ControlsAddress& owner : f.owners) {
		(void)deluge::bridge::projectMapping(song, deluge::project::ParamTarget{owner, {}});
	}
	deluge::midi::rebuildMappingIndex(song);
}

/// Random MPE zones on @p port: each zone off half the time.
void randomZones(std::mt19937& rng, MIDIPort& port) {
	port.mpeLowerZoneLastMemberChannel = rng() % 2 ? 0 : 1 + rng() % 14;
	port.mpeUpperZoneLastMemberChannel = rng() % 2 ? 15 : 1 + rng() % 14;
}

int mapping_dispatch_main(int /*argc*/, char** /*argv*/, deluge::scenarios::driver::Io io) {
	Fixture f;
	f.song = loadSong(kSongText, io.err);
	if (f.song == nullptr) {
		std::fprintf(io.err, "MAPPING-DISPATCH FAIL the song did not load\n");
		return 1;
	}
	Song& song = *f.song;

	// A kit of a MIDI row and a gate row sleeps, as an edited instrument a preset change replaced does: a hibernating
	// output keeps its bindings but is never offered MIDI.
	ParamManagerForTimeline sleeperParams;
	Instrument* sleeper = StorageManager::createNewInstrument(OutputType::KIT, &sleeperParams);
	if (sleeper == nullptr) {
		std::fprintf(io.err, "MAPPING-DISPATCH FAIL no kit to put to sleep\n");
		return 1;
	}
	song.live.param_backup.backUp(static_cast<ModControllableAudio*>(sleeper->toModControllable()), nullptr,
	                              &sleeperParams, true);
	song.addOutput(sleeper);
	static_cast<Kit*>(sleeper)->addDrum(StorageManager::createNewDrum(DrumType::MIDI), &song);
	static_cast<Kit*>(sleeper)->addDrum(StorageManager::createNewDrum(DrumType::GATE), &song);
	sleeper->editedByUser = true;
	song.deleteOrHibernateOutput(sleeper);
	if (song.live.media.first_hibernating_instrument != sleeper) {
		std::fprintf(io.err, "MAPPING-DISPATCH FAIL the kit did not hibernate\n");
		return 1;
	}

	if (song.live.clips.arrangement_only_clips.empty()) {
		std::fprintf(io.err, "MAPPING-DISPATCH FAIL the song has no arrangement-only clip\n");
		return 1;
	}
	for (Clip* clip : AllClips::everywhere(&song)) {
		f.clipIds.push_back(clip->id());
	}
	f.owners.push_back({ControlsAddress::Kind::SONG, 0});
	f.ownerIsSound.push_back(false);
	for (Output* first :
	     {song.live.media.first_output, static_cast<Output*>(song.live.media.first_hibernating_instrument)}) {
		for (Output* o = first; o != nullptr; o = o->next) {
			f.outputIds.push_back(*o->id());
			if (o->type == OutputType::SYNTH || o->type == OutputType::KIT || o->type == OutputType::AUDIO) {
				f.owners.push_back({ControlsAddress::Kind::OUTPUT, *o->id()});
				f.ownerIsSound.push_back(o->type == OutputType::SYNTH);
			}
			if (o->type != OutputType::KIT) {
				continue;
			}
			f.kitIds.push_back(*o->id());
			for (Drum* d = static_cast<Kit*>(o)->firstDrum; d != nullptr; d = d->next) {
				f.drumIds.push_back(*d->id());
				if (d->type == DrumType::SOUND) {
					f.owners.push_back({ControlsAddress::Kind::DRUM, *d->id()});
					f.ownerIsSound.push_back(true);
				}
			}
		}
	}
	std::fprintf(
	    io.out,
	    "MAPPING-DISPATCH song session-clips=%zu arrangement-clips=%zu outputs=%zu kits=%zu drums=%zu owners=%zu\n",
	    song.live.clips.session_clips.size(), song.live.clips.arrangement_only_clips.size(), f.outputIds.size(),
	    f.kitIds.size(), f.drumIds.size(), f.owners.size());

	std::array<MIDICable*, 2> cables{&MIDIDeviceManager::dinMIDIPorts, &MIDIDeviceManager::upstreamUSBMIDICable1};
	const bool differentiating = MIDIDeviceManager::differentiatingInputsByDevice;
	std::array<MIDIPort, 2> ports{cables[0]->ports[MIDI_DIRECTION_INPUT_TO_DELUGE],
	                              cables[1]->ports[MIDI_DIRECTION_INPUT_TO_DELUGE]};

	std::mt19937 rng{kSeed};
	int32_t cases = 0;
	int32_t mismatches = 0;
	std::map<std::string, int32_t> seen;
	std::string broken;
	auto report = [&](const char* what, const std::string& message, const std::vector<Hit>& index,
	                  const std::vector<Hit>& legacy) {
		if (mismatches++ < 10) {
			std::fprintf(io.err, "MAPPING-DISPATCH MISMATCH %s %s index=%s legacy=%s\n", what, message.c_str(),
			             describe(index).c_str(), describe(legacy).c_str());
		}
	};
	// Count what the legacy scan hit, so a run that never reaches a kind of match fails.
	auto tally = [&](const std::vector<Hit>& hits, bool zoned) {
		for (const Hit& hit : hits) {
			constexpr std::array<const char*, 6> kKinds{"knob", "clip", "rowmute", "section", "track", "rowinput"};
			seen[kKinds[hit.target.index()]]++;
			const auto* knob = std::get_if<deluge::project::ParamTarget>(&hit.target);
			const Mapping entry = knob != nullptr ? song.project.mappings.params_of(knob->owner)[hit.ordinal]
			                                      : *song.project.mappings.find(hit.target);
			if (zoned && std::holds_alternative<deluge::project::LaneInputTarget>(hit.target)) {
				seen["zone-rowinput"]++;
			}
			if (!entry.source.device) {
				seen["any-device"]++;
			}
			else if (deluge::bridge::find_cable(*entry.source.device) == nullptr) {
				seen["absent-device"]++;
			}
			else if (MIDIDeviceManager::differentiatingInputsByDevice) {
				seen["device-told-apart"]++;
			}
		}
	};
	for (int32_t t = 0; t < kTables; t++) {
		install(f, randomTable(rng, f));
		const deluge::midi::MappingIndex& index = deluge::midi::mappingIndex();
		for (int32_t m = 0; m < kMessagesPerTable; m++) {
			MIDIDeviceManager::differentiatingInputsByDevice = rng() % 2 != 0;
			randomZones(rng, cables[0]->ports[MIDI_DIRECTION_INPUT_TO_DELUGE]);
			randomZones(rng, cables[1]->ports[MIDI_DIRECTION_INPUT_TO_DELUGE]);
			MIDICable& cable = *cables[rng() % 2];
			constexpr std::array<int32_t, 7> kChannels{0, 1, 2, 3, 13, 14, 15};
			const int32_t channel = kChannels[rng() % kChannels.size()];
			const int32_t number = rng() % 8 == 0 ? 128 : static_cast<int32_t>(rng() % 4);
			MIDIPort& port = cable.ports[MIDI_DIRECTION_INPUT_TO_DELUGE];
			// A note offers its channel, a CC its channel or zone as a CC, a program change its channel as one.
			const std::array<int32_t, 3> encoded{channel, port.channelToZone(channel) + IS_A_CC, channel + IS_A_PC};
			const int32_t commandChannel = encoded[rng() % encoded.size()];
			char message[96];
			std::snprintf(message, sizeof message, "table=%d cable=%s diff=%d zones=%u/%u ch=%d cmdch=%d n=%d", t,
			              &cable == cables[0] ? "din" : "usb1", MIDIDeviceManager::differentiatingInputsByDevice,
			              port.mpeLowerZoneLastMemberChannel, port.mpeUpperZoneLastMemberChannel, channel,
			              commandChannel, number);
			cases++;

			std::vector<Hit> fromIndex;
			index.forEachCommand(cable, commandChannel, number,
			                     [&](const deluge::midi::IndexedEntry& e) { fromIndex.push_back(hitOf(e)); });
			std::vector<Hit> legacy = legacyCommands(song, cable, commandChannel, number);
			if (fromIndex != legacy) {
				report("commands", message, fromIndex, legacy);
			}
			tally(legacy, false);

			for (const ControlsAddress& owner : f.owners) {
				fromIndex.clear();
				index.forEachKnob(owner, cable, channel, number,
				                  [&](const deluge::midi::IndexedEntry& e) { fromIndex.push_back(hitOf(e)); });
				legacy = legacyKnobs(song, owner, cable, channel, number, broken);
				if (fromIndex != legacy) {
					report("knobs", message, fromIndex, legacy);
				}
				tally(legacy, false);
			}

			for (uint32_t kit : f.kitIds) {
				fromIndex.clear();
				index.forEachLaneMatch(kit, cable, channel, number,
				                       [&](const deluge::midi::IndexedEntry& e) { fromIndex.push_back(hitOf(e)); });
				legacy = legacyLanes(song, kit, cable, channel, number);
				if (fromIndex != legacy) {
					report("lanes", message, fromIndex, legacy);
				}
				tally(legacy, port.channelToZone(channel) != channel);
			}
		}
	}

	MIDIDeviceManager::differentiatingInputsByDevice = differentiating;
	cables[0]->ports[MIDI_DIRECTION_INPUT_TO_DELUGE] = ports[0];
	cables[1]->ports[MIDI_DIRECTION_INPUT_TO_DELUGE] = ports[1];
	deluge::midi::mappingIndex().clear();

	std::string hits;
	for (const auto& [kind, n] : seen) {
		hits += " " + kind + "=" + std::to_string(n);
	}
	std::fprintf(io.out, "MAPPING-DISPATCH cases=%d mismatches=%d hits:%s\n", static_cast<int>(cases),
	             static_cast<int>(mismatches), hits.c_str());
	int32_t failures = mismatches;
	if (!broken.empty()) {
		std::fprintf(io.err, "MAPPING-DISPATCH FAIL %s\n", broken.c_str());
		failures++;
	}
	constexpr std::array<const char*, 9> kRequired{"knob",       "clip",          "rowmute",
	                                               "section",    "rowinput",      "zone-rowinput",
	                                               "any-device", "absent-device", "device-told-apart"};
	if (cases < 10000 || std::ranges::any_of(kRequired, [&](const char* kind) { return seen[kind] == 0; })) {
		std::fprintf(io.err, "MAPPING-DISPATCH FAIL the property did not reach every kind of match\n");
		failures++;
	}
	return failures == 0 ? 0 : 3;
}

} // namespace

DELUGE_DRIVER_SCENARIO(mapping_dispatch, mapping_dispatch_main)
