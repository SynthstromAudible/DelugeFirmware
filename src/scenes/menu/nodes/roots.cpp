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
///     an address in `MenuContext::addresses` here, and no `currentSong` reference is left in this file.

#include "scenes/menu/nodes/roots.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "definitions_cxx.hpp"
#include "deluge-project/song.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
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

// ---------------------------------------------------------------------------------------------
// The MIDI / CV root

const Node& midi_or_cv_root(const MenuContext& ctx) {
	// `sound_editor.cpp:1780-1788`: one object, two titles, chosen by output type. CV first because
	// a null output (which `setup()` cannot produce for this root, but a spec can) should read as
	// the MIDI spelling -- the constructor's own argument.
	return ctx.addresses.instrument == deluge::engine::SettingContext::Instrument::CV ? root_cv : root_midi_or_cv;
}

// ---------------------------------------------------------------------------------------------
// The audio clip

// ---------------------------------------------------------------------------------------------
// The song root

int32_t get_swing_interval(const MenuContext& ctx) {
	return ctx.model != nullptr ? static_cast<int32_t>(ctx.model->groove.swing_interval) : 0; // `swing/interval.h`
}
void set_swing_interval(const MenuContext& ctx, int32_t value) {
	bindings::write_song(ctx, "swingInterval", value); // `swing/interval.h`
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
