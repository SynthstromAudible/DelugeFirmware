#include "mocks/model/model_stack.h"
#include "mocks/modulation/params/param_collection.h"
#include "modulation/params/param_manager.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

void check(bool condition, const char* message) {
	if (!condition) {
		std::fprintf(stderr, "%s\n", message);
		std::abort();
	}
}

ParamManager::ParamManager()
    : resonanceBackwardsCompatibilityProcessed(false), expressionParamSetOffset(0), summaries{} {
}
ParamManager::~ParamManager() = default;
void ParamManagerForTimeline::ensureSomeParamCollections() {
	check(has_valid_layout() && matches_type(ParamManagerType::ANY), "Fixture must have a valid nonempty layout");
}
extern "C" void freezeWithError(const char* code) {
	throw code;
}

class RecordingCollection : public ParamCollection {
public:
	explicit RecordingCollection(deluge::modulation::params::Kind kind) : ParamCollection(kind) {}
	std::vector<int> calls;
	int tickCount = 0;
	int processTicks = 0;
	int interpolationAfter = -1;
	int value = 0;
	int increment = 2;
	bool reversedSeen = false;
	bool pingpongSeen = false;

	void tickTicks(int32_t ticks, ModelStackWithParamCollection* stack) override {
		check(stack->paramCollectionSummary->paramCollection == this, "Tick must receive its own summary");
		calls.push_back(1);
		tickCount += ticks;
		value += ticks * increment;
	}
	void processCurrentPos(ModelStackWithParamCollection* stack, int32_t ticks, bool reversed, bool pingpong,
	                       bool interpolate) override {
		check(stack->paramCollectionSummary->paramCollection == this, "Processing must receive its own summary");
		check(interpolate, "Node processing must retain interpolation setup");
		calls.push_back(2);
		processTicks = ticks;
		reversedSeen = reversed;
		pingpongSeen = pingpong;
		increment = 100;
		if (interpolationAfter >= 0) {
			stack->paramCollectionSummary->whichParamsAreInterpolating[0] = interpolationAfter;
		}
	}
};

struct Fixture {
	RecordingCollection midi{deluge::modulation::params::Kind::MIDI};
	RecordingCollection expression{deluge::modulation::params::Kind::EXPRESSION};
	ParamManagerForTimeline manager;
	ModelStackWithThreeMainThings stack;
	Fixture() {
		manager.expressionParamSetOffset = 1;
		manager.summaries[0].paramCollection = &midi;
		manager.summaries[1].paramCollection = &expression;
		manager.summaries[0].whichParamsAreAutomated[0] = 1;
		midi.ticksTilNextEvent = 12;
		expression.ticksTilNextEvent = 5;
	}
	void process(int ticks, bool sampleInterpolation = false) {
		manager.processCurrentPos(&stack, ticks, true, true, sampleInterpolation);
	}
};

int main() {
	{
		Fixture fixture;
		fixture.manager.ticksTilNextEvent = 5;
		fixture.process(2);
		check(fixture.midi.calls.empty() && fixture.manager.ticksSkipped == 2 && fixture.manager.ticksTilNextEvent == 3,
		      "Ticks must accumulate until an event is due");
		fixture.process(3);
		check(fixture.midi.processTicks == 5 && fixture.manager.ticksSkipped == 0,
		      "Due event must receive accumulated ticks once");
		check(fixture.manager.ticksTilNextEvent == 12, "Non-interpolating automation must use collection deadline");
		check(fixture.expression.calls.empty(), "Non-automated collections must not process");
		check(fixture.midi.reversedSeen && fixture.midi.pingpongSeen, "Direction and pingpong must be forwarded");
	}
	{
		Fixture fixture;
		fixture.manager.summaries[0].whichParamsAreInterpolating[0] = 1;
		fixture.process(4);
		check(fixture.midi.calls == std::vector<int>({1, 2}), "Tick interpolation must precede node processing");
		check(fixture.midi.value == 8, "Skipped ticks must use the previous segment increment");
		check(fixture.manager.ticksTilNextEvent == 0, "Active tick interpolation must request the next tick");
	}
	{
		Fixture fixture;
		fixture.midi.interpolationAfter = 1;
		fixture.process(4);
		check(fixture.midi.calls == std::vector<int>({2}), "New interpolation must not advance retroactively");
		check(fixture.manager.ticksTilNextEvent == 0, "New interpolation must schedule the next tick");
		fixture.process(1);
		check(fixture.midi.tickCount == 1, "New segment must advance by only the next elapsed tick");
	}
	{
		Fixture fixture;
		fixture.manager.summaries[0].whichParamsAreInterpolating[0] = 1;
		fixture.midi.interpolationAfter = 0;
		fixture.process(3);
		check(fixture.midi.tickCount == 3 && fixture.manager.ticksTilNextEvent == 12,
		      "Finishing interpolation must advance old segment then restore normal scheduling");
	}
	{
		Fixture fixture;
		fixture.manager.summaries[0].whichParamsAreInterpolating[0] = 1;
		fixture.process(4, true);
		check(fixture.midi.calls == std::vector<int>({2}) && fixture.manager.ticksTilNextEvent == 12,
		      "Sample interpolation must not also advance by ticks or force per-tick scheduling");
	}
	{
		Fixture fixture;
		fixture.manager.summaries[1].whichParamsAreAutomated[1] = 1;
		fixture.process(2);
		check(fixture.expression.processTicks == 2 && fixture.manager.ticksTilNextEvent == 5,
		      "Automation in higher flag words must participate in earliest-event scheduling");
	}
	std::puts("Param manager automation regressions passed");
}