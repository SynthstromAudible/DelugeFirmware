/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "processing/sound/mod_matrix_host.h"

#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/sound.h"

ModMatrixHost::ModMatrixHost(Sound& sound, ParamManager& paramManager)
    : sound_(sound), paramManager_(paramManager), cables_(*paramManager.getPatchCableSet()) {
}

std::optional<size_t> ModMatrixHost::find(const deluge::synth::CableRecord& cable) const {
	return cables_.records().find(cable.source, cable.target);
}

CableEdit ModMatrixHost::finish(deluge::instrument::CableDelta delta, deluge::project::DocAutoParam lane) {
	delta.records_after = cables_.records();
	if (const std::optional<size_t> i =
	        find(delta.op == deluge::instrument::CableDelta::Op::REMOVE ? delta.before : delta.after)) {
		delta.index = static_cast<uint8_t>(*i);
	}
	return {std::move(delta), std::move(lane)};
}

std::optional<CableEdit> ModMatrixHost::addCable(deluge::synth::CableRecord cable) {
	std::optional<deluge::instrument::CableDelta> delta = deluge::instrument::begin_add(cables_.records(), cable);
	if (!delta || cables_.appendCable(cable) == nullptr) {
		return std::nullopt;
	}
	cables_.setupPatching(sound_, paramManager_);
	return finish(std::move(*delta));
}

std::optional<CableEdit> ModMatrixHost::removeCable(size_t index) {
	std::optional<deluge::instrument::CableDelta> delta = deluge::instrument::begin_remove(cables_.records(), index);
	if (!delta) {
		return std::nullopt;
	}
	deluge::project::DocAutoParam lane = *cables_.docForParam(static_cast<int32_t>(index));
	cables_.deletePatchCable(sound_, paramManager_, static_cast<uint8_t>(index));
	return finish(std::move(*delta), std::move(lane));
}

std::optional<CableEdit> ModMatrixHost::setPolarity(size_t index, deluge::synth::CablePolarity polarity) {
	std::optional<deluge::instrument::CableDelta> delta =
	    deluge::instrument::begin_set_polarity(cables_.records(), index, polarity);
	if (!delta) {
		return std::nullopt;
	}
	cables_.setPolarity(static_cast<uint8_t>(index), static_cast<Polarity>(polarity));
	cables_.setupPatching(sound_, paramManager_);
	return finish(std::move(*delta));
}

std::optional<CableEdit> ModMatrixHost::reroute(size_t index, deluge::synth::CableTarget target) {
	std::optional<deluge::instrument::CableDelta> delta =
	    deluge::instrument::begin_reroute(cables_.records(), index, target);
	if (!delta) {
		return std::nullopt;
	}
	cables_.retarget(static_cast<uint8_t>(index), target);
	cables_.setupPatching(sound_, paramManager_);
	return finish(std::move(*delta));
}

void ModMatrixHost::apply(CableEdit& edit, TimeType time) {
	using Op = deluge::instrument::CableDelta::Op;
	using deluge::instrument::CableSide;
	const deluge::instrument::CableDelta& delta = edit.delta;
	const CableSide side = time == AFTER ? CableSide::AFTER : CableSide::BEFORE;
	// A rerouted cable changes identity across the edit, so its depth is carried from one identity to the other; an
	// added or removed one takes its depth with it while it is out of the clip.
	const deluge::synth::CableRecord& leaving = side == CableSide::AFTER ? delta.before : delta.after;
	const deluge::synth::CableRecord& arriving = side == CableSide::AFTER ? delta.after : delta.before;
	if (delta.op == Op::REROUTE || !delta.present_at(side)) {
		if (const std::optional<size_t> i = find(delta.op == Op::REROUTE ? leaving : delta.cable())) {
			edit.lane = *cables_.docForParam(static_cast<int32_t>(*i));
		}
	}
	const bool restores = delta.op == Op::REROUTE || (delta.present_at(side) && delta.op != Op::SET_POLARITY);
	const CableDepth restored{delta.op == Op::REROUTE ? arriving : delta.cable(), edit.lane};
	cables_.replaceRecords(sound_, paramManager_, side == CableSide::AFTER ? delta.records_after : delta.records_before,
	                       restores ? &restored : nullptr);
	// The param the cable goes to recomputes with its depth back (or gone).
	sound_.recalculatePatchingToParam(delta.cable().target.param_index(), &paramManager_);
	if (delta.op == Op::REROUTE) {
		sound_.recalculatePatchingToParam(delta.after.target.param_index(), &paramManager_);
	}
}

std::optional<CableEdit> ModMatrixHost::ensureCable(PatchSource source, deluge::synth::CableTarget target) {
	if (!deluge::instrument::needs_cable(cables_.records(), source, target)) {
		return std::nullopt;
	}
	return addCable(
	    {source, target, static_cast<deluge::synth::CablePolarity>(PatchCable::getDefaultPolarity(source))});
}

std::optional<CableEdit> ModMatrixHost::settleDepth(size_t index, bool clipActive) {
	if (index >= cables_.records().size()) {
		return std::nullopt;
	}
	const bool holdsSomething =
	    cables_.patchCables[index].param.containsSomething(*cables_.docForParam(static_cast<int32_t>(index)), 0);
	if (!deluge::instrument::settles(holdsSomething, clipActive)) {
		return std::nullopt;
	}
	return removeCable(index);
}
