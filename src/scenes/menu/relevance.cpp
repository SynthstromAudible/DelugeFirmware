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
#include "dsp/reverb/reverb.hpp" // dsp::Reverb::Model -- audio_engine.h only forward declares it
#include "model/clip/audio_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/filters/filter_config.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/output.h"
#include "model/settings/runtime_feature_settings.h"
#include "modulation/arpeggiator.h"
#include "processing/audio_output.h"
#include "processing/engines/audio_engine.h"
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
	const auto& filter = ctx.mod->modControllableSnapshot().effects.filter;
	return (high_pass ? filter.hpf_mode : filter.lpf_mode) != ::FilterMode::OFF;
}

/// @brief `isSampleModeSample` (`gui/menu_item/sample/utils.cpp:9-15`).
bool is_sample_mode_sample(const MenuContext& ctx, uint8_t index) {
	const Source* source = source_of(ctx, index);
	return ctx.sound != nullptr && ctx.sound->getSynthMode() == ::SynthMode::SUBTRACTIVE && source != nullptr
	       && source->oscType == OscType::SAMPLE && loaded(source);
}

/// @brief `FileSelector::isRelevant` (`gui/menu_item/file_selector.cpp:54-65`): browsing can load a sample even when
///        the oscillator uses a synth waveform, so any subtractive sound qualifies, and ring-mod with a wavetable.
bool file_selector(const MenuContext& ctx, uint8_t index) {
	if (ctx.is_audio_clip) {
		return true; // `:55-57` -- `getCurrentClip()->type == ClipType::AUDIO`
	}
	const Source* source = source_of(ctx, index);
	if (ctx.sound == nullptr || source == nullptr) {
		return false;
	}
	const ::SynthMode mode = ctx.sound->getSynthMode();
	return mode == ::SynthMode::SUBTRACTIVE || (mode == ::SynthMode::RINGMOD && source->oscType == OscType::WAVETABLE);
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

// ---------------------------------------------------------------------------------------------
// Batch 2's shared clip-shape tests -- the `SoundEditor::editing*` family
// (`gui/ui/sound_editor.cpp:199-240`) against `MenuContext` instead of globals.

/// @brief `SoundEditor::editingCVOrMIDIClip()` (`sound_editor.cpp:211-213`).
bool cv_or_midi_clip(const MenuContext& ctx) {
	return ctx.output != nullptr && (ctx.output->type == OutputType::MIDI_OUT || ctx.output->type == OutputType::CV);
}

/// @brief `getCurrentKit()->selectedDrum`, or null when a kit is not what is being edited.
///
/// Read from `ctx.output` rather than from `MenuContext::root`: `context_for` only reaches
/// `Root::MidiDrum` / `Root::GateDrum` when the kit's global-FX page is *not* open
/// (`context.cpp:269-275`), whereas legacy's `editingNonAudioDrumRow()` asks about the selected
/// drum whatever page is open. Going to the kit keeps the transcription exact.
const Drum* selected_drum(const MenuContext& ctx) {
	if (!ctx.is_kit || ctx.output == nullptr) {
		return nullptr;
	}
	return static_cast<const Kit*>(ctx.output)->selectedDrum;
}

/// @brief `SoundEditor::editingNonAudioDrumRow()` (`sound_editor.cpp:215-222`).
bool non_audio_drum_row(const MenuContext& ctx) {
	const Drum* drum = selected_drum(ctx);
	return drum != nullptr && (drum->type == DrumType::MIDI || drum->type == DrumType::GATE);
}

/// @brief `SoundEditor::editingMidiDrumRow()` (`sound_editor.cpp:224-231`).
bool midi_drum_row(const MenuContext& ctx) {
	const Drum* drum = selected_drum(ctx);
	return drum != nullptr && drum->type == DrumType::MIDI;
}

/// @brief `SoundEditor::editingGateDrumRow()` (`sound_editor.cpp:233-240`).
bool gate_drum_row(const MenuContext& ctx) {
	const Drum* drum = selected_drum(ctx);
	return drum != nullptr && drum->type == DrumType::GATE;
}

/// @brief `SoundEditor::editingKitAffectEntire()` (`sound_editor.cpp:203-205`) -- the kit's own
///        global-FX page, which is what `setupKitGlobalFXMenu` selects (`context.h`'s
///        `kit_global_fx`).
bool kit_affect_entire(const MenuContext& ctx) {
	return ctx.is_kit && ctx.is_kit_global_fx;
}

/// @brief Is the arpeggiator on? The `soundEditor.currentArpSettings->mode != ArpMode::OFF` test
///        the randomizer items share. A context with no arp settings answers false rather than
///        dereferencing null (`relevance.cpp`'s file note).
bool arp_on(const MenuContext& ctx) {
	return ctx.arp != nullptr && ctx.arp->mode != ArpMode::OFF;
}

/// @brief `ModControllableAudio::getModFXType()` on a context that may have no mod controllable.
ModFXType mod_fx_type(const MenuContext& ctx) {
	return ctx.mod != nullptr ? ctx.mod->getModFXType() : ModFXType::NONE;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Sound-wide

bool is_fm(const MenuContext& ctx) {
	return ctx.sound != nullptr && ctx.sound->getSynthMode() == ::SynthMode::FM;
}
bool is_subtractive(const MenuContext& ctx) {
	return ctx.sound != nullptr && ctx.sound->getSynthMode() == ::SynthMode::SUBTRACTIVE;
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
bool osc0_is_sample(const MenuContext& ctx) {
	return is_sample_mode_sample(ctx, 0);
}
bool osc1_is_sample(const MenuContext& ctx) {
	return is_sample_mode_sample(ctx, 1);
}
bool osc0_interpolation(const MenuContext& ctx) {
	return interpolation(ctx, 0);
}
bool osc1_interpolation(const MenuContext& ctx) {
	return interpolation(ctx, 1);
}

// ---------------------------------------------------------------------------------------------
// Batch 2 -- the rest of the sound-editor root

bool is_kit_row(const MenuContext& ctx) {
	return ctx.is_kit && !ctx.is_kit_global_fx;
}
bool not_gate_drum_or_kit_fx(const MenuContext& ctx) {
	return !gate_drum_row(ctx) && !kit_affect_entire(ctx);
}

bool reverb_sidechain_on(const MenuContext& /*ctx*/) {
	return AudioEngine::reverbSidechainVolume >= 0; // `reverb/sidechain/shape.h:32`
}

bool stutter_quantize_allowed(const MenuContext& ctx) {
	return ctx.mod != nullptr && (ctx.mod->isSong() || !ctx.mod->effects().stutter.use_song_stutter);
}

bool bend_submenu(const MenuContext& ctx) {
	if (ctx.output == nullptr) {
		return false;
	}
	const OutputType type = ctx.output->type;
	return type == OutputType::SYNTH || type == OutputType::CV || type == OutputType::MIDI_OUT;
}

// --- Batch 3 (Task 8) -------------------------------------------------------------------------

bool is_midi_out(const MenuContext& ctx) {
	return ctx.output != nullptr && ctx.output->type == OutputType::MIDI_OUT;
}

bool audio_source_is_specific(const MenuContext& ctx) {
	if (ctx.output == nullptr || ctx.output->type != OutputType::AUDIO) {
		return false;
	}
	return static_cast<AudioOutput*>(ctx.output)->inputChannel == AudioInputChannel::SPECIFIC_OUTPUT;
}

bool audio_clip_has_sample(const MenuContext& ctx) {
	if (ctx.clip == nullptr || ctx.clip->type != ClipType::AUDIO) {
		return false;
	}
	return static_cast<AudioClip*>(ctx.clip)->sampleHolder.audioFile != nullptr;
}

bool battery_shown(const MenuContext& /*ctx*/) {
	// `battery::Level::isRelevant` (`gui/menu_item/battery/level.h:33-35`). A global read rather
	// than a context one -- the runtime feature settings are device-wide, exactly as legacy has it.
	return runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::ShowBatteryLevel);
}

bool is_session_view(const MenuContext& ctx) {
	// `song::MidiLearn::isRelevant` (`gui/menu_item/song/midi_learn.h:35-37`) and
	// `song::ConfigureMacros::isRelevant` (`gui/menu_item/song/configure_macros.h:35-37`), both
	// `return (getRootUI() == &sessionView);`. `is_session_view`, NOT `is_song_view`: the latter is
	// `!rootUIIsClipMinderScreen()` (`sound_editor.cpp:1687`) and stays true in the arranger and the
	// song automation view, the two places this test is what hides both items.
	return ctx.is_session_view;
}

// ---------------------------------------------------------------------------------------------
// The registry (Task 10's dump)
//
// **Adding a predicate to `relevance.h` means adding a row here.** Nothing can check that
// mechanically -- C++ has no way to enumerate a header's declarations -- so the guards are: the
// `static_assert` on the row count below, which forces a deliberate edit; `name_of` answering "?"
// for an unregistered predicate; and `tables_1_spec`, which asserts every predicate the tables
// actually use resolves to a real name and that the count is what this file says.
//
// **The sixteen `nodes::relevant_*_iterance_step_*` rows below are the one exception to "declared
// in `relevance.h`".** They are `scenes/menu/nodes/note.h`'s (spec E-note-family task 5), not this
// header's, but `name_of` is the only mechanism that turns a `RelevantFn` into a printable name --
// `deluge_menu_dump`'s "available_when" field calls it on every node's `relevant`, note family
// included -- and an unregistered predicate there prints `"?"`, not silence: fix round 1 caught
// sixteen `"?"` entries in the committed dump this way
// (`website/src/data/generated/menu-tree.json`) that no *running* spec had noticed, because
// every spec that asserts "no `?`" walks a lockstep pairing against the ten `MenuContext::Root`
// fixtures (`tables_1_spec.cpp`'s `all_pairs()`, `tables_2_spec.cpp`/`tables_3_spec.cpp`'s
// `tree().pairs`), and the note family has no `Root` of its own to be paired through. Forward
// declared here rather than pulled in through `nodes/note.h`, to avoid dragging that header's own
// (unrelated) dependency closure into this one just for sixteen function addresses.

} // namespace deluge::scenes::menu::relevance

namespace deluge::scenes::menu::nodes {
bool relevant_note_iterance_step_1(const MenuContext&);
bool relevant_note_iterance_step_2(const MenuContext&);
bool relevant_note_iterance_step_3(const MenuContext&);
bool relevant_note_iterance_step_4(const MenuContext&);
bool relevant_note_iterance_step_5(const MenuContext&);
bool relevant_note_iterance_step_6(const MenuContext&);
bool relevant_note_iterance_step_7(const MenuContext&);
bool relevant_note_iterance_step_8(const MenuContext&);
bool relevant_note_row_iterance_step_1(const MenuContext&);
bool relevant_note_row_iterance_step_2(const MenuContext&);
bool relevant_note_row_iterance_step_3(const MenuContext&);
bool relevant_note_row_iterance_step_4(const MenuContext&);
bool relevant_note_row_iterance_step_5(const MenuContext&);
bool relevant_note_row_iterance_step_6(const MenuContext&);
bool relevant_note_row_iterance_step_7(const MenuContext&);
bool relevant_note_row_iterance_step_8(const MenuContext&);
} // namespace deluge::scenes::menu::nodes

namespace deluge::scenes::menu::relevance {

namespace {

#define DELUGE_RELEVANCE_ROW(fn)                                                                                       \
	NamedPredicate {                                                                                                   \
		&fn, #fn                                                                                                       \
	}

constexpr std::array<NamedPredicate, 36> kRegistry{
    DELUGE_RELEVANCE_ROW(is_fm),
    DELUGE_RELEVANCE_ROW(is_subtractive),
    DELUGE_RELEVANCE_ROW(osc0_file_selector),
    DELUGE_RELEVANCE_ROW(osc1_file_selector),
    DELUGE_RELEVANCE_ROW(osc0_transpose),
    DELUGE_RELEVANCE_ROW(osc1_transpose),
    DELUGE_RELEVANCE_ROW(osc0_is_sample),
    DELUGE_RELEVANCE_ROW(osc1_is_sample),
    DELUGE_RELEVANCE_ROW(osc0_interpolation),
    DELUGE_RELEVANCE_ROW(osc1_interpolation),
    DELUGE_RELEVANCE_ROW(is_kit_row),
    DELUGE_RELEVANCE_ROW(not_gate_drum_or_kit_fx),
    DELUGE_RELEVANCE_ROW(reverb_sidechain_on),
    DELUGE_RELEVANCE_ROW(stutter_quantize_allowed),
    DELUGE_RELEVANCE_ROW(bend_submenu),
    DELUGE_RELEVANCE_ROW(is_midi_out),
    DELUGE_RELEVANCE_ROW(battery_shown),
    DELUGE_RELEVANCE_ROW(audio_source_is_specific),
    DELUGE_RELEVANCE_ROW(audio_clip_has_sample),
    DELUGE_RELEVANCE_ROW(is_session_view),
    // spec E-note-family task 5, fix round 1 -- see this section's own note above.
    DELUGE_RELEVANCE_ROW(nodes::relevant_note_iterance_step_1),
    DELUGE_RELEVANCE_ROW(nodes::relevant_note_iterance_step_2),
    DELUGE_RELEVANCE_ROW(nodes::relevant_note_iterance_step_3),
    DELUGE_RELEVANCE_ROW(nodes::relevant_note_iterance_step_4),
    DELUGE_RELEVANCE_ROW(nodes::relevant_note_iterance_step_5),
    DELUGE_RELEVANCE_ROW(nodes::relevant_note_iterance_step_6),
    DELUGE_RELEVANCE_ROW(nodes::relevant_note_iterance_step_7),
    DELUGE_RELEVANCE_ROW(nodes::relevant_note_iterance_step_8),
    DELUGE_RELEVANCE_ROW(nodes::relevant_note_row_iterance_step_1),
    DELUGE_RELEVANCE_ROW(nodes::relevant_note_row_iterance_step_2),
    DELUGE_RELEVANCE_ROW(nodes::relevant_note_row_iterance_step_3),
    DELUGE_RELEVANCE_ROW(nodes::relevant_note_row_iterance_step_4),
    DELUGE_RELEVANCE_ROW(nodes::relevant_note_row_iterance_step_5),
    DELUGE_RELEVANCE_ROW(nodes::relevant_note_row_iterance_step_6),
    DELUGE_RELEVANCE_ROW(nodes::relevant_note_row_iterance_step_7),
    DELUGE_RELEVANCE_ROW(nodes::relevant_note_row_iterance_step_8),
};

#undef DELUGE_RELEVANCE_ROW

// One row per predicate declared in `relevance.h` (54), plus the sixteen
// `nodes::relevant_*_iterance_step_*` rows: declared in `nodes/note.h`, not `relevance.h`, but
// registered here for the reason this section's own note gives. 54 + 16 = 70.
static_assert(kRegistry.size() == 36);

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
