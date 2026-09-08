#include "memory/general_memory_allocator.h"
#include "mocks/modulation/params/param_collection.h"
#include "modulation/params/param_manager.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>
#include <random>
#include <unordered_set>

using Kind = deluge::modulation::params::Kind;
static std::unordered_set<void*> allocations;
static int failAfter = -1;
static uint32_t sequenceSeed = 0;
static int sequenceStep = -1;

void check(bool condition, const char* message) {
	if (!condition) {
		std::fprintf(stderr, "%s\n", message);
		std::fprintf(stderr, "seed=%u step=%d\n", sequenceSeed, sequenceStep);
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
		check(std::memcmp(manager.summaries[slot].whichParamsAreInterpolating,
		                  snapshot.summaries[slot].whichParamsAreInterpolating,
		                  sizeof(manager.summaries[slot].whichParamsAreInterpolating))
		          == 0,
		      "Interpolation flags must remain unchanged");
	}
}

int main() {
	for (bool steal : {false, true}) {
		for (bool sourceExpression : {false, true}) {
			for (bool destinationExpression : {false, true}) {
				for (bool transferExpression : {false, true}) {
					for (Kind kind : {Kind::MIDI, Kind::UNPATCHED_GLOBAL, Kind::UNPATCHED_SOUND}) {
						{
							ParamManager source, destination;
							populate(source, Kind::NONE, sourceExpression);
							populate(destination, kind, destinationExpression);
							auto* retained = destination.getExpressionParamSet();
							auto* original = source.getExpressionParamSet();
							if (steal) {
								destination.stealParamCollectionsFrom(&source, transferExpression);
							}
							else {
								check(destination.cloneParamCollectionsFrom(&source, true, transferExpression)
								          == Error::NONE,
								      "Empty-main source clone must succeed");
							}
							check(destination.matches_type(ParamManagerType::NONE) && source.has_valid_layout(),
							      "Empty-main source must remove destination main collections");
							auto* result = destination.getExpressionParamSet();
							check(bool(result) == (destinationExpression || (sourceExpression && transferExpression)),
							      "Empty-source transfer must honor expression policy");
							if (retained) {
								check(result == retained, "Destination expression takes precedence");
							}
							check(source.getExpressionParamSet()
							          == ((steal && transferExpression) ? nullptr : original),
							      "Source expression ownership must follow transfer policy");
						}
						check(allocations.empty(), "Empty-source transfer must not leak");
					}
				}
			}
		}
	}
	for (uint32_t seed : {1u, 42u, 0xDE1u, 0xC0FFEEu}) {
		sequenceSeed = seed;
		{
			std::mt19937 random(seed);
			std::array<ParamManager, 4> managers;
			for (sequenceStep = 0; sequenceStep < 2000; ++sequenceStep) {
				auto index = random() % managers.size();
				auto& destination = managers[index];
				auto& source = managers[(index + 1 + random() % 3) % managers.size()];
				failAfter = random() % 4 == 0 ? int(random() % 4) : -1;
				switch (random() % 8) {
				case 0:
					destination.setupMIDI();
					break;
				case 1:
					destination.setupUnpatched();
					break;
				case 2:
					destination.setupWithPatching();
					break;
				case 3:
					destination.ensureExpressionParamSetExists();
					break;
				case 4:
					destination.destructMainParamCollections();
					break;
				case 5:
					destination.destructAndForgetParamCollections();
					break;
				case 6:
					destination.cloneParamCollectionsFrom(&source, random() % 2, random() % 2);
					break;
				case 7:
					destination.stealParamCollectionsFrom(&source, random() % 2);
					break;
				}
				failAfter = -1;
				std::unordered_set<void*> owned;
				for (auto& manager : managers) {
					for (auto& summary : manager.summaries) {
						if (summary.paramCollection) {
							check(allocations.contains(summary.paramCollection),
							      "Every pointer must reference live storage");
							check(owned.insert(summary.paramCollection).second,
							      "Collections must have exactly one owner");
						}
					}
					check(manager.has_valid_layout(), "Every operation must leave a valid layout");
				}
				check(owned == allocations, "Every allocation must remain reachable from an owner");
			}
		}
		check(allocations.empty(), "Sequence teardown must release every allocation");
	}
	sequenceStep = -1;
	for (bool steal : {false, true}) {
		for (Kind destinationKind : {Kind::MIDI, Kind::UNPATCHED_GLOBAL, Kind::UNPATCHED_SOUND}) {
			for (bool expression : {false, true}) {
				{
					ParamManager source, destination;
					populate(source, Kind::MIDI, true);
					populate(destination, destinationKind, expression);
					auto* retained = destination.getExpressionParamSet();
					auto before = allocations.size();
					int oldMainCount = destination.expressionParamSetOffset;
					if (steal) {
						destination.stealParamCollectionsFrom(&source, true);
						check(allocations.size() == before - oldMainCount - unsigned(expression),
						      "Steal must free old destination main collections and redundant source expression");
					}
					else {
						check(destination.cloneParamCollectionsFrom(&source, true, true) == Error::NONE,
						      "Clone into populated destination must succeed");
						check(allocations.size() == before - oldMainCount + (expression ? 1 : 2),
						      "Successful clone must release overwritten destination main collections");
					}
					check(destination.matches_type(ParamManagerType::MIDI) && source.has_valid_layout(),
					      "Replacing a destination must leave valid layouts");
					if (expression) {
						check(destination.getExpressionParamSet() == retained,
						      "Replacing destination main collections must preserve its expression");
					}
				}
				check(allocations.empty(), "Populated destination transfers must not leak");
			}
		}
	}
#if ALPHA_OR_BETA_VERSION
	{
		ParamManager source, destination;
		auto expectFreeze = [](auto operation, const char* expected) {
			bool froze = false;
			try {
				operation();
			} catch (const char* code) {
				froze = std::strcmp(code, expected) == 0;
			}
			check(froze, "Invalid transfer must diagnose before touching ownership");
		};
		expectFreeze([&] { destination.stealParamCollectionsFrom(nullptr); }, "PM0B");
		expectFreeze([&] { destination.stealParamCollectionsFrom(&destination); }, "PM0E");
		expectFreeze([&] { destination.cloneParamCollectionsFrom(nullptr, true); }, "PM0F");
		source.expressionParamSetOffset = 255;
		expectFreeze([&] { destination.stealParamCollectionsFrom(&source); }, "PM0E");
		expectFreeze([&] { destination.cloneParamCollectionsFrom(&source, true); }, "PM0F");
		source.expressionParamSetOffset = 0;
		destination.expressionParamSetOffset = 255;
		expectFreeze([&] { destination.stealParamCollectionsFrom(&source); }, "PM0E");
		expectFreeze([&] { destination.cloneParamCollectionsFrom(&source, true); }, "PM0F");
		destination.expressionParamSetOffset = 0;
		check(allocations.empty(), "Rejected transfers must not allocate");
	}
#endif
	using Setup = Error (ParamManager::*)();
	for (Kind initial : {Kind::NONE, Kind::MIDI, Kind::UNPATCHED_GLOBAL, Kind::UNPATCHED_SOUND}) {
		for (bool expression : {false, true}) {
			for (Setup setup :
			     {&ParamManager::setupMIDI, &ParamManager::setupUnpatched, &ParamManager::setupWithPatching}) {
				int allocationCount = setup == &ParamManager::setupWithPatching ? 3 : 1;
				for (int failure = 0; failure < allocationCount; ++failure) {
					ParamManager manager;
					populate(manager, initial, expression);
					ParamManager snapshot = manager;
					auto owned = allocations;
					failAfter = failure;
					check((manager.*setup)() == Error::INSUFFICIENT_RAM, "Setup allocation failure must propagate");
					failAfter = -1;
					check_unchanged(manager, snapshot);
					check(allocations == owned, "Failed setup must retain old allocations and release all temporaries");
					for (auto& summary : snapshot.summaries) {
						summary = {};
					}
					snapshot.expressionParamSetOffset = 0;
				}
				check(allocations.empty(), "Setup failure cases must not leak");
			}
			{
				ParamManager manager;
				populate(manager, initial, expression);
				auto owned = allocations;
				auto* existing = manager.getExpressionParamSet();
				failAfter = 0;
				check(manager.getOrCreateExpressionParamSet(true) == existing,
				      "Expression allocation failure must return null or reuse the existing expression");
				check(allocations == owned, "Expression failure or reuse must not allocate or free");
				failAfter = -1;
				auto* created = manager.getOrCreateExpressionParamSet(true);
				check(created != nullptr && manager.has_valid_layout(),
				      "Expression creation must yield a valid layout");
				owned = allocations;
				check(manager.getOrCreateExpressionParamSet(false) == created && allocations == owned,
				      "Repeated expression creation must preserve identity without allocating");
			}
			check(allocations.empty(), "Expression creation must not leak");
		}
	}
	for (bool expression : {false, true}) {
		{
			ParamManager manager;
			populate(manager, Kind::NONE, expression);
			auto* retained = manager.getExpressionParamSet();
			for (int cycle = 0; cycle < 100; ++cycle) {
				for (Setup setup :
				     {&ParamManager::setupWithPatching, &ParamManager::setupMIDI, &ParamManager::setupUnpatched}) {
					check((manager.*setup)() == Error::NONE, "Repeated setup must succeed");
					check(manager.has_valid_layout() && manager.getExpressionParamSet() == retained,
					      "Type transitions must preserve expression identity and layout validity");
					check(allocations.size() == manager.expressionParamSetOffset + unsigned(expression),
					      "Only current layout collections may remain allocated");
					if (expression) {
						check(manager.getExpressionParamSetSummary()->whichParamsAreAutomated[0] == 42
						          && manager.getExpressionParamSetSummary()->whichParamsAreInterpolating[0] == 17,
						      "Transitions must preserve expression flags");
					}
				}
				manager.destructMainParamCollections();
				check(manager.matches_type(ParamManagerType::CV) && allocations.size() == unsigned(expression),
				      "CV transition must release all main collections");
			}
		}
		check(allocations.empty(), "Repeated transitions and destruction must not leak");
	}
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
