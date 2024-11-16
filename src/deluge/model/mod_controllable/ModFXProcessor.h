//
// Created by Mark Adams on 2024-11-15.
//

#ifndef DELUGE_MODFXPROCESSOR_H
#define DELUGE_MODFXPROCESSOR_H

#include "definitions_cxx.hpp"
#include "dsp/stereo_sample.h"
#include "modulation/lfo.h"
#include "util/containers.h"
class ModFXProcessor {

	void setupChorus(const ModFXType& modFXType, int32_t modFXDepth, int32_t* postFXVolume,
	                 UnpatchedParamSet* unpatchedParams, LFOType& modFXLFOWaveType, int32_t& modFXDelayOffset,
	                 int32_t& thisModFXDelayDepth) const;
	/// flanger, phaser, warble - generally any modulated delay tap based effect with feedback
	void setupModFXWFeedback(const ModFXType& modFXType, int32_t modFXDepth, int32_t* postFXVolume,
	                         UnpatchedParamSet* unpatchedParams, LFOType& modFXLFOWaveType, int32_t& modFXDelayOffset,
	                         int32_t& thisModFXDelayDepth, int32_t& feedback) const;
	// not grain!
	template <ModFXType modFXType>
	void processModFXBuffer(StereoSample* buffer, int32_t modFXRate, int32_t modFXDepth, const StereoSample* bufferEnd,
	                        LFOType& modFXLFOWaveType, int32_t modFXDelayOffset, int32_t thisModFXDelayDepth,
	                        int32_t feedback, bool stereo);
	template <ModFXType modFXType>
	void processModLFOs(int32_t modFXRate, LFOType& modFXLFOWaveType, int32_t& lfoOutput, int32_t& lfo2Output);
	template <ModFXType modFXType, bool stereo>
	void processOneModFXSample(int32_t modFXDelayOffset, int32_t thisModFXDelayDepth, int32_t feedback,
	                           StereoSample* currentSample, int32_t lfoOutput, int32_t lfo2Output);
	void processOnePhaserSample(int32_t modFXDepth, int32_t feedback, StereoSample* currentSample, int32_t lfoOutput);

public:
	ModFXProcessor() {
		// Mod FX
		ModFXProcessor::modFXBuffer = nullptr;
		ModFXProcessor::modFXBufferWriteIndex = 0;
		memset(allpassMemory, 0, sizeof(allpassMemory));
	}
	~ModFXProcessor() {
		// Free the mod fx memory
		if (modFXBuffer) {
			delugeDealloc(modFXBuffer);
		}
	}
	// Phaser
	StereoSample phaserMemory{0, 0};
	StereoSample allpassMemory[kNumAllpassFiltersPhaser];
	StereoSample* modFXBuffer{nullptr};
	uint16_t modFXBufferWriteIndex{0};
	LFO modFXLFO;
	LFO modFXLFOStereo;
	void processModFX(StereoSample* buffer, const ModFXType& modFXType, int32_t modFXRate, int32_t modFXDepth,
	                  int32_t* postFXVolume, UnpatchedParamSet* unpatchedParams, const StereoSample* bufferEnd,
	                  bool anySoundComingIn);
	void resetMemory();
	void setupBuffer();
	void disableBuffer();
	void tickLFO(int32_t numSamples, int32_t phaseIncrement) {
		// Do Mod FX
		modFXLFO.tick(numSamples, phaseIncrement);
	}
};
// helper functions for the mod fx types
namespace modfx {
deluge::vector<std::string_view> getModNames();

const char* getParamName(ModFXType type, ModFXParam param);

const char* modFXToString(ModFXType type);
} // namespace modfx

#endif // DELUGE_MODFXPROCESSOR_H
