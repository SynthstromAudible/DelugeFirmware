/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "model/project_bridge/param_selection.h"

#include "definitions_cxx.hpp"
#include "model/model_stack.h"
#include "modulation/params/param_collection.h"
#include "util/misc.h"

namespace deluge::bridge {

namespace params = deluge::modulation::params;
using deluge::project::PluginParam;

namespace {

constexpr RuntimeParam kNoSelection{params::Kind::NONE, static_cast<int32_t>(params::kNoParamID)};

/// Whether @p value is one of params::Kind's values.
bool isKind(int32_t value) {
	return value >= util::to_underlying(params::Kind::NONE) && value <= util::to_underlying(params::Kind::ARP);
}

} // namespace

std::optional<PluginParam> pluginParamOf(params::Kind kind, int32_t index) {
	if (kind == params::Kind::NONE) {
		return std::nullopt;
	}
	const OwnerPlugin plugin =
	    kind == params::Kind::UNPATCHED_GLOBAL ? OwnerPlugin::GLOBAL_EFFECTABLE : OwnerPlugin::INSTRUMENT;
	const std::optional<deluge::project::ParamTarget> target = targetOf(plugin, kind, index);
	if (!target) {
		return std::nullopt;
	}
	return PluginParam{.plugin = plugin, .target = *target};
}

std::optional<RuntimeParam> selectionPairOf(const std::optional<PluginParam>& selection) {
	return selection ? runtimeParamOf(selection->plugin, selection->target) : std::nullopt;
}

RuntimeParam runtimePairOf(const std::optional<PluginParam>& selection) {
	return selectionPairOf(selection).value_or(kNoSelection);
}

std::optional<PluginParam> clipSelectionOfFile(int32_t kind, int32_t id) {
	if (id == kNoSelection.index || !isKind(kind)) {
		return std::nullopt;
	}
	const auto read = static_cast<params::Kind>(kind);
	if (read == params::Kind::NONE || (read == params::Kind::EXPRESSION && id >= kNumExpressionDimensions)) {
		return deluge::project::midi_cc_param(id);
	}
	return pluginParamOf(read, id);
}

std::optional<PluginParam> songSelectionOfFile(int32_t kind, int32_t id) {
	if (id == kNoSelection.index || !isKind(kind)) {
		return std::nullopt;
	}
	return pluginParamOf(static_cast<params::Kind>(kind), id);
}

ModelStackWithAutoParam* autoParamOf(ModelStackWithThreeMainThings* stack, OwnerPlugin owner,
                                     deluge::project::ParamTarget target, bool forEdit) {
	const std::optional<RuntimeParam> runtime = runtimeParamOf(owner, target);
	if (stack == nullptr || !runtime) {
		return nullptr;
	}
	switch (runtime->kind) {
	case params::Kind::PATCHED:
		return stack->getPatchedAutoParamFromId(runtime->index);
	case params::Kind::UNPATCHED_SOUND:
	case params::Kind::UNPATCHED_GLOBAL:
		return stack->getUnpatchedAutoParamFromId(runtime->index);
	case params::Kind::PATCH_CABLE:
		return stack->getPatchCableAutoParamFromId(runtime->index, forEdit);
	case params::Kind::EXPRESSION:
		return stack->getExpressionAutoParamFromID(runtime->index);
	case params::Kind::ARP:
		return stack->getArpAutoParamFromId(runtime->index);
	default:
		return nullptr;
	}
}

bool isCableCollection(ParamCollection* collection) {
	return collection != nullptr && collection->getParamKind() == params::Kind::PATCH_CABLE;
}

} // namespace deluge::bridge
