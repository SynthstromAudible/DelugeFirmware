#include "playback/clock_output_scheduler.h"

ClockSchedulingResult computeExternalClockSchedule(int64_t lastTickDone, int64_t lastInputTickReceived,
                                                   uint32_t internalTicksPer, uint32_t outTicksPer,
                                                   uint32_t inputTicksPer, uint32_t internalTicksPerInput,
                                                   uint32_t timePerInputTickMovingAverage, uint32_t inputTickTime) {
	ClockSchedulingResult result;

	int64_t currentInternalTick = lastInputTickReceived * internalTicksPerInput / inputTicksPer;

	int64_t nextOutTick = lastTickDone + 1;
	int64_t internalTickForNextOut = nextOutTick * internalTicksPer / outTicksPer;

	bool shouldEmit = (internalTickForNextOut <= currentInternalTick);

	if (shouldEmit) {
		// Look one tick past the one we're about to emit (see header comment for + 2 rationale).
		int64_t tickAfterEmit = lastTickDone + 2;
		int64_t internalTickForTickAfterEmit = tickAfterEmit * internalTicksPer / outTicksPer;

		if (internalTickForTickAfterEmit < currentInternalTick) {
			result.action = ClockScheduleAction::EmitAndResync;
			return result;
		}

		// Update for scheduling calculation below.
		nextOutTick = tickAfterEmit;
		internalTickForNextOut = internalTickForTickAfterEmit;
	}

	int64_t internalTicksUntilNext = internalTickForNextOut - currentInternalTick;

	if (internalTicksUntilNext >= 0 && timePerInputTickMovingAverage > 0) {
		uint32_t timeUntilNext = (uint32_t)((uint64_t)internalTicksUntilNext * timePerInputTickMovingAverage
		                                    * inputTicksPer / internalTicksPerInput);
		result.action = shouldEmit ? ClockScheduleAction::EmitAndSchedule : ClockScheduleAction::Schedule;
		result.scheduledTime = inputTickTime + timeUntilNext;
	}
	else if (shouldEmit) {
		// Emitted a tick but can't compute a schedule (e.g. zero tempo) -- treat as resync.
		result.action = ClockScheduleAction::EmitAndResync;
	}

	return result;
}
