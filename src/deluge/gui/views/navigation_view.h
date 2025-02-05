/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "util/d_string.h"
#include <cstdint>

class InstrumentClip;
class AudioClip;
class UI;
class RootUI;
class Output;

// Active view of current state of the Deluge
class NavigationView {
public:
	NavigationView();

	// AutomationView parameters
	int32_t knobPosLeft;
	int32_t knobPosRight;
	const char* isAutomated;
	DEF_STACK_STRING_BUF(parameterName, 25);
	char noteRowName[50]; // Used by VELOCITY automation

	const char* textBuffer;     // Used by KEYBOARD and PERFORMANCE as label to replace navigation
	bool hasTempoBPM;           // True when BPM is drawn on dashboard
	bool hasScale;              // True when Scale is drawn on dashboard
	bool hasRemainingCountdown; // True when cue overlays the tempo BPM

	bool useNavigationView();
	void drawDashboard();
	void drawMainboard(const char* nameToDraw = nullptr);
	void drawBaseboard();
	void drawRemainingCountdown(const char* msg = nullptr);
	void drawTempoBPM();

private:
	void clearBuffer(char* buffer, int8_t bufferLength = 0);
	const char* retrieveOutputTypeName(Output* output);
};

extern NavigationView naviview;
