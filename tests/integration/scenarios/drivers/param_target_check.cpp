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
#include "gui/views/automation_view.h"
#include "gui/views/performance_view.h"
#include "io/midi/midi_follow.h"
#include "model/project_bridge/param_ids.h"
#include "modulation/params/param.h"
#include "modulation/params/param_targets.h"
#include "modulation/patch/patch_cable.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/drivers/midi_follow_xml_reference.h"
#include "scenarios/drivers/param_target_tables_reference.h"
#include "storage/storage_manager.h"

#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <vector>

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

using Table = param_target_tables_reference::Table;

/// The recorded cells of @p table, in the order recorded.
std::vector<param_target_tables_reference::Cell> recorded(Table table) {
	std::vector<param_target_tables_reference::Cell> cells;
	for (const auto& cell : param_target_tables_reference::kCells) {
		if (cell.table == table) {
			cells.push_back(cell);
		}
	}
	return cells;
}

/// The recorded cell @p number of @p table.
param_target_tables_reference::Cell recordedCell(Table table, int32_t number) {
	for (const auto& cell : param_target_tables_reference::kCells) {
		if (cell.table == table && cell.cell == number) {
			return cell;
		}
	}
	return {table, number, params::Kind::NONE, -1};
}

/// @p target, for an owner of @p owner's plugin, names the runtime param the recorded @p cell holds (an empty cell no
/// param).
void checkCell(Checker& check, const char* table, OwnerPlugin owner, std::optional<ParamTarget> target,
               const param_target_tables_reference::Cell& cell) {
	if (cell.kind == params::Kind::NONE) {
		if (target) {
			check.fail(table, cell.cell, static_cast<int>(target->space), static_cast<int>(target->id.value));
		}
		return;
	}
	std::optional<RuntimeParam> runtime = target ? deluge::bridge::runtimeParamOf(owner, *target) : std::nullopt;
	if (!runtime || runtime->kind != cell.kind || runtime->index != cell.index) {
		check.fail(table, cell.cell, static_cast<int>(cell.kind), cell.index);
	}
}

/// The grid shortcuts and the automation orders, against their recorded cells.
void checkShortcutsAndOrders(Checker& check) {
	for (int32_t x = 0; x < kDisplayWidth; ++x) {
		for (int32_t y = 0; y < kDisplayHeight; ++y) {
			const int32_t n = x * kDisplayHeight + y;
			// The sound grid merges the patched, unpatched and cable shortcuts, which never share a cell.
			param_target_tables_reference::Cell sound{Table::GRID_PATCHED, n, params::Kind::NONE, -1};
			int filled = 0;
			for (Table t : {Table::GRID_PATCHED, Table::GRID_UNPATCHED_SOUND, Table::GRID_CABLE}) {
				param_target_tables_reference::Cell c = recordedCell(t, n);
				if (c.kind != params::Kind::NONE) {
					sound = c;
					++filled;
				}
			}
			if (filled > 1) {
				check.fail("sound grid cell shared", n, filled, 0);
			}
			const auto& grids = deluge::modulation::params::kSoundParamShortcuts;
			checkCell(check, "sound grid", OwnerPlugin::INSTRUMENT, grids[x][y], sound);
			checkCell(check, "sound grid second layer", OwnerPlugin::INSTRUMENT,
			          deluge::modulation::params::kSoundParamShortcutsSecondLayer[x][y],
			          recordedCell(Table::GRID_PATCHED_SECOND, n));
			checkCell(check, "global grid", OwnerPlugin::GLOBAL_EFFECTABLE,
			          deluge::modulation::params::kGlobalParamShortcuts[x][y],
			          recordedCell(Table::GRID_UNPATCHED_GLOBAL, n));
			checkCell(check, "expression shortcut", OwnerPlugin::INSTRUMENT,
			          deluge::modulation::params::expressionShortcut(x, y), recordedCell(Table::GRID_EXPRESSION, n));
		}
	}
	auto order = [&](const char* name, OwnerPlugin owner, std::span<const ParamTarget> targets, Table table) {
		std::vector<param_target_tables_reference::Cell> cells = recorded(table);
		if (cells.size() != targets.size()) {
			check.fail(name, static_cast<int>(cells.size()), static_cast<int>(targets.size()), 0);
			return;
		}
		for (size_t i = 0; i < cells.size(); ++i) {
			checkCell(check, name, owner, targets[i], cells[i]);
		}
	};
	order("sound automation order", OwnerPlugin::INSTRUMENT, deluge::modulation::params::kSoundParamsForAutomation,
	      Table::AUTOMATION_SOUND);
	order("global automation order", OwnerPlugin::GLOBAL_EFFECTABLE,
	      deluge::modulation::params::kGlobalParamsForAutomation, Table::AUTOMATION_GLOBAL);
}

/// A MIDI clip's automation overview shows, at each sound shortcut, the CC MIDI follow maps to that param in a sound's
/// map, and none when the sound map has none; the expression shortcuts show their pseudo-CCs.
void checkMIDICCShortcuts(Checker& check) {
	MidiFollow follow;
	auto expect = [&](const char* what) {
		for (int32_t x = 0; x < kDisplayWidth; ++x) {
			for (int32_t y = 0; y < kDisplayHeight; ++y) {
				uint32_t expected = deluge::modulation::params::kNoParamID;
				if (const auto& cell = deluge::modulation::params::kSoundParamShortcuts[x][y]) {
					RuntimeParam param = *deluge::bridge::runtimeParamOf(OwnerPlugin::INSTRUMENT, *cell);
					uint8_t cc = MIDI_CC_NONE;
					if (param.kind == params::Kind::PATCHED) {
						cc = follow.soundParamToCC[param.index];
					}
					else if (param.kind == params::Kind::UNPATCHED_SOUND) {
						cc = follow.soundParamToCC[params::UNPATCHED_START + param.index];
					}
					if (cc != MIDI_CC_NONE) {
						expected = cc;
					}
				}
				if (x == 14 && y == 7) {
					expected = CC_NUMBER_PITCH_BEND;
				}
				else if (x == 15 && y == 0) {
					expected = CC_NUMBER_AFTERTOUCH;
				}
				else if (x == 15 && y == 7) {
					expected = CC_NUMBER_Y_AXIS;
				}
				const uint32_t shown = automationMIDICCShortcut(follow, x, y);
				if (shown != expected) {
					check.fail(what, x * kDisplayHeight + y, static_cast<int>(shown), static_cast<int>(expected));
				}
			}
		}
	};
	expect("MIDI CC shortcut, default mappings");
	// A patched param the sound map leaves unmapped (the master level, at 6,0) has no CC; nor does portamento (7,0),
	// whose unpatched index is the global map's mod FX rate.
	follow.soundParamToCC[params::GLOBAL_VOLUME_POST_FX] = MIDI_CC_NONE;
	expect("MIDI CC shortcut, patched param unmapped");
	follow.soundParamToCC[params::UNPATCHED_START + params::UNPATCHED_PORTAMENTO] = MIDI_CC_NONE;
	expect("MIDI CC shortcut, portamento unmapped");
}

/// A CC, or -1 for none.
int ccNumber(std::optional<uint8_t> cc) {
	return cc ? static_cast<int>(*cc) : -1;
}

/// The MIDIFollow.XML @p follow writes.
std::string followFile(MidiFollow& follow) {
	XMLSerializer writer;
	writer.setMemoryBased();
	writer.write(StorageManager::kXmlDocumentHead);
	follow.serialiseDefaults(writer);
	return {writer.getBufferPtr(), static_cast<size_t>(writer.bytesWritten())};
}

/// MIDI follow's maps against their recorded cells; the CC that follows each param against the recorded inverse maps;
/// and its file, as the defaults write it and as a read of it writes it back.
void checkMidiFollow(Checker& check) {
	MidiFollow follow;
	std::vector<param_target_tables_reference::Cell> sound = recorded(Table::FOLLOW_CC_TO_SOUND);
	std::vector<param_target_tables_reference::Cell> global = recorded(Table::FOLLOW_CC_TO_GLOBAL);
	for (int32_t cc = 0; cc <= kMaxMIDIValue; ++cc) {
		checkCell(check, "follow sound map", OwnerPlugin::INSTRUMENT, follow.ccToSoundTarget[cc], sound[cc]);
		checkCell(check, "follow global map", OwnerPlugin::GLOBAL_EFFECTABLE, follow.ccToGlobalTarget[cc], global[cc]);
	}
	// Feedback for a param goes out on the CC that controls it: the CC whose recorded map entry names it.
	auto controlling = [](Table table, params::Kind kind, int32_t index) {
		for (const auto& cell : recorded(table)) {
			if (cell.kind == kind && cell.index == index) {
				return static_cast<int>(cell.cell);
			}
		}
		return -1;
	};
	for (const auto& cell : recorded(Table::FOLLOW_SOUND_TO_CC)) {
		// the cell is the param's flat id, which is its instrument id; the gap between the patched and unpatched
		// params names no param
		const ParamTarget target = deluge::project::engine_target(static_cast<uint32_t>(cell.cell));
		std::optional<RuntimeParam> param = deluge::bridge::runtimeParamOf(OwnerPlugin::INSTRUMENT, target);
		if (!param) {
			continue;
		}
		const int expected = controlling(Table::FOLLOW_CC_TO_SOUND, param->kind, param->index);
		std::optional<uint8_t> cc = follow.ccOf(OwnerPlugin::INSTRUMENT, target);
		if (ccNumber(cc) != expected) {
			check.fail("follow sound CC", cell.cell, ccNumber(cc), expected);
		}
	}
	for (const auto& cell : recorded(Table::FOLLOW_GLOBAL_TO_CC)) {
		std::optional<ParamTarget> target =
		    deluge::bridge::targetOf(OwnerPlugin::GLOBAL_EFFECTABLE, params::Kind::UNPATCHED_GLOBAL, cell.cell);
		const int expected = controlling(Table::FOLLOW_CC_TO_GLOBAL, params::Kind::UNPATCHED_GLOBAL, cell.cell);
		std::optional<uint8_t> cc = target ? follow.ccOf(OwnerPlugin::GLOBAL_EFFECTABLE, *target) : std::nullopt;
		if (ccNumber(cc) != expected) {
			check.fail("follow global CC", cell.cell, ccNumber(cc), expected);
		}
	}
	// no CC follows a cable, an expression or a CC
	if (follow.ccOf(OwnerPlugin::INSTRUMENT, deluge::modulation::params::kSoundParamShortcuts[6][2].value())
	    || follow.ccOf(OwnerPlugin::INSTRUMENT, *deluge::modulation::params::expressionShortcut(14, 7))) {
		check.fail("follow CC of a cable or an expression", 0, 0, 0);
	}

	const std::string written = followFile(follow);
	if (written != midi_follow_xml_reference::kDefaults) {
		check.fail("follow file", static_cast<int>(written.size()),
		           static_cast<int>(midi_follow_xml_reference::kDefaults.size()), 0);
		std::fprintf(check.io.err, "%s\n", written.c_str());
	}
	MidiFollow reread;
	reread.ccToSoundTarget.fill(std::nullopt);
	reread.ccToGlobalTarget.fill(std::nullopt);
	reread.soundParamToCC.fill(MIDI_CC_NONE);
	reread.globalParamToCC.fill(MIDI_CC_NONE);
	std::string bytes = written;
	MidiFollow::parseDefaultsBytes(&reread, bytes.data(), static_cast<uint32_t>(bytes.size()));
	const std::string rewritten = followFile(reread);
	if (rewritten != written) {
		check.fail("follow file read back", static_cast<int>(rewritten.size()), static_cast<int>(written.size()), 0);
		std::fprintf(check.io.err, "%s\n", rewritten.c_str());
	}
}

/// The performance view's tables, against their recorded cells.
void checkPerformanceTables(Checker& check) {
	auto target = [](const std::optional<deluge::plugin::ParamId>& param) -> std::optional<ParamTarget> {
		return param ? std::optional{deluge::project::engine_target(param->value)} : std::nullopt;
	};
	auto list = [&](const char* name, std::span<const ParamsForPerformance> rows, Table table) {
		std::vector<param_target_tables_reference::Cell> cells = recorded(table);
		if (cells.size() != rows.size()) {
			check.fail(name, static_cast<int>(cells.size()), static_cast<int>(rows.size()), 0);
			return;
		}
		for (size_t i = 0; i < rows.size(); ++i) {
			checkCell(check, name, OwnerPlugin::GLOBAL_EFFECTABLE, target(rows[i].param), cells[i]);
		}
	};
	list("performance params", performanceSongParams(), Table::PERFORMANCE_SONG);
	list("performance default layout", performanceDefaultLayout(), Table::PERFORMANCE_LAYOUT);
	for (int32_t x = 0; x < kDisplayWidth; ++x) {
		for (int32_t y = 0; y < kDisplayHeight; ++y) {
			checkCell(check, "performance shortcut", OwnerPlugin::GLOBAL_EFFECTABLE, target(performanceShortcut(x, y)),
			          recordedCell(Table::PERFORMANCE_SHORTCUTS, x * kDisplayHeight + y));
		}
	}
}

int param_target_check_main(int /*argc*/, char** /*argv*/, Io io) {
	Checker check{io};
	checkRoundTrip(check);
	checkShortcutsAndOrders(check);
	checkMIDICCShortcuts(check);
	checkMidiFollow(check);
	checkPerformanceTables(check);
	std::fprintf(io.out, "%d disagreement(s)\n", check.disagreements);
	return check.disagreements == 0 ? 0 : 3;
}

} // namespace

DELUGE_DRIVER_SCENARIO(param_target_check, param_target_check_main)
