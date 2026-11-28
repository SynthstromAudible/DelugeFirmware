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

#include "model/fx/chain_codec.h"

#include "arp/params.h"
#include "effects/codec.h"
#include "instruments/classic/engine_params.h"
#include "model/fx/chain_params.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/project_bridge/midi_command.h"
#include "model/project_bridge/param_projection.h"
#include "model/project_bridge/param_serialization.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "modulation/params/arp_param_set.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound.h"
#include "storage/storage_manager.h"
#include "storage/value_tree_deserializer.h"
#include "util/functions.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace deluge::model {

namespace params = deluge::modulation::params;

namespace {

/// The codec context a file's effect entries are written in: the current song's tick magnitude, even when the song
/// being saved is another (the learned knobs take the saved song; this does not).
[[nodiscard]] plugin::CodecContext effectsCodecContext() {
	return {.input_tick_magnitude = currentSong->getInputTickMagnitude()};
}

/// @brief Write one learned knob as a boxed `midiKnob` item: @p attributes' channel and CC, its relative flag and the
/// param @p descriptor names (with its patch sources), then its device, which @p writeDevice writes when @p hasDevice.
/// @param writer        Where the item goes.
/// @param attributes    The knob's channel and CC.
/// @param relative      Whether the knob sends relative changes.
/// @param paramName     The file name of the param the knob turns (a cable's: the param its depth patches).
/// @param cable         The cable whose depth the knob turns, or nullptr for a param.
/// @param hasDevice     Whether the knob names a device.
/// @param writeDevice   Writes the device element; called only when @p hasDevice.
template <typename WriteDevice>
void writeMidiKnob(Serializer& writer, LearnedMIDI attributes, bool relative, char const* paramName,
                   const synth::CableTarget* cable, bool hasDevice, WriteDevice writeDevice) {
	writer.writeOpeningTagBeginning("midiKnob", true);
	attributes.writeAttributesToFile(writer, MIDI_MESSAGE_CC);
	writer.writeAttribute("relative", relative);
	writer.writeAttribute("controlsParam", paramName);
	if (cable != nullptr) { // only a sound's knob can turn a patch cable's depth
		writer.writeAttribute("patchAmountFromSource", sourceToString(cable->top_source()));
		if (cable->has_second_source()) {
			writer.writeAttribute("patchAmountFromSecondSource", sourceToString(cable->second_source_from_top()));
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

/// Reads a host chain's param block, the engine way.
bool readGlobalParamTag(Deserializer& reader, char const* tagName, ParamManagerForTimeline* paramManager,
                        int32_t readAutomationUpToPos) {
	ParamCollectionSummary* unpatchedParamsSummary = paramManager->getUnpatchedParamSetSummary();
	auto* unpatchedParams = static_cast<UnpatchedParamSet*>(unpatchedParamsSummary->paramCollection);
	// A kit's own arp params land in its arp set.
	const bridge::LiveArp arp{.set = paramManager->getArpParamSet(), .summary = paramManager->getArpParamSetSummary()};
	return bridge::readGlobalEffectableParamTag(reader, tagName, *unpatchedParams, *unpatchedParamsSummary,
	                                            readAutomationUpToPos, arp);
}

void readGlobalParams(Deserializer& reader, ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos) {
	char const* tagName;
	reader.match('{'); // descend into the <defaultParams> object (needed for JSON; a no-op in XML)
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (readGlobalParamTag(reader, tagName, paramManager, readAutomationUpToPos)) {}
		else {
			reader.exitTag(tagName);
		}
	}
	reader.match('}');
}

} // namespace

void writeChainAttributes(Serializer& writer, EffectsOwner owner) {
	// Its record's, while it plays one.
	writer.writeAttribute("modFXCurrentParam", modFXParamToString(owner.view->modFXParam()));
	writer.writeAttribute("currentFilterType", filterTypeToString(owner.view->filterType()));
	writer.writeValue(effects::encode_pieces(owner.effects(), effectsCodecContext()).attributes);
}

void writeEffectTags(Serializer& writer, EffectsOwner owner, ParamManager* paramManager, bool writeAutomation,
                     ::Song* knobsSong, std::optional<project::ControlsAddress> knobsOwner) {
	if (paramManager != nullptr) {
		writer.writeOpeningTagBeginning("defaultParams");
		writeGlobalParamAttributes(writer, paramManager, writeAutomation);
		writer.writeOpeningTagEnd();
		writeGlobalParamTags(writer, paramManager, writeAutomation);
		writer.writeClosingTag("defaultParams");
	}
	// A track chain's render follows its threshold param; the file holds the setting, as the record does.
	const effects::EffectPieces pieces = effects::encode_pieces(owner.effects(), effectsCodecContext());
	writer.writeValue(pieces.delay);
	writeMidiKnobs(writer, owner, knobsSong, knobsOwner);
	writer.writeValue(pieces.tags);
}

void writeMidiKnobs(Serializer& writer, EffectsOwner owner, ::Song* song,
                    std::optional<project::ControlsAddress> address) {
	std::optional<std::vector<project::Mapping>> saved =
	    song != nullptr ? bridge::savedKnobsOf(*song, owner, address) : std::nullopt;
	if (!saved && !owner.knobs->loaded.empty()) {
		saved = owner.knobs->loaded; // an owner no song holds keeps its knobs here
	}
	if (!saved) {
		return;
	}
	// The file names engine and arp params only; a sound's, only its playing engine's and its own
	std::erase_if(*saved, [&owner](const project::Mapping& knob) {
		const project::ParamTarget& param = std::get<project::OwnerParamTarget>(knob.target).param;
		if (param.space != project::ParamSpace::ENGINE) {
			return param.space != project::ParamSpace::ARP;
		}
		return owner.sound != nullptr && !instruments::classic::target_is_live(owner.sound->getSynthMode(), param.id);
	});
	if (saved->empty()) {
		return;
	}
	const params::Kind kind = owner.unpatchedKind();
	writer.writeArrayStart("midiKnobs");
	for (const project::Mapping& knob : *saved) {
		const LearnedMIDI attributes =
		    bridge::learned_of({.channel_or_zone = knob.source.channel_or_zone, .note_or_cc = knob.source.note_or_cc});
		const project::ParamTarget& param = std::get<project::OwnerParamTarget>(knob.target).param;
		const auto writeDevice = [&] { bridge::write_reference(writer, "device", *knob.source.device); };
		if (param.space == project::ParamSpace::ARP) { // an arp param, by the arp's file name
			writeMidiKnob(writer, attributes, knob.relative, arp::row(param.id).info.file_name.data(), nullptr,
			              knob.source.device.has_value(), writeDevice);
			continue;
		}
		const synth::CableTarget descriptor = modulation::knobWord(bridge::knobOfControl(param, kind));
		writeMidiKnob(writer, attributes, knob.relative, params::paramNameForFile(kind, descriptor.param_index()),
		              descriptor.is_param() ? nullptr : &descriptor, knob.source.device.has_value(), writeDevice);
	}
	writer.writeArrayEnding("midiKnobs");
}

Error readEffectTag(Deserializer& reader, const char* tagName, EffectsOwner owner,
                    ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos,
                    ArpeggiatorSettings* arpSettings, bool* arpRead, ::Song* song) {
	if (paramManager != nullptr && std::strcmp(tagName, "defaultParams") == 0) {
		if (!paramManager->containsAnyMainParamCollections()) {
			// An owner with an arp (a kit) holds its arp's set beside its own params.
			const Error error = paramManager->setupUnpatched(
			    arpSettings != nullptr ? std::optional{deluge::arp::Owner::KIT} : std::nullopt);
			if (error != Error::NONE) {
				return error;
			}
			initGlobalParams(paramManager);
		}
		readGlobalParams(reader, paramManager, readAutomationUpToPos);
		reader.exitTag("defaultParams");
	}
	// An owner's effects are read from its element (the effects library's codec), not here.
	else if (std::strcmp(tagName, "arpeggiator") == 0 && arpSettings != nullptr) {
		if (arpRead != nullptr) {
			*arpRead = true;
		}
		// Set default values in case they are not configured
		arpSettings->syncType = SYNC_TYPE_EVEN;
		arpSettings->syncLevel = SYNC_LEVEL_NONE;
		reader.match('{');
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			const bool readAndExited = arpSettings->readCommonTagsFromFile(reader, tagName, song);
			if (!readAndExited) {
				reader.exitTag(tagName);
			}
		}
		reader.exitTag("arpeggiator", true);
	}
	else if (std::strcmp(tagName, "midiKnobs") == 0) {
		// The model's codec reads the learned knobs; the song's table takes them once a song holds this owner.
		if (const storage::Value* knobs = storage::treeReaderOf(reader).currentValue()) {
			owner.knobs->loaded = bridge::loadedKnobsOf(*knobs, owner.sound != nullptr);
		}
		reader.exitTag("midiKnobs");
	}
	else {
		return Error::RESULT_TAG_UNUSED;
	}
	return Error::NONE;
}

void readEffectsFromElement(EffectsOwner owner, const serialization::Value& element, int32_t tickMagnitude) {
	effects::EffectStates next = owner.effects();
	effects::decode(element, next, {.input_tick_magnitude = tickMagnitude});
	owner.binding->delay_type_pending = !effects::saves_delay_type(element);
	ModFXParam param = owner.view->modFXParam();
	FilterType type = owner.view->filterType();
	if (const serialization::Value* word = element.find("modFXCurrentParam"); word && word->isScalar()) {
		param = stringToModFXParam(word->str().c_str());
	}
	if (const serialization::Value* word = element.find("currentFilterType"); word && word->isScalar()) {
		type = stringToFilterType(word->str().c_str());
	}
	owner.view->write(param, type);
	storeEffects(owner, next); // projects the view with the effects
}

void writeGlobalParamAttributes(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
                                int32_t* valuesForOverride) {
	// Serialized from the value DOM: the global flat attributes around the shared ones and the community ones.
	project::DocParamSet unpatched = bridge::toDocParamSetFromStore(*paramManager->getUnpatchedParamSet());
	std::optional<project::DocParamSet> arp;
	if (ArpParamSet* set = paramManager->getArpParamSet()) {
		arp = bridge::toDocParamSetFromStore(*set);
	}
	bridge::serializeGlobalEffectableParamAttributes(unpatched, writer, writeAutomation, valuesForOverride,
	                                                 arp ? &*arp : nullptr);
}

void writeGlobalParamTags(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
                          int32_t* valuesForOverride) {
	project::DocParamSet unpatched = bridge::toDocParamSetFromStore(*paramManager->getUnpatchedParamSet());
	bridge::serializeGlobalEffectableParamTags(unpatched, writer, writeAutomation, valuesForOverride);
}

void readGlobalParamsDomFirst(Deserializer& reader, ParamManagerForTimeline* paramManager,
                              int32_t readAutomationUpToPos) {
	// initGlobalParams already wrote each default straight into the set's own DOM store (via docForParam), so
	// file-omitted params keep their default with no engine-to-store copy. Read the present params into the store
	// through the same tables the engine reader uses, then project each engine RT cursor from the store.
	UnpatchedParamSet* unpatched = paramManager->getUnpatchedParamSet();
	ParamCollectionSummary* summary = paramManager->getUnpatchedParamSetSummary();
	unpatched->bindDomStore();
	project::DocParamSet* store = unpatched->domStore();
	// A kit's own arp params land in its arp set's store.
	ArpParamSet* arp = paramManager->getArpParamSet();
	project::DocParamSet* arpStore = nullptr;
	if (arp != nullptr) {
		arp->bindDomStore();
		arpStore = arp->domStore();
	}
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (bridge::readGlobalEffectableParamTagToDoc(reader, tagName, *store, readAutomationUpToPos, arpStore)) {}
		else {
			reader.exitTag(tagName);
		}
	}
	unpatched->projectFromStoreAfterRead(summary);
	if (arp != nullptr) {
		arp->projectFromStoreAfterRead(paramManager->getArpParamSetSummary());
	}
}

} // namespace deluge::model
