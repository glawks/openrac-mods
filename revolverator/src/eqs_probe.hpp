// SPDX-License-Identifier: GPL-3.0-or-later
// Experimental optional interop with glawks/expanded-quick-select (MIT API v1).
// Never loads another plugin or introduces a hard dependency on EQS.
#pragma once
#include <cstdint>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace revolverator::eqs_probe {
struct State {
    std::uint32_t size = sizeof(State);
    std::uint32_t page_count = 0;
    std::uint32_t gameplay_page = 0xffffffffu;
    std::uint32_t editor_page = 0xffffffffu;
    std::uint32_t flags = 0;
};
struct Api {
    std::uint32_t size;
    std::uint32_t version;
    int (*get_state)(State*);
    int (*get_slots)(std::uint32_t, void*);
};
static_assert(sizeof(State) == 20, "EQS state ABI mismatch");
static_assert(sizeof(Api) == sizeof(void*) * 2 + 8, "EQS API ABI mismatch");

inline bool get_state(State& out) noexcept {
#if defined(_WIN32)
    // GetModuleHandle never loads a DLL: non-EQS users keep the existing path.
    const HMODULE module = GetModuleHandleW(L"expanded_quick_select.dll");
    if (!module) return false;
    using Query = const Api* (*)(std::uint32_t);
    const auto query = reinterpret_cast<Query>(
        GetProcAddress(module, "openrac_expanded_quick_select_query"));
    if (!query) return false;
    const Api* api = query(1u);
    if (!api || api->version != 1u || api->size < sizeof(Api) || !api->get_state)
        return false;
    State candidate{};
    if (!api->get_state(&candidate) || candidate.page_count < 1u ||
        candidate.page_count > 8u) return false;
    out = candidate;
    return true;
#else
    // This Windows-only probe intentionally does not resolve ELF/Mach-O mods.
    (void)out;
    return false;
#endif
}
inline bool installed() noexcept { State state{}; return get_state(state); }
inline bool editor_secondary() noexcept {
    State state{};
    return get_state(state) && (state.flags & 2u) != 0 &&
           state.editor_page > 0u && state.editor_page < state.page_count;
}
inline bool gameplay_secondary() noexcept {
    State state{};
    return get_state(state) && (state.flags & 1u) != 0 &&
           state.gameplay_page > 0u && state.gameplay_page < state.page_count;
}
}  // namespace revolverator::eqs_probe
