/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "deluge-project/param_automation.h"

#include <cstdint>
#include <span>
#include <vector>

class ParamSet;
class ParamCollectionSummary;
class Serializer;
class Deserializer;

namespace deluge::bridge {

using deluge::project::DocAutoParam;
using deluge::project::DocCable;
using deluge::project::DocMidiParam;
using deluge::project::DocParamSet;

// --- Generic serialization of the document model ---
// Emits the song file format from the value DOM.

/// @brief Write one AutoParam's value (and automation when @p writeAutomation) as the `0x…` hex blob; the
/// document-side equivalent of `AutoParam::writeToFile`.
///
/// The caller emits the surrounding tag/quotes. The automation nodes are always written as-is.
///
/// @param doc              The param to write.
/// @param out              The serializer to write to.
/// @param writeAutomation  Whether to write the automation nodes after the value.
/// @param valueForOverride Mirrors the engine arg: when non-null and the param is automated, that value is written
///                         in place of `current_value` (the Song-level "params in automation mode" override).
void writeDocAutoParam(const DocAutoParam& doc, Serializer& out, bool writeAutomation,
                       const int32_t* valueForOverride = nullptr);

/// @brief Write one of a collection's params as a named attribute (`name="0x…"`), byte-identical to
/// `ParamSet::writeParamAsAttribute`.
///
/// This is the building block the owner param blocks (GlobalEffectable / ModControllableAudio / Sound / clips) are
/// composed of.
///
/// @param doc                    The collection holding the param.
/// @param name                   The attribute name.
/// @param id                     The param's id.
/// @param out                    The serializer to write to.
/// @param writeAutomation        Whether to write the automation nodes.
/// @param onlyIfContainsSomething Skip the attribute unless the param is automated or non-neutral.
/// @param valuesForOverride      When non-null, indexed by the param's position in @p doc: that entry is the override
///                               value (see writeDocAutoParam).
void writeDocParamAsAttribute(const DocParamSet& doc, char const* name, deluge::engine::ParamId id, Serializer& out,
                              bool writeAutomation, bool onlyIfContainsSomething = false,
                              const int32_t* valuesForOverride = nullptr);

/// @brief One entry of an owner's param block: a file tag/attribute name and the id of the param it holds (its
/// engine's id; for the expression block, the dimension).
///
/// Owner blocks are expressed as compact arrays of these driving the generic helpers.
struct DocParamAttr {
	const char* name;
	deluge::engine::ParamId id;
};

/// @brief Write a nested group of params (e.g. `<equalizer>` / `<lpf>`) from a collection: the group's opening tag,
/// each member as an attribute (writeDocParamAsAttribute), then closeTag().
///
/// @param out              The serializer to write to.
/// @param groupName        The group's tag name.
/// @param params           The group's members.
/// @param doc              The collection holding the params.
/// @param writeAutomation  Whether to write the automation nodes.
/// @param valuesForOverride When non-null, indexed by each param's position in @p doc: those entries are the
///                          override values (see writeDocAutoParam).
void writeDocParamGroup(Serializer& out, const char* groupName, std::span<const DocParamAttr> params,
                        const DocParamSet& doc, bool writeAutomation, const int32_t* valuesForOverride = nullptr);

/// @brief Serialize the ModControllableAudio shared param block's flat attrs from the unpatched collection (what
/// `ModControllableAudio::writeParamAttributesToFile` writes).
///
/// The attrs and the `<equalizer>` group (serializeModControllableParamTags) are emitted at different points in the
/// owner's tag, so they are separate.
///
/// @param unpatched        The unpatched collection.
/// @param out              The serializer to write to.
/// @param writeAutomation  Whether to write the automation nodes.
/// @param valuesForOverride When non-null, its entries override the first six attrs' values (see writeDocAutoParam).
void serializeModControllableParamAttributes(const DocParamSet& unpatched, Serializer& out, bool writeAutomation,
                                             const int32_t* valuesForOverride = nullptr);
/// @brief Serialize the ModControllableAudio shared param block's tags (the `<equalizer>` group; what
/// `ModControllableAudio::writeParamTagsToFile` writes).
///
/// @param unpatched        The unpatched collection.
/// @param out              The serializer to write to.
/// @param writeAutomation  Whether to write the automation nodes.
/// @param valuesForOverride When non-null, its entries override the group members' values (see writeDocAutoParam).
void serializeModControllableParamTags(const DocParamSet& unpatched, Serializer& out, bool writeAutomation,
                                       const int32_t* valuesForOverride = nullptr);

/// @brief Serialize the GlobalEffectable param block's flat attrs from the unpatched collection (what
/// `GlobalEffectable::writeParamAttributesToFile` writes).
///
/// The flat global attrs (reverb/volume/pan, the conditional pitchAdjust + sidechain volume, modFX…) wrap the shared
/// ModControllableAudio attrs, then lpfMorph/hpfMorph/tempo/arpeggiatorRate follow.
///
/// @param unpatched        The unpatched collection.
/// @param out              The serializer to write to.
/// @param writeAutomation  Whether to write the automation nodes.
/// @param valuesForOverride When non-null, entry [p] overrides param p's value (see writeDocAutoParam) for every
///                         attr except the ModControllableAudio arp/probability/spread ones.
void serializeGlobalEffectableParamAttributes(const DocParamSet& unpatched, Serializer& out, bool writeAutomation,
                                              const int32_t* valuesForOverride = nullptr);
/// @brief Serialize the GlobalEffectable param block's tags from the unpatched collection (what
/// `GlobalEffectable::writeParamTagsToFile` writes): the `<delay>` / `<lpf>` / `<hpf>` groups, then the shared
/// ModControllableAudio tags.
///
/// @param unpatched        The unpatched collection.
/// @param out              The serializer to write to.
/// @param writeAutomation  Whether to write the automation nodes.
/// @param valuesForOverride When non-null, its entries override the group members' values (see writeDocAutoParam).
void serializeGlobalEffectableParamTags(const DocParamSet& unpatched, Serializer& out, bool writeAutomation,
                                        const int32_t* valuesForOverride = nullptr);

/// @brief Serialize the ExpressionParamSet (MPE) block from the expression collection (what
/// `ExpressionParamSet::writeToFile` writes).
///
/// The `<expressionData>` group holds pitchBend / yExpression / pressure, emitted only when at least one dimension
/// contains something (and each dimension only when it does).
///
/// @param expression                 The expression collection.
/// @param out                        The serializer to write to.
/// @param mustWriteOpeningTagEndFirst Mirrors the engine arg: close the parent's opening tag before the first child.
/// @param writeAutomation            Whether to write the automation nodes.
/// @return Whether anything was written.
bool serializeExpressionParams(const DocParamSet& expression, Serializer& out, bool mustWriteOpeningTagEndFirst,
                               bool writeAutomation);

/// @brief Serialize the Sound param block from a sound's collections (what `Sound::writeParamsToFile` writes).
///
/// The flat patched + unpatched attrs (osc / filter / LFO / FM / envelope-less flat params, the conditional
/// pitchAdjust family) wrap the shared ModControllableAudio attrs; then, after the parent's opening tag is closed,
/// come the four `<envelope*>` groups, the `<patchCables>` block, and the shared ModControllableAudio tags.
/// `writeOpeningTagEnd` is emitted between the attrs and the child tags.
///
/// @param patched          The patched collection.
/// @param unpatched        The unpatched collection.
/// @param cables           The sound's patch cables.
/// @param out              The serializer to write to.
/// @param writeAutomation  Whether to write the automation nodes.
void serializeSoundParams(const DocParamSet& patched, const DocParamSet& unpatched, const std::vector<DocCable>& cables,
                          Serializer& out, bool writeAutomation);

/// @brief Write the `<patchCables>` block from the document's patch cables, byte-identical to
/// `PatchCableSet::writePatchCablesToFile` (including the depth-controlling / range-cable nesting).
///
/// @param cables          The patch cables.
/// @param out             The serializer to write to.
/// @param writeAutomation Whether to write the automation nodes.
void serializePatchCables(const std::vector<DocCable>& cables, Serializer& out, bool writeAutomation);

/// @brief Write the `<midiParams>` block from the document's MIDI-CC params (what
/// `MIDIParamCollection::writeToFile` writes).
///
/// One `<param>` per CC holds a `<cc>` tag (the number, or "none" for CC_NUMBER_NONE) and a `<value>` leaf
/// (writeDocAutoParam). Nothing is emitted when there are no params.
///
/// @param midiParams      The MIDI-CC params.
/// @param out             The serializer to write to.
/// @param writeAutomation Whether to write the automation nodes.
void serializeMidiParams(const std::vector<DocMidiParam>& midiParams, Serializer& out, bool writeAutomation);

/// @brief Read one AutoParam from the hex blob into the document form; the read counterpart of writeDocAutoParam.
///
/// Reuses the engine's `AutoParam::readFromFile` (so the leaf hex parsing and its back-compat are shared), then
/// projects the result.
///
/// @param in                    The deserializer positioned at the leaf.
/// @param readAutomationUpToPos Upper bound on the automation node positions to read.
/// @return The param in document form.
DocAutoParam readDocAutoParam(Deserializer& in, int32_t readAutomationUpToPos);

// --- DOM-native read of the owner param blocks ---
// The read counterpart of the serializers above. Each `read*ParamTag` is the table-driven DOM reader for one owner's
// param block: if `tagName` is a current-format param tag, it reads the leaf via `readDocAutoParam` into the live
// collection (and marks the summary's automation flag, matching `ParamSet::readParam`) and returns true; otherwise it
// returns false so the engine's remaining (legacy-only) dispatch can handle it. Driven by the same `DocParamAttr`
// tables the serializers use.

/// @brief Read one ModControllableAudio shared-block tag (the `<equalizer>` group + the flat MCA attrs) into the
/// live collection.
///
/// @param in                    The deserializer.
/// @param tagName               The tag just opened.
/// @param unpatched             The live unpatched set.
/// @param unpatchedSummary      The unpatched set's summary, whose automation flag is marked.
/// @param readAutomationUpToPos Upper bound on the automation node positions to read.
/// @return true if @p tagName was a param tag and was read.
bool readModControllableParamTag(Deserializer& in, char const* tagName, ParamSet& unpatched,
                                 ParamCollectionSummary& unpatchedSummary, int32_t readAutomationUpToPos);

/// @brief Read one GlobalEffectable param-block tag into the live collection: the `<delay>`/`<lpf>`/`<hpf>` groups +
/// the GE flat attrs, then the shared MCA block.
///
/// The `<lpf>`/`<hpf>` read groups also accept the legacy nested `morph` member (which the writer emits as a flat
/// `lpfMorph`/`hpfMorph` attr).
///
/// @param in                    The deserializer.
/// @param tagName               The tag just opened.
/// @param unpatched             The live unpatched set.
/// @param unpatchedSummary      The unpatched set's summary, whose automation flag is marked.
/// @param readAutomationUpToPos Upper bound on the automation node positions to read.
/// @return true if @p tagName was a param tag and was read.
bool readGlobalEffectableParamTag(Deserializer& in, char const* tagName, ParamSet& unpatched,
                                  ParamCollectionSummary& unpatchedSummary, int32_t readAutomationUpToPos);

/// @brief Read one Sound param-block tag into the live collections: the two unpatched flat attrs (portamento /
/// compressorShape), the patched flat attrs (osc / filter / LFO / FM / pitchAdjust family / community attrs), the
/// four `<envelope*>` groups, then the shared MCA block.
///
/// @note The `<patchCables>` tag is not handled here: the caller keeps the engine's reader for it (it carries
///       pre-V3.2 cable back-compat the DOM reader drops).
///
/// @param in                    The deserializer.
/// @param tagName               The tag just opened.
/// @param patched               The live patched set.
/// @param patchedSummary        The patched set's summary, whose automation flag is marked.
/// @param unpatched             The live unpatched set.
/// @param unpatchedSummary      The unpatched set's summary, whose automation flag is marked.
/// @param readAutomationUpToPos Upper bound on the automation node positions to read.
/// @return true if @p tagName was a param tag and was read.
bool readSoundParamTag(Deserializer& in, char const* tagName, ParamSet& patched, ParamCollectionSummary& patchedSummary,
                       ParamSet& unpatched, ParamCollectionSummary& unpatchedSummary, int32_t readAutomationUpToPos);

/// @brief Read one ExpressionParamSet (MPE) tag (pitchBend / yExpression / pressure) into the live expression
/// collection.
///
/// @note The legacy `channelPressure` alias is not handled here: the caller keeps it as a tail.
///
/// @param in                    The deserializer.
/// @param tagName               The tag just opened.
/// @param expression            The live expression set.
/// @param summary               The expression set's summary, whose automation flag is marked.
/// @param readAutomationUpToPos Upper bound on the automation node positions to read.
/// @return true if @p tagName was a param tag and was read.
bool readExpressionParamTag(Deserializer& in, char const* tagName, ParamSet& expression,
                            ParamCollectionSummary& summary, int32_t readAutomationUpToPos);

// DOM-target variants: identical dispatch, but each leaf lands in a `DocParamSet` (the persistent DOM store) instead
// of a live engine `ParamSet`. `doc.params` must be pre-sized to the collection's param count; the engine is then
// reprojected from the store via `fromDocParamSet`. These share the same `DocParamAttr` tables as the
// engine-target readers above.

/// @brief Read one ModControllableAudio shared-block tag into @p doc (the `<equalizer>` group + the flat MCA
/// attrs).
///
/// @param in                    The deserializer.
/// @param tagName               The tag just opened.
/// @param doc                   The unpatched store, pre-sized.
/// @param readAutomationUpToPos Upper bound on the automation node positions to read.
/// @return true if @p tagName was a param tag and was read.
bool readModControllableParamTagToDoc(Deserializer& in, char const* tagName, DocParamSet& doc,
                                      int32_t readAutomationUpToPos);

/// @brief Read one GlobalEffectable param-block tag into @p doc: the `<delay>`/`<lpf>`/`<hpf>` groups + GE flat
/// attrs (incl. the legacy nested `morph` members) + the shared MCA block.
///
/// @param in                    The deserializer.
/// @param tagName               The tag just opened.
/// @param doc                   The unpatched store, pre-sized.
/// @param readAutomationUpToPos Upper bound on the automation node positions to read.
/// @return true if @p tagName was a param tag and was read.
bool readGlobalEffectableParamTagToDoc(Deserializer& in, char const* tagName, DocParamSet& doc,
                                       int32_t readAutomationUpToPos);

/// @brief Read one ExpressionParamSet (MPE) tag (pitchBend / yExpression / pressure) into @p doc, incl. the legacy
/// `channelPressure` alias (pressure / dimension 2) that the engine reader keeps as a caller tail.
///
/// @param in                    The deserializer.
/// @param tagName               The tag just opened.
/// @param doc                   The expression store, pre-sized.
/// @param readAutomationUpToPos Upper bound on the automation node positions to read.
/// @return true if @p tagName was a param tag and was read.
bool readExpressionParamTagToDoc(Deserializer& in, char const* tagName, DocParamSet& doc,
                                 int32_t readAutomationUpToPos);

/// @brief Read one Sound param-block tag into the two DOM stores: unpatched flat attrs + the shared MCA block land in
/// @p unpatched; patched flat attrs + the `<envelope1..4>` groups land in @p patched.
///
/// @note `<patchCables>` is not handled here: the caller keeps the engine reader for it (pre-V3.2 cable back-compat
///       the DOM reader drops).
///
/// @param in                    The deserializer.
/// @param tagName               The tag just opened.
/// @param patched               The patched store, pre-sized.
/// @param unpatched             The unpatched store, pre-sized.
/// @param readAutomationUpToPos Upper bound on the automation node positions to read.
/// @return true if @p tagName was a param tag and was read.
bool readSoundParamTagToDoc(Deserializer& in, char const* tagName, DocParamSet& patched, DocParamSet& unpatched,
                            int32_t readAutomationUpToPos);

} // namespace deluge::bridge
