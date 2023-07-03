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

#ifndef MASTERCOMPRESSOR_H_
#define MASTERCOMPRESSOR_H_


#include "RZA1/system/r_typedefs.h"
#include "dsp/master_compressor/simpleSource/SimpleComp.h"

class StereoSample;

class MasterCompressor {
public:
	MasterCompressor();
	void render(StereoSample* buffer, uint16_t numSamples);
	double makeup;
	double gr;
	double wet;
	inline void setMakeup(double dB){ makeup=   pow(10.0,(dB/20.0)); if(fabs(1.0-makeup)<0.0001)makeup=1.0; if(makeup>20.0)makeup=20.0; if(makeup<0.0001)makeup=0.0; }
	inline double getMakeup(){ return   20.0 * log10(makeup) ;}

	chunkware_simple::SimpleComp compressor;

};

#endif /* MASTERCOMPRESSOR_H_ */
