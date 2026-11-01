#pragma once

/// @file
/// @brief `ParamPopupScene` -- the mod-encoder value popup: a one-line "name: value" strip pinned
///        to the top of the OLED whenever a gold knob (or `ModKnobs`' other callers) turns a
///        parameter.
///
/// The port of `View::displayModEncoderValuePopup` (`gui/views/view.cpp:1101-1305`),
/// `View::flushPendingModEncoderValuePopup` (`:1290-1301`) and `View::calculateKnobPosForDisplay`
/// (`:1302-1325`). `ModKnobs` shows it on a gold-knob turn.
///
/// @par The context `show()` carries
///
/// Three of legacy's arms read the knob owner: the morph params' filter-aware names, the stutter rate's bucket
/// text, and a MIDI clip's CC names. `PopupContext` carries what they read, from the records and the runtime
/// facts; a default `PopupContext` gives the context-free text.
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
/// nothing here can read "now" outside of a `show()` or `hand_off()` call (which do receive `now_ms`, exactly as
/// `UiContext::now_ms` reaches `handle()`). Two independent deadlines are tracked, both as
/// "milliseconds remaining", counted down by `tick(dt_ms)`:
///
///   * `remaining_ms_` -- the shown popup's lifetime (legacy's `TimerName::DISPLAY`, reset to
///     2000ms by both an actual redraw and a same-parameter refresh). Zero means nothing is
///     showing. Each reset also records the clock time it runs out, which `hand_off()` reads against
///     its own `now_ms`: a hand-off between ticks gives what is truly left. `next_timer()` reports it so the shell
///     wakes the OLED surface exactly once more, at expiry; `render_oled` then draws nothing and `after_render()` files
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

#include "effects/state.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace deluge::scenes::overlay {

/// @brief The `SceneKind` this scene is registered under: `FirstApp + 6` (`registry.h`).
inline constexpr deluge::ui::shell::SceneKind kParamPopupSceneKind =
    static_cast<deluge::ui::shell::SceneKind>(static_cast<uint8_t>(deluge::ui::shell::SceneKind::FirstApp) + 6);

/// @brief What the popup reads beyond the param: the knob owner's records and the stutter's live state.
///
/// Default-constructed, this is "no context": each arm it unlocks falls back to the context-free text.
struct PopupContext {
	/// The knob owner's filters, whose modes name the morph params; none for no owner.
	std::optional<deluge::effects::FilterState> filter;
	/// The stutter rate the knob turns is quantized: the song's stutter's setting where the owner plays the song's.
	bool stutter_quantized = false;
	/// The knobs are on a MIDI clip, whose CCs and expression the popup names.
	bool midi_clip = false;
	/// The MIDI track's name for the knob's CC, from its record; empty for none.
	std::string cc_label;
	/// A stutter is playing, which shows the rate's value instead of its bucket.
	bool stuttering = false;
};

/// @brief What a param popup was showing when it was handed to another display, and for how much longer.
struct ShownPopup {
	/// The param name line, as drawn.
	std::string name;
	/// The value line, as drawn.
	std::string value;
	/// What is left of the popup's lifetime, at most `kPopupLifetimeMs`.
	uint32_t remaining_ms{0};
	/// The param shown, its value as the change check compares it, and its patch sources: what the receiving
	/// display needs to tell a later draw of the same param and value from a change.
	deluge::modulation::params::Kind kind{deluge::modulation::params::Kind::NONE};
	int32_t param_id{-1};
	int32_t display_value{0};
	PatchSource source1{PatchSource::NONE};
	PatchSource source2{PatchSource::NONE};
};

/// @brief The mod-encoder value popup. One instance for the whole application (`registry.h`).
class ParamPopupScene final : public deluge::ui::shell::Scene {
public:
	/// @brief `View::displayModEncoderValuePopup` (`view.cpp:1101-1305`): format @p kind/@p
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
	///
	/// @return **True when this call actually changed what is on the panel** -- i.e. the arbitration
	///         took its commit arm (`view.cpp:1260-1262`, whose body is
	///         `display->displayNotification(...)`, a write AND a blit). False on the throttled arm
	///         (which arms its own flush timer and therefore repaints itself), on the
	///         same-param-refresh arm, and on legacy's implicit fourth arm, all of which leave the
	///         pixels alone.
	///
	///         **A caller must file `Action::Kind::Repaint` on `SurfaceRole::Main` when this is
	///         true, and `services::ModKnobs` does** (`services/mod_knobs.cpp`'s `show_popup`).
	///         This scene has no `Result` of its own to file one in -- `show()` is not a
	///         `Scene::handle` -- and its `next_timer()` while showing is the popup's whole
	///         `kPopupLifetimeMs`, so without the caller's repaint the next OLED frame is the one
	///         that draws the popup *disappearing*. E4 task 13 found exactly that by looking at a
	///         golden: a second gold-knob detent moved this scene's text from "26" to "45" and the
	///         panel kept showing "26" until the popup expired.
	///
	/// @note A draw, here or in `tick()`'s deferred flush, raises a `CancelNotification` notice: legacy's popup is the
	///       display's notification, so it replaces whatever notification was up. A hand-off raises none.
	bool show(deluge::modulation::params::Kind kind, int32_t param_id, int32_t knob_pos, PatchSource source1,
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
	/// @brief True: this popup never takes input, so the scene beneath keeps its own input focus.
	bool input_transparent() const override { return true; }

	/// @name Observable state
	/// For specs.
	/// @{
	/// @return Milliseconds left before the shown popup expires; 0 if nothing is showing.
	uint32_t remaining_ms() const { return remaining_ms_; }
	/// @brief Give up what is showing to another display: a deferred update is flushed first, as its timer would
	///        have shown it, and then nothing shows here.
	/// @param now_ms The clock at the hand-off: the lifetime left is read off it, ticked or not.
	/// @return What shows and the lifetime left; none when nothing shows or the lifetime has run out by @p now_ms.
	std::optional<ShownPopup> hand_off(uint32_t now_ms);
	/// @return The text last actually shown (post-arbitration), for specs.
	std::string_view shown_name() const { return {name_, name_len_}; }
	/// @return ditto, the value half.
	std::string_view shown_value() const { return {value_, value_len_}; }
	/// @return True while a deferred (throttled) update is queued for a later flush.
	bool has_pending() const { return has_pending_; }
	/// @}

private:
	/// @brief The arbitration core: `displayModEncoderValuePopup`'s "Check if we need to update the
	///        notification" block (`view.cpp:1214-1304`), shared by `show()` and the flush path in
	///        `tick()`.
	/// @return True when the commit arm ran; see `show()`.
	bool attempt_display(deluge::modulation::params::Kind kind, int32_t param_id, int32_t knob_pos, PatchSource source1,
	                     PatchSource source2, uint32_t now_ms, PopupContext context);

	static constexpr size_t kTextCap = 40; // `etl::string<40>` (view.cpp:1124-1125).
	char name_[kTextCap + 1] = {};
	size_t name_len_ = 0;
	char value_[kTextCap + 1] = {};
	size_t value_len_ = 0;

	uint32_t remaining_ms_ = 0;  // 0: nothing showing. The popup's own lifetime countdown.
	uint32_t expires_at_ms_ = 0; // The clock time the lifetime runs out, for a hand-off between ticks.
	bool was_showing_ = false;   // Set true whenever remaining_ms_ becomes nonzero; after_render's
	                             // "just expired" edge.

	// `last_param_*`/`last_display_value_`: legacy's change-detection statics (view.cpp:1105-1109).
	deluge::modulation::params::Kind last_param_kind_ = deluge::modulation::params::Kind::NONE;
	int32_t last_param_id_ = -1;
	int32_t last_display_value_ = INT32_MIN;
	PatchSource last_source1_ = PatchSource::NONE;
	PatchSource last_source2_ = PatchSource::NONE;

	// `display_owner_*`/`display_ownership_start_time`/`last_display_update_time`/
	// `last_actual_display_time`: the arbitration ("juggling ball") statics (view.cpp:1112-1118).
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
