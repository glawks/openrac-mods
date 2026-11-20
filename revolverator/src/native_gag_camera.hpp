// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#pragma once
#include "controller.hpp"
#include <openrac/openrac_mod.h>

namespace revolverator {

// An encounter can own one shot. The game continues to update its follow
// camera underneath it, and takes over again after the impact.
class NativeGagCamera final {
public:
    using Trace = std::optional<Vec3> (*)(const void*, Vec3 from, Vec3 to);
    NativeGagCamera(const openrac_api*, openrac_mod*, const void* scene, Trace);
    ~NativeGagCamera();
    void begin(Vec3 hero, Vec3 enemy, float yaw, float subject_height);
    void update(const Frame&, bool encounter_active, bool impact);
    void reset();

private:
    const openrac_api* api_;
    openrac_mod* mod_;
    const void* scene_;
    Trace trace_;
    openrac_camera entry_{};
    Vec3 eye_{}, focus_{}, impact_focus_{};
    float time_{}, since_impact_{};
    bool owns_{}, landed_{};

    bool read_camera(openrac_camera&) const;
};

}
