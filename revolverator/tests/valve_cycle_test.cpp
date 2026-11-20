// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#include "valve_cycle.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace revolverator;
namespace {
void require(bool condition,const char* message){
    if(!condition){std::fprintf(stderr,"valve cycle: %s\n",message);std::exit(1);}
}
}

int main(){
    for(const unsigned hz:{50u,60u}){
        ValveCycle valve;
        float minimum=2.f,maximum=1.f;
        unsigned idle_puffs=0,loaded_puffs=0;
        for(unsigned i=0;i<5*hz;++i){
            const auto frame=valve.update(1.f/float(hz),1.f,true,false);
            minimum=std::min(minimum,frame.scale);
            maximum=std::max(maximum,frame.scale);
            idle_puffs+=frame.puffs;
            require(frame.load==0.f,"idle smoke retains the cool palette");
        }
        require(minimum<1.001f&&maximum>1.05f&&maximum<1.056f,"idle valve has a gentle repeating pulse");
        require(idle_puffs>=7&&idle_puffs<=8,"idle emits only a thin smoke stream");
        for(unsigned i=0;i<5*hz;++i){
            const auto frame=valve.update(1.f/float(hz),42.f,true,false);
            loaded_puffs+=frame.puffs;
            require(frame.scale>=1.f&&frame.scale<=1.0851f,"captured-enemy pulse is bounded");
        }
        require(loaded_puffs>=70&&loaded_puffs<=75,"the loaded drill emits denser smoke");
        const auto moving=valve.update(1.f/float(hz),42.f,true,false);
        const auto paused=valve.update(.08f,0,false,true);
        require(paused.scale==moving.scale&&paused.load==moving.load&&!paused.puffs,"pause freezes the pose and emission");
        const auto stopped=valve.update(1.f/float(hz),0.f,false,false);
        require(stopped.scale==1.f&&!stopped.load&&!stopped.puffs,"unequipping or stopping resets the valve");
        const auto invalid=valve.update(std::numeric_limits<float>::quiet_NaN(),42.f,true,false);
        require(invalid.scale==1.f&&!invalid.puffs,"invalid frame time does not advance the effect");
    }
}
