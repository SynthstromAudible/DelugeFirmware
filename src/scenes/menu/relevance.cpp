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

/// @file
/// @brief The batch-1 relevance predicates. See `relevance.h` for the interface and for why the
///        per-source ones come in twos.
///
/// Every function here is a transcription of the legacy `isRelevant` override cited on its
/// declaration -- `src/scenes` may not include `gui/menu_item/`, so the tests cross-check the two
/// (`sim/scenes_spec/tables_1_spec.cpp` calls the legacy overrides, which are public
/// (`gui/menu_item/menu_item.h:265`), against these).
///
/// **A null `sound` is never relevant.** Legacy's overrides all begin by
/// `static_cast<Sound*>(modControllable)` and dereference it: they are only ever reached from a
/// root whose `currentModControllable` IS a `Sound`. A `MenuContext` can carry a null `sound` (a
/// MIDI clip, the settings root), so each predicate below tests it first and answers false --
/// "this item does not apply here", which is exactly what relevance means.

#include "scenes/menu/relevance.h"

#include "scenes/menu/context.h"

#include "definitions_cxx.hpp"
#include "model/mod_controllable/filters/filter_config.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "processing/sound/sound.h"
#include "processing/source.h"

#include <array>

namespace deluge::scenes::menu::relevance {

namespace {

/// @brief Source @p index of the context's sound, or null when there is no sound.
const Source* source_of(const MenuContext& ctx, uint8_t index) {
	return ctx.sound != nullptr ? &ctx.sound->sources[index] : nullptr;
}

/// @brief `Source::hasAtLeastOneAudioFileLoaded()` on a source that may not exist.
bool loaded(const Source* source) {
	// `hasAtLeastOneAudioFileLoaded` is non-const in the model; the const_cast keeps the predicate
	// signature honest about not mutating anything.
	return source != nullptr && const_cast<Source*>(source)->hasAtLeastOneAudioFileLoaded();
}

/// @brief `FilterInfo::isOn()` (`gui/menu_item/filter/info.h:91`) for whichever slot.
bool filter_on(const MenuContext& ctx, bool high_pass) {
	if (ctx.mod == nullptr) {
		return false;
	}
	return (high_pass ? ctx.mod->hpfMode : ctx.mod->lpfMode) != ::FilterMode::OFF;
}

/// @brief `isSampleModeSample` (`gui/menu_item/sample/utils.cpp:9-15`).
bool is_sample_mode_sample(const MenuContext& ctx, uint8_t index) {
	const Source* source = source_of(ctx, index);
	return ctx.sound != nullptr && ctx.sound->getSynthMode() == ::SynthMode::SUBTRACTIVE && source != nullptr
	       && source->oscType == OscType::SAMPLE && loaded(source);
}

/// @brief `FileSelector::isRelevant` (`gui/menu_item/file_selector.cpp:54-67`).
bool file_selector(const MenuContext& ctx, uint8_t index) {
	if (ctx.is_audio_clip) {
		return true; // `:55-57` -- `getCurrentClip()->type == ClipType::AUDIO`
	}
	const Source* source = source_of(ctx, index);
	if (ctx.sound == nullptr || source == nullptr) {
		return false;
	}
	if (source->oscType == OscType::WAVETABLE) {
		return ctx.sound->getSynthMode() != ::SynthMode::FM; // `:62-64`
	}
	return ctx.sound->getSynthMode() == ::SynthMode::SUBTRACTIVE && source->oscType == OscType::SAMPLE; // `:66`
}

/// @brief `sample::Transpose::isRelevant` (`gui/menu_item/sample/transpose.h:139-145`).
bool transpose(const MenuContext& ctx, uint8_t index) {
	const Source* source = source_of(ctx, index);
	if (source == nullptr) {
		return false;
	}
	if (source->oscType == OscType::SAMPLE || source->oscType == OscType::WAVETABLE) {
		return loaded(source);
	}
	return true;
}

/// @brief `osc::source::WaveIndex::isRelevant` (`gui/menu_item/osc/source/wave_index.h:30-35`).
bool wave_index(const MenuContext& ctx, uint8_t index) {
	const Source* source = source_of(ctx, index);
	return ctx.sound != nullptr && source != nullptr && ctx.sound->getSynthMode() != ::SynthMode::FM
	       && source->oscType == OscType::WAVETABLE && loaded(source);
}

/// @brief `osc::PulseWidth::isRelevant` (`gui/menu_item/osc/pulse_width.h:39-52`).
bool pulse_width(const MenuContext& ctx, uint8_t index) {
	const Source* source = source_of(ctx, index);
	if (ctx.sound == nullptr || source == nullptr) {
		return false;
	}
	if (ctx.sound->getSynthMode() == ::SynthMode::FM) {
		return false;
	}
	if (source->oscType == OscType::WAVETABLE) {
		return loaded(source);
	}
	return source->oscType != OscType::SAMPLE && source->oscType != OscType::INPUT_L
	       && source->oscType != OscType::INPUT_R && source->oscType != OscType::INPUT_STEREO;
}

/// @brief `osc::source::Feedback::isRelevant` (`gui/menu_item/osc/source/feedback.h:31-34`).
bool carrier_feedback(const MenuContext& ctx, uint8_t index) {
	const Source* source = source_of(ctx, index);
	return ctx.sound != nullptr && source != nullptr
	       && (ctx.sound->getSynthMode() == ::SynthMode::FM || source->oscType == OscType::DX7);
}

/// @brief `osc::RetriggerPhase::isRelevant` (`gui/menu_item/osc/retrigger_phase.h:76-87`).
/// @param for_modulator The item's own `for_modulator_` flag (`retrigger_phase.h:131`).
bool retrigger_phase(const MenuContext& ctx, uint8_t index, bool for_modulator) {
	const Source* source = source_of(ctx, index);
	if (ctx.sound == nullptr || source == nullptr) {
		return false;
	}
	if (for_modulator && ctx.sound->getSynthMode() != ::SynthMode::FM) {
		return false;
	}
	if (source->oscType == OscType::WAVETABLE) {
		return loaded(source);
	}
	return source->oscType != OscType::SAMPLE || ctx.sound->getSynthMode() == ::SynthMode::FM;
}

/// @brief `sample::PitchSpeed::isRelevant` (`gui/menu_item/sample/pitch_speed.h:35-37`).
bool pitch_speed(const MenuContext& ctx, uint8_t index) {
	return ctx.is_audio_clip || is_sample_mode_sample(ctx, index);
}

/// @brief `sample::Interpolation::isRelevant` (`gui/menu_item/sample/interpolation.h:61-71`).
bool interpolation(const MenuContext& ctx, uint8_t index) {
	if (ctx.is_audio_clip) {
		return true;
	}
	const Source* source = source_of(ctx, index);
	if (ctx.sound == nullptr || source == nullptr) {
		return false;
	}
	return ctx.sound->getSynthMode() == ::SynthMode::SUBTRACTIVE
	       && ((source->oscType == OscType::SAMPLE && loaded(source)) || source->oscType == OscType::INPUT_L
	           || source->oscType == OscType::INPUT_R || source->oscType == OscType::INPUT_STEREO);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Sound-wide

bool has_lpf(const MenuContext& ctx) {
	return filter_on(ctx, /*high_pass=*/false);
}
bool has_hpf(const MenuContext& ctx) {
	return filter_on(ctx, /*high_pass=*/true);
}
bool is_fm(const MenuContext& ctx) {
	return ctx.sound != nullptr && ctx.sound->getSynthMode() == ::SynthMode::FM;
}
bool not_fm(const MenuContext& ctx) {
	return ctx.sound != nullptr && ctx.sound->getSynthMode() != ::SynthMode::FM;
}
bool not_ringmod(const MenuContext& ctx) {
	return ctx.sound != nullptr && ctx.sound->getSynthMode() != ::SynthMode::RINGMOD;
}
bool is_subtractive(const MenuContext& ctx) {
	return ctx.sound != nullptr && ctx.sound->getSynthMode() == ::SynthMode::SUBTRACTIVE;
}
bool osc_sync_allowed(const MenuContext& ctx) {
	return ctx.sound != nullptr && ctx.sound->getSynthMode() != ::SynthMode::FM
	       && ctx.sound->sources[0].oscType != OscType::SAMPLE && ctx.sound->sources[1].oscType != OscType::SAMPLE;
}

// ---------------------------------------------------------------------------------------------
// Per-source

bool osc0_file_selector(const MenuContext& ctx) {
	return file_selector(ctx, 0);
}
bool osc1_file_selector(const MenuContext& ctx) {
	return file_selector(ctx, 1);
}
bool osc0_transpose(const MenuContext& ctx) {
	return transpose(ctx, 0);
}
bool osc1_transpose(const MenuContext& ctx) {
	return transpose(ctx, 1);
}
bool osc0_wave_index(const MenuContext& ctx) {
	return wave_index(ctx, 0);
}
bool osc1_wave_index(const MenuContext& ctx) {
	return wave_index(ctx, 1);
}
bool osc0_pulse_width(const MenuContext& ctx) {
	return pulse_width(ctx, 0);
}
bool osc1_pulse_width(const MenuContext& ctx) {
	return pulse_width(ctx, 1);
}
bool osc0_carrier_feedback(const MenuContext& ctx) {
	return carrier_feedback(ctx, 0);
}
bool osc1_carrier_feedback(const MenuContext& ctx) {
	return carrier_feedback(ctx, 1);
}
bool osc0_retrigger_phase(const MenuContext& ctx) {
	return retrigger_phase(ctx, 0, /*for_modulator=*/false);
}
bool osc1_retrigger_phase(const MenuContext& ctx) {
	return retrigger_phase(ctx, 1, /*for_modulator=*/false);
}
bool modulator0_retrigger_phase(const MenuContext& ctx) {
	return retrigger_phase(ctx, 0, /*for_modulator=*/true);
}
bool modulator1_retrigger_phase(const MenuContext& ctx) {
	return retrigger_phase(ctx, 1, /*for_modulator=*/true);
}
bool osc0_is_sample(const MenuContext& ctx) {
	return is_sample_mode_sample(ctx, 0);
}
bool osc1_is_sample(const MenuContext& ctx) {
	return is_sample_mode_sample(ctx, 1);
}
bool osc0_pitch_speed(const MenuContext& ctx) {
	return pitch_speed(ctx, 0);
}
bool osc1_pitch_speed(const MenuContext& ctx) {
	return pitch_speed(ctx, 1);
}
bool osc0_interpolation(const MenuContext& ctx) {
	return interpolation(ctx, 0);
}
bool osc1_interpolation(const MenuContext& ctx) {
	return interpolation(ctx, 1);
}

// ---------------------------------------------------------------------------------------------
// The registry (Task 10's dump)
//
// **Adding a predicate to `relevance.h` means adding a row here.** Nothing can check that
// mechanically -- C++ has no way to enumerate a header's declarations -- so the guards are: the
// `static_assert` on the row count below, which forces a deliberate edit; `name_of` answering "?"
// for an unregistered predicate; and `tables_1_spec`, which asserts every predicate the tables
// actually use resolves to a real name and that the count is what this file says.

namespace {

#define DELUGE_RELEVANCE_ROW(fn)                                                                                       \
	NamedPredicate {                                                                                                   \
		&fn, #fn                                                                                                       \
	}

constexpr std::array<NamedPredicate, 27> kRegistry{
    DELUGE_RELEVANCE_ROW(has_lpf),
    DELUGE_RELEVANCE_ROW(has_hpf),
    DELUGE_RELEVANCE_ROW(is_fm),
    DELUGE_RELEVANCE_ROW(not_fm),
    DELUGE_RELEVANCE_ROW(not_ringmod),
    DELUGE_RELEVANCE_ROW(is_subtractive),
    DELUGE_RELEVANCE_ROW(osc_sync_allowed),
    DELUGE_RELEVANCE_ROW(osc0_file_selector),
    DELUGE_RELEVANCE_ROW(osc1_file_selector),
    DELUGE_RELEVANCE_ROW(osc0_transpose),
    DELUGE_RELEVANCE_ROW(osc1_transpose),
    DELUGE_RELEVANCE_ROW(osc0_wave_index),
    DELUGE_RELEVANCE_ROW(osc1_wave_index),
    DELUGE_RELEVANCE_ROW(osc0_pulse_width),
    DELUGE_RELEVANCE_ROW(osc1_pulse_width),
    DELUGE_RELEVANCE_ROW(osc0_carrier_feedback),
    DELUGE_RELEVANCE_ROW(osc1_carrier_feedback),
    DELUGE_RELEVANCE_ROW(osc0_retrigger_phase),
    DELUGE_RELEVANCE_ROW(osc1_retrigger_phase),
    DELUGE_RELEVANCE_ROW(modulator0_retrigger_phase),
    DELUGE_RELEVANCE_ROW(modulator1_retrigger_phase),
    DELUGE_RELEVANCE_ROW(osc0_is_sample),
    DELUGE_RELEVANCE_ROW(osc1_is_sample),
    DELUGE_RELEVANCE_ROW(osc0_pitch_speed),
    DELUGE_RELEVANCE_ROW(osc1_pitch_speed),
    DELUGE_RELEVANCE_ROW(osc0_interpolation),
    DELUGE_RELEVANCE_ROW(osc1_interpolation),
};

#undef DELUGE_RELEVANCE_ROW

// One row per predicate declared in `relevance.h`: seven sound-wide and ten per-source pairs.
static_assert(kRegistry.size() == 27);

} // namespace

std::span<const NamedPredicate> registry() {
	return kRegistry;
}

std::string_view name_of(RelevantFn fn) {
	if (fn == nullptr) {
		return "always";
	}
	for (const NamedPredicate& row : kRegistry) {
		if (row.fn == fn) {
			return row.name;
		}
	}
	return "?";
}

} // namespace deluge::scenes::menu::relevance
