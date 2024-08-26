#include "io/debug/print.h"
#include <cstdio>
#include <iostream>
using namespace std;

namespace Debug {

MIDICable* midiDebugDevice = nullptr;

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

extern "C" void putchar_(char c) {
	putchar(c);
}
