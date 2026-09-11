#include "modulation/params/param_manager.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

// ParamManager's ctor/dtor live in param_manager.cpp, which drags in the allocator and every real collection, so
// they're stubbed here. The stub must mirror the real ctor's contract: every slot cleared, offset 0.
ParamManager::ParamManager()
    : resonanceBackwardsCompatibilityProcessed(false), expressionParamSetOffset(0), summaries{} {
}
ParamManager::~ParamManager() = default;
#if ALPHA_OR_BETA_VERSION
ParamManagerForTimeline* ParamManager::toForTimeline() {
	return nullptr;
}
#endif

// Turn firmware freezes into observable test failures without continuing an unsafe accessor.
extern "C" void freezeWithError(const char* code) {
	throw code;
}

template <typename Accessor>
void expect_freeze(Accessor accessor, const char* expected) {
	try {
		accessor();
	} catch (const char* code) {
		if (std::strcmp(code, expected) == 0) {
			return;
		}
		std::fprintf(stderr, "Expected %s, got %s\n", expected, code);
		std::abort();
	}
	std::fprintf(stderr, "Expected freeze %s\n", expected);
	std::abort();
}

using Kind = deluge::modulation::params::Kind;
using Type = ParamManagerType;

void check(bool condition, const char* message) {
	if (!condition) {
		std::fprintf(stderr, "%s\n", message);
		std::abort();
	}
}

int main() {
	ParamCollection sound(Kind::UNPATCHED_SOUND), global(Kind::UNPATCHED_GLOBAL), patched(Kind::PATCHED),
	    cables(Kind::PATCH_CABLE), midi(Kind::MIDI), expression(Kind::EXPRESSION);

	{
		ParamManager fresh;
		check(fresh.has_valid_layout(), "Empty managers are valid during initialization");
		check(!fresh.matches_type(Type::ANY), "A default-constructed manager holds nothing");
		check(!fresh.matches_type(Type::ANY_MAIN), "A default-constructed manager has no main collections");
		check(!fresh.matches_type(Type::SOUND) && !fresh.matches_type(Type::GLOBAL) && !fresh.matches_type(Type::MIDI),
		      "A default-constructed manager satisfies no audio layout");
		check(fresh.matches_type(Type::CV) && fresh.matches_type(Type::NONE),
		      "A default-constructed manager is a valid CV / non-audio-drum layout");
	}

	ParamManager manager;
	auto layout = [&](int offset, std::initializer_list<ParamCollection*> collections) {
		for (auto& summary : manager.summaries) {
			summary = {};
		}
		manager.expressionParamSetOffset = offset;
		int index = 0;
		for (auto* collection : collections) {
			manager.summaries[index++].paramCollection = collection;
		}
	};

	for (auto* optional_expression : {static_cast<ParamCollection*>(nullptr), &expression}) {
		layout(3, {&sound, &patched, &cables, optional_expression});
		check(manager.has_valid_layout(), "A complete sound layout is valid");
		check(manager.matches_type(Type::SOUND), "A sound preset must work without expression (E073 regression)");
		check(!manager.matches_type(Type::MIDI), "Sound must not be reused as MIDI");
		check(!manager.matches_type(Type::GLOBAL), "Sound must not be reused as global effects");

		layout(1, {&midi, optional_expression});
		check(manager.has_valid_layout(), "A complete MIDI layout is valid");
		check(manager.matches_type(Type::MIDI), "MIDI must work with and without expression");
		check(!manager.matches_type(Type::GLOBAL), "MIDI and global effects have different main collections");
		check(!manager.matches_type(Type::SOUND), "A duplicated MIDI manager cannot satisfy synth patching");

		layout(1, {&global, optional_expression});
		check(manager.has_valid_layout(), "A complete global layout is valid");
		check(manager.matches_type(Type::GLOBAL), "Global effects allow retained expression");
		check(!manager.matches_type(Type::MIDI), "Global effects must not be reused as MIDI");

		layout(0, {optional_expression});
		check(manager.has_valid_layout(), "Empty and expression-only layouts are valid");
		check(manager.matches_type(Type::CV), "CV must accept empty and expression-only managers");
		check(manager.matches_type(Type::NONE), "Non-audio drums may have no expression or retained expression");
		check(!manager.matches_type(Type::ANY_MAIN), "Expression alone is not a main collection");
		check(!manager.matches_type(Type::SOUND), "Expression alone cannot satisfy a sound");
	}

	layout(1, {&midi});
	check(!manager.matches_type(Type::NONE), "NONE must reject main collections");
	layout(1, {&midi, &midi});
	check(!manager.matches_type(Type::MIDI), "Reject a MIDI collection incorrectly shifted into the expression slot");
	layout(3, {&sound, &expression, &cables});
	check(!manager.matches_type(Type::SOUND), "Expression cannot replace patched parameters");
	layout(3, {&sound, &patched});
	check(!manager.matches_type(Type::SOUND), "Missing patch cables must be rejected");
	layout(1, {&sound, &patched, &cables});
	check(!manager.matches_type(Type::SOUND), "Sound requires the correct expression offset");
	layout(3, {&sound, &patched, &cables, &expression, &midi});
	check(!manager.matches_type(Type::SOUND), "Reject an unterminated sound layout");

	// Regression: stealing/cloning a shorter layout must clear the slots the previous longer layout used.
	layout(1, {&midi, nullptr, &cables});
	check(!manager.matches_type(Type::MIDI), "A stale collection past the terminator invalidates a MIDI layout");
	layout(1, {&global, nullptr, &cables});
	check(!manager.matches_type(Type::GLOBAL), "A stale collection past the terminator invalidates a global layout");

	// Reject stale pointers anywhere beyond the optional expression slot, not just the next slot.
	for (int stale_slot = 2; stale_slot < PARAM_COLLECTIONS_STORAGE_NUM; ++stale_slot) {
		layout(1, {&midi});
		manager.summaries[stale_slot].paramCollection = &cables;
		check(!manager.matches_type(Type::MIDI), "MIDI must reject every stale tail slot");
		layout(1, {&global, &expression});
		manager.summaries[stale_slot].paramCollection = &cables;
		check(!manager.matches_type(Type::GLOBAL), "Global effects must reject every stale tail slot");
	}
	for (int stale_slot = 1; stale_slot < PARAM_COLLECTIONS_STORAGE_NUM; ++stale_slot) {
		layout(0, {&expression});
		manager.summaries[stale_slot].paramCollection = &cables;
		check(!manager.matches_type(Type::CV) && !manager.matches_type(Type::NONE),
		      "Expression-only layouts must reject every stale tail slot");
	}
	for (int invalid_offset : {2, 4, 5, 255}) {
		layout(invalid_offset, {&sound, &patched, &cables});
		check(!manager.matches_type(Type::SOUND) && !manager.matches_type(Type::GLOBAL)
		          && !manager.matches_type(Type::MIDI) && !manager.matches_type(Type::CV)
		          && !manager.matches_type(Type::NONE),
		      "Invalid offsets must fail layout checks without indexing outside the array");
	}

	// Every required sound collection must exist, including slot zero. Presence alone
	// must not be used to decide whether a malformed manager needs a diagnostic.
	for (int missing = 0; missing < 3; ++missing) {
		layout(3, {&sound, &patched, &cables});
		manager.summaries[missing].paramCollection = nullptr;
		check(!manager.has_valid_layout(), "Reject each missing required collection");
	}
	for (int invalid_offset : {2, 4, 5, 255}) {
		layout(invalid_offset, {&expression});
		check(!manager.has_valid_layout(), "Invalid offsets are never valid layouts");
		expect_freeze([&] { manager.getExpressionParamSetSummary(); }, "PM0D");
	}

	layout(1, {&midi, &expression});
	check(manager.getMIDIParamCollectionSummary()->paramCollection == &midi, "Accept MIDI accessor");
	check(manager.getExpressionParamSetSummary()->paramCollection == &expression, "Accept optional expression");
	expect_freeze([&] { manager.getUnpatchedParamSet(); }, "PM02");
	expect_freeze([&] { manager.getUnpatchedParamSetSummary(); }, "PM03");
	expect_freeze([&] { manager.getPatchedParamSet(); }, "PM04");
	expect_freeze([&] { manager.getPatchedParamSetSummary(); }, "PM05");
	expect_freeze([&] { manager.getPatchCableSetSummary(); }, "PM06");
	expect_freeze([&] { manager.getPatchCableSet(); }, "PM07");

	layout(3, {&sound, &patched, &cables});
	check(manager.getUnpatchedParamSetSummary()->paramCollection == &sound, "Accept sound unpatched accessor");
	check(manager.getPatchedParamSetSummary()->paramCollection == &patched, "Accept patched accessor");
	check(manager.getPatchCableSetSummary()->paramCollection == &cables, "Accept patch cable accessor");
	check(manager.getExpressionParamSet() == nullptr, "Absent expression is valid");
	expect_freeze([&] { manager.getMIDIParamCollection(); }, "PM00");
	expect_freeze([&] { manager.getMIDIParamCollectionSummary(); }, "PM01");
	manager.summaries[2].paramCollection = &expression;
	expect_freeze([&] { manager.getPatchCableSet(); }, "PM07");
	manager.summaries[3].paramCollection = &midi;
	expect_freeze([&] { manager.getExpressionParamSet(); }, "PM0D");

	layout(1, {&global});
	check(manager.getUnpatchedParamSetSummary()->paramCollection == &global, "Accept global unpatched accessor");
	layout(0, {});
	expect_freeze([&] { manager.getMIDIParamCollection(); }, "PM00");
	expect_freeze([&] { manager.getUnpatchedParamSet(); }, "PM02");
	expect_freeze([&] { manager.getPatchedParamSet(); }, "PM04");
	expect_freeze([&] { manager.getPatchCableSet(); }, "PM07");

	std::puts("Param manager layout regressions passed");
}
