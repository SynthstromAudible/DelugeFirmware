/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#ifndef CLIPVIEW_H_
#define CLIPVIEW_H_

#include "ClipNavigationTimelineView.h"

class Action;

class ClipView : public ClipNavigationTimelineView {
public:
	ClipView();

	unsigned int getMaxZoom();
	uint32_t getMaxLength();
	int horizontalEncoderAction(int offset);
	int32_t getLengthChopAmount(int32_t square);
	int32_t getLengthExtendAmount(int32_t square);
	void focusRegained();
	int buttonAction(int x, int y, bool on, bool inCardRoutine);

protected:
	int getTickSquare();

private:
	Action* lengthenClip(int32_t newLength);
	Action* shortenClip(int32_t newLength);
};

#endif /* CLIPVIEW_H_ */
