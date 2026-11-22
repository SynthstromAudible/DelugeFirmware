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
///     `uiNeedsRendering(&audioClipView, ...)` (`origin/main:src/deluge/gui/menu_item/audio_clip/reverse.h`) and the
///     MIDI-instrument toggles end in `getCurrentInstrument()->editedByUser = true` -- the latter IS model state and is
///     transcribed; the former is a `hid/`-side repaint that `bindings::write` already replaces with
///     `notify::Dirty::Oled` plus, for a param, `GridMain`. A `set` hook that wants a pad repaint raises
///     `Dirty::GridMain` itself, which the two audio-clip sample items do.
///   * **`getCurrent*()` globals.** Legacy reaches `getCurrentOutput()`, `getCurrentInstrument()`,
///     `getCurrentInstrumentClip()` and `getCurrentAudioClip()`; every one of those is
///     an address in `MenuContext::addresses` here, and no `currentSong` reference is left in this file.

#include "ui/scenes/menu/nodes/roots.h"

#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"

#include "base/notify/dirty_set.h"
#include "plugin/types/display_mapping.h"
#include "project/song.h"

namespace deluge::ui::scenes::menu::nodes {

// ---------------------------------------------------------------------------------------------
// The MIDI / CV root

const Node& midi_or_cv_root(const MenuContext& ctx) {
	// `SoundEditor::setup` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): one object, two titles, chosen by output
	// type. CV first because a null output (which `setup()` cannot produce for this root, but a spec can) should read
	// as the MIDI spelling -- the constructor's own argument.
	return ctx.addresses.instrument == deluge::plugin::SettingContext::Instrument::CV ? root_cv : root_midi_or_cv;
}

// ---------------------------------------------------------------------------------------------
// The audio clip

// ---------------------------------------------------------------------------------------------
// The song root

int32_t get_swing_interval(const MenuContext& ctx) {
	// `origin/main:src/deluge/gui/menu_item/swing/interval.h`
	return ctx.model != nullptr ? static_cast<int32_t>(ctx.model->groove.swing_interval) : 0;
}
std::optional<int32_t> swing_interval_turn(const MenuContext&, int32_t value, int32_t offset) {
	return deluge::plugin::wrap_swing_interval(value + offset);
}
void set_swing_interval(const MenuContext& ctx, int32_t value) {
	bindings::write_song(ctx, "swingInterval", value); // `origin/main:src/deluge/gui/menu_item/swing/interval.h`
}

int32_t get_song_threshold_recording(const MenuContext& ctx) {
	// `record::ThresholdMode::readCurrentValue`'s SONG arm
	// (`origin/main:src/deluge/gui/menu_item/record/threshold_mode.h`).
	return ctx.ports.audio != nullptr ? ctx.ports.audio->song_threshold_recording() : 0;
}
void set_song_threshold_recording(const MenuContext& ctx, int32_t value) {
	// `ThresholdMode::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/record/threshold_mode.h`)
	if (ctx.ports.writes != nullptr) {
		ctx.ports.writes->set_song_threshold_recording(value);
	}
}

// ---------------------------------------------------------------------------------------------

// This file exports no root list of its own: `nodes.h`'s `kAllRoots` is the one list
// `MenuScene::roots()`, `validate_tree` and the menu dump all take, so a second hand-maintained
// list could only drift from it.

} // namespace deluge::ui::scenes::menu::nodes
