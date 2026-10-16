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
/// @brief The nine non-settings roots' `get`/`set` hooks and their exported root span. The `Node`
///        literals are in `roots.h`.
///
/// Every hook is a transcription of the legacy `readCurrentValue`/`writeCurrentValue` cited above
/// it. Two things legacy does that no hook here can are recorded rather than silently dropped:
///
///   * **The UI side effects.** `audio_clip::Reverse::writeCurrentValue` ends in
///     `uiNeedsRendering(&audioClipView, ...)` (`audio_clip/reverse.h:37`) and the MIDI-instrument
///     toggles end in `getCurrentInstrument()->editedByUser = true` -- the latter IS model state
///     and is transcribed; the former is a `hid/`-side repaint that `bindings::write` already
///     replaces with `notify::Dirty::Oled` plus, for a param, `GridMain`. A `set` hook that wants
///     a pad repaint raises `Dirty::GridMain` itself, which the two audio-clip sample items do.
///   * **`getCurrent*()` globals.** Legacy reaches `getCurrentOutput()`, `getCurrentInstrument()`,
///     `getCurrentInstrumentClip()` and `getCurrentAudioClip()`; every one of those is
///     `MenuContext::output` / `::clip` here (task-3 ruling 3 -- `currentSong` references in this
///     file: 0).

#include "scenes/menu/nodes/roots.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "definitions_cxx.hpp"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/arp_setting_for.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/output.h"
#include "model/sample/sample.h"
#include "model/sample/sample_holder.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "modulation/sync.h"
#include "notify/dirty_set.h"
#include "processing/stem_export/stem_export.h"

namespace deluge::scenes::menu::nodes {

namespace {

/// @brief The context's output as a `MIDIInstrument`, or null.
///
/// Legacy's own comment on these items is "this is safe since it's only shown in midi clips"
/// (`midi/y_axis_to_cc1.h:22`) -- it casts unconditionally. The `relevance::is_midi_out` guard on
/// every node below says the same thing declaratively; this test is the belt to that brace, so a
/// hook driven from a spec or from E2's shortcut path cannot corrupt a CV instrument.
MIDIInstrument* midi_instrument(const MenuContext& ctx) {
	if (ctx.output == nullptr || ctx.output->type != OutputType::MIDI_OUT) {
		return nullptr;
	}
	return static_cast<MIDIInstrument*>(ctx.output);
}

/// @brief `getCurrentInstrument()->editedByUser = true`, the tail of the three MIDI toggles
///        (`y_axis_to_cc1.h:31`, `after_touch_to_mono.h:27`, `mpe_to_mono.h:27`).
void mark_edited(const MenuContext& ctx) {
	if (ctx.output != nullptr && ctx.output->type != OutputType::AUDIO) {
		static_cast<Instrument*>(ctx.output)->editedByUser = true;
	}
}

/// @brief The context's instrument clip, or null -- `getCurrentInstrumentClip()`.
InstrumentClip* instrument_clip(const MenuContext& ctx) {
	return (ctx.clip != nullptr && ctx.clip->type == ClipType::INSTRUMENT) ? static_cast<InstrumentClip*>(ctx.clip)
	                                                                       : nullptr;
}

/// @brief The context's audio clip, or null -- `getCurrentAudioClip()`.
AudioClip* audio_clip(const MenuContext& ctx) {
	return (ctx.clip != nullptr && ctx.clip->type == ClipType::AUDIO) ? static_cast<AudioClip*>(ctx.clip) : nullptr;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// The MIDI / CV root

const Node& midi_or_cv_root(const MenuContext& ctx) {
	// `sound_editor.cpp:1780-1788`: one object, two titles, chosen by output type. CV first because
	// a null output (which `setup()` cannot produce for this root, but a spec can) should read as
	// the MIDI spelling -- the constructor's own argument.
	return (ctx.output != nullptr && ctx.output->type == OutputType::CV) ? root_cv : root_midi_or_cv;
}

int32_t get_midi_hide_unlabeled(const MenuContext& ctx) {
	// `device_definition::HideUnlabeled::readCurrentValue` (`midi/device_definition/hide_unlabeled.h:32-35`).
	MIDIInstrument* instrument = midi_instrument(ctx);
	return instrument != nullptr && instrument->hideUnlabeledCC ? 1 : 0;
}
void set_midi_hide_unlabeled(const MenuContext& ctx, int32_t value) {
	// `hide_unlabeled.h:26-31`. Legacy also stores a copy in its own `t` member, which exists only
	// so `renderSubmenuItemTypeForOled` can draw the checkbox without a re-read; there is no such
	// member here, and the node's value comes back through `bindings::read`.
	if (MIDIInstrument* instrument = midi_instrument(ctx); instrument != nullptr) {
		deluge::edits::trackSettingFor(*instrument, "hideUnlabeledCC",
		                               deluge::serialization::Value::makeNumber(value != 0 ? 1 : 0));
	}
}

int32_t get_midi_bank(const MenuContext& ctx) {
	InstrumentClip* clip = instrument_clip(ctx); // `midi/bank.h:21`
	return clip != nullptr ? clip->midiBank : 0;
}
void set_midi_bank(const MenuContext& ctx, int32_t value) {
	if (InstrumentClip* clip = instrument_clip(ctx); clip != nullptr) { // `bank.h:22-28`
		deluge::edits::clipValueSettingFor(*clip, "midiBank", deluge::serialization::Value::makeNumber(value));
	}
}

int32_t get_midi_sub_bank(const MenuContext& ctx) {
	InstrumentClip* clip = instrument_clip(ctx); // `midi/sub.h:21`
	return clip != nullptr ? clip->midiSub : 0;
}
void set_midi_sub_bank(const MenuContext& ctx, int32_t value) {
	if (InstrumentClip* clip = instrument_clip(ctx); clip != nullptr) { // `sub.h:22-28`
		deluge::edits::clipValueSettingFor(*clip, "midiSub", deluge::serialization::Value::makeNumber(value));
	}
}

int32_t get_midi_pgm(const MenuContext& ctx) {
	InstrumentClip* clip = instrument_clip(ctx); // `midi/pgm.h:21`
	return clip != nullptr ? clip->midiPGM : 0;
}
void set_midi_pgm(const MenuContext& ctx, int32_t value) {
	if (InstrumentClip* clip = instrument_clip(ctx); clip != nullptr) { // `pgm.h:22-28`
		deluge::edits::clipValueSettingFor(*clip, "midiPGM", deluge::serialization::Value::makeNumber(value));
	}
}

int32_t get_cv2_source(const MenuContext& ctx) {
	// `cv::DualCVSelection::readCurrentValue` (`cv/cv2Mapping.h:30-38`): the stored `CVMode` with
	// `pitch` skipped, "until duophony works".
	if (ctx.output == nullptr || ctx.output->type != OutputType::CV) {
		return 0;
	}
	const auto mode = static_cast<int32_t>(static_cast<CVInstrument*>(ctx.output)->getCV2Mode());
	return mode != 0 ? mode - 1 : 0;
}
void set_cv2_source(const MenuContext& ctx, int32_t value) {
	if (ctx.output == nullptr || ctx.output->type != OutputType::CV) {
		return;
	}
	int32_t stored = value; // `cv2Mapping.h:30-38`
	if (stored != 0) {
		stored += 1;
	}
	deluge::edits::trackSettingFor(*ctx.output, "cv2Source", deluge::serialization::Value::makeNumber(stored));
}

int32_t get_mpe_y_to_mod_wheel(const MenuContext& ctx) {
	// `midi::MPEYToModWheel::readCurrentValue` (`midi/y_axis_to_cc1.h:30-33`).
	MIDIInstrument* instrument = midi_instrument(ctx);
	return instrument != nullptr && instrument->outputMPEY == CC_EXTERNAL_MOD_WHEEL ? 1 : 0;
}
void set_mpe_y_to_mod_wheel(const MenuContext& ctx, int32_t value) {
	if (MIDIInstrument* instrument = midi_instrument(ctx); instrument != nullptr) { // `y_axis_to_cc1.h:28-32`
		deluge::edits::trackSettingFor(*instrument, "outputMpeY",
		                               deluge::serialization::Value::makeNumber(static_cast<int32_t>(
		                                   value != 0 ? CC_EXTERNAL_MOD_WHEEL : CC_EXTERNAL_MPE_Y)));
		mark_edited(ctx);
	}
}

int32_t get_aftertouch_to_mono(const MenuContext& ctx) {
	MIDIInstrument* instrument = midi_instrument(ctx); // `midi/after_touch_to_mono.h:29`
	return instrument != nullptr && instrument->collapseAftertouch ? 1 : 0;
}
void set_aftertouch_to_mono(const MenuContext& ctx, int32_t value) {
	if (MIDIInstrument* instrument = midi_instrument(ctx); instrument != nullptr) { // `after_touch_to_mono.h:24-28`
		deluge::edits::trackSettingFor(*instrument, "collapseAftertouch",
		                               deluge::serialization::Value::makeNumber(value != 0 ? 1 : 0));
		mark_edited(ctx);
	}
}

int32_t get_mpe_to_mono(const MenuContext& ctx) {
	MIDIInstrument* instrument = midi_instrument(ctx); // `midi/mpe_to_mono.h:29`
	return instrument != nullptr && instrument->collapseMPE ? 1 : 0;
}
void set_mpe_to_mono(const MenuContext& ctx, int32_t value) {
	if (MIDIInstrument* instrument = midi_instrument(ctx); instrument != nullptr) { // `mpe_to_mono.h:24-28`
		deluge::edits::trackSettingFor(*instrument, "collapseMpe",
		                               deluge::serialization::Value::makeNumber(value != 0 ? 1 : 0));
		mark_edited(ctx);
	}
}

// ---------------------------------------------------------------------------------------------
// The MIDI / CV arpeggiator's own gate and rate
//
// `ArpNonSoundInteger`'s two derivatives store straight onto `ArpeggiatorSettings` rather than into
// a param collection, and neither sweeps the kit (`arpeggiator/midi_cv/gate.h:22-30`,
// `rate.h:22-30`), which is why these write the one `ctx.arp` and are not
// `apply_to_arp` sweeps.

int32_t get_arp_gate_midi(const MenuContext& ctx) {
	return ctx.arp != nullptr ? bindings::standard_to_menu(ctx.arp->gate) : 0; // `midi_cv/gate.h:30-32`
}
void set_arp_gate_midi(const MenuContext& ctx, int32_t value) {
	if (ctx.arp != nullptr) { // `midi_cv/gate.h:27-30`
		deluge::edits::arpSettingFor(*ctx.arp, "arpeggiator/gate",
		                             deluge::serialization::Value::makeNumber(bindings::menu_to_standard(value)));
	}
}

int32_t get_arp_rate_midi(const MenuContext& ctx) {
	return ctx.arp != nullptr ? bindings::standard_to_menu(ctx.arp->rate) : 0; // `midi_cv/rate.h:24-26`
}
void set_arp_rate_midi(const MenuContext& ctx, int32_t value) {
	if (ctx.arp != nullptr) { // `midi_cv/rate.h:27-30`
		deluge::edits::arpSettingFor(*ctx.arp, "arpeggiator/rate",
		                             deluge::serialization::Value::makeNumber(bindings::menu_to_standard(value)));
	}
}

// ---------------------------------------------------------------------------------------------
// The audio clip

int32_t get_audio_clip_transpose(const MenuContext& ctx) {
	// `audio_clip::Transpose::readCurrentValue` (`audio_clip/transpose.h:28-31`), over
	// `computeCurrentValueForTranspose` (`gui/menu_item/value_scaling.cpp:64-66`) -- the same
	// packing `sound_osc.cpp` transcribes for the four oscillator/modulator transposes.
	AudioClip* clip = audio_clip(ctx);
	if (clip == nullptr) {
		return 0;
	}
	return clip->sampleHolder.transpose * 100 + clip->sampleHolder.cents;
}
void set_audio_clip_transpose(const MenuContext& ctx, int32_t value) {
	AudioClip* clip = audio_clip(ctx);
	if (clip == nullptr) {
		return;
	}
	// `computeFinalValuesForTranspose` (`value_scaling.cpp:68-73`) then `transpose.h:25-33`.
	const int32_t current = value + 25600;
	const int32_t semitones = (current + 50) / 100;
	deluge::edits::audioClipSettingFor(*clip, "transpose", deluge::serialization::Value::makeNumber(semitones - 256));
	deluge::edits::audioClipSettingFor(*clip, "cents",
	                                   deluge::serialization::Value::makeNumber(current - semitones * 100));
}

int32_t get_audio_clip_reverse(const MenuContext& ctx) {
	AudioClip* clip = audio_clip(ctx); // `audio_clip/reverse.h:36`
	return clip != nullptr && clip->sampleControls.reversed ? 1 : 0;
}
void set_audio_clip_reverse(const MenuContext& ctx, int32_t value) {
	// `audio_clip::Reverse::writeCurrentValue` (`reverse.h`): the projection takes the voice off the sample, clamps
	// the end marker into a reversed sample and resumes a playing clip; the closing `uiNeedsRendering` is the dirty
	// raise below.
	AudioClip* clip = audio_clip(ctx);
	if (clip == nullptr) {
		return;
	}
	deluge::edits::audioClipSettingFor(*clip, "reversed", deluge::serialization::Value::makeNumber(value != 0 ? 1 : 0));
	if (clip->sampleHolder.audioFile != nullptr) {
		deluge::notify::raise(deluge::notify::Dirty::GridMain);
	}
}

int32_t get_audio_clip_interpolation(const MenuContext& ctx) {
	AudioClip* clip = audio_clip(ctx); // `audio_interpolation.h:14-17`
	return clip != nullptr ? static_cast<int32_t>(clip->sampleControls.interpolationMode) : 0;
}
void set_audio_clip_interpolation(const MenuContext& ctx, int32_t value) {
	if (AudioClip* clip = audio_clip(ctx); clip != nullptr) { // `audio_interpolation.h:20-23`
		deluge::edits::audioClipSettingFor(
		    *clip, "linearInterpolation",
		    deluge::serialization::Value::makeNumber(
		        static_cast<InterpolationMode>(value) == InterpolationMode::LINEAR ? 1 : 0));
	}
}

int32_t get_audio_clip_attack(const MenuContext& ctx) {
	AudioClip* clip = audio_clip(ctx); // `audio_clip/attack.h:22-24`
	return clip != nullptr ? bindings::standard_to_menu(clip->attack()) : 0;
}
void set_audio_clip_attack(const MenuContext& ctx, int32_t value) {
	// `audio_clip/attack.h:25-27` **verbatim**, including its bug: legacy's write calls
	// `computeCurrentValueForStandardMenuItem` where every other item calls
	// `computeFinalValueForStandardMenuItem`, so the stored `attack` is a menu-scale number rather
	// than an INT32-scale one and the item does not round-trip. Transcribed as-is rather than
	// "fixed": E1's contract is that the port behaves as legacy does, and `tables_3_spec`'s
	// write-coverage compares the two on the same fixture. Worth an issue of its own.
	if (AudioClip* clip = audio_clip(ctx); clip != nullptr) {
		deluge::edits::audioClipSettingFor(*clip, "attack",
		                                   deluge::serialization::Value::makeNumber(bindings::standard_to_menu(value)));
	}
}

// ---------------------------------------------------------------------------------------------
// The song root

int32_t get_swing_interval(const MenuContext& ctx) {
	return ctx.song != nullptr ? static_cast<int32_t>(ctx.song->project.groove.swing_interval)
	                           : 0; // `swing/interval.h`
}
void set_swing_interval(const MenuContext& ctx, int32_t value) {
	if (ctx.song != nullptr) {
		deluge::edits::songSettingFor("swingInterval",
		                              deluge::serialization::Value::makeNumber(value)); // `swing/interval.h`
	}
}

int32_t get_song_threshold_recording(const MenuContext& ctx) {
	// `record::ThresholdMode::readCurrentValue`'s SONG arm (`record/threshold_mode.h:29-31`).
	return ctx.song != nullptr ? static_cast<int32_t>(ctx.song->thresholdRecordingMode) : 0;
}
void set_song_threshold_recording(const MenuContext& ctx, int32_t value) {
	if (ctx.song != nullptr) { // `threshold_mode.h:46-48`
		ctx.song->thresholdRecordingMode = static_cast<ThresholdRecordingMode>(value);
	}
}

// ---------------------------------------------------------------------------------------------
// Stem export
//
// Seven `ToggleBool`s over `stemExport`'s own members (`menus.cpp:2020-2035`); `StemExport` lives
// in `processing/stem_export/`, inside this layer's allow-list, so each is a direct read and write.

#define DELUGE_STEM_TOGGLE(fn, member)                                                                                 \
	int32_t get_stem_##fn(const MenuContext&) {                                                                        \
		return stemExport.member ? 1 : 0;                                                                              \
	}                                                                                                                  \
	void set_stem_##fn(const MenuContext&, int32_t value) {                                                            \
		stemExport.member = value != 0;                                                                                \
	}

DELUGE_STEM_TOGGLE(normalization, allowNormalization)
DELUGE_STEM_TOGGLE(normalization_drums, allowNormalizationForDrums)
DELUGE_STEM_TOGGLE(silence, exportToSilence)
DELUGE_STEM_TOGGLE(song_fx, includeSongFX)
DELUGE_STEM_TOGGLE(kit_fx, includeKitFX)
DELUGE_STEM_TOGGLE(offline, renderOffline)
DELUGE_STEM_TOGGLE(mixdown, exportMixdown)

#undef DELUGE_STEM_TOGGLE

// ---------------------------------------------------------------------------------------------

// (Fix round 1, minor 7: `kBatch3OtherRoots` was exported here and referenced by nothing -- a second
// hand-maintained root list that could drift from `nodes.h`'s `kAllRoots`, which is the one list
// `MenuScene::roots()`, `validate_tree` and Task 10's dump all take. Deleted.)

} // namespace deluge::scenes::menu::nodes
