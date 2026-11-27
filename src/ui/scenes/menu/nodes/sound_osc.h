#pragma once

/// @brief The oscillator subtree as data: the sample recorder, both sources, both FM modulators and
///        the mixer -- six of the seventeen roots in `kBatch1Roots`.
///
/// `inline constexpr` for the reason `sound_filters.h` explains. From `origin/main:src/deluge/gui/ui/menus.cpp`'s
/// oscillator block, from `sample0RecorderMenu`. The largest of the six subtrees, and the only one with bespoke
/// (`Kind::Custom`) items.
///
// clang-format off
/// | node | legacy global | legacy class |
/// |---|---|---|
/// | `sample<i>_recorder` | `sample<i>RecorderMenu` | `osc::AudioRecorder` (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`) |
/// | `recorder` | `recorderMenu` | `HorizontalMenu` |
/// | `osc<i>_type` | `osc<i>TypeMenu` | `osc::Type` (`origin/main:src/deluge/gui/menu_item/osc/type.h`) |
/// | `file<i>_selector` | `file<i>SelectorMenu` | `FileSelector` (`origin/main:src/deluge/gui/menu_item/file_selector.h`) |
/// | `source<i>_transpose` | `source<i>TransposeMenu` | `sample::Transpose` (`origin/main:src/deluge/gui/menu_item/sample/transpose.h`) |
/// | `source<i>_wave_index` | `source<i>WaveIndexMenu` | `osc::source::WaveIndex` (`origin/main:src/deluge/gui/menu_item/osc/source/wave_index.h`) |
/// | `osc<i>_pulse_width` | `osc<i>PulseWidthMenu` | `osc::PulseWidth` (`origin/main:src/deluge/gui/menu_item/osc/pulse_width.h`) |
/// | `source<i>_feedback` | `source<i>FeedbackMenu` | `osc::source::Feedback` (`origin/main:src/deluge/gui/menu_item/osc/source/feedback.h`) |
/// | `osc<i>_phase` | `osc<i>PhaseMenu` | `osc::RetriggerPhase` (`origin/main:src/deluge/gui/menu_item/osc/retrigger_phase.h`) |
/// | `sample<i>_reverse` | `sample<i>ReverseMenu` | `sample::Reverse` (`origin/main:src/deluge/gui/menu_item/sample/reverse.h`) |
/// | `sample<i>_repeat` | `sample<i>RepeatMenu` | `sample::Repeat` (`origin/main:src/deluge/gui/menu_item/sample/repeat.h`) |
/// | `sample<i>_time_stretch` | `sample<i>TimeStretchMenu` | `sample::TimeStretch` (`origin/main:src/deluge/gui/menu_item/sample/time_stretch.h`) |
/// | `sample<i>_pitch_speed` | `sample<i>PitchSpeedMenu` | `sample::PitchSpeed` (`origin/main:src/deluge/gui/menu_item/sample/pitch_speed.h`) |
/// | `sample<i>_interpolation` | `sample<i>InterpolationMenu` | `sample::Interpolation` (`origin/main:src/deluge/gui/menu_item/sample/interpolation.h`) |
/// | `sample<i>_start` / `_end` | `sample<i>StartMenu` / `EndMenu` | `sample::Start` / `End` over `LoopPoint` (`origin/main:src/deluge/gui/menu_item/sample/loop_point.h`) |
/// | `source<i>` | `source<i>Menu` | `submenu::ActualSource` (`origin/main:src/deluge/gui/menu_item/submenu/actual_source.h`) |
/// | `modulator<i>_transpose` | `modulator<i>TransposeMenu` | `modulator::Transpose` (`origin/main:src/deluge/gui/menu_item/modulator/transpose.h`) |
/// | `modulator<i>_feedback` | `modulator<i>FeedbackMenu` | `source::patched_param::ModulatorFeedback` (`origin/main:src/deluge/gui/menu_item/source/patched_param/modulator_feedback.h`) |
/// | `modulator<i>_phase_node` | `modulator<i>PhaseMenu` | `osc::RetriggerPhase`, `for_modulator` |
/// | `modulator_dest` | `modulatorDestMenu` | `modulator::Destination` (`origin/main:src/deluge/gui/menu_item/modulator/destination.h`) |
/// | `modulator<i>` | `modulator<i>Menu` | `submenu::Modulator` (`origin/main:src/deluge/gui/menu_item/submenu/modulator.h`) |
/// | `source<i>_volume` | `source<i>VolumeMenu` | `osc::source::Volume` (`origin/main:src/deluge/gui/menu_item/osc/source/volume.h`) |
/// | `modulator<i>_volume` | `modulator<i>Volume` | `source::patched_param::ModulatorLevel` (`origin/main:src/deluge/gui/menu_item/source/patched_param/modulator_level.h`) |
/// | `noise` | `noiseMenu` | `patched_param::IntegerNonFM` (`origin/main:src/deluge/gui/menu_item/patched_param/integer_non_fm.h`) |
/// | `osc_sync` | `oscSyncMenu` | `osc::Sync` (`origin/main:src/deluge/gui/menu_item/osc/sync.h`) |
/// | `osc_mixer` | `oscMixerMenu` | `HorizontalMenu` |
// clang-format on
///
/// @note **The four bespoke items, and they are real now.** `FileSelector`, `osc::AudioRecorder` and
///       the two `LoopPoint`s do not edit a value at all -- each `beginSession`s straight into
///       another whole UI (the sample browser, the audio recorder, the sample marker editor). They
///       are `Kind::Custom` nodes over `nodes/sample.h`'s three item classes -- `FileBrowserItem`
///       over `browser::BrowserScene`, `MarkerItem` over `marker::MarkerScene` and
///       `AudioRecorderItem` over `recorder::RecorderScene` -- as six distinct instances, one per
///       (item, source) pair.
///
///       **In a horizontal page the item still does not run**: `present_column` (`menu/present.cpp`)
///       draws each of these columns from the node's `Binding` hooks, which `column_hooks.h` supplies
///       -- `file_selector_art` and `file0_label`/`file1_label` for the file selector,
///       `recorder0_draw`/`recorder1_draw` for the recorder, `loop_start_draw`/`loop_end_draw` for the
///       loop points. Legacy renders each of them itself (`FileSelector::renderInHorizontalMenu`,
///       `origin/main:src/deluge/gui/menu_item/file_selector.cpp`; `LoopPoint::renderInHorizontalMenu`,
///       `origin/main:src/deluge/gui/menu_item/sample/loop_point.cpp`; `osc::AudioRecorder::renderInHorizontalMenu`,
///       `origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`), and the hooks are their transcriptions, not the
///       items.
///
/// @note **`range_specific` and `range_dependent`.** Four legacy classes in this subtree override
///       `checkPermissionToBeginSession` to route through
///       `SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam`: `sample::Transpose`
///       (`origin/main:src/deluge/gui/menu_item/sample/transpose.h`, which also overrides `isRangeDependent()`),
///       `LoopPoint` (`origin/main:src/deluge/gui/menu_item/sample/loop_point.cpp`, also `isRangeDependent()`),
///       `FileSelector` (`origin/main:src/deluge/gui/menu_item/file_selector.cpp`) and `osc::AudioRecorder`
///       (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`), each in its
///       `checkPermissionToBeginSession`. Ten nodes
///       here carry `range_specific` (`nodes.h`'s `kRangeSpecificNodes`; the validator's
///       `Flag::RangeSpecific` audit re-checks it). Only the transposes and the loop points
///       override `isRangeDependent()`, so only they carry `range_dependent` (`with_range_dependent`
///       sets both; the file selectors and the recorders take `with_range_specific`).
///
/// @note **The four transposes carry a learn-only `ParamRef`** beside their `get`/`set` (`with_learn_param`,
///       `nodes/detail.h`). Legacy `sample::Transpose` and `modulator::Transpose` derive from `menu_item::Transpose`
///       (`origin/main:src/deluge/gui/menu_item/transpose.h`), which is a `Decimal` *and* a `PatchedParam` and
///       re-exports the whole learning surface -- `getParamIndex()`, `getLearningThing()` ->
///       `setToHaveParamOnly(getP())` (`origin/main:src/deluge/gui/menu_item/source/transpose.h`),
///       `unlearnAction`/`allowsLearnMode`/`learnKnob`. Without the reference, MIDI/CC learn on
///       `LOCAL_OSC_A/B_PITCH_ADJUST` and `LOCAL_MODULATOR_0/1_PITCH_ADJUST` would silently vanish
///      . `node.h`'s `Binding` precedence note explains why declaring both is safe.
///
/// @note **Per-source relevance.** Legacy bakes `source_id_` into each item; `RelevantFn` has no
///       room for one, so `relevance.h` declares `osc0_*` / `osc1_*` pairs. See its own note.
///
/// @note `submenu::ActualSource::getTitle()` swaps to `STRING_FOR_OSC_SAMPLE_MENU_TITLE` once the source is a sample
///       and the page is past the first (`origin/main:src/deluge/gui/menu_item/submenu/actual_source.h`):
///       `source<i>_page_title`. Every `FormattedTitle` here carries its source number. `sample::Repeat` is given none
///       on `main`, whose title then reads empty; `sample<i>_repeat` fills the digit as the reverse items do
///       (DIFFERENCES D149). `sample::PitchSpeed::isToggle()` is true
///       (`origin/main:src/deluge/gui/menu_item/sample/pitch_speed.h`): `Node::selection_is_toggle`, so a select press
///       or an already-focused button press flips the two-option `Kind::Enum` in place.
///
/// @note **Columns.** Legacy's horizontal-menu overrides here are data: the fixed labels (`RetriggerPhase`'s PHSE, the
///       reverse's PLAY, SPD, INTR, SYNC) are `with_column_name`; the shapes, direction, repeat-mode and keyboard icons
///       are `column_hooks.h`'s art; the labels that read the value or the formatted name (repeat mode, pitch/speed,
///       the level columns) and the OFF/ON notifications are its `value_text` hooks. `osc::Type` overrides only
///       `showColumnLabel()` to false (`origin/main:src/deluge/gui/menu_item/osc/type.h`), and is
///       `without_column_label`; `osc::AudioRecorder` overrides both `showColumnLabel()` and `showNotification()` to
///       false (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`), and is
///       `without_column_label`/`without_notification`.

#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/column_hooks.h"
#include "ui/scenes/menu/nodes/detail.h"
#include "ui/scenes/menu/nodes/fan_out_rules.h"
#include "ui/scenes/menu/nodes/param_shows.h"
#include "ui/scenes/menu/nodes/patch_cables.h" // with_patch_chain -- every PATCHED param chains
#include "ui/scenes/menu/nodes/sample.h"       // the six bespoke items this subtree's nodes point at
#include "ui/scenes/menu/nodes/sound_dx7.h"    // the DX7's columns, OSC1's in DX7 mode
#include "ui/scenes/menu/relevance.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace deluge::ui::scenes::menu {
struct MenuContext;
struct MenuState;
} // namespace deluge::ui::scenes::menu

namespace deluge::ui::scenes::menu::nodes {

/// @brief The shared placeholder for a node whose real item is still owed.
///
/// **Nothing points here at present** (`validate.h`'s `kPendingCustomNodes` is 0). It is a live
/// mechanism: a table that has to declare a node's shape before its item exists points here, and
/// `validate_tree` reports that, with no option able to suppress it.
///
/// Both members are defined in `sound_osc.cpp`: declaring them here keeps this header free of the
/// oled toolkit's and the shell's dependency closures, exactly as `node.h` keeps `CustomItem` free
/// of them.
class PendingCustom final : public CustomItem {
public:
	void render(deluge::ui::widgets::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;

	/// @brief Yes -- that is the whole point of this class (`node.h`'s `CustomItem::is_placeholder`).
	///        `validate_tree` reports every node pointing here unless the caller opts in.
	bool is_placeholder() const override { return true; }
};

/// @brief The single shared instance. Not `constexpr` (it has a vtable), but its *address* is a
///        constant expression, which is all `custom()` needs.
inline const PendingCustom kPending{};

/// @name Hooks with no per-source index. Bodies (and their citations) in the `.cpp`.
/// @{
/// @}

/// `menu_item::Transpose::getMinValue`/`getMaxValue`/`getNumDecimalPlaces`
/// (`origin/main:src/deluge/gui/menu_item/transpose.h`).
inline constexpr IntRange kTransposeRange{-9600, 9600, 1};

/// @brief One oscillator source page: fourteen leaves and the submenu over them.
///
/// A macro because the `get`/`set` hooks are plain function pointers with no room for legacy's
/// `source_id_` member, so each source needs its own pair, and because the `Node`s have to be
/// distinct objects for their addresses to go in a `children` array. Bodies in the `.cpp`
/// (`DELUGE_OSC_SOURCE_HOOKS`).
/// @brief `sample::Transpose::getNotificationValue`'s range suffix for source 1 or 2
///        (`origin/main:src/deluge/gui/menu_item/sample/transpose.h`): " (BOTTOM-C3)" and the like on a multisample's
///        chosen range, nothing otherwise. Defined beside the range picker's labels, in `nodes/multi_range.cpp`.
/// @param ctx The menu context: its source and range.
/// @param out Where the suffix goes.
/// @return Its length.
size_t transpose0_range_suffix(const MenuContext& ctx, std::span<char> out);
size_t transpose1_range_suffix(const MenuContext& ctx, std::span<char> out);

/// @brief `osc::RetriggerPhase`'s full screen on @p n: OFF below zero with no cursor (`drawPixelsForOled`,
///        `horizontalEncoderAction`), a turn up from OFF lands on 0 (`selectEncoderAction`), and the cursor starts on
///        the tens (`getDefaultEditPos` 1).
constexpr Node retrigger_phase_screen(Node n) {
	return with_default_edit_pos(with_decimal_restart_from_negative(with_decimal_word(n, retrigger_phase_text)), 1);
}

// OSC1's place is the DX7's in DX7 mode: its page lists the DX7's columns after its own, which hide there. OSC2's page
// hides in DX7 mode.
#define SOURCE0_DX7_COLUMNS , &dx7_engine, &dx7_transpose, &dx7_amp_mod, &dx7_phase, &dx7_level
#define SOURCE1_DX7_COLUMNS
inline constexpr RelevantFn source0_relevant = nullptr;
inline constexpr RelevantFn source1_relevant = relevance::not_dx7;

#define DELUGE_OSC_SOURCE(i, name_id, osc_path)                                                                        \
	int32_t get_source##i##_transpose(const MenuContext& ctx);                                                         \
	void set_source##i##_transpose(const MenuContext& ctx, int32_t v);                                                 \
	inline constexpr Node osc##i##_type = with_wrap_in_horizontal(                                                     \
	    with_column_art(without_column_label(with_title_arg(                                                           \
	                        with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_TYPE), Kind::Enum,            \
	                                                {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,            \
	                                                 .path = osc_path "/type",                                         \
	                                                 .whole_kit = false}),                                             \
	                                   sid(deluge::l10n::String::STRING_FOR_OSC_TYPE_MENU_TITLE)),                     \
	                        (i) + 1)),                                                                                 \
	                    osc##i##_type_art));                                                                           \
	inline constexpr Node file##i##_selector =                                                                         \
	    with_value_text(with_column_art(with_allow_session_from_horizontal(with_range_specific(                        \
	                                        custom(sid(deluge::l10n::String::STRING_FOR_FILE_BROWSER),                 \
	                                               &kFile##i##Selector, relevance::osc##i##_file_selector))),          \
	                                    file_selector_art),                                                            \
	                    file##i##_label);                                                                              \
	inline constexpr Node source##i##_transpose = with_notification_suffix(                                            \
	    with_title_arg(                                                                                                \
	        with_patch_chain(with_range_dependent(with_learn_param(                                                    \
	            with_title(decimal_item(sid(deluge::l10n::String::STRING_FOR_TRANSPOSE), get_source##i##_transpose,    \
	                                    set_source##i##_transpose, kTransposeRange, 2, relevance::osc##i##_transpose), \
	                       sid(deluge::l10n::String::STRING_FOR_OSC_TRANSPOSE_MENU_TITLE)),                            \
	            {deluge::plugin::ParamKind::PATCHED,                                                                   \
	             static_cast<uint8_t>(                                                                                 \
	                 deluge::instruments::classic::runtime_index(deluge::instruments::classic::param::kOscAPitch)      \
	                 + (i))}))),                                                                                       \
	        (i) + 1),                                                                                                  \
	    transpose##i##_range_suffix);                                                                                  \
	inline constexpr Node source##i##_wave_index = with_title_arg(                                                     \
	    with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_WAVE_INDEX),                                       \
	                           {deluge::plugin::ParamKind::PATCHED,                                                    \
	                            static_cast<uint8_t>(deluge::instruments::classic::runtime_index(                      \
	                                                     deluge::instruments::classic::param::kOscAWavetablePosition)  \
	                                                 + (i))},                                                          \
	                           Hint::Slider, param_shows::kOscWaveIndex[i],                                            \
	                           sid(deluge::l10n::String::STRING_FOR_OSC_WAVE_IND_MENU_TITLE))),                        \
	    (i) + 1);                                                                                                      \
	inline constexpr Node osc##i##_pulse_width = with_column_draw(                                                     \
	    with_title_arg(with_patch_chain(param(                                                                         \
	                       sid(deluge::l10n::String::STRING_FOR_PULSE_WIDTH),                                          \
	                       {deluge::plugin::ParamKind::PATCHED,                                                        \
	                        static_cast<uint8_t>(deluge::instruments::classic::runtime_index(                          \
	                                                 deluge::instruments::classic::param::kOscAPhaseWidth)             \
	                                             + (i))},                                                              \
	                       Hint::None, param_shows::kOscPulseWidth[i],                                                 \
	                       sid(deluge::l10n::String::STRING_FOR_OSC_P_WIDTH_MENU_TITLE), Scaling::HalfPrecision)),     \
	                   (i) + 1),                                                                                       \
	    pulse_width_draw);                                                                                             \
	inline constexpr Node source##i##_feedback = with_title_arg(                                                       \
	    with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_FEEDBACK),                                         \
	                           {deluge::plugin::ParamKind::PATCHED,                                                    \
	                            static_cast<uint8_t>(deluge::instruments::classic::runtime_index(                      \
	                                                     deluge::instruments::classic::param::kCarrier1Feedback)       \
	                                                 + (i))},                                                          \
	                           Hint::Bar, param_shows::kOscCarrierFeedback[i],                                         \
	                           sid(deluge::l10n::String::STRING_FOR_CARRIER_FEED_MENU_TITLE))),                        \
	    (i) + 1);                                                                                                      \
	inline constexpr Node osc##i##_phase = retrigger_phase_screen(with_horizontal_step(                                \
	    with_column_name(                                                                                              \
	        with_value_text(                                                                                           \
	            with_title_arg(                                                                                        \
	                with_hint(                                                                                         \
	                    with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_RETRIGGER_PHASE), Kind::Decimal,  \
	                                            {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,                \
	                                             .path = osc_path "/retrigPhase",                                      \
	                                             .whole_kit = false}),                                                 \
	                               sid(deluge::l10n::String::STRING_FOR_OSC_R_PHASE_MENU_TITLE)),                      \
	                    Hint::Slider),                                                                                 \
	                (i) + 1),                                                                                          \
	            retrigger_phase_text),                                                                                 \
	        sid(deluge::l10n::String::STRING_FOR_RETRIGGER_PHASE_SHORT)),                                              \
	    HorizontalStep{.coarse = 10, .fine = 1, .restart_from_negative = true}));                                      \
	inline constexpr Node sample##i##_reverse = with_affect_entire(with_reverse_in_horizontal(                         \
	    with_column_name(                                                                                              \
	        with_value_text(                                                                                           \
	            with_column_art(                                                                                       \
	                with_title_arg(                                                                                    \
	                    with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_REVERSE), Kind::Toggle,           \
	                                            {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,                \
	                                             .path = osc_path "/reversed"}),                                       \
	                               sid(deluge::l10n::String::STRING_FOR_SAMP_REVERSE_MENU_TITLE)),                     \
	                    (i) + 1),                                                                                      \
	                direction_art),                                                                                    \
	            on_off_text),                                                                                          \
	        sid(deluge::l10n::String::STRING_FOR_PLAY)),                                                               \
	    reverse_always));                                                                                              \
	inline constexpr Node sample##i##_repeat = with_affect_entire(with_value_text(                                     \
	    with_column_art(                                                                                               \
	        with_title_arg(with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_REPEAT_MODE), Kind::Enum,      \
	                                               {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,             \
	                                                .path = osc_path "/loopMode"}),                                    \
	                                  sid(deluge::l10n::String::STRING_FOR_SAMP_REPEAT_MENU_TITLE)),                   \
	                       (i) + 1),                                                                                   \
	        repeat_art),                                                                                               \
	    repeat_text));                                                                                                 \
	inline constexpr Node sample##i##_time_stretch = with_affect_entire(with_column_name(                              \
	    with_title_arg(with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_SPEED), Kind::Int,                 \
	                                           {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,                 \
	                                            .path = osc_path "/timeStretchAmount"}),                               \
	                              sid(deluge::l10n::String::STRING_FOR_SAMP_SPEED_MENU_TITLE)),                        \
	                   (i) + 1),                                                                                       \
	    sid(deluge::l10n::String::STRING_FOR_SPEED_SHORT)));                                                           \
	inline constexpr Node sample##i##_pitch_speed = with_affect_entire(with_value_text(                                \
	    with_column_art(                                                                                               \
	        with_selection_is_toggle(setting_item(sid(deluge::l10n::String::STRING_FOR_PITCH_SPEED), Kind::Enum,       \
	                                              {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,              \
	                                               .path = osc_path "/timeStretchEnable",                              \
	                                               .audio_clip_path = "pitchSpeedIndependent"})),                      \
	        keyboard_art),                                                                                             \
	    pitch_speed_text));                                                                                            \
	inline constexpr Node sample##i##_interpolation = with_affect_entire(with_column_name(                             \
	    with_title_arg(with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_INTERPOLATION), Kind::Enum,        \
	                                           {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,                 \
	                                            .path = osc_path "/linearInterpolation",                               \
	                                            .audio_clip_path = "linearInterpolation"}),                            \
	                              sid(deluge::l10n::String::STRING_FOR_SAMP_INTERP_MENU_TITLE)),                       \
	                   (i) + 1),                                                                                       \
	    sid(deluge::l10n::String::STRING_FOR_INTERPOLATION_SHORT)));                                                   \
	inline constexpr Node sample##i##_start =                                                                          \
	    with_column_name(with_column_draw(with_allow_session_from_horizontal(with_range_dependent(                     \
	                                          custom(sid(deluge::l10n::String::STRING_FOR_START_POINT),                \
	                                                 &kSample##i##Start, relevance::osc##i##_is_sample))),             \
	                                      loop_start_draw),                                                            \
	                     sid(deluge::l10n::String::STRING_FOR_START_POINT_SHORT));                                     \
	inline constexpr Node sample##i##_end =                                                                            \
	    with_value_text(with_column_draw(with_allow_session_from_horizontal(with_range_dependent(                      \
	                                         custom(sid(deluge::l10n::String::STRING_FOR_END_POINT), &kSample##i##End, \
	                                                relevance::osc##i##_is_sample))),                                  \
	                                     loop_end_draw),                                                               \
	                    end_point_label);                                                                              \
	inline constexpr const Node* kSource##i##Children[] = {                                                            \
	    &osc##i##_type,           &file##i##_selector,                                                                 \
	    &source##i##_transpose,   &source##i##_wave_index,                                                             \
	    &osc##i##_pulse_width,    &source##i##_feedback,                                                               \
	    &osc##i##_phase,          &sample##i##_reverse,                                                                \
	    &sample##i##_repeat,      &sample##i##_time_stretch,                                                           \
	    &sample##i##_pitch_speed, &sample##i##_interpolation,                                                          \
	    &sample##i##_start,       &sample##i##_end SOURCE##i##_DX7_COLUMNS};                                           \
	inline constexpr Node source##i =                                                                                  \
	    with_name_text(with_page_title(submenu(sid(deluge::l10n::String::name_id), kSource##i##Children,               \
	                                           Layout::Horizontal, source##i##_relevant),                              \
	                                   source##i##_page_title),                                                        \
	                   source##i##_name)

/// @brief One FM modulator page's three shared leaves. Bodies in the `.cpp`.
#define DELUGE_OSC_MODULATOR(i, modulator_path)                                                                        \
	inline constexpr Node modulator##i##_transpose = with_title_arg(                                                   \
	    with_patch_chain(with_learn_param(                                                                             \
	        with_title(                                                                                                \
	            with_decimal_places(setting_item(sid(deluge::l10n::String::STRING_FOR_TRANSPOSE), Kind::Decimal,       \
	                                             {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,               \
	                                              .path = modulator_path "/transpose",                                 \
	                                              .fan_out_when = fan_out::kFm}),                                      \
	                                2),                                                                                \
	            sid(deluge::l10n::String::STRING_FOR_FM_MOD_TRAN_MENU_TITLE)),                                         \
	        {deluge::plugin::ParamKind::PATCHED,                                                                       \
	         static_cast<uint8_t>(                                                                                     \
	             deluge::instruments::classic::runtime_index(deluge::instruments::classic::param::kModulator1Pitch)    \
	             + (i))})),                                                                                            \
	    (i) + 1);                                                                                                      \
	inline constexpr Node modulator##i##_feedback = with_title_arg(                                                    \
	    with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_FEEDBACK),                                         \
	                           {deluge::plugin::ParamKind::PATCHED,                                                    \
	                            static_cast<uint8_t>(deluge::instruments::classic::runtime_index(                      \
	                                                     deluge::instruments::classic::param::kModulator1Feedback)     \
	                                                 + (i))},                                                          \
	                           Hint::Bar, param_shows::kOnFm,                                                          \
	                           sid(deluge::l10n::String::STRING_FOR_FM_MOD_FBACK_MENU_TITLE))),                        \
	    (i) + 1);                                                                                                      \
	inline constexpr Node modulator##i##_phase_node = retrigger_phase_screen(with_horizontal_step(                     \
	    with_column_name(                                                                                              \
	        with_value_text(                                                                                           \
	            with_title_arg(                                                                                        \
	                with_hint(                                                                                         \
	                    with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_RETRIGGER_PHASE), Kind::Decimal,  \
	                                            {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,                \
	                                             .path = modulator_path "/retrigPhase",                                \
	                                             .whole_kit = false}),                                                 \
	                               sid(deluge::l10n::String::STRING_FOR_FM_MOD_RETRIG_MENU_TITLE)),                    \
	                    Hint::Slider),                                                                                 \
	                (i) + 1),                                                                                          \
	            retrigger_phase_text),                                                                                 \
	        sid(deluge::l10n::String::STRING_FOR_RETRIGGER_PHASE_SHORT)),                                              \
	    HorizontalStep{.coarse = 10, .fine = 1, .restart_from_negative = true}))

DELUGE_OSC_SOURCE(0, STRING_FOR_OSCILLATOR_1, "osc1");
DELUGE_OSC_SOURCE(1, STRING_FOR_OSCILLATOR_2, "osc2");
DELUGE_OSC_MODULATOR(0, "modulator1");
DELUGE_OSC_MODULATOR(1, "modulator2");

#undef DELUGE_OSC_SOURCE
#undef DELUGE_OSC_MODULATOR

inline constexpr Node modulator_dest = with_affect_entire(
    with_short_options(with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_DESTINATION), Kind::Enum,
                                               {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,
                                                .path = "modulator2/toModulator1",
                                                .fan_out_when = fan_out::kFm}),
                                  sid(deluge::l10n::String::STRING_FOR_FM_MOD2_DEST_MENU_TITLE))));

inline constexpr const Node* kModulator0Children[] = {&modulator0_transpose, &modulator0_feedback,
                                                      &modulator0_phase_node};
inline constexpr Node modulator0 = submenu(sid(deluge::l10n::String::STRING_FOR_FM_MODULATOR_1), kModulator0Children,
                                           Layout::Horizontal, relevance::is_fm);
inline constexpr const Node* kModulator1Children[] = {&modulator1_transpose, &modulator1_feedback,
                                                      &modulator1_phase_node, &modulator_dest};
inline constexpr Node modulator1 = submenu(sid(deluge::l10n::String::STRING_FOR_FM_MODULATOR_2), kModulator1Children,
                                           Layout::Horizontal, relevance::is_fm);

// The recorder page (`sample0RecorderMenu` .. `recorderMenu` in `origin/main:src/deluge/gui/ui/menus.cpp`). Two slots
// wide each -- `AudioRecorder::getOccupiedSlots()` (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`).
inline constexpr Node sample0_recorder =
    with_column_draw(without_notification(without_column_label(
                         with_title_arg(with_slots(with_allow_session_from_horizontal(with_range_specific(
                                                       custom(sid(deluge::l10n::String::STRING_FOR_RECORD_TO_SAMPLE),
                                                              &kSample0Recorder, relevance::is_subtractive))),
                                                   2),
                                        1))),
                     recorder0_draw);
inline constexpr Node sample1_recorder =
    with_column_draw(without_notification(without_column_label(
                         with_title_arg(with_slots(with_allow_session_from_horizontal(with_range_specific(
                                                       custom(sid(deluge::l10n::String::STRING_FOR_RECORD_TO_SAMPLE),
                                                              &kSample1Recorder, relevance::is_subtractive))),
                                                   2),
                                        2))),
                     recorder1_draw);
inline constexpr const Node* kRecorderChildren[] = {&sample0_recorder, &sample1_recorder};
inline constexpr Node recorder =
    submenu(sid(deluge::l10n::String::STRING_FOR_SAMPLE_RECORDER), kRecorderChildren, Layout::Horizontal);

// The mixer page (`source0VolumeMenu` onward in `origin/main:src/deluge/gui/ui/menus.cpp`).
inline constexpr Node source0_volume = with_value_text(
    with_title_arg(with_patch_chain(param(
                       sid(deluge::l10n::String::STRING_FOR_OSC_LEVEL_MENU_TITLE),
                       {deluge::plugin::ParamKind::PATCHED,
                        deluge::instruments::classic::runtime_index(deluge::instruments::classic::param::kOscAVolume)},
                       Hint::Bar, param_shows::kOffRingMod)),
                   1),
    source0_volume_text);
inline constexpr Node source1_volume = with_value_text(
    with_title_arg(with_patch_chain(param(
                       sid(deluge::l10n::String::STRING_FOR_OSC_LEVEL_MENU_TITLE),
                       {deluge::plugin::ParamKind::PATCHED,
                        deluge::instruments::classic::runtime_index(deluge::instruments::classic::param::kOscBVolume)},
                       Hint::Bar, param_shows::kOffRingMod)),
                   2),
    source1_volume_text);
inline constexpr Node modulator0_volume =
    with_value_text(with_title_arg(with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_FM_MOD_LEVEL_MENU_TITLE),
                                                          {deluge::plugin::ParamKind::PATCHED,
                                                           deluge::instruments::classic::runtime_index(
                                                               deluge::instruments::classic::param::kModulator1Volume)},
                                                          Hint::Bar, param_shows::kOnFm)),
                                   1),
                    modulator0_volume_text);
inline constexpr Node modulator1_volume =
    with_value_text(with_title_arg(with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_FM_MOD_LEVEL_MENU_TITLE),
                                                          {deluge::plugin::ParamKind::PATCHED,
                                                           deluge::instruments::classic::runtime_index(
                                                               deluge::instruments::classic::param::kModulator2Volume)},
                                                          Hint::Bar, param_shows::kOnFm)),
                                   2),
                    modulator1_volume_text);
inline constexpr Node noise = with_patch_chain(
    param(sid(deluge::l10n::String::STRING_FOR_NOISE_LEVEL),
          {deluge::plugin::ParamKind::PATCHED,
           deluge::instruments::classic::runtime_index(deluge::instruments::classic::param::kNoiseVolume)},
          Hint::Bar, param_shows::kOffFm));
inline constexpr Node osc_sync = with_affect_entire(
    with_column_name(setting_item(sid(deluge::l10n::String::STRING_FOR_OSCILLATOR_SYNC), Kind::Toggle,
                                  {.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,
                                   .path = "osc2/oscillatorSync",
                                   .fan_out_when = fan_out::kOscSync}),
                     sid(deluge::l10n::String::STRING_FOR_SYNC)));
inline constexpr const Node* kMixerChildren[] = {&source0_volume,    &source1_volume, &modulator0_volume,
                                                 &modulator1_volume, &noise,          &osc_sync};
inline constexpr Node osc_mixer = submenu(sid(deluge::l10n::String::STRING_FOR_OSCILLATOR_MIXER), kMixerChildren,
                                          Layout::Horizontal, relevance::not_dx7);

/// @brief This file's six contributions to `kBatch1Roots`, in `origin/main:src/deluge/gui/menu_item/generate/main.py`'s
///        order: the recorder, both sources, both modulators, the mixer.
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundOscChildren;

} // namespace deluge::ui::scenes::menu::nodes
