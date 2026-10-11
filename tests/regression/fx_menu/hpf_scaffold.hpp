#include <cassert>
#include <iostream>
#include "definitions_cxx.hpp"
#include "dsp/reverb/reverb.hpp"
struct Song{deluge::dsp::Reverb::Model model=deluge::dsp::Reverb::Model::MUTABLE;float reverbRoomSize=.5f,reverbLPF=1,reverbDamp=.5f,reverbWidth=.5f,reverbHPF=.2f;int reverbPan=0,reverbSidechainVolume=0,reverbSidechainShape=0,reverbSidechainAttack=0,reverbSidechainRelease=0,reverbSidechainSync=0;};
deluge::dsp::Reverb reverb;
int reverbPan,reverbSidechainVolume,reverbSidechainShape;
struct {int attack,release,syncLevel;}reverbSidechain;
