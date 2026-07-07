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
		// A clip's node storage is tree-resident only if it has been relocated here (keyed by serialId); otherwise
		// nullptr → the caller falls back to the engine store. The kind selects the collection within the clip's
		// DocParamManager. Patch cables / MIDI resolve in a later slice.
		DocParamManager* mgr = clip_params.find(address.owner.clip_serial_id);
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
		if (set->has_value() && address.param_id >= 0
		    && address.param_id < static_cast<int32_t>((*set)->params.size())) {
			return &(*set)->params[address.param_id];
		}
		return nullptr;
	}
	}
	return nullptr;
}

} // namespace deluge::project
