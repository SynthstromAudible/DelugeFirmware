#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
template <class E>
void check() {
	for (float note : {24.f, 69.f, 108.f})
		for (float h : {0.f, .5f, 1.f})
			for (float t : {0.f, .5f, 1.f})
				for (float m : {0.f, .5f, 1.f}) {
					E full{}, main{};
					float mem1[24]{}, mem2[24]{};
					stmlib::BufferAllocator a(mem1, sizeof mem1), b(mem2, sizeof mem2);
					full.Init(&a);
					main.Init(&b);
					for (int n : {1, 12, 24})
						for (int j = 0; j < 20; ++j) {
							plaits::EngineParameters p{};
							p.note = note + j * .17f;
							p.harmonics = h;
							p.timbre = t;
							p.morph = m;
							p.trigger = plaits::TRIGGER_UNPATCHED;
							p.accent = 1;
							float x[26], y[26], aux[26];
							x[0] = x[n + 1] = y[0] = y[n + 1] = aux[0] = aux[n + 1] = 1234;
							bool env = false;
							full.Render(p, x + 1, aux + 1, n, &env);
							main.Render(p, y + 1, nullptr, n, &env);
							assert(x[0] == 1234 && x[n + 1] == 1234 && y[0] == 1234 && y[n + 1] == 1234
							       && aux[0] == 1234 && aux[n + 1] == 1234);
							for (int i = 1; i <= n; ++i) {
								assert(std::isfinite(y[i]));
								assert(x[i] == y[i]);
							}
						}
				}
}
int main() {
	check<plaits::FMEngine>();
	check<plaits::WaveshapingEngine>();
	check<plaits::VirtualAnalogEngine>();
	puts("PASS AUX-null primary output equality, parameter corners, dynamic pitch, guard samples");
}
