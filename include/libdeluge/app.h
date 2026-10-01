/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

/// libdeluge/app.h — the inbound half of the boundary.
///
/// These functions are **implemented by the application** and **called by the
/// platform/BSP**. The platform owns `main` and the runtime; the application
/// never assumes it owns either (so an Embassy executor can own them later).
///
/// The Rust/Embassy BSP's `main` brings up Embassy and calls `deluge_app_init()`.
/// From then on the BSP's audio tier calls `deluge_app_audio_tick()`, and its app
/// loop calls `deluge_app_service()`.
#ifndef LIBDELUGE_APP_H
#define LIBDELUGE_APP_H

#include "abi.h"
#include "board.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// One-time application setup. Called once after the platform and all BSP
/// services are initialised, before audio starts.
/// [task]
void deluge_app_init(const DelugeBoard* board) DELUGE_NOEXCEPT;

/// Why the BSP's app loop called deluge_app_service: a set of these bits.
typedef uint32_t DelugeWake;
#define DELUGE_WAKE_TIMER 1u    ///< a time deluge_app_service returned has come
#define DELUGE_WAKE_ENCODERS 2u ///< an encoder has edges to take (deluge_encoder_take_edges)
#define DELUGE_WAKE_APP 4u      ///< the app asked for a pass (deluge_app_wake)
#define DELUGE_WAKE_INPUT 8u    ///< pad or button events are queued (deluge_control_poll_event)

/// @brief Runs at most one due, woken or requested job of the app's loop, called by the BSP on the
/// interaction tier from the end of deluge_app_init. A pass runs every such job once each, in order,
/// one job per call: while the current pass still has jobs to run, the return value is now_us, so
/// the caller gives its executor a turn and calls again to continue the pass.
/// @param now_us the BSP clock, in µs since boot
/// @param reasons the DelugeWake bits gathered since the last pass
/// @return now_us while the current pass has more jobs to run; otherwise, when the app next needs a
/// pass, in µs on the same clock, or UINT64_MAX for none
/// [task]
uint64_t deluge_app_service(uint64_t now_us, DelugeWake reasons) DELUGE_NOEXCEPT;

/// @brief Asks the BSP's app loop for a pass with @p reasons added. Callable from any tier.
/// [task] [audio] [isr]
void deluge_app_wake(DelugeWake reasons) DELUGE_NOEXCEPT;

/// Realtime audio callback. Render `frames` stereo frames into `out`, reading
/// the same number of input frames from `in` (either may be NULL if the board
/// has no input/output). Samples are Q31.
///
/// MUST NOT block, allocate, take locks, or call any non-`[audio]` boundary
/// function. Note: final volume / click-free fades are handled by the BSP
/// (e.g. the RZ/A1L SCUX DVU block) — render at unity here.
/// [task] [audio]
void deluge_app_render(const DelugeStereoSample* in, DelugeStereoSample* out, uint32_t frames) DELUGE_NOEXCEPT;

/// @brief A render block the board chose not to produce (a host harness's null sink that skips rendering).
///
/// Runs only the render's block-start lifetime step -- the epoch advance and the let-go of retired
/// objects -- with no audio, so an object handed to the render is still let go of and freed. Call it
/// on the render tier, in place of `deluge_app_render`, for that block.
/// [task] [audio]
void deluge_app_render_skipped(void) DELUGE_NOEXCEPT;

/// @brief One tick of the render, called by the BSP's audio tier on its poll cadence.
///
/// Renders as far as the output has played. Returns true if the tick rendered at least one block,
/// the ticks the BSP's render-time average counts. Returns false, and renders nothing, while an
/// offline export owns rendering or the render is locked.
/// [audio]
bool deluge_app_audio_tick(void) DELUGE_NOEXCEPT;

/// @brief Opt this host out of the boot-settings prelude, leaving only the startup song.
///
/// For a host that drives its own scenario and never services the byte channel: the prelude's reads
/// would sit in the channel forever and wedge whatever else needs it. Call it before
/// `deluge_app_init`.
/// [task]
void deluge_startup_skip_settings_prelude(void) DELUGE_NOEXCEPT;

/// @brief The render clock, in output frames since boot (`AudioEngine::audioSampleTimer`).
///
/// The Rust resource manager measures loader service latency with it: it has no clock of its own and
/// cannot reach the audio engine. A plain 32-bit read, so it is atomic even while the render
/// advances it; it wraps.
/// [task] [audio]
uint32_t deluge_debug_now_frames(void) DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_APP_H
