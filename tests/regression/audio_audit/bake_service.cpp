#include "sequencer/mdd/bake.hpp"
#include <cassert>
#include <tuple>
#include <vector>
#include <iostream>
using Event=std::tuple<uint32_t,uint32_t,uint8_t,uint8_t>;
int main() {
 snake::Core core; core.lanes[0].ticks=1;
 std::vector<Event> plain, serviced;
 auto capture=[](auto& out) { return [&](uint32_t p,uint32_t d,const snake::Frame& f) { out.emplace_back(p,d,f.note,f.velocity); return true; }; };
 assert(snake::bake(core,1000,capture(plain)));
 unsigned calls=0;
 assert(snake::bake(core,1000,capture(serviced),[&]{++calls;}));
 assert(plain==serviced && calls==32);
 core.lanes[0].muted=65535;
 unsigned emits=0; calls=0;
 assert(!snake::bake(core,UINT32_MAX,[&](auto,auto,const auto&){++emits;return true;},[&]{++calls;}));
 assert(emits==0 && calls==32768);
 std::cout << "PASS WRITE servicing preserves events; fully muted enormous clip has bounded work\n";
}
