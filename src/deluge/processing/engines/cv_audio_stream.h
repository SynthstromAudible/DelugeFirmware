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

#pragma once

#include <cstdint>

namespace deluge::processing::engines {

/// An assignable auxiliary audio send bus on the two CV sockets.
///
/// The DAC feeding the CV sockets is a real 16-bit converter on a fast SPI bus, and its
/// chip-select sits on P6_1 -- the SPI block's own SSL00 pin, already configured as such at
/// boot. That lets the transfer engine feed it continuously with no processor involvement,
/// so the sockets can carry audio rather than only the occasional note voltage.

/// False on OLED models, where the DAC shares its SPI channel with the display and
/// this cannot work. Everything that acts on a Clip's sends checks this first, so
/// on an OLED model every Clip behaves as if it were routed to MAIN alone.
bool cvOutputsAvailable();

/// True while the CV sockets are being streamed to.
bool cvStreamIsRunning();

/// True while a playing Clip is routed with STEREO SPLIT on.
///
/// INTENDED BEHAVIOUR, not an oversight. STEREO SPLIT only has anything to split when
/// the engine is rendering in stereo, which it does only with headphones or the right
/// main output connected, or while internally recording. Rendering in mono is not just a
/// missing pan -- stereo samples have their channels combined and every voice costs about
/// half as much. So this must not be used to force stereo on: that would make a CV routing
/// choice change the main mix's level and its CPU cost, which is far worse than the split
/// quietly falling back to the mono sum on both sockets.
bool cvStreamNeedsStereo();

/// Hands one Clip's isolated audio to the sockets its sends name. A send above zero is what
/// "routed to that socket" means. `sourceId` identifies the Clip, so the correction filter
/// can be reset when a socket's set of sources changes.
///
/// The Clip is isolated here rather than by the caller: `post` is the interleaved mix after
/// the Clip rendered, `pre` the snapshot taken before it did, and the difference is taken
/// per sample inside the accumulate loop that was going to read them anyway.
void cvStreamCapture(uint32_t sourceId, const int32_t* post, const int32_t* pre, uint32_t numSamples,
                     const int32_t* sendGain, int32_t* lastSendGain);

/// Snapshot space for the mix as it stood before a Clip rendered.
int32_t* cvStreamCaptureScratch();

/// Converts everything captured this window and queues it for output.
void cvStreamPump(uint32_t numSamples);

/// Called once when the render loop has finished, so the full set of Clips feeding
/// each socket is known. Resets the correction filter where that set changed, and
/// starts or stops the stream to match -- nothing else has to.
void cvStreamRenderComplete();

/// Stereo split, GLOBAL rather than per Clip. It describes how the two cables are plugged
/// in -- one stereo destination, or two mono ones -- which is a property of the rig and not
/// of any clip. Per-Clip it permitted a state where one clip treated the pair as stereo
/// while another treated them as two mono outs, which is never what anyone meant.
bool cvGetStereoSplit();
void cvSetStereoSplit(bool on);

/// AUX MASTER, one per socket. Set once per render window from the song's two master params,
/// which is where the level now lives.
///
/// Displayed 0-50 like every other param on the machine, and 0 is a true mute rather than the
/// bottom of the taper: "quiet enough" is not silence, and a mute is what an off position is
/// for. 50 is x256; each step below it is a fixed 1.2 dB, so the steps sound evenly spaced the
/// whole way down.
void cvSetMasterFromParams(int32_t cv1Value, int32_t cv2Value);

/// Which socket the AUX MASTER gold knob is currently editing, 0 or 1. Only meaningful when
/// split is off; with split on there is one master and the knob always drives CV1's param.
/// Deliberately not saved anywhere -- it is where a knob is pointing, not a setting.
extern uint8_t cvMasterKnobSocket;

/// Menu display value, 0-50, for a raw master param value. The menu and the gold knob edit one
/// param between them, so this is only a rendering of it -- there is no second copy to drift.
int32_t cvMasterParamToDisplay(int32_t paramValue);
int32_t cvMasterDisplayToParam(int32_t display);
constexpr int32_t kCvMasterDisplayMax = 50;

/// Per-Clip send amount, Q16. Attenuation only, so this is the ceiling.
constexpr int32_t kCvSendGainUnity = 65536;

/// Convert a send param value into the Q16 gain the capture path wants. Squared, which is
/// how the Deluge treats every other volume-ish param -- a linear send feels dead until the
/// top of its travel.
int32_t cvSendParamToGain(int32_t paramValue);

} // namespace deluge::processing::engines
