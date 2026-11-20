// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#include "native_valve_smoke.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace revolverator {
namespace {

constexpr unsigned frame_delta_time = 0x0015ed7cu;

int smoke_color(float shade, float opacity) {
    const int gray = static_cast<int>(std::lround(shade));
    const int alpha = static_cast<int>(std::lround(opacity));
    return (alpha << 24) | (gray << 16) | (gray << 8) | gray;
}

}

NativeValveSmoke::NativeValveSmoke(const openrac_api* api)
    : Memory(api), spawn_(reinterpret_cast<Spawn>(find_function("FUN_L00_0026ced0"))) {}

unsigned NativeValveSmoke::emit(Vec3 valve_world_point, float load, unsigned count) {
    if (!count || !spawn_ || !api_->memory || !api_->stack_push || !api_->stack_pop ||
        !finite(valve_world_point) || !std::isfinite(load)) {
        return 0;
    }

    const float intensity = std::clamp(load, 0.f, 1.f);
    count = std::min(count, 2u);
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
    // The SDK can allocate stack storage outside the original EE RAM range.
    auto* const buffer = api_->memory + stack;

    // The Novalis smoke spawner takes two aligned vectors and owns the cloud's
    // upward acceleration, expansion and fade. Its PS2 color uses 0x80 for full alpha.
    const int initial_color = smoke_color(128.f - 94.f * intensity, 40.f + 58.f * intensity);
    const int final_color = smoke_color(86.f - 66.f * intensity, 0.f);
    // PAL can run at 60 Hz in OpenRAC. Match the timing used by live particles.
    const float frame_seconds = api_->relocate_data ? read<float>(global(frame_delta_time)) : 0.f;
    const float seconds_per_tick = frame_seconds >= 1.f / 120.f && frame_seconds <= 1.f / 30.f
        ? frame_seconds : 1.f / bindings().nominal_hz();
    const int lifetime = static_cast<int>(std::lround((.46f + .18f * intensity) / seconds_per_tick));
    const float final_size = 36000.f + 42000.f * intensity;
    const float lateral_speed = (.055f + .205f * intensity) * seconds_per_tick;
    const float upward_speed = (.36f + .24f * intensity) * seconds_per_tick;

    const GuestVector position{valve_world_point.x, valve_world_point.y, valve_world_point.z, 1.f};
    std::memcpy(buffer, position.data(), vector_bytes);
    for (unsigned i = 0; i < count; ++i) {
        // Rotate the lateral drift between puffs while keeping the source at the valve.
        const float phase = float(sequence_++ % 4096u) * 2.39996323f;
        const GuestVector velocity{
            std::cos(phase) * lateral_speed,
            std::sin(phase) * lateral_speed,
            upward_speed,
            0.f,
        };
        std::memcpy(buffer + vector_bytes, velocity.data(), vector_bytes);
        spawn_(stack, stack + vector_bytes, initial_color, final_color, lifetime, final_size);
    }
    return count;
}

}
