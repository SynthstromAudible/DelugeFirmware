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
#include "deluge-project/audio_input.h"
#include "deluge-project/setting_group.h"
#include "deluge-project/song.h"
#include "engines/deluge/sound_state.h"
#include "model/settings/runtime_feature_settings.h"
#include "scenes/bind/runtime_facts.h"
#include "scenes/menu/bind_host.h"
#include "scenes/services/waveforms.h"

#include <array>

namespace deluge::scenes::menu::relevance {

namespace {

using deluge::instrument::OscType;
using deluge::instrument::SynthMode;

/// @brief The edited sound's state, as its record holds it, or null where the context edits no sound.
const deluge::instrument::SoundState* sound_state(const MenuContext& ctx) {
	return ctx.facts != nullptr && ctx.addresses.slot ? ctx.facts->sound_state(*ctx.addresses.slot) : nullptr;
}

/// @brief Does source @p index hold a loaded audio file? `Source::hasAtLeastOneAudioFileLoaded`: a fact of the card.
bool loaded(const MenuContext& ctx, uint8_t index) {
	return ctx.facts != nullptr && ctx.addresses.slot
	       && ((ctx.facts->loaded_sources(*ctx.addresses.slot) >> index) & 1U) != 0;
}

/// @brief `isSampleModeSample` (`gui/menu_item/sample/utils.cpp:9-15`).
bool is_sample_mode_sample(const MenuContext& ctx, uint8_t index) {
	const deluge::instrument::SoundState* state = sound_state(ctx);
	return state != nullptr && state->synth_mode == SynthMode::SUBTRACTIVE
	       && state->sources[index].osc_type == OscType::SAMPLE && loaded(ctx, index);
}

/// @brief `FileSelector::isRelevant` (`gui/menu_item/file_selector.cpp:54-65`): browsing can load a sample even when
///        the oscillator uses a synth waveform, so any subtractive sound qualifies, and ring-mod with a wavetable.
bool file_selector(const MenuContext& ctx, uint8_t index) {
	if (ctx.is_audio_clip) {
		return true; // `:55-57` -- `getCurrentClip()->type == ClipType::AUDIO`
	}
	const deluge::instrument::SoundState* state = sound_state(ctx);
	if (state == nullptr) {
		return false;
	}
	return state->synth_mode == SynthMode::SUBTRACTIVE
	       || (state->synth_mode == SynthMode::RINGMOD && state->sources[index].osc_type == OscType::WAVETABLE);
}

/// @brief `sample::Transpose::isRelevant` (`gui/menu_item/sample/transpose.h:139-145`).
bool transpose(const MenuContext& ctx, uint8_t index) {
	const deluge::instrument::SoundState* state = sound_state(ctx);
	if (state == nullptr) {
		return false;
	}
	const OscType type = state->sources[index].osc_type;
	if (type == OscType::SAMPLE || type == OscType::WAVETABLE) {
		return loaded(ctx, index);
	}
	return true;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Sound-wide

bool is_fm(const MenuContext& ctx) {
	const deluge::instrument::SoundState* state = sound_state(ctx);
	return state != nullptr && state->synth_mode == SynthMode::FM;
}
bool is_subtractive(const MenuContext& ctx) {
	const deluge::instrument::SoundState* state = sound_state(ctx);
	return state != nullptr && state->synth_mode == SynthMode::SUBTRACTIVE;
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
// ---------------------------------------------------------------------------------------------
// Batch 2 -- the rest of the sound-editor root

bool is_kit_row(const MenuContext& ctx) {
	return ctx.is_kit && !ctx.is_kit_global_fx;
}
bool not_gate_drum_or_kit_fx(const MenuContext& ctx) {
	// `!editingGateDrumRow() && !editingKitAffectEntire()` (`sound_editor.cpp:203-240`). The kit's own global-FX page
	// answers false whatever row is selected, so the selected row's kind matters only off it, where the root names it.
	return ctx.root != MenuContext::Root::GateDrum && !(ctx.is_kit && ctx.is_kit_global_fx);
}

bool reverb_sidechain_on(const MenuContext& ctx) {
	return ctx.model != nullptr && ctx.model->reverb.sidechain_volume >= 0; // `reverb/sidechain/shape.h:32`
}

bool stutter_quantize_allowed(const MenuContext& ctx) {
	// The song's own stutter always offers quantize; anything else's does unless it plays the song's.
	if (ctx.facts == nullptr) {
		return false;
	}
	if (const deluge::instrument::SoundState* state = sound_state(ctx); state != nullptr) {
		return !state->effects.stutter.use_song_stutter;
	}
	if (!ctx.addresses.chain) {
		return false;
	}
	if (ctx.addresses.chain_owner == bind::ChainOwner::SONG) {
		return true;
	}
	const deluge::effects::EffectStates* effects = ctx.facts->chain_effects(*ctx.addresses.chain);
	return effects != nullptr && !effects->stutter.use_song_stutter;
}

bool bend_submenu(const MenuContext& ctx) {
	using Instrument = deluge::engine::SettingContext::Instrument;
	const Instrument type = ctx.addresses.instrument;
	return type == Instrument::SYNTH || type == Instrument::CV || type == Instrument::MIDI;
}

// --- Batch 3 (Task 8) -------------------------------------------------------------------------

bool is_midi_out(const MenuContext& ctx) {
	return ctx.addresses.instrument == deluge::engine::SettingContext::Instrument::MIDI;
}

bool audio_source_is_specific(const MenuContext& ctx) {
	// An audio clip's track recording a specific track: its AUDIO_INPUT record's channel.
	if (!ctx.is_audio_clip || !ctx.addresses.track || ctx.model == nullptr) {
		return false;
	}
	const std::optional<deluge::serialization::Value> channel = deluge::project::read_setting(
	    *ctx.model,
	    deluge::project::SettingAddress{.group = deluge::project::SettingGroupId::AUDIO_INPUT,
	                                    .owner = *ctx.addresses.track},
	    "inputChannel");
	return channel
	       && channel->str() == deluge::project::kInputChannelWords[deluge::project::kInputChannelSpecificTrack];
}

bool audio_clip_has_sample(const MenuContext& ctx) {
	// A fact of the card: the clip's file is loaded.
	if (!ctx.is_audio_clip || !ctx.addresses.clip || ctx.facts == nullptr) {
		return false;
	}
	return menu::host_waveforms()
	    .info({.kind = deluge::scenes::services::WaveformAddress::Kind::AUDIO_CLIP, .clip = *ctx.addresses.clip})
	    .has_value();
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

constexpr std::array<NamedPredicate, 34> kRegistry{
    DELUGE_RELEVANCE_ROW(is_fm),
    DELUGE_RELEVANCE_ROW(is_subtractive),
    DELUGE_RELEVANCE_ROW(osc0_file_selector),
    DELUGE_RELEVANCE_ROW(osc1_file_selector),
    DELUGE_RELEVANCE_ROW(osc0_transpose),
    DELUGE_RELEVANCE_ROW(osc1_transpose),
    DELUGE_RELEVANCE_ROW(osc0_is_sample),
    DELUGE_RELEVANCE_ROW(osc1_is_sample),
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

// One row per predicate declared in `relevance.h` (18), plus the sixteen
// `nodes::relevant_*_iterance_step_*` rows: declared in `nodes/note.h`, not `relevance.h`, but
// registered here for the reason this section's own note gives. 18 + 16 = 34.
static_assert(kRegistry.size() == 34);

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
