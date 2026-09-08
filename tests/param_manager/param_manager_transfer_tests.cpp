#include "memory/general_memory_allocator.h"
#include "mocks/modulation/params/param_collection.h"
#include "modulation/params/param_manager.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>
#include <unordered_set>

using Kind = deluge::modulation::params::Kind;
static std::unordered_set<void*> allocations;
static int failAfter = -1;

void check(bool condition, const char* message) {
	if (!condition) {
		std::fprintf(stderr, "%s\n", message);
		std::abort();
	}
}

GeneralMemoryAllocator& GeneralMemoryAllocator::get() {
	static GeneralMemoryAllocator allocator;
	return allocator;
}

void* GeneralMemoryAllocator::allocMaxSpeed(uint32_t size) {
	if (failAfter == 0) {
		return nullptr;
	}
	if (failAfter > 0) {
		--failAfter;
	}
	void* memory = std::malloc(size);
	check(memory != nullptr, "Host allocation failed");
	allocations.insert(memory);
	return memory;
}

void delugeDealloc(void* address) {
	check(allocations.erase(address) == 1, "Free must release an owned allocation exactly once");
	std::free(address);
}

extern "C" void freezeWithError(const char* code) {
	throw code;
}

ParamManager::ParamManager()
    : resonanceBackwardsCompatibilityProcessed(false), expressionParamSetOffset(0), summaries{} {
}
ParamManager::~ParamManager() {
	destructAndForgetParamCollections();
}
void populate(ParamManager& manager, Kind kind, bool expression) {
	auto add = [&](int slot, Kind collectionKind) {
		manager.summaries[slot].paramCollection =
		    new (GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(ParamCollection))) ParamCollection(collectionKind);
		manager.summaries[slot].whichParamsAreAutomated[0] = 42;
		manager.summaries[slot].whichParamsAreInterpolating[0] = 17;
	};
	if (kind == Kind::UNPATCHED_SOUND) {
		add(0, kind);
		add(1, Kind::PATCHED);
		add(2, Kind::PATCH_CABLE);
		manager.expressionParamSetOffset = 3;
	}
	else if (kind != Kind::NONE) {
		add(0, kind);
		manager.expressionParamSetOffset = 1;
	}
	if (expression) {
		add(manager.expressionParamSetOffset, Kind::EXPRESSION);
	}
}

void check_unchanged(ParamManager& manager, const ParamManager& snapshot) {
	check(manager.expressionParamSetOffset == snapshot.expressionParamSetOffset, "Offset must remain unchanged");
	for (int slot = 0; slot < PARAM_COLLECTIONS_STORAGE_NUM; ++slot) {
		check(manager.summaries[slot].paramCollection == snapshot.summaries[slot].paramCollection,
		      "Collection ownership must remain unchanged");
		check(std::memcmp(manager.summaries[slot].whichParamsAreAutomated,
		                  snapshot.summaries[slot].whichParamsAreAutomated,
		                  sizeof(manager.summaries[slot].whichParamsAreAutomated))
		          == 0,
		      "Automation flags must remain unchanged");
	}
}

int main() {
	{
		ParamManagerForTimeline timeline;
		ParamManager* base = &timeline;
		check(base->toForTimeline() == &timeline, "Timeline conversion must dispatch through a base pointer");
		check(timeline.ticksSkipped == 0 && timeline.ticksTilNextEvent == 0,
		      "Timeline counters must initialize to zero");
#if ALPHA_OR_BETA_VERSION
		ParamManager plain;
		bool froze = false;
		try {
			plain.toForTimeline();
		} catch (const char* code) {
			froze = std::strcmp(code, "PM09") == 0;
		}
		check(froze, "Plain managers must reject timeline conversion with PM09");
#endif
	}
	for (Kind kind : {Kind::NONE, Kind::MIDI, Kind::UNPATCHED_GLOBAL, Kind::UNPATCHED_SOUND}) {
		for (bool sourceExpression : {false, true}) {
			for (bool destinationExpression : {false, true}) {
				for (bool transferExpression : {false, true}) {
					for (bool steal : {false, true}) {
						ParamManager source, destination;
						populate(source, kind, sourceExpression);
						populate(destination, Kind::NONE, destinationExpression);
						auto* originalExpression = destination.getExpressionParamSetSummary()->paramCollection;
						auto* sourceExpressionPointer = source.getExpressionParamSetSummary()->paramCollection;
						auto* sourceMain = source.summaries[0].paramCollection;
						int offset = source.expressionParamSetOffset;
						if (steal) {
							destination.stealParamCollectionsFrom(&source, transferExpression);
							check(source.expressionParamSetOffset == 0, "Steal must empty source main collections");
							check(source.getExpressionParamSetSummary()->paramCollection
							          == (transferExpression ? nullptr : sourceExpressionPointer),
							      "Steal must honor source expression ownership");
						}
						else {
							check(destination.cloneParamCollectionsFrom(&source, true, transferExpression, 96)
							          == Error::NONE,
							      "Clone must succeed");
							check(source.summaries[0].paramCollection == sourceMain,
							      "Clone must preserve source ownership");
						}
						check(source.has_valid_layout() && destination.has_valid_layout(),
						      "Transfers must yield valid layouts");
						check(destination.expressionParamSetOffset == offset,
						      "Transfer must retain source layout type");
						auto* resultingExpression = destination.getExpressionParamSetSummary()->paramCollection;
						check(bool(resultingExpression)
						          == (destinationExpression || (sourceExpression && transferExpression)),
						      "Expression presence must follow transfer policy");
						if (originalExpression) {
							check(resultingExpression == originalExpression,
							      "Destination expression must take precedence");
						}
						if (offset > 0) {
							check((destination.summaries[0].paramCollection == sourceMain) == steal,
							      "Clone must allocate independently; steal must preserve identity");
							if (!steal) {
								check(destination.summaries[0].paramCollection->clonedAutomation
								          && destination.summaries[0].paramCollection->clonedReverseLength == 96,
								      "Clone must forward automation and reversal options");
							}
						}
					}
					check(allocations.empty(), "Transfer must not leak collections");
				}
			}
		}
	}
	for (int failure = 0; failure < 4; ++failure) {
		for (bool shallow : {false, true}) {
			ParamManager source, destination;
			populate(source, Kind::UNPATCHED_SOUND, true);
			if (shallow) {
				destination = source;
			}
			else {
				populate(destination, Kind::MIDI, false);
			}
			ParamManager snapshot = destination;
			auto before = allocations.size();
			failAfter = failure;
			Error result =
			    shallow ? destination.beenCloned() : destination.cloneParamCollectionsFrom(&source, true, true);
			failAfter = -1;
			check(result == Error::INSUFFICIENT_RAM, "Each allocation failure must propagate");
			check(allocations.size() == before, "Failed clone must release temporary allocations only");
			if (shallow) {
				check(destination.has_valid_layout() && !destination.matches_type(ParamManagerType::ANY),
				      "Failed shallow clone must relinquish all borrowed pointers");
				check_unchanged(source, snapshot);
			}
			else {
				check_unchanged(destination, snapshot);
			}
			for (auto& summary : snapshot.summaries) {
				summary = {};
			}
			snapshot.expressionParamSetOffset = 0;
		}
		check(allocations.empty(), "Failure cleanup must not leak or double-free source collections");
	}
	{
		ParamManager source;
		populate(source, Kind::UNPATCHED_SOUND, true);
		ParamManager shallow = source;
		check(shallow.beenCloned(96) == Error::NONE, "Shallow clone must succeed");
		for (int slot = 0; slot < 4; ++slot) {
			check(shallow.summaries[slot].paramCollection != source.summaries[slot].paramCollection,
			      "Successful shallow clone must own independent collections including expression");
			check(shallow.summaries[slot].whichParamsAreAutomated[0] == 42
			          && shallow.summaries[slot].whichParamsAreInterpolating[0] == 17,
			      "Successful clone must preserve summary flags");
		}
		check(shallow.has_valid_layout(), "Successful shallow clone must retain a valid layout");
	}
	check(allocations.empty(), "Successful shallow clone must release its independent allocations");
	std::puts("Param manager transfer regressions passed");
}