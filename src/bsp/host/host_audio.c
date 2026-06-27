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

/// Host-sim audio backend: the <libdeluge/audio_io.h> driver. On the SoC the
/// SSI/DMA hardware paces deluge_audio_drive() against the DAC play head; here
/// there is no DAC, so we drive a deterministic OFFLINE render — each
/// deluge_audio_drive() renders a fixed block via deluge_app_render() and writes
/// it to a 16-bit stereo WAV. After a target duration the capture is finalised
/// and the process quick_exit()s (the app loop never returns on its own).
///
/// Config (env): DELUGE_HOST_WAV (output path, default deluge_host_out.wav),
/// DELUGE_HOST_CAPTURE_SECONDS (default 2.0). This is the substrate for the
/// audio WAV-diff regression harness.

#include "libdeluge/app.h"      // deluge_app_render
#include "libdeluge/audio_io.h" // the boundary we implement
#include "libdeluge/types.h"    // DelugeStereoSample

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOST_AUDIO_SAMPLE_RATE 44100u
#define HOST_AUDIO_BLOCK_FRAMES 128u

// Silence in, rendered audio out. Both app-visible only through deluge_app_render.
static DelugeStereoSample host_input_block[HOST_AUDIO_BLOCK_FRAMES];
static DelugeStereoSample host_render_block[HOST_AUDIO_BLOCK_FRAMES];

static FILE* wav_file;
static uint32_t wav_frames_written;
static uint32_t wav_frames_target;
static int capture_done;

// Total frames rendered so far — a deterministic "audio clock" the input injection keys off
// (instead of wall-clock poll counts), so the note onset is identical across the x86 and
// arm-linux builds, making their captures diffable.
uint32_t host_rendered_frames = 0;

// ---- little-endian WAV helpers -------------------------------------------

static void put_u32le(unsigned char* p, uint32_t v) {
	p[0] = (unsigned char)(v);
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

static void put_u16le(unsigned char* p, uint16_t v) {
	p[0] = (unsigned char)(v);
	p[1] = (unsigned char)(v >> 8);
}

// Write a 44-byte canonical PCM WAV header for `frames` stereo 16-bit samples.
static void wav_write_header(FILE* f, uint32_t frames) {
	const uint16_t channels = 2;
	const uint16_t bits = 16;
	const uint32_t byte_rate = HOST_AUDIO_SAMPLE_RATE * channels * (bits / 8);
	const uint16_t block_align = channels * (bits / 8);
	const uint32_t data_bytes = frames * block_align;

	unsigned char h[44];
	memcpy(h + 0, "RIFF", 4);
	put_u32le(h + 4, 36u + data_bytes);
	memcpy(h + 8, "WAVE", 4);
	memcpy(h + 12, "fmt ", 4);
	put_u32le(h + 16, 16u);      // fmt chunk size
	put_u16le(h + 20, 1u);       // PCM
	put_u16le(h + 22, channels); //
	put_u32le(h + 24, HOST_AUDIO_SAMPLE_RATE);
	put_u32le(h + 28, byte_rate);   //
	put_u16le(h + 32, block_align); //
	put_u16le(h + 34, bits);        //
	memcpy(h + 36, "data", 4);
	put_u32le(h + 40, data_bytes);
	fwrite(h, 1, sizeof h, f);
}

static void capture_init(void) {
	const char* path = getenv("DELUGE_HOST_WAV");
	if (!path) {
		path = "deluge_host_out.wav";
	}
	const char* secs = getenv("DELUGE_HOST_CAPTURE_SECONDS");
	double seconds = secs ? atof(secs) : 2.0;
	if (seconds <= 0.0) {
		seconds = 2.0;
	}
	wav_frames_target = (uint32_t)(seconds * HOST_AUDIO_SAMPLE_RATE);

	wav_file = fopen(path, "wb");
	if (!wav_file) {
		fprintf(stderr, "[host-audio] could not open %s for writing\n", path);
		capture_done = 1;
		return;
	}
	wav_write_header(wav_file, 0); // placeholder; patched at finalise
	fprintf(stderr, "[host-audio] capturing %.2fs (%u frames) -> %s\n", seconds, wav_frames_target, path);
}

static void capture_finalize(void) {
	if (wav_file) {
		fseek(wav_file, 0, SEEK_SET);
		wav_write_header(wav_file, wav_frames_written); // patch RIFF/data sizes
		fclose(wav_file);
		wav_file = NULL;
	}
	fprintf(stderr, "[host-audio] wrote %u frames (%.2fs); exiting\n", wav_frames_written,
	        (double)wav_frames_written / HOST_AUDIO_SAMPLE_RATE);
	fflush(NULL);
	quick_exit(0);
}

// ---- audio_io.h boundary --------------------------------------------------

DelugeStatus deluge_audio_start(void) {
	return DELUGE_OK;
}

void deluge_audio_stop(void) {
}

uint32_t deluge_audio_max_block_frames(void) {
	return HOST_AUDIO_BLOCK_FRAMES;
}

uint32_t deluge_audio_sample_rate(void) {
	return HOST_AUDIO_SAMPLE_RATE;
}

uint32_t deluge_audio_output_latency_frames(void) {
	return HOST_AUDIO_BLOCK_FRAMES;
}

// Offline render driver. Renders one block per call and appends it to the WAV;
// finalises + exits once the target duration is captured. Returns the number of
// render passes (1 while capturing, 0 once done) per the audio_io.h contract.
extern void deluge_host_debug_poll(void); // reason-leak signal triggers (host_debug.cpp)

uint32_t deluge_audio_drive(void) {
	// Service any pending reason-leak signal (SIGUSR1/2) here, on the app thread, between renders.
	deluge_host_debug_poll();

	if (capture_done) {
		return 0;
	}
	if (!wav_file) {
		capture_init();
		if (capture_done) {
			return 0;
		}
	}

	uint32_t frames = HOST_AUDIO_BLOCK_FRAMES;
	if (wav_frames_written + frames > wav_frames_target) {
		frames = wav_frames_target - wav_frames_written;
	}
	if (frames == 0) {
		capture_done = 1;
		capture_finalize(); // does not return
		return 0;
	}

	deluge_app_render(host_input_block, host_render_block, frames);
	host_rendered_frames += frames;

	// Q31 -> 16-bit signed PCM, interleaved L/R.
	for (uint32_t i = 0; i < frames; i++) {
		unsigned char frame[4];
		put_u16le(frame + 0, (uint16_t)(int16_t)(host_render_block[i].l >> 16));
		put_u16le(frame + 2, (uint16_t)(int16_t)(host_render_block[i].r >> 16));
		fwrite(frame, 1, sizeof frame, wav_file);
	}
	wav_frames_written += frames;

	if (wav_frames_written >= wav_frames_target) {
		capture_done = 1;
		capture_finalize(); // does not return
	}
	return 1;
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
