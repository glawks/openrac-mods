// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#include "valve_cycle.hpp"
#include <algorithm>
#include <cmath>

namespace revolverator {
namespace {
constexpr float tau=6.2831853071795864769f;
// Keep the pump legible at idle and avoid high-frequency valve flicker while
// the drill head is visibly turning much faster.
constexpr float idle_pulses_per_second=.9f;
constexpr float loaded_pulses_per_second=2.45f;
constexpr float idle_puffs_per_second=1.6f;
constexpr float loaded_puffs_per_second=15.f;
}

ValveFrame ValveCycle::update(float seconds,float angular_speed,bool powered,bool paused) {
    if(paused)return {scale_,load_,0};
    if(!powered||!std::isfinite(angular_speed)||std::abs(angular_speed)<.3f){
        reset();
        return {};
    }
    if(!std::isfinite(seconds)||seconds<=0.f||seconds>.1f)return {scale_,load_,0};

    const auto speed=std::clamp((std::abs(angular_speed)-1.f)/41.f,0.f,1.f);
    const float demand=speed*speed*(3.f-2.f*speed);
    load_+=(demand-load_)*(1.f-std::exp(-seconds*(demand>load_?6.f:3.f)));
    const float frequency=idle_pulses_per_second+(loaded_pulses_per_second-idle_pulses_per_second)*load_;
    phase_=std::fmod(phase_+seconds*frequency*tau,tau);
    const float compression=.5f-.5f*std::cos(phase_);
    scale_=1.f+(.055f+.030f*load_)*compression*compression;

    // A fractional accumulator makes the same gentle stream at 50 and 60 Hz.
    smoke_budget_+=seconds*(idle_puffs_per_second+(loaded_puffs_per_second-idle_puffs_per_second)*load_);
    const auto puffs=static_cast<unsigned>(smoke_budget_);
    smoke_budget_-=float(puffs);
    return {scale_,load_,puffs};
}

void ValveCycle::reset() {
    phase_=load_=smoke_budget_=0.f;
    scale_=1.f;
}

}
