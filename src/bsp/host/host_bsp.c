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

#include "host_link.h" // emulator transport (display/LED out, input in)

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ===========================================================================
// system.h
// ===========================================================================

DelugeStatus deluge_platform_init(void) {
	// Bring up the emulator transport. When DELUGE_HOST_LINK is set this blocks until the
	// spark GUI attaches; when unset it is a no-op and the build runs headless (offline
	// WAV-capture harness). Either way the app boots identically.
	host_link_init();
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

extern uint32_t host_rendered_frames; // host_audio.c — the offline audio clock

// DELUGE_HOST_DETERMINISTIC: drive BOTH clocks off the rendered-frame count instead of wall time,
// so two builds (e.g. x86/SIMDe vs arm-linux/NEON) reach byte-identical state and their WAV captures
// are diffable. The scheduler still makes progress because audio is the always-ready fallback task
// that advances host_rendered_frames. It also makes the dither deterministic: seedRandom() seeds the
// PRNG from deluge_clock_now(), which is then identical across builds.
static int host_deterministic(void) {
	static int v = -1;
	if (v < 0) {
		v = getenv("DELUGE_HOST_DETERMINISTIC") ? 1 : 0;
	}
	return v;
}

// Deterministic clock anchored to the rendered-frame count but STRICTLY INCREASING per call: a pure
// frames*rate value freezes within a render block (host_rendered_frames only advances between blocks),
// which deadlocks any in-block `while (clock < target)` wait. Bumping by 1 per call when the frame base
// hasn't moved keeps every such loop making progress while staying deterministic (the per-block call
// count is identical across builds when the DSP is) and sample-accurate at block boundaries.
static uint64_t host_det_clock(uint64_t base, uint64_t* state) {
	if (base <= *state) {
		*state += 1;
	}
	else {
		*state = base;
	}
	return *state;
}

uint64_t deluge_clock_now(void) {
	if (host_deterministic()) {
		static uint64_t s = 0;
		return host_det_clock((uint64_t)host_rendered_frames * HOST_CLOCK_HZ / 44100u, &s);
	}
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
	if (host_deterministic()) {
		static uint64_t s = 0;
		return host_det_clock((uint64_t)host_rendered_frames * DELUGE_TICK_HZ / 44100u, &s);
	}
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
// memory.h — static-backed regions. The app's GeneralMemoryAllocator carves its
// regions from the firmware linker's boundary symbols (__sdram_bss_end,
// __heap_start/program_stack_start, __frunk_bss_end/__frunk_slack_end) plus
// deluge_memory_external_end(). On the SoC those describe one contiguous SDRAM
// block (stealable | external | external_small) and the on-chip SRAM. We
// reproduce that with static .bss pools and emulate the linker's PROVIDE()'d
// boundary symbols by `.set`-ting them to the pool edges (the allocator only
// reads their *addresses*). `.set` keeps the big arrays in .bss → no file bloat;
// the offsets must be bare integers (the assembler can't parse the `u`/paren C
// forms), so HOST_*_BYTES are plain literals and STR() stringifies them.
// ===========================================================================

#define STR_(x) #x
#define STR(x) STR_(x)

#define HOST_SDRAM_BYTES 67108864   // 64 MiB — holds stealable + external + external_small
#define HOST_INTERNAL_BYTES 2097152 // 2 MiB on-chip-SRAM-equivalent (internal heap region)
#define HOST_FRUNK_BYTES 262144     // 256 KiB small-internal ("frunk") region
#define HOST_SCRATCH_BYTES (32u * 1024u)

__attribute__((used)) static uint8_t host_sdram[HOST_SDRAM_BYTES];
__attribute__((used)) static uint8_t host_internal[HOST_INTERNAL_BYTES];
__attribute__((used)) static uint8_t host_frunk[HOST_FRUNK_BYTES];
static uint8_t host_scratch[HOST_SCRATCH_BYTES];

// clang-format off
__asm__(
    ".global __sdram_bss_end\n\t.set __sdram_bss_end, host_sdram\n"
    ".global __heap_start\n\t.set __heap_start, host_internal\n"
    ".global program_stack_start\n\t.set program_stack_start, host_internal + " STR(HOST_INTERNAL_BYTES) "\n"
    ".global program_stack_end\n\t.set program_stack_end, host_internal + " STR(HOST_INTERNAL_BYTES) "\n"
    ".global __frunk_bss_end\n\t.set __frunk_bss_end, host_frunk\n"
    ".global __frunk_slack_end\n\t.set __frunk_slack_end, host_frunk + " STR(HOST_FRUNK_BYTES) "\n");
// clang-format on

uint8_t deluge_memory_region_count(void) {
	return 2;
}

DelugeStatus deluge_memory_region(uint8_t index, DelugeMemoryRegion* out) {
	if (index == 0) {
		out->base = host_sdram;
		out->size = HOST_SDRAM_BYTES;
		out->kind = DELUGE_MEM_LARGE_EXTERNAL;
		return DELUGE_OK;
	}
	if (index == 1) {
		out->base = host_internal;
		out->size = HOST_INTERNAL_BYTES;
		out->kind = DELUGE_MEM_FAST_INTERNAL;
		return DELUGE_OK;
	}
	return DELUGE_ERR_PARAM;
}

uintptr_t deluge_memory_external_end(void) {
	return (uintptr_t)host_sdram + HOST_SDRAM_BYTES;
}

uintptr_t deluge_memory_internal_begin(void) {
	return (uintptr_t)host_internal;
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

// audio_io.h is implemented in host_audio.c (the render-driver + WAV capture).

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
	// The firmware hands us oledCurrentImage: already the SSD1309 page-major hardware
	// buffer ((height/8) pages × width bytes, bit b of byte[page*width+col] = panel row
	// page*8+b) — exactly what spark's update_display_from_buffer expects. Emit it verbatim.
	if (host_link_active()) {
		uint16_t bytes = (uint16_t)((height >> 3) * width); // 48/8 × 128 = 768
		host_link_send(HOST_LINK_MSG_UPDATE_DISPLAY, pixels, bytes);
	}
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
// --- Live input bridge (spark GUI → firmware) ------------------------------------
//
// Inbound frames from the GUI are pumped here and split into two destinations:
//  - pad/button presses become DelugeInputEvents drained by deluge_control_poll_event()
//  - encoder rotations accumulate into per-encoder edge counts taken by
//    deluge_encoder_take_edges()
// matching the two distinct paths the firmware reads input through.

// Pad/button event ring. Power-of-two size; one producer (pump) one consumer (poll).
#define HOST_EVENT_QUEUE_CAP 256u
static DelugeInputEvent ev_queue[HOST_EVENT_QUEUE_CAP];
static uint32_t ev_head; // next write
static uint32_t ev_tail; // next read

static void ev_push(DelugeInputEventKind kind, uint8_t x, uint8_t y, int16_t value) {
	uint32_t next = (ev_head + 1u) & (HOST_EVENT_QUEUE_CAP - 1u);
	if (next == ev_tail) {
		return; // full — drop (UI back-pressure; should never happen at human rates)
	}
	ev_queue[ev_head].kind = kind;
	ev_queue[ev_head].x = x;
	ev_queue[ev_head].y = y;
	ev_queue[ev_head].value = value;
	ev_head = next;
}

// Accumulated encoder edges, indexed by the firmware's deluge_encoder_take_edges() index
// (0=scrollY, 1=scrollX, 2=tempo, 3=select, 4=mod1, 5=mod0 — see encoder_input.cpp).
static int8_t encoder_edges[6];

// spark encoder id (0=SCROLL_X,1=TEMPO,2=MOD_0,3=MOD_1,4=SCROLL_Y,5=SELECT) → firmware index.
static const uint8_t kSparkEncoderToFwIndex[6] = {1, 2, 5, 4, 0, 3};

// Drain all currently-available inbound frames. Cheap to call every poll.
static void host_input_pump(void) {
	if (!host_link_active()) {
		return;
	}
	uint8_t type;
	uint8_t data[8];
	uint16_t len;
	for (;;) {
		len = sizeof(data);
		if (!host_link_recv(&type, data, &len)) {
			break;
		}
		switch (type) {
		case HOST_LINK_MSG_PAD_PRESSED: // [col][row]
			if (len >= 2) {
				ev_push(DELUGE_EVENT_PAD, data[0], data[1], 255);
			}
			break;
		case HOST_LINK_MSG_PAD_RELEASED: // [col][row]
			if (len >= 2) {
				ev_push(DELUGE_EVENT_PAD, data[0], data[1], 0);
			}
			break;
		case HOST_LINK_MSG_BUTTON_PRESSED:  // [button_id], id = 9*(y+16)+x
		case HOST_LINK_MSG_BUTTON_RELEASED: // (encoder pushes are buttons too — decode uniformly)
			if (len >= 1 && data[0] >= 144) {
				uint8_t k = (uint8_t)(data[0] - 144);
				uint8_t x = (uint8_t)(k % 9);
				uint8_t y = (uint8_t)(k / 9);
				int16_t value = (type == HOST_LINK_MSG_BUTTON_PRESSED) ? 255 : 0;
				ev_push(DELUGE_EVENT_BUTTON, x, y, value);
			}
			break;
		case HOST_LINK_MSG_ENCODER_ROTATED: // [encoder_id][delta:i8]
			if (len >= 2 && data[0] < 6) {
				int8_t* slot = &encoder_edges[kSparkEncoderToFwIndex[data[0]]];
				int32_t acc = (int32_t)*slot + (int8_t)data[1];
				if (acc > 127) {
					acc = 127;
				}
				else if (acc < -128) {
					acc = -128;
				}
				*slot = (int8_t)acc;
			}
			break;
		default:
			break; // ignore unknown / response types
		}
	}
}

// Pad press injection (offline harness, used only when no GUI link is active).
// DELUGE_HOST_PAD="x,y" presses+holds a grid pad. To audition the active synth in a clip, press the
// rightmost (audition) column: x = kDisplayWidth+kSideBarWidth-1 = 17, y = 0..7. Fired once the audio
// clock (host_rendered_frames) passes a frame threshold — keying off rendered frames rather than
// wall-clock poll counts makes the onset deterministic and identical across the x86 and arm-linux
// builds (so their captures are diffable). Held (no release) afterwards, so the note sustains for the
// rest of the capture. DELUGE_HOST_PAD_AT_FRAME overrides the threshold.
extern uint32_t host_rendered_frames; // host_audio.c
static int host_pad_inited;
static int host_pad_x = -1;
static int host_pad_y;
static uint32_t host_pad_at_frame = 4410; // ~0.1 s at 44.1 kHz
static int host_pad_done;
static uint32_t host_pad_release_at; // 0 = press-and-hold; else release at this frame
static int host_pad_released;

static void host_pad_init(void) {
	host_pad_inited = 1;
	const char* p = getenv("DELUGE_HOST_PAD");
	if (p) {
		int x = -1, y = -1;
		if (sscanf(p, "%d,%d", &x, &y) == 2 && x >= 0 && y >= 0) {
			host_pad_x = x;
			host_pad_y = y;
		}
	}
	const char* at = getenv("DELUGE_HOST_PAD_AT_FRAME");
	if (at) {
		host_pad_at_frame = (uint32_t)strtoul(at, NULL, 10);
	}
	// DELUGE_HOST_PAD_RELEASE_FRAME makes the scripted pad RELEASE at that frame (instead of
	// holding) — a deterministic, socket-free note-on→note-off for reproducing release-path
	// bugs under gdb.
	const char* rel = getenv("DELUGE_HOST_PAD_RELEASE_FRAME");
	if (rel) {
		host_pad_release_at = (uint32_t)strtoul(rel, NULL, 10);
	}
}

bool deluge_control_poll_event(DelugeInputEvent* out) {
	// Live GUI link: drain decoded events from the wire.
	if (host_link_active()) {
		host_input_pump();
		if (ev_tail != ev_head) {
			*out = ev_queue[ev_tail];
			ev_tail = (ev_tail + 1u) & (HOST_EVENT_QUEUE_CAP - 1u);
			return true;
		}
		return false;
	}

	// Headless fallback: the scripted offline pad injection.
	if (!host_pad_inited) {
		host_pad_init();
	}
	if (host_pad_x >= 0 && !host_pad_done && host_rendered_frames >= host_pad_at_frame) {
		out->kind = DELUGE_EVENT_PAD;
		out->x = (uint8_t)host_pad_x;
		out->y = (uint8_t)host_pad_y;
		out->value = 255; // default-on velocity
		host_pad_done = 1;
		fprintf(stderr, "[host-pad] pad press (x=%d, y=%d) at frame %u\n", host_pad_x, host_pad_y,
		        host_rendered_frames);
		return true;
	}
	// Deterministic release (note-off) once requested and reached.
	if (host_pad_done && host_pad_release_at && !host_pad_released && host_rendered_frames >= host_pad_release_at) {
		out->kind = DELUGE_EVENT_PAD;
		out->x = (uint8_t)host_pad_x;
		out->y = (uint8_t)host_pad_y;
		out->value = 0; // release
		host_pad_released = 1;
		fprintf(stderr, "[host-pad] pad release (x=%d, y=%d) at frame %u\n", host_pad_x, host_pad_y,
		        host_rendered_frames);
		return true;
	}
	return false;
}
void deluge_control_set_pad(uint8_t x, uint8_t y, DelugeColour colour) {
	if (host_link_active()) {
		uint8_t d[5] = {x, y, colour.r, colour.g, colour.b};
		host_link_send(HOST_LINK_MSG_SET_PAD_RGB, d, sizeof(d));
	}
}
void deluge_control_set_led(uint8_t led, bool on) {
	if (host_link_active()) {
		uint8_t d[2] = {led, (uint8_t)(on ? 1 : 0)};
		host_link_send(HOST_LINK_MSG_SET_LED, d, sizeof(d));
	}
}
void deluge_control_set_indicator(uint8_t which, const uint8_t* levels, uint8_t count) {
	// spark's SetKnobIndicator carries exactly 4 segment levels; pad/truncate to match.
	if (host_link_active()) {
		uint8_t d[5] = {which, 0, 0, 0, 0};
		for (uint8_t i = 0; i < count && i < 4; i++) {
			d[1 + i] = levels[i];
		}
		host_link_send(HOST_LINK_MSG_SET_KNOB_INDICATOR, d, sizeof(d));
	}
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
	// On hardware this is the free space in the PIC's bulk pad-output UART buffer; the
	// app gates UI redraws on it (doAnyPendingUIRendering bails when it's low). The host
	// link has no such bottleneck, so always report ample room — otherwise a too-small
	// value permanently suppresses *all* UI rendering (pads and OLED alike).
	return 65535;
}
void deluge_control_set_pad_columns(uint8_t idx, const DelugeColour* colours, uint8_t count) {
	// The surface drives main pads as 16-LED column pairs: colours[0..7] = column idx*2
	// rows 0..7, colours[8..15] = column idx*2+1 rows 0..7 (see pad_leds.cpp sortLedsForCol).
	// Fan out to per-pad SetPadRGB frames the GUI renders directly.
	if (!host_link_active()) {
		return;
	}
	for (uint8_t i = 0; i < count; i++) {
		uint8_t col = (uint8_t)(idx * 2u + (i >> 3)); // first 8 → idx*2, next 8 → idx*2+1
		uint8_t row = (uint8_t)(i & 7u);
		uint8_t d[5] = {col, row, colours[i].r, colours[i].g, colours[i].b};
		host_link_send(HOST_LINK_MSG_SET_PAD_RGB, d, sizeof(d));
	}
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
	// `encoder` is the firmware's own index (0=scrollY … 5=mod0). Return and clear the
	// edges the input pump accumulated from inbound EncoderRotated frames.
	if (encoder >= 6) {
		return 0;
	}
	int8_t edges = encoder_edges[encoder];
	encoder_edges[encoder] = 0;
	return edges;
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
// DIN MIDI note injection (host harness). DELUGE_HOST_MIDI_NOTE=<0..127> feeds a
// single channel-1 note-on at startup so the default synth sounds (a blank song
// otherwise renders silence). The note is held (no note-off) for the capture.
static uint8_t host_din_seq[8];
static uint32_t host_din_len;
static uint32_t host_din_pos;
static int host_din_inited;

static void host_din_init(void) {
	host_din_inited = 1;
	const char* n = getenv("DELUGE_HOST_MIDI_NOTE");
	if (!n) {
		return;
	}
	int note = atoi(n);
	if (note < 0 || note > 127) {
		return;
	}
	host_din_seq[0] = 0x90;          // note-on, channel 1
	host_din_seq[1] = (uint8_t)note; //
	host_din_seq[2] = 100;           // velocity
	host_din_len = 3;
	fprintf(stderr, "[host-midi] injecting DIN note-on ch1 note=%d vel=100\n", note);
}

bool deluge_midi_din_read_timed(uint8_t* byte, uint32_t* arrival_ticks) {
	if (!host_din_inited) {
		host_din_init();
	}
	if (host_din_pos < host_din_len) {
		if (byte) {
			*byte = host_din_seq[host_din_pos++];
		}
		if (arrival_ticks) {
			*arrival_ticks = (uint32_t)deluge_clock_monotonic();
		}
		return true;
	}
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
