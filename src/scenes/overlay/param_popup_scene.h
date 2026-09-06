#pragma once

/// @file
/// @brief `ParamPopupScene` -- the mod-encoder value popup: a one-line "name: value" strip pinned
///        to the top of the OLED whenever a gold knob (or `ModKnobs`' other callers) turns a
///        parameter.
///
/// The port of `View::displayModEncoderValuePopup` (`gui/views/view.cpp:1084-1288`),
/// `View::flushPendingModEncoderValuePopup` (`:1290-1301`) and `View::calculateKnobPosForDisplay`
/// (`:1302-1325`). `ModKnobs` (E4 task 2) is the caller this scene exists for -- it does not exist
/// yet, so this scene is driven directly by its own `show()` in every spec here, exactly the way a
/// future `ModKnobs::mod_encoder_action` will drive it.
///
/// @par The context `show()` carries, and why
///
/// Legacy's function reads `view.activeModControllableModelStack.modControllable` (a
/// `ModControllableAudio*`) and the current clip/output for three of its arms. E4 task 1 shipped
/// `show()` with legacy's five value-conversion parameters only, which left those three arms
/// permanently unreachable; task 2 -- `services::ModKnobs`, the first real caller, and the class
/// that *has* that context -- widened the signature rather than leave them uncovered, on the
/// reasoning that this is cheap with one caller and expensive with three. The extra argument is
/// `PopupContext`, defaulted so a caller with no context still gets exactly task 1's behaviour:
///
///   * `PopupContext::mod` reaches `getPatchedParamShortName`/`getParamDisplayName`'s
///     two-argument overloads (the LPF/HPF-morph-aware name) and `isParamQuantizedStutter` (the
///     "4ths"/"8ths"/... rate-bucket text). All three null-check it and fall back, so a default
///     `PopupContext` is the old behaviour exactly.
///   * `PopupContext::midi_instrument` is legacy's `isClipContext() && getCurrentOutputType() ==
///     OutputType::MIDI_OUT` **and** its `(MIDIInstrument*)getCurrentOutput()` in one pointer: the
///     caller resolves the two-part test and hands over the instrument, or null. It selects the
///     MIDI-clip naming arm (Pitch Bend / Channel Pressure / Mod Wheel for `Kind::EXPRESSION`,
///     else the CC's own name or `"CC <n>"`).
///   * `PopupContext::stuttering` is legacy's `!isUIModeActive(UI_MODE_STUTTERING)`, negated to a
///     positive. **No caller can set it true yet** -- nothing in the port owns the stutter gesture
///     -- so the rate-bucket text is currently always shown when the param qualifies, which is
///     what legacy does whenever a stutter is *not* in progress. Recorded here rather than
///     silently assumed.
///
/// @par What is deliberately not ported
///
///   * **7SEG.** The brief for this task describes a "7SEG/OLED split" in the legacy function.
///     There is none: `Display::displayNotification` (`hid/display/display.h:72`) is pure virtual
///     with exactly one override, `OLED::displayNotification` (`hid/display/oled.cpp:744-794`) --
///     7SEG support was removed from legacy itself before this task (there is a
///     `seven_segment_tombstone.cpp` in `hid/display/` and nothing else there answers to the
///     name), so `displayModEncoderValuePopup` has no 7SEG arm to drop. This is an error in the
///     task brief, not a decision made here.
///
/// @par The arbitration ("juggling ball") system
///
/// `displayModEncoderValuePopup` is not just a formatter -- it is called at high frequency (once
/// per encoder tick, potentially from two gold knobs) and arbitrates which of possibly several
/// parameters currently "owns" the one-line display, throttles how often it actually redraws, and
/// queues a deferred flush when a genuine change arrives inside the throttle window. All of that
/// state is `View`'s own function-local statics, which persist for the process's life -- exactly
/// what this scene's members are for, since it too is a singleton. Ported in full:
///
///   * `show()` is `displayModEncoderValuePopup` -- formats the name/value text, then runs the
///     three-way arbitration (immediate update / deferred-pending / refresh-only-no-redraw).
///   * `tick()`'s pending branch is `flushPendingModEncoderValuePopup` -- when the deferred flush's
///     own countdown reaches zero, it replays the cached arguments through the same arbitration
///     core `show()` uses, exactly as the legacy timer callback replays them through
///     `displayModEncoderValuePopup` itself.
///   * `calculateKnobPosForDisplay` is `format_value`'s plain-integer arm, transcribed unchanged
///     (`isParamPan`/`isParamPitch`, no context needed).
///
/// Legacy's timing constants are `AudioEngine::audioSampleTimer` units (`view.h:180-182`); this
/// port uses the millisecond equivalents (`kSampleRate` divided out): `MIN_DISPLAY_OWNERSHIP_TIME`
/// = 1000ms, `DISPLAY_TIMEOUT` = 250ms, `MIN_UPDATE_INTERVAL` ~= 45ms.
///
/// @par Timing: a deadline, not a wall clock
///
/// `Scene::tick(dt_ms)` receives no absolute time and `Scene::next_timer()` takes no arguments, so
/// nothing here can read "now" outside of a `show()` call (which does receive `now_ms`, exactly as
/// `UiContext::now_ms` reaches `handle()`). Two independent deadlines are tracked, both as
/// "milliseconds remaining", counted down by `tick(dt_ms)`:
///
///   * `remaining_ms_` -- the shown popup's lifetime (legacy's `TimerName::DISPLAY`, reset to
///     2000ms by both an actual redraw and a same-parameter refresh). Zero means nothing is
///     showing. `next_timer()` reports it so the shell wakes the OLED surface exactly once more,
///     at expiry; `render_oled` then draws nothing and `after_render()` files
///     `Action::Kind::Remove` (never `Pop`, which would also take down whatever a caller has
///     legitimately stacked above a passive, `input_transparent`-shaped overlay like this one --
///     see the warning on `Action` in `shell/scene.h`).
///   * `pending_flush_remaining_ms_` -- armed only while a deferred update is queued; when it
///     reaches zero inside `tick()`, the cached arguments are replayed through the arbitration
///     core using the deadline's own target time as "now" (exact by construction: that is the
///     instant the flush was scheduled to fire).
///
/// @warning **Must not raise `Action::Kind::LockInput`**, and does not -- see the warning on
///          `Action` in `shell/scene.h`. This scene has no gate that would ever want it: `handle()`
///          is unreachable (`input_transparent()` is true) and nothing here blocks on the card.

#include "shell/scene.h"

#include "definitions_cxx.hpp"
#include "modulation/params/param.h"

#include <cstdint>
#include <optional>
#include <string_view>

class MIDIInstrument;
class ModControllableAudio;

namespace deluge::scenes::overlay {

/// @brief The `SceneKind` this scene is registered under: `FirstApp + 6` (`registry.h`).
inline constexpr deluge::ui::shell::SceneKind kParamPopupSceneKind =
    static_cast<deluge::ui::shell::SceneKind>(static_cast<uint8_t>(deluge::ui::shell::SceneKind::FirstApp) + 6);

/// @brief The model context legacy's popup reads off `View` and `getCurrentOutput()`, as three
///        plain values a caller supplies.
///
/// Default-constructed, this is exactly "no context": every arm it unlocks null-checks its own
/// field and falls back to the context-free path, which is what E4 task 1 shipped. See the class
/// doc above for the per-field mapping and its citations.
struct PopupContext {
	/// `view.activeModControllableModelStack.modControllable`, cast to `ModControllableAudio*`
	/// exactly as legacy casts it (`view.cpp:1119`, `:1149-1150`, `:1160-1161`).
	ModControllableAudio* mod = nullptr;
	/// Non-null iff legacy's `isClipContext() && getCurrentOutputType() == OutputType::MIDI_OUT`
	/// (`view.cpp:1122`), in which case it is `(MIDIInstrument*)getCurrentOutput()` (`:1123`).
	MIDIInstrument* midi_instrument = nullptr;
	/// `isUIModeActive(UI_MODE_STUTTERING)` (`view.cpp:1161`). Nothing in the port owns the stutter
	/// gesture yet, so no caller sets this true; see the class doc.
	bool stuttering = false;
};

/// @brief The mod-encoder value popup. One instance for the whole application (`registry.h`).
class ParamPopupScene final : public deluge::ui::shell::Scene {
public:
	/// @brief `View::displayModEncoderValuePopup` (`view.cpp:1084-1288`): format @p kind/@p
	///        param_id/@p knob_pos/@p source1/@p source2 and run the display-ownership
	///        arbitration, updating the shown text (and re-arming the popup's lifetime) only when
	///        the arbitration says this call may.
	/// @param kind     The param's kind; `Kind::PATCH_CABLE` selects the "src->[src->]name" form.
	/// @param param_id The param id within @p kind.
	/// @param knob_pos The raw knob position (legacy's pre-`kKnobPosOffset` `newKnobPos`).
	/// @param source1  The first patch source, for `Kind::PATCH_CABLE`.
	/// @param source2  The second patch source (a double patch cable), or `PatchSource::NONE`.
	/// @param now_ms   The tick's wall clock, exactly as `UiContext::now_ms` reaches `handle()`.
	/// @param context  The model context legacy reads off `View` and the current output; see
	///                 `PopupContext` and the class doc. Defaulted to "none", which reproduces the
	///                 context-free naming and value paths exactly.
	void show(deluge::modulation::params::Kind kind, int32_t param_id, int32_t knob_pos, PatchSource source1,
	          PatchSource source2, uint32_t now_ms, PopupContext context = {});

	deluge::ui::shell::SceneKind kind() const override { return kParamPopupSceneKind; }
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::oled::OledFrame& frame) override;
	/// @brief The popup routes nothing: legacy never calls `buttonAction`/`padAction` on the popup
	///        image, it is drawn straight into the OLED image on the way out
	///        (`OLED::sendMainImage`).
	/// @return Always `Result::unhandled()`.
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	void tick(uint32_t dt_ms) override;
	/// @brief Files `Action::Kind::Remove` the tick the popup's own lifetime reaches zero; see the
	///        class doc's "a deadline, not a wall clock" note.
	deluge::ui::shell::Result after_render() override;
	std::optional<uint32_t> next_timer() const override;
	/// @return `{oled=true, grid=false}` -- this scene paints the OLED only; arming the grid for a
	///         surface it never touches is a permanent blit every tick until something else covers
	///         for it (shell spec §15.1, §15.3).
	deluge::ui::shell::Scene::RepaintSurfaces timer_repaints() const override { return {.oled = true, .grid = false}; }

	/// @brief True: the scene beneath keeps painting the `Main` surface around this popup's small
	///        top strip (spec E §6). This popup covers eleven rows, not the whole panel.
	bool oled_transparent(deluge::ui::shell::SurfaceRole role) const override {
		return role == deluge::ui::shell::SurfaceRole::Main;
	}
	/// @brief True: must not be mistaken for the grid base, even though it paints no grid pixels.
	bool is_grid_overlay() const override { return true; }
	/// @brief False: this popup is OLED-only, exactly as the shell-owned `PopupScene`/`ConsoleScene`
	///        are (`shell/scene.h`'s `dims_grid` note) -- it must not halve the brightness of a grid
	///        it never touches.
	bool dims_grid() const override { return false; }
	/// @brief False: this popup never takes input, so the scene beneath keeps its own input focus.
	bool input_transparent() const override { return true; }

	/// @name Observable state
	/// For specs.
	/// @{
	/// @return Milliseconds left before the shown popup expires; 0 if nothing is showing.
	uint32_t remaining_ms() const { return remaining_ms_; }
	/// @return The text last actually shown (post-arbitration), for specs.
	std::string_view shown_name() const { return {name_, name_len_}; }
	/// @return ditto, the value half.
	std::string_view shown_value() const { return {value_, value_len_}; }
	/// @return True while a deferred (throttled) update is queued for a later flush.
	bool has_pending() const { return has_pending_; }
	/// @}

private:
	/// @brief The arbitration core: `displayModEncoderValuePopup`'s "Check if we need to update the
	///        notification" block (`view.cpp:1197-1287`), shared by `show()` and the flush path in
	///        `tick()`.
	void attempt_display(deluge::modulation::params::Kind kind, int32_t param_id, int32_t knob_pos, PatchSource source1,
	                     PatchSource source2, uint32_t now_ms, PopupContext context);

	static constexpr size_t kTextCap = 40; // `etl::string<40>` (view.cpp:1107-1108).
	char name_[kTextCap + 1] = {};
	size_t name_len_ = 0;
	char value_[kTextCap + 1] = {};
	size_t value_len_ = 0;

	uint32_t remaining_ms_ = 0; // 0: nothing showing. The popup's own lifetime countdown.
	bool was_showing_ = false;  // Set true whenever remaining_ms_ becomes nonzero; after_render's
	                            // "just expired" edge.

	// `last_param_*`/`last_display_value_`: legacy's change-detection statics (view.cpp:1088-1092).
	deluge::modulation::params::Kind last_param_kind_ = deluge::modulation::params::Kind::NONE;
	int32_t last_param_id_ = -1;
	int32_t last_display_value_ = INT32_MIN;
	PatchSource last_source1_ = PatchSource::NONE;
	PatchSource last_source2_ = PatchSource::NONE;

	// `display_owner_*`/`display_ownership_start_time`/`last_display_update_time`/
	// `last_actual_display_time`: the arbitration ("juggling ball") statics (view.cpp:1095-1101).
	deluge::modulation::params::Kind display_owner_kind_ = deluge::modulation::params::Kind::NONE;
	int32_t display_owner_param_id_ = -1;
	PatchSource display_owner_source1_ = PatchSource::NONE;
	PatchSource display_owner_source2_ = PatchSource::NONE;
	uint32_t display_ownership_start_ms_ = 0;
	uint32_t last_display_update_ms_ = 0;
	uint32_t last_actual_display_ms_ = 0;

	// `hasPendingModEncoderValuePopup`/`pendingPopup*` (view.h:184-189).
	bool has_pending_ = false;
	deluge::modulation::params::Kind pending_kind_ = deluge::modulation::params::Kind::NONE;
	int32_t pending_param_id_ = 0;
	int32_t pending_knob_pos_ = 0;
	PatchSource pending_source1_ = PatchSource::NONE;
	PatchSource pending_source2_ = PatchSource::NONE;
	/// Cached alongside the rest of the deferred call: legacy's pending replay re-reads the same
	/// `View` state the original call did, so the port must replay the same context too.
	PopupContext pending_context_{};
	uint32_t pending_flush_remaining_ms_ = 0;
};

/// @brief The application's one mod-encoder value popup.
/// @return The singleton.
ParamPopupScene& param_popup_scene();

} // namespace deluge::scenes::overlay
