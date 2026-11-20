// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#pragma once

#include <algorithm>
#include <array>

namespace revolverator {

using DrillGlow=std::array<float,4>;

class DrillTemperature final {
public:
    void advance(float seconds,bool spinning){
        if(spinning)return;
        pending_=0.f;
        const float cooling_time=std::max(0.f,seconds-inertia_);
        inertia_=std::max(0.f,inertia_-seconds);
        retained_=std::max(0.f,retained_-cooling_time*cooling_rate_);
        if(retained_<.00001f){retained_=0.f;overheated_=false;}
    }

    void spin(float seconds,float charge_duration,float auto_release){
        if(!(charge_duration>0.f)||!(auto_release>0.f)){pending_=0.f;return;}
        // At room temperature only the final part of the automatic spin
        // starts to heat the tip. A warm drill builds heat during the next charge.
        const float warmup=std::min(auto_release,.65f);
        pending_=retained_>0.f
            ?std::clamp(seconds/charge_duration,0.f,1.f)
            :std::clamp((seconds-(auto_release-warmup))/warmup,0.f,1.f);
    }

    bool thrown(){
        if(pending_<=0.f)return false;
        retained_=std::min(maximum_heat,retained_+pending_);
        pending_=0.f;
        // The metal does not lose a charge during a prompt native regrip.
        inertia_=.85f;
        cooling_rate_=retained_/5.f;
        if(retained_<maximum_heat||overheated_)return false;
        overheated_=true;
        return true;
    }

    DrillGlow glow()const{
        DrillGlow result{};
        for(unsigned i=0;i<result.size();++i){
            const float level=std::clamp(retained_+pending_-float(i),0.f,1.f);
            result[i]=level*level*(3.f-2.f*level);
        }
        return result;
    }

    void abort_spin(){pending_=0.f;}
    void reset(){*this={};}
private:
    static constexpr float maximum_heat=float(DrillGlow{}.size());
    float retained_{},pending_{},inertia_{},cooling_rate_{};
    bool overheated_{};
};

}
