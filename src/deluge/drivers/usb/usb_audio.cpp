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
#include "storage/flash_storage.h"
#include "timers_interrupts/timers_interrupts.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

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
void usb_cstd_clr_stall(usb_utr_t* ptr, uint16_t pipe);
void v7_dma_flush_range(uint32_t start, uint32_t end);
}

namespace deluge::usb_audio {
// Defaults make the Deluge work as an audio interface out of the box
bool directMonitoring = true;
OutputSource outputSource = OutputSource::MAIN_MIX;
} // namespace deluge::usb_audio

namespace {

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

// Copies between a ring (indexed by a free-running frame count) and a flat buffer, in at most two contiguous runs
template <size_t N>
void copyFromRing(std::array<StereoSample, N> const& ring, uint32_t start, StereoSample* destination, size_t count) {
	static_assert((N & (N - 1)) == 0);
	uint32_t index = start & (N - 1);
	size_t first = std::min<size_t>(count, N - index);
	std::memcpy(destination, &ring[index], first * sizeof(StereoSample));
	std::memcpy(destination + first, &ring[0], (count - first) * sizeof(StereoSample));
}

template <size_t N>
void copyToRing(std::array<StereoSample, N>& ring, uint32_t start, StereoSample const* source, size_t count) {
	static_assert((N & (N - 1)) == 0);
	uint32_t index = start & (N - 1);
	size_t first = std::min<size_t>(count, N - index);
	std::memcpy(&ring[index], source, first * sizeof(StereoSample));
	std::memcpy(&ring[0], source + first, (count - first) * sizeof(StereoSample));
}

// Frames from the host, written by the DMA-complete interrupt, read by readFromHost() in the audio routine
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
// Maps frame counts in toHostRing to positions in the SSI TX buffer: (count + this) % SSI_TX_BUFFER_NUM_SAMPLES. -1
// until sendToHost() has given us something to go by
std::atomic<int32_t> toHostTxOffset{-1};

volatile bool fromHostStreaming = false;
volatile bool toHostStreaming = false;
volatile bool fromHostDmaBusy = false;
volatile bool toHostDmaBusy = false;
uint32_t fromHostDmaBytes;

// The SOF interrupt (every 1ms while either direction's streaming) doubles as a watchdog, so that nothing that goes
// wrong - a BRDY that slipped past, a DMA that never finished or whose interrupt never came, a pipe that stopped
// accepting packets - can leave a stream dead until the host restarts it. These count the SOF interrupts each DMA's
// been running for
constexpr uint32_t kDmaTimeoutFrames = 4; // A packet's DMA takes microseconds, so this is never a slow one
uint32_t fromHostDmaFrames = 0;
uint32_t toHostDmaFrames = 0;
// How many times the watchdog had to step in - for looking at in a debugger
uint32_t fromHostRecoveries = 0;
uint32_t toHostRecoveries = 0;

// SSI-RX-shaped ring holding the audio from the host, for the rest of the Deluge to read like an input
PLACE_SDRAM_BSS alignas(CACHE_LINE_SIZE) int32_t inputRing[SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS];
uint32_t inputRingWriteFrame = 0; // Where fillInputRing() got up to, in frames from the start
// How many frames of silence have been written since the host last sent anything. Once it's the whole ring, there's
// nothing left to clear
uint32_t inputRingSilentFrames = 0;

bool interruptsSetUp = false;

bool active = false; // Latched from FlashStorage::usbAudioEnabled each time USB starts as a peripheral

/*********************************************************************************************************************
 * DMA, and the interrupts driving it
 */

void stopDma(int32_t channel) {
	DMACn(channel).CHCTRL_n = DMAC0_CHCTRL_n_CLREN | DMAC0_CHCTRL_n_CLRRQ;
	for (int32_t i = 0; i < 10000 && (DMACn(channel).CHSTAT_n & (DMAC0_CHSTAT_n_EN | DMAC0_CHSTAT_n_TACT)); i++) {}
	DMACn(channel).CHCTRL_n = DMAC0_CHCTRL_n_SWRST;
}

bool dmaFinished(int32_t channel) {
	return DMACn(channel).CHSTAT_n & (DMAC0_CHSTAT_n_END | DMAC0_CHSTAT_n_TC); // Cleared by startDma()
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
	fromHostDmaFrames = 0;
	startDma(USB_AUDIO_RX_DMA_CHANNEL, reinterpret_cast<uint32_t>(&USB200.D1FIFO.UINT32),
	         reinterpret_cast<uint32_t>(fromHostStaging), (numBytes + 3) & ~3u);
	USB200.D1FIFOSEL |= USB_DREQE;
}

void fromHostDmaComplete(uint32_t) {
	if (!fromHostDmaBusy || !dmaFinished(USB_AUDIO_RX_DMA_CHANNEL)) {
		return; // Late - the watchdog's already dealt with this transfer
	}
	USB200.D1FIFOSEL &= ~USB_DREQE;

	if (fromHostStreaming) {
		auto const* frames = uncached(reinterpret_cast<StereoSample const*>(fromHostStaging));
		uint32_t numFrames = fromHostDmaBytes / kBytesPerFrame;
		uint32_t written = fromHostWritten.load(std::memory_order_relaxed);
		uint32_t space = kFromHostRingFrames - (written - fromHostRead.load(std::memory_order_acquire));
		numFrames = std::min(numFrames, space); // If the reader's that far behind, it'll be resyncing anyway
		copyToRing(fromHostRing, written, frames, numFrames);
		fromHostWritten.store(written + numFrames, std::memory_order_release);
	}

	fromHostDmaBusy = false;
	startFromHostDma(); // The pipe is double buffered, so the next packet might already be there
}

// How many frames are waiting in the SSI TX buffer for the codec to play, up to and including txFrame (an index into
// it). 1 to SSI_TX_BUFFER_NUM_SAMPLES - the audio engine counts both ends being in the same place as it being full
uint32_t txFramesQueued(uint32_t txFrame) {
	uint32_t playing =
	    (reinterpret_cast<int32_t*>(getTxBufferCurrentPlace()) - getTxBufferStart()) / NUM_MONO_OUTPUT_CHANNELS;
	return ((txFrame - playing - 1) & (SSI_TX_BUFFER_NUM_SAMPLES - 1)) + 1;
}

// How many frames to send the host this USB frame. 44.1 on average, nudged by one either way to follow our sample
// clock - which a host doing implicit feedback then copies, so this sets the rate both ways.
//
// What we steer by is the backlog: frames the codec's played that we haven't sent yet. The audio routine renders
// anywhere up to a TX buffer (128 frames) ahead of the codec, depending on how busy it is, so how full toHostRing is
// swings around by that much on its own - which a busy Deluge made us chase, running dry and stopping to rebuild. The
// backlog only moves with the codec's clock against the USB one
constexpr int32_t kToHostTargetBacklog = 96;
constexpr int32_t kToHostTolerance = 24;
uint32_t toHostRateAccumulator = 0;
int32_t toHostAverageBacklog = 0; // 24.8 fixed point
bool toHostPrimed = false;

uint32_t framesForNextPacket(int32_t backlog) {
	if (!toHostPrimed) {
		if (backlog < kToHostTargetBacklog) {
			return 0;
		}
		toHostPrimed = true;
		toHostAverageBacklog = backlog * 256;
	}

	toHostAverageBacklog += (backlog * 256 - toHostAverageBacklog) >> 6;

	toHostRateAccumulator += USB_PAUDIO_SAMPLE_RATE / 100;
	uint32_t numFrames = toHostRateAccumulator / 10;
	toHostRateAccumulator %= 10;

	int32_t error = (toHostAverageBacklog >> 8) - kToHostTargetBacklog;
	if (error > kToHostTolerance) {
		numFrames++;
	}
	else if (error < -kToHostTolerance) {
		numFrames--;
	}
	return numFrames;
}

void toHostDmaComplete(uint32_t) {
	if (!toHostDmaBusy || !dmaFinished(USB_AUDIO_TX_DMA_CHANNEL)) {
		return; // Late - the watchdog's already dealt with this transfer
	}
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

// Gives up on a DMA that's never going to finish, throwing away the packet it was moving
void abortFromHostDma() {
	stopDma(USB_AUDIO_RX_DMA_CHANNEL);
	USB200.D1FIFOSEL &= ~USB_DREQE;
	DMACn(USB_AUDIO_RX_DMA_CHANNEL).CHCFG_n = kChcfgFromHost;
	USB200.D1FIFOCTR = USB_BCLR;
	fromHostDmaBusy = false;
}

void abortToHostDma() {
	stopDma(USB_AUDIO_TX_DMA_CHANNEL);
	USB200.D0FIFOSEL &= ~USB_DREQE;
	DMACn(USB_AUDIO_TX_DMA_CHANNEL).CHCFG_n = kChcfgToHost;
	USB200.D0FIFOCTR = USB_BCLR;
	toHostDmaBusy = false;
}

// Called each SOF while a DMA's running. Returns whether it had to step in
bool checkDma(int32_t channel, uint32_t& framesRunning, void (*complete)(uint32_t), void (*abort)()) {
	if (++framesRunning < kDmaTimeoutFrames) {
		return false;
	}
	if (dmaFinished(channel)) {
		complete(0); // Finished, but its interrupt never came
	}
	else {
		abort();
	}
	return true;
}

// Puts a pipe back to accepting packets, if anything's set it to NAK or STALL behind our back
bool ensurePipeAccepting(uint16_t pipe) {
	volatile uint16_t* pipeCtr = &USB200.PIPE1CTR + (pipe - USB_PIPE1);
	uint16_t pid = *pipeCtr & USB_PID;
	if (pid == USB_PID_BUF) {
		return false;
	}
	if (pid & USB_PID_STALL) {
		usb_cstd_clr_stall(USB_NULL, pipe);
	}
	usb_cstd_set_buf(USB_NULL, pipe);
	return true;
}

// The watchdog, for audio from the host
void serviceFromHost() {
	if (!fromHostStreaming) {
		return;
	}
	bool recovered = ensurePipeAccepting(kPipeFromHost);
	if (fromHostDmaBusy) {
		recovered |= checkDma(USB_AUDIO_RX_DMA_CHANNEL, fromHostDmaFrames, fromHostDmaComplete, abortFromHostDma);
	}
	// If a packet's waiting and nothing's reading it - because its BRDY came while the DMA was busy, and the buffer
	// wasn't ready yet when the DMA finished - then nothing else would ever start reading it, since there's no new
	// BRDY once both buffers are full
	if (!fromHostDmaBusy) {
		startFromHostDma();
		recovered |= fromHostDmaBusy;
	}
	if (recovered) {
		fromHostRecoveries++;
	}
}

void setSofInterrupt() {
	if (fromHostStreaming || toHostStreaming) {
		if (!(USB200.INTENB0 & USB_SOFE)) {
			USB200.INTSTS0 = (uint16_t)~USB_SOFR;
			USB200.INTENB0 |= USB_SOFE;
		}
	}
	else {
		USB200.INTENB0 &= ~USB_SOFE;
	}
}

/*********************************************************************************************************************
 * Starting and stopping the streams. These run in the USB task, not an interrupt
 */

void stopFromHost() {
	CriticalSectionGuard guard;
	fromHostStreaming = false;
	setSofInterrupt();
	USB200.BRDYENB &= ~(1 << kPipeFromHost);
	usb_cstd_set_nak(USB_NULL, kPipeFromHost);
	if (interruptsSetUp) {
		stopDma(USB_AUDIO_RX_DMA_CHANNEL);
	}
	fromHostDmaBusy = false;
	selectPipe(USB200.D1FIFOSEL, USB_PIPE0); // Let go of the pipe, so it can be reconfigured
}

void startFromHost() {
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
	setSofInterrupt();                         // For the watchdog
}

void stopToHost() {
	CriticalSectionGuard guard;
	toHostStreaming = false;
	setSofInterrupt();
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
	toHostTxOffset.store(-1, std::memory_order_relaxed);
	toHostPrimed = false;
	toHostRateAccumulator = 0;
	toHostDmaBusy = false;
	toHostStreaming = true;

	usb_cstd_set_buf(USB_NULL, kPipeToHost);
	setSofInterrupt(); // We send a packet each USB frame
}

/*********************************************************************************************************************
 * Reading the audio from the host. The endpoint's asynchronous with implicit feedback, so a host that supports that
 * sends us exactly as many frames as we send it, which follows our sample clock - and they can be copied straight
 * through. Hosts that ignore it send at their own clock's 44.1kHz, which drifts slowly against ours (around 4 frames a
 * second at 100ppm), so as a fallback we drop or repeat a single frame now and then to keep the ring at a steady fill.
 */

// How far ahead of the codec's playback we aim to have the host's audio, in frames. That's what's in the ring past the
// read position, plus what's already queued in the TX buffer in front of the render reading it - which is anywhere from
// almost nothing to a whole TX buffer (128 frames), depending on how busy the audio routine is. Steering by the fill
// alone, as we used to, chased those swings: a busy Deluge kept running the ring dry and restarting it. This only moves
// with the host's clock against ours. A render window can take up to 128 frames, and needs them all there already -
// the worst case for queued + window is ~190 - and packets bring ~44 at a time
constexpr uint32_t kFromHostTargetLatency = 256;
// How far the (averaged) latency can drift before we drop or repeat a frame. Packet and render timing jitter alone move
// it a lot less than this once averaged
constexpr int32_t kFromHostTolerance = 48;
// At most one correction per this many frames (~23ms): ~1000ppm of correction - far more than any clock mismatch - but
// spread out, so it's never more than a single, isolated frame
constexpr uint32_t kFromHostCorrectionInterval = 1024;

struct {
	uint32_t generation = 0;
	bool primed = false;
	uint32_t readFrame = 0;
	int32_t averageLatency = kFromHostTargetLatency << 8; // 24.8 fixed point
	uint32_t framesSinceCorrection = 0;
} fromHostReader;

// Throws away numFrames frames of audio from the host - for when the audio engine has skipped ahead
void skipFromHost(uint32_t numFrames) {
	auto& reader = fromHostReader;
	if (!reader.primed || reader.generation != fromHostGeneration.load(std::memory_order_relaxed)) {
		return; // It'll start from the latest audio anyway
	}
	uint32_t written = fromHostWritten.load(std::memory_order_acquire);
	if (numFrames >= written - reader.readFrame) {
		reader.readFrame = written;
		reader.primed = false;
	}
	else {
		reader.readFrame += numFrames;
	}
	fromHostRead.store(reader.readFrame, std::memory_order_release);
}

// Copies numFrames frames of audio from the host, interleaved stereo, into output. txQueued is how many frames the
// codec has to play before it gets to these
void readFromHost(int32_t* output, size_t numFrames, uint32_t txQueued) {
	auto& reader = fromHostReader;
	uint32_t generation = fromHostGeneration.load(std::memory_order_relaxed);
	if (reader.generation != generation) {
		// A new stream - anything left in the ring is from the last one
		reader.generation = generation;
		reader.readFrame = fromHostWritten.load(std::memory_order_acquire);
		reader.primed = false;
	}

	uint32_t written = fromHostWritten.load(std::memory_order_acquire);
	uint32_t fill = written - reader.readFrame;

	if (!reader.primed || fill > kFromHostRingFrames - 64) {
		// Waiting for enough to arrive - or way behind, because the host sped up enormously or we stalled. Either way,
		// (re)start the read position at the target fill
		if (fill > kFromHostRingFrames) {
			reader.readFrame = written; // Must have been from the previous stream
			fill = 0;
		}
		if (fill + txQueued < kFromHostTargetLatency) {
			std::memset(output, 0, numFrames * sizeof(StereoSample));
			fromHostRead.store(reader.readFrame, std::memory_order_release);
			return;
		}
		fill = kFromHostTargetLatency - txQueued;
		reader.readFrame = written - fill;
		reader.primed = true;
		reader.averageLatency = kFromHostTargetLatency << 8;
		reader.framesSinceCorrection = 0;
	}

	// Fallback for hosts not following our clock: nudge the read position by a frame if the latency's drifted
	reader.averageLatency += ((int32_t)((fill + txQueued) << 8) - reader.averageLatency) >> 5;
	reader.framesSinceCorrection += numFrames;
	if (reader.framesSinceCorrection >= kFromHostCorrectionInterval) {
		int32_t error = (reader.averageLatency >> 8) - (int32_t)kFromHostTargetLatency;
		if (error > kFromHostTolerance && fill > 0) {
			reader.readFrame++; // Host's faster - drop a frame
			fill--;
			reader.framesSinceCorrection = 0;
		}
		else if (error < -kFromHostTolerance && fill > numFrames) {
			reader.readFrame--; // Host's slower - repeat a frame
			fill++;
			reader.framesSinceCorrection = 0;
		}
	}

	size_t numToCopy = std::min<size_t>(numFrames, fill);
	auto* frames = reinterpret_cast<StereoSample*>(output);
	copyFromRing(fromHostRing, reader.readFrame, frames, numToCopy);
	reader.readFrame += numToCopy;

	if (numToCopy < numFrames) {
		// Ran out - the host's stopped sending, or fallen behind. Silence until there's enough again
		std::memset(&frames[numToCopy], 0, (numFrames - numToCopy) * sizeof(StereoSample));
		reader.primed = false;
		D_PRINTLN("USB audio: input underrun");
	}

	fromHostRead.store(reader.readFrame, std::memory_order_release);
}

} // namespace

/*********************************************************************************************************************
 * Hooks for the Renesas USB driver
 */

extern "C" bool usbAudioStartPeripheral(void) {
	active = FlashStorage::usbAudioEnabled;
	return active;
}

extern "C" bool usbAudioActive(void) {
	return active;
}

extern "C" void usbAudioSetInterface(uint16_t interfaceNumber, uint16_t alternateSetting) {
	if (!active) {
		return; // MIDI-only configuration - interface numbers don't mean the same
	}
	// Both of the out stream's streaming alternate settings use the same pipe and format. Only how the host times its
	// packets differs - readFromHost() copes with either
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
	serviceFromHost();

	if (!toHostStreaming) {
		return;
	}
	bool recovered = ensurePipeAccepting(kPipeToHost);
	if (toHostDmaBusy) {
		recovered |= checkDma(USB_AUDIO_TX_DMA_CHANNEL, toHostDmaFrames, toHostDmaComplete, abortToHostDma);
	}
	if (recovered) {
		toHostRecoveries++;
	}
	if (toHostDmaBusy) {
		return;
	}
	if (!(USB200.D0FIFOCTR & USB_FRDY)) {
		return; // Both buffers already hold packets the host hasn't collected
	}

	uint32_t read = toHostRead.load(std::memory_order_relaxed);
	uint32_t written = toHostWritten.load(std::memory_order_acquire);
	int32_t txOffset = toHostTxOffset.load(std::memory_order_relaxed);
	if (txOffset < 0) {
		return; // Nothing's been rendered for the host yet - it gets a zero-length packet
	}
	uint32_t fill = written - read;
	uint32_t txQueued = txFramesQueued(written + txOffset); // Of what's in the ring, how much the codec's yet to play
	if (fill > kToHostRingFrames * 3 / 4) {
		// Way behind - the host's been ignoring us. Skip ahead rather than send stale audio
		fill = txQueued + kToHostTargetBacklog;
		read = written - fill;
	}

	uint32_t numFrames = framesForNextPacket((int32_t)fill - (int32_t)txQueued);
	if (numFrames == 0) {
		toHostRead.store(read, std::memory_order_release);
		return; // The host gets a zero-length packet
	}

	// We keep sending a steady 44 or 45 frames even if the audio routine's fallen behind (when it's so busy the codec's
	// run out), padding with silence - a host doing implicit feedback takes its timing from these, so stopping to
	// catch up would throw that, and the audio coming back from it, into disarray
	uint32_t numToCopy = std::min(numFrames, fill);
	auto* staging = uncached(reinterpret_cast<StereoSample*>(toHostStaging));
	copyFromRing(toHostRing, read, staging, numToCopy);
	std::memset(&staging[numToCopy], 0, (numFrames - numToCopy) * sizeof(StereoSample));
	toHostRead.store(read + numToCopy, std::memory_order_release);

	toHostDmaBusy = true;
	toHostDmaFrames = 0;
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

void fillInputRing(uint32_t rxPosition, size_t numSamples, size_t txQueued) {
	constexpr uint32_t kRingFrames = SSI_RX_BUFFER_NUM_SAMPLES;
	uint32_t startFrame = (reinterpret_cast<int32_t*>(rxPosition) - getRxBufferStart()) / NUM_MONO_INPUT_CHANNELS;

	// Normally we pick up exactly where the last render left off. If the RX position has jumped (see
	// AudioEngine::slowRoutine()), clear the gap, so anything reading back through it gets silence rather than old
	// audio. The jump's because the audio routine fell a whole TX buffer behind the codec, which the host's audio
	// didn't, so skip the same amount of that too - or it'd end up that much further ahead of the codec
	uint32_t gap = (startFrame - inputRingWriteFrame) & (kRingFrames - 1);
	if (gap != 0) {
		if (gap < kRingFrames / 2) {
			for (uint32_t frame = inputRingWriteFrame; frame != startFrame; frame = (frame + 1) & (kRingFrames - 1)) {
				inputRing[frame * 2] = 0;
				inputRing[frame * 2 + 1] = 0;
			}
			if (fromHostStreaming) {
				skipFromHost(gap);
			}
		}
		inputRingWriteFrame = startFrame;
	}

	while (numSamples) {
		size_t numThisTime = std::min<size_t>(numSamples, kRingFrames - inputRingWriteFrame);
		int32_t* output = &inputRing[inputRingWriteFrame * 2];
		if (fromHostStreaming) {
			readFromHost(output, numThisTime, txQueued);
			inputRingSilentFrames = 0;
		}
		else if (inputRingSilentFrames < kRingFrames) {
			std::memset(output, 0, numThisTime * sizeof(StereoSample));
			inputRingSilentFrames += numThisTime;
		}
		inputRingWriteFrame = (inputRingWriteFrame + numThisTime) & (kRingFrames - 1);
		txQueued += numThisTime;
		numSamples -= numThisTime;
	}
}

void sendToHost(std::span<const StereoSample> frames, int32_t const* txPosition) {
	if (!toHostStreaming) {
		return;
	}
	uint32_t written = toHostWritten.load(std::memory_order_relaxed);
	uint32_t space = kToHostRingFrames - (written - toHostRead.load(std::memory_order_acquire));
	size_t numFrames = std::min<size_t>(frames.size(), space);
	copyToRing(toHostRing, written, frames.data(), numFrames);
	written += numFrames;
	// The offset only changes when the stream (re)starts or the ring overflowed - every frame rendered normally goes
	// both here and to the TX buffer. Leaving it alone otherwise means the SOF interrupt can't catch it out of step
	// with toHostWritten, between the two stores
	uint32_t txFrame = (txPosition - getTxBufferStart()) / NUM_MONO_OUTPUT_CHANNELS;
	auto txOffset = (int32_t)((txFrame - written) & (SSI_TX_BUFFER_NUM_SAMPLES - 1));
	if (txOffset != toHostTxOffset.load(std::memory_order_relaxed)) {
		toHostTxOffset.store(txOffset, std::memory_order_relaxed);
	}
	toHostWritten.store(written, std::memory_order_release);
}

} // namespace deluge::usb_audio
