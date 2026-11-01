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

// param_target_check — the host names params by target: every runtime param of each owner plugin maps to a target
// and back through the bridge, and every table that names params by target gives, cell by cell, the runtime param
// its recorded characterisation holds.
// Exit: 0 agree; 3 a check disagrees (each printed).

#include "definitions_cxx.hpp"
#include "model/project_bridge/param_ids.h"
#include "modulation/params/param.h"
#include "modulation/patch/patch_cable.h"
#include "scenarios/drivers/driver.h"

#include <cstdio>
#include <optional>

namespace {

namespace params = deluge::modulation::params;
using deluge::bridge::OwnerPlugin;
using deluge::bridge::RuntimeParam;
using deluge::project::ParamTarget;
using deluge::scenarios::driver::Io;

struct Checker {
	Io io;
	int disagreements = 0;

	void fail(const char* what, int a, int b, int c) {
		std::fprintf(io.err, "%s: %d %d %d\n", what, a, b, c);
		++disagreements;
	}
};

/// For each owner plugin and every runtime param it can have, targetOf then runtimeParamOf is the identity.
void checkRoundTrip(Checker& check) {
	auto trip = [&](OwnerPlugin owner, params::Kind kind, int32_t index) {
		std::optional<ParamTarget> target = deluge::bridge::targetOf(owner, kind, index);
		std::optional<RuntimeParam> back = target ? deluge::bridge::runtimeParamOf(owner, *target) : std::nullopt;
		if (!target || !back || back->kind != kind || back->index != index) {
			check.fail("round trip", static_cast<int>(owner), static_cast<int>(kind), index);
		}
	};
	for (int32_t p = 0; p < params::kNumParams; ++p) {
		trip(OwnerPlugin::INSTRUMENT, params::Kind::PATCHED, p);
	}
	for (int32_t u = 0; u < params::UNPATCHED_SOUND_MAX_NUM; ++u) {
		trip(OwnerPlugin::INSTRUMENT, params::Kind::UNPATCHED_SOUND, u);
	}
	for (int32_t u = 0; u < params::UNPATCHED_GLOBAL_MAX_NUM; ++u) {
		trip(OwnerPlugin::GLOBAL_EFFECTABLE, params::Kind::UNPATCHED_GLOBAL, u);
	}
	for (OwnerPlugin owner : {OwnerPlugin::INSTRUMENT, OwnerPlugin::GLOBAL_EFFECTABLE}) {
		for (int32_t cc = 0; cc <= 122; ++cc) {
			trip(owner, params::Kind::MIDI, cc);
		}
		for (int32_t e = 0; e < kNumExpressionDimensions; ++e) {
			trip(owner, params::Kind::EXPRESSION, e);
		}
	}
	// Every cable word the runtime forms: a patched param with one or two sources, and three (a range of a range).
	constexpr auto kSources = static_cast<uint32_t>(kNumPatchSources);
	for (uint32_t p = 0; p < params::kNumParams; ++p) {
		for (uint32_t s1 = 0; s1 < kSources; ++s1) {
			trip(OwnerPlugin::INSTRUMENT, params::Kind::PATCH_CABLE, static_cast<int32_t>(p | s1 << 8 | 0xFFFF0000u));
			for (uint32_t s2 = 0; s2 < kSources; ++s2) {
				trip(OwnerPlugin::INSTRUMENT, params::Kind::PATCH_CABLE,
				     static_cast<int32_t>(p | s1 << 8 | s2 << 16 | 0xFF000000u));
			}
			trip(OwnerPlugin::INSTRUMENT, params::Kind::PATCH_CABLE,
			     static_cast<int32_t>(p | s1 << 8 | s1 << 16 | (kSources - 1 - s1) << 24));
		}
	}
	// An id names a param only in its owner's plugin: a global effectable has no patched params or cables, an
	// instrument no global-effectable params.
	if (deluge::bridge::targetOf(OwnerPlugin::GLOBAL_EFFECTABLE, params::Kind::PATCHED, 0)) {
		check.fail("global effectable resolved a patched param", 0, 0, 0);
	}
	if (deluge::bridge::targetOf(OwnerPlugin::GLOBAL_EFFECTABLE, params::Kind::PATCH_CABLE, 0x00FFFF00)) {
		check.fail("global effectable resolved a cable", 0, 0, 0);
	}
	if (deluge::bridge::targetOf(OwnerPlugin::INSTRUMENT, params::Kind::UNPATCHED_GLOBAL, 0)) {
		check.fail("instrument resolved a global-effectable param", 0, 0, 0);
	}
	if (deluge::bridge::runtimeParamOf(OwnerPlugin::GLOBAL_EFFECTABLE, deluge::project::engine_target(0))) {
		check.fail("global effectable resolved id 0", 0, 0, 0);
	}
}

int param_target_check_main(int /*argc*/, char** /*argv*/, Io io) {
	Checker check{io};
	checkRoundTrip(check);
	std::fprintf(io.out, "%d disagreement(s)\n", check.disagreements);
	return check.disagreements == 0 ? 0 : 3;
}

} // namespace

DELUGE_DRIVER_SCENARIO(param_target_check, param_target_check_main)
