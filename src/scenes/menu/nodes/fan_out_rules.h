#pragma once

/// @file
/// @brief The drums an affect-entire write reaches, per setting, as legacy's loops pick them
/// (`gui/menu_item/**`'s `writeCurrentValue` over `kit->firstDrum`): a Target's fan_out_when.

#include "effects/settings.h"
#include "instruments/deluge/words.h"
#include "plugin-api/types/setting.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

namespace deluge::scenes::menu::fan_out {

namespace detail {
inline constexpr std::array<int32_t, 1> kPoly{std::to_underlying(instrument::PolyphonyMode::POLY)};
inline constexpr std::array<int32_t, 1> kFm{std::to_underlying(instrument::SynthMode::FM)};
inline constexpr std::array<int32_t, 1> kSample{std::to_underlying(instrument::OscType::SAMPLE)};
inline constexpr auto kRingable = [] {
	std::array<int32_t, std::to_underlying(instrument::OscType::WAVETABLE) + 1> types{};
	for (size_t i = 0; i < types.size(); ++i) {
		types[i] = static_cast<int32_t>(i);
	}
	return types;
}();
/// The stored index of choice @p word of effect row @p path (a filter's Off is not its FilterMode's number).
constexpr int32_t effect_choice(std::string_view path, std::string_view word) {
	const plugin::SettingInfo& row = *plugin::find_setting(effects::kEffectSettings, path);
	for (size_t i = 0; i < row.choices.size(); ++i) {
		if (row.choices[i] == word) {
			return static_cast<int32_t>(i);
		}
	}
	return -1;
}
inline constexpr std::array<int32_t, 1> kLpfOff{effect_choice("lpfMode", "Off")};
inline constexpr std::array<int32_t, 1> kHpfOff{effect_choice("hpfMode", "Off")};
static_assert(kLpfOff[0] >= 0 && kHpfOff[0] >= 0);
} // namespace detail

/// `voice/polyphony.h:65`: a polyphonic drum's voice count.
inline constexpr std::array kPolyphonic{plugin::setting_is("polyphonic", detail::kPoly)};
/// `synth_mode.h:55-56`: a drum whose two oscillators could both be ring-modulated, whatever the mode written.
inline constexpr std::array kRingable{plugin::setting_is("osc1/type", detail::kRingable),
                                      plugin::setting_is("osc2/type", detail::kRingable)};
/// `osc/sync.h:45-46`: not FM, and neither oscillator a sample.
inline constexpr std::array kOscSync{plugin::setting_is_not("mode", detail::kFm),
                                     plugin::setting_is_not("osc1/type", detail::kSample),
                                     plugin::setting_is_not("osc2/type", detail::kSample)};
/// `modulator/transpose.h:57`, `modulator/destination.h:47`: an FM drum.
inline constexpr std::array kFm{plugin::setting_is("mode", detail::kFm)};
/// `filter_route.h:49-50`: both filters on.
inline constexpr std::array kBothFilters{plugin::setting_is_not("lpfMode", detail::kLpfOff),
                                         plugin::setting_is_not("hpfMode", detail::kHpfOff)};

} // namespace deluge::scenes::menu::fan_out
