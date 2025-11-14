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

#include "definitions_cxx.hpp"
#include "dsp/fft/fft_config_manager.h"
#include "dsp_ng/core/types.hpp"
#include "gui/views/view.h"
#include "hid/display/visualizer/visualizer_common.h"
#include "model/settings/runtime_feature_settings.h"
#include "oled.h"
#include "oled_canvas/canvas.h"
#include "processing/engines/audio_engine.h"
#include <array>
#include <atomic>

// Forward declarations
class ModControllable;

namespace deluge::hid::display {

/// Visualizer rendering utilities for OLED display
/// Renders waveform, spectrum, and equalizer visualizations
class Visualizer {
public:
	/// Main entry point for rendering visualizer
	/// @param canvas The OLED canvas to render to
	static void renderVisualizer(oled_canvas::Canvas& canvas);

	/// Render waveform visualization
	/// @param canvas The OLED canvas to render to
	static void renderVisualizerWaveform(oled_canvas::Canvas& canvas);

	/// Render spectrum visualization using FFT
	/// @param canvas The OLED canvas to render to
	static void renderVisualizerSpectrum(oled_canvas::Canvas& canvas);

	/// Render equalizer visualization with 16 frequency bands
	/// @param canvas The OLED canvas to render to
	static void renderVisualizerEqualizer(oled_canvas::Canvas& canvas);

	/// Check if visualizer should be rendered and render it if conditions are met
	/// @param canvas The OLED canvas to render to
	/// @return true if visualizer was rendered
	static bool potentiallyRenderVisualizer(oled_canvas::Canvas& canvas);

	/// Check if visualizer should be rendered and render it if conditions are met
	/// @param canvas The OLED canvas to render to
	/// @param view The current view containing VU meter and mod controllable state
	/// @return true if visualizer was rendered
	static bool potentiallyRenderVisualizer(oled_canvas::Canvas& canvas, View& view);

	/// Check if visualizer should be rendered and render it if conditions are met
	/// @param canvas The OLED canvas to render to
	/// @param displayVUMeter Whether VU meter is enabled
	/// @param visualizerEnabled Whether visualizer feature is enabled
	/// @param modControllable Current mod controllable
	/// @param mod_knob_mode Current mod knob mode
	/// @return true if visualizer was rendered
	static bool potentiallyRenderVisualizer(oled_canvas::Canvas& canvas, bool displayVUMeter, bool visualizer_enabled,
	                                        ModControllable* modControllable, int32_t mod_knob_mode);

	/// Request OLED refresh for visualizer if active
	static void requestVisualizerUpdateIfNeeded();

	/// Request OLED refresh for visualizer if active
	/// @param view The current view containing VU meter and mod controllable state
	static void requestVisualizerUpdateIfNeeded(View& view);

	/// Request OLED refresh for visualizer if active
	/// @param displayVUMeter Whether VU meter is enabled
	/// @param visualizerEnabled Whether visualizer feature is enabled
	static void requestVisualizerUpdateIfNeeded(bool displayVUMeter, bool visualizer_enabled);

	/// Reset visualizer state (called when switching views)
	static void reset();

	/// Set whether visualizer display is enabled
	/// @param enabled Whether visualizer should be enabled
	static void setEnabled(bool enabled);

	/// Get whether visualizer is currently displaying
	/// @return true if visualizer display is active
	static bool isDisplaying();

	/// Get whether visualizer feature is enabled in runtime settings
	/// @return true if visualizer is set to Waveform, Spectrum, or Equalizer mode
	static bool isEnabled();

	/// Get whether visualizer is active (feature enabled AND display conditions met)
	/// @param view The current view containing VU meter and mod controllable state
	/// @return true if visualizer is actively running
	static bool isActive(View& view);

	/// Get whether visualizer is active (feature enabled AND display conditions met)
	/// @param displayVUMeter Whether VU meter is enabled
	/// @return true if visualizer is actively running
	static bool isActive(bool displayVUMeter);

	/// Get current visualizer mode (session override or community setting)
	/// @return Current visualizer mode
	static uint32_t getMode();

	/// Set session visualizer mode (temporary override of community setting)
	/// @param mode The visualizer mode to set for this session
	static void setSessionMode(uint32_t mode);

	/// Clear session visualizer mode (revert to community setting)
	static void clearSessionMode();

	/// Reset session visualizer mode when loading a new song
	static void resetSessionMode();

	/// Set whether visualizer toggle is enabled (independent of VU meter)
	/// @param enabled Whether visualizer toggle should be enabled
	static void setToggleEnabled(bool enabled);

	/// Get whether visualizer toggle is currently enabled
	/// @return true if visualizer toggle is enabled
	static bool isToggleEnabled();

	/// Sample audio data for visualizer display (waveform, spectrum, equalizer)
	/// Performs downsampling and stores samples in the circular buffer for display
	/// @param renderingBuffer The audio buffer to sample from
	/// @param numSamples Number of samples in the buffer
	static void sampleAudioForDisplay(deluge::dsp::StereoBuffer<q31_t> renderingBuffer, size_t numSamples);

	/// Whether visualizer display is enabled
	inline static bool display_visualizer = false;

	/// Session visualizer mode (overrides community setting when set)
	/// Set to VisualizerOff when using community setting
	inline static uint32_t session_visualizer_mode = RuntimeFeatureStateVisualizer::VisualizerOff;

	/// Frame counter for update timing (all visualizers use 30fps)
	inline static uint32_t visualizer_frame_counter = 0;

	/// Whether visualizer toggle is enabled (independent of VU meter)
	inline static bool visualizer_toggle_enabled = false;

	/// Visualizer sample buffer and related variables
	static constexpr size_t kVisualizerBufferSize = 512;
	inline static std::array<int32_t, kVisualizerBufferSize> visualizer_sample_buffer{};
	inline static std::atomic<uint32_t> visualizer_write_pos{0};
	inline static std::atomic<uint32_t> visualizer_sample_count{0};
};

} // namespace deluge::hid::display
