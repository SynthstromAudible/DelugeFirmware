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
#include "deluge/model/clip/clip.h"
#include "deluge/model/settings/runtime_feature_settings.h"
#include "extern.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/display/visualizer/visualizer_bar_spectrum.h"
#include "hid/display/visualizer/visualizer_cube.h"
#include "hid/display/visualizer/visualizer_line_spectrum.h"
#include "hid/display/visualizer/visualizer_midi_piano_roll.h"
#include "hid/display/visualizer/visualizer_pulsegrid.h"
#include "hid/display/visualizer/visualizer_skyline.h"
#include "hid/display/visualizer/visualizer_starfield.h"
#include "hid/display/visualizer/visualizer_stereo_bar_spectrum.h"
#include "hid/display/visualizer/visualizer_stereo_line_spectrum.h"
#include "hid/display/visualizer/visualizer_tunnel.h"
#include "hid/display/visualizer/visualizer_waveform.h"
#include "modulation/params/param.h"
#include <atomic>

namespace deluge::hid::display {

/// Render visualizer waveform or spectrum on OLED display (default implementation)
void Visualizer::renderVisualizerDefault(oled_canvas::Canvas& canvas) {
	// Check visualizer mode
	uint32_t visualizer_mode = getMode();

	if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerLineSpectrum) {
		// Render spectrum using FFT
		::deluge::hid::display::renderVisualizerLineSpectrum(canvas);
		return;
	}
	if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerBarSpectrum) {
		// Render equalizer using FFT
		::deluge::hid::display::renderVisualizerBarSpectrum(canvas);
		return;
	}
	if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerCube) {
		// Render 3D cube using FFT
		::deluge::hid::display::renderVisualizerCube(canvas);
		return;
	}
	if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerStereoLineSpectrum) {
		// Render stereo spectrum using FFT
		::deluge::hid::display::renderVisualizerStereoLineSpectrum(canvas);
		return;
	}
	if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerStereoBarSpectrum) {
		// Render stereo 8-band equalizer using FFT
		::deluge::hid::display::renderVisualizerStereoBarSpectrum(canvas);
		return;
	}
	if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerTunnel) {
		// Render tunnel visualization
		::deluge::hid::display::renderVisualizerTunnel(canvas);
		return;
	}
	if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerStarfield) {
		// Render starfield visualization
		::deluge::hid::display::renderVisualizerStarfield(canvas);
		return;
	}
	if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerSkyline) {
		// Render skyline visualization
		::deluge::hid::display::renderVisualizerSkyline(canvas);
		return;
	}
	if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerPulseGrid) {
		// Render pulse grid visualization
		::deluge::hid::display::renderVisualizerPulseGrid(canvas);
		return;
	}
	if (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerMidiPianoRoll) {
		// Render MIDI piano roll visualization
		::deluge::hid::display::renderVisualizerMidiPianoRoll(canvas);
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
void Visualizer::renderVisualizerLineSpectrum(oled_canvas::Canvas& canvas) {
	::deluge::hid::display::renderVisualizerLineSpectrum(canvas);
}

/// Render equalizer visualization with 16 frequency bands
/// @param canvas The OLED canvas to render to
void Visualizer::renderVisualizerBarSpectrum(oled_canvas::Canvas& canvas) {
	::deluge::hid::display::renderVisualizerBarSpectrum(canvas);
}

/// Render stereo spectrum visualization
/// @param canvas The OLED canvas to render to
void Visualizer::renderVisualizerStereoLineSpectrum(oled_canvas::Canvas& canvas) {
	::deluge::hid::display::renderVisualizerStereoLineSpectrum(canvas);
}

/// Render stereo 8-band equalizer visualization
/// @param canvas The OLED canvas to render to
void Visualizer::renderVisualizerStereoBarSpectrum(oled_canvas::Canvas& canvas) {
	::deluge::hid::display::renderVisualizerStereoBarSpectrum(canvas);
}

/// Render tunnel visualization
/// @param canvas The OLED canvas to render to
void Visualizer::renderVisualizerTunnel(oled_canvas::Canvas& canvas) {
	::deluge::hid::display::renderVisualizerTunnel(canvas);
}

/// Render starfield visualization
/// @param canvas The OLED canvas to render to
void Visualizer::renderVisualizerStarfield(oled_canvas::Canvas& canvas) {
	::deluge::hid::display::renderVisualizerStarfield(canvas);
}

/// Render skyline visualization
/// @param canvas The OLED canvas to render to
void Visualizer::renderVisualizerSkyline(oled_canvas::Canvas& canvas) {
	::deluge::hid::display::renderVisualizerSkyline(canvas);
}

/// Render pulse grid visualization
/// @param canvas The OLED canvas to render to
void Visualizer::renderVisualizerPulseGrid(oled_canvas::Canvas& canvas) {
	::deluge::hid::display::renderVisualizerPulseGrid(canvas);
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
		// Visualizer engages automatically with VU meter (session/arranger only) or when toggle is enabled (all views)
		bool shouldEnable =
		    (displayVUMeter && modControllable != nullptr && mod_knob_mode == 0) || visualizer_toggle_enabled;

		if (shouldEnable) {
			if (!display_visualizer) {
				display_visualizer = true;
			}
			renderVisualizer(canvas);
			return true;
		}
	}

	// If visualizer should be displayed but conditions aren't met, disable it
	bool shouldDisable = !visualizer_enabled || (!displayVUMeter && !visualizer_toggle_enabled);

	if (display_visualizer && shouldDisable) {
		display_visualizer = false;
		// Reset popup flag when visualizer is disabled
		clip_program_popup_shown = false;
	}
	return false;
}

/// Main entry point for rendering visualizer
void Visualizer::renderVisualizer(oled_canvas::Canvas& canvas) {
	// Check if we're in clip mode - if so, force Waveform mode
	if (isClipMode()) {
		// Always use Waveform mode in clip view
		::deluge::hid::display::renderVisualizerWaveform(canvas);
		return;
	}

	// Normal mode selection for session/arranger views - delegate to default implementation
	renderVisualizerDefault(canvas);
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
	clip_program_popup_shown = false;

	// Clear clip visualizer state when switching views
	// (this will be called when exiting clip view or switching to other views)
	setCurrentClipForVisualizer(nullptr);
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
	       || (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerLineSpectrum)
	       || (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerBarSpectrum)
	       || (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerCube)
	       || (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerStereoLineSpectrum)
	       || (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerStereoBarSpectrum)
	       || (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerTunnel)
	       || (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerStarfield)
	       || (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerSkyline)
	       || (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerPulseGrid)
	       || (visualizer_mode == RuntimeFeatureStateVisualizer::VisualizerMidiPianoRoll);
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
	cv_visualizer_mode =
	    RuntimeFeatureStateVisualizer::VisualizerBarSpectrum; // Reset to Bar Spectrum (default special visualizer)
	visualizer_toggle_enabled = false;
}

uint32_t Visualizer::getCVVisualizerMode() {
	return cv_visualizer_mode;
}

void Visualizer::setCVVisualizerMode(uint32_t mode) {
	cv_visualizer_mode = mode;
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
	// Only sample if visualizer feature is enabled AND not in clip mode (to avoid conflicts with clip-specific
	// sampling) Check if there's a current clip set for visualization - if so, we're in clip mode and should skip
	// full mix sampling
	if (isEnabled() && current_clip_for_visualizer.load(std::memory_order_acquire) == nullptr) {
		// Sample every N-th sample from the audio block for efficiency

		constexpr uint32_t visualizer_sample_interval = 2; // Keep CPU usage modest
		constexpr uint32_t q31_to_q15_shift = 16;          // Convert Q31 → Q15 (15 fractional bits)

		for (size_t i = 0; i < numSamples; i += visualizer_sample_interval) {
			// Convert stereo channels to Q15
			int32_t sample_l = renderingBuffer[i].l >> q31_to_q15_shift;
			int32_t sample_r = renderingBuffer[i].r >> q31_to_q15_shift;
			int32_t combined = (sample_l + sample_r) >> 1;

			// Write into the circular buffers
			uint32_t write_pos = visualizer_write_pos.load(std::memory_order_acquire);
			visualizer_sample_buffer_left[write_pos] = sample_l;
			visualizer_sample_buffer_right[write_pos] = sample_r;
			// Keep mono buffer for backward compatibility with existing visualizers
			visualizer_sample_buffer[write_pos] = combined;
			visualizer_write_pos.store((write_pos + 1) % kVisualizerBufferSize, std::memory_order_release);
			if (visualizer_sample_count.load(std::memory_order_acquire) < kVisualizerBufferSize) {
				visualizer_sample_count.fetch_add(1, std::memory_order_release);
			}
		}
	}
}

void Visualizer::sampleAudioForClipDisplay(deluge::dsp::StereoBuffer<q31_t> renderingBuffer, size_t numSamples,
                                           Clip* clip) {
	// Sample clip audio for visualizer visualization (same logic as full mix sampling)
	// Only sample if visualizer is enabled AND in clip view AND this clip matches current clip for visualization
	if (isEnabled() && clip == getCurrentClipForVisualizer()) {
		// Check if we're in instrument clip view with a Synth/Kit clip
		bool isInClipView = (getCurrentUI() == &instrumentClipView);
		bool isSynthOrKitClip = (clip->type == ClipType::INSTRUMENT
		                         && (clip->output->type == OutputType::SYNTH || clip->output->type == OutputType::KIT));

		if (isInClipView && isSynthOrKitClip) {
			constexpr uint32_t visualizer_sample_interval = 2; // Keep CPU usage modest
			constexpr uint32_t q31_to_q15_shift = 16;          // Convert Q31 → Q15 (15 fractional bits)

			for (size_t i = 0; i < numSamples; i += visualizer_sample_interval) {
				// Convert stereo channels to Q15
				int32_t sample_l = renderingBuffer[i].l >> q31_to_q15_shift;
				int32_t sample_r = renderingBuffer[i].r >> q31_to_q15_shift;
				int32_t combined = (sample_l + sample_r) >> 1;

				// Write into the circular buffers (reuse existing buffers)
				uint32_t write_pos = visualizer_write_pos.load(std::memory_order_acquire);
				visualizer_sample_buffer_left[write_pos] = sample_l;
				visualizer_sample_buffer_right[write_pos] = sample_r;
				// Keep mono buffer for backward compatibility with existing visualizers
				visualizer_sample_buffer[write_pos] = combined;
				visualizer_write_pos.store((write_pos + 1) % kVisualizerBufferSize, std::memory_order_release);
				if (visualizer_sample_count.load(std::memory_order_acquire) < kVisualizerBufferSize) {
					visualizer_sample_count.fetch_add(1, std::memory_order_release);
				}
			}
		}
	}
}

/// Check if visualizer should display clip-specific audio vs. full mix
/// @return true if in clip mode (instrument clip view with Synth/Kit clip)
bool Visualizer::isClipMode() {
	if (getCurrentUI() != &instrumentClipView) {
		return false;
	}

	Clip* currentClip = getCurrentClipForVisualizer();
	if (!currentClip) {
		return false;
	}

	// Only enable for Instrument clips with Synth/Kit outputs
	return (currentClip->type == ClipType::INSTRUMENT
	        && (currentClip->output->type == OutputType::SYNTH || currentClip->output->type == OutputType::KIT));
}

/// Check if clip visualizer is actively running
/// @param displayVUMeter Whether VU meter is enabled
/// @return true if clip visualizer is active
bool Visualizer::isClipVisualizerActive(bool displayVUMeter) {
	return isClipMode() && isEnabled() && (displayVUMeter || visualizer_toggle_enabled);
}

/// Display program name popup when entering clip visualizer mode
void Visualizer::displayClipProgramNamePopup() {
	Clip* currentClip = getCurrentClipForVisualizer();
	if (currentClip && currentClip->output) {
		// Get the program name from the output
		std::string_view programName = currentClip->output->name.get();
		if (!programName.empty()) {
			::display->displayPopup(programName.data());
		}
	}
	clip_program_popup_shown = true;
}

/// Set the current clip for visualizer (called by UI thread)
/// @param clip Pointer to the current clip, or nullptr to clear
void Visualizer::setCurrentClipForVisualizer(Clip* clip) {
	Clip* previousClip = current_clip_for_visualizer.load(std::memory_order_acquire);
	current_clip_for_visualizer.store(clip, std::memory_order_release);

	// Reset popup flag when switching clips
	if (clip != previousClip) {
		clip_program_popup_shown = false;
	}
}

/// Get the current clip for visualizer (thread-safe)
/// @return Pointer to the current clip, or nullptr
Clip* Visualizer::getCurrentClipForVisualizer() {
	return current_clip_for_visualizer.load(std::memory_order_acquire);
}

/// Get display name for a visualizer mode
/// @param mode The visualizer mode
/// @return String containing the display name
std::string_view Visualizer::getModeDisplayName(uint32_t mode) {
	switch (mode) {
	case RuntimeFeatureStateVisualizer::VisualizerWaveform:
		return "WAVEFORM";
	case RuntimeFeatureStateVisualizer::VisualizerLineSpectrum:
		return "LINE SPECTRUM";
	case RuntimeFeatureStateVisualizer::VisualizerBarSpectrum:
		return "BAR SPECTRUM";
	case RuntimeFeatureStateVisualizer::VisualizerCube:
		return "CUBE";
	case RuntimeFeatureStateVisualizer::VisualizerStereoLineSpectrum:
		return "STEREO LINE SPECTRUM";
	case RuntimeFeatureStateVisualizer::VisualizerStereoBarSpectrum:
		return "STEREO BAR SPECTRUM";
	case RuntimeFeatureStateVisualizer::VisualizerTunnel:
		return "TUNNEL";
	case RuntimeFeatureStateVisualizer::VisualizerStarfield:
		return "STARFIELD";
	case RuntimeFeatureStateVisualizer::VisualizerSkyline:
		return "SKYLINE";
	case RuntimeFeatureStateVisualizer::VisualizerPulseGrid:
		return "PULSE GRID";
	case RuntimeFeatureStateVisualizer::VisualizerMidiPianoRoll:
		return "MIDI PIANO ROLL";
	default:
		return "UNKNOWN";
	}
}

} // namespace deluge::hid::display
