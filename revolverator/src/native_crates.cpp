// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#include "native_crates.hpp"
#include "damage_targets.hpp"
#include <cmath>

namespace revolverator {
namespace {
constexpr unsigned above_offset=0xa0,below_offset=0xa4,flags_offset=0xac;
constexpr unsigned respawn_offset=0xc8,support_offset=0xf0;
constexpr unsigned pushed=4;
constexpr unsigned nanotech_crate_class=501,nanotech_ball_class=806;
constexpr unsigned position_offset=0x10,state_offset=0x20,stamp_offset=0x38;
constexpr unsigned pvars_offset=0x78,class_offset=0xa6,up_offset=0xe0;
constexpr unsigned owner_offset=0x0c;
}

NativeCrates::NativeCrates(const openrac_api* api):Memory(api),
    break_(reinterpret_cast<Break>(find_function("FUN_L00_002d09b8"))),
    drop_(reinterpret_cast<Drop>(find_function("FUN_L00_002d0538"))) {}

bool NativeCrates::enable(){return enabled_=break_&&drop_;}

bool NativeCrates::live_crate(Target crate) const {
    return enabled_&&capturable_crate_class(crate.klass)&&valid(crate.address,256)&&valid(crate.pvars,256)&&
        read<unsigned short>(crate.address+0xa6)==crate.klass&&read<unsigned>(crate.address+0x38)==crate.stamp&&
        read<unsigned>(crate.address+0x78)==crate.pvars&&read<unsigned char>(crate.address+0x20)==1;
}

bool NativeCrates::valid_neighbour(unsigned actor,unsigned reciprocal,unsigned owner) const {
    if(!actor)return true;
    if(actor==owner||!valid(actor,256)||read<unsigned char>(actor+0x20)>=0x80||
       !destructible_crate_class(read<unsigned short>(actor+0xa6)))return false;
    const auto vars=read<unsigned>(actor+0x78);
    return valid(vars,256)&&read<unsigned>(vars+reciprocal)==owner;
}

bool NativeCrates::can_carry(Target crate) const {
    if(!live_crate(crate)||!read<unsigned>(crate.address+0x94)||read<unsigned>(crate.address+0x98)||
       read<unsigned char>(crate.pvars+respawn_offset)||read<unsigned>(crate.pvars+support_offset)||
       (read<unsigned>(crate.pvars+flags_offset)&2u))return false;
    const auto above=read<unsigned>(crate.pvars+above_offset),below=read<unsigned>(crate.pvars+below_offset);
    return (!above||above!=below)&&valid_neighbour(above,below_offset,crate.address)&&
        valid_neighbour(below,above_offset,crate.address);
}

bool NativeCrates::detach(Target crate) {
    if(!can_carry(crate))return false;
    const auto above=read<unsigned>(crate.pvars+above_offset),below=read<unsigned>(crate.pvars+below_offset);
    // Splice the same reciprocal links as the native break routine. The
    // remaining crates keep their updates and settle under native gravity.
    if(above){
        const auto vars=read<unsigned>(above+0x78);
        write(vars+below_offset,below);
        write(vars+flags_offset,read<unsigned>(vars+flags_offset)|pushed);
    }
    if(below){
        const auto vars=read<unsigned>(below+0x78);
        write(vars+above_offset,above);
        write(vars+flags_offset,read<unsigned>(vars+flags_offset)|pushed);
    }
    write(crate.pvars+above_offset,0u);
    write(crate.pvars+below_offset,0u);
    write(crate.pvars+flags_offset,read<unsigned>(crate.pvars+flags_offset)|pushed);
    return true;
}

bool NativeCrates::break_in_void(Target crate) const {
    if(!live_crate(crate)||read<unsigned>(crate.pvars+above_offset)||read<unsigned>(crate.pvars+below_offset))return false;
    break_(crate.address,0,0);
    if(read<unsigned char>(crate.address+0x20)==3&&read<unsigned>(crate.address+0x78)==crate.pvars)
        drop_(crate.address);
    return true;
}

unsigned NativeCrates::nanotech_owner(unsigned orb) const {
    // NanoTechBallUpdate shares this layout on PAL and NTSC-U. States 0 and
    // 1 initialize and follow the owner. Detached pickups own their motion.
    if(!enabled_||!valid(orb,256)||read<unsigned short>(orb+class_offset)!=nanotech_ball_class||
       read<unsigned char>(orb+state_offset)>1)return 0;
    const auto vars=read<unsigned>(orb+pvars_offset);
    if(!valid(vars,128))return 0;
    const auto owner=read<unsigned>(vars+owner_offset);
    if(!valid(owner,256)||read<unsigned short>(owner+class_offset)!=nanotech_crate_class||
       read<unsigned char>(owner+state_offset)!=1)return 0;
    return owner;
}

void NativeCrates::update_carried_nanotech(unsigned orb,unsigned owner,Update next) const {
    const auto at=owner+position_offset;
    const auto position=read<Vec3>(at);
    const auto up=read<Vec3>(owner+up_offset);
    const float squared=length_squared(up);
    if(!finite(position)||!std::isfinite(squared)||squared<.99f||squared>1.01f){
        next(orb);
        return;
    }

    // The stock update samples the owner origin, then uses a world-Z offset
    // of 0.5 for the ball and its particle slots. Feed it a sampled origin that
    // puts that anchor on the same local axis as the rotated crate model.
    const auto sampled=position+(up-Vec3{0,0,1})*.5f;
    if(sampled.x==position.x&&sampled.y==position.y&&sampled.z==position.z){
        next(orb);
        return;
    }
    const struct RestorePosition {
        const Memory& memory;
        unsigned owner, pvars, stamp;
        Vec3 before, sampled;
        ~RestorePosition() {
            if(memory.read<unsigned short>(owner+class_offset)!=nanotech_crate_class||
               memory.read<unsigned>(owner+pvars_offset)!=pvars||
               memory.read<unsigned>(owner+stamp_offset)!=stamp)return;
            const auto current=memory.read<Vec3>(owner+position_offset);
            if(current.x==sampled.x&&current.y==sampled.y&&current.z==sampled.z)
                memory.write(owner+position_offset,before);
        }
    } restore{*this,owner,read<unsigned>(owner+pvars_offset),read<unsigned>(owner+stamp_offset),position,sampled};
    write(at,sampled);
    next(orb);
}
}
