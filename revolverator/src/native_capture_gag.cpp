// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#include "native_capture_gag.hpp"
#include "attachment.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace revolverator {
namespace {
constexpr int hero_dying_speed_limited = 0x80;
constexpr float revolution = 6.283185307f;

float ease(float progress) {
    const float t = std::clamp(progress, 0.f, 1.f);
    return t*t*(3.f-2.f*t);
}
}

NativeCaptureGag::NativeCaptureGag(const openrac_api* api, openrac_mod* mod,
    const CaptureGagProfile& profile)
    : Memory(api), profile_(profile), mod_(mod), smoke_(api),
      state_(reinterpret_cast<HeroState>(find_function("FUN_L00_002223f8"))),
      basis_(reinterpret_cast<Basis>(find_function("FUN_001fa030"))),
      rumble_(reinterpret_cast<Rumble>(find_function(profile.landing_rumble))),
      sound_(reinterpret_cast<PlayClassSound>(find_function("FUN_L00_0028dc90"))),
      trace_(api->setting_bool(mod,"trace")!=0) {}

NativeCaptureGag::~NativeCaptureGag() { reset(); }

bool NativeCaptureGag::same_actor(Target actor) const {
    const unsigned at = actor.address;
    return api_->loaded_level()==profile_.level && actor && valid(at,256) &&
        read<unsigned>(at+0x78)==actor.pvars && read<unsigned short>(at+0xa6)==actor.klass &&
        read<unsigned>(at+0x38)==actor.stamp && read<unsigned char>(at+0x20)<0x80;
}

void NativeCaptureGag::observe(Target actor) {
    if(actor.klass==profile_.enemy_class && same_actor(actor)) seen_=actor;
}

Target NativeCaptureGag::candidate(const Frame& frame) const {
    if(active() || !state_ || !basis_ || !frame.playing || !frame.equipped ||
       !frame.alive || !frame.grounded || !finite(frame.position) || !std::isfinite(frame.yaw) ||
       !same_actor(seen_)) return {};
    const auto state = read<unsigned char>(seen_.address+0x20);
    if(state<profile_.first_state || state>profile_.last_state ||
       !valid(seen_.pvars,profile_.health_offset+sizeof(float)) ||
       !(read<float>(seen_.pvars+profile_.health_offset)>0.f) ||
       !read<unsigned>(seen_.address+0x94)) return {};
    const auto delta = read<Vec3>(seen_.address+0x10)-frame.position;
    const float forward = delta.x*std::cos(frame.yaw)+delta.y*std::sin(frame.yaw);
    const float distance = std::hypot(delta.x,delta.y);
    return finite(delta) && std::abs(delta.z)<1.15f && distance<=profile_.reach &&
        forward>=distance*.25f ? seen_ : Target{};
}

bool NativeCaptureGag::start(const Frame& frame, Target hero, Target enemy) {
    if(!enemy || !(enemy==candidate(frame)) || !same_actor(hero) || hero.klass!=0) return false;
    origin_=read<Vec3>(enemy.address+0x10);
    angles_=read<Vec3>(enemy.address+0x40);
    const auto offset=read<Vec3>(enemy.address)*(1.f/1024.f)-origin_;
    if(!finite(origin_) || !finite(angles_) || !finite(offset) || length_squared(offset)>100.f)
        return false;
    const auto q=rotation_from_euler(angles_);
    local_bounds_=rotate({-q[0],-q[1],-q[2],q[3]},offset);
    enemy_=enemy;
    hero_=hero;
    const Vec3 forward{std::cos(frame.yaw),std::sin(frame.yaw),0};
    landing_=frame.position+forward*.55f;
    landing_.z=origin_.z;
    const auto to_hero=frame.position-origin_;
    turn_=profile_.turn_toward_hero && std::hypot(to_hero.x,to_hero.y)>.1f ?
        std::remainder(std::atan2(to_hero.y,to_hero.x)-angles_.z,revolution):0.f;
    collision_=read<unsigned>(enemy.address+0x94);
    write<unsigned>(enemy.address+0x94,0);
    if(trace_)api_->log(mod_,OPENRAC_LOG_INFO,"Snagglebeast gag: lift started");
    return true;
}

bool NativeCaptureGag::owns(unsigned actor) const {
    return actor==enemy_.address && same_actor(enemy_);
}

bool NativeCaptureGag::owns_hero(Target actor) const {
    return actor==hero_ && same_actor(hero_) && same_actor(enemy_);
}

void NativeCaptureGag::place(Target actor, Vec3 position, Vec3 angles) {
    const unsigned at=actor.address;
    write(at+0x10,position);
    write(at,(position+rotate(rotation_from_euler(angles),local_bounds_))*1024.f);
    write(at+0x40,angles);
    basis_(at+0xc0,at+0x40);
}

unsigned NativeCaptureGag::emit_impact_dust(float seconds_per_tick) const {
    const float scale=profile_.impact_dust_scale;
    if(scale<=0.f)return 0;
    constexpr unsigned ground_puffs=20, rising_puffs=12;
    std::array<NativeSmoke::Puff,ground_puffs+rising_puffs> puffs{};
    for(unsigned i=0;i<puffs.size();++i){
        const bool rising=i>=ground_puffs;
        const float angle=float(i)*2.39996323f;
        const float variation=float(i*7u%13u)/12.f;
        const Vec3 outward{std::cos(angle),std::sin(angle),0};
        const float radius=(rising?.30f:.68f)+variation*(rising?.70f:.80f);
        const auto point=landing_+(outward*radius+Vec3{0,0,rising?.26f:.14f})*scale;
        const auto velocity=outward*(rising?1.4f+variation:3.6f+1.8f*variation)+
            Vec3{0,0,rising?2.25f+1.2f*variation:.62f+.65f*variation};
        const float lifetime=(rising?.74f:.53f)+.15f*variation;
        puffs[i]={point,velocity*(seconds_per_tick*scale),rising?0x4649576b:0x594e6075,
            0x00323f4d,static_cast<int>(std::lround(lifetime/seconds_per_tick)),
            scale*(rising?680000.f+250000.f*variation:500000.f+220000.f*variation)};
    }
    return smoke_.emit(puffs.data(),static_cast<unsigned>(puffs.size()));
}

void NativeCaptureGag::tick(const Frame& frame, Target hero, unsigned hero_data) {
    if(!active())return;
    if(!owns_hero(hero) || !frame.alive || !valid(hero_data,0x22ac)) {
        release(true);
        return;
    }
    if(frame.paused)return;
    if(!frame.playing || !frame.equipped || !frame.grounded || !finite(frame.position) ||
       !std::isfinite(frame.yaw) || !std::isfinite(frame.seconds) || frame.seconds<=0 ||
       frame.seconds>.1f || length_squared(frame.position-landing_)>4.f) {
        release(true);
        return;
    }

    elapsed_+=frame.seconds;
    const float fall_start=profile_.lift_time+profile_.hang_time;
    const float fall=std::clamp((elapsed_-fall_start)/profile_.fall_time,0.f,1.f);
    const float lift=ease(elapsed_/profile_.lift_time);
    const float strained=lift*(1.f-fall);
    const float approach=.24f*lift+.10f*ease((elapsed_-profile_.lift_time)/profile_.hang_time)
        +.66f*ease(fall);
    const float speed=elapsed_<fall_start ? 10.f+26.f*lift : 36.f*(1.f-fall)*(1.f-fall);
    rotor_angle_=std::remainder(rotor_angle_+speed*frame.seconds,revolution);
    if(fall<1.f) {
        const auto pos=origin_+(landing_-origin_)*approach+
            Vec3{0,0,profile_.height*lift*(1.f-fall*fall)};
        const auto travel=unit(landing_-origin_);
        const float tip=.38f*std::sin(3.14159265f*fall);
        const auto lean=angles_+Vec3{-.10f*strained-travel.y*tip,
            .06f*strained*std::sin(17.f*elapsed_)+travel.x*tip,turn_*ease(elapsed_/fall_start)};
        place(enemy_,pos,lean);
        return;
    }

    const auto boss=enemy_;
    const auto native_origin=origin_, native_angles=angles_;
    place(boss,landing_,angles_+Vec3{0,0,turn_});
    release(false);
    // The original grounded death state clears HP, starts the death clip and
    // lets the game's control loop return to the active checkpoint.
    const auto previous_source=read<unsigned>(hero_data+0x2280);
    write(hero_data+0x2280,boss.address);
    state_(hero_dying_speed_limited,1);
    if(read<int>(hero_data+0x22a8)>0){
        if(read<unsigned>(hero_data+0x2280)==boss.address)write(hero_data+0x2280,previous_source);
        if(same_actor(boss))place(boss,native_origin,native_angles);
        api_->log(mod_,OPENRAC_LOG_WARN,"Snagglebeast gag: the native death state was refused");
        return;
    }
    unsigned dust=0;
    if(same_actor(boss)){
        if(rumble_)rumble_(boss.address);
        // Play the stock shockwave's impact sample without creating a second hit.
        if(sound_&&profile_.landing_sound_class)sound_(0,0,boss.address,int(profile_.landing_sound_class));
        dust=emit_impact_dust(frame.seconds);
    }
    impact_=true;
    if(trace_){
        char message[100];
        std::snprintf(message,sizeof message,"Snagglebeast gag: impact, native hero death, %u dust puffs",dust);
        api_->log(mod_,OPENRAC_LOG_INFO,message);
    }
}

void NativeCaptureGag::release(bool restore_pose) {
    if(same_actor(enemy_)) {
        if(restore_pose)place(enemy_,origin_,angles_);
        if(read<unsigned>(enemy_.address+0x94)==0)write(enemy_.address+0x94,collision_);
    }
    enemy_=hero_={};
    elapsed_=rotor_angle_=0;
}

bool NativeCaptureGag::take_impact() {return std::exchange(impact_,false);}

void NativeCaptureGag::reset() {
    release(true);
    seen_={};
    impact_=false;
}

}
