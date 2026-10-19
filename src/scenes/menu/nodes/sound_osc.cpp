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
/// @brief The oscillator subtree's `get`/`set` hooks, the `kPending` placeholder's two members, and
///        the exported root span. The `Node` literals are in `sound_osc.h`.

#include "scenes/menu/nodes/sound_osc.h"

#include "scenes/l10n_bridge.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h" // MenuState, for the placeholder `CustomItem`
#include "scenes/profile.h"

#include "shell/scene.h"
#include "ui/oled/frame.h"
#include "ui/oled/text.h"

#include "definitions_cxx.hpp"
#include "engines/deluge/words.h"
#include "model/clip/audio_clip.h"
#include "model/edits/setting_edit.h"
#include "model/project_bridge/source_bridge.h"
#include "model/sample/sample_controls.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "serialization/value.h"
#include "storage/multi_range/multisample_range.h"

#include <string>
#include <vector>

namespace deluge::scenes::menu::nodes {

using deluge::l10n::String;

// ---------------------------------------------------------------------------------------------
// The placeholder for the four bespoke items. See `sound_osc.h`'s note.

void PendingCustom::render(deluge::ui::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	// Deliberately the same "honest placeholder" `MenuScene`'s own unimplemented-kind arm draws
	// (`menu_scene.cpp:872-881`): the user sees which item they are on rather than the previous
	// screen. Replaced, per item, when E2 lands the browser, the recorder and the marker editor.
	const Node* node = state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
	deluge::ui::shell::TextScratch scratch;
	deluge::ui::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch)
	                                           : std::string_view{});
}

deluge::ui::shell::Result PendingCustom::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

namespace {

// ---------------------------------------------------------------------------------------------
// Shared accessors.

Source* source_of(const MenuContext& ctx, uint8_t index) {
	return ctx.sound != nullptr ? &ctx.sound->sources[index] : nullptr;
}

/// @brief The audio clip the context edits, if it is one.
AudioClip* audio_clip_of(const MenuContext& ctx) {
	return ctx.is_audio_clip ? static_cast<AudioClip*>(ctx.clip) : nullptr;
}

/// @brief `getCurrentSampleControls` (`gui/menu_item/sample/utils.cpp`): an audio clip's own
///        controls, else the source's.
SampleControls* sample_controls_of(const MenuContext& ctx, uint8_t index) {
	if (ctx.is_audio_clip) {
		return ctx.sample_controls;
	}
	Source* source = source_of(ctx, index);
	return source != nullptr ? &source->sampleControls : nullptr;
}

// ---------------------------------------------------------------------------------------------
// `osc::Type` (`gui/menu_item/osc/type.h:34-160`).

// ---------------------------------------------------------------------------------------------
// `sample::Transpose` (`gui/menu_item/sample/transpose.h:31-175`) -- "this is actually the
// Oscillator transpose" (`sample/transpose.h:31`).

/// @brief `computeCurrentValueForTranspose` (`gui/menu_item/value_scaling.cpp:64-66`).
constexpr int32_t transpose_to_menu(int32_t transpose, int32_t cents) {
	return transpose * 100 + cents;
}

/// @brief `computeFinalValuesForTranspose` (`value_scaling.cpp:68-73`).
constexpr void menu_to_transpose(int32_t value, int32_t* transpose, int32_t* cents) {
	const int32_t current = value + 25600;
	const int32_t semitones = (current + 50) / 100;
	*cents = current - semitones * 100;
	*transpose = semitones - 256;
}

/// @brief Which `MultisampleRange` a source's transpose lives in, or null for the source's own
///        `transpose`/`cents` (`sample/transpose.h:38-56`).
MultisampleRange* transpose_range(const MenuContext& ctx, uint8_t index) {
	Source* source = source_of(ctx, index);
	if (source == nullptr || source->ranges.empty() || ctx.sound->getSynthMode() == ::SynthMode::FM
	    || source->oscType != OscType::SAMPLE) {
		return nullptr;
	}
	// `:44-47`: the selected range when the menu is on this very source, else range 0.
	if (ctx.source_index == index && ctx.range != nullptr) {
		return static_cast<MultisampleRange*>(ctx.range);
	}
	return static_cast<MultisampleRange*>(source->ranges.getElement(0));
}

int32_t get_osc_transpose(const MenuContext& ctx, uint8_t index) {
	Source* source = source_of(ctx, index);
	if (source == nullptr) {
		return 0;
	}
	if (MultisampleRange* range = transpose_range(ctx, index); range != nullptr) {
		return transpose_to_menu(range->sampleHolder.transpose, range->sampleHolder.cents);
	}
	return transpose_to_menu(source->transpose, source->cents);
}

void set_osc_transpose(const MenuContext& ctx, int32_t value, uint8_t index) {
	// `sample::Transpose::writeCurrentValue` (`sample/transpose.h`): a sample source's transpose is
	// its range's -- the selected one, or range 0 in the kit sweep -- written as the source's ranges;
	// any other source's is its own setting. Legacy's affect-entire test here is
	// `soundEditor.editingKit()`, not the `editingKitRow()` every other item uses;
	// `bindings::writes_whole_kit` is the latter, so on a kit-global-FX context this writes one
	// target where legacy swept the kit. Unreachable in practice (the oscillator menus are not
	// reachable from the kit-global-FX root).
	bindings::apply_to_targets(
	    ctx, value, index, [](const bindings::SoundTarget& t, int32_t v, int32_t index, const MenuContext& ctx) {
		    if (t.sound == nullptr) {
			    return;
		    }
		    int32_t transpose = 0;
		    int32_t cents = 0;
		    menu_to_transpose(v, &transpose, &cents);
		    Source& source = t.sound->sources[index];
		    if (!source.ranges.empty() && t.sound->getSynthMode() != ::SynthMode::FM
		        && source.oscType == OscType::SAMPLE) {
			    const size_t r = !t.whole_kit && ctx.source_index == index && ctx.range != nullptr
			                         ? static_cast<size_t>(ctx.range_index)
			                         : 0;
			    std::vector<deluge::instrument::RangeState> ranges = deluge::bridge::rangesOf(source);
			    ranges[r].transpose = static_cast<int16_t>(transpose);
			    ranges[r].cents = static_cast<int8_t>(cents);
			    deluge::edits::rangesFor(*t.sound, index, std::move(ranges));
		    }
		    else {
			    deluge::edits::settingFor(*t.sound, deluge::edits::oscSettingPath(index, "transpose"),
			                              deluge::serialization::Value::makeNumber(transpose));
			    deluge::edits::settingFor(*t.sound, deluge::edits::oscSettingPath(index, "cents"),
			                              deluge::serialization::Value::makeNumber(cents));
		    }
	    });
}

// ---------------------------------------------------------------------------------------------
// `osc::RetriggerPhase` (`gui/menu_item/osc/retrigger_phase.h:25-135`).

// ---------------------------------------------------------------------------------------------
// The sample items.

int32_t get_pitch_speed(const MenuContext& ctx, uint8_t index) {
	const SampleControls* controls = sample_controls_of(ctx, index);
	return controls != nullptr && controls->pitchAndSpeedAreIndependent ? 1 : 0; // `sample/pitch_speed.h:39-42`
}
void set_pitch_speed(const MenuContext& ctx, int32_t value, uint8_t index) {
	// `sample/pitch_speed.h`. The single-target arm is `setCurrentPitchAndSpeedIndependent`
	// (`sample/utils.cpp`): an audio clip's own setter, else a setting Edit on the sound.
	if (!bindings::writes_whole_kit(ctx)) {
		if (AudioClip* clip = audio_clip_of(ctx)) {
			deluge::edits::audioClipSettingFor(*clip, "pitchSpeedIndependent",
			                                   deluge::serialization::Value::makeNumber(value != 0 ? 1 : 0));
		}
		else if (ctx.sound != nullptr) {
			deluge::edits::settingFor(*ctx.sound, deluge::edits::oscSettingPath(index, "timeStretchEnable"),
			                          deluge::serialization::Value::makeNumber(value != 0 ? 1 : 0));
		}
		return;
	}
	bindings::apply_to_targets(
	    ctx, value, index, [](const bindings::SoundTarget& t, int32_t v, int32_t index, const MenuContext&) {
		    if (t.sound != nullptr) {
			    deluge::edits::settingFor(*t.sound, deluge::edits::oscSettingPath(index, "timeStretchEnable"),
			                              deluge::serialization::Value::makeNumber(v));
		    }
	    });
}

int32_t get_interpolation(const MenuContext& ctx, uint8_t index) {
	const SampleControls* controls = sample_controls_of(ctx, index);
	// `sample/interpolation.h:32-35`
	return controls != nullptr ? static_cast<int32_t>(controls->interpolationMode) : 0;
}
void set_interpolation(const MenuContext& ctx, int32_t value, uint8_t index) {
	// `sample/interpolation.h`, same shape as pitch/speed above (`setCurrentInterpolationMode`).
	const bool linear = static_cast<InterpolationMode>(value) == InterpolationMode::LINEAR;
	if (!bindings::writes_whole_kit(ctx)) {
		if (AudioClip* clip = audio_clip_of(ctx)) {
			deluge::edits::audioClipSettingFor(*clip, "linearInterpolation",
			                                   deluge::serialization::Value::makeNumber(linear ? 1 : 0));
		}
		else if (ctx.sound != nullptr) {
			deluge::edits::settingFor(*ctx.sound, deluge::edits::oscSettingPath(index, "linearInterpolation"),
			                          deluge::serialization::Value::makeNumber(linear ? 1 : 0));
		}
		return;
	}
	bindings::apply_to_targets(
	    ctx, linear ? 1 : 0, index, [](const bindings::SoundTarget& t, int32_t v, int32_t index, const MenuContext&) {
		    if (t.sound != nullptr) {
			    deluge::edits::settingFor(*t.sound, deluge::edits::oscSettingPath(index, "linearInterpolation"),
			                              deluge::serialization::Value::makeNumber(v));
		    }
	    });
}

// ---------------------------------------------------------------------------------------------
// The FM modulator items.

constexpr const Node* kRoots[] = {&recorder, &source0, &source1, &modulator0, &modulator1, &osc_mixer};

} // namespace

// ---------------------------------------------------------------------------------------------
// The hooks `sound_osc.h`'s macros declared, each forwarding its baked-in source index.

#define DELUGE_OSC_SOURCE_HOOKS(i)                                                                                     \
	int32_t get_source##i##_transpose(const MenuContext& ctx) {                                                        \
		return get_osc_transpose(ctx, (i));                                                                            \
	}                                                                                                                  \
	void set_source##i##_transpose(const MenuContext& ctx, int32_t v) {                                                \
		set_osc_transpose(ctx, v, (i));                                                                                \
	}                                                                                                                  \
	int32_t get_sample##i##_pitch_speed(const MenuContext& ctx) {                                                      \
		return get_pitch_speed(ctx, (i));                                                                              \
	}                                                                                                                  \
	void set_sample##i##_pitch_speed(const MenuContext& ctx, int32_t v) {                                              \
		set_pitch_speed(ctx, v, (i));                                                                                  \
	}                                                                                                                  \
	int32_t get_sample##i##_interp(const MenuContext& ctx) {                                                           \
		return get_interpolation(ctx, (i));                                                                            \
	}                                                                                                                  \
	void set_sample##i##_interp(const MenuContext& ctx, int32_t v) {                                                   \
		set_interpolation(ctx, v, (i));                                                                                \
	}

DELUGE_OSC_SOURCE_HOOKS(0)
DELUGE_OSC_SOURCE_HOOKS(1)

#undef DELUGE_OSC_SOURCE_HOOKS

const std::span<const Node* const> kSoundOscChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
