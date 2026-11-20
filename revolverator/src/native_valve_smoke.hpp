// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#pragma once

#include "controller.hpp"
#include "memory.hpp"

namespace revolverator {

class NativeValveSmoke final : public Memory {
public:
    explicit NativeValveSmoke(const openrac_api* api);

    // Returns the number of puffs submitted to the native particle spawner.
    unsigned emit(Vec3 valve_world_point, float load, unsigned count);

private:
    using Spawn = void (*)(openrac_gaddr, openrac_gaddr, int, int, int, float);
    Spawn spawn_{};
    unsigned sequence_{};
};

}
