#pragma once

/// @brief The settings root as data -- `origin/main:src/deluge/gui/ui/menus.cpp`'s `settingsRootMenu`
///        literal and everything under it.
///
/// Fourteen children, in the literal's own order: the actions page, the CV and gate output
/// selectors, the trigger-clock page, the MIDI page, the USB audio page, the defaults page, the pads
/// page, the sample browser preview mode, the play cursor, the recording page, the community
/// features, the battery readout and the firmware version.
///
/// Unlike the sound tree, almost nothing here is a `ParamRef`: a setting is a DEVICE row the device port keeps (the
/// community features, the audio, the clocks, the MIDI and MIDI follow settings and the CV and gate outputs among
/// them), or a learned MIDI input -- a global command's or a follow input's -- which the MIDI devices port reads and
/// the learn port writes. No hook here reads or writes the device's state itself.
///
/// @note **No `Kind::Custom` placeholder remains here.** Every item whose state or action sits behind the
///       `hid/`/`gui/` wall this layer may not cross has a real node:
///
///         * **`hid/` state.** `flash_status` (`PadLEDs::flashCursor`, `Status::readCurrentValue` and
///         `Status::writeCurrentValue` in `origin/main:src/deluge/gui/menu_item/flash/status.h`), `shortcuts_version`
///           and the six `pad_colour_*` (`menu_item::Colour::value`) are DEVICE rows' nodes, which the device port
///           reads and writes. `pads/shortcutsVersion` selects a shortcut *layer* of this layer's own tables
///           (`menu::shortcuts_version`).
///           `battery_level` and `firmware_version` read the UI facts port (`UiFacts::battery_millivolts`,
///           `UiFacts::firmware_version`), so they are real, finished `CustomItem`s; see "12-13. Battery and
///           firmware" below.
///         * **UI navigation.** `cv_selection` / `gate_selection`
///         (`origin/main:src/deluge/gui/menu_item/cv/selection.h`,
///           `origin/main:src/deluge/gui/menu_item/gate/selection.h`: `Selection`s whose select press *navigates* to a
///           page rather than committing a value) and `midi_devices` (`Devices` in
///           `origin/main:src/deluge/gui/menu_item/midi/devices.h`, a scrolling device list) are the real items
///           `CvSelectionItem`, `GateSelectionItem` and `MidiDevicesItem`
///           (`nodes/dynamic.h`), with a `MenuWalkState::source_choice` / `midi_device_choice` write-back.
///           The five `reset_*` items are a real `ResetItem` opening `context_menu::ContextMenuScene`
///           through `MenuWalkState::pending_scene` (see "1. Actions" below). The twelve `midi_command_*`
///           (`origin/main:src/deluge/gui/menu_item/midi/command.h`, MIDI-learn items) are a real `MidiCommandItem`:
///           they navigate nowhere, they bind an incoming MIDI message to a `project::GlobalCommand`, which reaches
///           them through `MenuLearn::offer` in learn mode; `MenuScene::unlearn` forgets it.
///         * **Items with genuinely dynamic state.** `default_tempo`, `default_swing_amount` and
///           `default_key` are `Kind::Range` -- exactly the three consumers that
///           justify a `Range` kind -- and each end is a DEVICE row (`defaults/tempo/lower`, `.../upper`), not a
///           menu-item member. See `kDefaultTempoSpec` below and `ui/widgets/oled/values.h`'s `range_value`.
///           `default_active_scale` is `nodes::ActiveScalesItem` (`nodes/scales.h`); legacy's
///           `ActiveScaleMenu` (`origin/main:src/deluge/gui/menu_item/active_scales.h`) renders through
///           `Submenu::drawSubmenuItemsForOled` with a checkbox, which `ui::widgets::oled::list` already is.
///
/// @note **`cvSubmenu`, `gateModeMenu` and `gateOffTimeMenu` are declared, but not as children of anything.** They are
///       not children of `settingsRootMenu` -- legacy reaches them only from `cv::Selection::selectButtonPress` /
///       `gate::Selection::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/cv/selection.h`,
///       `origin/main:src/deluge/gui/menu_item/gate/selection.h`) -- so the port hangs them off `cv_selection` /
///       `gate_selection`'s `Node::chain_targets` instead, which is what the reachability walk follows to number and
///       validate them without inventing a parent main's menu tree does not have. Every chaining override in
///       `gui/menu_item/` was swept for this; these two selectors and `midi_devices` (below) are the only ones that
///       need it.
///
/// @note **Both halves of those two chains are closed.** (1) Legacy's selectors
///       *write* `soundEditor.currentSourceIndex` before navigating, and `MenuContext::source_index`
///       is still not writable from a `SelectChainFn` (handed a `const MenuContext&`) -- so the
///       selector records its cursor in `MenuWalkState::source_choice` and `MenuScene::after_render`
///       applies it to the scene's own mutable context ahead of the same tick's `select_chain`
///       call, the shape `MultiRangeItem`/`range_choice` established. (2) `cv_selection` and
///       `gate_selection` are real `CustomItem`s now (`nodes/dynamic.h`), so their renderers do
///       report a select press and the interpreter does fire the hooks. What is still owed is
///       narrower and is listed in `nodes/dynamic.h`: the formatted "CV OUTPUT 3" titles. (The
///       third chain, `midi_devices` -> the per-device page, is declared just below; the page
///       itself is `nodes/midi_device.h`.)
///
/// @note **What a write does besides storing is the host's.** A DEVICE row's node has no setter: the host's device
///       port stores the value and does what storing it implies (both screensavers take a new mode or timeout, the
///       pads dim to a new brightness and re-render for a new colour, main's canvas and the guest's panels take a new
///       highlighting, leaving the slow cursor flash clears its tick squares, sticky shift reaches the shift
///       machines, rounded corners restyles the display, a default velocity or threshold mode reaches the open song,
///       the resolution moves the swing interval, a trigger clock's pulses resync the ticks, a MIDI clock starts or
///       stops, a CV output's voltage goes out again, a gate switches and starts or stops carrying the clock or the run
///       signal, a USB audio enable that waits for a restart says so), and the open menu's own half is the node's
///       `after_write` (horizontal menus).

#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/column_hooks.h"
#include "ui/scenes/menu/nodes/context_menu_items.h" // the five kReset*Item instances
#include "ui/scenes/menu/nodes/detail.h"
#include "ui/scenes/menu/nodes/dynamic.h"      // the CV/gate selectors and the MIDI device list
#include "ui/scenes/menu/nodes/midi_command.h" // the twelve `MidiCommandItem`s
#include "ui/scenes/menu/nodes/midi_device.h"  // the per-device page `midi_devices` chains to
#include "ui/scenes/menu/nodes/roots.h"        // swing_interval_turn
#include "ui/scenes/menu/nodes/scales.h"       // kDefaultActiveScalesItem
#include "ui/scenes/menu/relevance.h"

#include "plugin/types/display_mapping.h"
#include "project/device_settings.h"
#include "project/features.h"
#include "project/setting_group.h"

#include <limits>
#include <optional>
#include <span>

namespace deluge::ui::scenes::menu {
struct MenuContext;
} // namespace deluge::ui::scenes::menu

namespace deluge::ui::scenes::menu::nodes {

/// @name Hooks. Bodies (and their legacy citations) in `nodes/settings.cpp`.
/// @{

/// @brief `midi::FollowChannel`'s read and write (`origin/main:src/deluge/gui/menu_item/midi/follow/follow_channel.h`),
///        with the input as an argument rather than a constructor member: the channel or zone the MIDI devices port
///        reads, `kFollowChannelUnassigned` for nothing learned, and a write through the learn port that keeps the
///        device.
///
/// The nineteen nodes are nineteen distinct addresses of the two templates below -- the same
/// device `relevance.h` uses for its `osc0_*`/`osc1_*` pairs, and for the same reason: a `Node`
/// carries no per-item payload, so what legacy stored in a member has to be a template argument.
/// @{
int32_t follow_channel_get(const MenuContext& ctx, project::FollowInput input);
void follow_channel_set(const MenuContext& ctx, project::FollowInput input, int32_t value);
template <project::FollowInput Input>
int32_t get_follow_channel(const MenuContext& ctx) {
	return follow_channel_get(ctx, Input);
}
template <project::FollowInput Input>
void set_follow_channel(const MenuContext& ctx, int32_t value) {
	follow_channel_set(ctx, Input, value);
}
/// @}

/// @brief `midi::FollowChannel::learnNoteOn` (`origin/main:src/deluge/gui/menu_item/midi/follow/follow_channel.h`) and
///        `learnCC`: a note-on or a CC learns the device it came on and its channel, through the learn port.
/// @return Whether it learned: a note-on or a CC from a device the MIDI devices port knows.
bool follow_channel_learn(const MenuContext& ctx, project::FollowInput input, const deluge::ui::shell::UiEvent& event);
/// @brief `midi::FollowChannel::unlearnAction`: the input goes back to unassigned. @return Whether there was a learn
///        port to unlearn it.
bool follow_channel_unlearn(const MenuContext& ctx, project::FollowInput input);
template <project::FollowInput Input>
bool learn_follow_channel(const MenuContext& ctx, const deluge::ui::shell::UiEvent& event) {
	return follow_channel_learn(ctx, Input, event);
}
template <project::FollowInput Input>
bool unlearn_follow_channel(const MenuContext& ctx) {
	return follow_channel_unlearn(ctx, Input);
}

/// @brief `midi::FollowChannel::selectEncoderAction`: past either end the channel becomes unassigned
///        (`kFollowChannelUnassigned`, whose write forgets the learned device), and from unassigned a turn up lands on
///        channel 1 and a turn down on the MPE upper zone.
/// @param value  The channel or zone before the turn.
/// @param offset The turn's detents.
/// @return The channel or zone after it.
std::optional<int32_t> turn_follow_channel(const MenuContext& ctx, int32_t value, int32_t offset);

/// @brief `midi::FollowChannel::drawInteger`: the input-differentiation line, the device and the channel or zone, in
///        the MIDI command screen's three rows.
/// @param input  The follow input the node edits.
/// @param ctx    The menu's context, whose MIDI devices port reads the binding and names the device.
/// @param f      The frame the item draws in.
/// @param value  The channel or zone after this tick's edit.
/// @param scroll The side-scroll state of the device's name.
void follow_channel_draw(project::FollowInput input, const MenuContext& ctx, deluge::ui::widgets::oled::OledFrame& f,
                         int32_t value, deluge::ui::widgets::core::ScrollText& scroll);
template <project::FollowInput Input>
void draw_follow_channel(deluge::ui::widgets::oled::OledFrame& f, const MenuContext& ctx, int32_t value,
                         deluge::ui::widgets::core::ScrollText& scroll) {
	follow_channel_draw(Input, ctx, f, value, scroll);
}

/// @brief `midi::FollowKitRootNote::learnNoteOn`
/// (`origin/main:src/deluge/gui/menu_item/midi/follow/follow_kit_root_note.h`): a note-on writes its note to the
/// DEVICE `midiFollow/kitRootNote` row.
/// @param ctx   The menu's context, whose device port takes the write.
/// @param event The MIDI message offered for learning.
/// @return Whether it learned: a note-on the port stored.
bool learn_follow_kit_root_note(const MenuContext& ctx, const deluge::ui::shell::UiEvent& event);

/// `defaults::SwingInterval::getNoteLengthName`: the default resolution, the DEVICE `defaults/magnitude` row.
int32_t default_resolution_magnitude(const MenuContext& ctx);
/// `sync_level::RelativeToSong::getNoteLengthName` names value v as magnitude 3 - v; `syncValueToString` at tick
/// magnitude 1 is that name.
int32_t relative_to_song_magnitude(const MenuContext& ctx);
/// @brief The pad brightness item's turn, `PadBrightness::selectEncoderAction`
///        (`origin/main:src/deluge/gui/menu_item/defaults/pad_brightness.h`): four a detent, from the
///        dimmest level to the brightest (`project::kDimmestPadLevel`, `kBrightestPadLevel`).
/// @param ctx    The menu's context (unused).
/// @param value  The value shown, four times the level.
/// @param offset The detents turned.
/// @return The value the turn lands on.
std::optional<int32_t> pad_brightness_turn(const MenuContext& ctx, int32_t value, int32_t offset);

/// @brief The HorizontalMenus feature's half in the open menu (`MenuScene::set_horizontal_menus`), once the device
///        stored a write of its row: `HorizontalMenu::renderingStyle()`
///        (`origin/main:src/deluge/gui/menu_item/horizontal_menu.cpp`) reads the setting live, and the open menu's
///        context holds the copy it lays out by.
void apply_horizontal_menus(const MenuContext& ctx, int32_t value);

/// @}

// ---------------------------------------------------------------------------------------------
// 1. Actions

/// The five reset items are `nodes::ContextMenuItem`s (`nodes/context_menu_items.h`) -- one class
/// for every menu item that opens `context_menu::ContextMenuScene` (`audio_source_selector` is the
/// sixth such item). See that header.

inline constexpr Node reset_flash = custom(sid(String::STRING_FOR_RESET_FLASH), &kResetFlashItem);
inline constexpr Node reset_community_features =
    custom(sid(String::STRING_FOR_RESET_COMMUNITY_FEATURES), &kResetCommunityFeaturesItem);
inline constexpr Node reset_midi_follow = custom(sid(String::STRING_FOR_RESET_MIDI_FOLLOW), &kResetMidiFollowItem);
inline constexpr Node reset_midi_devices = custom(sid(String::STRING_FOR_RESET_MIDI_DEVICES), &kResetMidiDevicesItem);
inline constexpr Node reset_all = custom(sid(String::STRING_FOR_FACTORY_RESET), &kResetAllItem);

inline constexpr const Node* kResetSettingsChildren[] = {&reset_flash, &reset_community_features, &reset_midi_follow,
                                                         &reset_midi_devices, &reset_all};
inline constexpr Node reset_settings = submenu(sid(String::STRING_FOR_RESET_SETTINGS), kResetSettingsChildren);
inline constexpr const Node* kSettingsActionsChildren[] = {&reset_settings};
inline constexpr Node settings_actions = submenu(sid(String::STRING_FOR_ACTIONS), kSettingsActionsChildren);

// ---------------------------------------------------------------------------------------------
// 2-3. The CV and gate output selectors, and the three pages only their select press reaches
//
// `cvSubmenu` (`origin/main:src/deluge/gui/ui/menus.cpp`), `gateModeMenu` and `gateOffTimeMenu` are not
// children of `settingsRootMenu` or of anything else: legacy reaches all three only through
// `cv::Selection::selectButtonPress` / `gate::Selection::selectButtonPress`. They are declared
// here, ahead of the two selectors, because a chain hook needs their addresses;
// `Node::chain_targets` is what puts them in the reachability walk so they get node ids.

/// `cv::Volts` (`origin/main:src/deluge/gui/menu_item/cv/volts.h`): the chosen output's `cv/<n>/voltsPerOctave` row,
/// 0..200 at two places, the cursor on the tenths, and Hz/V at 0 with no cursor (`drawPixelsForOled`,
/// `horizontalEncoderAction`).
inline constexpr Node cv_volts = with_default_edit_pos(
    with_decimal_word(
        with_title_arg(
            with_title(with_decimal_places(setting_item(sid(String::STRING_FOR_VOLTS_PER_OCTAVE), Kind::Decimal,
                                                        device_output_row(project::device_path::kCvVoltsPerOctave)),
                                           2),
                       sid(String::STRING_FOR_CV_V_PER_OCTAVE_MENU_TITLE)),
            kTitleArgSourceIndex),
        cv_volts_word),
    1);
/// `cv::Transpose` (`origin/main:src/deluge/gui/menu_item/cv/transpose.h`): the chosen output's `cv/<n>/transpose` row,
/// the same `{-9600, 9600}` two-place packing the oscillator transposes use (`sound_osc.h`'s `kTransposeRange`).
inline constexpr Node cv_transpose =
    with_title_arg(with_title(with_decimal_places(setting_item(sid(String::STRING_FOR_TRANSPOSE), Kind::Decimal,
                                                               device_output_row(project::device_path::kCvTranspose)),
                                                  2),
                              sid(String::STRING_FOR_CV_TRANSPOSE_MENU_TITLE)),
                   kTitleArgSourceIndex);
inline constexpr const Node* kCvSubmenuChildren[] = {&cv_volts, &cv_transpose};
/// `cvSubmenu{STRING_FOR_CV_OUTPUT_N, {&cvVoltsMenu, &cvTransposeMenu}}` (`origin/main:src/deluge/gui/ui/menus.cpp`).
/// Its title is a `FormattedTitle` legacy re-formats per output (`setCvNumberForTitle` in the same file), as are
/// `cv_volts`' and `cv_transpose`'s: `kTitleArgSourceIndex` numbers all three by the output the selector chose.
inline constexpr Node cv_submenu =
    with_title_arg(submenu(sid(String::STRING_FOR_CV_OUTPUT_N), kCvSubmenuChildren), kTitleArgSourceIndex);

/// `gate::Mode` (`origin/main:src/deluge/gui/menu_item/gate/mode.h`): V-TRIG and S-TRIG always, plus CLOCK or RUN
/// SIGNAL on the two outputs that carry them -- legacy's `updateOptions(value)`, which `gate::Selection` calls on its
/// way in (`Selection::selectButtonPress` in `origin/main:src/deluge/gui/menu_item/gate/selection.h`). Here each
/// output's `gate/<n>/mode` row lists its own choices, and the node edits the row of the output the selector chose.
///
/// Legacy's `gateModeMenu` is default-constructed in `origin/main:src/deluge/gui/ui/menus.cpp`, so it has **no name at
/// all** -- only a `FormattedTitle` (`Mode::Mode` in `origin/main:src/deluge/gui/menu_item/gate/mode.h`). A
/// `Node::name` of 0 would trip `validate_tree`'s `MissingTitle` (the builders default `title` to `name`), so the port
/// uses the title for both, as it does for `gate_off_time` just below.
inline constexpr Node gate_mode =
    with_title_arg(with_title(setting_item(sid(String::STRING_FOR_GATE_MODE_TITLE), Kind::Enum,
                                           device_output_row(project::device_path::kGateMode)),
                              sid(String::STRING_FOR_GATE_MODE_TITLE)),
                   kTitleArgSourceIndex);
/// `gate::OffTime` (`origin/main:src/deluge/gui/menu_item/gate/off_time.h`), the `gate/minOffTime` row -- the one page
/// here with no per-output state at all. Legacy names it `EMPTY_STRING` with the title
/// `STRING_FOR_MINIMUM_OFF_TIME` (`gateOffTimeMenu` in `origin/main:src/deluge/gui/ui/menus.cpp`); a `Node` with
/// `name == 0` would trip `validate_tree`'s `MissingTitle`, so the port uses the title for both. `getDefaultEditPos` is
/// 1: the cursor starts on the units.
inline constexpr Node gate_off_time =
    with_default_edit_pos(with_decimal_places(setting_item(sid(String::STRING_FOR_MINIMUM_OFF_TIME), Kind::Decimal,
                                                           device_row(project::device_path::kGateMinOffTime)),
                                              1),
                          1);

/// @brief `cv::Selection::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/cv/selection.h`) and
///        `gate::Selection::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/gate/selection.h`).
///
/// Both chain unconditionally; the gate one picks the off-time page on the one-past-the-last option
/// and the mode page otherwise. Legacy also assigns `soundEditor.currentSourceIndex` on the way
/// through -- the port's equivalent is `MenuContext::source_index`, which a `SelectChainFn` (handed
/// a `const MenuContext&`) cannot write. See the file note below on what that costs today.
/// @{
inline constexpr const Node* kCvSelectionChain[] = {&cv_submenu};
inline constexpr const Node* kGateSelectionChain[] = {&gate_mode, &gate_off_time};
inline const Node* cv_selection_chain(const MenuContext&, const MenuState&) {
	return &cv_submenu;
}
/// Body in the `.cpp`: it reads `MenuContext::source_index`, and this header only sees the forward
/// declaration `node.h` makes (the whole reason every other hook here is declared, not defined).
const Node* gate_selection_chain(const MenuContext& ctx, const MenuState& state);
/// @}

inline constexpr Node cv_selection =
    with_value_names(with_select_chain(with_title(custom(sid(String::STRING_FOR_CV), &kCvSelectionItem),
                                                  sid(String::STRING_FOR_CV_OUTPUTS)),
                                       cv_selection_chain, kCvSelectionChain),
                     kCvOutputNames);
inline constexpr Node gate_selection =
    with_value_names(with_select_chain(with_title(custom(sid(String::STRING_FOR_GATE), &kGateSelectionItem),
                                                  sid(String::STRING_FOR_GATE_OUTPUTS)),
                                       gate_selection_chain, kGateSelectionChain),
                     kGateOutputNames);

// ---------------------------------------------------------------------------------------------
// 4. Trigger clock

/// `trigger::in::PPQN` (`origin/main:src/deluge/gui/menu_item/trigger/in/ppqn.h`), whose range is
/// `menu_item::PPQN`'s (`origin/main:src/deluge/gui/menu_item/ppqn.h`): its write resyncs the song's ticks to the
/// trigger clock it follows; the device port's store does it.
inline constexpr Node trigger_in_ppqn =
    with_title(setting_item(sid(String::STRING_FOR_PPQN), Kind::Int, device_row(project::device_path::kTriggerInPpqn)),
               sid(String::STRING_FOR_INPUT_PPQN));
inline constexpr Node trigger_in_auto_start = setting_item(sid(String::STRING_FOR_AUTO_START), Kind::Toggle,
                                                           device_row(project::device_path::kTriggerInAutoStart));
inline constexpr const Node* kTriggerClockInChildren[] = {&trigger_in_ppqn, &trigger_in_auto_start};
inline constexpr Node trigger_clock_in =
    submenu(sid(String::STRING_FOR_INPUT), kTriggerClockInChildren, Layout::Vertical, nullptr,
            sid(String::STRING_FOR_T_CLOCK_INPUT_MENU_TITLE));

/// `trigger::out::PPQN` (`origin/main:src/deluge/gui/menu_item/trigger/out/ppqn.h`): its write resyncs the trigger
/// clock's output to the song's ticks; the device port's store does it.
inline constexpr Node trigger_out_ppqn =
    with_title(setting_item(sid(String::STRING_FOR_PPQN), Kind::Int, device_row(project::device_path::kTriggerOutPpqn)),
               sid(String::STRING_FOR_OUTPUT_PPQN));
inline constexpr const Node* kTriggerClockOutChildren[] = {&trigger_out_ppqn};
inline constexpr Node trigger_clock_out =
    submenu(sid(String::STRING_FOR_OUTPUT), kTriggerClockOutChildren, Layout::Vertical, nullptr,
            sid(String::STRING_FOR_T_CLOCK_OUT_MENU_TITLE));

inline constexpr const Node* kTriggerClockChildren[] = {&trigger_clock_in, &trigger_clock_out};
inline constexpr Node trigger_clock = submenu(sid(String::STRING_FOR_TRIGGER_CLOCK), kTriggerClockChildren);

// ---------------------------------------------------------------------------------------------
// 5. MIDI

/// `midi::SendClock` (`origin/main:src/deluge/gui/menu_item/midi/send_clock.h`) and `midi::ReceiveClock`
/// (`origin/main:src/deluge/gui/menu_item/midi/receive_clock.h`): a write starts or stops the clock at once, which the
/// device port's store does.
inline constexpr Node midi_clock_out = with_title(
    setting_item(sid(String::STRING_FOR_OUTPUT), Kind::Toggle, device_row(project::device_path::kMidiClockOut)),
    sid(String::STRING_FOR_MIDI_CLOCK_OUT));
inline constexpr Node midi_clock_in = with_title(
    setting_item(sid(String::STRING_FOR_INPUT), Kind::Toggle, device_row(project::device_path::kMidiClockIn)),
    sid(String::STRING_FOR_MIDI_CLOCK_IN));
inline constexpr Node tempo_magnitude_matching =
    setting_item(sid(String::STRING_FOR_TEMPO_MAGNITUDE_MATCHING), Kind::Toggle,
                 device_row(project::device_path::kTempoMagnitudeMatching));
inline constexpr const Node* kMidiClockChildren[] = {&midi_clock_out, &midi_clock_in, &tempo_magnitude_matching};
inline constexpr Node midi_clock = submenu(sid(String::STRING_FOR_CLOCK), kMidiClockChildren, Layout::Vertical, nullptr,
                                           sid(String::STRING_FOR_MIDI_CLOCK));

// A follow channel holds a channel 0-15, the MPE lower and upper zones (16, 17) or `kFollowChannelUnassigned` (255) for
// unassigned; `turn_follow_channel` moves between them, so the range only has to admit them all.
/// @brief The follow-channel menu value for an input with no channel learned. A menu value only: the learn port stores
///        no binding for it.
inline constexpr int32_t kFollowChannelUnassigned = 255;
inline constexpr IntRange kFollowChannelRange{0, kFollowChannelUnassigned, 1};

/// One follow-channel node. `n` is its `project::FollowInput`'s value (A = 0, B = 1, C = 2, TRACK_1 = 3 .. TRACK_16 =
/// 18), which also numbers a track's title.
#define DELUGE_FOLLOW_CHANNEL(node_name, n, name_id)                                                                   \
	inline constexpr Node node_name = with_leaf_draw(                                                                  \
	    with_turn(                                                                                                     \
	        with_learn(with_title_arg(                                                                                 \
	                       int_item(sid(String::name_id), get_follow_channel<static_cast<project::FollowInput>(n)>,    \
	                                set_follow_channel<static_cast<project::FollowInput>(n)>, kFollowChannelRange),    \
	                       (n) >= 3 ? (n) - 2 : 0),                                                                    \
	                   learn_follow_channel<static_cast<project::FollowInput>(n)>,                                     \
	                   unlearn_follow_channel<static_cast<project::FollowInput>(n)>),                                  \
	        turn_follow_channel),                                                                                      \
	    draw_follow_channel<static_cast<project::FollowInput>(n)>)

DELUGE_FOLLOW_CHANNEL(midi_follow_channel_a, 0, STRING_FOR_FOLLOW_CHANNEL_A);
DELUGE_FOLLOW_CHANNEL(midi_follow_channel_b, 1, STRING_FOR_FOLLOW_CHANNEL_B);
DELUGE_FOLLOW_CHANNEL(midi_follow_channel_c, 2, STRING_FOR_FOLLOW_CHANNEL_C);
DELUGE_FOLLOW_CHANNEL(midi_follow_track1, 3, STRING_FOR_FOLLOW_CHANNEL_TRACK);
DELUGE_FOLLOW_CHANNEL(midi_follow_track2, 4, STRING_FOR_FOLLOW_CHANNEL_TRACK);
DELUGE_FOLLOW_CHANNEL(midi_follow_track3, 5, STRING_FOR_FOLLOW_CHANNEL_TRACK);
DELUGE_FOLLOW_CHANNEL(midi_follow_track4, 6, STRING_FOR_FOLLOW_CHANNEL_TRACK);
DELUGE_FOLLOW_CHANNEL(midi_follow_track5, 7, STRING_FOR_FOLLOW_CHANNEL_TRACK);
DELUGE_FOLLOW_CHANNEL(midi_follow_track6, 8, STRING_FOR_FOLLOW_CHANNEL_TRACK);
DELUGE_FOLLOW_CHANNEL(midi_follow_track7, 9, STRING_FOR_FOLLOW_CHANNEL_TRACK);
DELUGE_FOLLOW_CHANNEL(midi_follow_track8, 10, STRING_FOR_FOLLOW_CHANNEL_TRACK);
DELUGE_FOLLOW_CHANNEL(midi_follow_track9, 11, STRING_FOR_FOLLOW_CHANNEL_TRACK);
DELUGE_FOLLOW_CHANNEL(midi_follow_track10, 12, STRING_FOR_FOLLOW_CHANNEL_TRACK);
DELUGE_FOLLOW_CHANNEL(midi_follow_track11, 13, STRING_FOR_FOLLOW_CHANNEL_TRACK);
DELUGE_FOLLOW_CHANNEL(midi_follow_track12, 14, STRING_FOR_FOLLOW_CHANNEL_TRACK);
DELUGE_FOLLOW_CHANNEL(midi_follow_track13, 15, STRING_FOR_FOLLOW_CHANNEL_TRACK);
DELUGE_FOLLOW_CHANNEL(midi_follow_track14, 16, STRING_FOR_FOLLOW_CHANNEL_TRACK);
DELUGE_FOLLOW_CHANNEL(midi_follow_track15, 17, STRING_FOR_FOLLOW_CHANNEL_TRACK);
DELUGE_FOLLOW_CHANNEL(midi_follow_track16, 18, STRING_FOR_FOLLOW_CHANNEL_TRACK);

#undef DELUGE_FOLLOW_CHANNEL

inline constexpr const Node* kMidiFollowChannelChildren[] = {
    &midi_follow_channel_a, &midi_follow_channel_b, &midi_follow_channel_c, &midi_follow_track1,  &midi_follow_track2,
    &midi_follow_track3,    &midi_follow_track4,    &midi_follow_track5,    &midi_follow_track6,  &midi_follow_track7,
    &midi_follow_track8,    &midi_follow_track9,    &midi_follow_track10,   &midi_follow_track11, &midi_follow_track12,
    &midi_follow_track13,   &midi_follow_track14,   &midi_follow_track15,   &midi_follow_track16};
inline constexpr Node midi_follow_channel = submenu(sid(String::STRING_FOR_CHANNEL), kMidiFollowChannelChildren);

inline constexpr Node midi_follow_kit_root_note =
    with_learn(setting_item(sid(String::STRING_FOR_FOLLOW_KIT_ROOT_NOTE), Kind::Int,
                            device_row(project::device_path::kFollowKitRootNote)),
               learn_follow_kit_root_note);

/// `FollowFeedbackChannelType::getOptions`
/// (`origin/main:src/deluge/gui/menu_item/midi/follow/follow_feedback_channel_type.h`) joins the track's label to each
/// follow channel's for its last three; the row names them with strings of the same text.
inline constexpr Node midi_follow_feedback_channel =
    setting_item(sid(String::STRING_FOR_CHANNEL), Kind::Enum, device_row(project::device_path::kFollowFeedbackChannel));
inline constexpr Node midi_follow_feedback_automation =
    setting_item(sid(String::STRING_FOR_FOLLOW_FEEDBACK_AUTOMATION), Kind::Enum,
                 device_row(project::device_path::kFollowFeedbackAutomation));
inline constexpr Node midi_follow_feedback_filter =
    setting_item(sid(String::STRING_FOR_FOLLOW_FEEDBACK_FILTER), Kind::Toggle,
                 device_row(project::device_path::kFollowFeedbackFilter));
inline constexpr const Node* kMidiFollowFeedbackChildren[] = {
    &midi_follow_feedback_channel, &midi_follow_feedback_automation, &midi_follow_feedback_filter};
inline constexpr Node midi_follow_feedback =
    submenu(sid(String::STRING_FOR_FOLLOW_FEEDBACK), kMidiFollowFeedbackChildren);

inline constexpr Node midi_follow_display_param = setting_item(
    sid(String::STRING_FOR_FOLLOW_DISPLAY_PARAM), Kind::Toggle, device_row(project::device_path::kFollowDisplayParam));
inline constexpr const Node* kMidiFollowChildren[] = {&midi_follow_channel, &midi_follow_kit_root_note,
                                                      &midi_follow_feedback, &midi_follow_display_param};
inline constexpr Node midi_follow = submenu(sid(String::STRING_FOR_FOLLOW_TITLE), kMidiFollowChildren);

inline constexpr Node midi_select_kit_row = setting_item(sid(String::STRING_FOR_SELECT_KIT_ROW), Kind::Toggle,
                                                         device_row(project::device_path::kMidiSelectKitRow));
inline constexpr Node midi_thru =
    setting_item(sid(String::STRING_FOR_MIDI_THRU), Kind::Toggle, device_row(project::device_path::kMidiThru));

/// `midi::Transpose` (`origin/main:src/deluge/gui/menu_item/midi/transpose.h`): its options are the row's two choices.
/// The chord method the device may hold reads clamped to chromatic, as the item's list shows it.
inline constexpr Node midi_transpose_method =
    setting_item(sid(String::STRING_FOR_TRANSPOSE), Kind::Enum, device_row(project::device_path::kMidiTranspose));

/// `midi::Takeover` (`origin/main:src/deluge/gui/menu_item/midi/takeover.h`).
inline constexpr Node midi_takeover =
    setting_item(sid(String::STRING_FOR_TAKEOVER), Kind::Enum, device_row(project::device_path::kMidiTakeover));

/// The twelve `midi::Command` learn items (`origin/main:src/deluge/gui/ui/menus.cpp`), each backed by its own
/// `MidiCommandItem` (`nodes/midi_command.h`) -- one per `project::GlobalCommand`.
/// The order is `midiCommandsMenu`'s own (`origin/main:src/deluge/gui/ui/menus.cpp`), which is NOT the enum's order.
/// @{
inline constexpr Node midi_command_play = with_learn_mode(custom(sid(String::STRING_FOR_PLAY), &kMidiCommandPlayItem));
inline constexpr Node midi_command_restart =
    with_learn_mode(custom(sid(String::STRING_FOR_RESTART), &kMidiCommandRestartItem));
inline constexpr Node midi_command_record =
    with_learn_mode(custom(sid(String::STRING_FOR_RECORD), &kMidiCommandRecordItem));
inline constexpr Node midi_command_tap =
    with_learn_mode(custom(sid(String::STRING_FOR_TAP_TEMPO), &kMidiCommandTapItem));
inline constexpr Node midi_command_undo = with_learn_mode(custom(sid(String::STRING_FOR_UNDO), &kMidiCommandUndoItem));
inline constexpr Node midi_command_redo = with_learn_mode(custom(sid(String::STRING_FOR_REDO), &kMidiCommandRedoItem));
inline constexpr Node midi_command_loop = with_learn_mode(custom(sid(String::STRING_FOR_LOOP), &kMidiCommandLoopItem));
inline constexpr Node midi_command_layering_loop =
    with_learn_mode(custom(sid(String::STRING_FOR_LAYERING_LOOP), &kMidiCommandLayeringLoopItem));
inline constexpr Node midi_command_fill = with_learn_mode(custom(sid(String::STRING_FOR_FILL), &kMidiCommandFillItem));
inline constexpr Node midi_command_transpose =
    with_learn_mode(custom(sid(String::STRING_FOR_TRANSPOSE), &kMidiCommandTransposeItem));
inline constexpr Node midi_command_next_song =
    with_learn_mode(custom(sid(String::STRING_FOR_SONG_LOAD_NEXT), &kMidiCommandNextSongItem));
inline constexpr Node midi_command_shift =
    with_learn_mode(custom(sid(String::STRING_FOR_SHIFT), &kMidiCommandShiftItem));
/// @}

inline constexpr const Node* kMidiCommandsChildren[] = {
    &midi_command_play, &midi_command_restart,   &midi_command_record,    &midi_command_tap,
    &midi_command_undo, &midi_command_redo,      &midi_command_loop,      &midi_command_layering_loop,
    &midi_command_fill, &midi_command_transpose, &midi_command_next_song, &midi_command_shift};
inline constexpr Node midi_commands = submenu(sid(String::STRING_FOR_COMMANDS), kMidiCommandsChildren, Layout::Vertical,
                                              nullptr, sid(String::STRING_FOR_MIDI_COMMANDS));

inline constexpr Node midi_input_differentiation = setting_item(
    sid(String::STRING_FOR_DIFFERENTIATE_INPUTS), Kind::Toggle, device_row(project::device_path::kDifferentiateInputs));
/// The device list, and the per-device page a select press on it opens --
/// `Devices::selectButtonPress` returns `&midiDeviceMenu` unconditionally
/// (`origin/main:src/deluge/gui/menu_item/midi/devices.cpp`). The page and its chain hook are `nodes/midi_device.h`;
/// this is the edge that makes them reachable, numbered, validated and dumped.
inline constexpr Node midi_devices = with_select_chain(
    with_title(custom(sid(String::STRING_FOR_DEVICES), &kMidiDevicesItem), sid(String::STRING_FOR_MIDI_DEVICES)),
    midi_devices_chain, kMidiDevicesChain);

inline constexpr const Node* kMidiChildren[] = {
    &midi_clock,    &midi_follow,   &midi_select_kit_row,        &midi_thru,   &midi_transpose_method,
    &midi_takeover, &midi_commands, &midi_input_differentiation, &midi_devices};
inline constexpr Node midi_settings = submenu(sid(String::STRING_FOR_MIDI), kMidiChildren);

// ---------------------------------------------------------------------------------------------
// 5b. USB audio
//
// `usbAudioMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`): whether USB audio is offered, the host's audio mixed
// straight into the outputs, and what the host receives.

/// `usb::Audio` (`origin/main:src/deluge/gui/menu_item/usb/audio.h`): the enable takes effect at the next USB start, so
/// the device port answers a write that no longer matches what this session presents as one for a restart, and the
/// menu says "restart to apply".
inline constexpr Node usb_audio_enabled = with_title(
    setting_item(sid(String::STRING_FOR_ENABLED), Kind::Toggle, device_row(project::device_path::kUsbAudioEnabled)),
    sid(String::STRING_FOR_USB_AUDIO));
inline constexpr Node usb_audio_input =
    with_title(setting_item(sid(String::STRING_FOR_USB_SMART_MONITORING), Kind::Toggle,
                            device_row(project::device_path::kUsbDirectMonitoring)),
               sid(String::STRING_FOR_USB_INPUT));
/// `usb::OutputSource` (`origin/main:src/deluge/gui/menu_item/usb/audio.h`): the main mix, then the line input.
inline constexpr Node usb_audio_output = with_title(
    setting_item(sid(String::STRING_FOR_OUTPUT), Kind::Enum, device_row(project::device_path::kUsbOutputSource)),
    sid(String::STRING_FOR_USB_OUTPUT));
inline constexpr const Node* kUsbAudioChildren[] = {&usb_audio_enabled, &usb_audio_input, &usb_audio_output};
inline constexpr Node usb_audio_settings = submenu(sid(String::STRING_FOR_USB_AUDIO), kUsbAudioChildren);

// ---------------------------------------------------------------------------------------------
// 6. Defaults

inline constexpr Node default_accessibility_shortcuts =
    setting_item(sid(String::STRING_FOR_DEFAULT_ACCESSIBILITY_SHORTCUTS), Kind::Toggle,
                 device_row(project::device_path::kAccessibilityShortcuts));
/// `AccessibilityMenuHighlighting` (`origin/main:src/deluge/gui/menu_item/defaults/accessibility_menu_highlighting.h`):
/// its write restyles main's canvas and the guest's panels; the device port's store does it.
inline constexpr Node default_menu_highlighting =
    setting_item(sid(String::STRING_FOR_DEFAULT_ACCESSIBILITY_MENU_HIGHLIGHTING), Kind::Enum,
                 device_row(project::device_path::kMenuHighlighting));
inline constexpr const Node* kDefaultAccessibilityChildren[] = {&default_accessibility_shortcuts,
                                                                &default_menu_highlighting};
inline constexpr Node default_accessibility =
    submenu(sid(String::STRING_FOR_DEFAULT_ACCESSIBILITY), kDefaultAccessibilityChildren);

inline constexpr Node default_grid_pads_unarm =
    setting_item(sid(String::STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS_UNARM), Kind::Toggle,
                 device_row(project::device_path::kGridEmptyPadsUnarm));
inline constexpr Node default_grid_pads_create_rec =
    setting_item(sid(String::STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS_CREATE_REC), Kind::Toggle,
                 device_row(project::device_path::kGridEmptyPadsCreateRec));
inline constexpr const Node* kDefaultEmptyPadChildren[] = {&default_grid_pads_unarm, &default_grid_pads_create_rec};
inline constexpr Node default_empty_pads =
    submenu(sid(String::STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS), kDefaultEmptyPadChildren);

/// `DefaultGridDefaultActiveMode` (`origin/main:src/deluge/gui/menu_item/defaults/grid_default_active_mode.h`).
inline constexpr Node default_grid_active_mode =
    setting_item(sid(String::STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ACTIVE_MODE), Kind::Enum,
                 device_row(project::device_path::kGridActiveMode));
inline constexpr Node default_grid_green_selection =
    setting_item(sid(String::STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ALLOW_GREEN_SELECTION), Kind::Toggle,
                 device_row(project::device_path::kGridAllowGreenSelection));
inline constexpr const Node* kDefaultSessionGridChildren[] = {&default_grid_active_mode, &default_grid_green_selection,
                                                              &default_empty_pads};
inline constexpr Node default_session_grid =
    submenu(sid(String::STRING_FOR_DEFAULT_UI_GRID), kDefaultSessionGridChildren);

/// `SessionLayout` (`origin/main:src/deluge/gui/menu_item/defaults/session_layout.h`).
inline constexpr Node default_session_layout = setting_item(sid(String::STRING_FOR_DEFAULT_UI_LAYOUT), Kind::Enum,
                                                            device_row(project::device_path::kSessionLayout));
inline constexpr const Node* kDefaultUiSessionChildren[] = {&default_session_layout, &default_session_grid};
inline constexpr Node default_ui_session = submenu(sid(String::STRING_FOR_DEFAULT_UI_SONG), kDefaultUiSessionChildren);

// The two keyboard-function toggles are `InvertedToggleBool`s (`origin/main:src/deluge/gui/ui/menus.cpp`'s
// `defaultUIKeyboardFunctionsVelocityGlide` and `defaultUIKeyboardFunctionsModwheelGlide`): the menu shows
// *momentary*, which is what their rows hold; the device keeps the glide mode, the other sense.
inline constexpr Node default_kb_velocity_glide =
    setting_item(sid(String::STRING_FOR_DEFAULT_UI_KB_CONTROLS_VELOCITY_MOMENTARY), Kind::Toggle,
                 device_row(project::device_path::kVelocityMomentary));
inline constexpr Node default_kb_modwheel_glide =
    setting_item(sid(String::STRING_FOR_DEFAULT_UI_KB_CONTROLS_MODWHEEL_MOMENTARY), Kind::Toggle,
                 device_row(project::device_path::kModwheelMomentary));
inline constexpr const Node* kDefaultKeyboardFunctionsChildren[] = {&default_kb_velocity_glide,
                                                                    &default_kb_modwheel_glide};
inline constexpr Node default_keyboard_functions =
    submenu(sid(String::STRING_FOR_DEFAULT_UI_KB_CONTROLS), kDefaultKeyboardFunctionsChildren);

/// `defaults::KeyboardLayout` (`origin/main:src/deluge/gui/menu_item/defaults/keyboard_layout.h`) lists three of the
/// device's seven layouts; one it does not list shows as the last, as its list clamps it.
inline constexpr Node default_keyboard_layout = setting_item(sid(String::STRING_FOR_DEFAULT_UI_LAYOUT), Kind::Enum,
                                                             device_row(project::device_path::kKeyboardLayout));
/// `DefaultFavouritesLayout` (`origin/main:src/deluge/gui/menu_item/defaults/favourites_layout.h`).
inline constexpr Node default_favourites_layout = setting_item(
    sid(String::STRING_FOR_DEFAULT_UI_FAVOURITES), Kind::Enum, device_row(project::device_path::kFavouritesLayout));
inline constexpr const Node* kDefaultUiKeyboardChildren[] = {&default_keyboard_layout, &default_keyboard_functions,
                                                             &default_favourites_layout};
inline constexpr Node default_ui_keyboard =
    submenu(sid(String::STRING_FOR_DEFAULT_UI_KEYBOARD), kDefaultUiKeyboardChildren);

/// `DefaultNewClipType::getOptions`
/// (`origin/main:src/deluge/gui/menu_item/defaults/ui/clip_type/default_new_clip_type.h`) returns five **literal**
/// strings rather than l10n ids -- there is no `STRING_FOR_SYNTH` / `_KIT` / `_AUDIO` to name -- so the list is dynamic
/// here too, and is the same five literals.
inline constexpr Node default_new_clip_type = setting_item(sid(String::STRING_FOR_DEFAULT_NEW_CLIP_TYPE), Kind::Enum,
                                                           device_row(project::device_path::kNewClipType));
inline constexpr Node default_use_last_clip_type =
    setting_item(sid(String::STRING_FOR_DEFAULT_USE_LAST_CLIP_TYPE), Kind::Toggle,
                 device_row(project::device_path::kUseLastClipType));
inline constexpr const Node* kDefaultClipTypeChildren[] = {&default_new_clip_type, &default_use_last_clip_type};
inline constexpr Node default_clip_type = submenu(sid(String::STRING_FOR_DEFAULT_CLIP_TYPE), kDefaultClipTypeChildren);

inline constexpr Node default_use_sharps =
    setting_item(sid(String::STRING_FOR_DEFAULT_UI_SHARPS), Kind::Toggle, device_row(project::device_path::kUseSharps));

inline constexpr const Node* kDefaultUiChildren[] = {&default_accessibility, &default_ui_session, &default_ui_keyboard,
                                                     &default_clip_type, &default_use_sharps};
inline constexpr Node default_ui = submenu(sid(String::STRING_FOR_DEFAULT_UI), kDefaultUiChildren);

inline constexpr Node default_automation_interpolate =
    setting_item(sid(String::STRING_FOR_DEFAULT_AUTOMATION_INTERPOLATION), Kind::Toggle,
                 device_row(project::device_path::kAutomationInterpolate));
inline constexpr Node default_automation_clear = setting_item(
    sid(String::STRING_FOR_DEFAULT_AUTOMATION_CLEAR), Kind::Toggle, device_row(project::device_path::kAutomationClear));
inline constexpr Node default_automation_shift = setting_item(
    sid(String::STRING_FOR_DEFAULT_AUTOMATION_SHIFT), Kind::Toggle, device_row(project::device_path::kAutomationShift));
inline constexpr Node default_automation_nudge =
    setting_item(sid(String::STRING_FOR_DEFAULT_AUTOMATION_NUDGE_NOTE), Kind::Toggle,
                 device_row(project::device_path::kAutomationNudgeNote));
inline constexpr Node default_automation_no_audition =
    setting_item(sid(String::STRING_FOR_DEFAULT_AUTOMATION_DISABLE_AUDITION_PAD_SHORTCUTS), Kind::Toggle,
                 device_row(project::device_path::kAutomationNoAuditionShortcuts));
inline constexpr const Node* kDefaultAutomationChildren[] = {&default_automation_interpolate, &default_automation_clear,
                                                             &default_automation_shift, &default_automation_nudge,
                                                             &default_automation_no_audition};
inline constexpr Node default_automation = submenu(sid(String::STRING_FOR_AUTOMATION), kDefaultAutomationChildren);

/// @name The three `Kind::Range` items -- `Kind::Range`'s entire consumer set (`node.h`).
///
/// `defaultTempoMenu` / `defaultSwingAmountMenu` are `IntegerRange`
/// (`origin/main:src/deluge/gui/ui/menus.cpp`), `defaultKeyMenu` is `KeyRange`. Each end is a DEVICE row, whose bounds
/// are the constructor arguments and, for the key, `KeyRange::selectEncoderAction`'s own `KEY_MIN`/`KEY_MAX` locals
/// (`origin/main:src/deluge/gui/menu_item/key_range.cpp`) -- `KeyRange` has no `minValue`/`maxValue` members at all.
/// @{

/// `IntegerRange::getText` (`origin/main:src/deluge/gui/menu_item/integer_range.cpp`), which is `intToString` per end;
/// `KeyRange::getText` (`origin/main:src/deluge/gui/menu_item/key_range.cpp`), the note letter plus an accidental,
/// spelled as the device's `defaults/useSharps` row says. @p hook_ctx is the menu's `MenuContext`, or null. Bodies in
/// the `.cpp`.
/// @{
std::string_view format_range_integer(int32_t value, const void* hook_ctx, std::span<char> scratch);
std::string_view format_range_key(int32_t value, const void* hook_ctx, std::span<char> scratch);
/// @}

/// `IntegerRange`: the ends push each other, and opening a split range lands on LEFT
/// (`IntegerRange::selectEncoderAction` for the push, `IntegerRange::beginSession` for the landing, in
/// `origin/main:src/deluge/gui/menu_item/integer_range.cpp`).
inline constexpr RangeSpec kDefaultTempoSpec{.upper_path = project::device_path::kTempoUpper,
                                             .format = format_range_integer,
                                             .clamp = deluge::ui::widgets::oled::RangeClamp::Push,
                                             .focus_left_when_split = true};
inline constexpr RangeSpec kDefaultSwingSpec{.upper_path = project::device_path::kSwingUpper,
                                             .format = format_range_integer,
                                             .clamp = deluge::ui::widgets::oled::RangeClamp::Push,
                                             .focus_left_when_split = true};
/// `KeyRange` clamps each end against the other and has **no** `beginSession` override, so it takes
/// `Range::beginSession`'s bare OFF (`origin/main:src/deluge/gui/menu_item/range.cpp`).
inline constexpr RangeSpec kDefaultKeySpec{.upper_path = project::device_path::kKeyUpper,
                                           .format = format_range_key,
                                           .clamp = deluge::ui::widgets::oled::RangeClamp::Bound,
                                           .focus_left_when_split = false};

inline constexpr Node default_tempo = range_item(sid(String::STRING_FOR_TEMPO), sid(String::STRING_FOR_DEFAULT_TEMPO),
                                                 device_row(project::device_path::kTempoLower), &kDefaultTempoSpec);
inline constexpr Node default_swing_amount =
    range_item(sid(String::STRING_FOR_SWING_AMOUNT), sid(String::STRING_FOR_DEFAULT_SWING),
               device_row(project::device_path::kSwingLower), &kDefaultSwingSpec);
inline constexpr Node default_key = range_item(sid(String::STRING_FOR_KEY), sid(String::STRING_FOR_DEFAULT_KEY),
                                               device_row(project::device_path::kKeyLower), &kDefaultKeySpec);
/// @}

/// `defaults::SwingInterval` (`origin/main:src/deluge/gui/menu_item/defaults/swing_interval.h`): it reuses the song
/// item's `swing_interval_turn`, and names its levels against the default resolution.
inline constexpr Node default_swing_interval =
    with_sync_name_magnitude(with_turn(with_title(setting_item(sid(String::STRING_FOR_SWING_INTERVAL), Kind::SyncLevel,
                                                               device_row(project::device_path::kSwingInterval)),
                                                  sid(String::STRING_FOR_DEFAULT_SWING)),
                                       swing_interval_turn),
                             default_resolution_magnitude);

/// `defaults::DefaultScale` (`origin/main:src/deluge/gui/menu_item/defaults/scale.h`): every scale name but the user
/// scale's, which is no default.
inline constexpr Node default_scale =
    setting_item(sid(String::STRING_FOR_INIT_SCALE), Kind::Enum, device_row(project::device_path::kScale));
inline constexpr Node default_active_scale =
    with_value_names(custom(sid(String::STRING_FOR_ACTIVE_SCALES), &kDefaultActiveScalesItem), preset_scale_names);
inline constexpr const Node* kDefaultScalesChildren[] = {&default_scale, &default_active_scale};
inline constexpr Node default_scales = submenu(sid(String::STRING_FOR_SCALE), kDefaultScalesChildren);

/// `defaults::Velocity`'s write reaches the open song's instruments; the device port's store does it.
inline constexpr Node default_velocity =
    with_title(setting_item(sid(String::STRING_FOR_VELOCITY), Kind::Int, device_row(project::device_path::kVelocity)),
               sid(String::STRING_FOR_DEFAULT_VELOC_MENU_TITLE));
/// `defaults::Magnitude` (`origin/main:src/deluge/gui/menu_item/defaults/magnitude.h`): an `Enumeration` of seven whose
/// screen is one number, `96 << value`; it turns and clamps as an integer does. Its write moves the default swing
/// interval by the same delta; the device port's store does it.
inline constexpr Node default_magnitude =
    with_leaf_text(with_title(setting_item(sid(String::STRING_FOR_RESOLUTION), Kind::Int,
                                           device_row(project::device_path::kMagnitude)),
                              sid(String::STRING_FOR_DEFAULT_RESOL_MENU_TITLE)),
                   default_magnitude_text);
inline constexpr Node default_bend_range = with_title(
    setting_item(sid(String::STRING_FOR_BEND_RANGE), Kind::Int, device_row(project::device_path::kBendRange)),
    sid(String::STRING_FOR_DEFAULT_BEND_R));
/// `defaults::MetronomeVolume`'s write sets the metronome's volume at once; the device port's store does it.
inline constexpr Node default_metronome_volume = with_title(
    setting_item(sid(String::STRING_FOR_METRONOME), Kind::Int, device_row(project::device_path::kMetronomeVolume)),
    sid(String::STRING_FOR_DEFAULT_METRO_MENU_TITLE));

/// `PatchCablePolarity` (`origin/main:src/deluge/gui/menu_item/defaults/patch_cable_polarity.h`): the DEVICE row,
/// whose two choices the name table names as `polarityToString`
/// (`origin/main:src/deluge/modulation/patch/patch_cable.cpp`) spells them.
inline constexpr Node default_patch_cable_polarity = setting_item(
    sid(String::STRING_FOR_DEFAULT_POLARITY), Kind::Enum, device_row(project::device_path::kPatchCablePolarity));

/// `StartupSongModeMenu` (`origin/main:src/deluge/gui/menu_item/defaults/startup_song_mode.h`).
inline constexpr Node default_startup_song = setting_item(sid(String::STRING_FOR_DEFAULT_UI_DEFAULT_STARTUP_SONG_MODE),
                                                          Kind::Enum, device_row(project::device_path::kStartupSong));

/// `PadBrightness` (`origin/main:src/deluge/gui/menu_item/defaults/pad_brightness.h`) shows four times the level its
/// row holds, and turns in steps of four within its own range; its write dims the pads at once, which the device
/// port's store does.
inline constexpr Node default_pad_brightness =
    with_turn(with_title(setting_item(sid(String::STRING_FOR_DEFAULT_PAD_BRIGHTNESS), Kind::Int,
                                      device_row(project::device_path::kPadBrightness)),
                         sid(String::STRING_FOR_DEFAULT_PAD_BRIGHTNESS_MENU_TITLE)),
              pad_brightness_turn);

/// `SliceMode` (`origin/main:src/deluge/gui/menu_item/defaults/slice_mode.h`).
inline constexpr Node default_slice_mode = with_title(
    setting_item(sid(String::STRING_FOR_DEFAULT_SLICE_MODE), Kind::Enum, device_row(project::device_path::kSliceMode)),
    sid(String::STRING_FOR_DEFAULT_SLICE_MODE_MENU_TITLE));
inline constexpr Node default_cpu_indicator =
    setting_item(sid(String::STRING_FOR_DEFAULT_HIGH_CPU_USAGE_INDICATOR), Kind::Toggle,
                 device_row(project::device_path::kCpuIndicator));
/// `HoldTime`'s write also sets the press timer's hold in samples; the device port's store does it.
inline constexpr Node default_hold_time = with_leaf_text(
    setting_item(sid(String::STRING_FOR_HOLD_TIME), Kind::Int, device_row(project::device_path::kHoldTime)),
    hold_time_text);

/// `ScreensaverModeMenu` (`origin/main:src/deluge/gui/menu_item/defaults/screensaver.h`): its write reaches both
/// screensavers at once, which the device port's store does.
inline constexpr Node screensaver_mode = setting_item(sid(String::STRING_FOR_SCREENSAVER_MODE), Kind::Enum,
                                                      device_row(project::device_path::kScreensaverMode));
/// `ScreensaverTimeout` (`origin/main:src/deluge/gui/menu_item/defaults/screensaver.h`): minutes, with `getUnit`'s "
/// MIN"; its write re-arms both screensavers, as the mode's does.
inline constexpr Node screensaver_timeout =
    with_value_style(setting_item(sid(String::STRING_FOR_SCREENSAVER_TIMEOUT), Kind::Int,
                                  device_row(project::device_path::kScreensaverTimeout)),
                     {.unit = " MIN"});
inline constexpr const Node* kScreensaverChildren[] = {&screensaver_mode, &screensaver_timeout};
inline constexpr Node screensaver = submenu(sid(String::STRING_FOR_SCREENSAVER), kScreensaverChildren);

inline constexpr const Node* kDefaultsChildren[] = {&default_ui,
                                                    &default_automation,
                                                    &default_tempo,
                                                    &default_swing_amount,
                                                    &default_swing_interval,
                                                    &default_key,
                                                    &default_scales,
                                                    &default_velocity,
                                                    &default_magnitude,
                                                    &default_bend_range,
                                                    &default_metronome_volume,
                                                    &default_patch_cable_polarity,
                                                    &default_startup_song,
                                                    &default_pad_brightness,
                                                    &default_slice_mode,
                                                    &default_cpu_indicator,
                                                    &default_hold_time,
                                                    &screensaver};
inline constexpr Node defaults_settings = submenu(sid(String::STRING_FOR_DEFAULTS), kDefaultsChildren);

// ---------------------------------------------------------------------------------------------
// 7. Pads

/// `shortcuts::Version` (`origin/main:src/deluge/gui/menu_item/shortcuts/version.h`): the pre-3.0 layout and the
/// current one, which the shortcut lookups select a layer by (`menu::shortcuts_version`).
inline constexpr Node shortcuts_version = with_title(setting_item(sid(String::STRING_FOR_SHORTCUTS_VERSION), Kind::Enum,
                                                                  device_row(project::device_path::kShortcutsVersion)),
                                                     sid(String::STRING_FOR_SHORTCUTS_VER_MENU_TITLE));

/// `keyboard::Layout` (`origin/main:src/deluge/gui/menu_item/keyboard/layout.h`): QWERTY, AZERTY, QWERTZ. It is the
/// *text-entry* keyboard, not the pad-grid one `defaults::KeyboardLayout` sets.
inline constexpr Node pads_keyboard_layout =
    with_title(setting_item(sid(String::STRING_FOR_KEYBOARD_FOR_TEXT), Kind::Enum,
                            device_row(project::device_path::kTextKeyboardLayout)),
               sid(String::STRING_FOR_KEY_LAYOUT));

/// `menu_item::Colour` (`origin/main:src/deluge/gui/menu_item/colour.h`), the six in `coloursSubmenu`'s own order
/// (`origin/main:src/deluge/gui/ui/menus.cpp`): each write re-renders every pad, which the device port's store does.
inline constexpr Node pad_colour_active =
    setting_item(sid(String::STRING_FOR_ACTIVE), Kind::Enum, device_row(project::device_path::kPadColourActive));
inline constexpr Node pad_colour_stopped =
    setting_item(sid(String::STRING_FOR_STOPPED), Kind::Enum, device_row(project::device_path::kPadColourStopped));
inline constexpr Node pad_colour_muted =
    setting_item(sid(String::STRING_FOR_MUTED), Kind::Enum, device_row(project::device_path::kPadColourMuted));
inline constexpr Node pad_colour_soloed =
    setting_item(sid(String::STRING_FOR_SOLOED), Kind::Enum, device_row(project::device_path::kPadColourSoloed));
inline constexpr Node pad_colour_fill =
    setting_item(sid(String::STRING_FOR_FILL), Kind::Enum, device_row(project::device_path::kPadColourFill));
inline constexpr Node pad_colour_once =
    setting_item(sid(String::STRING_FOR_ONCE), Kind::Enum, device_row(project::device_path::kPadColourOnce));
inline constexpr const Node* kColoursChildren[] = {&pad_colour_active, &pad_colour_stopped, &pad_colour_muted,
                                                   &pad_colour_soloed, &pad_colour_fill,    &pad_colour_once};
inline constexpr Node pad_colours = submenu(sid(String::STRING_FOR_COLOURS), kColoursChildren);

inline constexpr const Node* kPadsChildren[] = {&shortcuts_version, &pads_keyboard_layout, &pad_colours};
inline constexpr Node pads_settings = submenu(sid(String::STRING_FOR_PADS), kPadsChildren);

// ---------------------------------------------------------------------------------------------
// 8-9. Sample preview and play cursor

/// `sample::browser_preview::Mode` (`origin/main:src/deluge/gui/menu_item/sample/browser_preview/mode.h`).
inline constexpr Node sample_browser_preview =
    setting_item(sid(String::STRING_FOR_SAMPLE_PREVIEW), Kind::Enum, device_row(project::device_path::kSamplePreview));

/// `flash::Status` (`origin/main:src/deluge/gui/menu_item/flash/status.h`): the DEVICE row, listed in its *value*
/// order, `FLASH_CURSOR_FAST` 0 / `_OFF` 1 / `_SLOW` 2 (`hid/led/pad_leds.h`), not a reading order.
inline constexpr Node flash_status =
    setting_item(sid(String::STRING_FOR_PLAY_CURSOR), Kind::Enum, device_row(project::device_path::kPlayCursor));

// ---------------------------------------------------------------------------------------------
// 10. Recording

/// `record::CountIn` is an `IntegerWithOff` capped at 4 (`CountIn::getMaxValue` in
/// `origin/main:src/deluge/gui/menu_item/record/countin.h`).
inline constexpr Node record_count_in = with_value_style(
    with_title(setting_item(sid(String::STRING_FOR_COUNT_IN), Kind::Int, device_row(project::device_path::kCountIn)),
               sid(String::STRING_FOR_REC_COUNT_IN)),
    deluge::ui::widgets::oled::ValueStyle{.off_at_zero = true});
/// `record::Quantize::size()` is `SYNC_TYPE_TRIPLET` (`origin/main:src/deluge/model/sync.h`), the DEVICE row's range --
/// "can't do triplets/dots for quantize" (`origin/main:src/deluge/gui/menu_item/record/quantize.h`).
inline constexpr Node record_quantize =
    with_sync_name_magnitude(setting_item(sid(String::STRING_FOR_QUANTIZATION), Kind::SyncLevel,
                                          device_row(project::device_path::kRecordQuantize)),
                             relative_to_song_magnitude);
inline constexpr Node record_margins =
    setting_item(sid(String::STRING_FOR_LOOP_MARGINS), Kind::Toggle, device_row(project::device_path::kRecordMargins));

/// `monitor::Mode` (`origin/main:src/deluge/gui/menu_item/monitor/mode.h`): conditional, enabled, disabled.
inline constexpr Node monitor_mode = with_title(setting_item(sid(String::STRING_FOR_SAMPLING_MONITORING), Kind::Enum,
                                                             device_row(project::device_path::kMonitorMode)),
                                                sid(String::STRING_FOR_MONITORING));

/// `record::ThresholdMode`'s default kind (`origin/main:src/deluge/gui/menu_item/record/threshold_mode.h`): its write
/// sets the open song's own mode too; the device port's store does it.
inline constexpr Node default_threshold_recording_mode =
    setting_item(sid(String::STRING_FOR_MODE), Kind::Enum, device_row(project::device_path::kThresholdRecording));
inline constexpr const Node* kDefaultThresholdRecordingChildren[] = {&default_threshold_recording_mode};
inline constexpr Node default_threshold_recording =
    submenu(sid(String::STRING_FOR_THRESHOLD_RECORDING), kDefaultThresholdRecordingChildren);

inline constexpr const Node* kRecordChildren[] = {&record_count_in, &record_quantize, &record_margins, &monitor_mode,
                                                  &default_threshold_recording};
inline constexpr Node record_settings = submenu(sid(String::STRING_FOR_RECORDING), kRecordChildren);

// ---------------------------------------------------------------------------------------------
// 11. Community features
//
// `runtime_feature::Settings` is a `Submenu` over `subMenuEntries`
// (`origin/main:src/deluge/gui/menu_item/runtime_feature/settings.cpp`), 23 items in that array's order. All but one
// are `SettingToggle`s -- `Selection`s flagged `isToggle()`
// (`origin/main:src/deluge/gui/menu_item/runtime_feature/setting.h`) -- which is `Kind::Toggle` here;
// `SyncScalingAction` is a plain two-option `Setting` and `ShortcutOverlay` a three-option one.

/// @brief A community feature's node: its DEVICE row, `features/<name>` (`project::device_path::feature`).
/// @param name    The node's name and title.
/// @param feature The feature it edits.
/// @param kind    How it is shown: Toggle for an on/off feature, Enum for one with choices of its own.
/// @return The node.
constexpr Node feature_item(uint16_t name, project::Feature feature, Kind kind = Kind::Toggle) {
	return setting_item(name, kind, device_row(project::device_path::feature(feature)));
}

inline constexpr Node feature_drum_randomizer =
    feature_item(sid(String::STRING_FOR_COMMUNITY_FEATURE_DRUM_RANDOMIZER), project::Feature::DRUM_RANDOMIZER);
inline constexpr Node feature_quantize =
    feature_item(sid(String::STRING_FOR_COMMUNITY_FEATURE_QUANTIZE), project::Feature::QUANTIZE);
inline constexpr Node feature_fine_tempo =
    feature_item(sid(String::STRING_FOR_COMMUNITY_FEATURE_FINE_TEMPO_KNOB), project::Feature::FINE_TEMPO_KNOB);
inline constexpr Node feature_catch_notes =
    feature_item(sid(String::STRING_FOR_COMMUNITY_FEATURE_CATCH_NOTES), project::Feature::CATCH_NOTES);
inline constexpr Node feature_delete_unused_kit_rows = feature_item(
    sid(String::STRING_FOR_COMMUNITY_FEATURE_DELETE_UNUSED_KIT_ROWS), project::Feature::DELETE_UNUSED_KIT_ROWS);
inline constexpr Node feature_alt_delay_params = feature_item(
    sid(String::STRING_FOR_COMMUNITY_FEATURE_ALT_DELAY_PARAMS), project::Feature::ALT_GOLDEN_KNOB_DELAY_PARAMS);
inline constexpr Node feature_highlight_incoming_notes = feature_item(
    sid(String::STRING_FOR_COMMUNITY_FEATURE_HIGHLIGHT_INCOMING_NOTES), project::Feature::HIGHLIGHT_INCOMING_NOTES);
inline constexpr Node feature_norns_layout =
    feature_item(sid(String::STRING_FOR_COMMUNITY_FEATURE_NORNS_LAYOUT), project::Feature::DISPLAY_NORNS_LAYOUT);
inline constexpr Node feature_light_shift =
    feature_item(sid(String::STRING_FOR_COMMUNITY_FEATURE_LIGHT_SHIFT), project::Feature::LIGHT_SHIFT_LED);
inline constexpr Node feature_dx7_engine =
    feature_item(sid(String::STRING_FOR_COMMUNITY_FEATURE_DX7_ENGINE), project::Feature::ENABLE_DX7_ENGINE);
inline constexpr Node feature_kb_sidebar_menu_exit =
    feature_item(sid(String::STRING_FOR_COMMUNITY_FEATURE_KEYBOARD_VIEW_SIDEBAR_MENU_EXIT),
                 project::Feature::ENABLE_KEYBOARD_VIEW_SIDEBAR_MENU_EXIT);
inline constexpr Node feature_launch_event_playhead = feature_item(
    sid(String::STRING_FOR_COMMUNITY_FEATURE_LAUNCH_EVENT_PLAYHEAD), project::Feature::ENABLE_LAUNCH_EVENT_PLAYHEAD);
inline constexpr Node feature_chord_keyboard =
    feature_item(sid(String::STRING_FOR_COMMUNITY_FEATURE_CHORD_KEYBOARD), project::Feature::DISPLAY_CHORD_KEYBOARD);
inline constexpr Node feature_alt_playback_start =
    feature_item(sid(String::STRING_FOR_COMMUNITY_FEATURE_ALTERNATIVE_PLAYBACK_START_BEHAVIOUR),
                 project::Feature::ALTERNATIVE_PLAYBACK_START_BEHAVIOUR);
inline constexpr Node feature_grid_loop_pads = feature_item(
    sid(String::STRING_FOR_COMMUNITY_FEATURE_GRID_VIEW_LOOP_PADS), project::Feature::ENABLE_GRID_VIEW_LOOP_PADS);
inline constexpr Node feature_alt_tap_tempo =
    feature_item(sid(String::STRING_FOR_COMMUNITY_FEATURE_ALTERNATIVE_TAP_TEMPO_BEHAVIOUR),
                 project::Feature::ALTERNATIVE_TAP_TEMPO_BEHAVIOUR);
inline constexpr Node feature_trim_from_start =
    feature_item(sid(String::STRING_FOR_COMMUNITY_FEATURE_TRIM_FROM_START_OF_AUDIO_CLIP),
                 project::Feature::TRIM_FROM_START_OF_AUDIO_CLIP);
inline constexpr Node feature_show_battery_level =
    feature_item(sid(String::STRING_FOR_COMMUNITY_FEATURE_SHOW_BATTERY_LEVEL), project::Feature::SHOW_BATTERY_LEVEL);
/// `ShortcutOverlay` is a three-option `Setting`, named in its values' order.
inline constexpr Node feature_shortcut_overlay = feature_item(
    sid(String::STRING_FOR_COMMUNITY_FEATURE_SHORTCUT_OVERLAY), project::Feature::SHORTCUT_OVERLAY, Kind::Enum);
/// `ShiftIsSticky`'s write also lights the shift LED and reaches the shift machines
/// (`origin/main:src/deluge/gui/menu_item/runtime_feature/shift_is_sticky.cpp`); the device port's store does both.
inline constexpr Node feature_sticky_shift =
    feature_item(sid(String::STRING_FOR_COMMUNITY_FEATURE_STICKY_SHIFT), project::Feature::SHIFT_IS_STICKY);
/// `RoundedCorners` restyles main's canvas and the guest's panels as it is written; the device port's store does it.
inline constexpr Node feature_rounded_corners =
    feature_item(sid(String::STRING_FOR_COMMUNITY_FEATURE_ROUNDED_CORNERS), project::Feature::ROUNDED_CORNERS);
/// `HorizontalMenus` reaches the open menu once it is stored.
inline constexpr Node feature_horizontal_menus = with_after_write(
    feature_item(sid(String::STRING_FOR_COMMUNITY_FEATURE_HORIZONTAL_MENUS), project::Feature::HORIZONTAL_MENUS),
    apply_horizontal_menus);
/// `SyncScalingAction` is a plain two-option `Setting`.
inline constexpr Node feature_sync_scaling = feature_item(sid(String::STRING_FOR_COMMUNITY_FEATURE_SYNC_SCALING_ACTION),
                                                          project::Feature::SYNC_SCALING_ACTION, Kind::Enum);

inline constexpr const Node* kRuntimeFeatureChildren[] = {&feature_drum_randomizer,
                                                          &feature_fine_tempo,
                                                          &feature_quantize,
                                                          &feature_catch_notes,
                                                          &feature_delete_unused_kit_rows,
                                                          &feature_alt_delay_params,
                                                          &feature_sync_scaling,
                                                          &feature_highlight_incoming_notes,
                                                          &feature_norns_layout,
                                                          &feature_sticky_shift,
                                                          &feature_light_shift,
                                                          &feature_dx7_engine,
                                                          &feature_kb_sidebar_menu_exit,
                                                          &feature_launch_event_playhead,
                                                          &feature_chord_keyboard,
                                                          &feature_alt_playback_start,
                                                          &feature_grid_loop_pads,
                                                          &feature_alt_tap_tempo,
                                                          &feature_horizontal_menus,
                                                          &feature_rounded_corners,
                                                          &feature_trim_from_start,
                                                          &feature_show_battery_level,
                                                          &feature_shortcut_overlay};
inline constexpr Node runtime_features =
    submenu(sid(String::STRING_FOR_COMMUNITY_FTS), kRuntimeFeatureChildren, Layout::Vertical, nullptr,
            sid(String::STRING_FOR_COMMUNITY_FTS_MENU_TITLE));

// ---------------------------------------------------------------------------------------------
// 12-13. Battery and firmware
//
// Both are `CustomItem`s, not a shared `Kind`: the rule is that a kind exists only if >=3 items use it, which keeps a
// two-consumer kind out of `node.h`. Legacy's `battery::Level` / `firmware::Version`
// (`origin/main:src/deluge/gui/menu_item/battery/level.h`, `origin/main:src/deluge/gui/menu_item/firmware/version.h`)
// have no `writeCurrentValue` and no `buttonAction` override -- only a `drawPixelsForOled` that formats a string and
// draws it -- but `battery::Level` keeps state across renders (the charging heuristic:
// `lastBatteryMV`/`voltageCheckCounter`/`isCharging`) that only a stateful `CustomItem`, not a plain function pointer,
// can reproduce. Bodies (and their citations) in `nodes/settings.cpp`.

/// @brief `firmware::Version::drawPixelsForOled` (`origin/main:src/deluge/gui/menu_item/firmware/version.h`): a
///        build-time constant string, no state at all.
class FirmwareVersionItem final : public CustomItem {
public:
	void render(deluge::ui::widgets::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::widgets::core::InputClaims owned_input(const MenuContext& /*ctx*/,
	                                                   const MenuState& /*state*/) const override {
		return deluge::ui::widgets::core::InputClaims{.select = true};
	}
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
};
inline const FirmwareVersionItem kFirmwareVersionItem{};

/// @brief `battery::Level` (`origin/main:src/deluge/gui/menu_item/battery/level.h`): the percentage/voltage readout
///        plus legacy's charging indicator, reproduced with the same kind of state legacy keeps on the menu item
///        instance -- `mutable` fields on this one shared instance stand in for it.
class BatteryLevelItem final : public CustomItem {
public:
	void render(deluge::ui::widgets::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::widgets::core::InputClaims owned_input(const MenuContext& /*ctx*/,
	                                                   const MenuState& /*state*/) const override {
		return deluge::ui::widgets::core::InputClaims{.select = true};
	}
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	void on_focus(const MenuContext& ctx, MenuState& state) const override;

private:
	// `battery::Level::lastBatteryMV`/`voltageCheckCounter`/`isCharging`
	// (`origin/main:src/deluge/gui/menu_item/battery/level.h`), with the counter's 4-ticks-of-500ms cadence measured
	// directly off elapsed `now_ms()` instead: `render()` has no separate timer callback to count ticks on, only a
	// clock reading per call.
	mutable uint16_t last_check_mv_ = 0;
	mutable uint32_t last_check_ms_ = 0;
	mutable bool checked_once_ = false;
	mutable bool charging_ = false;
};
inline const BatteryLevelItem kBatteryLevelItem{};

inline constexpr Node battery_level =
    with_relevant(with_title(custom(sid(String::STRING_FOR_BATTERY_LEVEL), &kBatteryLevelItem),
                             sid(String::STRING_FOR_BATTERY_LEVEL_MENU_TITLE)),
                  relevance::battery_shown);
/// `cpu::VoiceUsage::VoiceCount` (`origin/main:src/deluge/gui/menu_item/cpu/voice_usage.h`): a counter the page
/// samples, read-only.
int32_t get_voice_usage_live(const MenuContext& ctx);
int32_t get_voice_usage_average(const MenuContext& ctx);
int32_t get_voice_usage_maximum(const MenuContext& ctx);
void set_voice_usage_count(const MenuContext& ctx, int32_t value);
bool voice_usage_permitted(const MenuContext& ctx);
inline constexpr Node voice_usage_live = with_permitted(
    with_column_name(int_item(sid(String::STRING_FOR_LIVE_VOICE_COUNT), get_voice_usage_live, set_voice_usage_count,
                              IntRange{0, std::numeric_limits<int32_t>::max(), 1}),
                     sid(String::STRING_FOR_LIVE)),
    voice_usage_permitted);
inline constexpr Node voice_usage_average = with_permitted(
    with_column_name(int_item(sid(String::STRING_FOR_AVERAGE_VOICE_COUNT), get_voice_usage_average,
                              set_voice_usage_count, IntRange{0, std::numeric_limits<int32_t>::max(), 1}),
                     sid(String::STRING_FOR_AVG)),
    voice_usage_permitted);
inline constexpr Node voice_usage_maximum = with_permitted(
    with_column_name(int_item(sid(String::STRING_FOR_MAXIMUM_VOICE_COUNT), get_voice_usage_maximum,
                              set_voice_usage_count, IntRange{0, std::numeric_limits<int32_t>::max(), 1}),
                     sid(String::STRING_FOR_MAX)),
    voice_usage_permitted);
inline constexpr const Node* kVoiceUsageChildren[] = {&voice_usage_live, &voice_usage_average, &voice_usage_maximum};
/// `voiceUsageMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`): a FIXED page the scene samples every 500 ms
/// (`MenuScene::tick`).
inline constexpr Node voice_usage =
    with_fixed_columns(submenu(sid(String::STRING_FOR_VOICE_USAGE), kVoiceUsageChildren, Layout::Horizontal));

inline constexpr Node firmware_version =
    with_title(custom(sid(String::STRING_FOR_FIRMWARE_VERSION), &kFirmwareVersionItem),
               sid(String::STRING_FOR_FIRMWARE_VER_MENU_TITLE));

// ---------------------------------------------------------------------------------------------

inline constexpr const Node* kSettingsRootChildren[] = {
    &settings_actions,  &cv_selection,  &gate_selection,         &trigger_clock, &midi_settings,   &usb_audio_settings,
    &defaults_settings, &pads_settings, &sample_browser_preview, &flash_status,  &record_settings, &runtime_features,
    &battery_level,     &voice_usage,   &firmware_version};
inline constexpr Node root_settings = submenu(sid(String::STRING_FOR_SETTINGS), kSettingsRootChildren);

/// @brief The settings root as a one-element span, matching `kSoundRootRoots`' shape.
///
/// Defined in `nodes/settings.cpp`.
extern const std::span<const Node* const> kSettingsRoots;

} // namespace deluge::ui::scenes::menu::nodes
