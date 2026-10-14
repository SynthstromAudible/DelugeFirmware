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

#include <vector>

class AutoParam;
class ParamSet;
class ParamManager;
class PatchCableSet;
class MIDIParamCollection;

namespace deluge::bridge {

using deluge::project::DocAutoParam;
using deluge::project::DocCable;
using deluge::project::DocMidiParam;
using deluge::project::DocParamManager;
using deluge::project::DocParamSet;

/// @brief Project a live ParamSet into its DocParamSet, owner-resolving each param's node store.
///
/// The scalar of each param is read from the engine cursor.
///
/// @param paramSet The live set.
/// @return The set's document form.
DocParamSet toDocParamSet(ParamSet& paramSet);
/// @brief Like toDocParamSet, but read the persisted scalar from the DOM store (docForParam), not the engine cursor.
///
/// The store is the authoritative source for saving: every persistent value-setter writes it.
///
/// @param paramSet The live set.
/// @return The set's document form.
DocParamSet toDocParamSetFromStore(ParamSet& paramSet);
/// @brief Project a DocParamSet into a live ParamSet, owner-resolving each param's node store.
///
/// @param doc      The document form.
/// @param paramSet The live set to fill.
void fromDocParamSet(const DocParamSet& doc, ParamSet& paramSet);

/// @brief Project a live `ParamManager` into its document form.
///
/// @param paramManager The live manager.
/// @return The manager's document form.
DocParamManager toDocParamManager(ParamManager& paramManager);
/// @brief Like toDocParamManager, but read each ParamSet's persisted scalar from the DOM store (patch cables are
/// store-sourced either way). Used by the Sound save path.
///
/// @param paramManager The live manager.
/// @return The manager's document form.
DocParamManager toDocParamManagerFromStore(ParamManager& paramManager);
/// @brief Project a document into an already-set-up live `ParamManager`.
///
/// Fills the collections that exist in both the document and the target (the engine builds the collection
/// structure on load before the values are projected in); it does not allocate or remove collections.
///
/// @param doc          The document form.
/// @param paramManager The live manager to fill.
void fromDocParamManager(const DocParamManager& doc, ParamManager& paramManager);

/// @brief Project a live PatchCableSet's cables into their document form, the identity-keyed persistent cable store
/// the engine set binds to.
///
/// Each cable's saved fields are copied as they are (the ParamDescriptor encodes the destination and any range-cable
/// nesting); nodes come from the cable's current store.
///
/// @param patchCableSet The live cable set.
/// @return The cables' document form.
std::vector<DocCable> toDocCables(PatchCableSet& patchCableSet);

/// @brief Project a live MIDIParamCollection's CC params into their document form, the identity-keyed (by cc)
/// persistent store the engine collection binds to.
///
/// Nodes come from each CC's current store.
///
/// @param midiParamCollection The live CC collection.
/// @return The CC params' document form.
std::vector<DocMidiParam> toDocMidiParams(MIDIParamCollection& midiParamCollection);

// --- DOM-first editing ---
// The DOM store is the authoritative record; the engine param is the runtime it drives. A DOM-first edit
// mutates the store lane, then reprojects it into the engine so playback hears it.

/// @brief DOM-first non-automated value edit: write @p value into the store's unpatched lane @p p (the
/// authoritative record), then reproject that lane into the engine @p engineParam (the runtime).
///
/// The caller performs the runtime notify/undo (those are engine concerns, and this layer must not depend on the
/// UI/action globals).
///
/// @param store       The model's param manager.
/// @param p           The unpatched lane (the runtime's index).
/// @param value       The new value.
/// @param engineParam The engine param to reproject into.
/// @return false (a no-op) if the store's unpatched set isn't populated or @p p is out of range, else true.
bool setUnpatchedValueDomFirst(DocParamManager& store, int32_t p, int32_t value, AutoParam& engineParam);

} // namespace deluge::bridge
