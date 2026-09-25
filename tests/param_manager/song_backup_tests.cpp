#include "memory/general_memory_allocator.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/song/song.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <set>
#include <unordered_set>

static std::unordered_set<void*> allocations;
static uint32_t lifecycleSeed = 0;
static int lifecycleStep = -1;
static int lifecycleOperation = -1;
void check(bool condition, const char* message) {
	if (!condition) {
		std::fprintf(stderr, "%s\n", message);
		std::fprintf(stderr, "seed=%u step=%d operation=%d\n", lifecycleSeed, lifecycleStep, lifecycleOperation);
		std::exit(EXIT_FAILURE);
	}
}
GeneralMemoryAllocator& GeneralMemoryAllocator::get() {
	static GeneralMemoryAllocator allocator;
	return allocator;
}
void* GeneralMemoryAllocator::allocMaxSpeed(uint32_t size) {
	void* memory = std::malloc(size);
	check(memory != nullptr, "Host allocation failed");
	allocations.insert(memory);
	return memory;
}
void delugeDealloc(void* memory) {
	check(allocations.erase(memory) == 1, "Collection must be freed exactly once");
	std::free(memory);
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

void populate(ParamManagerForTimeline& manager, bool expression = true) {
	check(manager.setupUnpatched() == Error::NONE, "Global setup must succeed");
	if (expression) {
		check(manager.ensureExpressionParamSetExists(), "Expression setup must succeed");
		manager.getExpressionParamSetSummary()->whichParamsAreAutomated[0] = 42;
	}
}

int main() {
	for (uint32_t seed : {1u, 42u, 0xDE1u, 0xC0FFEEu}) {
		lifecycleSeed = seed;
		{
			Song song;
			ModControllableAudio owners[3];
			Clip clips[5];
			std::array<ParamManagerForTimeline, 4> live;
			std::mt19937 random(seed);
			for (lifecycleStep = 0; lifecycleStep < 2000; ++lifecycleStep) {
				auto* owner = &owners[random() % 3];
				auto clipIndex = random() % 6;
				auto* clip = clipIndex == 5 ? nullptr : &clips[clipIndex];
				auto& manager = live[random() % live.size()];
				lifecycleOperation = random() % 7;
				song.backedUpParamManagers.failInsertion = random() % 4 == 0;
				switch (lifecycleOperation) {
				case 0:
				case 1:
					populate(manager, random() % 2);
					song.backUpParamManager(owner, clip, &manager, random() % 2);
					break;
				case 2:
				case 3: {
					BackedUpParamManager* expected = nullptr;
					for (void* memory : song.backedUpParamManagers.entries) {
						auto* entry = static_cast<BackedUpParamManager*>(memory);
						if (entry->modControllable != owner) {
							continue;
						}
						if (entry->clip == clip) {
							expected = entry;
							break;
						}
						if (lifecycleOperation == 3 && (!expected || !entry->clip)) {
							expected = entry;
						}
					}
					auto* expectedMain = expected ? expected->paramManager.summaries[0].paramCollection : nullptr;
					auto* previousMain = manager.summaries[0].paramCollection;
					auto* expression = manager.getExpressionParamSet();
					auto ownedBefore = allocations;
					int entriesBefore = song.backedUpParamManagers.getNumElements();
					auto* restored = lifecycleOperation == 2
					                     ? song.getBackedUpParamManagerForExactClip(owner, clip, &manager)
					                     : song.getBackedUpParamManagerPreferablyWithClip(owner, clip, &manager);
					check(bool(restored) == bool(expected), "Restore must agree with independent selection");
					if (restored) {
						check(restored == &manager && manager.summaries[0].paramCollection == expectedMain,
						      "Restore must transfer the selected backup's main collection");
						check(song.backedUpParamManagers.getNumElements() == entriesBefore - 1,
						      "Restore must consume exactly one entry");
						if (expression) {
							check(manager.getExpressionParamSet() == expression,
							      "Restore must preserve live expression");
						}
					}
					else {
						check(allocations == ownedBefore && manager.summaries[0].paramCollection == previousMain
						          && manager.getExpressionParamSet() == expression,
						      "Missing backup must leave live ownership unchanged");
					}
					break;
				}
				case 4:
					if (clip) {
						song.deleteBackedUpParamManagersForClip(clip);
						for (void* memory : song.backedUpParamManagers.entries) {
							check(static_cast<BackedUpParamManager*>(memory)->clip != clip,
							      "Clip deletion must remove every reference to that clip");
						}
					}
					break;
				case 5:
					song.deleteBackedUpParamManagersForModControllable(owner);
					for (void* memory : song.backedUpParamManagers.entries) {
						check(static_cast<BackedUpParamManager*>(memory)->modControllable != owner,
						      "Output cleanup must remove every backup for that output");
					}
					break;
				case 6:
					manager.destructAndForgetParamCollections();
					break;
				}
				song.backedUpParamManagers.failInsertion = false;
				std::unordered_set<void*> reachable;
				auto audit = [&](ParamManager& checked) {
					for (auto& summary : checked.summaries) {
						if (summary.paramCollection) {
							check(allocations.contains(summary.paramCollection),
							      "Every collection pointer must be live");
							check(reachable.insert(summary.paramCollection).second,
							      "Every collection must have one owner");
						}
					}
					check(checked.has_valid_layout(), "Every manager must retain a valid layout");
				};
				for (auto& checked : live) {
					audit(checked);
				}
				std::set<std::pair<uint32_t, uint32_t>> keys;
				std::pair<uint32_t, uint32_t> previous{};
				for (void* memory : song.backedUpParamManagers.entries) {
					auto* entry = static_cast<BackedUpParamManager*>(memory);
					audit(entry->paramManager);
					auto key = std::make_pair(BackupTable::key(entry->modControllable), BackupTable::key(entry->clip));
					check(keys.insert(key).second && key >= previous, "Backup keys must remain unique and sorted");
					previous = key;
				}
				check(reachable == allocations, "Every allocated collection must remain reachable");
			}
		}
		check(allocations.empty(), "Lifecycle teardown must release all live and backed-up collections");
	}
	lifecycleStep = -1;
	lifecycleOperation = -1;
	{
		Song song;
		ModControllableAudio owner;
		Clip clip;
		ParamManagerForTimeline valid, incompatible;
		populate(valid);
		song.backUpParamManager(&owner, &clip, &valid, true);
		auto* backup = song.getBackedUpParamManagerForExactClip(&owner, &clip);
		auto* savedMain = backup->summaries[0].paramCollection;
		check(incompatible.setupMIDI() == Error::NONE, "Create incompatible incoming backup");
		auto* incoming = incompatible.summaries[0].paramCollection;
		auto owned = allocations;
#if ALPHA_OR_BETA_VERSION
		bool froze = false;
		try {
			song.backUpParamManager(&owner, &clip, &incompatible, true);
		} catch (const char* code) {
			froze = std::strcmp(code, "PM10") == 0;
		}
		check(froze, "Diagnostics must flag an incompatible incoming backup");
#else
		song.backUpParamManager(&owner, &clip, &incompatible, true);
#endif
		check(allocations == owned && incompatible.summaries[0].paramCollection == incoming,
		      "Rejected backup must preserve incoming ownership in both diagnostics modes");
		check(song.getBackedUpParamManagerForExactClip(&owner, &clip) == backup
		          && backup->summaries[0].paramCollection == savedMain,
		      "Rejected backup must not replace the existing compatible backup");
	}
	check(allocations.empty(), "Rejected backup teardown must release all allocations");
	{
		Song song;
		ModControllableAudio owners[3];
		Clip clips[2];
		ParamManagerForTimeline fallback, exact, unrelated, destination;
		populate(fallback);
		populate(exact);
		populate(unrelated);
		populate(destination);
		song.backUpParamManager(&owners[0], nullptr, &fallback, true);
		song.backUpParamManager(&owners[0], &clips[0], &exact, true);
		song.backUpParamManager(&owners[1], &clips[0], &unrelated, true);
		auto* preferred = song.getBackedUpParamManagerForExactClip(&owners[0], &clips[0]);
		auto* nullBackup = song.getBackedUpParamManagerForExactClip(&owners[0], nullptr);
		check(song.getBackedUpParamManagerPreferablyWithClip(&owners[0], &clips[0]) == preferred,
		      "Compatible exact match must beat null fallback");
		check(preferred->setupMIDI() == Error::NONE, "Create incompatible backup fixture");
		auto owned = allocations;
		auto* destinationMain = destination.summaries[0].paramCollection;
		check(!song.getBackedUpParamManagerForExactClip(&owners[0], &clips[0], &destination),
		      "Exact lookup must reject incompatible backup");
		check(allocations == owned && destination.summaries[0].paramCollection == destinationMain,
		      "Rejected restore must leave destination untouched");
		check(song.getBackedUpParamManagerPreferablyWithClip(&owners[0], &clips[0]) == nullBackup,
		      "Incompatible exact match must not mask compatible fallback");
		check(nullBackup->setupMIDI() == Error::NONE, "Make all owner backups incompatible");
		check(!song.getBackedUpParamManagerPreferablyWithClip(&owners[0], &clips[0]),
		      "Lookup must not cross into another output's compatible backups");
		check(!song.getBackedUpParamManagerPreferablyWithClip(&owners[2], &clips[0]),
		      "Missing output must not borrow another output's backup");
		song.deleteBackedUpParamManagersForModControllable(&owners[0]);
		check(song.getBackedUpParamManagerForExactClip(&owners[1], &clips[0]) != nullptr,
		      "Deleting one output's backups must preserve another output");
	}
	check(allocations.empty(), "Selection cases must release all collections");
	{
		Song song;
		ModControllableAudio owner;
		Clip clips[2];
		ParamManagerForTimeline first, removed;
		populate(first);
		populate(removed);
		song.backUpParamManager(&owner, &clips[0], &first, true);
		song.backUpParamManager(&owner, &clips[1], &removed, true);
		auto* survivor = song.getBackedUpParamManagerForExactClip(&owner, &clips[0]);
		song.backedUpParamManagers.failInsertion = true;
		song.deleteBackedUpParamManagersForClip(&clips[1]);
		check(song.backedUpParamManagers.getNumElements() == 1 && allocations.size() == 2,
		      "Failed reinsertion must release removed main and expression collections");
		check(song.getBackedUpParamManagerPreferablyWithClip(&owner, &clips[1]) == survivor,
		      "Failed reinsertion must leave another clip's backup available as fallback");
		check(!song.getBackedUpParamManagerForExactClip(&owner, nullptr),
		      "Failed reinsertion must not leave a partial null-clip entry");
	}
	check(allocations.empty(), "Reinsertion failure must not leak");
	{
		Song song;
		ModControllableAudio owner;
		Clip clips[2];
		ParamManagerForTimeline first, second;
		populate(first);
		populate(second);
		song.backUpParamManager(&owner, &clips[0], &first, true);
		song.backUpParamManager(&owner, &clips[1], &second, true);
		song.deleteBackedUpParamManagersForClip(&clips[1]);
		song.deleteBackedUpParamManagersForModControllable(&owner);
		check(allocations.empty(), "Deleting a non-first clip backup must release its expression collection");
	}
	{
		Song song;
		ModControllableAudio owner;
		Clip clip;
		ParamManagerForTimeline source, destination;
		populate(source);
		populate(destination);
		auto* main = source.summaries[0].paramCollection;
		auto* expression = destination.getExpressionParamSet();
		song.backUpParamManager(&owner, &clip, &source, true);
		check(!source.matches_type(ParamManagerType::ANY), "Backup must relinquish source ownership");
		auto* backup = song.getBackedUpParamManagerForExactClip(&owner, &clip);
		check(backup && backup->summaries[0].paramCollection == main, "Exact lookup must retain collection identity");
		check(song.getBackedUpParamManagerForExactClip(&owner, &clip, &destination) == &destination,
		      "Restore must return destination");
		check(destination.summaries[0].paramCollection == main && destination.getExpressionParamSet() == expression,
		      "Restore must replace main collections and preserve destination expression");
		check(song.backedUpParamManagers.getNumElements() == 0 && allocations.size() == 2,
		      "Restore must remove backup and free superseded collections");
	}
	check(allocations.empty(), "Restored manager destruction must not leak");
	for (bool existingFallback : {false, true}) {
		Song song;
		ModControllableAudio owner;
		Clip clips[2];
		ParamManagerForTimeline source, deleted;
		populate(source);
		populate(deleted);
		song.backUpParamManager(&owner, existingFallback ? nullptr : &clips[0], &source, true);
		auto* deletedMain = deleted.summaries[0].paramCollection;
		song.backUpParamManager(&owner, &clips[1], &deleted, true);
		song.deleteBackedUpParamManagersForClip(&clips[1]);
		check(!song.getBackedUpParamManagerForExactClip(&owner, &clips[1]), "Deleted clip key must be removed");
		auto* fallback = song.getBackedUpParamManagerForExactClip(&owner, nullptr);
		check(fallback && fallback->summaries[0].paramCollection == deletedMain,
		      "Deleted clip main collections must become the null-clip fallback");
		check(song.getBackedUpParamManagerPreferablyWithClip(&owner, &clips[1]) == fallback,
		      "Missing exact clip must select null-clip fallback");
		song.deleteBackedUpParamManagersForModControllable(&owner);
		check(allocations.empty(), "Fallback replacement must release all superseded expression and main collections");
	}
	{
		Song song;
		ModControllableAudio owner;
		Clip clip;
		ParamManagerForTimeline source;
		populate(source);
		song.backUpParamManager(&owner, &clip, &source, false);
		check(source.getExpressionParamSet() != nullptr && !source.matches_type(ParamManagerType::ANY_MAIN),
		      "Main-only backup must retain clip expression");
		populate(source, false);
		auto* replacement = source.summaries[0].paramCollection;
		song.backUpParamManager(&owner, &clip, &source, true);
		check(song.backedUpParamManagers.getNumElements() == 1
		          && song.getBackedUpParamManagerForExactClip(&owner, &clip)->summaries[0].paramCollection
		                 == replacement,
		      "Backing up the same key must replace rather than duplicate");
		song.deleteBackedUpParamManagersForClip(&clip);
		check(!song.getBackedUpParamManagerForExactClip(&owner, &clip)
		          && song.getBackedUpParamManagerForExactClip(&owner, nullptr),
		      "Deleting the first backup must clear its clip reference");
	}
	check(allocations.empty(), "Song destruction must release remaining backups");
	{
		Song song;
		ModControllableAudio owner;
		Clip clip;
		ParamManagerForTimeline source;
		populate(source);
		song.backedUpParamManagers.failInsertion = true;
		song.backUpParamManager(&owner, &clip, &source, true);
		check(allocations.empty() && source.has_valid_layout() && !source.matches_type(ParamManagerType::ANY),
		      "Failed backup insertion must clean source collections exactly once");
		check(song.backedUpParamManagers.getNumElements() == 0, "Failed insertion must not create a backup");
	}
	std::puts("Song backup regressions passed");
}
