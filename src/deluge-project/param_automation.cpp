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
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include "storage/storage_manager.h"
#include "util/c_string.h"  // intToHex
#include "util/functions.h" // sourceToString
#include <algorithm>
#include <cstdint>

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

// --- Generic serialization (Phase 4) ---

void writeDocAutoParam(const DocAutoParam& doc, Serializer& out, bool writeAutomation) {
	// Mirrors AutoParam::writeToFile (no override): "0x" + currentValue(8 hex) + per-node value(8 hex)
	// + pos|interpolated<<31 (8 hex).
	char buffer[9];
	out.write("0x");
	intToHex(doc.current_value, buffer);
	out.write(buffer);
	if (writeAutomation) {
		for (const ParamNode& node : doc.nodes) {
			intToHex(node.value, buffer);
			out.write(buffer);
			uint32_t pos = node.pos;
			if (node.interpolated) {
				pos |= static_cast<uint32_t>(1) << 31;
			}
			intToHex(pos, buffer);
			out.write(buffer);
		}
	}
}

void serializePatchCables(const std::vector<DocPatchCable>& cables, Serializer& out, bool writeAutomation) {
	// Byte-for-byte reproduction of PatchCableSet::writePatchCablesToFile. The early-out matches the
	// engine's `if (!numPatchCables) return;`.
	if (cables.empty()) {
		return;
	}
	namespace params = deluge::modulation::params;
	out.writeArrayStart("patchCables");
	for (const DocPatchCable& cable : cables) {
		// Depth-controlling (range) cables aren't written at top level; they're nested below the cable
		// whose depth they control.
		if (!cable.destination.isJustAParam()) {
			continue;
		}
		out.writeOpeningTagBeginning("patchCable", true);
		out.writeAttribute("source", sourceToString(cable.from));
		out.writeAttribute("destination", params::paramNameForFile(params::Kind::UNPATCHED_SOUND,
		                                                           cable.destination.getJustTheParam()));
		out.writeAttribute("polarity", polarityToString(cable.polarity).data());
		out.insertCommaIfNeeded();
		out.write("\n");
		out.printIndents();
		out.writeTagNameAndSeperator("amount");
		out.write("\"");
		writeDocAutoParam(cable.amount, out, writeAutomation);
		out.write("\"");

		// Find the cable(s) whose destination is this cable's param-with-this-source — they control its
		// depth. NB: like the engine, the nested cable writes the OUTER cable's polarity (a quirk
		// preserved for byte-identity).
		ParamDescriptor controllerDescriptor = cable.destination;
		controllerDescriptor.addSource(cable.from);
		bool anyDepthControllingCablesFound = false;
		for (const DocPatchCable& d : cables) {
			if (d.destination == controllerDescriptor) {
				if (!anyDepthControllingCablesFound) {
					anyDepthControllingCablesFound = true;
					out.writeOpeningTagEnd();
					out.writeArrayStart("depthControlledBy");
				}
				out.writeOpeningTagBeginning("patchCable", true);
				out.writeAttribute("source", sourceToString(d.from));
				out.writeAttribute("polarity", polarityToString(cable.polarity).data());
				out.insertCommaIfNeeded();
				out.write("\n");
				out.printIndents();
				out.writeTagNameAndSeperator("amount");
				out.write("\"");
				writeDocAutoParam(d.amount, out, writeAutomation);
				out.write("\"");
				out.closeTag(true);
			}
		}
		if (anyDepthControllingCablesFound) {
			out.writeArrayEnding("depthControlledBy");
			out.writeClosingTag("patchCable", true, true);
		}
		else {
			out.closeTag(true);
		}
	}
	out.writeArrayEnding("patchCables");
}

} // namespace deluge::project
