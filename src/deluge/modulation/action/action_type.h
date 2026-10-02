/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

// The kind of undoable action. Split out of the heavy model/action/action.h (which pulls model/scale/note_set
// for an Action member) into this leaf so modulation/ code — notably AutoParam, which takes an ActionType as a
// default-argument value, and through it the synth core via ParamSet — can name it without reaching into
// model/. model/action/action.h re-includes this, so its existing users are unaffected.
enum class ActionType {
	MISC,
	NOTE_EDIT,
	NOTE_TAIL_EXTEND,
	CLIP_LENGTH_INCREASE,
	CLIP_LENGTH_DECREASE,
	RECORD,
	AUTOMATION_DELETE,
	PARAM_UNAUTOMATED_VALUE_CHANGE,
	SWING_CHANGE,
	TEMPO_CHANGE,
	CLIP_MULTIPLY,
	CLIP_CLEAR,
	CLIP_DELETE,
	NOTES_PASTE,
	PATTERN_PASTE,
	AUTOMATION_PASTE,
	CLIP_INSTANCE_EDIT,
	ARRANGEMENT_TIME_EXPAND,
	ARRANGEMENT_TIME_CONTRACT,
	ARRANGEMENT_CLEAR,
	ARRANGEMENT_RECORD,
	CLIP_HORIZONTAL_SHIFT,
	NOTE_NUDGE,
	NOTE_REPEAT_EDIT,
	EUCLIDEAN_NUM_EVENTS_EDIT,
	NOTEROW_ROTATE,
	NOTEROW_LENGTH_EDIT,
	NOTEROW_HORIZONTAL_SHIFT,
};
