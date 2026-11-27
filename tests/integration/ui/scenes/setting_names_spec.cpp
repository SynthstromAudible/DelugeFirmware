/// @file
/// @brief The host's name table against the reference rows of the menu items and the legacy param names.
///
/// Every setting row a menu shows, in every table that owns one (the engines' and each model setting group), has
/// names under its owner, equal to what its item showed (`tests/golden/ui/menu_oracle/descriptors.tsv`): its title and
/// name, and for a list its options (full and short) in each context the list changes with. A row with no single item
/// is one of `descriptor_oracle_spec.cpp`'s named skips, and a row no menu shows is named below; neither has names.
/// Every param of the two engine tables has a name equal to `getParamDisplayName`'s, the morph params under each pair
/// of filter modes included, and an id outside an engine's table has none.

#include "support/descriptor_cases.h"
#include "support/menu_engine.h"

#include "gui/l10n/setting_names.h"
#include "host/chains/params.h"
#include "instruments/classic/params.h"
#include "model/project_bridge/param_ids.h"
#include "support/matchers/value_matchers.h"

namespace {

using deluge::l10n::NamesOwner;
using deluge::l10n::ParamNameContext;
using deluge::l10n::setting_names;
using deluge::l10n::SettingNames;
using deluge::project::SettingGroupId;
namespace params = deluge::modulation::params;
namespace plugin = deluge::plugin;

/// @brief A table and its owner, as a row of it is looked up.
struct OwnedTable {
	NamesOwner owner;
	std::string_view label;
	std::span<const SettingInfo> rows;
};

std::vector<OwnedTable> owned_tables() {
	using deluge::project::table_of;
	return {
	    {plugin::kDelugeInstrument, "instrument", deluge::instruments::classic::kSettings},
	    {plugin::kDelugeInstrument, "instrument effects", deluge::effects::kEffectSettings},
	    {plugin::kGlobalEffectable, "global effectable effects", deluge::effects::kEffectSettings},
	    {plugin::kDelugeInstrument, "instrument arp", deluge::plugin::kArpSettingRows},
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
	    {SettingGroupId::SOUND_ROUTING, "SOUND_ROUTING", table_of(SettingGroupId::SOUND_ROUTING)},
	};
}

/// @brief Whether @p owner's tables hold @p path, or a path under it where it ends in '/'.
bool holds(const NamesOwner& owner, std::string_view path) {
	for (const OwnedTable& table : owned_tables()) {
		if (!(table.owner == owner)) {
			continue;
		}
		for (const SettingInfo& row : table.rows) {
			if (row.path == path || (path.ends_with('/') && row.path.starts_with(path))) {
				return true;
			}
		}
	}
	return false;
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

/// @brief Whether the item showing @p c lists options, which its rows say by holding them.
bool has_options(const Case& c) {
	return frozen().find(case_key(c), "options/full") != nullptr;
}

/// @brief The groups whose rows are named but not driven by the range, read and write checks: their cases list the
/// legacy options by menu position, which is the row's choice order from its first choice.
bool named_only(const Case& c) {
	return c.family == Family::NOTE_ROW || c.family == Family::CLIP || c.family == Family::INSTRUMENT_DEVICE
	       || c.family == Family::AUDIO_INPUT;
}

/// @brief One case's names, under each owner of its table, against its item's rows in its context.
/// @param variant The column suffix of a state the rows were also taken in (`@plugged`).
void check_names(auto& self, const Case& c, std::string_view variant = {}) {
	enter(c.where, c.source);
	const SettingInfo& row = row_of(c);
	const std::string key = case_key(c);
	const auto column = [&](const std::string& base) { return base + std::string{variant}; };
	const std::vector<std::string>* full = frozen().find(key, column("options/full"));
	const std::vector<std::string>* shortened = frozen().find(key, column("options/short"));
	for (const NamesOwner& owner : owners_of(c.family)) {
		const SettingNames* names = setting_names(owner, c.path, context_of(c.where, c.family));
		expect(std::string{c.path} + (names != nullptr ? " has names" : " has no names"))
		    .to_equal(std::string{c.path} + " has names");
		if (names == nullptr) {
			continue;
		}
		expect(std::string{c.path} + " title " + title_of(*names, c.path))
		    .to_equal(std::string{c.path} + " title " + cell(key, column("title")));
		expect(std::string{c.path} + " name " + text(names->name))
		    .to_equal(std::string{c.path} + " name " + cell(key, column("name")));
		if (full == nullptr) {
			expect(std::string{c.path} + " lists " + std::to_string(names->choices.size()))
			    .to_equal(std::string{c.path} + " lists 0");
			continue;
		}
		expect(shortened).not_().to_be_null(std::string{c.path} + " has short options");
		if (shortened == nullptr) {
			continue;
		}
		// A CHOICE row's list is its applicable choices in order, as main's (kStoredChoiceDifferences undone); any
		// other row's is its menu values.
		std::vector<int32_t> choices = main_choices(
		    c.path, c.where, applicable(row, context_of(c.where, c.family), enter(c.where, c.source), c.where));
		if (row.kind != SettingKind::CHOICE || named_only(c)) {
			choices.clear();
			for (size_t i = 0; i < full->size(); ++i) {
				choices.push_back(static_cast<int32_t>(i));
			}
		}
		expect(std::string{c.path} + " offers " + std::to_string(full->size()))
		    .to_equal(std::string{c.path} + " offers " + std::to_string(choices.size()));
		expect(std::string{c.path} + " short options " + std::to_string(shortened->size()))
		    .to_equal(std::string{c.path} + " short options " + std::to_string(full->size()));
		for (size_t position = 0; position < choices.size() && position < full->size() && position < shortened->size();
		     ++position) {
			const int32_t choice = choices[position];
			const std::string at = std::string{c.path} + " choice " + std::to_string(choice);
			expect(at + " " + choice_text(*names, static_cast<size_t>(choice), false))
			    .to_equal(at + " " + (*full)[position]);
			expect(at + " short " + choice_text(*names, static_cast<size_t>(choice), true, c.where == Where::KIT_LANE))
			    .to_equal(at + " short " + (*shortened)[position]);
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
			check_names(self, *c, "@plugged");
		}
	}
	AudioEngine::micPluggedIn = false;
}

/// @brief The legacy kind and index of the instrument or global effectable param @p id.
struct Legacy {
	params::Kind kind;
	int32_t index;
};
Legacy legacy_of(deluge::plugin::ParamId id, plugin::PluginId which) {
	params::Kind kind = params::Kind::UNPATCHED_GLOBAL;
	if (which == plugin::kDelugeInstrument) {
		kind = id.value < deluge::instruments::classic::kUnpatchedFirstId ? params::Kind::PATCHED
		                                                                  : params::Kind::UNPATCHED_SOUND;
	}
	return {kind, deluge::bridge::runtimeIndexOf(kind, id).value_or(-1)};
}

std::string param_text(plugin::PluginId which, deluge::plugin::ParamId id, const ParamNameContext& state) {
	return deluge::l10n::get(deluge::l10n::param_name(which, id, state));
}

void check_params(auto& self, std::span<const deluge::plugin::ParamInfo> table, plugin::PluginId which) {
	for (const deluge::plugin::ParamInfo& row : table) {
		const Legacy legacy = legacy_of(row.id, which);
		to_be_at_least(expect(legacy.index), 0);
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
		deluge::plugin::ParamId id;
		plugin::PluginId engine;
	};
	const Morph morphs[] = {
	    {deluge::instruments::classic::param::kLpfMorph, plugin::kDelugeInstrument},
	    {deluge::instruments::classic::param::kHpfMorph, plugin::kDelugeInstrument},
	    {deluge::host::chains::param::kLpfMorph, plugin::kGlobalEffectable},
	    {deluge::host::chains::param::kHpfMorph, plugin::kGlobalEffectable},
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
			deluge::edits::effectSettingFor(*deluge::ui::scenes::spec::menu_engine(ctx, song_of(Where::SYNTH)).mod,
			                                "lpfMode",
			                                Value::makeScalar(std::string{deluge::effects::to_string(lpf_mode)}));
			deluge::edits::effectSettingFor(*deluge::ui::scenes::spec::menu_engine(ctx, song_of(Where::SYNTH)).mod,
			                                "hpfMode",
			                                Value::makeScalar(std::string{deluge::effects::to_string(hpf_mode)}));
			const std::optional<deluge::model::EffectsOwner> owner =
			    deluge::ui::scenes::spec::menu_engine(ctx, song_of(Where::SYNTH)).mod->effectsOwner();
			for (const Morph& morph : morphs) {
				const Legacy legacy = legacy_of(morph.id, morph.engine);
				const ParamNameContext state{.lpf_mode = lpf_mode, .hpf_mode = hpf_mode};
				const std::string at = "morph " + std::to_string(morph.id.value) + " lpf " + std::to_string(lpf)
				                       + " hpf " + std::to_string(hpf) + " ";
				expect(at + param_text(morph.engine, morph.id, state))
				    .to_equal(at + params::getParamDisplayName(legacy.kind, legacy.index, &*owner));
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
				const bool excused = skipped(table.owner, row.path) || (group && no_menu(*group, row.path));
				const std::string at = std::string{table.label} + " " + std::string{row.path};
				expect(at + (setting_names(table.owner, row.path) != nullptr ? " named" : " unnamed"))
				    .to_equal(at + (shown ? " named" : (excused ? " unnamed" : " unlisted")));
			}
		}
	});
	it("names only rows its owner holds, in the skips and the no-menu rows", _ {
		for (const Skip& skip : kSkips) {
			for (const NamesOwner& owner : skip.owners) {
				const std::string at = "skip " + std::string{skip.path} + " of owner " + std::to_string(owner.index());
				expect(at + (holds(owner, skip.path) ? " held" : " not held")).to_equal(at + " held");
			}
		}
		for (const NoMenu& row : kNoMenu) {
			const std::string at = "no-menu row " + std::string{row.path} + " of group " + std::to_string(std::to_underlying(row.group));
			expect(at + (holds(row.group, row.path) ? " held" : " not held")).to_equal(at + " held");
		}
		for (const Exception& exception : kExceptions) {
			const std::string at = "exception " + std::string{exception.path} + " of owner " + std::to_string(exception.owner.index());
			expect(at + (holds(exception.owner, exception.path) ? " held" : " not held")).to_equal(at + " held");
		}
	});
	it("names the same path differently under owners whose menus differ", _ {
		const auto text_of = [](NamesOwner owner, std::string_view path) {
			const SettingNames* names = setting_names(owner, path);
			return names != nullptr ? text(names->name) : std::string{"<none>"};
		};
		expect(text_of(plugin::kDelugeInstrument, "transpose")).to_equal(std::string{"Master transpose"});
		expect(text_of(SettingGroupId::AUDIO_CLIP, "transpose")).to_equal(std::string{"Transpose"});
		expect(text_of(SettingGroupId::NOTE_ROW, "bendRange")).to_equal(std::string{"Bend range"});
		expect(text_of(SettingGroupId::CLIP_VALUES, "bendRange")).to_equal(std::string{"Normal"});
		expect(text_of(SettingGroupId::AUDIO_INPUT, "mode")).to_equal(std::string{"Audio source"});
		expect(text_of(plugin::kDelugeInstrument, "mode")).to_equal(std::string{"Synth Mode"});
		expect(setting_names(SettingGroupId::TRACK, "mode")).to_be_null();
	});
	it("gives a CHOICE row with a list one name per choice", _ {
		for (const Case* c : all_name_cases()) {
			const SettingInfo& row = row_of(*c);
			if (row.kind != SettingKind::CHOICE || !has_options(*c)) {
				continue;
			}
			for (const NamesOwner& owner : owners_of(c->family)) {
				const SettingNames* names = setting_names(owner, c->path);
				expect(names).not_().to_be_null();
				if (names == nullptr) {
					continue;
				}
				expect(std::string{c->path} + " " + std::to_string(names->choices.size()))
				    .to_equal(std::string{c->path} + " " + std::to_string(row.choices.size()));
			}
		}
	});
	it("names each row as its item's rows do: the voice rows", _ {
		run_names(self, voice_cases());
	});
	it("names each row as its item's rows do: the oscillator rows", _ {
		run_names(self, oscillator_cases());
		run_plugged_names(self);
	});
	it("names each row as its item's rows do: the LFO and modulator rows", _ {
		run_names(self, lfo_and_modulator_cases());
	});
	it("names each row as its item's rows do: the unison and MIDI rows", _ {
		run_names(self, unison_and_midi_cases());
	});
	it("names each row as its item's rows do: the effect rows", _ {
		run_names(self, effect_cases());
	});
	it("names each row as its item's rows do: the arp rows", _ {
		run_names(self, arp_cases());
		run_names(self, arp_host_cases());
	});
	it("names each row as its item's rows do: the model rows", _ {
		run_names(self, model_cases());
	});
	it("names each row as its item's rows do: the note row, clip, instrument device and audio input rows", _ {
		run_names(self, group_cases());
	});
	it("names every instrument param as getParamDisplayName does", _ {
		check_params(self, deluge::instruments::classic::kParams, plugin::kDelugeInstrument);
	});
	it("names every global effectable param as getParamDisplayName does", _ {
		check_params(self, deluge::host::chains::kParams, plugin::kGlobalEffectable);
	});
	it("names the morph params as legacy does under each pair of filter modes", _ {
		check_morphs(self);
	});
	it("names no id outside an engine's table", _ {
		using deluge::plugin::ParamId;
		const auto none = deluge::l10n::String::STRING_FOR_NONE;
		const ParamNameContext state{};
		expect(deluge::l10n::param_name(plugin::kGlobalEffectable, ParamId{0}, state)).to_equal(none);
		expect(deluge::l10n::param_name(plugin::kGlobalEffectable, ParamId{89}, state)).to_equal(none);
		expect(deluge::l10n::param_name(plugin::kGlobalEffectable, ParamId{133}, state)).to_equal(none);
		for (uint32_t id = 117; id <= 132; ++id) {
			expect(deluge::l10n::param_name(plugin::kDelugeInstrument, ParamId{id}, state)).to_equal(none);
		}
		for (uint32_t id = 55; id < 90; ++id) {
			expect(deluge::l10n::param_name(plugin::kDelugeInstrument, ParamId{id}, state)).to_equal(none);
		}
		expect(deluge::l10n::param_name(plugin::kDelugeInstrument, ParamId{116}, state))
		    .to_equal(deluge::l10n::String::STRING_FOR_PORTAMENTO);
		expect(deluge::l10n::param_name(plugin::kGlobalEffectable, ParamId{116}, state))
		    .to_equal(deluge::l10n::String::STRING_FOR_MOD_FX_RATE);
		expect(deluge::l10n::param_name(plugin::PluginId{"other.engine"}, ParamId{90}, state)).to_equal(none);
	});
});

describe setting_names_reference_rows("setting_names_spec: the reference rows", $ {
	it("parses descriptors.tsv clean and asks every row of its title, name and options columns", _ {
		spec::OracleTable& t = spec::OracleTable::named("descriptors");
		expect(t.problems().size()).to_equal(size_t{0});
		expect(t.size()).to_be_greater_than(size_t{3000});
		expect(t.unasked({"title", "name", "options/"})).to_equal(std::string{});
	});
});
// clang-format on

DELUGE_SPEC(setting_names_spec, setting_names_spec_body, setting_names_reference_rows)
