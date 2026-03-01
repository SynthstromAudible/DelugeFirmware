#pragma once

#include <algorithm> // for std::clamp
#include <array>
#include <cstdint>

static constexpr uint8_t kMaxLanesSteps = 64;

enum class LaneDirection : uint8_t {
	FORWARD,
	REVERSE,
	PINGPONG,
};

struct LanesLane {
	std::array<int16_t, kMaxLanesSteps> values{};
	/// Per-step probability (0–100). Only used by lanes that support it.
	std::array<uint8_t, kMaxLanesSteps> stepProbability{};
	uint8_t length{16};
	uint8_t position{0};
	uint8_t lastPosition{0}; // Position of the most recently played step (for display)
	LaneDirection direction{LaneDirection::FORWARD};
	bool pingpongReversed{false};
	/// Euclidean state (used by trigger lane)
	uint8_t euclideanPulses{0};

	/// Clock division: 1 = every step (1/16), 2 = every 2nd step (1/8), 4 = 1/4, etc.
	/// Range: 1–32. Default 1 (no division).
	uint8_t clockDivision{1};
	/// Internal counter for division. Counts down from clockDivision.
	uint8_t divisionCounter{1};

	/// Returns the value at the current position, or 0 if lane is disabled.
	int16_t currentValue() const;

	/// Advances the position by one step according to direction.
	/// No-op if lane is disabled (length == 0).
	void advance();

	/// Advances only if the division counter has elapsed.
	/// Returns true if the lane actually advanced (i.e., this is a "real" tick for this lane).
	bool advanceWithDivision();

	/// Resets position to 0, pingpong state, and division counter.
	void reset();

	/// Initializes all stepProbability entries to 100.
	void initProbability();
};

// Forward declaration
class MusicalKey;

struct LanesNote {
	int16_t noteCode{-1}; // -1 = rest
	uint8_t velocity{100};
	uint8_t gate{75}; // matches kDefaultGate
	uint8_t retrigger{0};
	bool glide{false};

	bool isRest() const { return noteCode < 0; }
	static LanesNote rest() { return LanesNote{-1, 0, 0, 0, false}; }
};

/// Converts a scale-degree offset (e.g., +3 or -2) to a semitone offset
/// using the given musical key's mode notes.
int32_t scaleDegreeToSemitoneOffset(int32_t degrees, const MusicalKey& key);

class LanesEngine {
public:
	static constexpr int32_t kNumLanes = 9;
	static constexpr const char* kLaneNames[kNumLanes] = {
	    "TRIGGER", "PITCH", "OCTAVE", "VELOCITY", "GATE", "INTERVAL", "RETRIG", "PROB", "GLIDE",
	};

	/// Gate special values (above 100% = percentage)
	static constexpr int16_t kGateTie = 101;
	static constexpr int16_t kGateLegato = 102;

	/// Default step values when toggling a lane step on
	static constexpr int16_t kLaneDefaults[kNumLanes] = {
	    1,   // trigger
	    0,   // pitch (offset 0 = base note)
	    0,   // octave
	    100, // velocity
	    75,  // gate: 75% of step length
	    1,   // interval
	    1,   // retrigger
	    100, // probability
	    1,   // glide
	};

	/// Minimum values per lane (for knob clamping)
	static constexpr int16_t kLaneMins[kNumLanes] = {
	    0,   // trigger: off
	    -24, // pitch: -24 scale degrees from base
	    -4,  // octave: -4 octaves
	    1,   // velocity: minimum audible
	    5,   // gate: 5% of step length
	    -12, // interval: -12 scale degrees (or semitones when chromatic)
	    0,   // retrigger: off
	    0,   // probability: 0%
	    0,   // glide: off
	};

	/// Maximum values per lane
	static constexpr int16_t kLaneMaxs[kNumLanes] = {
	    1,   // trigger: on
	    24,  // pitch: +24 scale degrees from base
	    4,   // octave: +4 octaves
	    127, // velocity: max
	    102, // gate: 5-100%, 101=TIE, 102=LEGATO
	    12,  // interval: +12 scale degrees (or semitones when chromatic)
	    8,   // retrigger: max subdivisions
	    100, // probability: 100%
	    1,   // glide: on
	};

	/// Musically-useful randomization ranges (tighter than absolute min/max)
	static constexpr int16_t kLaneRndMins[kNumLanes] = {
	    0,  // trigger: off
	    -7, // pitch: -7 scale degrees from base (1 octave in 7-note scale)
	    -2, // octave: -2 octaves
	    60, // velocity: moderate low
	    60, // gate: biased toward longer notes
	    -3, // interval: -3 scale degrees
	    0,  // retrigger: off
	    50, // probability: 50% floor
	    0,  // glide: off
	};

	static constexpr int16_t kLaneRndMaxs[kNumLanes] = {
	    1,   // trigger: on
	    12,  // pitch: one octave up from base
	    2,   // octave: +2 octaves
	    120, // velocity: near max
	    100, // gate: up to 100%
	    3,   // interval: +3 scale degrees
	    4,   // retrigger: moderate subdivisions
	    100, // probability: 100%
	    1,   // glide: on
	};

	/// Which lanes support per-step probability (vert encoder edits prob%)
	/// PITCH=1, OCTAVE=2, VELOCITY=3, INTERVAL=5, RETRIG=6
	static constexpr bool kLaneHasStepProb[kNumLanes] = {
	    false, // trigger
	    true,  // pitch
	    true,  // octave
	    true,  // velocity
	    false, // gate
	    true,  // interval
	    true,  // retrigger
	    false, // probability (IS the probability)
	    false, // glide
	};

	/// Which lanes are binary (tap toggle only, no knob editing)
	static constexpr bool kLaneIsBinary[kNumLanes] = {
	    true, // trigger (but has euclidean)
	    false, false, false, false, false, false, false,
	    true, // glide
	};

	/// Maximum clock division per lane
	static constexpr uint8_t kMaxClockDivision = 32;

	LanesEngine();

	// The 9 parameter lanes
	LanesLane trigger;
	LanesLane pitch;
	LanesLane interval;
	LanesLane velocity;
	LanesLane octave;
	LanesLane gate;
	LanesLane retrigger;
	LanesLane probability;
	LanesLane glide;

	/// Returns pointer to lane at given index (0=trigger, 1=pitch, ..., 8=glide), or nullptr
	LanesLane* getLane(int32_t index);

	/// Base note for the pitch lane (MIDI note number). Pitch lane values are offsets from this.
	int16_t baseNote{60}; // C4

	// Interval accumulation state (wraps between min/max)
	int32_t intervalAccumulator{0};
	int32_t intervalMin{-7};
	int32_t intervalMax{7};
	bool intervalScaleAware{true}; // true = scale degrees, false = chromatic semitones

	// Default values for disabled lanes (velocity is settable from instrument)
	uint8_t defaultVelocity{100};
	static constexpr uint8_t kDefaultGate = 75; // 75% of step length

	/// Track length: all lanes restart after this many ticks. 0 = off (free-running).
	uint16_t trackLength{0};
	uint16_t globalTickCounter{0};

	/// Steps the engine forward by one tick. All lanes advance independently every tick.
	/// Returns the composed note, or a rest if trigger is 0 or probability fails.
	LanesNote step(const MusicalKey& key);

	/// Resets all lane positions, interval accumulator, and global tick counter.
	void reset();

	/// Sets the RNG seed for deterministic testing.
	void setSeed(uint32_t seed);

	/// Loads a default polymetric pattern for hardware testing.
	void loadDefaultPattern();

	bool locked{false};
	uint32_t lockedSeed{0};

	void lock();
	void unlock();

	/// Returns a random value in [minVal, maxVal] (inclusive).
	int16_t randomInRange(int16_t minVal, int16_t maxVal);

	/// Generates a euclidean pattern on the trigger lane.
	/// Distributes `pulses` triggers across `length` steps using a Bresenham accumulator.
	void generateEuclidean(int32_t length, int32_t pulses);

	/// Rotates the trigger lane pattern by `offset` steps.
	void rotateTriggerPattern(int32_t offset);

	/// Clears an entire lane: sets all step values to 0 and resets per-step probability to 100%.
	void clearLane(int32_t laneIdx);

	/// Randomizes all active steps in a lane within its min/max range.
	void randomizeLane(int32_t laneIdx);

private:
	uint32_t rngState_{1};
	uint8_t randomPercent();
	/// Reads a lane's current value, applying per-step probability.
	/// Returns defaultVal if the lane is disabled or the step probability check fails.
	int16_t readLaneValue(LanesLane& lane, int32_t laneIdx, int16_t defaultVal);
	/// Advances all 9 lanes by one tick (called every step, regardless of trigger).
	void advanceAllLanes();
};
