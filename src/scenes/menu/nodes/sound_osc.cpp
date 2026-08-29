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
/// @brief The oscillator subtree: the sample recorder, both sources, both FM modulators, the mixer.
///
/// Six of the seventeen batch-1 roots, from
/// `gui/menu_item/generate/dmenus/oscillator/{__init__,source,modulator,recorder}.py` and
/// `generate/g_menus.inc:55-158`. The largest of the six files, and the only one with bespoke
/// (`Kind::Custom`) items.
///
/// | node | legacy global (`g_menus.inc`) | legacy class |
/// |---|---|---|
/// | `sample<i>_recorder` | `sample<i>RecorderMenu` (`:55-56`) | `osc::AudioRecorder` (`osc/audio_recorder.h:28`) |
/// | `recorder` | `recorderMenu` (`:61`) | `HorizontalMenu` |
/// | `osc<i>_type` | `osc<i>TypeMenu` (`:62`, `:93`) | `osc::Type` (`osc/type.h:34`) |
/// | `file<i>_selector` | `file<i>SelectorMenu` (`:63`, `:94`) | `FileSelector` (`file_selector.h:25`) |
/// | `source<i>_transpose` | `source<i>TransposeMenu` (`:64`, `:95`) | `sample::Transpose` (`sample/transpose.h:31`) |
/// | `source<i>_wave_index` | `source<i>WaveIndexMenu` (`:65`, `:96`) | `osc::source::WaveIndex`
/// (`osc/source/wave_index.h:23`) | | `osc<i>_pulse_width` | `osc<i>PulseWidthMenu` (`:66`, `:97`) | `osc::PulseWidth`
/// (`osc/pulse_width.h:25`) | | `source<i>_feedback` | `source<i>FeedbackMenu` (`:67`, `:98`) | `osc::source::Feedback`
/// (`osc/source/feedback.h:23`) | | `osc<i>_phase` | `osc<i>PhaseMenu` (`:68`, `:99`) | `osc::RetriggerPhase`
/// (`osc/retrigger_phase.h:25`) | | `sample<i>_reverse` | `sample<i>ReverseMenu` (`:69`, `:100`) | `sample::Reverse`
/// (`sample/reverse.h:27`) | | `sample<i>_repeat` | `sample<i>RepeatMenu` (`:70`, `:101`) | `sample::Repeat`
/// (`sample/repeat.h:33`) | | `sample<i>_time_stretch` | `sample<i>TimeStretchMenu` (`:71`, `:102`) |
/// `sample::TimeStretch` (`sample/time_stretch.h:27`) | | `sample<i>_pitch_speed` | `sample<i>PitchSpeedMenu` (`:72`,
/// `:103`) | `sample::PitchSpeed` (`sample/pitch_speed.h:29`) | | `sample<i>_interpolation` |
/// `sample<i>InterpolationMenu` (`:73`, `:104`) | `sample::Interpolation` (`sample/interpolation.h:26`) | |
/// `sample<i>_start` / `_end` | `sample<i>StartMenu` / `EndMenu` (`:74-75`, `:105-106`) | `sample::Start` / `End` over
/// `LoopPoint` (`sample/loop_point.h:24`) | | `source<i>` | `source<i>Menu` (`:92`, `:123`) | `submenu::ActualSource`
/// (`submenu/actual_source.h:22`) | | `modulator<i>_transpose` | `modulator<i>TransposeMenu` (`:124`, `:133`) |
/// `modulator::Transpose` (`modulator/transpose.h:27`) | | `modulator<i>_feedback` | `modulator<i>FeedbackMenu`
/// (`:125`, `:134`) | `source::patched_param::ModulatorFeedback` (`modulator_feedback.h:22`) | | `modulator<i>_phase` |
/// `modulator<i>PhaseMenu` (`:126`, `:135`) | `osc::RetriggerPhase`, `for_modulator` | | `modulator_dest` |
/// `modulatorDestMenu` (`:136`) | `modulator::Destination` (`modulator/destination.h:28`) | | `modulator<i>` |
/// `modulator<i>Menu` (`:132`, `:143`) | `submenu::Modulator` (`submenu/modulator.h:21`) | | `source<i>_volume` |
/// `source<i>VolumeMenu` (`:144-145`) | `osc::source::Volume` (`osc/source/volume.h:24`) | | `modulator<i>_volume` |
/// `modulator<i>Volume` (`:146-147`) | `source::patched_param::ModulatorLevel` (`modulator_level.h:22`) | | `noise` |
/// `noiseMenu` (`:148`) | `patched_param::IntegerNonFM` (`patched_param/integer_non_fm.h:22`) | | `osc_sync` |
/// `oscSyncMenu` (`:149`) | `osc::Sync` (`osc/sync.h:26`) | | `osc_mixer` | `oscMixerMenu` (`:158`) | `HorizontalMenu`
/// |
///
/// @note **The four bespoke items.** `FileSelector`, `osc::AudioRecorder` and the two `LoopPoint`s
///       do not edit a value at all -- each `beginSession`s straight into another whole UI (the
///       sample browser, the audio recorder, the sample marker editor). Spec E §4 calls those
///       `Kind::Custom` and names "file selector" among the seven it expects. The real
///       implementations are E2's `BrowserScene` / `AudioRecorderScene` / `SampleMarkerScene`; until
///       then each points at `kPending`, a placeholder that draws the item's title and consumes
///       nothing, so the node is honest data (the validator's `NullCustom` rule refuses a
///       `Kind::Custom` with no implementation) and the page renders the item rather than stale
///       pixels. Two instances of `LoopPoint` -- start and end -- so the count of distinct
///       `CustomItem`s here is 1, well inside spec E §4's ten.
///
/// @note **`range_dependent`.** Four legacy classes in this subtree override
///       `checkPermissionToBeginSession` to route through
///       `SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam`: `sample::Transpose`
///       (`sample/transpose.h:120-135`, which also overrides `isRangeDependent()` at `:137`),
///       `LoopPoint` (`sample/loop_point.cpp:38-58`, `loop_point.h:31`), `FileSelector`
///       (`file_selector.cpp:68-86`) and `osc::AudioRecorder` (`osc/audio_recorder.h:65-74`). That is
///       exactly `node.h`'s own list minus `multi_range.h`, so eight nodes here carry the flag
///       (`kOscRangeDependentNodes`); the validator's `Flag::RangeDependent` audit re-checks it.
///
/// @note **Per-source relevance.** Legacy bakes `source_id_` into each item; `RelevantFn` has no
///       room for one, so `relevance.h` declares `osc0_*` / `osc1_*` pairs. See its own note.

#include "scenes/menu/nodes/nodes.h"

#include "scenes/l10n_bridge.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h" // MenuState, for the placeholder `CustomItem`
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/relevance.h"
#include "scenes/profile.h"

#include "shell/scene.h"
#include "ui/oled/frame.h"
#include "ui/oled/text.h"

#include "definitions_cxx.hpp"
#include "model/sample/sample_controls.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/multi_range/multisample_range.h"

#include <array>

namespace deluge::scenes::menu::nodes {

namespace {

using deluge::l10n::String;
namespace params = deluge::modulation::params;

// ---------------------------------------------------------------------------------------------
// The placeholder for the four bespoke items. See the file note.

/// @brief Draws the item's own title and does nothing else.
///
/// Deliberately the same "honest placeholder" `MenuScene`'s own unimplemented-kind arm draws
/// (`menu_scene.cpp:425-432`): the user sees which item they are on rather than the previous
/// screen. Replaced, per item, when E2 lands the browser, the recorder and the marker editor.
class PendingCustom final : public CustomItem {
public:
	void render(deluge::ui::oled::OledFrame& f, const MenuContext&, MenuState& state) const override {
		const Node* node = state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
		deluge::ui::shell::TextScratch scratch;
		deluge::ui::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch)
		                                           : std::string_view{});
	}
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent&, const deluge::ui::shell::UiContext&,
	                                 const MenuContext&, MenuState&) const override {
		return {};
	}
};

// Not `constexpr`: a polymorphic object's address is still a core constant expression, which is
// all the `custom()` builder needs, and this way the class needs no `constexpr` virtual support.
const PendingCustom kPending{};

// ---------------------------------------------------------------------------------------------
// Shared accessors.

Source* source_of(const MenuContext& ctx, uint8_t index) {
	return ctx.sound != nullptr ? &ctx.sound->sources[index] : nullptr;
}

/// @brief `getCurrentSampleControls` (`gui/menu_item/sample/utils.cpp:17-24`): an audio clip's own
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
///        `editingKit()` is `getCurrentOutputType() == KIT` (`gui/ui/sound_editor.cpp:165-167`),
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

/// @brief `Type::writeCurrentValue` (`osc/type.h:49-70`). No affect-entire loop: `osc::Type`
///        overrides neither `usesAffectEntire()` nor the loop, unlike most of its neighbours.
void set_osc_type(const MenuContext& ctx, int32_t value, uint8_t index) {
	Source* source = source_of(ctx, index);
	if (source == nullptr) {
		return;
	}
	const OscType old_value = source->oscType;
	auto new_value = static_cast<OscType>(value);
	if (!may_use_dx(ctx, index) && static_cast<int32_t>(new_value) >= static_cast<int32_t>(OscType::DX7)) {
		new_value = static_cast<OscType>(static_cast<int32_t>(new_value) + 1);
	}

	// `osc/type.h:55-66`: the modes that need every voice unassigned first.
	const auto needs_unassignment = [](OscType t) {
		return t == OscType::INPUT_L || t == OscType::INPUT_R || t == OscType::INPUT_STEREO || t == OscType::SAMPLE
		       || t == OscType::DX7 || t == OscType::WAVETABLE;
	};
	if (needs_unassignment(old_value) || needs_unassignment(new_value)) {
		ctx.sound->killAllVoices();
	}

	source->setOscType(new_value);

	if (old_value == OscType::SQUARE || new_value == OscType::SQUARE) { // `:68-70`
		ctx.sound->setupPatchingForAllParamManagers(ctx.song);
	}
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
// Oscillator transpose" (`sample/transpose.h:29`).

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
	bindings::apply_to_targets(ctx, value, index,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t index, const MenuContext& ctx) {
		                           if (t.sound == nullptr) {
			                           return;
		                           }
		                           int32_t transpose = 0;
		                           int32_t cents = 0;
		                           menu_to_transpose(v, &transpose, &cents);
		                           Source& source = t.sound->sources[index];
		                           // `sample/transpose.h:59-76` (kit) / `:82-98` (single): the kit
		                           // arm always takes range 0, the single arm the selected range.
		                           MultisampleRange* range = nullptr;
		                           if (!source.ranges.empty() && t.sound->getSynthMode() != ::SynthMode::FM
		                               && source.oscType == OscType::SAMPLE) {
			                           range = t.whole_kit ? static_cast<MultisampleRange*>(source.ranges.getElement(0))
			                                               : transpose_range(ctx, static_cast<uint8_t>(index));
		                           }
		                           if (range != nullptr) {
			                           range->sampleHolder.transpose = transpose;
			                           range->sampleHolder.setCents(cents);
		                           }
		                           else {
			                           source.transpose = transpose;
			                           source.setCents(cents);
		                           }
		                           t.sound->recalculateAllVoicePhaseIncrements(t.stack);
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
	const uint32_t value =
	    for_modulator ? ctx.sound->modulatorRetriggerPhase[index] : ctx.sound->oscRetriggerPhase[index];
	return value == 0xFFFFFFFF ? -1 : static_cast<int32_t>(value / 11930464);
}

/// @brief `writeCurrentValue` (`retrigger_phase.h:49-58`). No affect-entire loop in legacy either.
void set_retrigger_phase(const MenuContext& ctx, int32_t value, uint8_t index, bool for_modulator) {
	if (ctx.sound == nullptr) {
		return;
	}
	const uint32_t stored = value < 0 ? 0xFFFFFFFFu : static_cast<uint32_t>(value * 11930464);
	if (for_modulator) {
		ctx.sound->modulatorRetriggerPhase[index] = stored;
	}
	else {
		ctx.sound->oscRetriggerPhase[index] = stored;
	}
}

// ---------------------------------------------------------------------------------------------
// The sample items.

int32_t get_sample_reverse(const MenuContext& ctx, uint8_t index) {
	Source* source = source_of(ctx, index);
	return source != nullptr && source->sampleControls.reversed ? 1 : 0; // `sample/reverse.h:40-43`
}
void set_sample_reverse(const MenuContext& ctx, int32_t value, uint8_t index) {
	// `sample/reverse.h:45-66`: kill this sound's voices, then flip the source's direction.
	bindings::apply_to_targets(ctx, value, index,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t index, const MenuContext&) {
		                           if (t.sound == nullptr) {
			                           return;
		                           }
		                           t.sound->killAllVoices();
		                           t.sound->sources[index].setReversed(v != 0);
	                           });
}

int32_t get_sample_repeat(const MenuContext& ctx, uint8_t index) {
	Source* source = source_of(ctx, index);
	return source != nullptr ? static_cast<int32_t>(source->repeatMode) : 0; // `sample/repeat.h:44-47`
}
void set_sample_repeat(const MenuContext& ctx, int32_t value, uint8_t index) {
	// `sample/repeat.h:48-108`, minus two calls this layer cannot make: the kit arpeggiator note-off
	// (`:70-73`, which reaches `instrumentClipView` through `getCurrentInstrumentClip`) and the
	// `uiNeedsRendering(&instrumentClipView, ...)` at `:106` -- the notify design replaces the second
	// with the `Dirty::Oled` `bindings::write` already raises for a `get`/`set` node, and the first
	// belongs to E2's `NoteClipScene`. Recorded, not silently dropped.
	bindings::apply_to_targets(ctx, value, index,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t index, const MenuContext&) {
		                           if (t.sound == nullptr) {
			                           return;
		                           }
		                           Source& source = t.sound->sources[index];
		                           const auto mode = static_cast<SampleRepeatMode>(v);
		                           // `:65-74`: STRETCH forces pitch/speed independence on, and
		                           // leaving STRETCH forces it off; both kill the voices first.
		                           if (mode == SampleRepeatMode::STRETCH) {
			                           t.sound->killAllVoices();
			                           source.sampleControls.pitchAndSpeedAreIndependent = true;
		                           }
		                           else if (source.repeatMode == SampleRepeatMode::STRETCH) {
			                           t.sound->killAllVoices();
			                           source.sampleControls.pitchAndSpeedAreIndependent = false;
		                           }
		                           source.repeatMode = mode;
	                           });
}

int32_t get_time_stretch(const MenuContext& ctx, uint8_t index) {
	Source* source = source_of(ctx, index);
	return source != nullptr ? source->timeStretchAmount : 0; // `sample/time_stretch.h:40-43`
}
void set_time_stretch(const MenuContext& ctx, int32_t value, uint8_t index) {
	bindings::apply_to_targets(ctx, value, index,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t index, const MenuContext&) {
		                           if (t.sound != nullptr) {
			                           t.sound->sources[index].timeStretchAmount = v; // `:45-63`
		                           }
	                           });
}

int32_t get_pitch_speed(const MenuContext& ctx, uint8_t index) {
	const SampleControls* controls = sample_controls_of(ctx, index);
	return controls != nullptr && controls->pitchAndSpeedAreIndependent ? 1 : 0; // `sample/pitch_speed.h:39-42`
}
void set_pitch_speed(const MenuContext& ctx, int32_t value, uint8_t index) {
	// `sample/pitch_speed.h:44-63`. The single-target arm goes through `getCurrentSampleControls`,
	// so on an audio clip it writes the clip's controls rather than a source's.
	if (!bindings::writes_whole_kit(ctx)) {
		if (SampleControls* controls = sample_controls_of(ctx, index); controls != nullptr) {
			controls->pitchAndSpeedAreIndependent = value != 0;
		}
		return;
	}
	bindings::apply_to_targets(ctx, value, index,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t index, const MenuContext&) {
		                           if (t.sound != nullptr) {
			                           t.sound->sources[index].sampleControls.pitchAndSpeedAreIndependent = v != 0;
		                           }
	                           });
}

int32_t get_interpolation(const MenuContext& ctx, uint8_t index) {
	const SampleControls* controls = sample_controls_of(ctx, index);
	return controls != nullptr ? static_cast<int32_t>(controls->interpolationMode)
	                           : 0; // `sample/interpolation.h:32-35`
}
void set_interpolation(const MenuContext& ctx, int32_t value, uint8_t index) {
	// `sample/interpolation.h:37-59`, same shape as pitch/speed above.
	if (!bindings::writes_whole_kit(ctx)) {
		if (SampleControls* controls = sample_controls_of(ctx, index); controls != nullptr) {
			controls->interpolationMode = static_cast<InterpolationMode>(value);
		}
		return;
	}
	bindings::apply_to_targets(
	    ctx, value, index, [](const bindings::SoundTarget& t, int32_t v, int32_t index, const MenuContext&) {
		    if (t.sound != nullptr) {
			    t.sound->sources[index].sampleControls.interpolationMode = static_cast<InterpolationMode>(v);
		    }
	    });
}

// ---------------------------------------------------------------------------------------------
// The FM modulator items.

int32_t get_modulator_transpose(const MenuContext& ctx, uint8_t index) {
	if (ctx.sound == nullptr) {
		return 0;
	}
	// `modulator/transpose.h:35-38`.
	return transpose_to_menu(ctx.sound->modulatorTranspose[index], ctx.sound->modulatorCents[index]);
}
void set_modulator_transpose(const MenuContext& ctx, int32_t value, uint8_t index) {
	bindings::apply_to_targets(
	    ctx, value, index, [](const bindings::SoundTarget& t, int32_t v, int32_t index, const MenuContext&) {
		    // `modulator/transpose.h:41-73`; the kit arm skips a drum that is
		    // not in FM mode (`:56`), the single arm does not test at all.
		    if (t.sound == nullptr || (t.whole_kit && t.sound->getSynthMode() != ::SynthMode::FM)) {
			    return;
		    }
		    int32_t transpose = 0;
		    int32_t cents = 0;
		    menu_to_transpose(v, &transpose, &cents);
		    t.sound->setModulatorTranspose(index, transpose, t.stack);
		    t.sound->setModulatorCents(index, cents, t.stack);
	    });
}

int32_t get_modulator_dest(const MenuContext& ctx) {
	return ctx.sound != nullptr && ctx.sound->modulator1ToModulator0 ? 1 : 0; // `modulator/destination.h:31`
}
void set_modulator_dest(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(
	    ctx, value, 0, [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		    // `modulator/destination.h:33-62`, kit arm filtered on FM (`:47`).
		    if (t.sound == nullptr || (t.whole_kit && t.sound->getSynthMode() != ::SynthMode::FM)) {
			    return;
		    }
		    t.sound->modulator1ToModulator0 = v != 0;
	    });
}

int32_t get_osc_sync(const MenuContext& ctx) {
	return ctx.sound != nullptr && ctx.sound->oscillatorSync ? 1 : 0; // `osc/sync.h:29`
}
void set_osc_sync(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(
	    ctx, value, 0, [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		    // `osc/sync.h:31-53`: the kit arm re-applies `isRelevant`'s own
		    // test per drum (`:42-44`), the single arm assigns unconditionally.
		    if (t.sound == nullptr) {
			    return;
		    }
		    if (t.whole_kit
		        && (t.sound->getSynthMode() == ::SynthMode::FM || t.sound->sources[0].oscType == OscType::SAMPLE
		            || t.sound->sources[1].oscType == OscType::SAMPLE)) {
			    return;
		    }
		    t.sound->oscillatorSync = v != 0;
	    });
}

// ---------------------------------------------------------------------------------------------
// Option name lists.

/// `sample::Repeat::getOptions` (`sample/repeat.h:99-107`).
constexpr uint16_t kRepeatNames[] = {
    sid(String::STRING_FOR_CUT),
    sid(String::STRING_FOR_ONCE),
    sid(String::STRING_FOR_LOOP),
    sid(String::STRING_FOR_STRETCH),
};
/// `sample::PitchSpeed::getOptions` (`sample/pitch_speed.h:65-68`).
constexpr uint16_t kPitchSpeedNames[] = {
    sid(String::STRING_FOR_LINKED),
    sid(String::STRING_FOR_INDEPENDENT),
};
/// `AudioInterpolation::getOptions` (`gui/menu_item/audio_interpolation.h:41-44`).
constexpr uint16_t kInterpolationNames[] = {
    sid(String::STRING_FOR_LINEAR),
    sid(String::STRING_FOR_SINC),
};
/// `modulator::Destination::getOptions` (`modulator/destination.h:57-63`).
constexpr uint16_t kModulatorDestNames[] = {
    sid(String::STRING_FOR_CARRIERS),
    sid(String::STRING_FOR_MODULATOR_1),
};

// ---------------------------------------------------------------------------------------------
// The tree.
//
// Two macros, one per repeated shape: the per-source `get`/`set` hooks have to be distinct
// functions (a `Binding` holds a plain function pointer, with no room for legacy's `source_id_`
// member), and the `Node`s themselves have to be distinct objects for their addresses to go in a
// `children` array.

#define DELUGE_OSC_SOURCE(i, name_id)                                                                                  \
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
	}                                                                                                                  \
	constexpr Node osc##i##_type =                                                                                     \
	    with_title(dynamic_enumeration(sid(String::STRING_FOR_TYPE), osc##i##_type_names, get_osc##i##_type,           \
	                                   set_osc##i##_type, relevance::not_fm),                                          \
	               sid(String::STRING_FOR_OSC_TYPE_MENU_TITLE));                                                       \
	constexpr Node file##i##_selector = with_range_dependent(                                                          \
	    custom(sid(String::STRING_FOR_FILE_BROWSER), &kPending, relevance::osc##i##_file_selector));                   \
	constexpr Node source##i##_transpose = with_range_dependent(                                                       \
	    with_title(decimal_item(sid(String::STRING_FOR_TRANSPOSE), get_source##i##_transpose,                          \
	                            set_source##i##_transpose, kTransposeRange, 2, relevance::osc##i##_transpose),         \
	               sid(String::STRING_FOR_OSC_TRANSPOSE_MENU_TITLE)));                                                 \
	constexpr Node source##i##_wave_index =                                                                            \
	    param(sid(String::STRING_FOR_WAVE_INDEX),                                                                      \
	          {params::Kind::PATCHED, static_cast<uint8_t>(params::LOCAL_OSC_A_WAVE_INDEX + (i))}, Hint::Slider,       \
	          relevance::osc##i##_wave_index, sid(String::STRING_FOR_OSC_WAVE_IND_MENU_TITLE));                        \
	constexpr Node osc##i##_pulse_width = param(                                                                       \
	    sid(String::STRING_FOR_PULSE_WIDTH),                                                                           \
	    {params::Kind::PATCHED, static_cast<uint8_t>(params::LOCAL_OSC_A_PHASE_WIDTH + (i))}, Hint::None,              \
	    relevance::osc##i##_pulse_width, sid(String::STRING_FOR_OSC_P_WIDTH_MENU_TITLE), Scaling::HalfPrecision);      \
	constexpr Node source##i##_feedback =                                                                              \
	    param(sid(String::STRING_FOR_FEEDBACK),                                                                        \
	          {params::Kind::PATCHED, static_cast<uint8_t>(params::LOCAL_CARRIER_0_FEEDBACK + (i))}, Hint::Bar,        \
	          relevance::osc##i##_carrier_feedback, sid(String::STRING_FOR_CARRIER_FEED_MENU_TITLE));                  \
	constexpr Node osc##i##_phase =                                                                                    \
	    with_hint(with_title(decimal_item(sid(String::STRING_FOR_RETRIGGER_PHASE), get_osc##i##_phase,                 \
	                                      set_osc##i##_phase, kPhaseRange, 0, relevance::osc##i##_retrigger_phase),    \
	                         sid(String::STRING_FOR_OSC_R_PHASE_MENU_TITLE)),                                          \
	              Hint::Slider);                                                                                       \
	constexpr Node sample##i##_reverse =                                                                               \
	    with_title(toggle_item(sid(String::STRING_FOR_REVERSE), get_sample##i##_reverse, set_sample##i##_reverse,      \
	                           relevance::osc##i##_is_sample),                                                         \
	               sid(String::STRING_FOR_SAMP_REVERSE_MENU_TITLE));                                                   \
	constexpr Node sample##i##_repeat =                                                                                \
	    with_title(enumeration(sid(String::STRING_FOR_REPEAT_MODE), kRepeatNames, get_sample##i##_repeat,              \
	                           set_sample##i##_repeat, relevance::osc##i##_is_sample),                                 \
	               sid(String::STRING_FOR_SAMP_REPEAT_MENU_TITLE));                                                    \
	constexpr Node sample##i##_time_stretch =                                                                          \
	    with_title(int_item(sid(String::STRING_FOR_SPEED), get_sample##i##_speed, set_sample##i##_speed,               \
	                        kTimeStretchRange, Hint::None, relevance::osc##i##_is_sample),                             \
	               sid(String::STRING_FOR_SAMP_SPEED_MENU_TITLE));                                                     \
	constexpr Node sample##i##_pitch_speed =                                                                           \
	    enumeration(sid(String::STRING_FOR_PITCH_SPEED), kPitchSpeedNames, get_sample##i##_pitch_speed,                \
	                set_sample##i##_pitch_speed, relevance::osc##i##_pitch_speed);                                     \
	constexpr Node sample##i##_interpolation =                                                                         \
	    with_title(enumeration(sid(String::STRING_FOR_INTERPOLATION), kInterpolationNames, get_sample##i##_interp,     \
	                           set_sample##i##_interp, relevance::osc##i##_interpolation),                             \
	               sid(String::STRING_FOR_SAMP_INTERP_MENU_TITLE));                                                    \
	constexpr Node sample##i##_start =                                                                                 \
	    with_range_dependent(custom(sid(String::STRING_FOR_START_POINT), &kPending, relevance::osc##i##_is_sample));   \
	constexpr Node sample##i##_end =                                                                                   \
	    with_range_dependent(custom(sid(String::STRING_FOR_END_POINT), &kPending, relevance::osc##i##_is_sample));     \
	constexpr const Node* kSource##i##Children[] = {                                                                   \
	    &osc##i##_type,        &file##i##_selector,       &source##i##_transpose,   &source##i##_wave_index,           \
	    &osc##i##_pulse_width, &source##i##_feedback,     &osc##i##_phase,          &sample##i##_reverse,              \
	    &sample##i##_repeat,   &sample##i##_time_stretch, &sample##i##_pitch_speed, &sample##i##_interpolation,        \
	    &sample##i##_start,    &sample##i##_end};                                                                      \
	constexpr Node source##i = submenu(sid(String::name_id), kSource##i##Children, Layout::Horizontal)

#define DELUGE_OSC_MODULATOR(i)                                                                                        \
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
	}                                                                                                                  \
	constexpr Node modulator##i##_transpose =                                                                          \
	    with_title(decimal_item(sid(String::STRING_FOR_TRANSPOSE), get_modulator##i##_transpose_v,                     \
	                            set_modulator##i##_transpose_v, kTransposeRange, 2, relevance::is_fm),                 \
	               sid(String::STRING_FOR_FM_MOD_TRAN_MENU_TITLE));                                                    \
	constexpr Node modulator##i##_feedback =                                                                           \
	    param(sid(String::STRING_FOR_FEEDBACK),                                                                        \
	          {params::Kind::PATCHED, static_cast<uint8_t>(params::LOCAL_MODULATOR_0_FEEDBACK + (i))}, Hint::Bar,      \
	          relevance::is_fm, sid(String::STRING_FOR_FM_MOD_FBACK_MENU_TITLE));                                      \
	constexpr Node modulator##i##_phase_node = with_hint(                                                              \
	    with_title(decimal_item(sid(String::STRING_FOR_RETRIGGER_PHASE), get_modulator##i##_phase,                     \
	                            set_modulator##i##_phase, kPhaseRange, 0, relevance::modulator##i##_retrigger_phase),  \
	               sid(String::STRING_FOR_FM_MOD_RETRIG_MENU_TITLE)),                                                  \
	    Hint::Slider)

/// `menu_item::Transpose::getMinValue`/`getMaxValue`/`getNumDecimalPlaces`
/// (`gui/menu_item/transpose.h:34-36`).
constexpr IntRange kTransposeRange{-9600, 9600, 1};
/// `osc::RetriggerPhase::getMinValue`/`getMaxValue` (`osc/retrigger_phase.h:34-35`); -1 is the "OFF"
/// sentinel legacy prints in place of a number (`:60-67`).
constexpr IntRange kPhaseRange{-1, 360, 1};
/// `sample::TimeStretch::getMinValue`/`getMaxValue` (`sample/time_stretch.h:65-66`).
constexpr IntRange kTimeStretchRange{-48, 48, 1};

DELUGE_OSC_SOURCE(0, STRING_FOR_OSCILLATOR_1);
DELUGE_OSC_SOURCE(1, STRING_FOR_OSCILLATOR_2);
DELUGE_OSC_MODULATOR(0);
DELUGE_OSC_MODULATOR(1);

#undef DELUGE_OSC_SOURCE
#undef DELUGE_OSC_MODULATOR

constexpr Node modulator_dest = with_title(enumeration(sid(String::STRING_FOR_DESTINATION), kModulatorDestNames,
                                                       get_modulator_dest, set_modulator_dest, relevance::is_fm),
                                           sid(String::STRING_FOR_FM_MOD2_DEST_MENU_TITLE));

constexpr const Node* kModulator0Children[] = {&modulator0_transpose, &modulator0_feedback, &modulator0_phase_node};
constexpr Node modulator0 =
    submenu(sid(String::STRING_FOR_FM_MODULATOR_1), kModulator0Children, Layout::Horizontal, relevance::is_fm);
constexpr const Node* kModulator1Children[] = {&modulator1_transpose, &modulator1_feedback, &modulator1_phase_node,
                                               &modulator_dest};
constexpr Node modulator1 =
    submenu(sid(String::STRING_FOR_FM_MODULATOR_2), kModulator1Children, Layout::Horizontal, relevance::is_fm);

// The recorder page (`g_menus.inc:55-61`). Two slots wide each --
// `AudioRecorder::getOccupiedSlots()` (`osc/audio_recorder.h:77`).
constexpr Node sample0_recorder = with_slots(
    with_range_dependent(custom(sid(String::STRING_FOR_RECORD_TO_SAMPLE), &kPending, relevance::is_subtractive)), 2);
constexpr Node sample1_recorder = with_slots(
    with_range_dependent(custom(sid(String::STRING_FOR_RECORD_TO_SAMPLE), &kPending, relevance::is_subtractive)), 2);
constexpr const Node* kRecorderChildren[] = {&sample0_recorder, &sample1_recorder};
constexpr Node recorder = submenu(sid(String::STRING_FOR_SAMPLE_RECORDER), kRecorderChildren, Layout::Horizontal);

// The mixer page (`g_menus.inc:144-158`).
constexpr Node source0_volume =
    param(sid(String::STRING_FOR_OSC_LEVEL_MENU_TITLE), {params::Kind::PATCHED, params::LOCAL_OSC_A_VOLUME}, Hint::Bar,
          relevance::not_ringmod);
constexpr Node source1_volume =
    param(sid(String::STRING_FOR_OSC_LEVEL_MENU_TITLE), {params::Kind::PATCHED, params::LOCAL_OSC_B_VOLUME}, Hint::Bar,
          relevance::not_ringmod);
constexpr Node modulator0_volume =
    param(sid(String::STRING_FOR_FM_MOD_LEVEL_MENU_TITLE), {params::Kind::PATCHED, params::LOCAL_MODULATOR_0_VOLUME},
          Hint::Bar, relevance::is_fm);
constexpr Node modulator1_volume =
    param(sid(String::STRING_FOR_FM_MOD_LEVEL_MENU_TITLE), {params::Kind::PATCHED, params::LOCAL_MODULATOR_1_VOLUME},
          Hint::Bar, relevance::is_fm);
constexpr Node noise = param(sid(String::STRING_FOR_NOISE_LEVEL), {params::Kind::PATCHED, params::LOCAL_NOISE_VOLUME},
                             Hint::Bar, relevance::not_fm);
constexpr Node osc_sync =
    toggle_item(sid(String::STRING_FOR_OSCILLATOR_SYNC), get_osc_sync, set_osc_sync, relevance::osc_sync_allowed);
constexpr const Node* kMixerChildren[] = {&source0_volume,    &source1_volume, &modulator0_volume,
                                          &modulator1_volume, &noise,          &osc_sync};
constexpr Node osc_mixer = submenu(sid(String::STRING_FOR_OSCILLATOR_MIXER), kMixerChildren, Layout::Horizontal);

/// `main.py`'s own order for this subtree: recorder, both sources, both modulators, the mixer.
constexpr const Node* kRoots[] = {&recorder, &source0, &source1, &modulator0, &modulator1, &osc_mixer};

constexpr const Node* kRangeDependent[] = {
    &sample0_recorder,  &sample1_recorder, &file0_selector, &file1_selector, &source0_transpose,
    &source1_transpose, &sample0_start,    &sample0_end,    &sample1_start,  &sample1_end,
};
constexpr const Node* kHalfPrecision[] = {&osc0_pulse_width, &osc1_pulse_width};

} // namespace

const std::span<const Node* const> kSoundOscChildren{kRoots};
const std::span<const Node* const> kOscRangeDependentNodes{kRangeDependent};
const std::span<const Node* const> kOscHalfPrecisionNodes{kHalfPrecision};

} // namespace deluge::scenes::menu::nodes
