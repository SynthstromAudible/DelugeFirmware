/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "model/mod_controllable/mod_controllable_audio.h"
#include "definitions_cxx.hpp"
#include "deluge-project/param_automation.h"
#include "deluge/model/settings/runtime_feature_settings.h"
#include "dsp-next/core/stereo_sample.h"
#include "effects/codec.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "instruments/deluge/sound_state.h"
#include "io/debug/log.h"
#include "io/midi/midi_engine.h"
#include "mem_functions.h"
#include "memory/general_memory_allocator.h"
#include "memory/heaps.h"
#include "memory/stack_guard.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/change_effect.h"
#include "model/fx/channel_setup.h"
#include "model/fx/song_stutter.h"
#include "model/mod_controllable/delay_arena_app.h"
#include "model/mod_controllable/delay_type.h"
#include "model/mod_controllable/mod_fx_allocator.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/project_bridge/midi_command.h"
#include "model/project_bridge/param_projection.h"
#include "model/project_bridge/param_serialization.h"
#include "model/song/reverb_presets.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "modulation/sidechain/sidechain.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "storage/storage_manager.h"
#include "storage/value_tree_deserializer.h"
#include "util/etl_string.h"
#include "util/functions.h"
#include "util/structural_edit_probe.h"
#include <algorithm>
#include <span>

namespace params = deluge::modulation::params;

ModControllableAudio::ModControllableAudio() = default;

ModControllableAudio::~ModControllableAudio() = default;

void ModControllableAudio::cloneFrom(ModControllableAudio* other) {
	// A clone takes the other's filters, saturation, mod-FX, sidechain, delay and stutter; it keeps its own
	// compressor.
	const deluge::effects::EffectStates& from = other->effects();
	deluge::effects::EffectStates next = effects();
	next.filter = from.filter;
	next.saturation = from.saturation;
	next.mod_fx = from.mod_fx;
	next.sidechain = from.sidechain;
	next.delay = from.delay;
	next.stutter = from.stutter;
	setEffects(next);
	loaded_knobs = other->loaded_knobs; // an owner no song holds keeps its knobs here (empty for one a song holds)
}

void ModControllableAudio::initParams(ParamManager* paramManager, params::Kind defaults) {
	UnpatchedParamSet& unpatchedParams = *paramManager->getUnpatchedParamSet();
	setParamsToDefaults(
	    unpatchedParams, defaults,
	    {params::UNPATCHED_BASS, params::UNPATCHED_TREBLE, params::UNPATCHED_BASS_FREQ, params::UNPATCHED_TREBLE_FREQ,
	     params::UNPATCHED_ARP_GATE, params::UNPATCHED_NOTE_PROBABILITY, params::UNPATCHED_ARP_BASS_PROBABILITY,
	     params::UNPATCHED_ARP_SWAP_PROBABILITY, params::UNPATCHED_ARP_GLIDE_PROBABILITY,
	     params::UNPATCHED_REVERSE_PROBABILITY, params::UNPATCHED_ARP_CHORD_PROBABILITY,
	     params::UNPATCHED_ARP_RATCHET_PROBABILITY, params::UNPATCHED_ARP_RATCHET_AMOUNT,
	     params::UNPATCHED_ARP_SEQUENCE_LENGTH, params::UNPATCHED_ARP_CHORD_POLYPHONY, params::UNPATCHED_ARP_RHYTHM,
	     params::UNPATCHED_SPREAD_VELOCITY, params::UNPATCHED_ARP_SPREAD_GATE, params::UNPATCHED_ARP_SPREAD_OCTAVE});
	deluge::model::initStutterParams(paramManager, defaults);
	setParamsToDefaults(unpatchedParams, defaults,
	                    {params::UNPATCHED_MOD_FX_OFFSET, params::UNPATCHED_MOD_FX_FEEDBACK,
	                     params::UNPATCHED_SAMPLE_RATE_REDUCTION, params::UNPATCHED_BITCRUSHING,
	                     params::UNPATCHED_SIDECHAIN_SHAPE, params::UNPATCHED_COMPRESSOR_THRESHOLD});
}

void ModControllableAudio::writeAttributesToFile(Serializer& writer, const deluge::effects::EffectStates& states) {
	writer.writeValue(deluge::effects::encode_pieces(states, effectsCodecContext()).attributes);
}

void ModControllableAudio::setEffects(const deluge::effects::EffectStates& next) {
	deluge::model::storeEffects(*effectsOwner(), next);
}

void ModControllableAudio::bindEffects(deluge::effects::EffectStates* record) {
	deluge::model::bindEffects(*effectsOwner(), record);
}

void ModControllableAudio::projectModControllableSnapshot() {
	deluge::model::projectEffects(*effectsOwner());
}

void ModControllableAudio::writeTagsToFile(Serializer& writer, const deluge::effects::EffectStates& states,
                                           Song* knobsSong, std::optional<deluge::project::ControlsAddress> knobsOwner,
                                           std::optional<int32_t> compressorThreshold) {
	const deluge::effects::EffectPieces pieces =
	    deluge::effects::encode_pieces(states, effectsCodecContext(), compressorThreshold);
	writer.writeValue(pieces.delay);
	writeMidiKnobsToFile(writer, knobsSong, knobsOwner);
	writer.writeValue(pieces.tags);
}

deluge::plugin::CodecContext ModControllableAudio::effectsCodecContext() {
	return {.input_tick_magnitude = currentSong->getInputTickMagnitude()};
}

namespace {

/// @brief Write one learned knob as a boxed `midiKnob` item: @p attributes' channel and CC, its relative flag and the
/// param @p descriptor names (with its patch sources), then its device, which @p writeDevice writes when @p hasDevice.
/// @param writer        Where the item goes.
/// @param attributes    The knob's channel and CC.
/// @param relative      Whether the knob sends relative changes.
/// @param descriptor    The param (and patch sources) the knob turns.
/// @param unpatchedKind Whose unpatched params @p descriptor names (a sound's or a global effectable's).
/// @param hasDevice     Whether the knob names a device.
/// @param writeDevice   Writes the device element; called only when @p hasDevice.
template <typename WriteDevice>
void writeMidiKnob(Serializer& writer, LearnedMIDI attributes, bool relative, deluge::synth::CableTarget descriptor,
                   params::Kind unpatchedKind, bool hasDevice, WriteDevice writeDevice) {
	writer.writeOpeningTagBeginning("midiKnob", true);
	attributes.writeAttributesToFile(writer, MIDI_MESSAGE_CC);
	writer.writeAttribute("relative", relative);
	writer.writeAttribute("controlsParam", params::paramNameForFile(unpatchedKind, descriptor.param_index()));
	if (!descriptor.is_param()) { // only a sound's knob can turn a patch cable's depth
		writer.writeAttribute("patchAmountFromSource", sourceToString(descriptor.top_source()));
		if (descriptor.has_second_source()) {
			writer.writeAttribute("patchAmountFromSecondSource", sourceToString(descriptor.second_source_from_top()));
		}
	}
	// The device is an element of its own, so it closes the knob's attributes.
	if (hasDevice) {
		writer.writeOpeningTagEnd();
		writeDevice();
		writer.writeClosingTag("midiKnob", true, true);
	}
	else {
		writer.closeTag(true); // the knob is a boxed list item
	}
}

} // namespace

void ModControllableAudio::writeMidiKnobsToFile(Serializer& writer, Song* song,
                                                std::optional<deluge::project::ControlsAddress> owner) {
	std::optional<std::vector<deluge::project::Mapping>> saved =
	    song != nullptr ? deluge::bridge::savedKnobsOf(*song, *this, owner) : std::nullopt;
	if (!saved && !loaded_knobs.empty()) {
		saved = loaded_knobs; // an owner no song holds keeps its knobs here
	}
	if (saved) {
		// The file names engine params only.
		std::erase_if(*saved, [](const deluge::project::Mapping& knob) {
			return std::get<deluge::project::OwnerParamTarget>(knob.target).param.space
			       != deluge::project::ParamSpace::ENGINE;
		});
		if (saved->empty()) {
			return;
		}
		writer.writeArrayStart("midiKnobs");
		for (const deluge::project::Mapping& knob : *saved) {
			const LearnedMIDI attributes = deluge::bridge::learned_of(
			    {.channel_or_zone = knob.source.channel_or_zone, .note_or_cc = knob.source.note_or_cc});
			const deluge::synth::CableTarget descriptor = deluge::modulation::knobWord(deluge::bridge::knobOfControl(
			    std::get<deluge::project::OwnerParamTarget>(knob.target).param, unpatchedParamKind_));
			writeMidiKnob(writer, attributes, knob.relative, descriptor, unpatchedParamKind_,
			              knob.source.device.has_value(),
			              [&] { deluge::bridge::write_reference(writer, "device", *knob.source.device); });
		}
		writer.writeArrayEnding("midiKnobs");
	}
}

void ModControllableAudio::writeParamAttributesToFile(Serializer& writer, ParamManager* paramManager,
                                                      bool writeAutomation, int32_t* valuesForOverride) {
	// Serialize from the value DOM (deluge::project).
	deluge::project::DocParamSet unpatched = deluge::bridge::toDocParamSet(*paramManager->getUnpatchedParamSet());
	deluge::bridge::serializeModControllableParamAttributes(unpatched, writer, writeAutomation, valuesForOverride);
}

void ModControllableAudio::writeParamTagsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
                                                int32_t* valuesForOverride) {
	deluge::project::DocParamSet unpatched = deluge::bridge::toDocParamSet(*paramManager->getUnpatchedParamSet());
	deluge::bridge::serializeModControllableParamTags(unpatched, writer, writeAutomation, valuesForOverride);
}

// paramManager is optional
Error ModControllableAudio::readTagFromFile(Deserializer& reader, char const* tagName,
                                            ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos,
                                            ArpeggiatorSettings* arpSettings, Song* song) {

	// An owner's effects are read from its element (the effects library's codec), not here.
	if (!strcmp(tagName, "arpeggiator") && arpSettings != nullptr) {
		arpRead = true;
		// Set default values in case they are not configured
		arpSettings->syncType = SYNC_TYPE_EVEN;
		arpSettings->syncLevel = SYNC_LEVEL_NONE;
		reader.match('{');
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			bool readAndExited = arpSettings->readCommonTagsFromFile(reader, tagName, song);
			if (!readAndExited) {
				reader.exitTag(tagName);
			}
		}

		reader.exitTag("arpeggiator", true);
	}

	else if (!strcmp(tagName, "midiKnobs")) {
		// The model's codec reads the learned knobs; the song's table takes them once a song holds this owner.
		if (const deluge::storage::Value* knobs = deluge::storage::treeReaderOf(reader).currentValue()) {
			loaded_knobs = deluge::bridge::loadedKnobsOf(
			    *knobs, unpatchedParamKind_ == deluge::modulation::params::Kind::UNPATCHED_SOUND);
		}
		reader.exitTag("midiKnobs");
	}

	else {
		return Error::RESULT_TAG_UNUSED;
	}

	return Error::NONE;
}
