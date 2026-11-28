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

#pragma once

/// @file
/// @brief The audio clip view: `AudioClipScene` as the legacy root view `audioClipView`, hosted on the shared shell.

#include "gui/ui/shell_root_view.h"

namespace deluge::ui::scenes::audio_clip {
class AudioClipScene;
}

/// @brief The audio clip view, which legacy code roots (`changeRootUI(&audioClipView)`) and compares against.
extern deluge::gui::ui::ShellRootView<deluge::ui::scenes::audio_clip::AudioClipScene> audioClipView;

/// @brief Undo's restored clip scroll, on the current UI: a timeline view animates to it (`initiateXScroll`); the audio
///        clip view's scene, which is no timeline view, slides to it.
/// @param scroll The restored scroll, in ticks.
void undoClipScroll(int32_t scroll);

/// @brief Undo's restored clip zoom, the song's already, and its scroll, on the current UI, as `undoClipScroll` does
///        it (`initiateXZoom`).
/// @param zoomMagnitude The zoom's change, as `initiateXZoom` takes it. @param scroll The restored scroll, in ticks.
/// @param zoomBefore The zoom before the undo.
void undoClipZoom(int32_t zoomMagnitude, int32_t scroll, uint32_t zoomBefore);
