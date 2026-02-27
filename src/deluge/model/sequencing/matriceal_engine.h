#pragma once

#include <algorithm> // for std::clamp
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

// Forward declaration
class MusicalKey;

struct MatricealNote {
	int16_t noteCode{-1}; // -1 = rest
	uint8_t velocity{100};
	uint8_t gate{64};
	uint8_t retrigger{0};
	bool glide{false};

	bool isRest() const { return noteCode < 0; }
	static MatricealNote rest() { return MatricealNote{-1, 0, 0, 0, false}; }
};

/// Converts a scale-degree offset (e.g., +3 or -2) to a semitone offset
/// using the given musical key's mode notes.
int32_t scaleDegreeToSemitoneOffset(int32_t degrees, const MusicalKey& key);

class MatricealEngine {
public:
	// The 9 parameter lanes
	MatricealLane trigger;
	MatricealLane pitch;
	MatricealLane interval;
	MatricealLane velocity;
	MatricealLane octave;
	MatricealLane gate;
	MatricealLane retrigger;
	MatricealLane probability;
	MatricealLane glide;

	// Interval accumulation state
	int32_t intervalAccumulator{0};
	static constexpr int32_t kMaxIntervalSemitones = 48;

	// Default values for disabled lanes
	static constexpr uint8_t kDefaultVelocity = 100;
	static constexpr uint8_t kDefaultGate = 64;

	/// Steps the engine forward by one tick.
	/// Returns the composed note, or a rest if trigger is 0 or probability fails.
	MatricealNote step(const MusicalKey& key);

	/// Resets all lane positions and interval accumulator.
	void reset();

	/// Sets the RNG seed for deterministic testing.
	void setSeed(uint32_t seed);

	bool locked{false};
	uint32_t lockedSeed{0};

	void lock();
	void unlock();

private:
	uint32_t rngState_{1};
	uint8_t randomPercent();
};
