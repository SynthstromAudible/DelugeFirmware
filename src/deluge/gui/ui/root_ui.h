/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "gui/ui/ui.h"
class Output;
class NoteRow;
class InstrumentClip;
class Clip;
class Sample;

class RootUI : public UI {
public:
	RootUI() = default;
	virtual bool getAffectEntire();
	bool canSeeViewUnderneath() final { return true; }
	[[nodiscard]] virtual bool supportsTriplets() const { return true; }
	virtual void notifyPlaybackBegun() {}
	virtual uint32_t getGreyedOutRowsNotRepresentingOutput(Output* output) { return 0; }
	virtual void noteRowChanged(InstrumentClip* clip, NoteRow* noteRow) {}
	virtual void playbackEnded() {}
	virtual void clipNeedsReRendering(Clip* clip) {}
	virtual void sampleNeedsReRendering(Sample* sample) {}
	virtual void midiLearnFlash() {}
};
