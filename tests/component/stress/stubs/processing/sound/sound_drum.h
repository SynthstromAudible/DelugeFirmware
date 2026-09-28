// A stand-in for the firmware's SoundDrum in the TSan stress build: just the rendering-list link and a magic the
// walker checks. The product RenderingDrums (model/instrument/rendering_drums.cpp) compiles against it unchanged; only
// what it uses of SoundDrum -- that it derives from RenderingDrumsLink -- is modelled.
#pragma once
#include "model/instrument/rendering_drums_link.h"
#include <cstdint>

class SoundDrum : public RenderingDrumsLink {
public:
	static constexpr uint32_t kMagic = 0xD8D8'0001u;
	uint32_t magic = kMagic;
};
