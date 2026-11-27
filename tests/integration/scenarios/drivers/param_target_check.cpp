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
//
// Gate param-target-check:
//     the host names params by target: every runtime param of the instrument and of the global effectable maps to a
//     target and back through the bridge, ids resolve only in their owner's plugin, and every table that names params
//     by target gives cell by cell the runtime param its recorded characterisation holds

#include "arp/params.h"
#include "board_controls.hpp"
#include "definitions_cxx.hpp"
#include "gui/views/automation_view.h"
#include "gui/views/performance_view.h"
#include "host/chains/params.h"
#include "instruments/classic/pages.h"
#include "io/midi/midi_follow.h"
#include "memory/general_memory_allocator.h"
#include "model/fx/chain_params.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/param_selection.h"
#include "model/song/song.h"
#include "modulation/knob_target.h"
#include "modulation/params/param.h"
#include "modulation/params/param_targets.h"
#include "modulation/patch/patch_cable.h"
#include "processing/sound/sound_instrument.h"
#include "project/song.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/drivers/midi_follow_xml_reference.h"
#include "scenarios/drivers/param_target_tables_reference.h"
#include "storage/storage_manager.h"
#include "support/main_numbering.h"
#include "ui/controls/knob_focus.h"
#include "ui/scenes/bind/knob_assignment.h"
#include "util/misc.h"

#include <array>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
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

/// The automation selection's crossings: every pair the views select comes back from its PluginParam, and a clip's or
/// the song's file attributes read as their selections (a MIDI clip's CC, whatever kind it kept).
void checkSelection(Checker& check) {
	using deluge::bridge::clipSelectionOfFile;
	using deluge::bridge::pluginParamOf;
	using deluge::bridge::runtimePairOf;
	using deluge::bridge::songSelectionOfFile;
	using deluge::project::midi_cc_param;
	using deluge::project::ParamSpace;
	auto trip = [&](params::Kind kind, int32_t index) {
		const RuntimeParam back = runtimePairOf(pluginParamOf(kind, index));
		if (back.kind != kind || back.index != index) {
			check.fail("selection round trip", static_cast<int>(kind), index, static_cast<int>(back.kind));
		}
	};
	for (int32_t p = 0; p < params::kNumParams; ++p) {
		trip(params::Kind::PATCHED, p);
	}
	for (int32_t u = 0; u < params::UNPATCHED_SOUND_MAX_NUM; ++u) {
		trip(params::Kind::UNPATCHED_SOUND, u);
	}
	for (int32_t u = 0; u < params::UNPATCHED_GLOBAL_MAX_NUM; ++u) {
		trip(params::Kind::UNPATCHED_GLOBAL, u);
	}
	for (int32_t e = 0; e < kNumExpressionDimensions; ++e) {
		trip(params::Kind::EXPRESSION, e);
	}
	for (int32_t a = 0; a < static_cast<int32_t>(deluge::arp::kParamCount); ++a) {
		trip(params::Kind::ARP, a);
	}
	for (int32_t cc = 0; cc < kNumCCExpression; ++cc) {
		trip(params::Kind::MIDI, cc);
	}
	for (const auto& column : params::kSoundParamShortcuts) {
		for (const auto& cell : column) {
			if (!cell) {
				continue;
			}
			const std::optional<RuntimeParam> runtime = deluge::bridge::runtimeParamOf(OwnerPlugin::INSTRUMENT, *cell);
			if (runtime && runtime->kind == params::Kind::PATCH_CABLE) {
				trip(params::Kind::PATCH_CABLE, runtime->index);
			}
		}
	}

	const RuntimeParam none = runtimePairOf(std::nullopt);
	if (pluginParamOf(params::Kind::NONE, 5) || none.kind != params::Kind::NONE
	    || none.index != static_cast<int32_t>(params::kNoParamID)) {
		check.fail("no selection", static_cast<int>(none.kind), none.index, 0);
	}

	constexpr int32_t kExpression = util::to_underlying(params::Kind::EXPRESSION);
	constexpr int32_t kPatched = util::to_underlying(params::Kind::PATCHED);
	constexpr int32_t kGlobal = util::to_underlying(params::Kind::UNPATCHED_GLOBAL);
	if (clipSelectionOfFile(0, 74) != midi_cc_param(74)) {
		check.fail("a MIDI clip's CC, kind none", 0, 74, 0);
	}
	if (clipSelectionOfFile(kExpression, 74) != midi_cc_param(74)) {
		check.fail("a MIDI clip's CC, kind expression", kExpression, 74, 0);
	}
	const std::optional<deluge::project::PluginParam> y = clipSelectionOfFile(kExpression, 1);
	if (!y || y->target.space != ParamSpace::EXPRESSION || y->target.id.value != 1) {
		check.fail("an expression lane", kExpression, 1, 0);
	}
	if (clipSelectionOfFile(kPatched, 24) != pluginParamOf(params::Kind::PATCHED, 24)) {
		check.fail("a patched param", kPatched, 24, 0);
	}
	for (int32_t kind = 0; kind <= util::to_underlying(params::Kind::ARP); ++kind) {
		if (clipSelectionOfFile(kind, -1) || songSelectionOfFile(kind, -1)) {
			check.fail("no id", kind, -1, 0);
		}
	}
	if (clipSelectionOfFile(99, 3) || songSelectionOfFile(99, 3)) {
		check.fail("no kind", 99, 3, 0);
	}
	const int32_t tempoIndex = *deluge::host::chains::find_runtime_index(deluge::host::chains::param::kTempo);
	const std::optional<deluge::project::PluginParam> tempo = songSelectionOfFile(kGlobal, tempoIndex);
	if (!tempo || tempo->plugin != OwnerPlugin::GLOBAL_EFFECTABLE
	    || tempo->target != deluge::project::engine_target(deluge::host::chains::param::kTempo.value)) {
		check.fail("the song's tempo", kGlobal, tempoIndex, 0);
	}
	if (songSelectionOfFile(0, tempoIndex)) {
		check.fail("the song's kind none", 0, tempoIndex, 0);
	}
	if (deluge::bridge::isCableCollection(nullptr)) {
		check.fail("no collection is a cable set", 0, 0, 0);
	}
}

/// The ids the edits name are the runtime params they replace, and a knob's control target is the same by plugin as by
/// the kind that numbers its owner's unpatched params.
void checkEditIds(Checker& check) {
	namespace classic = deluge::instruments::classic;
	namespace chains = deluge::host::chains;
	if (classic::runtime_slot(classic::param::kCompressorShape)->index != params::UNPATCHED_SIDECHAIN_SHAPE
	    || *chains::find_runtime_index(chains::param::kCompressorShape) != params::UNPATCHED_SIDECHAIN_SHAPE
	    || *chains::find_runtime_index(chains::param::kSidechainCompressorVolume) != params::UNPATCHED_SIDECHAIN_VOLUME
	    || classic::param::kVolumePostReverbSend.value != params::GLOBAL_VOLUME_POST_REVERB_SEND
	    || classic::param::kVolume.value != params::LOCAL_VOLUME) {
		check.fail("an edit's id", 0, 0, 0);
	}
	for (uint32_t p = 0; p < params::UNPATCHED_START + params::UNPATCHED_GLOBAL_MAX_NUM; ++p) {
		const deluge::modulation::KnobTarget knob = deluge::modulation::knobParam(p);
		if (deluge::bridge::controlTargetOf(knob, OwnerPlugin::INSTRUMENT)
		        != deluge::bridge::controlTargetOf(knob, params::Kind::UNPATCHED_SOUND)
		    || deluge::bridge::controlTargetOf(knob, OwnerPlugin::GLOBAL_EFFECTABLE)
		           != deluge::bridge::controlTargetOf(knob, params::Kind::UNPATCHED_GLOBAL)) {
			check.fail("control target by plugin", static_cast<int>(p), 0, 0);
		}
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
	// The cells are main's: its unpatched params past the arp's sit 15 (16) higher, and the arp's own are ours by arp
	// id (support/main_numbering.h).
	params::Kind kind = cell.kind;
	int32_t index = cell.index;
	if (kind == params::Kind::UNPATCHED_SOUND || kind == params::Kind::UNPATCHED_GLOBAL) {
		namespace numbering = deluge::ui::scenes::spec::main_numbering;
		if (const std::optional<uint32_t> arp =
		        numbering::arp_id_of_index(kind == params::Kind::UNPATCHED_GLOBAL, index)) {
			kind = params::Kind::ARP;
			index = static_cast<int32_t>(*arp);
		}
		else {
			index = numbering::index_of(index);
		}
	}
	std::optional<RuntimeParam> runtime = target ? deluge::bridge::runtimeParamOf(owner, *target) : std::nullopt;
	if (!runtime || runtime->kind != kind || runtime->index != index) {
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
					else if (param.kind == params::Kind::ARP) {
						cc = follow.soundArpParamToCC[param.index];
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
		// The cell is main's id; the controlling CC is main's for that id, the CC asked is ours for its param.
		const auto main_id = static_cast<uint32_t>(cell.cell);
		const bool unpatched = main_id >= deluge::ui::scenes::spec::main_numbering::kUnpatchedFirstId;
		const ParamTarget target = unpatched ? deluge::ui::scenes::spec::main_numbering::target_of_id(false, main_id)
		                                     : deluge::project::engine_target(main_id);
		std::optional<RuntimeParam> param = deluge::bridge::runtimeParamOf(OwnerPlugin::INSTRUMENT, target);
		if (!param) {
			continue;
		}
		const int expected = unpatched ? controlling(Table::FOLLOW_CC_TO_SOUND, params::Kind::UNPATCHED_SOUND,
		                                             static_cast<int32_t>(main_id - params::UNPATCHED_START))
		                               : controlling(Table::FOLLOW_CC_TO_SOUND, param->kind, param->index);
		std::optional<uint8_t> cc = follow.ccOf(OwnerPlugin::INSTRUMENT, target);
		if (ccNumber(cc) != expected || ccNumber(cc) != cell.index) {
			check.fail("follow sound CC", cell.cell, ccNumber(cc), cell.index);
		}
	}
	for (const auto& cell : recorded(Table::FOLLOW_GLOBAL_TO_CC)) {
		// The cell is main's unpatched index, as this tree names it (support/main_numbering.h).
		std::optional<ParamTarget> target;
		if (const std::optional<uint32_t> arp =
		        deluge::ui::scenes::spec::main_numbering::arp_id_of_index(true, cell.cell)) {
			target = ParamTarget{.space = deluge::project::ParamSpace::ARP, .id = deluge::plugin::ParamId{*arp}};
		}
		else {
			target = deluge::bridge::targetOf(OwnerPlugin::GLOBAL_EFFECTABLE, params::Kind::UNPATCHED_GLOBAL,
			                                  deluge::ui::scenes::spec::main_numbering::index_of(cell.cell));
		}
		const int expected = controlling(Table::FOLLOW_CC_TO_GLOBAL, params::Kind::UNPATCHED_GLOBAL, cell.cell);
		std::optional<uint8_t> cc = target ? follow.ccOf(OwnerPlugin::GLOBAL_EFFECTABLE, *target) : std::nullopt;
		// the recorded map is kept but for the EQ, whose feedback went out on the filters' CCs
		const bool eq = cell.cell == params::UNPATCHED_BASS || cell.cell == params::UNPATCHED_TREBLE
		                || cell.cell == params::UNPATCHED_BASS_FREQ || cell.cell == params::UNPATCHED_TREBLE_FREQ;
		if (ccNumber(cc) != expected || (!eq && ccNumber(cc) != cell.index) || (eq && cell.index == expected)) {
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

/// The published chain pages' knobs, over every mod button, knob, filter view, mod-FX view and compressor view, for the
/// song's chain and a kit's, against the recorded cells of the engine's layouts. The song's compressor cells that edit
/// a setting have no param in the layout: they must be the pages' compressor arms instead, and no other cell may be
/// one.
void checkChainKnobs(Checker& check) {
	namespace bind = deluge::ui::scenes::bind;
	namespace project = deluge::project;
	constexpr uint32_t kKit = 1;
	constexpr uint32_t kClip = 1;
	auto song = std::make_unique<project::Song>();
	project::DocOutput& output = song->output_structure.getOrCreate(kKit);
	output.effects.emplace();

	constexpr std::array kFilters{FilterType::LPF, FilterType::HPF, FilterType::EQ};
	constexpr std::array kModFX{ModFXParam::DEPTH, ModFXParam::OFFSET, ModFXParam::FEEDBACK}; // as recorded
	for (bool clip : {false, true}) {
		bind::KnobFocus focus;
		if (clip) {
			focus.owner = bind::ChainFocus{.chain = {.kind = project::ChainAddress::Kind::OUTPUT, .id = kKit},
			                               .owner = deluge::ui::controls::ChainOwner::KIT,
			                               .clip = project::ClipAddress{.clip_id = kClip}};
		}
		else {
			focus.owner = bind::SongFocus{};
		}
		project::FxChain& chain = clip ? *output.effects : song->master_effects;
		for (int32_t m = 0; m < kNumModButtons; ++m) {
			song->mod_knob_mode = static_cast<uint8_t>(m);
			output.mod_knob_mode = static_cast<uint8_t>(m);
			for (int32_t k = 0; k < kNumPhysicalModKnobs; ++k) {
				for (int32_t f = 0; f < 3; ++f) {
					for (int32_t x = 0; x < 3; ++x) {
						for (int32_t comp = 0; comp < 2; ++comp) {
							chain.filter_type = static_cast<uint8_t>(kFilters[f]);
							chain.mod_fx_param = static_cast<uint8_t>(kModFX[x]);
							focus.comp.editing = comp != 0;
							const int32_t n = (((m * kNumPhysicalModKnobs + k) * 3 + f) * 3 + x) * 2 + comp;
							const char* name = clip ? "clip knob layout" : "global knob layout";
							// A clip chain's knobs never see the compressor view (`View::knobComp` hands one to the
							// song's alone), so the layout's comp cells for it are its plain cells.
							const param_target_tables_reference::Cell cell =
							    recordedCell(clip ? Table::GE_CLIP_KNOBS : Table::GE_KNOBS, clip ? n - comp : n);
							const bind::KnobAssignment page = bind::knob_assignment(*song, focus, k, kDelugeControls);
							const auto* address = std::get_if<project::ParamAddress>(&page.target);
							const auto* arm = std::get_if<bind::CompArm>(&page.target);
							checkCell(check, name, OwnerPlugin::GLOBAL_EFFECTABLE,
							          address ? std::optional{address->target()} : std::nullopt, cell);
							// Where the layout has no param on the song's compressor button the page edits the setting.
							const bool setting = !clip && m == 4 && (k == 1 || comp != 0);
							if ((arm != nullptr) != (setting && cell.kind == params::Kind::NONE)) {
								check.fail(name, n, arm != nullptr, setting);
							}
							// The upper knob edits the threshold; the lower, in the view, the first setting (the
							// ratio).
							if (arm != nullptr
							    && arm->path != (k == 1 ? "audioCompressor/thresh" : "audioCompressor/ratio")) {
								check.fail(name, n, 1, 0);
							}
						}
					}
				}
			}
		}
	}
}

/// A sound's default mod knobs (fresh, and as a sample sets them up) and a global effectable's knob layouts over every
/// selector, against their recorded cells.
void checkKnobs(Checker& check) {
	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	deluge::model::initGlobalParams(&song->paramManager);
	Song* previous = currentSong;
	currentSong = song;
	ParamManagerForTimeline synthParams;
	auto* synth = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &synthParams));
	auto sound = [&](const char* name, Table table) {
		for (int32_t m = 0; m < kNumModButtons; ++m) {
			for (int32_t k = 0; k < kNumPhysicalModKnobs; ++k) {
				const int32_t n = m * kNumPhysicalModKnobs + k;
				checkCell(check, name, OwnerPlugin::INSTRUMENT, synth->modKnobs[m][k].target, recordedCell(table, n));
			}
		}
	};
	sound("sound knobs", Table::SOUND_KNOBS);
	// The sample row's knobs are the instrument's sample pages; setupAsSample takes them.
	for (const deluge::plugin::RemotePage& page : deluge::instruments::classic::kSamplePages) {
		for (int32_t k = 0; k < kNumPhysicalModKnobs; ++k) {
			checkCell(
			    check, "sample pages", OwnerPlugin::INSTRUMENT,
			    deluge::bridge::knobOfControl(*page.slots[k].param, params::Kind::UNPATCHED_SOUND),
			    recordedCell(Table::SOUND_KNOBS_SAMPLE, static_cast<int32_t>(page.section) * kNumPhysicalModKnobs + k));
		}
	}
	synth->setupAsSample(&synthParams);
	sound("sound knobs, sample", Table::SOUND_KNOBS_SAMPLE);
	currentSong = previous;

	checkChainKnobs(check);
}

/// A knob's or a learning menu's target and its descriptor word are each other's inverse over every word they form:
/// a sound's or a global effectable's param by its flat id, a cable from one source, a range cable, and none.
void checkKnobWords(Checker& check) {
	using deluge::modulation::knobTargetOf;
	using deluge::modulation::knobWord;
	using deluge::synth::CableTarget;
	auto trip = [&](CableTarget word) {
		if (knobWord(knobTargetOf(word)).word != word.word) {
			check.fail("knob word", static_cast<int>(word.word), 0, 0);
		}
	};
	trip(CableTarget{});
	for (uint32_t p = 0; p < params::UNPATCHED_START + params::UNPATCHED_SOUND_MAX_NUM; ++p) {
		trip(CableTarget::param(static_cast<uint8_t>(p)));
		if (knobTargetOf(CableTarget::param(static_cast<uint8_t>(p))) != deluge::modulation::knobParam(p)) {
			check.fail("knob param", static_cast<int>(p), 0, 0);
		}
	}
	for (uint32_t p = 0; p < params::kNumParams; ++p) {
		for (uint32_t s = 0; s < static_cast<uint32_t>(kNumPatchSources); ++s) {
			trip(CableTarget::depth_of(static_cast<uint8_t>(p), static_cast<PatchSource>(s)));
			for (uint32_t s2 = 0; s2 < static_cast<uint32_t>(kNumPatchSources); ++s2) {
				trip(CableTarget::depth_of_depth(static_cast<uint8_t>(p), static_cast<PatchSource>(s),
				                                 static_cast<PatchSource>(s2)));
			}
		}
	}
}

int param_target_check_main(int /*argc*/, char** /*argv*/, Io io) {
	Checker check{io};
	checkRoundTrip(check);
	checkSelection(check);
	checkEditIds(check);
	checkShortcutsAndOrders(check);
	checkMIDICCShortcuts(check);
	checkMidiFollow(check);
	checkPerformanceTables(check);
	checkKnobs(check);
	checkKnobWords(check);
	std::fprintf(io.out, "%d disagreement(s)\n", check.disagreements);
	return check.disagreements == 0 ? 0 : 3;
}

} // namespace

DELUGE_DRIVER_SCENARIO(param_target_check, param_target_check_main)
