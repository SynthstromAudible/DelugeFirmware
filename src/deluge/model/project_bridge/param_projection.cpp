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

#include "model/project_bridge/param_projection.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/patch_cable_words.h"
#include "modulation/automation/auto_param.h"
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include <algorithm>
#include <cstdint>

namespace deluge::bridge {

using namespace deluge::project;

// M2-F: projection operates on the OWNER-resolved DOM store. There is no bare-AutoParam form — an AutoParam holds
// no node storage.
DocParamSet toDocParamSet(ParamSet& paramSet) {
	// Owner-resolve each param's node store (docForParam) so a bound set serialises the DOM store's nodes. The
	// scalar comes from the engine cursor here; the SAVE path uses toDocParamSetFromStore (below) to read the
	// store's authoritative scalar instead. (This engine-cursor form still backs the projection unit tests, which
	// prove the DOM serializer matches the engine serializer for a given cursor value.)
	DocParamSet doc;
	doc.first = firstParamId(paramSet.getParamKind());
	int32_t numParams = paramSet.getNumParams();
	doc.params.reserve(numParams);
	for (int32_t p = 0; p < numParams; p++) {
		doc.params.push_back(toDocAutoParam(paramSet.params[p].currentValue, paramSet.docForParam(p)->nodes));
	}
	return doc;
}

DocParamSet toDocParamSetFromStore(ParamSet& paramSet) {
	// Option I (save-flip): serialise BOTH value and nodes from the owner-resolved DOM store — the authoritative
	// record. Every persistent value-setter (initParams, the DOM-first reads, edits, the setupAs*/initAmount preset
	// builders) writes docForParam->current_value, so the store holds the value to persist with NO engine read.
	DocParamSet doc;
	doc.first = firstParamId(paramSet.getParamKind());
	int32_t numParams = paramSet.getNumParams();
	doc.params.reserve(numParams);
	for (int32_t p = 0; p < numParams; p++) {
		deluge::project::DocAutoParam* slot = paramSet.docForParam(p);
		doc.params.push_back(toDocAutoParam(slot->current_value, slot->nodes));
	}
	return doc;
}

void fromDocParamSet(const DocParamSet& doc, ParamSet& paramSet) {
	// M2-F: owner-resolve each param's node store (docForParam), so the projection lands in the set's DOM store
	// (where the engine reads it) rather than the AutoParam's inline storage.
	int32_t count = std::min(static_cast<int32_t>(doc.params.size()), paramSet.getNumParams());
	for (int32_t p = 0; p < count; p++) {
		fromDocAutoParam(doc.params[p], paramSet.params[p].currentValue, paramSet.docForParam(p)->nodes);
	}
}

namespace params = deluge::modulation::params;

namespace {
using deluge::modulation::params::Kind;
// Slot 0 holds either the unpatched ParamSet or a MIDI-CC collection (the latter deferred to a later
// sub-phase). The unpatched set's own `kind` is often still NONE here (the owner sets UNPATCHED_SOUND/
// GLOBAL later), so identify it positionally: present and not the MIDI collection.
bool isUnpatchedSetSlot(ParamCollection* c) {
	return c != nullptr && c->getParamKind() != Kind::MIDI;
}

// Recompute a collection summary's automation flags from the live params after projection. The flags are
// only ever set incrementally during normal reads/edits (there is no post-load rebuild), so a projection
// that drives the live manager must set them or automation won't play back.
void refreshAutomationFlags(ParamSet& set, ParamCollectionSummary& summary) {
	for (int32_t i = 0; i < kMaxNumUnsignedIntegerstoRepAllParams; i++) {
		summary.whichParamsAreAutomated[i] = 0;
	}
	int32_t numParams = set.getNumParams();
	for (int32_t p = 0; p < numParams; p++) {
		if (set.params[p].isAutomated(*set.docForParam(p))) {
			set.paramHasAutomationNow(&summary, p);
		}
	}
}
} // namespace

DocParamManager toDocParamManager(ParamManager& paramManager) {
	DocParamManager doc;
	// Fixed slot layout: [0] unpatched (or MIDI, deferred), [1] patched, [2] patch cables (deferred),
	// expression at expressionParamSetOffset.
	if (ParamCollection* c = paramManager.summaries[0].paramCollection; isUnpatchedSetSlot(c)) {
		*storeFor(doc, c->getParamKind()) = toDocParamSet(*static_cast<ParamSet*>(c));
	}
	if (ParamCollection* c = paramManager.summaries[1].paramCollection;
	    c != nullptr && c->getParamKind() == Kind::PATCHED) {
		*storeFor(doc, Kind::PATCHED) = toDocParamSet(*static_cast<ParamSet*>(c));
	}
	if (paramManager.getExpressionParamSetOffset() != 0) {
		if (ExpressionParamSet* e = paramManager.getExpressionParamSet(); e != nullptr) {
			doc.expression = toDocParamSet(*e);
		}
	}
	// Slot [2]: patch cables.
	if (ParamCollection* c = paramManager.summaries[2].paramCollection;
	    c != nullptr && c->getParamKind() == Kind::PATCH_CABLE) {
		doc.cables = toDocCables(*static_cast<PatchCableSet*>(c));
	}
	return doc;
}

DocParamManager toDocParamManagerFromStore(ParamManager& paramManager) {
	// Option I save-flip variant: patched/unpatched/expression scalars from the DOM store (toDocParamSetFromStore);
	// patch cables already source their scalar+nodes from the store (toDocCables via docForParam). Used by the
	// Sound save path so the persisted values come from the authoritative store, never the engine cursor.
	DocParamManager doc;
	if (ParamCollection* c = paramManager.summaries[0].paramCollection; isUnpatchedSetSlot(c)) {
		*storeFor(doc, c->getParamKind()) = toDocParamSetFromStore(*static_cast<ParamSet*>(c));
	}
	if (ParamCollection* c = paramManager.summaries[1].paramCollection;
	    c != nullptr && c->getParamKind() == Kind::PATCHED) {
		*storeFor(doc, Kind::PATCHED) = toDocParamSetFromStore(*static_cast<ParamSet*>(c));
	}
	if (paramManager.getExpressionParamSetOffset() != 0) {
		if (ExpressionParamSet* e = paramManager.getExpressionParamSet(); e != nullptr) {
			doc.expression = toDocParamSetFromStore(*e);
		}
	}
	if (ParamCollection* c = paramManager.summaries[2].paramCollection;
	    c != nullptr && c->getParamKind() == Kind::PATCH_CABLE) {
		doc.cables = toDocCables(*static_cast<PatchCableSet*>(c));
	}
	return doc;
}

std::vector<DocMidiParam> toDocMidiParams(MIDIParamCollection& midi) {
	// Copy each CC param's identity (cc) + value/nodes. Nodes come from the CC's CURRENT store via docForParam —
	// the bound tree entry when bound (where runtime edits land), the inline nodes_ otherwise — NOT the inline
	// store directly (which goes stale once bound, so reading it would drop edits on save).
	std::vector<DocMidiParam> out;
	out.reserve(midi.params.size());
	for (int32_t i = 0; i < std::ssize(midi.params); i++) {
		// Option I (read-authority): value AND nodes both from the CC's CURRENT store via docForParam (the bound
		// tree entry when bound — where the read seeds current_value and runtime edits land — else the inline
		// nodes_). Reading param.currentValue (the engine cursor) would make the engine the source of the scalar.
		deluge::project::DocAutoParam* d = midi.docForParam(i);
		out.push_back(DocMidiParam{
		    .cc = midi.params[i].cc,
		    .param = toDocAutoParam(d->current_value, d->nodes),
		});
	}
	return out;
}

std::vector<DocCable> toDocCables(PatchCableSet& pcs) {
	// Copy each cable's saved fields verbatim (the ParamDescriptor encodes the destination + any range-cable
	// nesting); rangeAdjustmentPointer is runtime and rebuilt on projection. Nodes come from the cable's current
	// store — its inline amount_ pre-bind (seed time), which is exactly what the persistent store should hold.
	std::vector<DocCable> out;
	out.reserve(pcs.numPatchCables);
	for (int32_t i = 0; i < pcs.numPatchCables; i++) {
		PatchCable& cable = pcs.patchCables[i];
		// Option I (read-authority): value AND nodes both from the cable's CURRENT store via docForParam (the bound
		// tree entry when bound, where the read seeds current_value and runtime edits land — else the inline
		// amount_). Reading cable.param.currentValue (the engine cursor) would make the engine the scalar's source.
		deluge::project::DocAutoParam* d = pcs.docForParam(i);
		out.push_back(doc_cable(cable.from, cable.destinationParamDescriptor, cable.polarity,
		                        toDocAutoParam(d->current_value, d->nodes)));
	}
	return out;
}

void fromDocParamManager(const DocParamManager& doc, ParamManager& paramManager) {
	if (ParamCollection* c = paramManager.summaries[0].paramCollection; isUnpatchedSetSlot(c)) {
		if (const std::optional<DocParamSet>& unpatched = *storeFor(doc, c->getParamKind())) {
			ParamSet& set = *static_cast<ParamSet*>(c);
			fromDocParamSet(*unpatched, set);
			refreshAutomationFlags(set, paramManager.summaries[0]);
		}
	}
	if (const std::optional<DocParamSet>& patched = *storeFor(doc, Kind::PATCHED)) {
		if (ParamCollection* c = paramManager.summaries[1].paramCollection;
		    c != nullptr && c->getParamKind() == Kind::PATCHED) {
			ParamSet& set = *static_cast<ParamSet*>(c);
			fromDocParamSet(*patched, set);
			refreshAutomationFlags(set, paramManager.summaries[1]);
		}
	}
	if (doc.expression) {
		if (ExpressionParamSet* e = paramManager.getExpressionParamSet(); e != nullptr) {
			fromDocParamSet(*doc.expression, *e);
			if (ParamCollectionSummary* s = paramManager.getExpressionParamSetSummary(); s != nullptr) {
				refreshAutomationFlags(*e, *s);
			}
		}
	}
	// Patch cables: refill the cable set faithfully (count + each saved field). setupPatching (the
	// engine's post-load step) rebuilds the runtime rangeAdjustmentPointers.
	if (ParamCollection* c = paramManager.summaries[2].paramCollection;
	    c != nullptr && c->getParamKind() == Kind::PATCH_CABLE) {
		PatchCableSet* pcs = static_cast<PatchCableSet*>(c);
		int32_t count = std::min(static_cast<int32_t>(doc.cables.size()), int32_t{kMaxNumPatchCables});
		pcs->numPatchCables = count;
		for (int32_t i = 0; i < count; i++) {
			const DocCable& dc = doc.cables[i];
			pcs->patchCables[i].from = source_of(dc);
			pcs->patchCables[i].destinationParamDescriptor = destination_of(dc);
			pcs->patchCables[i].polarity = polarity_of(dc);
			// M2-F: project the value into the RT cursor and the nodes into the cable's own store.
			fromDocAutoParam(dc.amount, pcs->patchCables[i].param.currentValue, pcs->patchCables[i].amount_.nodes);
		}
	}
}

bool setUnpatchedValueDomFirst(DocParamManager& store, int32_t p, int32_t value, AutoParam& engineParam) {
	std::optional<DocParamSet>& unpatched = *storeFor(store, Kind::UNPATCHED_GLOBAL);
	if (!unpatched || p < 0 || p >= static_cast<int32_t>(unpatched->params.size())) {
		return false;
	}
	unpatched->params[p].current_value = value; // authoritative write (params[p] is the runtime's index p)
	// M2-F: only the value changed (not nodes, which the engine owner-resolves from this same store); sync the
	// engine's RT cursor. No projection into AutoParam storage (there is none).
	engineParam.currentValue = value;
	return true;
}

} // namespace deluge::bridge
