// clang-format off
#include "effects/setting_params.h"
#include <cppspec.hpp>
#include <string>
#include <utility>

using namespace deluge;
using namespace deluge::effects;

describe setting_params("effect setting params", ${
	it("names each setting by the path SetEffectSetting takes", _{
		expect(find_setting(setting::kDelayPingPong)->file_name).to_equal(std::string_view{"delay/pingPong"});
		expect(find_setting(setting::kLpfMode)->file_name).to_equal(std::string_view{"lpfMode"});
		expect(find_setting(setting::kCompThreshold)->file_name).to_equal(std::string_view{"audioCompressor/thresh"});
		expect(find_setting(setting::kDelayType)->flags.has(plugin::ParamFlag::SETTING)).to_be_true();
	});
	it("toggles the delay's ping-pong", _{
		EffectStates fx{};
		fx.delay.ping_pong = false;
		const auto s = step_setting(setting::kDelayPingPong, fx, {});
		expect(s->after.delay.ping_pong).to_be_true();
		expect(s->value.str()).to_equal(std::string{"1"});
	});
	it("cycles the delay's sync type through triplet, dotted and even", _{
		EffectStates fx{};
		fx.delay.sync_type = plugin::SyncType::DOTTED;
		expect(std::to_underlying(step_setting(setting::kDelaySyncType, fx, {})->after.delay.sync_type))
		    .to_equal(std::to_underlying(plugin::SyncType::EVEN));
	});
	it("cycles the delay's sync level from 1 to 9, skipping none", _{
		EffectStates fx{};
		fx.delay.sync_level = plugin::sync_level_from_index(9);
		expect(step_setting(setting::kDelaySyncLevel, fx, {})->value.str()).to_equal(std::string{"1"});
	});
	it("cycles the delay's type", _{
		EffectStates fx{};
		const auto s = step_setting(setting::kDelayType, fx, {});
		expect(s->after.delay.type == next_delay_type(fx.delay.type)).to_be_true();
		expect(s->value.str()).to_equal(std::string{to_string(s->after.delay.type)});
	});
	it("cycles the low-pass mode within the low-pass modes", _{
		EffectStates fx{};
		fx.filter.lpf_mode = static_cast<FilterMode>(kNumLPFModes - 1);
		expect(std::to_underlying(step_setting(setting::kLpfMode, fx, {})->after.filter.lpf_mode)).to_equal(0);
	});
	it("cycles the high-pass mode within the high-pass modes", _{
		EffectStates fx{};
		fx.filter.hpf_mode = static_cast<FilterMode>(kFirstHPFMode + kNumHPFModes - 1);
		expect(std::to_underlying(step_setting(setting::kHpfMode, fx, {})->after.filter.hpf_mode))
		    .to_equal(int{kFirstHPFMode});
	});
	it("toggles the sidechain between the song's slow and fast levels", _{
		EffectStates fx{};
		fx.sidechain.sync_level = plugin::sync_level_from_index(7 - 1);
		expect(step_setting(setting::kSidechainSyncLevel, fx, StepContext{.tick_magnitude = 1})->value.str())
		    .to_equal(std::to_string(9 - 1));
	});
	it("cycles the mod FX type", _{
		EffectStates fx{};
		fx.mod_fx.type = ModFXType::GRAIN;
		expect(step_setting(setting::kModFXType, fx, {})->after.mod_fx.type == ModFXType::NONE).to_be_true();
	});
	it("steps nothing for a continuous setting", _{
		expect(step_setting(setting::kCompThreshold, EffectStates{}, {}).has_value()).to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(setting_params)
