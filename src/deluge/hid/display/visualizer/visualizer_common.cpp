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

#include "visualizer_common.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "hid/button.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/display/visualizer.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/mod_controllable/mod_controllable.h"
#include "model/output.h"
#include "processing/engines/audio_engine.h"
#include <algorithm>
#include <cmath>

// currentUIMode is declared in gui/ui/ui.h

namespace deluge::hid::display {

// Shared constants
namespace {
// Visual compression parameters
constexpr float kCompressionExponent = 0.5f;         // 2:1 compression ratio
constexpr float kFrequencyBoostExponent = 0.1f;      // Treble emphasis bias
constexpr float kFrequencyNormalizationHz = 1000.0f; // Frequency normalization base
constexpr float kAmplitudeBoost = 1.0f;              // Amplitude boost

// Display constants
constexpr int32_t kDisplayMargin = 2; // Standard margin for visualizer display areas
} // namespace

// Helper function to get read start position from circular buffer for most recent samples
uint32_t getVisualizerReadStartPos(uint32_t sample_count) {
	uint32_t write_pos = 0;
	write_pos = Visualizer::visualizer_write_pos.load(std::memory_order_acquire);

	// Start reading from the most recent samples (just before write_pos)
	// Go back by sample_count to get the most recent samples, wrapping around the circular buffer
	return (write_pos - sample_count) % Visualizer::kVisualizerBufferSize;
}

// Apply music sweet-spot visual compression to amplitude and frequency
// Formula: ((amplitude^kCompressionExponent) * ((frequency/kFrequencyNormalizationHz)^kFrequencyBoostExponent)) *
// kAmplitudeBoost The compression exponent provides 2:1 visual compression (square root scaling) The frequency boost
// term provides industry standard treble emphasis (reduces bass/treble extremes) The amplitude boost increases overall
// visual amplitude by 10%
float applyVisualizerCompression(float amplitude, float frequency) {
	// Normalize amplitude to 0-1 range if not already
	amplitude = std::clamp(amplitude, 0.0f, 1.0f);
	return (std::pow(amplitude, kCompressionExponent)
	        * std::pow(frequency / kFrequencyNormalizationHz, kFrequencyBoostExponent))
	       * kAmplitudeBoost;
}

// Compute current audio amplitude from recent samples (peak detection)
// Returns normalized amplitude in range [0, 1]
float computeCurrentAmplitude() {
	float current_amplitude = 0.0f;
	uint32_t sample_count = Visualizer::visualizer_sample_count.load(std::memory_order_acquire);
	if (sample_count < 2) {
		return 0.0f;
	}
	uint32_t read_start_pos = getVisualizerReadStartPos(sample_count);
	float peak_amplitude = 0.0f;

	for (uint32_t i = 0; i < kAmplitudeSampleCount && i < sample_count; i++) {
		uint32_t buffer_index = (read_start_pos + i) % Visualizer::kVisualizerBufferSize;
		int32_t sample = Visualizer::visualizer_sample_buffer[buffer_index];
		auto abs_sample = static_cast<float>(std::abs(sample));
		peak_amplitude = std::max(peak_amplitude, abs_sample);
	}

	current_amplitude = peak_amplitude / kReferenceAmplitude;

	// Clamp to valid range [0, 1]
	return std::clamp(current_amplitude, 0.0f, 1.0f);
}

// Audio Sampling and Silence Detection Helpers

bool isValidClipForAudioSampling(Clip* clip) {
	if (clip == nullptr) {
		return false;
	}

	// Valid clip types: Synth, Kit, or Audio clips
	return ((clip->type == ClipType::INSTRUMENT
	         && (clip->output->type == OutputType::SYNTH || clip->output->type == OutputType::KIT))
	        || clip->type == ClipType::AUDIO);
}

uint32_t& getAppropriateSilenceTimer(uint32_t visualizer_mode, bool is_clip_mode) {
	// For MIDI piano roll, use MIDI note timer
	if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerMidiPianoRoll) {
		return Visualizer::midi_piano_roll_last_note_time;
	}
	// For clip mode, use clip visualizer timer
	if (is_clip_mode) {
		return Visualizer::clip_visualizer_last_audio_time;
	}
	// For global visualizer, use global timer
	return Visualizer::global_visualizer_last_audio_time;
}

bool shouldSilenceVisualizer(uint32_t visualizer_mode, bool is_clip_mode) {
	uint32_t& timer = getAppropriateSilenceTimer(visualizer_mode, is_clip_mode);
	uint32_t current_time = AudioEngine::audioSampleTimer;
	uint32_t time_since_last_audio = current_time - timer;

	return time_since_last_audio > kSilenceTimeoutSamples;
}

// Buffer Management Helpers

void clearAllVisualizerBuffers() {
	// Clear all sample buffers
	Visualizer::visualizer_sample_buffer.fill(0);
	Visualizer::visualizer_sample_buffer_left.fill(0);
	Visualizer::visualizer_sample_buffer_right.fill(0);

	// Reset buffer state
	resetVisualizerBufferState();
}

void resetVisualizerBufferState() {
	// Reset buffer positions and counters
	Visualizer::visualizer_write_pos.store(0, std::memory_order_release);
	Visualizer::visualizer_sample_count.store(0, std::memory_order_release);
}

bool validateClipContextForVisualizer(bool in_clip_context, bool toggle_enabled, Clip* current_clip) {
	// Must be in clip context, toggle must be enabled, and clip must be valid for audio sampling
	return in_clip_context && toggle_enabled && isValidClipForAudioSampling(current_clip);
}

/**
 * Check if current UI context should disable visualizer display
 * @return true if visualizer should be disabled (automation/performance views)
 */
bool shouldDisableVisualizerForCurrentUI() {
	// Don't show visualizer in automation view (including overview and editor modes)
	if (getRootUI() == &automationView) {
		return true;
	}

	// Don't show visualizer in performance mode
	if (getRootUI() == &performanceView) {
		return true;
	}

	return false;
}

// Button Action Helpers

ActionResult handleVisualizerModeButton(deluge::hid::Button button, View& view) {
	// Check if we should handle this button
	if (!shouldHandleVisualizerModeButtons(view)) {
		return ActionResult::NOT_DEALT_WITH;
	}

	switch (button) {
	case deluge::hid::button::SYNTH:
		deluge::hid::display::Visualizer::setSessionMode(RuntimeFeatureStateVisualizer::VisualizerWaveform);
		::display->displayPopup(
		    deluge::hid::display::Visualizer::getModeDisplayName(RuntimeFeatureStateVisualizer::VisualizerWaveform));
		return ActionResult::DEALT_WITH;

	case deluge::hid::button::KIT:
		deluge::hid::display::Visualizer::setSessionMode(RuntimeFeatureStateVisualizer::VisualizerLineSpectrum);
		::display->displayPopup(deluge::hid::display::Visualizer::getModeDisplayName(
		    RuntimeFeatureStateVisualizer::VisualizerLineSpectrum));
		return ActionResult::DEALT_WITH;

	case deluge::hid::button::MIDI:
		deluge::hid::display::Visualizer::setSessionMode(RuntimeFeatureStateVisualizer::VisualizerMidiPianoRoll);
		::display->displayPopup(deluge::hid::display::Visualizer::getModeDisplayName(
		    RuntimeFeatureStateVisualizer::VisualizerMidiPianoRoll));
		return ActionResult::DEALT_WITH;

	case deluge::hid::button::CV: {
		// CV button toggles to special visualizers: if viewing special visualizer, cycle to next; if viewing main
		// visualizer, return to last viewed special visualizer
		uint32_t current_session_mode = deluge::hid::display::Visualizer::getMode();

		// Check if currently viewing a special visualizer
		bool is_viewing_special =
		    (current_session_mode == RuntimeFeatureStateVisualizer::VisualizerBarSpectrum
		     || current_session_mode == RuntimeFeatureStateVisualizer::VisualizerStereoLineSpectrum
		     || current_session_mode == RuntimeFeatureStateVisualizer::VisualizerStereoBarSpectrum
		     || current_session_mode == RuntimeFeatureStateVisualizer::VisualizerCube
		     || current_session_mode == RuntimeFeatureStateVisualizer::VisualizerSkyline
		     || current_session_mode == RuntimeFeatureStateVisualizer::VisualizerStarfield
		     || current_session_mode == RuntimeFeatureStateVisualizer::VisualizerTunnel
		     || current_session_mode == RuntimeFeatureStateVisualizer::VisualizerPulseGrid);

		if (is_viewing_special) {
			// Already viewing a special visualizer, cycle to next one
			uint32_t next_mode = RuntimeFeatureStateVisualizer::VisualizerOff;
			switch (current_session_mode) {
			case RuntimeFeatureStateVisualizer::VisualizerBarSpectrum:
				next_mode = RuntimeFeatureStateVisualizer::VisualizerStereoLineSpectrum;
				break;
			case RuntimeFeatureStateVisualizer::VisualizerStereoLineSpectrum:
				next_mode = RuntimeFeatureStateVisualizer::VisualizerStereoBarSpectrum;
				break;
			case RuntimeFeatureStateVisualizer::VisualizerStereoBarSpectrum:
				next_mode = RuntimeFeatureStateVisualizer::VisualizerCube;
				break;
			case RuntimeFeatureStateVisualizer::VisualizerCube:
				next_mode = RuntimeFeatureStateVisualizer::VisualizerSkyline;
				break;
			case RuntimeFeatureStateVisualizer::VisualizerSkyline:
				next_mode = RuntimeFeatureStateVisualizer::VisualizerStarfield;
				break;
			case RuntimeFeatureStateVisualizer::VisualizerStarfield:
				next_mode = RuntimeFeatureStateVisualizer::VisualizerTunnel;
				break;
			case RuntimeFeatureStateVisualizer::VisualizerTunnel:
				next_mode = RuntimeFeatureStateVisualizer::VisualizerPulseGrid;
				break;
			case RuntimeFeatureStateVisualizer::VisualizerPulseGrid:
			default:
				next_mode = RuntimeFeatureStateVisualizer::VisualizerBarSpectrum;
				break;
			}

			deluge::hid::display::Visualizer::setSessionMode(next_mode);
			deluge::hid::display::Visualizer::setCVVisualizerMode(next_mode);
			::display->displayPopup(deluge::hid::display::Visualizer::getModeDisplayName(next_mode));
		}
		else {
			// Not viewing a special visualizer, switch to last viewed special visualizer
			uint32_t last_special_mode = deluge::hid::display::Visualizer::getCVVisualizerMode();
			deluge::hid::display::Visualizer::setSessionMode(last_special_mode);
			::display->displayPopup(deluge::hid::display::Visualizer::getModeDisplayName(last_special_mode));
			// cv_visualizer_mode stays the same
		}
		return ActionResult::DEALT_WITH;
	}

	default:
		return ActionResult::NOT_DEALT_WITH;
	}
}

bool shouldHandleVisualizerModeButtons(View& view) {
	// Handle visualizer mode switching when visualizer is active in Arranger or Song view
	UI* current_ui = getCurrentUI();
	return (current_ui == &sessionView || current_ui == &arrangerView)
	       && deluge::hid::display::Visualizer::isActive(view.displayVUMeter)
	       && currentUIMode != UI_MODE_CLIP_PRESSED_IN_SONG_VIEW
	       && currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION;
}

// Popup Display Helpers

void displayConditionalPopup(const char* text, View& view, PopupType popupType) {
	displayConditionalPopup(text, text, view, popupType);
}

void displayConditionalPopup(const char* text, const char* directText, View& view, PopupType popupType) {
	if (::display->haveOLED()) {
		if (deluge::hid::display::Visualizer::isActive(view.displayVUMeter)) {
			// Use popup for active visualizer users
			::display->popupText(text, popupType);
		}
		else {
			// Direct rendering for non-active visualizer users (original behavior)
			// Cancel any existing popup first
			::display->cancelPopup();
			deluge::hid::display::OLED::clearMainImage();
			deluge::hid::display::OLED::drawPermanentPopupLookingText(directText);
			deluge::hid::display::OLED::sendMainImage();
		}
	}
	else {
		// Seven segment display - always use popup
		::display->displayPopup(text, 1, true);
	}
}

void cancelPopupIfVisualizerActive(View& view) {
	if (::display->haveOLED() && deluge::hid::display::Visualizer::isActive(view.displayVUMeter)) {
		// Cancel any lingering popup when visualizer is active
		::display->cancelPopup();
	}
}

// Mod Knob Mode Extraction Helpers

int32_t extractModKnobMode(View& view) {
	if (view.activeModControllableModelStack.modControllable != nullptr) {
		uint8_t* mod_knob_mode_pointer = view.activeModControllableModelStack.modControllable->getModKnobMode();
		if (mod_knob_mode_pointer != nullptr) {
			return *mod_knob_mode_pointer;
		}
	}
	return 0; // Default to 0 when no mod controllable or no mode pointer
}

int32_t extractModKnobMode(ModControllable* modControllable) {
	if (modControllable != nullptr) {
		uint8_t* mod_knob_mode_pointer = modControllable->getModKnobMode();
		if (mod_knob_mode_pointer != nullptr) {
			return *mod_knob_mode_pointer;
		}
	}
	return 0; // Default to 0 when no mod controllable or no mode pointer
}

} // namespace deluge::hid::display
