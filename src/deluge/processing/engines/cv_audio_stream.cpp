/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "processing/engines/cv_audio_stream.h"
#include "definitions_cxx.hpp"
#include "hid/display/display.h"
#include "processing/engines/audio_engine.h"
#include "util/functions.h"
#include <cmath>
#include <cstring>

extern "C" {
#include "RZA1/system/iodefine.h"
#include "RZA1/system/iodefines/rspi_iodefine.h"
#include "drivers/dmac/dmac.h"
}

/// Inverse of the measured output filter, as a one-pole/one-zero shelf. The pole comes from
/// the socket's measured ~700 Hz corner.
constexpr float kCvEqPole = 0.9049f;

// ============================================================================
// CV audio outputs
// ----------------------------------------------------------------------------
// Streams two independent tracks out of the CV sockets while the main outputs
// carry the usual mix.
//
// The DAC's chip-select sits on P6_1, which is the SPI block's own SSL00 pin and
// is already configured as such at boot. That lets the transfer engine feed the
// DAC continuously with no processor involvement. Words alternate between the
// DAC's two channels, so one buffer serves both sockets.
//
// Each socket's analogue path is a single-pole low-pass with its corner near
// 700 Hz, so each channel gets the inverse applied before it leaves.
// ============================================================================

extern "C" void v7_dma_flush_range(uint32_t start, uint32_t end);

#define CV_STREAM_DMA_CHANNEL 5 // unassigned on every model

void cvStreamStart();
void cvStreamStop();

namespace {

/// Words in the streamed buffer. Alternating, so half belong to each socket.
/// The ring has to hold the target lead *plus* one whole burst, because the pump writes a
/// window's worth of frames in one go. At 512 words that was 256 frames against a 128-frame
/// lead and bursts of up to ~136 -- no margin at all, and a full-size engine window could
/// lap the reader. The engine doubles its window size under load, so the bursts get bigger
/// exactly when the machine is busiest. 2048 words is 1024 frames, which leaves the burst
/// six times the room it needs. Costs 8 KB of SDRAM and no latency: see kCvTargetLead.
constexpr uint32_t kCvStreamWords = 4096;
constexpr uint32_t kCvFramesPerChannel = kCvStreamWords / 2;
constexpr uint32_t kCvMaxWindow = 256;

/// How far ahead of the DMA read pointer the pump tries to stay, in frames. This *is* the
/// CV output's latency behind the main outputs, and it is also how late the engine may be
/// before the buffer runs dry.
///
/// This has to exceed the worst phase excursion the engine produces, or the buffer runs dry
/// and the DMA reads frames that were never written. Measured 2026-08-13: idle the lead
/// swings ~57 frames, loaded it swings ~650. Simulation against that jitter puts the
/// minimum lead near 100 frames with a 768-frame target, and underruns with 512.
///
/// **This is the CV output's latency behind the main outputs: 768 frames is ~16.4 ms.**
/// Fine for a send into a pedal, a mixer channel or a modular case, which is what these
/// sockets are for; summing a send back against the same source in the mains will
/// comb-filter. Lower it if the resync count stays at zero on real songs -- it is the one
/// constant here with a cost the player can hear.
constexpr int32_t kCvTargetLead = 768;

PLACE_SDRAM_DATA uint32_t cvStreamBuffer[kCvStreamWords] __attribute__((aligned(CACHE_LINE_SIZE)));
PLACE_SDRAM_DATA int32_t cvSourceMono[2][kCvMaxWindow] __attribute__((aligned(CACHE_LINE_SIZE)));
PLACE_SDRAM_DATA int32_t cvCaptureScratch[kCvMaxWindow * 2] __attribute__((aligned(CACHE_LINE_SIZE)));

const uint32_t cvStreamDmaLinkDescriptor[] __attribute__((aligned(CACHE_LINE_SIZE))) = {
    0b1101,                                                                             // Header
    (uint32_t)cvStreamBuffer,                                                           // Source
    (uint32_t)&RSPI(SPI_CHANNEL_CV).SPDR.LONG,                                          // Destination
    sizeof(cvStreamBuffer),                                                             // Transaction size
    0b10000001001000100010001000101000 | DMA_LVL_FOR_SSI | (CV_STREAM_DMA_CHANNEL & 7), // Config
    0,                                                                                  // Interval
    0,                                                                                  // Extension
    (uint32_t)cvStreamDmaLinkDescriptor                                                 // Next link: itself
};

bool cvStereoSplitGlobal = false;
bool cvStreamRunning = false;
uint32_t cvStreamWriteFrame = 0;
bool cvSourceValid[2] = {false, false};
float cvFeedLastIn[2] = {0.0f, 0.0f};
float cvFeedLastOut[2] = {0.0f, 0.0f};

/// Which Clips fed each socket. Accumulated while the outputs render, then
/// compared with the previous render's set: per-clip routing means the set can
/// change mid-performance, and carrying filter state across that change clicks.
uint32_t cvSourceSignatureAccum[2] = {0, 0};
uint32_t cvSourceSignatureLive[2] = {0, 0};
bool cvAnyRoutedAccum = false;
bool cvSplitAccum = false;
bool cvSplitLive = false;

/// Treble correction, expressed as the one number that actually decides it: how hard we
/// invert the socket's own 700 Hz low-pass.
///
/// The shelf is  out = scale*(in - pole*lastIn) + limit*lastOut.  At DC (z=1) its gain is
/// scale*(1-pole)/(1-limit); at Nyquist (z=-1) it is scale*(1+pole)/(1+limit). Their ratio
/// is the boost, and it depends on `limit` alone:
///
///     boost = K * (1-limit)/(1+limit),   K = (1+pole)/(1-pole)
///
/// so  limit = (K-boost)/(K+boost), and `scale` is then fixed by requiring unity gain at DC.
/// That unity-at-DC constraint is what makes this a tone control rather than a volume one:
/// the bass sits exactly where it would with no correction at all, and only the treble moves.
///
/// K itself is full inversion -- flat to 20 kHz -- and is unreachable, because the output
/// stage compresses above about a third of its swing and every dB of boost is a dB of
/// headroom spent. The chosen value is therefore a listening call, not a calculation.
///
/// **x6.87, chosen by ear 2026-08-15**, flat to roughly 4.8 kHz. It was picked from a 1-9
/// menu control built for that one session and removed again afterwards: a tone knob is a
/// question asked of a user who has no way to answer it, and the whole point of correcting
/// a known filter is that there is a right answer. If user feedback ever disagrees, the
/// control is a small revert away and this comment is the reason it went.
///
/// Supersedes the x3.125 tuned by ear on 2026-08-04. That earlier session tried x6 and
/// rejected it as harsh -- correctly at the time, because the resampler then in use put
/// frequency-modulation sidebands right where the boost lands (-25 dB by 5 kHz against
/// -51 dB at 260 Hz). The resampler rewrite dropped that floor by roughly 24 dB, and the
/// setting that had been unusable became the preferred one. **The verdict changed because
/// the signal path changed, not because the ear did.**
constexpr float kCvEqK = (1.0f + kCvEqPole) / (1.0f - kCvEqPole); // 20.03
constexpr float kCvEqBoost = 6.866f;
constexpr float kCvStreamEqLimit = (kCvEqK - kCvEqBoost) / (kCvEqK + kCvEqBoost);
constexpr float kCvStreamEqScale = (1.0f - kCvStreamEqLimit) / (1.0f - kCvEqPole);

constexpr float kCvFeedBaseScale = 0.098f;
/// Display 50 is x256, and every step below it is a fixed 1.2 dB rather than a fixed
/// multiplier, so the steps sound evenly spaced the whole way down and reach roughly 59 dB
/// down by 1. Display 0 is a true mute rather than the bottom of the taper.
///
/// The scale moved from 1-100 at 0.6 dB to 0-50 at 1.2 dB when this became a param. Coarser by
/// a factor of two, and taken deliberately: 0-50 is what every other param on the machine
/// displays, so a master recorded into an automation lane now reads the same numbers the menu
/// shows. The alternative kept the finer steps and made the two disagree.
constexpr float kCvLevelTopGain = 256.0f;
constexpr float kCvLevelDbPerStep = 1.2f;

float cvLevelToScale(int32_t display) {
	if (display <= 0) {
		return 0.0f;
	}
	if (display > kCvMasterDisplayMax) {
		display = kCvMasterDisplayMax;
	}
	const float decibels = (float)(display - kCvMasterDisplayMax) * kCvLevelDbPerStep;
	return kCvFeedBaseScale * kCvLevelTopGain * powf(10.0f, decibels / 20.0f);
}

float cvFeedScale[2] = {cvLevelToScale(40), cvLevelToScale(40)};
int32_t cvPrevSample[2] = {0, 0};

/// Output samples per input sample. SPBR=9 gives a 3.333 MHz bit clock, and the
/// note's measurement of ~35.5 bit-times per frame puts the stream at ~46.95 kHz
/// per socket against the engine's 44.1 kHz. That is only the starting estimate --
/// the loop below trims it to whatever the hardware actually does.
constexpr float kCvNominalRate = 46948.0f / 44100.0f;

/// How far the ratio may stray from nominal. Both clocks come off the same crystal, so the
/// true ratio is fixed and the only slack needed is for the estimate above being slightly
/// wrong -- the loop finds the real value within this band and holds it.
///
/// This used to be 0.90 to 1.25, which let a control loop transpose the audio by roughly a
/// musical third. Measured 2026-08-13: under load it sat *on* both of those clamps, which
/// was the "transposing" symptom. Nothing legitimate ever needs that range. If the resync
/// counter climbs steadily on a quiet song, this band is too narrow rather than too wide --
/// the true rate is then outside it and the loop cannot reach it.
/// Measured 2026-08-13 on an idle machine: the loop settles around 1064, which is the
/// nominal above -- so the estimate is good and the band only has to cover the residual.
/// +-0.3% is about 10 cents worst case, at the very edge of audible on a sustained tone,
/// against roughly 70 cents at the old +-2%.
constexpr float kCvRateSpan = 0.003f;
constexpr float kCvRateMin = kCvNominalRate * (1.0f - kCvRateSpan);
constexpr float kCvRateMax = kCvNominalRate * (1.0f + kCvRateSpan);

/// A rate trim can only fix a *rate* error. A phase error -- the buffer running empty or
/// overfull because the engine arrived late -- can only be nursed back over thousands of
/// windows, during which the integrator winds up and hits a clamp. Past this much error,
/// snap the write pointer back instead: one discontinuity, instantly recovered, rather than
/// continuous pitch movement. Measured 2026-08-13: without this the lead swung across the
/// entire ring, 0 to 1023.
/// Sized so that ordinary jitter never reaches it and only a real stall does: the lead may
/// wander between 128 and 1408 frames before this fires, inside a 2048-frame ring.
constexpr int32_t kCvResyncThreshold = 640;

/// The lead error is smoothed before it reaches the integrator, with a time constant of
/// roughly three seconds at the ~345 Hz window rate.
///
/// This is the fix. The engine renders in irregular windows, so the lead jitters by
/// hundreds of frames under load -- and that is *phase* jitter, which a rate trim cannot
/// correct and must not chase. Feeding the raw per-window error to the integrator made the
/// loop bang-bang between its clamps, measured 2026-08-13. Averaging leaves only genuine
/// rate drift, which is what a rate trim is actually for.
constexpr float kCvLeadAvgAlpha = 0.001f;
float cvLeadAvg = 0.0f;

/// Integrator, applied to the *averaged* error. Small enough that a full band traverse takes
/// seconds rather than the ten windows the old gain took. Measured 2026-08-13: at 1e-7 the
/// ratio swept the whole band in about a second, which is audible as a slow warble even
/// though the depth was only ~10 cents. Depth was fixed by the clamps; this fixes the speed.
constexpr float kCvRateTrackGain = 0.000000003f;

/// Proportional gain, on the SMOOTHED error.
///
/// A rate error integrates into lead, and this loop integrates lead error into rate: two
/// integrators in series, which is 180 degrees of phase lag and oscillates however small the
/// gain is made. Lowering the gain and adding a deadband only changed the period, which is
/// what every build on 2026-08-13 did. A double integrator needs damping, and that is what a
/// proportional term is.
///
/// There *was* a proportional term originally, fed the raw per-window error, which injected
/// jitter straight into the ratio -- so removing it was right, and putting it back on the
/// smoothed error is what it should always have been.
///
/// Sized so a 100-frame averaged error moves the ratio by ~0.03%, correcting that much lead
/// in about a second.
constexpr float kCvRateDampGain = 0.000003f;

// There is deliberately no proportional term any more. It applied the raw per-window error
// straight to the emitted rate, so a few hundred frames of jitter moved the ratio by ~0.8%
// -- about 14 cents -- injected from exactly the noise this loop is supposed to reject.

float cvRate = kCvNominalRate;
/// Resampling phase, carried across windows. See cvStreamPump.
float cvResamplePos = 0.0f;

constexpr uint16_t kCvCentre = 32768;
constexpr int32_t kCvClamp = 12000;

/// Command word the DAC expects: write-and-update, given channel, 16-bit value.
inline uint32_t cvWord(uint32_t channel, int32_t sample) {
	if (sample > kCvClamp) {
		sample = kCvClamp;
	}
	else if (sample < -kCvClamp) {
		sample = -kCvClamp;
	}
	const uint16_t voltage = (uint16_t)((int32_t)kCvCentre + sample);
	return ((uint32_t)(0b00110000 | (1u << channel)) << 24) | ((uint32_t)voltage << 8);
}

} // namespace

bool cvOutputsAvailable() {
	return !deluge::hid::display::have_oled_screen;
}

bool cvStreamIsRunning() {
	return cvStreamRunning;
}

namespace {

/// Adds one Clip's contribution to a socket. Several Clips can share a socket, so
/// the first one this window sets the buffer and the rest add into it.
/// `channel` picks what the socket gets: 0 for left, 1 for right, 2 for the mono sum.
///
/// The Clip's contribution is `post` minus `pre` -- the mix after it rendered minus the
/// snapshot taken before. That subtraction happens here, per sample, rather than in a
/// pass of its own beforehand: this loop already touches every sample it needs, so a
/// separate pass was a second walk over the same data for nothing.
///
/// The mono sum halves each side before adding, so a centred Clip comes out at the
/// level of one channel. A single channel is therefore taken at full scale, not
/// halved -- otherwise turning STEREO SPLIT on would drop a centred Clip by 6 dB.
///
/// `gainFrom` and `gainTo` are Q16 -- kCvSendGainUnity is unity, 0 is silence. The gain
/// ramps across the window rather than stepping at its edge: the window is only ~128
/// samples (~345 Hz), so a step per window is plainly audible as zipper noise while a
/// send is being ridden. At unity throughout there is no multiply at all, which keeps the
/// common case exactly as cheap as it was before sends existed -- this loop runs with all
/// interrupts disabled.
void cvAccumulateInto(uint32_t socket, uint32_t channel, const int32_t* post, const int32_t* pre, uint32_t numSamples,
                      int32_t gainFrom, int32_t gainTo) {
	const bool first = !cvSourceValid[socket];
	int32_t* const dest = cvSourceMono[socket];
	const bool unity = (gainFrom == kCvSendGainUnity) && (gainTo == kCvSendGainUnity);

	// Q16.16 accumulator so the per-sample step needs no divide inside the loop. 64-bit
	// because unity is 65536, and 65536 << 16 does not fit in a signed 32-bit.
	int64_t gain = (int64_t)gainFrom << 16;
	const int64_t gainStep = (((int64_t)(gainTo - gainFrom)) << 16) / (int32_t)numSamples;

	for (uint32_t i = 0; i < numSamples; i++) {
		int32_t sample;
		if (channel == 2) {
			// Shift each side before adding, exactly as the two-pass version did: the
			// difference was taken at full width and only halved here, so halving the two
			// differences separately keeps every rounding step where it already was.
			const int32_t diffL = post[i * 2] - pre[i * 2];
			const int32_t diffR = post[i * 2 + 1] - pre[i * 2 + 1];
			sample = (diffL >> 1) + (diffR >> 1);
		}
		else {
			sample = post[i * 2 + channel] - pre[i * 2 + channel];
		}
		if (!unity) {
			sample = (int32_t)(((int64_t)sample * (int32_t)(gain >> 16)) >> 16);
			gain += gainStep;
		}
		if (first) {
			dest[i] = sample;
		}
		else {
			dest[i] += sample;
		}
	}
	cvSourceValid[socket] = true;
}

} // namespace

/// Called from the per-output render loop with one Clip's isolated contribution.
/// With STEREO SPLIT off both sockets get the mono sum, so the same signal can go
/// to both; with it on CV1 gets left and CV2 gets right.
int32_t cvSendParamToGain(int32_t paramValue) {
	// Param space is the full signed range; the bottom is silence and the top is unity.
	const uint32_t position = (uint32_t)paramValue ^ 0x80000000u;
	const uint32_t linear = position >> 16; // 0 .. 65535
	if (linear >= 65535) {
		return kCvSendGainUnity;
	}
	// Unsigned deliberately: 65534 squared is 4.29e9, which overflows a signed 32-bit.
	return (int32_t)((linear * linear) >> 16);
}

/// `sendGain` is this Clip's two send amounts for this window, Q16. `lastSendGain` is the
/// same pair from the previous window, owned by the Clip and updated here -- it is what the
/// ramp starts from, so it has to persist between windows and be per Clip rather than per
/// socket, since several Clips can feed one socket at different amounts.
void cvStreamCapture(uint32_t sourceId, const int32_t* post, const int32_t* pre, uint32_t numSamples,
                     const int32_t* sendGain, int32_t* lastSendGain) {
	if (numSamples == 0 || numSamples > kCvMaxWindow) {
		return;
	}

	// Global now, not a Clip bit. The old per-Clip bit is still parsed from song files so
	// that a song saved before this change loads without complaint -- it just no longer
	// decides anything, because two cables cannot be a stereo pair for one clip and two
	// mono outs for another at the same time.
	const bool split = cvStereoSplitGlobal;
	// Mixed in rather than summed raw so that two Clips swapping sockets, or one
	// Clip changing its split mode, both register as a change.
	const uint32_t stamp = (sourceId ^ (split ? 0x5bd1e995u : 0u)) * 2654435761u;

	for (uint32_t socket = 0; socket < 2; socket++) {
		const int32_t gainTo = sendGain[socket];
		const int32_t gainFrom = lastSendGain[socket];
		lastSendGain[socket] = gainTo;

		// A send off at both ends of the window is simply not routed to this socket, and
		// must not stamp -- otherwise a send falling to zero never changes the socket's
		// signature and the correction filter is never reset.
		if (gainFrom == 0 && gainTo == 0) {
			continue;
		}

		cvAnyRoutedAccum = true;
		cvSplitAccum |= split;
		cvAccumulateInto(socket, split ? socket : 2, post, pre, numSamples, gainFrom, gainTo);
		cvSourceSignatureAccum[socket] += stamp + socket;
	}
}

/// Called once at the end of the render loop, when the full set of Clips feeding
/// each socket for this window is known. Resets the correction filter for any
/// socket whose sources changed, and starts or stops the stream to match.
void cvStreamRenderComplete() {
	for (uint32_t socket = 0; socket < 2; socket++) {
		if (cvSourceSignatureAccum[socket] != cvSourceSignatureLive[socket]) {
			cvSourceSignatureLive[socket] = cvSourceSignatureAccum[socket];
			cvFeedLastIn[socket] = 0.0f;
			cvFeedLastOut[socket] = 0.0f;
			cvPrevSample[socket] = 0;
		}
		cvSourceSignatureAccum[socket] = 0;
	}

	if (cvAnyRoutedAccum && !cvStreamRunning) {
		cvStreamStart();
	}
	else if (!cvAnyRoutedAccum && cvStreamRunning) {
		cvStreamStop();
	}
	cvAnyRoutedAccum = false;

	cvSplitLive = cvSplitAccum;
	cvSplitAccum = false;
}

bool cvStreamNeedsStereo() {
	return cvSplitLive;
}

bool cvGetStereoSplit() {
	return cvStereoSplitGlobal;
}

void cvSetStereoSplit(bool on) {
	cvStereoSplitGlobal = on;
}

int32_t cvMasterParamToDisplay(int32_t paramValue) {
	// Param space is the full signed range. Rounded rather than truncated so that the value
	// written back from a display number reads back as the same number -- truncating makes the
	// menu appear to drop a step every time you leave it and come back.
	const uint32_t position = (uint32_t)paramValue ^ 0x80000000u;
	return (int32_t)(((uint64_t)position * kCvMasterDisplayMax + 0x80000000ull) >> 32);
}

int32_t cvMasterDisplayToParam(int32_t display) {
	if (display <= 0) {
		return -2147483648;
	}
	if (display >= kCvMasterDisplayMax) {
		return 2147483647;
	}
	// Land in the middle of the display step's band, so a round trip through
	// cvMasterParamToDisplay returns what went in rather than sitting on a boundary. The
	// rounding term is half the *display* range, not half of param space -- getting that wrong
	// put steps 12 and 37 one out, which a simulation caught and no amount of listening would.
	const uint64_t position =
	    ((uint64_t)display * 4294967296ull + (uint64_t)(kCvMasterDisplayMax / 2)) / (uint64_t)kCvMasterDisplayMax;
	return (int32_t)((uint32_t)position ^ 0x80000000u);
}

uint8_t cvMasterKnobSocket = 0;

void cvSetMasterFromParams(int32_t cv1Value, int32_t cv2Value) {
	cvFeedScale[0] = cvLevelToScale(cvMasterParamToDisplay(cv1Value));
	// With the pair patched as one stereo destination there is one master, not two: the
	// sockets carry left and right of the same thing, so CV1's value drives both. This is the
	// same rule the sends follow, and it is why the second knob is idle when split is on.
	cvFeedScale[1] = cvLevelToScale(cvMasterParamToDisplay(cvStereoSplitGlobal ? cv1Value : cv2Value));
}

int32_t* cvStreamCaptureScratch() {
	return cvCaptureScratch;
}

/// Called once per audio window. Resamples both captured tracks to whatever rate
/// the transfer engine is running at, applies the correction, and interleaves
/// them into the buffer being streamed. Staying half a buffer ahead of the read
/// position is what keeps the two rates matched without knowing the ratio.
void cvStreamPump(uint32_t numSamples) {
	if (!cvStreamRunning || numSamples == 0 || numSamples > kCvMaxWindow) {
		cvSourceValid[0] = cvSourceValid[1] = false;
		return;
	}

	const uint32_t readFrame =
	    ((DMACn(CV_STREAM_DMA_CHANNEL).CRSA_n - (uint32_t)cvStreamBuffer) >> 3) & (kCvFramesPerChannel - 1);
	const uint32_t lead = (cvStreamWriteFrame - readFrame) & (kCvFramesPerChannel - 1);
	// Deliberately NOT half the ring: the lead sets latency, while the ring only has to be
	// big enough that a burst cannot lap the reader. Tying the two together would have made
	// every buffer enlargement cost delay for no reason.
	constexpr int32_t targetLead = kCvTargetLead;
	int32_t leadError = targetLead - (int32_t)lead;

	// Phase resync, for a genuine stall only. A rate trim cannot fix a phase error in any
	// useful time, so past the threshold the write pointer is simply put where it belongs.
	// Costs one discontinuity.
	//
	// The rate estimate is deliberately KEPT. Only the phase is broken here; the ratio is a
	// property of two crystals and is still correct, and throwing it away made the loop
	// re-acquire from nominal every time this fired.
	if (leadError > kCvResyncThreshold || leadError < -kCvResyncThreshold) {
		cvStreamWriteFrame = ((uint32_t)readFrame + (uint32_t)targetLead) & (kCvFramesPerChannel - 1);
		cvResamplePos = 0.0f;
		for (uint32_t socket = 0; socket < 2; socket++) {
			cvFeedLastIn[socket] = 0.0f;
			cvFeedLastOut[socket] = 0.0f;
			cvPrevSample[socket] = 0;
		}
		leadError = 0;
	}

	// Both clocks come off the same crystal, so the true ratio between them is a
	// constant -- there is nothing to chase, only something to converge on. The
	// integrator finds it and then stops moving; the proportional term only damps
	// acquisition, and is tiny so it does not move the ratio once locked.
	cvLeadAvg += ((float)leadError - cvLeadAvg) * kCvLeadAvgAlpha;

	// PI on the smoothed error. The integrator alone is a second integrator in series with
	// the one the buffer already provides, and oscillates; the proportional term damps it.
	cvRate += cvLeadAvg * kCvRateTrackGain;
	if (cvRate < kCvRateMin) {
		cvRate = kCvRateMin;
	}
	else if (cvRate > kCvRateMax) {
		cvRate = kCvRateMax;
	}
	float rateNow = cvRate + cvLeadAvg * kCvRateDampGain;
	if (rateNow < kCvRateMin) {
		rateNow = kCvRateMin;
	}
	else if (rateNow > kCvRateMax) {
		rateNow = kCvRateMax;
	}

	// Input samples consumed per output sample. Held constant and the phase carried
	// across windows, rather than restarting at zero each window and stretching that
	// window's input to fit a whole number of outputs.
	//
	// That difference is the whole point. Emitting a whole number of samples per
	// window forces the ratio to alternate between neighbouring integers -- about
	// 0.73% either way -- and a resampling ratio that alternates is frequency
	// modulation at the window rate. The sidebands it produces grow with frequency:
	// about -51 dB at 260 Hz, but only -25 dB by 5 kHz. That is why a sine sounded
	// clean while anything with strong harmonics sounded grainy. Carrying the phase
	// keeps the instantaneous ratio fixed and lets the sample count per window vary
	// instead, which costs nothing and takes the jitter to about 0.07%.
	const float step = 1.0f / rateNow;

	const float startPos = cvResamplePos;
	int32_t toEmit = 0;
	{
		float p = startPos;
		while (p < (float)numSamples && toEmit < (int32_t)kCvMaxWindow) {
			p += step;
			toEmit++;
		}
		if (toEmit < 1 || toEmit >= (int32_t)kCvMaxWindow) {
			toEmit = (toEmit < 1) ? 1 : (int32_t)kCvMaxWindow;
			cvResamplePos = 0.0f; // resync rather than carry a position we did not reach
		}
		else {
			cvResamplePos = p - (float)numSamples;
		}
	}

	for (uint32_t socket = 0; socket < 2; socket++) {
		// Folded into the scale rather than shifted off the integer first. Shifting
		// first quantised the signal to steps of `scale` DAC counts -- at the tuned
		// level that is a step of about 6 counts in a working range of 12000, so
		// roughly 11 bits of the converter's resolution instead of 13.5. Audible as
		// grit on quiet material, which is exactly where it hurts most.
		const float scale = cvFeedScale[socket] * (1.0f / 32768.0f);
		const int32_t* const source = cvSourceMono[socket];
		const bool valid = cvSourceValid[socket];
		float position = startPos;
		uint32_t frame = cvStreamWriteFrame;

		for (int32_t i = 0; i < toEmit; i++) {
			float in = 0.0f;
			if (valid) {
				uint32_t index = (uint32_t)position;
				if (index >= numSamples) {
					index = numSamples - 1;
				}
				// Interpolated rather than nearest-neighbour. 44.1 kHz into a stream
				// running near 47 kHz is a ratio no whole number of samples fits, so
				// picking the nearest one holds each value for an irregular length of
				// time. That irregularity is a modulation of the signal, and it lands
				// as inharmonic grit right across the band -- much louder than the
				// converter's own noise. The first output of a window interpolates
				// from the last sample of the previous one, hence cvPrevSample.
				const float frac = position - (float)index;
				const float a = (float)((index == 0) ? cvPrevSample[socket] : source[index - 1]);
				const float b = (float)source[index];
				in = (a + (b - a) * frac) * scale;
			}
			position += step;

			const float out =
			    kCvStreamEqScale * (in - kCvEqPole * cvFeedLastIn[socket]) + kCvStreamEqLimit * cvFeedLastOut[socket];
			cvFeedLastIn[socket] = in;
			cvFeedLastOut[socket] = out;

			cvStreamBuffer[frame * 2 + socket] = cvWord(socket, (int32_t)out);
			frame = (frame + 1) & (kCvFramesPerChannel - 1);
		}

		// Carried so the next window can interpolate across the join rather than
		// stepping. Cleared when a socket falls silent, so a stale sample can't
		// reappear when it comes back.
		cvPrevSample[socket] = valid ? source[numSamples - 1] : 0;
	}

	cvStreamWriteFrame = (cvStreamWriteFrame + (uint32_t)toEmit) & (kCvFramesPerChannel - 1);
	cvSourceValid[0] = cvSourceValid[1] = false;

	v7_dma_flush_range((uint32_t)cvStreamBuffer, (uint32_t)cvStreamBuffer + sizeof(cvStreamBuffer));
}

void cvStreamStart() {
	// The DAC shares this SPI channel with the display on OLED models, where boot
	// hands the chip-select to software and screen data is already using the bus.
	// Nothing here would work, so don't start.
	if (deluge::hid::display::have_oled_screen) {
		return;
	}

	for (uint32_t frame = 0; frame < kCvFramesPerChannel; frame++) {
		cvStreamBuffer[frame * 2] = cvWord(0, 0);
		cvStreamBuffer[frame * 2 + 1] = cvWord(1, 0);
	}
	v7_dma_flush_range((uint32_t)cvStreamBuffer, (uint32_t)cvStreamBuffer + sizeof(cvStreamBuffer));

	// Boot already sets 32-bit frames, master mode, transmit requests and the
	// hardware chip-select. Change only the clock -- twice as fast as for one
	// channel, since every sample now costs two frames.
	RSPI(SPI_CHANNEL_CV).SPCR &= ~(1 << 6);
	RSPI(SPI_CHANNEL_CV).SPBR = 9;         // ~3.3 MHz -> ~47 kHz per socket
	RSPI(SPI_CHANNEL_CV).SPCR |= (1 << 1); // transmit only
	RSPI(SPI_CHANNEL_CV).SPCR |= (1 << 6);

	initDMAWithLinkDescriptor(CV_STREAM_DMA_CHANNEL, cvStreamDmaLinkDescriptor, DMARS_FOR_RSPI_TX);
	dmaChannelStart(CV_STREAM_DMA_CHANNEL);

	// Start already at the target lead rather than at zero. The buffer was just filled with
	// the centre value, so the DMA reads silence until the writer catches up -- and the loop
	// begins with no error to correct instead of a full-scale one.
	//
	// Starting at zero meant every stream start was an acquisition transient that drove the
	// ratio hard onto one clamp and overshot onto the other. That is inaudible in itself,
	// but it is also what made two rounds of diagnostic readings meaningless, because the
	// extremes recorded the transient rather than the running state.
	cvStreamWriteFrame = (uint32_t)kCvTargetLead & (kCvFramesPerChannel - 1);
	cvFeedLastIn[0] = cvFeedLastIn[1] = 0.0f;
	cvFeedLastOut[0] = cvFeedLastOut[1] = 0.0f;
	cvPrevSample[0] = cvPrevSample[1] = 0;
	cvRate = kCvNominalRate;
	cvLeadAvg = 0.0f;
	cvResamplePos = 0.0f;
	cvStreamRunning = true;
}

void cvStreamStop() {
	// Stop the transfer engine before touching the SPI block, or it keeps writing
	// into a register we are reconfiguring.
	DMACn(CV_STREAM_DMA_CHANNEL).CHCTRL_n |= DMAC0_CHCTRL_n_CLREN;

	// Put the SPI back exactly as boot left it, rather than clearing it: the CV
	// sockets go back to being note-voltage outputs when nothing is routed here,
	// and that path uses this same channel.
	RSPI(SPI_CHANNEL_CV).SPCR &= ~(1 << 6); // disable while reconfiguring
	RSPI(SPI_CHANNEL_CV).SPBR = 1;          // boot's rate for the 30 MHz request
	RSPI(SPI_CHANNEL_CV).SPCR &= ~(1 << 1); // full duplex again, not transmit-only
	RSPI(SPI_CHANNEL_CV).SPCR |= (1 << 6);  // re-enable

	cvStreamRunning = false;
}
