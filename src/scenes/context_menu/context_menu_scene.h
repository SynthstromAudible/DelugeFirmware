#pragma once

/// @brief `ContextMenuScene` -- the one scene that replaces legacy's `gui::ContextMenu` hierarchy.
///
/// Legacy has a `ContextMenu` base (`gui/context_menu/context_menu.{h,cpp}`) and about a dozen
/// subclasses, every one of which differs only in three virtuals -- `getTitle()`, `getOptions()`
/// and `acceptCurrentOption()` -- plus, for a handful, `isCurrentOptionAvailable()` and
/// `getAcceptButton()`. Those three virtuals are the whole of `Descriptor` below, and
/// `SceneId::arg` selects one, so the subclass hierarchy becomes a table exactly as
/// `rename::Target` turned `gui/ui/rename/`'s into one.
///
/// **Ports.** `ContextMenu::renderOLED` (the pixels, already in `ui::oled::option_window`),
/// `selectEncoderAction`, `buttonAction`, `padAction`, `focusRegained`,
/// `getGreyoutColsAndRows` and `setupAndCheckAvailability`.
///
/// **What this scene may not do.** Like `MenuScene` and `RenameScene`, it never calls an
/// `OledFrame` drawing primitive: the window, its heading, its rule and its rows all come from
/// `ui::oled::option_window`, so the layout rules stay in one place. It never allocates and never
/// reads a wall clock other than the frame's own `now_ms()`.

#include "scenes/menu/context.h"
#include "shell/scene.h"
#include "ui/core/state.h"
#include "ui/grid/effects.h"

#include <cstdint>
#include <optional>
#include <span>

namespace deluge::scenes::context_menu {

/// @brief Which context menu `SceneId::arg` names.
///
/// **Five, not legacy's dozen.** These are the five `reset_settings::Reset` items
/// (`gui/menu_item/reset_settings/reset.h:29-33`), the only openers of a legacy context menu that
/// any node in the ported menu tree reaches (`nodes::reset_flash` .. `nodes::reset_all`,
/// `scenes/menu/nodes/settings.h`). Every other legacy subclass is opened from a view or a browser
/// this plan does not port -- `clearSong` from `view.cpp:290`, `clipSettings` / `newClipType` /
/// `audioInputSelector` from `session_view.cpp`, `overwriteFile` / `saveSongOrInstrument` /
/// `deleteFile` / `loadInstrumentPreset` from `gui/ui/save/` and `gui/ui/browser/`,
/// `cancelStemExport` from the three view files, `midiLearnMode` and `configureSongMacros` from
/// menu items that are still `Kind::Custom` placeholders -- so adding an entry for one now would
/// be an untested, unreachable code path.
///
/// @note **`audioInputSelector` is deliberately absent, and it does not fit `Descriptor` as it
///       stands.** `AudioInputSelector` (`gui/context_menu/audio_input_selector.cpp`) overrides
///       four more virtuals than the shape below has fields for: `selectEncoderAction` *commits on
///       every turn* (it writes `audioOutput->inputChannel` and the recording source as the cursor
///       moves, rather than on the accept button), `setupAndCheckAvailability` seeds the cursor
///       from the current channel instead of from zero, `renderOLED` appends a footer line naming
///       the selected track, and `padAction`/`getGreyoutColsAndRows` are both about the session
///       grid underneath. Whoever ports it adds a `bool (*on_move)(...)`, a
///       `uint8_t (*initial_option)(...)` and a footer hook here, rather than discovering
///       mid-port that the descriptor cannot say any of it.
enum class DescriptorId : uint16_t {
	ResetFlash = 0,
	ResetCommunityFeatures,
	ResetMidiFollow,
	ResetMidiDevices,
	ResetAll,
	Count,
};

/// @brief What one context menu shows and does -- one legacy `ContextMenu` subclass, as data.
struct Descriptor {
	/// @brief l10n string id for the window heading (`ContextMenu::getTitle`).
	uint16_t title;

	/// @brief l10n string ids for the options, in legacy's `getOptions()` order.
	///
	/// @note A descriptor with *dynamic availability* -- legacy's `isCurrentOptionAvailable()`,
	///       which `renderOLED` uses to skip rows and `selectEncoderAction` to turn past them --
	///       is expressed by handing the scene an already-compacted list, not by a per-option
	///       predicate: legacy's two loops are exactly "operate on the list with the unavailable
	///       entries removed", and `ui::oled::option_window` therefore takes no predicate either.
	///       That means a future dynamic descriptor grows a `size_t (*options_now)(const
	///       MenuContext&, std::span<uint16_t> out)` hook beside this field rather than a
	///       predicate. **None of the five below needs one**: every legacy `isCurrentOptionAvailable`
	///       override lives on a file-browser menu (`SaveSongOrInstrument`, `sample_browser::Kit`,
	///       `sample_browser::Synth`), and `ResetSettings` has neither an override nor more than
	///       one option.
	std::span<const uint16_t> options;

	/// @brief `ContextMenu::acceptCurrentOption()`. **False closes the menu**, which is legacy's
	///        own convention ("If returns false, will cause UI to exit", `context_menu.h:41`) and
	///        is what every `ResetSettings` action returns after doing its work.
	///
	/// @note Both parameters are ignored by all five hooks below -- the five factory resets are
	///       global calls and each menu has a single option. They are here because they are what
	///       `acceptCurrentOption()` reads off `this` (`soundEditor`'s resolved context, and
	///       `currentOption`), and every remaining legacy subclass needs both: `ClipSettingsMenu`
	///       and `NewClipType` switch on the option, and all of them address a model object the
	///       `MenuContext` carries.
	bool (*accept)(const menu::MenuContext&, uint8_t option);
};

/// @brief The table. Indexed by `DescriptorId`; `DescriptorId::Count` or past it returns entry 0.
/// @param id Which context menu.
/// @return Its heading, options and accept action.
const Descriptor& descriptor(DescriptorId id);

/// @brief The most options one descriptor may carry.
///
/// Eight, which is `AudioInputSelector`'s option count -- the widest legacy context menu there is
/// -- so the fixed arrays `render_oled` resolves the l10n ids into never truncate a descriptor a
/// later task adds. Nothing here allocates, so the ceiling has to be a number.
inline constexpr size_t kMaxOptions = 8;

/// @brief The cursor and the scroll window, as one value: legacy's `currentOption` / `scrollPos`
///        pair (`context_menu.h:47,51`).
struct Selection {
	uint8_t option = 0;
	uint8_t scroll = 0;
	bool operator==(const Selection&) const = default;
};

/// @brief `ContextMenu::selectEncoderAction` (`context_menu.cpp`), as a pure function.
///
/// Split out of the scene so the window rule can be exercised at any option count -- every shipped
/// descriptor has exactly one option and therefore never scrolls.
///
/// Legacy's rule, verbatim, and it is **not** `list_window_start`'s: the cursor is left where it
/// lands and `scrollPos` only follows once the cursor has already left it, so walking down a long
/// list keeps the cursor on the *bottom* of the two visible rows rather than the top. An offset
/// that would leave the list is refused outright (the cursor does not clamp to the end).
///
/// @note The `do { ... } while (!isCurrentOptionAvailable())` loop runs exactly once here, because
///       an unavailable option is expressed by not being in the list at all (see
///       `Descriptor::options`).
///
/// @param s      The current cursor/scroll pair.
/// @param offset Signed detents, as `selectEncoderAction`'s parameter.
/// @param count  How many options there are.
/// @return The new pair, or @p s unchanged when the turn would leave the list.
Selection step(Selection s, int32_t offset, uint8_t count);

/// @brief The `SceneKind` this scene is registered under: `FirstApp + 2` (`registry.h`).
inline constexpr deluge::ui::shell::SceneKind kContextMenuSceneKind =
    static_cast<deluge::ui::shell::SceneKind>(static_cast<uint8_t>(deluge::ui::shell::SceneKind::FirstApp) + 2);

/// @brief The centred option window. One instance for the whole application (`registry.h`'s
///        `scene_registry()`).
class ContextMenuScene final : public deluge::ui::shell::Scene {
public:
	/// @brief Point the scene at what @p ctx resolved.
	///
	/// The entry point for whoever opens the menu, split from `on_enter` for the reason
	/// `RenameScene::open` and `MenuScene::open` are: a `SceneId` carries one `uint16_t`, and a
	/// context is not one.
	/// @param ctx What the accept hook acts on.
	void open(const menu::MenuContext& ctx);

	deluge::ui::shell::SceneKind kind() const override { return kContextMenuSceneKind; }
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::oled::OledFrame& frame) override;
	void leds(deluge::ui::LedFrame& frame) override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	bool on_enter(const deluge::ui::shell::SceneId& id) override;
	void on_exit() override;
	std::optional<uint32_t> next_timer() const override;

	/// @brief True: the window is drawn over whatever grid is beneath, which keeps painting.
	bool is_grid_overlay() const override { return true; }
	/// @brief False: `ContextMenu` paints no pads at all, so the view underneath is what shows.
	bool occludes_grid() const override { return false; }
	/// @brief False: legacy greys through `getGreyoutColsAndRows` (`greyout_mask()` below), it
	///        never halves every pad. Same split `MenuScene` and `RenameScene` make.
	bool dims_grid() const override { return false; }
	/// @brief True: `ContextMenu`'s constructor sets `oledShowsUIUnderneath = true`
	///        (`context_menu.h:26`), and `renderOLED` then clears only its own window.
	bool oled_transparent(deluge::ui::shell::SurfaceRole) const override { return true; }
	/// @brief `ContextMenu::getGreyoutColsAndRows` (`context_menu.cpp`): `*cols = 0xFFFFFFFF`,
	///        `*rows` left at the caller's zero (`gui/ui/ui.cpp:74-83`) -- every column, no rows.
	deluge::ui::grid::GreyoutMask greyout_mask() const override { return {0xFFFFFFFFu, 0}; }

	/// @brief Which descriptor the last `on_enter` selected.
	DescriptorId descriptor_id() const { return id_; }
	/// @brief The cursor and scroll window (`currentOption` / `scrollPos`).
	Selection selection() const { return sel_; }

private:
	/// @brief `ContextMenu::close()`, as an `Action`.
	static deluge::ui::shell::Result close();
	/// @brief `buttonAction`'s `probablyAcceptCurrentOption` arm.
	deluge::ui::shell::Result accept_current_option();

	menu::MenuContext ctx_{};
	DescriptorId id_ = DescriptorId::ResetAll;
	Selection sel_{};

	/// Side-scroller state for the focused row (`OLED::setupSideScroller`).
	deluge::ui::ScrollText scroll_{};
	/// The clock the render pass last saw, so `next_timer()` can answer without one of its own --
	/// the same idiom `RenameScene::last_render_ms_` uses.
	uint32_t last_render_ms_ = 0;
};

/// @brief The application's one context menu.
///
/// A function-local static, for the reason `menu_scene()` and `rename_scene()` are: it must
/// outlive every `Shell` that borrows it and must not participate in static-initialisation order
/// across translation units.
/// @return The singleton.
ContextMenuScene& context_menu_scene();

} // namespace deluge::scenes::context_menu
