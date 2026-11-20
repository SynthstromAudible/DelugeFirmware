#pragma once

/// @file
/// @brief The per-device MIDI page -- the subtree `midi_devices` chains to -- and the MPE
///        direction/zone selectors that hang off it.
///
/// The port of `midi::Device midiDeviceMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`), the six-child submenu
/// `midi::Devices::selectButtonPress` returns (`origin/main:src/deluge/gui/menu_item/midi/devices.cpp`). The device
/// *list* is declared in `nodes/dynamic.h`; `nodes/settings.h`'s `midi_devices` chains to this page with
/// its `select_chain`, so the page is reachable, numbered, validated and dumped.
///
/// ## The six children, each with its legacy item
///
///   - `mpe_direction` -- legacy `mpe::DirectionSelector directionSelectorMenu`; writes its own
///     `whichDirection`.
///   - `device_default_velocity_to_level` -- legacy `midi::DefaultVelocityToLevel
///     defaultVelocityToLevelMenu`; writes `MIDICable::defaultVelocityToLevel`.
///   - `device_send_clock` -- legacy `midi::DeviceSendClock device_send_clock_menu`; writes
///     `MIDICable::sendClock`.
///   - `device_receive_clock` -- legacy `midi::DeviceReceiveClock device_receive_clock_menu`; writes
///     `MIDICable::receiveClock`.
///   - `device_is_relative` -- legacy `midi::DeviceIsRelative device_is_relative_menu`; writes
///     `MIDICable::is_relative`.
///   - `device_send_thru` -- legacy `midi::DeviceSendThru device_send_thru_menu`; writes
///     `MIDICable::midi_thru`.
///
/// ## The per-cable binding family
///
/// Five of the six are ordinary data nodes -- a `Kind::Int` and four `Kind::Toggle` -- whose `get`/`set` hooks read and
/// write **`MenuContext::midi_cable`**. Every other hook reads a device-wide global (`FlashStorage`, `midiEngine`,
/// `playbackHandler`, `cvEngine`) or the context's song/sound. `midi_cable` is the one `MenuContext` member
/// `context_for()` never assigns -- the device list publishes it, through `MenuWalkState::midi_cable_choice` and
/// `MenuScene::after_render` (`menu_scene.h`, mailbox 3) -- so these five are the first nodes in the tree whose value
/// depends on a *selection the user made two pages up*. Legacy spells the same dependency as
/// `soundEditor.currentMIDICable->...` in each of the five `readCurrentValue`/`writeCurrentValue` pairs.
///
/// A null `ctx.midi_cable` reads 0 and writes nothing. Legacy dereferences unconditionally, because its own navigation
/// cannot reach the page without `Devices::beginSession` having assigned the pointer first
/// (`origin/main:src/deluge/gui/menu_item/midi/devices.cpp`); this port can be pointed at the node directly by a
/// `SceneId` deep link or a spec, so the guard is the port's, in the same spirit as `cable_at`'s (`nodes/dynamic.cpp`).
///
/// Every one of the four also raises `MIDIDeviceManager::anyChangesToSave` on write, exactly where legacy does; the
/// velocity one additionally re-patches the song
/// (`Song::grabVelocityToLevelFromMIDICableAndSetupPatchingForEverything`, `DefaultVelocityToLevel::writeCurrentValue`
/// (`origin/main:src/deluge/gui/menu_item/midi/default_velocity_to_level.h`)), through `MenuContext::song` rather than
/// the `currentSong` global.
///
/// ## The MPE arm
///
/// `mpe::DirectionSelector` is the fifth child and the bespoke one. Three legacy files
/// (`gui/menu_item/mpe/`, 291 lines) become three nodes here:
///
///   * **`mpe_direction`** -- a two-option `Selection` (IN / OUT, `DirectionSelector::getOptions`
///     (`origin/main:src/deluge/gui/menu_item/mpe/direction_selector.h`)) whose value is its own
///     `uint8_t whichDirection` member, *not* anything in the model. `beginSession` resets it to
///     `MIDI_DIRECTION_INPUT_TO_DELUGE` on a forward entry only (`DirectionSelector::beginSession` in
///     `origin/main:src/deluge/gui/menu_item/mpe/direction_selector.cpp`), `getTitle()` is computed from it at render
///     time (`DirectionSelector::getTitle` in `origin/main:src/deluge/gui/menu_item/mpe/direction_selector.h`: MPE
///     INPUT or MPE OUTPUT), and `selectButtonPress` chains to `zoneSelectorMenu`
///     (`DirectionSelector::selectButtonPress` in `origin/main:src/deluge/gui/menu_item/mpe/direction_selector.cpp`).
///   * **`mpe_zone`** -- `mpe::ZoneSelector zoneSelectorMenu`
///     (`origin/main:src/deluge/gui/menu_item/mpe/zone_selector.cpp`), the same shape one level down: LOWER ZONE /
///     UPPER ZONE (`ZoneSelector::getOptions` in `origin/main:src/deluge/gui/menu_item/mpe/zone_selector.h`) over its
///     own `whichZone`, reset on a forward entry (`ZoneSelector::beginSession` in
///     `origin/main:src/deluge/gui/menu_item/mpe/zone_selector.cpp`), chaining to `zoneNumMemberChannelsMenu`
///     (`ZoneSelector::selectButtonPress` in `origin/main:src/deluge/gui/menu_item/mpe/zone_selector.cpp`).
///   * **`mpe_zone_num_member_channels`** -- `mpe::ZoneNumMemberChannels zoneNumMemberChannelsMenu`
///     (`origin/main:src/deluge/gui/menu_item/mpe/zone_num_member_channels.cpp`), an `IntegerWithOff` over
///     `MIDIPort::mpeLowerZoneLastMemberChannel` / `mpeUpperZoneLastMemberChannel`
///     (`ZoneNumMemberChannels::readCurrentValue` and `writeCurrentValue` in
///     `origin/main:src/deluge/gui/menu_item/mpe/zone_num_member_channels.cpp`) on the port the *other two* selectors
///     name.
///
/// **Where `whichDirection` and `whichZone` live in the port: `MenuState`.** Both are members of a legacy *menu item*
/// singleton that a **different page** reads -- `ZoneNumMemberChannels::getPort()` is
/// `soundEditor.currentMIDICable->ports[directionSelectorMenu.whichDirection]` (`ZoneNumMemberChannels::getPort` in
/// `origin/main:src/deluge/gui/menu_item/mpe/zone_num_member_channels.cpp`) and its read/write branch on
/// `zoneSelectorMenu.whichZone` (`ZoneNumMemberChannels::getMaxValue`, `readCurrentValue` and `writeCurrentValue`).
/// That is precisely the shape `MenuWalkState::patching_param` / `patch_source` / `patch_range_source` already have
/// (`menu_scene.h`: legacy's `source_selection::regularMenu.s`, read by four pages).
///
/// **The rule is two questions, not "where did legacy keep it".** "Legacy stores it on a menu item,
/// so it is `MenuState`" is only a proxy, and `MenuContext::midi_cable` is the counterexample that
/// breaks it: a `soundEditor.current*` member -- so context -- that is nonetheless *written* by a
/// menu item, through `MenuWalkState::midi_cable_choice` and `MenuScene::after_render`. What actually
/// decides is:
///
///   1. **Does a `Binding::get`/`set` -- handed only a `const MenuContext&` -- ever need to see
///      it?** If yes it must be a `MenuContext` field, whatever legacy called it and whoever writes
///      it. `midi_cable` answers yes (this page's four data nodes read it), `source_index` answers
///      yes (`gate_mode`'s hooks read it).
///   2. **Does it die when the walk is re-rooted?** If yes it is `MenuState`, and `retarget` clears
///      it with the rest of the walk.
///
/// `mpe_direction` / `mpe_zone` answer *no* to (1) -- the only readers are `CustomItem`s, which get
/// `MenuState` too -- and *yes* to (2). So they are `MenuState`, `retarget` drops them, and the
/// three items are `CustomItem`s. A future port of a menu-item member that a **data** node has to
/// read fails (1) and belongs in `MenuContext` with a mailbox, exactly as `midi_cable` does.
///
/// @note `mpe_direction` and `mpe_zone` share the name `STRING_FOR_MPE` and are **not** siblings
///       (`mpe_zone` is reached only through `mpe_direction`'s `chain_targets`), so
///       `validate_tree`'s sibling-scoped `NameCollision` rule does not fire on the pair.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"

#include "definitions_cxx.hpp" // kMaxMenuValue, for `device_default_velocity_to_level`

#include <cstdint>

namespace deluge::scenes::menu::nodes {

/// @name The per-cable binding family -- `MenuContext::midi_cable`, and nothing else.
///
/// Bodies in the `.cpp`, one legacy citation each. All eight tolerate a null `ctx.midi_cable`; see
/// the file note.
/// @{

/// `midi::DefaultVelocityToLevel::readCurrentValue`
/// (`origin/main:src/deluge/gui/menu_item/midi/default_velocity_to_level.h`).
int32_t get_device_velocity_to_level(const MenuContext& ctx);
/// `midi::DefaultVelocityToLevel::writeCurrentValue`.
void set_device_velocity_to_level(const MenuContext& ctx, int32_t value);

/// `midi::DeviceSendClock::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/midi/device_send_clock.h`).
int32_t get_device_send_clock(const MenuContext& ctx);
/// `midi::DeviceSendClock::writeCurrentValue`.
void set_device_send_clock(const MenuContext& ctx, int32_t value);

/// `midi::DeviceReceiveClock::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/midi/device_receive_clock.h`).
int32_t get_device_receive_clock(const MenuContext& ctx);
/// `midi::DeviceReceiveClock::writeCurrentValue`.
void set_device_receive_clock(const MenuContext& ctx, int32_t value);

/// `midi::DeviceIsRelative::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/midi/device_is_relative.h`).
int32_t get_device_is_relative(const MenuContext& ctx);
/// `midi::DeviceIsRelative::writeCurrentValue`.
void set_device_is_relative(const MenuContext& ctx, int32_t value);

/// `midi::DeviceSendThru::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/midi/device_send_thru.h`).
int32_t get_device_send_thru(const MenuContext& ctx);
/// `midi::DeviceSendThru::writeCurrentValue`.
void set_device_send_thru(const MenuContext& ctx, int32_t value);
/// @}

/// @name The member-channel page's two model reads, exposed rather than file-local.
///
/// `MpeZoneChannelsItem` is the only caller in this library, but both are transcriptions of a
/// legacy `MenuItem` override that a spec has to compare against directly -- and neither is
/// reachable through `bindings::` (the item has no `Binding` at all: its value depends on
/// `MenuState`). Same reason `connected_cables` is exposed from `nodes/dynamic.h`.
/// @{

/// @brief `ZoneNumMemberChannels::getMaxValue()`
///        (`origin/main:src/deluge/gui/menu_item/mpe/zone_num_member_channels.cpp`), with `Number::getMinValue`'s 0 as
///        the floor. A context naming no cable has an empty range.
IntRange mpe_zone_channels_range(const MenuContext& ctx, const MenuState& state);

/// @brief `ZoneNumMemberChannels::readCurrentValue()` -- the member-channel count on the
///        port `MenuWalkState::mpe_direction` names, for the zone `MenuWalkState::mpe_zone` names.
int32_t mpe_zone_channels_value(const MenuContext& ctx, const MenuState& state);
/// @}

/// @brief `mpe::DirectionSelector` (`origin/main:src/deluge/gui/menu_item/mpe/direction_selector.h`).
///
/// A `CustomItem` rather than a `Kind::Enum` node for two independent reasons, either of which
/// would be enough: its value is `MenuWalkState::mpe_direction` (see the file note), which a
/// `Binding::get`/`set` cannot reach; and its title is computed from that value at render time,
/// which `Node::title` cannot express.
class MpeDirectionItem final : public CustomItem {
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

/// @brief `mpe::ZoneSelector` (`origin/main:src/deluge/gui/menu_item/mpe/zone_selector.h`). Same shape as
///        `MpeDirectionItem`, over `MenuWalkState::mpe_zone`, minus the runtime title.
///
/// The node is named `STRING_FOR_MPE`, its parent selector's name, for `validate_tree`'s `MissingTitle` rule and the
/// dump; the page itself draws the empty title legacy's nameless item does.
class MpeZoneItem final : public CustomItem {
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

/// @brief `mpe::ZoneNumMemberChannels` (`origin/main:src/deluge/gui/menu_item/mpe/zone_num_member_channels.h`).
///
/// The one value editor here, and a `CustomItem` for the same reason the two selectors are: which half of which
/// `MIDIPort` it addresses is `MenuWalkState::mpe_direction` / `mpe_zone`, and its bounds move with them
/// (`getMaxValue`, `origin/main:src/deluge/gui/menu_item/mpe/zone_num_member_channels.cpp`). `on_focus` is
/// `Number::beginSession`'s `readCurrentValue` (there is no override of its own -- legacy's is commented out at
/// `origin/main:src/deluge/gui/menu_item/mpe/zone_num_member_channels.h`), `render` is `IntegerWithOff`'s huge-number
/// screen (`IntegerWithOff::drawPixelsForOled` in `origin/main:src/deluge/gui/menu_item/integer.cpp`), and `commit` is
/// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/mpe/zone_num_member_channels.cpp`) including its MCM
/// `sendRPN` tail.
class MpeZoneChannelsItem final : public CustomItem {
public:
	/// @brief True: the legacy item is a `Value` (`CustomItem::is_value`).
	bool is_value() const override { return true; }
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
	void on_reread(const MenuContext& ctx, MenuState& state) const override;
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::InputClaims owned_input(const MenuContext& /*ctx*/, const MenuState& /*state*/) const override {
		return kListInput;
	}
	void commit(const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
};

/// @name The three instances. `inline const`, like every other real `CustomItem`: the types have
///       vtables, and only their addresses need to be constant expressions.
/// @{
inline const MpeDirectionItem kMpeDirectionItem{};
inline const MpeZoneItem kMpeZoneItem{};
inline const MpeZoneChannelsItem kMpeZoneChannelsItem{};
/// @}

// ---------------------------------------------------------------------------------------------
// The nodes, innermost first -- a chain target has to be declared before the hook that names it.

/// `zoneNumMemberChannelsMenu` (`origin/main:src/deluge/gui/menu_item/mpe/zone_num_member_channels.cpp`), titled by its
/// own `getTitle()` override (`origin/main:src/deluge/gui/menu_item/mpe/zone_num_member_channels.h`) because legacy's
/// name is `EMPTY_STRING`.
inline constexpr Node mpe_zone_num_member_channels =
    custom(sid(deluge::l10n::String::STRING_FOR_NUM_MEMBER_CH_MENU_TITLE), &kMpeZoneChannelsItem);

/// `ZoneSelector::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/mpe/zone_selector.cpp`) -- unconditional.
/// @{
inline constexpr const Node* kMpeZoneChain[] = {&mpe_zone_num_member_channels};
inline const Node* mpe_zone_chain(const MenuContext&, const MenuState&) {
	return &mpe_zone_num_member_channels;
}
/// @}

inline constexpr Node mpe_zone =
    with_select_chain(custom(sid(deluge::l10n::String::STRING_FOR_MPE), &kMpeZoneItem), mpe_zone_chain, kMpeZoneChain);

/// `DirectionSelector::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/mpe/direction_selector.cpp`) --
/// unconditional.
/// @{
inline constexpr const Node* kMpeDirectionChain[] = {&mpe_zone};
inline const Node* mpe_direction_chain(const MenuContext&, const MenuState&) {
	return &mpe_zone;
}
/// @}

inline constexpr Node mpe_direction = with_select_chain(
    custom(sid(deluge::l10n::String::STRING_FOR_MPE), &kMpeDirectionItem), mpe_direction_chain, kMpeDirectionChain);

/// `DefaultVelocityToLevel::getMaxValue` is `kMaxMenuValue`
/// (`origin/main:src/deluge/gui/menu_item/midi/default_velocity_to_level.h`); `IntegerWithOff` draws 0 as "OFF"
/// (`origin/main:src/deluge/gui/menu_item/integer.cpp`), which is `ValueStyle::off_at_zero`.
inline constexpr Node device_default_velocity_to_level =
    with_value_style(int_item(sid(deluge::l10n::String::STRING_FOR_VELOCITY), get_device_velocity_to_level,
                              set_device_velocity_to_level, IntRange{0, kMaxMenuValue, 1}),
                     deluge::ui::oled::ValueStyle{.off_at_zero = true});

inline constexpr Node device_send_clock =
    toggle_item(sid(deluge::l10n::String::STRING_FOR_CLOCK_OUT), get_device_send_clock, set_device_send_clock);
inline constexpr Node device_receive_clock =
    toggle_item(sid(deluge::l10n::String::STRING_FOR_CLOCK_IN), get_device_receive_clock, set_device_receive_clock);
inline constexpr Node device_is_relative =
    toggle_item(sid(deluge::l10n::String::STRING_FOR_IS_RELATIVE), get_device_is_relative, set_device_is_relative);
inline constexpr Node device_send_thru =
    toggle_item(sid(deluge::l10n::String::STRING_FOR_MIDI_THRU), get_device_send_thru, set_device_send_thru);

/// `midiDeviceMenu`'s children, in the literal's own order (`origin/main:src/deluge/gui/ui/menus.cpp`).
inline constexpr const Node* kMidiDeviceChildren[] = {
    &mpe_direction,      &device_default_velocity_to_level,
    &device_send_clock,  &device_receive_clock,
    &device_is_relative, &device_send_thru,
};

/// `midi::Device midiDeviceMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`). See the file note on its title.
/// `midi::Device::getTitle` (`origin/main:src/deluge/gui/menu_item/midi/device.h`): the cable's own display name.
ValueText midi_device_title(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
inline constexpr Node midi_device =
    with_name_text(submenu(sid(deluge::l10n::String::STRING_FOR_MIDI_DEVICES), kMidiDeviceChildren), midi_device_title);

/// `Devices::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/midi/devices.cpp`) -- unconditional.
/// `nodes/settings.h`'s `midi_devices` carries these.
/// @{
inline constexpr const Node* kMidiDevicesChain[] = {&midi_device};
inline const Node* midi_devices_chain(const MenuContext&, const MenuState&) {
	return &midi_device;
}
/// @}

} // namespace deluge::scenes::menu::nodes
