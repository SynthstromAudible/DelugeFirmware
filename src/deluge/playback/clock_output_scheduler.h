#pragma once
#include <cstdint>

/// Result of computing the next clock output scheduling decision.
/// Invariant: shouldResync and shouldSchedule are mutually exclusive.
struct ClockSchedulingResult {
	bool shouldEmitTick{false};
	bool shouldResync{false};
	bool shouldSchedule{false};
	uint32_t scheduledTime{0};
};

/// Pure function that computes the external clock scheduling decision.
/// Used by both trigger (gate) clock and MIDI clock output paths.
///
/// When shouldEmitTick is true, the function has already accounted for the
/// post-emit counter increment internally (uses lastTickDone + 2 for the
/// "tick after next" check).
ClockSchedulingResult computeExternalClockSchedule(int64_t lastTickDone, int64_t lastInputTickReceived,
                                                   uint32_t internalTicksPer, uint32_t outTicksPer,
                                                   uint32_t inputTicksPer, uint32_t internalTicksPerInput,
                                                   uint32_t timePerInputTickMovingAverage, uint32_t inputTickTime);
