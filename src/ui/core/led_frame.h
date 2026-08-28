#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace deluge::ui {

// LED declarations (surface-agnostic data; composed by the shell).
/// Defined by the board profile.
enum class LedId : uint8_t;
/// One of the two physical knob LED indicators.
enum class IndicatorId : uint8_t { Upper, Lower };
/// Declared state of one LED: off, on, or blinking with a period and a deadline.
struct LedState {
	enum class Kind : uint8_t { Off, On, Blink } kind;
	uint16_t period_ms;
	uint32_t until_ms;
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
	/// @brief Reset the frame to empty (no LED or indicator declarations).
	void clear() {
		count_ = 0;
		has_indicator_[0] = has_indicator_[1] = false;
	}

private:
	static constexpr bool indicator_in_range(IndicatorId r) { return std::to_underlying(r) < 2; }

	std::pair<LedId, LedState> entries_[kMaxEntries];
	size_t count_ = 0;
	KnobIndicator indicators_[2]{};
	bool has_indicator_[2]{};
};

} // namespace deluge::ui
