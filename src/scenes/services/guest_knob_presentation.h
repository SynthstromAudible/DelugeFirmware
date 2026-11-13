#pragma once

/// @file
/// @brief `GuestKnobPresentation` -- the menu shell's `KnobPresentation`: its scene stack, its frame and its notices.

#include "scenes/overlay/param_popup_scene.h"
#include "scenes/services/knob_presentation.h"

#include "shell/scene.h"
#include "shell/scene_stack.h"

#include <cstdint>
#include <string_view>

namespace deluge::scenes::services {

/// @brief The gold knobs as the menu shell shows them.
///
/// The value popup is a scene on the shell's stack; the indicators' blink is state the frame reads
/// (`ModKnobs::leds`); a redraw is an action in the `shell::Result` the gesture returns; a popup or notice is a
/// `deluge::notify` notice. The indicators, the mod LEDs and the VU sidebar are redeclared every frame, so the hooks
/// that ask for them do nothing here.
class GuestKnobPresentation final : public KnobPresentation {
public:
	/// @brief How many gold knobs there are (`hid/encoders.h`'s `mod0`/`mod1`).
	static constexpr int32_t kKnobCount = 2;

	/// @param popup  The value popup.
	/// @param stack  The scene stack the popup is pushed onto, and whose top the same-param test reads.
	/// @param now_ms The clock a blink starts at: the knobs' last tick or event, read when the blink starts.
	GuestKnobPresentation(overlay::ParamPopupScene& popup, deluge::ui::shell::SceneStack& stack, const uint32_t& now_ms)
	    : popup_(popup), stack_(stack), now_ms_(now_ms) {}

	/// @name `KnobPresentation`
	/// @{
	/// @brief Is the menu on top focused on this very param (`View::modEncoderAction_existentParam`)?
	bool suppress_value_popup(const ModTurn& turn) override;
	/// @brief Hand the turn to the popup, push it if it is not stacked yet, and owe a repaint when it redrew.
	void show_value_popup(const ModTurn& turn, PatchSource source1, PatchSource source2,
	                      const overlay::PopupContext& context, uint32_t now_ms) override;
	void value_written() override {}
	void blink_indicator(int32_t knob, bool bipolar) override;
	void stop_blink_indicator(int32_t knob) override;
	void indicators_changed() override {}
	void mode_changed() override {}
	void vu_sidebar_changed() override {}
	/// @brief Repaint every pad: the shell's, and the root view's under a menu, which the legacy UI draws.
	void note_tails_changed() override;
	/// @brief A popup that stays until a mod button's release cancels it (`display->popupText`).
	void popup_text(std::string_view text) override;
	void notice_text(std::string_view text) override;
	void popup(deluge::l10n::String string) override;
	void cancel_popup() override;
	/// @}

	/// @brief The actions the gestures since the last call owe, and forget them.
	deluge::ui::shell::Result take_result();

	/// @brief Should knob @p knob's indicator be blinking? (`blinkKnobIndicator` / `stop...`).
	bool blink_on(int32_t knob) const { return in_range(knob) && blink_[knob]; }
	/// @brief Was the last blink decision for knob @p knob a bipolar param's?
	bool blink_bipolar(int32_t knob) const { return in_range(knob) && blink_bipolar_[knob]; }
	/// @brief When knob @p knob's blink started: the phase origin of its flash.
	uint32_t blink_start_ms(int32_t knob) const { return in_range(knob) ? blink_start_ms_[knob] : 0; }

private:
	static bool in_range(int32_t knob) { return knob >= 0 && knob < kKnobCount; }

	overlay::ParamPopupScene& popup_;
	deluge::ui::shell::SceneStack& stack_;
	const uint32_t& now_ms_;
	deluge::ui::shell::Result result_;
	bool blink_[kKnobCount]{};
	bool blink_bipolar_[kKnobCount]{};
	uint32_t blink_start_ms_[kKnobCount]{};
};

} // namespace deluge::scenes::services
