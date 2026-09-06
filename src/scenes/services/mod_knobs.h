#pragma once

/// @file
/// @brief `ModKnobs` -- the gold-knob half of `View`, as a `shell::Service`.
///
/// The port of `View::modEncoderAction` (`gui/views/view.cpp:818-842`) and everything it reaches:
/// the two turn arms (`modEncoderAction_nonExistentParam` `:843-852`,
/// `modEncoderAction_existentParam` `:853-948`), the turn arithmetic
/// (`calculateKnobPosForModEncoderTurn` `:949-970`), the param resolution
/// (`getModelStackWithParam` `:971-1010`), the parameter name
/// (`getParameterNameFromModEncoder` `:1012-1057`), the centre detent
/// (`potentiallyMakeItHarderToTurnKnob` `:1058-1083`), the three "set active mod controllable"
/// entry points (`setActiveModControllableTimelineCounter` `:1867-1899`,
/// `setActiveModControllableWithoutTimelineCounter` `:1900-1922`, `setModRegion` `:1923-1950`),
/// the knob-untouched backdate (`pretendModKnobsUntouchedForAWhile` `:1951-1955`), the automation
/// notification (`notifyParamAutomationOccurred` `:1691-1712`), the MIDI-follow feedback
/// (`sendMidiFollowFeedback` `:1713-1731`, `isClipContext` `:1732-1740`), the edited flag
/// (`instrumentBeenEdited` `:1326-1331`) and the mod-knob mode (`getModKnobMode` `:1680-1690`).
///
/// @par Why a service in `src/scenes/` and not in `src/shell/`
///
/// `shell::Service` is a shell interface, but `src/shell/**` may not include `model/**` -- the
/// `shell-includes-outside` row in `scripts/check_layer_rules.py` -- and this class is *entirely*
/// about the model: what the two gold knobs point at is a `ModControllable` plus a `ParamManager`
/// plus a `TimelineCounter`, and turning one writes an `AutoParam`. So it implements the shell's
/// interface from inside the scenes library, which may see both. That is the architectural point
/// of this class, and the same reason `GlobalControls` and `MidiLearn` (E4 tasks 4 and 5) live
/// here too. `shell::Notices` (`shell/notices.h`) and `shell::Screensaver`
/// (`shell/screensaver.h`) are the two services that legitimately stayed in the shell: neither
/// touches the model.
///
/// The model boundary is narrower still. Only `menu/context.cpp` and `menu/bindings.cpp` may
/// include `model/model_stack.h` (`scenes-model-stack-outside-context`), so the four transactions
/// that need a live `ModelStack` -- `menu::mod_target_from_timeline`, `menu::mod_encoder_turn`,
/// `menu::mod_param_info`, `menu::mod_region_grab_values` -- are declared in `menu/context.h` and
/// defined in `menu/context.cpp`, over the POD in `services/mod_target.h`. Every *decision* is
/// here; nothing here names a stack type.
///
/// @par What the router sends here, and what it does not
///
/// `Router::route` consults `services.mod_knobs` only for a mod button (`ButtonId::Mod0..Mod7`) or a
/// gold-knob turn (`EncoderRole::GoldUpper`/`GoldLower`) -- `shell/router.h:55-65`.
///
/// @warning **It consults it BEFORE the top scene, and that is not legacy's order.** Legacy sends
///          both to the *current UI* first, with `View` reached only as the base-class fallback:
///          `interpretEncoders` calls `getCurrentUI()->modEncoderAction`
///          (`hid/encoder_input.cpp:194`) and `Buttons::buttonAction`'s mod-button loop calls
///          `getCurrentUI()->modButtonAction` (`hid/buttons.cpp:104`), and it is `UI`'s own base
///          implementations that forward to `view` (`gui/ui/ui.cpp:35-41`). Six UIs override
///          `modEncoderAction` -- `automation_view.h:116`, `performance_view.h:96`,
///          `arranger_view.h:49`, `instrument_clip_view.h:215`, `session_view.h:100`,
///          `sound_editor.h:80` -- and two override `modButtonAction`
///          (`session_view.h:81`, `performance_view.h:98`). So the chain should be
///          top scene -> `mod_knobs` -> `global_controls`, and it is not yet.
///
///          Found and reported by E4 task 2 (its report's finding 1); **ruled on by the
///          controller and implemented as E4 task 3's first steps**, with its own spec. Nothing
///          regresses in the meantime -- no scene ported so far overrides either hook -- but
///          `SoundEditor`'s port is `MenuScene`, and `SoundEditor::modEncoderAction`
///          (`gui/ui/sound_editor.cpp:1583-1607`) has a real MIDI-learn arm, so E4 task 5 depends
///          on the fix. This class is unaffected by which way round it goes: it claims a gold turn
///          (nothing else in legacy sees one once the chain finishes) and claims nothing else.
///
/// A gold knob's own **push** is deliberately not this service's event. Legacy routes
/// `MOD_ENCODER_0`/`MOD_ENCODER_1` from `buttonAction`'s *tail*, after the current UI's own
/// `buttonAction` has already had it (`hid/buttons.cpp:225-231`), so it reaches the scene first
/// here too, exactly as any other plain button does. `mod_knobs_spec.cpp` pins that ordering.
/// `View::modEncoderButtonAction` (`view.cpp:1332-1350`) is therefore E4 task 3's, not this one's.
///
/// @par Acceleration: the carry item this closes
///
/// Gold-knob acceleration (`ContinuousEncoder::calcNextKnobSpeed`, `hid/encoders.cpp:46-77`) has
/// been recorded as owed since E1 and carried through E2 and E3 -- see `src/scenes/README.md`'s
/// carry list. **It is closed here.** The arithmetic itself was already ported, per encoder role,
/// as `InputLowering::calc_next_knob_speed` (plan D); what was missing was a caller, exactly as
/// `input_lowering.cpp:187-189` says ("acceleration is applied by whoever consumes it"). Legacy's
/// consumer is `hid/encoder_input.cpp:192`, `offset * encoder.calcNextKnobSpeed(offset)`, one line
/// before `modEncoderAction` -- so this class, which is `modEncoderAction`, is that consumer. The
/// curve is legacy's exactly (the six constants of `hid/encoders.cpp:54-59`) and so is the
/// narrowing back to `int8_t` (`encoder_input.cpp:192`, truncation toward zero, not rounding).
///
/// **Two things the port does not reproduce, both plan D's shape rather than this task's, written
/// down here because this is the task that closes the carry** (fix round 1, Minor 4):
///
///   1. **Legacy's ramp is shared by both gold knobs; the port's is per encoder role.**
///      `calcNextKnobSpeed`'s `last_offset` and `last_encoder_time` are function-local `static`s
///      (`hid/encoders.cpp:62-63`) on a method both `mod0` and `mod1` call, so on the device,
///      alternating the two knobs resets the ramp -- each turn sees the *other* knob's direction
///      and timestamp. `InputLowering` keeps a set per `EncoderRole`
///      (`input_lowering.h:385-388`), so here the two knobs accelerate independently. Only
///      `currentKnobSpeed` is genuinely per-encoder in legacy (`ContinuousEncoder`'s member);
///      the other two are not, and that looks far more like an oversight in legacy than a design,
///      which is presumably why plan D split them.
///   2. **Legacy's clock is sub-millisecond and the port's is not.** Legacy divides by
///      `getSystemTime()` deltas as a `double` (`hid/encoders.cpp:72`); the port's clock is whole
///      milliseconds and reads a zero interval as 1 ms
///      (`InputLowering::calc_next_knob_speed`'s `smallest_interval`). The acceleration term
///      `1.0 / dt * 0.1` is therefore capped at 100 per turn here where legacy has no cap, so a
///      very fast flick reaches the 3.0 ceiling in the same small number of detents but by a
///      slightly flatter path.
///
/// @par What is deliberately still owed
///
///   * **The LED and indicator half** -- `setModLedStates`, `setKnobIndicatorLevels`,
///     `setKnobIndicatorLevel`, `convertPatchCableKnobPosToIndicatorLevel`, `modButtonAction`,
///     `modEncoderButtonAction*`. Those are E4 task 3's citations, not this task's. What this
///     class does do is *record* what they will spend: `blink_indicator()` and
///     `blink_indicator_bipolar()` hold `potentiallyMakeItHarderToTurnKnob`'s decision, and
///     `automation_display_pending()` holds `notifyParamAutomationOccurred`'s.
///   * **`View::getParameterNameFromModEncoder`** (`view.cpp:1012-1057`). Cited by this task's
///     brief but deferred to task 3, deliberately. Its only two callers anywhere are
///     `ModControllableAudio::displayOtherModKnobSettings`
///     (`model/mod_controllable/mod_controllable_audio.cpp:1672` and `:1687`), which builds the
///     two-line popup a held mod button shows -- and `modButtonAction` (`view.cpp:1497-1547`), the
///     thing that *reaches* that method, is task 3's citation. So nothing here would call it and no
///     spec could drive it against a caller. It also needs `getSourceDisplayName`
///     (`util/functions.cpp:315-368`), whose only in-layer transcription today is private to
///     `menu/nodes/patch_cables.cpp` -- and whose *sibling* transcription of `sourceToStringShort`
///     there disagrees with legacy (see the task-2 report). Consolidating those belongs with the
///     task that owns the naming half. `menu::mod_param_info` -- the model half it needs -- is
///     already built and specced.
///
///     @note **Those two call sites are a back-reference the port will have to break.** They are
///           *model* code (`model/mod_controllable/`) calling into a `gui/` singleton, and the same
///           function reaches `view.isClipContext()` at `:1677` and `display->popupText` at
///           `:1694`. `src/scenes` may include `model/**` but the reverse edge does not exist and
///           must not be created, so whoever ports `displayOtherModKnobSettings` has to invert it
///           -- the mod-button popup is built by the UI from what the model reports, not by the
///           model calling the UI. Recorded here because nothing else records it (fix round 1,
///           Important 3).
///   * **The popup's three suppression tests** (`view.cpp:868-886`) -- see `popup_suppressed()`.
///   * **The VU-meter sidebar invalidation** at `:1889-1891`/`:1912-1914`. `View::renderedVUMeter`
///     is a render flag of the views E5/E6 port, not of the knobs; nothing here can read it.
///
/// @warning **No wall clock.** `Service::tick(now_ms, dt_ms)` is the only place a clock arrives,
///          and the three `set_*` entry points are called from the model, not from a tick. They
///          therefore backdate against the last `now_ms` this service was ticked with; see
///          `pretend_knobs_untouched()`.

#include "scenes/overlay/param_popup_scene.h"
#include "scenes/services/mod_target.h"

#include "shell/scene_stack.h"
#include "shell/service.h"

#include <cstdint>
#include <optional>

class ModControllable;
class ParamManager;
class Song;
class TimelineCounter;

namespace deluge::ui::shell {
class InputLowering;
}

namespace deluge::scenes::services {

/// @brief The two gold knobs: what they point at, what turning one does, and how fast.
class ModKnobs final : public deluge::ui::shell::Service {
public:
	/// @brief How many gold knobs there are -- legacy's `NUM_LEVEL_INDICATORS` / the two
	///        `ContinuousEncoder`s `mod0`/`mod1` (`hid/encoders.h:80-81`).
	static constexpr int32_t kKnobCount = 2;

	/// @param popup    The value popup this service shows through (E4 task 1).
	/// @param stack    The scene stack it pushes that popup onto.
	/// @param lowering The input lowering it reads acceleration from and backdates the
	///                 "last turned" clock on.
	///
	/// @note The `lowering` argument is an addition to the interface the plan specified, and it is
	///       forced: both `calcNextKnobSpeed`'s per-role state and `timeModEncoderLastTurned` live
	///       on `InputLowering`, and `UiContext::lowering` is a **`const`** pointer
	///       (`shell/scene.h`), so neither can be reached from `handle()`'s context. See the class
	///       doc's acceleration note.
	ModKnobs(overlay::ParamPopupScene& popup, deluge::ui::shell::SceneStack& stack,
	         deluge::ui::shell::InputLowering& lowering)
	    : popup_(popup), stack_(stack), lowering_(lowering) {}

	/// @name The target: what the knobs point at
	/// @{

	/// @brief Which song the target is resolved against -- legacy's `currentSong` read at
	///        `view.cpp:1873` and `:1905`.
	///
	/// An explicit setter because `scenes-currentSong` (`scripts/check_layer_rules.py`) forbids
	/// this layer the global, exactly as `menu::context_for` takes a `Song&` rather than reading
	/// it.
	/// @warning Legacy re-reads `currentSong` on every `setActiveModControllable*` call, so this
	///          must be updated whenever the song changes; a stale song resolves a target against
	///          the wrong one.
	/// @param song The current song, or null before one exists.
	void set_song(Song* song) { target_.song = song; }

	/// @brief Point the knobs at @p counter's active mod controllable --
	///        `View::setActiveModControllableTimelineCounter` (`view.cpp:1867-1899`).
	/// @param counter             The timeline counter (a clip, or the song), or null.
	/// @param send_midi_feedback  `shouldSendMidiFeedback` (`:1867`, default true at `view.h:76`):
	///                            re-send the learned params' CCs, because their values may have
	///                            changed.
	void set_target_with_timeline(TimelineCounter* counter, bool send_midi_feedback);

	/// @brief Point the knobs at a bare mod controllable with no timeline --
	///        `View::setActiveModControllableWithoutTimelineCounter` (`view.cpp:1900-1922`).
	/// @param controllable What to edit.
	/// @param params       Its param manager.
	void set_target_without_timeline(ModControllable* controllable, ParamManager* params);

	/// @brief Set the region automation is read and written at -- `View::setModRegion`
	///        (`view.cpp:1923-1950`).
	/// @param pos         `View::modPos`; `0xFFFFFFFF` is legacy's default (`view.h:81`).
	/// @param length      `View::modLength`; 0 means "not editing a region".
	/// @param note_row_id `View::modNoteRowId`.
	void set_mod_region(uint32_t pos = 0xFFFFFFFF, uint32_t length = 0, int32_t note_row_id = 0);

	/// @brief The mod-button mode the active mod controllable is in -- `View::getModKnobMode`
	///        (`view.cpp:1680-1690`).
	/// @return The mode, or -1 when there is no target or it does not support modes
	///         (`ModControllable::getModKnobMode` returns null).
	int32_t mode() const;

	/// @brief Backdate both knobs' "last turned" clocks --
	///        `View::pretendModKnobsUntouchedForAWhile` (`view.cpp:1951-1955`).
	///
	/// Called by all three `set_*` entry points above, exactly where legacy calls it (`:1871`,
	/// `:1903`, `:1929`). Backdates against `now_ms_`, the last clock `tick()` saw -- the entry
	/// points are model-driven and carry no clock of their own.
	void pretend_knobs_untouched();

	/// @brief Tell the model the instrument was edited -- `View::instrumentBeenEdited`
	///        (`view.cpp:1326-1331`).
	///
	/// Public because `modEncoderAction` calls it **outside** its `modControllable` test
	/// (`view.cpp:840`), so it runs on every turn including the no-target one, and because the
	/// mod-encoder *button* path (task 3) calls it too (`:1370-1372`).
	void instrument_been_edited();

	/// @brief A param was automated -- `View::notifyParamAutomationOccurred` (`view.cpp:1691-1712`).
	///
	/// Legacy arms two 25 ms `uiTimerManager` timers, `DISPLAY_AUTOMATION` and
	/// `SEND_MIDI_FEEDBACK_FOR_AUTOMATION` (`ui_timer_manager.cpp:157-171`, `:173-...`); this arms
	/// two deadlines `tick()` fires, since `shell-no-clock` forbids anything else. Only the second
	/// has a consumer here (the MIDI feedback); the first sets `automation_display_pending()` for
	/// task 3's `leds()` -- legacy's callback is `automationView.displayAutomation()` /
	/// `view.displayAutomation()`, both of which are view-side and not ported.
	///
	/// @note Legacy's second half of the test, `getCurrentUI() == &soundEditor && paramManager ==
	///       soundEditor.currentParamManager` (`:1692-1693`), is a UI read this layer cannot make;
	///       only the `paramManager == activeModControllableModelStack.paramManager` half is
	///       ported. A menu editing a param manager the knobs are *not* pointed at therefore does
	///       not arm these here.
	/// @param params            The param manager that changed.
	/// @param update_mod_levels Legacy's `updateModLevels` (default true, `view.h:82`).
	void notify_param_automation_occurred(ParamManager* params, bool update_mod_levels = true);

	/// @}

	/// @name `shell::Service`
	/// @{
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	void tick(uint32_t now_ms, uint32_t dt_ms) override;
	std::optional<uint32_t> next_timer() const override;
	/// @}

	/// @name Observable state
	/// For specs, and for E4 task 3's `leds()`.
	/// @{
	/// @return What the knobs currently point at.
	const ModTarget& target() const { return target_; }
	/// @brief Should knob @p which_encoder's indicator be blinking? --
	///        `indicator_leds::blinkKnobIndicator` / `stopBlinkingKnobIndicator`
	///        (`view.cpp:1074`, `:1080`).
	/// @param which_encoder 0 (lower) or 1 (upper).
	bool blink_indicator(int32_t which_encoder) const;
	/// @brief Was the last blink decision for a bipolar param? -- `isParamBipolar` (`view.cpp:1072`),
	///        which is `blinkKnobIndicator`'s second argument.
	/// @param which_encoder 0 (lower) or 1 (upper).
	bool blink_indicator_bipolar(int32_t which_encoder) const;
	/// @brief Is a `DISPLAY_AUTOMATION` refresh owed? See `notify_param_automation_occurred`.
	bool automation_display_pending() const { return automation_display_ms_.has_value(); }
	/// @brief Would `notify_param_automation_occurred`'s refresh also re-level the mod knobs?
	///        Legacy's `pendingParamAutomationUpdatesModLevels` (`view.h:123`).
	bool automation_updates_mod_levels() const { return automation_updates_mod_levels_; }
	/// @}

	/// @brief `View::calculateKnobPosForModEncoderTurn` (`view.cpp:949-970`) -- the pure turn
	///        arithmetic, exposed so a spec can drive it (and mutate it) on its own.
	///
	/// Clamps `knob_pos + offset` into the kind's range: patch cables reach down to -192, MIDI
	/// stops one short of the top at 63, everything else bottoms at -64. The lower limit is
	/// `min(limit, knob_pos)`, so a position already below the limit is never dragged up by a turn.
	/// @param kind     The param's kind.
	/// @param knob_pos The position before the turn.
	/// @param offset   The signed turn.
	/// @return The clamped new position.
	static int32_t calculate_knob_pos_for_mod_encoder_turn(deluge::modulation::params::Kind kind, int32_t knob_pos,
	                                                       int32_t offset);

	/// @brief `View::convertPatchCableKnobPosToIndicatorLevel` (`view.cpp:1473-1486`) -- a patch
	///        cable's -128..+128 knob position folded into the indicator's 0..128 range.
	///
	/// Ported here because `potentiallyMakeItHarderToTurnKnob` (this task's citation) needs it;
	/// E4 task 3, which owns the indicator levels proper, reuses it rather than porting it twice.
	/// @param knob_pos The patch-cable position, offset by `kKnobPosOffset`.
	/// @return The indicator-range position.
	static int32_t convert_patch_cable_knob_pos_to_indicator_level(int32_t knob_pos);

private:
	/// @brief One gold-knob turn -- `View::modEncoderAction` (`view.cpp:818-842`).
	/// @param which_encoder 0 (lower) or 1 (upper).
	/// @param offset        The already-accelerated signed turn.
	/// @param now_ms        The tick's wall clock.
	/// @return What the shell should do about it.
	deluge::ui::shell::Result mod_encoder_action(int32_t which_encoder, int32_t offset, uint32_t now_ms);

	/// @brief Show the value popup for a turn -- `view.cpp:899`, plus its three suppression tests.
	/// @param turn   What the turn resolved to.
	/// @param now_ms The tick's wall clock.
	void show_popup(const ModTurn& turn, uint32_t now_ms);

	/// @brief Should the popup be suppressed for this param? -- `view.cpp:868-886`.
	///
	/// Legacy has three reasons, and this port can answer one of them:
	///
	///   * `getCurrentUI() == &soundEditor` **and the menu is on this very param** (`:876-882`) --
	///     ported, through `menu_scene()`'s focused node, since the menu *is* ported.
	///   * `getRootUI() == &performanceView` and that view is already showing the param
	///     (`:870-874`) -- **not ported and counted as uncovered**: `PerformanceScene` does not
	///     exist (migration spec §10.4 phase 6), so there is no view to ask.
	///   * `getCurrentUI() == &loadSongUI` (`:885`) -- **not ported and counted as uncovered**: the
	///     song browser is one of the seven load/save UIs `BrowserScene` does not yet carry (see
	///     `src/scenes/README.md`'s carry list), so the "is the song browser open" question has no
	///     answer here yet.
	///
	/// @param turn What the turn resolved to.
	/// @return True to skip the popup.
	bool popup_suppressed(const ModTurn& turn) const;

	/// @brief `View::potentiallyMakeItHarderToTurnKnob` (`view.cpp:1058-1083`).
	/// @param which_encoder 0 (lower) or 1 (upper).
	/// @param turn          What the turn resolved to.
	void potentially_make_it_harder_to_turn_knob(int32_t which_encoder, const ModTurn& turn);

	/// @brief `View::sendMidiFollowFeedback` (`view.cpp:1713-1731`).
	/// @param kind          The param's kind; ignored when @p have_param is false.
	/// @param param_id      The param id.
	/// @param knob_pos      The new knob position.
	/// @param have_param    Legacy's `modelStackWithParam && modelStackWithParam->autoParam`.
	/// @param is_automation Legacy's `isAutomation` (default false, `view.h:141`).
	void send_midi_follow_feedback(deluge::modulation::params::Kind kind, int32_t param_id, int32_t knob_pos,
	                               bool have_param, bool is_automation);

	/// @brief The tail both `set_target_*` entry points share -- `view.cpp:1885-1898` and
	///        `:1908-1921`, minus the LED/indicator refresh (task 3) and the VU-meter
	///        invalidation (not ported; see the class doc).
	/// @param send_midi_feedback Whether to re-send the learned params' CCs.
	void target_changed(bool send_midi_feedback);

	overlay::ParamPopupScene& popup_;
	deluge::ui::shell::SceneStack& stack_;
	deluge::ui::shell::InputLowering& lowering_;

	ModTarget target_{};

	/// The last clock `tick()` saw, for the model-driven entry points that have none of their own.
	uint32_t now_ms_ = 0;

	/// `indicator_leds::blinkKnobIndicator`'s state, per knob (`view.cpp:1074`, `:1080`).
	bool blink_indicator_[kKnobCount]{};
	bool blink_indicator_bipolar_[kKnobCount]{};

	/// `TimerName::DISPLAY_AUTOMATION`'s deadline, and its `pendingParamAutomationUpdatesModLevels`
	/// payload (`view.cpp:1696-1705`).
	std::optional<uint32_t> automation_display_ms_;
	bool automation_updates_mod_levels_ = false;
	/// `TimerName::SEND_MIDI_FEEDBACK_FOR_AUTOMATION`'s deadline (`view.cpp:1707-1708`).
	std::optional<uint32_t> automation_feedback_ms_;

	/// Both of `notifyParamAutomationOccurred`'s timers are 25 ms (`view.cpp:1698`, `:1708`).
	static constexpr uint32_t kAutomationTimerMs = 25;
};

// (No singleton accessor here, deliberately. Unlike `menu_scene()` / `param_popup_scene()`, this
// service cannot be default-constructed -- it borrows a `SceneStack` and an `InputLowering` that
// only exist once a `Shell` does -- so the application constructs it beside its `Shell` and passes
// it in `shell::Services::mod_knobs`. Nothing links `deluge_scenes` into an application yet
// (E4 preamble), so today that caller is the spec.)

} // namespace deluge::scenes::services
