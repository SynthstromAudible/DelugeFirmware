#pragma once

/// @brief The relevance predicates the batch-1 tables use -- one per legacy `MenuItem::isRelevant`
///        override the six generated subtrees reach (spec E §4's "the 84 relevance predicates").
///
/// Legacy asked `item->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)`
/// (`gui/menu_item/submenu.cpp:63`); a `Node` carries a `RelevantFn` instead, called with the
/// `MenuContext` the scene resolved. Every predicate below names the legacy override it ports and
/// the line it lives on, and is a transcription -- `gui/menu_item/` is behind the
/// `scenes-includes-outside` wall.
///
/// @note **Why a source index is baked into a predicate's *name* rather than read from the
///       context.** A legacy oscillator item holds its own `source_id_` member and ignores the
///       `whichThing` argument it is handed (`osc/pulse_width.h:39-52`, `sample/transpose.h:139`,
///       ...), so `osc0PulseWidthMenu` asks about source 0 wherever it is shown. `RelevantFn` takes
///       only a `const MenuContext&` and a `Node` has no per-item payload, so the two spellings of
///       each per-source predicate (`osc0_*` / `osc1_*`) are how that member survives. Reading
///       `MenuContext::source_index` instead would be wrong today: nothing re-resolves the context
///       on the way into `source1Menu` (legacy did it in `submenu::ActualSource::beginSession` ->
///       `SoundEditor::setCurrentSource`, `sound_editor.cpp:204`), so the field would still say 0.
///
/// @note **`name_of`** exists for Task 10's `deluge_menu_dump`, which has to print *which*
///       predicate guards a node, and for the specs, which assert the registry covers every
///       predicate this header declares.

#include "scenes/menu/node.h" // RelevantFn

#include <span>
#include <string_view>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::relevance {

/// @name Sound-wide predicates
/// @{

/// @brief Is the low-pass filter on? `filter::FilterParam::isRelevant` (`filter/param.h:42-44`),
///        i.e. `FilterInfo::isOn()` (`filter/info.h:75`) for the LPF slot.
bool has_lpf(const MenuContext& ctx);
/// @brief Is the high-pass filter on? The same override for the HPF slot (`filter/param.h:42-44`,
///        `filter/info.h:75`).
bool has_hpf(const MenuContext& ctx);
/// @brief Is the sound in FM mode? `modulator::Transpose::isRelevant` (`modulator/transpose.h:76-79`),
///        `source::patched_param::ModulatorFeedback` (`modulator_feedback.h:31-34`),
///        `ModulatorLevel` (`modulator_level.h:31-34`), `submenu::Modulator` (`submenu/modulator.h:25-28`)
///        and `modulator::Destination` (`modulator/destination.h:65-68`) -- the last two spell it
///        `sound->synthMode`, the first three `sound->getSynthMode()`, which is the same member
///        (`processing/sound/sound.h:229`).
bool is_fm(const MenuContext& ctx);
/// @brief Is the sound NOT in FM mode? `osc::Type::isRelevant` (`osc/type.h:115-118`) and
///        `patched_param::IntegerNonFM::isRelevant` (`patched_param/integer_non_fm.h:25-28`).
bool not_fm(const MenuContext& ctx);
/// @brief Is the sound NOT in ring-mod mode? `osc::source::Volume::isRelevant`
///        (`osc/source/volume.h:32-35`).
bool not_ringmod(const MenuContext& ctx);
/// @brief Is the sound in subtractive mode? `osc::AudioRecorder::isRelevant`
///        (`osc/audio_recorder.h:60-63`).
bool is_subtractive(const MenuContext& ctx);
/// @brief May oscillator sync be used? `osc::Sync::isRelevant` (`osc/sync.h:56-60`): not FM, and
///        neither source is a sample.
bool osc_sync_allowed(const MenuContext& ctx);
/// @}

/// @name Per-source predicates -- `osc0_*` is legacy's `source_id_ == 0`, `osc1_*` its `== 1`
/// @{

/// @brief `FileSelector::isRelevant` (`gui/menu_item/file_selector.cpp:54-67`): always on an audio
///        clip; on a sound, wavetable needs only "not FM", sample needs subtractive.
/// @{
bool osc0_file_selector(const MenuContext& ctx);
bool osc1_file_selector(const MenuContext& ctx);
/// @}

/// @brief `sample::Transpose::isRelevant` (`sample/transpose.h:139-145`): a sample or wavetable
///        source needs a loaded audio file; anything else is always relevant.
/// @{
bool osc0_transpose(const MenuContext& ctx);
bool osc1_transpose(const MenuContext& ctx);
/// @}

/// @brief `osc::source::WaveIndex::isRelevant` (`osc/source/wave_index.h:30-35`): not FM, wavetable,
///        and an audio file loaded.
/// @{
bool osc0_wave_index(const MenuContext& ctx);
bool osc1_wave_index(const MenuContext& ctx);
/// @}

/// @brief `osc::PulseWidth::isRelevant` (`osc/pulse_width.h:39-52`): not FM; a wavetable source
///        needs a loaded file; sample and the three input modes are out.
/// @{
bool osc0_pulse_width(const MenuContext& ctx);
bool osc1_pulse_width(const MenuContext& ctx);
/// @}

/// @brief `osc::source::Feedback::isRelevant` (`osc/source/feedback.h:31-34`): FM, or the source is
///        a DX7 operator stack.
/// @{
bool osc0_carrier_feedback(const MenuContext& ctx);
bool osc1_carrier_feedback(const MenuContext& ctx);
/// @}

/// @brief `osc::RetriggerPhase::isRelevant` with `for_modulator == false`
///        (`osc/retrigger_phase.h:76-87`).
/// @{
bool osc0_retrigger_phase(const MenuContext& ctx);
bool osc1_retrigger_phase(const MenuContext& ctx);
/// @}

/// @brief The same override with `for_modulator == true` (`osc/retrigger_phase.h:76-87`): FM only,
///        then the source test.
/// @{
bool modulator0_retrigger_phase(const MenuContext& ctx);
bool modulator1_retrigger_phase(const MenuContext& ctx);
/// @}

/// @brief `isSampleModeSample` (`gui/menu_item/sample/utils.cpp:9-15`) -- the predicate
///        `sample::Reverse` (`sample/reverse.h:34-36`), `Repeat` (`sample/repeat.h:39-41`),
///        `TimeStretch` (`sample/time_stretch.h:36-38`) and `LoopPoint`
///        (`sample/loop_point.cpp:34-36`, i.e. `Start`/`End`) all delegate to.
/// @{
bool osc0_is_sample(const MenuContext& ctx);
bool osc1_is_sample(const MenuContext& ctx);
/// @}

/// @brief `sample::PitchSpeed::isRelevant` (`sample/pitch_speed.h:35-37`): an audio clip, or
///        `isSampleModeSample`.
/// @{
bool osc0_pitch_speed(const MenuContext& ctx);
bool osc1_pitch_speed(const MenuContext& ctx);
/// @}

/// @brief `sample::Interpolation::isRelevant` (`sample/interpolation.h:61-71`): an audio clip, or a
///        subtractive sound whose source is a loaded sample or one of the three input modes.
/// @{
bool osc0_interpolation(const MenuContext& ctx);
bool osc1_interpolation(const MenuContext& ctx);
/// @}

/// @}

/// @brief One `{predicate, name}` row of the registry `name_of` searches.
struct NamedPredicate {
	RelevantFn fn;
	std::string_view name;
};

/// @brief Every predicate this header declares, in declaration order.
///
/// Exposed (rather than hidden behind `name_of`) so a spec can assert the registry is complete and
/// so Task 10's dump can enumerate predicates as well as resolve them.
std::span<const NamedPredicate> registry();

/// @brief The registered name of @p fn, or `"?"` for null / an unregistered predicate.
///
/// A node with no `relevant` hook is always relevant; `name_of(nullptr)` answers `"always"` so a
/// dump can print something meaningful for it.
std::string_view name_of(RelevantFn fn);

} // namespace deluge::scenes::menu::relevance
