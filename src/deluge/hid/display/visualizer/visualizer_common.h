/*
 * Copyright (c) 2025 Bruce Zawadzki (Tone Coder)
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

#include "NE10.h"
#include "definitions_cxx.hpp"
#include "gui/views/view.h"
#include "hid/button.h"
#include "hid/display/display.h"
#include "model/settings/runtime_feature_settings.h"

// Forward declarations
class Clip;

namespace oled_canvas {
class Canvas;
}

namespace deluge::hid::display {

// FFT computation result structure
struct FFTResult {
	ne10_fft_cpx_int32_t* output;       // Mono FFT output (for backward compatibility)
	ne10_fft_cpx_int32_t* output_left;  // Left channel FFT output
	ne10_fft_cpx_int32_t* output_right; // Right channel FFT output
	bool isValid;
	bool isSilent;
	bool isStereo; // Whether stereo FFT results are available

	FFTResult(ne10_fft_cpx_int32_t* output = nullptr, ne10_fft_cpx_int32_t* output_left = nullptr,
	          ne10_fft_cpx_int32_t* output_right = nullptr, bool isValid = false, bool isSilent = false,
	          bool isStereo = false)
	    : output(output), output_left(output_left), output_right(output_right), isValid(isValid), isSilent(isSilent),
	      isStereo(isStereo) {}
};

// Shared constants
constexpr int32_t kDisplayMargin = 2;        // Standard margin for visualizer display areas
constexpr int32_t kDcBinReductionFactor = 4; // DC bin (bin 0) magnitude reduction factor (75% reduction)

// Amplitude computation constants
constexpr float kReferenceAmplitude = 10000.0f; // Q15 reference for moderate audio levels
constexpr uint32_t kAmplitudeSampleCount = 256; // Number of samples to analyze for amplitude

// Silence detection constants (mirrored from Visualizer class)
constexpr uint32_t kSilenceTimeoutSamples = 44100; // 1 second at 44.1kHz

// Helper functions
uint32_t getVisualizerReadStartPos(uint32_t sample_count);
float applyVisualizerCompression(float amplitude, float frequency);

// Compute current audio amplitude from recent samples (peak detection)
float computeCurrentAmplitude();

// Audio Sampling and Silence Detection Helpers

/**
 * Check if a clip is valid for visualizer audio sampling
 * @param clip The clip to validate
 * @return true if clip supports visualizer audio sampling (Synth, Kit, or Audio clips)
 */
bool isValidClipForAudioSampling(Clip* clip);

/**
 * Determine which silence timer to use based on current visualizer mode
 * @param visualizer_mode Current visualizer mode
 * @param is_clip_mode Whether we're in clip mode
 * @return Reference to the appropriate silence timer
 */
uint32_t& getAppropriateSilenceTimer(uint32_t visualizer_mode, bool is_clip_mode);

/**
 * Check if visualizer should be silenced based on current mode and timers
 * @param visualizer_mode Current visualizer mode
 * @param is_clip_mode Whether we're in clip mode
 * @return true if visualizer should be silenced
 */
bool shouldSilenceVisualizer(uint32_t visualizer_mode, bool is_clip_mode);

// Buffer Management Helpers

/**
 * Safely clear all visualizer sample buffers and reset positions
 */
void clearAllVisualizerBuffers();

/**
 * Reset visualizer buffer positions and counters to initial state
 */
void resetVisualizerBufferState();

/**
 * Validate if current clip context allows visualizer display
 * @param in_clip_context Whether we're in clip context
 * @param toggle_enabled Whether visualizer toggle is enabled
 * @param current_clip Current clip for visualization
 * @return true if visualizer should be enabled in clip context
 */
bool validateClipContextForVisualizer(bool in_clip_context, bool toggle_enabled, Clip* current_clip);

/**
 * Check if current UI context should disable visualizer display
 * @return true if visualizer should be disabled (automation/performance views)
 */
bool shouldDisableVisualizerForCurrentUI();

// Button Action Helpers

/**
 * Handle visualizer mode switching buttons (SYNTH/KIT/MIDI/CV) in session/arranger views
 * @param button The button that was pressed
 * @param view The current view containing VU meter state
 * @return ActionResult indicating if the button was handled
 */
ActionResult handleVisualizerModeButton(deluge::hid::Button button, View& view);

/**
 * Check if visualizer mode buttons should be active for the current UI state
 * @param view The current view containing VU meter state
 * @return true if visualizer mode buttons should respond
 */
bool shouldHandleVisualizerModeButtons(View& view);

// Popup Display Helpers

/**
 * Display popup text, choosing between popup and direct rendering based on visualizer state
 * @param text The text to display
 * @param view The current view containing VU meter state
 * @param popupType The type of popup (defaults to GENERAL)
 */
void displayConditionalPopup(const char* text, View& view, PopupType popupType = PopupType::GENERAL);

/**
 * Display popup text with auto-choice between popup and direct rendering
 * @param text The text to display in popup
 * @param directText The text to display via direct rendering (for OLED)
 * @param view The current view containing VU meter state
 * @param popupType The type of popup (defaults to GENERAL)
 */
void displayConditionalPopup(const char* text, const char* directText, View& view,
                             PopupType popupType = PopupType::GENERAL);

/**
 * Cancel popup if visualizer is active (handles visualizer-specific popup cleanup)
 * @param view The current view containing VU meter state
 */
void cancelPopupIfVisualizerActive(View& view);

// Mod Knob Mode Extraction Helpers

/**
 * Extract mod knob mode from a view's active mod controllable
 * @param view The view to extract mod knob mode from
 * @return The mod knob mode, or 0 if no mod controllable
 */
int32_t extractModKnobMode(View& view);

/**
 * Extract mod knob mode from a mod controllable
 * @param modControllable The mod controllable to extract mode from
 * @return The mod knob mode, or 0 if nullptr
 */
int32_t extractModKnobMode(ModControllable* modControllable);

} // namespace deluge::hid::display
