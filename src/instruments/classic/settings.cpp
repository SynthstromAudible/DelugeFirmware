/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "instruments/classic/settings.h"
#include "instruments/classic/composed_state.h"
#include "instruments/classic/record_edit.h"
#include "instruments/classic/state_access.h"

#include "plugin/settings.h"

#include "effects/settings.h"
#include "instruments/classic/codec.h"
#include "instruments/classic/words.h"
#include "plugin/types/arp.h"
#include "plugin/types/sync.h"
#include "plugin/types/voice_priority.h"
#include "serialization/tree.h"
#include "synth/modulation/envelope_state.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <string>
#include <utility>

namespace deluge::instruments::classic {

namespace {
/// The menu-time tables: read when a menu or an Edit looks a setting up, never by the audio tier or an interrupt.
/// The device image places this namespace's data in SDRAM (linker/sdram_sections.x matches its name).
inline namespace menu_tables {

using plugin::SettingInfo;
using enum plugin::SettingKind;

/// The names a file writes for each value of @p Enum, from its first to @p last.
template <class Enum, size_t N>
constexpr std::array<std::string_view, N> names_of() {
	std::array<std::string_view, N> names{};
	for (size_t i = 0; i < N; ++i) {
		names[i] = to_string(static_cast<Enum>(i));
	}
	return names;
}

constexpr auto kPolyphony = names_of<PolyphonyMode, 5>();
// The mode menu offers the first three; DX7 joins it with its pages.
constexpr auto kSynthModes = names_of<SynthMode, 3>();
// INPUT_STEREO is the last type, so the list grows with the enum.
constexpr auto kOscTypes = names_of<OscType, static_cast<size_t>(OscType::INPUT_STEREO) + 1>();
constexpr auto kLfoTypes = names_of<LFOType, 7>();
/// A synced rate's type, as its stored number (even, triplet, dotted).
constexpr std::array<std::string_view, 3> kSyncTypes{"0", "10", "19"};

/// An envelope's modes and follow inputs, as main's words.
constexpr std::array<std::string_view, 4> kEnvelopeModes{"adsr", "ahd", "ar", "mpe"};
constexpr std::array<std::string_view, 2> kEnvelopeFollows{"z", "y"};

constexpr int32_t kTransposeRange = 96; ///< semitones either way: the transpose menus' ±9600 cents
constexpr int32_t kCentsRange = 50;
/// A retrigger phase as a file writes it: the 32-bit phase (a fraction of 2^32) as a signed integer, −1
/// (kFreeRunningPhase) free-running. The menu's degrees are 11930464 apart.
constexpr int32_t kPhaseMin = std::numeric_limits<int32_t>::min();
constexpr int32_t kPhaseMax = std::numeric_limits<int32_t>::max();
/// The voice limit the max-voices menu's "off" writes: 1–16 are limits, this one is none in practice.
constexpr int32_t kMaxVoicesOff = 127;
/// The greatest limit the max-voices menu offers.
constexpr int32_t kMaxVoicesMenu = 16;
/// The max-voices menu's 0 is "off", stored as kMaxVoicesOff.
constexpr std::array<plugin::SettingSentinel, 1> kMaxVoicesSentinels{{{.user = 0, .stored = kMaxVoicesOff}}};

/// A sample's repeat mode as its stored number, in SampleRepeatMode's order (cut, once, loop, stretch).
constexpr std::array<std::string_view, 4> kLoopModes{"0", "1", "2", "3"};
static_assert(std::to_underlying(SampleRepeatMode::STRETCH) + 1 == kLoopModes.size());

/// The owners a kit lane stands for.
constexpr std::array<int32_t, 1> kKitLaneOwner{std::to_underlying(plugin::SettingContext::Owner::KIT_LANE)};
/// A kit lane: it alone offers choke (which shares a kit's voices among its lanes).
constexpr std::array kOnKitLane{plugin::owner_is(kKitLaneOwner)};
constexpr std::array kPolyphonyWhen{
    plugin::ChoiceCondition{.choice = std::to_underlying(PolyphonyMode::CHOKE), .when = kOnKitLane}};
constexpr plugin::SettingDescriptorExtras kPolyphonyExtras{.choice_when = kPolyphonyWhen};

/// The synth modes, as the "mode" row stores them.
constexpr std::array<int32_t, 1> kSubtractiveValue{std::to_underlying(SynthMode::SUBTRACTIVE)};
constexpr std::array<int32_t, 1> kFmValue{std::to_underlying(SynthMode::FM)};
constexpr std::array<int32_t, 1> kRingModeValue{std::to_underlying(SynthMode::RINGMOD)};

/// Ring mod mixes oscillators alone, so it offers no sample or input.
constexpr std::array kNotRingMode{plugin::setting_is_not("mode", kRingModeValue)};
/// DX7 in the first oscillator of any sound, a kit's included.
constexpr std::array kDx7Conditions{plugin::setting_is_not("mode", kRingModeValue)};
/// With no audio input plugged in, one input entry (the left input's) stands for the three.
constexpr std::array kInputConditions{plugin::setting_is_not("mode", kRingModeValue), plugin::inputs_are(true)};
/// The second oscillator's menu never offers DX7: only the first can hold one.
constexpr std::array kNeverOffered{plugin::never()};

/// The oscillator type menu's conditions; the choice list is shared, since stored indices are what files hold.
constexpr std::array<plugin::ChoiceCondition, 5> osc_type_when(std::span<const plugin::SettingCondition> dx7) {
	return {{{std::to_underlying(OscType::SAMPLE), kNotRingMode},
	         {std::to_underlying(OscType::DX7), dx7},
	         {std::to_underlying(OscType::INPUT_L), kNotRingMode},
	         {std::to_underlying(OscType::INPUT_R), kInputConditions},
	         {std::to_underlying(OscType::INPUT_STEREO), kInputConditions}}};
}
constexpr std::array kOsc1TypeWhen = osc_type_when(kDx7Conditions);
constexpr std::array kOsc2TypeWhen = osc_type_when(kNeverOffered);
/// FM's carriers are sines: an oscillator's type menu shows outside it.
constexpr std::array kNotFm{plugin::setting_is_not("mode", kFmValue)};
constexpr std::array kOscTypeExtras{
    plugin::SettingDescriptorExtras{.applies_when = kNotFm, .choice_when = kOsc1TypeWhen},
    plugin::SettingDescriptorExtras{.applies_when = kNotFm, .choice_when = kOsc2TypeWhen}};

/// The oscillator types ring mod can ring, as allows_setting has them: every type up to the wavetable.
constexpr auto kRingableTypes = [] {
	std::array<int32_t, std::to_underlying(OscType::WAVETABLE) + 1> types{};
	for (std::size_t i = 0; i < types.size(); ++i) {
		types[i] = static_cast<int32_t>(i);
	}
	return types;
}();
/// The synth mode menu shows only while ring mod could be chosen: neither oscillator past a wavetable.
constexpr std::array kModeConditions{plugin::setting_is("osc1/type", kRingableTypes),
                                     plugin::setting_is("osc2/type", kRingableTypes)};
constexpr plugin::SettingDescriptorExtras kModeExtras{.applies_when = kModeConditions};

/// The polyphony that has a voice count to limit.
constexpr std::array<int32_t, 1> kPolyValue{std::to_underlying(PolyphonyMode::POLY)};
constexpr std::array kPolyConditions{plugin::setting_is("polyphonic", kPolyValue)};

/// A stored limit past the menu's (none a menu writes) shows as off; the limit shows for a polyphonic sound alone.
constexpr plugin::SettingDescriptorExtras kMaxVoicesExtras{
    .mapping = {.kind = plugin::DisplayMappingKind::CLAMP_HIGH, .clamp_to = 0}, .applies_when = kPolyConditions};

/// An oscillator's sample playback rows: a subtractive sound's sample source with a file loaded.
constexpr std::array<int32_t, 1> kSampleValue{std::to_underlying(OscType::SAMPLE)};
constexpr std::array<std::array<plugin::SettingCondition, 3>, 2> kSamplePlaybackConditions{{
    {plugin::setting_is("mode", kSubtractiveValue), plugin::setting_is("osc1/type", kSampleValue),
     plugin::source_loaded(0)},
    {plugin::setting_is("mode", kSubtractiveValue), plugin::setting_is("osc2/type", kSampleValue),
     plugin::source_loaded(1)},
}};
constexpr std::array kSamplePlaybackExtras{
    plugin::SettingDescriptorExtras{.applies_when = kSamplePlaybackConditions[0]},
    plugin::SettingDescriptorExtras{.applies_when = kSamplePlaybackConditions[1]}};

/// Oscillator sync resets the second oscillator from the first's cycle: neither may be a sample, nor the sound FM.
constexpr std::array kOscSyncConditions{plugin::setting_is_not("mode", kFmValue),
                                        plugin::setting_is_not("osc1/type", kSampleValue),
                                        plugin::setting_is_not("osc2/type", kSampleValue)};
constexpr plugin::SettingDescriptorExtras kOscSyncExtras{.applies_when = kOscSyncConditions};

/// The modulators play in FM alone.
constexpr std::array kFmConditions{plugin::setting_is("mode", kFmValue)};
constexpr plugin::SettingDescriptorExtras kFmExtras{.applies_when = kFmConditions};

/// Each oscillator's type row, and the conditions on it and on its file that its tuning, phase and interpolation menus
/// show under: an oscillator playing a file shows them only once the file is loaded.
constexpr std::array<std::string_view, 2> kOscTypePaths{"osc1/type", "osc2/type"};
constexpr std::array<int32_t, 2> kFileTypes{std::to_underlying(OscType::SAMPLE),
                                            std::to_underlying(OscType::WAVETABLE)};
constexpr std::array<int32_t, 1> kWavetableValue{std::to_underlying(OscType::WAVETABLE)};
constexpr std::array<int32_t, 3> kInputTypes{std::to_underlying(OscType::INPUT_L), std::to_underlying(OscType::INPUT_R),
                                             std::to_underlying(OscType::INPUT_STEREO)};
constexpr std::array kSubtractiveConditions{plugin::setting_is("mode", kSubtractiveValue)};

/// Per oscillator (index 0 the first): its type plays no file; its file is loaded; it plays a loaded wavetable; it
/// holds a sample in FM; it plays a loaded sample; it plays an input; it is not a wavetable.
template <std::size_t N>
using Conditions = std::array<plugin::SettingCondition, N>;
constexpr std::array<Conditions<1>, 2> kPlaysNoFile{
    {{plugin::setting_is_not(kOscTypePaths[0], kFileTypes)}, {plugin::setting_is_not(kOscTypePaths[1], kFileTypes)}}};
constexpr std::array<Conditions<1>, 2> kFileLoaded{{{plugin::source_loaded(0)}, {plugin::source_loaded(1)}}};
constexpr std::array<Conditions<2>, 2> kLoadedWavetable{
    {{plugin::setting_is(kOscTypePaths[0], kWavetableValue), plugin::source_loaded(0)},
     {plugin::setting_is(kOscTypePaths[1], kWavetableValue), plugin::source_loaded(1)}}};
constexpr std::array<Conditions<2>, 2> kSampleInFm{
    {{plugin::setting_is(kOscTypePaths[0], kSampleValue), plugin::setting_is("mode", kFmValue)},
     {plugin::setting_is(kOscTypePaths[1], kSampleValue), plugin::setting_is("mode", kFmValue)}}};
constexpr std::array<Conditions<2>, 2> kLoadedSample{
    {{plugin::setting_is(kOscTypePaths[0], kSampleValue), plugin::source_loaded(0)},
     {plugin::setting_is(kOscTypePaths[1], kSampleValue), plugin::source_loaded(1)}}};
constexpr std::array<Conditions<1>, 2> kPlaysInput{
    {{plugin::setting_is(kOscTypePaths[0], kInputTypes)}, {plugin::setting_is(kOscTypePaths[1], kInputTypes)}}};
constexpr std::array<Conditions<1>, 2> kNotWavetable{{{plugin::setting_is_not(kOscTypePaths[0], kWavetableValue)},
                                                      {plugin::setting_is_not(kOscTypePaths[1], kWavetableValue)}}};

using Alternatives = std::span<const plugin::SettingCondition>;
/// An oscillator's transpose (and cents): any type that plays no file, or a loaded file.
constexpr std::array<std::array<Alternatives, 2>, 2> kOscTuningAny{
    {{kPlaysNoFile[0], kFileLoaded[0]}, {kPlaysNoFile[1], kFileLoaded[1]}}};
/// An oscillator's retrigger phase: a loaded wavetable, a type that plays no file, or a sample in FM.
constexpr std::array<std::array<Alternatives, 3>, 2> kOscPhaseAny{
    {{kLoadedWavetable[0], kPlaysNoFile[0], kSampleInFm[0]}, {kLoadedWavetable[1], kPlaysNoFile[1], kSampleInFm[1]}}};
/// An oscillator's interpolation, in subtractive: a loaded sample or an input.
constexpr std::array<std::array<Alternatives, 2>, 2> kInterpolationAny{
    {{kLoadedSample[0], kPlaysInput[0]}, {kLoadedSample[1], kPlaysInput[1]}}};
/// A modulator's retrigger phase, in FM: its oscillator not a wavetable, or its file loaded.
constexpr std::array<std::array<Alternatives, 2>, 2> kModulatorPhaseAny{
    {{kNotWavetable[0], kFileLoaded[0]}, {kNotWavetable[1], kFileLoaded[1]}}};

/// A retrigger phase menu: degrees, or −1 for free-running.
constexpr plugin::DisplayMapping kPhaseMapping{.kind = plugin::DisplayMappingKind::PHASE_DEGREES};
constexpr std::array kOscPhaseExtras{
    plugin::SettingDescriptorExtras{.mapping = kPhaseMapping, .applies_any = kOscPhaseAny[0]},
    plugin::SettingDescriptorExtras{.mapping = kPhaseMapping, .applies_any = kOscPhaseAny[1]}};
constexpr std::array kModulatorPhaseExtras{
    plugin::SettingDescriptorExtras{
        .mapping = kPhaseMapping, .applies_when = kFmConditions, .applies_any = kModulatorPhaseAny[0]},
    plugin::SettingDescriptorExtras{
        .mapping = kPhaseMapping, .applies_when = kFmConditions, .applies_any = kModulatorPhaseAny[1]}};
/// The interpolation menu lists linear first: menu 0 stores 1.
constexpr plugin::DisplayMapping kInterpolationMapping{.kind = plugin::DisplayMappingKind::INVERTED_BOOL};
constexpr std::array kInterpolationExtras{
    plugin::SettingDescriptorExtras{
        .mapping = kInterpolationMapping, .applies_when = kSubtractiveConditions, .applies_any = kInterpolationAny[0]},
    plugin::SettingDescriptorExtras{
        .mapping = kInterpolationMapping, .applies_when = kSubtractiveConditions, .applies_any = kInterpolationAny[1]}};

/// A transpose menu: one value in cents over its semitones and its cents row, @p partner.
constexpr plugin::SettingDescriptorExtras transpose_extras(std::string_view partner,
                                                           std::span<const plugin::SettingCondition> when = {},
                                                           std::span<const Alternatives> any = {}) {
	return {.mapping = {.kind = plugin::DisplayMappingKind::TRANSPOSE_CENTS, .partner = partner},
	        .applies_when = when,
	        .applies_any = any};
}
constexpr plugin::SettingDescriptorExtras kTransposeExtras = transpose_extras("cents");
constexpr std::array kOscTransposeExtras{transpose_extras("osc1/cents", {}, kOscTuningAny[0]),
                                         transpose_extras("osc2/cents", {}, kOscTuningAny[1])};
/// An oscillator's cents show within its transpose's menu, where that shows.
constexpr std::array kOscCentsExtras{plugin::SettingDescriptorExtras{.applies_any = kOscTuningAny[0]},
                                     plugin::SettingDescriptorExtras{.applies_any = kOscTuningAny[1]}};
constexpr std::array kModulatorTransposeExtras{transpose_extras("modulator1/cents", kFmConditions),
                                               transpose_extras("modulator2/cents", kFmConditions)};

/// A new sound's settings (a default SoundState's), as the rows store them.
constexpr int32_t kDefaultMaxVoices = 8;
constexpr int32_t kDefaultUnisonDetune = 8;
constexpr std::array<int32_t, 2> kDefaultModulatorTranspose{0, -12};
/// A new sound's oscillators run free; its modulators retrigger at 0.
constexpr int32_t kFreeRunning = -1;

#define OSC_ROWS(n)                                                                                                    \
	SettingInfo{.path = "osc" #n "/type",                                                                              \
	            .kind = CHOICE,                                                                                        \
	            .choices = kOscTypes,                                                                                  \
	            .default_value = std::to_underlying(OscType::SQUARE),                                                  \
	            .extras = &kOscTypeExtras[n - 1]},                                                                     \
	    SettingInfo{.path = "osc" #n "/transpose",                                                                     \
	                .min = -kTransposeRange,                                                                           \
	                .max = kTransposeRange,                                                                            \
	                .units = plugin::SettingUnits::CENTS,                                                              \
	                .extras = &kOscTransposeExtras[n - 1]},                                                            \
	    SettingInfo{.path = "osc" #n "/cents",                                                                         \
	                .min = -kCentsRange,                                                                               \
	                .max = kCentsRange,                                                                                \
	                .units = plugin::SettingUnits::CENTS,                                                              \
	                .extras = &kOscCentsExtras[n - 1]},                                                                \
	    SettingInfo{.path = "osc" #n "/retrigPhase",                                                                   \
	                .min = kPhaseMin,                                                                                  \
	                .max = kPhaseMax,                                                                                  \
	                .default_value = kFreeRunning,                                                                     \
	                .extras = &kOscPhaseExtras[n - 1]},                                                                \
	    SettingInfo{.path = "osc" #n "/loopMode",                                                                      \
	                .kind = CHOICE,                                                                                    \
	                .choices = kLoopModes,                                                                             \
	                .extras = &kSamplePlaybackExtras[n - 1]},                                                          \
	    SettingInfo{.path = "osc" #n "/reversed", .kind = BOOL, .extras = &kSamplePlaybackExtras[n - 1]},              \
	    SettingInfo{.path = "osc" #n "/timeStretchEnable", .kind = BOOL, .extras = &kSamplePlaybackExtras[n - 1]},     \
	    SettingInfo{                                                                                                   \
	        .path = "osc" #n "/timeStretchAmount", .min = -48, .max = 48, .extras = &kSamplePlaybackExtras[n - 1]},    \
	    SettingInfo{.path = "osc" #n "/linearInterpolation", .kind = BOOL, .extras = &kInterpolationExtras[n - 1]},    \
	    SettingInfo{.path = "osc" #n "/sampleRanges", .kind = STRUCTURED},                                             \
	    SettingInfo{.path = "osc" #n "/wavetableRanges", .kind = STRUCTURED},                                          \
	    SettingInfo{.path = "osc" #n "/fileName", .kind = STRUCTURED},                                                 \
	    SettingInfo{.path = "osc" #n "/zone", .kind = STRUCTURED},                                                     \
	    SettingInfo{.path = "osc" #n "/dx7patch", .kind = STRUCTURED},                                                 \
	    SettingInfo{.path = "osc" #n "/dx7enginemode", .kind = STRUCTURED}, SettingInfo {                              \
		.path = "osc" #n "/dx7randomdetune", .kind = STRUCTURED                                                        \
	}

/// An LFO's sync menu: one value over its level and its type row, @p partner.
constexpr plugin::SettingDescriptorExtras lfo_sync_extras(std::string_view partner) {
	return {.mapping = {.kind = plugin::DisplayMappingKind::COMBINED_SYNC, .partner = partner}};
}
constexpr std::array kLfoSyncExtras{lfo_sync_extras("lfo1/syncType"), lfo_sync_extras("lfo2/syncType"),
                                    lfo_sync_extras("lfo3/syncType"), lfo_sync_extras("lfo4/syncType")};

// An LFO's syncLevel is the level itself (1–9, 0 = off), whatever the song's tick magnitude; a file, and so a slot,
// writes it relative to that magnitude. Its range is the internal one's, the numbers a song at the default magnitude
// writes.
#define LFO_ROWS(n)                                                                                                    \
	SettingInfo{.path = "lfo" #n "/type",                                                                              \
	            .kind = CHOICE,                                                                                        \
	            .choices = kLfoTypes,                                                                                  \
	            .default_value = std::to_underlying(LFOType::TRIANGLE)},                                               \
	    SettingInfo{.path = "lfo" #n "/syncLevel",                                                                     \
	                .min = 0,                                                                                          \
	                .max = plugin::kMaxSyncLevel,                                                                      \
	                .units = plugin::SettingUnits::SYNC_LEVEL,                                                         \
	                .extras = &kLfoSyncExtras[n - 1]},                                                                 \
	    SettingInfo {                                                                                                  \
		.path = "lfo" #n "/syncType", .kind = CHOICE, .choices = kSyncTypes                                            \
	}

/// The follow input shows for an MPE envelope alone.
constexpr std::array<int32_t, 1> kMpeValue{std::to_underlying(synth::EnvelopeMode::MPE)};
constexpr std::array<std::array<plugin::SettingCondition, 1>, 4> kEnvelopeIsMpe{
    {{plugin::setting_is("envelope1Settings/mode", kMpeValue)},
     {plugin::setting_is("envelope2Settings/mode", kMpeValue)},
     {plugin::setting_is("envelope3Settings/mode", kMpeValue)},
     {plugin::setting_is("envelope4Settings/mode", kMpeValue)}}};
constexpr std::array<plugin::SettingDescriptorExtras, 4> kEnvelopeFollowExtras{{{.applies_when = kEnvelopeIsMpe[0]},
                                                                                {.applies_when = kEnvelopeIsMpe[1]},
                                                                                {.applies_when = kEnvelopeIsMpe[2]},
                                                                                {.applies_when = kEnvelopeIsMpe[3]}}};

// An envelope's settings are main's `envelopeNSettings` attributes: its mode, its one curve control (classic 32) and
// what MPE mode follows.
#define ENVELOPE_ROWS(n)                                                                                               \
	SettingInfo{.path = "envelope" #n "Settings/mode", .kind = CHOICE, .choices = kEnvelopeModes},                     \
	    SettingInfo{.path = "envelope" #n "Settings/curve",                                                            \
	                .choices = {},                                                                                     \
	                .min = synth::kMinCurve,                                                                           \
	                .max = synth::kMaxCurve,                                                                           \
	                .default_value = synth::kClassicCurve},                                                            \
	    SettingInfo {                                                                                                  \
		.path = "envelope" #n "Settings/follow", .kind = CHOICE, .choices = kEnvelopeFollows,                          \
		.extras = &kEnvelopeFollowExtras[(n) - 1]                                                                      \
	}

#define MODULATOR_ROWS(n)                                                                                              \
	SettingInfo{.path = "modulator" #n "/transpose",                                                                   \
	            .min = -kTransposeRange,                                                                               \
	            .max = kTransposeRange,                                                                                \
	            .default_value = kDefaultModulatorTranspose[n - 1],                                                    \
	            .units = plugin::SettingUnits::CENTS,                                                                  \
	            .extras = &kModulatorTransposeExtras[n - 1]},                                                          \
	    SettingInfo{.path = "modulator" #n "/cents",                                                                   \
	                .min = -kCentsRange,                                                                               \
	                .max = kCentsRange,                                                                                \
	                .units = plugin::SettingUnits::CENTS,                                                              \
	                .extras = &kFmExtras},                                                                             \
	    SettingInfo {                                                                                                  \
		.path = "modulator" #n "/retrigPhase", .min = kPhaseMin, .max = kPhaseMax,                                     \
		.extras = &kModulatorPhaseExtras[n - 1]                                                                        \
	}

constexpr std::array kRows{
    SettingInfo{.path = "polyphonic",
                .kind = CHOICE,
                .choices = kPolyphony,
                .default_value = std::to_underlying(PolyphonyMode::POLY),
                .extras = &kPolyphonyExtras},
    SettingInfo{.path = "voicePriority",
                .kind = CHOICE,
                .choices = plugin::kVoicePriorityWords,
                .default_value = std::to_underlying(plugin::VoicePriority::MEDIUM)},
    SettingInfo{.path = "mode", .kind = CHOICE, .choices = kSynthModes, .extras = &kModeExtras},
    SettingInfo{.path = "transpose",
                .min = -kTransposeRange,
                .max = kTransposeRange,
                .units = plugin::SettingUnits::CENTS,
                .extras = &kTransposeExtras},
    SettingInfo{.path = "cents", .min = -kCentsRange, .max = kCentsRange, .units = plugin::SettingUnits::CENTS},
    // A file holds any limit 1–127; the menu offers 0 (off) to kMaxVoicesMenu.
    SettingInfo{.path = "maxVoices",
                .min = 1,
                .max = kMaxVoicesOff,
                .user = {.user_min = 0,
                         .user_max = kMaxVoicesMenu,
                         .stored_min = 0,
                         .stored_max = kMaxVoicesMenu,
                         .sentinels = kMaxVoicesSentinels},
                .default_value = kDefaultMaxVoices,
                .extras = &kMaxVoicesExtras},
    OSC_ROWS(1),
    OSC_ROWS(2),
    SettingInfo{.path = "osc2/oscillatorSync", .kind = BOOL, .extras = &kOscSyncExtras},
    LFO_ROWS(1),
    LFO_ROWS(2),
    LFO_ROWS(3),
    LFO_ROWS(4),
    ENVELOPE_ROWS(1),
    ENVELOPE_ROWS(2),
    ENVELOPE_ROWS(3),
    ENVELOPE_ROWS(4),
    MODULATOR_ROWS(1),
    MODULATOR_ROWS(2),
    SettingInfo{.path = "modulator2/toModulator1", .kind = BOOL, .extras = &kFmExtras},
    SettingInfo{.path = "unison/num", .min = 1, .max = kMaxUnisonVoices, .default_value = 1},
    SettingInfo{.path = "unison/detune", .min = 0, .max = kMaxUnisonDetune, .default_value = kDefaultUnisonDetune},
    SettingInfo{.path = "unison/spread", .min = 0, .max = kMaxUnisonSpread},
};

#undef OSC_ROWS
#undef LFO_ROWS
#undef MODULATOR_ROWS

using Answer = std::optional<serialization::Value>;

Answer number(int64_t n) {
	return serialization::Value::makeNumber(n);
}

Answer flag(bool on) {
	return number(on ? 1 : 0);
}

Answer word(std::string_view text) {
	return serialization::Value::makeScalar(std::string(text));
}

/// A retrigger phase as a file writes it: the 32-bit phase as a signed integer.
Answer phase(uint32_t value) {
	return number(static_cast<int32_t>(value));
}

/// The digit after a family's prefix as an index from 0, or nothing when @p path has no `<digit>/` there.
std::optional<size_t> indexAfter(std::string_view path, size_t at, char last) {
	if (path.size() <= at + 1 || path[at] < '1' || path[at] > last || path[at + 1] != '/') {
		return std::nullopt;
	}
	return static_cast<size_t>(path[at] - '1');
}

/// The instrument's rows, then its effects'.
constexpr auto kAllRows = [] {
	std::array<plugin::SettingInfo, kRows.size() + effects::kEffectSettings.size()> all{};
	std::ranges::copy(kRows, all.begin());
	std::ranges::copy(effects::kEffectSettings, all.begin() + kRows.size());
	return all;
}();
static_assert(plugin::conditions_are_valid(kAllRows));
static_assert(plugin::mappings_are_valid(kAllRows));

} // namespace menu_tables
} // namespace

const std::span<const plugin::SettingInfo> kSettings{kAllRows};
const std::span<const plugin::SettingInfo> kClipScopedSettings{plugin::kArpSettingRows};

std::optional<serialization::Value> canonical_setting(std::string_view path, const serialization::Value& value) {
	if (!check_setting(path, value)) {
		return std::nullopt;
	}
	if (plugin::find_setting(kSettings, path)->kind == INTEGER) {
		return serialization::Value::makeNumber(*plugin::whole_number(*serialization::text_of(&value)));
	}
	return value;
}

namespace {

int32_t intOf(const serialization::Value& value) {
	return serialization::int_of(&value).value_or(0);
}

} // namespace

bool check_setting(std::string_view path, const serialization::Value& value) {
	return plugin::check_setting(kSettings, path, value);
}

namespace {

// The rows' readers and writers, over a sound's record through its readers and its record edits.

Answer top_level_value(const SoundState& state, std::string_view path) {
	const synth::VoicingState v = voicing(state);
	if (path == "polyphonic") {
		return word(to_string(v.polyphony));
	}
	if (path == "voicePriority") {
		return number(std::to_underlying(v.priority));
	}
	if (path == "mode") {
		return word(to_string(synth_mode(state)));
	}
	if (path == "transpose") {
		return number(v.master.transpose);
	}
	if (path == "cents") {
		return number(v.master.cents);
	}
	if (path == "maxVoices") {
		return number(v.max_voices);
	}
	return std::nullopt;
}

// One branch per oscillator key, each read where the source's kind and the engine say it applies:
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
Answer oscillator_value(const SoundState& state, std::string_view path, bool held) {
	const std::optional<size_t> s = indexAfter(path, 3, '2');
	if (!s) {
		return std::nullopt;
	}
	const auto id = static_cast<SourceId>(*s);
	const OscType type = source_type(state, id);
	const SamplePlayback playback = sample_playback(state, id);
	const std::string_view rest = path.substr(5);
	const bool fm = !held && synth_mode(state) == SynthMode::FM;
	const bool sample = held || type == OscType::SAMPLE;
	const bool input = held || type == OscType::INPUT_L || type == OscType::INPUT_R || type == OscType::INPUT_STEREO;
	if (rest == "linearInterpolation") {
		return !fm && (sample || input) ? flag(playback.interpolation == plugin::InterpolationMode::LINEAR)
		                                : std::nullopt;
	}
	if (rest == "loopMode" || rest == "reversed" || rest == "timeStretchEnable" || rest == "timeStretchAmount") {
		if (fm || !sample) {
			return std::nullopt;
		}
		if (rest == "loopMode") {
			return number(std::to_underlying(playback.repeat_mode));
		}
		if (rest == "reversed") {
			return flag(playback.reversed);
		}
		if (rest == "timeStretchEnable") {
			return flag(playback.pitch_and_speed_independent);
		}
		return number(playback.time_stretch_amount);
	}
	if (rest == "type") {
		return fm ? std::nullopt : word(to_string(type));
	}
	if (rest == "transpose" || rest == "cents") {
		if (!held && !fm && sample) {
			return std::nullopt; // a sample source's tuning is its ranges'
		}
		const Tuning tuning = source_tuning(state, id);
		return rest == "transpose" ? number(tuning.transpose) : number(tuning.cents);
	}
	if (rest == "retrigPhase") {
		return phase(phase_word(source_retrigger(state, id)));
	}
	if (rest == "oscillatorSync" && *s == 1) {
		return flag(oscillator_sync(state));
	}
	return std::nullopt;
}

Answer lfo_value(const SoundState& state, std::string_view path) {
	const std::optional<size_t> l = indexAfter(path, 3, '4');
	if (!l) {
		return std::nullopt;
	}
	const synth::LfoState lfo_state = lfo(state, *l);
	const std::string_view rest = path.substr(5);
	if (rest == "type") {
		return word(to_string(lfo_state.shape));
	}
	if (rest == "syncLevel") {
		return number(plugin::index_of(lfo_state.sync_level));
	}
	if (rest == "syncType") {
		return number(std::to_underlying(lfo_state.sync_type));
	}
	return std::nullopt;
}

/// The envelope an `envelope<n>Settings/` path names, and the rest of the path; nullopt for any other path.
std::optional<std::pair<size_t, std::string_view>> envelope_path(std::string_view path) {
	constexpr std::string_view kHead = "envelope";
	constexpr std::string_view kTail = "Settings/";
	if (!path.starts_with(kHead) || path.size() <= kHead.size() + kTail.size() || path[kHead.size()] < '1'
	    || path[kHead.size()] > '4' || path.substr(kHead.size() + 1, kTail.size()) != kTail) {
		return std::nullopt;
	}
	return std::pair{static_cast<size_t>(path[kHead.size()] - '1'), path.substr(kHead.size() + 1 + kTail.size())};
}

Answer envelope_value(const SoundState& state, std::string_view path) {
	const auto at = envelope_path(path);
	if (!at) {
		return std::nullopt;
	}
	const synth::EnvelopeState e = envelope(state, at->first);
	if (at->second == "mode") {
		return word(synth::to_string(e.mode));
	}
	if (at->second == "curve") {
		return number(e.curve);
	}
	if (at->second == "follow") {
		return word(synth::to_string(e.follow));
	}
	return std::nullopt;
}

Answer modulator_value(const SoundState& state, std::string_view path, bool held) {
	const std::optional<size_t> m = indexAfter(path, 9, '2');
	if (!m || (!held && synth_mode(state) != SynthMode::FM)) {
		return std::nullopt; // only FM writes its modulators
	}
	const synth::Pitch pitch = modulators(state)[*m];
	const std::string_view rest = path.substr(11);
	if (rest == "transpose") {
		return number(pitch.tuning.transpose);
	}
	if (rest == "cents") {
		return number(pitch.tuning.cents);
	}
	if (rest == "retrigPhase") {
		return phase(phase_word(pitch.retrigger));
	}
	if (rest == "toModulator1" && *m == 1) {
		return flag(modulator1_to_modulator0(state));
	}
	return std::nullopt;
}

Answer unison_value(const SoundState& state, std::string_view path) {
	const synth::UnisonState u = unison(state);
	const std::string_view rest = path.substr(7);
	if (rest == "num") {
		return number(u.count);
	}
	if (rest == "detune") {
		return number(u.detune);
	}
	if (rest == "spread") {
		return number(u.spread);
	}
	return std::nullopt;
}

/// A row's answer, by the family its path's prefix names (an effect's row by the effects' own reader).
Answer any_value(const SoundState& state, std::string_view path, bool held = false) {
	const SettingInfo* row = plugin::find_setting(kSettings, path);
	if (row == nullptr || row->kind == STRUCTURED) {
		return std::nullopt;
	}
	if (plugin::find_setting(effects::kEffectSettings, path) != nullptr) {
		return effects::effect_setting_value(sound_effects(state), path);
	}
	if (path.starts_with("osc")) {
		return oscillator_value(state, path, held);
	}
	if (path.starts_with("lfo")) {
		return lfo_value(state, path);
	}
	if (path.starts_with("envelope")) {
		return envelope_value(state, path);
	}
	if (path.starts_with("modulator")) {
		return modulator_value(state, path, held);
	}
	if (path.starts_with("unison/")) {
		return unison_value(state, path);
	}
	return top_level_value(state, path);
}

bool write_top_level(SoundState& state, std::string_view path, const serialization::Value& value) {
	const int32_t n = intOf(value);
	synth::VoicingState& v = state.plays.voicing;
	if (path == "polyphonic") {
		v.polyphony = parse_polyphony_mode(value.str());
	}
	else if (path == "voicePriority") {
		v.priority = static_cast<plugin::VoicePriority>(n);
	}
	else if (path == "mode") {
		set_engine(state, parse_synth_mode(value.str()));
	}
	else if (path == "transpose") {
		v.master.transpose = static_cast<int16_t>(n);
	}
	else if (path == "cents") {
		v.master.cents = static_cast<int8_t>(n);
	}
	else if (path == "maxVoices") {
		v.max_voices = static_cast<uint8_t>(n);
	}
	else {
		return false;
	}
	return true;
}

// One branch per oscillator key, each written to the part of the state that holds it; a type switch through the slot's
// whole source:
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool write_oscillator(SoundState& state, std::string_view path, const serialization::Value& value) {
	const std::optional<size_t> s = indexAfter(path, 3, '2');
	if (!s) {
		return false;
	}
	const std::string_view rest = path.substr(5);
	const int32_t n = intOf(value);
	constexpr std::array<std::string_view, 9> kSlotKeys{
	    "linearInterpolation", "loopMode", "reversed",   "timeStretchEnable", "timeStretchAmount", "type",
	    "transpose",           "cents",    "retrigPhase"};
	if (std::ranges::find(kSlotKeys, rest) == kSlotKeys.end() && (rest != "oscillatorSync" || *s != 1)) {
		return false;
	}
	if (rest == "type") {
		edit_slot(state, *s, [&](SlotEdit& edit) { edit.source.osc_type = parse_osc_type(value.str()); });
		return true;
	}
	if (rest == "oscillatorSync") { // on the second oscillator
		set_oscillator_sync(state, n != 0);
		return true;
	}
	if (rest == "transpose" || rest == "cents" || rest == "retrigPhase") {
		synth::Pitch& pitch = source_pitch_of(state, *s);
		if (rest == "transpose") {
			pitch.tuning.transpose = static_cast<int16_t>(n);
		}
		else if (rest == "cents") {
			pitch.tuning.cents = static_cast<int8_t>(n);
		}
		else {
			pitch.retrigger = retrigger_of_word(static_cast<uint32_t>(n)); // the phase as a file writes it, signed
		}
		return true;
	}
	synth::SamplePlayback& playback = sample_playback_of(state, *s);
	if (rest == "linearInterpolation") {
		playback.interpolation = n != 0 ? plugin::InterpolationMode::LINEAR : plugin::InterpolationMode::SMOOTH;
	}
	else if (rest == "loopMode") {
		playback.repeat_mode = static_cast<SampleRepeatMode>(n);
	}
	else if (rest == "reversed") {
		playback.reversed = n != 0;
	}
	else if (rest == "timeStretchEnable") {
		playback.pitch_and_speed_independent = n != 0;
	}
	else { // timeStretchAmount
		playback.time_stretch_amount = static_cast<int8_t>(n);
	}
	return true;
}

bool write_lfo(SoundState& state, std::string_view path, const serialization::Value& value) {
	const std::optional<size_t> l = indexAfter(path, 3, '4');
	if (!l) {
		return false;
	}
	synth::LfoState& lfo_state = lfo_of(state, *l);
	const std::string_view rest = path.substr(5);
	if (rest == "type") {
		lfo_state.shape = parse_lfo_type(value.str());
	}
	else if (rest == "syncLevel") {
		lfo_state.sync_level = plugin::sync_level_from_index(intOf(value));
	}
	else if (rest == "syncType") {
		lfo_state.sync_type = static_cast<plugin::SyncType>(intOf(value));
	}
	else {
		return false;
	}
	return true;
}

bool write_envelope(SoundState& state, std::string_view path, const serialization::Value& value) {
	const auto at = envelope_path(path);
	if (!at) {
		return false;
	}
	synth::EnvelopeState& e = envelope_of(state, at->first);
	if (at->second == "mode") {
		e.mode = synth::envelope_mode_from(value.str());
	}
	else if (at->second == "curve") {
		e.curve = synth::EnvelopeState::clamped_curve(intOf(value));
	}
	else if (at->second == "follow") {
		e.follow = synth::envelope_follow_from(value.str());
	}
	else {
		return false;
	}
	return true;
}

bool write_modulator(SoundState& state, std::string_view path, const serialization::Value& value) {
	const std::optional<size_t> m = indexAfter(path, 9, '2');
	if (!m) {
		return false;
	}
	synth::Pitch& pitch = modulators_of(state)[*m];
	const std::string_view rest = path.substr(11);
	const int32_t n = intOf(value);
	if (rest == "transpose") {
		pitch.tuning.transpose = static_cast<int16_t>(n);
	}
	else if (rest == "cents") {
		pitch.tuning.cents = static_cast<int8_t>(n);
	}
	else if (rest == "retrigPhase") {
		pitch.retrigger = retrigger_of_word(static_cast<uint32_t>(n));
	}
	else if (rest == "toModulator1" && *m == 1) {
		modulator_routing_of(state) = n != 0;
	}
	else {
		return false;
	}
	return true;
}

bool write_unison(SoundState& state, std::string_view path, const serialization::Value& value) {
	synth::UnisonState& u = state.plays.voicing.unison;
	const std::string_view rest = path.substr(7);
	const int32_t n = intOf(value);
	if (rest == "num") {
		u.count = static_cast<uint8_t>(n);
	}
	else if (rest == "detune") {
		u.detune = static_cast<int8_t>(n);
	}
	else if (rest == "spread") {
		u.spread = static_cast<uint8_t>(n);
	}
	else {
		return false;
	}
	return true;
}

} // namespace

std::optional<serialization::Value> setting_value(const SoundState& state, std::string_view path) {
	return any_value(state, path);
}

std::optional<int32_t> held_setting(const SoundState& state, std::string_view path) {
	const SettingInfo* row = plugin::find_setting(kSettings, path);
	const Answer value = any_value(state, path, true);
	if (!value) {
		return std::nullopt;
	}
	if (row->kind == CHOICE) {
		return plugin::stored_value(*row, value->str());
	}
	const std::optional<int64_t> n = plugin::whole_number(value->str());
	return n ? std::optional<int32_t>{static_cast<int32_t>(*n)} : std::nullopt;
}

bool allows_setting(const SoundState& state, std::string_view path, const serialization::Value& value) {
	const auto ringable = [](OscType type) { return type <= OscType::WAVETABLE; };
	if (path == "mode" && parse_synth_mode(value.str()) == SynthMode::RINGMOD) {
		return ringable(source_type(state, SourceId::A)) && ringable(source_type(state, SourceId::B));
	}
	if (synth_mode(state) == SynthMode::RINGMOD && path.starts_with("osc") && path.ends_with("/type")) {
		return ringable(parse_osc_type(value.str()));
	}
	return true;
}

bool takes_setting(const SoundState& state, std::string_view path) {
	return any_value(state, path).has_value();
}

std::vector<std::string_view> write_setting(SoundState& state, std::string_view path,
                                            const serialization::Value& value) {
	const SettingInfo* row = plugin::find_setting(kSettings, path);
	if (row == nullptr || row->kind == STRUCTURED) {
		return {};
	}
	const SoundState before = state;
	if (plugin::find_setting(effects::kEffectSettings, path) != nullptr) {
		(void)effects::write_effect_setting(state.plays.fx, path, value);
	}
	else if (path.starts_with("osc")) {
		(void)write_oscillator(state, path, value);
	}
	else if (path.starts_with("lfo")) {
		(void)write_lfo(state, path, value);
	}
	else if (path.starts_with("envelope")) {
		(void)write_envelope(state, path, value);
	}
	else if (path.starts_with("modulator")) {
		(void)write_modulator(state, path, value);
	}
	else if (path.starts_with("unison/")) {
		(void)write_unison(state, path, value);
	}
	else {
		(void)write_top_level(state, path, value);
	}
	return changed_entries(before, state);
}

} // namespace deluge::instruments::classic
