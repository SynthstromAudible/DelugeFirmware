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

#include "gui/views/audio_clip_view.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip_minder.h"
#include "model/consequence/consequence_clip_length.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/sample/sample_playback_guide.h"
#include "model/sample/sample_recorder.h"
#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/playback_mode.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/flash_storage.h"

extern "C" {
extern uint8_t currentlyAccessingCard;
}

using namespace deluge::gui;

AudioClipView audioClipView{};

inline Sample* getSample() {
    if (getCurrentAudioClip()->getCurrentlyRecordingLinearly()) {
        return getCurrentAudioClip()->recorder->sample;
    }
    else {
        return (Sample*)getCurrentAudioClip()->sampleHolder.audioFile;
    }
}

bool AudioClipView::opened() {
    mustRedrawTickSquares = true;
    uiNeedsRendering(this);

    getCurrentClip()->onAutomationClipView = false;

    focusRegained();
    return true;
}

void AudioClipView::focusRegained() {
    ClipView::focusRegained();
    endMarkerVisible   = false;
    startMarkerVisible = false; // *** ADDED FOR START TRIM ***
    indicator_leds::setLedState(IndicatorLED::BACK, false);
    view.focusRegained();
    view.setActiveModControllableTimelineCounter(getCurrentClip());

    if (display->have7SEG()) {
        view.displayOutputName(getCurrentOutput(), false);
    }
#ifdef currentClipStatusButtonX
    view.drawCurrentClipPad(getCurrentClip());
#endif
}

void AudioClipView::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
    view.displayOutputName(getCurrentOutput(), false, getCurrentClip());
}

bool AudioClipView::renderMainPads(uint32_t whichRows,
                                   RGB image[][kDisplayWidth + kSideBarWidth],
                                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                   bool drawUndefinedArea)
{
    if (!image) {
        return true;
    }

    if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING) || isUIModeActive(UI_MODE_IMPLODE_ANIMATION)) {
        return true;
    }

    // If no AudioClip or no sample, just clear:
    if (!getSample()) {
        for (int32_t y = 0; y < kDisplayHeight; y++) {
            memset(image[y], 0, kDisplayWidth * 3);
        }
        return true;
    }

    AudioClip* clip = getCurrentAudioClip();
    SampleRecorder* recorder = clip->recorder;

    // We'll compute endSquareDisplay for the end marker,
    // and startSquareDisplay for the start marker
    int32_t endSquareDisplay = divide_round_negative(
        clip->loopLength - currentSong->xScroll[NAVIGATION_CLIP] - 1,
        currentSong->xZoom[NAVIGATION_CLIP]);

    // *** ADDED FOR START TRIM: symmetrical calculation for the start
    int32_t startSquareDisplay = divide_round_negative(
        0 - currentSong->xScroll[NAVIGATION_CLIP] - 1,
        currentSong->xZoom[NAVIGATION_CLIP]);

    int64_t xScrollSamples;
    int64_t xZoomSamples;
    clip->getScrollAndZoomInSamples(currentSong->xScroll[NAVIGATION_CLIP],
                                    currentSong->xZoom[NAVIGATION_CLIP],
                                    &xScrollSamples, &xZoomSamples);

    RGB rgb = clip->getColour();

    // For the "end" marker blinking, we might shift xEnd by 1 if it's blinking
    int32_t visibleWaveformXEnd = endSquareDisplay + 1;
    if (endMarkerVisible && blinkOn) {
        visibleWaveformXEnd--;
    }
    int32_t xEnd = std::min(kDisplayWidth, visibleWaveformXEnd);

    bool success = waveformRenderer.renderFullScreen(
        getSample(), xScrollSamples, xZoomSamples, image,
        &clip->renderData, recorder, rgb,
        clip->sampleControls.reversed,
        xEnd);

    // If busy reading from card, we come back later
    if (!success && image == PadLEDs::image) {
        uiNeedsRendering(this, whichRows, 0);
        return true;
    }

    // If we are asked to draw the "undefined" area (grey) beyond the clip boundaries,
    // we do so for both ends:
    if (drawUndefinedArea) {
        for (int32_t y = 0; y < kDisplayHeight; y++) {
            // ========== Draw the end marker in red + fill the region to the right in grey
            if (endSquareDisplay < kDisplayWidth) {
                if (endSquareDisplay >= 0) {
                    if (endMarkerVisible && blinkOn) {
                        image[y][endSquareDisplay][0] = 255; // Red
                        image[y][endSquareDisplay][1] = 0;
                        image[y][endSquareDisplay][2] = 0;
                    }
                }
                int32_t xDisplay = endSquareDisplay + 1;
                if (xDisplay < kDisplayWidth) {
                    if (xDisplay < 0) {
                        xDisplay = 0;
                    }
                    // Grey fill
                    RGB greyCol = colours::grey;
                    std::fill(&image[y][xDisplay],
                              &image[y][kDisplayWidth],
                              greyCol);
                }
            }

            // ========== Draw the start marker in red + fill region to the left in grey
            if (startSquareDisplay >= 0) {
                if (startSquareDisplay < kDisplayWidth) {
                    if (startMarkerVisible && blinkOn) {
                        image[y][startSquareDisplay][0] = 255; // Red
                        image[y][startSquareDisplay][1] = 0;
                        image[y][startSquareDisplay][2] = 0;
                    }
                    int32_t fillEnd = startSquareDisplay;
                    if (fillEnd > kDisplayWidth) {
                        fillEnd = kDisplayWidth;
                    }
                    RGB greyCol = colours::grey;
                    for (int32_t xPos = 0; xPos < fillEnd; ++xPos) {
                        image[y][xPos][0] = greyCol.r;
                        image[y][xPos][1] = greyCol.g;
                        image[y][xPos][2] = greyCol.b;
                    }
                } else {
                    // If the startSquare is off to the right of the screen entirely,
                    // fill entire row grey
                    RGB greyCol = colours::grey;
                    std::fill(&image[y][0],
                              &image[y][kDisplayWidth],
                              greyCol);
                }
            }
        }
    }

    return true;
}

ActionResult AudioClipView::timerCallback() {
    blinkOn = !blinkOn;
    uiNeedsRendering(this, 0xFFFFFFFF, 0);

    // re-arm the blink timer
    uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kSampleMarkerBlinkTime);
    return ActionResult::DEALT_WITH;
}

bool AudioClipView::renderSidebar(uint32_t whichRows,
                                  RGB image[][kDisplayWidth + kSideBarWidth],
                                  uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth])
{
    if (!image) {
        return true;
    }

    if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING) || isUIModeActive(UI_MODE_IMPLODE_ANIMATION)) {
        return true;
    }

    int32_t macroColumn = kDisplayWidth;
    bool armed = false;
    for (int32_t y = 0; y < kDisplayHeight; y++) {
        RGB* const start = &image[y][kDisplayWidth];
        std::fill(start, start + kSideBarWidth, colours::black);

        if (isUIModeActive(UI_MODE_HOLDING_SONG_BUTTON)) {
            armed |= view.renderMacros(macroColumn, y, -1, image, occupancyMask);
        }
    }
    if (armed) {
        view.flashPlayEnable();
    }

    return true;
}

void AudioClipView::graphicsRoutine() {
    if (isUIModeActive(UI_MODE_AUDIO_CLIP_COLLAPSING)) {
        return;
    }

    int32_t newTickSquare;

    if (!playbackHandler.playbackState || !currentSong->isClipActive(getCurrentClip())
        || currentUIMode == UI_MODE_EXPLODE_ANIMATION || currentUIMode == UI_MODE_IMPLODE_ANIMATION
        || playbackHandler.ticksLeftInCountIn) {
        newTickSquare = 255;
    }
    else if (!playbackHandler.isEitherClockActive()
             || (currentPlaybackMode == &arrangement && getCurrentClip()->getCurrentlyRecordingLinearly())) {
        // Tempoless or linear arrangement recording
        newTickSquare = kDisplayWidth - 1;
    }
    else {
        newTickSquare = getTickSquare();
        if (newTickSquare < 0 || newTickSquare >= kDisplayWidth) {
            newTickSquare = 255;
        }
    }

    if (PadLEDs::flashCursor != FLASH_CURSOR_OFF && (newTickSquare != lastTickSquare || mustRedrawTickSquares)) {
        uint8_t tickSquares[kDisplayHeight];
        memset(tickSquares, newTickSquare, kDisplayHeight);

        const uint8_t* coloursPtr = getCurrentClip()->getCurrentlyRecordingLinearly()
                                    ? (const uint8_t*)"\2\2\2\2\2\2\2\2"
                                    : (const uint8_t*)"\0\0\0\0\0\0\0\0";
        PadLEDs::setTickSquares(tickSquares, coloursPtr);

        lastTickSquare = newTickSquare;
        mustRedrawTickSquares = false;
    }
}

void AudioClipView::needsRenderingDependingOnSubMode() {
    switch (currentUIMode) {
        case UI_MODE_HORIZONTAL_SCROLL:
        case UI_MODE_HORIZONTAL_ZOOM:
            break;
        default:
            uiNeedsRendering(this, 0xFFFFFFFF, 0);
    }
}

ActionResult AudioClipView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
    using namespace deluge::hid::button;

    if (b == SESSION_VIEW) {
        if (on) {
            if (currentUIMode == UI_MODE_NONE) {
                currentUIMode = UI_MODE_HOLDING_SONG_BUTTON;
                timeSongButtonPressed = AudioEngine::audioSampleTimer;
                indicator_leds::setLedState(IndicatorLED::SESSION_VIEW, true);
                uiNeedsRendering(this, 0, 0xFFFFFFFF);
            }
        }
        else {
            if (!isUIModeActive(UI_MODE_HOLDING_SONG_BUTTON)) {
                return ActionResult::DEALT_WITH;
            }
            if (inCardRoutine) {
                return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
            }
            exitUIMode(UI_MODE_HOLDING_SONG_BUTTON);
            if ((int32_t)(AudioEngine::audioSampleTimer - timeSongButtonPressed) > kShortPressTime) {
                uiNeedsRendering(this, 0, 0xFFFFFFFF);
                indicator_leds::setLedState(IndicatorLED::SESSION_VIEW, false);
                return ActionResult::DEALT_WITH;
            }

            uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);

            if (currentSong->lastClipInstanceEnteredStartPos != -1 || getCurrentClip()->isArrangementOnlyClip()) {
                bool success = arrangerView.transitionToArrangementEditor();
                if (!success) {
                    sessionView.transitionToSessionView();
                }
            }
            else {
                sessionView.transitionToSessionView();
            }
        }
    }
    else if (b == CLIP_VIEW) {
        if (on && currentUIMode == UI_MODE_NONE) {
            if (inCardRoutine) {
                return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
            }
            changeRootUI(&automationView);
        }
    }
    else if (b == PLAY || b == RECORD || b == SHIFT) {
        return ClipView::buttonAction(b, on, inCardRoutine);
    }
    else if (b == X_ENC) {
        if (Buttons::isButtonPressed(deluge::hid::button::Y_ENC)) {
            if (on && currentUIMode == UI_MODE_NONE) {
                if (inCardRoutine) {
                    return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
                }
                setClipLengthEqualToSampleLength();
            }
        }
        else if (!Buttons::isShiftButtonPressed()) {
            return ClipView::buttonAction(b, on, inCardRoutine);
        }
    }
    else if (b == SELECT_ENC && !Buttons::isShiftButtonPressed()) {
        if (on && currentUIMode == UI_MODE_NONE) {
            if (inCardRoutine) {
                return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
            }
            if (!soundEditor.setup(getCurrentClip())) {
                return ActionResult::DEALT_WITH;
            }
            openUI(&soundEditor);

            if (endMarkerVisible || startMarkerVisible) {
                endMarkerVisible   = false;
                startMarkerVisible = false;
                uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
                uiNeedsRendering(this, 0xFFFFFFFF, 0);
            }
            return ActionResult::DEALT_WITH;
        }
    }
    else if (b == BACK && currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) {
        if (on) {
            if (inCardRoutine) {
                return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
            }
            Action* action = actionLogger.getNewAction(ActionType::CLIP_CLEAR, ActionAddition::NOT_ALLOWED);

            char modelStackMemory[MODEL_STACK_MAX_SIZE];
            ModelStackWithTimelineCounter* modelStack =
                setupModelStackWithTimelineCounter(modelStackMemory, currentSong, getCurrentClip());

            getCurrentAudioClip()->clear(action, modelStack, !FlashStorage::automationClear, true);

            if (FlashStorage::automationClear) {
                display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_SAMPLE_CLEARED));
            }
            else {
                display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CLIP_CLEARED));
            }
            endMarkerVisible   = false;
            startMarkerVisible = false;
            uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
            uiNeedsRendering(this, 0xFFFFFFFF, 0);
        }
    }
    else {
        // Possibly a direct call to ClipMinder or parent
        ActionResult result = ClipMinder::buttonAction(b, on);
        if (result == ActionResult::NOT_DEALT_WITH) {
            result = ClipView::buttonAction(b, on, inCardRoutine);
        }
        if (result != ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
            if (endMarkerVisible || startMarkerVisible) {
                endMarkerVisible   = false;
                startMarkerVisible = false;
                uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
                uiNeedsRendering(this, 0xFFFFFFFF, 0);
            }
        }
        return result;
    }

    return ActionResult::DEALT_WITH;
}

ActionResult AudioClipView::padAction(int32_t x, int32_t y, int32_t on) {
    if (x < kDisplayWidth) {
        if (Buttons::isButtonPressed(deluge::hid::button::TEMPO_ENC)) {
            if (on) {
                playbackHandler.grabTempoFromClip(getCurrentAudioClip());
            }
        }
        else {
            if (sdRoutineLock) {
                return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
            }

            // Possibly a shortcut to SoundEditor
            ActionResult soundEditorResult = soundEditor.potentialShortcutPadAction(x, y, on);
            if (soundEditorResult != ActionResult::NOT_DEALT_WITH) {
                if (soundEditorResult == ActionResult::DEALT_WITH) {
                    endMarkerVisible   = false;
                    startMarkerVisible = false;
                    uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
                    uiNeedsRendering(this, 0xFFFFFFFF, 0);
                }
                return soundEditorResult;
            }
            else if (on && !currentUIMode) {
                AudioClip* clip = getCurrentAudioClip();
                if (!clip) {
                    return ActionResult::DEALT_WITH;
                }

                // Figure out which columns are "the end" or "the start"
                int32_t endSquareDisplay = divide_round_negative(
                    clip->loopLength - currentSong->xScroll[NAVIGATION_CLIP] - 1,
                    currentSong->xZoom[NAVIGATION_CLIP]);

                int32_t startSquareDisplay = divide_round_negative(
                    0 - currentSong->xScroll[NAVIGATION_CLIP] - 1,
                    currentSong->xZoom[NAVIGATION_CLIP]);

                // If user taps the "end marker" column while endMarkerVisible is on
                if (endMarkerVisible) {
                    if (x == endSquareDisplay) {
                        // Toggle off
                        endMarkerVisible = false;
                        uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
                        uiNeedsRendering(this, 0xFFFFFFFF, 0);
                    } else {
                        // Move the "end"
                        Sample* sample = getSample();
                        if (sample) {
                            int32_t newLength = (x + 1) * currentSong->xZoom[NAVIGATION_CLIP]
                                              + currentSong->xScroll[NAVIGATION_CLIP];
                            int32_t oldLength = clip->loopLength;
                            uint64_t oldLengthSamples = clip->sampleHolder.getDurationInSamples(true);
                            changeUnderlyingSampleLength(clip, sample, newLength, oldLength, oldLengthSamples);
                            uiNeedsRendering(this, 0xFFFFFFFF, 0);
                        }
                    }
                }
                // If user taps the "start marker" column while startMarkerVisible is on
                else if (startMarkerVisible) {
                    if (x == startSquareDisplay) {
                        // Toggle off
                        startMarkerVisible = false;
                        uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
                        uiNeedsRendering(this, 0xFFFFFFFF, 0);
                    } else {
                        // Move the "start"
                        Sample* sample = getSample();
                        if (sample) {
                            int32_t newStartTicks = x * currentSong->xZoom[NAVIGATION_CLIP]
                                                  + currentSong->xScroll[NAVIGATION_CLIP];
                            int32_t oldLength = clip->loopLength;
                            uint64_t oldLengthSamples = clip->sampleHolder.getDurationInSamples(true);
                            changeUnderlyingSampleStart(clip, sample, newStartTicks, oldLength, oldLengthSamples);
                            uiNeedsRendering(this, 0xFFFFFFFF, 0);
                        }
                    }
                }
                else {
                    // No marker currently visible. Check if user is tapping near the end or start.
                    if (x == endSquareDisplay || x == endSquareDisplay + 1) {
                        endMarkerVisible   = true;
                        startMarkerVisible = false;
                        blinkOn = true;
                        uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kSampleMarkerBlinkTime);
                        uiNeedsRendering(this, 0xFFFFFFFF, 0);
                    }
                    else if (x == startSquareDisplay || x == startSquareDisplay + 1) {
                        startMarkerVisible = true;
                        endMarkerVisible   = false;
                        blinkOn = true;
                        uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kSampleMarkerBlinkTime);
                        uiNeedsRendering(this, 0xFFFFFFFF, 0);
                    }
                }
            }
        }
    }
    else if (x == kDisplayWidth) {
        if (isUIModeActive(UI_MODE_HOLDING_SONG_BUTTON)) {
            if (sdRoutineLock) {
                return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
            }
            if (!on) {
                view.activateMacro(y);
            }
            return ActionResult::DEALT_WITH;
        }
    }

    return ActionResult::DEALT_WITH;
}

// Original "end" pointer logic (unchanged except references to local copies of grey)
void AudioClipView::changeUnderlyingSampleLength(AudioClip* clip,
                                                 const Sample* sample,
                                                 int32_t newLength,
                                                 int32_t oldLength,
                                                 uint64_t oldLengthSamples) const
{
    uint64_t* valueToChange;
    int64_t newEndPosSamples;

    // Convert newLength ticks to sample positions by ratio
    uint64_t newLengthSamples = (uint64_t)(oldLengthSamples * newLength + (oldLength >> 1))
                                / (uint32_t)oldLength; // rounding

    // If reversed, the "end" in the UI means we are actually adjusting "startPos" in sampleHolder
    if (clip->sampleControls.reversed) {
        newEndPosSamples = clip->sampleHolder.endPos - newLengthSamples;
        if (newEndPosSamples < 0) {
            newEndPosSamples = 0;
        }
        valueToChange = &clip->sampleHolder.startPos;
    }
    else {
        newEndPosSamples = clip->sampleHolder.startPos + newLengthSamples;
        if ((int64_t)newEndPosSamples > (int64_t)sample->lengthInSamples) {
            newEndPosSamples = sample->lengthInSamples;
        }
        valueToChange = &clip->sampleHolder.endPos;
    }

    ActionType actionType = (newLength < oldLength)
                            ? ActionType::CLIP_LENGTH_DECREASE
                            : ActionType::CLIP_LENGTH_INCREASE;

    uint64_t oldValue = *valueToChange;
    *valueToChange = newEndPosSamples;

    // Actually change the clip's loopLength
    Action* action = actionLogger.getNewAction(actionType, ActionAddition::NOT_ALLOWED);
    currentSong->setClipLength(clip, newLength, action);
    if (action) {
        if (action->firstConsequence && action->firstConsequence->type == Consequence::CLIP_LENGTH) {
            ConsequenceClipLength* consequence = (ConsequenceClipLength*)action->firstConsequence;
            consequence->pointerToMarkerValue   = valueToChange;
            consequence->markerValueToRevertTo  = oldValue;
        }
        actionLogger.closeAction(actionType);
    }
}

// *** ADDED FOR START TRIM ***
void AudioClipView::changeUnderlyingSampleStart(AudioClip* clip,
                                                const Sample* sample,
                                                int32_t newStartTicks,
                                                int32_t oldLength,
                                                uint64_t oldLengthSamples) const
{
    // We treat the "start" in the UI as "tick=0" and the end as "tick=oldLength."
    // newLengthTicks = oldEndTick - newStartTick
    // i.e. if user sets the start pointer to 10 ticks, the new total length is oldLength - 10.
    // If user sets start pointer negative, newLength can exceed oldLength => un-trimming.

    int32_t oldEndTick = oldLength;
    int32_t newLengthTicks = oldEndTick - newStartTicks;
    if (newLengthTicks < 1) {
        // can't have zero or negative loop length
        newLengthTicks = 1;
    }

    uint64_t newLengthSamples = (uint64_t)(oldLengthSamples * newLengthTicks + (oldLength >> 1))
                                / (uint32_t)oldLength;

    if (clip->sampleControls.reversed) {
        // If reversed, "start" in UI is the sampleHolder.endPos in code
        uint64_t oldValue = clip->sampleHolder.endPos;
        uint64_t newEndPos = clip->sampleHolder.startPos + newLengthSamples;
        if (newEndPos > sample->lengthInSamples) {
            newEndPos = sample->lengthInSamples;
        }
        clip->sampleHolder.endPos = newEndPos;

        ActionType actionType = (newLengthTicks < oldLength)
                                ? ActionType::CLIP_LENGTH_DECREASE
                                : ActionType::CLIP_LENGTH_INCREASE;

        Action* action = actionLogger.getNewAction(actionType, ActionAddition::NOT_ALLOWED);
        currentSong->setClipLength(clip, newLengthTicks, action);
        if (action) {
            if (action->firstConsequence && action->firstConsequence->type == Consequence::CLIP_LENGTH) {
                ConsequenceClipLength* consequence = (ConsequenceClipLength*)action->firstConsequence;
                consequence->pointerToMarkerValue  = &clip->sampleHolder.endPos;
                consequence->markerValueToRevertTo = oldValue;
            }
            actionLogger.closeAction(actionType);
        }
    }
    else {
        // Normal forward direction: move sampleHolder.startPos
        uint64_t oldValue = clip->sampleHolder.startPos;
        uint64_t newStartPos = clip->sampleHolder.endPos - newLengthSamples;
        if ((int64_t)newStartPos < 0) {
            newStartPos = 0;
        }
        clip->sampleHolder.startPos = newStartPos;

        ActionType actionType = (newLengthTicks < oldLength)
                                ? ActionType::CLIP_LENGTH_DECREASE
                                : ActionType::CLIP_LENGTH_INCREASE;

        Action* action = actionLogger.getNewAction(actionType, ActionAddition::NOT_ALLOWED);
        currentSong->setClipLength(clip, newLengthTicks, action);
        if (action) {
            if (action->firstConsequence && action->firstConsequence->type == Consequence::CLIP_LENGTH) {
                ConsequenceClipLength* consequence = (ConsequenceClipLength*)action->firstConsequence;
                consequence->pointerToMarkerValue  = &clip->sampleHolder.startPos;
                consequence->markerValueToRevertTo = oldValue;
            }
            actionLogger.closeAction(actionType);
        }
    }
}

void AudioClipView::playbackEnded() {
    uiNeedsRendering(this, 0xFFFFFFFF, 0);
}

void AudioClipView::clipNeedsReRendering(Clip* clip) {
    if (clip == getCurrentAudioClip()) {
        // If the loop length shrank so we are scrolled too far right, scroll left
        if (currentSong->xScroll[NAVIGATION_CLIP] >= clip->loopLength) {
            horizontalScrollForLinearRecording(0);
        } else {
            uiNeedsRendering(this, 0xFFFFFFFF, 0);
        }
    }
}

void AudioClipView::sampleNeedsReRendering(Sample* sample) {
    if (sample == getSample()) {
        uiNeedsRendering(this, 0xFFFFFFFF, 0);
    }
}

void AudioClipView::selectEncoderAction(int8_t offset) {
    if (currentUIMode) {
        return;
    }
    auto ao = (AudioOutput*)getCurrentAudioClip()->output;
    ao->scrollAudioOutputMode(offset);
}

void AudioClipView::setClipLengthEqualToSampleLength() {
    AudioClip* audioClip = getCurrentAudioClip();
    SamplePlaybackGuide guide = audioClip->guide;
    SampleHolder* sampleHolder = (SampleHolder*)guide.audioFileHolder;
    if (sampleHolder) {
        adjustLoopLength(sampleHolder->getLoopLengthAtSystemSampleRate(true));
        display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CLIP_LENGTH_ADJUSTED));
    }
    else {
        display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_SAMPLE));
    }
}

void AudioClipView::adjustLoopLength(int32_t newLength) {
    int32_t oldLength = getCurrentClip()->loopLength;

    if (oldLength != newLength) {
        Action* action = nullptr;

        if (newLength > oldLength) {
            // lengthen clip
            if (newLength <= (uint32_t)kMaxSequenceLength) {
                action = lengthenClip(newLength);
doReRender:
                uiNeedsRendering(getRootUI(), 0xFFFFFFFF, 0);
            }
        }
        else if (newLength < oldLength) {
            // shorten clip
            if (newLength > 0) {
                action = shortenClip(newLength);
                if (!scrollLeftIfTooFarRight(newLength)) {
                    if (zoomToMax(true)) {
                        // ...
                    }
                    else {
                        goto doReRender;
                    }
                }
            }
        }

        displayNumberOfBarsAndBeats(newLength, currentSong->xZoom[NAVIGATION_CLIP], false, "LONG");
        if (action) {
            action->xScrollClip[AFTER] = currentSong->xScroll[NAVIGATION_CLIP];
        }
    }
}

ActionResult AudioClipView::horizontalEncoderAction(int32_t offset) {
    // SHIFT + X_ENC => "edit length without time stretching"
    if (isNoUIModeActive()
        && Buttons::isButtonPressed(deluge::hid::button::X_ENC)
        && Buttons::isShiftButtonPressed())
    {
        return editClipLengthWithoutTimestretching(offset);
    }
    else {
        return ClipView::horizontalEncoderAction(offset);
    }
}

ActionResult AudioClipView::editClipLengthWithoutTimestretching(int32_t offset) {
    if (!getCurrentClip()->currentlyScrollableAndZoomable()) {
        display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_EDIT_LENGTH));
        return ActionResult::DEALT_WITH;
    }

    if (scrollRightToEndOfLengthIfNecessary(getCurrentClip()->loopLength)) {
        return ActionResult::DEALT_WITH;
    }

    if (sdRoutineLock) {
        return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
    }

    int32_t oldLength = getCurrentClip()->loopLength;
    uint64_t oldLengthSamples = getCurrentAudioClip()->sampleHolder.getDurationInSamples(true);

    Action* action = nullptr;
    uint32_t newLength = changeClipLength(offset, oldLength, action);

    AudioClip* audioClip = getCurrentAudioClip();
    SamplePlaybackGuide guide = audioClip->guide;
    SampleHolder* sampleHolder = (SampleHolder*)guide.audioFileHolder;
    if (sampleHolder) {
        Sample* sample = static_cast<Sample*>(sampleHolder->audioFile);
        if (sample) {
            changeUnderlyingSampleLength(audioClip, sample, newLength, oldLength, oldLengthSamples);
        }
    }

    displayNumberOfBarsAndBeats(newLength, currentSong->xZoom[NAVIGATION_CLIP], false, "LONG");
    if (action) {
        action->xScrollClip[AFTER] = currentSong->xScroll[NAVIGATION_CLIP];
    }
    return ActionResult::DEALT_WITH;
}

ActionResult AudioClipView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
    if (!currentUIMode && Buttons::isShiftButtonPressed()
        && !Buttons::isButtonPressed(deluge::hid::button::Y_ENC))
    {
        if (inCardRoutine && !allowSomeUserActionsEvenWhenInCardRoutine) {
            return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
        }
        getCurrentAudioClip()->colourOffset += offset;
        uiNeedsRendering(this, 0xFFFFFFFF, 0);
    }
    return ActionResult::DEALT_WITH;
}

bool AudioClipView::setupScroll(uint32_t oldScroll) {
    // If code anywhere else clamps xScroll to >= 0,
    // then you won't see the columns to the left of tick 0
    // (which is crucial for "un‐cropping").
    // This function mostly just calls the parent:
    if (!getCurrentAudioClip()->currentlyScrollableAndZoomable()) {
        return false;
    }
    return ClipView::setupScroll(oldScroll);
}

void AudioClipView::tellMatrixDriverWhichRowsContainSomethingZoomable() {
    memset(PadLEDs::transitionTakingPlaceOnRow, 1, sizeof(PadLEDs::transitionTakingPlaceOnRow));
}

uint32_t AudioClipView::getMaxLength() {
    // The original code returned loopLength if no marker,
    // or loopLength+1 if endMarker visible. We'll do the same:
    if (endMarkerVisible) {
        return getCurrentClip()->loopLength + 1;
    }
    return getCurrentClip()->loopLength;
}

uint32_t AudioClipView::getMaxZoom() {
    int32_t maxZoom = getCurrentClip()->getMaxZoom();
    if (endMarkerVisible && maxZoom < 1073741824) {
        maxZoom <<= 1;
    }
    return maxZoom;
}
