#include "gui/ui/sound_editor.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"

namespace params = deluge::modulation::params;

/// for a kit we have two types of automation: with Affect Entire and without Affect Entire
ModelStackWithAutoParam* Kit::getModelStackWithParam(ModelStackWithTimelineCounter* modelStack, Clip* clip,
                                                     int32_t paramID, params::Kind paramKind, bool affectEntire,
                                                     bool useMenuStack) {
	if (affectEntire) {
		return getModelStackWithParamForKit(modelStack, clip, paramID, paramKind, useMenuStack);
	}
	else {
		return getModelStackWithParamForKitRow(modelStack, clip, paramID, paramKind, useMenuStack);
	}
}

/// for a kit we have two types of automation: with Affect Entire and without Affect Entire
/// for a kit with affect entire on, we are automating information at the kit level
ModelStackWithAutoParam* Kit::getModelStackWithParamForKit(ModelStackWithTimelineCounter* modelStack, Clip* clip,
                                                           int32_t paramID, params::Kind paramKind, bool useMenuStack) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;

	if (useMenuStack) {
		modelStackWithThreeMainThings = modelStack->addOtherTwoThingsButNoNoteRow(soundEditor.currentModControllable,
		                                                                          soundEditor.currentParamManager);
	}
	else {
		modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsButNoNoteRow(toModControllable(), &clip->paramManager);
	}

	// Require UNPATCHED_GLOBAL and a valid GLOBAL manager. Previously another parameter kind could resolve to a global
	// parameter with the same numeric ID.
	if (paramKind == params::Kind::UNPATCHED_GLOBAL && modelStackWithThreeMainThings
	    && modelStackWithThreeMainThings->paramManager
	    && modelStackWithThreeMainThings->paramManager->matches_type(required_param_manager_type())) {
		modelStackWithParam = modelStackWithThreeMainThings->getUnpatchedAutoParamFromId(paramID);
	}

	return modelStackWithParam;
}

/// for a kit we have two types of automation: with Affect Entire and without Affect Entire
/// for a kit with affect entire off, we are automating information at the noterow level
ModelStackWithAutoParam* Kit::getModelStackWithParamForKitRow(ModelStackWithTimelineCounter* modelStack, Clip* clip,
                                                              int32_t paramID, params::Kind paramKind,
                                                              bool useMenuStack) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (selectedDrum && selectedDrum->type == DrumType::SOUND) { // no automation for MIDI or CV kit drum types

		ModelStackWithNoteRow* modelStackWithNoteRow = ((InstrumentClip*)clip)->getNoteRowForSelectedDrum(modelStack);

		if (modelStackWithNoteRow->getNoteRowAllowNull()) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;

			if (useMenuStack) {
				modelStackWithThreeMainThings = modelStackWithNoteRow->addOtherTwoThings(
				    soundEditor.currentModControllable, soundEditor.currentParamManager);
			}
			else {
				modelStackWithThreeMainThings = modelStackWithNoteRow->addOtherTwoThingsAutomaticallyGivenNoteRow();
			}

			// Require a valid SOUND manager, including when using sound-editor context, preventing lookup through an
			// incompatible manager.
			if (modelStackWithThreeMainThings && modelStackWithThreeMainThings->paramManager
			    && modelStackWithThreeMainThings->paramManager->matches_type(
			        selectedDrum->toModControllable()->required_param_manager_type())) {
				if (paramKind == deluge::modulation::params::Kind::PATCHED) {
					modelStackWithParam = modelStackWithThreeMainThings->getPatchedAutoParamFromId(paramID);
				}

				else if (paramKind == deluge::modulation::params::Kind::UNPATCHED_SOUND) {
					modelStackWithParam = modelStackWithThreeMainThings->getUnpatchedAutoParamFromId(paramID);
				}

				else if (paramKind == deluge::modulation::params::Kind::PATCH_CABLE) {
					modelStackWithParam = modelStackWithThreeMainThings->getPatchCableAutoParamFromId(paramID);
				}
				else if (paramKind == params::Kind::EXPRESSION) {
					modelStackWithParam = modelStackWithThreeMainThings->getExpressionAutoParamFromID(paramID);
				}
			}
		}
	}

	return modelStackWithParam;
}
