#pragma once

/// @file
/// @brief Where the scenes' param nodes show: conditions over the owner's setting rows, read from the rows' own words
///        and the engine's enums, as each node's legacy `isRelevant` states them.

#include "engine-api/types/arp.h"
#include "engine-api/types/setting.h"
#include "engines/deluge/words.h"
#include "engines/effects/settings.h"
#include "scenes/bind/target.h"
#include "scenes/menu/node.h"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>
#include <utility>

namespace deluge::scenes::menu::nodes::param_shows {

using engine::SettingCondition;
using Alternatives = std::span<const SettingCondition>;

/// The stored value of @p word in a host chain's or a sound's effect row @p path (a CHOICE row's index).
consteval int32_t effect_choice(std::string_view path, std::string_view word) {
	const engine::SettingInfo* row = engine::find_setting(effects::kEffectSettings, path);
	const auto found = std::ranges::find(row->choices, word);
	return static_cast<int32_t>(found - row->choices.begin());
}

template <typename E>
consteval int32_t value_of(E e) {
	return static_cast<int32_t>(std::to_underlying(e));
}

// Filters: on unless their mode is off (`FilterInfo::isOn`).
inline constexpr std::array<int32_t, 1> kLpfOff{effect_choice("lpfMode", "Off")};
inline constexpr std::array<int32_t, 1> kHpfOff{effect_choice("hpfMode", "Off")};
inline constexpr std::array kLpfOn{engine::setting_is_not("lpfMode", kLpfOff)};
inline constexpr std::array kHpfOn{engine::setting_is_not("hpfMode", kHpfOff)};

// The synth mode.
inline constexpr std::array<int32_t, 1> kFm{value_of(instrument::SynthMode::FM)};
inline constexpr std::array<int32_t, 1> kRingMod{value_of(instrument::SynthMode::RINGMOD)};
inline constexpr std::array kIsFm{engine::setting_is("mode", kFm)};
inline constexpr std::array kNotFm{engine::setting_is_not("mode", kFm)};
inline constexpr std::array kNotRingMod{engine::setting_is_not("mode", kRingMod)};

// The oscillators' types.
inline constexpr std::array<std::string_view, 2> kOscType{"osc1/type", "osc2/type"};
inline constexpr std::array<int32_t, 1> kWavetable{value_of(instrument::OscType::WAVETABLE)};
inline constexpr std::array<int32_t, 1> kSupersaw{value_of(instrument::OscType::SUPERSAW)};
inline constexpr std::array<int32_t, 1> kDx7{value_of(instrument::OscType::DX7)};
inline constexpr std::array<int32_t, 4> kSampleOrInput{
    value_of(instrument::OscType::SAMPLE), value_of(instrument::OscType::INPUT_L),
    value_of(instrument::OscType::INPUT_R), value_of(instrument::OscType::INPUT_STEREO)};

/// A source's wave index (`osc/source/wave_index.h`): not FM, and a supersaw (its spread) or a loaded wavetable.
inline constexpr std::array<std::array<SettingCondition, 1>, 2> kSupersawOsc{
    {{engine::setting_is(kOscType[0], kSupersaw)}, {engine::setting_is(kOscType[1], kSupersaw)}}};
inline constexpr std::array<std::array<SettingCondition, 2>, 2> kLoadedWavetable{{
    {engine::setting_is(kOscType[0], kWavetable), engine::source_loaded(0)},
    {engine::setting_is(kOscType[1], kWavetable), engine::source_loaded(1)},
}};
inline constexpr std::array<std::array<Alternatives, 2>, 2> kWaveIndexAny{
    {{kSupersawOsc[0], kLoadedWavetable[0]}, {kSupersawOsc[1], kLoadedWavetable[1]}}};
/// An oscillator's pulse width (`osc/pulse_width.h`): not FM, not a sample or an input, and a wavetable only once
/// loaded.
inline constexpr std::array<std::array<SettingCondition, 2>, 2> kPulseWidth{{
    {engine::setting_is_not("mode", kFm), engine::setting_is_not(kOscType[0], kSampleOrInput)},
    {engine::setting_is_not("mode", kFm), engine::setting_is_not(kOscType[1], kSampleOrInput)},
}};
inline constexpr std::array<std::array<SettingCondition, 1>, 2> kNotWavetable{
    {{engine::setting_is_not(kOscType[0], kWavetable)}, {engine::setting_is_not(kOscType[1], kWavetable)}}};
inline constexpr std::array<std::array<SettingCondition, 1>, 2> kLoaded{
    {{engine::source_loaded(0)}, {engine::source_loaded(1)}}};
inline constexpr std::array<std::array<Alternatives, 2>, 2> kPulseWidthAny{
    {{kNotWavetable[0], kLoaded[0]}, {kNotWavetable[1], kLoaded[1]}}};
/// A source's carrier feedback (`osc/source/feedback.h`): FM, or a DX7 oscillator.
inline constexpr std::array<std::array<SettingCondition, 1>, 2> kDx7Osc{
    {{engine::setting_is(kOscType[0], kDx7)}, {engine::setting_is(kOscType[1], kDx7)}}};
inline constexpr std::array<std::array<Alternatives, 2>, 2> kCarrierFeedbackAny{
    {{kIsFm, kDx7Osc[0]}, {kIsFm, kDx7Osc[1]}}};

// The mod FX type (`mod_fx/{depth,rate,feedback,offset}.h`).
inline constexpr std::array<int32_t, 1> kModFxNone{effect_choice("modFXType", "none")};
inline constexpr std::array<int32_t, 6> kModFxWithDepth{
    effect_choice("modFXType", "chorus"),     effect_choice("modFXType", "StereoChorus"),
    effect_choice("modFXType", "grainFX"),    effect_choice("modFXType", "phaser"),
    effect_choice("modFXType", "TapeWarble"), effect_choice("modFXType", "dimension")};
inline constexpr std::array<int32_t, 4> kModFxWithFeedback{
    effect_choice("modFXType", "flanger"), effect_choice("modFXType", "phaser"), effect_choice("modFXType", "grainFX"),
    effect_choice("modFXType", "TapeWarble")};
inline constexpr std::array<int32_t, 5> kModFxWithOffset{
    effect_choice("modFXType", "chorus"), effect_choice("modFXType", "StereoChorus"),
    effect_choice("modFXType", "grainFX"), effect_choice("modFXType", "TapeWarble"),
    effect_choice("modFXType", "dimension")};
inline constexpr std::array kModFxOn{engine::setting_is_not("modFXType", kModFxNone)};
inline constexpr std::array kModFxHasDepth{engine::setting_is("modFXType", kModFxWithDepth)};
inline constexpr std::array kModFxHasFeedback{engine::setting_is("modFXType", kModFxWithFeedback)};
inline constexpr std::array kModFxHasOffset{engine::setting_is("modFXType", kModFxWithOffset)};

// The arp's mode, on the arp record the menu's root names (a clip's, or a kit lane's): the randomizer's params show
// only while it is on (`randomizer/randomizer_unpatched_param.h`).
inline constexpr std::array<int32_t, 1> kArpOff{static_cast<int32_t>(
    std::ranges::find(engine::kArpModeWords, std::string_view{"off"}) - engine::kArpModeWords.begin())};
inline constexpr std::array kArpOnCondition{engine::setting_is_not("arpeggiator/arpMode", kArpOff)};
inline constexpr bind::Target kArpOn{.kind = bind::Target::Kind::MODEL,
                                     .path = "arpeggiator/arpMode",
                                     .group = project::SettingGroupId::CLIP_ARP,
                                     .applies_when = kArpOnCondition};
/// A sound's own: a synth's or a sound drum's, not a kit's affect-entire chain.
inline constexpr std::array<int32_t, 2> kSoundInstruments{
    static_cast<int32_t>(engine::SettingContext::Instrument::SYNTH),
    static_cast<int32_t>(engine::SettingContext::Instrument::SOUND_DRUM)};
inline constexpr std::array<int32_t, 1> kSynthInstrument{
    static_cast<int32_t>(engine::SettingContext::Instrument::SYNTH)};
inline constexpr std::array<int32_t, 1> kKitOwner{static_cast<int32_t>(engine::SettingContext::Owner::KIT)};
inline constexpr std::array kOnKitChain{engine::owner_is(kKitOwner)};
inline constexpr std::array kOnSound{engine::instrument_is(kSoundInstruments)};
inline constexpr std::array kOnSynth{engine::instrument_is(kSynthInstrument)};

// Where each family shows.
inline constexpr ParamShows kHasLpf{.when = kLpfOn};
inline constexpr ParamShows kHasHpf{.when = kHpfOn};
inline constexpr ParamShows kOnFm{.when = kIsFm};
inline constexpr ParamShows kOffFm{.when = kNotFm};
inline constexpr ParamShows kOffRingMod{.when = kNotRingMod};
inline constexpr std::array<ParamShows, 2> kOscWaveIndex{
    {{.when = kNotFm, .any = kWaveIndexAny[0]}, {.when = kNotFm, .any = kWaveIndexAny[1]}}};
inline constexpr std::array<ParamShows, 2> kOscPulseWidth{
    {{.when = kPulseWidth[0], .any = kPulseWidthAny[0]}, {.when = kPulseWidth[1], .any = kPulseWidthAny[1]}}};
inline constexpr std::array<ParamShows, 2> kOscCarrierFeedback{
    {{.any = kCarrierFeedbackAny[0]}, {.any = kCarrierFeedbackAny[1]}}};
inline constexpr ParamShows kModFxIsOn{.when = kModFxOn};
inline constexpr ParamShows kModFxDepth{.when = kModFxHasDepth};
inline constexpr ParamShows kModFxFeedback{.when = kModFxHasFeedback};
inline constexpr ParamShows kModFxOffset{.when = kModFxHasOffset};
/// `is_kit_global_fx`: a kit's own effects (with affect-entire), not an audio track's or the song's.
inline constexpr ParamShows kKitEffects{.when = kOnKitChain};
/// `randomizer_param`: with the arp on.
inline constexpr ParamShows kRandomizer{.with = &kArpOn};
/// `randomizer_sound_only`: a sound's own, with the arp on.
inline constexpr ParamShows kRandomizerSoundOnly{.when = kOnSound, .with = &kArpOn};
/// `randomizer_non_kit`: a synth's, with the arp on.
inline constexpr ParamShows kRandomizerNonKit{.when = kOnSynth, .with = &kArpOn};

} // namespace deluge::scenes::menu::nodes::param_shows
