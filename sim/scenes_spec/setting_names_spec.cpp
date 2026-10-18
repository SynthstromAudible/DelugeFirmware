/// @file
/// @brief The host's name table against the legacy menu items and the legacy param names.
///
/// Every setting row a menu shows has names in the table, equal to what its legacy item shows: its title and name, and
/// for a list its options (full and short) in each context the list changes with. A row with no single legacy item is
/// one of `descriptor_legacy_spec.cpp`'s named skips and has no names. Every param of the two engine tables has a name
/// equal to `getParamDisplayName`'s, the morph params under each pair of filter modes included.
///
/// @note The SPEC may include `gui/menu_item/`; the library may not (`scripts/check_layer_rules.py`).

#include "support/descriptor_cases.h"

#include "engines/deluge/params.h"
#include "engines/global_effectable/params.h"
#include "gui/l10n/setting_names.h"
#include "model/project_bridge/param_ids.h"

namespace {

using deluge::l10n::ParamNameContext;
using deluge::l10n::ParamTable;
using deluge::l10n::setting_names;
using deluge::l10n::SettingNames;
namespace params = deluge::modulation::params;

/// @brief A case whose names the table gives for the path's other row, and why.
struct NameException {
	std::string_view path;
	Family family;
	std::string_view reason;
};

/// Two tables' rows that share a path, where legacy names them differently: the table names the engine's.
constexpr NameException kNameExceptions[] = {
    {"transpose", Family::AUDIO_CLIP,
     "the audio clip's transpose shares its path with the instrument's master transpose, which the table names "
     "(\"Master transpose\"); legacy shows the clip's as \"Transpose\""},
};

bool excepted(const Case& c) {
	for (const NameException& e : kNameExceptions) {
		if (e.path == c.path && e.family == c.family) {
			return true;
		}
	}
	return false;
}

std::string text(deluge::l10n::String id) {
	return std::string{deluge::l10n::getView(id)};
}

/// @brief A title with its `*` replaced by the owner's number, which the path's first digit gives as legacy's
/// formatted titles do.
std::string title_of(const SettingNames& names, std::string_view path) {
	std::string title = text(names.title);
	const size_t star = title.find('*');
	const size_t digit = path.find_first_of("0123456789");
	if (star != std::string::npos && digit != std::string_view::npos) {
		title[star] = path[digit];
	}
	return title;
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

/// @brief The names table's text for choice @p index, full or short (short falls back to full where there are none).
std::string choice_text(const SettingNames& names, size_t index, bool short_form) {
	const auto list = short_form && !names.short_choices.empty() ? names.short_choices : names.choices;
	return index < list.size() ? text(list[index]) : std::string{"<no choice>"};
}

/// @brief The input choices' first, whose legacy name depends on whether any input is plugged in.
bool input_choice_unplugged(const Case& c, int32_t choice) {
	return c.path.starts_with("osc") && c.path.ends_with("/type") && choice == 9
	       && !(AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn);
}

/// @brief One case's names against its legacy item in its context.
void check_names(auto& self, const Case& c) {
	enter(c.where, c.source);
	const SettingNames* names = setting_names(c.path);
	expect(std::string{c.path} + (names != nullptr ? " has names" : " has no names"))
	    .to_equal(std::string{c.path} + " has names");
	if (names == nullptr || excepted(c)) {
		return;
	}
	expect(std::string{c.path} + " title " + title_of(*names, c.path))
	    .to_equal(std::string{c.path} + " title " + std::string{c.item->getTitle()});
	expect(std::string{c.path} + " name " + text(names->name))
	    .to_equal(std::string{c.path} + " name " + std::string{c.item->getName()});

	const SettingInfo& row = row_of(c);
	const std::vector<std::string> full = options_of(c.item, legacy::Selection::OptType::FULL);
	const std::vector<std::string> shortened = options_of(c.item, legacy::Selection::OptType::SHORT);
	if (!has_options(c.item)) {
		expect(std::string{c.path} + " lists " + std::to_string(names->choices.size()))
		    .to_equal(std::string{c.path} + " lists 0");
		return;
	}
	// A CHOICE row's list is its applicable choices in order; any other row's is its menu values.
	std::vector<int32_t> choices = applicable(row, context_of(c.where, c.source));
	if (row.kind != SettingKind::CHOICE) {
		choices.clear();
		for (size_t i = 0; i < full.size(); ++i) {
			choices.push_back(static_cast<int32_t>(i));
		}
	}
	expect(std::string{c.path} + " offers " + std::to_string(full.size()))
	    .to_equal(std::string{c.path} + " offers " + std::to_string(choices.size()));
	for (size_t position = 0; position < choices.size() && position < full.size(); ++position) {
		const int32_t choice = choices[position];
		if (input_choice_unplugged(c, choice)) {
			continue;
		}
		const std::string at = std::string{c.path} + " choice " + std::to_string(choice);
		expect(at + " " + choice_text(*names, static_cast<size_t>(choice), false)).to_equal(at + " " + full[position]);
		expect(at + " short " + choice_text(*names, static_cast<size_t>(choice), true))
		    .to_equal(at + " short " + shortened[position]);
	}
}

void run_names(auto& self, const std::vector<Case>& cases) {
	AudioEngine::micPluggedIn = false;
	AudioEngine::lineInPluggedIn = false;
	for (const Case& c : cases) {
		check_names(self, c);
	}
}

/// @brief The lone oscillator type cases again with an input plugged in: the input choices are named there.
void run_plugged_names(auto& self) {
	AudioEngine::micPluggedIn = true;
	for (const Case* c : all_cases()) {
		if (c->path.starts_with("osc") && c->path.ends_with("/type")) {
			check_names(self, *c);
		}
	}
	AudioEngine::micPluggedIn = false;
}

/// @brief Whether some case names @p path.
bool covered(std::string_view path) {
	for (const Case* c : all_cases()) {
		if (c->path == path) {
			return true;
		}
	}
	return false;
}

/// @brief The legacy kind and index of the instrument or global effectable param @p id.
struct Legacy {
	params::Kind kind;
	int32_t index;
};
Legacy legacy_of(deluge::engine::ParamId id, ParamTable table) {
	params::Kind kind = params::Kind::UNPATCHED_GLOBAL;
	if (table == ParamTable::INSTRUMENT) {
		kind = id.value < deluge::instrument::kUnpatchedFirstId ? params::Kind::PATCHED : params::Kind::UNPATCHED_SOUND;
	}
	return {kind, deluge::bridge::runtimeIndexOf(kind, id).value_or(-1)};
}

std::string param_text(deluge::engine::ParamId id, const ParamNameContext& state) {
	return deluge::l10n::get(deluge::l10n::param_name(id, state));
}

void check_params(auto& self, std::span<const deluge::engine::ParamInfo> table, ParamTable which) {
	for (const deluge::engine::ParamInfo& row : table) {
		const Legacy legacy = legacy_of(row.id, which);
		expect(legacy.index >= 0).to_be_true();
		// Legacy's plain name is the SVF one: the default filter modes are ladders, which rename the morph params.
		const ParamNameContext state{.table = which,
		                             .lpf_mode = deluge::effects::FilterMode::SVF_NOTCH,
		                             .hpf_mode = deluge::effects::FilterMode::SVF_BAND};
		expect(std::string{row.file_name} + " " + param_text(row.id, state))
		    .to_equal(std::string{row.file_name} + " " + params::getParamDisplayName(legacy.kind, legacy.index));
	}
}

/// @brief The morph params under every pair of filter modes, against legacy's names with the filters set.
void check_morphs(auto& self) {
	const MenuContext& ctx = enter(Where::SYNTH);
	struct Morph {
		deluge::engine::ParamId id;
		ParamTable table;
	};
	const Morph morphs[] = {
	    {deluge::instrument::param::kLpfMorph, ParamTable::INSTRUMENT},
	    {deluge::instrument::param::kHpfMorph, ParamTable::INSTRUMENT},
	    {deluge::global_effectable::param::kLpfMorph, ParamTable::GLOBAL_EFFECTABLE},
	    {deluge::global_effectable::param::kHpfMorph, ParamTable::GLOBAL_EFFECTABLE},
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
				const Legacy legacy = legacy_of(morph.id, morph.table);
				const ParamNameContext state{.table = morph.table, .lpf_mode = lpf_mode, .hpf_mode = hpf_mode};
				const std::string at = "morph " + std::to_string(morph.id.value) + " lpf " + std::to_string(lpf)
				                       + " hpf " + std::to_string(hpf) + " ";
				expect(at + param_text(morph.id, state))
				    .to_equal(at + params::getParamDisplayName(legacy.kind, legacy.index, ctx.mod));
			}
		}
	}
}

} // namespace

// clang-format off
describe setting_names_spec_body("The host's name table", $ {
	it("names every row a menu shows, and no skipped row", _ {
		using deluge::project::SettingGroupId;
		const std::span<const SettingInfo> tables[] = {
		    deluge::instrument::kSettings,
		    deluge::effects::kEffectSettings,
		    deluge::engine::kArpSettingRows,
		    deluge::engine::kArpHostRows,
		    deluge::project::table_of(SettingGroupId::SONG),
		    deluge::project::table_of(SettingGroupId::AUDIO_CLIP),
		    deluge::project::table_of(SettingGroupId::CLIP_VALUES),
		};
		for (auto table : tables) {
			for (const SettingInfo& row : table) {
				const bool shown = covered(row.path);
				expect(std::string{row.path} + (setting_names(row.path) != nullptr ? " named" : " unnamed"))
				    .to_equal(std::string{row.path} + (shown ? " named" : (skipped(row.path) ? " unnamed" : " skip-unlisted")));
			}
		}
	});
	it("gives a CHOICE row with a legacy list one name per choice", _ {
		for (const Case* c : all_cases()) {
			const SettingInfo& row = row_of(*c);
			if (row.kind != SettingKind::CHOICE || !has_options(c->item) || excepted(*c)) {
				continue;
			}
			const SettingNames* names = setting_names(c->path);
			expect(names != nullptr).to_be_true();
			if (names == nullptr) {
				continue;
			}
			expect(std::string{c->path} + " " + std::to_string(names->choices.size()))
			    .to_equal(std::string{c->path} + " " + std::to_string(row.choices.size()));
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
	it("names every instrument param as getParamDisplayName does", _ {
		check_params(self, deluge::instrument::kParams, ParamTable::INSTRUMENT);
	});
	it("names every global effectable param as getParamDisplayName does", _ {
		check_params(self, deluge::global_effectable::kParams, ParamTable::GLOBAL_EFFECTABLE);
	});
	it("names the morph params as legacy does under each pair of filter modes", _ {
		check_morphs(self);
	});
});
// clang-format on

DELUGE_SPEC(setting_names_spec, setting_names_spec_body)
