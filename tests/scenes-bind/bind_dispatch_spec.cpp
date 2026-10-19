#include "engine-api/types/engine_id.h"
#include "engine-api/types/setting.h"
#include "engines/deluge/settings.h"
#include "gui/l10n/setting_names.h"
#include "scenes/bind/dispatch.h"
#include "scenes/bind/setting_context.h"
#include "scenes/bind/slot_reader.h"
#include "support/model_fixture.h"

#include <cppspec.hpp>
#include <string>
#include <vector>

using namespace deluge::scenes::bind;
using deluge::instrument::OscType;
using deluge::l10n::String;
using deluge::scenes::bind::test::kDrum;
using deluge::scenes::bind::test::kSynth;
using deluge::scenes::bind::test::ModelFixture;

namespace {
const deluge::engine::SettingInfo& row_at(std::string_view path) {
	return *deluge::engine::find_setting(deluge::instrument::kSettings, path);
}

Target sound(std::string_view path) {
	return {.kind = Target::Kind::SOUND, .path = path};
}

Addresses at(deluge::project::SlotAddress slot) {
	return {.slot = slot};
}

/// The names choice_names gives, as their ids, an unavailable one written negative.
std::vector<int64_t> names_of(const ModelFixture& fixture, std::string_view path, deluge::project::SlotAddress slot) {
	std::array<ChoiceName, 32> out{};
	const size_t count = choice_names(sound(path), fixture.song, at(slot), fixture.facts, out);
	std::vector<int64_t> ids;
	for (size_t i = 0; i < count; ++i) {
		const auto id = static_cast<int64_t>(out[i].name);
		ids.push_back(out[i].unavailable ? -id : id);
	}
	return ids;
}
} // namespace

// clang-format off
describe bind_dispatch("a sound target's value, range, choices, names and applicability", ${
	it("lists only the choices that apply, by position, with the name table's names", _ {
		const ModelFixture fixture;
		const auto shown = view(sound("osc2/type"), fixture.song, at(kSynth), fixture.facts);
		expect(shown.has_value()).to_be_true();
		const SlotReader reader{fixture.song, kSynth, fixture.facts};
		const auto shown_for = setting_context(fixture.song, kSynth, fixture.facts);
		const auto& row = row_at("osc2/type");
		expect(shown->greatest + 1).to_equal(deluge::engine::applicable_choice_count(row, shown_for, reader.reader()));
		expect(shown->stale.has_value()).to_be_false();
		// With no input plugged in, the one input entry is named as legacy names it ("Input"), by the context.
		const auto* names = deluge::l10n::setting_names(deluge::engine::kDelugeInstrument, "osc2/type", shown_for);
		std::vector<int64_t> want;
		for (size_t choice = 0; choice < row.choices.size(); ++choice) {
			if (deluge::engine::setting_choice_applies(row, shown_for, reader.reader(), choice)) {
				want.push_back(static_cast<int64_t>(names->choices[choice]));
			}
		}
		expect(names_of(fixture, "osc2/type", kSynth)).to_equal(want);
	});

	it("lists a two-way setting a menu shows as a list (the FM modulator's destination) by the name table's names", _ {
		const ModelFixture fixture;
		const auto* table = deluge::l10n::setting_names(deluge::engine::kDelugeInstrument, "modulator2/toModulator1");
		std::vector<int64_t> want;
		for (const String name : table->choices) {
			want.push_back(static_cast<int64_t>(name));
		}
		expect(want.size()).to_equal(size_t(2));
		expect(names_of(fixture, "modulator2/toModulator1", kSynth)).to_equal(want);
	});

	it("shows a stored choice that no longer applies as a marked entry the menu cannot write", _ {
		ModelFixture fixture;
		auto state = fixture.facts.states.at(kDrum.id);
		state.sources[0].osc_type = OscType::DX7;
		fixture.set_state(kDrum, state);
		const auto shown = view(sound("osc1/type"), fixture.song, at(kDrum), fixture.facts);
		expect(shown->stale.has_value()).to_be_true();
		expect(shown->value).to_equal(*shown->stale);
		const auto names = names_of(fixture, "osc1/type", kDrum);
		const auto* table = deluge::l10n::setting_names(deluge::engine::kDelugeInstrument, "osc1/type");
		const auto dx7 = -static_cast<int64_t>(table->choices[static_cast<size_t>(OscType::DX7)]);
		expect(names[static_cast<size_t>(*shown->stale)]).to_equal(dx7);
		expect(request_for(sound("osc1/type"), fixture.song, at(kDrum), fixture.facts, *shown->stale).has_value())
		    .to_be_false();
		const auto next = request_for(sound("osc1/type"), fixture.song, at(kDrum), fixture.facts, *shown->stale - 1);
		expect(next.has_value()).to_be_true();
		const auto stored = deluge::engine::from_menu(row_at("osc1/type"), next->menu_value);
		expect(stored.has_value() && stored->first != static_cast<int32_t>(OscType::DX7)).to_be_true();
	});

	it("reads a stored value the menu never writes as legacy shows it", _ {
		ModelFixture fixture;
		auto state = fixture.facts.states.at(kSynth.id);
		state.polyphonic = deluge::instrument::PolyphonyMode::POLY;
		state.max_voice_count = 20;
		fixture.set_state(kSynth, state);
		const auto shown = view(sound("maxVoices"), fixture.song, at(kSynth), fixture.facts);
		expect(shown->value).to_equal(deluge::engine::to_menu(row_at("maxVoices"), 20));
	});

	it("reads a combined menu through its partner row", _ {
		ModelFixture fixture;
		auto state = fixture.facts.states.at(kSynth.id);
		state.transpose = -3;
		state.cents = 25;
		state.lfo_sync_level[0] = deluge::engine::SyncLevel{4};
		state.lfo_sync_type[0] = deluge::engine::SyncType::TRIPLET;
		fixture.set_state(kSynth, state);
		const SlotReader reader{fixture.song, kSynth, fixture.facts};
		expect(view(sound("transpose"), fixture.song, at(kSynth), fixture.facts)->value).to_equal(-275);
		expect(view(sound("lfo1/syncLevel"), fixture.song, at(kSynth), fixture.facts)->value)
		    .to_equal(deluge::engine::to_menu(row_at("lfo1/syncLevel"), 4, *reader.read("lfo1/syncType")));
	});

	it("says a row does not apply where its conditions fail", _ {
		ModelFixture fixture;
		auto state = fixture.facts.states.at(kSynth.id);
		state.polyphonic = deluge::instrument::PolyphonyMode::MONO;
		fixture.set_state(kSynth, state);
		expect(view(sound("maxVoices"), fixture.song, at(kSynth), fixture.facts)->applies).to_be_false();
		state.polyphonic = deluge::instrument::PolyphonyMode::POLY;
		fixture.set_state(kSynth, state);
		expect(view(sound("maxVoices"), fixture.song, at(kSynth), fixture.facts)->applies).to_be_true();
	});


	it("resolves every row the table holds, a row whose mapping clamps (no partner) included", _ {
		const ModelFixture fixture;
		std::string unresolved;
		for (const deluge::engine::SettingInfo& row : deluge::instrument::kSettings) {
			if (row.kind == deluge::engine::SettingKind::STRUCTURED || row.kind == deluge::engine::SettingKind::TEXT) {
				continue;
			}
			if (!view(sound(row.path), fixture.song, at(kSynth), fixture.facts)) {
				unresolved += std::string(row.path) + " ";
			}
		}
		expect(unresolved).to_equal(std::string{});
	});

	it("shows nothing for a target whose slot does not resolve", _ {
		const ModelFixture fixture;
		expect(view(sound("polyphonic"), fixture.song, Addresses{}, fixture.facts).has_value()).to_be_false();
		expect(view(sound("polyphonic"), fixture.song, at({deluge::project::SlotAddress::Kind::DRUM, 77}),
		            fixture.facts)
		           .has_value())
		    .to_be_false();
	});
});

CPPSPEC_SPEC(bind_dispatch)
