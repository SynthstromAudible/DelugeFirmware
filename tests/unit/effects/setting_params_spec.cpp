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
	it("toggles the delay's ping-pong both ways", _{
		EffectStates fx{};
		fx.delay.ping_pong = false;
		const auto on = step_setting(setting::kDelayPingPong, fx, {});
		expect(on->after.delay.ping_pong).to_be_true();
		expect(on->value.str()).to_equal(std::string{"1"});
		const auto off = step_setting(setting::kDelayPingPong, on->after, {});
		expect(off->after.delay.ping_pong).to_be_false();
		expect(off->value.str()).to_equal(std::string{"0"});
	});
	it("cycles the delay's sync type from even through triplet and dotted, by its stored number", _{
		EffectStates fx{};
		fx.delay.sync_type = plugin::SyncType::EVEN;
		for (const plugin::SyncType next : {plugin::SyncType::TRIPLET, plugin::SyncType::DOTTED, plugin::SyncType::EVEN}) {
			const auto s = step_setting(setting::kDelaySyncType, fx, {});
			expect(std::to_underlying(s->after.delay.sync_type)).to_equal(std::to_underlying(next));
			expect(s->value.str()).to_equal(std::to_string(std::to_underlying(next)));
			fx = s->after;
		}
	});
	it("cycles the delay's sync level from 1 to 9 and round, skipping none", _{
		EffectStates fx{};
		fx.delay.sync_level = plugin::sync_level_from_index(0);
		for (const int32_t next : {1, 2, 3, 4, 5, 6, 7, 8, 9, 1}) {
			const auto s = step_setting(setting::kDelaySyncLevel, fx, {});
			expect(plugin::index_of(s->after.delay.sync_level)).to_equal(next);
			expect(s->value.str()).to_equal(std::to_string(next));
			fx = s->after;
		}
	});
	it("cycles the delay's type from digital through analog and off, by its word", _{
		EffectStates fx{};
		fx.delay.type = DelayType::DIGITAL;
		for (const char* next : {"analog", "off", "digital"}) {
			const auto s = step_setting(setting::kDelayType, fx, {});
			expect(s->value.str()).to_equal(std::string{next});
			fx = s->after;
		}
		expect(fx.delay.type).to_equal(DelayType::DIGITAL);
	});
	it("cycles the low-pass mode through the low-pass modes, by their words", _{
		EffectStates fx{};
		fx.filter.lpf_mode = FilterMode::TRANSISTOR_12DB;
		for (const char* next : {"24dB", "24dBDrive", "SVF_Band", "SVF_Notch", "12dB"}) {
			const auto s = step_setting(setting::kLpfMode, fx, {});
			expect(s->value.str()).to_equal(std::string{next});
			fx = s->after;
		}
		expect(fx.filter.lpf_mode).to_equal(FilterMode::TRANSISTOR_12DB);
	});
	it("cycles the high-pass mode through the high-pass modes, by their words", _{
		EffectStates fx{};
		fx.filter.hpf_mode = FilterMode::SVF_BAND;
		for (const char* next : {"SVF_Notch", "HPLadder", "SVF_Band"}) {
			const auto s = step_setting(setting::kHpfMode, fx, {});
			expect(s->value.str()).to_equal(std::string{next});
			fx = s->after;
		}
		expect(fx.filter.hpf_mode).to_equal(FilterMode::SVF_BAND);
	});
	it("toggles the sidechain between the song's slow and fast levels, both ways, at each tick magnitude", _{
		for (const int32_t magnitude : {0, 1, 2}) {
			const int32_t slow = 7 - magnitude;
			const int32_t fast = 9 - magnitude;
			EffectStates fx{};
			fx.sidechain.sync_level = plugin::sync_level_from_index(slow);
			const auto to_fast = step_setting(setting::kSidechainSyncLevel, fx, {.tick_magnitude = magnitude});
			expect(to_fast->value.str()).to_equal(std::to_string(fast));
			const auto to_slow =
			    step_setting(setting::kSidechainSyncLevel, to_fast->after, {.tick_magnitude = magnitude});
			expect(to_slow->value.str()).to_equal(std::to_string(slow));
			// A level that is neither goes to the slow one.
			fx.sidechain.sync_level = plugin::sync_level_from_index(3);
			expect(step_setting(setting::kSidechainSyncLevel, fx, {.tick_magnitude = magnitude})->value.str())
			    .to_equal(std::to_string(slow));
		}
	});
	it("cycles the mod FX type through every type and round, by its word", _{
		EffectStates fx{};
		fx.mod_fx.type = ModFXType::NONE;
		for (const char* next : {"flanger", "chorus", "phaser", "StereoChorus", "TapeWarble", "dimension", "grainFX",
		                         "none"}) {
			const auto s = step_setting(setting::kModFXType, fx, {});
			expect(s->value.str()).to_equal(std::string{next});
			fx = s->after;
		}
		expect(fx.mod_fx.type).to_equal(ModFXType::NONE);
	});
	it("steps nothing for a continuous setting", _{
		expect(step_setting(setting::kCompThreshold, EffectStates{}, {}).has_value()).to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(setting_params)
