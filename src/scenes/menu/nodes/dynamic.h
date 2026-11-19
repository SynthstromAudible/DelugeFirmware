#pragma once

/// @file
/// @brief The three pages whose row list is decided at open time rather than declared: the CV and
///        gate output selectors, and the MIDI device list.
///
/// Mirrors main's `cv::Selection` (`origin/main:src/deluge/gui/menu_item/cv/selection.h`), `gate::Selection`
/// (`origin/main:src/deluge/gui/menu_item/gate/selection.h`) and `midi::Devices`
/// (`origin/main:src/deluge/gui/menu_item/midi/devices.h`) -- the last three items under the settings root that
/// legacy implements as bespoke `MenuItem` subclasses rather than as data.
///
/// @note **None of the three is a `Node::dynamic_children` node.** None can be, because a submenu's rows are labelled
///       from `Node::name` -- one static `uint16_t` l10n id (`node.h`, `MenuView::render_vertical`'s
///       `text_of(kids[i]->name, ...)`) -- and:
///
///         * `midi::Devices` labels every row with `MIDICable::getDisplayName()`
///           (`Devices::drawPixelsForOled` in `origin/main:src/deluge/gui/menu_item/midi/devices.cpp`), a runtime
///           string with no l10n id at all. No set of static `Node`s can spell those rows, however many of them a hook
///           hands back.
///         * `cv::Selection` and `gate::Selection` have *static* option lists in legacy --
///           `{CV OUTPUT 1, CV OUTPUT 2}` (`Selection::getOptions` in
///           `origin/main:src/deluge/gui/menu_item/cv/selection.h`) and
///           `{GATE OUTPUT 1..4, MINIMUM OFF TIME}` (`Selection::getOptions` in
///           `origin/main:src/deluge/gui/menu_item/gate/selection.h`). `NUM_CV_CHANNELS` and `NUM_GATE_CHANNELS` are
///           compile-time constants, so there is nothing about either list that a hook could decide that the table
///           cannot.
///
///       So all three are `CustomItem`s that draw their own list, which is also what legacy does.
///       The menu dump's fail-closed arm for `dynamic_children` therefore stays
///       as it is: there is no real case to design its JSON shape against, and
///       inventing one unexercised is the mistake that arm exists to prevent.
///
/// ## Writing `MenuContext::source_index`
///
/// `nodes/settings.h`'s own note records it -- legacy's two selectors assign `soundEditor.currentSourceIndex` on their
/// way out (`cv::Selection::selectButtonPress` in `origin/main:src/deluge/gui/menu_item/cv/selection.h`,
/// `gate::Selection::selectButtonPress` in `origin/main:src/deluge/gui/menu_item/gate/selection.h`), and a
/// `SelectChainFn` is handed a `const MenuContext&` and cannot. The fix is the shape `MultiRangeItem` already
/// established for `setCurrentMultiRange` (`MenuWalkState::range_choice`): the item records its cursor in
/// `MenuWalkState::source_choice` and `MenuScene::after_render` -- which owns the scene's own mutable `MenuContext` --
/// applies it, *before* the same tick's select press consults `select_chain`. `MidiDevicesItem` does the same for
/// `soundEditor.currentMIDICable` through `MenuWalkState::midi_cable_choice`.
///
/// With that in place `cv_selection` / `gate_selection` are finished items rather than `kPending` placeholders:
/// `gate_selection_chain` (`nodes/settings.cpp`) now sees the output the user actually picked, and `gate_mode_names` --
/// legacy's `gateModeMenu.updateOptions(value)` (`Selection::selectButtonPress` in
/// `origin/main:src/deluge/gui/menu_item/gate/selection.h`) -- offers CLOCK or RUN SIGNAL on the right output.
///
/// ## Divergence
///
///   * **`Devices::current_scroll_`.** Legacy keeps its own top-of-viewport index and its own
///     `computeScrollForSelected` (`origin/main:src/deluge/gui/menu_item/midi/devices.cpp`). `ui::oled::list` owns the
///     window and the scroll, so this builds every row and lets the widget pick the slice -- the same simplification
///     `MultiRangeItem` and `PatchCablesItem` make.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"

#include <cstddef>
#include <cstdint>
#include <span>

class MIDICable;

namespace deluge::scenes::menu::nodes {

/// @brief The lowest index `Devices::getCable` accepts -- `origin/main:src/deluge/gui/menu_item/midi/devices.cpp`'s
/// `lowestDeviceNum`.
///
/// Negative on purpose, and the reason DIN comes first: -3 is the DIN ports, -2 and -1 the two upstream USB cables, and
/// 0.. the hosted ones (`Devices::getCable`, `origin/main:src/deluge/gui/menu_item/midi/devices.cpp`).
inline constexpr int32_t kLowestDeviceNum = -3;

/// @brief The most device rows one frame materialises.
///
/// `hostedMIDIDevices` is a `fast_vector` with no fixed bound (`io/midi/midi_device_manager.h`),
/// and this layer allocates nothing, so the row array is a frame local of exactly the size every
/// other list page here uses -- `kMaxDynamicChildren` (`node.h`), which is also
/// `kMaxMenuChildren` (`menu_state.h`) and `MultiRangeItem`'s own `kMaxRows`. A rig with more than 64
/// connected cables loses the tail rather than the stack.
inline constexpr size_t kMaxDeviceRows = kMaxDynamicChildren;

/// @brief `Devices::getCable(index)` (`origin/main:src/deluge/gui/menu_item/midi/devices.cpp`), with legacy's own
/// out-of-range guard.
/// @return The cable at @p index, or `nullptr` when @p index names none.
MIDICable* cable_at(int32_t index);

/// @brief The cables `Devices::drawPixelsForOled` would draw, in its own order.
///
/// Legacy walks `kLowestDeviceNum .. hostedMIDIDevices.size()` and pushes a row only for a cable with `connectionFlags`
/// set (`Devices::drawPixelsForOled`, `origin/main:src/deluge/gui/menu_item/midi/devices.cpp`); the reason is spelled
/// out on the same test in `Devices::computeScrollForSelected` -- "Disconnected devices aren't drawn, so they don't
/// take up a row" (`origin/main:src/deluge/gui/menu_item/midi/devices.cpp`). So
/// the visible list *is* the connected cables in index order -- which is also exactly the set
/// `Devices::selectEncoderAction` skips its way along (`origin/main:src/deluge/gui/menu_item/midi/devices.cpp`).
/// @return How many were written.
size_t connected_cables(std::span<MIDICable*> out);

/// `cv::Selection::getOptions`: one row per physical CV output. The page draws these and the menu dump lists them.
inline constexpr uint16_t kCvOutputNames[] = {
    sid(deluge::l10n::String::STRING_FOR_CV_OUTPUT_1),
    sid(deluge::l10n::String::STRING_FOR_CV_OUTPUT_2),
};

/// `gate::Selection::getOptions`: the four gate outputs, then the minimum-off-time page -- the one-past-the-last
/// option `gate_selection_chain` tests for. The page draws these and the menu dump lists them.
inline constexpr uint16_t kGateOutputNames[] = {
    sid(deluge::l10n::String::STRING_FOR_GATE_OUTPUT_1),    sid(deluge::l10n::String::STRING_FOR_GATE_OUTPUT_2),
    sid(deluge::l10n::String::STRING_FOR_GATE_OUTPUT_3),    sid(deluge::l10n::String::STRING_FOR_GATE_OUTPUT_4),
    sid(deluge::l10n::String::STRING_FOR_MINIMUM_OFF_TIME),
};

/// @brief `cv::Selection` -- pick which CV output the `cv_submenu` pages address.
class CvSelectionItem final : public CustomItem {
public:
	/// @brief True: the legacy item is a `Value` (`CustomItem::is_value`).
	bool is_value() const override { return true; }
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::InputClaims owned_input(const MenuContext& /*ctx*/, const MenuState& /*state*/) const override {
		return kListInput;
	}
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
};

/// @brief `gate::Selection` -- pick which gate output `gate_mode` addresses, or the off-time page.
class GateSelectionItem final : public CustomItem {
public:
	/// @brief True: the legacy item is a `Value` (`CustomItem::is_value`).
	bool is_value() const override { return true; }
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
	/// @brief The one selector here whose backward arm does real work -- see the `.cpp`.
	void on_focus_backward(const MenuContext& ctx, MenuState& state) const override;
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::InputClaims owned_input(const MenuContext& /*ctx*/, const MenuState& /*state*/) const override {
		return kListInput;
	}
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
};

/// @brief `midi::Devices` -- the scrolling list of connected MIDI cables.
class MidiDevicesItem final : public CustomItem {
public:
	/// @brief True: the legacy item is a `Value` (`CustomItem::is_value`).
	bool is_value() const override { return true; }
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::InputClaims owned_input(const MenuContext& /*ctx*/, const MenuState& /*state*/) const override {
		return kListInput;
	}
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
};

/// @name The three instances. `inline const`, like every other real `CustomItem` here: the types
///       have vtables, and only their addresses need to be constant expressions.
/// @{
inline const CvSelectionItem kCvSelectionItem{};
inline const GateSelectionItem kGateSelectionItem{};
inline const MidiDevicesItem kMidiDevicesItem{};
/// @}

} // namespace deluge::scenes::menu::nodes
