// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#pragma once

namespace revolverator {

struct ValveFrame {
    float scale=1.f;
    float load=0.f;
    unsigned puffs=0;
};

class ValveCycle final {
public:
    ValveFrame update(float seconds,float angular_speed,bool powered,bool paused);
    void reset();
private:
    float phase_{};
    float load_{};
    float scale_=1.f;
    float smoke_budget_{};
};

}
