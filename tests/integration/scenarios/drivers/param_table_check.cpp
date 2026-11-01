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

// param_table_check — the engines' param tables agree with the runtime: every row's file name is the runtime's
// name for its runtime index, every runtime param has a row, and each row's scope, bipolarity and default are the
// runtime's (a default is what the engine's own init gives a fresh instance).
// Exit: 0 agree; 3 a row disagrees (each printed).

#include "effects/global_effectable/naming.h"
#include "effects/global_effectable/params.h"
#include "instruments/deluge/cables.h"
#include "instruments/deluge/naming.h"
#include "instruments/deluge/params.h"
#include "model/global_effectable/global_effectable.h"
#include "model/project_bridge/param_ids.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/drivers/param_defaults_reference.h"
#include "scenarios/drivers/param_names_reference.h"
#include "util/functions.h"

#include <cstdio>
#include <span>
#include <string>
#include <string_view>

namespace {

namespace params = deluge::modulation::params;
using deluge::plugin::ParamId;
using deluge::plugin::ParamInfo;
using deluge::plugin::ParamScope;
using deluge::scenarios::driver::Io;

struct Checker {
	Io io;
	int disagreements = 0;

	void fail(const char* table, const ParamInfo& row, const char* what) {
		std::fprintf(io.err, "%s %u %.*s: %s\n", table, row.id.value, static_cast<int>(row.file_name.size()),
		             row.file_name.data(), what);
		++disagreements;
	}

	/// One row against the runtime's name, scope and bipolarity for (@p kind, @p index).
	void row(const char* table, const ParamInfo& info, params::Kind kind, int32_t index, bool perVoice) {
		const int32_t named = kind == params::Kind::PATCHED ? index : params::UNPATCHED_START + index;
		std::string_view name = params::paramNameForFile(kind, static_cast<params::ParamType>(named));
		const bool unnamed = name == "none";
		if (!unnamed && name != info.file_name) {
			fail(table, info, "the runtime names it differently");
		}
		if (unnamed && info.id != deluge::global_effectable::param::kTempo) {
			fail(table, info, "the runtime has no name for it");
		}
		if ((info.scope == ParamScope::PER_VOICE) != perVoice) {
			fail(table, info, "its scope is not the runtime's");
		}
		if (info.bipolar != params::isParamBipolar(kind, index)) {
			fail(table, info, "its bipolarity is not the runtime's");
		}
		if (deluge::bridge::paramIdOf(kind, index) != info.id) {
			fail(table, info, "the boundary map gives the runtime index another id");
		}
		if (deluge::bridge::runtimeIndexOf(kind, info.id) != index) {
			fail(table, info, "the boundary map gives the id another runtime index");
		}
		const deluge::plugin::ParamNaming& naming =
		    kind == params::Kind::UNPATCHED_GLOBAL ? deluge::global_effectable::kNaming : deluge::instrument::kNaming;
		if (!unnamed && naming.id_of({name}) != std::optional<ParamId>{info.id}) {
			fail(table, info, "its naming does not give the runtime's name this id");
		}
	}
};

/// The instrument's cable words against the runtime's: each source's name, and every packed cable word the runtime
/// can form (a param with one, two or three sources) round-tripping through its depth id.
void checkCables(Checker& check) {
	for (uint8_t s = 0; s <= static_cast<uint8_t>(PatchSource::NONE); ++s) {
		std::string_view ours = deluge::instrument::to_string(static_cast<deluge::instrument::PatchSource>(s));
		std::string_view runtime = sourceToString(static_cast<PatchSource>(s));
		if (ours != runtime) {
			std::fprintf(check.io.err, "cable source %u: instrument %.*s, runtime %.*s\n", s,
			             static_cast<int>(ours.size()), ours.data(), static_cast<int>(runtime.size()), runtime.data());
			++check.disagreements;
		}
	}
	auto roundTrips = [&](uint32_t word) {
		if (deluge::bridge::cableWordOf(deluge::bridge::cableDepthIdOf(word)) != word) {
			std::fprintf(check.io.err, "cable word %08x does not round-trip\n", word);
			++check.disagreements;
		}
	};
	for (uint32_t p = 0; p < 256; ++p) {
		for (uint32_t s1 = 0; s1 <= 15; ++s1) {
			roundTrips(p | s1 << 8 | 0xFFFF0000u);
			for (uint32_t s2 = 0; s2 <= 15; ++s2) {
				roundTrips(p | s1 << 8 | s2 << 16 | 0xFF000000u);
			}
			roundTrips(p | s1 << 8 | s1 << 16 | (15 - s1) << 24);
		}
	}
}

/// paramNameForFile and fileStringToParam against the names they gave before the tables named the params: every kind,
/// both file vocabularies, every runtime index; and every name, "range" and an unknown name read back.
void checkNames(Checker& check) {
	auto expected = [](int32_t kind, bool midiFollow, int32_t index) -> std::string_view {
		for (const param_names_reference::Name& n : param_names_reference::kNames) {
			if (n.kind == kind && n.midi_follow == midiFollow && n.index == index) {
				return n.name;
			}
		}
		return "none";
	};
	for (int32_t kind = 0; kind <= static_cast<int32_t>(params::Kind::EXPRESSION); ++kind) {
		for (bool midiFollow : {false, true}) {
			for (int32_t p = 0; p < params::kUnpatchedAndPatchedMaximum; ++p) {
				std::string_view live = params::paramNameForFile(static_cast<params::Kind>(kind), p, midiFollow);
				std::string_view want = expected(kind, midiFollow, p);
				if (live != want) {
					std::fprintf(check.io.err, "name kind %d mf %d index %d: %.*s, was %.*s\n", kind, midiFollow, p,
					             static_cast<int>(live.size()), live.data(), static_cast<int>(want.size()),
					             want.data());
					++check.disagreements;
				}
			}
		}
	}
	auto readBack = [&](int32_t kind, bool allowPatched, std::string_view name) {
		std::string text{name};
		params::ParamType got = params::fileStringToParam(static_cast<params::Kind>(kind), text.c_str(), allowPatched);
		// The first index the oracle names so, from the first index this read may return
		params::ParamType want =
		    name == "range" ? util::to_underlying(params::PLACEHOLDER_RANGE) : util::to_underlying(params::GLOBAL_NONE);
		for (int32_t p = allowPatched ? 0 : params::UNPATCHED_START; p < params::kUnpatchedAndPatchedMaximum; ++p) {
			if (expected(kind, false, p) == name) {
				want = p;
				break;
			}
		}
		if (got != want) {
			std::fprintf(check.io.err, "read kind %d patched %d %.*s: %d, want %d\n", kind, allowPatched,
			             static_cast<int>(name.size()), name.data(), got, want);
			++check.disagreements;
		}
	};
	for (int32_t kind = 0; kind <= static_cast<int32_t>(params::Kind::EXPRESSION); ++kind) {
		for (bool allowPatched : {false, true}) {
			for (const param_names_reference::Name& n : param_names_reference::kNames) {
				readBack(kind, allowPatched, n.name);
			}
			readBack(kind, allowPatched, "range");
			readBack(kind, allowPatched, "nonsense");
		}
	}
}

/// The default the engines' init gave param @p id of @p table before it read the tables.
int32_t recordedDefault(param_defaults_reference::Table table, ParamId id) {
	for (const param_defaults_reference::Default& d : param_defaults_reference::kDefaults) {
		if (d.table == table && d.id == id.value) {
			return d.value;
		}
	}
	return 0;
}

/// Each row's default, and what the engine's init gives a fresh manager's param, against the recorded default.
void checkDefaults(Checker& check) {
	ParamManagerForTimeline sound;
	sound.setupWithPatching();
	Sound::initParams(&sound);
	for (const ParamInfo& info : deluge::instrument::kParams) {
		auto slot = *deluge::instrument::runtime_slot(info.id);
		ParamSet* set = slot.set == deluge::instrument::RuntimeSet::PATCHED
		                    ? static_cast<ParamSet*>(sound.getPatchedParamSet())
		                    : static_cast<ParamSet*>(sound.getUnpatchedParamSet());
		int32_t value = set->docForParam(slot.index)->current_value;
		int32_t recorded = recordedDefault(param_defaults_reference::Table::INSTRUMENT, info.id);
		if (value != recorded || info.default_value != recorded) {
			std::fprintf(check.io.err, "default instrument %u %.*s: table %d, runtime %d, recorded %d\n", info.id.value,
			             static_cast<int>(info.file_name.size()), info.file_name.data(), info.default_value, value,
			             recorded);
			++check.disagreements;
		}
	}
	ParamManagerForTimeline global;
	global.setupUnpatched();
	GlobalEffectable::initParams(&global);
	for (const ParamInfo& info : deluge::global_effectable::kParams) {
		int32_t value = global.getUnpatchedParamSet()
		                    ->docForParam(*deluge::global_effectable::runtime_index(info.id))
		                    ->current_value;
		int32_t recorded = recordedDefault(param_defaults_reference::Table::GLOBAL_EFFECTABLE, info.id);
		if (value != recorded || info.default_value != recorded) {
			std::fprintf(check.io.err, "default global-effectable %u %.*s: table %d, runtime %d, recorded %d\n",
			             info.id.value, static_cast<int>(info.file_name.size()), info.file_name.data(),
			             info.default_value, value, recorded);
			++check.disagreements;
		}
	}
}

int param_table_check_main(int /*argc*/, char** /*argv*/, Io io) {
	Checker check{io};
	using namespace deluge::instrument;
	for (const ParamInfo& info : kParams) {
		RuntimeSlot slot = *runtime_slot(info.id);
		const bool patched = slot.set == RuntimeSet::PATCHED;
		check.row("instrument", info, patched ? params::Kind::PATCHED : params::Kind::UNPATCHED_SOUND, slot.index,
		          patched && slot.index < params::FIRST_GLOBAL);
	}
	for (const ParamInfo& info : deluge::global_effectable::kParams) {
		check.row("global-effectable", info, params::Kind::UNPATCHED_GLOBAL,
		          *deluge::global_effectable::runtime_index(info.id), false);
	}
	// Every runtime param has a row.
	auto cover = [&](const char* table, std::span<const ParamInfo> rows, params::Kind kind, int32_t count) {
		for (int32_t i = 0; i < count; ++i) {
			if (deluge::plugin::find_param(rows, deluge::bridge::paramIdOf(kind, i)) == nullptr) {
				std::fprintf(io.err, "%s: runtime param %d of kind %d has no row\n", table, i, static_cast<int>(kind));
				++check.disagreements;
			}
		}
	};
	cover("instrument", kParams, params::Kind::PATCHED, params::kNumParams);
	cover("instrument", kParams, params::Kind::UNPATCHED_SOUND, params::UNPATCHED_SOUND_MAX_NUM);
	cover("global-effectable", deluge::global_effectable::kParams, params::Kind::UNPATCHED_GLOBAL,
	      params::UNPATCHED_GLOBAL_MAX_NUM);
	checkCables(check);
	checkNames(check);
	checkDefaults(check);
	std::fprintf(io.out, "%d disagreement(s)\n", check.disagreements);
	return check.disagreements == 0 ? 0 : 3;
}

} // namespace

DELUGE_DRIVER_SCENARIO(param_table_check, param_table_check_main)
