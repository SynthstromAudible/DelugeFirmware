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

#include "drivers/usb/usb_audio.h"
#include "definitions.h"
#include "io/debug/log.h"
#include "timers_interrupts/timers_interrupts.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <numbers>

extern "C" {
#include "RZA1/intc/devdrv_intc.h"
#include "RZA1/system/iodefine.h"
#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_bitdefine.h"
#include "drivers/dmac/dmac.h"
#include "drivers/ssi/ssi.h"
#include "drivers/usb/userdef/r_usb_paudio_config.h"

void usb_cstd_set_buf(usb_utr_t* ptr, uint16_t pipe);
void usb_cstd_set_nak(usb_utr_t* ptr, uint16_t pipe);
void v7_dma_flush_range(uint32_t start, uint32_t end);
}

namespace deluge::usb_audio {
bool directMonitoring = false;
}

namespace {
using deluge::usb_audio::directMonitoring;

// Pipe 1 (from host) is read through D1FIFO by DMA channel 1; pipe 2 (to host) is written through D0FIFO by channel 0
constexpr uint16_t kPipeFromHost = USB_CFG_PAUDIO_ISO_OUT;
constexpr uint16_t kPipeToHost = USB_CFG_PAUDIO_ISO_IN;
constexpr uint16_t kPipeBits = (1 << kPipeFromHost) | (1 << kPipeToHost);
constexpr uint32_t kMaxPacketBytes = USB_PAUDIO_MAX_PACKET_BYTES;
constexpr uint32_t kBytesPerFrame = USB_PAUDIO_BYTES_PER_FRAME;
static_assert(kBytesPerFrame == sizeof(StereoSample), "USB audio frames are copied straight to and from StereoSamples");

constexpr uint8_t kInterruptPriority = 9; // Same as the USB interrupt, so neither can interrupt the other

// DMA request sources for USB0's D0FIFO and D1FIFO - the same as r_usb_dma.c uses
constexpr uint32_t kDmarsD0Fifo = 0x83;
constexpr uint32_t kDmarsD1Fifo = 0x87;

// CHCFG: requests come from the USB module, level-detected, active high, in bus cycle mode; 32-bit accesses at both
// ends; transfer end interrupt enabled
constexpr uint32_t kChcfgCommon = (0b010 << 8)            // AM: bus cycle mode
                                  | (1 << 6)              // LVL: level detection
                                  | (1 << 5)              // HIEN: active high
                                  | (0b010 << 12)         // SDS: 32-bit
                                  | (0b010 << 16);        // DDS: 32-bit
constexpr uint32_t kChcfgToHost = kChcfgCommon | (1 << 3) // REQD: the request comes from the destination (the FIFO)
                                  | (1 << 21)             // DAD: destination address fixed
                                  | (USB_AUDIO_TX_DMA_CHANNEL & 7);
constexpr uint32_t kChcfgFromHost = kChcfgCommon | (1 << 20) // SAD: source address fixed
                                    | (USB_AUDIO_RX_DMA_CHANNEL & 7);

// Each direction has one packet's worth of staging memory for the DMA. Whole cache lines, so nothing else shares them,
// and the CPU only touches them through the uncached mirror
alignas(CACHE_LINE_SIZE) uint32_t toHostStaging[kMaxPacketBytes / sizeof(uint32_t)];
alignas(CACHE_LINE_SIZE) uint32_t fromHostStaging[kMaxPacketBytes / sizeof(uint32_t)];
static_assert(sizeof(toHostStaging) % CACHE_LINE_SIZE == 0);

template <typename T>
T* uncached(T* address) {
	return reinterpret_cast<T*>(reinterpret_cast<uint32_t>(address) + UNCACHED_MIRROR_OFFSET);
}

// Frames from the host, written by the DMA-complete interrupt, read by the resampler in the audio routine
constexpr uint32_t kFromHostRingFrames = 1024;
std::array<StereoSample, kFromHostRingFrames> fromHostRing;
std::atomic<uint32_t> fromHostWritten{0}; // Free-running frame counts
std::atomic<uint32_t> fromHostRead{0};
std::atomic<uint32_t> fromHostGeneration{0}; // Bumped each time the stream (re)starts

// Frames to the host, written by the audio routine, read by the SOF interrupt
constexpr uint32_t kToHostRingFrames = 1024;
std::array<StereoSample, kToHostRingFrames> toHostRing;
std::atomic<uint32_t> toHostWritten{0};
std::atomic<uint32_t> toHostRead{0};

volatile bool fromHostStreaming = false;
volatile bool toHostStreaming = false;
volatile bool fromHostDmaBusy = false;
volatile bool toHostDmaBusy = false;
uint32_t fromHostDmaBytes;

// SSI-RX-shaped ring holding the resampled audio from the host, for the rest of the Deluge to read like an input
PLACE_SDRAM_BSS alignas(CACHE_LINE_SIZE) int32_t inputRing[SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS];
uint32_t inputRingWriteFrame = 0; // Where fillInputRing() got up to, in frames from the start

bool interruptsSetUp = false;

/*********************************************************************************************************************
 * DMA, and the interrupts driving it
 */

void stopDma(int32_t channel) {
	DMACn(channel).CHCTRL_n = DMAC0_CHCTRL_n_CLREN | DMAC0_CHCTRL_n_CLRRQ;
	for (int32_t i = 0; i < 10000 && (DMACn(channel).CHSTAT_n & (DMAC0_CHSTAT_n_EN | DMAC0_CHSTAT_n_TACT)); i++) {}
	DMACn(channel).CHCTRL_n = DMAC0_CHCTRL_n_SWRST;
}

void startDma(int32_t channel, uint32_t source, uint32_t destination, uint32_t numBytes) {
	auto& dma = DMACn(channel);
	dma.CHCTRL_n = DMAC0_CHCTRL_n_SWRST | DMAC0_CHCTRL_n_CLRTC | DMAC0_CHCTRL_n_CLREND;
	dma.N0SA_n = source;
	dma.N0DA_n = destination;
	dma.N0TB_n = numBytes;
	dma.CHCTRL_n = DMAC0_CHCTRL_n_SETEN;
}

// Points a FIFO port at a pipe, ready for 32-bit access, and waits for the controller to confirm
bool selectPipe(volatile uint16_t& fifoSel, uint16_t pipe) {
	fifoSel = USB_MBW_32 | pipe;
	for (int32_t i = 0; i < 10000; i++) {
		if ((fifoSel & USB_CURPIPE) == pipe) {
			return true;
		}
	}
	return false;
}

// If a packet from the host is waiting in the pipe's buffer, start the DMA reading it out
void startFromHostDma() {
	if (fromHostDmaBusy || !fromHostStreaming) {
		return;
	}
	uint16_t fifoCtr = USB200.D1FIFOCTR;
	if (!(fifoCtr & USB_FRDY)) {
		return;
	}
	uint32_t numBytes = fifoCtr & USB_DTLN;
	if (numBytes == 0 || numBytes > kMaxPacketBytes) {
		USB200.D1FIFOCTR = USB_BCLR; // Nothing we can use - throw it away
		return;
	}
	fromHostDmaBytes = numBytes;
	fromHostDmaBusy = true;
	startDma(USB_AUDIO_RX_DMA_CHANNEL, reinterpret_cast<uint32_t>(&USB200.D1FIFO.UINT32),
	         reinterpret_cast<uint32_t>(fromHostStaging), (numBytes + 3) & ~3u);
	USB200.D1FIFOSEL |= USB_DREQE;
}

void fromHostDmaComplete(uint32_t) {
	USB200.D1FIFOSEL &= ~USB_DREQE;

	if (fromHostStreaming) {
		auto const* frames = uncached(reinterpret_cast<StereoSample const*>(fromHostStaging));
		uint32_t numFrames = fromHostDmaBytes / kBytesPerFrame;
		uint32_t written = fromHostWritten.load(std::memory_order_relaxed);
		uint32_t space = kFromHostRingFrames - (written - fromHostRead.load(std::memory_order_acquire));
		numFrames = std::min(numFrames, space); // If the resampler's that far behind, it'll be resyncing anyway
		for (uint32_t i = 0; i < numFrames; i++) {
			fromHostRing[(written + i) & (kFromHostRingFrames - 1)] = frames[i];
		}
		fromHostWritten.store(written + numFrames, std::memory_order_release);
	}

	fromHostDmaBusy = false;
	startFromHostDma(); // The pipe is double buffered, so the next packet might already be there
}

// How many frames to send the host this USB frame. 44.1 on average, nudged by one either way to keep our buffer
// from filling or emptying, since our sample clock isn't the host's
constexpr uint32_t kToHostTargetFill = 160; // Audio routine writes up to 128 at a time
constexpr int32_t kToHostTolerance = 24;
uint32_t toHostRateAccumulator = 0;
int32_t toHostAverageFill = 0; // 24.8 fixed point
bool toHostPrimed = false;

uint32_t framesForNextPacket(uint32_t fill) {
	if (!toHostPrimed) {
		if (fill < kToHostTargetFill) {
			return 0;
		}
		toHostPrimed = true;
		toHostAverageFill = fill << 8;
	}

	toHostAverageFill += ((int32_t)(fill << 8) - toHostAverageFill) >> 6;

	toHostRateAccumulator += USB_PAUDIO_SAMPLE_RATE / 100;
	uint32_t numFrames = toHostRateAccumulator / 10;
	toHostRateAccumulator %= 10;

	int32_t error = (toHostAverageFill >> 8) - (int32_t)kToHostTargetFill;
	if (error > kToHostTolerance) {
		numFrames++;
	}
	else if (error < -kToHostTolerance) {
		numFrames--;
	}

	if (numFrames > fill) {
		numFrames = fill;
		toHostPrimed = false; // Ran dry - build the cushion back up before carrying on
	}
	return numFrames;
}

void toHostDmaComplete(uint32_t) {
	USB200.D0FIFOSEL &= ~USB_DREQE;
	// Packets are always shorter than the max packet size, so we have to tell the controller this one's done
	USB200.D0FIFOCTR = USB_BVAL;
	toHostDmaBusy = false;
}

void setUpInterrupts() {
	if (interruptsSetUp) {
		return;
	}
	v7_dma_flush_range(reinterpret_cast<uint32_t>(toHostStaging),
	                   reinterpret_cast<uint32_t>(toHostStaging) + sizeof(toHostStaging));
	v7_dma_flush_range(reinterpret_cast<uint32_t>(fromHostStaging),
	                   reinterpret_cast<uint32_t>(fromHostStaging) + sizeof(fromHostStaging));
	setupAndEnableInterrupt(toHostDmaComplete, INTC_ID_DMAINT0 + USB_AUDIO_TX_DMA_CHANNEL, kInterruptPriority);
	setupAndEnableInterrupt(fromHostDmaComplete, INTC_ID_DMAINT0 + USB_AUDIO_RX_DMA_CHANNEL, kInterruptPriority);
	interruptsSetUp = true;
}

/*********************************************************************************************************************
 * Starting and stopping the streams. These run in the USB task, not an interrupt
 */

void computeResamplerKernel(); // Below

void stopFromHost() {
	CriticalSectionGuard guard;
	fromHostStreaming = false;
	USB200.BRDYENB &= ~(1 << kPipeFromHost);
	usb_cstd_set_nak(USB_NULL, kPipeFromHost);
	if (interruptsSetUp) {
		stopDma(USB_AUDIO_RX_DMA_CHANNEL);
	}
	fromHostDmaBusy = false;
	selectPipe(USB200.D1FIFOSEL, USB_PIPE0); // Let go of the pipe, so it can be reconfigured
}

void startFromHost() {
	computeResamplerKernel();
	setUpInterrupts();

	CriticalSectionGuard guard;
	stopDma(USB_AUDIO_RX_DMA_CHANNEL);
	if (!selectPipe(USB200.D1FIFOSEL, kPipeFromHost)) {
		D_PRINTLN("USB audio: couldn't select pipe %d", kPipeFromHost);
		return;
	}
	setDMARS(USB_AUDIO_RX_DMA_CHANNEL, kDmarsD1Fifo);
	DMACn(USB_AUDIO_RX_DMA_CHANNEL).CHCFG_n = kChcfgFromHost;
	DMACn(USB_AUDIO_RX_DMA_CHANNEL).CHITVL_n = 0;
	DMACn(USB_AUDIO_RX_DMA_CHANNEL).CHEXT_n = 0;

	fromHostDmaBusy = false;
	fromHostGeneration.fetch_add(1, std::memory_order_relaxed);
	fromHostStreaming = true;

	USB200.BRDYSTS = (uint16_t)~(1 << kPipeFromHost);
	USB200.BRDYENB |= (1 << kPipeFromHost);
	usb_cstd_set_buf(USB_NULL, kPipeFromHost); // Start accepting packets
}

void stopToHost() {
	CriticalSectionGuard guard;
	toHostStreaming = false;
	USB200.INTENB0 &= ~USB_SOFE;
	usb_cstd_set_nak(USB_NULL, kPipeToHost);
	if (interruptsSetUp) {
		stopDma(USB_AUDIO_TX_DMA_CHANNEL);
	}
	toHostDmaBusy = false;
	selectPipe(USB200.D0FIFOSEL, USB_PIPE0);
}

void startToHost() {
	setUpInterrupts();

	CriticalSectionGuard guard;
	stopDma(USB_AUDIO_TX_DMA_CHANNEL);
	if (!selectPipe(USB200.D0FIFOSEL, kPipeToHost)) {
		D_PRINTLN("USB audio: couldn't select pipe %d", kPipeToHost);
		return;
	}
	setDMARS(USB_AUDIO_TX_DMA_CHANNEL, kDmarsD0Fifo);
	DMACn(USB_AUDIO_TX_DMA_CHANNEL).CHCFG_n = kChcfgToHost;
	DMACn(USB_AUDIO_TX_DMA_CHANNEL).CHITVL_n = 0;
	DMACn(USB_AUDIO_TX_DMA_CHANNEL).CHEXT_n = 0;

	// Throw away anything left over from last time
	toHostRead.store(toHostWritten.load(std::memory_order_relaxed), std::memory_order_relaxed);
	toHostPrimed = false;
	toHostRateAccumulator = 0;
	toHostDmaBusy = false;
	toHostStreaming = true;

	usb_cstd_set_buf(USB_NULL, kPipeToHost);
	USB200.INTSTS0 = (uint16_t)~USB_SOFR;
	USB200.INTENB0 |= USB_SOFE; // We send a packet each USB frame
}

/*********************************************************************************************************************
 * Resampling audio from the host to our sample clock. Both nominally run at 44.1kHz, but on different crystals, so the
 * ratio is very slightly off 1 and drifts. A servo nudges it to keep the incoming ring buffer at a steady fill.
 * Since the ratio's always so close to 1, the read position's fractional part moves very slowly, so a fixed table of
 * windowed-sinc kernels, one per fractional position, is plenty.
 */

constexpr int32_t kResamplerTaps = 32;
constexpr int32_t kResamplerTapsBehind = kResamplerTaps / 2 - 1; // Frames before the read position the kernel reaches
constexpr int32_t kResamplerTapsAhead = kResamplerTaps / 2;      // And after
constexpr int32_t kResamplerPhaseBits = 9;
constexpr int32_t kResamplerCoefficientBits = 30;
PLACE_SDRAM_BSS int32_t resamplerKernel[1 << kResamplerPhaseBits][kResamplerTaps];
bool resamplerKernelComputed = false;

// Modified Bessel function of the first kind, order 0 - for the Kaiser window
double besselI0(double x) {
	double sum = 1;
	double term = 1;
	for (int32_t k = 1; k < 30; k++) {
		term *= (x / (2 * k)) * (x / (2 * k));
		sum += term;
	}
	return sum;
}

void computeResamplerKernel() {
	if (resamplerKernelComputed) {
		return;
	}
	constexpr double kCutoff = 0.95; // Relative to Nyquist
	constexpr double kBeta = 8;
	double const windowNormalization = besselI0(kBeta);

	for (int32_t phase = 0; phase < (1 << kResamplerPhaseBits); phase++) {
		double fraction = (phase + 0.5) / (1 << kResamplerPhaseBits);
		double taps[kResamplerTaps];
		double sum = 0;
		for (int32_t tap = 0; tap < kResamplerTaps; tap++) {
			double distance = fraction - (tap - kResamplerTapsBehind); // From this tap's frame to the read position
			double sincArgument = std::numbers::pi * kCutoff * distance;
			double sinc = (sincArgument == 0) ? 1 : std::sin(sincArgument) / sincArgument;
			double windowPosition = distance / (kResamplerTaps / 2);
			double window =
			    (std::abs(windowPosition) >= 1)
			        ? 0
			        : besselI0(kBeta * std::sqrt(1 - windowPosition * windowPosition)) / windowNormalization;
			taps[tap] = sinc * window;
			sum += taps[tap];
		}
		// Unity gain at DC for every phase, so the level doesn't wobble as the phase drifts
		for (int32_t tap = 0; tap < kResamplerTaps; tap++) {
			resamplerKernel[phase][tap] = std::lround(taps[tap] / sum * (1 << kResamplerCoefficientBits));
		}
	}
	resamplerKernelComputed = true;
}

// Fill level of the incoming ring we aim for when starting each render window, in frames past the read position. The
// window takes up to 128 at once, packets bring ~44 at a time, and the kernel needs to see kResamplerTapsAhead ahead
constexpr float kFromHostTargetFill = 224;
constexpr float kFromHostServoGain = 4e-6; // Ratio adjustment per frame of fill error
constexpr float kFromHostMaxRatioAdjustment = 1e-3;

struct {
	uint32_t generation = 0;
	bool primed = false;
	uint32_t readFrame = 0; // The frame the read position is at or just after
	uint32_t phase = 0;     // How far past readFrame the read position is, as a fraction of a frame
	float averageFill = kFromHostTargetFill;
	int64_t increment = (int64_t)1 << 32; // How far to move the read position per output frame, 32.32
} resampler;

[[gnu::always_inline]] inline int32_t saturate(int64_t value) {
	return std::clamp<int64_t>(value, INT32_MIN, INT32_MAX);
}

// Renders numFrames frames of audio from the host, interleaved stereo, into output
void resampleFromHost(int32_t* output, size_t numFrames) {
	uint32_t generation = fromHostGeneration.load(std::memory_order_relaxed);
	if (resampler.generation != generation) {
		// A new stream - anything left in the ring is from the last one
		resampler.generation = generation;
		resampler.readFrame = fromHostWritten.load(std::memory_order_acquire);
		resampler.primed = false;
		resampler.averageFill = kFromHostTargetFill;
		resampler.increment = (int64_t)1 << 32;
	}

	uint32_t written = fromHostWritten.load(std::memory_order_acquire);
	uint32_t fill = written - resampler.readFrame;

	if (!resampler.primed) {
		// Wait for enough to arrive, then start the read position that far back
		if (fill > kFromHostRingFrames || fill < kFromHostTargetFill) {
			if (fill > kFromHostRingFrames) {
				resampler.readFrame = written; // Must have been from the previous stream
			}
			std::memset(output, 0, numFrames * sizeof(StereoSample));
			fromHostRead.store(resampler.readFrame - kResamplerTapsBehind, std::memory_order_release);
			return;
		}
		resampler.readFrame = written - (uint32_t)kFromHostTargetFill;
		resampler.phase = 0;
		resampler.primed = true;
		fill = kFromHostTargetFill;
	}
	else if (fill > kFromHostRingFrames - 64) {
		// Way too far behind - the host must have sped up enormously, or we stalled. Jump forward
		resampler.readFrame = written - (uint32_t)kFromHostTargetFill;
		fill = kFromHostTargetFill;
	}

	// Steer the ratio to bring the fill back to target
	resampler.averageFill += ((float)fill - resampler.averageFill) * (1.f / 32);
	float adjustment = std::clamp((resampler.averageFill - kFromHostTargetFill) * kFromHostServoGain,
	                              -kFromHostMaxRatioAdjustment, kFromHostMaxRatioAdjustment);
	resampler.increment = ((int64_t)1 << 32) + (int64_t)(adjustment * 4294967296.f);

	for (size_t i = 0; i < numFrames; i++) {
		if ((int32_t)(written - resampler.readFrame) <= kResamplerTapsAhead) {
			// Ran out - the host's stopped sending, or fallen behind. Silence until there's enough again
			std::memset(&output[i * 2], 0, (numFrames - i) * sizeof(StereoSample));
			resampler.primed = false;
			D_PRINTLN("USB audio: input underrun");
			break;
		}

		int32_t const* kernel = resamplerKernel[resampler.phase >> (32 - kResamplerPhaseBits)];
		uint32_t frame = resampler.readFrame - kResamplerTapsBehind;
		int64_t sumL = 0;
		int64_t sumR = 0;
		for (int32_t tap = 0; tap < kResamplerTaps; tap++) {
			StereoSample const& sample = fromHostRing[(frame + tap) & (kFromHostRingFrames - 1)];
			sumL += (int64_t)sample.l * kernel[tap];
			sumR += (int64_t)sample.r * kernel[tap];
		}
		output[i * 2] = saturate(sumL >> kResamplerCoefficientBits);
		output[i * 2 + 1] = saturate(sumR >> kResamplerCoefficientBits);

		uint64_t position = (uint64_t)resampler.phase + resampler.increment;
		resampler.readFrame += (uint32_t)(position >> 32);
		resampler.phase = (uint32_t)position;
	}

	// Everything before the kernel's reach can now be overwritten
	fromHostRead.store(resampler.readFrame - kResamplerTapsBehind, std::memory_order_release);
}

} // namespace

/*********************************************************************************************************************
 * Hooks for the Renesas USB driver
 */

extern "C" void usbAudioSetInterface(uint16_t interfaceNumber, uint16_t alternateSetting) {
	switch (interfaceNumber) {
	case USB_PAUDIO_INTERFACE_STREAM_OUT:
		stopFromHost();
		if (alternateSetting) {
			startFromHost();
		}
		break;

	case USB_PAUDIO_INTERFACE_STREAM_IN:
		stopToHost();
		if (alternateSetting) {
			startToHost();
		}
		break;

	default:
		break;
	}
}

extern "C" void usbAudioStopAll(void) {
	if (fromHostStreaming) {
		stopFromHost();
	}
	if (toHostStreaming) {
		stopToHost();
	}
}

extern "C" uint16_t usbAudioHandleBrdy(uint16_t pipeBits) {
	uint16_t ours = pipeBits & kPipeBits;
	if (ours) {
		USB200.BRDYSTS = (uint16_t)~ours;
		if (ours & (1 << kPipeFromHost)) {
			startFromHostDma();
		}
		// The to-host pipe's BRDY just says a buffer's free. We don't enable it - SOF paces that side - but it still
		// gets flagged, and we don't want the MIDI code thinking it's for it
	}
	return ours;
}

extern "C" void usbAudioHandleSof(void) {
	if (!toHostStreaming || toHostDmaBusy) {
		return;
	}
	if (!(USB200.D0FIFOCTR & USB_FRDY)) {
		return; // Both buffers already hold packets the host hasn't collected
	}

	uint32_t read = toHostRead.load(std::memory_order_relaxed);
	uint32_t fill = toHostWritten.load(std::memory_order_acquire) - read;
	if (fill > kToHostRingFrames * 3 / 4) {
		// Way behind - the host's been ignoring us. Skip ahead rather than send stale audio
		read += fill - kToHostTargetFill;
		fill = kToHostTargetFill;
	}

	uint32_t numFrames = framesForNextPacket(fill);
	if (numFrames == 0) {
		toHostRead.store(read, std::memory_order_release);
		return; // The host gets a zero-length packet
	}

	auto* staging = uncached(reinterpret_cast<StereoSample*>(toHostStaging));
	for (uint32_t i = 0; i < numFrames; i++) {
		staging[i] = toHostRing[(read + i) & (kToHostRingFrames - 1)];
	}
	toHostRead.store(read + numFrames, std::memory_order_release);

	toHostDmaBusy = true;
	startDma(USB_AUDIO_TX_DMA_CHANNEL, reinterpret_cast<uint32_t>(toHostStaging),
	         reinterpret_cast<uint32_t>(&USB200.D0FIFO.UINT32), numFrames * kBytesPerFrame);
	USB200.D0FIFOSEL |= USB_DREQE;
}

/*********************************************************************************************************************
 * For the audio engine
 */

namespace deluge::usb_audio {

bool receivingFromHost() {
	return fromHostStreaming;
}

bool sendingToHost() {
	return toHostStreaming;
}

int32_t* getInputRingStart() {
	return inputRing;
}

int32_t* getInputRingEnd() {
	return &inputRing[SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS];
}

int32_t* toInputRing(int32_t const* rxPosition) {
	return inputRing + (rxPosition - getRxBufferStart());
}

void fillInputRing(uint32_t rxPosition, size_t numSamples) {
	constexpr uint32_t kRingFrames = SSI_RX_BUFFER_NUM_SAMPLES;
	uint32_t startFrame = (reinterpret_cast<int32_t*>(rxPosition) - getRxBufferStart()) / NUM_MONO_INPUT_CHANNELS;

	// Normally we pick up exactly where the last render left off. If the RX position has jumped (see
	// AudioEngine::slowRoutine()), clear the gap, so anything reading back through it gets silence rather than old
	// audio
	uint32_t gap = (startFrame - inputRingWriteFrame) & (kRingFrames - 1);
	if (gap != 0) {
		if (gap < kRingFrames / 2) {
			for (uint32_t frame = inputRingWriteFrame; frame != startFrame; frame = (frame + 1) & (kRingFrames - 1)) {
				inputRing[frame * 2] = 0;
				inputRing[frame * 2 + 1] = 0;
			}
		}
		inputRingWriteFrame = startFrame;
	}

	while (numSamples) {
		size_t numThisTime = std::min<size_t>(numSamples, kRingFrames - inputRingWriteFrame);
		int32_t* output = &inputRing[inputRingWriteFrame * 2];
		if (fromHostStreaming) {
			resampleFromHost(output, numThisTime);
		}
		else {
			std::memset(output, 0, numThisTime * sizeof(StereoSample));
		}
		inputRingWriteFrame = (inputRingWriteFrame + numThisTime) & (kRingFrames - 1);
		numSamples -= numThisTime;
	}
}

void sendToHost(std::span<const StereoSample> frames) {
	if (!toHostStreaming) {
		return;
	}
	uint32_t written = toHostWritten.load(std::memory_order_relaxed);
	uint32_t space = kToHostRingFrames - (written - toHostRead.load(std::memory_order_acquire));
	size_t numFrames = std::min<size_t>(frames.size(), space);
	for (size_t i = 0; i < numFrames; i++) {
		toHostRing[(written + i) & (kToHostRingFrames - 1)] = frames[i];
	}
	toHostWritten.store(written + numFrames, std::memory_order_release);
}

} // namespace deluge::usb_audio
