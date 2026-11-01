#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace deluge::ui {

// LED declarations (surface-agnostic data; composed by the shell).
/// Defined by the board profile.
enum class LedId : uint8_t;
/// One of the two physical knob LED indicators.
///
/// `Lower` is 0 and `Upper` is 1, deliberately: these values ARE the `which` index the board's
/// `set_indicator` takes, and the board passes it straight through to
/// `deluge_control_set_indicator` (`shell/board_surfaces.cpp`). On the wire that index is legacy's
/// `whichKnob`, which is the *mod encoder* number -- `View::setKnobIndicatorLevel(whichModEncoder)`
/// (`src/deluge/gui/views/view.cpp:1420-1485`) -> `indicator_leds::setKnobIndicatorLevel` ->
/// `deluge_control_set_indicator(whichKnob, …)` (`src/deluge/hid/led/indicator_leds.cpp:241`) --
/// and mod encoder 0 is `mod0`, the LOWER gold encoder (`src/deluge/hid/encoders.h:80-81`).
/// Numbering these the other way round would make the pass-through silently swap the two knobs.
enum class IndicatorId : uint8_t { Lower, Upper };
/// @brief The two blink rates every blinking LED shares a phase with, as `indicator_leds::blinkLed`'s
///        `blinkingType` 0 and 1.
enum class BlinkRate : uint8_t { Normal, Fast };

/// @brief Where one rate's shared blink last (re)started: `indicator_leds::blinkLed`'s
///        `ledBlinkState[blinkingType]` and its timer.
struct BlinkPhase {
	/// The restart, on the shell's clock.
	uint32_t origin_ms = 0;
	/// Whether the blink starts lit (`blinkLed`'s `initialState`).
	bool initially_on = true;
	/// Whether any blink of this rate has started; one that never has reads lit.
	bool running = false;
};

/// Declared state of one LED: off, on, or blinking with a period and a deadline.
struct LedState {
	enum class Kind : uint8_t { Off, On, Blink, Shared } kind;
	uint16_t period_ms;
	uint32_t until_ms;
	/// A blink's start, on the shell's clock; read only when `first_ms` is set.
	uint32_t origin_ms = 0;
	/// When set, a blink lit from `origin_ms` for this long and then toggling every `period_ms`, as
	/// `indicator_leds::blinkLed` runs one (`kInitialFlashTime`, then `kFlashTime`). Zero: the phase is the absolute
	/// clock's.
	uint16_t first_ms = 0;
	/// For `Kind::Shared`: whose phase.
	BlinkRate rate = BlinkRate::Normal;

	/// @brief A blink on the shell's shared phase for @p rate: every LED declared this way at one rate
	///        blinks together, and a restart by any declarer (`LedFrame::restart`) restarts them all.
	/// @param rate Normal (`kInitialFlashTime` lit, then `kFlashTime`) or Fast (`kFastFlashTime`).
	/// @return The declaration.
	static constexpr LedState shared(BlinkRate rate) { return LedState{Kind::Shared, 0, 0, 0, 0, rate}; }
};
/// Per-segment LED levels of one knob's indicator column.
struct KnobIndicator {
	uint8_t levels[4];
};

/// @brief Accumulates one frame's worth of LED and knob-indicator declarations, to be read back by the
///        shell and rendered to the hardware.
class LedFrame {
public:
	static constexpr size_t kMaxEntries = 64;

	/// @brief Declare @p id's state for this frame, replacing any previous declaration of the same
	///        id.
	///
	/// Returns false when the frame is full (kMaxEntries distinct ids) and the declaration was
	/// dropped -- the same reasoning as NoticeQueue::push: a dropped LED is indistinguishable
	/// from an LED nobody declared, so the fact is returned rather than swallowed.
	/// @param id LED to declare state for.
	/// @param s  State to declare.
	/// @return false if the frame was already full and the declaration was dropped.
	[[nodiscard]] bool set(LedId id, LedState s);

	/// @brief Declare indicator @p r's LED levels for this frame.
	/// @param r Indicator to set.
	/// @param k Levels to store.
	/// @return false when @p r is out of range and the indicator was not stored.
	[[nodiscard]] bool indicator(IndicatorId r, KnobIndicator k) {
		// IndicatorId has a fixed underlying type, so an out-of-range value (IndicatorId{2}) is
		// constructible; guard rather than index indicators_/has_indicator_ unchecked (same house style as
		// gfx::grid::PadGrid::set's in_range guard for caller-supplied coordinates).
		if (!indicator_in_range(r)) {
			return false;
		}
		indicators_[std::to_underlying(r)] = k;
		has_indicator_[std::to_underlying(r)] = true;
		return true;
	}
	/// @brief All LED declarations made so far this frame.
	/// @return A view over the (id, state) pairs declared via set().
	std::span<const std::pair<LedId, LedState>> entries() const { return {entries_, count_}; }
	/// @brief Indicator @p r's declared levels.
	/// @param r Indicator to look up.
	/// @return Pointer to the stored levels, or nullptr if @p r is out of range or was not
	///         declared this frame.
	const KnobIndicator* indicator_or_null(IndicatorId r) const {
		return indicator_in_range(r) && has_indicator_[std::to_underlying(r)] ? &indicators_[std::to_underlying(r)]
		                                                                      : nullptr;
	}
	/// @brief Start a frame for the tick at @p now_ms: empty, with no restarts asked for.
	/// @param now_ms The tick's clock, which `now_ms()` reports to the declarers.
	void begin(uint32_t now_ms) {
		clear();
		now_ms_ = now_ms;
	}
	/// @brief The tick's clock.
	/// @return What `begin` was given.
	uint32_t now_ms() const { return now_ms_; }
	/// @brief Restart @p rate's shared phase now, as `indicator_leds::blinkLed` does wherever it is called.
	/// @param rate         The phase to restart.
	/// @param initially_on Whether the blink starts lit (`blinkLed`'s `initialState`).
	void restart(BlinkRate rate, bool initially_on = true) { restarts_[std::to_underlying(rate)] = initially_on; }
	/// @brief The restart asked for @p rate this frame, if any.
	/// @param rate The phase.
	/// @return Its `initially_on`, or nothing.
	std::optional<bool> restart_requested(BlinkRate rate) const { return restarts_[std::to_underlying(rate)]; }
	/// @brief Reset the frame to empty (no LED or indicator declarations).
	void clear() {
		restarts_[0] = restarts_[1] = std::nullopt;
		count_ = 0;
		has_indicator_[0] = has_indicator_[1] = false;
	}

private:
	static constexpr bool indicator_in_range(IndicatorId r) { return std::to_underlying(r) < 2; }

	std::pair<LedId, LedState> entries_[kMaxEntries];
	size_t count_ = 0;
	KnobIndicator indicators_[2]{};
	bool has_indicator_[2]{};
	uint32_t now_ms_ = 0;
	std::optional<bool> restarts_[2];
};

} // namespace deluge::ui
