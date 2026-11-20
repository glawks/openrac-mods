// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#pragma once
#include "controller.hpp"
#include "memory.hpp"

namespace revolverator {
class NativeCrates final : public Memory {
public:
    explicit NativeCrates(const openrac_api* api);
    bool enable();
    bool ready() const { return enabled_; }
    bool live_crate(Target crate) const;
    bool can_carry(Target crate) const;
    bool detach(Target crate);
    bool break_in_void(Target crate) const;
    using Update=void(*)(unsigned);
    unsigned nanotech_owner(unsigned orb) const;
    void update_carried_nanotech(unsigned orb,unsigned owner,Update next) const;
    // The native Hoven ankle-biter fall handler retires its actor below z=10.
    static constexpr float void_height=10.f;
private:
    using Break=void(*)(unsigned,int,unsigned);
    using Drop=void(*)(unsigned);
    Break break_{};
    Drop drop_{};
    bool enabled_{};
    bool valid_neighbour(unsigned actor,unsigned reciprocal,unsigned owner) const;
};
}
