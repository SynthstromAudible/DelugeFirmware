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
/// @brief The members of the three sample items. See `sample.h`.

#include "scenes/menu/nodes/sample.h"

#include "scenes/browser/browser_scene.h" // FileBrowserItem::on_focus opens this
#include "scenes/marker/marker_scene.h"   // MarkerItem::on_focus opens this
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/profile.h"
#include "scenes/recorder/recorder_scene.h" // AudioRecorderItem::on_focus opens this

#include "definitions_cxx.hpp" // MarkerType, for the numbering static_asserts below
#include "shell/profile.h"
#include "ui/oled/text.h"

namespace deluge::scenes::menu::nodes {

namespace {

// `sample.h` builds the five `MarkerItem`s from raw `uint16_t` literals, because including
// `definitions_cxx.hpp`'s `MarkerType` there would put it in the include closure of every table
// that names one of these items. Those literals are only correct while `MarkerType` keeps its
// current numbering, so the numbering is pinned here, in the translation unit that already sees
// both -- exactly what `context_menu_items.cpp` does for `DescriptorId`. Renumber the enum and this
// fires, instead of five menu items silently opening on the wrong marker.
static_assert(static_cast<int16_t>(MarkerType::START) == 0);
static_assert(static_cast<int16_t>(MarkerType::END) == 3);

/// @brief The `BrowserId` the two file selectors open, as the `uint16_t` a `SceneId::arg` carries.
constexpr uint16_t kSampleBrowserArg = static_cast<uint16_t>(browser::BrowserId::Sample);

} // namespace

void SampleSceneItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	// Drawn for at most the one tick between `on_focus` asking for the push and the router applying
	// it, so what matters is only that it is never a blank frame -- `EditNameItem::render`'s own
	// reasoning (`nodes/edit_name.cpp`). Legacy never draws any of these five items: `beginSession`
	// opens the browser / marker editor / recorder before `SoundEditor` renders anything.
	const Node* node = state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
	deluge::ui::shell::TextScratch scratch;
	deluge::ui::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch)
	                                           : std::string_view{});
}

deluge::ui::shell::Result SampleSceneItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                  const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                  const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	// The scene this item opened owns every input while it is up, and the item is only ever focused
	// for the tick before it appears. Nothing to consume.
	return {};
}

MenuContext SampleSceneItem::with_source(const MenuContext& ctx) const {
	MenuContext copy = ctx;
	select_source(copy, source_index_);
	return copy;
}

void FileBrowserItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `FileSelector::beginSession` (`gui/menu_item/file_selector.cpp:36-47`): set the source, then
	// `openUI(&sampleBrowser)`. `BrowserScene::open` is legacy's implicit "the browser reads
	// `soundEditor`'s globals" -- generalised, so the descriptor reads whichever member of the
	// context it needs (`browser_scene.h`'s `Descriptor::start_at`).
	//
	// Not ported, and stated rather than dropped silently: legacy's
	// `keyboardScreen.exitAuditionMode()` (`:40-42`) reaches a `gui/` root UI this layer may not
	// name (`scenes-no-root-ui`) -- the same input `MarkerScene` drops for the same reason
	// (`marker_scene.h`) -- and `uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK)` (`:45`) is
	// the shortcut-pad flash, which `MenuScene` owns rather than an item.
	//
	// Nor is `sampleBrowser.menuItemHeadingTo`/`parentMenuHeadingTo`, which legacy writes not here
	// but in `SoundEditor::maybeGetParentMenu` (`gui/ui/sound_editor.cpp:2073 maybeGetParentMenu`),
	// whose file-selector arm is `:2034-2039`.
	// It is a *return* affordance: `SampleBrowser::claimCurrentFile` reads the pair back
	// (`gui/ui/browser/sample_browser.cpp:1111-1122`) and re-roots the navigation record onto the
	// oscillator's horizontal page with the file selector focused, so accepting a file lands on the
	// source page rather than back on the item. `MenuState::path`/`depth` already are that record
	// and `go_up_on_resume` already steps out of the item, so what is actually missing is the
	// *re-rooting* -- and it belongs to whoever performs `claimCurrentFile` against
	// `BrowserScene::take_choice()`, which is the same seam and the same owner
	// (`browser_scene.h`'s `Descriptor::accept`).
	browser::browser_scene().open(with_source(ctx));
	state.pending_scene = deluge::ui::shell::SceneId{browser::kBrowserSceneKind, kSampleBrowserArg};
}

void MarkerItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `LoopPoint::beginSession` (`gui/menu_item/sample/loop_point.cpp:60-74`) and
	// `audio_clip::SampleMarkerEditor::beginSession`
	// (`gui/menu_item/audio_clip/sample_marker_editor.cpp:43-51`). Both end in
	// `sampleMarkerEditor.markerType = <the item's marker>; openUI(&sampleMarkerEditor)`; the marker
	// travels on `SceneId::arg` here instead of through a member the caller has to remember to write
	// (`MarkerScene::on_enter`).
	//
	// `with_source` is `LoopPoint`'s `setCurrentSource` (`:68`) and, for `kAudioClipWaveform`'s
	// negative index, the audio-clip opener's *absence* of one.
	marker::marker_scene().open(with_source(ctx));
	state.pending_scene = deluge::ui::shell::SceneId{marker::kMarkerSceneKind, marker_};
}

void AudioRecorderItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `osc::AudioRecorder::beginSession` (`gui/menu_item/osc/audio_recorder.h:36-58`): set the
	// source, then `openUI(&audioRecorder)`. **`SceneId::arg` is unused** -- there is exactly one
	// recorder screen and `RecorderScene::on_enter` ignores it (task-5 report §9).
	//
	// Legacy's `parentMenuHeadingTo` / `menuItemHeadingTo` arm (`:40-48`) has no port, and the same
	// affordance is missing from `FileBrowserItem` above for the same reason. The pair is written by
	// `SoundEditor::maybeGetParentMenu` (`gui/ui/sound_editor.cpp:2073 maybeGetParentMenu`), whose
	// recorder arm (`:2042-2047`) points a recorder at the matching file selector; this arm then
	// re-roots `menuItemNavigationRecord` onto the source page with that selector focused and clears
	// `shouldGoUpOneLevelOnBegin`, so a finished recording lands on the source page rather than one
	// level out from the recorder. `MenuState::path`/`depth` already are that record, but re-rooting
	// it is `MenuScene`'s to do, not an item's -- an item writing the menu's own navigation state is
	// exactly what `MenuState`'s mailbox contract (`menu_scene.h`) exists to avoid. Recorded rather
	// than half-built; the consequence is one extra BACK press after a recording.
	//
	// `process()` is not called either, and that is the whole of task 5: legacy runs the recorder
	// as a blocking loop from this stack frame (`gui/ui/audio_recorder.cpp:193-242`), while the
	// scene is driven by the shell's own tick (`recorder_scene.h` §1).
	recorder::recorder_scene().open(with_source(ctx));
	state.pending_scene = deluge::ui::shell::SceneId{recorder::kRecorderSceneKind, 0};
}

} // namespace deluge::scenes::menu::nodes
