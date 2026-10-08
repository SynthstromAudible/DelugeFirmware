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
#include "ModFXProcessor.h"
#include "definitions_cxx.hpp"
#include "deluge-project/param_automation.h"
#include "deluge/dsp/granular/GranularProcessor.h"
#include "deluge/model/settings/runtime_feature_settings.h"
#include "dsp/stereo_sample.h"
#include "engines/deluge/sound_state.h"
#include "engines/effects/codec.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "gui/views/automation_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "io/debug/log.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_takeover.h"
#include "mem_functions.h"
#include "memory/heaps.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/change_effect.h"
#include "model/edits/edit.h"
#include "model/edits/param_address_for.h"
#include "model/edits/set_param_value.h"
#include "model/note/note_row.h"
#include "model/project_bridge/param_projection.h"
#include "model/project_bridge/param_serialization.h"
#include "model/song/song.h"
#include "modulation/knob.h"
#include "modulation/params/param_set.h"
#include "modulation/sidechain/sidechain.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "storage/storage_manager.h"
#include "util/etl_string.h"
#include <algorithm>

namespace params = deluge::modulation::params;

extern int32_t spareRenderingBuffer[][SSI_TX_BUFFER_NUM_SAMPLES];

ModControllableAudio::ModControllableAudio() {

	// Grain

	// Filters

	// Sample rate reduction

	SyncLevel syncLevel;
	Song* song = preLoadedSong;
	if (!song) {
		song = currentSong;
	}
	if (song) {
		syncLevel = (SyncLevel)(8
		                        - (song->project.transport.inside_world_tick_magnitude
		                           + song->project.transport.inside_world_tick_magnitude_offset_from_bpm));
	}
	else {
		syncLevel = (SyncLevel)(8 - FlashStorage::defaultMagnitude);
	}
	ownEffects_.delay.sync_level = syncLevel;
	ownEffects_.sidechain = deluge::modulation::default_sidechain_state();
	modControllableSnapshot_.effects = ownEffects_; // nothing renders this owner yet
}

ModControllableAudio::~ModControllableAudio() {
	// delay.discardBuffers(); // No! The DelayBuffers will themselves destruct and do this
	delete grainFX;
}

void ModControllableAudio::cloneFrom(ModControllableAudio* other) {
	// A clone takes the other's filters, saturation, mod-FX, sidechain and stutter, and its delay but for its sync
	// type; it keeps its own compressor.
	const deluge::effects::EffectStates& from = other->effects();
	deluge::effects::EffectStates next = effects();
	next.filter = from.filter;
	next.saturation = from.saturation;
	next.mod_fx = from.mod_fx;
	next.sidechain = from.sidechain;
	next.delay.ping_pong = from.delay.ping_pong;
	next.delay.analog = from.delay.analog;
	next.delay.sync_level = from.delay.sync_level;
	next.stutter = from.stutter;
	setEffects(next);
	equalizer_.copy_coefficients_from(other->equalizer_);
	midi_knobs = other->midi_knobs; // Could fail if no RAM... not too big a concern
}

void ModControllableAudio::initParams(ParamManager* paramManager) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	unpatchedParams->params[params::UNPATCHED_BASS].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_BASS), 0);
	unpatchedParams->params[params::UNPATCHED_TREBLE].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_TREBLE), 0);
	unpatchedParams->params[params::UNPATCHED_BASS_FREQ].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_BASS_FREQ), 0);
	unpatchedParams->params[params::UNPATCHED_TREBLE_FREQ].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_TREBLE_FREQ), 0);

	unpatchedParams->params[params::UNPATCHED_ARP_GATE].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_ARP_GATE), 0);
	unpatchedParams->params[params::UNPATCHED_NOTE_PROBABILITY].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_NOTE_PROBABILITY), 2147483647);
	unpatchedParams->params[params::UNPATCHED_ARP_BASS_PROBABILITY].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_ARP_BASS_PROBABILITY), -2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_SWAP_PROBABILITY].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_ARP_SWAP_PROBABILITY), -2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_GLIDE_PROBABILITY].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_ARP_GLIDE_PROBABILITY), -2147483648);
	unpatchedParams->params[params::UNPATCHED_REVERSE_PROBABILITY].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_REVERSE_PROBABILITY), -2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_CHORD_PROBABILITY].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_ARP_CHORD_PROBABILITY), -2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_RATCHET_PROBABILITY].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_ARP_RATCHET_PROBABILITY), -2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_RATCHET_AMOUNT].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_ARP_RATCHET_AMOUNT), -2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_SEQUENCE_LENGTH].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_ARP_SEQUENCE_LENGTH), -2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_CHORD_POLYPHONY].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_ARP_CHORD_POLYPHONY), -2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_RHYTHM].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_ARP_RHYTHM), -2147483648);
	unpatchedParams->params[params::UNPATCHED_SPREAD_VELOCITY].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_SPREAD_VELOCITY), -2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_SPREAD_GATE].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_ARP_SPREAD_GATE), -2147483648);
	unpatchedParams->params[params::UNPATCHED_ARP_SPREAD_OCTAVE].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_ARP_SPREAD_OCTAVE), -2147483648);

	Stutterer::initParams(paramManager);

	unpatchedParams->params[params::UNPATCHED_MOD_FX_OFFSET].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_MOD_FX_OFFSET), 0);
	unpatchedParams->params[params::UNPATCHED_MOD_FX_FEEDBACK].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_MOD_FX_FEEDBACK), 0);

	unpatchedParams->params[params::UNPATCHED_SAMPLE_RATE_REDUCTION].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_SAMPLE_RATE_REDUCTION), -2147483648);

	unpatchedParams->params[params::UNPATCHED_BITCRUSHING].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_BITCRUSHING), -2147483648);

	unpatchedParams->params[params::UNPATCHED_SIDECHAIN_SHAPE].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_SIDECHAIN_SHAPE), -601295438);
	unpatchedParams->params[params::UNPATCHED_COMPRESSOR_THRESHOLD].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_COMPRESSOR_THRESHOLD), 0);
}

void ModControllableAudio::processFX(std::span<StereoSample> buffer, ModFXType modFXType, int32_t modFXRate,
                                     int32_t modFXDepth, const Delay::State& delayWorkingState, int32_t* postFXVolume,
                                     ParamManager* paramManager, bool anySoundComingIn, q31_t reverbSendAmount) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	// Mod FX -----------------------------------------------------------------------------------
	if (modFXType == ModFXType::GRAIN) {
		processGrainFX(buffer, modFXRate, modFXDepth, postFXVolume, unpatchedParams, anySoundComingIn,
		               reverbSendAmount);
	}
	else {
		modfx.processModFX(buffer, modFXType, modFXRate, modFXDepth, postFXVolume, unpatchedParams, anySoundComingIn);
	}

	// EQ -------------------------------------------------------------------------------------
	equalizer_.process(buffer, {.bass = unpatchedParams->getValue(params::UNPATCHED_BASS),
	                            .treble = unpatchedParams->getValue(params::UNPATCHED_TREBLE),
	                            .bass_frequency = unpatchedParams->getValue(params::UNPATCHED_BASS_FREQ),
	                            .treble_frequency = unpatchedParams->getValue(params::UNPATCHED_TREBLE_FREQ)});

	// Delay ----------------------------------------------------------------------------------
	delay.process(buffer, delayWorkingState, {reinterpret_cast<StereoSample*>(spareRenderingBuffer[0]), buffer.size()},
	              AudioEngine::renderInStereo);
}
void ModControllableAudio::processGrainFX(std::span<StereoSample> buffer, int32_t modFXRate, int32_t modFXDepth,
                                          int32_t* postFXVolume, UnpatchedParamSet* unpatchedParams,
                                          bool anySoundComingIn, q31_t verbAmount) {
	// this shouldn't be possible but just in case
	if (anySoundComingIn && !grainFX) [[unlikely]] {
		enableGrain();
	}

	if (grainFX) {
		int32_t reverbSendAmountAndPostFXVolume = multiply_32x32_rshift32(*postFXVolume, verbAmount) << 5;
		grainFX->processGrainFX(buffer, modFXRate, modFXDepth,
		                        unpatchedParams->getValue(params::UNPATCHED_MOD_FX_OFFSET),
		                        unpatchedParams->getValue(params::UNPATCHED_MOD_FX_FEEDBACK), postFXVolume,
		                        anySoundComingIn, currentSong->calculateBPM(), reverbSendAmountAndPostFXVolume);
	}
}

void ModControllableAudio::processReverbSendAndVolume(std::span<StereoSample> buffer, int32_t* reverbBuffer,
                                                      int32_t postFXVolume, int32_t postReverbVolume,
                                                      int32_t reverbSendAmount, int32_t pan,
                                                      bool doAmplitudeIncrement) {
	if (sendAndVolume_.process(buffer, {reverbBuffer, buffer.size()},
	                           {.post_fx_volume = postFXVolume,
	                            .post_reverb_volume = postReverbVolume,
	                            .reverb_send = reverbSendAmount,
	                            .pan = pan,
	                            .ramp = doAmplitudeIncrement,
	                            .stereo = AudioEngine::renderInStereo})) {
		AudioEngine::timeThereWasLastSomeReverb = AudioEngine::audioSampleTimer;
	}
}

bool ModControllableAudio::isBitcrushingEnabled(ParamManager* paramManager) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	return (unpatchedParams->getValue(params::UNPATCHED_BITCRUSHING) >= -2113929216);
}

bool ModControllableAudio::isSRREnabled(ParamManager* paramManager) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	return (unpatchedParams->getValue(params::UNPATCHED_SAMPLE_RATE_REDUCTION) != -2147483648);
}

void ModControllableAudio::processSRRAndBitcrushing(std::span<StereoSample> buffer, int32_t* postFXVolume,
                                                    ParamManager* paramManager) {
	UnpatchedParamSet* unpatched = paramManager->getUnpatchedParamSet();
	bitcrush_.process(buffer,
	                  {.bitcrush = unpatched->getValue(params::UNPATCHED_BITCRUSHING),
	                   .sample_rate_reduction = unpatched->getValue(params::UNPATCHED_SAMPLE_RATE_REDUCTION)},
	                  *postFXVolume);
}

void ModControllableAudio::writeAttributesToFile(Serializer& writer) {
	writer.writeValue(deluge::effects::encode_pieces(effects(), effectsCodecContext()).attributes);
}

void ModControllableAudio::setEffects(const deluge::effects::EffectStates& next) {
	if (boundEffects_ != nullptr) {
		*boundEffects_ = next;
	}
	else {
		ownEffects_ = next;
	}
	projectModControllableSnapshot();
}

void ModControllableAudio::bindEffects(deluge::effects::EffectStates* record) {
	if (record != nullptr) {
		*record = effects();
	}
	else if (boundEffects_ != nullptr) {
		ownEffects_ = *boundEffects_;
	}
	boundEffects_ = record;
	projectModControllableSnapshot();
}

void ModControllableAudio::projectModControllableSnapshot() {
	AudioExcludedSection section; // the render reads the copy
	modControllableSnapshot_.effects = effects();
}

namespace {
/// @p mode as a filter mode setting's Edit form: its file name.
deluge::serialization::Value effectWord(FilterMode mode) {
	return deluge::serialization::Value::makeScalar(std::string(deluge::effects::to_string(mode)));
}

/// @p owner's effect settings with @p change made to them, made its settings.
template <class Change>
void changeEffects(ModControllableAudio& owner, Change change) {
	deluge::effects::EffectStates next = owner.effects();
	change(next);
	owner.setEffects(next);
}
} // namespace

void ModControllableAudio::assignModFXType(ModFXType value) {
	changeEffects(*this, [&](auto& e) { e.mod_fx.type = value; });
}
void ModControllableAudio::setClippingAmount(uint8_t value) {
	changeEffects(*this, [&](auto& e) { e.saturation.clipping_amount = value; });
}
void ModControllableAudio::setLpfMode(FilterMode value) {
	changeEffects(*this, [&](auto& e) { e.filter.lpf_mode = value; });
}
void ModControllableAudio::setHpfMode(FilterMode value) {
	changeEffects(*this, [&](auto& e) { e.filter.hpf_mode = value; });
}
void ModControllableAudio::setFilterRoute(FilterRoute value) {
	changeEffects(*this, [&](auto& e) { e.filter.route = value; });
}
void ModControllableAudio::setDelayPingPong(bool value) {
	changeEffects(*this, [&](auto& e) { e.delay.ping_pong = value; });
}
void ModControllableAudio::setDelayAnalog(bool value) {
	changeEffects(*this, [&](auto& e) { e.delay.analog = value; });
}
void ModControllableAudio::setDelaySyncType(SyncType value) {
	changeEffects(*this, [&](auto& e) { e.delay.sync_type = value; });
}
void ModControllableAudio::setDelaySyncLevel(SyncLevel value) {
	changeEffects(*this, [&](auto& e) { e.delay.sync_level = value; });
}
void ModControllableAudio::setStutterQuantized(bool value) {
	changeEffects(*this, [&](auto& e) { e.stutter.quantized = value; });
}
void ModControllableAudio::setStutterReversed(bool value) {
	changeEffects(*this, [&](auto& e) { e.stutter.reversed = value; });
}
void ModControllableAudio::setStutterPingPong(bool value) {
	changeEffects(*this, [&](auto& e) { e.stutter.ping_pong = value; });
}
void ModControllableAudio::setUseSongStutter(bool value) {
	changeEffects(*this, [&](auto& e) { e.stutter.use_song_stutter = value; });
}
void ModControllableAudio::setSidechainAttack(int32_t value) {
	changeEffects(*this, [&](auto& e) { e.sidechain.attack = value; });
}
void ModControllableAudio::setSidechainRelease(int32_t value) {
	changeEffects(*this, [&](auto& e) { e.sidechain.release = value; });
}
void ModControllableAudio::setSidechainSyncType(SyncType value) {
	changeEffects(*this, [&](auto& e) { e.sidechain.sync_type = value; });
}
void ModControllableAudio::setSidechainSyncLevel(SyncLevel value) {
	changeEffects(*this, [&](auto& e) { e.sidechain.sync_level = value; });
}
void ModControllableAudio::setCompressorAttack(q31_t value) {
	changeEffects(*this, [&](auto& e) { e.compressor.attack = value; });
}
void ModControllableAudio::setCompressorRelease(q31_t value) {
	changeEffects(*this, [&](auto& e) { e.compressor.release = value; });
}
void ModControllableAudio::setCompressorThreshold(q31_t value) {
	changeEffects(*this, [&](auto& e) { e.compressor.threshold = value; });
}
void ModControllableAudio::setCompressorRatio(q31_t value) {
	changeEffects(*this, [&](auto& e) { e.compressor.ratio = value; });
}
void ModControllableAudio::setCompressorSidechainHpf(q31_t value) {
	changeEffects(*this, [&](auto& e) { e.compressor.sidechain_hpf = value; });
}
void ModControllableAudio::setCompressorBlend(q31_t value) {
	changeEffects(*this, [&](auto& e) { e.compressor.blend = value; });
}

void ModControllableAudio::writeTagsToFile(Serializer& writer) {
	const deluge::effects::EffectPieces pieces =
	    deluge::effects::encode_pieces(effects(), effectsCodecContext(), savedCompressorThreshold());
	writer.writeValue(pieces.delay);
	writeMidiKnobsToFile(writer);
	writer.writeValue(pieces.tags);
}

deluge::engine::CodecContext ModControllableAudio::effectsCodecContext() {
	return {.input_tick_magnitude = currentSong->getInputTickMagnitude()};
}

void ModControllableAudio::writeMidiKnobsToFile(Serializer& writer) {
	// MIDI knobs
	if (midi_knobs.size() > 0) {
		writer.writeArrayStart("midiKnobs");
		for (MIDIKnob& knob : midi_knobs) {
			writer.writeOpeningTagBeginning("midiKnob", true);
			knob.midiInput.writeAttributesToFile(
			    writer,
			    MIDI_MESSAGE_CC); // Writes channel and CC, but not device - we do that below.
			writer.writeAttribute("relative", knob.relative);
			writer.writeAttribute(
			    "controlsParam", params::paramNameForFile(unpatchedParamKind_, knob.paramDescriptor.getJustTheParam()));
			if (!knob.paramDescriptor.isJustAParam()) { // TODO: this only applies to Sounds
				writer.writeAttribute("patchAmountFromSource",
				                      sourceToString(knob.paramDescriptor.getTopLevelSource()));

				if (knob.paramDescriptor.hasSecondSource()) {
					writer.writeAttribute("patchAmountFromSecondSource",
					                      sourceToString(knob.paramDescriptor.getSecondSourceFromTop()));
				}
			}

			// Because we manually called LearnedMIDI::writeAttributesToFile() above, we have to give the MIDIDevice its
			// own tag, cos that can't be written as just an attribute.
			if (knob.midiInput.cable != nullptr) {
				writer.writeOpeningTagEnd();
				knob.midiInput.cable->writeReferenceToFile(writer);
				writer.writeClosingTag("midiKnob", true, true);
			}
			else {
				writer.closeTag(true); // the knob is a boxed list item
			}
		}
		writer.writeArrayEnding("midiKnobs");
	}
}

void ModControllableAudio::writeParamAttributesToFile(Serializer& writer, ParamManager* paramManager,
                                                      bool writeAutomation, int32_t* valuesForOverride) {
	// Serialize from the value DOM (deluge::project). Byte-identical to the former hand-written sequence
	// (param_automation.cpp, verified in the param-projection driver), the first production use of the param DOM.
	deluge::project::DocParamSet unpatched = deluge::bridge::toDocParamSet(*paramManager->getUnpatchedParamSet());
	deluge::bridge::serializeModControllableParamAttributes(unpatched, writer, writeAutomation, valuesForOverride);
}

void ModControllableAudio::writeParamTagsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
                                                int32_t* valuesForOverride) {
	deluge::project::DocParamSet unpatched = deluge::bridge::toDocParamSet(*paramManager->getUnpatchedParamSet());
	deluge::bridge::serializeModControllableParamTags(unpatched, writer, writeAutomation, valuesForOverride);
}

bool ModControllableAudio::readParamTagFromFile(Deserializer& reader, char const* tagName,
                                                ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos) {
	// Read the shared param block from the value DOM (deluge::project). The MCA block has no legacy-only
	// tag names, so this fully replaces the former hand-written dispatch; verified by the golden round-trip
	// and the param-projection driver.
	ParamCollectionSummary* unpatchedParamsSummary = paramManager->getUnpatchedParamSetSummary();
	UnpatchedParamSet* unpatchedParams = (UnpatchedParamSet*)unpatchedParamsSummary->paramCollection;
	return deluge::bridge::readModControllableParamTag(reader, tagName, *unpatchedParams, *unpatchedParamsSummary,
	                                                   readAutomationUpToPos);
}

// paramManager is optional
Error ModControllableAudio::readTagFromFile(Deserializer& reader, char const* tagName,
                                            ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos,
                                            ArpeggiatorSettings* arpSettings, Song* song) {

	// An owner's effects are read from its element (the effects library's codec), not here.
	if (!strcmp(tagName, "arpeggiator") && arpSettings != nullptr) {
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
		reader.match('[');
		// Standard array-of-boxes idiom (mirrors the noteRows/clips loops): match('{') enters the box, the
		// name is read, then the value object is entered. The previous readNext-before-match('{') ordering
		// relied on XML implicit descent and read nothing in JSON mode. match('{')/match('}') are no-ops in XML.
		while (reader.match('{') && *(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "midiKnob")) {

				MIDICable* cable = nullptr;
				uint8_t channel = 0;
				uint8_t ccNumber = 0;
				bool relative = false;
				uint8_t p = params::GLOBAL_NONE;
				PatchSource s = PatchSource::NOT_AVAILABLE;
				PatchSource s2 = PatchSource::NOT_AVAILABLE;

				reader.match('{'); // enter the midiKnob value object
				while (*(tagName = reader.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "device")) {
						cable = MIDIDeviceManager::readDeviceReferenceFromFile(reader);
					}
					else if (!strcmp(tagName, "channel")) {
						channel = reader.readTagOrAttributeValueInt();
					}
					else if (!strcmp(tagName, "mpeZone")) { // a knob learned on an MPE zone
						const std::string_view zone = reader.readTagOrAttributeValue();
						if (zone == "lower") {
							channel = MIDI_CHANNEL_MPE_LOWER_ZONE;
						}
						else if (zone == "upper") {
							channel = MIDI_CHANNEL_MPE_UPPER_ZONE;
						}
					}
					else if (!strcmp(tagName, "ccNumber")) {
						ccNumber = reader.readTagOrAttributeValueInt();
					}
					else if (!strcmp(tagName, "relative")) {
						relative = reader.readTagOrAttributeValueInt();
					}
					else if (!strcmp(tagName, "controlsParam")) {
						// if the unpatched kind for the current mod controllable is sound then we also want to check
						// against patched params. Otherwise skip them to avoid a bug from patched volume params having
						// the same name in files as unpatched global volumes
						p = params::fileStringToParam(unpatchedParamKind_, reader.readTagOrAttributeValue(),
						                              unpatchedParamKind_
						                                  == deluge::modulation::params::Kind::UNPATCHED_SOUND);
					}
					else if (!strcmp(tagName, "patchAmountFromSource")) {
						s = stringToSource(reader.readTagOrAttributeValue());
					}
					else if (!strcmp(tagName, "patchAmountFromSecondSource")) {
						s2 = stringToSource(reader.readTagOrAttributeValue());
					}
					reader.exitTag();
				}
				reader.match('}'); // close the midiKnob value object.

				if (p != params::GLOBAL_NONE && p != params::PLACEHOLDER_RANGE) {
					try {
						MIDIKnob& new_knob = midi_knobs.emplace_back();
						new_knob.midiInput.cable = cable;
						new_knob.midiInput.channelOrZone = channel;
						new_knob.midiInput.noteOrCC = ccNumber;
						new_knob.relative = relative;

						if (s == PatchSource::NOT_AVAILABLE) {
							new_knob.paramDescriptor.setToHaveParamOnly(p);
						}
						else if (s2 == PatchSource::NOT_AVAILABLE) {
							new_knob.paramDescriptor.setToHaveParamAndSource(p, s);
						}
						else {
							new_knob.paramDescriptor.setToHaveParamAndTwoSources(p, s, s2);
						}
					} catch (deluge::exception e) {
						if (e == deluge::exception::BAD_ALLOC) {
							// If we run out of memory, we just ignore the knob
							// TODO(@stellar-aria): This is bad practice, we should handle this better, but it
							//  currently follows the previous implementation
						}
						else {
							throw e;
						}
					}
				}
			}
			reader.match('}'); // close the box.
			reader.exitTag(nullptr, true);
		}
		reader.match(']'); // close array.
		reader.exitTag("midiKnobs");
	}

	else {
		return Error::RESULT_TAG_UNUSED;
	}

	return Error::NONE;
}

ModelStackWithAutoParam* ModControllableAudio::getParamFromMIDIKnob(MIDIKnob& knob,
                                                                    ModelStackWithThreeMainThings* modelStack) {

	ParamCollectionSummary* summary = modelStack->paramManager->getUnpatchedParamSetSummary();
	ParamCollection* paramCollection = summary->paramCollection;

	int32_t param_id = knob.paramDescriptor.getJustTheParam() - params::UNPATCHED_START;

	ModelStackWithParamId* modelStackWithParamId =
	    modelStack->addParamCollectionAndId(paramCollection, summary, param_id);

	ModelStackWithAutoParam* modelStackWithAutoParam = paramCollection->getAutoParamFromId(modelStackWithParamId);

	return modelStackWithAutoParam;
}

ModelStackWithThreeMainThings* ModControllableAudio::addNoteRowIndexAndStuff(ModelStackWithTimelineCounter* modelStack,
                                                                             int32_t noteRowIndex) {
	NoteRow* noteRow = nullptr;
	int32_t noteRowId = 0;
	ParamManager* paramManager;

	if (noteRowIndex != -1) {
		InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();
#if ALPHA_OR_BETA_VERSION
		if (noteRowIndex >= std::ssize(clip->noteRows)) {
			FREEZE_WITH_ERROR("E406");
		}
#endif
		noteRow = &clip->noteRows[noteRowIndex];
		noteRowId = clip->getNoteRowId(noteRow, noteRowIndex);
		paramManager = &noteRow->paramManager;
	}
	else {
		if (modelStack->timelineCounterIsSet()) {
			paramManager = &modelStack->getTimelineCounter()->paramManager;
		}
		else {
			paramManager = modelStack->song->live.param_backup.getPreferablyWithClip(
			    this,
			    nullptr); // Could be NULL if a NonAudioInstrument - those don't back up any paramManagers (when they
			              // even have them).
		}
	}

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addNoteRow(noteRowId, noteRow)->addOtherTwoThings(this, paramManager);
	return modelStackWithThreeMainThings;
}

bool ModControllableAudio::offerReceivedCCToLearnedParamsForClip(MIDICable& cable, uint8_t channel, uint8_t ccNumber,
                                                                 uint8_t value,
                                                                 ModelStackWithTimelineCounter* modelStack,
                                                                 int32_t noteRowIndex) {
	bool messageUsed = false;

	// For each MIDI knob...
	for (MIDIKnob& knob : midi_knobs) {

		// If this is the knob...
		if (knob.midiInput.equalsNoteOrCC(&cable, channel, ccNumber)) {

			messageUsed = true;

			// See if this message is evidence that the knob is not "relative"
			if (value >= 16 && value < 112) {
				knob.relative = false;
			}

			int32_t modPos = 0;
			int32_t modLength = 0;
			bool isStepEditing = false;

			if (modelStack->timelineCounterIsSet()) {
				TimelineCounter* timelineCounter = modelStack->getTimelineCounter();

				// Only if this exact TimelineCounter is having automation step-edited, we can set the value for just a
				// region.
				if (view.modLength
				    && timelineCounter == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {
					modPos = view.modPos;
					modLength = view.modLength;
					isStepEditing = true;
				}

				timelineCounter->possiblyCloneForArrangementRecording(modelStack);
			}

			// Ok, that above might have just changed modelStack->timelineCounter. So we're basically starting from
			// scratch now from that.
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    addNoteRowIndexAndStuff(modelStack, noteRowIndex);

			ModelStackWithAutoParam* modelStackWithParam = getParamFromMIDIKnob(knob, modelStackWithThreeMainThings);

			if (modelStackWithParam && modelStackWithParam->autoParam) {
				int32_t newKnobPos;
				int32_t currentValue;

				// get current value
				if (isStepEditing) {
					currentValue = modelStackWithParam->autoParam->getValuePossiblyAtPos(modPos, modelStackWithParam);
				}
				else {
					currentValue = modelStackWithParam->autoParam->getCurrentValue();
				}

				// convert current value to knobPos to compare to cc value being received
				int32_t knobPos =
				    modelStackWithParam->paramCollection->paramValueToKnobPos(currentValue, modelStackWithParam);

				// calculate new knob position based on value received and deluge current value
				newKnobPos =
				    MidiTakeover::calculateKnobPos(cable, knobPos, value, &knob, false, CC_NUMBER_NONE, isStepEditing);

				// is the cc being received for the same value as the current knob pos? If so, do nothing
				if (newKnobPos == knobPos) {
					continue;
				}

				// Convert the New Knob Position to a Parameter Value
				int32_t newValue =
				    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

				// Set the new Parameter Value for the MIDI Learned Parameter
				deluge::edits::Context editCtx{*currentSong, currentSong->project};
				deluge::edits::SetParamValue edit{deluge::edits::addressFor(*modelStackWithParam), newValue, modPos,
				                                  modLength, modelStackWithParam};
				deluge::edits::apply(editCtx, edit);

				// if you're in automation view and editing the same parameter that was just updated
				// by a learned midi knob, then re-render the pads on the automation editor grid
				if (getRootUI() == &automationView && !automationView.onArrangerView) {
					Clip* clip = (Clip*)modelStack->getTimelineCounter();
					// check that the clip that the param is being edited for is the same as the
					// current clip as the current clip is what's actively displayed in automation view
					if (clip == getCurrentClip()) {
						// pass the current clip because you want to check that you're editing the param
						// for the same clip active in automation view
						int32_t id = modelStackWithParam->paramId;
						params::Kind kind = modelStackWithParam->paramCollection->getParamKind();
						automationView.possiblyRefreshAutomationEditorGrid(clip, kind, id);
					}
				}
			}
		}
	}
	return messageUsed;
}

bool ModControllableAudio::offerReceivedCCToLearnedParamsForSong(
    MIDICable& cable, uint8_t channel, uint8_t ccNumber, uint8_t value,
    ModelStackWithThreeMainThings* modelStackWithThreeMainThings) {
	bool messageUsed = false;

	// For each MIDI knob...
	for (MIDIKnob& knob : midi_knobs) {

		// If this is the knob...
		if (knob.midiInput.equalsNoteOrCC(&cable, channel, ccNumber)) {

			messageUsed = true;

			// See if this message is evidence that the knob is not "relative"
			if (value >= 16 && value < 112) {
				knob.relative = false;
			}

			int32_t modPos = 0;
			int32_t modLength = 0;
			bool isStepEditing = false;

			if (modelStackWithThreeMainThings->timelineCounterIsSet()) {
				TimelineCounter* timelineCounter = modelStackWithThreeMainThings->getTimelineCounter();

				// Only if this exact TimelineCounter is having automation step-edited, we can set the value for just a
				// region.
				if (view.modLength
				    && timelineCounter == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {
					modPos = view.modPos;
					modLength = view.modLength;
					isStepEditing = true;
				}
			}

			ModelStackWithAutoParam* modelStackWithParam = getParamFromMIDIKnob(knob, modelStackWithThreeMainThings);

			if (modelStackWithParam && modelStackWithParam->autoParam) {
				int32_t newKnobPos;
				int32_t currentValue;

				// get current value
				if (isStepEditing) {
					currentValue = modelStackWithParam->autoParam->getValuePossiblyAtPos(modPos, modelStackWithParam);
				}
				else {
					currentValue = modelStackWithParam->autoParam->getCurrentValue();
				}

				// convert current value to knobPos to compare to cc value being received
				int32_t knobPos =
				    modelStackWithParam->paramCollection->paramValueToKnobPos(currentValue, modelStackWithParam);

				// calculate new knob position based on value received and deluge current value
				newKnobPos =
				    MidiTakeover::calculateKnobPos(cable, knobPos, value, &knob, false, CC_NUMBER_NONE, isStepEditing);

				// is the cc being received for the same value as the current knob pos? If so, do nothing
				if (newKnobPos == knobPos) {
					continue;
				}

				// Convert the New Knob Position to a Parameter Value
				int32_t newValue =
				    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

				// Set the new Parameter Value for the MIDI Learned Parameter
				deluge::edits::Context editCtx{*currentSong, currentSong->project};
				deluge::edits::SetParamValue edit{deluge::edits::addressFor(*modelStackWithParam), newValue, modPos,
				                                  modLength, modelStackWithParam};
				deluge::edits::apply(editCtx, edit);

				// check if you're currently editing the same learned param in automation view or
				// performance view if so, you will need to refresh the automation editor grid or the
				// performance view
				RootUI* rootUI = getRootUI();
				if (rootUI == &automationView || rootUI == &performanceView) {
					int32_t id = modelStackWithParam->paramId;
					params::Kind kind = modelStackWithParam->paramCollection->getParamKind();

					if (rootUI == &automationView) {
						automationView.possiblyRefreshAutomationEditorGrid(nullptr, kind, id);
					}
					else {
						performanceView.possiblyRefreshPerformanceViewDisplay(kind, id, newKnobPos);
					}
				}
			}
		}
	}
	return messageUsed;
}

// Returns true if the message was used by something
bool ModControllableAudio::offerReceivedPitchBendToLearnedParams(MIDICable& cable, uint8_t channel, uint8_t data1,
                                                                 uint8_t data2,
                                                                 ModelStackWithTimelineCounter* modelStack,
                                                                 int32_t noteRowIndex) {

	bool messageUsed = false;

	// For each MIDI knob...
	for (MIDIKnob& knob : midi_knobs) {

		// If this is the knob...
		if (knob.midiInput.equalsNoteOrCC(&cable, channel,
		                                  128)) { // I've got 128 representing pitch bend here... why again?

			messageUsed = true;

			// Only if this exact TimelineCounter is having automation step-edited, we can set the value for just a
			// region.
			int32_t modPos = 0;
			int32_t modLength = 0;

			if (modelStack->timelineCounterIsSet()) {
				TimelineCounter* timelineCounter = modelStack->getTimelineCounter();
				if (view.modLength
				    && timelineCounter == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {
					modPos = view.modPos;
					modLength = view.modLength;
				}

				timelineCounter->possiblyCloneForArrangementRecording(modelStack);
			}

			// Ok, that above might have just changed modelStack->timelineCounter. So we're basically starting from
			// scratch now from that.
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    addNoteRowIndexAndStuff(modelStack, noteRowIndex);

			ModelStackWithAutoParam* modelStackWithParam = getParamFromMIDIKnob(knob, modelStackWithThreeMainThings);

			if (modelStackWithParam->autoParam) {

				uint32_t value14 = (uint32_t)data1 | ((uint32_t)data2 << 7);

				int32_t newValue = (value14 << 18) - 2147483648;

				deluge::edits::Context editCtx{*currentSong, currentSong->project};
				deluge::edits::SetParamValue edit{deluge::edits::addressFor(*modelStackWithParam), newValue, modPos,
				                                  modLength, modelStackWithParam};
				deluge::edits::apply(editCtx, edit);
				return true;
			}
		}
	}
	return messageUsed;
}

const uint32_t stutterUIModes[] = {UI_MODE_CLIP_PRESSED_IN_SONG_VIEW, UI_MODE_HOLDING_ARRANGEMENT_ROW,
                                   UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION, UI_MODE_AUDITIONING, 0};

void ModControllableAudio::beginStutter(ParamManagerForTimeline* paramManager) {
	if (!isUIModeWithinRange(stutterUIModes)) {
		return;
	}
	if (Error::NONE
	    == stutterer.beginStutter(
	        this, paramManager,
	        effects().stutter.use_song_stutter ? currentSong->live.global_effectable.effects().stutter
	                                           : effects().stutter,
	        currentSong->getInputTickMagnitude(), playbackHandler.getTimePerInternalTickInverse())) {
		// Redraw the LEDs. Really only for quantized stutter, but doing it for unquantized won't hurt.
		view.notifyParamAutomationOccurred(paramManager);
		enterUIMode(UI_MODE_STUTTERING);
	}
}

void ModControllableAudio::processStutter(std::span<StereoSample> buffer, ParamManager* paramManager) {
	if (stutterer.isStuttering(this)) {
		stutterer.processStutter(buffer, paramManager, currentSong->getInputTickMagnitude(),
		                         playbackHandler.getTimePerInternalTickInverse());
	}
}

// paramManager is optional - if you don't send it, it won't restore the stutter rate and we won't redraw the LEDs
void ModControllableAudio::endStutter(ParamManagerForTimeline* paramManager) {
	stutterer.endStutter(paramManager);
	if (paramManager) {
		// Redraw the LEDs.
		view.notifyParamAutomationOccurred(paramManager);
	}
	exitUIMode(UI_MODE_STUTTERING);
}

void ModControllableAudio::switchDelayPingPong() {
	deluge::edits::changeEffect(*this, "delay/pingPong",
	                            deluge::serialization::Value::makeNumber(effects().delay.ping_pong ? 0 : 1));
}

void ModControllableAudio::switchDelayAnalog() {
	deluge::edits::changeEffect(*this, "delay/analog",
	                            deluge::serialization::Value::makeNumber(effects().delay.analog ? 0 : 1));
}

char const* ModControllableAudio::getDelayTypeDisplayName() {
	switch (effects().delay.analog) {
		using enum deluge::l10n::String;
	case 0:
		return l10n::get(STRING_FOR_DIGITAL_DELAY);
	default:
		return l10n::get(STRING_FOR_ANALOG_DELAY);
	}
}

void ModControllableAudio::switchDelaySyncType() {
	SyncType next = SYNC_TYPE_TRIPLET; // after even
	switch (effects().delay.sync_type) {
	case SYNC_TYPE_TRIPLET:
		next = SYNC_TYPE_DOTTED;
		break;
	case SYNC_TYPE_DOTTED:
		next = SYNC_TYPE_EVEN;
		break;
	default:
		break;
	}
	deluge::edits::changeEffect(*this, "delay/syncType",
	                            deluge::serialization::Value::makeNumber(util::to_underlying(next)));
}

char const* ModControllableAudio::getDelaySyncTypeDisplayName() {
	switch (effects().delay.sync_type) {
	case SYNC_TYPE_TRIPLET:
		return "Triplet";
	case SYNC_TYPE_DOTTED:
		return "Dotted";
	default: // SYNC_TYPE_EVEN
		return "Even";
	}
}

void ModControllableAudio::switchDelaySyncLevel() {
	// Note: SYNC_LEVEL_NONE (value 0) can't be selected
	deluge::edits::changeEffect(
	    *this, "delay/syncLevel",
	    deluge::serialization::Value::makeNumber(deluge::engine::index_of(effects().delay.sync_level)
	                                                 % deluge::engine::index_of(SYNC_LEVEL_256TH)
	                                             + 1)); // cycle from 1 to 9 (omit 0)
}

void ModControllableAudio::getDelaySyncLevelDisplayName(char* displayName) {
	// Note: SYNC_LEVEL_NONE (value 0) can't be selected
	deluge::edits::changeEffect(*this, "delay/syncLevel",
	                            deluge::serialization::Value::makeNumber(
	                                deluge::engine::index_of(effects().delay.sync_level)
	                                % deluge::engine::index_of(SYNC_LEVEL_256TH))); // cycle from 1 to 9 (omit 0)
	etl::string<kShortStringBufferSize> buffer;
	currentSong->getNoteLengthName(buffer, (uint32_t)3 << (deluge::engine::index_of(SYNC_LEVEL_256TH)
	                                                       - deluge::engine::index_of(effects().delay.sync_level)));
	strncpy(displayName, buffer.data(), 29);
}

char const* ModControllableAudio::getFilterTypeDisplayName(FilterType currentFilterType) {
	using enum deluge::l10n::String;
	switch (currentFilterType) {
	case FilterType::LPF:
		return l10n::get(STRING_FOR_LPF);
	case FilterType::HPF:
		return l10n::get(STRING_FOR_HPF);
	case FilterType::EQ:
		return l10n::get(STRING_FOR_EQ);
	default:
		return l10n::get(STRING_FOR_NONE);
	}
}

void ModControllableAudio::switchLPFMode() {
	deluge::edits::changeEffect(
	    *this, "lpfMode",
	    effectWord(static_cast<FilterMode>((util::to_underlying(effects().filter.lpf_mode) + 1) % kNumLPFModes)));
}

// for future use with FM
void ModControllableAudio::switchLPFModeWithOff() {
	FilterMode mode = static_cast<FilterMode>((util::to_underlying(effects().filter.lpf_mode) + 1) % kNumLPFModes);
	switch (mode) {
	case FilterMode::OFF:
		mode = FilterMode::TRANSISTOR_12DB;
		break;
	case lastLpfMode:
		mode = FilterMode::OFF;
		break;
	default:
		mode = static_cast<FilterMode>((util::to_underlying(mode) + 1));
	}
	deluge::edits::changeEffect(*this, "lpfMode", effectWord(mode));
}

char const* ModControllableAudio::getFilterModeDisplayName(FilterType currentFilterType) {
	switch (currentFilterType) {
	case FilterType::LPF:
		return getLPFModeDisplayName();
	case FilterType::HPF:
		return getHPFModeDisplayName();
	default:
		return l10n::get(deluge::l10n::String::STRING_FOR_NONE);
	}
}

char const* ModControllableAudio::getLPFModeDisplayName() {

	using enum deluge::l10n::String;
	switch (effects().filter.lpf_mode) {
	case FilterMode::TRANSISTOR_12DB:
		return l10n::get(STRING_FOR_12DB_LADDER);
	case FilterMode::TRANSISTOR_24DB:
		return l10n::get(STRING_FOR_24DB_LADDER);
	case FilterMode::TRANSISTOR_24DB_DRIVE:
		return l10n::get(STRING_FOR_DRIVE);
	case FilterMode::SVF_BAND:
		return l10n::get(STRING_FOR_SVF_BAND);
	case FilterMode::SVF_NOTCH:
		return l10n::get(STRING_FOR_SVF_NOTCH);
	default:
		return l10n::get(STRING_FOR_NONE);
	}
}

void ModControllableAudio::switchHPFMode() {
	// this works fine, the offset to the first hpf doesn't matter with the modulus
	deluge::edits::changeEffect(
	    *this, "hpfMode",
	    effectWord(static_cast<FilterMode>((util::to_underlying(effects().filter.hpf_mode) + 1) % kNumHPFModes
	                                       + kFirstHPFMode)));
}

// for future use with FM
void ModControllableAudio::switchHPFModeWithOff() {
	switch (effects().filter.hpf_mode) {
	case FilterMode::OFF:
		deluge::edits::changeEffect(*this, "hpfMode", effectWord(firstHPFMode));
		break;
	default:
		deluge::edits::changeEffect(
		    *this, "hpfMode",
		    effectWord(static_cast<FilterMode>((util::to_underlying(effects().filter.hpf_mode) + 1))));
	}
}

char const* ModControllableAudio::getHPFModeDisplayName() {
	using enum deluge::l10n::String;
	switch (effects().filter.hpf_mode) {
	case FilterMode::HPLADDER:
		return l10n::get(STRING_FOR_HPLADDER);
	case FilterMode::SVF_BAND:
		return l10n::get(STRING_FOR_SVF_BAND);
	case FilterMode::SVF_NOTCH:
		return l10n::get(STRING_FOR_SVF_NOTCH);
	default:
		return l10n::get(STRING_FOR_NONE);
	}
}

// This can get called either for hibernation, or because drum now has no active noteRow
void ModControllableAudio::wontBeRenderedForAWhile() {
	delay.discardBuffers();
	endStutter(nullptr);
}

void ModControllableAudio::clearModFXMemory() {
	if (modControllableSnapshot_.effects.mod_fx.type == ModFXType::GRAIN) {
		grainFX->clearGrainFXBuffer();
	}
	else if (modControllableSnapshot_.effects.mod_fx.type != ModFXType::NONE) {
		modfx.resetMemory();
	}
}

bool ModControllableAudio::setModFXType(ModFXType newType) {
	// For us ModControllableAudios, this is really simple. Memory gets allocated in
	// GlobalEffectable::processFXForGlobalEffectable(). This function is overridden in Sound
	assignModFXType(newType);
	return true;
}

// whichKnob is either which physical mod knob, or which MIDI CC code.
// For mod knobs, supply midiChannel as 255
// Returns false if fail due to insufficient RAM.
bool ModControllableAudio::learnKnob(MIDICable* cable, ParamDescriptor paramDescriptor, uint8_t whichKnob,
                                     uint8_t modKnobMode, uint8_t midiChannel, Song* song) {
	// If a mod knob
	if (midiChannel >= 16) {
		return false;
	}

	// Was there a MIDI knob already set to control this thing?
	auto result = std::ranges::find_if(midi_knobs, [&](const MIDIKnob& knob) -> bool {
		return knob.midiInput.equalsNoteOrCC(cable, midiChannel, whichKnob) && paramDescriptor == knob.paramDescriptor;
	});

	try {
		MIDIKnob& knob = result != midi_knobs.end()
		                     ? *result                    // If it already exists, use that one
		                     : midi_knobs.emplace_back(); // If it doesn't exist, create a new one
		knob.midiInput.noteOrCC = whichKnob;
		knob.midiInput.channelOrZone = midiChannel;
		knob.midiInput.cable = cable;
		knob.paramDescriptor = paramDescriptor;
		knob.relative = (whichKnob != 128); // Guess that it's relative, unless this is a pitch-bend "knob"
		return true;

	} catch (...) {
		return false;
	}
}

// Returns whether anything was found to unlearn
bool ModControllableAudio::unlearnKnobs(ParamDescriptor paramDescriptor, Song* song) {
	// I've deactivated the unlearning of mod knobs, mainly because, if you want to unlearn a MIDI knob, you might not
	// want to also deactivate a mod knob to the same param at the same time
	size_t erased = std::erase_if(midi_knobs, [&](const MIDIKnob& knob) -> bool {
		return knob.paramDescriptor == paramDescriptor; //<
	});

	if (erased > 0) {
		ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(song);
	}

	return erased > 0;
}

void ModControllableAudio::displayFilterSettings(bool on, FilterType currentFilterType) {
	if (on) {
		etl::string<40> popupMsg;
		popupMsg.append(getFilterTypeDisplayName(currentFilterType));
		if (currentFilterType != FilterType::EQ) {
			popupMsg.append("\n");
			popupMsg.append(getFilterModeDisplayName(currentFilterType));
		}
		display->popupText(popupMsg.c_str());
	}
	else {
		display->cancelPopup();
	}
}

void ModControllableAudio::displayDelaySettings(bool on) {
	if (on) {
		etl::string<100> popupMsg;
		if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::AltGoldenKnobDelayParams)
		    == RuntimeFeatureStateToggle::On) {
			popupMsg.append("Type: ");
			popupMsg.append(getDelaySyncTypeDisplayName());

			popupMsg.append("\nSync: ");
			char displayName[30];
			getDelaySyncLevelDisplayName(displayName);
			popupMsg.append(displayName);
		}
		else {
			popupMsg.append("Ping pong: ");
			popupMsg.append(getDelayPingPongStatusDisplayName());
			popupMsg.append("\n");
			popupMsg.append(getDelayTypeDisplayName());
		}

		display->popupText(popupMsg.c_str());
	}
	else {
		display->cancelPopup();
	}
}

char const* ModControllableAudio::getDelayPingPongStatusDisplayName() {
	using enum deluge::l10n::String;
	switch (effects().delay.ping_pong) {
	case 0:
		return l10n::get(STRING_FOR_DISABLED);
	default:
		return l10n::get(STRING_FOR_ENABLED);
	}
}

void ModControllableAudio::displaySidechainAndReverbSettings(bool on) {
	// Sidechain
	if (on) {
		etl::string<100> popupMsg;
		// Sidechain
		popupMsg.append("Sidechain: ");
		popupMsg.append(getSidechainDisplayName());

		popupMsg.append("\n");

		// Reverb
		popupMsg.append(view.getReverbPresetDisplayName(view.getCurrentReverbPreset()));

		display->popupText(popupMsg.c_str());
	}
	else {
		display->cancelPopup();
	}
}

char const* ModControllableAudio::getSidechainDisplayName() {
	int32_t insideWorldTickMagnitude;
	if (currentSong) { // Bit of a hack just referring to currentSong in here...
		insideWorldTickMagnitude = (currentSong->project.transport.inside_world_tick_magnitude
		                            + currentSong->project.transport.inside_world_tick_magnitude_offset_from_bpm);
	}
	else {
		insideWorldTickMagnitude = FlashStorage::defaultMagnitude;
	}
	using enum deluge::l10n::String;
	if (effects().sidechain.sync_level == (SyncLevel)(7 - insideWorldTickMagnitude)) {
		return l10n::get(STRING_FOR_SLOW);
	}
	else {
		return l10n::get(STRING_FOR_FAST);
	}
}

/// displays names of parameters assigned to gold knobs
void ModControllableAudio::displayOtherModKnobSettings(uint8_t whichModButton, bool on) {
	// the code below handles displaying parameter names on OLED and 7SEG

	/* logic for OLED:
	- it will display parameter names when you are holding down the mod button
	- it will display the parameter name assigned to the top and bottom gold knob
	- e.g. Volume
	       Pan
	*/

	/* logic for 7SEG:
	- while holding down mod button: display parameter assigned to top gold knob
	- after mod button is released: display parameter assigned to bottom gold knob
	*/

	etl::string<100> popupMsg;
	// if we have an OLED display
	// or a 7SEG display and mod button is pressed
	// then we will display the top gold knob parameter
	{
		char parameterName[30];
		view.getParameterNameFromModEncoder(1, parameterName);
		popupMsg.append(parameterName);
	}
	// in the song context,
	// the bottom knob for modButton 6 (stutter) doesn't have a parameter
	if (!((whichModButton == 6) && (!view.isClipContext()))) {
		// if we have an OLED display, we want to add a new line so we can
		// display the bottom gold knob param below the top gold knob param
		popupMsg.append("\n");
		// if we have an OLED display
		// or a 7SEG display and mod button is released
		// then we will display the bottom gold knob parameter
		// on OLED bottom gold knob param is rendered below the top gold knob param
		{
			char parameterName[30];
			view.getParameterNameFromModEncoder(0, parameterName);
			popupMsg.append(parameterName);
		}
	}
	// if we have OLED, a pop up is shown while we are holding down mod button
	// pop-up is removed after we release the mod button
	if (on) {
		display->popupText(popupMsg.c_str());
	}
	else {
		display->cancelPopup();
	}
}
bool ModControllableAudio::enableGrain() {

	if (grainFX == nullptr) {
		// Plain SDRAM allocation via the typed-allocator surface (NOT allocStealable,
		// and not GeneralMemoryAllocator::get()): GranularProcessor is not a Stealable
		// (only its GrainBuffer member is), so marking its block stealable was a latent
		// hazard — the neighbour-grab could cast it to Stealable* and call steal() on a
		// non-Stealable. (Strangle taxonomy cleanup, step 1; clean-API call site.)
		void* grainMemory = deluge::memory::alloc_external(sizeof(GranularProcessor), alignof(GranularProcessor));
		if (grainMemory) {
			grainFX = new (grainMemory) GranularProcessor;
			return true;
		}
	}
	else {
		grainFX->clearGrainFXBuffer();
		return false;
	}
	return false;
}
void ModControllableAudio::disableGrain() {
	// grainFX is lazily allocated (null until GRAIN mod-FX is first used), but this is called whenever
	// mod-FX is *not* grain — so null is the common case. Guard it like every other grainFX access.
	if (grainFX) {
		grainFX->startSkippingRendering();
	}
}
