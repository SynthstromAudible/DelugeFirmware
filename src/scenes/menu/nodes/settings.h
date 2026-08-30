#pragma once

/// @brief The settings root as data (task 8, batch 3) -- `gui/ui/menus.cpp`'s `settingsRootMenu`
///        literal (`:2150-2166`) and everything under it.
///
/// Thirteen children, in the literal's own order: the actions page, the CV and gate output
/// selectors, the trigger-clock page, the MIDI page, the defaults page, the pads page, the sample
/// browser preview mode, the play cursor, the recording page, the community features, the battery
/// readout and the firmware version.
///
/// Unlike the sound tree, almost nothing here is a `ParamRef`: a setting lives in `FlashStorage`
/// (`storage/flash_storage.h`), on `midiEngine` (`io/midi/midi_engine.h`), on `playbackHandler`
/// (`playback/playback_handler.h`), on `runtimeFeatureSettings`
/// (`model/settings/runtime_feature_settings.h`) or on `AudioEngine`. Every one of those is inside
/// this layer's include allow-list (see `scripts/check_layer_rules.py`'s `scenes-includes-outside`,
/// which task 8 widened to `io/**` on exactly the argument tasks 3 and 7 made for `processing/**`
/// and `dsp/**` -- `io` is already in the checker's own NON_UI glob and is held to the same
/// no-`gui/`, no-`hid/` rules as `model/`), so the hooks read and write the real thing.
///
/// @note **What is a `Kind::Custom` placeholder here, and why.** Three groups, all of them items
///       whose state or whose action is behind the wall this layer may not cross:
///
///         * **`hid/` state.** `flash_status` (`PadLEDs::flashCursor`, `flash/status.h:21-28`),
///           `shortcuts_version` (`soundEditor.shortcutsVersion`, `shortcuts/version.h:21-24`), and
///           the six `pad_colour_*` (`menu_item::Colour::value`, a member of the legacy menu item
///           itself -- `colour.h:38`, which `storage/flash_storage.cpp:576-579` reads and writes
///           by name). (`battery_level`/`firmware_version` used to point here too, at `kPending`,
///           but neither actually reaches behind the `hid/` wall -- `batteryMV` is a plain global
///           set from `deluge.cpp`'s main loop, the same one `sound_editor.cpp:1403` reads with its
///           own bare `extern` -- so E2 task 2 gave them real, finished `CustomItem`s instead of
///           leaving them as placeholders; see "12-13. Battery and firmware" below.)
///         * **UI navigation.** The five `reset_*` items (`reset_settings/reset.h:23-27`, each
///           opening a context menu), `cv_selection` / `gate_selection`
///           (`cv/selection.h:31-36`, `gate/selection.h:26-38`: `Selection`s whose select press
///           *navigates* to a page rather than committing a value), the twelve `midi_command_*`
///           (`midi/command.h`, MIDI-learn items), and `midi_devices`
///           (`midi/devices.h`, a scrolling device list).
///         * **Two items with genuinely dynamic state.** `default_tempo`, `default_swing_amount`
///           and `default_key` are `IntegerRange` / `KeyRange`, whose `lower`/`upper` live on the
///           legacy menu item (`integer_range.h:29`, `key_range.h:26`) and whose editor is spec E
///           §4's `Range` kind -- E2's. `dev_sysex_allowed`'s ON option is a **randomly generated**
///           hex key built at read time (`runtime_feature/devSysexSetting.cpp:24-31`), which no
///           static option list can express. `default_active_scale` is `ActiveScaleMenu`, an editor
///           of its own.
///
/// @note **`cvSubmenu`, `gateModeMenu` and `gateOffTimeMenu` are not declared.** They are not
///       children of `settingsRootMenu` -- legacy reaches them only from
///       `cv::Selection::selectButtonPress` / `gate::Selection::selectButtonPress`
///       (`cv/selection.h:41-46`, `gate/selection.h:43-54`), i.e. through navigation, so a table
///       that declared them would not match the legacy tree the spec walks. They arrive with E2's
///       `selectButtonPress()`-chaining work, alongside the two selectors above -- declaring these
///       three pages and implementing chaining are one job. The complete sweep of every chaining
///       override in `gui/menu_item/` is the design spec's `selectButtonPress()` amendment;
///       `README.md`'s E2 roadmap and the E2 carry list name every affected leaf (fix round 2,
///       I5).
///
/// @note **Three write side effects stay behind, and are gaps, not omissions.**
///       `defaults::PadBrightness::writeCurrentValue` also calls `PadLEDs::setBrightnessLevel`
///       (`defaults/pad_brightness.h:26`), `ScreensaverModeMenu`/`ScreensaverTimeout` also call
///       `hid::display::Screensaver::settingsChanged()` (`defaults/screensaver.h:27,58`), the
///       accessibility highlighting item also pokes `Canvas::menuHighlightingInverts`
///       (`accessibility_menu_highlighting.h:14-15`), and `ShiftIsSticky` also calls
///       `Buttons::clearShiftSticky()` (`runtime_feature/shift_is_sticky.cpp:23`). All four are
///       `hid/`. The `FlashStorage` / `runtimeFeatureSettings` half of each write IS transcribed,
///       so the setting persists; only the immediate apply is missing, and it belongs to whoever
///       owns those globals once E2 has moved them.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/nodes/roots.h" // kThresholdModeNames, and `kPending` through it
#include "scenes/menu/relevance.h"

#include <span>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// @name Hooks. Bodies (and their legacy citations) in `settings.cpp`.
/// @{
int32_t get_trigger_in_ppqn(const MenuContext& ctx);
void set_trigger_in_ppqn(const MenuContext& ctx, int32_t value);
int32_t get_trigger_in_auto_start(const MenuContext& ctx);
void set_trigger_in_auto_start(const MenuContext& ctx, int32_t value);
int32_t get_trigger_out_ppqn(const MenuContext& ctx);
void set_trigger_out_ppqn(const MenuContext& ctx, int32_t value);

int32_t get_midi_clock_out(const MenuContext& ctx);
void set_midi_clock_out(const MenuContext& ctx, int32_t value);
int32_t get_midi_clock_in(const MenuContext& ctx);
void set_midi_clock_in(const MenuContext& ctx, int32_t value);
int32_t get_tempo_magnitude_matching(const MenuContext& ctx);
void set_tempo_magnitude_matching(const MenuContext& ctx, int32_t value);

/// @brief `midi::FollowChannel`'s read and write over `midiEngine.midiFollowChannelType[which]`
///        (`midi/follow/follow_channel.h:29-30`), with the channel type as an argument rather than
///        a constructor member.
///
/// The nineteen nodes are nineteen distinct addresses of the two templates below -- the same
/// device `relevance.h` uses for its `osc0_*`/`osc1_*` pairs, and for the same reason: a `Node`
/// carries no per-item payload, so what legacy stored in a member has to be a template argument.
/// @{
int32_t follow_channel_get(int32_t which);
void follow_channel_set(int32_t which, int32_t value);
template <int Which>
int32_t get_follow_channel(const MenuContext&) {
	return follow_channel_get(Which);
}
template <int Which>
void set_follow_channel(const MenuContext&, int32_t value) {
	follow_channel_set(Which, value);
}
/// @}

int32_t get_follow_kit_root_note(const MenuContext& ctx);
void set_follow_kit_root_note(const MenuContext& ctx, int32_t value);
int32_t get_follow_feedback_channel(const MenuContext& ctx);
void set_follow_feedback_channel(const MenuContext& ctx, int32_t value);
size_t follow_feedback_channel_names(const MenuContext& ctx, std::span<std::string_view> out);
int32_t get_follow_feedback_automation(const MenuContext& ctx);
void set_follow_feedback_automation(const MenuContext& ctx, int32_t value);
int32_t get_follow_feedback_filter(const MenuContext& ctx);
void set_follow_feedback_filter(const MenuContext& ctx, int32_t value);
int32_t get_follow_display_param(const MenuContext& ctx);
void set_follow_display_param(const MenuContext& ctx, int32_t value);

int32_t get_midi_select_kit_row(const MenuContext& ctx);
void set_midi_select_kit_row(const MenuContext& ctx, int32_t value);
int32_t get_midi_thru(const MenuContext& ctx);
void set_midi_thru(const MenuContext& ctx, int32_t value);
int32_t get_midi_transpose_method(const MenuContext& ctx);
void set_midi_transpose_method(const MenuContext& ctx, int32_t value);
int32_t get_midi_takeover(const MenuContext& ctx);
void set_midi_takeover(const MenuContext& ctx, int32_t value);
int32_t get_midi_input_differentiation(const MenuContext& ctx);
void set_midi_input_differentiation(const MenuContext& ctx, int32_t value);

int32_t get_default_accessibility_shortcuts(const MenuContext& ctx);
void set_default_accessibility_shortcuts(const MenuContext& ctx, int32_t value);
int32_t get_default_menu_highlighting(const MenuContext& ctx);
void set_default_menu_highlighting(const MenuContext& ctx, int32_t value);
int32_t get_default_session_layout(const MenuContext& ctx);
void set_default_session_layout(const MenuContext& ctx, int32_t value);
int32_t get_default_grid_active_mode(const MenuContext& ctx);
void set_default_grid_active_mode(const MenuContext& ctx, int32_t value);
int32_t get_default_grid_green_selection(const MenuContext& ctx);
void set_default_grid_green_selection(const MenuContext& ctx, int32_t value);
int32_t get_default_grid_pads_unarm(const MenuContext& ctx);
void set_default_grid_pads_unarm(const MenuContext& ctx, int32_t value);
int32_t get_default_grid_pads_create_rec(const MenuContext& ctx);
void set_default_grid_pads_create_rec(const MenuContext& ctx, int32_t value);
int32_t get_default_keyboard_layout(const MenuContext& ctx);
void set_default_keyboard_layout(const MenuContext& ctx, int32_t value);
int32_t get_default_kb_velocity_glide(const MenuContext& ctx);
void set_default_kb_velocity_glide(const MenuContext& ctx, int32_t value);
int32_t get_default_kb_modwheel_glide(const MenuContext& ctx);
void set_default_kb_modwheel_glide(const MenuContext& ctx, int32_t value);
int32_t get_default_favourites_layout(const MenuContext& ctx);
void set_default_favourites_layout(const MenuContext& ctx, int32_t value);
int32_t get_default_new_clip_type(const MenuContext& ctx);
void set_default_new_clip_type(const MenuContext& ctx, int32_t value);
int32_t get_default_use_last_clip_type(const MenuContext& ctx);
void set_default_use_last_clip_type(const MenuContext& ctx, int32_t value);
int32_t get_default_use_sharps(const MenuContext& ctx);
void set_default_use_sharps(const MenuContext& ctx, int32_t value);

int32_t get_default_automation_interpolate(const MenuContext& ctx);
void set_default_automation_interpolate(const MenuContext& ctx, int32_t value);
int32_t get_default_automation_clear(const MenuContext& ctx);
void set_default_automation_clear(const MenuContext& ctx, int32_t value);
int32_t get_default_automation_shift(const MenuContext& ctx);
void set_default_automation_shift(const MenuContext& ctx, int32_t value);
int32_t get_default_automation_nudge(const MenuContext& ctx);
void set_default_automation_nudge(const MenuContext& ctx, int32_t value);
int32_t get_default_automation_no_audition(const MenuContext& ctx);
void set_default_automation_no_audition(const MenuContext& ctx, int32_t value);

int32_t get_default_swing_interval(const MenuContext& ctx);
void set_default_swing_interval(const MenuContext& ctx, int32_t value);
int32_t get_default_scale(const MenuContext& ctx);
void set_default_scale(const MenuContext& ctx, int32_t value);
size_t default_scale_names(const MenuContext& ctx, std::span<std::string_view> out);
int32_t get_default_velocity(const MenuContext& ctx);
void set_default_velocity(const MenuContext& ctx, int32_t value);
int32_t get_default_magnitude(const MenuContext& ctx);
void set_default_magnitude(const MenuContext& ctx, int32_t value);
size_t default_magnitude_names(const MenuContext& ctx, std::span<std::string_view> out);
int32_t get_default_bend_range(const MenuContext& ctx);
void set_default_bend_range(const MenuContext& ctx, int32_t value);
int32_t get_default_metronome_volume(const MenuContext& ctx);
void set_default_metronome_volume(const MenuContext& ctx, int32_t value);
int32_t get_default_patch_cable_polarity(const MenuContext& ctx);
void set_default_patch_cable_polarity(const MenuContext& ctx, int32_t value);
int32_t get_default_startup_song(const MenuContext& ctx);
void set_default_startup_song(const MenuContext& ctx, int32_t value);
int32_t get_default_pad_brightness(const MenuContext& ctx);
void set_default_pad_brightness(const MenuContext& ctx, int32_t value);
int32_t get_default_slice_mode(const MenuContext& ctx);
void set_default_slice_mode(const MenuContext& ctx, int32_t value);
int32_t get_default_cpu_indicator(const MenuContext& ctx);
void set_default_cpu_indicator(const MenuContext& ctx, int32_t value);
int32_t get_default_hold_time(const MenuContext& ctx);
void set_default_hold_time(const MenuContext& ctx, int32_t value);
int32_t get_screensaver_mode(const MenuContext& ctx);
void set_screensaver_mode(const MenuContext& ctx, int32_t value);
int32_t get_screensaver_timeout(const MenuContext& ctx);
void set_screensaver_timeout(const MenuContext& ctx, int32_t value);

int32_t get_pads_keyboard_layout(const MenuContext& ctx);
void set_pads_keyboard_layout(const MenuContext& ctx, int32_t value);
int32_t get_sample_browser_preview(const MenuContext& ctx);
void set_sample_browser_preview(const MenuContext& ctx, int32_t value);

int32_t get_record_count_in(const MenuContext& ctx);
void set_record_count_in(const MenuContext& ctx, int32_t value);
int32_t get_record_quantize(const MenuContext& ctx);
void set_record_quantize(const MenuContext& ctx, int32_t value);
int32_t get_record_margins(const MenuContext& ctx);
void set_record_margins(const MenuContext& ctx, int32_t value);
int32_t get_monitor_mode(const MenuContext& ctx);
void set_monitor_mode(const MenuContext& ctx, int32_t value);
int32_t get_default_threshold_recording(const MenuContext& ctx);
void set_default_threshold_recording(const MenuContext& ctx, int32_t value);
int32_t get_loop_command(const MenuContext& ctx);
void set_loop_command(const MenuContext& ctx, int32_t value);
size_t new_clip_type_names(const MenuContext& ctx, std::span<std::string_view> out);
size_t polarity_names(const MenuContext& ctx, std::span<std::string_view> out);
size_t pads_keyboard_layout_names(const MenuContext& ctx, std::span<std::string_view> out);

/// @brief `runtime_feature::Setting`'s read and write (`runtime_feature/setting.cpp:22-38`), with
///        the `RuntimeFeatureSettingType` as a template argument rather than a member.
///
/// Every one of the 23 settings' option lists is `{value 0, value 1}` in index order
/// (`runtime_feature_settings.cpp:41-77`), so legacy's option-scan collapses to the value itself.
/// @{
int32_t runtime_feature_get(int32_t type);
void runtime_feature_set(int32_t type, int32_t value);
template <int Type>
int32_t get_runtime_feature(const MenuContext&) {
	return runtime_feature_get(Type);
}
template <int Type>
void set_runtime_feature(const MenuContext&, int32_t value) {
	runtime_feature_set(Type, value);
}
/// @}

/// @brief `ShiftIsSticky::writeCurrentValue` (`runtime_feature/shift_is_sticky.cpp:20-33`) minus
///        its `Buttons::clearShiftSticky()` arm (`hid/`, see the file note).
void set_shift_is_sticky(const MenuContext& ctx, int32_t value);
size_t sync_scaling_action_names(const MenuContext& ctx, std::span<std::string_view> out);
/// @}

// ---------------------------------------------------------------------------------------------
// 1. Actions

inline constexpr Node reset_flash = custom(sid(String::STRING_FOR_RESET_FLASH), &kPending); // E2
inline constexpr Node reset_community_features = custom(sid(String::STRING_FOR_RESET_COMMUNITY_FEATURES), &kPending);
inline constexpr Node reset_midi_follow = custom(sid(String::STRING_FOR_RESET_MIDI_FOLLOW), &kPending);   // E2
inline constexpr Node reset_midi_devices = custom(sid(String::STRING_FOR_RESET_MIDI_DEVICES), &kPending); // E2
inline constexpr Node reset_all = custom(sid(String::STRING_FOR_FACTORY_RESET), &kPending);               // E2

inline constexpr const Node* kResetSettingsChildren[] = {&reset_flash, &reset_community_features, &reset_midi_follow,
                                                         &reset_midi_devices, &reset_all};
inline constexpr Node reset_settings = submenu(sid(String::STRING_FOR_RESET_SETTINGS), kResetSettingsChildren);
inline constexpr const Node* kSettingsActionsChildren[] = {&reset_settings};
inline constexpr Node settings_actions = submenu(sid(String::STRING_FOR_ACTIONS), kSettingsActionsChildren);

// ---------------------------------------------------------------------------------------------
// 2-3. The CV and gate output selectors (navigation items -- see the file note)

inline constexpr Node cv_selection =
    with_title(custom(sid(String::STRING_FOR_CV), &kPending), sid(String::STRING_FOR_CV_OUTPUTS)); // E2
inline constexpr Node gate_selection =
    with_title(custom(sid(String::STRING_FOR_GATE), &kPending), sid(String::STRING_FOR_GATE_OUTPUTS)); // E2

// ---------------------------------------------------------------------------------------------
// 4. Trigger clock

/// `menu_item::PPQN::getMinValue`/`getMaxValue` (`gui/menu_item/ppqn.h:21-22`).
inline constexpr IntRange kPpqnRange{1, 192, 1};

inline constexpr Node trigger_in_ppqn =
    with_title(int_item(sid(String::STRING_FOR_PPQN), get_trigger_in_ppqn, set_trigger_in_ppqn, kPpqnRange),
               sid(String::STRING_FOR_INPUT_PPQN));
inline constexpr Node trigger_in_auto_start =
    toggle_item(sid(String::STRING_FOR_AUTO_START), get_trigger_in_auto_start, set_trigger_in_auto_start);
inline constexpr const Node* kTriggerClockInChildren[] = {&trigger_in_ppqn, &trigger_in_auto_start};
inline constexpr Node trigger_clock_in =
    submenu(sid(String::STRING_FOR_INPUT), kTriggerClockInChildren, Layout::Vertical, nullptr,
            sid(String::STRING_FOR_T_CLOCK_INPUT_MENU_TITLE));

inline constexpr Node trigger_out_ppqn =
    with_title(int_item(sid(String::STRING_FOR_PPQN), get_trigger_out_ppqn, set_trigger_out_ppqn, kPpqnRange),
               sid(String::STRING_FOR_OUTPUT_PPQN));
inline constexpr const Node* kTriggerClockOutChildren[] = {&trigger_out_ppqn};
inline constexpr Node trigger_clock_out =
    submenu(sid(String::STRING_FOR_OUTPUT), kTriggerClockOutChildren, Layout::Vertical, nullptr,
            sid(String::STRING_FOR_T_CLOCK_OUT_MENU_TITLE));

inline constexpr const Node* kTriggerClockChildren[] = {&trigger_clock_in, &trigger_clock_out};
inline constexpr Node trigger_clock = submenu(sid(String::STRING_FOR_TRIGGER_CLOCK), kTriggerClockChildren);

// ---------------------------------------------------------------------------------------------
// 5. MIDI

inline constexpr Node midi_clock_out =
    with_title(toggle_item(sid(String::STRING_FOR_OUTPUT), get_midi_clock_out, set_midi_clock_out),
               sid(String::STRING_FOR_MIDI_CLOCK_OUT));
inline constexpr Node midi_clock_in =
    with_title(toggle_item(sid(String::STRING_FOR_INPUT), get_midi_clock_in, set_midi_clock_in),
               sid(String::STRING_FOR_MIDI_CLOCK_IN));
inline constexpr Node tempo_magnitude_matching = toggle_item(
    sid(String::STRING_FOR_TEMPO_MAGNITUDE_MATCHING), get_tempo_magnitude_matching, set_tempo_magnitude_matching);
inline constexpr const Node* kMidiClockChildren[] = {&midi_clock_out, &midi_clock_in, &tempo_magnitude_matching};
inline constexpr Node midi_clock = submenu(sid(String::STRING_FOR_CLOCK), kMidiClockChildren, Layout::Vertical, nullptr,
                                           sid(String::STRING_FOR_MIDI_CLOCK));

// `FollowChannel::getMaxValue` is `NUM_CHANNELS` (`follow_channel.h:32`) -- one past the last real
// channel, which the item draws as "unassigned".
inline constexpr IntRange kFollowChannelRange{0, NUM_CHANNELS, 1};

/// One follow-channel node. `n` is the `MIDIFollowChannelType` enumerator's value
/// (`definitions_cxx.hpp:609-631`: A = 0, B = 1, C = 2, Track1 = 3 .. Track16 = 18).
#define DELUGE_FOLLOW_CHANNEL(node_name, n, name_id)                                                                   \
	inline constexpr Node node_name =                                                                                  \
	    int_item(sid(String::name_id), get_follow_channel<(n)>, set_follow_channel<(n)>, kFollowChannelRange)

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
    int_item(sid(String::STRING_FOR_FOLLOW_KIT_ROOT_NOTE), get_follow_kit_root_note, set_follow_kit_root_note,
             IntRange{0, kMaxMIDIValue, 1});

/// `FollowFeedbackChannelType::getOptions` (`midi/follow/follow_feedback_channel_type.h:24-42`)
/// builds three of its eight labels at run time ("Track + A", ...), so the list is dynamic.
inline constexpr Node midi_follow_feedback_channel =
    dynamic_enumeration(sid(String::STRING_FOR_CHANNEL), follow_feedback_channel_names, get_follow_feedback_channel,
                        set_follow_feedback_channel);
inline constexpr Node midi_follow_feedback_automation =
    enumeration(sid(String::STRING_FOR_FOLLOW_FEEDBACK_AUTOMATION), kThresholdModeNames, get_follow_feedback_automation,
                set_follow_feedback_automation);
inline constexpr Node midi_follow_feedback_filter =
    toggle_item(sid(String::STRING_FOR_FOLLOW_FEEDBACK_FILTER), get_follow_feedback_filter, set_follow_feedback_filter);
inline constexpr const Node* kMidiFollowFeedbackChildren[] = {
    &midi_follow_feedback_channel, &midi_follow_feedback_automation, &midi_follow_feedback_filter};
inline constexpr Node midi_follow_feedback =
    submenu(sid(String::STRING_FOR_FOLLOW_FEEDBACK), kMidiFollowFeedbackChildren);

inline constexpr Node midi_follow_display_param =
    toggle_item(sid(String::STRING_FOR_FOLLOW_DISPLAY_PARAM), get_follow_display_param, set_follow_display_param);
inline constexpr const Node* kMidiFollowChildren[] = {&midi_follow_channel, &midi_follow_kit_root_note,
                                                      &midi_follow_feedback, &midi_follow_display_param};
inline constexpr Node midi_follow = submenu(sid(String::STRING_FOR_FOLLOW_TITLE), kMidiFollowChildren);

inline constexpr Node midi_select_kit_row =
    toggle_item(sid(String::STRING_FOR_SELECT_KIT_ROW), get_midi_select_kit_row, set_midi_select_kit_row);
inline constexpr Node midi_thru = toggle_item(sid(String::STRING_FOR_MIDI_THRU), get_midi_thru, set_midi_thru);

/// `midi::Transpose::getOptions` (`midi/transpose.h:23-31`).
inline constexpr uint16_t kMidiTransposeNames[] = {
    sid(String::STRING_FOR_TRANSPOSE_INKEY),
    sid(String::STRING_FOR_TRANSPOSE_CHROMATIC),
};
inline constexpr Node midi_transpose_method = enumeration(sid(String::STRING_FOR_TRANSPOSE), kMidiTransposeNames,
                                                          get_midi_transpose_method, set_midi_transpose_method);

/// `midi::Takeover::getOptions` (`midi/takeover.h:23-32`).
inline constexpr uint16_t kMidiTakeoverNames[] = {
    sid(String::STRING_FOR_JUMP),
    sid(String::STRING_FOR_PICK_UP),
    sid(String::STRING_FOR_SCALE),
    sid(String::STRING_FOR_RELATIVE),
};
inline constexpr Node midi_takeover =
    enumeration(sid(String::STRING_FOR_TAKEOVER), kMidiTakeoverNames, get_midi_takeover, set_midi_takeover);

/// The twelve `midi::Command` learn items (`menus.cpp:1416-1428`), all E2 placeholders.
/// @{
inline constexpr Node midi_command_play = custom(sid(String::STRING_FOR_PLAY), &kPending);
inline constexpr Node midi_command_restart = custom(sid(String::STRING_FOR_RESTART), &kPending);
inline constexpr Node midi_command_record = custom(sid(String::STRING_FOR_RECORD), &kPending);
inline constexpr Node midi_command_tap = custom(sid(String::STRING_FOR_TAP_TEMPO), &kPending);
inline constexpr Node midi_command_undo = custom(sid(String::STRING_FOR_UNDO), &kPending);
inline constexpr Node midi_command_redo = custom(sid(String::STRING_FOR_REDO), &kPending);
inline constexpr Node midi_command_loop = custom(sid(String::STRING_FOR_LOOP), &kPending);
inline constexpr Node midi_command_layering_loop = custom(sid(String::STRING_FOR_LAYERING_LOOP), &kPending);
inline constexpr Node midi_command_fill = custom(sid(String::STRING_FOR_FILL), &kPending);
inline constexpr Node midi_command_transpose = custom(sid(String::STRING_FOR_TRANSPOSE), &kPending);
inline constexpr Node midi_command_next_song = custom(sid(String::STRING_FOR_SONG_LOAD_NEXT), &kPending);
inline constexpr Node midi_command_shift = custom(sid(String::STRING_FOR_SHIFT), &kPending);
/// @}

inline constexpr const Node* kMidiCommandsChildren[] = {
    &midi_command_play, &midi_command_restart,   &midi_command_record,    &midi_command_tap,
    &midi_command_undo, &midi_command_redo,      &midi_command_loop,      &midi_command_layering_loop,
    &midi_command_fill, &midi_command_transpose, &midi_command_next_song, &midi_command_shift};
inline constexpr Node midi_commands = submenu(sid(String::STRING_FOR_COMMANDS), kMidiCommandsChildren, Layout::Vertical,
                                              nullptr, sid(String::STRING_FOR_MIDI_COMMANDS));

inline constexpr Node midi_input_differentiation = toggle_item(
    sid(String::STRING_FOR_DIFFERENTIATE_INPUTS), get_midi_input_differentiation, set_midi_input_differentiation);
inline constexpr Node midi_devices =
    with_title(custom(sid(String::STRING_FOR_DEVICES), &kPending), sid(String::STRING_FOR_MIDI_DEVICES)); // E2

inline constexpr const Node* kMidiChildren[] = {
    &midi_clock,    &midi_follow,   &midi_select_kit_row,        &midi_thru,   &midi_transpose_method,
    &midi_takeover, &midi_commands, &midi_input_differentiation, &midi_devices};
inline constexpr Node midi_settings = submenu(sid(String::STRING_FOR_MIDI), kMidiChildren);

// ---------------------------------------------------------------------------------------------
// 6. Defaults

/// `AccessibilityMenuHighlighting::getOptions` (`defaults/accessibility_menu_highlighting.h:17-22`).
inline constexpr uint16_t kMenuHighlightingNames[] = {
    sid(String::STRING_FOR_FULL_INVERSION),
    sid(String::STRING_FOR_PARTIAL_INVERSION),
    sid(String::STRING_FOR_NO_INVERSION),
};
inline constexpr Node default_accessibility_shortcuts =
    toggle_item(sid(String::STRING_FOR_DEFAULT_ACCESSIBILITY_SHORTCUTS), get_default_accessibility_shortcuts,
                set_default_accessibility_shortcuts);
inline constexpr Node default_menu_highlighting =
    enumeration(sid(String::STRING_FOR_DEFAULT_ACCESSIBILITY_MENU_HIGHLIGHTING), kMenuHighlightingNames,
                get_default_menu_highlighting, set_default_menu_highlighting);
inline constexpr const Node* kDefaultAccessibilityChildren[] = {&default_accessibility_shortcuts,
                                                                &default_menu_highlighting};
inline constexpr Node default_accessibility =
    submenu(sid(String::STRING_FOR_DEFAULT_ACCESSIBILITY), kDefaultAccessibilityChildren);

/// `SessionLayout::getOptions` (`defaults/session_layout.h:19-24`).
inline constexpr uint16_t kSessionLayoutNames[] = {
    sid(String::STRING_FOR_DEFAULT_UI_SONG_LAYOUT_ROWS),
    sid(String::STRING_FOR_DEFAULT_UI_GRID),
};
/// `DefaultGridDefaultActiveMode::getOptions` (`defaults/grid_default_active_mode.h:19-26`).
inline constexpr uint16_t kGridActiveModeNames[] = {
    sid(String::STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ACTIVE_MODE_SELECTION),
    sid(String::STRING_FOR_GREEN),
    sid(String::STRING_FOR_BLUE),
};

inline constexpr Node default_grid_pads_unarm =
    toggle_item(sid(String::STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS_UNARM), get_default_grid_pads_unarm,
                set_default_grid_pads_unarm);
inline constexpr Node default_grid_pads_create_rec =
    toggle_item(sid(String::STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS_CREATE_REC), get_default_grid_pads_create_rec,
                set_default_grid_pads_create_rec);
inline constexpr const Node* kDefaultEmptyPadChildren[] = {&default_grid_pads_unarm, &default_grid_pads_create_rec};
inline constexpr Node default_empty_pads =
    submenu(sid(String::STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS), kDefaultEmptyPadChildren);

inline constexpr Node default_grid_active_mode =
    enumeration(sid(String::STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ACTIVE_MODE), kGridActiveModeNames,
                get_default_grid_active_mode, set_default_grid_active_mode);
inline constexpr Node default_grid_green_selection =
    toggle_item(sid(String::STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ALLOW_GREEN_SELECTION), get_default_grid_green_selection,
                set_default_grid_green_selection);
inline constexpr const Node* kDefaultSessionGridChildren[] = {&default_grid_active_mode, &default_grid_green_selection,
                                                              &default_empty_pads};
inline constexpr Node default_session_grid =
    submenu(sid(String::STRING_FOR_DEFAULT_UI_GRID), kDefaultSessionGridChildren);

inline constexpr Node default_session_layout =
    enumeration(sid(String::STRING_FOR_DEFAULT_UI_LAYOUT), kSessionLayoutNames, get_default_session_layout,
                set_default_session_layout);
inline constexpr const Node* kDefaultUiSessionChildren[] = {&default_session_layout, &default_session_grid};
inline constexpr Node default_ui_session = submenu(sid(String::STRING_FOR_DEFAULT_UI_SONG), kDefaultUiSessionChildren);

/// `defaults::KeyboardLayout::getOptions` (`defaults/keyboard_layout.h:19-26`).
inline constexpr uint16_t kDefaultKeyboardLayoutNames[] = {
    sid(String::STRING_FOR_KEYBOARD_LAYOUT_ISOMORPHIC),
    sid(String::STRING_FOR_KEYBOARD_LAYOUT_IN_KEY),
    sid(String::STRING_FOR_KEYBOARD_LAYOUT_PIANO),
};
/// `DefaultFavouritesLayout::getOptions` (`defaults/favourites_layout.h:19-26`).
inline constexpr uint16_t kFavouritesLayoutNames[] = {
    sid(String::STRING_FOR_DEFAULT_UI_DEFAULT_FAVOURITES_LAYOUT_FAVOURITES),
    sid(String::STRING_FOR_DEFAULT_UI_DEFAULT_FAVOURITES_LAYOUT_FAVOURITES_AND_BANKS),
    sid(String::STRING_FOR_OFF),
};

// The two keyboard-function toggles are `InvertedToggleBool`s (`menus.cpp:1537,1542`): the stored
// flag is the *glide* mode and the menu shows *momentary*, so read and write both negate.
inline constexpr Node default_kb_velocity_glide =
    toggle_item(sid(String::STRING_FOR_DEFAULT_UI_KB_CONTROLS_VELOCITY_MOMENTARY), get_default_kb_velocity_glide,
                set_default_kb_velocity_glide);
inline constexpr Node default_kb_modwheel_glide =
    toggle_item(sid(String::STRING_FOR_DEFAULT_UI_KB_CONTROLS_MODWHEEL_MOMENTARY), get_default_kb_modwheel_glide,
                set_default_kb_modwheel_glide);
inline constexpr const Node* kDefaultKeyboardFunctionsChildren[] = {&default_kb_velocity_glide,
                                                                    &default_kb_modwheel_glide};
inline constexpr Node default_keyboard_functions =
    submenu(sid(String::STRING_FOR_DEFAULT_UI_KB_CONTROLS), kDefaultKeyboardFunctionsChildren);

inline constexpr Node default_keyboard_layout =
    enumeration(sid(String::STRING_FOR_DEFAULT_UI_LAYOUT), kDefaultKeyboardLayoutNames, get_default_keyboard_layout,
                set_default_keyboard_layout);
inline constexpr Node default_favourites_layout =
    enumeration(sid(String::STRING_FOR_DEFAULT_UI_FAVOURITES), kFavouritesLayoutNames, get_default_favourites_layout,
                set_default_favourites_layout);
inline constexpr const Node* kDefaultUiKeyboardChildren[] = {&default_keyboard_layout, &default_keyboard_functions,
                                                             &default_favourites_layout};
inline constexpr Node default_ui_keyboard =
    submenu(sid(String::STRING_FOR_DEFAULT_UI_KEYBOARD), kDefaultUiKeyboardChildren);

/// `DefaultNewClipType::getOptions` (`defaults/ui/clip_type/default_new_clip_type.h:19-25`) returns
/// five **literal** strings rather than l10n ids -- there is no `STRING_FOR_SYNTH` / `_KIT` /
/// `_AUDIO` to name -- so the list is dynamic here too, and is the same five literals.
inline constexpr Node default_new_clip_type =
    dynamic_enumeration(sid(String::STRING_FOR_DEFAULT_NEW_CLIP_TYPE), new_clip_type_names, get_default_new_clip_type,
                        set_default_new_clip_type);
inline constexpr Node default_use_last_clip_type = toggle_item(
    sid(String::STRING_FOR_DEFAULT_USE_LAST_CLIP_TYPE), get_default_use_last_clip_type, set_default_use_last_clip_type);
inline constexpr const Node* kDefaultClipTypeChildren[] = {&default_new_clip_type, &default_use_last_clip_type};
inline constexpr Node default_clip_type = submenu(sid(String::STRING_FOR_DEFAULT_CLIP_TYPE), kDefaultClipTypeChildren);

inline constexpr Node default_use_sharps =
    toggle_item(sid(String::STRING_FOR_DEFAULT_UI_SHARPS), get_default_use_sharps, set_default_use_sharps);

inline constexpr const Node* kDefaultUiChildren[] = {&default_accessibility, &default_ui_session, &default_ui_keyboard,
                                                     &default_clip_type, &default_use_sharps};
inline constexpr Node default_ui = submenu(sid(String::STRING_FOR_DEFAULT_UI), kDefaultUiChildren);

inline constexpr Node default_automation_interpolate =
    toggle_item(sid(String::STRING_FOR_DEFAULT_AUTOMATION_INTERPOLATION), get_default_automation_interpolate,
                set_default_automation_interpolate);
inline constexpr Node default_automation_clear = toggle_item(
    sid(String::STRING_FOR_DEFAULT_AUTOMATION_CLEAR), get_default_automation_clear, set_default_automation_clear);
inline constexpr Node default_automation_shift = toggle_item(
    sid(String::STRING_FOR_DEFAULT_AUTOMATION_SHIFT), get_default_automation_shift, set_default_automation_shift);
inline constexpr Node default_automation_nudge = toggle_item(
    sid(String::STRING_FOR_DEFAULT_AUTOMATION_NUDGE_NOTE), get_default_automation_nudge, set_default_automation_nudge);
inline constexpr Node default_automation_no_audition =
    toggle_item(sid(String::STRING_FOR_DEFAULT_AUTOMATION_DISABLE_AUDITION_PAD_SHORTCUTS),
                get_default_automation_no_audition, set_default_automation_no_audition);
inline constexpr const Node* kDefaultAutomationChildren[] = {&default_automation_interpolate, &default_automation_clear,
                                                             &default_automation_shift, &default_automation_nudge,
                                                             &default_automation_no_audition};
inline constexpr Node default_automation = submenu(sid(String::STRING_FOR_AUTOMATION), kDefaultAutomationChildren);

inline constexpr Node default_tempo =
    with_title(custom(sid(String::STRING_FOR_TEMPO), &kPending), sid(String::STRING_FOR_DEFAULT_TEMPO)); // E2
inline constexpr Node default_swing_amount =
    with_title(custom(sid(String::STRING_FOR_SWING_AMOUNT), &kPending), sid(String::STRING_FOR_DEFAULT_SWING)); // E2
inline constexpr Node default_key =
    with_title(custom(sid(String::STRING_FOR_KEY), &kPending), sid(String::STRING_FOR_DEFAULT_KEY)); // E2

inline constexpr Node default_swing_interval =
    sync_level_item(sid(String::STRING_FOR_SWING_INTERVAL), sid(String::STRING_FOR_DEFAULT_SWING),
                    get_default_swing_interval, set_default_swing_interval, kSwingIntervalRange);

inline constexpr Node default_scale =
    dynamic_enumeration(sid(String::STRING_FOR_INIT_SCALE), default_scale_names, get_default_scale, set_default_scale);
inline constexpr Node default_active_scale = custom(sid(String::STRING_FOR_ACTIVE_SCALES), &kPending); // E2
inline constexpr const Node* kDefaultScalesChildren[] = {&default_scale, &default_active_scale};
inline constexpr Node default_scales = submenu(sid(String::STRING_FOR_SCALE), kDefaultScalesChildren);

inline constexpr Node default_velocity = with_title(
    int_item(sid(String::STRING_FOR_VELOCITY), get_default_velocity, set_default_velocity, IntRange{1, 127, 1}),
    sid(String::STRING_FOR_DEFAULT_VELOC_MENU_TITLE));
/// `defaults::Magnitude` is an `Enumeration` with `size() == 7` that draws `96 << value`
/// (`defaults/magnitude.h:29-32`), i.e. a number rather than a name -- so the option list is the
/// seven resolutions, built at render time.
inline constexpr Node default_magnitude =
    with_title(dynamic_enumeration(sid(String::STRING_FOR_RESOLUTION), default_magnitude_names, get_default_magnitude,
                                   set_default_magnitude),
               sid(String::STRING_FOR_DEFAULT_RESOL_MENU_TITLE));
inline constexpr Node default_bend_range = with_title(
    int_item(sid(String::STRING_FOR_BEND_RANGE), get_default_bend_range, set_default_bend_range, IntRange{0, 96, 1}),
    sid(String::STRING_FOR_DEFAULT_BEND_R));
inline constexpr Node default_metronome_volume =
    with_title(int_item(sid(String::STRING_FOR_METRONOME), get_default_metronome_volume, set_default_metronome_volume,
                        IntRange{kMinMenuMetronomeVolumeValue, kMaxMenuMetronomeVolumeValue, 1}),
               sid(String::STRING_FOR_DEFAULT_METRO_MENU_TITLE));

/// `PatchCablePolarity::getOptions` (`defaults/patch_cable_polarity.h:19-22`) is
/// `polarityToString(...)` (`modulation/patch/patch_cable.cpp:33-41`) for the two polarities --
/// literals again, so a dynamic list.
inline constexpr Node default_patch_cable_polarity =
    dynamic_enumeration(sid(String::STRING_FOR_DEFAULT_POLARITY), polarity_names, get_default_patch_cable_polarity,
                        set_default_patch_cable_polarity);

/// `StartupSongModeMenu::getOptions` (`defaults/startup_song_mode.h:19-25`).
inline constexpr uint16_t kStartupSongNames[] = {
    sid(String::STRING_FOR_DEFAULT_UI_DEFAULT_STARTUP_SONG_MODE_BLANK),
    sid(String::STRING_FOR_DEFAULT_UI_DEFAULT_STARTUP_SONG_MODE_TEMPLATE),
    sid(String::STRING_FOR_DEFAULT_UI_DEFAULT_STARTUP_SONG_MODE_LAST_OPENED),
    sid(String::STRING_FOR_DEFAULT_UI_DEFAULT_STARTUP_SONG_MODE_LAST_SAVED),
};
inline constexpr Node default_startup_song =
    enumeration(sid(String::STRING_FOR_DEFAULT_UI_DEFAULT_STARTUP_SONG_MODE), kStartupSongNames,
                get_default_startup_song, set_default_startup_song);

/// `PadBrightness` edits in steps of four and stores a quarter of what it shows
/// (`defaults/pad_brightness.h:20-30`).
inline constexpr Node default_pad_brightness =
    with_title(int_item(sid(String::STRING_FOR_DEFAULT_PAD_BRIGHTNESS), get_default_pad_brightness,
                        set_default_pad_brightness, IntRange{kMinLedBrightness << 2, kMaxLedBrightness << 2, 4}),
               sid(String::STRING_FOR_DEFAULT_PAD_BRIGHTNESS_MENU_TITLE));

/// `SliceMode::getOptions` (`defaults/slice_mode.h:19-28`).
inline constexpr uint16_t kSliceModeNames[] = {
    sid(String::STRING_FOR_CUT),
    sid(String::STRING_FOR_ONCE),
    sid(String::STRING_FOR_LOOP),
    sid(String::STRING_FOR_STRETCH),
};
inline constexpr Node default_slice_mode =
    with_title(enumeration(sid(String::STRING_FOR_DEFAULT_SLICE_MODE), kSliceModeNames, get_default_slice_mode,
                           set_default_slice_mode),
               sid(String::STRING_FOR_DEFAULT_SLICE_MODE_MENU_TITLE));
inline constexpr Node default_cpu_indicator = toggle_item(sid(String::STRING_FOR_DEFAULT_HIGH_CPU_USAGE_INDICATOR),
                                                          get_default_cpu_indicator, set_default_cpu_indicator);
inline constexpr Node default_hold_time =
    int_item(sid(String::STRING_FOR_HOLD_TIME), get_default_hold_time, set_default_hold_time, IntRange{1, 20, 1});

/// `ScreensaverModeMenu::getOptions` (`defaults/screensaver.h:29-36`).
inline constexpr uint16_t kScreensaverModeNames[] = {
    sid(String::STRING_FOR_OFF),
    sid(String::STRING_FOR_SCREENSAVER_MODE_BLANK),
    sid(String::STRING_FOR_SCREENSAVER_MODE_STARSCAPE),
    sid(String::STRING_FOR_SCREENSAVER_MODE_DELUGE),
};
inline constexpr Node screensaver_mode = enumeration(sid(String::STRING_FOR_SCREENSAVER_MODE), kScreensaverModeNames,
                                                     get_screensaver_mode, set_screensaver_mode);
inline constexpr Node screensaver_timeout =
    int_item(sid(String::STRING_FOR_SCREENSAVER_TIMEOUT), get_screensaver_timeout, set_screensaver_timeout,
             IntRange{kMinScreensaverTimeoutMinutes, kMaxScreensaverTimeoutMinutes, 1});
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

inline constexpr Node shortcuts_version = with_title(custom(sid(String::STRING_FOR_SHORTCUTS_VERSION), &kPending),
                                                     sid(String::STRING_FOR_SHORTCUTS_VER_MENU_TITLE)); // E2

/// `keyboard::Layout::getOptions` (`keyboard/layout.h:19-26`) -- QWERTY / AZERTY / QWERTZ, three
/// literals with no l10n ids, so the list is dynamic. It is the *text-entry* keyboard, not the
/// pad-grid one `defaults::KeyboardLayout` sets.
inline constexpr Node pads_keyboard_layout =
    with_title(dynamic_enumeration(sid(String::STRING_FOR_KEYBOARD_FOR_TEXT), pads_keyboard_layout_names,
                                   get_pads_keyboard_layout, set_pads_keyboard_layout),
               sid(String::STRING_FOR_KEY_LAYOUT));

inline constexpr Node pad_colour_active = custom(sid(String::STRING_FOR_ACTIVE), &kPending);   // E2
inline constexpr Node pad_colour_stopped = custom(sid(String::STRING_FOR_STOPPED), &kPending); // E2
inline constexpr Node pad_colour_muted = custom(sid(String::STRING_FOR_MUTED), &kPending);     // E2
inline constexpr Node pad_colour_soloed = custom(sid(String::STRING_FOR_SOLOED), &kPending);   // E2
inline constexpr Node pad_colour_fill = custom(sid(String::STRING_FOR_FILL), &kPending);       // E2
inline constexpr Node pad_colour_once = custom(sid(String::STRING_FOR_ONCE), &kPending);       // E2
inline constexpr const Node* kColoursChildren[] = {&pad_colour_active, &pad_colour_stopped, &pad_colour_muted,
                                                   &pad_colour_soloed, &pad_colour_fill,    &pad_colour_once};
inline constexpr Node pad_colours = submenu(sid(String::STRING_FOR_COLOURS), kColoursChildren);

inline constexpr const Node* kPadsChildren[] = {&shortcuts_version, &pads_keyboard_layout, &pad_colours};
inline constexpr Node pads_settings = submenu(sid(String::STRING_FOR_PADS), kPadsChildren);

// ---------------------------------------------------------------------------------------------
// 8-9. Sample preview and play cursor

/// `sample::browser_preview::Mode::getOptions` (`sample/browser_preview/mode.h:19-27`).
inline constexpr uint16_t kSampleBrowserPreviewNames[] = {
    sid(String::STRING_FOR_DISABLED),
    sid(String::STRING_FOR_CONDITIONAL),
    sid(String::STRING_FOR_ENABLED),
};
inline constexpr Node sample_browser_preview =
    enumeration(sid(String::STRING_FOR_SAMPLE_PREVIEW), kSampleBrowserPreviewNames, get_sample_browser_preview,
                set_sample_browser_preview);

inline constexpr Node flash_status = custom(sid(String::STRING_FOR_PLAY_CURSOR), &kPending); // E2

// ---------------------------------------------------------------------------------------------
// 10. Recording

/// `record::CountIn` is an `IntegerWithOff` capped at 4 (`record/countin.h:24`).
inline constexpr Node record_count_in =
    with_title(int_item(sid(String::STRING_FOR_COUNT_IN), get_record_count_in, set_record_count_in, IntRange{0, 4, 1}),
               sid(String::STRING_FOR_REC_COUNT_IN));
/// `record::Quantize::size()` is `SYNC_TYPE_TRIPLET` -- "can't do triplets/dots for quantize"
/// (`record/quantize.h:23`).
inline constexpr Node record_quantize = sync_level_item(sid(String::STRING_FOR_QUANTIZATION), 0, get_record_quantize,
                                                        set_record_quantize, IntRange{0, SYNC_TYPE_TRIPLET - 1, 1});
inline constexpr Node record_margins =
    toggle_item(sid(String::STRING_FOR_LOOP_MARGINS), get_record_margins, set_record_margins);

/// `monitor::Mode::getOptions` (`monitor/mode.h:21-29`).
inline constexpr uint16_t kMonitorModeNames[] = {
    sid(String::STRING_FOR_CONDITIONAL),
    sid(String::STRING_FOR_ENABLED),
    sid(String::STRING_FOR_DISABLED),
};
inline constexpr Node monitor_mode = with_title(
    enumeration(sid(String::STRING_FOR_SAMPLING_MONITORING), kMonitorModeNames, get_monitor_mode, set_monitor_mode),
    sid(String::STRING_FOR_MONITORING));

inline constexpr Node default_threshold_recording_mode =
    enumeration(sid(String::STRING_FOR_MODE), kThresholdModeNames, get_default_threshold_recording,
                set_default_threshold_recording);
inline constexpr const Node* kDefaultThresholdRecordingChildren[] = {&default_threshold_recording_mode};
inline constexpr Node default_threshold_recording =
    submenu(sid(String::STRING_FOR_THRESHOLD_RECORDING), kDefaultThresholdRecordingChildren);

/// `record::LoopCommand::getOptions` (`record/loop_command.h:30-36`).
inline constexpr uint16_t kLoopCommandNames[] = {
    sid(String::STRING_FOR_LOOP),
    sid(String::STRING_FOR_LAYERING_LOOP),
};
inline constexpr Node loop_command =
    enumeration(sid(String::STRING_FOR_LOOP_COMMAND), kLoopCommandNames, get_loop_command, set_loop_command);

inline constexpr const Node* kRecordChildren[] = {
    &record_count_in, &record_quantize, &record_margins, &monitor_mode, &default_threshold_recording, &loop_command};
inline constexpr Node record_settings = submenu(sid(String::STRING_FOR_RECORDING), kRecordChildren);

// ---------------------------------------------------------------------------------------------
// 11. Community features
//
// `runtime_feature::Settings` is a `Submenu` over `subMenuEntries`
// (`runtime_feature/settings.cpp:56-80`), 23 items in that array's order. Twenty-one of them are
// `SettingToggle`s -- `Selection`s flagged `isToggle()` (`runtime_feature/setting.h:26`) -- which is
// `Kind::Toggle` here; `SyncScalingAction` is a plain two-option `Setting`; `DevSysexAllowed` is the
// E2 placeholder the file note explains.

/// One community-feature toggle. `n` is the `RuntimeFeatureSettingType` enumerator's value
/// (`model/settings/runtime_feature_settings.h:41-66`).
#define DELUGE_RUNTIME_TOGGLE(node_name, n, name_id)                                                                   \
	inline constexpr Node node_name =                                                                                  \
	    toggle_item(sid(String::name_id), get_runtime_feature<(n)>, set_runtime_feature<(n)>)

DELUGE_RUNTIME_TOGGLE(feature_drum_randomizer, 0, STRING_FOR_COMMUNITY_FEATURE_DRUM_RANDOMIZER);
DELUGE_RUNTIME_TOGGLE(feature_quantize, 1, STRING_FOR_COMMUNITY_FEATURE_QUANTIZE);
DELUGE_RUNTIME_TOGGLE(feature_fine_tempo, 2, STRING_FOR_COMMUNITY_FEATURE_FINE_TEMPO_KNOB);
DELUGE_RUNTIME_TOGGLE(feature_catch_notes, 3, STRING_FOR_COMMUNITY_FEATURE_CATCH_NOTES);
DELUGE_RUNTIME_TOGGLE(feature_delete_unused_kit_rows, 4, STRING_FOR_COMMUNITY_FEATURE_DELETE_UNUSED_KIT_ROWS);
DELUGE_RUNTIME_TOGGLE(feature_alt_delay_params, 5, STRING_FOR_COMMUNITY_FEATURE_ALT_DELAY_PARAMS);
DELUGE_RUNTIME_TOGGLE(feature_highlight_incoming_notes, 8, STRING_FOR_COMMUNITY_FEATURE_HIGHLIGHT_INCOMING_NOTES);
DELUGE_RUNTIME_TOGGLE(feature_norns_layout, 9, STRING_FOR_COMMUNITY_FEATURE_NORNS_LAYOUT);
DELUGE_RUNTIME_TOGGLE(feature_light_shift, 11, STRING_FOR_COMMUNITY_FEATURE_LIGHT_SHIFT);
DELUGE_RUNTIME_TOGGLE(feature_dx7_engine, 12, STRING_FOR_COMMUNITY_FEATURE_DX7_ENGINE);
DELUGE_RUNTIME_TOGGLE(feature_kb_sidebar_menu_exit, 13, STRING_FOR_COMMUNITY_FEATURE_KEYBOARD_VIEW_SIDEBAR_MENU_EXIT);
DELUGE_RUNTIME_TOGGLE(feature_launch_event_playhead, 14, STRING_FOR_COMMUNITY_FEATURE_LAUNCH_EVENT_PLAYHEAD);
DELUGE_RUNTIME_TOGGLE(feature_chord_keyboard, 15, STRING_FOR_COMMUNITY_FEATURE_CHORD_KEYBOARD);
DELUGE_RUNTIME_TOGGLE(feature_alt_playback_start, 16,
                      STRING_FOR_COMMUNITY_FEATURE_ALTERNATIVE_PLAYBACK_START_BEHAVIOUR);
DELUGE_RUNTIME_TOGGLE(feature_grid_loop_pads, 17, STRING_FOR_COMMUNITY_FEATURE_GRID_VIEW_LOOP_PADS);
DELUGE_RUNTIME_TOGGLE(feature_alt_tap_tempo, 18, STRING_FOR_COMMUNITY_FEATURE_ALTERNATIVE_TAP_TEMPO_BEHAVIOUR);
DELUGE_RUNTIME_TOGGLE(feature_horizontal_menus, 19, STRING_FOR_COMMUNITY_FEATURE_HORIZONTAL_MENUS);
DELUGE_RUNTIME_TOGGLE(feature_trim_from_start, 20, STRING_FOR_COMMUNITY_FEATURE_TRIM_FROM_START_OF_AUDIO_CLIP);
DELUGE_RUNTIME_TOGGLE(feature_show_battery_level, 21, STRING_FOR_COMMUNITY_FEATURE_SHOW_BATTERY_LEVEL);
DELUGE_RUNTIME_TOGGLE(feature_rounded_corners, 22, STRING_FOR_COMMUNITY_FEATURE_ROUNDED_CORNERS);

#undef DELUGE_RUNTIME_TOGGLE

/// `ShiftIsSticky` (index 10) is the one toggle with a write of its own
/// (`runtime_feature/shift_is_sticky.cpp:20-33`).
inline constexpr Node feature_sticky_shift =
    toggle_item(sid(String::STRING_FOR_COMMUNITY_FEATURE_STICKY_SHIFT), get_runtime_feature<10>, set_shift_is_sticky);
/// `DevSysexAllowed` (index 6) -- the placeholder the file note explains.
inline constexpr Node feature_dev_sysex = custom(sid(String::STRING_FOR_COMMUNITY_FEATURE_DEV_SYSEX), &kPending);
/// `SyncScalingAction` (index 7) -- two literal options, not l10n ids.
inline constexpr Node feature_sync_scaling =
    dynamic_enumeration(sid(String::STRING_FOR_COMMUNITY_FEATURE_SYNC_SCALING_ACTION), sync_scaling_action_names,
                        get_runtime_feature<7>, set_runtime_feature<7>);

inline constexpr const Node* kRuntimeFeatureChildren[] = {&feature_drum_randomizer,
                                                          &feature_fine_tempo,
                                                          &feature_quantize,
                                                          &feature_catch_notes,
                                                          &feature_delete_unused_kit_rows,
                                                          &feature_alt_delay_params,
                                                          &feature_dev_sysex,
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
                                                          &feature_show_battery_level};
inline constexpr Node runtime_features =
    submenu(sid(String::STRING_FOR_COMMUNITY_FTS), kRuntimeFeatureChildren, Layout::Vertical, nullptr,
            sid(String::STRING_FOR_COMMUNITY_FTS_MENU_TITLE));

// ---------------------------------------------------------------------------------------------
// 12-13. Battery and firmware
//
// Both are `CustomItem`s, not a shared `Kind`: spec E's own rule (design spec line 95, "a kind
// exists only if >=3 items use it" -- the same rule Task 8 used to delete `Bipolar`/`Range`/
// `MultiRange`/`Colour`/`Action`) keeps a two-consumer kind out of `node.h`. Legacy's
// `battery::Level` / `firmware::Version` (`gui/menu_item/battery/level.h`, `firmware/version.h`)
// have no `writeCurrentValue` and no `buttonAction` override -- only a `drawPixelsForOled` that
// formats a string and draws it -- but `battery::Level` keeps state across renders (the charging
// heuristic: `lastBatteryMV`/`voltageCheckCounter`/`isCharging`) that only a stateful `CustomItem`,
// not a plain function pointer, can reproduce. Bodies (and their citations) in `settings.cpp`.

/// @brief `firmware::Version::drawPixelsForOled` (`gui/menu_item/firmware/version.h:29-32`): a
///        build-time constant string, no state at all.
class FirmwareVersionItem final : public CustomItem {
public:
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
};
inline const FirmwareVersionItem kFirmwareVersionItem{};

/// @brief `battery::Level` (`gui/menu_item/battery/level.h`): the percentage/voltage readout plus
///        legacy's charging indicator, reproduced with the same kind of state legacy keeps on the
///        menu item instance -- `mutable` fields on this one shared instance stand in for it.
class BatteryLevelItem final : public CustomItem {
public:
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;

private:
	// `battery::Level::lastBatteryMV`/`voltageCheckCounter`/`isCharging` (`battery/level.h:71-73`),
	// with the counter's 4-ticks-of-500ms cadence measured directly off elapsed `now_ms()` instead:
	// `render()` has no separate timer callback to count ticks on, only a clock reading per call.
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
inline constexpr Node firmware_version =
    with_title(custom(sid(String::STRING_FOR_FIRMWARE_VERSION), &kFirmwareVersionItem),
               sid(String::STRING_FOR_FIRMWARE_VER_MENU_TITLE));

// ---------------------------------------------------------------------------------------------

inline constexpr const Node* kSettingsRootChildren[] = {
    &settings_actions,  &cv_selection,  &gate_selection,         &trigger_clock, &midi_settings,
    &defaults_settings, &pads_settings, &sample_browser_preview, &flash_status,  &record_settings,
    &runtime_features,  &battery_level, &firmware_version};
inline constexpr Node root_settings = submenu(sid(String::STRING_FOR_SETTINGS), kSettingsRootChildren);

/// @brief The settings root as a one-element span, matching `kSoundRootRoots`' shape.
///
/// Defined in `settings.cpp`.
extern const std::span<const Node* const> kSettingsRoots;

} // namespace deluge::scenes::menu::nodes
