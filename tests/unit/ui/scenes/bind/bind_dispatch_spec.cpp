#include "gui/l10n/setting_names.h"
#include "instruments/classic/settings.h"
#include "instruments/classic/words.h"
#include "plugin/types/plugin_id.h"
#include "plugin/types/setting.h"
#include "project/device_settings.h"
#include "support/model_fixture.h"
#include "support/record_state.h"
#include "ui/scenes/bind/dispatch.h"
#include "ui/scenes/bind/setting_context.h"
#include "ui/scenes/bind/slot_reader.h"

#include <array>
#include <cppspec.hpp>
#include <string>
#include <utility>
#include <vector>

using namespace deluge::ui::scenes::bind;
using deluge::instruments::classic::OscType;
using deluge::l10n::String;
using deluge::ui::scenes::bind::test::kDrum;
using deluge::ui::scenes::bind::test::kSynth;
using deluge::ui::scenes::bind::test::ModelFixture;

namespace {
const deluge::plugin::SettingInfo& row_at(std::string_view path) {
	return *deluge::plugin::find_setting(deluge::instruments::classic::kSettings, path);
}

Target sound(std::string_view path) {
	return {.kind = Target::Kind::SOUND, .path = path};
}

Addresses at(deluge::project::SlotAddress slot) {
	return {.slot = slot};
}

/// The names choice_names gives, as their ids, an unavailable one written negative.
std::vector<int64_t> names_of(ModelFixture& fixture, std::string_view path, deluge::project::SlotAddress slot) {
	std::array<ChoiceName, 32> out{};
	const size_t count = choice_names(sound(path), &fixture.song, at(slot), fixture.ports(), out);
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
		ModelFixture fixture;
		const auto shown = view(sound("osc2/type"), &fixture.song, at(kSynth), fixture.ports());
		expect(shown.has_value()).to_be_true();
		const SlotReader reader{fixture.song, kSynth, fixture.host.sound};
		const auto shown_for = setting_context(fixture.song, kSynth, fixture.host.sound, fixture.host.audio);
		const auto& row = row_at("osc2/type");
		expect(shown->greatest + 1).to_equal(deluge::plugin::applicable_choice_count(row, shown_for, reader.reader()));
		expect(shown->stale.has_value()).to_be_false();
		// With no input plugged in, the one input entry is named as legacy names it ("Input"), by the context.
		const auto* names = deluge::l10n::setting_names(deluge::plugin::kDelugeInstrument, "osc2/type", shown_for);
		std::vector<int64_t> want;
		for (size_t choice = 0; choice < row.choices.size(); ++choice) {
			if (deluge::plugin::setting_choice_applies(row, shown_for, reader.reader(), choice)) {
				want.push_back(static_cast<int64_t>(names->choices[choice]));
			}
		}
		expect(names_of(fixture, "osc2/type", kSynth)).to_equal(want);
	});

	it("lists a two-way setting a menu shows as a list (the FM modulator's destination) by the name table's names", _ {
		ModelFixture fixture;
		const auto* table = deluge::l10n::setting_names(deluge::plugin::kDelugeInstrument, "modulator2/toModulator1");
		std::vector<int64_t> want;
		for (const String name : table->choices) {
			want.push_back(static_cast<int64_t>(name));
		}
		expect(want.size()).to_equal(size_t(2));
		expect(names_of(fixture, "modulator2/toModulator1", kSynth)).to_equal(want);
	});

	it("shows a stored choice that no longer applies as a marked entry the menu cannot write", _ {
		ModelFixture fixture;
		deluge::instruments::classic::SoundState state = fixture.host.sound.states.at(kDrum.id);
		deluge::test::set(state, "osc1/type", deluge::instruments::classic::to_string(OscType::INPUT_R));
		fixture.set_state(kDrum, state);
		const auto shown = view(sound("osc1/type"), &fixture.song, at(kDrum), fixture.ports());
		expect(shown->stale.has_value()).to_be_true();
		expect(shown->value).to_equal(*shown->stale);
		const auto names = names_of(fixture, "osc1/type", kDrum);
		const auto* table = deluge::l10n::setting_names(deluge::plugin::kDelugeInstrument, "osc1/type");
		const auto input_r = -static_cast<int64_t>(table->choices[static_cast<size_t>(OscType::INPUT_R)]);
		expect(names[static_cast<size_t>(*shown->stale)]).to_equal(input_r);
		expect(request_for(sound("osc1/type"), &fixture.song, at(kDrum), fixture.ports(), *shown->stale).has_value())
		    .to_be_false();
		const auto next = request_for(sound("osc1/type"), &fixture.song, at(kDrum), fixture.ports(), *shown->stale - 1);
		expect(next.has_value()).to_be_true();
		const auto stored = deluge::plugin::from_menu(row_at("osc1/type"), next->menu_value);
		expect(stored.has_value()).to_be_true();
		if (stored.has_value()) {
			expect(stored->first).not_().to_equal(static_cast<int32_t>(OscType::INPUT_R));
		}
	});

	it("reads a stored value the menu never writes as legacy shows it", _ {
		ModelFixture fixture;
		deluge::instruments::classic::SoundState state = fixture.host.sound.states.at(kSynth.id);
		deluge::test::set(state, "polyphonic", deluge::instruments::classic::to_string(deluge::instruments::classic::PolyphonyMode::POLY));
		deluge::test::set(state, "maxVoices", "20");
		fixture.set_state(kSynth, state);
		const auto shown = view(sound("maxVoices"), &fixture.song, at(kSynth), fixture.ports());
		expect(shown->value).to_equal(deluge::plugin::to_menu(row_at("maxVoices"), 20));
	});

	it("reads a combined menu through its partner row", _ {
		ModelFixture fixture;
		deluge::instruments::classic::SoundState state = fixture.host.sound.states.at(kSynth.id);
		deluge::test::set(state, "transpose", "-3");
		deluge::test::set(state, "cents", "25");
		deluge::test::set(state, "lfo1/syncLevel", "4");
		deluge::test::set(state, "lfo1/syncType", std::to_string(std::to_underlying(deluge::plugin::SyncType::TRIPLET)));
		fixture.set_state(kSynth, state);
		const SlotReader reader{fixture.song, kSynth, fixture.host.sound};
		expect(view(sound("transpose"), &fixture.song, at(kSynth), fixture.ports())->value).to_equal(-275);
		expect(view(sound("lfo1/syncLevel"), &fixture.song, at(kSynth), fixture.ports())->value)
		    .to_equal(deluge::plugin::to_menu(row_at("lfo1/syncLevel"), 4, *reader.read("lfo1/syncType")));
	});

	it("says a row does not apply where its conditions fail", _ {
		ModelFixture fixture;
		deluge::instruments::classic::SoundState state = fixture.host.sound.states.at(kSynth.id);
		deluge::test::set(state, "polyphonic", deluge::instruments::classic::to_string(deluge::instruments::classic::PolyphonyMode::MONO));
		fixture.set_state(kSynth, state);
		expect(view(sound("maxVoices"), &fixture.song, at(kSynth), fixture.ports())->applies).to_be_false();
		deluge::test::set(state, "polyphonic", deluge::instruments::classic::to_string(deluge::instruments::classic::PolyphonyMode::POLY));
		fixture.set_state(kSynth, state);
		expect(view(sound("maxVoices"), &fixture.song, at(kSynth), fixture.ports())->applies).to_be_true();
	});


	it("resolves every row the table holds, a row whose mapping clamps (no partner) included", _ {
		ModelFixture fixture;
		std::string unresolved;
		for (const deluge::plugin::SettingInfo& row : deluge::instruments::classic::kSettings) {
			if (row.kind == deluge::plugin::SettingKind::STRUCTURED || row.kind == deluge::plugin::SettingKind::TEXT) {
				continue;
			}
			if (!view(sound(row.path), &fixture.song, at(kSynth), fixture.ports())) {
				unresolved += std::string(row.path) + " ";
			}
		}
		expect(unresolved).to_equal(std::string{});
	});

	it("shows nothing for a target whose slot does not resolve", _ {
		ModelFixture fixture;
		expect(view(sound("polyphonic"), &fixture.song, Addresses{}, fixture.ports()).has_value()).to_be_false();
		expect(view(sound("polyphonic"), &fixture.song, at({deluge::project::SlotAddress::Kind::DRUM, 77}),
		            fixture.ports())
		           .has_value())
		    .to_be_false();
	});

	it("reads, names and asks for a DEVICE target through the device port, with no song", _ {
		test::FakeHost host;
		const Target cursor{.kind = Target::Kind::MODEL,
		                    .path = deluge::project::device_path::kPlayCursor,
		                    .group = deluge::project::SettingGroupId::DEVICE};
		const auto shown = view(cursor, nullptr, Addresses{}, host.ports());
		expect(shown.has_value()).to_be_true();
		if (shown) {
			expect(shown->applies).to_be_true();
			expect(shown->value).to_equal(static_cast<int32_t>(deluge::project::FlashCursor::SLOW));
			expect(shown->greatest).to_equal(2);
		}
		std::array<ChoiceName, 4> out{};
		expect(choice_names(cursor, nullptr, Addresses{}, host.ports(), out)).to_equal(size_t{3});
		expect(static_cast<int>(out[0].name)).to_equal(static_cast<int>(String::STRING_FOR_FAST));
		const auto request = request_for(cursor, nullptr, Addresses{}, host.ports(), 1);
		expect(request.has_value()).to_be_true();
		if (request) {
			expect(request->menu_value).to_equal(1);
		}
		expect(request_for(cursor, nullptr, Addresses{}, host.ports(), 3).has_value()).to_be_false();

		// No device port: the target resolves to nothing, as any unresolved target does. A song's target with no song
		// resolves to nothing too.
		Ports no_device = host.ports();
		no_device.device = nullptr;
		expect(view(cursor, nullptr, Addresses{}, no_device).has_value()).to_be_false();
		expect(view(sound("polyphonic"), nullptr, at(kSynth), host.ports()).has_value()).to_be_false();
		expect(host.device.stores.empty()).to_be_true();
	});

	it("fills a source-indexed target's `*` with the output the addresses name, and resolves nothing past the rows", _ {
		test::FakeHost host;
		host.device.values.insert_or_assign("cv/1/transpose", deluge::serialization::Value::makeNumber(250));
		const Target transpose{.kind = Target::Kind::MODEL,
		                       .path = deluge::project::device_path::kCvTranspose,
		                       .source_indexed = true,
		                       .group = deluge::project::SettingGroupId::DEVICE};
		const Addresses second{.source_index = 1};
		const auto named = concrete(transpose, second);
		expect(named.has_value()).to_be_true();
		if (named) {
			expect(std::string{named->path}).to_equal("cv/1/transpose");
			expect(named->source_indexed).to_be_false();
		}
		const auto shown = view(transpose, nullptr, second, host.ports());
		expect(shown.has_value()).to_be_true();
		if (shown) {
			expect(shown->value).to_equal(250);
		}
		const auto first = view(transpose, nullptr, Addresses{.source_index = 0}, host.ports());
		expect(first.has_value()).to_be_true();
		if (first) {
			expect(first->value).to_equal(0);
		}
		// The write asks for the row the index names.
		const auto request = request_for(transpose, nullptr, second, host.ports(), -120);
		expect(request.has_value()).to_be_true();
		if (request) {
			expect(std::string{request->target.path}).to_equal("cv/1/transpose");
		}

		// An output the table has no row for, or a pattern with no `*`: nothing, as any unresolved target.
		const Addresses sixth{.source_index = 5};
		expect(concrete(transpose, sixth).has_value()).to_be_false();
		expect(view(transpose, nullptr, sixth, host.ports()).has_value()).to_be_false();
		expect(request_for(transpose, nullptr, sixth, host.ports(), 0).has_value()).to_be_false();
		std::array<ChoiceName, 4> out{};
		const Target mode{.kind = Target::Kind::MODEL,
		                  .path = deluge::project::device_path::kGateMode,
		                  .source_indexed = true,
		                  .group = deluge::project::SettingGroupId::DEVICE};
		expect(choice_names(mode, nullptr, sixth, host.ports(), out)).to_equal(size_t{0});
		// Addresses that name no output (a caller that is no selector's menu) resolve it to nothing too.
		expect(concrete(transpose, Addresses{}).has_value()).to_be_false();
		expect(view(transpose, nullptr, Addresses{}, host.ports()).has_value()).to_be_false();
		Target unpatterned = transpose;
		unpatterned.path = deluge::project::device_path::kGateMinOffTime;
		expect(concrete(unpatterned, second).has_value()).to_be_false();
		expect(host.device.stores.empty()).to_be_true();
	});
});

CPPSPEC_SPEC(bind_dispatch)
