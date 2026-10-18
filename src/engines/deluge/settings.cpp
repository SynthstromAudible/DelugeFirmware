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

#include "engines/deluge/settings.h"

#include "engine-api/settings.h"

#include "engine-api/types/arp.h"
#include "engine-api/types/sync.h"
#include "engine-api/types/voice_priority.h"
#include "engines/deluge/codec.h"
#include "engines/deluge/words.h"
#include "engines/effects/settings.h"
#include "serialization/tree.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <string>
#include <utility>

namespace deluge::instrument {

namespace {
/// The menu-time tables: read when a menu or an Edit looks a setting up, never by the audio tier or an interrupt.
/// The device image places this namespace's data in SDRAM (linker/sdram_sections.x matches its name).
inline namespace menu_tables {

using engine::SettingInfo;
using enum engine::SettingKind;

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
constexpr auto kSynthModes = names_of<SynthMode, 3>();
constexpr auto kOscTypes = names_of<OscType, 12>();
constexpr auto kLfoTypes = names_of<LFOType, 7>();
/// A synced rate's type, as its stored number (even, triplet, dotted).
constexpr std::array<std::string_view, 3> kSyncTypes{"0", "10", "19"};
/// A sound's MIDI output channel as a file writes it: 0–15, or 255 for none. Nothing between is a channel.
constexpr std::array<std::string_view, 17> kMidiChannels{"0", "1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",
                                                         "9", "10", "11", "12", "13", "14", "15", "255"};

constexpr int32_t kTransposeRange = 96; ///< semitones either way: the transpose menus' ±9600 cents
constexpr int32_t kCentsRange = 50;
/// A retrigger phase as a file writes it: the 32-bit phase (a fraction of 2^32) as a signed integer, −1 (0xFFFFFFFF)
/// free-running. The menu's degrees are 11930464 apart.
constexpr int32_t kPhaseMin = std::numeric_limits<int32_t>::min();
constexpr int32_t kPhaseMax = std::numeric_limits<int32_t>::max();
/// The voice limit the max-voices menu's "off" writes: 1–16 are limits, this one is none in practice.
constexpr int32_t kMaxVoicesOff = 127;
/// The greatest limit the max-voices menu offers.
constexpr int32_t kMaxVoicesMenu = 16;
/// The max-voices menu's 0 is "off", stored as kMaxVoicesOff.
constexpr std::array<engine::SettingSentinel, 1> kMaxVoicesSentinels{{{.user = 0, .stored = kMaxVoicesOff}}};

/// The note an unset drum note (stored 255, past the row) plays and shows as.
constexpr int32_t kUnsetDrumNoteShownAs = 60;

/// A sample's repeat mode as its stored number, in SampleRepeatMode's order (cut, once, loop, stretch).
constexpr std::array<std::string_view, 4> kLoopModes{"0", "1", "2", "3"};
static_assert(std::to_underlying(SampleRepeatMode::STRETCH) + 1 == kLoopModes.size());

/// The owners a kit lane stands for.
constexpr std::array<int32_t, 1> kKitLaneOwner{std::to_underlying(engine::SettingContext::Owner::KIT_LANE)};
/// The owners that hold a kit's sounds: a kit lane, and the kit itself.
constexpr std::array<int32_t, 2> kKitOwners{std::to_underlying(engine::SettingContext::Owner::KIT_LANE),
                                            std::to_underlying(engine::SettingContext::Owner::KIT)};
/// A kit lane: it alone offers choke (which shares a kit's voices among its lanes), a send to the kit's sidechain and
/// a drum note.
constexpr std::array kOnKitLane{engine::owner_is(kKitLaneOwner)};
constexpr std::array kPolyphonyWhen{engine::ChoiceCondition{std::to_underlying(PolyphonyMode::CHOKE), kOnKitLane}};
constexpr engine::SettingDescriptorExtras kPolyphonyExtras{.choice_when = kPolyphonyWhen};

/// The synth modes, as the "mode" row stores them.
constexpr std::array<int32_t, 1> kSubtractiveValue{std::to_underlying(SynthMode::SUBTRACTIVE)};
constexpr std::array<int32_t, 1> kFmValue{std::to_underlying(SynthMode::FM)};
constexpr std::array<int32_t, 1> kRingModeValue{std::to_underlying(SynthMode::RINGMOD)};

/// Ring mod mixes oscillators alone, so it offers no sample or input.
constexpr std::array kNotRingMode{engine::setting_is_not("mode", kRingModeValue)};
/// DX7 only where the owner may hold one: not in a kit's sounds.
constexpr std::array kDx7Conditions{engine::setting_is_not("mode", kRingModeValue), engine::owner_is_not(kKitOwners)};
/// With no audio input plugged in, one input entry (the left input's) stands for the three.
constexpr std::array kInputConditions{engine::setting_is_not("mode", kRingModeValue), engine::inputs_are(true)};
/// The second oscillator's menu never offers DX7: only the first can hold one.
constexpr std::array kNeverOffered{engine::never()};

/// The oscillator type menu's conditions; the choice list is shared, since stored indices are what files hold.
constexpr std::array<engine::ChoiceCondition, 5> osc_type_when(std::span<const engine::SettingCondition> dx7) {
	return {{{std::to_underlying(OscType::SAMPLE), kNotRingMode},
	         {std::to_underlying(OscType::DX7), dx7},
	         {std::to_underlying(OscType::INPUT_L), kNotRingMode},
	         {std::to_underlying(OscType::INPUT_R), kInputConditions},
	         {std::to_underlying(OscType::INPUT_STEREO), kInputConditions}}};
}
constexpr std::array kOsc1TypeWhen = osc_type_when(kDx7Conditions);
constexpr std::array kOsc2TypeWhen = osc_type_when(kNeverOffered);
/// FM's carriers are sines: an oscillator's type menu shows outside it.
constexpr std::array kNotFm{engine::setting_is_not("mode", kFmValue)};
constexpr std::array kOscTypeExtras{
    engine::SettingDescriptorExtras{.applies_when = kNotFm, .choice_when = kOsc1TypeWhen},
    engine::SettingDescriptorExtras{.applies_when = kNotFm, .choice_when = kOsc2TypeWhen}};

/// The oscillator types ring mod can ring, as allows_setting has them: every type up to the wavetable.
constexpr auto kRingableTypes = [] {
	std::array<int32_t, std::to_underlying(OscType::WAVETABLE) + 1> types{};
	for (std::size_t i = 0; i < types.size(); ++i) {
		types[i] = static_cast<int32_t>(i);
	}
	return types;
}();
/// The synth mode menu shows only while ring mod could be chosen: neither oscillator past a wavetable.
constexpr std::array kModeConditions{engine::setting_is("osc1/type", kRingableTypes),
                                     engine::setting_is("osc2/type", kRingableTypes)};
constexpr engine::SettingDescriptorExtras kModeExtras{.applies_when = kModeConditions};

/// The polyphony that has a voice count to limit.
constexpr std::array<int32_t, 1> kPolyValue{std::to_underlying(PolyphonyMode::POLY)};
constexpr std::array kPolyConditions{engine::setting_is("polyphonic", kPolyValue)};

/// A stored limit past the menu's (none a menu writes) shows as off; the limit shows for a polyphonic sound alone.
constexpr engine::SettingDescriptorExtras kMaxVoicesExtras{
    .mapping = {.kind = engine::DisplayMappingKind::CLAMP_HIGH, .clamp_to = 0}, .applies_when = kPolyConditions};

/// A drum note's menu: an unset note shows as the one it plays; only a kit lane has one.
constexpr engine::SettingDescriptorExtras kNoteForDrumExtras{
    .mapping = {.kind = engine::DisplayMappingKind::CLAMP_HIGH, .clamp_to = kUnsetDrumNoteShownAs},
    .applies_when = kOnKitLane};

/// An oscillator's sample playback rows: a subtractive sound's sample source with a file loaded.
constexpr std::array<int32_t, 1> kSampleValue{std::to_underlying(OscType::SAMPLE)};
constexpr std::array<std::array<engine::SettingCondition, 3>, 2> kSamplePlaybackConditions{{
    {engine::setting_is("mode", kSubtractiveValue), engine::setting_is("osc1/type", kSampleValue),
     engine::source_loaded(0)},
    {engine::setting_is("mode", kSubtractiveValue), engine::setting_is("osc2/type", kSampleValue),
     engine::source_loaded(1)},
}};
constexpr std::array kSamplePlaybackExtras{
    engine::SettingDescriptorExtras{.applies_when = kSamplePlaybackConditions[0]},
    engine::SettingDescriptorExtras{.applies_when = kSamplePlaybackConditions[1]}};

/// Oscillator sync resets the second oscillator from the first's cycle: neither may be a sample, nor the sound FM.
constexpr std::array kOscSyncConditions{engine::setting_is_not("mode", kFmValue),
                                        engine::setting_is_not("osc1/type", kSampleValue),
                                        engine::setting_is_not("osc2/type", kSampleValue)};
constexpr engine::SettingDescriptorExtras kOscSyncExtras{.applies_when = kOscSyncConditions};

/// The modulators play in FM alone.
constexpr std::array kFmConditions{engine::setting_is("mode", kFmValue)};
constexpr engine::SettingDescriptorExtras kFmExtras{.applies_when = kFmConditions};

/// The sidechain send menu: fiftieths of the whole send, rounded to the nearest.
constexpr engine::SettingDescriptorExtras kSendExtras{.mapping = {.kind = engine::DisplayMappingKind::FIFTIETHS},
                                                      .applies_when = kOnKitLane};

/// Each oscillator's type row, and the conditions on it and on its file that its tuning, phase and interpolation menus
/// show under: an oscillator playing a file shows them only once the file is loaded.
constexpr std::array<std::string_view, 2> kOscTypePaths{"osc1/type", "osc2/type"};
constexpr std::array<int32_t, 2> kFileTypes{std::to_underlying(OscType::SAMPLE),
                                            std::to_underlying(OscType::WAVETABLE)};
constexpr std::array<int32_t, 1> kWavetableValue{std::to_underlying(OscType::WAVETABLE)};
constexpr std::array<int32_t, 3> kInputTypes{std::to_underlying(OscType::INPUT_L), std::to_underlying(OscType::INPUT_R),
                                             std::to_underlying(OscType::INPUT_STEREO)};
constexpr std::array kSubtractiveConditions{engine::setting_is("mode", kSubtractiveValue)};

/// Per oscillator (index 0 the first): its type plays no file; its file is loaded; it plays a loaded wavetable; it
/// holds a sample in FM; it plays a loaded sample; it plays an input; it is not a wavetable.
template <std::size_t N>
using Conditions = std::array<engine::SettingCondition, N>;
constexpr std::array<Conditions<1>, 2> kPlaysNoFile{
    {{engine::setting_is_not(kOscTypePaths[0], kFileTypes)}, {engine::setting_is_not(kOscTypePaths[1], kFileTypes)}}};
constexpr std::array<Conditions<1>, 2> kFileLoaded{{{engine::source_loaded(0)}, {engine::source_loaded(1)}}};
constexpr std::array<Conditions<2>, 2> kLoadedWavetable{
    {{engine::setting_is(kOscTypePaths[0], kWavetableValue), engine::source_loaded(0)},
     {engine::setting_is(kOscTypePaths[1], kWavetableValue), engine::source_loaded(1)}}};
constexpr std::array<Conditions<2>, 2> kSampleInFm{
    {{engine::setting_is(kOscTypePaths[0], kSampleValue), engine::setting_is("mode", kFmValue)},
     {engine::setting_is(kOscTypePaths[1], kSampleValue), engine::setting_is("mode", kFmValue)}}};
constexpr std::array<Conditions<2>, 2> kLoadedSample{
    {{engine::setting_is(kOscTypePaths[0], kSampleValue), engine::source_loaded(0)},
     {engine::setting_is(kOscTypePaths[1], kSampleValue), engine::source_loaded(1)}}};
constexpr std::array<Conditions<1>, 2> kPlaysInput{
    {{engine::setting_is(kOscTypePaths[0], kInputTypes)}, {engine::setting_is(kOscTypePaths[1], kInputTypes)}}};
constexpr std::array<Conditions<1>, 2> kNotWavetable{{{engine::setting_is_not(kOscTypePaths[0], kWavetableValue)},
                                                      {engine::setting_is_not(kOscTypePaths[1], kWavetableValue)}}};

using Alternatives = std::span<const engine::SettingCondition>;
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
constexpr engine::DisplayMapping kPhaseMapping{.kind = engine::DisplayMappingKind::PHASE_DEGREES};
constexpr std::array kOscPhaseExtras{
    engine::SettingDescriptorExtras{.mapping = kPhaseMapping, .applies_any = kOscPhaseAny[0]},
    engine::SettingDescriptorExtras{.mapping = kPhaseMapping, .applies_any = kOscPhaseAny[1]}};
constexpr std::array kModulatorPhaseExtras{
    engine::SettingDescriptorExtras{
        .mapping = kPhaseMapping, .applies_when = kFmConditions, .applies_any = kModulatorPhaseAny[0]},
    engine::SettingDescriptorExtras{
        .mapping = kPhaseMapping, .applies_when = kFmConditions, .applies_any = kModulatorPhaseAny[1]}};
/// The interpolation menu lists linear first: menu 0 stores 1.
constexpr engine::DisplayMapping kInterpolationMapping{.kind = engine::DisplayMappingKind::INVERTED_BOOL};
constexpr std::array kInterpolationExtras{
    engine::SettingDescriptorExtras{
        .mapping = kInterpolationMapping, .applies_when = kSubtractiveConditions, .applies_any = kInterpolationAny[0]},
    engine::SettingDescriptorExtras{
        .mapping = kInterpolationMapping, .applies_when = kSubtractiveConditions, .applies_any = kInterpolationAny[1]}};
/// The MIDI output channel menu lists none first, then channels 1–16 (stored 0–15).
constexpr std::array<uint8_t, 17> kMidiChannelOrder{16, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
constexpr engine::SettingDescriptorExtras kMidiChannelExtras{
    .mapping = {.kind = engine::DisplayMappingKind::CHOICE_ORDER, .order = kMidiChannelOrder}};

/// A transpose menu: one value in cents over its semitones and its cents row, @p partner.
constexpr engine::SettingDescriptorExtras transpose_extras(std::string_view partner,
                                                           std::span<const engine::SettingCondition> when = {},
                                                           std::span<const Alternatives> any = {}) {
	return {.mapping = {.kind = engine::DisplayMappingKind::TRANSPOSE_CENTS, .partner = partner},
	        .applies_when = when,
	        .applies_any = any};
}
constexpr engine::SettingDescriptorExtras kTransposeExtras = transpose_extras("cents");
constexpr std::array kOscTransposeExtras{transpose_extras("osc1/cents", {}, kOscTuningAny[0]),
                                         transpose_extras("osc2/cents", {}, kOscTuningAny[1])};
/// An oscillator's cents show within its transpose's menu, where that shows.
constexpr std::array kOscCentsExtras{engine::SettingDescriptorExtras{.applies_any = kOscTuningAny[0]},
                                     engine::SettingDescriptorExtras{.applies_any = kOscTuningAny[1]}};
constexpr std::array kModulatorTransposeExtras{transpose_extras("modulator1/cents", kFmConditions),
                                               transpose_extras("modulator2/cents", kFmConditions)};

/// A new sound's settings (new_synth_state), as the rows store them.
constexpr int32_t kDefaultMaxVoices = 8;
constexpr int32_t kDefaultMidiChannel = 16; // "255": none
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
	                .units = engine::SettingUnits::CENTS,                                                              \
	                .extras = &kOscTransposeExtras[n - 1]},                                                            \
	    SettingInfo{.path = "osc" #n "/cents",                                                                         \
	                .min = -kCentsRange,                                                                               \
	                .max = kCentsRange,                                                                                \
	                .units = engine::SettingUnits::CENTS,                                                              \
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
constexpr engine::SettingDescriptorExtras lfo_sync_extras(std::string_view partner) {
	return {.mapping = {.kind = engine::DisplayMappingKind::COMBINED_SYNC, .partner = partner}};
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
	                .max = engine::kMaxSyncLevel,                                                                      \
	                .units = engine::SettingUnits::SYNC_LEVEL,                                                         \
	                .extras = &kLfoSyncExtras[n - 1]},                                                                 \
	    SettingInfo {                                                                                                  \
		.path = "lfo" #n "/syncType", .kind = CHOICE, .choices = kSyncTypes                                            \
	}

#define MODULATOR_ROWS(n)                                                                                              \
	SettingInfo{.path = "modulator" #n "/transpose",                                                                   \
	            .min = -kTransposeRange,                                                                               \
	            .max = kTransposeRange,                                                                                \
	            .default_value = kDefaultModulatorTranspose[n - 1],                                                    \
	            .units = engine::SettingUnits::CENTS,                                                                  \
	            .extras = &kModulatorTransposeExtras[n - 1]},                                                          \
	    SettingInfo{.path = "modulator" #n "/cents",                                                                   \
	                .min = -kCentsRange,                                                                               \
	                .max = kCentsRange,                                                                                \
	                .units = engine::SettingUnits::CENTS,                                                              \
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
                .choices = engine::kVoicePriorityWords,
                .default_value = std::to_underlying(engine::VoicePriority::MEDIUM)},
    SettingInfo{.path = "sideChainSend", .min = 0, .max = std::numeric_limits<int32_t>::max(), .extras = &kSendExtras},
    SettingInfo{.path = "mode", .kind = CHOICE, .choices = kSynthModes, .extras = &kModeExtras},
    SettingInfo{.path = "transpose",
                .min = -kTransposeRange,
                .max = kTransposeRange,
                .units = engine::SettingUnits::CENTS,
                .extras = &kTransposeExtras},
    SettingInfo{.path = "cents", .min = -kCentsRange, .max = kCentsRange, .units = engine::SettingUnits::CENTS},
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
    MODULATOR_ROWS(1),
    MODULATOR_ROWS(2),
    SettingInfo{.path = "modulator2/toModulator1", .kind = BOOL, .extras = &kFmExtras},
    SettingInfo{.path = "unison/num", .min = 1, .max = kMaxUnisonVoices, .default_value = 1},
    SettingInfo{.path = "unison/detune", .min = 0, .max = kMaxUnisonDetune, .default_value = kDefaultUnisonDetune},
    SettingInfo{.path = "unison/spread", .min = 0, .max = kMaxUnisonSpread},
    SettingInfo{.path = "midiOutput/channel",
                .kind = CHOICE,
                .choices = kMidiChannels,
                .default_value = kDefaultMidiChannel,
                .extras = &kMidiChannelExtras},
    // A new sound's note is unset (255, outside the row): its menu shows kUnsetDrumNoteShownAs there, and no menu
    // value stores it.
    SettingInfo{.path = "midiOutput/noteForDrum",
                .min = 0,
                .max = 127,
                .user = {.user_min = 0, .user_max = 127, .stored_min = 0, .stored_max = 127},
                .extras = &kNoteForDrumExtras},
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

Answer topLevelValue(const SoundState& state, std::string_view path, bool /*held*/) {
	if (path == "polyphonic") {
		return word(to_string(state.polyphonic));
	}
	if (path == "voicePriority") {
		return number(std::to_underlying(state.voice_priority));
	}
	if (path == "sideChainSend") {
		return number(state.side_chain_send_level);
	}
	if (path == "mode") {
		return word(to_string(state.synth_mode));
	}
	if (path == "transpose") {
		return number(state.transpose);
	}
	if (path == "cents") {
		return number(state.cents);
	}
	if (path == "maxVoices") {
		return number(state.max_voice_count);
	}
	return std::nullopt;
}

/// @p held: answer what the state holds whether or not the path applies to it.
Answer oscillatorValue(const SoundState& state, std::string_view path, bool held) {
	const std::optional<size_t> s = indexAfter(path, 3, '2');
	if (!s) {
		return std::nullopt;
	}
	const SourceState& src = state.sources[*s];
	const std::string_view rest = path.substr(5);
	const bool fm = !held && state.synth_mode == SynthMode::FM;
	const bool sample = held || src.osc_type == OscType::SAMPLE;
	const bool input = held || src.osc_type == OscType::INPUT_L || src.osc_type == OscType::INPUT_R
	                   || src.osc_type == OscType::INPUT_STEREO;
	if (rest == "linearInterpolation") {
		return !fm && (sample || input) ? flag(src.interpolation_mode == engine::InterpolationMode::LINEAR)
		                                : std::nullopt;
	}
	if (rest == "loopMode" || rest == "reversed" || rest == "timeStretchEnable" || rest == "timeStretchAmount") {
		if (fm || !sample) {
			return std::nullopt;
		}
		return rest == "loopMode"            ? number(std::to_underlying(src.repeat_mode))
		       : rest == "reversed"          ? flag(src.reversed)
		       : rest == "timeStretchEnable" ? flag(src.pitch_and_speed_independent)
		                                     : number(src.time_stretch_amount);
	}
	if (rest == "type") {
		return fm ? std::nullopt : word(to_string(src.osc_type));
	}
	if (rest == "transpose" || rest == "cents") {
		if (!held && !fm && sample) {
			return std::nullopt; // a sample source's tuning is its ranges'
		}
		return rest == "transpose" ? number(src.transpose) : number(src.cents);
	}
	if (rest == "retrigPhase") {
		return phase(state.osc_retrigger_phase[*s]);
	}
	if (rest == "oscillatorSync" && *s == 1) {
		return flag(state.oscillator_sync);
	}
	return std::nullopt;
}

Answer lfoValue(const SoundState& state, std::string_view path, bool /*held*/) {
	const std::optional<size_t> l = indexAfter(path, 3, '4');
	if (!l) {
		return std::nullopt;
	}
	const std::string_view rest = path.substr(5);
	if (rest == "type") {
		return word(to_string(state.lfo_wave_type[*l]));
	}
	if (rest == "syncLevel") {
		return number(engine::index_of(state.lfo_sync_level[*l]));
	}
	if (rest == "syncType") {
		return number(std::to_underlying(state.lfo_sync_type[*l]));
	}
	return std::nullopt;
}

Answer modulatorValue(const SoundState& state, std::string_view path, bool held) {
	const std::optional<size_t> m = indexAfter(path, 9, '2');
	if (!m || (!held && state.synth_mode != SynthMode::FM)) {
		return std::nullopt; // only FM writes its modulators
	}
	const std::string_view rest = path.substr(11);
	if (rest == "transpose") {
		return number(state.modulator_transpose[*m]);
	}
	if (rest == "cents") {
		return number(state.modulator_cents[*m]);
	}
	if (rest == "retrigPhase") {
		return phase(state.modulator_retrigger_phase[*m]);
	}
	if (rest == "toModulator1" && *m == 1) {
		return flag(state.modulator1_to_modulator0);
	}
	return std::nullopt;
}

Answer unisonValue(const SoundState& state, std::string_view path, bool /*held*/) {
	const std::string_view rest = path.substr(7);
	if (rest == "num") {
		return number(state.num_unison);
	}
	if (rest == "detune") {
		return number(state.unison_detune);
	}
	if (rest == "spread") {
		return number(state.unison_stereo_spread);
	}
	return std::nullopt;
}

Answer midiOutputValue(const SoundState& state, std::string_view path, bool /*held*/) {
	const std::string_view rest = path.substr(11);
	if (rest == "channel") {
		return number(state.output_midi_channel);
	}
	if (rest == "noteForDrum") {
		return number(state.output_midi_note_for_drum);
	}
	return std::nullopt;
}

/// Each family of setting paths, by its prefix, and how a state answers one; the empty prefix takes the rest.
struct Family {
	std::string_view prefix;
	Answer (*value)(const SoundState&, std::string_view, bool held);
};
constexpr std::array kFamilies{
    Family{"osc", oscillatorValue},         Family{"lfo", lfoValue},
    Family{"modulator", modulatorValue},    Family{"unison/", unisonValue},
    Family{"midiOutput/", midiOutputValue}, Family{"", topLevelValue},
};

/// The instrument's rows, then its effects'.
constexpr auto kAllRows = [] {
	std::array<engine::SettingInfo, kRows.size() + effects::kEffectSettings.size()> all{};
	std::ranges::copy(kRows, all.begin());
	std::ranges::copy(effects::kEffectSettings, all.begin() + kRows.size());
	return all;
}();
static_assert(engine::conditions_are_valid(kAllRows));

} // namespace menu_tables
} // namespace

const std::span<const engine::SettingInfo> kSettings{kAllRows};
const std::span<const engine::SettingInfo> kClipScopedSettings{engine::kArpSettingRows};

namespace {

/// @p path's value in @p state, an unset one included, or nothing where the path does not apply (with @p held, where
/// the state holds none).
Answer anyValue(const SoundState& state, std::string_view path, bool held = false) {
	const SettingInfo* row = engine::find_setting(kSettings, path);
	if (row == nullptr || row->kind == STRUCTURED) {
		return std::nullopt;
	}
	if (engine::find_setting(effects::kEffectSettings, path) != nullptr) {
		return effects::effect_setting_value(state.effects, path);
	}
	for (const Family& family : kFamilies) {
		if (path.starts_with(family.prefix)) {
			return family.value(state, path, held);
		}
	}
	return std::nullopt;
}

} // namespace

std::optional<serialization::Value> canonical_setting(std::string_view path, const serialization::Value& value) {
	if (!check_setting(path, value)) {
		return std::nullopt;
	}
	if (engine::find_setting(kSettings, path)->kind == INTEGER) {
		return serialization::Value::makeNumber(*engine::whole_number(*serialization::text_of(&value)));
	}
	return value;
}

std::optional<serialization::Value> setting_value(const SoundState& state, std::string_view path) {
	Answer value = anyValue(state, path);
	if (value && path == "midiOutput/noteForDrum" && value->str() == "255") {
		return std::nullopt; // unset: the menu shows its default there and writes only a note
	}
	return value;
}

std::optional<int32_t> held_setting(const SoundState& state, std::string_view path) {
	const SettingInfo* row = engine::find_setting(kSettings, path);
	const Answer value = anyValue(state, path, true);
	if (!value) {
		return std::nullopt;
	}
	if (row->kind == CHOICE) {
		const auto found = std::ranges::find(row->choices, value->str());
		return found != row->choices.end() ? std::optional<int32_t>{static_cast<int32_t>(found - row->choices.begin())}
		                                   : std::nullopt;
	}
	const std::optional<int64_t> n = engine::whole_number(value->str());
	return n ? std::optional<int32_t>{static_cast<int32_t>(*n)} : std::nullopt;
}

bool allows_setting(const SoundState& state, std::string_view path, const serialization::Value& value) {
	const auto ringable = [](OscType type) { return type <= OscType::WAVETABLE; };
	if (path == "mode" && parse_synth_mode(value.str()) == SynthMode::RINGMOD) {
		return std::ranges::all_of(state.sources, ringable, &SourceState::osc_type);
	}
	if (state.synth_mode == SynthMode::RINGMOD && path.starts_with("osc") && path.ends_with("/type")) {
		return ringable(parse_osc_type(value.str()));
	}
	return true;
}

bool takes_setting(const SoundState& state, std::string_view path) {
	return anyValue(state, path).has_value();
}

bool check_setting(std::string_view path, const serialization::Value& value) {
	return engine::check_setting(kSettings, path, value);
}

} // namespace deluge::instrument
