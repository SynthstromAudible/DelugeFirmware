#pragma once

#include <array>
#include <cstdint>

static constexpr uint8_t kMaxMatricealSteps = 64;

enum class LaneDirection : uint8_t {
	FORWARD,
	REVERSE,
	PINGPONG,
};

struct MatricealLane {
	std::array<int16_t, kMaxMatricealSteps> values{};
	uint8_t length{0};
	uint8_t position{0};
	LaneDirection direction{LaneDirection::FORWARD};
	bool pingpongReversed{false};

	/// Returns the value at the current position, or 0 if lane is disabled.
	int16_t currentValue() const;

	/// Advances the position by one step according to direction.
	/// No-op if lane is disabled (length == 0).
	void advance();

	/// Resets position to 0 and pingpong state.
	void reset();
};
