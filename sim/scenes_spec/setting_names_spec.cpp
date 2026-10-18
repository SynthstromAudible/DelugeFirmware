/// @file
/// @brief The host's name table against the legacy menu items and the legacy param names.
///
/// Every setting row a menu shows, in every table that owns one (the engines' and each model setting group), has
/// names under its owner, equal to what its legacy item shows: its title and name, and for a list its options (full
/// and short) in each context the list changes with. A row with no single legacy item is one of
/// `descriptor_legacy_spec.cpp`'s named skips, and a row no menu shows is named below; neither has names. Every param
/// of the two engine tables has a name equal to `getParamDisplayName`'s, the morph params under each pair of filter
/// modes included, and an id outside an engine's table has none.
///
/// @note The SPEC may include `gui/menu_item/`; the library may not (`scripts/check_layer_rules.py`).

#include "support/descriptor_cases.h"

#include "engines/deluge/params.h"
#include "engines/global_effectable/params.h"
#include "gui/l10n/setting_names.h"
#include "model/project_bridge/param_ids.h"

namespace {

using deluge::l10n::NamesOwner;
using deluge::l10n::ParamNameContext;
using deluge::l10n::setting_names;
using deluge::l10n::SettingNames;
using deluge::project::SettingGroupId;
namespace params = deluge::modulation::params;
namespace engine = deluge::engine;

/// @brief A table and its owner, as a row of it is looked up.
struct OwnedTable {
	NamesOwner owner;
	std::string_view label;
	std::span<const SettingInfo> rows;
};

std::vector<OwnedTable> owned_tables() {
	using deluge::project::table_of;
	return {
	    {engine::kDelugeInstrument, "instrument", deluge::instrument::kSettings},
	    {engine::kDelugeInstrument, "instrument effects", deluge::effects::kEffectSettings},
	    {engine::kGlobalEffectable, "global effectable effects", deluge::effects::kEffectSettings},
	    {engine::kDelugeInstrument, "instrument arp", deluge::engine::kArpSettingRows},
	    {SettingGroupId::SONG, "SONG", table_of(SettingGroupId::SONG)},
	    {SettingGroupId::TRACK, "TRACK", table_of(SettingGroupId::TRACK)},
	    {SettingGroupId::INSTRUMENT_DEVICE, "INSTRUMENT_DEVICE", table_of(SettingGroupId::INSTRUMENT_DEVICE)},
	    {SettingGroupId::LANE, "LANE", table_of(SettingGroupId::LANE)},
	    {SettingGroupId::NOTE_ROW, "NOTE_ROW", table_of(SettingGroupId::NOTE_ROW)},
	    {SettingGroupId::AUDIO_CLIP, "AUDIO_CLIP", table_of(SettingGroupId::AUDIO_CLIP)},
	    {SettingGroupId::CLIP, "CLIP", table_of(SettingGroupId::CLIP)},
	    {SettingGroupId::CLIP_VALUES, "CLIP_VALUES", table_of(SettingGroupId::CLIP_VALUES)},
	    {SettingGroupId::AUDIO_INPUT, "AUDIO_INPUT", table_of(SettingGroupId::AUDIO_INPUT)},
	    {SettingGroupId::CLIP_ARP, "CLIP_ARP", table_of(SettingGroupId::CLIP_ARP)},
	    {SettingGroupId::LANE_ARP, "LANE_ARP", table_of(SettingGroupId::LANE_ARP)},
	};
}

/// @brief A row, or every row under a path ending in '/', that no menu shows, and why.
struct NoMenu {
	SettingGroupId group;
	std::string_view path;
	std::string_view reason;
};

constexpr NoMenu kNoMenu[] = {
    {SettingGroupId::TRACK, "name", "a text row: the rename UI edits it, and `editNameMenu` is an action, not a value"},
    {SettingGroupId::LANE, "name", "a text row: the rename UI edits it, and `editNameMenu` is an action, not a value"},
    {SettingGroupId::CLIP, "name", "a text row: the rename UI edits it, and `editNameMenu` is an action, not a value"},
    {SettingGroupId::INSTRUMENT_DEVICE, "channel",
     "no menu item: the MIDI channel is set by the channel selector gesture"},
    {SettingGroupId::INSTRUMENT_DEVICE, "channelSuffix",
     "no menu item: the channel selector gesture sets the suffix with the channel"},
    {SettingGroupId::INSTRUMENT_DEVICE, "ccLabels/", "text rows: the CC rename UI edits each label"},
};

bool no_menu(SettingGroupId group, std::string_view path) {
	for (const NoMenu& row : kNoMenu) {
		if (row.group == group && (row.path == path || (row.path.ends_with('/') && path.starts_with(row.path)))) {
			return true;
		}
	}
	return false;
}

/// @brief Whether some case names @p path in @p owner's table.
bool covered(const NamesOwner& owner, std::string_view path) {
	for (const Case* c : all_name_cases()) {
		if (c->path != path) {
			continue;
		}
		for (const NamesOwner& candidate : owners_of(c->family)) {
			if (candidate == owner) {
				return true;
			}
		}
	}
	return false;
}

/// @brief The legacy item's options, full or short.
std::vector<std::string> options_of(::MenuItem* item, legacy::Selection::OptType type) {
	std::vector<std::string> out;
	if (auto* list = dynamic_cast<legacy::Selection*>(item); list != nullptr) {
		for (std::string_view option : list->getOptions(type)) {
			out.emplace_back(option);
		}
	}
	return out;
}

bool has_options(::MenuItem* item) {
	return dynamic_cast<legacy::Selection*>(item) != nullptr;
}

/// @brief The groups whose rows are named but not driven by the range, read and write checks: their cases list the
/// legacy options by menu position, which is the row's choice order from its first choice.
bool named_only(const Case& c) {
	return c.family == Family::NOTE_ROW || c.family == Family::CLIP || c.family == Family::INSTRUMENT_DEVICE
	       || c.family == Family::AUDIO_INPUT;
}

/// @brief One case's names, under each owner of its table, against its legacy item in its context.
void check_names(auto& self, const Case& c) {
	enter(c.where, c.source);
	const SettingInfo& row = row_of(c);
	const std::vector<std::string> full = options_of(c.item, legacy::Selection::OptType::FULL);
	const std::vector<std::string> shortened = options_of(c.item, legacy::Selection::OptType::SHORT);
	for (const NamesOwner& owner : owners_of(c.family)) {
		const SettingNames* names = setting_names(owner, c.path, context_of(c.where));
		expect(std::string{c.path} + (names != nullptr ? " has names" : " has no names"))
		    .to_equal(std::string{c.path} + " has names");
		if (names == nullptr) {
			continue;
		}
		expect(std::string{c.path} + " title " + title_of(*names, c.path))
		    .to_equal(std::string{c.path} + " title " + std::string{c.item->getTitle()});
		expect(std::string{c.path} + " name " + text(names->name))
		    .to_equal(std::string{c.path} + " name " + std::string{c.item->getName()});
		if (!has_options(c.item)) {
			expect(std::string{c.path} + " lists " + std::to_string(names->choices.size()))
			    .to_equal(std::string{c.path} + " lists 0");
			continue;
		}
		// A CHOICE row's list is its applicable choices in order; any other row's is its menu values.
		std::vector<int32_t> choices = applicable(row, context_of(c.where), enter(c.where, c.source), c.where);
		if (row.kind != SettingKind::CHOICE || named_only(c)) {
			choices.clear();
			for (size_t i = 0; i < full.size(); ++i) {
				choices.push_back(static_cast<int32_t>(i));
			}
		}
		expect(std::string{c.path} + " offers " + std::to_string(full.size()))
		    .to_equal(std::string{c.path} + " offers " + std::to_string(choices.size()));
		for (size_t position = 0; position < choices.size() && position < full.size(); ++position) {
			const int32_t choice = choices[position];
			const std::string at = std::string{c.path} + " choice " + std::to_string(choice);
			expect(at + " " + choice_text(*names, static_cast<size_t>(choice), false))
			    .to_equal(at + " " + full[position]);
			expect(at + " short " + choice_text(*names, static_cast<size_t>(choice), true))
			    .to_equal(at + " short " + shortened[position]);
		}
	}
}

void run_names(auto& self, const std::vector<Case>& cases) {
	AudioEngine::micPluggedIn = false;
	AudioEngine::lineInPluggedIn = false;
	for (const Case& c : cases) {
		check_names(self, c);
	}
}

/// @brief The oscillator type cases again with an input plugged in: the input choices are named there.
void run_plugged_names(auto& self) {
	AudioEngine::micPluggedIn = true;
	for (const Case* c : all_cases()) {
		if (c->path.starts_with("osc") && c->path.ends_with("/type")) {
			check_names(self, *c);
		}
	}
	AudioEngine::micPluggedIn = false;
}

/// @brief The legacy kind and index of the instrument or global effectable param @p id.
struct Legacy {
	params::Kind kind;
	int32_t index;
};
Legacy legacy_of(deluge::engine::ParamId id, engine::EngineId which) {
	params::Kind kind = params::Kind::UNPATCHED_GLOBAL;
	if (which == engine::kDelugeInstrument) {
		kind = id.value < deluge::instrument::kUnpatchedFirstId ? params::Kind::PATCHED : params::Kind::UNPATCHED_SOUND;
	}
	return {kind, deluge::bridge::runtimeIndexOf(kind, id).value_or(-1)};
}

std::string param_text(engine::EngineId which, deluge::engine::ParamId id, const ParamNameContext& state) {
	return deluge::l10n::get(deluge::l10n::param_name(which, id, state));
}

void check_params(auto& self, std::span<const deluge::engine::ParamInfo> table, engine::EngineId which) {
	for (const deluge::engine::ParamInfo& row : table) {
		const Legacy legacy = legacy_of(row.id, which);
		expect(legacy.index >= 0).to_be_true();
		// Legacy's plain name is the SVF one: the default filter modes are ladders, which rename the morph params.
		const ParamNameContext state{.lpf_mode = deluge::effects::FilterMode::SVF_NOTCH,
		                             .hpf_mode = deluge::effects::FilterMode::SVF_BAND};
		expect(std::string{row.file_name} + " " + param_text(which, row.id, state))
		    .to_equal(std::string{row.file_name} + " " + params::getParamDisplayName(legacy.kind, legacy.index));
	}
}

/// @brief The morph params under every pair of filter modes, against legacy's names with the filters set.
void check_morphs(auto& self) {
	const MenuContext& ctx = enter(Where::SYNTH);
	struct Morph {
		deluge::engine::ParamId id;
		engine::EngineId engine;
	};
	const Morph morphs[] = {
	    {deluge::instrument::param::kLpfMorph, engine::kDelugeInstrument},
	    {deluge::instrument::param::kHpfMorph, engine::kDelugeInstrument},
	    {deluge::global_effectable::param::kLpfMorph, engine::kGlobalEffectable},
	    {deluge::global_effectable::param::kHpfMorph, engine::kGlobalEffectable},
	};
	using deluge::effects::FilterMode;
	const FilterMode lpf_modes[] = {
	    FilterMode::TRANSISTOR_12DB, FilterMode::TRANSISTOR_24DB, FilterMode::TRANSISTOR_24DB_DRIVE,
	    FilterMode::SVF_BAND,        FilterMode::SVF_NOTCH,       FilterMode::OFF};
	const FilterMode hpf_modes[] = {FilterMode::SVF_BAND, FilterMode::SVF_NOTCH, FilterMode::HPLADDER, FilterMode::OFF};
	for (const FilterMode lpf_mode : lpf_modes) {
		for (const FilterMode hpf_mode : hpf_modes) {
			const int lpf = static_cast<int>(lpf_mode);
			const int hpf = static_cast<int>(hpf_mode);
			deluge::edits::effectSettingFor(*ctx.mod, "lpfMode",
			                                Value::makeScalar(std::string{deluge::effects::to_string(lpf_mode)}));
			deluge::edits::effectSettingFor(*ctx.mod, "hpfMode",
			                                Value::makeScalar(std::string{deluge::effects::to_string(hpf_mode)}));
			for (const Morph& morph : morphs) {
				const Legacy legacy = legacy_of(morph.id, morph.engine);
				const ParamNameContext state{.lpf_mode = lpf_mode, .hpf_mode = hpf_mode};
				const std::string at = "morph " + std::to_string(morph.id.value) + " lpf " + std::to_string(lpf)
				                       + " hpf " + std::to_string(hpf) + " ";
				expect(at + param_text(morph.engine, morph.id, state))
				    .to_equal(at + params::getParamDisplayName(legacy.kind, legacy.index, ctx.mod));
			}
		}
	}
}

} // namespace

// clang-format off
describe setting_names_spec_body("The host's name table", $ {
	it("names every row a menu shows under its owner, and no skipped row", _ {
		for (const OwnedTable& table : owned_tables()) {
			for (const SettingInfo& row : table.rows) {
				const bool shown = covered(table.owner, row.path);
				const std::optional<SettingGroupId> group = table.owner.group();
				const bool excused = skipped(row.path) || (group && no_menu(*group, row.path));
				const std::string at = std::string{table.label} + " " + std::string{row.path};
				expect(at + (setting_names(table.owner, row.path) != nullptr ? " named" : " unnamed"))
				    .to_equal(at + (shown ? " named" : (excused ? " unnamed" : " unlisted")));
			}
		}
	});
	it("names the same path differently under owners whose menus differ", _ {
		const auto text_of = [](NamesOwner owner, std::string_view path) {
			const SettingNames* names = setting_names(owner, path);
			return names != nullptr ? text(names->name) : std::string{"<none>"};
		};
		expect(text_of(engine::kDelugeInstrument, "transpose")).to_equal(std::string{"Master transpose"});
		expect(text_of(SettingGroupId::AUDIO_CLIP, "transpose")).to_equal(std::string{"Transpose"});
		expect(text_of(SettingGroupId::NOTE_ROW, "bendRange")).to_equal(std::string{"Bend range"});
		expect(text_of(SettingGroupId::CLIP_VALUES, "bendRange")).to_equal(std::string{"Normal"});
		expect(text_of(SettingGroupId::AUDIO_INPUT, "mode")).to_equal(std::string{"Audio source"});
		expect(text_of(engine::kDelugeInstrument, "mode")).to_equal(std::string{"Synth Mode"});
		expect(setting_names(SettingGroupId::TRACK, "mode") == nullptr).to_be_true();
	});
	it("gives a CHOICE row with a legacy list one name per choice", _ {
		for (const Case* c : all_name_cases()) {
			const SettingInfo& row = row_of(*c);
			if (row.kind != SettingKind::CHOICE || !has_options(c->item)) {
				continue;
			}
			for (const NamesOwner& owner : owners_of(c->family)) {
				const SettingNames* names = setting_names(owner, c->path);
				expect(names != nullptr).to_be_true();
				if (names == nullptr) {
					continue;
				}
				expect(std::string{c->path} + " " + std::to_string(names->choices.size()))
				    .to_equal(std::string{c->path} + " " + std::to_string(row.choices.size()));
			}
		}
	});
	it("names each row as its legacy item does: the voice rows", _ {
		run_names(self, voice_cases());
	});
	it("names each row as its legacy item does: the oscillator rows", _ {
		run_names(self, oscillator_cases());
		run_plugged_names(self);
	});
	it("names each row as its legacy item does: the LFO and modulator rows", _ {
		run_names(self, lfo_and_modulator_cases());
	});
	it("names each row as its legacy item does: the unison and MIDI rows", _ {
		run_names(self, unison_and_midi_cases());
	});
	it("names each row as its legacy item does: the effect rows", _ {
		run_names(self, effect_cases());
	});
	it("names each row as its legacy item does: the arp rows", _ {
		run_names(self, arp_cases());
		run_names(self, arp_host_cases());
	});
	it("names each row as its legacy item does: the model rows", _ {
		run_names(self, model_cases());
	});
	it("names each row as its legacy item does: the note row, clip, instrument device and audio input rows", _ {
		run_names(self, group_cases());
	});
	it("names every instrument param as getParamDisplayName does", _ {
		check_params(self, deluge::instrument::kParams, engine::kDelugeInstrument);
	});
	it("names every global effectable param as getParamDisplayName does", _ {
		check_params(self, deluge::global_effectable::kParams, engine::kGlobalEffectable);
	});
	it("names the morph params as legacy does under each pair of filter modes", _ {
		check_morphs(self);
	});
	it("names no id outside an engine's table", _ {
		using deluge::engine::ParamId;
		const auto none = deluge::l10n::String::STRING_FOR_NONE;
		const ParamNameContext state{};
		expect(deluge::l10n::param_name(engine::kGlobalEffectable, ParamId{0}, state) == none).to_be_true();
		expect(deluge::l10n::param_name(engine::kGlobalEffectable, ParamId{89}, state) == none).to_be_true();
		expect(deluge::l10n::param_name(engine::kGlobalEffectable, ParamId{133}, state) == none).to_be_true();
		for (uint32_t id = 117; id <= 132; ++id) {
			expect(deluge::l10n::param_name(engine::kDelugeInstrument, ParamId{id}, state) == none).to_be_true();
		}
		for (uint32_t id = 55; id < 90; ++id) {
			expect(deluge::l10n::param_name(engine::kDelugeInstrument, ParamId{id}, state) == none).to_be_true();
		}
		expect(deluge::l10n::param_name(engine::kDelugeInstrument, ParamId{116}, state)
		       == deluge::l10n::String::STRING_FOR_PORTAMENTO)
		    .to_be_true();
		expect(deluge::l10n::param_name(engine::kGlobalEffectable, ParamId{116}, state)
		       == deluge::l10n::String::STRING_FOR_MOD_FX_RATE)
		    .to_be_true();
		expect(deluge::l10n::param_name(engine::EngineId{"other.engine"}, ParamId{90}, state) == none).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(setting_names_spec, setting_names_spec_body)
