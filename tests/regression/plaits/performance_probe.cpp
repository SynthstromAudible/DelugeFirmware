#include "dsp/plaits/voice.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <vector>
using deluge::dsp::PlaitsVoice;
volatile float sink;
int main(int argc, char** argv) {
	std::ofstream samples;
	if (argc > 1)
		samples.open(argv[1], std::ios::binary);
	for (unsigned model = 0; model < 3; ++model) {
		PlaitsVoice reference(model);
		float b[24];
		for (int j = 0; j < 2000; ++j) {
			reference.render(uint32_t((110.0 + (j % 11) * 29) * 4294967296.0 / 44100.0), float(j % 19) / 18,
			                 float(j % 17) / 16, float(j % 13) / 12, b, 24);
			if (samples)
				samples.write(reinterpret_cast<char*>(b), sizeof b);
		}
		for (bool moving : {false, true}) {
			std::vector<std::unique_ptr<PlaitsVoice>> voices;
			for (int i = 0; i < 8; ++i)
				voices.push_back(std::make_unique<PlaitsVoice>(model));
			auto start = std::chrono::steady_clock::now();
			double energy = 0;
			for (int j = 0; j < 20000; ++j)
				for (int v = 0; v < 8; ++v) {
					const float h = moving ? float(j % 101) / 100 : .5f;
					voices[v]->render(uint32_t((110. + v * .2) * 4294967296.0 / 44100.), h, .6f, .5f, b, 24);
					energy += b[j % 24] * b[j % 24];
				}
			sink = energy;
			auto ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
			printf("model=%u controls=%s ns/sample=%.3f energy=%.4f\n", model + 1, moving ? "moving" : "static",
			       ns / (20000. * 8 * 24), energy);
		}
	}
}
