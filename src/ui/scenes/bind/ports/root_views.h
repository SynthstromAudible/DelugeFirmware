#pragma once

/// @file
/// @brief What a root scene asks of the other root views and of the menu: leaving the clip, and the menus a press
///        opens. The host performs each as the legacy view's arm does.

#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::ui::scenes::bind {

/// @brief One main pad's colour in a clip's SHIFT shortcut overview, by channel.
struct OverviewPad {
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
	bool operator==(const OverviewPad&) const = default;
};

/// The main grid the overview covers.
/// @{
inline constexpr size_t kOverviewColumns = 16;
inline constexpr size_t kOverviewRows = 8;
/// @}

/// @brief The overview's pads, `[y][x]` with y = 0 the bottom row.
using OverviewPads = std::array<std::array<OverviewPad, kOverviewColumns>, kOverviewRows>;

/// @brief What a root scene asks of the other root views; the host performs each as the legacy view does. Each acts
///        on the current clip, which is the clip the root scene shows. A request that changes the root view or opens a
///        UI is performed after the shell's tick.
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
	/// @brief Open the current clip's own menu (SELECT without shift), as `openMenu`'s site 5 does; the host tells the
	///        scene when it has opened, which drops the scene's marker.
	virtual void open_clip_menu() = 0;
	/// @brief The SHIFT shortcut overview over the current clip (`renderClipShortcutsOverview`): each main pad the
	///        colour of the menu shortcut it opens here, black where it opens nothing.
	/// @param pads Filled, every pad.
	virtual void clip_shortcut_overview(OverviewPads& pads) = 0;
	/// @brief Open the song saver (`View::buttonAction`'s SAVE arm).
	virtual void open_song_saver() = 0;
	/// @brief Open the song loader (`View::buttonAction`'s LOAD arm).
	virtual void open_song_loader() = 0;
	/// @brief Open the clear-song menu, when it is available here (SHIFT with LOAD).
	virtual void open_clear_song() = 0;
};

} // namespace deluge::ui::scenes::bind
