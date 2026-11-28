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
/// @brief The audio clip view's one instance, and the half of its focus the scene cannot take.

#include "gui/views/audio_clip_root.h"
#include "ui/scenes/audio_clip/audio_clip_scene.h"

#include "board_config.h" // PLACE_SDRAM_BSS
#include "gui/ui/ui.h"
#include "gui/views/timeline_view.h"
#include "gui/views/view.h"
#include "hid/led/indicator_leds.h"
#include "model/clip/clip.h"
#include "model/song/song.h"

namespace {

/// @brief `AudioClipView::opened` and `focusRegained`'s statements on the shared legacy state: the clip is off the
///        automation view, the shared `View` takes its focus and puts the knobs on the clip, and the TRIPLETS LED is
///        off, which `View::focusRegained` sets only for a `TimelineView` (an audio clip is never in triplets view).
void audioClipFocusRegained() {
	::Clip* clip = getCurrentClip();
	if (clip != nullptr) {
		clip->onAutomationClipView = false;
	}
	view.focusRegained();
	indicator_leds::setLedState(IndicatorLED::TRIPLETS, false);
	view.setActiveModControllableTimelineCounter(clip);
}

} // namespace

PLACE_SDRAM_BSS deluge::gui::ui::ShellRootView<deluge::ui::scenes::audio_clip::AudioClipScene> audioClipView{
    &deluge::ui::scenes::audio_clip::audio_clip_scene, deluge::ui::scenes::audio_clip::kAudioClipSceneKind,
    deluge::gui::ui::RootViewTraits{
        .type = UIType::AUDIO_CLIP, .knobs = UIModControllableContext::CLIP, .clip_minder = true, .triplets = false},
    &audioClipFocusRegained};

void undoClipScroll(int32_t scroll) {
	if (TimelineView* timeline = getCurrentUI()->toTimelineView(); timeline != nullptr) {
		timeline->initiateXScroll(scroll);
	}
	else if (getCurrentUI() == &audioClipView) {
		currentSong->persistentView.xScroll[NAVIGATION_CLIP] = scroll;
		audioClipView.navigationRestored();
	}
}

void undoClipZoom(int32_t zoomMagnitude, int32_t scroll, uint32_t zoomBefore) {
	if (TimelineView* timeline = getCurrentUI()->toTimelineView(); timeline != nullptr) {
		timeline->initiateXZoom(zoomMagnitude, scroll, zoomBefore);
	}
	else if (getCurrentUI() == &audioClipView) {
		currentSong->persistentView.xScroll[NAVIGATION_CLIP] = scroll;
		audioClipView.navigationRestored();
	}
}
