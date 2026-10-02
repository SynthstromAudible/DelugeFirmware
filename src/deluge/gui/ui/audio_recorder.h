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
#include "runtime/app_loop.h"

class Clip;
class MultiRange;
class Output;
class SampleRecorder;
class Sound;

class AudioRecorder final : public UI {
public:
	AudioRecorder();
	bool opened() override;
	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) override;

	ActionResult buttonAction(deluge::hid::Button b, bool on) override;
	// A take consumes the pads and encoders: none of them may act on what is underneath while it runs.
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override { return ActionResult::DEALT_WITH; }
	ActionResult horizontalEncoderAction(int32_t offset) override { return ActionResult::DEALT_WITH; }
	ActionResult verticalEncoderAction(int32_t offset) override { return ActionResult::DEALT_WITH; }
	void selectEncoderAction(int8_t offset) override {}
	void modEncoderAction(int32_t whichModEncoder, int32_t offset) override {}

	bool beginOutputRecording(AudioRecordingFolder folder = AudioRecordingFolder::RESAMPLE,
	                          AudioInputChannel channel = AudioInputChannel::OUTPUT, bool writeLoopPoints = false,
	                          bool shouldNormalize = true);
	/// @brief One step of an audio-input take, run by its own job (@ref jobId).
	///
	/// While the recorder UI is on top it ends a take that hit a card error, attaches a settled take's sample to the
	/// sound editor's source and closes, and otherwise shows clipping and the Waiting to Recording change. It asks for
	/// its own next run every @ref kTickUs until the take is over, so it costs nothing while no input take runs.
	/// Internal-source takes (resampling, stem export) are slowRoutine()'s, and this never touches them.
	void tick();
	/// @brief Finishes a settled internal-source take. Never touches an input take, which tick() finishes.
	void slowRoutine();
	bool isCurrentlyResampling();

	AudioInputChannel recordingSource;

	SampleRecorder* recorder;

	void endRecordingSoon(int32_t buttonLatency = 0);

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;

	bool updatedRecordingStatus = false;

	/// The job that runs tick().
	deluge::runtime::JobId jobId{deluge::runtime::kNoJob};
	/// The interval between tick() runs while an input take is in progress, in µs: the clipping popup and the status
	/// line follow the take this closely.
	static constexpr deluge::runtime::Micros kTickUs = 10'000;

	// ui
	UIType getUIType() override { return UIType::AUDIO_RECORDER; }

private:
	/// What an input take records for: the sound editor's sound, source and range as the take started, and the clip
	/// and output that held that sound. Compared by address only; never dereferenced unless still held.
	struct TakeTarget {
		Clip* clip = nullptr;
		Output* output = nullptr;
		Sound* sound = nullptr;
		MultiRange* range = nullptr;
		int32_t sourceIndex = 0;
	};

	/// Whether a machine that can replace a clip's output, or a sound's sample, is in flight. A take does not start
	/// then: what it would attach to could be swapped out from under it.
	[[nodiscard]] static bool anOutputMayBeReplaced();
	/// Whether @ref target_ is still in the current song, and still what the sound editor is editing.
	[[nodiscard]] bool targetStillHeld() const;
	/// Whether an audio-input take is in progress: the takes tick() owns.
	[[nodiscard]] bool inputTakeInProgress() const;
	/// Ends a settled input take: attaches its sample unless it was aborted, hit a card error or lost its target,
	/// then closes.
	void finishInputTake();

	TakeTarget target_{};
	void finishRecording();
	bool setupRecordingToFile(AudioInputChannel newMode, int32_t newNumChannels, AudioRecordingFolder folderID,
	                          bool writeLoopPoints = false, bool shouldNormalize = true);
};

extern AudioRecorder audioRecorder;
