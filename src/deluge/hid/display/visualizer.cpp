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

#include "hid/display/visualizer.h"
#include "deluge/model/settings/runtime_feature_settings.h"
#include "extern.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "gui/views/view.h"
#include "hid/display/visualizer/visualizer_equalizer.h"
#include "hid/display/visualizer/visualizer_spectrum.h"
#include "hid/display/visualizer/visualizer_waveform.h"
#include "modulation/params/param.h"
#include <atomic>

namespace deluge::hid::display {

/// Render visualizer waveform or spectrum on OLED display
void Visualizer::renderVisualizer(oled_canvas::Canvas& canvas) {
	// Check visualizer mode
	uint32_t visualizer_mode = getMode();

	if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerSpectrum) {
		// Render spectrum using FFT
		::deluge::hid::display::renderVisualizerSpectrum(canvas);
		return;
	}
	if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerEqualizer) {
		// Render equalizer using FFT
		::deluge::hid::display::renderVisualizerEqualizer(canvas);
		return;
	}
	// Default to waveform rendering (for VisualizerWaveform)
	::deluge::hid::display::renderVisualizerWaveform(canvas);
}

/// Render waveform visualization
/// @param canvas The OLED canvas to render to
void Visualizer::renderVisualizerWaveform(oled_canvas::Canvas& canvas) {
	::deluge::hid::display::renderVisualizerWaveform(canvas);
}

/// Render spectrum visualization using FFT
/// @param canvas The OLED canvas to render to
void Visualizer::renderVisualizerSpectrum(oled_canvas::Canvas& canvas) {
	::deluge::hid::display::renderVisualizerSpectrum(canvas);
}

/// Render equalizer visualization with 16 frequency bands
/// @param canvas The OLED canvas to render to
void Visualizer::renderVisualizerEqualizer(oled_canvas::Canvas& canvas) {
	::deluge::hid::display::renderVisualizerEqualizer(canvas);
}

/// Check if visualizer should be rendered and render it if conditions are met
bool Visualizer::potentiallyRenderVisualizer(oled_canvas::Canvas& canvas) {
	int32_t mod_knob_mode = 0;
	if (view.activeModControllableModelStack.modControllable != nullptr) {
		mod_knob_mode = *view.activeModControllableModelStack.modControllable->getModKnobMode();
	}

	return potentiallyRenderVisualizer(canvas, view.displayVUMeter, isEnabled(),
	                                   view.activeModControllableModelStack.modControllable, mod_knob_mode);
}

/// Check if visualizer should be rendered and render it if conditions are met
bool Visualizer::potentiallyRenderVisualizer(oled_canvas::Canvas& canvas, View& view) {
	int32_t mod_knob_mode = 0;
	if (view.activeModControllableModelStack.modControllable != nullptr) {
		mod_knob_mode = *view.activeModControllableModelStack.modControllable->getModKnobMode();
	}

	return potentiallyRenderVisualizer(canvas, view.displayVUMeter, isEnabled(),
	                                   view.activeModControllableModelStack.modControllable, mod_knob_mode);
}

/// Check if visualizer should be rendered and render it if conditions are met
bool Visualizer::potentiallyRenderVisualizer(oled_canvas::Canvas& canvas, bool displayVUMeter, bool visualizer_enabled,
                                             ModControllable* modControllable, int32_t mod_knob_mode) {
	// Check if visualizer feature is enabled in Waveform, Spectrum, or Equalizer mode in runtime settings
	if (visualizer_enabled) {
		// Visualizer engages automatically with VU meter or when toggle is enabled
		if ((displayVUMeter && modControllable != nullptr && mod_knob_mode == 0) || visualizer_toggle_enabled) {
			if (!display_visualizer) {
				display_visualizer = true;
			}
			renderVisualizer(canvas);
			return true;
		}
	}

	// If visualizer should be displayed but conditions aren't met, disable it
	if (display_visualizer && (!visualizer_enabled || (!displayVUMeter && !visualizer_toggle_enabled))) {
		display_visualizer = false;
	}
	return false;
}

/// Request OLED refresh for visualizer if active
void Visualizer::requestVisualizerUpdateIfNeeded() {
	requestVisualizerUpdateIfNeeded(view.displayVUMeter, isEnabled());
}

/// Request OLED refresh for visualizer if active
void Visualizer::requestVisualizerUpdateIfNeeded(View& view) {
	requestVisualizerUpdateIfNeeded(view.displayVUMeter, isEnabled());
}

void Visualizer::requestVisualizerUpdateIfNeeded(bool displayVUMeter, bool visualizer_enabled) {
	// Check if visualizer should be active (VU meter conditions OR toggle conditions)
	if (visualizer_enabled && (displayVUMeter || visualizer_toggle_enabled)) {
		// Enable visualizer if conditions are met
		if (!display_visualizer) {
			display_visualizer = true;
		}

		// All visualizers use 30fps (frame_skip = 2)
		constexpr uint32_t frame_skip = 2;

		// Request OLED update at 30fps
		visualizer_frame_counter++;
		if (visualizer_frame_counter >= frame_skip) {
			visualizer_frame_counter = 0;
			renderUIsForOled();
		}
		return;
	}
	// Disable visualizer if conditions aren't met
	if (display_visualizer) {
		display_visualizer = false;
	}
}

void Visualizer::reset() {
	display_visualizer = false;
	// Don't reset session_visualizer_mode here - it should persist within the same song
	// and only reset when loading a new song
	visualizer_frame_counter = 0;
}

void Visualizer::setEnabled(bool enabled) {
	display_visualizer = enabled;
}

bool Visualizer::isDisplaying() {
	return display_visualizer;
}

bool Visualizer::isEnabled() {
	uint32_t visualizer_mode = runtimeFeatureSettings.get(RuntimeFeatureSettingType::Visualizer);
	return (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerWaveform)
	       || (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerSpectrum)
	       || (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerEqualizer);
}

/// Check if visualizer is actively running
bool Visualizer::isActive(View& view) {
	return isActive(view.displayVUMeter);
}

bool Visualizer::isActive(bool displayVUMeter) {
	return isEnabled() && (displayVUMeter || visualizer_toggle_enabled);
}

uint32_t Visualizer::getMode() {
	// Return session mode if set, otherwise return community setting
	if (session_visualizer_mode != RuntimeFeatureStateVisualizer::VisualizerOff) {
		return session_visualizer_mode;
	}
	return runtimeFeatureSettings.get(RuntimeFeatureSettingType::Visualizer);
}

void Visualizer::setSessionMode(uint32_t mode) {
	session_visualizer_mode = mode;
}

void Visualizer::clearSessionMode() {
	session_visualizer_mode = RuntimeFeatureStateVisualizer::VisualizerOff;
}

/// Reset session visualizer mode when loading a new song
void Visualizer::resetSessionMode() {
	session_visualizer_mode = RuntimeFeatureStateVisualizer::VisualizerOff;
	visualizer_toggle_enabled = false;
}

/// Set whether visualizer toggle is enabled (independent of VU meter)
/// @param enabled Whether visualizer toggle should be enabled
void Visualizer::setToggleEnabled(bool enabled) {
	visualizer_toggle_enabled = enabled;
}

/// Get whether visualizer toggle is currently enabled
/// @return true if visualizer toggle is enabled
bool Visualizer::isToggleEnabled() {
	return visualizer_toggle_enabled;
}

void Visualizer::sampleAudioForDisplay(deluge::dsp::StereoBuffer<q31_t> renderingBuffer, size_t numSamples) {
	// Sample audio for visualizer visualization (downsample for efficiency)
	// Only sample if visualizer feature is enabled in Waveform, Spectrum, or Equalizer mode to save CPU cycles
	if (isEnabled()) {
		// We previously captured only one sample per audio render block which
		// down-sampled the signal by roughly the block-size (typically 128
		// samples).  This reduced the effective sample-rate to <1 kHz and pushed
		// every musical frequency into the lowest few FFT bins, making the
		// spectrum and equalizer appear several bars to the right.
		//
		// Fix: iterate over the whole block and grab every N-th sample.  With
		// visualizer_sample_interval = 2 the visualizer now runs at 22.05 kHz –
		// high enough to display up to 11 kHz (well above the Deluge’s OLED
		// bandwidth) while still cutting the memory writes in half.

		constexpr uint32_t visualizer_sample_interval = 2; // Keep CPU usage modest
		constexpr uint32_t q31_to_q15_shift = 16;          // Convert Q31 → Q15 (15 fractional bits)

		for (size_t i = 0; i < numSamples; i += visualizer_sample_interval) {
			// Combine stereo channels and convert to Q15
			int32_t sample_l = renderingBuffer[i].l >> q31_to_q15_shift;
			int32_t sample_r = renderingBuffer[i].r >> q31_to_q15_shift;
			int32_t combined = (sample_l + sample_r) >> 1;

			// Write into the circular buffer
			uint32_t write_pos = visualizer_write_pos.load(std::memory_order_acquire);
			visualizer_sample_buffer[write_pos] = combined;
			visualizer_write_pos.store((write_pos + 1) % kVisualizerBufferSize, std::memory_order_release);
			if (visualizer_sample_count.load(std::memory_order_acquire) < kVisualizerBufferSize) {
				visualizer_sample_count.fetch_add(1, std::memory_order_release);
			}
		}
	}
}

} // namespace deluge::hid::display
