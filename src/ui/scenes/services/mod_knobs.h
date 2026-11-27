#pragma once

/// @file
/// @brief `ModKnobs` -- the gold knobs, as a `shell::Service`.
///
/// The one implementation of the gold knobs, for the guest's menus and the root views alike, each shown through its own
/// `KnobPresentation`: the turn, the push, the mod buttons, the indicators and mod LEDs, the centre blink, the mod
/// region, the automation notification and the MIDI-follow feedback. Recorded from `View::modEncoderAction`
/// (`origin/main:src/deluge/gui/views/view.cpp`), `modEncoderButtonAction`, `modButtonAction`,
/// `setKnobIndicatorLevels`, `setModLedStates`, `potentiallyMakeItHarderToTurnKnob` and `setModRegion`; no engine
/// object keeps a gold-knob path of its own.
///
/// @par What the knobs point at, and how they read and write
///
/// The knobs point at a `bind::KnobFocus`: addresses into the song's records, and the view's knob state (the mod
/// region, the song compressor's view). What a knob moves is `bind::knob_assignment` over the records; a value is the
/// `controls::Facts::param_value` fact, or the record's automation at a held region; the position is
/// `ui/controls/knob_math`'s. A turn is a `controls::Sink::write_param` request (the Edit makes what the knob turns and
/// writes a take being recorded); a push and a mod button resolve through the controls library
/// (`controls::resolve_press`, `controls::resolve_section`) into `services/knob_push`'s requests, sent to the sink. The
/// service holds no engine object.
///
/// @par Its ports
///
/// The host installs its port set (`set_ports`) and the song's record (`set_model`). The service reads the knobs'
/// facts (`controls::Facts`), the owners' effects (`bind::TrackFacts`, `bind::SoundFacts`), whether either clock runs
/// (`bind::Transport`) and the follow feedback's automation row (`bind::DeviceSettings`). It writes through
/// `controls::Sink` and `bind::WriteSink`, and sends MIDI follow's feedback through `bind::MidiFeedback`. With no set
/// installed the knobs are unbound: a turn, a push and a mod button do nothing, and nothing is sent. With a set that
/// lacks a port, each read of it answers its default and each write or send through it is skipped.
///
/// @par Why a service in `src/ui/scenes/` and not in `src/ui/shell/`
///
/// `shell::Service` is a shell interface, but `src/ui/shell/**` may not include `ui/scenes/**` or `project/**`
/// (`scripts/layers.toml`'s `shell-includes-outside` rule). The knobs read the song's record (`project::Song`)
/// and act through the scenes' ports (`ui/scenes/bind/`). So the shell's interface is implemented from inside the
/// scenes library, which may see both, as `GlobalControls` and `MidiLearn` do.
///
/// @par Who sends gestures here
///
/// With a menu up, the router: a mod button, a gold-knob turn and a gold-knob push, each only after the top scene has
/// declined it (`ui/shell/router.h`'s `route()`), shown through the shell's `GuestKnobPresentation`. With no menu, the
/// root views: `UI`'s base-class hooks (`UI::modEncoderAction`, `UI::modButtonAction`, `UI::modEncoderButtonAction`)
/// call `turn`, `section_button` and `push` through `gui/ui/root_knobs`, shown through the legacy display.
///
/// @par Acceleration
///
/// The turn's acceleration is `InputLowering::calc_next_knob_speed`: legacy's `EncoderAcceleration`, one ramp per gold
/// knob as legacy keeps one per `ContinuousEncoder` (`hid/encoders.h`). Legacy's clock is sub-millisecond; the
/// port's is whole milliseconds.
///
/// @par Not this service's
///
/// The three LED groups above `setModLedStates`' "Sort out actual mod LEDs" block (affect-entire, clip view,
/// session view) and the VU-meter sidebar are the root views' questions, not the knobs'.
///
/// @note **Not inert before it is bound.** `leds()` drives the eight mod LEDs every frame and, with no bound
///       owner, clears both gold knobs' indicator levels to 0 (`declare_indicators`). Installing the service takes
///       those LEDs from legacy whether or not a `ModTarget` is set.
///
/// @warning **No wall clock.** `Service::tick(now_ms, dt_ms)` is the only place a clock arrives; the `set_*` entry
///          points use the last `now_ms` the service was ticked with.

#include "base/panic.h"
#include "effects/state.h"
#include "ui/controls/board.h"
#include "ui/scenes/bind/knob_focus.h"
#include "ui/scenes/overlay/param_popup_scene.h"
#include "ui/scenes/services/guest_knob_presentation.h"
#include "ui/scenes/services/knob_presentation.h"
#include "ui/scenes/services/mod_target.h"

#include "ui/widgets/core/knob_indicator.h"
#include "ui/widgets/core/led_frame.h"
#include "ui/widgets/core/metrics.h"

#include "ui/shell/scene_stack.h"
#include "ui/shell/service.h"

#include <cstdint>
#include <functional>
#include <optional>

namespace deluge::ui::scenes::bind {
struct Ports;
}

namespace deluge::ui::shell {
class InputLowering;
}

namespace deluge::project {
struct ParamAddress;
class Song;
} // namespace deluge::project

namespace deluge::ui::scenes::services {

struct PushInputs;

/// @brief The two gold knobs: what they point at, what turning, pushing and their mod buttons do, and how fast.
class ModKnobs final : public deluge::ui::shell::Service {
public:
	/// @param popup    The value popup the shell's presentation shows through.
	/// @param stack    The scene stack it pushes that popup onto.
	/// @param lowering The input lowering it reads acceleration from and backdates the "last turned" clock on.
	/// @param board    The board the knobs and mod buttons are on: its map turns each physical control into the section
	///                 or slot it drives, and its counts bound them. At most `KnobPresentation::kKnobCount` slots.
	ModKnobs(const ModKnobs&) = delete;
	ModKnobs& operator=(const ModKnobs&) = delete;
	ModKnobs(overlay::ParamPopupScene& popup, deluge::ui::shell::SceneStack& stack,
	         deluge::ui::shell::InputLowering& lowering, const deluge::ui::controls::Board& board)
	    : stack_(stack), lowering_(lowering), board_(board),
	      guest_(popup, stack, now_ms_, view_.root_is_timeline_view) {
		if (board.slots > KnobPresentation::kKnobCount) {
			freezeWithError("E-board");
		}
	}

	/// @brief The board the knobs and mod buttons are on: a caller mapping a physical control for them maps it through
	///        this one.
	/// @return The board.
	[[nodiscard]] const deluge::ui::controls::Board& board() const { return board_; }

	/// @name What the knobs point at
	/// @{

	/// @brief The port set the knobs read, write and send through: the knobs' facts and sink, the track and sound
	///        facts, the write sink, the transport, the device's settings and MIDI follow's feedback. The set is not
	///        owned and must outlive the service's use of it.
	/// @param ports The set, or null for none: the knobs are unbound.
	void set_ports(const deluge::ui::scenes::bind::Ports* ports) { ports_ = ports; }
	/// @brief The record of the song the knobs resolve against: what each knob moves, its mode and the push's table.
	///        Not owned.
	/// @warning A stale record makes every knob resolve against the wrong song; update it whenever the song changes.
	/// @param model The song's record, or null before one exists: the knobs are unbound.
	void set_model(const deluge::project::Song* model) { model_ = model; }

	/// @brief Point the knobs at @p focus -- `View::setActiveModControllableTimelineCounter` and
	///        `setActiveModControllableWithoutTimelineCounter`. The mod region is kept.
	/// @param focus              What the knobs point at.
	/// @param send_midi_feedback Re-send the learned params' CCs, whose values may have changed.
	void set_focus(const bind::KnobFocus& focus, bool send_midi_feedback = true);

	/// @brief Set the region automation is read and written at -- `View::setModRegion`. A held
	///        region on a stopped clip grabs its params' values from @p pos.
	/// @param pos         The region's start; `0xFFFFFFFF` (legacy's default) is "at the playhead".
	/// @param length      The region's length; 0 for none.
	/// @param note_row_id The held note's row id.
	void set_mod_region(uint32_t pos = 0xFFFFFFFF, uint32_t length = 0, int32_t note_row_id = 0);

	/// @brief The mod button the knobs follow -- `View::getModKnobMode` (`origin/main:src/deluge/gui/views/view.cpp`).
	/// @return The mode, or -1 with no focus (or a CV track's, which has no modes).
	int32_t mode() const;

	/// @return What the knobs point at.
	const bind::KnobFocus& focus() const { return *focus_; }

	/// @brief Keep the focus in @p store, which the legacy views keep too, rather than in this service's own.
	/// @param store   The one focus store; it outlives this service.
	/// @param changed Called after this service writes the store's owner or region, so its other keeper follows.
	void use_focus(bind::KnobFocus& store, std::function<void()> changed = {}) {
		focus_ = &store;
		focus_changed_ = std::move(changed);
	}

	/// @}

	/// @name The view context this service cannot read for itself
	/// @{

	/// @brief The root-UI questions `View::modButtonAction` (`origin/main:src/deluge/gui/views/view.cpp`),
	///        `setKnobIndicatorLevels` and `setModLedStates` ask, told by whoever knows: `src/ui/scenes` may not
	///        include `gui/`. The defaults are a plain root view's.
	struct ViewState {
		/// The automation editor is up (`View::modButtonAction`, `setKnobIndicatorLevels`, `setModLedStates`): mod
		/// buttons are ignored, the indicators are left as they are, and every mod LED is off.
		bool in_automation_editor = false;
		/// The automation editor is on the arranger (`View::modButtonAction`): mod button 0 still toggles the VU meter.
		bool automation_on_arranger = false;
		/// The root is performance view (`View::modButtonAction` in `origin/main:src/deluge/gui/views/view.cpp`): a mod
		/// button is taken whatever UI mode is active, and it leaves a stutter playing.
		bool root_is_performance = false;
		/// The root is a clip minder (`rootUIIsClipMinderScreen`): the VU meter is not offered. Not the same question
		/// as `is_clip_context`: in session view with a clip pressed the knobs are on the clip and the root is not one.
		bool root_is_clip_minder = false;
		/// `View::modButtonUIModeInRange`: true with no UI mode active.
		bool mod_button_ui_mode_in_range = true;
		/// The root is a timeline view (`UI::toTimelineView`, `View::modEncoderAction_existentParam`
		/// (`origin/main:src/deluge/gui/views/view.cpp`)): a turn that changes a sound's note tails repaints the root's
		/// pads only then.
		bool root_is_timeline_view = false;
	};

	/// @brief Tell this service what the root view is.
	void set_view_state(const ViewState& state) { view_ = state; }
	/// @brief What was last passed to `set_view_state`.
	const ViewState& view_state() const { return view_; }

	/// @}

	/// @name Gestures and their state
	///
	/// Each gesture decides here and shows through a `KnobPresentation`: `handle()` passes the shell's own, a root
	/// view its own.
	/// @{

	/// @brief Push context a caller reads from its own button state.
	struct PushContext {
		bool shift = false;     ///< Shift held or latched: the push deletes the knob's automation.
		bool learn = false;     ///< Learn held: the push is a copy or paste, not the knobs'.
		bool mode_held = false; ///< The knobs' mode's mod button is held: its popup is re-shown.
	};

	/// @brief One gold-knob turn (`View::modEncoderAction` in `origin/main:src/deluge/gui/views/view.cpp`), already
	///        accelerated: the turn, then the instrument marked edited on a clip.
	/// @param knob   0 for the lower knob, 1 for the upper.
	/// @param offset The accelerated detents.
	/// @param now_ms The gesture's clock, which the value popup times out from.
	/// @param out    Where the turn shows.
	void turn(int32_t knob, int32_t offset, uint32_t now_ms, KnobPresentation& out);

	/// @brief One gold-knob push (`View::modEncoderButtonAction` in `origin/main:src/deluge/gui/views/view.cpp`).
	/// @param knob 0 for the lower knob, 1 for the upper.
	/// @param on   Press or release.
	/// @param ctx  The buttons held with it.
	/// @param out  Where the push shows.
	void push(int32_t knob, bool on, const PushContext& ctx, KnobPresentation& out);

	/// @brief One mod button pressed or released (`View::modButtonAction` in
	///        `origin/main:src/deluge/gui/views/view.cpp`): the knobs' mode, the VU-meter toggle, the end of a stutter,
	///        and the button's popup.
	/// @param button 0..7.
	/// @param on     Press or release.
	/// @param out    Where the button shows.
	void section_button(int32_t button, bool on, KnobPresentation& out);

	/// @brief One mod button, shown through the shell's presentation.
	/// @param which_button 0..7.
	/// @param on           Press or release.
	/// @note This drives the shell's presentation outside `handle()`, which nothing then asks for its `Result`: any
	///       action the press owes waits there for the next `handle()` to return it. A mod button owes none today
	///       (its popups are notices, and the frame redeclares its LEDs), so a caller that needs the actions goes
	///       through `handle()` instead.
	void mod_button_action(int32_t which_button, bool on) { section_button(which_button, on, guest_); }

	/// @brief What knob @p knob's indicator reads.
	/// @param knob 0 for the lower knob, 1 for the upper.
	/// @return What it reads; with no song, a default that declares nothing (`indicator_level`).
	[[nodiscard]] ModIndicatorInfo indicator_info(int32_t knob) const;

	/// @brief The address knob @p knob turns, if it turns a param (clip view's automation copy and paste).
	/// @param knob 0 for the lower knob, 1 for the upper.
	/// @return The address; none with no focus, or for a knob on a setting or on nothing.
	[[nodiscard]] std::optional<project::ParamAddress> knob_address(int32_t knob) const;

	/// @brief Is the VU meter shown? (the focus's, which View keeps too), which pressing the selected mod button 0
	///        outside a clip minder toggles; its one effect here is the blinking volume mod LED.
	bool display_vu_meter() const { return focus_->vu_meter; }

	/// @brief A param was automated -- `View::notifyParamAutomationOccurred`: arms the
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
	void leds(deluge::ui::widgets::core::LedFrame& frame) override;
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

	/// @brief One knob's indicator level -- the arithmetic half of `View::setKnobIndicatorLevel`
	/// (`origin/main:src/deluge/gui/views/view.cpp`).
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
	/// @brief The turn itself: the comp arm's setting, or the param's write, popup, feedback and centre blink.
	void turn_knob(int32_t which_encoder, int32_t offset, uint32_t now_ms, KnobPresentation& out);
	/// @brief The value popup's patch sources and owner context for @p turn, handed to @p out.
	void show_popup(const ModTurn& turn, uint32_t now_ms, KnobPresentation& out);
	/// @brief `View::potentiallyMakeItHarderToTurnKnob`: blink the indicator at a centred position of a param that
	///        shows its middle; stop the blink otherwise.
	void potentially_make_it_harder_to_turn_knob(int32_t which_encoder, const ModTurn& turn, KnobPresentation& out);
	/// @brief Declare both knob indicator columns -- `View::setKnobIndicatorLevels`.
	void declare_indicators(deluge::ui::widgets::core::LedFrame& frame);
	/// @brief Is knob @p which_encoder inside its blink's flash? At most one knob blinks at a time.
	bool indicator_blinking(int32_t which_encoder) const;
	/// @brief The param a knob turn reports to MIDI follow: which param in its plugin, where it is and the knob's new
	///        position.
	struct TurnedParam {
		deluge::project::PluginParam param;           ///< The param the knob turned, in its plugin.
		const deluge::project::ParamAddress& address; ///< The param the knob turned.
		int32_t knob_pos;                             ///< The knob's new position, -64..64.
	};
	/// @brief `View::sendMidiFollowFeedback`, through the installed `bind::MidiFeedback`.
	/// @param param         The param the knob turned, or nullopt for the no-param form of the feedback.
	/// @param is_automation Whether the feedback reports automation.
	void send_midi_follow_feedback(const std::optional<TurnedParam>& param, bool is_automation);
	/// @brief Whether the knobs have a song's record and a port set to act on.
	bool bound() const;
	/// @brief Whether MIDI follow sends back the values a followed param's automation plays: the device's
	///        `midiFollow/feedback/automation` row is not DISABLED. With no device port, the row's default (DISABLED).
	[[nodiscard]] bool feedback_automation_on() const;
	/// @brief The focus's owner's effect settings: a sound's own, or its chain's; null for none.
	const effects::EffectStates* owner_effects() const;
	/// @brief A stutter-rate knob on the focus's owner is quantized.
	bool owner_stutter_quantized() const;
	/// @brief The gesture's context a push or a mod button's popup reads.
	/// @param mode_held The knobs' mode's mod button is held.
	PushInputs push_inputs(bool mode_held) const;

	deluge::ui::shell::SceneStack& stack_;
	deluge::ui::shell::InputLowering& lowering_;
	const deluge::ui::controls::Board board_;                ///< what the knobs and buttons are on
	const deluge::ui::scenes::bind::Ports* ports_ = nullptr; ///< the installed ports, or null for none
	const deluge::project::Song* model_ = nullptr;           ///< the song's record, or null for none
	/// The focus this service keeps when it is given no store.
	bind::KnobFocus own_focus_{};
	/// Where the focus is kept: @ref own_focus_, or the store @ref use_focus names.
	bind::KnobFocus* focus_ = &own_focus_;
	/// What @ref use_focus asked to be told of a write to the store.
	std::function<void()> focus_changed_;
	/// A MIDI track's knob (@ref selecting_knob_) is choosing its CC, which the next mod button ends.
	bool selecting_cc_ = false;
	int32_t selecting_knob_ = 0;
	/// The last clock `tick()` or `handle()` saw.
	uint32_t now_ms_ = 0;
	/// The shell's presentation, which `handle()` shows through and whose blink state `leds()` declares.
	GuestKnobPresentation guest_;
	ViewState view_{};
	std::optional<uint32_t> automation_display_ms_;
	bool automation_updates_mod_levels_ = false;
	std::optional<uint32_t> automation_feedback_ms_;
	static constexpr uint32_t kAutomationTimerMs = 25;
};

} // namespace deluge::ui::scenes::services
