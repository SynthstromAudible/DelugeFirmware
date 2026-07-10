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

#pragma once

#include "deluge-project/param_address.h"    // OwnerId
#include "deluge-project/param_automation.h" // DocParamManager
#include "util/containers.h"                 // deluge::fast_map
#include <compare>
#include <cstdint>
#include <optional>

namespace deluge::project {

/// Composite identity of one instrument-shared automation store, mirroring `ParamBackup`'s
/// `(ModControllableAudio*, Clip*)` key in engine-pointer-free serial terms: the `owner` is an
/// `INSTRUMENT` (SoundInstrument / Kit, by Output::id) or a `DRUM` (SoundDrum, by Drum::id),
/// and `clip_id` is which clip's override this is — nullopt = the owner's shared default (the
/// null-Clip backup entry). Ordered so it can key the `fast_map` (a `std::map`) store.
struct BackedParamKey {
	OwnerId owner{};
	std::optional<uint32_t> clip_id{};

	auto operator<=>(const BackedParamKey&) const = default;
};

/// Per-(owner, clip) automation node storage for the params that migrate between a clip's live manager, the
/// instrument/drum shared default, and NoteRows (synth, kit clip-level FX, and per-drum params). This is the
/// persistent-tree form of `ParamBackup`: under the single-store model the nodes live here (addressed by
/// `Song::resolveParam` via a `ParamAddress`), while the engine `ParamManager`/`ParamBackup` retains only the
/// interpolation cursors. Keyed by stable serial ids, so it is unaffected by clip reorders and by
/// `stealParamCollectionsFrom` moving the engine cursor-holder. A component of `deluge::project::Song`.
struct BackedParams {
	deluge::fast_map<BackedParamKey, DocParamManager> by_owner;

	/// The store for exactly (owner, clip_id), or nullptr if none is tree-resident.
	DocParamManager* find(const OwnerId& owner, std::optional<uint32_t> clipSerial) {
		auto it = by_owner.find(BackedParamKey{owner, clipSerial});
		return it == by_owner.end() ? nullptr : &it->second;
	}

	/// The store for (owner, clip_id), creating an empty one if absent.
	DocParamManager& getOrCreate(const OwnerId& owner, std::optional<uint32_t> clipSerial) {
		return by_owner[BackedParamKey{owner, clipSerial}];
	}

	/// Drop the store for (owner, clip_id).
	void remove(const OwnerId& owner, std::optional<uint32_t> clipSerial) {
		by_owner.erase(BackedParamKey{owner, clipSerial});
	}

	/// Drop EVERY store for `owner` (all clip overrides). Entries for one owner are contiguous in the ordered map
	/// (BackedParamKey compares owner before clip_id), so this is a single range-erase. Called on
	/// instrument/drum death (retire-on-death) to keep the tree a faithful mirror of live owners — safe only once
	/// the owner's engine ParamManagers are gone, so nothing is still bound to these stores.
	void removeOwner(const OwnerId& owner) {
		auto it = by_owner.lower_bound(BackedParamKey{owner, std::nullopt});
		while (it != by_owner.end() && it->first.owner == owner) {
			it = by_owner.erase(it);
		}
	}
};

} // namespace deluge::project
