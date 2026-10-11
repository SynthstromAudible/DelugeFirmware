#include <cassert>
#include <cmath>
#include <string>
#include <vector>
#include "definitions_cxx.hpp"
#include "dsp/reverb/reverb.hpp"
struct Probe : deluge::dsp::reverb::Digital {
 float first()const{return lp_decay_1_;}
 float second()const{return lp_decay_2_;}
};
int main(int argc,char** argv){
 assert(argc==2);constexpr int n=44100;
 std::vector<int32_t> in(n);in[0]=1000000;
 std::vector<StereoSample> a(n),b(n),c(n);Probe x{},y{},z{};
 for(auto p:{&x,&y,&z}){p->setLPF(1);p->setDamping(.5f);p->setRoomSize(.8f);p->setPanLevels(1<<27,1<<27);}
 x.setWidth(0);y.setWidth(1);z.setWidth(.5f);x.process(in,a);y.process(in,b);z.process(in,c);
 if(std::string(argv[1])=="damping"){
  assert(x.first()!=0&&x.second()!=0&&std::isfinite(x.first())&&std::isfinite(x.second()));
 }else{
  int different=0,stereo=0;double halfEnergy=0,fullEnergy=0;
  for(int i=0;i<n;++i){assert(a[i].l==a[i].r);different+=a[i].l!=b[i].l||a[i].r!=b[i].r;stereo+=b[i].l!=b[i].r;
   double half=double(c[i].l)-c[i].r,full=double(b[i].l)-b[i].r;halfEnergy+=half*half;fullEnergy+=full*full;}
  assert(different>0&&stereo>0&&std::abs(halfEnergy/fullEnergy-.25)<.001);
 }
}
