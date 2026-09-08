#include "mocks/modulation/params/param_collection.h"
#include "modulation/params/param_manager.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>

ParamManager::ParamManager()
    : resonanceBackwardsCompatibilityProcessed(false), expressionParamSetOffset(0), summaries{} {
}
ParamManager::~ParamManager() {
	destructAndForgetParamCollections();
}
#if ALPHA_OR_BETA_VERSION
ParamManagerForTimeline* ParamManager::toForTimeline() {
	return nullptr;
}
#endif

extern "C" void freezeWithError(const char* code) {
	throw code;
}

static int destroyed = 0;
static int freed = 0;

void delugeDealloc(void* address) {
	++freed;
	std::free(address);
}

class TrackedCollection : public ParamCollection {
public:
	explicit TrackedCollection(deluge::modulation::params::Kind kind) : ParamCollection(kind) {}
	~TrackedCollection() override { ++destroyed; }
};

void check(bool condition, const char* message) {
	if (!condition) {
		std::fprintf(stderr, "%s\n", message);
		std::abort();
	}
}

int main() {
	using Kind = deluge::modulation::params::Kind;
	auto allocate = [](Kind kind) { return new (std::malloc(sizeof(TrackedCollection))) TrackedCollection(kind); };
	auto check_empty = [](ParamManager& manager) {
		check(manager.has_valid_layout() && !manager.matches_type(ParamManagerType::ANY),
		      "Cleanup must restore an empty valid layout");
		for (auto& summary : manager.summaries) {
			for (auto flags : summary.whichParamsAreAutomated) {
				check(flags == 0, "Cleanup must reset automation flags");
			}
			for (auto flags : summary.whichParamsAreInterpolating) {
				check(flags == 0, "Cleanup must reset interpolation flags");
			}
		}
	};
	for (int slot = 0; slot < PARAM_COLLECTIONS_STORAGE_NUM; ++slot) {
		ParamManager manager;
		int before = freed;
		manager.summaries[slot].paramCollection = allocate(Kind::PATCH_CABLE);
		manager.summaries[4].whichParamsAreAutomated[0] = 1;
		manager.summaries[4].whichParamsAreInterpolating[0] = 2;
		manager.destructAndForgetParamCollections();
		check(freed == before + 1 && destroyed == freed, "Cleanup must destroy collections beyond null slots");
		check_empty(manager);
		manager.destructAndForgetParamCollections();
		check(freed == before + 1, "Repeated cleanup must not free twice");
	}
	for (int offset : {0, 1, 3}) {
		ParamManager manager;
		int before = freed;
		manager.expressionParamSetOffset = offset;
		auto* expression = allocate(Kind::EXPRESSION);
		manager.summaries[offset].paramCollection = expression;
		manager.summaries[offset].whichParamsAreAutomated[0] = 42;
		manager.summaries[offset].whichParamsAreInterpolating[0] = 17;
		manager.summaries[4].paramCollection = allocate(Kind::MIDI);
		manager.summaries[2].paramCollection = manager.summaries[4].paramCollection;
		manager.destructMainParamCollections();
		check(freed == before + 1 && destroyed == freed, "Main cleanup must free stale aliases exactly once");
		check(manager.has_valid_layout() && manager.getExpressionParamSetSummary()->paramCollection == expression,
		      "Main cleanup must retain expression");
		check(manager.summaries[0].whichParamsAreAutomated[0] == 42
		          && manager.summaries[0].whichParamsAreInterpolating[0] == 17,
		      "Main cleanup must preserve expression automation and interpolation flags");
		manager.destructAndForgetParamCollections();
		check(freed == before + 2 && destroyed == freed, "Full cleanup must release retained expression");
		check_empty(manager);

		manager.expressionParamSetOffset = offset;
		manager.summaries[offset].paramCollection = allocate(Kind::MIDI);
		manager.destructMainParamCollections();
		check(freed == before + 3 && destroyed == freed, "Wrong-kind expression must be destroyed");
		check_empty(manager);
	}
	{
		ParamManager manager;
		int before = freed;
		auto* expression = allocate(Kind::EXPRESSION);
		manager.expressionParamSetOffset = 3;
		manager.summaries[0].paramCollection = expression;
		manager.summaries[3].paramCollection = expression;
		manager.destructMainParamCollections();
		check(freed == before && manager.has_valid_layout(), "Expression aliases must not free retained expression");
		manager.destructAndForgetParamCollections();
		check(freed == before + 1 && destroyed == freed, "Expression aliases must be released once");
	}
	for (int offset : {2, 4, 5, 255}) {
		ParamManager manager;
		manager.expressionParamSetOffset = offset;
		manager.summaries[4].paramCollection = allocate(Kind::MIDI);
#if ALPHA_OR_BETA_VERSION
		bool froze = false;
		try {
			manager.destructMainParamCollections();
		} catch (const char* code) {
			froze = std::strcmp(code, "PM0C") == 0;
		}
		check(froze, "Invalid expression offset must diagnose before indexing");
		manager.destructAndForgetParamCollections();
#else
		manager.destructMainParamCollections();
#endif
		check_empty(manager);
	}
	check(destroyed == freed, "Every freed collection must run its destructor");
	std::puts("Param manager cleanup regressions passed");
}