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

#include "gui/ui/slicer.h"
#include "definitions_cxx.hpp"
#include "gui/colour/colour.h"
#include "gui/menu_item/setting_edit.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/waveform/waveform_basic_navigator.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "model/voice/voice_sample.h"
#include "model/voice/voice_slot.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "plugin-api/types/param_mapping.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multisample_range.h"
#include "util/functions.h"
#include <algorithm>
#include <cstring>
#include <iterator>

using namespace deluge::gui;

PLACE_SDRAM_BSS Slicer slicer{};

namespace params = deluge::modulation::params;

void Slicer::focusRegained() {

	// `slicer` is a singleton, so an audition load held from the PREVIOUS visit is still held here, naming a slice of
	// whatever sample that visit was slicing. graphicsRoutine() ticks it again the moment this UI is current, so
	// without this it would sound that stale audition into the new session.
	cancelPreview();

	actionLogger.deleteAllLogs();

	numClips = 16;

	numManualSlice = 1;
	currentSlice = 0;
	slicerMode = SLICER_MODE_REGION;
	for (int32_t i = 0; i < MAX_MANUAL_SLICES; i++) {
		manualSlicePoints[i].startPos = 0;
		manualSlicePoints[i].transpose = 0;
	}
}

void Slicer::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {

	int32_t windowWidth = 100;
	int32_t windowHeight = 31;
	int32_t horizontalShift = 6;

	int32_t windowMinX = (OLED_MAIN_WIDTH_PIXELS - windowWidth) >> 1;
	windowMinX += horizontalShift;
	int32_t windowMaxX = windowMinX + windowWidth;

	int32_t windowMinY = (OLED_MAIN_HEIGHT_PIXELS - windowHeight) >> 1;
	windowMinY += 2;
	int32_t windowMaxY = windowMinY + windowHeight;

	canvas.clearAreaExact(windowMinX + 1, windowMinY + 1, windowMaxX - 1, windowMaxY - 1);

	canvas.drawRectangle(windowMinX, windowMinY, windowMaxX, windowMaxY);
	canvas.drawHorizontalLine(windowMinY + 15, 26, OLED_MAIN_WIDTH_PIXELS - 22);
	canvas.drawString("Num. slices", 30, windowMinY + 6, kTextSpacingX, kTextSpacingY);

	char buffer[12];
	intToString(slicerMode == SLICER_MODE_REGION ? numClips : numManualSlice, buffer);
	canvas.drawStringCentred(buffer, windowMinY + 18, kTextSpacingX, kTextSpacingY,
	                         (OLED_MAIN_WIDTH_PIXELS >> 1) + horizontalShift);
}

void Slicer::redraw() {
}

bool Slicer::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                            uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	// closeUI() calls this with no image, asking only whether the slicer renders the main pads. A slice's commit closes
	// the browser beneath the slicer, so every slice reaches that call.
	if (!image) {
		return true;
	}

	if (slicerMode == SLICER_MODE_REGION) {
		RGB myImage[kDisplayHeight][kDisplayWidth + kSideBarWidth];
		waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
		                                  waveformBasicNavigator.xZoom, image, &waveformBasicNavigator.renderData);
	}
	else if (slicerMode == SLICER_MODE_MANUAL) {

		RGB myImage[kDisplayHeight][kDisplayWidth + kSideBarWidth];
		waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
		                                  waveformBasicNavigator.xZoom, myImage, &waveformBasicNavigator.renderData);

		for (int32_t xx = 0; xx < kDisplayWidth; xx++) {
			for (int32_t yy = 0; yy < kDisplayHeight / 2; yy++) {
				image[yy + 4][xx] = RGB::average(myImage[yy * 2][xx], myImage[yy * 2 + 1][xx]);
			}
		}
		for (int32_t i = 0; i < numManualSlice; i++) { // Slices
			int32_t x = manualSlicePoints[i].startPos / (waveformBasicNavigator.sample->lengthInSamples + 0.0) * 16;
			image[4][x] = RGB{
			    1,
			    (i == currentSlice) ? 200_u8 : 16_u8,
			    1,
			};
		}

		for (int32_t i = 0; i < MAX_MANUAL_SLICES; i++) { // Lower screen
			int32_t xx = (i % 4) + (i / 16) * 4;
			int32_t yy = (i / 4) % 4;
			int32_t page = i / 16;

			RGB colour = RGB::monochrome(3);
			size_t dimLevel = (i < numManualSlice) ? 2 : 6;
			if (page % 2 == 0) {
				colour = colours::green.dim(dimLevel);
			}
			else {
				colour = colours::darkblue.dim(dimLevel);
			}
			if (i == this->currentSlice) {
				colour = colours::green.dim();
			}

			image[yy][xx] = colour;
		}
	}
	return true;
}

const uint8_t zeroes[] = {0, 0, 0, 0, 0, 0, 0, 0};

void Slicer::graphicsRoutine() {
	// This surface (every ~15 ms while the slicer is on top) is the only thing that finishes a slice or a held
	// audition.
	if (core_.slicing()) {
		if (const std::optional<deluge::gui::ui::SliceCore::SliceEnd> end = core_.tick_slice()) {
			if (end->landed) {
				sampleBrowser.exitAndNeverDeleteDrum();
				uiNeedsRendering(&instrumentClipView);
			}
			else if (end->error != Error::NONE) {
				display->displayError(end->error);
			}
			// Release the gate buttonAction() closed.
			currentUIMode = UI_MODE_NONE;
			return; // the slice ended; one that landed has closed the slicer
		}
	}
	core_.tick_audition();

	int32_t newTickSquare = 255;
	if (const std::optional<int32_t> samplePos = core_.play_position()) {
		if (*samplePos >= waveformBasicNavigator.xScroll) {
			newTickSquare = (*samplePos - waveformBasicNavigator.xScroll) / waveformBasicNavigator.xZoom;
			if (newTickSquare >= kDisplayWidth) {
				newTickSquare = 255;
			}
		}
	}

	uint8_t tickSquares[kDisplayHeight];
	memset(tickSquares, 255, kDisplayHeight);
	tickSquares[kDisplayHeight - 1] = newTickSquare;
	tickSquares[kDisplayHeight - 2] = newTickSquare;
	tickSquares[kDisplayHeight - 3] = newTickSquare;
	tickSquares[kDisplayHeight - 4] = newTickSquare;

	PadLEDs::setTickSquares(tickSquares, zeroes);
}

ActionResult Slicer::horizontalEncoderAction(int32_t offset) {

	if (slicerMode == SLICER_MODE_MANUAL) {
		int32_t newPos = manualSlicePoints[currentSlice].startPos;
		newPos += ((Buttons::isShiftButtonPressed() == true) ? 10 : 100) * offset;

		if (currentSlice > 0 && currentSlice < numManualSlice - 1) {
			if (newPos <= manualSlicePoints[currentSlice - 1].startPos + 1)
				newPos = manualSlicePoints[currentSlice - 1].startPos + 1;
			if (newPos >= manualSlicePoints[currentSlice + 1].startPos - 1)
				newPos = manualSlicePoints[currentSlice + 1].startPos - 1;
		}

		if (newPos < 0)
			newPos = 0;
		if (newPos > waveformBasicNavigator.sample->lengthInSamples)
			newPos = waveformBasicNavigator.sample->lengthInSamples;
		manualSlicePoints[currentSlice].startPos = newPos;

		char buffer[24];
		strcpy(buffer, "Start: ");
		intToString(manualSlicePoints[currentSlice].startPos, buffer + strlen(buffer));
		display->popupTextTemporary(buffer);
		uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
	}
	return ActionResult::DEALT_WITH;
}

ActionResult Slicer::verticalEncoderAction(int32_t offset) {
	if (slicerMode == SLICER_MODE_MANUAL) {

		manualSlicePoints[currentSlice].transpose += offset;
		if (manualSlicePoints[currentSlice].transpose > 24)
			manualSlicePoints[currentSlice].transpose = 24;
		if (manualSlicePoints[currentSlice].transpose < -24)
			manualSlicePoints[currentSlice].transpose = -24;
		char buffer[32];
		snprintf(buffer, 32, "Transpose: %d", manualSlicePoints[currentSlice].transpose);
		display->popupTextTemporary(buffer);
	}
	return ActionResult::DEALT_WITH;
}

void Slicer::selectEncoderAction(int8_t offset) {
	if (slicerMode == SLICER_MODE_REGION) {
		numClips += offset;
		if (numClips == 257) {
			numClips = 2;
		}
		else if (numClips == 1) {
			numClips = 256;
		}
	}
	else { // SLICER_MODE_MANUAL
		if (offset < 0) {
			numManualSlice += offset;
			if (numManualSlice <= 0) {
				numManualSlice = 1;
				manualSlicePoints[0].startPos = 0;
				manualSlicePoints[0].transpose = 0;
			}
		}
		if (currentSlice >= numManualSlice - 1) {
			currentSlice = numManualSlice - 1;
		}
		uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
	}

	renderUIsForOled();
}

ActionResult Slicer::buttonAction(deluge::hid::Button b, bool on) {
	using namespace deluge::hid::button;

	if (currentUIMode != UI_MODE_NONE || !on) {
		return ActionResult::NOT_DEALT_WITH;
	}

	// switch slicer mode
	if (b == X_ENC && on) {
		// Auditions only exist in MANUAL mode, and this leaves the mode a held one was pressed in, killing its voices
		// on the way out. Nothing asks for a replacement, so without cancelling, a held audition would sound after the
		// mode switch that stopped it.
		cancelPreview();
		slicerMode++;
		slicerMode %= 2;
		if (slicerMode == SLICER_MODE_MANUAL)
			AudioEngine::stopAnyPreviewing();
		renderUIsForOled();

		getCurrentKit()->firstDrum->killAllVoices(); // stop
		uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
		return ActionResult::DEALT_WITH;
	}

	// pop up Transpose value
	if (b == Y_ENC && on && slicerMode == SLICER_MODE_MANUAL && currentSlice < numManualSlice) {
		char buffer[24];
		strcpy(buffer, "Transpose: ");
		intToString(manualSlicePoints[currentSlice].transpose, buffer + strlen(buffer));
		display->popupTextTemporary(buffer);
		return ActionResult::DEALT_WITH;
	}

	// delete slice
	if (b == SAVE && on && slicerMode == SLICER_MODE_MANUAL) {
		int32_t xx = (currentSlice % 4) + (currentSlice / 16) * 4;
		int32_t yy = (currentSlice / 4) % 4;
		if (matrixDriver.isPadPressed(xx, yy) && currentSlice < numManualSlice) {
			int32_t target = currentSlice;

			for (int32_t i = 0; i < MAX_MANUAL_SLICES - 1; i++) {
				manualSlicePoints[i] = manualSlicePoints[(i >= target) ? i + 1 : i];
			}
			manualSlicePoints[MAX_MANUAL_SLICES - 1].startPos = 0;
			manualSlicePoints[MAX_MANUAL_SLICES - 1].transpose = 0;

			numManualSlice--;
			if (numManualSlice <= 0) {
				numManualSlice = 1;
				manualSlicePoints[0].startPos = 0;
				manualSlicePoints[0].transpose = 0;
			}
			if (currentSlice >= numManualSlice - 1) {
				currentSlice = numManualSlice - 1;
			}

			uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
			renderUIsForOled();
			return ActionResult::DEALT_WITH;
		}
	}

	if (b == SELECT_ENC) {
		// Committing ends in sampleBrowser.exitAndNeverDeleteDrum() → close(), i.e. the slicer is gone by the time a
		// held audition could land, and the drums it would sound through have been rebuilt underneath it.
		cancelPreview();

		// Snapshot the gesture: the slice runs from these plain values, never from the live members, so an encoder
		// turn or pad press while its load is out can't desync what gets sliced from what gets painted.
		deluge::gui::ui::SliceCore::Target target{};
		target.isManual = (slicerMode == SLICER_MODE_MANUAL);
		target.sliceCount = target.isManual ? numManualSlice : numClips;
		if (target.isManual) {
			std::copy(std::begin(manualSlicePoints), std::end(manualSlicePoints), std::begin(target.manualPoints));
		}

		// The gate, until graphicsRoutine() sees the slice end: the top of this function refuses BACK and a second
		// Select while it is set, so nothing closes the slicer or slices twice while the load is out.
		currentUIMode = UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
		core_.begin_slice(target, static_cast<Browser&>(sampleBrowser).getCurrentFilePath());
	}

	else if (b == BACK) {
		// Leaving the slicer: close() pops it off the UI stack, so graphicsRoutine() stops running and a held audition
		// would otherwise sit held until the next visit.
		cancelPreview();
		if (slicerMode == SLICER_MODE_MANUAL) {
			RGB myImage[kDisplayHeight][kDisplayWidth + kSideBarWidth];
			waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
			                                  waveformBasicNavigator.xZoom, PadLEDs::image,
			                                  &waveformBasicNavigator.renderData);
			// A first drum that never held a file is left as it is.
			(void)core_.restore_full_range();
		}

		close();
	}
	else {
		return ActionResult::NOT_DEALT_WITH;
	}

	return ActionResult::DEALT_WITH;
}

void Slicer::stopAnyPreviewing() {
	core_.unbind_first_drum();
}
void Slicer::preview(int64_t startPoint, int64_t endPoint, int32_t transpose, int32_t on) {
	core_.audition(*waveformBasicNavigator.sample, deluge::gui::ui::SliceCore::AuditionTarget{
	                                                   .startPoint = startPoint,
	                                                   .endPoint = endPoint,
	                                                   .transpose = transpose,
	                                                   .on = on,
	                                               });
}

ActionResult Slicer::padAction(int32_t x, int32_t y, int32_t on) {
	// Refused while a slice's load is out: a press would point the first drum's holder at its audition under the load
	// binding it, and the browser beneath would take a region pad.
	if (core_.slicing()) {
		return ActionResult::DEALT_WITH;
	}

	if (on && x < kDisplayWidth && y < kDisplayHeight / 2 && slicerMode == SLICER_MODE_MANUAL) { // pad on

		int32_t slicePadIndex = (x % 4 + (x / 4) * 16) + ((y % 4) * 4); //

		if (slicePadIndex < numManualSlice) { // play slice
			bool closePopup = (currentSlice != slicePadIndex);
			currentSlice = slicePadIndex;
			if (slicePadIndex + 1 < numManualSlice) {
				preview(manualSlicePoints[slicePadIndex].startPos, manualSlicePoints[slicePadIndex + 1].startPos,
				        manualSlicePoints[slicePadIndex].transpose, on);
			}
			else if (slicePadIndex + 1 == numManualSlice) {
				preview(manualSlicePoints[slicePadIndex].startPos, waveformBasicNavigator.sample->lengthInSamples,
				        manualSlicePoints[slicePadIndex].transpose, on);
			}

			if (closePopup) {
				display->cancelPopup();
			}
		}
		else { // do slice

			VoiceSample* voiceSample = nullptr;
			SamplePlaybackGuide* guide = nullptr;
			MultisampleRange* range;
			Kit* kit = getCurrentKit();
			SoundDrum* drum = (SoundDrum*)kit->firstDrum;

			if (getCurrentClip()->type == ClipType::INSTRUMENT && drum->hasActiveVoices()) {
				auto valid_voices_view = drum->voices() | std::views::filter([&](const Sound::ActiveVoice& voice) {
					                         // Ensure correct MultisampleRange.
					                         return voice->guides[0].audioFileHolder == range->getAudioFileHolder();
				                         });

				range = (MultisampleRange*)drum->sources[0].getOrCreateFirstRange();

				if (!valid_voices_view.empty()) {
					const Sound::ActiveVoice& assigned_voice =
					    *std::ranges::max_element(valid_voices_view, {}, &VoiceSlot::order_sounded);

					const int32_t u = drum->snapshot().num_unison >> 1;
					deluge::synth::SourceSlot* part = &assigned_voice->kernel.unison_parts[u].slots[0];
					if (part != nullptr && part->active) {
						voiceSample = assigned_voice->ports(u, 0).sample.voiceSample();
						guide = &assigned_voice->guides[soundEditor.currentSourceIndex];
					}
				}
			}
			if (voiceSample != nullptr) {
				int32_t samplePos = voiceSample->getPlaySample((Sample*)range->sampleHolder.audioFile, guide);
				if (samplePos < waveformBasicNavigator.sample->lengthInSamples && numManualSlice < MAX_MANUAL_SLICES) {
					manualSlicePoints[numManualSlice].startPos = samplePos;
					manualSlicePoints[numManualSlice].transpose = 0;

					numManualSlice++;
					display->cancelPopup();

					SliceItem tmp{};
					for (int32_t i = 0; i < (numManualSlice - 1); i++) {
						for (int32_t j = (numManualSlice - 1); j > i; j--) {
							if (manualSlicePoints[j].startPos < manualSlicePoints[j - 1].startPos) {
								tmp = manualSlicePoints[j];
								manualSlicePoints[j] = manualSlicePoints[j - 1];
								manualSlicePoints[j - 1] = tmp;
							}
						}
					}
				}
			}
		}

		renderUIsForOled();
		uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
	}
	else if (!on && x < kDisplayWidth && y < kDisplayHeight / 2 && slicerMode == SLICER_MODE_MANUAL) { // pad off
		preview(0, 0, 0, 0);                                                                           // off
	}

	if (slicerMode == SLICER_MODE_MANUAL) {
		return ActionResult::DEALT_WITH;
	}

	return sampleBrowser.padAction(x, y, on);
}
