#pragma once

/// @file
/// @brief The three `CustomItem`s behind the nine menu items that open a card-backed scene: the two
///        oscillator file browsers, the four sample loop points plus the audio clip's waveform view,
///        and the two record-to-sample items.
///
// clang-format off
/// | node | legacy item | legacy `beginSession` opens | scene |
/// |---|---|---|---|
/// | `file0_selector` / `file1_selector` | `menu_item::FileSelector` (`origin/main:src/deluge/gui/menu_item/file_selector.h`) | `sampleBrowser` (`origin/main:src/deluge/gui/menu_item/file_selector.cpp` `beginSession`) | `browser::BrowserScene`, `BrowserId::Sample` |
/// | `sample0_start` / `sample1_start` | `sample::Start` (`origin/main:src/deluge/gui/menu_item/sample/start.h`) | `sampleMarkerEditor`, `markerType = START` (`origin/main:src/deluge/gui/menu_item/sample/loop_point.cpp` `beginSession`) | `marker::MarkerScene`, `project::SampleMarker::START` |
/// | `sample0_end` / `sample1_end` | `sample::End` (`origin/main:src/deluge/gui/menu_item/sample/end.h`) | `sampleMarkerEditor`, `markerType = END` (set in its constructor) | `marker::MarkerScene`, `project::SampleMarker::END` |
/// | `audio_clip_waveform` | `audio_clip::SampleMarkerEditor` (`origin/main:src/deluge/gui/menu_item/audio_clip/sample_marker_editor.h`) | `sampleMarkerEditor`, `markerType = whichMarker` (`origin/main:src/deluge/gui/menu_item/audio_clip/sample_marker_editor.cpp`, `beginSession`) | `marker::MarkerScene`, `project::SampleMarker::END` |
/// | `sample0_recorder` / `sample1_recorder` | `osc::AudioRecorder` (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`) | `audioRecorder` (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h` `beginSession`) | `recorder::RecorderScene` |
// clang-format on
///
/// All five legacy classes are the same shape, and it is `EditNameItem`'s (`nodes/edit_name.h`) and `ContextMenuItem`'s
/// (`nodes/context_menu_items.h`): a `MenuItem` whose entire body is a `beginSession` override that points a UI at
/// something and calls `openUI()` on it. `CustomItem::on_focus` IS `beginSession` (`node.h`), so that is where these do
/// the same thing -- except that a scene may not touch the `Shell` stack itself, so instead of opening a UI each
/// records the push in `MenuWalkState::pending_scene` and `MenuScene::after_render` turns it into an
/// `Action::Kind::Push`. `MenuWalkState::go_up_on_resume` is set alongside it, which is legacy's
/// `soundEditor.shouldGoUpOneLevelOnBegin = true` -- set by all five (`FileSelector::beginSession` in
/// `origin/main:src/deluge/gui/menu_item/file_selector.cpp`, `LoopPoint::beginSession` in
/// `origin/main:src/deluge/gui/menu_item/sample/loop_point.cpp`, `AudioRecorder::beginSession` in
/// `origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`,
/// `origin/main:src/deluge/gui/menu_item/audio_clip/sample_marker_editor.cpp`).
///
/// @note **Each opener also applies `menu::select_source`, legacy's `setCurrentSource`.**
///       Three of the five legacy classes follow `shouldGoUpOneLevelOnBegin` with
///       `soundEditor.setCurrentSource(sourceId_)`, because a legacy per-source item carries its own
///       `source_id_` and the `SoundEditor` globals it is about to hand to another UI still name
///       whichever source `setup()` resolved. The port has the identical problem -- nothing
///       re-resolves `MenuContext` on the way into `source1`'s page (`relevance.h`'s note) -- and
///       `menu::select_source` (`menu/context.h`) is that statement. Each item applies it to its own
///       **copy** of the context and hands the copy to the scene, which is what `Scene::open` takes.
///       `audio_clip::SampleMarkerEditor` is the one opener with no such call, and it is the one
///       item here built with a negative source index.
///
/// @note **No refusal is spelled here, deliberately.** Legacy refuses in `checkPermissionToBeginSession`, which
///       `SoundEditor::setup` consults *before* navigating; the port splits that in two. The range half is
///       `menu::permission_for` and the walk's range gate (`MenuWalk::enter_range_picker_if_needed`,
///       `MenuScene::on_enter`'s deep link), which make the first range and refuse with INSUFFICIENT_RAM before the
///       item is focused, so `on_focus` never meets a source without one. They ask about the item's own source
///       (`CustomItem::range_source`, `menu::range_rule_context`), as `main`'s overrides pass their own `source_id_`:
///       REC 2 under oscillator 1's context is about oscillator 2. `Node::range_specific` is carried by all but
///       `audio_clip_waveform` -- exactly as legacy, whose `audio_clip::SampleMarkerEditor` does not route through
///       `checkPermissionToBeginSessionForRangeSpecificParam`. The rest -- `LoopPoint`'s "make sure a Sample is loaded"
///       (`LoopPoint::checkPermissionToBeginSession` in `origin/main:src/deluge/gui/menu_item/sample/loop_point.cpp`),
///       `audio_clip::SampleMarkerEditor`'s null `sampleHolder.audioFile` (`SampleMarkerEditor::isRelevant` in
///       `origin/main:src/deluge/gui/menu_item/audio_clip/sample_marker_editor.cpp`), `AudioRecorder`'s "already
///       recording" (`AudioRecorder::opened`) -- is each scene's own `can()`, consulted by its `on_enter`. A refused
///       push is not a lost frame: `SceneStack::push` (`ui/shell/scene_stack.cpp`) unwinds and resumes the input top,
///       which is the menu whenever the refused scene is an opaque one (all of these are) -- and that is where
///       `go_up_on_resume` steps back out of the item, the same place legacy's failed `openUI` leaves it. This is
///       `EditNameItem`'s shipped behaviour, not a new rule.

#include "ui/scenes/menu/node.h"

#include <cstdint>

namespace deluge::ui::scenes::menu {
struct MenuContext;
} // namespace deluge::ui::scenes::menu

namespace deluge::ui::scenes::menu::nodes {

/// @brief What the three items share: the one-tick render, the no-op `handle`, and the source index
///        `on_focus` re-points the context at.
///
/// Not a convenience base -- it is the whole of two of legacy's five classes minus their
/// `beginSession`. The derived classes add exactly the `openUI` target.
class SampleSceneItem : public CustomItem {
public:
	/// @brief Which of `Sound::sourceResources` this item is about, or negative for an item with no source
	///        (the audio clip's waveform view). Exposed so a spec can assert the item-to-source
	///        mapping by identity rather than re-deriving it.
	int8_t source_index() const { return source_index_; }
	int8_t range_source() const final { return source_index_; }

	void render(deluge::ui::widgets::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const final;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const final;

protected:
	explicit SampleSceneItem(int8_t source_index) : source_index_{source_index} {}

	/// @brief @p ctx with `menu::select_source` applied -- legacy's
	///        `soundEditor.setCurrentSource(sourceId_)` against a copy rather than a global.
	MenuContext with_source(const MenuContext& ctx) const;

private:
	int8_t source_index_;
};

/// @brief `menu_item::FileSelector` -- opens the sample browser on this item's source.
class FileBrowserItem final : public SampleSceneItem {
public:
	explicit FileBrowserItem(int8_t source_index) : SampleSceneItem{source_index} {}
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
};

/// @brief `sample::Start` / `sample::End` / `audio_clip::SampleMarkerEditor` -- opens the sample
///        marker editor on this item's marker, and (where it has one) its source.
///
/// One class for two legacy ones. `sample::LoopPoint` and `audio_clip::SampleMarkerEditor` differ in
/// exactly two respects: the audio-clip opener does not call `setCurrentSource` (it has no source),
/// and it reads its marker from `whichMarker` rather than `markerType`. Both are carried here as
/// constructor arguments -- a negative source index, and the marker -- rather than as two classes
/// whose bodies would be the same statement.
class MarkerItem final : public SampleSceneItem {
public:
	/// @param source_index Which source, or **negative** for the audio-clip opener, which selects
	///                     none.
	/// @param marker       The `project::SampleMarker` to open on, as the plain `uint16_t` a `SceneId` carries.
	///                     Spelled as a number rather than the enum so the marker word stays out of
	///                     every table that names an item --
	///                     the same trade `EditNameItem::target_` and `ContextMenuItem::
	///                     descriptor_id_` make, and pinned by `static_assert` in `nodes/sample.cpp`.
	MarkerItem(int8_t source_index, uint16_t marker) : SampleSceneItem{source_index}, marker_{marker} {}

	/// @brief Which marker this item opens on. Exposed for the same reason `source_index()` is.
	uint16_t marker() const { return marker_; }

	void on_focus(const MenuContext& ctx, MenuState& state) const override;

private:
	uint16_t marker_;
};

/// @brief `osc::AudioRecorder` -- opens the audio recorder on this item's source.
class AudioRecorderItem final : public SampleSceneItem {
public:
	explicit AudioRecorderItem(int8_t source_index) : SampleSceneItem{source_index} {}
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
};

/// @name The nine instances.
///
/// `inline const`, not `constexpr` (they have a vtable); only their addresses need to be constant
/// expressions, which is all `custom()` needs -- exactly the spelling `nodes::kPending` uses.
///
/// Nine instances rather than three parameterised by the node, for `MidiCommandItem`'s reason
/// (`nodes/midi_command.h`): `on_focus` is handed a `MenuContext` and a `MenuState`, never its own
/// `Node`, so an item shared across the sources could not tell which source it was being focused as.
/// @{
inline const FileBrowserItem kFile0Selector{0};
inline const FileBrowserItem kFile1Selector{1};

inline const MarkerItem kSample0Start{0, 0}; ///< `project::SampleMarker::START`
inline const MarkerItem kSample1Start{1, 0}; ///< `project::SampleMarker::START`
inline const MarkerItem kSample0End{0, 3};   ///< `project::SampleMarker::END`
inline const MarkerItem kSample1End{1, 3};   ///< `project::SampleMarker::END`
/// `audioClipSampleMarkerEditorMenuEnd` -- the **END** marker, and the only one of the pair the audio-clip root shows
/// (`audioClipSampleMenu` in `origin/main:src/deluge/gui/ui/menus.cpp`). Its sibling
/// `audioClipSampleMarkerEditorMenuStart` is reachable only from the V1 grid shortcut table (`setShortcutsVersion` in
/// `origin/main:src/deluge/gui/ui/sound_editor.cpp`), which is `menu/shortcuts.h`'s business and not a node in any
/// submenu.
inline const MarkerItem kAudioClipWaveform{-1, 3};
/// `audioClipSampleMarkerEditorMenuStart`: the **START** marker of the audio clip's waveform.
inline const MarkerItem kAudioClipWaveformStart{-1, 0};

inline const AudioRecorderItem kSample0Recorder{0};
inline const AudioRecorderItem kSample1Recorder{1};
/// @}

} // namespace deluge::ui::scenes::menu::nodes
