// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#pragma once
#include "controller.hpp"
#include "memory.hpp"
#include <array>

namespace revolverator {

class NativeSmoke final : public Memory {
public:
    struct Puff {
        Vec3 point, velocity_per_tick;
        int start_color, end_color, lifetime_ticks;
        float final_size;
    };

    explicit NativeSmoke(const openrac_api* api)
        : Memory(api), spawn_(reinterpret_cast<Spawn>(find_function("FUN_L00_0026ced0"))) {}

    // The native Type 22 emitter copies both vectors, then owns particle
    // allocation, rise, expansion and fade.
    unsigned emit(const Puff* puffs, unsigned count) const {
        if (!count || !spawn_ || !api_->memory || !api_->stack_push || !api_->stack_pop) return 0;
        using GuestVector = std::array<float, 4>;
        constexpr unsigned vector_bytes = sizeof(GuestVector);
        openrac_gaddr saved{};
        const auto stack = api_->stack_push(2 * vector_bytes, &saved);
        if (!stack) return 0;
        const struct StackFrame {
            const openrac_api* api;
            openrac_gaddr saved;
            ~StackFrame() { api->stack_pop(saved); }
        } frame{api_, saved};
        // The SDK's stack may be outside the original EE memory range.
        auto* const buffer = api_->memory + stack;
        for (unsigned i = 0; i < count; ++i) {
            const auto& puff = puffs[i];
            const auto& point = puff.point;
            const auto& velocity = puff.velocity_per_tick;
            const GuestVector position{point.x, point.y, point.z, 1.f};
            const GuestVector drift{velocity.x, velocity.y, velocity.z, 0.f};
            std::memcpy(buffer, position.data(), vector_bytes);
            std::memcpy(buffer + vector_bytes, drift.data(), vector_bytes);
            spawn_(stack, stack + vector_bytes, puff.start_color, puff.end_color,
                puff.lifetime_ticks, puff.final_size);
        }
        return count;
    }

private:
    using Spawn = void (*)(openrac_gaddr, openrac_gaddr, int, int, int, float);
    Spawn spawn_{};
};

}
