// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#include "native_contact_sparks.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
using namespace revolverator;
namespace {
unsigned checks;
void check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"sparks: %s\n",why);std::exit(1);}}
const Target robot{0x20000,0x30000,459,7};
struct Fixture;
Fixture* live;
struct Fixture {
 static constexpr unsigned pool=0x100000, pool_addr=0x15000, next_addr=0x15004, count_addr=0x15008;
 static constexpr unsigned native_color=0x78404052, golden_color=0x7800ff7f;
 std::vector<unsigned char> memory=std::vector<unsigned char>(0x2000000);
 openrac_api api{};unsigned pushes{},pops{},calls{};bool missing{},allocation_failure{};
 bool pal{};unsigned pool_reads{},extra_allocations{},allocation_step{1},trailing_alpha{0x78};
 std::vector<std::array<float,4>> positions,velocities;std::string symbol;
 template<class T>T read(unsigned at)const{T value{};std::memcpy(&value,memory.data()+at,sizeof value);return value;}
 template<class T>void write(unsigned at,T value){std::memcpy(memory.data()+at,&value,sizeof value);}
 static unsigned particle(unsigned slot){return pool+slot*0x40;}
 void fill_streak(unsigned slot){
  const auto at=particle(slot);
  write<std::uint8_t>(at,19);
  write(at+4,native_color);
  write(at+12,(trailing_alpha<<24)|(native_color&0xffffffu));
 }
 static void spawn(unsigned p,unsigned v){
  check((p&15u)==0&&(v&15u)==0,"native vectors are guest aligned16");
  std::array<float,4> position{},velocity{};
  std::memcpy(position.data(),live->memory.data()+p,16);std::memcpy(velocity.data(),live->memory.data()+v,16);
  live->positions.push_back(position);live->velocities.push_back(velocity);++live->calls;
  const auto slot=live->read<unsigned>(next_addr);
  if(slot>=2048)return;
  live->fill_streak(slot);
  live->write(next_addr,slot+live->allocation_step);
  live->write(count_addr,live->read<unsigned>(count_addr)+1+live->extra_allocations);
 }
 explicit Fixture(bool use_pal):pal(use_pal){live=this;api.memory=memory.data();api.game="rac1";api.version=pal?"pal":"ntsc";
  write(pool_addr,pool);write(next_addr,4u);write(count_addr,1u);fill_streak(3);
  api.function=+[](const char* name)->void*{live->symbol=name;return live->missing?nullptr:reinterpret_cast<void*>(&spawn);};
  api.relocate_data=+[](unsigned address)->unsigned{
   ++live->pool_reads;
   const unsigned base=live->pal?0x1601ac:0x1600ac;
   switch(address-base){
   case 0:return pool_addr;
   case 4:return next_addr;
   case 12:return count_addr;
   default:check(false,"particle globals use the canonical address for the current disc");return 0;
   }
  };
  api.stack_push=+[](unsigned size,unsigned* saved)->unsigned{check(size==32,"only two copied vectors are reserved");*saved=0x1e10000;++live->pushes;return live->allocation_failure?0:0x1e00000;};
  api.stack_pop=+[](unsigned saved){check(saved==0x1e10000,"exact stack cursor restored");++live->pops;};
 }
 Frame frame()const{Frame f;f.playing=f.equipped=f.alive=true;return f;}
};
}
int main(){
 for(float fps:{30.f,50.f,60.f,120.f})for(float speed:{24.f,42.f,1000.f}){
  ContactSparks rate;unsigned total=0;
  for(unsigned i=0;i<unsigned(fps*3);++i){const auto count=rate.update(1.f/fps,true,false,robot,speed);total+=count;check(count<=4,"per-frame budget is bounded");}
  const unsigned expected=speed==24?72:126;check(total>=expected&&total<=expected+1,"emission rate follows elapsed time and has a speed cap");
  for(unsigned i=0;i<100;++i)check(rate.update(1.f/fps,true,true,robot,speed)==0,"pause never emits or accumulates debt");
  check(rate.update(1.f/fps,true,false,robot,speed)<=2,"resume has no catch-up burst");
  check(rate.update(1.f/fps,false,false,{},speed)==0,"release ends the contact effect");
 }
 for(float bad:{0.f,-1.f,.11f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
  ContactSparks rate;check(rate.update(bad,true,false,robot,42)==0,"invalid timing is rejected");
 }
 {ContactSparks rate;for(float speed:{0.f,.00001f,.99f,1.f})
  check(rate.update(1.f/60.f,true,false,robot,speed)==0,"initial stationary contact and sub-pixel angle noise cannot create sparks");}
 for(bool pal:{false,true}){
  Fixture f(pal);NativeContactSparks sparks(&f.api);auto frame=f.frame();const Vec3 point{10,20,30};
  check(f.symbol==(pal?"func_L00_0026D588":"FUN_L00_0026c6e8"),"exact regional native wrench effect is selected");
  for(Vec3 shaft:{Vec3{0,0,1},Vec3{1,0,0},Vec3{0,1,0},Vec3{.6f,0,.8f}}){
   for(unsigned i=0;i<60;++i)sparks.update(frame,robot,point,shaft,float(i)*.6f,36.f,false);
  }
  check(f.calls>100&&f.calls<170&&sparks.emitted()==f.calls,"one second per shaft emits a bounded native streak population");
  for(unsigned i=0;i<f.calls;++i){
   const auto& p=f.positions[i];const auto& v=f.velocities[i];
   check(std::abs(p[0]-10)<.013f&&std::abs(p[1]-20)<.013f&&std::abs(p[2]-30)<.013f,"all sparks originate within the drill contact point");
   const float speed=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
   check(speed>.06f&&speed<.12f&&p[3]==1&&v[3]==0,"finite short native streak vectors are bounded");
  }
  const auto before=f.calls;frame.paused=true;for(unsigned i=0;i<120;++i)sparks.update(frame,robot,point,{0,0,1},0,42,false);
  check(f.calls==before,"paused gameplay creates no native particles");frame.paused=false;
  for(unsigned reason=0;reason<5;++reason){auto invalid=frame;if(reason==0)invalid.equipped=false;if(reason==1)invalid.alive=false;if(reason==2)invalid.playing=false;
   sparks.update(invalid,reason==3?Target{}:robot,point,reason==4?Vec3{}:Vec3{0,0,1},0,42,false);
  }
  check(f.calls==before,"unequip, death, loading, missing contact and invalid shaft stop emission");
  check(f.pushes==f.pops,"native stack reservations cannot leak across frames");
  f.allocation_failure=true;sparks.update(frame,robot,point,{0,0,1},0,42,false);check(f.calls==before,"particle stack allocation failure is cosmetic only");
  check(f.pool_reads==0,"base contact sparks do not inspect the native particle pool");
 }
 for(bool pal:{false,true}){
  Fixture f(pal);NativeContactSparks sparks(&f.api);
  auto emit=[&](bool golden){
   sparks.reset();
   check(sparks.update(f.frame(),robot,{1,2,3},{0,0,1},0,42,golden)==1,"fresh contact attempts one native emission");
  };
  auto color_is=[&](unsigned slot,unsigned color){
   const auto at=Fixture::particle(slot);
   return f.read<unsigned>(at+4)==color&&f.read<unsigned>(at+12)==color;
  };
  f.allocation_step=7;
  f.fill_streak(11);
  emit(true);
  check(color_is(4,Fixture::golden_color),"gold tint sets both endpoints to red 127, green 255, blue 0 and preserves alpha 120");
  check(color_is(3,Fixture::native_color)&&color_is(11,Fixture::native_color),"only the allocated slot is tinted even when the allocator cursor jumps");
  f.allocation_step=1;
  emit(false);
  check(color_is(11,Fixture::native_color)&&color_is(4,Fixture::golden_color),"switching to base affects only subsequent emissions");
  f.extra_allocations=1;
  emit(true);
  check(color_is(12,Fixture::native_color),"an unexpected change in the live count prevents recoloring");
  f.extra_allocations=0;
  f.write(Fixture::pool_addr,0x1fffff0u);
  emit(true);
  check(color_is(13,Fixture::native_color),"an invalid pool range prevents recoloring");
  f.write(Fixture::pool_addr,Fixture::pool);
  f.write(Fixture::next_addr,2047u);
  f.write(Fixture::count_addr,2047u);
  f.trailing_alpha=0x65;
  emit(true);
  const auto last=Fixture::particle(2047);
  check(f.read<unsigned>(Fixture::next_addr)==2048u&&f.read<unsigned>(Fixture::count_addr)==2048u,
     "the final allocation advances the cursor to the full-pool sentinel");
  check(f.read<unsigned>(last+4)==Fixture::golden_color&&f.read<unsigned>(last+12)==0x6500ff7fu,
     "the last free slot is tinted with each endpoint's own alpha");
  const auto before=f.calls;
  emit(true);
  check(f.calls==before+1&&color_is(13,Fixture::native_color)&&color_is(4,Fixture::golden_color)&&
     f.read<unsigned>(last+12)==0x6500ff7fu,"pool exhaustion leaves earlier particles alone");
 }
 {Fixture f(true);f.missing=true;NativeContactSparks sparks(&f.api);check(!sparks.available()&&sparks.update(f.frame(),robot,{1,2,3},{0,0,1},0,42,true)==0,"missing cosmetic primitive cannot break weapon functionality");}
 std::printf("contact sparks: %u timing, anchor, regional binding and lifecycle checks passed\n",checks);
}
