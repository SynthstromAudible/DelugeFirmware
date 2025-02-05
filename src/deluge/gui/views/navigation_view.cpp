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

#include "gui/views/navigation_view.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/root_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "io/midi/midi_engine.h"
#include "lib/printf.h"
#include "model/clip/clip.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/instrument/melodic_instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"
#include "navigation_view.h"
#include "playback/mode/session.h"
#include "processing/audio_output.h"
#include "util/d_string.h"

using namespace deluge;
using namespace gui;

NavigationView naviview{};

NavigationView::NavigationView() {
	knobPosLeft = 0;
	knobPosRight = 0;
}

// Returns true when In-Key Keyboard is set
// as default and the Deluge has an OLED display.
bool NavigationView::useNavigationView() {
	return (display->haveOLED() && FlashStorage::defaultKeyboardLayout == KeyboardLayoutType::KeyboardLayoutTypeInKey);
}

// Top Line Dashboard on the OLED screen
void NavigationView::drawDashboard() {
	DEF_STACK_STRING_BUF(info, 25);
	char buffer[25];
	const char* name;

	hasTempoBPM = false;
	hasScale = false;
	hasRemainingCountdown = false;
	RootUI* rootUI = getRootUI();
	UIType rootUIType = rootUI->getUIType();
	bool isSessionView = (rootUI == &sessionView);
	bool isPerformanceView = (rootUI == &performanceView);
	bool isArrangerView = (rootUI == &arrangerView);
	bool isAudioClipView = (rootUI == &audioClipView);
	bool isAutomationView = (rootUI == &automationView);
	bool isAutomationOverview = (isAutomationView && ((AutomationView*)rootUI)->onAutomationOverview());
	bool isKeyboardView = (rootUIType == UIType::KEYBOARD_SCREEN);
	Clip* clip = getCurrentClip();
	Output* output = getCurrentOutput();
	OutputType outputType = output->type;
	bool isKit = outputType == OutputType::KIT;

	int32_t channel = -1;
	switch (outputType) {
	case OutputType::MIDI_OUT:
	case OutputType::CV:
		channel = ((NonAudioInstrument*)output)->getChannel();
		break;
	case OutputType::AUDIO:
		channel = static_cast<int32_t>(((AudioOutput*)output)->mode);
		break;
	default:
		break;
	}
	const char* outputTypeText = getOutputTypeName(outputType, channel);

	int32_t root = currentSong->key.rootNote;
	Scale scale = currentSong->getCurrentScale();

	int32_t navSysId;
	uint32_t maxLength{0};

	if (isArrangerView) {
		navSysId = NAVIGATION_ARRANGEMENT;
	}
	else {
		navSysId = NAVIGATION_CLIP;
	}

	uint32_t xScroll = currentSong->xScroll[navSysId];
	uint32_t noteLength = currentSong->xZoom[navSysId];
	if (isArrangerView) {
		maxLength = ((ArrangerView*)rootUI)->getMaxLength();
	}
	else if (isSessionView) {
		maxLength = ((SessionView*)rootUI)->getMaxLength();
	}
	else if (clip) {
		maxLength = clip->getMaxLength();
	}

	int32_t magnitude = getNoteMagnitudeFfromNoteLength(noteLength, currentSong->getInputTickMagnitude());
	// Positive magnitudes are bars, negative magnitudes are divisions of bars.
	uint32_t numBars = (uint32_t)1 << magnitude;
	uint32_t division = (uint32_t)1 << (0 - magnitude);

	// Get Number of Bars and Beats
	uint32_t oneBar = currentSong->getBarLength();
	uint32_t totalBarsInClip = maxLength / oneBar;
	uint32_t whichBar = xScroll / oneBar;
	uint32_t posWithinBar = xScroll - whichBar * oneBar;
	uint32_t whichBeat = posWithinBar / (oneBar >> 2);
	uint32_t posWithinBeat = posWithinBar - whichBeat * (oneBar >> 2);
	uint32_t whichSubBeat = posWithinBeat / (oneBar >> 4);
	whichBar++;
	whichBeat++;
	whichSubBeat++;

	if (totalBarsInClip < whichBar) { // 1/0 or 3/2 appear when the last measure is less than 4 beats.
		totalBarsInClip = whichBar;
	}

	if (isArrangerView && magnitude < 0 && division >= 8) {
		sprintf(buffer, "%d/%d:%d:%d", whichBar, totalBarsInClip, whichBeat, whichSubBeat);
	}
	else if (isArrangerView && magnitude < 0 && division >= 2) {
		sprintf(buffer, "%d/%d:%d", whichBar, totalBarsInClip, whichBeat);
	}
	else if (magnitude < 0 && division == 128) {
		sprintf(buffer, "%d/%d:%d:%d", whichBar, totalBarsInClip, whichBeat, whichSubBeat);
	}
	else if (magnitude < 0 && division >= 32) {
		sprintf(buffer, "%d/%d:%d", whichBar, totalBarsInClip, whichBeat);
	}
	else {
		sprintf(buffer, "%d/%d", whichBar, totalBarsInClip);
	}

	if (isKeyboardView || isPerformanceView || isAutomationOverview) {
		// do nothing, navigation is not available, label added below
	}
	else {
		info.append(buffer);
	}

	// Get Subdivision
	if (magnitude < 0) {
		sprintf(buffer, " 1/%d", division);
	}
	else {
		sprintf(buffer, " %dB/C", numBars);
	}
	if (isKeyboardView || isPerformanceView || isAutomationOverview) {
		// do nothing, navigation is not available, label added below
	}
	else {
		info.append(buffer);
	}

	if (isPerformanceView) {
		name = l10n::get(l10n::String::STRING_FOR_PERFORM_VIEW);
		info.append(name);
	}
	else if (isAutomationOverview) {
		name = l10n::get(l10n::String::STRING_FOR_AUTOMATION_OVERVIEW);
		info.append(name);
	}
	else if (isKeyboardView) {
		info.append(textBuffer);
	}
	else if (isAutomationView) {
		if (strcmp(isAutomated, l10n::get(l10n::String::STRING_FOR_AUTOMATION_ON)) == 0) {
			info.append(" "); // Text added after navigation
			info.append(isAutomated);
		}
	}
	else if (isAudioClipView) {
		info.append(" ");                // text added after navigation
		info.append(outputTypeText + 6); // Skip 'Audio '
	}
	else if (!isKit) { // Key and Scale added after navigation
		hasScale = true;
		// Get Key and Scale
		clearBuffer(buffer, 25);
		int32_t isNatural = 1; // gets modified inside noteCodeToString to be 0 if sharp.
		noteCodeToString(root, buffer, &isNatural, false);
		// noteCodeToString(root, buffer, false, root, scale);
		info.append(" ");
		info.append(buffer);
		NoteSet notes = currentSong->key.modeNotes;
		bool moreMajor = (notes.majorness() >= 0);
		if (!moreMajor) {
			info.append("-");
		}
		switch (scale) {
		case MAJOR_SCALE:
			break;
		case MINOR_SCALE:
			info.append(" vi");
			break;
		case DORIAN_SCALE:
			info.append(" ii");
			break;
		case PHRYGIAN_SCALE:
			info.append(" iii");
			break;
		case LYDIAN_SCALE:
			info.append(" IV");
			break;
		case MIXOLYDIAN_SCALE:
			info.append(" V");
			break;
		case LOCRIAN_SCALE:
			info.append(" vii");
			break;
		default:
			sprintf(buffer, " %s", getScaleName(scale));
			for (int i = 1; i < 25 - 1; ++i) {
				if (buffer[i] == 0)
					break;
				if (buffer[i] == ' ' && buffer[i + 1] == 'M') { // Strip off ' Minor'
					buffer[i] = 0;
					break;
				}
			}
			info.append(buffer);
		}
	}

#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif
	hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;
	canvas.clearAreaExact(0, yPos, OLED_MAIN_WIDTH_PIXELS - 1, yPos + kTextSpacingY);
	canvas.drawString(info.data(), 0, yPos, kTextSpacingX, kTextSpacingY);
	if (!isAudioClipView && !isAutomationView
	    && (isSessionView || isArrangerView || isPerformanceView || scale <= LOCRIAN_SCALE)) {
		drawTempoBPM();
	}
	hid::display::OLED::markChanged();
}

// Middle Line of OLED screen
void NavigationView::drawMainboard(const char* nameToDraw) {
	DEF_STACK_STRING_BUF(info, 25);
	char buffer[12];
	const char* name;
	RootUI* rootUI = getRootUI();
	UIType rootUIType = rootUI->getUIType();
	Clip* clip = getCurrentClip();
	Output* output = getCurrentOutput();
	OutputType outputType = output->type;
	Instrument* instrument = (Instrument*)output;

	int32_t channel{0};
	int32_t channelSuffix{0};
	switch (outputType) {
	case OutputType::MIDI_OUT:
		channelSuffix = ((MIDIInstrument*)instrument)->channelSuffix;
		// no break;
	case OutputType::CV:
		channel = ((NonAudioInstrument*)output)->getChannel();
		break;
	case OutputType::AUDIO:
		channel = static_cast<int32_t>(((AudioOutput*)output)->mode);
		break;
	default:
		break;
	}
	const char* outputTypeText = getOutputTypeName(outputType, channel);

	switch (rootUIType) {
	case UIType::INSTRUMENT_CLIP:
	case UIType::AUDIO_CLIP:
	case UIType::AUDIO_RECORDER:
	case UIType::AUTOMATION:
	case UIType::KEYBOARD_SCREEN:
		if (outputType == OutputType::MIDI_OUT) {
			if (strncmp(outputTypeText, "Int", 3) == 0) {
				name = "Int.";
			}
			else {
				name = outputTypeText;
			}
		}
		else if (outputType == OutputType::CV) {
			name = "CV";
		}
		else {
			if (nameToDraw != nullptr) { // current output is old, load_instrument_preset_ui
				                         // calls drawOutputNameFromDetails before loading is complete
				                         // so use the parameter instead.
				name = nameToDraw;
			}
			else {
				name = getCurrentOutput()->name.get();
			}
		}
		info.append(name);
		break;
	case UIType::SESSION:
	case UIType::ARRANGER:
	case UIType::PERFORMANCE:
	case UIType::LOAD_SONG:
	case UIType::NONE:
		if (nameToDraw != nullptr) { // current output is old, load_instrument_preset_ui
			                         // calls drawOutputNameFromDetails before loading is complete
			                         // so use the parameter instead.
			name = nameToDraw;
		}
		else if (currentSong->name.isEmpty()) {
			name = "UNSAVED";
		}
		else {
			name = currentSong->name.get();
		}
		info.append(name);
		break;
	default:
		return; // nothing to draw
	}

	if (outputType == OutputType::MIDI_OUT) {
		info.append(" ");
		if (channel < 16) {
			slotToString(channel + 1, channelSuffix, buffer, 1);
			info.append(buffer);
		}
		else if (channel == MIDI_CHANNEL_MPE_LOWER_ZONE || channel == MIDI_CHANNEL_MPE_UPPER_ZONE) {
			info.append((channel == MIDI_CHANNEL_MPE_LOWER_ZONE) ? "Lower" : "Upper");
		}
		else {
			info.append("Transpose");
		}
	}
	if (outputType == OutputType::CV) {
		info.append(" ");
		if (channel < both) {
			info.appendInt(channel + 1);
		}
		else {
			info.append("1 and 2");
		}
	}

	deluge::hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;
#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 30;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 17;
#endif
	canvas.clearAreaExact(0, yPos, OLED_MAIN_WIDTH_PIXELS - 1, yPos + kTextSpacingY);
	int32_t stringLengthPixels = canvas.getStringWidthInPixels(name, kTextTitleSizeY);
	if (stringLengthPixels <= OLED_MAIN_WIDTH_PIXELS) {
		canvas.drawStringCentred(info.data(), yPos, kTextTitleSpacingX, kTextTitleSizeY);
	}
	else {
		canvas.drawString(info.data(), 0, yPos, kTextTitleSpacingX, kTextTitleSizeY);
		deluge::hid::display::OLED::setupSideScroller(0, name, 0, OLED_MAIN_WIDTH_PIXELS, yPos, yPos + kTextTitleSizeY,
		                                              kTextTitleSpacingX, kTextTitleSizeY, false);
	}
	hid::display::OLED::markChanged();
}

// Bottom Line of OLED screen
void NavigationView::drawBaseboard() {
	DEF_STACK_STRING_BUF(info, 25);
	char const* baseText;
	RootUI* rootUI = getRootUI();
	UIType rootUIType = rootUI->getUIType();
	Clip* clip = getCurrentClip();
	Output* output = getCurrentOutput();
	OutputType outputType = output->type;

	switch (rootUIType) {
	case UIType::PERFORMANCE:
		if (!parameterName.empty()) {
			info.append(parameterName.data());
			info.append(":");
			if (textBuffer) {
				info.append(textBuffer);
			}
			else {
				info.appendInt(knobPosLeft);
			}
		}
		break;
	case UIType::AUTOMATION:
		if (((AutomationView*)rootUI)->inAutomationEditor()) {
			// display parameter name
			info.append(parameterName.data());
			if (knobPosRight != kNoSelection) {
				info.append(":L");
				info.appendInt(knobPosLeft);
				info.append("-R");
				info.appendInt(knobPosRight);
			}
			else {
				info.append(":");
				info.appendInt(knobPosLeft);
			}
		}
		else if (((AutomationView*)rootUI)->inNoteEditor()) {
			if (((AutomationView*)rootUI)->automationParamType == AutomationParamType::NOTE_VELOCITY) {
				info.append("Velocity: ");
				info.append(noteRowName);
			}
		}
		break;
	case UIType::INSTRUMENT_CLIP:
	case UIType::AUDIO_CLIP:
	case UIType::AUDIO_RECORDER:
	case UIType::KEYBOARD_SCREEN:
		if (clip != nullptr) {
			if (clip->name.isEmpty()) {
				info.append("Section ");
				info.appendInt(clip->section + 1);
			}
			else {
				info.appendInt(clip->section + 1);
				info.append(": ");
				info.append(clip->name.get());
			}
		}
		break;
	case UIType::SESSION:
	case UIType::ARRANGER:
	case UIType::LOAD_SONG:
	case UIType::NONE:
	default:
		break;
	}

	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 32;
	hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;
	canvas.clearAreaExact(0, yPos, OLED_MAIN_WIDTH_PIXELS - 1, yPos + kTextSpacingY);
	canvas.drawString(info.data(), 0, yPos, kTextSpacingX, kTextSpacingY);
	deluge::hid::display::OLED::setupSideScroller(1, info.data(), 0, OLED_MAIN_WIDTH_PIXELS, yPos, yPos + kTextSpacingY,
	                                              kTextSpacingX, kTextSpacingY, false);
	hid::display::OLED::markChanged();
}

void NavigationView::drawRemainingCountdown() {
	hasRemainingCountdown = true;
	int32_t sixteenthNotesRemaining = session.getNumSixteenthNotesRemainingTilLaunch();
	int32_t barsRemaining = ((sixteenthNotesRemaining - 1) / 16) + 1;
	int32_t quarterNotesRemaining = ((sixteenthNotesRemaining - 1) / 4) + 1;
	int32_t rndQuarterNotesRemaining = ((quarterNotesRemaining - 1) % 4) + 1;

	DEF_STACK_STRING_BUF(buffer, 10);
	buffer.append(" Q");
	buffer.appendInt(barsRemaining);
	buffer.append(":");
	buffer.appendInt(rndQuarterNotesRemaining);

	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
	hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;

	canvas.clearAreaExact(OLED_MAIN_WIDTH_PIXELS - (kTextSpacingX * buffer.length() + 1), yPos,
	                      OLED_MAIN_WIDTH_PIXELS - 1, yPos + kTextSpacingY);
	canvas.drawStringAlignRight(buffer.data(), yPos, kTextSpacingX, kTextSpacingY);
	hid::display::OLED::markChanged();
}

void NavigationView::drawTempoBPM() {
	hasTempoBPM = true;
	DEF_STACK_STRING_BUF(tempoBPM, 10);
	sessionView.lastDisplayedTempo = playbackHandler.calculateBPM(playbackHandler.getTimePerInternalTickFloat());
	playbackHandler.getTempoStringForOLED(sessionView.lastDisplayedTempo, tempoBPM);
	hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;
	sessionView.displayTempoBPM(canvas, tempoBPM, true);
	deluge::hid::display::OLED::markChanged();
}

void NavigationView::clearBuffer(char* buffer, int8_t bufferLength) {
	int8_t i;
	for (i = 0; i < bufferLength; ++i) {
		if (buffer[i] != 0) {
			buffer[i] = 0;
		}
	}
}
