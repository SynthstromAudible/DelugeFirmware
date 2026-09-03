#pragma once

/// @file
/// @brief The per-device MIDI page -- the subtree `midi_devices` chains to -- and the MPE
///        direction/zone selectors that hang off it.
///
/// The port of `midi::Device midiDeviceMenu` (`gui/ui/menus.cpp:1444-1453`), the five-child submenu
/// `midi::Devices::selectButtonPress` returns (`gui/menu_item/midi/devices.cpp:154-156`). E2 task 10
/// declared the device *list* and recorded this page as owed (`nodes/dynamic.h`, "What is still
/// owed"); this file is that debt, and `settings.h`'s `midi_devices` gains its `select_chain` with
/// it, so the page is reachable, numbered, validated and dumped.
///
/// ## The five children, each with its legacy item
///
///   | node | legacy item | legacy write |
///   |---|---|---|
///   | `mpe_direction` | `mpe::DirectionSelector directionSelectorMenu` (`menus.cpp:1470`,
///   `gui/menu_item/mpe/direction_selector.h:25-43`) | its own `whichDirection` (`:37`) | |
///   `device_default_velocity_to_level` | `midi::DefaultVelocityToLevel defaultVelocityToLevelMenu` (`menus.cpp:1440`,
///   `midi/default_velocity_to_level.h:24-37`) | `MIDICable::defaultVelocityToLevel` | | `device_send_clock` |
///   `midi::DeviceSendClock device_send_clock_menu` (`menus.cpp:1441`, `midi/device_send_clock.h:24-32`) |
///   `MIDICable::sendClock` | | `device_receive_clock` | `midi::DeviceReceiveClock device_receive_clock_menu`
///   (`menus.cpp:1442`, `midi/device_receive_clock.h:24-32`) | `MIDICable::receiveClock` | | `device_is_relative` |
///   `midi::DeviceIsRelative device_is_relative_menu` (`menus.cpp:1443`, `midi/device_is_relative.h:24-32`) |
///   `MIDICable::is_relative` |
///
/// ## The per-cable binding family
///
/// Four of the five are ordinary data nodes -- a `Kind::Int` and three `Kind::Toggle` -- whose
/// `get`/`set` hooks read and write **`MenuContext::midi_cable`**. That field is new territory for a
/// binding: every hook shipped before this one reads a device-wide global (`FlashStorage`,
/// `midiEngine`, `playbackHandler`, `cvEngine`) or the context's song/sound. `midi_cable` is the
/// one `MenuContext` member `context_for()` never assigns -- the device list publishes it, through
/// `MenuState::midi_cable_choice` and `MenuScene::after_render` (`menu_scene.h`, mailbox 3) -- so
/// these four are the first nodes in the tree whose value depends on a *selection the user made two
/// pages up*. Legacy spells the same dependency as `soundEditor.currentMIDICable->...` in each of
/// the four `readCurrentValue`/`writeCurrentValue` pairs.
///
/// A null `ctx.midi_cable` reads 0 and writes nothing. Legacy dereferences unconditionally, because
/// its own navigation cannot reach the page without `Devices::beginSession` having assigned the
/// pointer first (`devices.cpp:55`); this port can be pointed at the node directly by a `SceneId`
/// deep link or a spec, so the guard is the port's, in the same spirit as `cable_at`'s
/// (`nodes/dynamic.cpp:119-135`).
///
/// Every one of the four also raises `MIDIDeviceManager::anyChangesToSave` on write, exactly where
/// legacy does; the velocity one additionally re-patches the song
/// (`Song::grabVelocityToLevelFromMIDICableAndSetupPatchingForEverything`,
/// `default_velocity_to_level.h:34`), through `MenuContext::song` rather than the `currentSong`
/// global (task-3 ruling 3).
///
/// ## The MPE arm
///
/// `mpe::DirectionSelector` is the fifth child and the bespoke one. Three legacy files
/// (`gui/menu_item/mpe/`, 291 lines) become three nodes here:
///
///   * **`mpe_direction`** -- a two-option `Selection` (IN / OUT, `direction_selector.h:29-35`)
///     whose value is its own `uint8_t whichDirection` member (`:39`), *not* anything in the model.
///     `beginSession` resets it to `MIDI_DIRECTION_INPUT_TO_DELUGE` on a forward entry only
///     (`direction_selector.cpp:24-29`), `getTitle()` is computed from it at render time
///     (`direction_selector.h:40-43`: MPE INPUT or MPE OUTPUT), and `selectButtonPress` chains to
///     `zoneSelectorMenu` (`direction_selector.cpp:31-33`).
///   * **`mpe_zone`** -- `mpe::ZoneSelector zoneSelectorMenu` (`zone_selector.cpp:23`), the same
///     shape one level down: LOWER ZONE / UPPER ZONE (`zone_selector.h:32-38`) over its own
///     `whichZone` (`:41`), reset on a forward entry (`zone_selector.cpp:25-30`), chaining to
///     `zoneNumMemberChannelsMenu` (`:32-34`).
///   * **`mpe_zone_num_member_channels`** -- `mpe::ZoneNumMemberChannels zoneNumMemberChannelsMenu`
///     (`zone_num_member_channels.cpp:27`), an `IntegerWithOff` over
///     `MIDIPort::mpeLowerZoneLastMemberChannel` / `mpeUpperZoneLastMemberChannel`
///     (`zone_num_member_channels.cpp:56-85`) on the port the *other two* selectors name.
///
/// **Where `whichDirection` and `whichZone` live in the port: `MenuState`.** Both are members of a
/// legacy *menu item* singleton that a **different page** reads --
/// `ZoneNumMemberChannels::getPort()` is
/// `soundEditor.currentMIDICable->ports[directionSelectorMenu.whichDirection]`
/// (`zone_num_member_channels.cpp:29-31`) and its read/write branch on
/// `zoneSelectorMenu.whichZone` (`:38`, `:58`, `:68`). That is precisely the shape
/// `MenuState::patching_param` / `patch_source` / `patch_range_source` already have
/// (`menu_scene.h`: legacy's `source_selection::regularMenu.s`, read by four pages), and the
/// mapping rule those three establish is the one applied here: a **`soundEditor.current*` member**
/// becomes a `MenuContext` field, a **menu-item member read by another page** becomes `MenuState`.
/// So `MenuState::mpe_direction` / `mpe_zone` are the port, `retarget` drops them with the rest of
/// the walk's state, and the three items are `CustomItem`s -- a data node's `get`/`set` is handed
/// only a `const MenuContext&` and could not see either.
///
/// ## What did not survive
///
///   * **`midiDeviceMenu`'s runtime title.** `midi::Device::getTitle()` is
///     `soundEditor.currentMIDICable->getDisplayName()` (`gui/menu_item/midi/device.h:8`), a
///     device-supplied string with no l10n id; `Node::title` is one static `uint16_t` and
///     `MenuScene::render_vertical` draws it directly, so the page carries "MIDI DEVICES" instead
///     of the device's own name. Exactly the class of gap `nodes/dynamic.h` already records for
///     `setCvNumberForTitle` -- and the same one, for the same reason: a submenu has no
///     `CustomItem` to compute a title from. (Legacy's `name` is `EMPTY_STRING`, which
///     `validate_tree` reports as `MissingTitle`, so the port names it too -- the resolution
///     `gate_off_time` already uses, `settings.h`.)
///   * **`zoneSelectorMenu`'s empty name.** `ZoneSelector` is default-constructed
///     (`zone_selector.cpp:23`), so both its name and title are `EMPTY_STRING` and legacy draws a
///     blank title bar over it. `MissingTitle` again, so the port names it `STRING_FOR_MPE` --
///     the name its own parent selector carries (`menus.cpp:1470`), which is what the page is
///     about. `mpe_zone_num_member_channels` needs no such invention: it is default-constructed
///     too, but overrides `getTitle()` to `STRING_FOR_NUM_MEMBER_CH_MENU_TITLE`
///     (`zone_num_member_channels.h:34-36`).
///
/// @note `mpe_direction` and `mpe_zone` share the name `STRING_FOR_MPE` and are **not** siblings
///       (`mpe_zone` is reached only through `mpe_direction`'s `chain_targets`), so
///       `validate_tree`'s sibling-scoped `NameCollision` rule does not fire on the pair.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"

#include "definitions_cxx.hpp" // kMaxMenuValue -- `DefaultVelocityToLevel::getMaxValue`

#include <cstdint>

namespace deluge::scenes::menu::nodes {

/// @name The per-cable binding family -- `MenuContext::midi_cable`, and nothing else.
///
/// Bodies in the `.cpp`, one legacy citation each. All eight tolerate a null `ctx.midi_cable`; see
/// the file note.
/// @{

/// `midi::DefaultVelocityToLevel::readCurrentValue` (`midi/default_velocity_to_level.h:29-32`).
int32_t get_device_velocity_to_level(const MenuContext& ctx);
/// `midi::DefaultVelocityToLevel::writeCurrentValue` (`:33-37`).
void set_device_velocity_to_level(const MenuContext& ctx, int32_t value);

/// `midi::DeviceSendClock::readCurrentValue` (`midi/device_send_clock.h:27`).
int32_t get_device_send_clock(const MenuContext& ctx);
/// `midi::DeviceSendClock::writeCurrentValue` (`:28-31`).
void set_device_send_clock(const MenuContext& ctx, int32_t value);

/// `midi::DeviceReceiveClock::readCurrentValue` (`midi/device_receive_clock.h:27`).
int32_t get_device_receive_clock(const MenuContext& ctx);
/// `midi::DeviceReceiveClock::writeCurrentValue` (`:28-31`).
void set_device_receive_clock(const MenuContext& ctx, int32_t value);

/// `midi::DeviceIsRelative::readCurrentValue` (`midi/device_is_relative.h:27`).
int32_t get_device_is_relative(const MenuContext& ctx);
/// `midi::DeviceIsRelative::writeCurrentValue` (`:28-31`).
void set_device_is_relative(const MenuContext& ctx, int32_t value);
/// @}

/// @name The member-channel page's two model reads, exposed rather than file-local.
///
/// `MpeZoneChannelsItem` is the only caller in this library, but both are transcriptions of a
/// legacy `MenuItem` override that a spec has to compare against directly -- and neither is
/// reachable through `bindings::` (the item has no `Binding` at all: its value depends on
/// `MenuState`). Same reason `connected_cables` is exposed from `nodes/dynamic.h`.
/// @{

/// @brief `ZoneNumMemberChannels::getMaxValue()` (`mpe/zone_num_member_channels.cpp:33-54`), with
///        `Number::getMinValue`'s 0 as the floor. A context naming no cable has an empty range.
IntRange mpe_zone_channels_range(const MenuContext& ctx, const MenuState& state);

/// @brief `ZoneNumMemberChannels::readCurrentValue()` (`:56-64`) -- the member-channel count on the
///        port `MenuState::mpe_direction` names, for the zone `MenuState::mpe_zone` names.
int32_t mpe_zone_channels_value(const MenuContext& ctx, const MenuState& state);
/// @}

/// @brief `mpe::DirectionSelector` (`gui/menu_item/mpe/direction_selector.{h,cpp}`).
///
/// A `CustomItem` rather than a `Kind::Enum` node for two independent reasons, either of which
/// would be enough: its value is `MenuState::mpe_direction` (see the file note), which a
/// `Binding::get`/`set` cannot reach; and its title is computed from that value at render time,
/// which `Node::title` cannot express.
class MpeDirectionItem final : public CustomItem {
public:
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
};

/// @brief `mpe::ZoneSelector` (`gui/menu_item/mpe/zone_selector.{h,cpp}`). Same shape as
///        `MpeDirectionItem`, over `MenuState::mpe_zone`, minus the runtime title.
class MpeZoneItem final : public CustomItem {
public:
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
};

/// @brief `mpe::ZoneNumMemberChannels` (`gui/menu_item/mpe/zone_num_member_channels.{h,cpp}`).
///
/// The one value editor here, and a `CustomItem` for the same reason the two selectors are: which
/// half of which `MIDIPort` it addresses is `MenuState::mpe_direction` / `mpe_zone`, and its bounds
/// move with them (`getMaxValue`, `zone_num_member_channels.cpp:33-54`). `on_focus` is
/// `Number::beginSession`'s `readCurrentValue` (there is no override of its own -- legacy's is
/// commented out at `zone_num_member_channels.h:28`), `render` is `IntegerWithOff`'s huge-number
/// screen (`gui/menu_item/integer.cpp:41-50`), and `commit` is `writeCurrentValue`
/// (`zone_num_member_channels.cpp:66-85`) including its MCM `sendRPN` tail.
class MpeZoneChannelsItem final : public CustomItem {
public:
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
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

/// `zoneNumMemberChannelsMenu` (`zone_num_member_channels.cpp:27`), titled by its own `getTitle()`
/// override (`zone_num_member_channels.h:34-36`) because legacy's name is `EMPTY_STRING`.
inline constexpr Node mpe_zone_num_member_channels =
    custom(sid(deluge::l10n::String::STRING_FOR_NUM_MEMBER_CH_MENU_TITLE), &kMpeZoneChannelsItem);

/// `ZoneSelector::selectButtonPress` (`zone_selector.cpp:32-34`) -- unconditional.
/// @{
inline constexpr const Node* kMpeZoneChain[] = {&mpe_zone_num_member_channels};
inline const Node* mpe_zone_chain(const MenuContext&, const MenuState&) {
	return &mpe_zone_num_member_channels;
}
/// @}

inline constexpr Node mpe_zone =
    with_select_chain(custom(sid(deluge::l10n::String::STRING_FOR_MPE), &kMpeZoneItem), mpe_zone_chain, kMpeZoneChain);

/// `DirectionSelector::selectButtonPress` (`direction_selector.cpp:31-33`) -- unconditional.
/// @{
inline constexpr const Node* kMpeDirectionChain[] = {&mpe_zone};
inline const Node* mpe_direction_chain(const MenuContext&, const MenuState&) {
	return &mpe_zone;
}
/// @}

inline constexpr Node mpe_direction = with_select_chain(
    custom(sid(deluge::l10n::String::STRING_FOR_MPE), &kMpeDirectionItem), mpe_direction_chain, kMpeDirectionChain);

/// `DefaultVelocityToLevel::getMaxValue` is `kMaxMenuValue` (`default_velocity_to_level.h:28`);
/// `IntegerWithOff` draws 0 as "OFF" (`gui/menu_item/integer.cpp:41-50`), which is
/// `ValueStyle::off_at_zero`.
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

/// `midiDeviceMenu`'s children, in the literal's own order (`gui/ui/menus.cpp:1447-1451`).
inline constexpr const Node* kMidiDeviceChildren[] = {
    &mpe_direction, &device_default_velocity_to_level, &device_send_clock, &device_receive_clock, &device_is_relative,
};

/// `midi::Device midiDeviceMenu` (`gui/ui/menus.cpp:1444-1453`). See the file note on its title.
inline constexpr Node midi_device = submenu(sid(deluge::l10n::String::STRING_FOR_MIDI_DEVICES), kMidiDeviceChildren);

/// `Devices::selectButtonPress` (`gui/menu_item/midi/devices.cpp:154-156`) -- unconditional.
/// `settings.h`'s `midi_devices` carries these.
/// @{
inline constexpr const Node* kMidiDevicesChain[] = {&midi_device};
inline const Node* midi_devices_chain(const MenuContext&, const MenuState&) {
	return &midi_device;
}
/// @}

} // namespace deluge::scenes::menu::nodes
