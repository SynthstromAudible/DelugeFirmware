#include "gui/menu_item/patch_cable_strength.h"
#include "gui/ui/sound_editor.h"
#include "model/model_stack.h"

namespace deluge::gui::menu_item {
ModelStackWithAutoParam* PatchCableStrength::getModelStackWithParam(void* memory) {
	return getModelStack(memory);
}

// Might return a ModelStack with NULL autoParam - check for that!
ModelStackWithAutoParam* PatchCableStrength::getModelStack(void* memory, bool allowCreation) {
	ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
	// Reject missing model stacks, missing managers, and non-SOUND layouts before accessing the patch-cable collection.
	if (!modelStack || !modelStack->paramManager
	    || !modelStack->paramManager->matches_type(soundEditor.currentModControllable->required_param_manager_type())) {
		return nullptr;
	}
	ParamCollectionSummary* paramSetSummary = modelStack->paramManager->getPatchCableSetSummary();

	ModelStackWithParamCollection* modelStackWithParamCollection =
	    modelStack->addParamCollectionSummary(paramSetSummary);
	ModelStackWithParamId* ModelStackWithParamId = modelStackWithParamCollection->addParamId(getLearningThing().data);
	ModelStackWithAutoParam* modelStackMaybeWithAutoParam =
	    paramSetSummary->paramCollection->getAutoParamFromId(ModelStackWithParamId, allowCreation);

	if (allowCreation && modelStackMaybeWithAutoParam->autoParam && !patch_cable_exists_ && !isInHorizontalMenu()) {
		// If we created a patch cable then set the polarity to match the menus
		setPatchCablePolarity(polarity_in_the_ui_);
		patch_cable_exists_ = true;
	}
	return modelStackMaybeWithAutoParam;
}
} // namespace deluge::gui::menu_item
