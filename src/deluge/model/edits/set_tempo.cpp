#include "model/edits/set_tempo.h"

#include "deluge-project/song.h"
#include "memory/heaps.h" // alloc_sdram
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/consequence/consequence_tempo_change.h"
#include "model/song/song.h"

namespace deluge::edits {

Error SetTempo::apply(Context& ctx) {
	Action* action = actionLogger.getNewAction(ActionType::TEMPO_CHANGE, ActionAddition::NOT_ALLOWED);

	uint64_t timePerBigBefore = ctx.model.transport.samples_per_timer_tick_big;

	// shouldLogAction=false: setTempoFromNumSamples' own logging does "all this other stuff"; we record the plain
	// Consequence ourselves, verbatim from the call site.
	ctx.song.setTempoFromNumSamples(timePerTick_, false);

	if (action != nullptr) {
		void* consMemory = deluge::memory::alloc_sdram(sizeof(ConsequenceTempoChange));
		if (consMemory != nullptr) {
			auto* consequence = new (consMemory)
			    ConsequenceTempoChange(timePerBigBefore, ctx.model.transport.samples_per_timer_tick_big);
			action->addConsequence(consequence);
		}
	}
	return Error::NONE;
}

} // namespace deluge::edits
