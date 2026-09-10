#pragma once

/// @brief The oscillator subtree as data: the sample recorder, both sources, both FM modulators and
///        the mixer -- six of the seventeen batch-1 roots.
///
/// `inline constexpr` for the reason `sound_filters.h` explains (fix round 1, I3). From
/// `gui/menu_item/generate/dmenus/oscillator/{__init__,source,modulator,recorder}.py` and
/// `generate/g_menus.inc:55-158` (at `ce4a79de0`). The largest of the six subtrees, and the only one
/// with bespoke (`Kind::Custom`) items.
///
// clang-format off
/// | node | legacy global (`g_menus.inc`) | legacy class |
/// |---|---|---|
/// | `sample<i>_recorder` | `sample<i>RecorderMenu` (`:55-56`) | `osc::AudioRecorder` (`osc/audio_recorder.h:28`) |
/// | `recorder` | `recorderMenu` (`:318`) | `HorizontalMenu` |
/// | `osc<i>_type` | `osc<i>TypeMenu` (`:62`, `:93`) | `osc::Type` (`osc/type.h:34`) |
/// | `file<i>_selector` | `file<i>SelectorMenu` (`:63`, `:94`) | `FileSelector` (`file_selector.h:26`) |
/// | `source<i>_transpose` | `source<i>TransposeMenu` (`:64`, `:95`) | `sample::Transpose` (`sample/transpose.h:31`) |
/// | `source<i>_wave_index` | `source<i>WaveIndexMenu` (`:65`, `:96`) | `osc::source::WaveIndex` (`osc/source/wave_index.h:23`) |
/// | `osc<i>_pulse_width` | `osc<i>PulseWidthMenu` (`:66`, `:97`) | `osc::PulseWidth` (`osc/pulse_width.h:25`) |
/// | `source<i>_feedback` | `source<i>FeedbackMenu` (`:67`, `:98`) | `osc::source::Feedback` (`osc/source/feedback.h:23`) |
/// | `osc<i>_phase` | `osc<i>PhaseMenu` (`:68`, `:99`) | `osc::RetriggerPhase` (`osc/retrigger_phase.h:26`) |
/// | `sample<i>_reverse` | `sample<i>ReverseMenu` (`:69`, `:100`) | `sample::Reverse` (`sample/reverse.h:27`) |
/// | `sample<i>_repeat` | `sample<i>RepeatMenu` (`:70`, `:101`) | `sample::Repeat` (`sample/repeat.h:32`) |
/// | `sample<i>_time_stretch` | `sample<i>TimeStretchMenu` (`:71`, `:102`) | `sample::TimeStretch` (`sample/time_stretch.h:27`) |
/// | `sample<i>_pitch_speed` | `sample<i>PitchSpeedMenu` (`:72`, `:103`) | `sample::PitchSpeed` (`sample/pitch_speed.h:29`) |
/// | `sample<i>_interpolation` | `sample<i>InterpolationMenu` (`:73`, `:104`) | `sample::Interpolation` (`sample/interpolation.h:25`) |
/// | `sample<i>_start` / `_end` | `sample<i>StartMenu` / `EndMenu` (`:74-75`, `:105-106`) | `sample::Start` / `End` over `LoopPoint` (`sample/loop_point.h:25`) |
/// | `source<i>` | `source<i>Menu` (`:92`, `:123`) | `submenu::ActualSource` (`submenu/actual_source.h:23`) |
/// | `modulator<i>_transpose` | `modulator<i>TransposeMenu` (`:124`, `:133`) | `modulator::Transpose` (`modulator/transpose.h:28`) |
/// | `modulator<i>_feedback` | `modulator<i>FeedbackMenu` (`:125`, `:134`) | `source::patched_param::ModulatorFeedback` (`modulator_feedback.h:23`) |
/// | `modulator<i>_phase_node` | `modulator<i>PhaseMenu` (`:126`, `:135`) | `osc::RetriggerPhase`, `for_modulator` |
/// | `modulator_dest` | `modulatorDestMenu` (`:393`) | `modulator::Destination` (`modulator/destination.h:28`) |
/// | `modulator<i>` | `modulator<i>Menu` (`:132`, `:143`) | `submenu::Modulator` (`submenu/modulator.h:21`) |
/// | `source<i>_volume` | `source<i>VolumeMenu` (`:144-145`) | `osc::source::Volume` (`osc/source/volume.h:24`) |
/// | `modulator<i>_volume` | `modulator<i>Volume` (`:146-147`) | `source::patched_param::ModulatorLevel` (`modulator_level.h:23`) |
/// | `noise` | `noiseMenu` (`:405`) | `patched_param::IntegerNonFM` (`patched_param/integer_non_fm.h:22`) |
/// | `osc_sync` | `oscSyncMenu` (`:406`) | `osc::Sync` (`osc/sync.h:26`) |
/// | `osc_mixer` | `oscMixerMenu` (`:415`) | `HorizontalMenu` |
// clang-format on
///
/// @note **The four bespoke items, and they are real now.** `FileSelector`, `osc::AudioRecorder` and
///       the two `LoopPoint`s do not edit a value at all -- each `beginSession`s straight into
///       another whole UI (the sample browser, the audio recorder, the sample marker editor). Spec
///       E §4 calls those `Kind::Custom` and names "file selector" among the seven it expects.
///       E1 and E2 pointed all eight nodes at `kPending`; **E3 task 6 gave them
///       `nodes/sample.h`'s three item classes** -- `FileBrowserItem` over `browser::BrowserScene`,
///       `MarkerItem` over `marker::MarkerScene` and `AudioRecorderItem` over
///       `recorder::RecorderScene` -- as six distinct instances, one per (item, source) pair, so
///       spec E §4's budget of ten now counts 3 classes / 6 instances here rather than 1.
///
///       **In a horizontal page the item still does not run**: `MenuScene::render_columns`
///       dispatches on `Kind` and `Custom` falls to its `default:` arm -> `meter_column`
///       (`menu_scene.cpp:743-754`), so these eight columns draw empty bars. Legacy renders each of
///       them itself (`FileSelector::renderInHorizontalMenu`, `file_selector.cpp:88-91`;
///       `LoopPoint::renderInHorizontalMenu`, `sample/loop_point.cpp:75-90`;
///       `osc::AudioRecorder::renderInHorizontalMenu`, `osc/audio_recorder.h:81-114`), which `Node`
///       has no field for and which is a `Hint`/widget question rather than a wiring one.
///
/// @note **`range_dependent`.** Four legacy classes in this subtree override
///       `checkPermissionToBeginSession` to route through
///       `SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam`: `sample::Transpose`
///       (`sample/transpose.h:120-135`, which also overrides `isRangeDependent()` at `:137`),
///       `LoopPoint` (`sample/loop_point.cpp:38-58`, `loop_point.h:31`), `FileSelector`
///       (`file_selector.cpp:68-86`) and `osc::AudioRecorder` (`osc/audio_recorder.h:65-74`). That is
///       exactly `node.h`'s own list minus `multi_range.h`, so ten nodes here carry the flag
///       (`nodes.h`'s `kRangeDependentNodes`); the validator's `Flag::RangeDependent` audit
///       re-checks it.
///
/// @note **The four transposes carry a learn-only `ParamRef`** beside their `get`/`set`
///       (`with_learn_param`, `detail.h`). Legacy `sample::Transpose` and `modulator::Transpose`
///       derive from `menu_item::Transpose` (`gui/menu_item/transpose.h:26`), which is a `Decimal`
///       *and* a `PatchedParam` and re-exports the whole learning surface -- `getParamIndex()`
///       (`transpose.h:37`), `getLearningThing()` -> `setToHaveParamOnly(getP())`
///       (`source/transpose.h:28-32`), `unlearnAction`/`allowsLearnMode`/`learnKnob`
///       (`transpose.h:28-32`). Without the reference, MIDI/CC learn on
///       `LOCAL_OSC_A/B_PITCH_ADJUST` and `LOCAL_MODULATOR_0/1_PITCH_ADJUST` would silently vanish
///       (fix round 1, I1). `node.h`'s `Binding` precedence note explains why declaring both is safe.
///
/// @note **Per-source relevance.** Legacy bakes `source_id_` into each item; `RelevantFn` has no
///       room for one, so `relevance.h` declares `osc0_*` / `osc1_*` pairs. See its own note.
///
/// @note **Legacy behaviours with no `Node` spelling, recorded rather than dropped silently.**
///       `submenu::ActualSource::getTitle()` swaps to `STRING_FOR_OSC_SAMPLE_MENU_TITLE` once the
///       source is a sample and the page number is past the first (`submenu/actual_source.h:29-40`);
///       `osc::source::Volume`, `ModulatorLevel` and `osc::AudioRecorder` all render a
///       `FormattedTitle` with the index substituted in; `sample::PitchSpeed::isToggle()` is true
///       (`sample/pitch_speed.h:80`) so legacy toggles it in place from a submenu rather than
///       opening it, while the node is a plain two-option `Kind::Enum`; and
///       `osc::Type`/`osc::AudioRecorder` return false from `showColumnLabel()`/`showNotification()`
///       (`osc/type.h:127`, `audio_recorder.h:78-79`), which `Node` cannot express.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/nodes/patch_cables.h" // with_patch_chain -- every PATCHED param chains
#include "scenes/menu/nodes/sample.h"       // the six bespoke items this subtree's nodes point at
#include "scenes/menu/relevance.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace deluge::scenes::menu {
struct MenuContext;
struct MenuState;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// @brief The shared placeholder for a node whose real item is still owed.
///
/// **Nothing anywhere points here any more** -- E3 task 6 retired this file's own eight users and
/// E3 task 7 the last two, in `nodes/roots.h` (`validate.h`'s `kPendingCustomNodes` is 0). It stays
/// for its history *and as a live mechanism*: a table that has to declare a node's shape before its
/// item exists still points here, and `validate_tree` now reports that with nothing able to
/// suppress it -- the option that could was deleted with the last user.
///
/// Both members are defined in `sound_osc.cpp`: declaring them here keeps this header free of the
/// oled toolkit's and the shell's dependency closures, exactly as `node.h` keeps `CustomItem` free
/// of them.
class PendingCustom final : public CustomItem {
public:
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
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
int32_t get_modulator_dest(const MenuContext& ctx);
void set_modulator_dest(const MenuContext& ctx, int32_t value);
int32_t get_osc_sync(const MenuContext& ctx);
void set_osc_sync(const MenuContext& ctx, int32_t value);
/// @}

/// `sample::Repeat::getOptions` (`sample/repeat.h:99-107`).
inline constexpr uint16_t kRepeatNames[] = {
    sid(deluge::l10n::String::STRING_FOR_CUT),
    sid(deluge::l10n::String::STRING_FOR_ONCE),
    sid(deluge::l10n::String::STRING_FOR_LOOP),
    sid(deluge::l10n::String::STRING_FOR_STRETCH),
};
/// `sample::PitchSpeed::getOptions` (`sample/pitch_speed.h:65-68`).
inline constexpr uint16_t kPitchSpeedNames[] = {
    sid(deluge::l10n::String::STRING_FOR_LINKED),
    sid(deluge::l10n::String::STRING_FOR_INDEPENDENT),
};
/// `AudioInterpolation::getOptions` (`gui/menu_item/audio_interpolation.h:25-28`).
inline constexpr uint16_t kInterpolationNames[] = {
    sid(deluge::l10n::String::STRING_FOR_LINEAR),
    sid(deluge::l10n::String::STRING_FOR_SINC),
};
/// `modulator::Destination::getOptions` (`modulator/destination.h:57-63`).
inline constexpr uint16_t kModulatorDestNames[] = {
    sid(deluge::l10n::String::STRING_FOR_CARRIERS),
    sid(deluge::l10n::String::STRING_FOR_MODULATOR_1),
};

/// `menu_item::Transpose::getMinValue`/`getMaxValue`/`getNumDecimalPlaces`
/// (`gui/menu_item/transpose.h:34-36`).
inline constexpr IntRange kTransposeRange{-9600, 9600, 1};
/// `osc::RetriggerPhase::getMinValue`/`getMaxValue` (`osc/retrigger_phase.h:34-35`); -1 is the "OFF"
/// sentinel legacy prints in place of a number (`:60-67`).
inline constexpr IntRange kPhaseRange{-1, 360, 1};
/// `sample::TimeStretch::getMinValue`/`getMaxValue` (`sample/time_stretch.h:65-66`).
inline constexpr IntRange kTimeStretchRange{-48, 48, 1};

/// @brief One oscillator source page: fourteen leaves and the submenu over them.
///
/// A macro because the `get`/`set` hooks are plain function pointers with no room for legacy's
/// `source_id_` member, so each source needs its own pair, and because the `Node`s have to be
/// distinct objects for their addresses to go in a `children` array. Bodies in the `.cpp`
/// (`DELUGE_OSC_SOURCE_HOOKS`).
#define DELUGE_OSC_SOURCE(i, name_id)                                                                                  \
	int32_t get_osc##i##_type(const MenuContext& ctx);                                                                 \
	void set_osc##i##_type(const MenuContext& ctx, int32_t v);                                                         \
	size_t osc##i##_type_names(const MenuContext& ctx, std::span<std::string_view> out);                               \
	int32_t get_source##i##_transpose(const MenuContext& ctx);                                                         \
	void set_source##i##_transpose(const MenuContext& ctx, int32_t v);                                                 \
	int32_t get_osc##i##_phase(const MenuContext& ctx);                                                                \
	void set_osc##i##_phase(const MenuContext& ctx, int32_t v);                                                        \
	int32_t get_sample##i##_reverse(const MenuContext& ctx);                                                           \
	void set_sample##i##_reverse(const MenuContext& ctx, int32_t v);                                                   \
	int32_t get_sample##i##_repeat(const MenuContext& ctx);                                                            \
	void set_sample##i##_repeat(const MenuContext& ctx, int32_t v);                                                    \
	int32_t get_sample##i##_speed(const MenuContext& ctx);                                                             \
	void set_sample##i##_speed(const MenuContext& ctx, int32_t v);                                                     \
	int32_t get_sample##i##_pitch_speed(const MenuContext& ctx);                                                       \
	void set_sample##i##_pitch_speed(const MenuContext& ctx, int32_t v);                                               \
	int32_t get_sample##i##_interp(const MenuContext& ctx);                                                            \
	void set_sample##i##_interp(const MenuContext& ctx, int32_t v);                                                    \
	inline constexpr Node osc##i##_type =                                                                              \
	    with_title(dynamic_enumeration(sid(deluge::l10n::String::STRING_FOR_TYPE), osc##i##_type_names,                \
	                                   get_osc##i##_type, set_osc##i##_type, relevance::not_fm),                       \
	               sid(deluge::l10n::String::STRING_FOR_OSC_TYPE_MENU_TITLE));                                         \
	inline constexpr Node file##i##_selector = with_allow_session_from_horizontal(with_range_dependent(custom(         \
	    sid(deluge::l10n::String::STRING_FOR_FILE_BROWSER), &kFile##i##Selector, relevance::osc##i##_file_selector))); \
	inline constexpr Node source##i##_transpose = with_patch_chain(with_range_dependent(with_learn_param(              \
	    with_title(decimal_item(sid(deluge::l10n::String::STRING_FOR_TRANSPOSE), get_source##i##_transpose,            \
	                            set_source##i##_transpose, kTransposeRange, 2, relevance::osc##i##_transpose),         \
	               sid(deluge::l10n::String::STRING_FOR_OSC_TRANSPOSE_MENU_TITLE)),                                    \
	    {deluge::modulation::params::Kind::PATCHED,                                                                    \
	     static_cast<uint8_t>(deluge::modulation::params::LOCAL_OSC_A_PITCH_ADJUST + (i))})));                         \
	inline constexpr Node source##i##_wave_index = with_patch_chain(param(                                             \
	    sid(deluge::l10n::String::STRING_FOR_WAVE_INDEX),                                                              \
	    {deluge::modulation::params::Kind::PATCHED,                                                                    \
	     static_cast<uint8_t>(deluge::modulation::params::LOCAL_OSC_A_WAVE_INDEX + (i))},                              \
	    Hint::Slider, relevance::osc##i##_wave_index, sid(deluge::l10n::String::STRING_FOR_OSC_WAVE_IND_MENU_TITLE))); \
	inline constexpr Node osc##i##_pulse_width =                                                                       \
	    with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_PULSE_WIDTH),                                      \
	                           {deluge::modulation::params::Kind::PATCHED,                                             \
	                            static_cast<uint8_t>(deluge::modulation::params::LOCAL_OSC_A_PHASE_WIDTH + (i))},      \
	                           Hint::None, relevance::osc##i##_pulse_width,                                            \
	                           sid(deluge::l10n::String::STRING_FOR_OSC_P_WIDTH_MENU_TITLE), Scaling::HalfPrecision)); \
	inline constexpr Node source##i##_feedback =                                                                       \
	    with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_FEEDBACK),                                         \
	                           {deluge::modulation::params::Kind::PATCHED,                                             \
	                            static_cast<uint8_t>(deluge::modulation::params::LOCAL_CARRIER_0_FEEDBACK + (i))},     \
	                           Hint::Bar, relevance::osc##i##_carrier_feedback,                                        \
	                           sid(deluge::l10n::String::STRING_FOR_CARRIER_FEED_MENU_TITLE)));                        \
	inline constexpr Node osc##i##_phase =                                                                             \
	    with_hint(with_title(decimal_item(sid(deluge::l10n::String::STRING_FOR_RETRIGGER_PHASE), get_osc##i##_phase,   \
	                                      set_osc##i##_phase, kPhaseRange, 0, relevance::osc##i##_retrigger_phase),    \
	                         sid(deluge::l10n::String::STRING_FOR_OSC_R_PHASE_MENU_TITLE)),                            \
	              Hint::Slider);                                                                                       \
	inline constexpr Node sample##i##_reverse =                                                                        \
	    with_title(toggle_item(sid(deluge::l10n::String::STRING_FOR_REVERSE), get_sample##i##_reverse,                 \
	                           set_sample##i##_reverse, relevance::osc##i##_is_sample),                                \
	               sid(deluge::l10n::String::STRING_FOR_SAMP_REVERSE_MENU_TITLE));                                     \
	inline constexpr Node sample##i##_repeat =                                                                         \
	    with_title(enumeration(sid(deluge::l10n::String::STRING_FOR_REPEAT_MODE), kRepeatNames,                        \
	                           get_sample##i##_repeat, set_sample##i##_repeat, relevance::osc##i##_is_sample),         \
	               sid(deluge::l10n::String::STRING_FOR_SAMP_REPEAT_MENU_TITLE));                                      \
	inline constexpr Node sample##i##_time_stretch =                                                                   \
	    with_title(int_item(sid(deluge::l10n::String::STRING_FOR_SPEED), get_sample##i##_speed, set_sample##i##_speed, \
	                        kTimeStretchRange, Hint::None, relevance::osc##i##_is_sample),                             \
	               sid(deluge::l10n::String::STRING_FOR_SAMP_SPEED_MENU_TITLE));                                       \
	inline constexpr Node sample##i##_pitch_speed =                                                                    \
	    enumeration(sid(deluge::l10n::String::STRING_FOR_PITCH_SPEED), kPitchSpeedNames, get_sample##i##_pitch_speed,  \
	                set_sample##i##_pitch_speed, relevance::osc##i##_pitch_speed);                                     \
	inline constexpr Node sample##i##_interpolation =                                                                  \
	    with_title(enumeration(sid(deluge::l10n::String::STRING_FOR_INTERPOLATION), kInterpolationNames,               \
	                           get_sample##i##_interp, set_sample##i##_interp, relevance::osc##i##_interpolation),     \
	               sid(deluge::l10n::String::STRING_FOR_SAMP_INTERP_MENU_TITLE));                                      \
	inline constexpr Node sample##i##_start = with_allow_session_from_horizontal(with_range_dependent(custom(          \
	    sid(deluge::l10n::String::STRING_FOR_START_POINT), &kSample##i##Start, relevance::osc##i##_is_sample)));       \
	inline constexpr Node sample##i##_end = with_allow_session_from_horizontal(with_range_dependent(                   \
	    custom(sid(deluge::l10n::String::STRING_FOR_END_POINT), &kSample##i##End, relevance::osc##i##_is_sample)));    \
	inline constexpr const Node* kSource##i##Children[] = {                                                            \
	    &osc##i##_type,        &file##i##_selector,       &source##i##_transpose,   &source##i##_wave_index,           \
	    &osc##i##_pulse_width, &source##i##_feedback,     &osc##i##_phase,          &sample##i##_reverse,              \
	    &sample##i##_repeat,   &sample##i##_time_stretch, &sample##i##_pitch_speed, &sample##i##_interpolation,        \
	    &sample##i##_start,    &sample##i##_end};                                                                      \
	inline constexpr Node source##i =                                                                                  \
	    submenu(sid(deluge::l10n::String::name_id), kSource##i##Children, Layout::Horizontal)

/// @brief One FM modulator page's three shared leaves. Bodies in the `.cpp`.
#define DELUGE_OSC_MODULATOR(i)                                                                                        \
	int32_t get_modulator##i##_transpose_v(const MenuContext& ctx);                                                    \
	void set_modulator##i##_transpose_v(const MenuContext& ctx, int32_t v);                                            \
	int32_t get_modulator##i##_phase(const MenuContext& ctx);                                                          \
	void set_modulator##i##_phase(const MenuContext& ctx, int32_t v);                                                  \
	inline constexpr Node modulator##i##_transpose = with_patch_chain(with_learn_param(                                \
	    with_title(decimal_item(sid(deluge::l10n::String::STRING_FOR_TRANSPOSE), get_modulator##i##_transpose_v,       \
	                            set_modulator##i##_transpose_v, kTransposeRange, 2, relevance::is_fm),                 \
	               sid(deluge::l10n::String::STRING_FOR_FM_MOD_TRAN_MENU_TITLE)),                                      \
	    {deluge::modulation::params::Kind::PATCHED,                                                                    \
	     static_cast<uint8_t>(deluge::modulation::params::LOCAL_MODULATOR_0_PITCH_ADJUST + (i))}));                    \
	inline constexpr Node modulator##i##_feedback = with_patch_chain(                                                  \
	    param(sid(deluge::l10n::String::STRING_FOR_FEEDBACK),                                                          \
	          {deluge::modulation::params::Kind::PATCHED,                                                              \
	           static_cast<uint8_t>(deluge::modulation::params::LOCAL_MODULATOR_0_FEEDBACK + (i))},                    \
	          Hint::Bar, relevance::is_fm, sid(deluge::l10n::String::STRING_FOR_FM_MOD_FBACK_MENU_TITLE)));            \
	inline constexpr Node modulator##i##_phase_node = with_hint(                                                       \
	    with_title(decimal_item(sid(deluge::l10n::String::STRING_FOR_RETRIGGER_PHASE), get_modulator##i##_phase,       \
	                            set_modulator##i##_phase, kPhaseRange, 0, relevance::modulator##i##_retrigger_phase),  \
	               sid(deluge::l10n::String::STRING_FOR_FM_MOD_RETRIG_MENU_TITLE)),                                    \
	    Hint::Slider)

DELUGE_OSC_SOURCE(0, STRING_FOR_OSCILLATOR_1);
DELUGE_OSC_SOURCE(1, STRING_FOR_OSCILLATOR_2);
DELUGE_OSC_MODULATOR(0);
DELUGE_OSC_MODULATOR(1);

#undef DELUGE_OSC_SOURCE
#undef DELUGE_OSC_MODULATOR

inline constexpr Node modulator_dest =
    with_title(enumeration(sid(deluge::l10n::String::STRING_FOR_DESTINATION), kModulatorDestNames, get_modulator_dest,
                           set_modulator_dest, relevance::is_fm),
               sid(deluge::l10n::String::STRING_FOR_FM_MOD2_DEST_MENU_TITLE));

inline constexpr const Node* kModulator0Children[] = {&modulator0_transpose, &modulator0_feedback,
                                                      &modulator0_phase_node};
inline constexpr Node modulator0 = submenu(sid(deluge::l10n::String::STRING_FOR_FM_MODULATOR_1), kModulator0Children,
                                           Layout::Horizontal, relevance::is_fm);
inline constexpr const Node* kModulator1Children[] = {&modulator1_transpose, &modulator1_feedback,
                                                      &modulator1_phase_node, &modulator_dest};
inline constexpr Node modulator1 = submenu(sid(deluge::l10n::String::STRING_FOR_FM_MODULATOR_2), kModulator1Children,
                                           Layout::Horizontal, relevance::is_fm);

// The recorder page (`g_menus.inc:55-61`). Two slots wide each --
// `AudioRecorder::getOccupiedSlots()` (`osc/audio_recorder.h:77`).
inline constexpr Node sample0_recorder = with_slots(
    with_allow_session_from_horizontal(with_range_dependent(
        custom(sid(deluge::l10n::String::STRING_FOR_RECORD_TO_SAMPLE), &kSample0Recorder, relevance::is_subtractive))),
    2);
inline constexpr Node sample1_recorder = with_slots(
    with_allow_session_from_horizontal(with_range_dependent(
        custom(sid(deluge::l10n::String::STRING_FOR_RECORD_TO_SAMPLE), &kSample1Recorder, relevance::is_subtractive))),
    2);
inline constexpr const Node* kRecorderChildren[] = {&sample0_recorder, &sample1_recorder};
inline constexpr Node recorder =
    submenu(sid(deluge::l10n::String::STRING_FOR_SAMPLE_RECORDER), kRecorderChildren, Layout::Horizontal);

// The mixer page (`g_menus.inc:144-158`).
inline constexpr Node source0_volume =
    with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_OSC_LEVEL_MENU_TITLE),
                           {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_OSC_A_VOLUME},
                           Hint::Bar, relevance::not_ringmod));
inline constexpr Node source1_volume =
    with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_OSC_LEVEL_MENU_TITLE),
                           {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_OSC_B_VOLUME},
                           Hint::Bar, relevance::not_ringmod));
inline constexpr Node modulator0_volume = with_patch_chain(
    param(sid(deluge::l10n::String::STRING_FOR_FM_MOD_LEVEL_MENU_TITLE),
          {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_MODULATOR_0_VOLUME}, Hint::Bar,
          relevance::is_fm));
inline constexpr Node modulator1_volume = with_patch_chain(
    param(sid(deluge::l10n::String::STRING_FOR_FM_MOD_LEVEL_MENU_TITLE),
          {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_MODULATOR_1_VOLUME}, Hint::Bar,
          relevance::is_fm));
inline constexpr Node noise =
    with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_NOISE_LEVEL),
                           {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_NOISE_VOLUME},
                           Hint::Bar, relevance::not_fm));
inline constexpr Node osc_sync = toggle_item(sid(deluge::l10n::String::STRING_FOR_OSCILLATOR_SYNC), get_osc_sync,
                                             set_osc_sync, relevance::osc_sync_allowed);
inline constexpr const Node* kMixerChildren[] = {&source0_volume,    &source1_volume, &modulator0_volume,
                                                 &modulator1_volume, &noise,          &osc_sync};
inline constexpr Node osc_mixer =
    submenu(sid(deluge::l10n::String::STRING_FOR_OSCILLATOR_MIXER), kMixerChildren, Layout::Horizontal);

/// @brief This file's six contributions to `kBatch1Roots`, in `main.py`'s order: the recorder,
///        both sources, both modulators, the mixer.
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundOscChildren;

} // namespace deluge::scenes::menu::nodes
