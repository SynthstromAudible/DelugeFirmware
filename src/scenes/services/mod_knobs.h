#pragma once

/// @file
/// @brief `ModKnobs` -- the gold-knob half of `View`, as a `shell::Service`.
///
/// The port of `View::modEncoderAction` (`gui/views/view.cpp:835-859`) and what it reaches: the turn, the push
/// (`modEncoderButtonAction`), the mod buttons (`modButtonAction`), the indicators and mod LEDs
/// (`setKnobIndicatorLevels`, `setModLedStates`), the centre detent (`potentiallyMakeItHarderToTurnKnob`), the mod
/// region (`setModRegion`), the automation notification and the MIDI-follow feedback.
///
/// @par What the knobs point at, and how they read and write
///
/// The knobs point at a `bind::KnobFocus`: addresses into the song's records, and the view's knob state (the mod
/// region, the song compressor's view). What a knob moves is `bind::knob_assignment` over the records; a value is the
/// `RuntimeFacts::param_value` fact, or the record's automation at a held region; the position is `bind/knob_math`'s.
/// A turn is a `WriteSink::write_param` request (the Edit makes what the knob turns and writes a take being
/// recorded); a push and a mod button are `services/knob_push`'s pure tables, sent to the sink. The service holds no
/// engine object.
///
/// @par Why a service in `src/scenes/` and not in `src/shell/`
///
/// `src/shell/**` may not include `model/**` (`shell-includes-outside`), and this class is about the song's
/// records, so it implements the shell's interface from the scenes library, as `GlobalControls` and `MidiLearn` do.
///
/// @par What the router sends here
///
/// A mod button, a gold-knob turn and a gold-knob push, each only after the top scene has declined it
/// (`shell/router.h`'s `route()`): `View` is what `UI`'s base-class hooks forward to (`gui/ui/ui.cpp:35-45`).
///
/// @par Acceleration
///
/// The turn's acceleration is `InputLowering::calc_next_knob_speed`, legacy's curve per encoder role (legacy shares
/// one ramp between the two knobs, and its clock is sub-millisecond; the port's ramps are per knob, on whole ms).
///
/// @par Not this service's
///
/// The three LED groups above `setModLedStates`' "Sort out actual mod LEDs" block (affect-entire, clip view,
/// session view) and the VU-meter sidebar are the root views' questions, not the knobs'.
///
/// @warning **No wall clock.** `Service::tick(now_ms, dt_ms)` is the only place a clock arrives; the `set_*` entry
///          points backdate against the last `now_ms` the service was ticked with (`pretend_knobs_untouched()`).

#include "engines/effects/state.h"
#include "scenes/bind/knob_focus.h"
#include "scenes/overlay/param_popup_scene.h"
#include "scenes/services/mod_target.h"

#include "ui/core/knob_indicator.h"
#include "ui/core/led_frame.h"
#include "ui/core/metrics.h"

#include "shell/scene_stack.h"
#include "shell/service.h"

#include <cstdint>
#include <optional>

class Song;

namespace deluge::ui::shell {
class InputLowering;
}

namespace deluge::project {
struct ParamAddress;
}

namespace deluge::scenes::services {

/// @brief The two gold knobs: what they point at, what turning, pushing and their mod buttons do, and how fast.
class ModKnobs final : public deluge::ui::shell::Service {
public:
	/// @brief How many gold knobs there are (`mod0`/`mod1`, `hid/encoders.h:81-82`).
	static constexpr int32_t kKnobCount = 2;

	/// @param popup    The value popup this service shows through.
	/// @param stack    The scene stack it pushes that popup onto.
	/// @param lowering The input lowering it reads acceleration from and backdates the "last turned" clock on.
	ModKnobs(overlay::ParamPopupScene& popup, deluge::ui::shell::SceneStack& stack,
	         deluge::ui::shell::InputLowering& lowering)
	    : popup_(popup), stack_(stack), lowering_(lowering) {}

	/// @name What the knobs point at
	/// @{

	/// @brief The song whose records the knobs read, which `scenes-currentSong` forbids this layer to fetch.
	/// @param song The current song, or null before one exists.
	void set_song(Song* song) { song_ = song; }

	/// @brief Point the knobs at @p focus -- `View::setActiveModControllableTimelineCounter` and
	///        `setActiveModControllableWithoutTimelineCounter` (`view.cpp:1884-1939`). The mod region is kept.
	/// @param focus              What the knobs point at.
	/// @param send_midi_feedback Re-send the learned params' CCs, whose values may have changed.
	void set_focus(const bind::KnobFocus& focus, bool send_midi_feedback = true);

	/// @brief Set the region automation is read and written at -- `View::setModRegion` (`view.cpp:1940-1967`). A held
	///        region on a stopped clip grabs its params' values from @p pos.
	/// @param pos    The region's start; `0xFFFFFFFF` (legacy's default) is "at the playhead".
	/// @param length The region's length; 0 for none.
	void set_mod_region(uint32_t pos = 0xFFFFFFFF, uint32_t length = 0);

	/// @brief The mod button the knobs follow -- `View::getModKnobMode` (`view.cpp:1697-1706`).
	/// @return The mode, or -1 with no focus (or a CV track's, which has no modes).
	int32_t mode() const;

	/// @return What the knobs point at.
	const bind::KnobFocus& focus() const { return focus_; }

	/// @}

	/// @name The view context this service cannot read for itself
	/// @{

	/// @brief The root-UI questions `View::modButtonAction`, `setKnobIndicatorLevels` and `setModLedStates` ask,
	///        told by whoever knows: `src/scenes` may not include `gui/`. The defaults are a plain root view's.
	struct ViewState {
		/// The automation editor is up (`view.cpp:1518`, `:1402`, `:1687`): mod buttons are ignored, the indicators
		/// are left as they are, and every mod LED is off.
		bool in_automation_editor = false;
		/// The automation editor is on the arranger (`view.cpp:1520`): mod button 0 still toggles the VU meter.
		bool automation_on_arranger = false;
		/// The root is performance view (`view.cpp:1529`): a mod button is taken whatever UI mode is active, and it
		/// leaves a stutter playing.
		bool root_is_performance = false;
		/// The root is a clip minder (`gui/ui/ui.cpp:157-160`): the VU meter is not offered. Not the same question as
		/// `is_clip_context`: in session view with a clip pressed the knobs are on the clip and the root is not one.
		bool root_is_clip_minder = false;
		/// `isUIModeWithinRange(modButtonUIModes)` (`view.cpp:1504-1512`): true with no UI mode active.
		bool mod_button_ui_mode_in_range = true;
	};

	/// @brief Tell this service what the root view is.
	void set_view_state(const ViewState& state) { view_ = state; }
	/// @brief What was last passed to `set_view_state`.
	const ViewState& view_state() const { return view_; }

	/// @}

	/// @name Gestures and their state
	/// @{

	/// @brief One mod button pressed or released -- `View::modButtonAction` (`view.cpp:1514-1563`): the knobs' mode,
	///        the VU-meter toggle, the end of a stutter, and the button's popup.
	/// @param which_button 0..7.
	/// @param on           Press or release.
	void mod_button_action(int32_t which_button, bool on);

	/// @brief Is the VU meter shown? (`View::knobFocus().vu_meter`), which pressing the selected mod button 0 outside a
	/// clip
	///        minder toggles; its one effect here is the blinking volume mod LED.
	bool display_vu_meter() const { return display_vu_meter_; }

	/// @brief Backdate both knobs' "last turned" clocks -- `View::pretendModKnobsUntouchedForAWhile`.
	void pretend_knobs_untouched();

	/// @brief A param was automated -- `View::notifyParamAutomationOccurred` (`view.cpp:1708-1729`): arms the
	///        automation display and the MIDI-follow feedback deadlines when it is the knobs' owner's.
	/// @param owner             The automated param's owner and clip, as an address.
	/// @param update_mod_levels Legacy's `updateModLevels`.
	void notify_param_automation_occurred(const project::ParamAddress& owner, bool update_mod_levels = true);

	/// @}

	/// @name `shell::Service`
	/// @{
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	/// @brief Declare the eight mod LEDs and the two gold-knob indicator columns, from the focus every tick.
	void leds(deluge::ui::LedFrame& frame) override;
	void tick(uint32_t now_ms, uint32_t dt_ms) override;
	std::optional<uint32_t> next_timer() const override;
	/// @}

	/// @name Observable state
	/// @{
	/// @brief Should knob @p which_encoder's indicator be blinking? (`blinkKnobIndicator` / `stop...`).
	bool blink_indicator(int32_t which_encoder) const;
	/// @brief Was the last blink decision for a bipolar param?
	bool blink_indicator_bipolar(int32_t which_encoder) const;
	/// @brief Is a `DISPLAY_AUTOMATION` refresh owed?
	bool automation_display_pending() const { return automation_display_ms_.has_value(); }
	/// @brief Would that refresh also re-level the mod knobs? (`pendingParamAutomationUpdatesModLevels`).
	bool automation_updates_mod_levels() const { return automation_updates_mod_levels_; }
	/// @}

	/// @brief One knob's indicator level -- the arithmetic half of `View::setKnobIndicatorLevel`.
	struct IndicatorLevel {
		/// False where legacy reads an uninitialised position (see `ModIndicatorInfo`).
		bool declared = false;
		/// The level legacy hands `indicator_leds::setKnobIndicatorLevel`, truncated where legacy truncates it.
		uint8_t level = 0;
		bool bipolar = false;
	};
	/// @param info       What the knob reads.
	/// @param stuttering A stutter is playing.
	static IndicatorLevel indicator_level(const ModIndicatorInfo& info, bool stuttering);

private:
	/// @brief One gold-knob turn -- `View::modEncoderAction`: the turn, then the instrument marked edited on a clip.
	deluge::ui::shell::Result mod_encoder_action(int32_t which_encoder, int32_t offset, uint32_t now_ms);
	/// @brief The turn itself: the comp arm's setting, or the param's write, popup, feedback and detent.
	deluge::ui::shell::Result turn_knob(int32_t which_encoder, int32_t offset, uint32_t now_ms);
	/// @brief Hand @p turn to the popup and push it if it is not stacked yet.
	/// @return True when the popup redrew, so the caller owes a `Repaint`.
	bool show_popup(const ModTurn& turn, uint32_t now_ms);
	/// @brief Is the menu on top focused on this very param (`view.cpp:876-882`)? The performance-view and
	///        song-browser suppressions have no scene to ask yet.
	bool popup_suppressed(const ModTurn& turn) const;
	/// @brief `View::potentiallyMakeItHarderToTurnKnob`: blink a centred knob and make it harder to turn away.
	void potentially_make_it_harder_to_turn_knob(int32_t which_encoder, const ModTurn& turn);
	/// @brief One gold-knob push -- `View::modEncoderButtonAction`.
	/// @param mode_held The knobs' mode's mod button is held, which re-shows its popup.
	deluge::ui::shell::Result mod_encoder_button_action(int32_t which_encoder, bool on, bool shift, bool learn,
	                                                    bool mode_held);
	/// @brief What knob @p which_encoder's indicator reads.
	ModIndicatorInfo indicator_info(int32_t which_encoder) const;
	/// @brief Declare both knob indicator columns -- `View::setKnobIndicatorLevels`.
	void declare_indicators(deluge::ui::LedFrame& frame);
	/// @brief Is knob @p which_encoder inside its blink's flash? Each knob keeps its own phase.
	bool indicator_blinking(int32_t which_encoder) const;
	/// @brief `View::sendMidiFollowFeedback` (`view.cpp:1730-1748`).
	void send_midi_follow_feedback(deluge::modulation::params::Kind kind, int32_t param_id, int32_t knob_pos,
	                               bool have_param, bool is_automation);
	/// @brief Whether the app's facts and sink act on the song the knobs read.
	bool bound() const;
	/// @brief The focus's owner's effect settings: a sound's own, or its chain's; null for none.
	const effects::EffectStates* owner_effects() const;
	/// @brief A stutter-rate knob on the focus's owner is quantized.
	bool owner_stutter_quantized() const;

	overlay::ParamPopupScene& popup_;
	deluge::ui::shell::SceneStack& stack_;
	deluge::ui::shell::InputLowering& lowering_;
	Song* song_ = nullptr;
	bind::KnobFocus focus_{};
	/// A MIDI track's knob (@ref selecting_knob_) is choosing its CC, which the next mod button ends.
	bool selecting_cc_ = false;
	int32_t selecting_knob_ = 0;
	/// The last clock `tick()` saw.
	uint32_t now_ms_ = 0;
	bool blink_indicator_[kKnobCount]{};
	bool blink_indicator_bipolar_[kKnobCount]{};
	uint32_t blink_indicator_start_ms_[kKnobCount]{};
	bool display_vu_meter_ = false;
	ViewState view_{};
	std::optional<uint32_t> automation_display_ms_;
	bool automation_updates_mod_levels_ = false;
	std::optional<uint32_t> automation_feedback_ms_;
	static constexpr uint32_t kAutomationTimerMs = 25;
};

} // namespace deluge::scenes::services
