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

/// Projection between a live ParamSet and its DocParamSet, owner-resolving each param's node store. Defined in
/// param_automation.cpp.
DocParamSet toDocParamSet(ParamSet& paramSet);
/// Option I save-flip: like toDocParamSet but reads the persisted scalar from the DOM store (docForParam), not the
/// engine cursor — the authoritative source for saving. Every persistent value-setter writes the store.
DocParamSet toDocParamSetFromStore(ParamSet& paramSet);
void fromDocParamSet(const DocParamSet& doc, ParamSet& paramSet);

/// Project to/from a live `ParamManager` (defined in param_automation.cpp). `fromDocParamManager`
/// projects into an already-set-up manager — it fills the collections that exist in both the document
/// and the target (the engine builds the collection structure on load before the values are projected
/// in); it does not allocate or remove collections.
DocParamManager toDocParamManager(ParamManager& paramManager);
/// Option I save-flip: like toDocParamManager but reads each ParamSet's persisted scalar from the DOM store
/// (patch cables already store-sourced). Used by the Sound save path.
DocParamManager toDocParamManagerFromStore(ParamManager& paramManager);
void fromDocParamManager(const DocParamManager& doc, ParamManager& paramManager);

/// Project a live PatchCableSet's cables into their document form — the identity-keyed persistent cable store the
/// engine set binds to (S4b). Each cable's saved fields are copied verbatim (the ParamDescriptor encodes the
/// destination + any range-cable nesting); nodes come from the cable's current store. Defined in
/// param_automation.cpp.
std::vector<DocCable> toDocCables(PatchCableSet& patchCableSet);

/// Project a live MIDIParamCollection's CC params into their document form — the identity-keyed (by cc) persistent
/// store the engine collection binds to (S4c). Nodes come from each CC's current store. Defined in
/// param_automation.cpp.
std::vector<DocMidiParam> toDocMidiParams(MIDIParamCollection& midiParamCollection);

// --- DOM-first editing ---
// The DOM store is the authoritative record; the engine param is the runtime it drives. A DOM-first edit
// mutates the store lane, then reprojects it into the engine so playback hears it (until the future
// graph-builder/executor split lets the runtime read the DOM directly, this reprojection is the seam).

/// DOM-first non-automated value edit: write `value` into the store's unpatched lane `p` (the authoritative
/// record), then reproject that lane into the engine `engineParam` (the runtime). Returns false (no-op) if the
/// store's unpatched set isn't populated or `p` is out of range. The caller performs the runtime notify/undo
/// (those are engine concerns, and this layer must not depend on the UI/action globals).
bool setUnpatchedValueDomFirst(DocParamManager& store, int32_t p, int32_t value, AutoParam& engineParam);

} // namespace deluge::bridge
