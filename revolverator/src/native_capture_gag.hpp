// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#pragma once
#include "controller.hpp"
#include "memory.hpp"

namespace revolverator {

// Captures that play a scripted beat own their actor separately from normal
// thrown enemies. The profile identifies the encounter and its choreography.
struct CaptureGagProfile {
    int level;
    unsigned enemy_class;
    unsigned first_state, last_state;
    unsigned health_offset;
    float reach, height, lift_time, hang_time, fall_time;
    const char* landing_rumble;
    unsigned landing_sound_class;
};

inline constexpr CaptureGagProfile snagglebeast_crush{
    7, 1106, 1, 7, 0x20, 4.8f, .92f, .24f, .22f, .24f, "FUN_L07_003120d8", 0x416
};

class NativeCaptureGag final : public Memory {
public:
    NativeCaptureGag(const openrac_api*, openrac_mod*, const CaptureGagProfile&);
    ~NativeCaptureGag();
    void observe(Target actor);
    Target candidate(const Frame&) const;
    bool start(const Frame&, Target hero, Target enemy);
    void tick(const Frame&, Target hero, unsigned hero_data);
    bool owns(unsigned actor) const;
    bool owns_hero(Target actor) const;
    bool active() const { return bool(enemy_); }
    float rotor_angle() const { return rotor_angle_; }
    void reset();

private:
    const CaptureGagProfile& profile_;
    openrac_mod* mod_;
    using HeroState = int (*)(int,int);
    using Basis = void (*)(unsigned,unsigned);
    using Rumble = void (*)(unsigned);
    using PlayClassSound = int (*)(int,int,unsigned,int);
    HeroState state_;
    Basis basis_;
    Rumble rumble_;
    PlayClassSound sound_;
    Target seen_{}, enemy_{}, hero_{};
    Vec3 origin_{}, landing_{}, angles_{}, bounds_offset_{};
    unsigned collision_{};
    float elapsed_{}, rotor_angle_{};
    bool trace_{};

    bool same_actor(Target) const;
    void place(Target actor, Vec3 position, Vec3 angles);
    void release(bool restore_pose);
};

}
