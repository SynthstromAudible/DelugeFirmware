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
#include <string_view>

// Forward declarations
class Clip;
class ModControllable;

namespace deluge::hid::display {

/// Visualizer rendering utilities for OLED display
/// Renders waveform, line spectrum, and bar spectrum visualizations
class Visualizer {
public:
	/// Main entry point for rendering visualizer
	/// @param canvas The OLED canvas to render to
	static void renderVisualizer(oled_canvas::Canvas& canvas);

	/// Render visualizer waveform or spectrum on OLED display (default implementation)
	/// @param canvas The OLED canvas to render to
	static void renderVisualizerDefault(oled_canvas::Canvas& canvas);

	/// Render waveform visualization
	/// @param canvas The OLED canvas to render to
	static void renderVisualizerWaveform(oled_canvas::Canvas& canvas);

	/// Render line spectrum visualization using FFT
	/// @param canvas The OLED canvas to render to
	static void renderVisualizerLineSpectrum(oled_canvas::Canvas& canvas);

	/// Render bar spectrum visualization with 16 frequency bands
	/// @param canvas The OLED canvas to render to
	static void renderVisualizerBarSpectrum(oled_canvas::Canvas& canvas);

	/// Render stereo line spectrum visualization
	/// @param canvas The OLED canvas to render to
	static void renderVisualizerStereoLineSpectrum(oled_canvas::Canvas& canvas);

	/// Render stereo 8-band bar spectrum visualization
	/// @param canvas The OLED canvas to render to
	static void renderVisualizerStereoBarSpectrum(oled_canvas::Canvas& canvas);

	/// Render tunnel visualization
	/// @param canvas The OLED canvas to render to
	static void renderVisualizerTunnel(oled_canvas::Canvas& canvas);

	/// Render starfield visualization
	/// @param canvas The OLED canvas to render to
	static void renderVisualizerStarfield(oled_canvas::Canvas& canvas);

	/// Render skyline visualization
	/// @param canvas The OLED canvas to render to
	static void renderVisualizerSkyline(oled_canvas::Canvas& canvas);

	/// Render pulse grid visualization
	/// @param canvas The OLED canvas to render to
	static void renderVisualizerPulseGrid(oled_canvas::Canvas& canvas);

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

	/// Get current CV visualizer mode (Cube, Stereo Spectrum, or Stereo EQ)
	/// @return Current CV visualizer mode
	static uint32_t getCVVisualizerMode();

	/// Set CV visualizer mode (Cube, Stereo Spectrum, or Stereo EQ)
	/// @param mode The CV visualizer mode to set
	static void setCVVisualizerMode(uint32_t mode);

	/// Set whether visualizer toggle is enabled (independent of VU meter)
	/// @param enabled Whether visualizer toggle should be enabled
	static void setToggleEnabled(bool enabled);

	/// Get whether visualizer toggle is currently enabled
	/// @return true if visualizer toggle is enabled
	static bool isToggleEnabled();

	/// Check if we're currently in a clip context (clip view, keyboard screen, or holding clip)
	/// @return true if in clip context
	static bool isInClipContext();

	/// Check if visualizer should display clip-specific audio vs. full mix
	/// @return true if in clip mode (instrument clip view with Synth/Kit clip, MIDI clips excluded)
	static bool isClipMode();

	/// Check if clip visualizer is actively running
	/// @param displayVUMeter Whether VU meter is enabled
	/// @return true if clip visualizer is active
	static bool isClipVisualizerActive(bool displayVUMeter);

	/// Set the current clip for visualizer (called by UI thread)
	/// @param clip Pointer to the current clip, or nullptr to clear
	static void setCurrentClipForVisualizer(Clip* clip);

	/// Get the current clip for visualizer (thread-safe)
	/// @return Pointer to the current clip, or nullptr
	static Clip* getCurrentClipForVisualizer();

	/// Clear visualizer buffer (called when switching clips or entering clip view)
	static void clearVisualizerBuffer();

	/// Display program name popup when entering clip visualizer mode
	static void displayClipProgramNamePopup();

	/// Check if clip visualizer should be activated for a given clip
	/// @param clip Pointer to the clip to check
	/// @return true if clip visualizer should be activated
	static bool shouldActivateClipVisualizer(Clip* clip);

	/// Try to set the current clip for visualizer if conditions are met
	/// @param clip Pointer to the clip to set, or nullptr to clear
	static void trySetClipForVisualizer(Clip* clip);

	/// Clear the current clip for visualizer (safe cleanup function)
	static void clearClipForVisualizer();

	/// Sample audio data for visualizer display (waveform, line spectrum, bar spectrum)
	/// Performs downsampling and stores samples in the circular buffer for display
	/// @param renderingBuffer The audio buffer to sample from
	/// @param numSamples Number of samples in the buffer
	static void sampleAudioForDisplay(deluge::dsp::StereoBuffer<q31_t> renderingBuffer, size_t numSamples);

	/// Sample audio data for clip-specific visualizer display
	/// Performs downsampling and stores samples in the circular buffer for display
	/// @param renderingBuffer The audio buffer to sample from
	/// @param numSamples Number of samples in the buffer
	/// @param clip Pointer to the clip being sampled
	static void sampleAudioForClipDisplay(deluge::dsp::StereoBuffer<q31_t> renderingBuffer, size_t numSamples,
	                                      Clip* clip);

	/// Check buffer for audio activity and update silence timer
	/// @param renderingBuffer The audio buffer to check
	/// @param numSamples Number of samples in the buffer
	/// @param lastAudioTime Reference to the timer to update if audio is detected
	static void updateSilenceTimer(deluge::dsp::StereoBuffer<q31_t> renderingBuffer, size_t numSamples,
	                               uint32_t& lastAudioTime);

	/// Get display name for a visualizer mode
	/// @param mode The visualizer mode
	/// @return String containing the display name
	static std::string_view getModeDisplayName(uint32_t mode);

	/// Whether visualizer display is enabled
	inline static bool display_visualizer = false;

	/// Session visualizer mode (overrides community setting when set)
	/// Set to VisualizerOff when using community setting
	inline static uint32_t session_visualizer_mode = RuntimeFeatureStateVisualizer::VisualizerOff;

	/// Last selected special visualizer mode (Cube, Stereo Spectrum, Stereo EQ)
	/// Used to remember which special visualizer to return to when pressing CV from main visualizers
	inline static uint32_t cv_visualizer_mode = RuntimeFeatureStateVisualizer::VisualizerBarSpectrum;

	/// Frame counter for update timing (all visualizers use 30fps)
	inline static uint32_t visualizer_frame_counter = 0;

	/// Whether visualizer toggle is enabled (independent of VU meter)
	inline static bool visualizer_toggle_enabled = false;

	/// Current clip for visualizer display (thread-safe atomic pointer)
	/// Written by UI thread in setCurrentClipForVisualizer(), read by both UI and audio threads
	inline static std::atomic<Clip*> current_clip_for_visualizer{nullptr};

	/// Track if we've shown the program name popup for the current clip
	inline static bool clip_program_popup_shown = false;

	/// Silence detection timers (in audio samples)
	/// Global visualizer hides after 1 second of silence
	inline static uint32_t global_visualizer_last_audio_time = 0;
	/// Clip visualizer hides after 1 second of silence for that specific clip
	inline static uint32_t clip_visualizer_last_audio_time = 0;

	/// Silence detection constants
	static constexpr uint32_t kSilenceTimeoutSamples = 44100; // 1 second at 44.1kHz
	static constexpr int32_t kSilenceThreshold =
	    1 << 20; // ~0.0005 in Q31 format (small threshold to avoid noise floor triggering)

	/// Audio sampling constants
	static constexpr uint32_t kVisualizerSampleInterval =
	    2;                                         // Sample every N-th sample for efficiency (reduces CPU usage)
	static constexpr uint32_t kQ31ToQ15Shift = 16; // Convert Q31 → Q15 (15 fractional bits)

	/// Frame rate constants
	static constexpr uint32_t kFrameSkip =
	    2; // Frame skip for 30fps: (44.1kHz/30fps)/(128 samples/frame) ≈ 11.5, but OLED refresh limits to ~2

	/// Visualizer sample buffer and related variables
	static constexpr size_t kVisualizerBufferSize = 512;
	inline static std::array<int32_t, kVisualizerBufferSize> visualizer_sample_buffer_left{};
	inline static std::array<int32_t, kVisualizerBufferSize> visualizer_sample_buffer_right{};
	// Keep mono buffer for backward compatibility with existing visualizers
	inline static std::array<int32_t, kVisualizerBufferSize> visualizer_sample_buffer{};
	/// Current write position in circular buffer (accessed by audio thread)
	inline static std::atomic<uint32_t> visualizer_write_pos{0};
	/// Number of valid samples in buffer (accessed by audio thread)
	inline static std::atomic<uint32_t> visualizer_sample_count{0};
};

} // namespace deluge::hid::display
