#include "lookup_fixture.h"
#include <climits>
#include <cstdio>
#include <cstdlib>

SoundEditor soundEditor;
void* currentUI = nullptr;
static int expressionRequests = 0;
static int assertions = 0;
void check(bool condition, const char* message) {
	++assertions;
	if (!condition) {
		std::fprintf(stderr, "%s (check %d)\n", message, assertions);
		std::exit(EXIT_FAILURE);
	}
}
extern "C" void freezeWithError(const char* code) {
	std::fprintf(stderr, "%s\n", code);
	std::exit(EXIT_FAILURE);
}
ParamManager::ParamManager()
    : resonanceBackwardsCompatibilityProcessed(false), expressionParamSetOffset(0), summaries{} {
}
ParamManager::~ParamManager() = default;
bool ParamManager::ensureExpressionParamSetExists(bool) {
	++expressionRequests;
	return summaries[expressionParamSetOffset].paramCollection != nullptr;
}
ModelStackWithThreeMainThings* ModelStackWithNoteRow::addOtherTwoThingsAutomaticallyGivenNoteRow() const {
	return addOtherTwoThings(nullptr, &getNoteRow()->paramManager);
}

struct Collections {
	ParamSet unpatched{params::Kind::UNPATCHED_SOUND, 8};
	ParamSet patched{params::Kind::PATCHED, 11};
	ParamSet cables{params::Kind::PATCH_CABLE, 5};
	ParamSet expression{params::Kind::EXPRESSION, kNumExpressionDimensions};
	void install(ParamManager& manager, bool global = false, bool withExpression = true) {
		for (auto& summary : manager.summaries) {
			summary = {};
		}
		manager.expressionParamSetOffset = global ? 1 : 3;
		unpatched.kind = global ? params::Kind::UNPATCHED_GLOBAL : params::Kind::UNPATCHED_SOUND;
		manager.summaries[0].paramCollection = &unpatched;
		if (!global) {
			manager.summaries[1].paramCollection = &patched;
			manager.summaries[2].paramCollection = &cables;
		}
		if (withExpression) {
			manager.summaries[manager.expressionParamSetOffset].paramCollection = &expression;
		}
	}
};

class RecordingCable : public ParamCollection {
public:
	RecordingCable() : ParamCollection(params::Kind::PATCH_CABLE) {}
	AutoParam parameter;
	int32_t expectedId = 0x123456;
	int calls = 0;
	bool creation = false;
	bool exists = true;
	ModelStackWithAutoParam* getAutoParamFromId(ModelStackWithParamId* stack, bool allowCreation) override {
		++calls;
		creation = allowCreation;
		return stack->addAutoParam(stack->paramId == expectedId && (exists || allowCreation) ? &parameter : nullptr);
	}
};

void expect(ModelStackWithAutoParam* result, ParamManager& manager, int slot, int id) {
	auto* collection = static_cast<ParamSet*>(manager.summaries[slot].paramCollection);
	check(result && result->autoParam == &collection->params[id], "Lookup must return the exact AutoParam");
	check(result->paramManager == &manager && result->summary == &manager.summaries[slot]
	          && result->paramCollection == collection && result->paramId == id,
	      "Lookup must preserve the complete parameter ownership context");
}
void expectNull(ModelStackWithAutoParam* result) {
	check(!result || !result->autoParam, "Invalid lookup must not expose an AutoParam");
}

int main() {
	ModelStackWithAutoParam storage{};
	ParamManagerForTimeline manager;
	Collections collections;
	collections.install(manager);
	auto* stack = setupModelStackWithTimelineCounter(&storage, nullptr, nullptr)
	                  ->addOtherTwoThingsButNoNoteRow(nullptr, &manager);
	using Lookup = ModelStackWithAutoParam* (ModelStackWithThreeMainThings::*)(int32_t);
	Lookup lookups[] = {&ModelStackWithThreeMainThings::getUnpatchedAutoParamFromId,
	                    &ModelStackWithThreeMainThings::getPatchedAutoParamFromId,
	                    &ModelStackWithThreeMainThings::getPatchCableAutoParamFromId,
	                    &ModelStackWithThreeMainThings::getExpressionAutoParamFromID};
	for (int slot = 0; slot < 4; ++slot) {
		auto* collection = static_cast<ParamSet*>(manager.summaries[slot].paramCollection);
		for (int id = 0; id < collection->numParams_; ++id) {
			expect((stack->*lookups[slot])(id), manager, slot, id);
		}
		for (int id : {INT_MIN, -1, collection->numParams_, 255, INT_MAX}) {
			expect((stack->*lookups[slot])(0), manager, slot, 0);
			expectNull((stack->*lookups[slot])(id));
		}
	}
	collections.install(manager, true);
	expect(stack->getUnpatchedAutoParamFromId(0), manager, 0, 0);
	expectNull(stack->getPatchedAutoParamFromId(0));
	expectNull(stack->getPatchCableAutoParamFromId(0));
	for (auto kind : {params::Kind::MIDI, params::Kind::NONE}) {
		collections.unpatched.kind = kind;
		expectNull(stack->getUnpatchedAutoParamFromId(0));
	}
	collections.install(manager, false, false);
	int requests = expressionRequests;
	expectNull(stack->getExpressionAutoParamFromID(-1));
	check(expressionRequests == requests, "Invalid expression ID must not request allocation");
	expectNull(stack->getExpressionAutoParamFromID(0));
	check(expressionRequests == requests + 1, "Failed expression allocation must return null");
	stack->paramManager = nullptr;
	for (auto lookup : lookups) {
		expectNull((stack->*lookup)(0));
	}
	stack->paramManager = &manager;
	collections.install(manager);
	for (int missing = 0; missing < 3; ++missing) {
		auto* saved = manager.summaries[missing].paramCollection;
		manager.summaries[missing].paramCollection = nullptr;
		for (int slot = 0; slot < 3; ++slot) {
			expectNull((stack->*lookups[slot])(0));
		}
		manager.summaries[missing].paramCollection = saved;
	}

	MelodicInstrument synth;
	synth.type = OutputType::SYNTH;
	InstrumentClip clip;
	clip.output = &synth;
	Collections clipCollections;
	clipCollections.install(clip.paramManager);
	auto* timeline = setupModelStackWithTimelineCounter(&storage, nullptr, &clip);
	MidiFollow follow;
	expect(follow.getModelStackWithParam(timeline, &clip, 2, 3, true), clip.paramManager, 1, 2);
	expect(follow.getModelStackWithParam(timeline, &clip, params::UNPATCHED_START + 2, 3, true), clip.paramManager, 0,
	       2);
	for (int id : {-1, 255, INT_MAX}) {
		expectNull(follow.getModelStackWithParam(timeline, &clip, id, 3, true));
	}
	check(follow.errors == 3, "MIDI follow must report rejected parameter requests");
	expectNull(follow.getModelStackWithParam(nullptr, &clip, 0, 0, false));
	expectNull(follow.getModelStackWithParam(timeline, nullptr, 0, 0, false));
	for (auto type : {OutputType::MIDI_OUT, OutputType::CV, OutputType::NONE}) {
		synth.type = type;
		expectNull(follow.getModelStackWithParam(timeline, &clip, 0, 0, false));
	}
	AudioOutput audio;
	audio.type = OutputType::AUDIO;
	clip.output = &audio;
	clipCollections.install(clip.paramManager, true);
	expect(follow.getModelStackWithParam(timeline, &clip, 2, 3, false), clip.paramManager, 0, 3);
	for (int id : {-1, 8, 255, INT_MAX}) {
		expectNull(follow.getModelStackWithParam(timeline, &clip, 0, id, false));
	}
	Kit kit;
	kit.type = OutputType::KIT;
	Drum drum;
	kit.selectedDrum = &drum;
	clip.output = &kit;
	NoteRow row;
	Collections rowCollections;
	rowCollections.install(row.paramManager);
	clip.row = &row;
	expect(follow.getModelStackWithParam(timeline, &clip, 2, 3, false), row.paramManager, 1, 2);
	expectNull(follow.getModelStackWithParam(timeline, &clip, params::UNPATCHED_START + params::UNPATCHED_PORTAMENTO, 3,
	                                         false));
	clip.affectEntire = true;
	expect(follow.getModelStackWithParam(timeline, &clip, 2, 3, false), clip.paramManager, 0, 3);
	clip.affectEntire = false;
	clip.row = nullptr;
	expectNull(follow.getModelStackWithParam(timeline, &clip, 2, 3, false));
	clip.row = &row;
	kit.selectedDrum = nullptr;
	expectNull(follow.getModelStackWithParam(timeline, &clip, 2, 3, false));
	kit.selectedDrum = &drum;
	drum.type = DrumType::MIDI;
	expectNull(follow.getModelStackWithParam(timeline, &clip, 2, 3, false));
	drum.type = DrumType::SOUND;
	ParamManagerForTimeline menuManager;
	Collections menuCollections;
	menuCollections.install(menuManager);
	soundEditor.currentParamManager = &menuManager;
	soundEditor.currentModControllable = &synth;
	AutomationView view;
	clip.lastSelectedParamID = 2;
	clip.lastSelectedParamKind = params::Kind::PATCHED;
	expect(view.getModelStackWithParamForClip(timeline, &clip, params::kNoParamID, params::Kind::NONE),
	       row.paramManager, 1, 2);
	expect(view.getModelStackWithParamForClip(timeline, &clip, 3, params::Kind::UNPATCHED_SOUND), row.paramManager, 0,
	       3);
	currentUI = &soundEditor;
	expect(view.getModelStackWithParamForClip(timeline, &clip, params::kNoParamID, params::Kind::NONE), menuManager, 1,
	       2);
	soundEditor.settings = true;
	expect(view.getModelStackWithParamForClip(timeline, &clip, params::kNoParamID, params::Kind::NONE),
	       row.paramManager, 1, 2);
	soundEditor.settings = false;
	view.affectEntire = true;
	menuCollections.install(menuManager, true);
	expect(view.getModelStackWithParamForClip(timeline, &clip, 3, params::Kind::UNPATCHED_GLOBAL), menuManager, 0, 3);
	currentUI = nullptr;
	expect(view.getModelStackWithParamForClip(timeline, &clip, 3, params::Kind::UNPATCHED_GLOBAL), clip.paramManager, 0,
	       3);
	for (auto kind : {params::Kind::PATCHED, params::Kind::UNPATCHED_SOUND, params::Kind::EXPRESSION,
	                  params::Kind::MIDI, params::Kind::NONE, params::Kind::PATCH_CABLE}) {
		expectNull(view.getModelStackWithParamForClip(timeline, &clip, 0, kind));
		expectNull(audio.getModelStackWithParam(timeline, &clip, 0, kind, true, false));
	}
	menuCollections.install(menuManager);
	deluge::gui::menu_item::PatchedParam patchedMenu;
	deluge::gui::menu_item::UnpatchedParam unpatchedMenu;
	for (int id = 0; id < 8; ++id) {
		patchedMenu.id = unpatchedMenu.id = id;
		expect(patchedMenu.getModelStack(&storage), menuManager, 1, id);
		expect(unpatchedMenu.getModelStack(&storage), menuManager, 0, id);
	}
	for (int id : {-1, 255, INT_MAX}) {
		patchedMenu.id = unpatchedMenu.id = id;
		expectNull(patchedMenu.getModelStack(&storage));
		expectNull(unpatchedMenu.getModelStack(&storage));
	}
	patchedMenu.id = unpatchedMenu.id = 0;
	menuCollections.install(menuManager, true);
	expectNull(patchedMenu.getModelStack(&storage));
	expect(unpatchedMenu.getModelStack(&storage), menuManager, 0, 0);
	soundEditor.currentParamManager = nullptr;
	expectNull(patchedMenu.getModelStack(&storage));
	expectNull(unpatchedMenu.getModelStack(&storage));
	Song song;
	stack->paramManager = &menuManager;
	expect(song.getModelStackWithParam(stack, 0), menuManager, 0, 0);
	expectNull(song.getModelStackWithParam(stack, -1));
	expectNull(song.getModelStackWithParam(nullptr, 0));
	MIDIInstrument midi;
	ParamSet midiParams(params::Kind::MIDI, kNumRealCCNumbers);
	menuManager.summaries[0].paramCollection = &midiParams;
	for (int cc = 0; cc < kNumRealCCNumbers; ++cc) {
		int slot = cc == CC_NUMBER_Y_AXIS ? 1 : 0;
		int id = cc == CC_NUMBER_Y_AXIS ? 1 : cc;
		expect(midi.getParamToControlFromInputMIDIChannel(cc, stack), menuManager, slot, id);
	}
	expect(midi.getParamToControlFromInputMIDIChannel(CC_NUMBER_PITCH_BEND, stack), menuManager, 1, 0);
	expect(midi.getParamToControlFromInputMIDIChannel(CC_NUMBER_AFTERTOUCH, stack), menuManager, 1, 2);
	for (int cc : {-1, kNumCCExpression, 255, INT_MAX}) {
		expectNull(midi.getParamToControlFromInputMIDIChannel(cc, stack));
	}
	menuManager.summaries[1].paramCollection = nullptr;
	expectNull(midi.getParamToControlFromInputMIDIChannel(CC_NUMBER_PITCH_BEND, stack));
	expectNull(midi.getModelStackWithParam(timeline, &clip, 0, params::Kind::MIDI, true, false));
	clip.paramManager.summaries[0].paramCollection = &midiParams;
	expect(midi.getModelStackWithParam(timeline, &clip, 12, params::Kind::MIDI, true, false), clip.paramManager, 0, 12);

	menuCollections.install(menuManager);
	soundEditor.currentParamManager = &menuManager;
	RecordingCable cable;
	menuManager.summaries[2].paramCollection = &cable;
	deluge::gui::menu_item::PatchCableStrength cableMenu;
	cableMenu.descriptor.data = cable.expectedId;
	auto* cableResult = cableMenu.getModelStackWithParam(&storage);
	check(cableResult && cableResult->autoParam == &cable.parameter && cableResult->paramId == cable.expectedId
	          && cableResult->paramManager == &menuManager && cableResult->summary == &menuManager.summaries[2],
	      "Patch cable menu must preserve descriptor and collection identity");
	check(!cable.creation && cableMenu.polarityChanges == 0,
	      "Read-only cable lookup must not create or change polarity");
	cable.exists = false;
	expectNull(cableMenu.getModelStackWithParam(&storage));
	check(cableMenu.getModelStack(&storage, true)->autoParam == &cable.parameter,
	      "Creation lookup must forward permission");
	check(cable.creation && cableMenu.polarityChanges == 1, "New cable must receive menu polarity once");
	cableMenu.getModelStack(&storage, true);
	check(cableMenu.polarityChanges == 1, "Repeated lookup must not reset polarity");
	soundEditor.currentParamManager = nullptr;
	expectNull(cableMenu.getModelStackWithParam(&storage));
	soundEditor.currentParamManager = &menuManager;
	menuCollections.install(menuManager, true);
	expectNull(cableMenu.getModelStackWithParam(&storage));
	clipCollections.install(clip.paramManager, true);
	expectNull(synth.getModelStackWithParam(timeline, &clip, 0, params::Kind::UNPATCHED_SOUND, true, false));
	rowCollections.install(row.paramManager, true);
	expectNull(kit.getModelStackWithParam(timeline, &clip, 0, params::Kind::UNPATCHED_SOUND, false, false));
	for (auto& summary : clip.paramManager.summaries) {
		summary = {};
	}
	clip.paramManager.expressionParamSetOffset = 0;
	clip.paramManager.summaries[0].paramCollection = &clipCollections.expression;
	expect(synth.getModelStackWithParam(timeline, &clip, 1, params::Kind::EXPRESSION, true, false), clip.paramManager,
	       0, 1);
	expectNull(synth.getModelStackWithParam(timeline, &clip, 0, params::Kind::PATCHED, true, false));
	storage.paramManager = &clip.paramManager;
	clip.paramManager.expressionParamSetOffset = 255;
	expectNull(storage.getExpressionAutoParamFromID(0));
	std::printf("Parameter lookup regressions passed (%d checks)\n", assertions);
}