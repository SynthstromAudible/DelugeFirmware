#pragma once

/// @file
/// @brief The five menu items whose select press runs a root-UI action, as `CustomItem`s over the
///        action seam (`scenes/menu/action.h`).
///
// clang-format off
/// | node | legacy global | legacy class | emits |
/// |---|---|---|---|
/// | `perform_editor` | `performEditorMenu` (`gui/ui/menus.cpp:2005`) | `performance_session_view::EditingMode` (`performance_session_view/editing_mode.h:29`) | `OpenPerformanceView` + mode |
/// | `start_stem_export` | `startStemExportMenu` (`gui/ui/menus.cpp:2018`) | `stem_export::Start` (`stem_export/start.h:27`) | `StartStemExport` |
/// | `set_clip_length` | `setClipLengthMenu` (`gui/ui/menus.cpp:1131-1132`) | `audio_clip::SetClipLengthEqualToSampleLength` (`audio_clip/set_clip_length_equal_to_sample_length.h:22`) | `SetClipLengthToSampleLength` |
/// | `song_midi_learn` | `midiLearnMenu` (`gui/ui/menus.cpp:2082`) | `song::MidiLearn` (`song/midi_learn.h:23`) | `OpenSongMidiLearn` |
/// | `configure_song_macros` | `configureSongMacrosMenu` (`gui/ui/menus.cpp:2081`) | `song::ConfigureMacros` (`song/configure_macros.h:23`) | `OpenSongMacros` |
// clang-format on
///
/// ## Two shapes, because legacy has two
///
/// **`ActionItem` -- the four that are pure verbs.** All four legacy classes override
/// `shouldEnterSubmenu()` to `false` (`stem_export/start.h:51`,
/// `audio_clip/set_clip_length_equal_to_sample_length.h:31`, `song/midi_learn.h:33`,
/// `song/configure_macros.h:33`), which is what makes `SoundEditor` call
/// `newItem->selectButtonPress()` *without* navigating into them (`gui/ui/sound_editor.cpp:420-423`).
/// The port has no equivalent of that flag: `MenuScene::after_render`'s `entered` arm always
/// `enter_child`s. So the port descends for one tick, `on_focus` (= `beginSession`, `node.h`) files
/// the request, and `after_render` drains it and ascends straight back out -- which is what the five
/// `ResetItem`s already do for the same legacy shape (`nodes/settings.h:392-415`), except that they
/// ascend by way of `go_up_on_resume` after a pushed scene closes and these have no scene to push.
/// From the user's side the two are the same gesture: press, the parent list is still there.
///
/// **`PerformEditorItem` -- the one that is a value *and* a verb.** `EditingMode` is a `Selection`
/// with three options (`editing_mode.h:99-107`) whose `selectButtonPress` acts on the *chosen* one
/// (`:49-74`), so it is a page the user enters, turns, and presses. It draws with `enum_value`, the
/// same widget `Kind::Enum` uses, and files the request from its own render when the select lands.
///
/// @note **The DISABLED arm is legacy's `return nullptr`, and is ported as such.**
///       `EditingMode::selectButtonPress` returns `nullptr` on `DISABLED` -- "go up a level"
///       (`node.h`'s `SelectChainFn`) -- and `NO_NAVIGATION` on the other two, i.e. stay put while
///       the performance view is opened over the menu. `PerformEditorItem::render` reproduces both:
///       on `DISABLED` it leaves `MenuState::render_resp.entered` set, so `after_render`'s leaf arm
///       ascends; on `VALUE`/`PARAM` it files the request and *clears* `entered`, so nothing
///       navigates. Clearing it is the only way a `CustomItem` can say "I consumed this press",
///       because `enum_value` sets the flag before the item sees it.
///
/// ## Divergences, named rather than absorbed
///
///   * **`soundEditor.exitCompletely()`** (`stem_export/start.h:32`) closes the whole menu before
///     the export starts. `MenuAction::StartStemExport`'s dispatcher performs it, as of F2 task 3,
///     and it has to be the dispatcher rather than this item: the scene cannot pop itself out from
///     under a request it has not yet handed over. The port's own ascend is a no-op once the
///     dispatcher pops the scene. **Performed in equivalent form rather than verbatim** --
///     `closePortedMenu` (`gui/ui/shell_action_dispatch.cpp:170-172`) empties the guest's scene
///     stack, because under the guest the open UI is `ShellHostUI` and the literal call would walk
///     `closeUI` off the front of the navigation stack. Argued in full at that helper.
///   * (**`isRelevant() == (getRootUI() == &sessionView)`** on `song::MidiLearn` (`song/midi_learn.h:35-37`)
///     and `song::ConfigureMacros` (`song/configure_macros.h:35-37`) is no longer a divergence --
///     E3 task 9. It used to be one because `MenuContext::is_song_view` is `context_for`'s
///     `session_or_arranger_root` argument, i.e. `!rootUIIsClipMinderScreen()`, which is true in the
///     arranger and the song automation view as well; `setup()` (`sound_editor.cpp:1819-1822`) opens
///     one song root for all three. `context_for` now takes the narrower `session_view_root` too and
///     reports it as `MenuContext::is_session_view`, and both nodes carry
///     `relevance::is_session_view`. The two bools are a deliberately coarse encoding of that
///     three-way fact: nothing tells the arranger from the automation view, because no legacy
///     `isRelevant` does.)
///   * **`ConfigureMacros`/`MidiLearn` open legacy `ContextMenu` subclasses this plan has not
///     ported.** The intent names the action, not a `context_menu::DescriptorId`; when those two
///     menus are ported the dispatcher is where the two spellings meet.

#include "scenes/menu/action.h"
#include "scenes/menu/node.h"

#include <cstdint>

namespace deluge::scenes::menu::nodes {

/// @brief A menu item whose entire body is "file one `MenuAction` and get out of the way".
///
/// One class, four instances -- the `ResetItem` shape (`nodes/settings.h:403`), which is the same
/// legacy shape (a `MenuItem` that is only a `selectButtonPress`).
class ActionItem final : public CustomItem {
public:
	/// @param action What this item requests. Stored as the enum, not as a `uint16_t`: unlike
	///               `ResetItem`'s descriptor id and `EditNameItem`'s target id, this is not a
	///               `SceneId::arg` in disguise, and `action.h` is inside this layer.
	explicit ActionItem(MenuAction action) : action_{action} {}

	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	void on_focus(const MenuContext& ctx, MenuState& state) const override;

	/// @brief Which action this item files. Exposed so a spec can assert the item-to-action mapping
	///        by identity rather than by re-deriving it.
	MenuAction action() const { return action_; }

	/// @brief Yes -- `on_focus` here IS `selectButtonPress` (see the class note above), so it must
	///        not run for a focus with no press behind it (`node.h`'s `CustomItem::on_focus_is_press`).
	///
	/// E3 task 7. The exposure this closes is the `SceneId::arg` deep link and `restore_state`:
	/// at depth >= 1 the action drain ascends inside the same `after_render`, so a save can never
	/// capture one of these as the parked leaf -- but a menu opened *on* the item, or restored onto
	/// it, re-filed `StartStemExport` / `SetClipLengthToSampleLength` / the two song verbs with the
	/// user having pressed nothing. `PerformEditorItem` does **not** need this: it files from
	/// `render`, on a press it observes itself, which is the same discriminator `after_render`'s
	/// ascend arm already uses to tell the two apart.
	bool on_focus_is_press() const override { return true; }

private:
	MenuAction action_;
};

/// @brief `performance_session_view::EditingMode` -- pick an editing mode, and enter the performance
///        view in it.
class PerformEditorItem final : public CustomItem {
public:
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
};

/// @name The five instances. `inline const`, like every other real `CustomItem` here.
/// @{
inline const ActionItem kStartStemExportItem{MenuAction::StartStemExport};
inline const ActionItem kSetClipLengthItem{MenuAction::SetClipLengthToSampleLength};
inline const ActionItem kSongMidiLearnItem{MenuAction::OpenSongMidiLearn};
inline const ActionItem kConfigureSongMacrosItem{MenuAction::OpenSongMacros};
inline const PerformEditorItem kPerformEditorItem{};
/// @}

} // namespace deluge::scenes::menu::nodes
