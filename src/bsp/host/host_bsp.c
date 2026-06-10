/*
 * Copyright © 2026 Synthstrom Audible Limited
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

/// Host-sim BSP: implements the <libdeluge/...> boundary for an off-target
/// (x86-64 / ARM64) build of the portable application. The point of this build
/// is twofold: (1) it is the definitive proof that the app has no transitive HAL
/// dependency (it links with no RZA1/ on the include path), and (2) it is the
/// substrate for host CI + the audio WAV-diff harness.
///
/// SCAFFOLD STAGE: most functions are minimal stubs sufficient to compile + link.
/// The board-shaped ones (memory, clock, log) are real enough to let the app
/// init; the I/O ones (audio/display/control/midi/cv/block) are inert and get
/// fleshed out as the host harness needs them. Every function is marked [stub]
/// where it still needs a real host implementation.

#define _POSIX_C_SOURCE 199309L // clock_gettime, nanosleep, CLOCK_MONOTONIC

#include "libdeluge/audio_io.h"
#include "libdeluge/block_device.h"
#include "libdeluge/board.h"
#include "libdeluge/clock.h"
#include "libdeluge/control_surface.h"
#include "libdeluge/cv_gate.h"
#include "libdeluge/display.h"
#include "libdeluge/encoder_io.h"
#include "libdeluge/flash.h"
#include "libdeluge/memory.h"
#include "libdeluge/midi_io.h"
#include "libdeluge/signals.h"
#include "libdeluge/system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ===========================================================================
// system.h
// ===========================================================================

DelugeStatus deluge_platform_init(void) {
	return DELUGE_OK;
}

void deluge_system_reset(void) {
	exit(0);
}

void deluge_system_quiesce(void) {
}

void deluge_log(const char* text) {
	fputs(text, stderr);
}

// Single-threaded host: the app's critical sections need no real masking yet.
// (When the host runtime gains an audio thread, back these with a mutex.) [stub]
void ENTER_CRITICAL_SECTION(void) {
}
void EXIT_CRITICAL_SECTION(void) {
}

// The host has no preemptive interrupts — code never runs in ISR context.
bool deluge_in_interrupt(void) {
	return false;
}

// ===========================================================================
// clock.h — a real monotonic clock so app timing behaves.
// ===========================================================================

#define HOST_CLOCK_HZ 1000000u // 1 MHz tick (microseconds)

uint64_t deluge_clock_ticks_per_second(void) {
	return HOST_CLOCK_HZ;
}

uint64_t deluge_clock_now(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * HOST_CLOCK_HZ + (uint64_t)ts.tv_nsec / 1000u;
}

// The scheduler's monotonic clock. Present 33.33 MHz "deluge ticks" (the unit the
// scheduler's Time type assumes) by scaling CLOCK_MONOTONIC.
#define DELUGE_TICK_HZ 33330000u

uint64_t deluge_clock_monotonic_hz(void) {
	return DELUGE_TICK_HZ;
}

uint64_t deluge_clock_monotonic(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * DELUGE_TICK_HZ + (uint64_t)ts.tv_nsec * DELUGE_TICK_HZ / 1000000000ull;
}

void deluge_clock_delay_us(uint32_t us) {
	struct timespec ts = {.tv_sec = us / 1000000u, .tv_nsec = (long)(us % 1000000u) * 1000L};
	nanosleep(&ts, NULL);
}

void deluge_clock_delay_ms(uint32_t ms) {
	deluge_clock_delay_us(ms * 1000u);
}

// ===========================================================================
// memory.h — malloc-backed regions so the app's allocator has real memory.
// ===========================================================================

#define HOST_EXTERNAL_BYTES (64u * 1024u * 1024u) // mirrors the RZ/A1L SDRAM size
#define HOST_INTERNAL_BYTES (256u * 1024u)
#define HOST_SCRATCH_BYTES (32u * 1024u)

static uint8_t* host_external_base;
static uint8_t* host_internal_base;
static uint8_t host_scratch[HOST_SCRATCH_BYTES];

static void host_memory_ensure(void) {
	if (!host_external_base) {
		host_external_base = (uint8_t*)malloc(HOST_EXTERNAL_BYTES);
		host_internal_base = (uint8_t*)malloc(HOST_INTERNAL_BYTES);
	}
}

uint8_t deluge_memory_region_count(void) {
	return 2;
}

DelugeStatus deluge_memory_region(uint8_t index, DelugeMemoryRegion* out) {
	host_memory_ensure();
	if (index == 0) {
		out->base = host_external_base;
		out->size = HOST_EXTERNAL_BYTES;
		out->kind = DELUGE_MEM_LARGE_EXTERNAL;
		return DELUGE_OK;
	}
	if (index == 1) {
		out->base = host_internal_base;
		out->size = HOST_INTERNAL_BYTES;
		out->kind = DELUGE_MEM_FAST_INTERNAL;
		return DELUGE_OK;
	}
	return DELUGE_ERR_PARAM;
}

uintptr_t deluge_memory_external_end(void) {
	host_memory_ensure();
	return (uintptr_t)host_external_base + HOST_EXTERNAL_BYTES;
}

uintptr_t deluge_memory_internal_begin(void) {
	host_memory_ensure();
	return (uintptr_t)host_internal_base;
}

void* deluge_memory_scratch(void) {
	return host_scratch;
}

uint32_t deluge_cache_line_size(void) {
	return 64;
}

// No DMA on host → cache maintenance is a no-op.
void deluge_cache_clean(const void* addr, uint32_t size) {
	(void)addr;
	(void)size;
}
void deluge_cache_invalidate(const void* addr, uint32_t size) {
	(void)addr;
	(void)size;
}

// ===========================================================================
// board.h
// ===========================================================================

static const DelugeBoard host_board = {
    .name = "Host-sim (libdeluge)",
    .pad_grid_width = 16,
    .pad_grid_height = 8,
    .button_rows = 0,
    .button_cols = 0,
    .encoder_count = 6,
    .cv_channels = 2,
    .gate_channels = 4,
    .audio_sample_rate_hz = 44100,
};

const DelugeBoard* deluge_board(void) {
	return &host_board;
}

bool deluge_board_probe_oled(void) {
	return true;
}
void deluge_board_init_early(bool have_oled) {
	(void)have_oled;
}
void deluge_board_init_audio(void) {
}
void deluge_board_init_storage(void) {
}
void deluge_board_unlock_data_cache(void) {
}

// ===========================================================================
// audio_io.h  [stub — host audio sink / WAV harness goes here]
// ===========================================================================

DelugeStatus deluge_audio_start(void) {
	return DELUGE_OK;
}
void deluge_audio_stop(void) {
}
uint32_t deluge_audio_max_block_frames(void) {
	return 128;
}
uint32_t deluge_audio_sample_rate(void) {
	return 44100;
}
uint32_t deluge_audio_output_latency_frames(void) {
	return 128;
}
uint32_t deluge_audio_drive(void) {
	return 0;
}
void deluge_audio_input_resync(void) {
}
uint32_t deluge_audio_frames_until_block_offset(uint32_t offset_frames, uint32_t* elapsed_frames_out) {
	(void)offset_frames;
	if (elapsed_frames_out) {
		*elapsed_frames_out = 0;
	}
	return 0;
}
uint32_t deluge_audio_stamp_to_render_offset(uint32_t stamp) {
	(void)stamp;
	return 0;
}

// ===========================================================================
// block_device.h  [stub — a file-backed card image goes here]
// ===========================================================================

uint8_t deluge_block_sd_unit(void) {
	return 0;
}
DelugeStatus deluge_block_init(uint8_t unit) {
	(void)unit;
	return DELUGE_ERR_NODEV;
}
bool deluge_block_ready(uint8_t unit) {
	(void)unit;
	return false;
}
DelugeStatus deluge_block_read(uint8_t unit, uint8_t* dst, uint32_t sector, uint32_t count) {
	(void)unit;
	(void)dst;
	(void)sector;
	(void)count;
	return DELUGE_ERR_NODEV;
}
DelugeStatus deluge_block_write(uint8_t unit, const uint8_t* src, uint32_t sector, uint32_t count) {
	(void)unit;
	(void)src;
	(void)sector;
	(void)count;
	return DELUGE_ERR_NODEV;
}
uint32_t deluge_block_sector_count(uint8_t unit) {
	(void)unit;
	return 0;
}
uint32_t deluge_block_sector_size(uint8_t unit) {
	(void)unit;
	return 512;
}
DelugeStatus deluge_block_sync(uint8_t unit) {
	(void)unit;
	return DELUGE_OK;
}
DelugeCardEvent deluge_block_poll_card_event(uint8_t unit) {
	(void)unit;
	return DELUGE_CARD_EVENT_NONE;
}

// ===========================================================================
// display.h  [stub — headless / framebuffer dump goes here]
// ===========================================================================

DelugeStatus deluge_display_blit_oled(const uint8_t* pixels, uint16_t width, uint16_t height) {
	(void)pixels;
	(void)width;
	(void)height;
	return DELUGE_OK;
}
bool deluge_display_busy(void) {
	return false;
}
DelugeStatus deluge_display_write_seven_segment(const uint8_t* digits, uint8_t count) {
	(void)digits;
	(void)count;
	return DELUGE_OK;
}
void deluge_display_init(void) {
}
void deluge_display_service(void) {
}
void deluge_display_timer_event(void) {
}
bool deluge_display_consume_transfer_ack(void) {
	return false;
}
void deluge_display_freeze(const uint8_t* pixels) {
	(void)pixels;
}

// ===========================================================================
// control_surface.h  [stub — headless / scripted input goes here]
// ===========================================================================

void deluge_control_read_boot_info(DelugeBootInfo* out) {
	if (out) {
		memset(out, 0, sizeof(*out));
	}
}
bool deluge_control_poll_event(DelugeInputEvent* out) {
	(void)out;
	return false;
}
void deluge_control_set_pad(uint8_t x, uint8_t y, DelugeColour colour) {
	(void)x;
	(void)y;
	(void)colour;
}
void deluge_control_set_led(uint8_t led, bool on) {
	(void)led;
	(void)on;
}
void deluge_control_set_indicator(uint8_t which, const uint8_t* levels, uint8_t count) {
	(void)which;
	(void)levels;
	(void)count;
}
void deluge_control_flush(void) {
}
void deluge_control_init(void) {
}
void deluge_control_wait_for_flush(void) {
}
void deluge_control_setup_for_pads(void) {
}
void deluge_control_enable_oled(void) {
}
bool deluge_control_poll_resume(void) {
	return false;
}
uint32_t deluge_control_pad_output_space(void) {
	return 256;
}
void deluge_control_set_pad_columns(uint8_t idx, const DelugeColour* colours, uint8_t count) {
	(void)idx;
	(void)colours;
	(void)count;
}
void deluge_control_flash_pad(uint8_t idx) {
	(void)idx;
}
void deluge_control_flash_pad_colour(uint8_t idx, int32_t colour_idx) {
	(void)idx;
	(void)colour_idx;
}
void deluge_control_scroll_horizontal(uint8_t flags) {
	(void)flags;
}
void deluge_control_scroll_vertical(bool up, const DelugeColour* colours, uint8_t count) {
	(void)up;
	(void)colours;
	(void)count;
}
void deluge_control_scroll_row(uint8_t row, DelugeColour colour) {
	(void)row;
	(void)colour;
}
void deluge_control_scroll_done(void) {
}
void deluge_control_set_refresh_time(uint8_t ms) {
	(void)ms;
}
void deluge_control_set_dimmer_interval(uint8_t interval) {
	(void)interval;
}

// ===========================================================================
// cv_gate.h  [stub]
// ===========================================================================

void deluge_cv_init(bool display_shares_spi) {
	(void)display_shares_spi;
}
void deluge_cv_set(uint8_t channel, uint16_t value) {
	(void)channel;
	(void)value;
}
uint32_t deluge_cv_sent_count(void) {
	return 0;
}
void deluge_gate_init(void) {
}
void deluge_gate_set(uint8_t channel, bool on) {
	(void)channel;
	(void)on;
}
void deluge_trigger_clock_set_handler(DelugeTriggerClockHandler handler) {
	(void)handler;
}

// ===========================================================================
// encoder_io.h  [stub]
// ===========================================================================

void deluge_encoder_io_init(int8_t wake_task) {
	(void)wake_task;
}
int8_t deluge_encoder_take_edges(uint8_t encoder) {
	(void)encoder;
	return 0;
}

// ===========================================================================
// flash.h  [stub — settings persistence; back with a host file later]
// ===========================================================================

void deluge_flash_read(uint32_t offset, void* dst, uint32_t len) {
	(void)offset;
	memset(dst, 0xFF, len); // erased flash reads as 0xFF
}
void deluge_flash_erase(uint32_t offset) {
	(void)offset;
}
void deluge_flash_program(uint32_t offset, const void* src, uint32_t len) {
	(void)offset;
	(void)src;
	(void)len;
}

// ===========================================================================
// signals.h  [stub]
// ===========================================================================

void deluge_signal_write(DelugeSignal signal, bool on) {
	(void)signal;
	(void)on;
}
bool deluge_signal_read(DelugeSignal signal) {
	(void)signal;
	return false;
}
bool deluge_battery_read_raw(uint16_t* out) {
	(void)out;
	return false;
}
void deluge_battery_start_conversion(void) {
}
bool deluge_midi_gate_timer_pending(void) {
	return false;
}
void deluge_midi_gate_timer_arm(uint32_t samples_from_now) {
	(void)samples_from_now;
}

// ===========================================================================
// midi_io.h  [stub — virtual MIDI ports go here]
// ===========================================================================

void deluge_midi_init(void) {
}
uint8_t deluge_midi_port_count(void) {
	return 1; // DIN only on the host stub
}
DelugeMidiPortKind deluge_midi_port_kind(DelugeMidiPort port) {
	(void)port;
	return DELUGE_MIDI_DIN;
}
DelugeMidiPort deluge_midi_usb_port(uint8_t controller, uint8_t device) {
	(void)controller;
	(void)device;
	return 0;
}
uint32_t deluge_midi_read(DelugeMidiPort port, uint8_t* dst, uint32_t max) {
	(void)port;
	(void)dst;
	(void)max;
	return 0;
}
uint32_t deluge_midi_read_timed(DelugeMidiPort port, uint8_t* dst, uint32_t max, uint32_t* arrival_ticks) {
	(void)port;
	(void)dst;
	(void)max;
	(void)arrival_ticks;
	return 0;
}
uint32_t deluge_midi_write(DelugeMidiPort port, const uint8_t* src, uint32_t len) {
	(void)port;
	(void)src;
	return len; // accept and drop
}
uint32_t deluge_midi_write_space(DelugeMidiPort port) {
	(void)port;
	return 256;
}
uint32_t deluge_midi_write_pending(DelugeMidiPort port) {
	(void)port;
	return 0;
}
bool deluge_midi_din_read_timed(uint8_t* byte, uint32_t* arrival_ticks) {
	(void)byte;
	(void)arrival_ticks;
	return false;
}
void deluge_midi_flush(DelugeMidiPort port) {
	(void)port;
}
void deluge_midi_service(void) {
}
bool deluge_midi_port_connected(DelugeMidiPort port) {
	(void)port;
	return false;
}
bool deluge_midi_usb_is_host(void) {
	return false;
}
bool deluge_midi_usb_peripheral_connected(void) {
	return false;
}
DelugeUsbHostEvent deluge_midi_poll_usb_host_event(void) {
	return DELUGE_USB_HOST_NONE;
}
