#include "engine-api/types/engine_id.h"
#include "engine-api/types/setting.h"
#include "engines/effects/settings.h"
#include "gui/l10n/setting_names.h"
#include "scenes/bind/chain_reader.h"
#include "scenes/bind/dispatch.h"
#include "scenes/bind/setting_context.h"
#include "support/model_fixture.h"

#include <cppspec.hpp>
#include <string>
#include <vector>

using namespace deluge::scenes::bind;
using deluge::effects::FilterMode;
using deluge::project::ChainAddress;
using deluge::scenes::bind::test::FakeFacts;
using deluge::scenes::bind::test::kAudioChain;
using deluge::scenes::bind::test::kDrum;
using deluge::scenes::bind::test::kKitChain;
using deluge::scenes::bind::test::kMasterChain;
using deluge::scenes::bind::test::kSynth;
using deluge::scenes::bind::test::ModelFixture;

namespace {
Target effect(std::string_view path) {
	return {.kind = Target::Kind::EFFECT, .path = path};
}

Addresses on_chain(ChainAddress chain, ChainOwner owner) {
	return {.chain = chain, .chain_owner = owner};
}

/// The names choice_names gives for @p path at @p where, as their ids.
std::vector<int64_t> names_at(const ModelFixture& fixture, std::string_view path, const Addresses& where) {
	std::array<ChoiceName, 32> out{};
	const size_t count = choice_names(effect(path), fixture.song, where, fixture.facts, out);
	std::vector<int64_t> ids;
	for (size_t i = 0; i < count; ++i) {
		ids.push_back(static_cast<int64_t>(out[i].name));
	}
	return ids;
}

/// The names @p owner's table gives @p path in @p context, every choice applying.
std::vector<int64_t> table_names(deluge::engine::EngineId owner, std::string_view path,
                                 deluge::engine::SettingContext shown_for) {
	const auto* names = deluge::l10n::setting_names(owner, path, shown_for);
	std::vector<int64_t> ids;
	if (names != nullptr) {
		for (const auto name : names->choices) {
			ids.push_back(static_cast<int64_t>(name));
		}
	}
	return ids;
}

/// The paths where held_effect_setting does not agree with effect_setting_value read through stored_value.
std::vector<std::string> held_mismatches(const deluge::effects::EffectStates& states) {
	std::vector<std::string> mismatched;
	for (const auto& row : deluge::effects::kEffectSettings) {
		const auto value = deluge::effects::effect_setting_value(states, row.path);
		const auto held = deluge::effects::held_effect_setting(states, row.path);
		std::optional<int32_t> want;
		if (value) {
			if (row.kind == deluge::engine::SettingKind::CHOICE) {
				want = deluge::engine::stored_value(row, value->str());
			}
			else if (const auto n = deluge::engine::whole_number(value->str())) {
				want = static_cast<int32_t>(*n);
			}
		}
		if (held != want) {
			mismatched.emplace_back(row.path);
		}
	}
	return mismatched;
}
} // namespace

// clang-format off
describe bind_effect("an effect target on a sound or a chain", ${
	it("reads an effect row's held value as its file word or number gives it", _ {
		deluge::effects::EffectStates states;
		expect(held_mismatches(states)).to_equal(std::vector<std::string>{});
		states.filter.lpf_mode = FilterMode::OFF;
		states.delay.ping_pong = false;
		states.saturation.clipping_amount = 7;
		states.sidechain.attack = 123456;
		expect(held_mismatches(states)).to_equal(std::vector<std::string>{});
		expect(deluge::effects::held_effect_setting(states, "osc1/type").has_value()).to_be_false();
	});

	it("reads a sound's own effects through its slot", _ {
		ModelFixture fixture;
		auto state = fixture.facts.states.at(kSynth.id);
		state.effects.filter.lpf_mode = FilterMode::SVF_NOTCH;
		fixture.set_state(kSynth, state);
		const auto shown = view(effect("lpfMode"), fixture.song, {.slot = kSynth}, fixture.facts);
		expect(shown.has_value()).to_be_true();
		const auto& row = *deluge::engine::find_setting(deluge::effects::kEffectSettings, "lpfMode");
		expect(shown->value).to_equal(static_cast<int32_t>(deluge::engine::stored_value(row, "SVF_Notch").value_or(-1)));
	});

	it("reads each chain's own effects", _ {
		ModelFixture fixture;
		fixture.facts.chains[FakeFacts::chain_key(kKitChain)].delay.ping_pong = false;
		fixture.facts.chains[FakeFacts::chain_key(kAudioChain)].saturation.clipping_amount = 9;
		fixture.facts.chains[FakeFacts::chain_key(kMasterChain)].delay.ping_pong = false;
		const ChainReader kit{fixture.song, kKitChain, fixture.facts};
		const ChainReader audio{fixture.song, kAudioChain, fixture.facts};
		const ChainReader master{fixture.song, kMasterChain, fixture.facts};
		expect(kit.resolved() && audio.resolved() && master.resolved()).to_be_true();
		expect(kit.read("delay/pingPong").value_or(-1)).to_equal(0);
		expect(audio.read("delay/pingPong").value_or(-1)).to_equal(1);
		expect(audio.read("clippingAmount").value_or(-1)).to_equal(9);
		expect(master.read("delay/pingPong").value_or(-1)).to_equal(0);
		expect(kit.read("osc1/type").has_value()).to_be_false();
		const auto shown = view(effect("clippingAmount"), fixture.song, on_chain(kAudioChain, ChainOwner::AUDIO_TRACK),
		                        fixture.facts);
		expect(shown.has_value() && shown->applies).to_be_true();
		expect(shown->value).to_equal(9);
	});

	it("gives each kind of chain its owner's context", _ {
		using Owner = deluge::engine::SettingContext::Owner;
		using Instrument = deluge::engine::SettingContext::Instrument;
		const auto kit = setting_context(kKitChain, ChainOwner::KIT);
		const auto audio = setting_context(kAudioChain, ChainOwner::AUDIO_TRACK);
		const auto master = setting_context(kMasterChain, ChainOwner::SONG);
		expect(kit.owner == Owner::KIT && kit.instrument == Instrument::KIT).to_be_true();
		expect(audio.owner == Owner::TRACK && audio.instrument == Instrument::AUDIO).to_be_true();
		expect(master.owner == Owner::SONG && master.instrument == Instrument::NONE).to_be_true();
		expect(deluge::engine::context_is_coherent(kit) && deluge::engine::context_is_coherent(audio)
		       && deluge::engine::context_is_coherent(master)).to_be_true();
	});

	it("does not apply saturation on a kit's chain, nor the sidechain on the master's", _ {
		const ModelFixture fixture;
		const auto clipping = view(effect("clippingAmount"), fixture.song, on_chain(kKitChain, ChainOwner::KIT),
		                           fixture.facts);
		expect(clipping.has_value()).to_be_true();
		expect(clipping->applies).to_be_false();
		const auto attack = view(effect("sidechain/attack"), fixture.song, on_chain(kMasterChain, ChainOwner::SONG),
		                         fixture.facts);
		expect(attack.has_value()).to_be_true();
		expect(attack->applies).to_be_false();
		const auto kit_attack = view(effect("sidechain/attack"), fixture.song, on_chain(kKitChain, ChainOwner::KIT),
		                             fixture.facts);
		expect(kit_attack.has_value() && kit_attack->applies).to_be_true();
	});

	it("names a chain's choices from the host chains' table and a sound's from the instrument's", _ {
		const ModelFixture fixture;
		const auto chain = names_at(fixture, "lpfMode", on_chain(kKitChain, ChainOwner::KIT));
		expect(chain).to_equal(table_names(deluge::engine::kGlobalEffectable, "lpfMode",
		                                   setting_context(kKitChain, ChainOwner::KIT)));
		expect(chain.empty()).to_be_false();
		const auto sound = names_at(fixture, "lpfMode", {.slot = kDrum});
		expect(sound).to_equal(table_names(deluge::engine::kDelugeInstrument, "lpfMode",
		                                   setting_context(fixture.song, kDrum, fixture.facts)));
	});

	it("resolves nothing for a chain the song does not hold, or with no address", _ {
		const ModelFixture fixture;
		const ChainAddress gone{ChainAddress::Kind::OUTPUT, 77};
		expect(view(effect("lpfMode"), fixture.song, on_chain(gone, ChainOwner::KIT), fixture.facts).has_value())
		    .to_be_false();
		expect(view(effect("lpfMode"), fixture.song, {}, fixture.facts).has_value()).to_be_false();
		expect(ChainReader(fixture.song, gone, fixture.facts).resolved()).to_be_false();
	});

	it("does not resolve a sound target on a chain", _ {
		const ModelFixture fixture;
		const Target sound{.kind = Target::Kind::SOUND, .path = "lpfMode"};
		expect(view(sound, fixture.song, on_chain(kKitChain, ChainOwner::KIT), fixture.facts).has_value())
		    .to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(bind_effect)
