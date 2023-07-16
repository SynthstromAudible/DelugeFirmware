/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "RZA1/system/r_typedefs.h"

#define INLINE inline
#include <algorithm> // for min(), max()
#include <cassert>   // for assert()
#include <cmath>

class StereoSample;

namespace chunkware_simple {
/*
 *	© 2006, ChunkWare Music Software, OPEN-SOURCE
 *
 *	Permission is hereby granted, free of charge, to any person obtaining a
 *	copy of this software and associated documentation files (the "Software"),
 *	to deal in the Software without restriction, including without limitation
 *	the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *	and/or sell copies of the Software, and to permit persons to whom the
 *	Software is furnished to do so, subject to the following conditions:
 *
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.
 *
 *	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *	THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *	DEALINGS IN THE SOFTWARE.
 */
//-------------------------------------------------------------
// gain functions
//-------------------------------------------------------------
// linear -> dB conversion
static INLINE double lin2dB(double lin) {
	static const double LOG_2_DB = 8.6858896380650365530225783783321; // 20 / ln( 10 )
	return log(lin) * LOG_2_DB;
}
// dB -> linear conversion
static INLINE double dB2lin(double dB) {
	static const double DB_2_LOG = 0.11512925464970228420089957273422; // ln( 10 ) / 20
	return exp(dB * DB_2_LOG);
}

static const double DC_OFFSET = 1.0E-25;
//-------------------------------------------------------------
// envelope detector
//-------------------------------------------------------------
class EnvelopeDetector {
public:
	EnvelopeDetector(double ms = 1.0, double sampleRate = 44100.0);
	virtual ~EnvelopeDetector() {}

	// time constant
	virtual void setTc(double ms);
	virtual double getTc(void) const { return timeConstant_; }

	// sample rate
	virtual void setSampleRate(double sampleRate);
	virtual double getSampleRate(void) const { return sampleRate_; }

	// runtime function
	INLINE void run(double in, double& state) { state = in + nSamplesInverse_ * (state - in); }

protected:
	double sampleRate_;         // sample rate
	double timeConstant_;       // time constant in ms
	double nSamplesInverse_;    // runtime coefficient
	virtual void setCoef(void); // coef calculation

}; // end SimpleComp class

//-------------------------------------------------------------
// attack/release envelope
//-------------------------------------------------------------
class AttRelEnvelope {
public:
	AttRelEnvelope(double att_ms = 10.0, double rel_ms = 100.0, double sampleRate = 44100.0);
	virtual ~AttRelEnvelope() {}

	// attack time constant
	virtual void setAttack(double ms);
	virtual double getAttack(void) const { return attackEnvelope_.getTc(); }

	// release time constant
	virtual void setRelease(double ms);
	virtual double getRelease(void) const { return releaseEnvelope_.getTc(); }

	// sample rate dependencies
	virtual void setSampleRate(double sampleRate);
	virtual double getSampleRate(void) const { return attackEnvelope_.getSampleRate(); }

	// runtime function
	INLINE void run(double in, double& state) {

		/* assumes that:
			* positive delta = attack
			* negative delta = release
			* good for linear & log values
			*/

		if (in > state)
			attackEnvelope_.run(in, state); // attack
		else
			releaseEnvelope_.run(in, state); // release
	}

private:
	EnvelopeDetector attackEnvelope_;
	EnvelopeDetector releaseEnvelope_;

}; // end AttRelEnvelope class

class SimpleComp : public AttRelEnvelope {
public:
	SimpleComp();
	virtual ~SimpleComp() {}

	// parameters
	virtual void setThresh(double dB);
	virtual void setRatio(double dB);

	virtual double getThresh(void) const { return threshdB_; }
	virtual double getRatio(void) const { return ratio_; }

	// runtime
	virtual void initRuntime(void); // call before runtime (in resume())
	//void process(double& in1, double& in2);                   // compressor runtime process
	//void process(double& in1, double& in2, double keyLinked); // with stereo-linked key in

	INLINE void process(double& in1, double& in2) {
		// create sidechain

		double rect1 = fabs(in1); // rectify input
		double rect2 = fabs(in2);

		/* if desired, one could use another EnvelopeDetector to smooth
			 * the rectified signal.
			 */

		double link = std::max(rect1, rect2); // link channels with greater of 2

		process(in1, in2, link); // rest of process
	}

	//-------------------------------------------------------------
	INLINE void process(double& in1, double& in2, double keyLinked) {
		keyLinked = fabs(keyLinked); // rectify (just in case)

		// convert key to dB
		keyLinked += DC_OFFSET;           // add DC offset to avoid log( 0 )
		double keydB = lin2dB(keyLinked); // convert linear -> dB

		// threshold
		double overdB = keydB - threshdB_; // delta over threshold
		if (overdB < 0.0)
			overdB = 0.0;

		// attack/release

		overdB += DC_OFFSET;                 // add DC offset to avoid denormal
		AttRelEnvelope::run(overdB, envdB_); // run attack/release envelope
		overdB = envdB_ - DC_OFFSET;         // subtract DC offset

		/* REGARDING THE DC OFFSET: In this case, since the offset is added before
			 * the attack/release processes, the envelope will never fall below the offset,
			 * thereby avoiding denormals. However, to prevent the offset from causing
			 * constant gain reduction, we must subtract it from the envelope, yielding
			 * a minimum value of 0dB.
			 */

		// transfer function
		double gr = overdB * (ratio_ - 1.0); // gain reduction (dB)
		gr = dB2lin(gr);                     // convert dB -> linear

		// output gain
		in1 *= gr; // apply gain reduction to input
		in2 *= gr;
	}

private:
	// transfer function
	double threshdB_; // threshold (dB)
	double ratio_;    // ratio (compression: < 1 ; expansion: > 1)

	// runtime variables
	double envdB_; // over-threshold envelope (dB)

}; // end SimpleComp class

} // end namespace chunkware_simple

class MasterCompressor {
public:
	MasterCompressor();
	void render(StereoSample* buffer, uint16_t numSamples, int32_t masterVolumeAdjustmentL,
	            int32_t masterVolumeAdjustmentR);
	double makeup;
	double gr;
	double wet;
	inline void setMakeup(double dB) {
		makeup = pow(10.0, (dB / 20.0));
		if (fabs(1.0 - makeup) < 0.0001)
			makeup = 1.0;
		if (makeup > 20.0)
			makeup = 20.0;
		if (makeup < 0.0001)
			makeup = 0.0;
	}
	inline double getMakeup() { return 20.0 * log10(makeup); }

	chunkware_simple::SimpleComp compressor;
};
