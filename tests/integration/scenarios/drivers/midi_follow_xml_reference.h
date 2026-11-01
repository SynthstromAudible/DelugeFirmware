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

// The MIDIFollow.XML the default mappings write, recorded from next d70387530 before MIDI follow named params by
// target: the file must stay byte for byte the same.
#pragma once

#include <string_view>

namespace midi_follow_xml_reference {

inline constexpr std::string_view kDefaults = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<defaults>
	<cc_mappings>
		<pitch>3</pitch>
		<portamento>5</portamento>
		<volumePostFX>7</volumePostFX>
		<pan>10</pan>
		<oscAPitch>12</oscAPitch>
		<oscBPitch>13</oscBPitch>
		<modulator1Pitch>14</modulator1Pitch>
		<modulator2Pitch>15</modulator2Pitch>
		<modFXRate>16</modFXRate>
		<modFXFeedback>17</modFXFeedback>
		<modFXOffset>18</modFXOffset>
		<waveFold>19</waveFold>
		<stutterRate>20</stutterRate>
		<oscAVolume>21</oscAVolume>
		<oscAPhaseWidth>23</oscAPhaseWidth>
		<carrier1Feedback>24</carrier1Feedback>
		<oscAWavetablePosition>25</oscAWavetablePosition>
		<oscBVolume>26</oscBVolume>
		<compressorThreshold>27</compressorThreshold>
		<oscBPhaseWidth>28</oscBPhaseWidth>
		<carrier2Feedback>29</carrier2Feedback>
		<oscBWavetablePosition>30</oscBWavetablePosition>
		<reverseProbability>36</reverseProbability>
		<spreadVelocity>37</spreadVelocity>
		<spreadOctave>39</spreadOctave>
		<spreadGate>40</spreadGate>
		<noiseVolume>41</noiseVolume>
		<rhythm>42</rhythm>
		<sequenceLength>43</sequenceLength>
		<chordPolyphony>44</chordPolyphony>
		<ratchetAmount>45</ratchetAmount>
		<noteProbability>46</noteProbability>
		<bassProbability>47</bassProbability>
		<chordProbability>48</chordProbability>
		<ratchetProbability>49</ratchetProbability>
		<arpGate>50</arpGate>
		<arpRate>51</arpRate>
		<delayFeedback>52</delayFeedback>
		<delayRate>53</delayRate>
		<modulator1Volume>54</modulator1Volume>
		<modulator1Feedback>55</modulator1Feedback>
		<modulator2Volume>56</modulator2Volume>
		<modulator2Feedback>57</modulator2Feedback>
		<lfo1Rate>58</lfo1Rate>
		<lfo2Rate>59</lfo2Rate>
		<compressorShape>60</compressorShape>
		<sidechainCompressorVolume>61</sidechainCompressorVolume>
		<bitcrushAmount>62</bitcrushAmount>
		<sampleRateReduction>63</sampleRateReduction>
		<lpfMorph>70</lpfMorph>
		<lpfResonance>71</lpfResonance>
		<env1Release>72</env1Release>
		<env1Attack>73</env1Attack>
		<lpfFrequency>74</lpfFrequency>
		<env1Decay>75</env1Decay>
		<env1Sustain>76</env1Sustain>
		<env2Attack>77</env2Attack>
		<env2Decay>78</env2Decay>
		<env2Sustain>79</env2Sustain>
		<env2Release>80</env2Release>
		<hpfFrequency>81</hpfFrequency>
		<hpfResonance>82</hpfResonance>
		<hpfMorph>83</hpfMorph>
		<bassFreq>84</bassFreq>
		<trebleFreq>85</trebleFreq>
		<bass>86</bass>
		<treble>87</treble>
		<reverbAmount>91</reverbAmount>
		<modFXDepth>93</modFXDepth>
		<env3Attack>102</env3Attack>
		<env3Decay>103</env3Decay>
		<env3Sustain>104</env3Sustain>
		<env3Release>105</env3Release>
		<env4Attack>106</env4Attack>
		<env4Decay>107</env4Decay>
		<env4Sustain>108</env4Sustain>
		<env4Release>109</env4Release>
		<lfo3Rate>110</lfo3Rate>
		<lfo4Rate>111</lfo4Rate>
		<swapProbability>112</swapProbability>
		<glideProbability>113</glideProbability>
	</cc_mappings>
	<settings>
		<channels>
			<a>
				<channel>1</channel>
			</a>
			<b>
				<channel>256</channel>
			</b>
			<c>
				<channel>256</channel>
			</c>
			<track_1>
				<channel>256</channel>
			</track_1>
			<track_2>
				<channel>256</channel>
			</track_2>
			<track_3>
				<channel>256</channel>
			</track_3>
			<track_4>
				<channel>256</channel>
			</track_4>
			<track_5>
				<channel>256</channel>
			</track_5>
			<track_6>
				<channel>256</channel>
			</track_6>
			<track_7>
				<channel>256</channel>
			</track_7>
			<track_8>
				<channel>256</channel>
			</track_8>
			<track_9>
				<channel>256</channel>
			</track_9>
			<track_10>
				<channel>256</channel>
			</track_10>
			<track_11>
				<channel>256</channel>
			</track_11>
			<track_12>
				<channel>256</channel>
			</track_12>
			<track_13>
				<channel>256</channel>
			</track_13>
			<track_14>
				<channel>256</channel>
			</track_14>
			<track_15>
				<channel>256</channel>
			</track_15>
			<track_16>
				<channel>256</channel>
			</track_16>
		</channels>
		<kit_root_note>
			<note>36</note>
		</kit_root_note>
		<feedback>
			<channel>none</channel>
			<automation>disabled</automation>
			<filter>disabled</filter>
		</feedback>
		<display_param>
			<popup>disabled</popup>
		</display_param>
	</settings>
</defaults>
)xml";

} // namespace midi_follow_xml_reference
