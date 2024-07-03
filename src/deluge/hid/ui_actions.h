//
// Created by Mark Adams on 2024-07-02.
//

#ifndef DELUGE_UI_ACTIONS_H
#define DELUGE_UI_ACTIONS_H
#include "io/debug/log.h"
enum class ActionResult {
	DEALT_WITH,
	NOT_DEALT_WITH,
	REMIND_ME_OUTSIDE_CARD_ROUTINE,
	ACTIONED_AND_CAUSED_CHANGE,
};

#define HANDLED_ACTION                                                                                                 \
	{                                                                                                                  \
		D_PRINTLN("handled action");                                                                                   \
		return ActionResult::DEALT_WITH;                                                                               \
	}
#endif // DELUGE_UI_ACTIONS_H
