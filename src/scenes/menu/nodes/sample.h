#pragma once

/// @file
/// @brief The three `CustomItem`s behind the nine menu items that open a card-backed scene: the two
///        oscillator file browsers, the four sample loop points plus the audio clip's waveform view,
///        and the two record-to-sample items.
///
// clang-format off
/// | node | legacy item | legacy `beginSession` opens | scene |
/// |---|---|---|---|
/// | `file0_selector` / `file1_selector` | `menu_item::FileSelector` (`gui/menu_item/file_selector.h:26`) | `sampleBrowser` (`file_selector.cpp:36-47 beginSession`) | `browser::BrowserScene`, `BrowserId::Sample` |
/// | `sample0_start` / `sample1_start` | `sample::Start` (`gui/menu_item/sample/start.h:21`) | `sampleMarkerEditor`, `markerType = START` (`sample/loop_point.cpp:69 beginSession`) | `marker::MarkerScene`, `MarkerType::START` |
/// | `sample0_end` / `sample1_end` | `sample::End` (`gui/menu_item/sample/end.h:21`) | `sampleMarkerEditor`, `markerType = END` (same line) | `marker::MarkerScene`, `MarkerType::END` |
/// | `audio_clip_waveform` | `audio_clip::SampleMarkerEditor` (`gui/menu_item/audio_clip/sample_marker_editor.h:26`) | `sampleMarkerEditor`, `markerType = whichMarker` (`audio_clip/sample_marker_editor.cpp:46 beginSession`) | `marker::MarkerScene`, `MarkerType::END` |
/// | `sample0_recorder` / `sample1_recorder` | `osc::AudioRecorder` (`gui/menu_item/osc/audio_recorder.h:28`) | `audioRecorder` (`osc/audio_recorder.h:50 beginSession`) | `recorder::RecorderScene` |
// clang-format on
///
/// All five legacy classes are the same shape, and it is `EditNameItem`'s
/// (`nodes/edit_name.h`) and `ContextMenuItem`'s (`nodes/context_menu_items.h`): a `MenuItem` whose
/// entire body is a `beginSession` override that points a UI at something and calls `openUI()` on
/// it. `CustomItem::on_focus` IS `beginSession` (`node.h`), so that is where these do the same
/// thing -- except that a scene may not touch the `Shell` stack itself, so instead of opening a UI
/// each records the push in `MenuState::pending_scene` and `MenuScene::after_render` turns it into
/// an `Action::Kind::Push`. `MenuState::go_up_on_resume` is set alongside it, which is legacy's
/// `soundEditor.shouldGoUpOneLevelOnBegin = true` -- set by all five
/// (`file_selector.cpp:37`, `sample/loop_point.cpp:67`, `osc/audio_recorder.h:37`,
/// `audio_clip/sample_marker_editor.cpp:45`).
///
/// @note **`menu::select_source` is the fourth statement none of the three worked examples needed.**
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
/// @note **No refusal is spelled here, deliberately.** Legacy refuses in
///       `checkPermissionToBeginSession`, which `SoundEditor::setup` consults *before* navigating;
///       the port splits that in two. The range half is `menu::permission_for` +
///       `MenuScene::enter_child` (`Node::range_dependent`, which all but `audio_clip_waveform`
///       carry -- exactly as legacy, whose `audio_clip::SampleMarkerEditor` overrides no
///       `isRangeDependent`). The rest -- `LoopPoint`'s "make sure a Sample is loaded"
///       (`sample/loop_point.cpp:50-55`), `audio_clip::SampleMarkerEditor`'s null `sampleHolder.
///       audioFile` (`audio_clip/sample_marker_editor.cpp:35-38`), `AudioRecorder`'s
///       "already recording" (`gui/ui/audio_recorder.cpp:69-71`) -- is each scene's own `can()`,
///       consulted by its `on_enter`. A refused push is not a lost frame: `SceneStack::push`
///       (`shell/scene_stack.cpp:44-59`) unwinds and resumes the input top, which is the menu
///       whenever the refused scene is an opaque one (all of these are) -- and that is where
///       `go_up_on_resume` steps back out of the item, the same place legacy's failed `openUI`
///       leaves it. This is `EditNameItem`'s shipped behaviour, not a new rule.

#include "scenes/menu/node.h"

#include <cstdint>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// @brief What the three items share: the one-tick render, the no-op `handle`, and the source index
///        `on_focus` re-points the context at.
///
/// Not a convenience base -- it is the whole of two of legacy's five classes minus their
/// `beginSession`. The derived classes add exactly the `openUI` target.
class SampleSceneItem : public CustomItem {
public:
	/// @brief Which of `Sound::sources` this item is about, or negative for an item with no source
	///        (the audio clip's waveform view). Exposed so a spec can assert the item-to-source
	///        mapping by identity rather than re-deriving it.
	int8_t source_index() const { return source_index_; }

	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const final;
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
	/// @param marker       The `MarkerType` to open on, as the plain `uint16_t` a `SceneId` carries.
	///                     Spelled as a number rather than the enum so this header does not have to
	///                     pull `definitions_cxx.hpp`'s enum into every table that names an item --
	///                     the same trade `EditNameItem::target_` and `ContextMenuItem::
	///                     descriptor_id_` make, and pinned by `static_assert` in `sample.cpp`.
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

inline const MarkerItem kSample0Start{0, 0}; ///< `MarkerType::START`
inline const MarkerItem kSample1Start{1, 0}; ///< `MarkerType::START`
inline const MarkerItem kSample0End{0, 3};   ///< `MarkerType::END`
inline const MarkerItem kSample1End{1, 3};   ///< `MarkerType::END`
/// `audioClipSampleMarkerEditorMenuEnd` (`gui/ui/menus.cpp:1183`) -- the **END** marker, and the
/// only one of the pair the audio-clip root shows (`menus.cpp:1193`, `audioClipSampleMenu`). Its
/// sibling `audioClipSampleMarkerEditorMenuStart` (`:1182`) is reachable only from the grid
/// shortcut table (`gui/ui/sound_editor.cpp:119-120`), which is `menu/shortcuts.h`'s business and
/// not a node in any submenu.
inline const MarkerItem kAudioClipWaveform{-1, 3};

inline const AudioRecorderItem kSample0Recorder{0};
inline const AudioRecorderItem kSample1Recorder{1};
/// @}

} // namespace deluge::scenes::menu::nodes
