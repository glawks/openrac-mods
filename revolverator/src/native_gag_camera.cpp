// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#include "native_gag_camera.hpp"
#include "arm_math.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace revolverator {
namespace {
constexpr float reveal_time=.51f, maximum_death_hold=2.2f;

float ease(float t) {
    t=std::clamp(t,0.f,1.f);
    return t*t*t*(10.f+t*(6.f*t-15.f));
}

Vec3 from_row(const float (&row)[3]) {return {row[0],row[1],row[2]};}

void to_row(float (&row)[3],Vec3 vector) {row[0]=vector.x;row[1]=vector.y;row[2]=vector.z;}

Vec3 mix(Vec3 from,Vec3 to,float weight) {return from+(to-from)*weight;}

Vec3 cross(Vec3 a,Vec3 b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}

openrac_camera shot(Vec3 eye,Vec3 forward) {
    openrac_camera camera{};
    camera.size=sizeof camera;
    forward=unit(forward);
    const auto left=unit(cross({0,0,1},forward));
    to_row(camera.eye,eye);
    to_row(camera.forward,forward);
    to_row(camera.left,left);
    to_row(camera.up,cross(forward,left));
    return camera;
}
}

NativeGagCamera::NativeGagCamera(const openrac_api* api,openrac_mod* mod,const void* scene,Trace trace)
    :api_(api),mod_(mod),scene_(scene),trace_(trace) {}

NativeGagCamera::~NativeGagCamera() {reset();}

bool NativeGagCamera::read_camera(openrac_camera& camera) const {
    camera.size=sizeof camera;
    return api_->api_minor>=12 &&
        api_->size>=offsetof(openrac_api,camera_set)+sizeof(api_->camera_set) &&
        api_->camera_view && api_->camera_set &&
        api_->camera_view(&camera)==OPENRAC_HOOK_OK &&
        finite(from_row(camera.eye)) && finite(from_row(camera.forward));
}

void NativeGagCamera::begin(Vec3 hero,Vec3 enemy,float yaw,float height) {
    if(owns_||!read_camera(entry_)||!std::isfinite(height)||height<=0.f)return;
    auto direction=enemy-hero;
    direction.z=0;
    const auto travel=length_squared(direction)>.01f?unit(direction):Vec3{std::cos(yaw),std::sin(yaw),0};
    const Vec3 side{-travel.y,travel.x,0};
    const float side_sign=dot(from_row(entry_.eye)-hero,side)>=0?1.f:-1.f;
    focus_=hero+travel*1.85f+Vec3{0,0,height*.56f};
    impact_focus_=hero+travel*.95f+Vec3{0,0,height*.47f};
    const Vec3 elevated_base=focus_-travel*(height*1.22f)+Vec3{0,0,height*.16f};
    const Vec3 offset=side*(side_sign*height*1.40f);
    const auto near_side=elevated_base+offset;
    const auto far_side=elevated_base-offset;
    // Pick a clear side before the reveal. Later movement of the boss cannot
    // turn the camera around, even when it crosses the player's position.
    if(!trace_||!trace_(scene_,focus_,near_side))eye_=near_side;
    else if(!trace_(scene_,focus_,far_side))eye_=far_side;
    else return;
    // An unrelated native arena camera may still be crossing the level.
    // Enter on the composed view if the old eye is outside the encounter.
    const auto native_eye=from_row(entry_.eye);
    const auto composed=shot(eye_,focus_-eye_);
    bool can_glide=length_squared(native_eye-hero)<=height*height*6.25f &&
        dot(from_row(entry_.forward),from_row(composed.forward))>-.25f;
    if(can_glide&&trace_){
        for(float weight:{.25f,.5f,.75f}){
            if(trace_(scene_,focus_,mix(native_eye,eye_,weight))){can_glide=false;break;}
        }
    }
    if(!can_glide)entry_=composed;
    owns_=api_->camera_set(mod_,&entry_)==OPENRAC_HOOK_OK;
}

void NativeGagCamera::update(const Frame& frame,bool encounter_active,bool impact) {
    if(!owns_)return;
    if((!encounter_active&&!impact&&!landed_)||(landed_&&frame.alive)){
        reset();
        return;
    }
    if(frame.paused)return;
    if(!frame.playing||!std::isfinite(frame.seconds)||frame.seconds<=0||frame.seconds>.1f){
        reset();
        return;
    }
    if(impact)landed_=true;
    else if(landed_)since_impact_+=frame.seconds;
    time_+=frame.seconds;
    // Through the game's death fade, so the checkpoint comes back on the
    // native camera without a visible jump to the native low-angle death view.
    if(since_impact_>=maximum_death_hold){reset();return;}

    const float reveal=ease(time_/reveal_time);
    const float land=ease(since_impact_/.32f);
    const float kick=landed_?.24f*std::sin(2.6f*since_impact_/.095f)*
        std::exp(-since_impact_/.053f):0.f;
    const auto focus=mix(focus_,impact_focus_,land);
    const auto eye=mix(from_row(entry_.eye),eye_,reveal)+Vec3{0,0,kick};
    const auto forward=mix(from_row(entry_.forward),unit(focus-eye),reveal);
    const auto camera=shot(eye,forward);
    if(api_->camera_set(mod_,&camera)!=OPENRAC_HOOK_OK)reset();
}

void NativeGagCamera::reset() {
    if(owns_)api_->camera_set(mod_,nullptr);
    owns_=landed_=false;
    time_=since_impact_=0;
}

}
