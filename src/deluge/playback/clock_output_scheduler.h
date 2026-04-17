#pragma once
#include <cstdint>

/// The four possible outcomes of the external clock scheduling decision.
enum class ClockScheduleAction : uint8_t {
	None,            ///< No action needed (e.g. tempo not yet established)
	Schedule,        ///< Schedule next tick at scheduledTime; nothing to emit now
	EmitAndSchedule, ///< Emit one immediate tick, then schedule the following tick at scheduledTime
	EmitAndResync,   ///< Emit one immediate tick, then resync (too far behind to schedule)
};

struct ClockSchedulingResult {
	ClockScheduleAction action{ClockScheduleAction::None};
	uint32_t scheduledTime{0};
};

/// Pure function that computes the external clock scheduling decision.
/// Used by both trigger (gate) clock and MIDI clock output paths.
///
/// After deciding to emit a tick, the function looks one tick further ahead
/// (lastTickDone + 2, because the caller will increment lastTickDone by 1 for
/// the tick we just emitted, making the *next* tick lastTickDone + 1 + 1 = + 2)
/// to decide whether the following tick can be scheduled normally or whether
/// we're too far behind and need to resync.
ClockSchedulingResult computeExternalClockSchedule(int64_t lastTickDone, int64_t lastInputTickReceived,
                                                   uint32_t internalTicksPer, uint32_t outTicksPer,
                                                   uint32_t inputTicksPer, uint32_t internalTicksPerInput,
                                                   uint32_t timePerInputTickMovingAverage, uint32_t inputTickTime);
