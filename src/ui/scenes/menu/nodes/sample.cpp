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
/// @brief The members of the three sample items. See `nodes/sample.h`.

#include "ui/scenes/menu/nodes/sample.h"

#include "ui/scenes/browser/browser_scene.h" // FileBrowserItem::on_focus opens this
#include "ui/scenes/marker/marker_scene.h"   // MarkerItem::on_focus opens this
#include "ui/scenes/menu/chains.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/nodes/sound_osc.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/recorder/recorder_scene.h" // AudioRecorderItem::on_focus opens this

#include "project/sample_marker.h"

#include "ui/shell/profile.h"
#include "ui/widgets/oled/text.h"
#include <utility>

namespace deluge::ui::scenes::menu::nodes {

namespace {

// `nodes/sample.h` builds the five `MarkerItem`s from raw `uint16_t` literals: the `Node` tables are
// `inline constexpr` in headers every table includes, and a literal keeps the marker word out of all of them. Those
// literals are only correct while `project::SampleMarker` keeps its numbering, so the numbering is pinned here --
// exactly what `context_menu_items.cpp` does for `DescriptorId`. Renumber the enum and this fires, instead of five menu
// items silently opening on the wrong marker.
static_assert(std::to_underlying(deluge::project::SampleMarker::START) == 0);
static_assert(std::to_underlying(deluge::project::SampleMarker::END) == 3);

/// @brief The `BrowserId` the two file selectors open, as the `uint16_t` a `SceneId::arg` carries.
constexpr uint16_t kSampleBrowserArg = static_cast<uint16_t>(browser::BrowserId::Sample);

} // namespace

void SampleSceneItem::render(deluge::ui::widgets::oled::OledFrame& f, const MenuContext& /*ctx*/,
                             MenuState& state) const {
	// Drawn for at most the one tick between `on_focus` asking for the push and the router applying
	// it, so what matters is only that it is never a blank frame -- `EditNameItem::render`'s own
	// reasoning (`nodes/edit_name.cpp`). Legacy never draws any of these five items: `beginSession`
	// opens the browser / marker editor / recorder before `SoundEditor` renders anything.
	const Node* node = state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
	deluge::ui::shell::TextScratch scratch;
	deluge::ui::widgets::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch)
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
	// `FileSelector::beginSession` (`origin/main:src/deluge/gui/menu_item/file_selector.cpp`): set the source, then
	// `openUI(&sampleBrowser)`. `BrowserScene::open` is legacy's implicit "the browser reads
	// `soundEditor`'s globals" -- generalised, so the descriptor reads whichever member of the
	// context it needs (`browser_scene.h`'s `Descriptor::start_at`).
	//
	// Main's `keyboardScreen.exitAuditionMode()`, with the keyboard at the root and a note auditioning, is a root
	// UI's: the view actions port ends it before the browser opens, as main's runs before its `openUI`.
	// `uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK)`, which main runs only when the browser refuses to open,
	// is the shortcut-pad flash, which `MenuScene` owns rather than an item.
	//
	// Nor is `sampleBrowser.menuItemHeadingTo`/`parentMenuHeadingTo`, which legacy writes not here but in
	// `SoundEditor::maybeGetParentMenu` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), in its file-selector arm.
	// It is a *return* affordance: the sample claim (`SampleClaim`) reads the pair back
	// (`SampleBrowser::claimCurrentFile` in `origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`) and re-roots
	// the navigation record onto the oscillator's horizontal page with the file selector focused, so accepting a file
	// lands on the source page rather than back on the item. `MenuState::path`/`depth` already are that record and
	// `go_up_on_resume` already steps out of the item, so what was actually missing was the *re-rooting* -- and it
	// belonged to whoever performs `claimCurrentFile` for the browser (`BrowserHost::perform`), which is the same seam
	// and the same owner (`browser_scene.h`'s `Descriptor::accept`). This item's half of the pair is
	// `MenuWalkState::scene_opener`, recorded by `MenuScene::after_render` where legacy's `maybeGetParentMenu` records
	// its own; the page half is resolved from the tables and the re-rooting applied by
	// `MenuScene::reroot_to_scene_opener_page()`, which `deluge::gui::ui::shellSampleClaimDone` calls on the claim's
	// success.
	if (auto* v = ctx.ports.view_actions) {
		v->end_audition(bind::AuditionEnd::KeyboardWhenAuditioning);
	}
	browser::browser_scene().open(with_source(ctx));
	state.walk.pending_scene = deluge::ui::shell::SceneId{browser::kBrowserSceneKind, kSampleBrowserArg};
}

void MarkerItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `LoopPoint::beginSession` (a sound's markers; the audio clip's opener has no such arm): a keyboard audition ends.
	if (source_index() >= 0) {
		if (auto* v = ctx.ports.view_actions) {
			v->end_audition(bind::AuditionEnd::KeyboardWhenAuditioning);
		}
	}
	// `LoopPoint::beginSession` (`origin/main:src/deluge/gui/menu_item/sample/loop_point.cpp`) and
	// `audio_clip::SampleMarkerEditor::beginSession`
	// (`origin/main:src/deluge/gui/menu_item/audio_clip/sample_marker_editor.cpp`). Both end in
	// `sampleMarkerEditor.markerType = <the item's marker>; openUI(&sampleMarkerEditor)`; the marker
	// travels on `SceneId::arg` here instead of through a member the caller has to remember to write
	// (`MarkerScene::on_enter`).
	//
	// `with_source` is `LoopPoint`'s `setCurrentSource` call
	// (`origin/main:src/deluge/gui/menu_item/sample/loop_point.cpp`) and, for `kAudioClipWaveform`'s negative index,
	// the audio-clip opener's *absence* of one.
	marker::marker_scene().open(with_source(ctx));
	state.walk.pending_scene = deluge::ui::shell::SceneId{marker::kMarkerSceneKind, marker_};
}

void AudioRecorderItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `osc::AudioRecorder::beginSession`: set the source, then `openUI(&audioRecorder)`. `SceneId::arg` is unused --
	// there is exactly one recorder screen and `RecorderScene::on_enter` ignores it -- and legacy's app-loop job
	// (`AudioRecorder::slowRoutine`) is the shell's own tick (`recorder_scene.h`).
	//
	// The source's first range is already there: `osc::AudioRecorder::checkPermissionToBeginSession`
	// (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`) makes it, on the item's own source, before the item
	// is entered, and the walk's range gate is that check (`range_rule_context`).
	recorder::recorder_scene().open(with_source(ctx));
	state.walk.pending_scene = deluge::ui::shell::SceneId{recorder::kRecorderSceneKind, 0};
	// `beginSession`'s heading-pair arm. `SoundEditor::maybeGetParentMenu`
	// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) gives a recorder on the chain's SAMPLE RECORDER page its
	// source's file selector on `sourceMenuGroup`, and `beginSession` re-roots there before it opens the recorder, so
	// the take returns to the source page with the selector focused. A recorder opened on the item alone (the drum
	// creator's take) has no page above it, and the menu goes up as usual.
	if (state.depth > 0 && state.path[state.depth - 1] == &nodes::recorder
	    && renders_horizontal(nodes::recorder, ctx)) {
		state.walk.resume_landing = source_index() == 0 ? &nodes::file0_selector : &nodes::file1_selector;
	}
}

} // namespace deluge::ui::scenes::menu::nodes
