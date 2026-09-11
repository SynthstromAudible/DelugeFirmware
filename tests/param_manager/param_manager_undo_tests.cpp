#include "model/consequence/consequence_param_change.h"
#include "modulation/params/param_collection.h"
#include <cstdio>
#include <cstdlib>

Consequence::Consequence() : next(nullptr), type(0) {
}
Consequence::~Consequence() = default;

void check(bool condition, const char* message) {
	if (!condition) {
		std::fprintf(stderr, "%s\n", message);
		std::abort();
	}
}

void fill(AutoParam& param, int value) {
	param.currentValue = value;
	param.nodes.size = 3;
	param.nodes.buffer = std::make_unique<int[]>(3);
	for (int index = 0; index < 3; ++index) {
		param.nodes.buffer[index] = value + index;
	}
}

int main() {
	for (bool steal : {false, true}) {
		for (bool destroyWhileUndone : {false, true}) {
			AutoParam live;
			fill(live, 10);
			ParamCollection collection{&live, 37};
			ModelStackWithAutoParam stack{{&collection, 37}, &live};
			auto* original = live.nodes.buffer.get();
			{
				ConsequenceParamChange backup(&stack, steal);
				check(backup.type == Consequence::PARAM_CHANGE, "Backup must be registered as a parameter change");
				check(backup.state.value == 10 && backup.state.nodes.buffer[2] == 12,
				      "Backup must preserve value and node contents");
				check((backup.state.nodes.buffer.get() == original) == steal,
				      "Steal must transfer ownership; clone must allocate independent storage");
				check(bool(live.nodes.buffer) != steal, "Only stealing may empty live storage");
				fill(live, 20);
				auto* edited = live.nodes.buffer.get();
				auto* saved = backup.state.nodes.buffer.get();
				for (int cycle = 0; cycle < 100; ++cycle) {
					check(backup.revert(TimeType::BEFORE, nullptr) == Error::NONE, "Undo must succeed");
					check(live.currentValue == 10 && live.nodes.buffer.get() == saved && live.nodes.buffer[2] == 12,
					      "Undo must restore saved values and storage identity");
					check(backup.state.nodes.buffer.get() == edited && backup.state.value == 20,
					      "Undo must retain edited state for redo");
					check(backup.revert(TimeType::AFTER, nullptr) == Error::NONE, "Redo must succeed");
					check(live.currentValue == 20 && live.nodes.buffer.get() == edited && live.nodes.buffer[2] == 22,
					      "Redo must restore edited values without cloning storage");
				}
				if (destroyWhileUndone) {
					backup.revert(TimeType::BEFORE, nullptr);
				}
			}
			check(live.nodes.buffer[2] == (destroyWhileUndone ? 12 : 22),
			      "Discarding history must not invalidate live nodes");
			live.nodes.buffer[0] = 99;
		}
	}
	std::puts("Parameter backup/undo integration regressions passed");
}