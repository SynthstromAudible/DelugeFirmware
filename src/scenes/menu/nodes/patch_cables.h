#pragma once

/// @brief The patch-cable pages: the mod-matrix list, the two source pickers, and the three
///        cable-strength editors -- E2 task 9's port of `gui/menu_item/patch_cables.{h,cpp}` (192
///        lines), `patch_cable_strength.{h,cpp}` (390) and the four singletons under
///        `gui/menu_item/source_selection/` and `gui/menu_item/patch_cable_strength/`.
///
/// Six legacy globals become six `CustomItem`s and four new `Node`s:
///
/// | legacy global (`gui/ui/menus.cpp`) | port |
/// |---|---|
/// | `patchCablesMenu` (`:1735`) | `nodes::patch_cables` -> `kPatchCablesItem` |
/// | `source_selection::regularMenu` (`source_selection/regular.cpp:23`) | `nodes::source_selection_regular` |
/// | `patch_cable_strength::regularMenu` (`patch_cable_strength/regular.cpp:24`) |
/// `nodes::patch_cable_strength_regular` | | `source_selection::rangeMenu` (`source_selection/range.cpp:21`) |
/// `nodes::source_selection_range` | | `patch_cable_strength::rangeMenu` (`patch_cable_strength/range.cpp:21`) |
/// `nodes::patch_cable_strength_range` | | `vibratoMenu` (`:1725`), `sidechainVolumeShortcutMenu` (`:806`) |
/// `nodes::vibrato`, `nodes::sidechain_volume` |
///
/// ## The chain
///
/// Every one of these pages is reached through `MenuItem::selectButtonPress()`, i.e. through
/// `Node::select_chain` + `Node::chain_targets` (E2 task 3). The whole graph:
///
/// ```
///   53 PatchedParam nodes --(patched_param.cpp:32-38)--> source_selection_regular
///   source_selection_regular --(source_selection/regular.cpp:34)--> patch_cable_strength_regular
///   patch_cable_strength_regular --(patch_cable_strength.cpp:302-306)--> source_selection_range
///   source_selection_range --(source_selection/range.cpp:31)--> patch_cable_strength_range
///   patch_cable_strength_range --(patch_cable_strength.cpp:302-306)--> source_selection_range   <-- a 2-cycle
///   patch_cables --(patch_cables.cpp:168-190)--> patch_cable_strength_{regular,range}
///   vibrato, sidechain_volume --(patch_cable_strength.cpp:302-306)--> source_selection_range
/// ```
///
/// @note **The last-but-two edge closes a genuine cycle**, and it is legacy's, not an artefact of
///       the port: from the range-strength page a select press goes back to the range source
///       picker, whose own select press returns to the strength page. It is the first cyclic chain
///       in the tree, so `deluge_menu_dump` grew the `"ref"` stub its own `fail_on_chain_cycle`
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
/// ## What did NOT come across
///
///   * **Every `OledFrame` drawing primitive.** `PatchCableStrength::renderOLED`
///     (`patch_cable_strength.cpp:74-176`) is 100 lines of `drawVerticalLine` /
///     `drawGraphicMultiLine` / `invertAreaRounded` on the raw canvas; the scenes layer draws only
///     through `ui::oled` widgets (`scripts/check_layer_rules.py`'s `scenes-no-frame-primitives`).
///     The port draws the same *information* -- source, second source, destination, value -- as
///     title plus text rows plus `decimal_value`, and the arrow glyphs and the BI/UNI switch box
///     are not reproduced. The polarity itself is not lost: it is shown as a word and edited the
///     same way legacy's `selectEncoderAction` does (select held + turn).
///   * `PatchCables::blinkShortcuts` / `blinkShortcutsSoon` / `timerCallback`
///     (`patch_cables.cpp:118-152`) and the three `shouldBlinkPatchingSourceShortcut` overrides:
///     grid-pad blinking driven by `uiTimerManager` and `soundEditor.setupShortcutBlink`.
///     `MenuScene::render_grid` owns the port's one blink cycle and keys it off the focused *node*
///     (`menu_scene.h`'s `kShortcutBlinkMs`); making it key off a focused item's own answer is the
///     wiring plan's, not this task's.
///   * `PatchCableStrength::horizontalEncoderAction`'s `delayHorizontalScrollUntil` escape into
///     `automationView` / `keyboardScreen` (`:322-347`) -- both are `gui/views/`.
///   * `PatchCableStrength::buttonAction`'s MIDI/CV button polarity shortcut (`:308-320`) and
///     `endSession`'s `setLedState` pair (`:70-73`): `IndicatorLED` is `hid/`.
///   * `Regular::checkPermissionToBeginSession`'s POST_FX -> POST_REVERB_SEND -> LOCAL_VOLUME
///     fallback walk (`patch_cable_strength/regular.cpp:45-62`) IS ported, in
///     `PatchCableStrengthItem::on_focus`.
///   * `Fixed::patchingSourceShortcutPress` and friends -- patching-source *pad* presses, which
///     arrive as grid input this task does not wire.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"

#include "definitions_cxx.hpp" // PatchSource -- one of the root-level headers this layer may name

#include <cstdint>
#include <span>

namespace deluge::scenes::menu::nodes {

// ---------------------------------------------------------------------------------------------
// The items.

/// @brief The MOD MATRIX list -- `menu_item::PatchCables` (`gui/menu_item/patch_cables.cpp`).
///
/// One row per existing patch cable, "src [src2] value dest", and a select press that opens that
/// cable's strength page. With no cables at all a select press goes up a level, exactly as
/// `selectButtonPress`'s own "there were no items" arm does (`patch_cables.cpp:171-175`).
class PatchCablesItem final : public CustomItem {
public:
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
};

/// @brief One of the two source pickers -- `source_selection::Regular` / `source_selection::Range`.
///
/// The shared base is `menu_item::SourceSelection` (`gui/menu_item/source_selection.cpp`); the two
/// subclasses differ only in which destination descriptor they build and which `MenuState` field
/// their choice lands in, so one parameterised class covers both (the `MidiCommandItem` shape).
class SourceSelectionItem final : public CustomItem {
public:
	enum class Which : uint8_t {
		/// `source_selection::regularMenu`: picks the cable's own source. Destination is the bare
		/// param (`source_selection/regular.cpp:26-30`).
		Regular,
		/// `source_selection::rangeMenu`: picks the source modulating that cable's depth. Destination
		/// is param + the regular source (`source_selection/range.cpp:23-27`).
		Range,
	};
	explicit constexpr SourceSelectionItem(Which which) : which_{which} {}

	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	void on_focus(const MenuContext& ctx, MenuState& state) const override;

	constexpr Which which() const { return which_; }

private:
	Which which_;
};

/// @brief One of the three cable-strength editors -- `patch_cable_strength::Regular` / `::Range` /
///        `::Fixed`.
///
/// `Fixed` is a `Regular` whose destination param and source are baked in rather than navigated to
/// (`patch_cable_strength/fixed.h:24-27`): its `checkPermissionToBeginSession` writes both into the
/// shared state and then defers to `Regular` (`fixed.cpp:28-33`), which is exactly what
/// `on_focus` does here.
class PatchCableStrengthItem final : public CustomItem {
public:
	enum class Which : uint8_t { Regular, Range, Fixed };

	/// @param which        Which of the three legacy classes this instance is.
	/// @param param        `Fixed::p` -- the destination param. Ignored unless @p which is `Fixed`.
	/// @param source       `Fixed::s` -- the cable's source. Ignored unless @p which is `Fixed`.
	/// @param updates_reverb `sidechain::VolumeShortcut::writeCurrentValue`'s extra line
	///                     (`sidechain/volume.h:26-29`): the cable write, then
	///                     `AudioEngine::mustUpdateReverbParamsBeforeNextRender = true`. A
	///                     `Binding::updates_reverb_params` cannot carry it -- this node has no
	///                     `Binding` at all -- so it is a constructor argument.
	constexpr PatchCableStrengthItem(Which which, uint8_t param = 0, PatchSource source = PatchSource::NOT_AVAILABLE,
	                                 bool updates_reverb = false)
	    : which_{which}, param_{param}, source_{source}, updates_reverb_{updates_reverb} {}

	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
	void commit(const MenuContext& ctx, MenuState& state) const override;

	/// `PatchCableStrength::getLearningThing()` -- `MenuItemWithCCLearning`'s answer, which is a
	/// function of the navigation state rather than of this item (see `CustomItem::learn_descriptor`).
	ParamDescriptor learn_descriptor(const MenuContext& ctx, const MenuState& state) const override;

	constexpr Which which() const { return which_; }

private:
	Which which_;
	uint8_t param_;
	PatchSource source_;
	bool updates_reverb_;
};

/// @name The instances. `inline const`, like every other `CustomItem` singleton in this directory.
/// @{
inline const PatchCablesItem kPatchCablesItem{}; ///< `patchCablesMenu`
inline const SourceSelectionItem kSourceSelectionRegularItem{SourceSelectionItem::Which::Regular}; ///< `regularMenu`
inline const SourceSelectionItem kSourceSelectionRangeItem{SourceSelectionItem::Which::Range};     ///< `rangeMenu`
inline const PatchCableStrengthItem kPatchCableStrengthRegularItem{PatchCableStrengthItem::Which::Regular};
inline const PatchCableStrengthItem kPatchCableStrengthRangeItem{PatchCableStrengthItem::Which::Range};
/// `vibratoMenu` (`menus.cpp:1725-1726`): LOCAL_PITCH_ADJUST from LFO_GLOBAL_1.
inline const PatchCableStrengthItem kVibratoItem{
    PatchCableStrengthItem::Which::Fixed, deluge::modulation::params::LOCAL_PITCH_ADJUST, PatchSource::LFO_GLOBAL_1};
/// `sidechainVolumeShortcutMenu` (`menus.cpp:806-807`): GLOBAL_VOLUME_POST_REVERB_SEND from
/// SIDECHAIN, and the one instance that raises the reverb-recompute flag.
inline const PatchCableStrengthItem kSidechainVolumeItem{PatchCableStrengthItem::Which::Fixed,
                                                         deluge::modulation::params::GLOBAL_VOLUME_POST_REVERB_SEND,
                                                         PatchSource::SIDECHAIN, /*updates_reverb=*/true};
/// @}

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
/// `source_selection::rangeMenu`, title `STRING_FOR_MODULATE_DEPTH` (`source_selection/range.h:26`).
extern const Node source_selection_range;
/// `patch_cable_strength::rangeMenu`. Same titling note as its regular twin.
extern const Node patch_cable_strength_range;

// ---------------------------------------------------------------------------------------------
// The chain hooks and their target spans.

/// @brief `PatchedParam::selectButtonPress`'s non-shift arm (`gui/menu_item/patched_param.cpp:32-38`).
///
/// Always the source picker. The `soundEditor.patchingParamSelected = this->getP()` half of that
/// function happens in `SourceSelectionItem::on_focus` instead -- a `SelectChainFn` is handed a
/// **`const MenuState&`** and cannot write it (see `MenuState::patching_param`).
///
/// The shift-held arm (`Automation::selectButtonPress`, automation delete) has no port: `SelectChainFn`
/// is not passed the button state, and `Automation` is `gui/menu_item/`. E2 task 3 recorded that
/// same gap for every other chaining node.
const Node* patched_param_chain(const MenuContext& ctx, const MenuState& state);

/// @brief `source_selection::Regular::selectButtonPress` (`source_selection/regular.cpp:33-35`).
const Node* source_selection_regular_chain(const MenuContext& ctx, const MenuState& state);
/// @brief `source_selection::Range::selectButtonPress` (`source_selection/range.cpp:30-32`).
const Node* source_selection_range_chain(const MenuContext& ctx, const MenuState& state);
/// @brief `PatchCableStrength::selectButtonPress`'s non-shift arm
///        (`patch_cable_strength.cpp:302-306`) -- always the range source picker. Shared by all
///        three strength pages, `Fixed` included.
const Node* patch_cable_strength_chain(const MenuContext& ctx, const MenuState& state);
/// @brief `PatchCables::selectButtonPress` (`patch_cables.cpp:168-190`): the selected cable's own
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

/// @brief @p n with the chain every `PatchedParam`-derived legacy item has.
///
/// Applied at the 29 declaration sites whose `bind.param.kind == params::Kind::PATCHED` (the macros
/// in `sound_osc.h` / `sound_env.h` / `sound_lfo.h` expand several of them), which E2 task 3
/// measured to be **exactly 53 nodes** -- the same set as the legacy items that derive from
/// `menu_item::PatchedParam`, verified by `dynamic_cast` over a lockstep walk. `tables_2_spec` pins
/// both halves of that equality, so a `param(... Kind::PATCHED ...)` added without this wrapper
/// fails there rather than silently losing its select press.
constexpr Node with_patch_chain(Node n) {
	return with_select_chain(n, &patched_param_chain, kPatchedParamChain);
}

} // namespace deluge::scenes::menu::nodes
