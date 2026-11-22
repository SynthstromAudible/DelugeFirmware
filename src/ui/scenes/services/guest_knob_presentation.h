#pragma once

/// @file
/// @brief `GuestKnobPresentation` -- the menu shell's `KnobPresentation`: its scene stack, its frame and its notices.

#include "ui/scenes/overlay/param_popup_scene.h"
#include "ui/scenes/services/knob_presentation.h"

#include "synth/modulation/patch_source.h"
#include "ui/shell/scene.h"
#include "ui/shell/scene_stack.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace deluge::ui::scenes::services {

/// @brief The gold knobs as the menu shell shows them.
///
/// The value popup is a scene on the shell's stack; the indicators' blink is state the frame reads
/// (`ModKnobs::leds`); a redraw is an action in the `shell::Result` the gesture returns; a popup or notice is a
/// `deluge::base::notify` notice. The indicators, the mod LEDs and the VU sidebar are redeclared every frame, so the
/// hooks that ask for them do nothing here.
class GuestKnobPresentation final : public KnobPresentation {
public:
	/// @param popup  The value popup.
	/// @param stack  The scene stack the popup is pushed onto, and whose top the same-param test reads.
	/// @param now_ms The clock a blink starts at: the knobs' last tick or event, read when the blink starts.
	/// @param root_is_timeline_view Whether the root view under the menu is a timeline view, read when a turn changes
	///                              note tails.
	/// @note @p now_ms and @p root_is_timeline_view are held by reference, so they must outlive this presentation.
	///       `ModKnobs` owns them and the presentation and cannot be copied, so the references always read its
	///       current state, with no setter for each entry into the presentation to remember.
	GuestKnobPresentation(overlay::ParamPopupScene& popup, deluge::ui::shell::SceneStack& stack, const uint32_t& now_ms,
	                      const bool& root_is_timeline_view)
	    : popup_(popup), stack_(stack), now_ms_(now_ms), root_is_timeline_view_(root_is_timeline_view) {}

	/// @name `KnobPresentation`
	/// @{
	/// @brief Is the menu on top focused on this very param (`View::modEncoderAction_existentParam`
	/// (`origin/main:src/deluge/gui/views/view.cpp`))?
	bool suppress_value_popup(const ModTurn& turn) override;
	/// @brief Hand the turn to the popup, push it if it is not stacked yet, and owe a repaint when it redrew.
	void show_value_popup(const ModTurn& turn, deluge::synth::PatchSource source1, deluge::synth::PatchSource source2,
	                      const overlay::PopupContext& context, uint32_t now_ms) override;
	void value_written() override {}
	void blink_indicator(int32_t knob, bool bipolar) override;
	void stop_blink_indicator(int32_t knob) override;
	void indicators_changed() override {}
	void mode_changed() override {}
	void vu_sidebar_changed() override {}
	/// @brief Repaint every pad of the shell's, and the root view's under the menu (which the legacy UI draws) when
	///        it is a timeline view.
	void note_tails_changed() override;
	/// @brief A popup that stays until a mod button's release cancels it (`display->popupText`).
	void popup_text(std::string_view text) override;
	void notice_text(std::string_view text) override;
	/// @brief A `Notification` notice, which the shell shows as its title-and-value popup.
	void notification(std::string_view title, std::string_view value) override;
	/// @brief Nothing: the frame redeclares both indicators from the records.
	void setting_indicator(int32_t /*knob*/, int32_t /*knob_pos*/) override {}
	void popup(deluge::l10n::String string) override;
	void cancel_popup() override;
	/// @}

	/// @brief The actions the gestures since the last call owe, and forget them.
	[[nodiscard]] deluge::ui::shell::Result take_result();

	/// @brief Should knob @p knob's indicator be blinking? (`blinkKnobIndicator` / `stop...`).
	[[nodiscard]] bool blink_on(int32_t knob) const { return in_range(knob) && blink_[knob]; }
	/// @brief Was the last blink decision for knob @p knob a bipolar param's?
	[[nodiscard]] bool blink_bipolar(int32_t knob) const { return in_range(knob) && blink_bipolar_[knob]; }
	/// @brief When knob @p knob's blink started: the phase origin of its flash.
	[[nodiscard]] uint32_t blink_start_ms(int32_t knob) const { return in_range(knob) ? blink_start_ms_[knob] : 0; }

private:
	static bool in_range(int32_t knob) { return knob >= 0 && knob < kKnobCount; }

	overlay::ParamPopupScene& popup_;
	deluge::ui::shell::SceneStack& stack_;
	const uint32_t& now_ms_;
	const bool& root_is_timeline_view_;
	deluge::ui::shell::Result result_;
	std::array<bool, kKnobCount> blink_{};
	std::array<bool, kKnobCount> blink_bipolar_{};
	std::array<uint32_t, kKnobCount> blink_start_ms_{};
};

} // namespace deluge::ui::scenes::services
