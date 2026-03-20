#include "playback/clock_output_scheduler.h"

ClockSchedulingResult computeExternalClockSchedule(int64_t lastTickDone, int64_t lastInputTickReceived,
                                                   uint32_t internalTicksPer, uint32_t outTicksPer,
                                                   uint32_t inputTicksPer, uint32_t internalTicksPerInput,
                                                   uint32_t timePerInputTickMovingAverage, uint32_t inputTickTime) {
	ClockSchedulingResult result;

	int64_t currentInternalTick = lastInputTickReceived * internalTicksPerInput / inputTicksPer;

	int64_t nextOutTick = lastTickDone + 1;
	int64_t internalTickForNextOut = nextOutTick * internalTicksPer / outTicksPer;

	// If behind (or exactly on boundary), emit one immediate tick.
	if (internalTickForNextOut <= currentInternalTick) {
		result.shouldEmitTick = true;

		// Simulate post-emit state: the caller will increment lastTickDone by 1.
		int64_t tickAfterEmit = lastTickDone + 2;
		int64_t internalTickForTickAfterEmit = tickAfterEmit * internalTicksPer / outTicksPer;

		// If strictly behind after emitting one, resync. Exactly on the boundary means the
		// tick is due now — schedule it for the ISR to fire with proper temporal spacing.
		if (internalTickForTickAfterEmit < currentInternalTick) {
			result.shouldResync = true;
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
		result.shouldSchedule = true;
		result.scheduledTime = inputTickTime + timeUntilNext;
	}

	return result;
}
