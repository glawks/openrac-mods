// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#include "native_contact_sparks.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace revolverator {
namespace {
constexpr unsigned particle_pool=0x1600ac;
constexpr unsigned particle_next=0x1600b0;
constexpr unsigned particle_count=0x1600b8;
constexpr unsigned slot_size=0x40;
constexpr unsigned slot_count=2048;
constexpr unsigned streak_type=19;
constexpr std::uint32_t alpha_mask=0xff000000u;
// Stock Gold Devastator spark RGB, obtained by swapping red and green in 0x007fff.
constexpr std::uint32_t golden_spark_rgb=0x00ff7fu;

Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Vec3 normalized(Vec3 v){return v*(1.f/std::sqrt(length_squared(v)));}
}
NativeContactSparks::NativeContactSparks(const openrac_api* api):Memory(api),
    spawn_(reinterpret_cast<Spawn>(find_function("FUN_L00_0026c6e8"))){}

void NativeContactSparks::spawn_golden(unsigned stack)const{
    const auto pool=read<unsigned>(global(particle_pool));
    const auto next_addr=global(particle_next);
    const auto count_addr=global(particle_count);
    const auto slot=read<unsigned>(next_addr);
    const auto previous_count=read<unsigned>(count_addr);

    // OpenRAC's PAL and NTSC allocators use the current cursor for the next
    // slot, then increment the live count. The streak spawner allocates once.
    spawn_(stack,stack+16);
    if(!valid(pool,slot_size*slot_count)||!valid(next_addr,4)||!valid(count_addr,4)||
        slot>=slot_count||previous_count>=slot_count||
        read<unsigned>(next_addr)==slot||read<unsigned>(count_addr)!=previous_count+1)return;

    const auto particle=pool+slot*slot_size;
    if(read<std::uint8_t>(particle)!=streak_type)return;
    // The streak stores its endpoint colors at +0x04 and +0x0c. Native Type19
    // updating fades alpha and keeps the two endpoint colors in sync.
    const auto leading=read<std::uint32_t>(particle+0x04);
    const auto trailing=read<std::uint32_t>(particle+0x0c);
    write(particle+0x04,(leading&alpha_mask)|golden_spark_rgb);
    write(particle+0x0c,(trailing&alpha_mask)|golden_spark_rgb);
}

unsigned NativeContactSparks::update(const Frame& frame,Target target,Vec3 point,Vec3 shaft,float angle,float speed,bool golden){
    const bool active=spawn_&&frame.playing&&frame.alive&&frame.equipped&&target&&
        finite(point)&&finite(shaft)&&length_squared(shaft)>.25f&&length_squared(shaft)<4.f&&std::isfinite(angle);
    const auto count=rate_.update(frame.seconds,active,frame.paused,target,speed);
    if(!count||!api_->stack_push||!api_->stack_pop)return 0;
    unsigned saved{};const auto stack=api_->stack_push(32,&saved);
    if(!stack){rate_.reset();return 0;}
    struct Pop{const openrac_api* api;unsigned saved;~Pop(){api->stack_pop(saved);}} pop{api_,saved};
    // The resident wrench-contact spawner copies both aligned vectors into
    // native type19 streaks. It owns allocation and their ~20-tick lifetime.
    // No native particle, model pointer or guest stack address is retained.
    shaft=normalized(shaft);
    const auto radial=normalized(cross(shaft,std::abs(shaft.z)<.9f?Vec3{0,0,1}:Vec3{1,0,0}));
    const auto tangent=cross(shaft,radial);
    const float intensity=std::clamp((speed-24.f)/18.f,0.f,1.f);
    for(unsigned i=0;i<count;++i){
        // Local deterministic dispersion avoids drawing from gameplay RNG.
        const float phase=std::remainder(angle+float(sequence_%4096u)*2.39996323f,6.283185307f);++sequence_;
        const auto outward=radial*std::cos(phase)+tangent*std::sin(phase);
        const auto velocity=cross(shaft,outward)*(.065f+.025f*intensity)+outward*.025f+shaft*.015f;
        const auto origin=point+outward*.012f;
        const std::array<float,4> position{origin.x,origin.y,origin.z,1},direction{velocity.x,velocity.y,velocity.z,0};
        std::memcpy(api_->memory+stack,position.data(),16);std::memcpy(api_->memory+stack+16,direction.data(),16);
        if(golden&&api_->relocate_data)spawn_golden(stack);
        else spawn_(stack,stack+16);
        ++emitted_;
    }
    return count;
}
}
