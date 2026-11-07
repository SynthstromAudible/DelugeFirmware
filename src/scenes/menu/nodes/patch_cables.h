#pragma once

/// @brief The patch-cable pages: the mod-matrix list, the two source pickers, and the three
///        cable-strength editors -- the port of `gui/menu_item/patch_cables.{h,cpp}`,
///        `patch_cable_strength.{h,cpp}` and the four singletons under
///        `gui/menu_item/source_selection/` and `gui/menu_item/patch_cable_strength/`.
///
/// Six legacy globals become six `CustomItem`s and four new `Node`s:
///
/// | legacy global (`gui/ui/menus.cpp`) | port |
/// |---|---|
/// | `patchCablesMenu` (`:1737`) | `nodes::patch_cables` -> `kPatchCablesItem` |
/// | `source_selection::regularMenu` (`source_selection/regular.cpp`) | `nodes::source_selection_regular` |
/// | `patch_cable_strength::regularMenu` | `nodes::patch_cable_strength_regular` |
/// | `source_selection::rangeMenu` | `nodes::source_selection_range` |
/// | `patch_cable_strength::rangeMenu` | `nodes::patch_cable_strength_range` |
/// | `vibratoMenu`, `sidechainVolumeShortcutMenu` | `nodes::vibrato`, `nodes::sidechain_volume` |
///
/// ## The chain
///
/// Every one of these pages is reached through `MenuItem::selectButtonPress()`, i.e. through
/// `Node::select_chain` + `Node::chain_targets`. The whole graph:
///
/// ```
///   53 PatchedParam nodes --(patched_param.cpp:32-40)--> source_selection_regular
///   source_selection_regular --(source_selection/regular.cpp:33)--> patch_cable_strength_regular
///   patch_cable_strength_regular --(patch_cable_strength.cpp:302-309)--> source_selection_range
///   source_selection_range --(source_selection/range.cpp)--> patch_cable_strength_range
///   patch_cable_strength_range --(patch_cable_strength.cpp:302-309)--> source_selection_range   <-- a 2-cycle
///   patch_cables --(patch_cables.cpp:164-190)--> patch_cable_strength_{regular,range}
///   vibrato, sidechain_volume --(patch_cable_strength.cpp:302-309)--> source_selection_range
/// ```
///
/// @note **The last-but-two edge closes a genuine cycle**, and it is legacy's, not an artefact of
///       the port: from the range-strength page a select press goes back to the range source
///       picker, whose own select press returns to the strength page. It is the first cyclic chain
///       in the tree, so the menu dump grew the `"ref"` stub its own `fail_on_chain_cycle`
///       comment had been asking for (`menu_dump_main.cpp`). `assign_node_ids` and `validate.cpp`
///       already survived a cycle (both stop at a node they have seen).
///
/// ## Shared state
///
/// Legacy keeps `soundEditor.patchingParamSelected` plus a `PatchSource s` member on each of the two
/// `source_selection` singletons. The port keeps all three on `MenuState`
/// (`menu_scene.h`: `patching_param`, `patch_source`, `patch_range_source`) because they are
/// navigation state shared by four pages, and because a `CustomItem` is a `const` singleton with
/// nowhere to put them.
///
/// ## Source-pad blinking
///
/// Legacy's `PatchCables::blinkShortcuts` / `blinkShortcutsSoon` / `timerCallback`
/// (`patch_cables.cpp`) and the three `shouldBlinkPatchingSourceShortcut` overrides drive grid-pad
/// blinking from `uiTimerManager` and `soundEditor.setupShortcutBlink`. The port has no per-item
/// timer: `MenuScene::render_grid` owns the one blink cycle (`shortcut_blink.h`'s
/// `kShortcutBlinkMs`), and each item here answers into it through `CustomItem::patched_param_to_blink`,
/// `source_blink` and `blink_delay_ms` (read by `shortcut_blink.cpp` and `pads.cpp`).
///
/// ## What is ported
///
/// The whole of `renderOLED`: the routing diagram and the BI/UNI box are `ui::oled::routing_chain` and
/// `ui::oled::segmented_switch`, the number is `ui::oled::cable_strength_value` (`ui/oled/routing.h`), transcribed
/// coordinate for coordinate and pinned byte-for-byte against legacy in `tests/unit/spec/ui/routing_spec.cpp`. The
/// scenes layer may not call an `OledFrame` primitive (`scripts/check_layer_rules.py`'s
/// `scenes-no-frame-primitives`), so the drawing lives in the toolkit and this page composes it. The number's digits
/// are the exact value, where legacy's build truncates (DIFFERENCES.md D71, accepted).
///
/// The polarity gestures and LEDs: `buttonAction`'s MIDI/CV pair is `PatchCableStrengthItem::handle`,
/// `updatePolarityUI`'s LEDs are `PatchCableStrengthItem::leds`, and `selectEncoderAction`'s held-Select turn is in
/// `render`, which uses the press up through `MenuTick::use_up_select`. `horizontalEncoderAction`'s
/// `delayHorizontalScrollUntil` escape and X_ENC guard are `MenuScene::route_horizontal_turn`.
///
/// `Regular::checkPermissionToBeginSession`'s POST_FX -> POST_REVERB_SEND -> LOCAL_VOLUME fallback walk
/// (`patch_cable_strength/regular.cpp:51-62`) is in `PatchCableStrengthItem::on_focus`.
/// `Fixed::patchingSourceShortcutPress` and friends -- patching-source *pad* presses, which arrive as grid input -- are
/// `source_press` below.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"

#include "definitions_cxx.hpp" // PatchSource -- one of the root-level headers this layer may name

#include "modulation/patch/patch_cable.h" // Polarity
#include "scenes/bind/write_sink.h"

#include <cstdint>
#include <optional>
#include <span>

namespace deluge::scenes::menu::nodes {

// ---------------------------------------------------------------------------------------------
// The items.

/// @brief The MOD MATRIX list -- `menu_item::PatchCables` (`gui/menu_item/patch_cables.cpp`).
///
/// One row per existing patch cable, "src [src2] value dest", and a select press that opens that
/// cable's strength page. With no cables at all a select press goes up a level, exactly as
/// `selectButtonPress`'s own "there were no items" arm does (`patch_cables.cpp:168-172`).
class PatchCablesItem final : public CustomItem {
public:
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::InputClaims owned_input(const MenuContext& /*ctx*/, const MenuState& /*state*/) const override {
		return kListInput;
	}
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
	/// `PatchCables::blinkShortcutsSoon` (`patch_cables.cpp`): 500 ms on an OLED panel.
	uint16_t blink_delay_ms() const override { return 500; }
	/// The cable under the cursor's destination, as `PatchCables::blinkShortcuts` blinks it (`patch_cables.cpp`).
	uint8_t patched_param_to_blink(const MenuContext& ctx, const MenuState& state) const override;
	/// Its source every cycle, and the source whose depth it modulates every fourth.
	uint8_t source_blink(const MenuContext& ctx, const MenuState& state, PatchSource source,
	                     deluge::ui::grid::FlashColour& colour) const override;
};

/// @brief One of the two source pickers -- `source_selection::Regular` / `source_selection::Range`.
///
/// The shared base is `menu_item::SourceSelection` (`gui/menu_item/source_selection.cpp`); the two
/// subclasses differ only in which destination descriptor they build and which `MenuState` field
/// their choice lands in, so one parameterised class covers both (the `MidiCommandItem` shape).
class SourceSelectionItem final : public CustomItem {
public:
	/// @brief True: the legacy item is a `Value` (`CustomItem::is_value`).
	bool is_value() const override { return true; }
	enum class Which : uint8_t {
		/// `source_selection::regularMenu`: picks the cable's own source. Destination is the bare
		/// param (`source_selection/regular.cpp:26-30`).
		Regular,
		/// `source_selection::rangeMenu`: picks the source modulating that cable's depth. Destination
		/// is param + the regular source (`source_selection/range.cpp:26-30`).
		Range,
	};
	explicit constexpr SourceSelectionItem(Which which) : which_{which} {}

	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::InputClaims owned_input(const MenuContext& /*ctx*/, const MenuState& /*state*/) const override {
		return kListInput;
	}
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	void on_focus(const MenuContext& ctx, MenuState& state) const override;

	constexpr Which which() const { return which_; }

	/// `source_selection::Range::getPatchingSourceShortcutTarget` (`range.h:28-30`): the item above.
	SourceTarget source_target(bool /*previous_held*/) const override {
		return which_ == Which::Range ? SourceTarget::Parent : SourceTarget::Parameter;
	}

	/// `source_selection::Regular::patchingSourceShortcutPress` (`source_selection/regular.cpp`): the source's strength
	/// page, or PARAMETER NOT APPLICABLE for a source the destination refuses; `Range`'s asks the item above.
	deluge::scenes::menu::SourcePress source_press(const MenuContext& ctx, MenuState& state, PatchSource source,
	                                               bool previous_held, const Node*& open) const override;

	/// `SourceSelection::getIndexOfPatchedParamToBlink` / `shouldBlinkPatchingSourceShortcut`
	/// (`source_selection.cpp`): the param, and every source patched to the destination.
	uint8_t patched_param_to_blink(const MenuContext& ctx, const MenuState& state) const override;
	uint8_t source_blink(const MenuContext& ctx, const MenuState& state, PatchSource source,
	                     deluge::ui::grid::FlashColour& colour) const override;

private:
	Which which_;
};

/// @brief One of the three cable-strength editors -- `patch_cable_strength::Regular` / `::Range` /
///        `::Fixed`.
///
/// `Fixed` is a `Regular` whose destination param and source are baked in rather than navigated to
/// (`patch_cable_strength/fixed.h:25-26`): its `checkPermissionToBeginSession` writes both into the
/// shared state and then defers to `Regular` (`fixed.cpp:28-33`), which is exactly what
/// `on_focus` does here.
class PatchCableStrengthItem final : public CustomItem {
public:
	/// @brief True: the legacy item is a `Value` (`CustomItem::is_value`).
	bool is_value() const override { return true; }
	enum class Which : uint8_t { Regular, Range, Fixed };

	/// @param which        Which of the three legacy classes this instance is.
	/// @param param        `Fixed::p` -- the destination param. Ignored unless @p which is `Fixed`.
	/// @param source       `Fixed::s` -- the cable's source. Ignored unless @p which is `Fixed`.
	constexpr PatchCableStrengthItem(Which which, uint8_t param = 0, PatchSource source = PatchSource::NOT_AVAILABLE)
	    : which_{which}, param_{param}, source_{source} {}

	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::InputClaims owned_input(const MenuContext& /*ctx*/, const MenuState& /*state*/) const override {
		return deluge::ui::InputClaims{.turn = true, .horizontal = true, .select = true};
	}
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
	void commit(const MenuContext& ctx, MenuState& state) const override;
	void leds(deluge::ui::LedFrame& f, const MenuContext& ctx, const MenuState& state) const override;

	/// `PatchCableStrength::getLearningThing()` -- `MenuItemWithCCLearning`'s answer, which is a
	/// function of the navigation state rather than of this item (see `CustomItem::learn_descriptor`).
	deluge::synth::CableTarget learn_descriptor(const MenuContext& ctx, const MenuState& state) const override;

	constexpr Which which() const { return which_; }
	/// @brief `Fixed::s` / `Fixed::p`: the cable a `Fixed` page edits. Meaningless for `Regular` and `Range`.
	constexpr PatchSource fixed_source() const { return source_; }
	constexpr uint8_t fixed_param() const { return param_; }

	/// The three classes' `patchingSourceShortcutPress` (`patch_cable_strength/regular.cpp`, `range.cpp:58-60`,
	/// `fixed.cpp:45-48`): another held source pad opens the range page for the pressed source (`Fixed` always opens
	/// it); otherwise the item above answers.
	deluge::scenes::menu::SourcePress source_press(const MenuContext& ctx, MenuState& state, PatchSource source,
	                                               bool previous_held, const Node*& open) const override;
	bool is_patch_cable() const override { return true; }
	/// `Regular::getPatchingSourceShortcutTarget` (`regular.h:35-37`): a range with another source pad held, else the
	/// item above; `Range` the item above; `Fixed` always a range.
	SourceTarget source_target(bool previous_held) const override {
		if (which_ == Which::Fixed || (which_ == Which::Regular && previous_held)) {
			return SourceTarget::Range;
		}
		return SourceTarget::Parent;
	}

	/// `PatchCableStrength::getIndexOfPatchedParamToBlink` (`patch_cable_strength.cpp:284-290`, the volume triple
	/// folded to POST_FX) and the three classes' `shouldBlinkPatchingSourceShortcut`
	/// (`patch_cable_strength/regular.cpp`, `range.cpp:42-56`, `fixed.cpp:35-43`).
	uint8_t patched_param_to_blink(const MenuContext& ctx, const MenuState& state) const override;
	uint8_t source_blink(const MenuContext& ctx, const MenuState& state, PatchSource source,
	                     deluge::ui::grid::FlashColour& colour) const override;

	/// @brief Set the polarity the page shows to @p polarity, and the cable's own where it exists (MIDI, CV, or a turn
	///        under a held Select); a cable the page makes later takes it. Restarts the cursor's blink dark, as
	///        `updatePolarityUI`'s re-render does.
	void set_polarity(const MenuContext& ctx, MenuState& state, Polarity polarity) const;

	/// `PatchCableStrength : Automation` (`patch_cable_strength.h:29`): shift + select deletes the cable's automation.
	bool is_automation_item() const override { return true; }
	/// The address of the cable this page edits.
	std::optional<deluge::project::ParamAddress> automation_address(const MenuContext& ctx,
	                                                                const MenuState& state) const override;

private:
	Which which_;
	uint8_t param_;
	PatchSource source_;
};

/// @name The instances. `inline const`, like every other `CustomItem` singleton in this directory.
/// @{
inline const PatchCablesItem kPatchCablesItem{}; ///< `patchCablesMenu`
inline const SourceSelectionItem kSourceSelectionRegularItem{SourceSelectionItem::Which::Regular}; ///< `regularMenu`
inline const SourceSelectionItem kSourceSelectionRangeItem{SourceSelectionItem::Which::Range};     ///< `rangeMenu`
inline const PatchCableStrengthItem kPatchCableStrengthRegularItem{PatchCableStrengthItem::Which::Regular};
inline const PatchCableStrengthItem kPatchCableStrengthRangeItem{PatchCableStrengthItem::Which::Range};
/// `vibratoMenu` (`menus.cpp:1727-1728`): LOCAL_PITCH_ADJUST from LFO_GLOBAL_1.
inline const PatchCableStrengthItem kVibratoItem{
    PatchCableStrengthItem::Which::Fixed, deluge::modulation::params::LOCAL_PITCH_ADJUST, PatchSource::LFO_GLOBAL_1};
/// @brief `PatchCableStrength`'s value text on a fixed cable's column: the notification (`getNotificationValue`,
///        `patch_cable_strength.h:73-75`) and the column (`Decimal::renderInHorizontalMenu`, `decimal.cpp:176-192`,
///        rendering style NUMBER), both the value over 100 to two places; the column keeps three characters of a value
///        at or below -10.00.
ValueText cable_strength_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// @brief `sidechain::VolumeShortcut`'s: the notification only. Its column is the ducking meter
///        (`getRenderingStyle() == SIDECHAIN_DUCKING`, `sidechain/volume.h`), which the node's `Hint::Ducking` draws.
ValueText ducking_strength_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);

/// `sidechainVolumeShortcutMenu` (`menus.cpp:806-807`): GLOBAL_VOLUME_POST_REVERB_SEND from
/// SIDECHAIN. The reverb copies this cable, so its Edit raises the reverb's recompute.
inline const PatchCableStrengthItem kSidechainVolumeItem{PatchCableStrengthItem::Which::Fixed,
                                                         deluge::modulation::params::GLOBAL_VOLUME_POST_REVERB_SEND,
                                                         PatchSource::SIDECHAIN};
/// @}

/// @brief The menu-unit strength of the cable `kVibratoItem` edits (LFO_GLOBAL_1 -> LOCAL_PITCH_ADJUST):
///        `PatchCableStrength::readCurrentValue` (`patch_cable_strength.cpp:198-212`), 0 where there is no cable.
int32_t vibrato_strength(const MenuContext& ctx);
/// @brief The same for `kSidechainVolumeItem`'s cable (SIDECHAIN -> GLOBAL_VOLUME_POST_REVERB_SEND).
int32_t ducking_strength(const MenuContext& ctx);

/// @brief @p n as a fixed cable's column on a horizontal page: it reads @p get, ranges over +/-50.00
///        (`getMinValue`/`getMaxValue`, `patch_cable_strength.h:36-37`), steps 1.00 a detent and 0.01 with SELECT or
///        SHIFT held (`Decimal::getNumberEditSize`, `decimal.cpp:49-54`), and a select press on it opens its own
///        strength page (`allowToBeginSessionFromHorizontalMenu`, `patch_cable_strength.h:50`). The column's edit is
///        written by the item's `commit`, as on the page.
/// @param n   The cable's custom node.
/// @param get Its strength getter.
/// @return @p n as a column.
constexpr Node fixed_cable_column(Node n, GetFn get) {
	n.bind.get = get;
	n.bind.fixed_range = IntRange{kMinMenuPatchCableValue, kMaxMenuPatchCableValue, 1};
	n.bind.horizontal_step = HorizontalStep{.coarse = 100, .fine = 1};
	return with_allow_session_from_horizontal(n);
}

// ---------------------------------------------------------------------------------------------
// The nodes.
//
// `extern const Node` defined in `patch_cables.cpp`, not `inline constexpr` like every other node in
// this directory, for one reason: the chain graph above is cyclic, and a `constexpr` initialiser
// cannot name an array holding the address of a node whose own initialiser names it back. An
// `extern` declaration makes every address an address constant, which is all `with_select_chain`'s
// spans need, and constant initialisation still happens (the builders are all `constexpr`).

/// `source_selection::regularMenu`, title `STRING_FOR_MODULATE_WITH` (`source_selection/regular.h:29`).
extern const Node source_selection_regular;
/// `patch_cable_strength::regularMenu`. Legacy draws no title on this page (`renderOLED` replaces
/// `drawPixelsForOled` wholesale); the port gives it the title of the picker that leads into it.
extern const Node patch_cable_strength_regular;
/// `source_selection::rangeMenu`, title `STRING_FOR_MODULATE_DEPTH` (`source_selection/range.h:27`).
extern const Node source_selection_range;
/// `patch_cable_strength::rangeMenu`. Same titling note as its regular twin.
extern const Node patch_cable_strength_range;

// ---------------------------------------------------------------------------------------------
// The chain hooks and their target spans.

/// @brief `PatchedParam::selectButtonPress`'s non-shift arm (`gui/menu_item/patched_param.cpp:32-40`).
///
/// Always the source picker. The `soundEditor.patchingParamSelected = this->getP()` half of that
/// function happens in `SourceSelectionItem::on_focus` instead -- a `SelectChainFn` is handed a
/// **`const MenuState&`** and cannot write it (see `MenuWalkState::patching_param`).
///
/// The shift-held arm (`Automation::selectButtonPress`, automation delete) is the interpreter's
/// (`MenuScene::after_render`), which sees shift; this hook answers the unshifted press.
const Node* patched_param_chain(const MenuContext& ctx, const MenuState& state);

/// @brief `source_selection::Regular::selectButtonPress` (`source_selection/regular.cpp:32-34`).
const Node* source_selection_regular_chain(const MenuContext& ctx, const MenuState& state);
/// @brief `source_selection::Range::selectButtonPress` (`source_selection/range.cpp`).
const Node* source_selection_range_chain(const MenuContext& ctx, const MenuState& state);
/// @brief `PatchCableStrength::selectButtonPress`'s non-shift arm
///        (`patch_cable_strength.cpp:302-309`) -- always the range source picker. Shared by all
///        three strength pages, `Fixed` included.
const Node* patch_cable_strength_chain(const MenuContext& ctx, const MenuState& state);
/// @brief `PatchCables::selectButtonPress` (`patch_cables.cpp:164-190`): the selected cable's own
///        strength page -- the range one when the cable's destination is itself a cable.
const Node* patch_cables_chain(const MenuContext& ctx, const MenuState& state);

/// @name Chain-target spans (`Node::chain_targets`).
/// @{
inline constexpr const Node* kPatchedParamChainArray[] = {&source_selection_regular};
inline constexpr std::span<const Node* const> kPatchedParamChain{kPatchedParamChainArray};

inline constexpr const Node* kSourceSelectionRegularChainArray[] = {&patch_cable_strength_regular};
inline constexpr std::span<const Node* const> kSourceSelectionRegularChain{kSourceSelectionRegularChainArray};

inline constexpr const Node* kSourceSelectionRangeChainArray[] = {&patch_cable_strength_range};
inline constexpr std::span<const Node* const> kSourceSelectionRangeChain{kSourceSelectionRangeChainArray};

inline constexpr const Node* kPatchCableStrengthChainArray[] = {&source_selection_range};
inline constexpr std::span<const Node* const> kPatchCableStrengthChain{kPatchCableStrengthChainArray};

inline constexpr const Node* kPatchCablesChainArray[] = {&patch_cable_strength_regular, &patch_cable_strength_range};
inline constexpr std::span<const Node* const> kPatchCablesChain{kPatchCablesChainArray};
/// @}

/// @brief The request that sets the cable @p learn names (its bottom-level source to its destination) on the sound
///        @p ctx edits to @p menu_value, the strength page's own value; a cable it makes takes @p polarity.
/// @return The request, or nullopt where @p ctx edits no sound.
std::optional<deluge::scenes::bind::WriteRequest>
cable_request(const MenuContext& ctx, deluge::synth::CableTarget learn, int32_t menu_value, Polarity polarity);

/// @brief @p n with the chain every `PatchedParam`-derived legacy item has.
///
/// Applied at the 29 declaration sites whose `bind.param.kind == params::Kind::PATCHED` (the macros
/// in `sound_osc.h` / `sound_env.h` / `sound_lfo.h` expand several of them), which is **exactly 53 nodes** -- the same
/// set as the legacy items that derive from `menu_item::PatchedParam`, verified by `dynamic_cast` over a lockstep walk.
/// `tables_2_spec` pins both halves of that equality, so a `param(... Kind::PATCHED ...)` added without this wrapper
/// fails there rather than silently losing its select press.
/// @param n The patched-param node.
/// @return @p n with the patch-cable select chain attached.
constexpr Node with_patch_chain(Node n) {
	return with_select_chain(n, &patched_param_chain, kPatchedParamChain);
}

} // namespace deluge::scenes::menu::nodes
