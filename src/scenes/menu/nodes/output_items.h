#pragma once

/// @file
/// @brief The last two bespoke items: the two that edit a clip's **`Output`** rather than its sound.
///
// clang-format off
/// | node | legacy global | legacy class | what it is |
/// |---|---|---|---|
/// | `midi_device_linked` | `midiDeviceLinkedMenu` (`gui/ui/menus.cpp:905`) | `midi::device_definition::Linked` (`midi/device_definition/linked.h:29`) | a **toggle**: on opens the definition browser, off clears the linked file |
/// | `specific_output_selector` | `specificOutputSelectorMenu` (`gui/ui/menus.cpp:1151`) | `audio_clip::SpecificSourceOutputSelector` (`audio_clip/specific_output_source_selector.h:26`) | an **output picker**: which track an audio track records from |
// clang-format on
///
/// Grouped in one file because they are one subject: both read `MenuContext::output` and write
/// through it, and neither touches `MenuContext::sound`, `source` or `range` at all. That is also
/// why neither belongs in `nodes/sample.h` -- those nine are the file/sample family, parameterised
/// by which oscillator they are about.
///
/// @note **`specific_output_selector` is not a browser, and the plan's own brief warned not to
///       assume it was.** It is the one item in this pair that draws a page of its own and edits a
///       value on it: `SpecificSourceOutputSelector` overrides `drawPixelsForOled` and
///       `selectEncoderAction` and walks the song's output list, skipping every output the audio
///       track cannot record from (`AudioOutput::canRecordFrom`,
///       `processing/audio_output.h:109-111`, which excludes itself, MIDI and CV).
///
/// @note **`midi_device_linked` is a `Toggle`, which the brief did not say.**
///       `midi::device_definition::Linked` derives from `menu_item::Toggle`
///       (`midi/device_definition/linked.h:29`): its value is "is a definition file linked"
///       (`readCurrentValue`, `:33-36`, i.e. `!deviceDefinitionFileName.empty()`) and its
///       `writeCurrentValue` (`:37-49`) has **two** arms -- turning it *on* opens
///       `loadMidiDeviceDefinitionUI` (`:42`), turning it *off* clears the file name (`:46-47`).
///       Porting only the browser half would have shipped an item that can link a definition and
///       never unlink one, so both arms are here.
///
/// ## Two shapes, again, and they are the two `nodes/actions.h` already names
///
/// **`MidiDeviceLinkedItem` is the "legacy never enters this item" shape.** `Toggle` overrides
/// `shouldEnterSubmenu()` to `false` (`gui/menu_item/toggle.h:19`), so a select press on the row
/// runs `Toggle::selectButtonPress` -> `toggleValue()` (`:25-29`, `:33-44`) and leaves the user on
/// the parent list. The port has no equivalent of that flag (`nodes/actions.h`): it descends for one
/// tick, `on_focus` -- which **is** `beginSession` (`node.h`) -- does the toggle, and the item leaves
/// again. On the *link* arm that is `MenuState::pending_scene` plus `go_up_on_resume`, exactly as
/// `nodes/sample.h`'s items; on the *unlink* arm there is no scene to push, so `render` reports
/// `entered` and `MenuScene::after_render`'s leaf arm ascends -- which is what `ResetItem` and the
/// four `ActionItem`s already do for the same legacy shape.
///
/// **`SpecificOutputItem` is the "a page the user enters, turns and presses" shape.**
/// `SpecificSourceOutputSelector::shouldEnterSubmenu()` really is `true`
/// (`specific_output_source_selector.h:107`), so legacy navigates into it and the user turns the
/// select encoder there. Its cursor lives in `MenuState::list[depth]` for `SourceSelectionItem`'s
/// reason (`nodes/patch_cables.h`) -- the item is `const` and shared -- and the model write it
/// implies goes through `CustomItem::commit`, because a render may not write the model
/// (`menu_scene.h`).
///
/// ## Divergences, stated here rather than absorbed
///
///   * **The parent row's checkbox and file name are not drawn.** `Linked` overrides
///     `renderSubmenuItemTypeForOled` (`linked.h:56-78`) to draw a checked/unchecked box on its row
///     in the parent list and, when checked, the linked file's base name under it.
///     `MenuScene::render_oled`'s `row_icon` gives a checkbox to `Kind::Toggle` nodes only
///     (`menu_scene.cpp`), and this node is `Kind::Custom` -- it must stay `Kind::Custom`, because
///     the committed `deluge_menu_dump` JSON carries the node's type. Closing this needs a
///     `CustomItem` hook `row_icon` can ask, which is a `node.h` change rather than an item's, and
///     is recorded on the E3 roadmap in `src/scenes/README.md`. Consequence on device: the row says
///     `LINKED` with no state, so the toggle is write-only from the user's side until that hook
///     exists.
///   * **`on_focus` toggles, so a *restore* toggles too.** `MenuScene::restore_state` re-runs
///     `enter_leaf` on the leaf a saved menu was parked on, which reaches `on_focus`
///     (`menu_scene.cpp`'s note there, which argues that this is right for an item that opens a
///     scene). Legacy's `Toggle::beginSession` does **not** toggle -- only `selectButtonPress`
///     does, and it has a guard for exactly the other way in (`toggle.h:35-37`: opened by grid
///     shortcut -> go up, do not toggle). The port cannot tell the two apart: `retarget` and
///     `descend_to` both end with this node focused. The same hazard is already shipped by the four
///     `ActionItem`s and by `EditNameItem`; it is worse here only because the unlink arm changes
///     model state, so it is written down rather than left to be discovered. `ascend()` deliberately
///     does *not* re-run `on_focus` for `Kind::Custom` (`menu_scene.cpp`), so backing out of the
///     browser does not re-toggle -- that is the path a user actually takes.
///   * **`renderUIsForOled()`** (`specific_output_source_selector.h:46`, `:57`) has no port: the
///     shell renders every tick, and `after_render`'s own repaint arm covers the frames a
///     navigation or a commit invalidates.
///   * **The long-name side-scroller** (`:96-98`) is not ported, for `ui::oled::output_name`'s
///     reason (`ui/oled/misc.h`): `OLED::setupSideScroller` registers a scroller composited after
///     the renderer has returned, and `ui::oled::scroll_text` is the widget that would replace it.
///     A name too wide for the panel is left-aligned and clipped, which is what legacy draws on the
///     first frame anyway.

#include "scenes/menu/node.h"

#include <cstdint>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// @brief `midi::device_definition::Linked` -- link or unlink a MIDI device definition file.
class MidiDeviceLinkedItem final : public CustomItem {
public:
	/// @param browser_arg The `browser::BrowserId` this item's *link* arm opens, as the plain
	///                    `uint16_t` a `SceneId` carries. Spelled as a number rather than the enum
	///                    so this header does not pull `scenes/browser/browser_scene.h` into every
	///                    table that names the item -- the trade `MarkerItem::marker_`
	///                    (`nodes/sample.h`) makes, and pinned by `static_assert` in the `.cpp`.
	explicit MidiDeviceLinkedItem(uint16_t browser_arg) : browser_arg_{browser_arg} {}

	/// @brief Which browser the link arm opens. Exposed so a spec can assert the wiring by identity
	///        rather than only by driving it.
	uint16_t browser_arg() const { return browser_arg_; }

	/// @brief `Linked::readCurrentValue` (`midi/device_definition/linked.h:33-36`): is a definition
	///        file linked to whatever @p ctx names?
	///
	/// False for a context whose output is not a `MIDIInstrument` -- legacy's unguarded cast is
	/// safe only because `isRelevant` (`:51-54`) has already refused, which is
	/// `relevance::is_midi_out` on the node.
	static bool linked(const MenuContext& ctx);

	/// @brief Yes -- `on_focus` here is `Toggle::selectButtonPress` -> `toggleValue()`
	///        (`gui/menu_item/toggle.h:33-44`), not `beginSession`.
	///
	/// Without this, a focus with no press behind it -- `MenuScene::restore_state` rebuilding a
	/// saved path onto this leaf (which `SceneStack::replace_root` produces whenever the browser
	/// this item opened is sitting above the menu), or a `SceneId` deep link -- ran the toggle, and
	/// on the linked arm **cleared the user's definition file with no user action**. Legacy guards
	/// exactly this case and no other: `Toggle::selectButtonPress` returns without toggling when
	/// `soundEditor.getCurrentMenuItem() == this` (`toggle.h:35-37`), i.e. when the item was
	/// reached without a press on its row. `MenuScene::enter_leaf`'s `FocusCause` is the port of
	/// that test.
	bool on_focus_is_press() const override { return true; }

	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	void on_focus(const MenuContext& ctx, MenuState& state) const override;

private:
	uint16_t browser_arg_;
};

/// @brief `audio_clip::SpecificSourceOutputSelector` -- which track this audio track records from.
class SpecificOutputItem final : public CustomItem {
public:
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
	void commit(const MenuContext& ctx, MenuState& state) const override;
};

/// @name The two instances -- one each, because each has exactly one node.
///
/// `inline const`, not `constexpr` (they have a vtable); only their addresses need to be constant
/// expressions, which is all `custom()` needs.
/// @{
/// `browser::BrowserId::MidiDeviceDefinition`, `static_assert`ed in `output_items.cpp`.
inline const MidiDeviceLinkedItem kMidiDeviceLinked{1};
inline const SpecificOutputItem kSpecificOutputSelector{};
/// @}

} // namespace deluge::scenes::menu::nodes
