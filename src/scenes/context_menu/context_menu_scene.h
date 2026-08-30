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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace deluge::scenes::context_menu {

/// @brief Which context menu `SceneId::arg` names.
///
/// **Six, not legacy's dozen.** These are every legacy context menu a node in the ported menu tree
/// opens: the five `reset_settings::Reset` items (`gui/menu_item/reset_settings/reset.h:29-33`,
/// `nodes::reset_flash` .. `nodes::reset_all`) and `audio_clip::AudioSourceSelector`
/// (`nodes::audio_source_selector`). Every other legacy subclass is opened from a view or a browser
/// this plan does not port -- `clearSong` from `view.cpp:290`, `clipSettings` / `newClipType` from
/// `session_view.cpp`, `overwriteFile` / `saveSongOrInstrument` / `deleteFile` /
/// `loadInstrumentPreset` from `gui/ui/save/` and `gui/ui/browser/`, `cancelStemExport` from the
/// three view files -- so adding an entry for one now would be an untested, unreachable code path.
/// `midiLearnMode` and `configureSongMacros` *are* opened from ported menu items, but through the
/// action seam (`scenes/menu/action.h`) rather than from here: they are legacy `ContextMenu`
/// subclasses this plan has not ported, and the wiring plan's dispatcher opens the legacy objects.
///
/// @note **`audioInputSelector` was deliberately absent and is now the sixth entry (E2 task 11b).**
///       Task 6 recorded the three hooks it would need before it could fit -- an `on_move`, an
///       `initial_option` and a footer -- and those are exactly the three `Descriptor` grew; all
///       three default to null, so the five reset descriptors are unchanged, literally as well as
///       behaviourally (their aggregate initialisers still carry three fields).
///
///       Two corrections to that note, found by porting it:
///
///         * It also predicted a dynamic `options_now` hook, "since which inputs are available is a
///           runtime question". **It is not.** `AudioInputSelector::getOptions()`
///           (`audio_input_selector.cpp:77-85`) returns the same eight strings every time and the
///           class has no `isCurrentOptionAvailable()` override at all. No hook was added, and
///           `Descriptor::options`' note on how a dynamic list *would* be spelled still stands
///           unused.
///         * `padAction` and `getGreyoutColsAndRows` are not descriptor business and no hook is
///           offered for them. `getGreyoutColsAndRows` calls
///           `getRootUI()->getGreyedOutRowsNotRepresentingOutput(audioOutput)` (`:129-132`) and
///           `padAction` reads `sessionView.getOutputFromPad` (`:199-220`); both are the session
///           view's, which this plan does not port. The port greys as the base class does and a pad
///           press closes the menu, which is `ContextMenu`'s own behaviour -- the *record-from-this-
///           clip's-pad* gesture is the piece that is missing, and it is named here rather than
///           half-built.
enum class DescriptorId : uint16_t {
	ResetFlash = 0,
	ResetCommunityFeatures,
	ResetMidiFollow,
	ResetMidiDevices,
	ResetAll,
	/// `context_menu::audioInputSelector` (`gui/context_menu/audio_input_selector.{h,cpp}`), opened
	/// by `audio_clip::AudioSourceSelector::selectButtonPress`
	/// (`gui/menu_item/audio_clip/audio_source_selector.h:27-32`) -- the sixth, added by E2 task 11b
	/// together with the three `Descriptor` hooks the note below said it would need.
	AudioSource,
	Count,
};

/// @brief The cursor and the scroll window, as one value: legacy's `currentOption` / `scrollPos`
///        pair (`context_menu.h:47,51`).
struct Selection {
	uint8_t option = 0;
	uint8_t scroll = 0;
	bool operator==(const Selection&) const = default;
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

	/// @brief Which option the menu opens on, or null for legacy's own "the first available one".
	///
	/// `ContextMenu::setupAndCheckAvailability` walks to the first available option and seeds
	/// `scrollPos` from it; with availability expressed by compaction that is option 0, which is what
	/// a null hook means. `AudioInputSelector::setupAndCheckAvailability`
	/// (`gui/context_menu/audio_input_selector.cpp:87-127`) replaces the walk outright: it maps
	/// `audioOutput->inputChannel` onto its own `Value` enum and puts both `currentOption` and
	/// `scrollPos` there.
	///
	/// @return The option to open on. Clamped into the list by the scene, so a hook cannot open the
	///         menu on a row that is not there.
	uint8_t (*initial_option)(const menu::MenuContext&) = nullptr;

	/// @brief Commit-on-turn, or null for a menu that commits only when the accept button is pressed.
	///
	/// `AudioInputSelector::selectEncoderAction` (`:134-196`) calls the base and then *writes the
	/// model* -- `audioOutput->inputChannel`, the recording source, and the
	/// `defaultAudioOutputInputChannel` global -- so the audio input follows the cursor and backing
	/// out with BACK leaves the last row you passed through selected. That is legacy's behaviour, not
	/// an accident, and it is the reason this is a hook rather than something the accept path could
	/// cover.
	///
	/// Takes the `Selection` by reference because legacy's override writes `scrollPos` as well
	/// (`:142-145`: "Keep Track on the first visible row so the selected track name has room below
	/// it").
	void (*on_move)(const menu::MenuContext&, Selection& selection) = nullptr;

	/// @brief An extra line under the option rows, or null for none.
	///
	/// `AudioInputSelector::renderOLED` (`:222-237`) draws the base window and then a line naming the
	/// track it is recording from -- only while the channel is `SPECIFIC_OUTPUT`, which is why this
	/// returns a view rather than being a plain string id: the text is a model object's name, and
	/// whether there is any at all is a runtime question.
	///
	/// @param scratch Storage the returned view may point into; it outlives the render call. May be
	///                ignored by a hook returning a static or model-owned string.
	/// @return The footer text, or an empty view for none.
	std::string_view (*footer)(const menu::MenuContext&, std::span<char> scratch) = nullptr;
};

/// @brief The scratch a `Descriptor::footer` hook is handed.
///
/// Legacy's own footer is `Output::name.c_str()` or the literal "No track"
/// (`audio_input_selector.cpp:230-231`), neither of which needs scratch at all; the buffer is here
/// so a later descriptor can format one without this layer allocating. 48 is `notify::Notice`'s own
/// text slot, the widest single line anything in this stack carries.
inline constexpr size_t kFooterScratch = 48;

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
