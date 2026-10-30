#include "engines/effects/settings.h"

#include <cppspec.hpp>
#include <string>

using namespace deluge::effects;
using deluge::serialization::Value;

namespace {
EffectStates busy() {
	EffectStates s;
	s.filter = {.lpf_mode = FilterMode::SVF_NOTCH, .hpf_mode = FilterMode::SVF_BAND, .route = FilterRoute::PARALLEL};
	s.saturation.clipping_amount = 3;
	s.mod_fx.type = ModFXType::GRAIN;
	s.delay = {.ping_pong = false,
	           .type = DelayType::ANALOG,
	           .sync_type = deluge::plugin::SyncType::TRIPLET,
	           .sync_level = deluge::plugin::SyncLevel::EIGHTH};
	s.compressor.threshold = 3 << 24;
	return s;
}

/// The first row whose answer for @p states is one the schema refuses, or empty.
std::string firstUncheckedAnswer(const EffectStates& states) {
	for (const deluge::plugin::SettingInfo& row : kEffectSettings) {
		const std::optional<Value> value = effect_setting_value(states, row.path);
		if (!value || !check_effect_setting(row.path, *value)) {
			return std::string(row.path);
		}
	}
	return {};
}
} // namespace

// clang-format off
describe effect_settings("the effects' settings in an Edit's form", ${
	it("answers every row with a value the schema allows", _ {
		expect(firstUncheckedAnswer(EffectStates{})).to_equal(std::string{});
		expect(firstUncheckedAnswer(busy())).to_equal(std::string{});
	});
	it("gives a sync level as the level itself", _ {
		expect(effect_setting_value(busy(), "delay/syncLevel")->str()).to_equal(std::string("4"));
	});
	it("gives a delay's type as its word, and no longer an analog flag", _ {
		expect(effect_setting_value(busy(), "delay/type")->str()).to_equal(std::string("analog"));
		expect(effect_setting_value(EffectStates{}, "delay/type")->str()).to_equal(std::string("off"));
		expect(effect_setting_value(busy(), "delay/analog")).not_().to_have_value();
	});
	it("gives the compressor's threshold as its state holds it", _ {
		expect(effect_setting_value(busy(), "audioCompressor/thresh")->str()).to_equal(std::to_string(3 << 24));
	});
	it("answers nothing for a path it does not hold", _ {
		expect(effect_setting_value(busy(), "osc1/type")).not_().to_have_value();
	});
});

CPPSPEC_SPEC(effect_settings)
