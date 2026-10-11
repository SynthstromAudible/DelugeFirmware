#include "sequencer/mdd/controller.hpp"
#include "sequencer/mdd/sample_clock.hpp"
#include <cassert>
#include <iostream>
using namespace snake;
struct Sink {int on=0,off=0,mods=0;void noteOn(uint8_t,uint8_t){++on;}void noteOff(uint8_t){++off;}void custom(int,uint16_t){++mods;}};
int main(){
 SampleClock clock;auto before=clock.milliseconds(0xfffffff0u);auto after=clock.milliseconds(20);assert(after>=before&&after-before<=1);assert(clock.milliseconds(44120)-after==1000);

 Controller ui;ui.down(Control::Gates);assert(ui.lane==1);ui.up();ui.choose(5);
 assert(ui.map(0,42,123));ui.down(Control::Notes);assert(!ui.map(0,42,123));ui.up();
 ui.gestures.down(3,0);ui.back();assert(ui.gestures.held==-1&&ui.held==Control::None);
 Player player;Sink sink;player.tick(0,sink);assert(sink.on==1&&sink.off==0);player.tick(12,sink);assert(sink.off==1);player.tick(24,sink);assert(sink.on==2);player.stop(sink);assert(sink.off==2&&!player.sounding);player.stop(sink);assert(sink.off==2);
 Core c;assert(c.advance(0).trigger);assert(!c.advance(1).trigger);
 Gestures g;g.select(c.lanes[0]);g.down(0,0);g.down(7,10);g.up(7,20);g.up(0,30);assert(c.lanes[0].length==8&&c.lanes[0].muted==0);
 for(auto& l:c.lanes){g.select(l);g.down(2,0);g.up(2,100);assert(l.muted&4);g.down(3,200);g.edit(1,600,65535);g.up(3,610);assert(!(l.muted&8));}
 for(int shape=0;shape<5;++shape){Lane l;l.shape=Shape(shape);uint16_t mask=0;for(int i=0;i<16;++i)mask|=1u<<l.cell(i*24);assert(mask==65535);for(int n=1;n<=16;++n)for(int d=0;d<4;++d){l.length=n;l.direction=Direction(d);for(int i=0;i<500;++i)assert(l.cell(i)<16);}}
 Lane l;l.values[0]=123;l.values[1]=456;assert(l.sample(0)==123);l.muted=2;assert(l.sample(24)==123);
 Lane a,b;a.randomize(10,20);b.randomize(10,20);assert(a.values==b.values);for(auto v:a.values)assert(v>=10&&v<=20);
 Core x; x.lanes[1].values.fill(0);assert(!x.advance(0).trigger);x.lanes[1].values.fill(65535);assert(x.advance(0).duration==24);x.lanes[3].values.fill(99);assert(x.advance(0).custom[0]==99&&x.advance(0).custom[1]==0);
 g.select(x.lanes[0]);g.down(0,0);g.up(0,500);assert(x.lanes[0].muted==0);g.down(1,600);g.select(x.lanes[1]);g.up(1,650);assert(x.lanes[1].muted==0);
 std::cout<<"PASS: paths, lengths, directions, gestures, mute, hold, random, gate, independent Custom, clip editor state\n";
}
