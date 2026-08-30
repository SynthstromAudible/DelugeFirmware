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
///         * **`hid/` state.** (This group is empty now.) `flash_status` (`PadLEDs::flashCursor`,
///           `flash/status.h:26-32`), `shortcuts_version` (`soundEditor.shortcutsVersion`,
///           `shortcuts/version.h:26-27`) and the six `pad_colour_*` (`menu_item::Colour::value`,
///           `colour.h:66`) were its members until **E2 task 11**, which found that not one of
///           the three actually needed a bespoke item -- only a reachable byte:
///             - `flashCursor` is a plain namespace-scope `uint8_t` (`hid/led/pad_leds.h:47`) that
///               a declaration reaches without including anything from `hid/`, exactly as
///               `batteryMV` is reached below.
///             - `Colour::value` genuinely could not be reached -- it was a member of a `gui/`
///               class -- so task 11 **moved the six bytes** to `FlashStorage::padColours`
///               (`storage/flash_storage.h`), where the persistence code that has always read and
///               written them (`storage/flash_storage.cpp:576-579`, `:972-975`) already lived, and
///               made `Colour::value` a reference onto one of them. No legacy call site changed.
///             - `shortcutsVersion` selects a shortcut *layer*, and this layer owns its own tables,
///               so the port keeps the setting in `menu/shortcuts.cpp` beside them
///               (`menu::set_shortcuts_version`). That is the one item whose value legacy and the
///               port hold separately; `tables_3_spec.cpp`'s `value_is_port_owned` says so.
///           All three are ordinary `Kind::Enum` nodes with get/set hooks now, as is
///           `feature_dev_sysex`. (`battery_level`/`firmware_version` used to point here too, at `kPending`,
///           but neither actually reaches behind the `hid/` wall -- `batteryMV` is a plain global
///           set from `deluge.cpp`'s main loop, the same one `sound_editor.cpp:1403` reads with its
///           own bare `extern` -- so E2 task 2 gave them real, finished `CustomItem`s instead of
///           leaving them as placeholders; see "12-13. Battery and firmware" below.)
///         * **UI navigation.** (This group is empty now. `cv_selection` / `gate_selection`
///           (`cv/selection.h:27-55`, `gate/selection.h:29-72`: `Selection`s whose select press
///           *navigates* to a page rather than committing a value) and `midi_devices`
///           (`midi/devices.h:24-39`, a scrolling device list) were its last three members until
///           E2 task 10 gave all three real items -- `CvSelectionItem`, `GateSelectionItem` and
///           `MidiDevicesItem`, `nodes/dynamic.h` -- along with the `MenuState::source_choice` /
///           `midi_cable_choice` write-back the note below used to say they were missing.) (Two
///           groups used to be here too, at
///           `kPending`: the five `reset_*` items, which E2 task 7 gave a real `ResetItem` opening
///           `context_menu::ContextMenuScene` through `MenuState::pending_scene` (see "1. Actions"
///           below); and the twelve `midi_command_*` (`midi/command.h`, MIDI-learn items), which E2
///           task 8 gave a real `MidiCommandItem` -- they navigate nowhere, they bind an incoming
///           MIDI message to a `GlobalMIDICommand`, and the entry points that deliver that message
///           are `MenuScene::learn_note_on`/`learn_cc`/`learn_program_change`/`unlearn`.)
///         * **Items with genuinely dynamic state.** (`default_tempo`, `default_swing_amount` and
///           `default_key` were on this list. E2 task 11b brought `Kind::Range` back -- they are
///           exactly the three consumers spec E §4's own threshold asks for -- and moved their
///           `lower`/`upper` off the legacy menu items into `FlashStorage::defaultRanges`, the same
///           move `padColours` made, because a member of a `gui/` class has no bare-`extern`
///           escape. See `kDefaultTempoRange` below and `ui/oled/values.h`'s `range_value`.)
///           (`default_active_scale` was `ActiveScaleMenu` (`gui/menu_item/active_scales.h:13-31`)
///           and still is -- but task 11b ported it: `nodes::ActiveScalesItem` (`nodes/scales.h`)
///           needed no new machinery either, because legacy's own renderer is
///           `Submenu::drawSubmenuItemsForOled` with a checkbox, which `ui::oled::list` already is.)
///           (`dev_sysex_allowed` was on this list: its ON option is a **randomly generated** hex
///           key built at read time, `runtime_feature/devSysexSetting.cpp:30-59`. That needed no
///           new machinery either -- a `dynamic_enumeration` names its options at render time, and
///           `feature_dev_sysex` is one. E2 task 11.)
///
/// @note **`cvSubmenu`, `gateModeMenu` and `gateOffTimeMenu` are declared, but not as children of
///       anything.** They are not children of `settingsRootMenu` -- legacy reaches them only from
///       `cv::Selection::selectButtonPress` / `gate::Selection::selectButtonPress`
///       (`cv/selection.h:41-46`, `gate/selection.h:43-54`) -- so the port hangs them off
///       `cv_selection` / `gate_selection`'s `Node::chain_targets` instead, which is what the
///       reachability walk follows to number and validate them without inventing a parent the
///       legacy tree does not have. The complete sweep of every chaining override in
///       `gui/menu_item/` is the design spec's `selectButtonPress()` amendment.
///
/// @note **Both halves of those two chains are closed (E2 task 10).** (1) Legacy's selectors
///       *write* `soundEditor.currentSourceIndex` before navigating, and `MenuContext::source_index`
///       is still not writable from a `SelectChainFn` (handed a `const MenuContext&`) -- so the
///       selector records its cursor in `MenuState::source_choice` and `MenuScene::after_render`
///       applies it to the scene's own mutable context ahead of the same tick's `select_chain`
///       call, the shape `MultiRangeItem`/`range_choice` established. (2) `cv_selection` and
///       `gate_selection` are real `CustomItem`s now (`nodes/dynamic.h`), so their renderers do
///       report a select press and the interpreter does fire the hooks. What is still owed is
///       narrower and is listed in `nodes/dynamic.h`: the formatted "CV OUTPUT 3" titles, and the
///       per-device page `midi_devices` would chain to.
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
///
///       **Task 11 adds two of exactly the same kind**, and for the same reason:
///       `flash::Status::writeCurrentValue` also calls `PadLEDs::clearTickSquares()` when the old
///       value was `FLASH_CURSOR_SLOW` (`flash/status.h:28-30`), and `Colour::writeCurrentValue`
///       also calls `renderingNeededRegardlessOfUI()` (`colour.h:46`). Both are the immediate
///       repaint, not the setting; the byte each one stores is written identically.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/nodes/dynamic.h"      // the CV/gate selectors and the MIDI device list
#include "scenes/menu/nodes/midi_command.h" // the twelve `MidiCommandItem`s
#include "scenes/menu/nodes/roots.h"        // kThresholdModeNames, and `kPending` through it
#include "scenes/menu/nodes/scales.h"       // kDefaultActiveScalesItem (task 11b)
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

/// @brief `runtime_feature::DevSysexSetting`'s read, write and option list
///        (`runtime_feature/devSysexSetting.cpp:30-59`).
///
/// Unlike its 23 plain siblings this one is not a toggle over `{0, 1}`: the stored value is a
/// **random 31-bit key** and the menu shows off/on, so `get` reports `raw != 0`
/// (`devSysexSetting.cpp:31-32`) and `set(1)` writes the key `dev_sysex_names` last displayed
/// (`:44-49`). The key itself is `onValue`, a member of the legacy item, seeded from the stored
/// value when there is one and from `getNoise()` when there is not (`:33-40`); here it is a
/// file-scope cache in `settings.cpp`, for the reason spelled out there.
/// @{
int32_t get_dev_sysex(const MenuContext& ctx);
void set_dev_sysex(const MenuContext& ctx, int32_t value);
size_t dev_sysex_names(const MenuContext& ctx, std::span<std::string_view> out);
/// @}

/// @brief `flash::Status`'s read and write (`flash/status.h:26-32`) over `PadLEDs::flashCursor`.
///
/// The `PadLEDs::clearTickSquares()` arm of `writeCurrentValue` (`flash/status.h:28-30`) does not
/// come along -- it is `hid/`, and it joins the three "write side effects stay behind" gaps the
/// file note lists. The *value* does: `flashCursor` is a plain namespace-scope `uint8_t`
/// (`hid/led/pad_leds.h:47`) that a declaration alone reaches, exactly like `batteryMV`, so
/// `settings.cpp` names it without including anything from `hid/`.
/// @{
int32_t get_flash_status(const MenuContext& ctx);
void set_flash_status(const MenuContext& ctx, int32_t value);
/// @}

/// @brief `shortcuts::Version`'s read and write (`shortcuts/version.h:26-27`).
///
/// Legacy keeps the setting on `soundEditor` and applies it by *mutating* ten cells of two shortcut
/// tables in place (`SoundEditor::setShortcutsVersion`, `gui/ui/sound_editor.cpp:111-143`). The
/// port keeps it in `menu/shortcuts.cpp` beside the tables it selects, and selects a whole layer
/// rather than editing one -- see `menu::set_shortcuts_version`.
/// @{
int32_t get_shortcuts_version(const MenuContext& ctx);
void set_shortcuts_version(const MenuContext& ctx, int32_t value);
/// @}

/// @brief `menu_item::Colour`'s read and write (`colour.h:43-47`), with the slot as a template
///        argument rather than an object identity.
///
/// The six colour items differ in exactly one thing -- which of `FlashStorage::padColours` they
/// edit -- so they are six distinct addresses of the two templates below, the shape
/// `get_runtime_feature`/`set_runtime_feature` above already uses. The `Slot` literals are plain
/// `int`s so this header need not include `storage/flash_storage.h`; `settings.cpp` pins each one
/// against `FlashStorage::PadColourSlot` with a `static_assert`, exactly as it pins `ResetItem`'s
/// `DescriptorId` literals.
///
/// `writeCurrentValue`'s `renderingNeededRegardlessOfUI()` (`colour.h:46`) does not come along --
/// it is the same class of `hid/` immediate-apply the file note lists as a gap. The stored byte,
/// which is all any renderer reads, is written identically.
/// @{
int32_t pad_colour_get(int32_t slot);
void pad_colour_set(int32_t slot, int32_t value);
template <int Slot>
int32_t get_pad_colour(const MenuContext&) {
	return pad_colour_get(Slot);
}
template <int Slot>
void set_pad_colour(const MenuContext&, int32_t value) {
	pad_colour_set(Slot, value);
}
/// @}

/// @name The CV and gate output pages -- the three legacy reaches only through a chain.
///
/// All four hook pairs address `cvEngine`'s per-output arrays through `MenuContext::source_index`,
/// which is `soundEditor.currentSourceIndex` (`context.h`), the very global legacy's
/// `cv::Selection::selectButtonPress` / `gate::Selection::selectButtonPress` assign before
/// navigating (`cv/selection.h:41-46`, `gate/selection.h:43-54`).
/// @{
int32_t get_cv_volts(const MenuContext& ctx);
void set_cv_volts(const MenuContext& ctx, int32_t value);
int32_t get_cv_transpose(const MenuContext& ctx);
void set_cv_transpose(const MenuContext& ctx, int32_t value);
size_t gate_mode_names(const MenuContext& ctx, std::span<std::string_view> out);
int32_t get_gate_mode(const MenuContext& ctx);
void set_gate_mode(const MenuContext& ctx, int32_t value);
int32_t get_gate_off_time(const MenuContext& ctx);
void set_gate_off_time(const MenuContext& ctx, int32_t value);
/// @}
/// @}

// ---------------------------------------------------------------------------------------------
// 1. Actions

/// @brief `reset_settings::Reset::selectButtonPress` (`gui/menu_item/reset_settings/reset.h:29-33`):
///        opens `context_menu::resetSettings` with the item's own `ResetSettingsAction` and returns
///        `NO_NAVIGATION` (legacy's own screen is the context menu, not a page of this item's own).
///        The port of that is `CustomItem::on_focus` -- exactly `EditNameItem`'s shape
///        (`nodes/edit_name.h`), a `MenuState::pending_scene` push rather than a direct scene call,
///        because a `scenes/menu/` node may not touch `scenes/context_menu/`'s `Shell` stack itself.
///
/// @note Spelled with a plain `uint16_t` rather than `context_menu::DescriptorId`, for the same
///       reason `EditNameItem::target_` is a `uint16_t` rather than a `rename::TargetId`: a
///       `SceneId::arg` carries a `uint16_t`, and this header should not have to pull
///       `scenes/context_menu/` in just to name five items.
class ResetItem final : public CustomItem {
public:
	explicit ResetItem(uint16_t descriptor_id) : descriptor_id_{descriptor_id} {}

	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	void on_focus(const MenuContext& ctx, MenuState& state) const override;

private:
	uint16_t descriptor_id_;
};

/// @name The five instances, one per `context_menu::DescriptorId` (`context_menu_scene.h`).
/// @{
inline const ResetItem kResetFlashItem{0};             ///< `DescriptorId::ResetFlash`
inline const ResetItem kResetCommunityFeaturesItem{1}; ///< `DescriptorId::ResetCommunityFeatures`
inline const ResetItem kResetMidiFollowItem{2};        ///< `DescriptorId::ResetMidiFollow`
inline const ResetItem kResetMidiDevicesItem{3};       ///< `DescriptorId::ResetMidiDevices`
inline const ResetItem kResetAllItem{4};               ///< `DescriptorId::ResetAll`
/// @}

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
// `cvSubmenu` (`menus.cpp:1232`), `gateModeMenu` (`:1223`) and `gateOffTimeMenu` (`:1224`) are not
// children of `settingsRootMenu` or of anything else: legacy reaches all three only through
// `cv::Selection::selectButtonPress` / `gate::Selection::selectButtonPress`. They are declared
// here, ahead of the two selectors, because a chain hook needs their addresses;
// `Node::chain_targets` is what puts them in the reachability walk so they get node ids.

/// `cv::Volts::getMinValue`/`getMaxValue`/`getNumDecimalPlaces` (`cv/volts.h:31-33`). Legacy draws
/// "Hz/V" instead of a number at zero and refuses the horizontal encoder there (`:42-56`) -- a
/// bespoke render this generation's `Decimal` interpreter has no field for, recorded not dropped.
inline constexpr Node cv_volts = with_title(
    decimal_item(sid(String::STRING_FOR_VOLTS_PER_OCTAVE), get_cv_volts, set_cv_volts, IntRange{0, 200, 1}, 2),
    sid(String::STRING_FOR_CV_V_PER_OCTAVE_MENU_TITLE));
/// `cv::Transpose` (`cv/transpose.h:31-33`) -- the same `{-9600, 9600}` two-place packing the
/// oscillator transposes use (`sound_osc.h`'s `kTransposeRange`).
inline constexpr Node cv_transpose = with_title(
    decimal_item(sid(String::STRING_FOR_TRANSPOSE), get_cv_transpose, set_cv_transpose, IntRange{-9600, 9600, 1}, 2),
    sid(String::STRING_FOR_CV_TRANSPOSE_MENU_TITLE));
inline constexpr const Node* kCvSubmenuChildren[] = {&cv_volts, &cv_transpose};
/// `cvSubmenu{STRING_FOR_CV_OUTPUT_N, {&cvVoltsMenu, &cvTransposeMenu}}` (`menus.cpp:1232`). Its
/// title is a `FormattedTitle` legacy re-formats per output (`setCvNumberForTitle`, `:2314-2316`);
/// a `Node::title` is one static string id, so the port carries the unformatted "CV OUTPUT N".
inline constexpr Node cv_submenu = submenu(sid(String::STRING_FOR_CV_OUTPUT_N), kCvSubmenuChildren);

/// `gate::Mode` (`gate/mode.h:29-73`): V-TRIG and S-TRIG always, plus CLOCK or RUN SIGNAL on the
/// two outputs that carry them -- legacy's `updateOptions(value)`, which `gate::Selection` calls
/// on its way in (`gate/selection.h:52`). The port makes that a `dynamic_names` hook over
/// `MenuContext::source_index` rather than mutable state on the item.
///
/// Legacy's `gateModeMenu` is default-constructed (`menus.cpp:1223`), so it has **no name at all**
/// -- only a `FormattedTitle` (`gate/mode.h:26-27`). A `Node::name` of 0 would trip
/// `validate_tree`'s `MissingTitle` (the builders default `title` to `name`), so the port uses the
/// title for both, as it does for `gate_off_time` just below.
inline constexpr Node gate_mode = with_title(
    dynamic_enumeration(sid(String::STRING_FOR_GATE_MODE_TITLE), gate_mode_names, get_gate_mode, set_gate_mode),
    sid(String::STRING_FOR_GATE_MODE_TITLE));
/// `gate::OffTime` (`gate/off_time.h:22-30`), over `cvEngine.minGateOffTime` -- the one page here
/// with no per-output state at all. Legacy names it `EMPTY_STRING` with the title
/// `STRING_FOR_MINIMUM_OFF_TIME` (`menus.cpp:1224`); a `Node` with `name == 0` would trip
/// `validate_tree`'s `MissingTitle`, so the port uses the title for both.
inline constexpr Node gate_off_time = decimal_item(sid(String::STRING_FOR_MINIMUM_OFF_TIME), get_gate_off_time,
                                                   set_gate_off_time, IntRange{1, 100, 1}, 1);

/// @brief `cv::Selection::selectButtonPress` (`cv/selection.h:41-46`) and
///        `gate::Selection::selectButtonPress` (`gate/selection.h:43-54`).
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

inline constexpr Node cv_selection = with_select_chain(
    with_title(custom(sid(String::STRING_FOR_CV), &kCvSelectionItem), sid(String::STRING_FOR_CV_OUTPUTS)),
    cv_selection_chain, kCvSelectionChain);
inline constexpr Node gate_selection = with_select_chain(
    with_title(custom(sid(String::STRING_FOR_GATE), &kGateSelectionItem), sid(String::STRING_FOR_GATE_OUTPUTS)),
    gate_selection_chain, kGateSelectionChain);

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

/// The twelve `midi::Command` learn items (`menus.cpp:1416-1428`), each backed by its own
/// `MidiCommandItem` (E2 task 8, `nodes/midi_command.h`) -- one per learnable `GlobalMIDICommand`.
/// The order is `midiCommandsMenu`'s own (`menus.cpp:1430-1436`), which is NOT the enum's order.
/// @{
inline constexpr Node midi_command_play = custom(sid(String::STRING_FOR_PLAY), &kMidiCommandPlayItem);
inline constexpr Node midi_command_restart = custom(sid(String::STRING_FOR_RESTART), &kMidiCommandRestartItem);
inline constexpr Node midi_command_record = custom(sid(String::STRING_FOR_RECORD), &kMidiCommandRecordItem);
inline constexpr Node midi_command_tap = custom(sid(String::STRING_FOR_TAP_TEMPO), &kMidiCommandTapItem);
inline constexpr Node midi_command_undo = custom(sid(String::STRING_FOR_UNDO), &kMidiCommandUndoItem);
inline constexpr Node midi_command_redo = custom(sid(String::STRING_FOR_REDO), &kMidiCommandRedoItem);
inline constexpr Node midi_command_loop = custom(sid(String::STRING_FOR_LOOP), &kMidiCommandLoopItem);
inline constexpr Node midi_command_layering_loop =
    custom(sid(String::STRING_FOR_LAYERING_LOOP), &kMidiCommandLayeringLoopItem);
inline constexpr Node midi_command_fill = custom(sid(String::STRING_FOR_FILL), &kMidiCommandFillItem);
inline constexpr Node midi_command_transpose = custom(sid(String::STRING_FOR_TRANSPOSE), &kMidiCommandTransposeItem);
inline constexpr Node midi_command_next_song =
    custom(sid(String::STRING_FOR_SONG_LOAD_NEXT), &kMidiCommandNextSongItem);
inline constexpr Node midi_command_shift = custom(sid(String::STRING_FOR_SHIFT), &kMidiCommandShiftItem);
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
    with_title(custom(sid(String::STRING_FOR_DEVICES), &kMidiDevicesItem), sid(String::STRING_FOR_MIDI_DEVICES));

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

/// @name The three `Kind::Range` items -- `Kind::Range`'s entire consumer set (`node.h`).
///
/// `defaultTempoMenu` / `defaultSwingAmountMenu` are `IntegerRange`
/// (`gui/ui/menus.cpp:1645-1648`), `defaultKeyMenu` is `KeyRange` (`:1650`). Their `lower`/`upper`
/// are now `FlashStorage::defaultRanges[...]` (`storage/flash_storage.h`), which both sides address,
/// so the port and legacy edit the same six values.
///
/// The bounds are the constructor arguments and, for the key, `KeyRange::selectEncoderAction`'s own
/// `KEY_MIN`/`KEY_MAX` locals (`gui/menu_item/key_range.cpp:207-208`) -- `KeyRange` has no
/// `minValue`/`maxValue` members at all.
/// @{
inline constexpr IntRange kDefaultTempoRange{60, 240, 1};
inline constexpr IntRange kDefaultSwingAmountRange{1, 99, 1};
inline constexpr IntRange kDefaultKeyRange{0, 11, 1};

/// `IntegerRange::getText` (`gui/menu_item/integer_range.cpp:107-132`), which is `intToString` per
/// end; `KeyRange::getText` (`key_range.cpp:238-278`), the note letter plus an accidental. Bodies in
/// the `.cpp`.
/// @{
std::string_view format_range_integer(int32_t value, std::span<char> scratch);
std::string_view format_range_key(int32_t value, std::span<char> scratch);
/// @}

/// @name The six edge hooks. Each is one `FlashStorage::defaultRanges` field; bodies in the `.cpp`.
/// @{
int32_t get_default_tempo_lower(const MenuContext& ctx);
void set_default_tempo_lower(const MenuContext& ctx, int32_t value);
int32_t get_default_tempo_upper(const MenuContext& ctx);
void set_default_tempo_upper(const MenuContext& ctx, int32_t value);
int32_t get_default_swing_lower(const MenuContext& ctx);
void set_default_swing_lower(const MenuContext& ctx, int32_t value);
int32_t get_default_swing_upper(const MenuContext& ctx);
void set_default_swing_upper(const MenuContext& ctx, int32_t value);
int32_t get_default_key_lower(const MenuContext& ctx);
void set_default_key_lower(const MenuContext& ctx, int32_t value);
int32_t get_default_key_upper(const MenuContext& ctx);
void set_default_key_upper(const MenuContext& ctx, int32_t value);
/// @}

/// `IntegerRange`: the ends push each other, and opening a split range lands on LEFT
/// (`gui/menu_item/integer_range.cpp:63-68`, `:76-89`).
inline constexpr RangeSpec kDefaultTempoSpec{.get_upper = get_default_tempo_upper,
                                             .set_upper = set_default_tempo_upper,
                                             .format = format_range_integer,
                                             .clamp = deluge::ui::oled::RangeClamp::Push,
                                             .focus_left_when_split = true};
inline constexpr RangeSpec kDefaultSwingSpec{.get_upper = get_default_swing_upper,
                                             .set_upper = set_default_swing_upper,
                                             .format = format_range_integer,
                                             .clamp = deluge::ui::oled::RangeClamp::Push,
                                             .focus_left_when_split = true};
/// `KeyRange` clamps each end against the other and has **no** `beginSession` override, so it takes
/// `Range::beginSession`'s bare OFF (`gui/menu_item/range.cpp:30-33`).
inline constexpr RangeSpec kDefaultKeySpec{.get_upper = get_default_key_upper,
                                           .set_upper = set_default_key_upper,
                                           .format = format_range_key,
                                           .clamp = deluge::ui::oled::RangeClamp::Bound,
                                           .focus_left_when_split = false};

inline constexpr Node default_tempo =
    range_item(sid(String::STRING_FOR_TEMPO), sid(String::STRING_FOR_DEFAULT_TEMPO), get_default_tempo_lower,
               set_default_tempo_lower, &kDefaultTempoSpec, kDefaultTempoRange);
inline constexpr Node default_swing_amount =
    range_item(sid(String::STRING_FOR_SWING_AMOUNT), sid(String::STRING_FOR_DEFAULT_SWING), get_default_swing_lower,
               set_default_swing_lower, &kDefaultSwingSpec, kDefaultSwingAmountRange);
inline constexpr Node default_key =
    range_item(sid(String::STRING_FOR_KEY), sid(String::STRING_FOR_DEFAULT_KEY), get_default_key_lower,
               set_default_key_lower, &kDefaultKeySpec, kDefaultKeyRange);
/// @}

inline constexpr Node default_swing_interval =
    sync_level_item(sid(String::STRING_FOR_SWING_INTERVAL), sid(String::STRING_FOR_DEFAULT_SWING),
                    get_default_swing_interval, set_default_swing_interval, kSwingIntervalRange);

inline constexpr Node default_scale =
    dynamic_enumeration(sid(String::STRING_FOR_INIT_SCALE), default_scale_names, get_default_scale, set_default_scale);
inline constexpr Node default_active_scale = custom(sid(String::STRING_FOR_ACTIVE_SCALES), &kDefaultActiveScalesItem);
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

/// `shortcuts::Version::getOptions` (`shortcuts/version.h:28-35`): the pre-3.0 layout and the
/// current one, in `SHORTCUTS_VERSION_1` 0 / `_3` 1 value order (`gui/ui/sound_editor.h:26-27`,
/// mirrored as `menu::ShortcutsVersion` so this layer names no `gui/` header).
inline constexpr uint16_t kShortcutsVersionNames[] = {
    sid(String::STRING_FOR_SHORTCUTS_VERSION_1),
    sid(String::STRING_FOR_SHORTCUTS_VERSION_3),
};
inline constexpr Node shortcuts_version =
    with_title(enumeration(sid(String::STRING_FOR_SHORTCUTS_VERSION), kShortcutsVersionNames, get_shortcuts_version,
                           set_shortcuts_version),
               sid(String::STRING_FOR_SHORTCUTS_VER_MENU_TITLE));

/// `keyboard::Layout::getOptions` (`keyboard/layout.h:19-26`) -- QWERTY / AZERTY / QWERTZ, three
/// literals with no l10n ids, so the list is dynamic. It is the *text-entry* keyboard, not the
/// pad-grid one `defaults::KeyboardLayout` sets.
inline constexpr Node pads_keyboard_layout =
    with_title(dynamic_enumeration(sid(String::STRING_FOR_KEYBOARD_FOR_TEXT), pads_keyboard_layout_names,
                                   get_pads_keyboard_layout, set_pads_keyboard_layout),
               sid(String::STRING_FOR_KEY_LAYOUT));

/// `menu_item::Colour::getOptions` (`colour.h:48-56`) -- nine names, shared by all six items, in
/// `Colour::Option` order (`colour.h:27-37`).
inline constexpr uint16_t kPadColourNames[] = {
    sid(String::STRING_FOR_RED),    sid(String::STRING_FOR_GREEN), sid(String::STRING_FOR_BLUE),
    sid(String::STRING_FOR_YELLOW), sid(String::STRING_FOR_CYAN),  sid(String::STRING_FOR_MAGENTA),
    sid(String::STRING_FOR_AMBER),  sid(String::STRING_FOR_WHITE), sid(String::STRING_FOR_PINK),
};

/// The six, in `coloursSubmenu`'s own order (`gui/ui/menus.cpp:1248-1253`). The template argument is
/// `FlashStorage::PadColourSlot`, pinned in `settings.cpp`.
inline constexpr Node pad_colour_active =
    enumeration(sid(String::STRING_FOR_ACTIVE), kPadColourNames, get_pad_colour<0>, set_pad_colour<0>);
inline constexpr Node pad_colour_stopped =
    enumeration(sid(String::STRING_FOR_STOPPED), kPadColourNames, get_pad_colour<1>, set_pad_colour<1>);
inline constexpr Node pad_colour_muted =
    enumeration(sid(String::STRING_FOR_MUTED), kPadColourNames, get_pad_colour<2>, set_pad_colour<2>);
inline constexpr Node pad_colour_soloed =
    enumeration(sid(String::STRING_FOR_SOLOED), kPadColourNames, get_pad_colour<3>, set_pad_colour<3>);
inline constexpr Node pad_colour_fill =
    enumeration(sid(String::STRING_FOR_FILL), kPadColourNames, get_pad_colour<4>, set_pad_colour<4>);
inline constexpr Node pad_colour_once =
    enumeration(sid(String::STRING_FOR_ONCE), kPadColourNames, get_pad_colour<5>, set_pad_colour<5>);
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

/// `flash::Status::getOptions` (`flash/status.h:33-41`) -- and the order is the *value* order,
/// `FLASH_CURSOR_FAST` 0 / `_OFF` 1 / `_SLOW` 2 (`hid/led/pad_leds.h:25-27`), not a reading order.
inline constexpr uint16_t kFlashStatusNames[] = {
    sid(String::STRING_FOR_FAST),
    sid(String::STRING_FOR_DISABLED),
    sid(String::STRING_FOR_SLOW),
};
inline constexpr Node flash_status =
    enumeration(sid(String::STRING_FOR_PLAY_CURSOR), kFlashStatusNames, get_flash_status, set_flash_status);

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
/// `DevSysexAllowed` (index 6). A `dynamic_enumeration` rather than a `toggle_item` because its ON
/// option is not a fixed string: `DevSysexSetting::getOptions` renders the eight hex digits of the
/// key that would be written (`runtime_feature/devSysexSetting.cpp:52-59`).
inline constexpr Node feature_dev_sysex = dynamic_enumeration(sid(String::STRING_FOR_COMMUNITY_FEATURE_DEV_SYSEX),
                                                              dev_sysex_names, get_dev_sysex, set_dev_sysex);
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
	void on_focus(const MenuContext& ctx, MenuState& state) const override;

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
