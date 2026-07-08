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

#include "deluge-project/song.h"

namespace deluge::project {

Song::Song() = default;

namespace {
/// Resolve one param slot within a tree-resident DocParamManager: the address's params::Kind selects the
/// collection, param_id indexes it. nullptr if the manager/collection/index isn't tree-resident. Patch cables /
/// MIDI resolve in a later slice.
DocAutoParam* slotInManager(DocParamManager* mgr, const ParamAddress& address) {
	if (mgr == nullptr) {
		return nullptr;
	}
	std::optional<DocParamSet>* set = nullptr;
	switch (address.kind) {
	case deluge::modulation::params::Kind::UNPATCHED_SOUND:
	case deluge::modulation::params::Kind::UNPATCHED_GLOBAL:
		set = &mgr->unpatched;
		break;
	case deluge::modulation::params::Kind::PATCHED:
		set = &mgr->patched;
		break;
	case deluge::modulation::params::Kind::EXPRESSION:
		set = &mgr->expression;
		break;
	default:
		return nullptr; // PATCH_CABLE / MIDI / etc. — not tree-resident yet
	}
	if (set->has_value() && address.param_id >= 0 && address.param_id < static_cast<int32_t>((*set)->params.size())) {
		return &(*set)->params[address.param_id];
	}
	return nullptr;
}

/// Resolve a patch-cable amount lane within a tree-resident manager by IDENTITY (S4b). The address's param_id is
/// the packed (destination + source) word `PatchCableSet::getParamId` produces, so recompute that word from each
/// stored cable's (destination, from) — via the same `ParamDescriptor::addSource` the engine uses — and match.
/// Identity, not index: the engine reorders cables at runtime (setupPatching), so only (from, dest) is stable.
DocAutoParam* patchCableInManager(DocParamManager* mgr, int32_t paramId) {
	if (mgr == nullptr) {
		return nullptr;
	}
	for (DocPatchCable& cable : mgr->patch_cables) {
		ParamDescriptor packed = cable.destination;
		packed.addSource(cable.from);
		if (static_cast<int32_t>(packed.data) == paramId) {
			return &cable.amount;
		}
	}
	return nullptr;
}

/// Resolve a MIDI-CC lane within a tree-resident manager by IDENTITY (S4c). The address's param_id is the CC
/// number (the MIDIParamVector keys by cc); scan the manager's per-clip CC store for it. Identity, not index: a
/// newly-learned CC reorders the engine vector, so only cc is stable.
DocAutoParam* midiParamInManager(DocParamManager* mgr, int32_t cc) {
	if (mgr == nullptr) {
		return nullptr;
	}
	for (DocMidiParam& entry : mgr->midi_params) {
		if (entry.cc == cc) {
			return &entry.param;
		}
	}
	return nullptr;
}
} // namespace

DocAutoParam* Song::resolveParam(const ParamAddress& address) {
	switch (address.owner.kind) {
	case OwnerId::Kind::SONG: {
		// Song-global params are unpatched-only; their live authority is global_fx.params.unpatched. (The kind is
		// carried for future collections but is unambiguous here — the caller only builds a SONG address for the
		// song's unpatched set.)
		if (global_fx.params.unpatched && address.param_id >= 0
		    && address.param_id < static_cast<int32_t>(global_fx.params.unpatched->params.size())) {
			return &global_fx.params.unpatched->params[address.param_id];
		}
		return nullptr;
	}
	case OwnerId::Kind::CLIP: {
		// A clip's node storage is tree-resident either by its persisted serialId (clip_params) or — for a
		// not-yet-saved clip (S5-b) — by a transient session id (session_clip_params). A MIDI_OUT clip's CC
		// automation (S4c) resolves by cc within the same per-clip store; the audio-clip ParamSet resolves by index.
		DocParamManager* mgr = address.owner.is_session ? session_clip_params.find(address.owner.serial_id)
		                                                : clip_params.find(address.owner.serial_id);
		if (address.kind == deluge::modulation::params::Kind::MIDI) {
			return midiParamInManager(mgr, address.param_id);
		}
		return slotInManager(mgr, address);
	}
	case OwnerId::Kind::INSTRUMENT:
	case OwnerId::Kind::DRUM: {
		// Instrument-shared / per-drum params, keyed by (owner, clip) — the persistent form of ParamBackup. Patch
		// cables (S4b) resolve by cable identity within the manager; the ParamSets resolve by index. nullptr when
		// the owner/lane isn't tree-resident → engine fallback.
		DocParamManager* mgr = backed_params.find(address.owner, address.clip_serial);
		if (address.kind == deluge::modulation::params::Kind::PATCH_CABLE) {
			return patchCableInManager(mgr, address.param_id);
		}
		return slotInManager(mgr, address);
	}
	}
	return nullptr;
}

} // namespace deluge::project
