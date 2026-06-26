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

#include "deluge-project/param_automation.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include <algorithm>

namespace deluge::project {

DocAutoParam toDocAutoParam(const AutoParam& param) {
	return toDocAutoParam(param.currentValue, param.nodes);
}

void fromDocAutoParam(const DocAutoParam& doc, AutoParam& param) {
	fromDocAutoParam(doc, param.currentValue, param.nodes);
}

DocParamSet toDocParamSet(deluge::modulation::params::Kind kind, const AutoParam* params, int32_t numParams) {
	DocParamSet doc;
	doc.kind = kind;
	doc.params.reserve(numParams);
	for (int32_t p = 0; p < numParams; p++) {
		doc.params.push_back(toDocAutoParam(params[p]));
	}
	return doc;
}

void fromDocParamSet(const DocParamSet& doc, AutoParam* params, int32_t numParams) {
	int32_t count = std::min(static_cast<int32_t>(doc.params.size()), numParams);
	for (int32_t p = 0; p < count; p++) {
		fromDocAutoParam(doc.params[p], params[p]);
	}
}

DocParamSet toDocParamSet(ParamSet& paramSet) {
	return toDocParamSet(paramSet.getParamKind(), paramSet.params, paramSet.getNumParams());
}

void fromDocParamSet(const DocParamSet& doc, ParamSet& paramSet) {
	fromDocParamSet(doc, paramSet.params, paramSet.getNumParams());
}

namespace {
using deluge::modulation::params::Kind;
// Slot 0 holds either the unpatched ParamSet or a MIDI-CC collection (the latter deferred to a later
// sub-phase). The unpatched set's own `kind` is often still NONE here (the owner sets UNPATCHED_SOUND/
// GLOBAL later), so identify it positionally: present and not the MIDI collection.
bool isUnpatchedSetSlot(ParamCollection* c) {
	return c != nullptr && c->getParamKind() != Kind::MIDI;
}
} // namespace

DocParamManager toDocParamManager(ParamManager& paramManager) {
	DocParamManager doc;
	// Fixed slot layout: [0] unpatched (or MIDI, deferred), [1] patched, [2] patch cables (deferred),
	// expression at expressionParamSetOffset.
	if (ParamCollection* c = paramManager.summaries[0].paramCollection; isUnpatchedSetSlot(c)) {
		doc.unpatched = toDocParamSet(*static_cast<ParamSet*>(c));
	}
	if (ParamCollection* c = paramManager.summaries[1].paramCollection;
	    c != nullptr && c->getParamKind() == Kind::PATCHED) {
		doc.patched = toDocParamSet(*static_cast<ParamSet*>(c));
	}
	if (paramManager.getExpressionParamSetOffset() != 0) {
		if (ExpressionParamSet* e = paramManager.getExpressionParamSet(); e != nullptr) {
			doc.expression = toDocParamSet(*e);
		}
	}
	// Slot [2]: patch cables. Copy each cable's saved fields verbatim (the ParamDescriptor encodes the
	// destination + any range-cable nesting); rangeAdjustmentPointer is runtime and rebuilt on projection.
	if (ParamCollection* c = paramManager.summaries[2].paramCollection;
	    c != nullptr && c->getParamKind() == Kind::PATCH_CABLE) {
		PatchCableSet* pcs = static_cast<PatchCableSet*>(c);
		doc.patch_cables.reserve(pcs->numPatchCables);
		for (int32_t i = 0; i < pcs->numPatchCables; i++) {
			PatchCable& cable = pcs->patchCables[i];
			doc.patch_cables.push_back(DocPatchCable{
			    .from = cable.from,
			    .destination = cable.destinationParamDescriptor,
			    .polarity = cable.polarity,
			    .amount = toDocAutoParam(cable.param),
			});
		}
	}
	return doc;
}

void fromDocParamManager(const DocParamManager& doc, ParamManager& paramManager) {
	if (doc.unpatched) {
		if (ParamCollection* c = paramManager.summaries[0].paramCollection; isUnpatchedSetSlot(c)) {
			fromDocParamSet(*doc.unpatched, *static_cast<ParamSet*>(c));
		}
	}
	if (doc.patched) {
		if (ParamCollection* c = paramManager.summaries[1].paramCollection;
		    c != nullptr && c->getParamKind() == Kind::PATCHED) {
			fromDocParamSet(*doc.patched, *static_cast<ParamSet*>(c));
		}
	}
	if (doc.expression) {
		if (ExpressionParamSet* e = paramManager.getExpressionParamSet(); e != nullptr) {
			fromDocParamSet(*doc.expression, *e);
		}
	}
	// Patch cables: refill the cable set faithfully (count + each saved field). setupPatching (the
	// engine's post-load step) rebuilds the runtime rangeAdjustmentPointers.
	if (ParamCollection* c = paramManager.summaries[2].paramCollection;
	    c != nullptr && c->getParamKind() == Kind::PATCH_CABLE) {
		PatchCableSet* pcs = static_cast<PatchCableSet*>(c);
		int32_t count = std::min(static_cast<int32_t>(doc.patch_cables.size()), int32_t{kMaxNumPatchCables});
		pcs->numPatchCables = count;
		for (int32_t i = 0; i < count; i++) {
			const DocPatchCable& dc = doc.patch_cables[i];
			pcs->patchCables[i].from = dc.from;
			pcs->patchCables[i].destinationParamDescriptor = dc.destination;
			pcs->patchCables[i].polarity = dc.polarity;
			fromDocAutoParam(dc.amount, pcs->patchCables[i].param);
		}
	}
}

} // namespace deluge::project
