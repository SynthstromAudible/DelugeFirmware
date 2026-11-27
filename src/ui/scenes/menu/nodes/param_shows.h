#pragma once

/// @file
/// @brief Where the scenes' param nodes show: conditions over the owner's setting rows, read from the rows' own words
///        and the engine's enums, as each node's legacy `isRelevant` states them.

#include "effects/settings.h"
#include "instruments/classic/words.h"
#include "plugin/types/arp.h"
#include "plugin/types/setting.h"
#include "synth/modulation/envelope_state.h"
#include "ui/scenes/bind/target.h"
#include "ui/scenes/menu/node.h"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>
#include <utility>

namespace deluge::ui::scenes::menu::nodes::param_shows {

using plugin::SettingCondition;
using Alternatives = std::span<const SettingCondition>;

/// The stored value of @p word in a host chain's or a sound's effect row @p path (a CHOICE row's index).
consteval int32_t effect_choice(std::string_view path, std::string_view word) {
	const plugin::SettingInfo* row = plugin::find_setting(effects::kEffectSettings, path);
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
inline constexpr std::array kLpfOn{plugin::setting_is_not("lpfMode", kLpfOff)};
inline constexpr std::array kHpfOn{plugin::setting_is_not("hpfMode", kHpfOff)};

// The synth mode.
inline constexpr std::array<int32_t, 1> kFm{value_of(instruments::classic::SynthMode::FM)};
inline constexpr std::array<int32_t, 1> kRingMod{value_of(instruments::classic::SynthMode::RINGMOD)};
inline constexpr std::array<int32_t, 1> kDx7Mode{value_of(instruments::classic::SynthMode::DX7)};
inline constexpr std::array kIsFm{plugin::setting_is("mode", kFm)};
inline constexpr std::array kNotFm{plugin::setting_is_not("mode", kFm)};
inline constexpr std::array kIsDx7{plugin::setting_is("mode", kDx7Mode)};
/// FM's carriers are sines and the DX7 plays no oscillator: an oscillator's wave shows outside both.
inline constexpr std::array kNotFmNorDx7{plugin::setting_is_not("mode", kFm), plugin::setting_is_not("mode", kDx7Mode)};
inline constexpr std::array kNotRingMod{plugin::setting_is_not("mode", kRingMod)};

// The oscillators' types.
inline constexpr std::array<std::string_view, 2> kOscType{"osc1/type", "osc2/type"};
inline constexpr std::array<int32_t, 1> kWavetable{value_of(instruments::classic::OscType::WAVETABLE)};
inline constexpr std::array<int32_t, 1> kSupersaw{value_of(instruments::classic::OscType::SUPERSAW)};
inline constexpr std::array<int32_t, 4> kSampleOrInput{
    value_of(instruments::classic::OscType::SAMPLE), value_of(instruments::classic::OscType::INPUT_L),
    value_of(instruments::classic::OscType::INPUT_R), value_of(instruments::classic::OscType::INPUT_STEREO)};

/// A source's wave index (`origin/main:src/deluge/gui/menu_item/osc/source/wave_index.h`): not FM, and a supersaw (its
/// spread) or a loaded wavetable.
inline constexpr std::array<std::array<SettingCondition, 1>, 2> kSupersawOsc{
    {{plugin::setting_is(kOscType[0], kSupersaw)}, {plugin::setting_is(kOscType[1], kSupersaw)}}};
inline constexpr std::array<std::array<SettingCondition, 2>, 2> kLoadedWavetable{{
    {plugin::setting_is(kOscType[0], kWavetable), plugin::source_loaded(0)},
    {plugin::setting_is(kOscType[1], kWavetable), plugin::source_loaded(1)},
}};
inline constexpr std::array<std::array<Alternatives, 2>, 2> kWaveIndexAny{
    {{kSupersawOsc[0], kLoadedWavetable[0]}, {kSupersawOsc[1], kLoadedWavetable[1]}}};
/// An oscillator's pulse width (`origin/main:src/deluge/gui/menu_item/osc/pulse_width.h`): not FM, not a sample or an
/// input, and a wavetable only once loaded; in DX7 mode the DX7's column shows it.
inline constexpr std::array<std::array<SettingCondition, 3>, 2> kPulseWidth{{
    {plugin::setting_is_not("mode", kFm), plugin::setting_is_not("mode", kDx7Mode),
     plugin::setting_is_not(kOscType[0], kSampleOrInput)},
    {plugin::setting_is_not("mode", kFm), plugin::setting_is_not("mode", kDx7Mode),
     plugin::setting_is_not(kOscType[1], kSampleOrInput)},
}};
inline constexpr std::array<std::array<SettingCondition, 1>, 2> kNotWavetable{
    {{plugin::setting_is_not(kOscType[0], kWavetable)}, {plugin::setting_is_not(kOscType[1], kWavetable)}}};
inline constexpr std::array<std::array<SettingCondition, 1>, 2> kLoaded{
    {{plugin::source_loaded(0)}, {plugin::source_loaded(1)}}};
inline constexpr std::array<std::array<Alternatives, 2>, 2> kPulseWidthAny{
    {{kNotWavetable[0], kLoaded[0]}, {kNotWavetable[1], kLoaded[1]}}};
/// A source's carrier feedback (`origin/main:src/deluge/gui/menu_item/osc/source/feedback.h`): FM (`main`'s DX7
/// oscillator showed it too, which the DX7 engine never reads).

// The mod FX type, as the `isRelevant` overrides of main's mod FX items gate on it
// (`origin/main:src/deluge/gui/menu_item/mod_fx/depth_patched.h`, `origin/main:src/deluge/gui/menu_item/mod_fx/rate.h`,
// `origin/main:src/deluge/gui/menu_item/mod_fx/feedback.h`, `origin/main:src/deluge/gui/menu_item/mod_fx/offset.h`).
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
inline constexpr std::array kModFxOn{plugin::setting_is_not("modFXType", kModFxNone)};
inline constexpr std::array kModFxHasDepth{plugin::setting_is("modFXType", kModFxWithDepth)};
inline constexpr std::array kModFxHasFeedback{plugin::setting_is("modFXType", kModFxWithFeedback)};
inline constexpr std::array kModFxHasOffset{plugin::setting_is("modFXType", kModFxWithOffset)};

// The arp's mode, on the arp record the menu's root names (a clip's, or a kit lane's): the randomizer's params show
// only while it is on (`origin/main:src/deluge/gui/menu_item/randomizer/randomizer_unpatched_param.h`).
inline constexpr std::array<int32_t, 1> kArpOff{static_cast<int32_t>(
    std::ranges::find(plugin::kArpModeWords, std::string_view{"off"}) - plugin::kArpModeWords.begin())};
inline constexpr std::array kArpOnCondition{plugin::setting_is_not("arpeggiator/arpMode", kArpOff)};
inline constexpr bind::Target kArpOn{.kind = bind::Target::Kind::MODEL,
                                     .path = "arpeggiator/arpMode",
                                     .group = project::SettingGroupId::CLIP_ARP,
                                     .applies_when = kArpOnCondition};
/// A sound's own: a synth's or a sound drum's, not a kit's affect-entire chain.
inline constexpr std::array<int32_t, 2> kSoundInstruments{
    static_cast<int32_t>(plugin::SettingContext::Instrument::SYNTH),
    static_cast<int32_t>(plugin::SettingContext::Instrument::SOUND_DRUM)};
inline constexpr std::array<int32_t, 1> kSynthInstrument{
    static_cast<int32_t>(plugin::SettingContext::Instrument::SYNTH)};
inline constexpr std::array<int32_t, 1> kKitOwner{static_cast<int32_t>(plugin::SettingContext::Owner::KIT)};
inline constexpr std::array kOnKitChain{plugin::owner_is(kKitOwner)};
inline constexpr std::array kOnSound{plugin::instrument_is(kSoundInstruments)};
inline constexpr std::array kOnSynth{plugin::instrument_is(kSynthInstrument)};

// An envelope's mode (`envelope::Segment::isRelevant`): AR and MPE have no decay, AHD no sustain.
inline constexpr std::array<int32_t, 2> kArOrMpe{value_of(synth::EnvelopeMode::AR), value_of(synth::EnvelopeMode::MPE)};
inline constexpr std::array<int32_t, 1> kAhd{value_of(synth::EnvelopeMode::AHD)};
inline constexpr std::array<std::string_view, 4> kEnvelopeModePath{"envelope1Settings/mode", "envelope2Settings/mode",
                                                                   "envelope3Settings/mode", "envelope4Settings/mode"};
inline constexpr std::array<std::array<SettingCondition, 1>, 4> kHasDecay{
    {{plugin::setting_is_not(kEnvelopeModePath[0], kArOrMpe)},
     {plugin::setting_is_not(kEnvelopeModePath[1], kArOrMpe)},
     {plugin::setting_is_not(kEnvelopeModePath[2], kArOrMpe)},
     {plugin::setting_is_not(kEnvelopeModePath[3], kArOrMpe)}}};
inline constexpr std::array<std::array<SettingCondition, 1>, 4> kHasSustain{
    {{plugin::setting_is_not(kEnvelopeModePath[0], kAhd)},
     {plugin::setting_is_not(kEnvelopeModePath[1], kAhd)},
     {plugin::setting_is_not(kEnvelopeModePath[2], kAhd)},
     {plugin::setting_is_not(kEnvelopeModePath[3], kAhd)}}};

// Where each family shows.
inline constexpr std::array<ParamShows, 4> kEnvelopeDecay{
    {{.when = kHasDecay[0]}, {.when = kHasDecay[1]}, {.when = kHasDecay[2]}, {.when = kHasDecay[3]}}};
inline constexpr std::array<ParamShows, 4> kEnvelopeSustain{
    {{.when = kHasSustain[0]}, {.when = kHasSustain[1]}, {.when = kHasSustain[2]}, {.when = kHasSustain[3]}}};
inline constexpr ParamShows kHasLpf{.when = kLpfOn};
inline constexpr ParamShows kHasHpf{.when = kHpfOn};
inline constexpr ParamShows kOnFm{.when = kIsFm};
inline constexpr ParamShows kOffFm{.when = kNotFm};
inline constexpr ParamShows kOffRingMod{.when = kNotRingMod};
inline constexpr std::array<ParamShows, 2> kOscWaveIndex{
    {{.when = kNotFmNorDx7, .any = kWaveIndexAny[0]}, {.when = kNotFmNorDx7, .any = kWaveIndexAny[1]}}};
/// The DX7's own columns, in DX7 mode.
inline constexpr ParamShows kOnDx7{.when = kIsDx7};
inline constexpr std::array<ParamShows, 2> kOscPulseWidth{
    {{.when = kPulseWidth[0], .any = kPulseWidthAny[0]}, {.when = kPulseWidth[1], .any = kPulseWidthAny[1]}}};
inline constexpr std::array<ParamShows, 2> kOscCarrierFeedback{{{.when = kIsFm}, {.when = kIsFm}}};
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

} // namespace deluge::ui::scenes::menu::nodes::param_shows
