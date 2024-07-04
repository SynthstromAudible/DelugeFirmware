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

#include "definitions_cxx.hpp"
#include "gui/ui/ui.h"
#include "hid/button.h"

extern "C" {
#include "fatfs/ff.h"
}

class SampleRecorder;

class AudioRecorder final : public UI {
public:
	AudioRecorder();
	bool opened();
	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows);

	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);
	bool beginOutputRecording();
	void process();
	void slowRoutine();
	bool isCurrentlyResampling();

	AudioInputChannel recordingSource;

	SampleRecorder* recorder;

	void endRecordingSoon(int32_t buttonLatency = 0);

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;

	// ui
	UIType getUIType() { return UIType::AUDIO_RECORDER; }
	const char* getName() { return "audio_recorder"; }

private:
	void finishRecording();
	bool setupRecordingToFile(AudioInputChannel newMode, int32_t newNumChannels, AudioRecordingFolder folderID);
};

extern AudioRecorder audioRecorder;
