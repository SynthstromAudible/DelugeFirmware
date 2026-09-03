#pragma once

/// @file
/// @brief The three pages whose row list is decided at open time rather than declared: the CV and
///        gate output selectors, and the MIDI device list.
///
/// The port of `cv::Selection` (`gui/menu_item/cv/selection.h:27-55`), `gate::Selection`
/// (`gui/menu_item/gate/selection.h:29-72`) and `midi::Devices`
/// (`gui/menu_item/midi/devices.{h,cpp}`) -- the last three items under the settings root that
/// legacy implements as bespoke `MenuItem` subclasses rather than as data.
///
/// @note **None of the three is a `Node::dynamic_children` node, and that is a finding, not an
///       oversight.** The plan expected these to be the tree's first dynamic submenus. They cannot
///       be, because a submenu's rows are labelled from `Node::name` -- one static `uint16_t` l10n
///       id (`node.h`, `MenuScene::render_vertical`'s `text_of(kids[i]->name, ...)`) -- and:
///
///         * `midi::Devices` labels every row with `MIDICable::getDisplayName()`
///           (`devices.cpp:169`), a runtime string with no l10n id at all. No set of static `Node`s
///           can spell those rows, however many of them a hook hands back.
///         * `cv::Selection` and `gate::Selection` have *static* option lists in legacy --
///           `{CV OUTPUT 1, CV OUTPUT 2}` (`cv/selection.h:47-54`) and
///           `{GATE OUTPUT 1..4, MINIMUM OFF TIME}` (`gate/selection.h:56-71`). `NUM_CV_CHANNELS`
///           and `NUM_GATE_CHANNELS` are compile-time constants, so there is nothing about either
///           list that a hook could decide that the table cannot.
///
///       So all three are `CustomItem`s that draw their own list, which is also what legacy does.
///       `deluge_menu_dump`'s fail-closed arm for `dynamic_children` therefore stays exactly as
///       Task 9 left it: there is still no real case to design its JSON shape against, and
///       inventing one unexercised is the mistake that arm exists to prevent.
///
/// ## The half Task 8 left owed: writing `MenuContext::source_index`
///
/// `settings.h`'s own note records it -- legacy's two selectors assign
/// `soundEditor.currentSourceIndex` on their way out (`cv/selection.h:42`, `gate/selection.h:47`),
/// and a `SelectChainFn` is handed a `const MenuContext&` and cannot. The fix is the shape
/// `MultiRangeItem` already established for `setCurrentMultiRange` (`MenuState::range_choice`): the
/// item records its cursor in `MenuState::source_choice` and `MenuScene::after_render` -- which owns
/// the scene's own mutable `MenuContext` -- applies it, *before* the same tick's select press
/// consults `select_chain`. `MidiDevicesItem` does the same for `soundEditor.currentMIDICable`
/// through `MenuState::midi_cable_choice`.
///
/// With that in place `cv_selection` / `gate_selection` are finished items rather than `kPending`
/// placeholders: `gate_selection_chain` (`settings.cpp`) now sees the output the user actually
/// picked, and `gate_mode_names` -- legacy's `gateModeMenu.updateOptions(value)`
/// (`gate/selection.h:52`) -- offers CLOCK or RUN SIGNAL on the right output.
///
/// ## What is still owed
///
///   * ~~**The per-device page.**~~ **Paid, E3 task 8** (`nodes/midi_device.h`).
///     `Devices::selectButtonPress` returns `&midiDeviceMenu` (`devices.cpp:154-156`), the
///     five-child submenu at `gui/ui/menus.cpp:1444-1453` (MPE direction selector, default
///     velocity, clock out, clock in, is-relative); all five are declared now, `midi_devices`
///     carries the `select_chain` that reaches them (`settings.h`), and the MPE arm's two further
///     chained pages come with it. What that page still owes is one thing only, and it is the same
///     thing as the next bullet: `midi::Device::getTitle()` is the device's own display name
///     (`gui/menu_item/midi/device.h:8`) and a `Node::title` is one static id.
///   * **The formatted titles.** `setCvNumberForTitle` / `gateModeMenu.format(value + 1)`
///     (`cv/selection.h:43`, `gate/selection.h:50`) put the output number into the *destination*
///     page's title; a `Node::title` is one static string id, so the port carries the unformatted
///     "CV OUTPUT N" (`settings.h`'s own note on `cv_submenu`).
///   * **`Devices::current_scroll_`.** Legacy keeps its own top-of-viewport index and its own
///     `computeScrollForSelected` (`devices.cpp:59-76`). `ui::oled::list` owns the window and the
///     scroll, so this builds every row and lets the widget pick the slice -- the same
///     simplification `MultiRangeItem` and `PatchCablesItem` make.

#include "scenes/menu/node.h"

#include <cstddef>
#include <cstdint>
#include <span>

class MIDICable;

namespace deluge::scenes::menu::nodes {

/// @brief The lowest index `Devices::getCable` accepts -- `devices.cpp:36`'s `lowestDeviceNum`.
///
/// Negative on purpose, and the reason DIN comes first: -3 is the DIN ports, -2 and -1 the two
/// upstream USB cables, and 0.. the hosted ones (`Devices::getCable`, `devices.cpp:132-152`).
inline constexpr int32_t kLowestDeviceNum = -3;

/// @brief The most device rows one frame materialises.
///
/// `hostedMIDIDevices` is a `fast_vector` with no fixed bound (`io/midi/midi_device_manager.h:97`),
/// and this layer allocates nothing, so the row array is a frame local of exactly the size every
/// other list page here uses -- `kMaxDynamicChildren` (`node.h`), which is also
/// `MenuScene::kMaxMenuChildren` and `MultiRangeItem`'s own `kMaxRows`. A rig with more than 64
/// connected cables loses the tail rather than the stack.
inline constexpr size_t kMaxDeviceRows = kMaxDynamicChildren;

/// @brief `Devices::getCable(index)` (`devices.cpp:132-152`), with legacy's own out-of-range guard.
/// @return The cable at @p index, or `nullptr` when @p index names none.
MIDICable* cable_at(int32_t index);

/// @brief The cables `Devices::drawPixelsForOled` would draw, in its own order.
///
/// Legacy walks `kLowestDeviceNum .. hostedMIDIDevices.size()` and pushes a row only for a cable
/// with `connectionFlags` set (`Devices::drawPixelsForOled`, `devices.cpp:163-176`); the reason is
/// spelled out on the same test in `Devices::computeScrollForSelected` -- "Disconnected devices
/// aren't drawn, so they don't take up a row" (`devices.cpp:70`). So the visible list *is* the
/// connected cables in index order -- which is also exactly the set
/// `Devices::selectEncoderAction` skips its way along (`devices.cpp:86-104`).
/// @return How many were written.
size_t connected_cables(std::span<MIDICable*> out);

/// @brief `cv::Selection` -- pick which CV output the `cv_submenu` pages address.
class CvSelectionItem final : public CustomItem {
public:
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
};

/// @brief `gate::Selection` -- pick which gate output `gate_mode` addresses, or the off-time page.
class GateSelectionItem final : public CustomItem {
public:
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
};

/// @brief `midi::Devices` -- the scrolling list of connected MIDI cables.
class MidiDevicesItem final : public CustomItem {
public:
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
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
