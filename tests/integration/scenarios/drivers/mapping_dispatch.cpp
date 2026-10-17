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
/// upstream USB or from a hosted device the Deluge did not know. Each table is projected onto the engine fields and
/// the index rebuilt. Each random message (from DIN or upstream USB, with inputs told apart by device or not, and
/// random MPE zones on both cables) is then matched by the index and by a transcription of the scans the dispatch makes
/// over the engine fields, and the two sequences of hits (target and position, in order) must be equal.
///
/// Projecting a binding to a hosted device the Deluge did not know makes the device an entry, as a song's load does,
/// which no message arrives on. A section's command is not projected, so a section learned from a device the Deluge
/// still does not know is kept as such: its hosted sources name a device nothing else names.
///
/// Then the legs. First the dispatch's: a message sent in through the app's MIDI input does what its bindings say. A
/// note toggles a learned clip, launches a section, mutes and sounds a kit row; a CC moves a learned knob's param; a
/// note learned to a section and a clip launches the section and then toggles the clip; and the clips one note
/// toggles are toggled once each, from the session's end, a clip a toggle before deletes never. Then the index
/// follows the song and the devices. A binding to a device plugged in after it was learned starts matching, and stops
/// when it is unplugged; an any-device binding matches every cable; a hibernated kit's rows and knobs stop matching and
/// match again once it wakes; a session clip reordered and a kit row removed are offered at their new places. Each
/// change reaches the index through the step the app loop takes before it matches a message. The table writes the
/// render holds land, in order, through every rebuild; an app-loop clone writes the table at once; a write held for a
/// song a swap replaced never lands on the incoming one.
///
/// No arguments. Prints `MAPPING-DISPATCH cases=<n> mismatches=<m> ...` and a line per leg, and exits 0 when no
/// message's hits differ, every kind of hit was seen and every leg held.

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
#include "io/midi/midi_engine.h"
#include "memory/general_memory_allocator.h"
#include "memory/heaps.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/edits/edit.h"
#include "model/edits/learn_mapping.h"
#include "model/global_effectable/global_effectable.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/project_bridge/midi_command.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "modulation/knob.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
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

// The USB host stack's reports of a hosted device being set up, configured and detached. The Rust BSP has no USB host
// stack calling them yet, so the legs stand in for it; in the shipping app only the device list's own mark (an entry
// made, renamed or given new ids) runs.
extern "C" {
void giveDetailsOfDeviceBeingSetUp(int32_t ip, char const* name, uint16_t vendorId, uint16_t productId);
void hostedDeviceConfigured(int32_t ip, int32_t midiDeviceNum);
void hostedDeviceDetached(int32_t ip, int32_t midiDeviceNum);
}

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
		if (clip->muteMIDICommand.equalsNoteOrCC(&cable, channel, note)) {
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
		if (knobs->midi_knobs[i].midiInput.equalsNoteOrCC(&cable, channel, cc)) {
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
		if (drum->midiInput.equalsNoteOrCCAllowMPE(&cable, channel, note)) {
			hits.push_back({input, ordinal});
		}
		else if (drum->muteMIDICommand.equalsNoteOrCC(&cable, channel, note)) {
			hits.push_back({mute, ordinal});
		}
	}
	return hits;
}

Hit hitOf(const deluge::midi::IndexedEntry& entry) {
	return {entry.target, entry.ordinal};
}

/// The hosted device a section's command names: no binding that is projected names it, so the Deluge never knows it.
MidiDeviceRef unknownToSections() {
	return MidiDeviceRef{
	    .port = MidiDeviceRef::Port::HOSTED, .name = "MAPPING DISPATCH NEVER", .vendor_id = 0x1209, .product_id = 2};
}

/// A random source: a channel, MPE zone or their CC or program-change encodings, a small note or CC (pitch bend among
/// them), and any device, DIN, upstream USB or a hosted device the Deluge did not know.
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
			if (mapping.source.device && mapping.source.device->port == MidiDeviceRef::Port::HOSTED) {
				mapping.source.device = unknownToSections();
			}
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

// ---- The legs: the index follows the song and the devices ----

/// A hosted device the legs plug in and out. The Deluge knows nothing of it when its bindings are learned.
struct LateDevice {
	const char* name;
	uint16_t product;
	int32_t slot; ///< its place among the devices on the USB host port

	[[nodiscard]] MidiDeviceRef ref() const {
		return MidiDeviceRef{
		    .port = MidiDeviceRef::Port::HOSTED, .name = name, .vendor_id = 0x1209, .product_id = product};
	}
	void plugIn() const {
		giveDetailsOfDeviceBeingSetUp(0, name, 0x1209, product);
		hostedDeviceConfigured(0, slot);
	}
	void unplug() const { hostedDeviceDetached(0, slot); }
};

/// Played by a kit row's and a track's inputs: projecting them makes it an entry before it is plugged in.
constexpr LateDevice kLate{"MAPPING DISPATCH LATE", 0x0de1, 1};
/// Played by a section's command alone, which is not projected: the Deluge first knows it when it is plugged in.
constexpr LateDevice kLateToSection{"MAPPING DISPATCH LATE SECTION", 0x0de2, 2};

/// Learn @p mapping as MIDI learn does: whether the Edit took it.
bool learn(Song& song, const Mapping& mapping) {
	deluge::edits::Context ctx{song, song.project};
	deluge::edits::LearnMapping edit{mapping};
	return deluge::edits::apply(ctx, edit) == Error::NONE;
}

/// The index's command hits for a note.
std::vector<Hit> commandHits(MIDICable& cable, int32_t channel, int32_t note) {
	std::vector<Hit> hits;
	deluge::midi::mappingIndex().forEachCommand(cable, channel, note,
	                                            [&](const deluge::midi::IndexedEntry& e) { hits.push_back(hitOf(e)); });
	return hits;
}

/// The index's hits among kit @p kit's rows for a note.
std::vector<Hit> laneHits(uint32_t kit, MIDICable& cable, int32_t channel, int32_t note) {
	std::vector<Hit> hits;
	deluge::midi::mappingIndex().forEachLaneMatch(
	    kit, cable, channel, note, [&](const deluge::midi::IndexedEntry& e) { hits.push_back(hitOf(e)); });
	return hits;
}

/// The index's hits among @p owner's learned knobs for a CC.
std::vector<Hit> knobHits(const ControlsAddress& owner, MIDICable& cable, int32_t channel, int32_t cc) {
	std::vector<Hit> hits;
	deluge::midi::mappingIndex().forEachKnob(owner, cable, channel, cc,
	                                         [&](const deluge::midi::IndexedEntry& e) { hits.push_back(hitOf(e)); });
	return hits;
}

/// The hit on @p target among @p hits, if any.
std::optional<Hit> hitOn(const std::vector<Hit>& hits, const MappingTarget& target) {
	const auto it = std::ranges::find(hits, target, &Hit::target);
	return it != hits.end() ? std::optional{*it} : std::nullopt;
}

/// The app loop's step before it matches an incoming message, run as the loop runs it until nothing waits; @p what is
/// set when a change it should act on is still waiting after the second. A projection that makes a device entry marks
/// the devices once more, and the next refresh finds that entry and marks nothing.
void refresh(std::string& what) {
	deluge::midi::refreshMappingIndex();
	if (deluge::mappingIndexStale()) {
		deluge::midi::refreshMappingIndex();
	}
	if (what.empty() && deluge::mappingIndexStale()) {
		what = "two refreshes left the index marked stale";
	}
}

/// What the legs drive: the fixture's playing kit and synth, and the cables messages arrive on.
struct Stage {
	Song& song;
	Kit& kit;
	Instrument& synth;
	std::array<MIDICable*, 2> cables;
};

/// A binding to a device plugged in after it was learned starts matching once the device is plugged in, and unplugging
/// it stops it: a binding never listens to a cable that can still send unless it names it, and the device plugged back
/// in plays it again. A section's command names one device; a kit row's and a track's input another. The plugging in
/// and out goes through the USB host stack's hooks, which stand in for a stack the Rust BSP does not have yet.
std::string lateDeviceLeg(Stage& s) {
	if (deluge::bridge::find_cable(kLate.ref()) != nullptr
	    || deluge::bridge::find_cable(kLateToSection.ref()) != nullptr) {
		return "a late device is known before it is plugged in";
	}
	const Mapping section{.source = {.channel_or_zone = 4, .note_or_cc = 60, .device = kLateToSection.ref()},
	                      .target = deluge::project::SectionLaunchTarget{3}};
	const Mapping rowInput{.source = {.channel_or_zone = 5, .note_or_cc = 61, .device = kLate.ref()},
	                       .target = deluge::project::LaneInputTarget{*s.kit.firstDrum->id()}};
	const Mapping trackInput{.source = {.channel_or_zone = 6, .note_or_cc = 62, .device = kLate.ref()},
	                         .target = deluge::project::TrackInputTarget{*s.synth.id()}};
	for (const Mapping& mapping : {section, rowInput, trackInput}) {
		if (!learn(s.song, mapping)) {
			return "a learn was refused";
		}
	}
	std::string what;
	refresh(what);
	// Nothing that can send plays a binding to a device not plugged in.
	auto silentElsewhere = [&](const char* when) {
		for (MIDICable* cable : s.cables) {
			const char* name = cable == s.cables[0] ? "DIN" : "upstream USB";
			if (hitOn(commandHits(*cable, 4, 60), section.target)) {
				return std::string{when} + ": " + name + " launches a section learned from a late device";
			}
			if (hitOn(laneHits(*s.kit.id(), *cable, 5, 61), rowInput.target)) {
				return std::string{when} + ": " + name + " sounds a kit row learned from a late device";
			}
			if (s.synth.midiInput.checkMatch(cable, 6) != MIDIMatchType::NO_MATCH) {
				return std::string{when} + ": " + name + " plays a track whose input is a late device";
			}
		}
		return std::string{};
	};
	if (what.empty()) {
		what = silentElsewhere("before they are plugged in");
	}

	kLate.plugIn();
	kLateToSection.plugIn();
	refresh(what);
	MIDICable* plugged = deluge::bridge::find_cable(kLate.ref());
	MIDICable* pluggedToSection = deluge::bridge::find_cable(kLateToSection.ref());
	if (what.empty()
	    && (plugged == nullptr || plugged->connectionFlags == 0 || pluggedToSection == nullptr
	        || pluggedToSection->connectionFlags == 0)) {
		what = "a plugged-in device has no connected cable";
	}
	auto playsFromLate = [&](const char* when) {
		if (!hitOn(commandHits(*pluggedToSection, 4, 60), section.target)) {
			return std::string{when} + ": the late device does not launch its section";
		}
		if (!hitOn(laneHits(*s.kit.id(), *plugged, 5, 61), rowInput.target)) {
			return std::string{when} + ": the late device does not sound its kit row";
		}
		if (s.synth.midiInput.checkMatch(plugged, 6) != MIDIMatchType::CHANNEL) {
			return std::string{when} + ": the late device does not play its track";
		}
		return std::string{};
	};
	if (what.empty()) {
		what = playsFromLate("plugged in");
	}
	if (what.empty()) {
		what = silentElsewhere("plugged in");
	}

	kLate.unplug();
	kLateToSection.unplug();
	refresh(what);
	if (what.empty() && (plugged->connectionFlags != 0 || pluggedToSection->connectionFlags != 0)) {
		what = "an unplugged device's cable is still connected";
	}
	if (what.empty()) {
		what = silentElsewhere("unplugged");
	}
	if (what.empty()
	    && (deluge::bridge::find_cable(kLate.ref()) != plugged
	        || deluge::bridge::find_cable(kLateToSection.ref()) != pluggedToSection)) {
		what = "an unplugged device's bindings no longer resolve to its cable";
	}
	kLate.plugIn();
	kLateToSection.plugIn();
	refresh(what);
	if (what.empty()) {
		what = playsFromLate("plugged back in");
	}
	kLate.unplug();
	kLateToSection.unplug();
	refresh(what);
	return what;
}

/// A binding learned from any device matches every cable: DIN, upstream USB and a hosted device.
std::string anyDeviceLeg(Stage& s) {
	const ControlsAddress kitKnobs{ControlsAddress::Kind::OUTPUT, *s.kit.id()};
	const Mapping section{.source = {.channel_or_zone = 7, .note_or_cc = 63},
	                      .target = deluge::project::SectionLaunchTarget{5}};
	const Mapping clip{.source = {.channel_or_zone = 8, .note_or_cc = 64},
	                   .target = deluge::project::ClipMuteTarget{s.song.live.clips.session_clips[0]->id()}};
	const Mapping knob{.source = {.channel_or_zone = 9, .note_or_cc = 10},
	                   .target = deluge::project::ParamTarget{
	                       kitKnobs, knobParam(params::UNPATCHED_LPF_FREQ, params::Kind::UNPATCHED_GLOBAL)}};
	for (const Mapping& mapping : {section, clip, knob}) {
		if (!learn(s.song, mapping)) {
			return "a learn was refused";
		}
	}
	std::string what;
	kLate.plugIn();
	refresh(what);
	MIDICable* plugged = deluge::bridge::find_cable(kLate.ref());
	if (what.empty() && plugged == nullptr) {
		what = "the plugged-in device has no cable";
	}
	for (MIDICable* cable : {s.cables[0], s.cables[1], plugged}) {
		if (!what.empty()) {
			break;
		}
		const char* name = cable == s.cables[0] ? "DIN" : cable == s.cables[1] ? "upstream USB" : "the hosted device";
		if (!hitOn(commandHits(*cable, 7, 63), section.target)) {
			what = std::string{name} + " does not launch an any-device section";
		}
		else if (!hitOn(commandHits(*cable, 8, 64), clip.target)) {
			what = std::string{name} + " does not toggle an any-device clip";
		}
		else if (!hitOn(knobHits(kitKnobs, *cable, 9, 10), knob.target)) {
			what = std::string{name} + " does not move an any-device knob";
		}
	}
	kLate.unplug();
	refresh(what);
	return what;
}

/// A hibernated kit's row input and knobs do not match; waking the kit makes them match again.
std::string hibernationLeg(Stage& s) {
	static ParamManagerForTimeline kitParams;
	Instrument* kit = StorageManager::createNewInstrument(OutputType::KIT, &kitParams);
	if (kit == nullptr) {
		return "no kit to put to sleep";
	}
	s.song.live.param_backup.backUp(static_cast<ModControllableAudio*>(kit->toModControllable()), nullptr, &kitParams,
	                                true);
	s.song.addOutput(kit);
	static_cast<Kit*>(kit)->addDrum(StorageManager::createNewDrum(DrumType::MIDI), &s.song);
	const uint32_t kitId = *kit->id();
	const ControlsAddress knobs{ControlsAddress::Kind::OUTPUT, kitId};
	const Mapping rowInput{
	    .source = {.channel_or_zone = 10, .note_or_cc = 65, .device = MidiDeviceRef{.port = MidiDeviceRef::Port::DIN}},
	    .target = deluge::project::LaneInputTarget{*static_cast<Kit*>(kit)->firstDrum->id()}};
	const Mapping knob{.source = {.channel_or_zone = 11, .note_or_cc = 20},
	                   .target = deluge::project::ParamTarget{
	                       knobs, knobParam(params::UNPATCHED_DELAY_AMOUNT, params::Kind::UNPATCHED_GLOBAL)}};
	if (!learn(s.song, rowInput) || !learn(s.song, knob)) {
		return "a learn was refused";
	}
	MIDICable& din = *s.cables[0];
	std::string what;
	auto matches = [&](bool expected, const char* when) {
		refresh(what);
		if (!what.empty()) {
			return;
		}
		if (hitOn(laneHits(kitId, din, 10, 65), rowInput.target).has_value() != expected) {
			what = std::string{when} + (expected ? ": its row input does not match" : ": its row input matches");
		}
		else if (hitOn(knobHits(knobs, din, 11, 20), knob.target).has_value() != expected) {
			what = std::string{when} + (expected ? ": its knob does not match" : ": its knob matches");
		}
	};
	matches(true, "playing");
	kit->editedByUser = true;
	s.song.deleteOrHibernateOutput(kit);
	if (what.empty() && s.song.live.media.first_hibernating_instrument != kit) {
		what = "the kit did not hibernate";
	}
	matches(false, "hibernated");
	s.song.removeInstrumentFromHibernationList(kit);
	s.song.addOutput(kit);
	matches(true, "woken");
	return what;
}

/// A clip's mute command is offered at the clip's place in the session after the session's clips are reordered.
std::string sessionOrderLeg(Stage& s) {
	ClipArray& clips = s.song.live.clips.session_clips;
	const int32_t last = static_cast<int32_t>(clips.size()) - 1;
	const Mapping mute{.source = {.channel_or_zone = 12, .note_or_cc = 66},
	                   .target = deluge::project::ClipMuteTarget{clips[0]->id()}};
	if (!learn(s.song, mute)) {
		return "a learn was refused";
	}
	std::string what;
	clips.swapClips(0, last);
	refresh(what);
	const std::optional<Hit> hit = hitOn(commandHits(*s.cables[0], 12, 66), mute.target);
	if (what.empty() && (!hit || hit->ordinal != static_cast<uint32_t>(last))) {
		what = "the moved clip's mute is offered at " + (hit ? std::to_string(hit->ordinal) : std::string{"no place"})
		       + ", not its session index " + std::to_string(last);
	}
	clips.swapClips(0, last);
	refresh(what);
	return what;
}

/// A kit row's input is offered at its new place after a row before it is removed.
std::string rowRemovedLeg(Stage& s) {
	static ParamManagerForTimeline kitParams;
	Instrument* instrument = StorageManager::createNewInstrument(OutputType::KIT, &kitParams);
	if (instrument == nullptr) {
		return "no kit";
	}
	Kit& kit = *static_cast<Kit*>(instrument);
	s.song.live.param_backup.backUp(static_cast<ModControllableAudio*>(kit.toModControllable()), nullptr, &kitParams,
	                                true);
	s.song.addOutput(&kit);
	Drum* removed = StorageManager::createNewDrum(DrumType::MIDI);
	Drum* kept = StorageManager::createNewDrum(DrumType::GATE);
	kit.addDrum(removed, &s.song);
	kit.addDrum(kept, &s.song);
	const Mapping input{
	    .source = {.channel_or_zone = 13, .note_or_cc = 67, .device = MidiDeviceRef{.port = MidiDeviceRef::Port::DIN}},
	    .target = deluge::project::LaneInputTarget{*kept->id()}};
	if (!learn(s.song, input)) {
		return "a learn was refused";
	}
	std::string what;
	kit.removeDrum(removed);
	refresh(what);
	const std::optional<Hit> hit = hitOn(laneHits(*kit.id(), *s.cables[0], 13, 67), input.target);
	if (what.empty() && (!hit || hit->ordinal != 0)) {
		what = "the remaining row's input is offered at "
		       + (hit ? std::to_string(hit->ordinal) : std::string{"no place"}) + ", not 0";
	}
	return what;
}

/// A new audio track made and given @p from's engine knobs, as an auto-overdub's clone of an audio track is.
Output* engineClone(Song& song, Output& from) {
	std::vector<Output*> before;
	for (Output* o = song.live.media.first_output; o != nullptr; o = o->next) {
		before.push_back(o);
	}
	(void)song.createNewAudioOutput();
	Output* to = nullptr;
	for (Output* o = song.live.media.first_output; o != nullptr; o = o->next) {
		if (std::ranges::find(before, o) == before.end()) {
			to = o;
		}
	}
	bool isSound = false;
	ModControllableAudio* fromKnobs =
	    deluge::bridge::controlsOwnerAt(song, {ControlsAddress::Kind::OUTPUT, *from.id()}, &isSound);
	ModControllableAudio* toKnobs =
	    to != nullptr && to->id()
	        ? deluge::bridge::controlsOwnerAt(song, {ControlsAddress::Kind::OUTPUT, *to->id()}, &isSound)
	        : nullptr;
	if (fromKnobs == nullptr || toKnobs == nullptr) {
		return nullptr;
	}
	toKnobs->midi_knobs = fromKnobs->midi_knobs;
	return to;
}

/// An auto-overdub's clone of an audio track, made on the song's tick, which may be the audio tier: its engine knobs
/// play at once, the table and the device list are untouched until the app loop's refresh, and the refresh leaves the
/// table as a clone on the app loop would. The tick's own entry point (AudioClip::cloneOutput) is reached only by
/// linear recording starting on a pending overdub, so the leg makes the same calls it makes: a new audio track with its
/// source's engine knobs, then cloneOutputMappings, inside the render block. A full queue falls back to recording the
/// clone's knobs from its engine.
std::string cloneLeg(Stage& s) {
	Output* from = nullptr;
	for (Output* o = s.song.live.media.first_output; o != nullptr && from == nullptr; o = o->next) {
		if (o->type == OutputType::AUDIO) {
			from = o;
		}
	}
	if (from == nullptr) {
		return "no audio track to clone";
	}
	const ControlsAddress fromKnobs{ControlsAddress::Kind::OUTPUT, *from->id()};
	const MidiDeviceRef unknown{.port = MidiDeviceRef::Port::HOSTED,
	                            .name = "MAPPING DISPATCH CLONE",
	                            .vendor_id = 0x1209,
	                            .product_id = 0x0de3};
	if (!learn(s.song,
	           Mapping{.source = {.channel_or_zone = 14,
	                              .note_or_cc = 31,
	                              .device = MidiDeviceRef{.port = MidiDeviceRef::Port::DIN}},
	                   .target = deluge::project::ParamTarget{
	                       fromKnobs, knobParam(params::UNPATCHED_LPF_FREQ, params::Kind::UNPATCHED_GLOBAL)}})) {
		return "a learn was refused";
	}
	// A knob naming a device the Deluge does not know, written to the table alone as a load reads it before anything
	// projects it.
	s.song.project.mappings.learn(
	    Mapping{.source = {.channel_or_zone = 14, .note_or_cc = 30, .device = unknown},
	            .target = deluge::project::ParamTarget{
	                fromKnobs, knobParam(params::UNPATCHED_DELAY_AMOUNT, params::Kind::UNPATCHED_GLOBAL)}});
	std::string what;
	refresh(what);

	Output* to = engineClone(s.song, *from);
	if (to == nullptr) {
		return "no clone";
	}
	const ControlsAddress toKnobs{ControlsAddress::Kind::OUTPUT, *to->id()};
	MappingTable expected = s.song.project.mappings;
	expected.copy_params(*from->id(), *to->id());
	const std::vector<Mapping> before{s.song.project.mappings.entries().begin(),
	                                  s.song.project.mappings.entries().end()};
	deluge::memory::noteRenderBlock(true); // the song's tick, on the render
	deluge::bridge::cloneOutputMappings(*from, *to, s.song);
	deluge::memory::noteRenderBlock(false);
	bool isSound = false;
	ModControllableAudio* toEngine = deluge::bridge::controlsOwnerAt(s.song, toKnobs, &isSound);
	if (what.empty()
	    && !std::ranges::equal(before, std::vector<Mapping>{s.song.project.mappings.entries().begin(),
	                                                        s.song.project.mappings.entries().end()})) {
		what = "the clone wrote the table before the refresh";
	}
	if (what.empty() && deluge::bridge::find_cable(unknown) != nullptr) {
		what = "the clone made a device entry before the refresh";
	}
	if (what.empty()
	    && !hitOn(knobHits(fromKnobs, *s.cables[0], 14, 31),
	              deluge::project::ParamTarget{
	                  fromKnobs, knobParam(params::UNPATCHED_LPF_FREQ, params::Kind::UNPATCHED_GLOBAL)})) {
		what = "the source's knob stopped matching";
	}
	if (what.empty()
	    && (toEngine->midi_knobs.size() != 1
	        || !toEngine->midi_knobs[0].midiInput.equalsNoteOrCC(s.cables[0], 14, 31))) {
		what = "the clone's engine knob does not play at once";
	}
	if (what.empty() && !deluge::mappingIndexStale()) {
		what = "the clone left nothing for the refresh";
	}
	refresh(what);
	const std::vector<Mapping> after{s.song.project.mappings.entries().begin(),
	                                 s.song.project.mappings.entries().end()};
	if (what.empty()
	    && !std::ranges::equal(after, std::vector<Mapping>{expected.entries().begin(), expected.entries().end()})) {
		what = "after the refresh the table is not what a clone on the app loop makes";
	}
	MIDICable* made = deluge::bridge::find_cable(unknown);
	if (what.empty() && (made == nullptr || made->connectionFlags != 0)) {
		what = "the refresh made no unconnected entry for the device";
	}
	if (what.empty()
	    && (toEngine->midi_knobs.size() != 2 || std::ranges::none_of(toEngine->midi_knobs, [&](const MIDIKnob& k) {
		        return k.midiInput.cable == made;
	        }))) {
		what = "the refresh did not project the clone's knobs";
	}
	if (what.empty()
	    && !hitOn(knobHits(toKnobs, *s.cables[0], 14, 31),
	              deluge::project::ParamTarget{
	                  toKnobs, knobParam(params::UNPATCHED_LPF_FREQ, params::Kind::UNPATCHED_GLOBAL)})) {
		what = "the index does not offer the clone's knob";
	}

	// A full queue: the clone's knobs are recorded from its engine.
	Output* overflowed = engineClone(s.song, *from);
	if (what.empty() && overflowed == nullptr) {
		what = "no second clone";
	}
	if (what.empty()) {
		deluge::memory::noteRenderBlock(true);
		for (size_t i = 0; i < deluge::bridge::kPendingMappingOps; i++) {
			deluge::bridge::cloneOutputMappings(*to, *to, s.song);
		}
		deluge::bridge::cloneOutputMappings(*from, *overflowed, s.song);
		deluge::memory::noteRenderBlock(false);
		refresh(what);
		ModControllableAudio* engine =
		    deluge::bridge::controlsOwnerAt(s.song, {ControlsAddress::Kind::OUTPUT, *overflowed->id()}, &isSound);
		const std::vector<Mapping> knobs =
		    s.song.project.mappings.params_of({ControlsAddress::Kind::OUTPUT, *overflowed->id()});
		if (what.empty()
		    && (knobs.empty() || knobs.size() != engine->midi_knobs.size()
		        || knobs
		               != deluge::bridge::knobsOf({ControlsAddress::Kind::OUTPUT, *overflowed->id()}, *engine,
		                                          false))) {
			what = "after a full queue the clone's knobs are not recorded from its engine";
		}
	}
	return what;
}

/// Writes the render makes to the table are held for the app loop and land in order. A pending overdub deleted on the
/// song's tick (Song::deletePendingOverdubs, as Session's launch handling calls it) leaves its clip's mute command in
/// the table until the refresh drops it; a clone held and then a forget of the clone end with the clone's knobs gone.
std::string renderWritesLeg(Stage& s) {
	ClipArray& clips = s.song.live.clips.session_clips;
	Clip* clip = clips[1];
	const deluge::project::ClipMuteTarget mute{clip->id()};
	if (!learn(s.song, Mapping{.source = {.channel_or_zone = 15, .note_or_cc = 70}, .target = mute})) {
		return "a learn was refused";
	}
	std::string what;
	refresh(what);
	clip->isPendingOverdub = true;
	deluge::memory::noteRenderBlock(true); // the song's tick, on the render
	(void)s.song.deletePendingOverdubs();
	deluge::memory::noteRenderBlock(false);
	if (what.empty() && std::ranges::find(clips, clip) != clips.end()) {
		what = "the pending overdub was not deleted";
	}
	if (what.empty() && s.song.project.mappings.find(mute) == nullptr) {
		what = "the render wrote the table: the deleted clip's mute command is gone before the refresh";
	}
	if (what.empty() && !deluge::mappingIndexStale()) {
		what = "the delete left nothing for the refresh";
	}
	refresh(what);
	if (what.empty() && s.song.project.mappings.find(mute) != nullptr) {
		what = "the refresh did not drop the deleted clip's mute command";
	}

	Output* from = nullptr;
	for (Output* o = s.song.live.media.first_output; o != nullptr && from == nullptr; o = o->next) {
		if (o->type == OutputType::AUDIO) {
			from = o;
		}
	}
	if (what.empty()
	    && (from == nullptr
	        || !learn(s.song, Mapping{.source = {.channel_or_zone = 15, .note_or_cc = 71},
	                                  .target = deluge::project::ParamTarget{
	                                      {ControlsAddress::Kind::OUTPUT, *from->id()},
	                                      knobParam(params::UNPATCHED_LPF_FREQ, params::Kind::UNPATCHED_GLOBAL)}}))) {
		what = "no audio track learned a knob";
	}
	Output* to = from != nullptr ? engineClone(s.song, *from) : nullptr;
	if (what.empty() && to == nullptr) {
		what = "no audio track with knobs to clone";
	}
	if (what.empty()) {
		deluge::memory::noteRenderBlock(true);
		deluge::bridge::cloneOutputMappings(*from, *to, s.song);
		deluge::bridge::forgetOutputMappings(s.song, *to->id());
		deluge::memory::noteRenderBlock(false);
		refresh(what);
		if (what.empty() && !s.song.project.mappings.params_of({ControlsAddress::Kind::OUTPUT, *to->id()}).empty()) {
			what = "a clone held before a forget of it left its knobs in the table";
		}
	}
	return what;
}

/// A rebuild of the index made directly (a preset change projects its new instrument and rebuilds) while the render
/// holds a write leaves nothing waiting: the held write lands and the index is current, so the next refresh has
/// nothing to do and offers nothing the write dropped.
std::string rebuildDrainsLeg(Stage& s) {
	Clip* clip = s.song.live.clips.session_clips[0];
	const deluge::project::ClipMuteTarget mute{clip->id()};
	if (!learn(s.song, Mapping{.source = {.channel_or_zone = 12, .note_or_cc = 50}, .target = mute})) {
		return "a learn was refused";
	}
	std::string what;
	refresh(what);
	deluge::memory::noteRenderBlock(true); // the song's tick, on the render
	deluge::bridge::forgetClipMappings(s.song, clip->id());
	deluge::memory::noteRenderBlock(false);
	deluge::bridge::projectOutputMappings(s.song, *s.synth.id());
	if (what.empty() && deluge::mappingIndexStale()) {
		what = "a direct rebuild left the index marked stale";
	}
	if (what.empty() && s.song.project.mappings.find(mute) != nullptr) {
		what = "a direct rebuild left a held write waiting: the forgotten clip's mute command is still in the table";
	}
	refresh(what);
	if (what.empty() && s.song.project.mappings.find(mute) != nullptr) {
		what = "a held write was stranded past the refresh: the forgotten clip's mute command is still in the table";
	}
	if (what.empty() && hitOn(commandHits(*s.cables[0], 12, 50), mute)) {
		what = "the index still offers the forgotten clip's mute command";
	}

	// The rebuild itself, with no writer before it to apply the held write.
	if (what.empty() && !learn(s.song, Mapping{.source = {.channel_or_zone = 12, .note_or_cc = 50}, .target = mute})) {
		what = "a second learn was refused";
	}
	refresh(what);
	deluge::memory::noteRenderBlock(true);
	deluge::bridge::forgetClipMappings(s.song, clip->id());
	deluge::memory::noteRenderBlock(false);
	deluge::midi::rebuildMappingIndex(s.song);
	refresh(what);
	if (what.empty() && s.song.project.mappings.find(mute) != nullptr) {
		what = "a rebuild cleared the stale mark over a held write: the forgotten clip's mute command is still in the "
		       "table";
	}
	return what;
}

/// An overdub's clone of an audio track made on the app loop (linear recording begun by the record or play button,
/// through Session::resetPlayPos) writes the table at once: the queue is the render's alone. The leg makes the calls
/// AudioClip::cloneOutput makes, on the app loop.
std::string appLoopCloneLeg(Stage& s) {
	Output* from = nullptr;
	for (Output* o = s.song.live.media.first_output; o != nullptr && from == nullptr; o = o->next) {
		if (o->type == OutputType::AUDIO) {
			from = o;
		}
	}
	if (from == nullptr) {
		return "no audio track to clone";
	}
	const ControlsAddress fromKnobs{ControlsAddress::Kind::OUTPUT, *from->id()};
	const deluge::project::ParamTarget knob{fromKnobs,
	                                        knobParam(params::UNPATCHED_LPF_FREQ, params::Kind::UNPATCHED_GLOBAL)};
	if (!learn(s.song, Mapping{.source = {.channel_or_zone = 13, .note_or_cc = 40}, .target = knob})) {
		return "a learn was refused";
	}
	std::string what;
	refresh(what);
	Output* to = engineClone(s.song, *from);
	if (to == nullptr) {
		return "no clone";
	}
	const ControlsAddress toKnobs{ControlsAddress::Kind::OUTPUT, *to->id()};
	MappingTable expected = s.song.project.mappings;
	expected.copy_params(*from->id(), *to->id());
	deluge::bridge::cloneOutputMappings(*from, *to, s.song);
	if (what.empty() && s.song.project.mappings.params_of(toKnobs) != expected.params_of(toKnobs)) {
		what = "a clone on the app loop was held for the refresh: the table does not hold the clone's knobs at once";
	}
	refresh(what);
	if (what.empty()
	    && !hitOn(knobHits(toKnobs, *s.cables[0], 13, 40), deluge::project::ParamTarget{toKnobs, knob.param})) {
		what = "the index does not offer the clone's knob";
	}
	return what;
}

/// A write the render held for the outgoing song never lands on the incoming one, even when the tick makes it after
/// the swap dropped the queue and before the incoming song is current.
std::string songSwapLeg(Stage& s) {
	Clip* clip = s.song.live.clips.session_clips[0];
	const deluge::project::ClipMuteTarget mute{clip->id()};
	const Mapping command{.source = {.channel_or_zone = 11, .note_or_cc = 52}, .target = mute};
	if (!learn(s.song, command)) {
		return "a learn was refused";
	}
	std::string what;
	refresh(what);
	Song* incoming = loadSong(kSongText, stderr);
	currentSong = &s.song;
	if (incoming == nullptr) {
		return "the incoming song did not load";
	}
	incoming->project.mappings.learn(command); // the same clip id, learned in the incoming song too
	// The swap's own steps, with the song's tick between dropping the queue and the incoming song becoming current.
	deluge::bridge::discardPendingMappingOps();
	deluge::memory::noteRenderBlock(true);
	deluge::bridge::forgetClipMappings(s.song, clip->id());
	deluge::memory::noteRenderBlock(false);
	currentSong = incoming;
	deluge::markMappingIndexStale();
	refresh(what);
	if (what.empty() && incoming->project.mappings.find(mute) == nullptr) {
		what = "a write the render held for the outgoing song landed on the incoming one";
	}
	currentSong = &s.song;
	deluge::markMappingIndexStale();
	refresh(what);
	return what;
}

// ---- The dispatch: what a learned binding does when its message arrives ----

/// Send a message in through the app's MIDI input, as one arriving on @p cable is.
void send(MIDICable& cable, uint8_t status, uint8_t channel, uint8_t data1, uint8_t data2) {
	midiEngine.midiMessageReceived(cable, status, channel, data1, data2);
}

/// The session's clips and whether each plays, put back as they were when it goes out of scope.
class SessionPlaying {
public:
	explicit SessionPlaying(Song& song) {
		for (Clip* clip : song.live.clips.session_clips) {
			clips_.emplace_back(clip, clip->activeIfNoSolo);
		}
	}
	SessionPlaying(const SessionPlaying&) = delete;
	SessionPlaying& operator=(const SessionPlaying&) = delete;
	~SessionPlaying() {
		for (auto [clip, active] : clips_) {
			clip->activeIfNoSolo = active;
			clip->armState = ArmState::OFF;
		}
	}

private:
	std::vector<std::pair<Clip*, bool>> clips_;
};

/// The session clips the dispatch legs drive: the synth's two, another synth's, the MIDI track's and the kit's. A leg
/// sets which play before it sends a message whose effect depends on it.
struct DispatchClips {
	Clip* z{};   ///< the synth's first, in section 0
	Clip* s{};   ///< the synth's second, in section 1
	Clip* x{};   ///< the other synth's, in section 0
	Clip* y{};   ///< the MIDI track's, in section 1
	Clip* kit{}; ///< the kit's, its active clip, in section 2
};

/// The fixture's dispatch clips, or nullopt when the session is not the fixture's (a leg that deletes a session clip
/// runs after the dispatch legs).

std::optional<DispatchClips> dispatchClips(Stage& s) {
	DispatchClips c;
	for (Clip* clip : s.song.live.clips.session_clips) {
		if (clip->output == &s.synth) {
			(c.z == nullptr ? c.z : c.s) = clip;
		}
		else if (clip->output->type == OutputType::SYNTH && c.x == nullptr) {
			c.x = clip;
		}
		else if (clip->output->type == OutputType::MIDI_OUT && c.y == nullptr) {
			c.y = clip;
		}
		else if (clip->output == &s.kit) {
			c.kit = clip;
		}
	}
	if (c.z == nullptr || c.s == nullptr || c.x == nullptr || c.y == nullptr || c.kit == nullptr
	    || s.kit.getActiveClip() != c.kit) {
		return std::nullopt;
	}
	return c;
}

constexpr uint8_t kNoteOn = 0x09;
constexpr uint8_t kNoteOff = 0x08;
constexpr uint8_t kCC = 0x0B;

/// A note learned to a session clip's mute toggles the clip; its note-off does not.
std::string clipToggleLeg(Stage& s) {
	const std::optional<DispatchClips> c = dispatchClips(s);
	if (!c) {
		return "the song's session is not the fixture's";
	}
	SessionPlaying restore{s.song};
	if (!learn(s.song, {.source = {.channel_or_zone = 0, .note_or_cc = 72},
	                    .target = deluge::project::ClipMuteTarget{c->x->id()}})) {
		return "a learn was refused";
	}
	c->x->activeIfNoSolo = false;
	send(*s.cables[0], kNoteOn, 0, 72, 100);
	if (!c->x->activeIfNoSolo) {
		return "the note did not start the clip";
	}
	send(*s.cables[0], kNoteOff, 0, 72, 0);
	if (!c->x->activeIfNoSolo) {
		return "the note-off stopped the clip";
	}
	send(*s.cables[0], kNoteOn, 0, 72, 100);
	if (c->x->activeIfNoSolo) {
		return "the second note did not stop the clip";
	}
	return {};
}

/// A note learned to a section launches it: its clips play and the others stop.
std::string sectionArmLeg(Stage& s) {
	const std::optional<DispatchClips> c = dispatchClips(s);
	if (!c) {
		return "the song's session is not the fixture's";
	}
	SessionPlaying restore{s.song};
	if (!learn(s.song, {.source = {.channel_or_zone = 0, .note_or_cc = 73},
	                    .target = deluge::project::SectionLaunchTarget{1}})) {
		return "a learn was refused";
	}
	send(*s.cables[0], kNoteOn, 0, 73, 100);
	if (!c->s->activeIfNoSolo || !c->y->activeIfNoSolo) {
		return "the section's clips do not play";
	}
	if (c->z->activeIfNoSolo || c->x->activeIfNoSolo || c->kit->activeIfNoSolo) {
		return "a clip of another section still plays";
	}
	return {};
}

/// The kit clip's row for the kit's first row.
NoteRow* firstRowOf(Stage& s, Clip& kitClip) {
	return static_cast<InstrumentClip&>(kitClip).getNoteRowForDrum(s.kit.firstDrum);
}

/// A note learned to a kit row's mute mutes the row in the kit's clip, and unmutes it again.
std::string rowMuteLeg(Stage& s) {
	const std::optional<DispatchClips> c = dispatchClips(s);
	if (!c) {
		return "the song's session is not the fixture's";
	}
	NoteRow* row = firstRowOf(s, *c->kit);
	if (row == nullptr || row->muted()) {
		return "the kit clip has no unmuted row for the kit's first row";
	}
	if (!learn(s.song, {.source = {.channel_or_zone = 0, .note_or_cc = 74},
	                    .target = deluge::project::DrumMuteTarget{*s.kit.firstDrum->id()}})) {
		return "a learn was refused";
	}
	send(*s.cables[0], kNoteOn, 0, 74, 100);
	if (!row->muted()) {
		return "the note did not mute the row";
	}
	send(*s.cables[0], kNoteOff, 0, 74, 0);
	if (!row->muted()) {
		return "the note-off unmuted the row";
	}
	send(*s.cables[0], kNoteOn, 0, 74, 100);
	if (row->muted()) {
		return "the second note did not unmute the row";
	}
	return {};
}

/// A note learned to a kit row's input sounds the row, and its note-off stops it.
std::string rowSoundLeg(Stage& s) {
	Drum* drum = s.kit.firstDrum;
	if (!learn(s.song, {.source = {.channel_or_zone = 0,
	                               .note_or_cc = 75,
	                               .device = MidiDeviceRef{.port = MidiDeviceRef::Port::DIN}},
	                    .target = deluge::project::LaneInputTarget{*drum->id()}})) {
		return "a learn was refused";
	}
	if (drum->auditioned) {
		return "the row sounds before its note";
	}
	send(*s.cables[1], kNoteOn, 0, 75, 100);
	if (drum->auditioned) {
		return "a note from another device sounded the row";
	}
	send(*s.cables[0], kNoteOn, 0, 75, 100);
	if (!drum->auditioned) {
		return "the note did not sound the row";
	}
	send(*s.cables[0], kNoteOff, 0, 75, 0);
	if (drum->auditioned) {
		return "the note-off did not stop the row";
	}
	return {};
}

/// A CC learned to a knob moves its param.
std::string knobLeg(Stage& s) {
	const std::optional<DispatchClips> c = dispatchClips(s);
	if (!c) {
		return "the song's session is not the fixture's";
	}
	const MIDITakeoverMode takeover = midiEngine.midiTakeover;
	midiEngine.midiTakeover = MIDITakeoverMode::JUMP;
	// A knob before it, on another CC, so the moved knob is not the owner's first.
	for (const auto& [cc, param] : {std::pair{21, params::LOCAL_PAN}, std::pair{20, params::LOCAL_LPF_FREQ}}) {
		if (!learn(s.song, {.source = {.channel_or_zone = 0, .note_or_cc = static_cast<uint8_t>(cc)},
		                    .target = deluge::project::ParamTarget{{ControlsAddress::Kind::OUTPUT, *s.synth.id()},
		                                                           knobParam(param)}})) {
			midiEngine.midiTakeover = takeover;
			return "a learn was refused";
		}
	}
	Clip* active = s.synth.getActiveClip();
	if (active == nullptr) {
		midiEngine.midiTakeover = takeover;
		return "the synth has no clip";
	}
	PatchedParamSet& patched = *active->paramManager.getPatchedParamSet();
	const int32_t freq = patched.getValue(params::LOCAL_LPF_FREQ);
	const int32_t pan = patched.getValue(params::LOCAL_PAN);
	std::string what;
	// A value in the middle of the range: absolute, whether or not the knob was learned as sending relative changes.
	send(*s.cables[0], kCC, 0, 20, 32);
	if (patched.getValue(params::LOCAL_LPF_FREQ) == freq) {
		what = "the CC did not move its knob's param";
	}
	else if (patched.getValue(params::LOCAL_PAN) != pan) {
		what = "the CC moved another knob's param";
	}
	send(*s.cables[0], kCC, 0, 20, 127);
	if (what.empty() && patched.getValue(params::LOCAL_LPF_FREQ) != freq) {
		what = "the CC at its top did not put the param back at its top";
	}
	midiEngine.midiTakeover = takeover;
	return what;
}

/// A note learned to a section and to a clip launches the section, then toggles the clip: the clip, of another section,
/// plays.
std::string sectionAndClipLeg(Stage& s) {
	const std::optional<DispatchClips> c = dispatchClips(s);
	if (!c) {
		return "the song's session is not the fixture's";
	}
	SessionPlaying restore{s.song};
	for (const MappingTarget& target : {MappingTarget{deluge::project::SectionLaunchTarget{2}},
	                                    MappingTarget{deluge::project::ClipMuteTarget{c->x->id()}}}) {
		if (!learn(s.song, {.source = {.channel_or_zone = 0, .note_or_cc = 76}, .target = target})) {
			return "a learn was refused";
		}
	}
	send(*s.cables[0], kNoteOn, 0, 76, 100);
	if (!c->kit->activeIfNoSolo) {
		return "the section did not launch";
	}
	if (!c->x->activeIfNoSolo) {
		return "the clip was not toggled after the section launched";
	}
	if (c->z->activeIfNoSolo) {
		return "the section's launch did not stop the other clips";
	}
	return {};
}

/// A clip toggle that changes the session mid-loop: toggling a clip recording linearly deletes its pending overdub,
/// which sits below it and is learned to the same note. Every other learned clip is toggled once and the deleted one
/// never. Then, two clips of one track learned to one note toggle in the session's order from its end: the later one
/// starts and the first, started after it, stops it.
std::string sessionChangesMidLoopLeg(Stage& s) {
	const std::optional<DispatchClips> c = dispatchClips(s);
	if (!c) {
		return "the song's session is not the fixture's";
	}
	SessionPlaying restore{s.song};
	const SessionLayoutType layout = s.song.persistentView.sessionLayout;
	const int32_t scroll = s.song.persistentView.songViewYScroll;
	s.song.persistentView.sessionLayout = SessionLayoutType::SessionLayoutTypeRows;
	Clip* pending = s.song.createPendingNextOverdubBelowClip(
	    c->s, s.song.live.clips.session_clips.getIndexForClip(c->s), OverDubType::Normal);
	s.song.persistentView.sessionLayout = layout;
	if (pending == nullptr || pending == c->s || !pending->isPendingOverdub) {
		return "no pending overdub was made";
	}
	c->s->activeIfNoSolo = false;
	c->x->activeIfNoSolo = false;
	c->y->activeIfNoSolo = false;
	const uint32_t pendingId = pending->id();
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	(void)c->s->beginLinearRecording(setupModelStackWithTimelineCounter(modelStackMemory, &s.song, c->s), 0);
	c->s->armState = ArmState::ON_NORMAL;
	std::string what;
	for (Clip* clip : {c->s, pending, c->x, c->y}) {
		if (what.empty()
		    && !learn(s.song, {.source = {.channel_or_zone = 0, .note_or_cc = 70},
		                       .target = deluge::project::ClipMuteTarget{clip->id()}})) {
			what = "a learn was refused";
		}
	}
	if (what.empty()) {
		send(*s.cables[0], kNoteOn, 0, 70, 100);
		bool pendingGone = true;
		for (Clip* clip : AllClips::everywhere(&s.song)) {
			pendingGone = pendingGone && clip->id() != pendingId;
		}
		if (!pendingGone) {
			what = "the recording clip's toggle did not delete its pending overdub";
		}
		else if (c->s->armState != ArmState::OFF || c->s->activeIfNoSolo) {
			what = "the recording clip was not toggled once";
		}
		else if (!c->x->activeIfNoSolo || !c->y->activeIfNoSolo) {
			what = "a clip learned to the note was not toggled once";
		}
	}
	c->s->abortRecording();
	s.song.persistentView.songViewYScroll = scroll;

	if (what.empty()) {
		c->z->activeIfNoSolo = false;
		for (Clip* clip : {c->z, c->s}) {
			if (what.empty()
			    && !learn(s.song, {.source = {.channel_or_zone = 0, .note_or_cc = 71},
			                       .target = deluge::project::ClipMuteTarget{clip->id()}})) {
				what = "a learn was refused";
			}
		}
		if (what.empty()) {
			send(*s.cables[0], kNoteOn, 0, 71, 100);
			if (!c->z->activeIfNoSolo || c->s->activeIfNoSolo) {
				what = "one track's clips were not toggled from the session's end: the first does not play";
			}
		}
	}
	return what;
}

/// Run the legs on @p f's song, each from an empty table; their failures, printed to @p io.
int32_t runLegs(const Fixture& f, std::array<MIDICable*, 2> cables, deluge::scenarios::driver::Io io) {
	Song& song = *f.song;
	Kit* kit = nullptr;
	Instrument* synth = nullptr;
	for (Output* o = song.live.media.first_output; o != nullptr; o = o->next) {
		if (o->type == OutputType::KIT && kit == nullptr) {
			kit = static_cast<Kit*>(o);
		}
		if (o->type == OutputType::SYNTH && synth == nullptr) {
			synth = static_cast<Instrument*>(o);
		}
	}
	if (kit == nullptr || synth == nullptr) {
		std::fprintf(io.err, "MAPPING-DISPATCH FAIL the legs have no kit or synth\n");
		return 1;
	}
	const bool differentiating = MIDIDeviceManager::differentiatingInputsByDevice;
	MIDIDeviceManager::differentiatingInputsByDevice = true;
	Stage stage{song, *kit, *synth, cables};
	constexpr std::array<std::pair<const char*, std::string (*)(Stage&)>, 17> kLegs{{
	    {"dispatch-clip-toggle", clipToggleLeg},
	    {"dispatch-section-arm", sectionArmLeg},
	    {"dispatch-row-mute", rowMuteLeg},
	    {"dispatch-row-sound", rowSoundLeg},
	    {"dispatch-knob", knobLeg},
	    {"dispatch-section-and-clip", sectionAndClipLeg},
	    {"dispatch-session-changes-mid-loop", sessionChangesMidLoopLeg},
	    {"late-device", lateDeviceLeg},
	    {"any-device", anyDeviceLeg},
	    {"hibernation", hibernationLeg},
	    {"session-order", sessionOrderLeg},
	    {"row-removed", rowRemovedLeg},
	    {"clone", cloneLeg},
	    {"render-writes", renderWritesLeg},
	    {"rebuild-drains", rebuildDrainsLeg},
	    {"app-loop-clone", appLoopCloneLeg},
	    {"song-swap", songSwapLeg},
	}};
	int32_t failures = 0;
	for (const auto& [name, leg] : kLegs) {
		install(f, MappingTable{});
		const std::string what = leg(stage);
		if (what.empty()) {
			std::fprintf(io.out, "MAPPING-DISPATCH leg %s ok\n", name);
		}
		else {
			std::fprintf(io.err, "MAPPING-DISPATCH FAIL leg %s: %s\n", name, what.c_str());
			std::fprintf(io.out, "MAPPING-DISPATCH leg %s FAILED\n", name);
			failures++;
		}
	}
	MIDIDeviceManager::differentiatingInputsByDevice = differentiating;
	install(f, MappingTable{});
	return failures;
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
			else if (MIDICable* named = deluge::bridge::find_cable(*entry.source.device); named == nullptr) {
				seen["absent-device"]++;
			}
			else if (named != cables[0] && named != cables[1]) {
				seen["unconnected-device"]++;
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
	const int32_t legFailures = runLegs(f, cables, io);
	deluge::midi::mappingIndex().clear();

	std::string hits;
	for (const auto& [kind, n] : seen) {
		hits += " " + kind + "=" + std::to_string(n);
	}
	std::fprintf(io.out, "MAPPING-DISPATCH cases=%d mismatches=%d hits:%s\n", static_cast<int>(cases),
	             static_cast<int>(mismatches), hits.c_str());
	int32_t failures = mismatches + legFailures;
	if (!broken.empty()) {
		std::fprintf(io.err, "MAPPING-DISPATCH FAIL %s\n", broken.c_str());
		failures++;
	}
	constexpr std::array<const char*, 10> kRequired{"knob",
	                                                "clip",
	                                                "rowmute",
	                                                "section",
	                                                "rowinput",
	                                                "zone-rowinput",
	                                                "any-device",
	                                                "absent-device",
	                                                "unconnected-device",
	                                                "device-told-apart"};
	if (cases < 10000 || std::ranges::any_of(kRequired, [&](const char* kind) { return seen[kind] == 0; })) {
		std::fprintf(io.err, "MAPPING-DISPATCH FAIL the property did not reach every kind of match\n");
		failures++;
	}
	return failures == 0 ? 0 : 3;
}

} // namespace

DELUGE_DRIVER_SCENARIO(mapping_dispatch, mapping_dispatch_main)
