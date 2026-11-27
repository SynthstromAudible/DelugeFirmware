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
// name for its runtime index, every runtime param has a row, and each row's scope, flags and default are the
// runtime's (a default is what the engine's own init gives a fresh instance).
// Exit: 0 agree; 3 a row disagrees (each printed).
//
// Gate param-table-check:
//     the engines' param tables agree with the runtime: every row of the Deluge instrument's table and of the
//     host chain's table has the runtime's file name, scope, bipolarity and default (what the engine's
//     own init gives a fresh instance) for its runtime index, the boundary map gives that index the row's id, and every
//     runtime param has a row; each chain's init leaves every cursor at its lane's base, its set's kind and store as
//     recorded

#include "effects/bitcrush.h"
#include "effects/compressor.h"
#include "effects/delay.h"
#include "effects/equalizer.h"
#include "effects/filter.h"
#include "effects/mod_fx.h"
#include "effects/reverb.h"
#include "effects/stutter.h"
#include "host/chains/naming.h"
#include "host/chains/params.h"
#include "instruments/classic/cables.h"
#include "instruments/classic/naming.h"
#include "instruments/classic/params.h"
#include "model/fx/chain_params.h"
#include "model/project_bridge/param_ids.h"
#include "modulation/params/arp_param_set.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/drivers/param_defaults_reference.h"
#include "scenarios/drivers/param_names_reference.h"
#include "support/main_numbering.h"
#include "synth/matrix/descriptors.h"
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

/// Reference copies of the runtime's param predicates, kept verbatim as the oracle for the rows' flags, from
/// modulation/params/param.cpp (isParamPan, isParamPitch, paramNeedsLPF) and param_set.cpp (the Unpatched and Patched
/// sets' shouldParamIndicateMiddleValue, doesParamIdAllowAutomation, shouldRecordUnautomatedParamChange,
/// shouldInterpolateWithFloat), with the set's kind as an argument: the oracle for each row's flags. The stutter rate's
/// middle detent depends on the stutter's state and is left out.
namespace reference_predicates {
using namespace params;

bool isParamPan(Kind kind, int32_t paramID) {
	return (kind == Kind::PATCHED && paramID == LOCAL_PAN)
	       || (kind == Kind::UNPATCHED_GLOBAL && paramID == UNPATCHED_PAN);
}
bool isParamPitch(Kind kind, int32_t paramID) {
	if (kind == Kind::PATCHED) {
		return (paramID == LOCAL_PITCH_ADJUST) || (paramID == LOCAL_OSC_A_PITCH_ADJUST)
		       || (paramID == LOCAL_OSC_B_PITCH_ADJUST) || (paramID == LOCAL_MODULATOR_0_PITCH_ADJUST)
		       || (paramID == LOCAL_MODULATOR_1_PITCH_ADJUST);
	}
	if (kind == Kind::UNPATCHED_GLOBAL) {
		return static_cast<UnpatchedGlobal>(paramID) == UNPATCHED_PITCH_ADJUST;
	}
	return false;
}
bool paramNeedsLPF(ParamType p, bool fromAutomation) {
	switch (p) {
	case GLOBAL_VOLUME_POST_FX:
	case GLOBAL_VOLUME_POST_REVERB_SEND:
	case GLOBAL_REVERB_AMOUNT:
	case LOCAL_VOLUME:
	case LOCAL_PAN:
	case LOCAL_LPF_FREQ:
	case LOCAL_HPF_FREQ:
	case LOCAL_OSC_A_VOLUME:
	case LOCAL_OSC_B_VOLUME:
	case LOCAL_OSC_A_WAVE_INDEX:
	case LOCAL_OSC_B_WAVE_INDEX:
		return !fromAutomation;
	case LOCAL_MODULATOR_0_VOLUME:
	case LOCAL_MODULATOR_1_VOLUME:
	case LOCAL_MODULATOR_0_FEEDBACK:
	case LOCAL_MODULATOR_1_FEEDBACK:
	case LOCAL_CARRIER_0_FEEDBACK:
	case LOCAL_CARRIER_1_FEEDBACK:
	case GLOBAL_MOD_FX_DEPTH:
	case GLOBAL_DELAY_FEEDBACK:
		return true;
	default:
		return false;
	}
}
bool unpatchedIndicatesMiddle(Kind kind, int32_t paramId) {
	switch (paramId) {
	case UNPATCHED_BASS:
	case UNPATCHED_TREBLE:
		return true;
	}
	if (kind == Kind::UNPATCHED_GLOBAL) {
		switch (paramId) {
		case UNPATCHED_DELAY_RATE:
		case UNPATCHED_DELAY_AMOUNT:
		case UNPATCHED_PAN:
		case UNPATCHED_PITCH_ADJUST:
			return true;
		}
	}
	return false;
}
bool patchedIndicatesMiddle(int32_t paramId) {
	switch (paramId) {
	case LOCAL_PAN:
	case LOCAL_PITCH_ADJUST:
	case LOCAL_OSC_A_PITCH_ADJUST:
	case LOCAL_OSC_B_PITCH_ADJUST:
	case LOCAL_MODULATOR_0_PITCH_ADJUST:
	case LOCAL_MODULATOR_1_PITCH_ADJUST:
	case GLOBAL_DELAY_FEEDBACK:
	case GLOBAL_DELAY_RATE:
		return true;
	default:
		return false;
	}
}

/// The flags the predicates above give the param at @p index of a @p kind collection.
deluge::plugin::ParamFlags flagsOf(Kind kind, int32_t index) {
	using deluge::plugin::ParamFlag;
	deluge::plugin::ParamFlags flags;
	auto add = [&](bool on, ParamFlag flag) {
		if (on) {
			flags = flags | flag;
		}
	};
	const bool patched = kind == Kind::PATCHED;
	add(isParamPan(kind, index) || isParamPitch(kind, index), ParamFlag::CENTRED);
	add(patched ? patchedIndicatesMiddle(index) : unpatchedIndicatesMiddle(kind, index), ParamFlag::MIDDLE_DETENT);
	add(!patched && index == UNPATCHED_TEMPO, ParamFlag::NO_UNDO);
	add(kind == Kind::UNPATCHED_GLOBAL && index == UNPATCHED_TEMPO, ParamFlag::FLOAT_INTERPOLATION);
	add(patched && paramNeedsLPF(index, true), ParamFlag::SMOOTHED);
	add(patched && paramNeedsLPF(index, false) && !paramNeedsLPF(index, true), ParamFlag::SMOOTHED_USER_ONLY);
	return flags;
}
/// The mapping the reference knob overrides (UnpatchedParamSet's compressor threshold, PatchedParamSet's phase widths)
/// and pan menus (patched_param::Pan on LOCAL_PAN, unpatched_param::Pan on UNPATCHED_PAN) give the param.
deluge::plugin::MappingKind mappingOf(Kind kind, int32_t index) {
	using deluge::plugin::MappingKind;
	if (kind == Kind::PATCHED) {
		if (index == LOCAL_OSC_A_PHASE_WIDTH || index == LOCAL_OSC_B_PHASE_WIDTH) {
			return MappingKind::UNIPOLAR;
		}
		return index == LOCAL_PAN ? MappingKind::PAN : MappingKind::STANDARD;
	}
	if (index == UNPATCHED_COMPRESSOR_THRESHOLD) {
		return MappingKind::UNIPOLAR;
	}
	return kind == Kind::UNPATCHED_GLOBAL && index == UNPATCHED_PAN ? MappingKind::PAN : MappingKind::STANDARD;
}
} // namespace reference_predicates

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
		if (unnamed && info.id != deluge::host::chains::param::kTempo) {
			fail(table, info, "the runtime has no name for it");
		}
		if ((info.scope == ParamScope::PER_VOICE) != perVoice) {
			fail(table, info, "its scope is not the runtime's");
		}
		if (info.flags != reference_predicates::flagsOf(kind, index)) {
			std::fprintf(io.err, "%s %u: flags %#x, the runtime's predicates give %#x\n", table, info.id.value,
			             info.flags.bits, reference_predicates::flagsOf(kind, index).bits);
			fail(table, info, "its flags are not the runtime's");
		}
		if (info.mapping != reference_predicates::mappingOf(kind, index)) {
			fail(table, info, "its mapping is not the runtime's knob and menu arithmetic");
		}
		const bool centred = info.flags.has(deluge::plugin::ParamFlag::CENTRED);
		if (centred != params::isParamBipolar(kind, index)) {
			fail(table, info, "the runtime's isParamBipolar disagrees with CENTRED");
		}
		if (kind == params::Kind::PATCHED
		    && (params::paramNeedsLPF(index, true) != reference_predicates::paramNeedsLPF(index, true)
		        || params::paramNeedsLPF(index, false) != reference_predicates::paramNeedsLPF(index, false))) {
			fail(table, info, "the runtime's paramNeedsLPF disagrees with the smoothing flags");
		}
		if (deluge::bridge::paramIdOf(kind, index) != info.id) {
			fail(table, info, "the boundary map gives the runtime index another id");
		}
		if (deluge::bridge::runtimeIndexOf(kind, info.id) != index) {
			fail(table, info, "the boundary map gives the id another runtime index");
		}
		const deluge::plugin::ParamNaming& naming = kind == params::Kind::UNPATCHED_GLOBAL
		                                                ? deluge::host::chains::kNaming
		                                                : deluge::instruments::classic::kNaming;
		if (!unnamed && naming.id_of({name}) != std::optional<ParamId>{info.id}) {
			fail(table, info, "its naming does not give the runtime's name this id");
		}
	}
};

/// The instrument's cable words against the runtime's: each source's name, and every packed cable word the runtime
/// can form (a param with one, two or three sources) round-tripping through its depth id.
void checkCables(Checker& check) {
	for (uint8_t s = 0; s <= static_cast<uint8_t>(PatchSource::NONE); ++s) {
		std::string_view ours =
		    deluge::instruments::classic::to_string(static_cast<deluge::instruments::classic::PatchSource>(s));
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

/// paramNameForFile and fileStringToParam against the recorded names: every kind,
/// both file vocabularies, every runtime index; and every name, "range" and an unknown name read back.
void checkNames(Checker& check) {
	auto main_name = [](int32_t kind, bool midiFollow, int32_t index) -> std::string_view {
		for (const param_names_reference::Name& n : param_names_reference::kNames) {
			if (n.kind == kind && n.midi_follow == midiFollow && n.index == index) {
				return n.name;
			}
		}
		return "none";
	};
	// The names were recorded from main, which kept the arp's params in the unpatched sets (indices 11 to 25, and a
	// chain's arp rate at 30): an unpatched index here past 10 is main's 15 higher, past 14 main's 16 higher.
	auto expected = [&](int32_t kind, bool midiFollow, int32_t index) -> std::string_view {
		if (index < params::UNPATCHED_START) {
			return main_name(kind, midiFollow, index);
		}
		const int32_t ours = index - params::UNPATCHED_START;
		const int32_t main = ours <= 10 ? ours : (ours <= 14 ? ours + 15 : ours + 16);
		return main_name(kind, midiFollow, params::UNPATCHED_START + main);
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

/// Each effect's params against the runtime: a curved param's neutral and curve are the instrument's for the same
/// param (the patched one a sound's chain uses). The tables' effect rows are built from the effects' params with
/// effects::row and pinned field for field by the params specs; effects::built_from checks the id lists a table names.
void checkEffectFragments(Checker& check) {
	using deluge::effects::EffectParam;
	struct Curved {
		const EffectParam& fragment;
		params::ParamType patched;
	};
	namespace fx = deluge::effects;
	const Curved curved[] = {
	    {fx::Filter::kParams[0], params::LOCAL_LPF_FREQ},      {fx::Filter::kParams[1], params::LOCAL_LPF_RESONANCE},
	    {fx::Filter::kParams[2], params::LOCAL_LPF_MORPH},     {fx::Filter::kParams[3], params::LOCAL_HPF_FREQ},
	    {fx::Filter::kParams[4], params::LOCAL_HPF_RESONANCE}, {fx::Filter::kParams[5], params::LOCAL_HPF_MORPH},
	    {fx::ModFx::kParams[0], params::GLOBAL_MOD_FX_RATE},   {fx::ModFx::kParams[1], params::GLOBAL_MOD_FX_DEPTH},
	    {fx::Delay::kRate, params::GLOBAL_DELAY_RATE},         {fx::Delay::kFeedback, params::GLOBAL_DELAY_FEEDBACK},
	    {fx::Stutter::kRate, params::GLOBAL_DELAY_RATE},       {fx::Reverb::kSend, params::GLOBAL_REVERB_AMOUNT},
	};
	for (const Curved& c : curved) {
		const deluge::synth::matrix::ParamRules& rules = deluge::synth::matrix::kParamRules[c.patched];
		if (c.fragment.neutral != deluge::synth::matrix::kParamNeutral[c.patched] || !c.fragment.curve.has_value()
		    || *c.fragment.curve != rules.curve) {
			std::fprintf(check.io.err, "effect param %.*s: neutral %d or curve differs from the instrument's\n",
			             static_cast<int>(c.fragment.name.size()), c.fragment.name.data(), c.fragment.neutral);
			++check.disagreements;
		}
	}
}

/// The first id of the store a fresh sound's or host chain's unpatched set binds to: bridge::firstParamId of the kind
/// the set has when its init first reaches a lane.
constexpr uint32_t kRecordedStoreFirst = 90;

/// The recorded default of param @p id of @p table.
int32_t recordedDefault(param_defaults_reference::Table table, ParamId id) {
	for (const param_defaults_reference::Default& d : param_defaults_reference::kDefaults) {
		if (d.table == table && d.id == id.value) {
			return d.value;
		}
	}
	return 0;
}

/// Each row's default, and what the engine's init gives a fresh manager's param, against the recorded default.
namespace numbering = deluge::ui::scenes::spec::main_numbering;

void checkDefaults(Checker& check) {
	ParamManagerForTimeline sound;
	sound.setupWithPatching();
	Sound::initParams(&sound);
	for (const ParamInfo& info : deluge::instruments::classic::kParams) {
		auto slot = *deluge::instruments::classic::runtime_slot(info.id);
		ParamSet* set = slot.set == deluge::instruments::classic::RuntimeSet::PATCHED
		                    ? static_cast<ParamSet*>(sound.getPatchedParamSet())
		                    : static_cast<ParamSet*>(sound.getUnpatchedParamSet());
		int32_t value = set->docForParam(slot.index)->current_value;
		// Recorded by main, whose unpatched ids past the arp's sit 15 higher (support/main_numbering.h).
		const ParamId main_id = info.id.value >= numbering::kUnpatchedFirstId
		                            ? ParamId{numbering::main_id_of(false, info.id.value)}
		                            : info.id;
		int32_t recorded = recordedDefault(param_defaults_reference::Table::INSTRUMENT, main_id);
		if (value != recorded || info.default_value != recorded) {
			std::fprintf(check.io.err, "default instrument %u %.*s: table %d, runtime %d, recorded %d\n", info.id.value,
			             static_cast<int>(info.file_name.size()), info.file_name.data(), info.default_value, value,
			             recorded);
			++check.disagreements;
		}
	}
	ParamManagerForTimeline global;
	global.setupUnpatched();
	deluge::model::initGlobalParams(&global);
	for (const ParamInfo& info : deluge::host::chains::kParams) {
		int32_t value = global.getUnpatchedParamSet()
		                    ->docForParam(*deluge::host::chains::find_runtime_index(info.id))
		                    ->current_value;
		int32_t recorded = recordedDefault(param_defaults_reference::Table::HOST_CHAIN,
		                                   ParamId{numbering::main_id_of(true, info.id.value)});
		if (value != recorded || info.default_value != recorded) {
			std::fprintf(check.io.err, "default host-chain %u %.*s: table %d, runtime %d, recorded %d\n", info.id.value,
			             static_cast<int>(info.file_name.size()), info.file_name.data(), info.default_value, value,
			             recorded);
			++check.disagreements;
		}
	}
	// The arp's starting values are main's for its rows: a sound's arp the instrument's (its rate a patched param
	// there, so not compared), a kit's the host chain's.
	for (const bool chain : {false, true}) {
		ParamManagerForTimeline owner;
		if (chain) {
			owner.setupUnpatched(deluge::arp::Owner::KIT);
		}
		else {
			owner.setupWithPatching();
		}
		const ArpParamSet* arp = owner.getArpParamSet();
		for (const deluge::arp::Param& row : deluge::arp::kParams) {
			const std::optional<uint32_t> main_id = numbering::main_id_of_arp(chain, row.info.id.value);
			if (!main_id) {
				continue;
			}
			const int32_t recorded = recordedDefault(chain ? param_defaults_reference::Table::HOST_CHAIN
			                                               : param_defaults_reference::Table::INSTRUMENT,
			                                         ParamId{*main_id});
			const int32_t value = arp != nullptr ? arp->word(row.info.id) : 0;
			if (arp == nullptr || value != recorded) {
				std::fprintf(check.io.err, "default %s arp %.*s: set %d, recorded %d\n", chain ? "kit" : "sound",
				             static_cast<int>(row.info.file_name.size()), row.info.file_name.data(), value, recorded);
				++check.disagreements;
			}
		}
	}
}

/// What a chain's init leaves: every unpatched cursor at its lane's base, the set's kind and its store's first id as
/// recorded, and an audio clip's volume below a host chain's.
void checkInitState(Checker& check) {
	auto check_set = [&](const char* owner, UnpatchedParamSet& set, params::Kind kind, uint32_t first) {
		if (set.kind != kind) {
			std::fprintf(check.io.err, "init %s: kind %d, recorded %d\n", owner, static_cast<int>(set.kind),
			             static_cast<int>(kind));
			++check.disagreements;
		}
		if (set.domStore() == nullptr || set.domStore()->first.value != first) {
			std::fprintf(check.io.err, "init %s: store first id %u, recorded %u\n", owner,
			             set.domStore() == nullptr ? 0U : set.domStore()->first.value, first);
			++check.disagreements;
		}
		for (int32_t p = 0; p < set.getNumParams(); ++p) {
			const deluge::project::DocAutoParam* lane = set.findDocParamForId(p);
			if (lane == nullptr || lane->current_value != set.getValue(p)) {
				std::fprintf(check.io.err, "init %s %d: cursor %d, lane %d\n", owner, p, set.getValue(p),
				             lane == nullptr ? 0 : lane->current_value);
				++check.disagreements;
			}
		}
	};
	ParamManagerForTimeline sound;
	sound.setupWithPatching();
	Sound::initParams(&sound);
	check_set("sound", *sound.getUnpatchedParamSet(), params::Kind::UNPATCHED_SOUND, kRecordedStoreFirst);

	ParamManagerForTimeline global;
	global.setupUnpatched();
	deluge::model::initGlobalParams(&global);
	check_set("host chain", *global.getUnpatchedParamSet(), params::Kind::UNPATCHED_GLOBAL, kRecordedStoreFirst);

	ParamManagerForTimeline clip;
	clip.setupUnpatched();
	deluge::model::initAudioClipParams(&clip);
	check_set("audio clip", *clip.getUnpatchedParamSet(), params::Kind::UNPATCHED_GLOBAL, kRecordedStoreFirst);
	if (clip.getUnpatchedParamSet()->getValue(params::UNPATCHED_VOLUME) != -536870912) {
		std::fprintf(check.io.err, "init audio clip: volume %d, recorded -536870912\n",
		             clip.getUnpatchedParamSet()->getValue(params::UNPATCHED_VOLUME));
		++check.disagreements;
	}
}

int param_table_check_main(int /*argc*/, char** /*argv*/, Io io) {
	Checker check{io};
	using namespace deluge::instruments::classic;
	for (const ParamInfo& info : kParams) {
		RuntimeSlot slot = *runtime_slot(info.id);
		const bool patched = slot.set == RuntimeSet::PATCHED;
		check.row("instrument", info, patched ? params::Kind::PATCHED : params::Kind::UNPATCHED_SOUND, slot.index,
		          patched && slot.index < params::FIRST_GLOBAL);
	}
	for (const ParamInfo& info : deluge::host::chains::kParams) {
		check.row("host-chain", info, params::Kind::UNPATCHED_GLOBAL,
		          *deluge::host::chains::find_runtime_index(info.id), false);
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
	cover("host-chain", deluge::host::chains::kParams, params::Kind::UNPATCHED_GLOBAL,
	      params::UNPATCHED_GLOBAL_MAX_NUM);
	checkCables(check);
	checkNames(check);
	checkEffectFragments(check);
	checkDefaults(check);
	checkInitState(check);
	std::fprintf(io.out, "%d disagreement(s)\n", check.disagreements);
	return check.disagreements == 0 ? 0 : 3;
}

} // namespace

DELUGE_DRIVER_SCENARIO(param_table_check, param_table_check_main)
