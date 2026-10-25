#pragma once

/// @file
/// @brief The menu's shortcut blinks: which pads blink for the focused node, and legacy's counter
///        that decides when each one flashes.
///
/// Ports `SoundEditor::updatePadLightsFor` (`gui/ui/sound_editor.cpp:1028-1146`), which sets the
/// blink up, and `SoundEditor::blinkShortcut` (`:1205-1245`), which plays it. A blink is a pad
/// flash the control surface plays by itself (`ui::grid::GridFrame::flash`); it paints no pixel.

#include "scenes/menu/context.h"
#include "scenes/menu/shortcuts.h"
#include "ui/grid/frame.h"

#include <array>
#include <cstdint>

namespace deluge::scenes::menu {

struct MenuState;
struct Node;

/// @brief The two steps of legacy's blink counter, in milliseconds.
///
/// `blinkShortcut` alternates on one counter: an even step flashes the patching-source pads and
/// re-arms the timer at **20 ms**, an odd step flashes the focused param's pad and re-arms at
/// **180 ms**. So one cycle is two steps and 200 ms, and the two kinds of pad never flash at the
/// same instant (legacy: "blinking two pads on the same row at same time doesn't work").
/// @{
inline constexpr uint16_t kShortcutBlinkMs = 180;
inline constexpr uint16_t kSourceBlinkMs = 20;
inline constexpr uint32_t kBlinkCycleMs = uint32_t{kSourceBlinkMs} + kShortcutBlinkMs;
/// @}

/// @brief A source pad's frequency mask that means "does not blink": legacy's 255 in
///        `sourceShortcutBlinkFrequencies`.
inline constexpr uint8_t kNoBlink = 255;

/// @brief The focused param's pad and how it blinks: legacy's `currentParamShortcutX`/`Y`,
///        `paramShortcutBlinkFrequency` and `currentParamColour`.
struct ParamBlink {
	uint8_t x = 0;
	uint8_t y = 0;
	/// False when no param pad blinks.
	bool valid = false;
	/// The pad flashes on a cycle `c` where `(c & freq_mask) == 0`: 0 is every cycle, 3 every fourth.
	uint8_t freq_mask = 0;
	deluge::ui::grid::FlashColour colour = deluge::ui::grid::FlashColour::Default;
	bool operator==(const ParamBlink&) const = default;
};

/// @brief One patching-source pad's blink: legacy's `sourceShortcutBlinkFrequencies`,
///        `sourceShortcutBlinkColours` and `source_shortcut_blink_alternates` at one cell.
struct SourceBlink {
	/// As `ParamBlink::freq_mask`; `kNoBlink` for a pad that does not blink.
	uint8_t freq_mask = kNoBlink;
	deluge::ui::grid::FlashColour colour = deluge::ui::grid::FlashColour::Default;
	/// Flash yellow instead on a cycle where `(c & (freq_mask + 1)) != 0`: both layers' sources
	/// share the pad.
	bool alternates = false;
	bool operator==(const SourceBlink&) const = default;
};

/// @brief Everything that blinks while one node is focused.
struct BlinkPlan {
	ParamBlink param{};
	/// `sources[sx][y]` is pad `(14 + sx, y)`, the patching-source columns.
	std::array<std::array<SourceBlink, kShortcutRows>, 2> sources{};

	/// @return True when any source pad blinks.
	bool any_source() const;
	bool operator==(const BlinkPlan&) const = default;
};

/// @brief What blinks while @p focused is focused.
///
/// The focused node's own shortcut pad, every cycle, yellow when it is the second layer's
/// (`updatePadLightsFor`'s `setupShortcutBlink(x, y, 0, ...)`, `sound_editor.cpp:1093-1112`).
/// Nothing blinks for the name editors and the sample-marker editors, nor under the settings, note
/// and note-row roots (`:1029-1040`).
/// @param ctx What the menu edits.
/// @param state The menu's state.
/// @param focused The focused node.
/// @return The plan; an empty one when nothing blinks.
BlinkPlan plan_blinks(const MenuContext& ctx, const MenuState& state, const Node& focused);

/// @brief Legacy's `shortcutBlinkCounter` at @p now_ms for a cycle started at @p anchor_ms.
///
/// Legacy zeroes the counter when it sets a blink up and steps it at once, so step 0 begins at
/// the anchor; odd steps begin `kSourceBlinkMs` into each cycle. Written as a difference so it
/// survives the `uint32_t` clock's wrap.
/// @param anchor_ms When the cycle started.
/// @param now_ms The tick's clock.
/// @return The counter's value.
uint32_t blink_step(uint32_t anchor_ms, uint32_t now_ms);

/// @brief Milliseconds from @p now_ms to the start of the counter's next step.
/// @param anchor_ms When the cycle started.
/// @param now_ms The tick's clock.
/// @return The remainder of legacy's 20 ms or 180 ms arm.
uint32_t ms_to_next_step(uint32_t anchor_ms, uint32_t now_ms);

/// @brief Flash what @p plan asks for at counter value @p step, as `blinkShortcut` does.
/// @param f The frame to flash through.
/// @param plan What blinks.
/// @param step The counter's value.
void fire_blink_step(deluge::ui::grid::GridFrame& f, const BlinkPlan& plan, uint32_t step);

} // namespace deluge::scenes::menu
