#include "dsp/plaits/voice.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
using deluge::dsp::PlaitsVoice;
uint32_t increment(float hz) {
	return static_cast<uint32_t>(hz * (4294967296.0 / 44100.0));
}
int main(int argc, char** argv) {
	for (unsigned model = 0; model < 3; ++model) {
		for (float hz : {8.18f, 55.f, 440.f, 10000.f, 20000.f})
			for (float h : {0.f, .5f, 1.f})
				for (float t : {0.f, .5f, 1.f})
					for (float m : {0.f, .5f, 1.f}) {
						PlaitsVoice v(model);
						for (int n : {1, 12, 24, 31, 128, 257}) {
							std::vector<float> b(n + 2, 1234.f);
							v.render(increment(hz), h, t, m, b.data() + 1, n);
							assert(b.front() == 1234.f && b.back() == 1234.f);
							for (int i = 1; i <= n; ++i)
								assert(std::isfinite(b[i]) && std::abs(b[i]) <= 1.f);
						}
					}
		PlaitsVoice a(model), b(model);
		float x[256], y[256];
		a.render(increment(110), .6f, .6f, .6f, x, 256);
		b.render(increment(110), .6f, .6f, .6f, y, 256);
		double energy = 0;
		for (int i = 0; i < 256; ++i) {
			assert(x[i] == y[i]);
			energy += x[i] * x[i];
		}
		assert(energy > 1e-4);
		a.render(0, 1, 1, 1, x, 256);
		for (float sample : x)
			assert(sample == 0);
		std::cout << "PASS model " << model << ": buffers, macro corners, reset determinism, non-silent output\n";
	}

	// Switch repeatedly inside an active voice, at boundaries unrelated to engine block size.
	PlaitsVoice switching(0);
	float block[67];
	for (unsigned step = 0; step < 1000; ++step) {
		switching.setModel(step % 3);
		assert(switching.model() == step % 3);
		switching.render(increment(110), .4f, .7f, .5f, block, 67);
		for (float sample : block)
			assert(std::isfinite(sample) && std::abs(sample) <= 1.f);
	}
	switching.setModel(99);
	assert(switching.model() == 2);
	// Re-selecting the current model must preserve its DSP history.
	PlaitsVoice unchanged(0), reference(0);
	float left[67], right[67];
	unchanged.render(increment(110), .4f, .7f, .5f, left, 67);
	reference.render(increment(110), .4f, .7f, .5f, right, 67);
	unchanged.setModel(0);
	unchanged.render(increment(110), .4f, .7f, .5f, left, 67);
	reference.render(increment(110), .4f, .7f, .5f, right, 67);
	for (unsigned i = 0; i < 67; ++i)
		assert(left[i] == right[i]);
	std::cout << "PASS 1000 live model switches, finite output and unchanged-model continuity\n";
	PlaitsVoice sine(0);
	std::vector<float> s(44100);
	sine.render(increment(440), 0, 0, .5f, s.data(), s.size());
	int crossings = 0;
	for (size_t i = 2206; i < s.size(); ++i)
		if (s[i - 1] <= 0 && s[i] > 0)
			++crossings;
	double measured = crossings / (double(s.size() - 2206) / 44100);
	assert(std::abs(measured - 440) < 2);
	std::cout << "PASS A4 pitch: " << measured << " Hz; host voice bytes " << sizeof(PlaitsVoice) << "\n";
	if (argc > 1) {
		std::ofstream out(argv[1], std::ios::binary);
		for (unsigned model = 0; model < 3; ++model)
			for (int beat = 0; beat < 16; ++beat) {
				constexpr int notes[] = {36, 36, 43, 36, 48, 36, 39, 43};
				float note = notes[beat % 8];
				PlaitsVoice v(model);
				std::vector<float> a(5512);
				v.render(increment(440 * std::exp2((note - 69) / 12)), .2f + beat * .04f, .65f, .4f, a.data(),
				         a.size());
				for (size_t i = 0; i < a.size(); ++i) {
					float env = std::min(1.f, i / 88.f) * std::exp(-float(i) / 1400.f);
					int16_t sample = a[i] * env * 16000;
					out.write(reinterpret_cast<char*>(&sample), 2);
				}
			}
	}
}
