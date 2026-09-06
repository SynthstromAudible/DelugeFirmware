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
/// input half of `QwertyUI` -- its typing rules: `padAction` (`gui/ui/qwerty_ui.cpp:202-373`),
/// `processBackspace` (`gui/ui/qwerty_ui.cpp:408-419`) and `horizontalEncoderAction`
/// (`gui/ui/qwerty_ui.cpp:420-448`). The *pixels* are already
/// ported and are not re-implemented here: `ui::grid::text_keyboard` is `QwertyUI::drawKeys` and
/// `ui::oled::text_edit` is `QwertyUI::drawTextForOLEDEditing`.
///
/// **What this scene may not do.** Like `MenuScene`, it never calls an `OledFrame` drawing
/// primitive -- the box, the title and the edited text all come from `ui::oled` widgets
/// (`popup_frame`, `text`, `text_edit`), so the layout rules stay in one place. It never
/// allocates: the edited name lives in a fixed buffer on the scene.

#include "scenes/menu/context.h"
#include "shell/scene.h"
#include "ui/core/response.h"
#include "ui/core/state.h"
#include "ui/grid/effects.h"
#include "ui/grid/keyboard.h"
#include "ui/oled/text_edit.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::scenes::rename {

/// @brief Which rename target `SceneId::arg` names.
///
/// **Three, not legacy's four.** `renameClipUI`, `renameOutputUI` and `renameDrumUI` are all
/// reachable from the ported menu tree (`nodes::edit_clip_name`, `nodes::edit_track_name`,
/// `nodes::drum_name_edit`, all three `EditName` items in `gui/ui/menus.cpp:1125-1129`).
/// `renameMidiCCUI` is **not**: its only opener is `AutomationView::…` (`gui/views/automation_view.cpp:1641`),
/// a view no part of this plan ports, and no `Node` in `nodes::kAllRoots` reaches it. Adding a
/// fourth entry now would be an untested, unreachable code path, so it is deliberately absent --
/// whoever ports the automation view adds `MidiCc` here, with `read`/`write` transcribed from
/// `rename_midi_cc_ui.cpp` and a `can` from its `canRename()` (the only real `canRename` override
/// legacy has).
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

	/// @brief `RenameUI::getCurrentName()`. Points into model storage; the scene copies it at
	///        `on_enter` and never holds it.
	std::string_view (*read)(const menu::MenuContext&);

	/// @brief `RenameUI::trySetName()`. False rejects the name and leaves the editor open.
	bool (*write)(const menu::MenuContext&, std::string_view);

	/// @brief `RenameUI::allowEmpty()` -- true for every target but the drum
	///        (`rename_drum_ui.h:32`).
	bool allow_empty;
};

/// @brief The table. Indexed by `TargetId`; `TargetId::Count` or past it returns entry 0.
/// @param id Which target.
/// @return The target's read/write/refuse rules.
const Target& target(TargetId id);

/// @brief The longest name this editor will build.
///
/// Legacy's `QwertyUI::enteredText` is an unbounded `std::string`; this layer allocates nothing, so
/// the buffer is fixed. 64 is well past anything the model actually carries -- the box shows 17
/// characters and every name in a shipped song is a handful -- and past it a keypress simply types
/// nothing, which is the same outcome legacy's own `resize`+`append` would reach on a full heap.
inline constexpr size_t kMaxNameChars = 64;

/// @brief How long a held backspace pad waits before it starts repeating, and how fast it repeats
///        after that.
///
/// Legacy's own two numbers: `QwertyUI::padAction` arms `TimerName::UI_SPECIFIC` at **500 ms** on
/// the press (`gui/ui/qwerty_ui.cpp:220`) and `QwertyUI::timerCallback` re-arms it at **80 ms**
/// after each repeat (`:452`). The press itself deletes one character immediately (`:219`), so a
/// tap is one deletion and a hold is one, a half-second pause, then twelve and a half a second.
/// @{
inline constexpr uint32_t kBackspaceRepeatDelayMs = 500;
inline constexpr uint32_t kBackspaceRepeatMs = 80;
/// @}

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
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::oled::OledFrame& frame) override;
	void render_grid(deluge::ui::grid::GridFrame& frame) override;
	deluge::ui::shell::Result after_render() override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	bool on_enter(const deluge::ui::shell::SceneId& id) override;
	void on_exit() override;
	std::optional<uint32_t> next_timer() const override;

	/// @brief True: the keyboard is painted over whatever grid is beneath, and the sidebar
	///        columns the keyboard does not own keep showing the view underneath -- exactly what
	///        `QwertyUI::drawKeys`'s `clearMainPadsWithoutSending` (main pads only) leaves.
	bool is_grid_overlay() const override { return true; }
	/// @brief False: the base view still paints, and the sidebar it paints is what shows through.
	bool occludes_grid() const override { return false; }
	/// @brief False: legacy greys through `getGreyoutColsAndRows` (`greyout_mask()` below), it
	///        never halves every pad.
	bool dims_grid() const override { return false; }
	/// @brief True: `RenameUI`'s constructor sets `oledShowsUIUnderneath = true`
	///        (`rename_ui.cpp:26`), and `renderOLED` then clears only its own window.
	bool oled_transparent(deluge::ui::shell::SurfaceRole) const override { return true; }
	/// @brief `RenameUI::getGreyoutColsAndRows` (`rename_ui.cpp:58-61`): `*cols = 0b11`.
	deluge::ui::grid::GreyoutMask greyout_mask() const override { return {0b11, 0}; }

	/// @brief The name being edited. Exposed for specs and for whoever needs to see the in-flight
	///        text; nothing outside may write it.
	std::string_view entered_text() const { return std::string_view{buf_, len_}; }
	/// @brief Which target the last `on_enter` selected.
	TargetId target_id() const { return target_; }
	/// @brief The insertion point (`QwertyUI::enteredTextEditPos`).
	int32_t cursor() const { return edit_.cursor; }

private:
	/// @brief `QwertyUI::padAction`'s "normal keys" arm (`qwerty_ui.cpp:254-369`), minus the
	///        prediction and card-routine machinery neither of which a rename has.
	void type_char(char c);
	/// @brief `QwertyUI::processBackspace` (`qwerty_ui.cpp:408-418`).
	void backspace();
	/// @brief `QwertyUI::timerCallback`'s `UI_MODE_HOLDING_BACKSPACE` arm (`qwerty_ui.cpp:449-453`):
	///        delete another character if the pad is still down and the cadence says it is due.
	/// @param held_char This tick's `Response::held_index`.
	/// @param now       This tick's clock.
	/// @return True if a character was deleted, so the caller can ask for a repaint.
	bool advance_repeat(int16_t held_char, uint32_t now);
	/// @brief `RenameUI::enterKeyPress` (`rename_ui.cpp:45-52`).
	deluge::ui::shell::Result enter_key_press();
	/// @brief `RenameUI::exitUI` -> `close()` (`rename_ui.cpp:99-102`), as an `Action`.
	static deluge::ui::shell::Result close();

	menu::MenuContext ctx_{};
	TargetId target_ = TargetId::Clip;

	char buf_[kMaxNameChars] = {};
	uint16_t len_ = 0;

	deluge::ui::oled::TextEditState edit_{};
	deluge::ui::grid::TextKeyboardState kb_{};
	deluge::ui::grid::KeyboardLayoutKind layout_ = deluge::ui::grid::KeyboardLayoutKind::Qwerty;

	/// The end-of-string cursor's blink, `OLED::setupBlink`'s square wave as the toolkit spells it
	/// (`ui/core/state.h`'s `Blink`, and `values.cpp`'s decimal cursor for the same idiom).
	deluge::ui::Blink cursor_blink_{};
	/// `cursor_blink_.period_ms` doubles as "armed", exactly as `decimal_value`'s does; this is the
	/// clock the render pass last saw, so `next_timer()` can answer without one of its own.
	uint32_t last_render_ms_ = 0;

	/// Legacy's `UI_MODE_HOLDING_BACKSPACE` (`qwerty_ui.cpp:218`), as a deadline rather than a mode:
	/// the tick the next repeat is due. `repeat_armed_` is the mode bit itself -- set by the press,
	/// cleared the moment `Response::held_index` stops naming the backspace key.
	/// @{
	bool repeat_armed_ = false;
	uint32_t repeat_at_ms_ = 0;
	/// @}

	/// What this tick's `render_grid` observed, for `after_render` to act on -- the same
	/// render-observes / after_render-acts split `MenuScene` uses.
	deluge::ui::Response resp_{};

	/// @brief Did `render_grid` actually run this tick?
	///
	/// **The shell calls `after_render()` for every scene that drew on ANY surface**
	/// (`Shell::render_oled` and `Shell::render_grid` both `append_rendered`, and step 7 walks that
	/// one list -- `shell/shell.cpp`), so an OLED-only tick reaches `after_render` with `resp_`
	/// default-constructed, i.e. `held_index == -1`. Without this flag `advance_repeat` reads that
	/// as "the backspace pad is no longer down" and disarms the hold on the very first such tick.
	///
	/// E2 task 12 found that with a level-2 script and a 10 ms tick: the OLED repaints on its own
	/// for the cursor blink (and again for whatever is drawn underneath this overlay), so the
	/// disarming tick arrives ~10 ms after the press and the auto-repeat task 5's ruling asked for
	/// never fired ONCE on the real path. `rename_scene_spec.cpp`'s own repeat example missed it
	/// only because its three hand-placed ticks all happened to land on grid passes -- the exact
	/// failure a single-frame or sparse-tick test cannot see.
	bool grid_pass_ = false;
};

/// @brief The application's one rename editor.
///
/// A function-local static, for the reason `menu_scene()` is one: it must outlive every `Shell`
/// that borrows it and must not participate in static-initialisation order across translation
/// units.
/// @return The singleton.
RenameScene& rename_scene();

} // namespace deluge::scenes::rename
