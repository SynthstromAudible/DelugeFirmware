/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "gui/views/performance_view.h"
#include "model/action/action.h"
#include <cstdint>

class ParamCollection;
class Sound;
class ModelStack;

enum class ActionAddition {
	NOT_ALLOWED,
	ALLOWED,
	ALLOWED_ONLY_IF_NO_TIME_PASSED,
};

class ActionLogger {
public:
	ActionLogger();

	// warning - super not thread safe
	Action* getNewAction(ActionType newActionType,
	                     ActionAddition addToExistingIfPossible = ActionAddition::NOT_ALLOWED);
	void recordUnautomatedParamChange(ModelStackWithAutoParam const* modelStack,
	                                  ActionType actionType = ActionType::PARAM_UNAUTOMATED_VALUE_CHANGE);
	void recordSwingChange(int8_t swingBefore, int8_t swingAfter);
	void recordTempoChange(uint64_t timePerBigBefore, uint64_t timePerBigAfter);
	void recordPerformanceViewPress(FXColumnPress fxPressBefore[kDisplayWidth],
	                                FXColumnPress fxPressAfter[kDisplayWidth], int32_t xDisplay);
	void closeAction(ActionType actionType);
	void closeActionUnlessCreatedJustNow(ActionType actionType);
	void deleteAllLogs();
	void deleteLog(int32_t time);
	bool revert(TimeType time, bool updateVisually = true, bool doNavigation = true);
	void updateAction(Action* newAction);
	void undo();
	void redo();
	bool undoJustOneConsequencePerNoteRow(ModelStack* modelStack);
	bool allowedToDoReversion();
	void notifyClipRecordingAborted(Clip* clip);

	Action* firstAction[2];

private:
	void revertAction(Action* action, bool updateVisually, bool doNavigation, TimeType time);
	void deleteLastActionIfEmpty();
	void deleteLastAction();
};

extern ActionLogger actionLogger;
