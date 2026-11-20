// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#include "native_valve_smoke.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace revolverator {
namespace {

constexpr unsigned frame_delta_time = 0x0015ed7cu;
constexpr unsigned max_puffs_per_update = 3;

int smoke_color(float shade, float opacity) {
    const int gray = static_cast<int>(std::lround(shade));
    const int alpha = static_cast<int>(std::lround(opacity));
    return (alpha << 24) | (gray << 16) | (gray << 8) | gray;
}

}

NativeValveSmoke::NativeValveSmoke(const openrac_api* api)
    : Memory(api), smoke_(api) {}

unsigned NativeValveSmoke::emit(Vec3 valve_world_point, float load, unsigned count) {
    if (!count || !api_->memory || !finite(valve_world_point) || !std::isfinite(load)) {
        return 0;
    }

    const float intensity = std::clamp(load, 0.f, 1.f);
    count = std::min(count, max_puffs_per_update);
    // The Novalis smoke spawner takes two aligned vectors and owns the cloud's
    // upward acceleration, expansion and fade. Its PS2 color uses 0x80 for full alpha.
    const int initial_color = smoke_color(170.f - 128.f * intensity, 40.f + 76.f * intensity);
    const int final_color = smoke_color(113.f - 88.f * intensity, 0.f);
    // PAL can run at 60 Hz in OpenRAC. Match the timing used by live particles.
    const float frame_seconds = api_->relocate_data ? read<float>(global(frame_delta_time)) : 0.f;
    const float seconds_per_tick = frame_seconds >= 1.f / 120.f && frame_seconds <= 1.f / 30.f
        ? frame_seconds : 1.f / bindings().nominal_hz();
    const int lifetime = static_cast<int>(std::lround((.72f + .24f * intensity) / seconds_per_tick));
    const float final_size = 66000.f + 72000.f * intensity;
    const float lateral_speed = (.085f + .275f * intensity) * seconds_per_tick;
    const float upward_speed = (.51f + .24f * intensity) * seconds_per_tick;

    std::array<NativeSmoke::Puff, max_puffs_per_update> puffs{};
    for (unsigned i = 0; i < count; ++i) {
        // Rotate the lateral drift between puffs while keeping the source at the valve.
        const float phase = float((sequence_ + i) % 4096u) * 2.39996323f;
        puffs[i] = {valve_world_point,
            {std::cos(phase) * lateral_speed, std::sin(phase) * lateral_speed, upward_speed},
            initial_color, final_color, lifetime, final_size};
    }
    const auto emitted = smoke_.emit(puffs.data(), count);
    sequence_ += emitted;
    return emitted;
}

}
