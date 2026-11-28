#pragma once

/// @file
/// @brief What a root scene asks of the other root views and of the menu: leaving the clip, and the menus a press
///        opens. The host performs each as the legacy view's arm does.

#include <cstdint>

namespace deluge::ui::scenes::bind {

/// @brief What a root scene asks of the other root views; the host performs each as the legacy view does. Each acts
///        on the current clip, which is the clip the root scene shows.
class RootViews {
public:
	virtual ~RootViews() = default;
	/// @brief Leave the clip for the song: the arranger when the clip was entered from it (or lives only there) and
	///        the arranger will take it, otherwise the session (`ClipMinder::transitionToArrangerOrSession`).
	virtual void to_session_or_arranger() = 0;
	/// @brief Open the current clip in the automation view (the CLIP button's arm).
	virtual void to_automation() = 0;
	/// @brief Offer a main pad to the menu's shortcuts, as `menuShortcutPadAction` does, press and release alike: the
	///        host decides from the shortcut settings and the menu's tables whether the pad is a shortcut.
	/// @param x The pad's column. @param y The pad's row. @param on True for a press.
	/// @return True when the menu took the pad (opened at its page, showed "coming soon", or swallowed it), so the
	///         view runs nothing of its own for it.
	[[nodiscard]] virtual bool menu_shortcut_pad(uint8_t x, uint8_t y, bool on) = 0;
	/// @brief Move the current clip to the next or previous audio output (`navigateThroughAudioOutputsForAudioClip`).
	/// @param direction The select encoder's signed detents.
	virtual void next_audio_output(int8_t direction) = 0;
	/// @brief Open the current clip's own menu (SELECT without shift), as `openMenu`'s site 5 does.
	/// @return True when the menu opened.
	[[nodiscard]] virtual bool open_clip_menu() = 0;
};

} // namespace deluge::ui::scenes::bind
