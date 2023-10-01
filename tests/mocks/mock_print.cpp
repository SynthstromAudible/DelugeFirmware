#include "io/debug/print.h"
#include <iostream>
using namespace std;

namespace Debug {

MIDIDevice* midiDebugDevice = nullptr;

void println(char const* output) {
	cout << output;
}

void println(int32_t number) {
	cout << number;
}

void print(char const* output) {
	cout << output;
}

void print(int32_t number) {
	cout << number;
}

} // namespace Debug
