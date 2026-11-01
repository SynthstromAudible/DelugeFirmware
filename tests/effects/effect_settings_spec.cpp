#include "effects/settings.h"

#include <cppspec.hpp>
#include <string>
#include <utility>

using namespace deluge::effects;
using deluge::serialization::Value;

namespace {
/// What a test reads where from_menu refuses a menu value.
constexpr std::pair<int32_t, int32_t> kNone{-1, -1};

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
	it("shows the delay's sync as one menu value over all 28 of its level and type", _ {
		const deluge::plugin::SettingInfo& row = *deluge::plugin::find_setting(kEffectSettings, "delay/syncLevel");
		const deluge::plugin::DisplayMapping& mapping = deluge::plugin::mapping_of(row);
		expect(mapping.kind == deluge::plugin::DisplayMappingKind::COMBINED_SYNC).to_be_true();
		expect(std::string(mapping.partner)).to_equal(std::string("delay/syncType"));
		expect(row.units == deluge::plugin::SettingUnits::SYNC_LEVEL).to_be_true();
		expect(deluge::plugin::from_menu(row, 27).value_or(kNone)).to_equal(std::pair<int32_t, int32_t>{9, 2});
		expect(deluge::plugin::from_menu(row, 28)).not_().to_have_value();
	});
	it("shows the sidechain's sync as one menu value over the even levels alone, as legacy's menu offers", _ {
		const deluge::plugin::SettingInfo& row =
		    *deluge::plugin::find_setting(kEffectSettings, "sidechain/syncLevel");
		const deluge::plugin::DisplayMapping& mapping = deluge::plugin::mapping_of(row);
		expect(mapping.kind == deluge::plugin::DisplayMappingKind::COMBINED_SYNC).to_be_true();
		expect(std::string(mapping.partner)).to_equal(std::string("sidechain/syncType"));
		expect(row.units == deluge::plugin::SettingUnits::SYNC_LEVEL).to_be_true();
		expect(deluge::plugin::from_menu(row, 0).value_or(kNone)).to_equal(std::pair<int32_t, int32_t>{0, 0});
		expect(deluge::plugin::from_menu(row, 9).value_or(kNone)).to_equal(std::pair<int32_t, int32_t>{9, 0});
		expect(deluge::plugin::from_menu(row, 10)).not_().to_have_value();
		expect(deluge::plugin::from_menu(row, -1)).not_().to_have_value();
	});
	it("answers nothing for a path it does not hold", _ {
		expect(effect_setting_value(busy(), "osc1/type")).not_().to_have_value();
	});
});

CPPSPEC_SPEC(effect_settings)
