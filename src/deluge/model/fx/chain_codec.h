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

#pragma once

#include "definitions_cxx.hpp" // Error
#include "model/fx/effects_owner.h"
#include "project/controller_mapping.h"
#include "serialization/value.h"

#include <cstdint>
#include <optional>

class ArpeggiatorSettings;
class Deserializer;
class ParamManager;
class ParamManagerForTimeline;
class Serializer;
class Song;

/// @file
/// @brief An effects owner's part of its file: a host chain's view words, the effects' attributes and tags, its learned
/// MIDI knobs, a host chain's param block, and the reads of them. Each owner's codec calls these where its file holds
/// them (the kit's, the audio track's, the song's and the sound's).

namespace deluge::model {

/// @brief A host chain's attributes: its view words (which mod FX value and filter the knobs edit), then the effects'.
/// @param writer Destination.
/// @param owner  The host chain's owner (owner.view set).
void writeChainAttributes(Serializer& writer, EffectsOwner owner);

/// @brief An owner's effect tags: the `defaultParams` block (a host chain's, when @p paramManager), then the delay,
/// the learned MIDI knobs and the other effect tags.
/// @param writer          Destination.
/// @param owner           The owner.
/// @param paramManager    A host chain's params to write, or nullptr for none.
/// @param writeAutomation Whether the params' automation is written.
/// @param knobsSong       The song whose table the learned knobs are written from, or nullptr.
/// @param knobsOwner      Whose controls these are, if known.
void writeEffectTags(Serializer& writer, EffectsOwner owner, ParamManager* paramManager, bool writeAutomation,
                     ::Song* knobsSong, std::optional<project::ControlsAddress> knobsOwner);

/// @brief The learned MIDI knobs' tag, which a file writes between the delay and the other effect tags: those
/// @p song's table holds for @p address, in table order, when it holds @p owner there; else those @p owner holds
/// outside a table; nothing when there are none.
/// @param writer  Destination.
/// @param owner   The knobs' owner.
/// @param song    The song being saved, or the one a preset is saved from; nullptr when there is none.
/// @param address Whose controls these are, if known.
void writeMidiKnobs(Serializer& writer, EffectsOwner owner, ::Song* song,
                    std::optional<project::ControlsAddress> address);

/// @brief Reads one of an owner's tags: `defaultParams` (a host chain's, when @p paramManager), `arpeggiator` (when
/// @p arpSettings) or `midiKnobs` (into the owner's knob carrier).
/// @param reader                Source, at @p tagName.
/// @param tagName               The tag.
/// @param owner                 The owner being read.
/// @param paramManager          A host chain's params, or nullptr.
/// @param readAutomationUpToPos Automation is read up to this position.
/// @param arpSettings           The arpeggiator to read into, or nullptr.
/// @param arpRead               Set when the arpeggiator was read; may be null.
/// @param song                  The song being read.
/// @return RESULT_TAG_UNUSED for another tag; an error from setting the params up; else NONE.
[[nodiscard]] Error readEffectTag(Deserializer& reader, const char* tagName, EffectsOwner owner,
                                  ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos,
                                  ArpeggiatorSettings* arpSettings, bool* arpRead, ::Song* song);

/// @brief Reads a host chain's effects and its view from its element, as the effects library decodes them, sync levels
/// relative to @p tickMagnitude; marks the binding's delay type pending when the element saves none.
/// @param owner         The host chain's owner (owner.view set).
/// @param element       The owner's serialized element.
/// @param tickMagnitude Tick magnitude that sync levels are relative to.
void readEffectsFromElement(EffectsOwner owner, const serialization::Value& element, int32_t tickMagnitude);

/// @name A host chain's param block
/// @{
/// @brief Writes a host chain's param block attributes from its DOM store.
/// @param writer            Destination.
/// @param paramManager      The host chain's params.
/// @param writeAutomation   Whether the params' automation is written.
/// @param valuesForOverride When given, overrides each param's value but the shared arp ones.
void writeGlobalParamAttributes(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
                                int32_t* valuesForOverride = nullptr);
/// @brief Writes a host chain's param block tags (the `<delay>`, `<lpf>` and `<hpf>` groups, then the shared
/// effects tags) from its DOM store.
/// @param writer            Destination.
/// @param paramManager      The host chain's params.
/// @param writeAutomation   Whether the params' automation is written.
/// @param valuesForOverride When given, overrides each param's value.
void writeGlobalParamTags(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
                          int32_t* valuesForOverride = nullptr);
/// @brief Reads the param block into the owner's DOM store, then projects each engine RT cursor from the store. The
/// defaults must already be in the store (initGlobalParams), so the DOM holds the authoritative current value, not just
/// the automation nodes. Used by audio and kit clips.
/// @param reader                Source of the param block.
/// @param paramManager          The owner's param manager.
/// @param readAutomationUpToPos Automation is read up to this position.
void readGlobalParamsDomFirst(Deserializer& reader, ParamManagerForTimeline* paramManager,
                              int32_t readAutomationUpToPos);
/// @}

} // namespace deluge::model
