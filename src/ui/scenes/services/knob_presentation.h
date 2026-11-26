#pragma once

/// @file
/// @brief `KnobPresentation` -- the port `ModKnobs`' gestures show through.

#include "ui/scenes/overlay/param_popup_scene.h"
#include "ui/scenes/services/mod_target.h"

#include "gui/l10n/strings.h"
#include "synth/modulation/patch_source.h"

#include <cstdint>
#include <string_view>

namespace deluge::ui::scenes::services {

/// @brief What a gold-knob gesture shows, and where.
///
/// `ModKnobs` decides; a presentation draws. The menu shell draws through its scene stack and frame
/// (`GuestKnobPresentation`); the root views through the legacy display and LEDs (`gui/ui/root_knobs`).
class KnobPresentation {
public:
	/// @brief The most gold knobs a presentation keeps state for: the Deluge's two (`hid/encoders.h` `mod0`/`mod1`: 0
	///        the lower, 1 the upper). `ModKnobs` refuses a board with more slots than this.
	static constexpr int32_t kKnobCount = 2;

	virtual ~KnobPresentation() = default;

	/// @brief Should this turn show no value popup? Asked once per turn, before the write.
	/// @param turn The turn, with `new_knob_pos` set.
	/// @return True to show nothing. A presentation may draw its own display here (performance view's).
	virtual bool suppress_value_popup(const ModTurn& turn) = 0;

	/// @brief Show the value popup for @p turn.
	/// @param turn    The turn, with `new_knob_pos` set.
	/// @param source1 The cable's bottom source, or `PatchSource::NONE`.
	/// @param source2 The cable's top source, or `PatchSource::NONE`.
	/// @param context What the popup reads of the owner.
	/// @param now_ms  The gesture's clock.
	virtual void show_value_popup(const ModTurn& turn, deluge::synth::PatchSource source1,
	                              deluge::synth::PatchSource source2, const overlay::PopupContext& context,
	                              uint32_t now_ms) = 0;

	/// @brief A turn wrote a value (`View::refreshKnobParamManager`'s moment).
	virtual void value_written() = 0;

	/// @brief Start knob @p knob's centre blink, ending the other knob's (one blinks at a time).
	/// @param knob    0 for the lower knob, 1 for the upper.
	/// @param bipolar The param is bipolar.
	virtual void blink_indicator(int32_t knob, bool bipolar) = 0;
	/// @brief Stop knob @p knob's blink.
	/// @param knob 0 for the lower knob, 1 for the upper.
	virtual void stop_blink_indicator(int32_t knob) = 0;

	/// @brief A push may have moved the knobs to other params: re-level both indicators.
	/// @note A presentation may re-tell `ModKnobs` its song and view state from here (the root's does, through
	///       `View::setKnobIndicatorLevels`), so `ModKnobs::push` calls this last and reads nothing after it.
	virtual void indicators_changed() = 0;
	/// @brief A mod button changed the knobs' mode or owner state: re-level the indicators and the mod LEDs.
	/// @note As `indicators_changed`: `ModKnobs::section_button` calls this last and reads nothing after it.
	virtual void mode_changed() = 0;
	/// @brief A mod button press outside a clip minder, in range: the VU sidebar redraws if it is showing.
	virtual void vu_sidebar_changed() = 0;
	/// @brief A turn changed the sound's note tails: the pads repaint.
	virtual void note_tails_changed() = 0;

	/// @brief A one-line popup (a push's, a mod button's).
	/// @param text What it says.
	virtual void popup_text(std::string_view text) = 0;
	/// @brief A short notice (a push's setting change).
	/// @param text What it says.
	virtual void notice_text(std::string_view text) = 0;
	/// @brief A turn moved a knob on a setting (the song compressor's): its name and new value.
	/// @param title The setting's name.
	/// @param value Its value, in its units.
	virtual void notification(std::string_view title, std::string_view value) = 0;
	/// @brief A turn moved knob @p knob on a setting: its indicator shows @p knob_pos now.
	/// @param knob     0 for the lower knob, 1 for the upper.
	/// @param knob_pos The setting's knob position, -64..64.
	virtual void setting_indicator(int32_t knob, int32_t knob_pos) = 0;
	/// @brief A localised popup (automation deleted).
	/// @param string The string to show.
	virtual void popup(deluge::l10n::String string) = 0;
	/// @brief End any popup a mod button or push left up.
	virtual void cancel_popup() = 0;
};

} // namespace deluge::ui::scenes::services
