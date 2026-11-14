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

#include "model/global_effectable/global_effectable.h"
#include "definitions_cxx.hpp"
#include "deluge-project/param_automation.h"
#include "dsp-next/core/stereo_sample.h"
#include "effects/codec.h"
#include "effects/compressor.h"
#include "effects/global_effectable/params.h"
#include "effects/plugin.h"
#include "effects/words.h"
#include "gui/l10n/l10n.h"
#include "gui/views/performance_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/led/indicator_leds.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/edits/change_effect.h"
#include "model/mod_controllable/mod_fx_names.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/param_projection.h"
#include "model/project_bridge/param_serialization.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/reverb_presets.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/render_scratch.h"
#include "storage/storage_manager.h"
#include "util/etl_string.h"
#include "util/firmware_version.h"
#include "util/functions.h"
#include "util/notify.h"
#include <limits>

using namespace deluge;
namespace params = deluge::modulation::params;

GlobalEffectable::GlobalEffectable() {
	unpatchedParamKind_ = params::Kind::UNPATCHED_GLOBAL;
}

void GlobalEffectable::cloneFrom(ModControllableAudio* other) {
	ModControllableAudio::cloneFrom(other);

	const auto& from = static_cast<GlobalEffectable&>(*other);
	setView(from.modFXParam(), from.filterType());
}

ModFXParam GlobalEffectable::modFXParam() const {
	return chainView().modFXParam();
}

FilterType GlobalEffectable::filterType() const {
	return chainView().filterType();
}

void GlobalEffectable::bindChain(deluge::project::FxChain& chain) {
	if (chainView().bound() == &chain) {
		return;
	}
	chain.effects = deluge::plugin::StateHandle::of<deluge::effects::DelugeEffects>(effects());
	chainView().bind(chain);
	bindEffects(chain.effects.as<deluge::effects::DelugeEffects>());
}

void GlobalEffectable::unbindChain() {
	if (chainView().bound() == nullptr) {
		return;
	}
	chainView().unbind();
	bindEffects(nullptr); // copies the record's effects in
}

void GlobalEffectable::setView(ModFXParam param, FilterType type) {
	writeView(param, type);
	projectModControllableSnapshot();
}

void GlobalEffectable::writeView(ModFXParam param, FilterType type) {
	chainView().write(param, type);
}

std::optional<ModFXParam> GlobalEffectable::projectedModFXParam() const {
	return modFXParam();
}

void GlobalEffectable::initParams(ParamManager* paramManager) {
	ModControllableAudio::initParams(paramManager, params::Kind::UNPATCHED_GLOBAL);

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	unpatchedParams->kind = deluge::modulation::params::Kind::UNPATCHED_GLOBAL;
	setParamsToDefaults(*unpatchedParams, params::Kind::UNPATCHED_GLOBAL,
	                    {params::UNPATCHED_ARP_RATE, params::UNPATCHED_MOD_FX_RATE, params::UNPATCHED_MOD_FX_DEPTH,
	                     params::UNPATCHED_DELAY_RATE, params::UNPATCHED_PAN, params::UNPATCHED_DELAY_AMOUNT,
	                     params::UNPATCHED_REVERB_SEND_AMOUNT, params::UNPATCHED_VOLUME,
	                     params::UNPATCHED_SIDECHAIN_VOLUME, params::UNPATCHED_PITCH_ADJUST, params::UNPATCHED_LPF_RES,
	                     params::UNPATCHED_LPF_FREQ, params::UNPATCHED_HPF_RES, params::UNPATCHED_HPF_FREQ,
	                     params::UNPATCHED_LPF_MORPH, params::UNPATCHED_HPF_MORPH});
}

void GlobalEffectable::initParamsForAudioClip(ParamManagerForTimeline* paramManager) {
	initParams(paramManager);
	// An audio track starts quieter than the song and a kit's affect-entire, which keep the table's default
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	unpatchedParams->params[params::UNPATCHED_VOLUME].setBase(*unpatchedParams->docForParam(params::UNPATCHED_VOLUME),
	                                                          -536870912);
}

void GlobalEffectable::writeAttributesToFile(Serializer& writer, bool writeAutomation) {
	// Its record's, while it plays one.
	writer.writeAttribute("modFXCurrentParam", modFXParamToString(modFXParam()));
	writer.writeAttribute("currentFilterType", filterTypeToString(filterType()));
	ModControllableAudio::writeAttributesToFile(writer, effects());
	// Community Firmware parameters (always write them after the official ones, just before closing the parent tag)
	// <--
}

void GlobalEffectable::writeTagsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
                                       Song* knobsSong, std::optional<deluge::project::ControlsAddress> knobsOwner) {
	if (paramManager) {
		writer.writeOpeningTagBeginning("defaultParams");
		GlobalEffectable::writeParamAttributesToFile(writer, paramManager, writeAutomation);
		writer.writeOpeningTagEnd();
		GlobalEffectable::writeParamTagsToFile(writer, paramManager, writeAutomation);
		writer.writeClosingTag("defaultParams");
	}

	// A track chain's render follows its threshold param; the file holds the setting, as the record does.
	ModControllableAudio::writeTagsToFile(writer, effects(), knobsSong, knobsOwner);
}

void GlobalEffectable::writeParamAttributesToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
                                                  int32_t* valuesForOverride) {
	// Serialize from the value DOM (deluge::project). serializeGlobalEffectableParamAttributes emits the global flat
	// attrs around the shared ModControllableAudio attrs and the community attrs.
	deluge::project::DocParamSet unpatched =
	    deluge::bridge::toDocParamSetFromStore(*paramManager->getUnpatchedParamSet());
	deluge::bridge::serializeGlobalEffectableParamAttributes(unpatched, writer, writeAutomation, valuesForOverride);
}

void GlobalEffectable::writeParamTagsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
                                            int32_t* valuesForOverride) {
	deluge::project::DocParamSet unpatched =
	    deluge::bridge::toDocParamSetFromStore(*paramManager->getUnpatchedParamSet());
	deluge::bridge::serializeGlobalEffectableParamTags(unpatched, writer, writeAutomation, valuesForOverride);
}

void GlobalEffectable::readParamsFromFile(Deserializer& reader, ParamManagerForTimeline* paramManager,
                                          int32_t readAutomationUpToPos) {
	char const* tagName;

	reader.match('{'); // descend into the <defaultParams> object (needed for JSON; a no-op in XML)
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (readParamTagFromFile(reader, tagName, paramManager, readAutomationUpToPos)) {}
		else {
			reader.exitTag(tagName);
		}
	}
	reader.match('}');
}

void GlobalEffectable::readParamsFromFileDomFirst(Deserializer& reader, ParamManagerForTimeline* paramManager,
                                                  int32_t readAutomationUpToPos) {
	// initParams already wrote each default straight into the set's own DOM store (via docForParam), so file-omitted
	// params keep their default with NO engine->store copy. Read the present params into the store via the same
	// tables the engine reader uses, then project each engine RT cursor from the store.
	UnpatchedParamSet* unpatched = paramManager->getUnpatchedParamSet();
	ParamCollectionSummary* summary = paramManager->getUnpatchedParamSetSummary();
	unpatched->bindDomStore();
	deluge::project::DocParamSet* store = unpatched->domStore();
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (deluge::bridge::readGlobalEffectableParamTagToDoc(reader, tagName, *store, readAutomationUpToPos)) {}
		else {
			reader.exitTag(tagName);
		}
	}
	unpatched->projectFromStoreAfterRead(summary);
}

bool GlobalEffectable::readParamTagFromFile(Deserializer& reader, char const* tagName,
                                            ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos) {
	// Read the GlobalEffectable param block from the value DOM (deluge::project), as the writer writes it.
	ParamCollectionSummary* unpatchedParamsSummary = paramManager->getUnpatchedParamSetSummary();
	UnpatchedParamSet* unpatchedParams = (UnpatchedParamSet*)unpatchedParamsSummary->paramCollection;
	return deluge::bridge::readGlobalEffectableParamTag(reader, tagName, *unpatchedParams, *unpatchedParamsSummary,
	                                                    readAutomationUpToPos);
}

// paramManager is optional
Error GlobalEffectable::readTagFromFile(Deserializer& reader, char const* tagName,
                                        ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos,
                                        ArpeggiatorSettings* arpSettings, Song* song) {

	// else
	if (paramManager && !strcmp(tagName, "defaultParams")) {

		if (!paramManager->containsAnyMainParamCollections()) {
			Error error = paramManager->setupUnpatched();
			if (error != Error::NONE) {
				return error;
			}
			initParams(paramManager);
		}

		GlobalEffectable::readParamsFromFile(reader, paramManager, readAutomationUpToPos);
		reader.exitTag("defaultParams");
	}

	else {
		return ModControllableAudio::readTagFromFile(reader, tagName, NULL, readAutomationUpToPos, arpSettings, song);
	}

	return Error::NONE;
}

void GlobalEffectable::readEffectsFromElement(const deluge::serialization::Value& element, int32_t tickMagnitude) {
	deluge::effects::EffectStates next = effects();
	deluge::effects::decode(element, next, {.input_tick_magnitude = tickMagnitude});
	delayTypePending = !deluge::effects::saves_delay_type(element);
	ModFXParam param = modFXParam();
	FilterType type = filterType();
	if (const deluge::serialization::Value* word = element.find("modFXCurrentParam"); word && word->isScalar()) {
		param = stringToModFXParam(word->str().c_str());
	}
	if (const deluge::serialization::Value* word = element.find("currentFilterType"); word && word->isScalar()) {
		type = stringToFilterType(word->str().c_str());
	}
	writeView(param, type);
	setEffects(next); // projects the view with the effects
}

namespace modfx {
const char* getParamName(ModFXType type, ModFXParam param, bool shortName) {
	using enum deluge::l10n::String;
	using namespace deluge;

	switch (type) {
	case ModFXType::GRAIN: {
		switch (param) {
		case ModFXParam::DEPTH:
			return l10n::get(shortName ? STRING_FOR_GRAIN_AMOUNT_SHORT : STRING_FOR_GRAIN_AMOUNT);
		case ModFXParam::FEEDBACK:
			return l10n::get(shortName ? STRING_FOR_GRAIN_RANDOMNESS_SHORT : STRING_FOR_GRAIN_RANDOMNESS);
		case ModFXParam::OFFSET:
			return l10n::get(shortName ? STRING_FOR_GRAIN_DENSITY_SHORT : STRING_FOR_GRAIN_DENSITY);
		default:
			return l10n::get(STRING_FOR_NONE);
		}
	}

	default: {
		switch (param) {
		case ModFXParam::DEPTH:
			return l10n::get(shortName ? STRING_FOR_DEPTH_SHORT : STRING_FOR_DEPTH);
		case ModFXParam::FEEDBACK:
			return l10n::get(shortName ? STRING_FOR_FEEDBACK_SHORT : STRING_FOR_FEEDBACK);
		case ModFXParam::OFFSET:
			return l10n::get(shortName ? STRING_FOR_OFFSET_SHORT : STRING_FOR_OFFSET);
		default:
			return l10n::get(STRING_FOR_NONE);
		}
	}
	}
}

const char* modFXToString(ModFXType type) {
	switch (type) {
		using namespace deluge;
		using enum deluge::l10n::String;
	case ModFXType::FLANGER:
		return l10n::get(STRING_FOR_FLANGER);
	case ModFXType::PHASER:
		return l10n::get(STRING_FOR_PHASER);
	case ModFXType::CHORUS:
		return l10n::get(STRING_FOR_CHORUS);
	case ModFXType::CHORUS_STEREO:
		return l10n::get(STRING_FOR_STEREO_CHORUS);
	case ModFXType::DIMENSION:
		return l10n::get(STRING_FOR_DIMENSION);
	case ModFXType::GRAIN:
		return l10n::get(STRING_FOR_GRAIN);
	case ModFXType::WARBLE:
		return l10n::get(STRING_FOR_WARBLE);
	default:
		return l10n::get(STRING_FOR_DISABLED);
	}
}
} // namespace modfx
