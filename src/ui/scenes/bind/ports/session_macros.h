#pragma once

/// @file
/// @brief The song's eight session macros, as a root scene shows and runs them while SESSION is held.

#include <cstdint>

namespace deluge::ui::scenes::bind {

/// @brief What a session macro does (`project::SessionMacroKind`).
enum class MacroKind : uint8_t { NONE, CLIP_LAUNCH, OUTPUT_CYCLE, SECTION };

/// @brief One macro slot as the macros column shows it.
struct MacroFact {
	MacroKind kind = MacroKind::NONE;
	bool has_clip = false;    ///< `CLIP_LAUNCH`: the clip it names is in the song
	bool clip_active = false; ///< `CLIP_LAUNCH`: the clip plays when nothing is soloed (`activeIfNoSolo`)
	bool clip_armed = false;  ///< `CLIP_LAUNCH`: the clip is armed to launch or to stop
	bool operator==(const MacroFact&) const = default;
};

/// @brief The modifiers held as a macro's pad is released, which `View::activateMacro` reads from the buttons.
struct MacroPress {
	bool affect_entire = false; ///< AFFECT_ENTIRE held: a clip launch opens the clip instead of toggling it
	bool shift = false;         ///< SHIFT held: a toggle is immediate rather than launched on the bar
	bool operator==(const MacroPress&) const = default;
};

/// @brief The song's session macros: what each slot shows, and running one.
class SessionMacros {
public:
	virtual ~SessionMacros() = default;
	/// @brief How many slots the song holds, one per pad of the macros column.
	static constexpr uint8_t kSlots = 8;
	/// @brief The macro in slot @p y.
	/// @param y 0 to `kSlots - 1`.
	/// @return What it does and its clip's state.
	[[nodiscard]] virtual MacroFact macro(uint8_t y) const = 0;
	/// @brief Run slot @p y's macro, as `View::activateMacro` does.
	/// @param y 0 to `kSlots - 1`. @param press The modifiers held.
	virtual void activate(uint8_t y, MacroPress press) = 0;
};

} // namespace deluge::ui::scenes::bind
