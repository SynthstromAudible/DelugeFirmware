#include "io/debug/print.h"
#include <iostream>
using namespace std;

namespace Debug {

MIDIDevice* midiDebugDevice = nullptr;

void println(char const* output) {
	cout << output << endl;
}

void println(int32_t number) {
	cout << number << endl;
}

void print(char const* output) {
	cout << output << endl;
}

void print(int32_t number) {
	cout << number << endl;
}

} // namespace Debug
