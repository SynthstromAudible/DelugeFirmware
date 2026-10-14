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

/// @brief `Type::mayUseDx()` (`osc/type.h:40`): `!soundEditor.editingKit() && sourceId_ == 0`.
///        `editingKit()` is `getCurrentOutputType() == KIT` (`gui/ui/sound_editor.cpp:203-205`),
///        which `MenuContext` carries as `is_kit`.
bool may_use_dx(const MenuContext& ctx, uint8_t index) {
	return !ctx.is_kit && index == 0;
}

/// @brief `Type::readCurrentValue` (`osc/type.h:42-48`): the raw enumerator, with DX7 squeezed out
///        of the list where it is not offered.
int32_t osc_type_value(const MenuContext& ctx, uint8_t index) {
	Source* source = source_of(ctx, index);
	if (source == nullptr) {
		return 0;
	}
	auto raw = static_cast<int32_t>(source->oscType);
	if (!may_use_dx(ctx, index) && raw > static_cast<int32_t>(OscType::DX7)) {
		raw -= 1;
	}
	return raw;
}

/// @brief `Type::writeCurrentValue` (`osc/type.h`): a setting Edit on the sound. No affect-entire
///        loop: `osc::Type` overrides neither `usesAffectEntire()` nor the loop.
void set_osc_type(const MenuContext& ctx, int32_t value, uint8_t index) {
	if (ctx.sound == nullptr) {
		return;
	}
	auto new_value = static_cast<OscType>(value);
	if (!may_use_dx(ctx, index) && static_cast<int32_t>(new_value) >= static_cast<int32_t>(OscType::DX7)) {
		new_value = static_cast<OscType>(static_cast<int32_t>(new_value) + 1);
	}
	deluge::edits::settingFor(
	    *ctx.sound, deluge::edits::oscSettingPath(index, "type"),
	    deluge::serialization::Value::makeScalar(std::string(deluge::instrument::to_string(new_value))));
}

/// @brief `Type::getOptions` (`osc/type.h:72-104`) -- a genuinely dynamic list (ring-mod drops
///        every sample/input entry, DX7 appears only on a non-kit's oscillator 1, and the three
///        input entries collapse to one when nothing is plugged in), so a `dynamic_names` hook
///        rather than a static `names` span.
size_t osc_type_names(const MenuContext& ctx, std::span<std::string_view> out, uint8_t index) {
	static constexpr String kBase[] = {
	    String::STRING_FOR_SINE,          String::STRING_FOR_TRIANGLE, String::STRING_FOR_SQUARE,
	    String::STRING_FOR_ANALOG_SQUARE, String::STRING_FOR_SAW,      String::STRING_FOR_ANALOG_SAW,
	    String::STRING_FOR_WAVETABLE,
	};
	size_t n = 0;
	const auto emit = [&](String s) {
		if (n < out.size()) {
			out[n++] = deluge::scenes::text_lookup(sid(s));
		}
	};
	for (String s : kBase) {
		emit(s);
	}
	if (ctx.sound == nullptr || ctx.sound->getSynthMode() == ::SynthMode::RINGMOD) {
		return n; // `:86-88`
	}
	emit(String::STRING_FOR_SAMPLE);
	if (may_use_dx(ctx, index)) {
		emit(String::STRING_FOR_DX7);
	}
	if (AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn) { // `:96-103`
		emit(String::STRING_FOR_INPUT_LEFT);
		emit(String::STRING_FOR_INPUT_RIGHT);
		emit(String::STRING_FOR_INPUT_STEREO);
	}
	else {
		emit(String::STRING_FOR_INPUT);
	}
	return n;
}

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
		    if (t.stack != nullptr) {
			    t.sound->recalculateAllVoicePhaseIncrements(t.stack);
		    }
	    });
}

// ---------------------------------------------------------------------------------------------
// `osc::RetriggerPhase` (`gui/menu_item/osc/retrigger_phase.h:25-135`).

/// @brief `readCurrentValue` (`retrigger_phase.h:39-47`): the 32-bit phase as degrees, or -1
///        ("OFF") for the `0xFFFFFFFF` sentinel.
int32_t get_retrigger_phase(const MenuContext& ctx, uint8_t index, bool for_modulator) {
	if (ctx.sound == nullptr) {
		return -1;
	}
	const auto& snapshot = ctx.sound->snapshot();
	const uint32_t value = for_modulator ? snapshot.modulatorRetriggerPhase[index] : snapshot.oscRetriggerPhase[index];
	return value == 0xFFFFFFFF ? -1 : static_cast<int32_t>(value / 11930464);
}

/// @brief `writeCurrentValue` (`retrigger_phase.h`): a setting Edit on the sound. No affect-entire
///        loop in legacy either.
void set_retrigger_phase(const MenuContext& ctx, int32_t value, uint8_t index, bool for_modulator) {
	if (ctx.sound == nullptr) {
		return;
	}
	const uint32_t stored = value < 0 ? 0xFFFFFFFFu : static_cast<uint32_t>(value * 11930464);
	const std::string path =
	    std::string(for_modulator ? "modulator" : "osc") + std::to_string(index + 1) + "/retrigPhase";
	deluge::edits::settingFor(*ctx.sound, path, deluge::serialization::Value::makeNumber(static_cast<int32_t>(stored)));
}

// ---------------------------------------------------------------------------------------------
// The sample items.

int32_t get_sample_reverse(const MenuContext& ctx, uint8_t index) {
	Source* source = source_of(ctx, index);
	return source != nullptr && source->sampleControls.reversed ? 1 : 0; // `sample/reverse.h:40-43`
}
void set_sample_reverse(const MenuContext& ctx, int32_t value, uint8_t index) {
	// `sample/reverse.h`: a setting Edit on each target sound.
	bindings::apply_to_targets(
	    ctx, value, index, [](const bindings::SoundTarget& t, int32_t v, int32_t index, const MenuContext&) {
		    if (t.sound != nullptr) {
			    deluge::edits::settingFor(*t.sound, deluge::edits::oscSettingPath(index, "reversed"),
			                              deluge::serialization::Value::makeNumber(v));
		    }
	    });
}

int32_t get_sample_repeat(const MenuContext& ctx, uint8_t index) {
	Source* source = source_of(ctx, index);
	return source != nullptr ? static_cast<int32_t>(source->repeatMode) : 0; // `sample/repeat.h:44-47`
}
void set_sample_repeat(const MenuContext& ctx, int32_t value, uint8_t index) {
	// `sample/repeat.h`: a setting Edit on each target sound, minus two calls this layer cannot make:
	// the kit arpeggiator note-off on ONCE -- which reaches `instrumentClipView` through
	// `getCurrentInstrumentClip` -- and the `uiNeedsRendering(&instrumentClipView, ...)` at the end.
	// The notify design replaces the second with the `Dirty::Oled` `bindings::write` already raises
	// for a `get`/`set` node; the first belongs to E2's `NoteClipScene`. Legacy's affect-entire test
	// here is also looser than `bindings::writes_whole_kit`: `kit != nullptr && currentUIMode == …`
	// with no row test at all. Recorded, not silently dropped.
	bindings::apply_to_targets(
	    ctx, value, index, [](const bindings::SoundTarget& t, int32_t v, int32_t index, const MenuContext&) {
		    if (t.sound != nullptr) {
			    deluge::edits::settingFor(*t.sound, deluge::edits::oscSettingPath(index, "loopMode"),
			                              deluge::serialization::Value::makeNumber(v));
		    }
	    });
}

int32_t get_time_stretch(const MenuContext& ctx, uint8_t index) {
	Source* source = source_of(ctx, index);
	return source != nullptr ? source->timeStretchAmount : 0; // `sample/time_stretch.h:40-43`
}
void set_time_stretch(const MenuContext& ctx, int32_t value, uint8_t index) {
	// `sample/time_stretch.h`: a setting Edit on each target sound.
	bindings::apply_to_targets(
	    ctx, value, index, [](const bindings::SoundTarget& t, int32_t v, int32_t index, const MenuContext&) {
		    if (t.sound != nullptr) {
			    deluge::edits::settingFor(*t.sound, deluge::edits::oscSettingPath(index, "timeStretchAmount"),
			                              deluge::serialization::Value::makeNumber(v));
		    }
	    });
}

int32_t get_pitch_speed(const MenuContext& ctx, uint8_t index) {
	const SampleControls* controls = sample_controls_of(ctx, index);
	return controls != nullptr && controls->pitchAndSpeedAreIndependent ? 1 : 0; // `sample/pitch_speed.h:39-42`
}
void set_pitch_speed(const MenuContext& ctx, int32_t value, uint8_t index) {
	// `sample/pitch_speed.h`. The single-target arm is `setCurrentPitchAndSpeedIndependent`
	// (`sample/utils.cpp`): an audio clip's own setter, else a setting Edit on the sound.
	if (!bindings::writes_whole_kit(ctx)) {
		if (AudioClip* clip = audio_clip_of(ctx)) {
			clip->setPitchAndSpeedIndependent(value != 0);
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
			clip->setInterpolationMode(static_cast<InterpolationMode>(value));
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

int32_t get_modulator_transpose(const MenuContext& ctx, uint8_t index) {
	if (ctx.sound == nullptr) {
		return 0;
	}
	// `modulator/transpose.h`'s `readCurrentValue`.
	const auto& snapshot = ctx.sound->snapshot();
	return transpose_to_menu(snapshot.modulatorTranspose[index], snapshot.modulatorCents[index]);
}
void set_modulator_transpose(const MenuContext& ctx, int32_t value, uint8_t index) {
	bindings::apply_to_targets(
	    ctx, value, index, [](const bindings::SoundTarget& t, int32_t v, int32_t index, const MenuContext&) {
		    // `modulator/transpose.h`: the kit arm skips a drum that is not in FM mode, the single arm
		    // does not test at all.
		    if (t.sound == nullptr || (t.whole_kit && t.sound->getSynthMode() != ::SynthMode::FM)) {
			    return;
		    }
		    int32_t transpose = 0;
		    int32_t cents = 0;
		    menu_to_transpose(v, &transpose, &cents);
		    const std::string prefix = "modulator" + std::to_string(index + 1) + "/";
		    deluge::edits::settingFor(*t.sound, prefix + "transpose",
		                              deluge::serialization::Value::makeNumber(transpose));
		    deluge::edits::settingFor(*t.sound, prefix + "cents", deluge::serialization::Value::makeNumber(cents));
	    });
}

constexpr const Node* kRoots[] = {&recorder, &source0, &source1, &modulator0, &modulator1, &osc_mixer};

} // namespace

// ---------------------------------------------------------------------------------------------
// The hooks `sound_osc.h`'s macros declared, each forwarding its baked-in source index.

#define DELUGE_OSC_SOURCE_HOOKS(i)                                                                                     \
	int32_t get_osc##i##_type(const MenuContext& ctx) {                                                                \
		return osc_type_value(ctx, (i));                                                                               \
	}                                                                                                                  \
	void set_osc##i##_type(const MenuContext& ctx, int32_t v) {                                                        \
		set_osc_type(ctx, v, (i));                                                                                     \
	}                                                                                                                  \
	size_t osc##i##_type_names(const MenuContext& ctx, std::span<std::string_view> out) {                              \
		return osc_type_names(ctx, out, (i));                                                                          \
	}                                                                                                                  \
	int32_t get_source##i##_transpose(const MenuContext& ctx) {                                                        \
		return get_osc_transpose(ctx, (i));                                                                            \
	}                                                                                                                  \
	void set_source##i##_transpose(const MenuContext& ctx, int32_t v) {                                                \
		set_osc_transpose(ctx, v, (i));                                                                                \
	}                                                                                                                  \
	int32_t get_osc##i##_phase(const MenuContext& ctx) {                                                               \
		return get_retrigger_phase(ctx, (i), false);                                                                   \
	}                                                                                                                  \
	void set_osc##i##_phase(const MenuContext& ctx, int32_t v) {                                                       \
		set_retrigger_phase(ctx, v, (i), false);                                                                       \
	}                                                                                                                  \
	int32_t get_sample##i##_reverse(const MenuContext& ctx) {                                                          \
		return get_sample_reverse(ctx, (i));                                                                           \
	}                                                                                                                  \
	void set_sample##i##_reverse(const MenuContext& ctx, int32_t v) {                                                  \
		set_sample_reverse(ctx, v, (i));                                                                               \
	}                                                                                                                  \
	int32_t get_sample##i##_repeat(const MenuContext& ctx) {                                                           \
		return get_sample_repeat(ctx, (i));                                                                            \
	}                                                                                                                  \
	void set_sample##i##_repeat(const MenuContext& ctx, int32_t v) {                                                   \
		set_sample_repeat(ctx, v, (i));                                                                                \
	}                                                                                                                  \
	int32_t get_sample##i##_speed(const MenuContext& ctx) {                                                            \
		return get_time_stretch(ctx, (i));                                                                             \
	}                                                                                                                  \
	void set_sample##i##_speed(const MenuContext& ctx, int32_t v) {                                                    \
		set_time_stretch(ctx, v, (i));                                                                                 \
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

#define DELUGE_OSC_MODULATOR_HOOKS(i)                                                                                  \
	int32_t get_modulator##i##_transpose_v(const MenuContext& ctx) {                                                   \
		return get_modulator_transpose(ctx, (i));                                                                      \
	}                                                                                                                  \
	void set_modulator##i##_transpose_v(const MenuContext& ctx, int32_t v) {                                           \
		set_modulator_transpose(ctx, v, (i));                                                                          \
	}                                                                                                                  \
	int32_t get_modulator##i##_phase(const MenuContext& ctx) {                                                         \
		return get_retrigger_phase(ctx, (i), true);                                                                    \
	}                                                                                                                  \
	void set_modulator##i##_phase(const MenuContext& ctx, int32_t v) {                                                 \
		set_retrigger_phase(ctx, v, (i), true);                                                                        \
	}

DELUGE_OSC_SOURCE_HOOKS(0)
DELUGE_OSC_SOURCE_HOOKS(1)
DELUGE_OSC_MODULATOR_HOOKS(0)
DELUGE_OSC_MODULATOR_HOOKS(1)

#undef DELUGE_OSC_SOURCE_HOOKS
#undef DELUGE_OSC_MODULATOR_HOOKS

int32_t get_modulator_dest(const MenuContext& ctx) {
	return ctx.sound != nullptr && ctx.sound->snapshot().modulator1ToModulator0 ? 1 : 0; // `modulator/destination.h`
}
void set_modulator_dest(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(
	    ctx, value, 0, [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		    // `modulator/destination.h`, kit arm filtered on FM.
		    if (t.sound == nullptr || (t.whole_kit && t.sound->getSynthMode() != ::SynthMode::FM)) {
			    return;
		    }
		    deluge::edits::settingFor(*t.sound, "modulator2/toModulator1",
		                              deluge::serialization::Value::makeNumber(v != 0 ? 1 : 0));
	    });
}

int32_t get_osc_sync(const MenuContext& ctx) {
	return ctx.sound != nullptr && ctx.sound->snapshot().oscillatorSync ? 1 : 0; // `osc/sync.h`
}
void set_osc_sync(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(
	    ctx, value, 0, [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		    // `osc/sync.h`: the kit arm re-applies `isRelevant`'s own test per drum, the single arm
		    // assigns unconditionally.
		    if (t.sound == nullptr) {
			    return;
		    }
		    if (t.whole_kit
		        && (t.sound->getSynthMode() == ::SynthMode::FM || t.sound->sources[0].oscType == OscType::SAMPLE
		            || t.sound->sources[1].oscType == OscType::SAMPLE)) {
			    return;
		    }
		    deluge::edits::settingFor(*t.sound, "osc2/oscillatorSync",
		                              deluge::serialization::Value::makeNumber(v != 0 ? 1 : 0));
	    });
}

const std::span<const Node* const> kSoundOscChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
