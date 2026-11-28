#pragma once

/// @brief `RenameScene` -- the one scene that replaces legacy's whole `gui/ui/rename/` directory.
///
/// Legacy has a `RenameUI` base plus four subclasses (`rename_clip_ui`, `rename_drum_ui`,
/// `rename_output_ui`, `rename_midi_cc_ui`) over `QwertyUI`, differing only in three virtuals:
/// `getCurrentName()`, `trySetName()` and (for two of them) `allowEmpty()` / `canRename()`. Those
/// three virtuals are the whole of `Target` below, and `SceneId::arg` selects one -- so the
/// subclass hierarchy becomes a table.
///
/// **Ports.** `RenameUI::opened` / `renderOLED` / `enterKeyPress` / `buttonAction` / `padAction` /
/// `verticalEncoderAction` / `getGreyoutColsAndRows` (`gui/ui/rename/rename_ui.cpp`), and the
/// input half of `QwertyUI` -- its typing rules: `padAction`,
/// `processBackspace` and `horizontalEncoderAction` (all in `gui/ui/qwerty_ui.cpp`). The *pixels* are already
/// ported and are not re-implemented here: `ui::widgets::grid::text_keyboard` is `QwertyUI::drawKeys` and
/// `ui::widgets::oled::text_edit` is `QwertyUI::drawTextForOLEDEditing`.
///
/// **What this scene may not do.** Like `MenuScene`, it never calls an `OledFrame` drawing
/// primitive -- the box, the title and the edited text all come from `ui::widgets::oled` widgets
/// (`popup_frame`, `text`, `text_edit`), so the layout rules stay in one place. It never
/// allocates: the edited name lives in a fixed buffer on the scene.

#include "ui/scenes/menu/context.h"
#include "ui/shell/scene.h"
#include "ui/views/grid/keyboard.h"
#include "ui/views/oled/rename.h"
#include "ui/widgets/core/response.h"
#include "ui/widgets/core/state.h"
#include "ui/widgets/grid/effects.h"
#include "ui/widgets/grid/keyboard.h"
#include "ui/widgets/oled/text_edit.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace deluge::ui::scenes::rename {

/// @brief Which rename target `SceneId::arg` names.
///
/// **Three, not legacy's four.** `renameClipUI`, `renameOutputUI` and `renameDrumUI` are all reachable from the menu
/// tree (`nodes::edit_clip_name`, `nodes::edit_track_name`, `nodes::drum_name_edit`, all three `EditName` items in
/// `nameEditMenu`, `drumNameEditMenu` and `audioOutputNameEditMenu` of `origin/main:src/deluge/gui/ui/menus.cpp`).
/// `renameMidiCCUI` is **not**: its only opener is `AutomationView::padAction` (`gui/views/automation_view.cpp`), a
/// view the port does not have, and no `Node` in `nodes::kAllRoots` reaches it. Adding a fourth entry now would be an
/// untested, unreachable code path, so it is deliberately absent -- whoever ports the automation view adds `MidiCc`
/// here, with `read`/`write` transcribed from `rename_midi_cc_ui.cpp` and a `can` from its `canRename()` (the only real
/// `canRename` override legacy has).
enum class TargetId : uint16_t { Clip = 0, Output = 1, Drum = 2, Count };

/// @brief What one rename target reads, writes and refuses -- one legacy `RenameUI` subclass, as
///        data.
struct Target {
	/// @brief l10n string id for the box title (`RenameUI::title`, drawn by `renderOLED`).
	uint16_t title;

	/// @brief `RenameUI::canRename()` -- may the editor open on this context at all?
	///
	/// Legacy has exactly one real override (`RenameMidiCCUI::canRename`, not covered here), but
	/// two of the three targets below also `FREEZE_WITH_ERROR` when their model is missing
	/// (`RenameDrumUI::getCurrentName`'s `RN01`). Refusing to open is the survivable spelling of
	/// that, and it is what `RenameUI::opened()` already does with `canRename()`.
	bool (*can)(const menu::MenuContext&);

	/// @brief `RenameUI::getCurrentName()`, read from the owner's record.
	std::string (*read)(const menu::MenuContext&);

	/// @brief `RenameUI::trySetName()`, as a text request to the menu's sink. False (a name another clip of the
	///        track, audio track or drum of the kit already has, or a write refused) rejects the name and leaves the
	///        editor open.
	bool (*write)(const menu::MenuContext&, std::string_view);

	/// @brief `RenameUI::allowEmpty()` -- true for every target but the drum
	///        (`RenameDrumUI::allowEmpty` in `origin/main:src/deluge/gui/ui/rename/rename_drum_ui.h`).
	bool allow_empty;
};

/// @brief The table. Indexed by `TargetId`; `TargetId::Count` or past it returns entry 0.
/// @param id Which target.
/// @return The target's read/write/refuse rules.
const Target& target(TargetId id);

/// @brief The `SceneKind` this scene is registered under: `FirstApp + 1` (`registry.h`).
inline constexpr deluge::ui::shell::SceneKind kRenameSceneKind =
    static_cast<deluge::ui::shell::SceneKind>(static_cast<uint8_t>(deluge::ui::shell::SceneKind::FirstApp) + 1);

/// @brief The on-pad text editor. One instance for the whole application (`registry.h`'s
///        `rename_scene()`).
class RenameScene final : public deluge::ui::shell::Scene {
public:
	/// @brief Point the scene at what @p ctx resolved.
	///
	/// The entry point for whoever opens the editor (`nodes::EditNameItem::on_focus`): the caller
	/// hands over the same `MenuContext` the menu is editing, and `on_enter` then picks the target
	/// out of `SceneId::arg`. Split from `on_enter` for exactly the reason `MenuScene::open` is:
	/// `SceneId` carries one `uint16_t`, and a context is not one.
	/// @param ctx What the rename targets read and write.
	void open(const menu::MenuContext& ctx);

	deluge::ui::shell::SceneKind kind() const override { return kRenameSceneKind; }
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::widgets::oled::OledFrame& frame) override;
	void render_grid(deluge::ui::widgets::grid::GridFrame& frame) override;
	deluge::ui::shell::Result after_render() override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	bool on_enter(const deluge::ui::shell::SceneId& id) override;
	void on_exit() override;
	std::optional<uint32_t> next_timer() const override;
	/// @brief `QwertyUI::opened`'s `blinkLed(IndicatorLED::BACK)`: BACK blinks, restarted where the editor opens.
	void leds(deluge::ui::widgets::core::LedFrame& frame) override;

	/// @brief True: the keyboard is painted over whatever grid is beneath, and the sidebar
	///        columns the keyboard does not own keep showing the view underneath -- exactly what
	///        `QwertyUI::drawKeys`'s `clearMainPadsWithoutSending` (main pads only) leaves.
	bool is_grid_overlay() const override { return true; }
	/// @brief `Main`: the keyboard is drawn on the main pads, and the sidebar is the view
	///        beneath's (`QwertyUI::drawKeys`, `gui/ui/qwerty_ui.cpp`).
	deluge::ui::shell::PadRegions pad_regions() const override { return deluge::ui::shell::PadRegions::Main; }
	/// @brief False: the base view still paints, and the sidebar it paints is what shows through.
	bool occludes_grid() const override { return false; }
	/// @brief True: `RenameUI`'s constructor sets `oledShowsUIUnderneath = true`
	///        (`rename_ui.cpp`), and `renderOLED` then clears only its own window.
	bool oled_transparent(deluge::ui::shell::SurfaceRole) const override { return true; }
	/// @brief `RenameUI::getGreyoutColsAndRows` (`rename_ui.cpp`): `*cols = 0b11`.
	deluge::ui::widgets::grid::GreyoutMask greyout_mask() const override { return {0b11, 0}; }

	/// @brief The name being edited. Exposed for specs and for whoever needs to see the in-flight
	///        text; nothing outside may write it.
	std::string_view entered_text() const { return text_; }
	/// @brief Which target the last `on_enter` selected.
	TargetId target_id() const { return target_; }
	/// @brief The insertion point (`QwertyUI::enteredTextEditPos`).
	int32_t cursor() const { return edit_.cursor; }

	/// @name What the surfaces show (`rename/present.cpp`)
	/// @{
	/// @brief What the window shows: the target's title, set in @p title, and the name typed so far.
	[[nodiscard]] deluge::ui::views::oled::RenameWindow present_window(deluge::ui::shell::TextScratch& title) const;
	/// @brief What the pads show: the keyboard in this scene's layout, drawn.
	[[nodiscard]] deluge::ui::views::grid::KeyboardPads present_keys() const;
	/// @}

private:
	/// @brief `QwertyUI::padAction`'s "normal keys" arm (`qwerty_ui.cpp`), minus the
	///        prediction and card-routine machinery neither of which a rename has.
	void type_char(char c);
	/// @brief `QwertyUI::processBackspace` (`gui/ui/qwerty_ui.cpp`).
	void backspace();
	/// @brief `QwertyUI::timerCallback`'s `UI_MODE_HOLDING_BACKSPACE` arm:
	///        delete another character if the pad is still down and the cadence says it is due.
	/// @param backspace_held Whether this tick's grid pass saw a backspace pad down
	///                       (`TextKeyboardState::backspace_held`).
	/// @param now            This tick's clock.
	/// @return True if a character was deleted, so the caller can ask for a repaint.
	bool advance_repeat(bool backspace_held, uint32_t now);
	/// @brief `RenameUI::enterKeyPress` (`rename_ui.cpp`).
	deluge::ui::shell::Result enter_key_press();
	/// @brief `RenameUI::exitUI` -> `close()` (`rename_ui.cpp`), as an `Action`.
	static deluge::ui::shell::Result close();

	menu::MenuContext ctx_{};
	TargetId target_ = TargetId::Clip;

	/// `QwertyUI::enteredText`, as unbounded as main's.
	std::string text_;

	deluge::ui::widgets::oled::TextEditState edit_{};
	deluge::ui::widgets::grid::TextKeyboardState kb_{};
	deluge::ui::widgets::grid::KeyboardLayoutKind layout_ = deluge::ui::widgets::grid::KeyboardLayoutKind::Qwerty;

	/// The end-of-string cursor's blink, `OLED::setupBlink`'s square wave as the toolkit spells it
	/// (`ui/widgets/core/state.h`'s `Blink`, and `values.cpp`'s decimal cursor for the same idiom).
	deluge::ui::widgets::core::Blink cursor_blink_{};
	/// `cursor_blink_.period_ms` doubles as "armed", exactly as `decimal_value`'s does; this is the
	/// clock the render pass last saw, so `next_timer()` can answer without one of its own.
	uint32_t last_render_ms_ = 0;

	/// Legacy's `UI_MODE_HOLDING_BACKSPACE` (`QwertyUI::padAction`), as a deadline rather than a mode:
	/// the tick the next repeat is due. `repeat_armed_` is the mode bit itself -- set by the press,
	/// cleared the moment the backspace pad lifts. While it is set the keys, Enter, BACK, SELECT and the
	/// sidebar do nothing, as `QwertyUI::padAction` and `RenameUI::buttonAction` refuse them in that mode.
	/// @{
	bool repeat_armed_ = false;
	uint32_t repeat_at_ms_ = 0;
	/// Whether the next `leds()` restarts the shared normal blink: the editor has just opened.
	bool blink_restart_ = false;
	/// @}

	/// What this tick's `render_grid` observed, for `after_render` to act on -- the same
	/// render-observes / after_render-acts split `MenuScene` uses.
	deluge::ui::widgets::core::Response resp_{};

	/// @brief Did `render_grid` actually run this tick?
	///
	/// **The shell calls `after_render()` for every scene that drew on ANY surface**
	/// (`Shell::render_oled` and `Shell::render_grid` both `append_rendered`, and step 7 walks that
	/// one list -- `ui/shell/shell.cpp`), so an OLED-only tick reaches `after_render` with `resp_`
	/// default-constructed. The repeat and the key it drops are acted on only on a tick whose grid pass
	/// read the pads.
	bool grid_pass_ = false;
};

/// @brief The application's one rename editor.
///
/// A function-local static, for the reason `menu_scene()` is one: it must outlive every `Shell`
/// that borrows it and must not participate in static-initialisation order across translation
/// units.
/// @return The singleton.
RenameScene& rename_scene();

} // namespace deluge::ui::scenes::rename
