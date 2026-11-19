// SPDX-License-Identifier: GPL-3.0-or-later
// Run only in an isolated Gaspar fixture with an empty card.
#include "memory.hpp"
#include <array>
#include <cmath>
#include <cstdio>

namespace {
using revolverator::Memory;
struct Vec3 { float x, y, z; };
using Give = void(*)(int, int);
using Teleport = void(*)(unsigned, unsigned, int, int);
constexpr int gaspar = 9;
constexpr int pilot_helmet = 7;
constexpr unsigned pilot_helmet_class = 1290;
constexpr unsigned hero_base = 0x13f350, item_owned = 0x13d4c0;
constexpr unsigned pending = 0x20b8, held_item = 0x10b8;
const openrac_api* api;
openrac_mod* mod;
Give next_give;
Teleport teleport;
unsigned playable, ticks_since_pickup, pickup_calls;
bool positioned, finished, proper_grant;
int expected_hand;
unsigned placed_actor, watched_frames;

void log(const char* value){
    api->log(mod, OPENRAC_LOG_INFO, value);
}

bool pickup_owned(const Memory& mem) {
    return mem.read<unsigned char>(mem.global(item_owned)+pilot_helmet)!=0;
}

void observe_grant(int item, int flag) {
    if (!positioned || finished || api->loaded_level()!=gaspar || item!=pilot_helmet) {
        next_give(item, flag);
        return;
    }
    Memory mem(api);
    const auto hero=mem.global(hero_base);
    const auto before=mem.read<std::array<int, 3>>(hero+pending);
    const bool owned_before=pickup_owned(mem);
    next_give(item, flag);
    const auto after=mem.read<std::array<int, 3>>(hero+pending);
    const bool owned_after=pickup_owned(mem);
    ++pickup_calls;
    proper_grant=flag==1 && !owned_before && owned_after &&
        before[0]==after[0] && before[1]==after[1] && after[2]==pilot_helmet;
    char trace[300];
    std::snprintf(trace,sizeof trace,
        "GASPAR_HELMET grant item=%d flag=%d owned=%d->%d pending_before=%d,%d,%d pending_after=%d,%d,%d preserved=%d",
        item,flag,int(owned_before),int(owned_after),before[0],before[1],before[2],
        after[0],after[1],after[2],int(proper_grant));
    log(trace);
}

unsigned placed_helmet(const Memory& mem) {
    const auto first=mem.read<unsigned>(mem.global(0x15ff18));
    const auto last=mem.read<unsigned>(mem.global(0x15ff20));
    if (!Memory::valid(first,256) || !Memory::valid(last,256) || last<first ||
        last-first>4096*256) return 0;
    for (auto actor=first; actor<=last; actor+=256) {
        if (mem.read<unsigned char>(actor+0x20)==1 &&
            mem.read<unsigned short>(actor+0xa6)==pilot_helmet_class) return actor;
    }
    return 0;
}

void frame(void*) {
    if (finished || api->loaded_level()!=gaspar) return;
    Memory mem(api);
    const auto hero=mem.global(hero_base);
    const auto mode=mem.read<int>(mem.level(0x15f5c4));
    if (positioned && ++watched_frames<=600 && (watched_frames<5 || watched_frames%120==0)) {
        const auto position=mem.read<Vec3>(hero+0x80);
        char trace[220];
        std::snprintf(trace,sizeof trace,
            "GASPAR_HELMET tick=%u mode=%d actor_state=%u hp=%d owned=%d hero=%.2f,%.2f,%.2f",
            watched_frames,mode,unsigned(mem.read<unsigned char>(placed_actor+0x20)),
            mem.read<int>(hero+0x22a8),int(pickup_owned(mem)),position.x,position.y,position.z);
        log(trace);
    }
    if (mode!=0 ||
        !Memory::valid(hero,0x22ac) || mem.read<int>(hero+0x22a8)<=0) return;
    const auto body=mem.read<unsigned>(hero+0x2080);
    if (!Memory::valid(body,256) || mem.read<unsigned short>(body+0xa6)!=0 ||
        mem.read<unsigned char>(body+0x20)>=0x80) return;
    if (positioned) {
        if (!pickup_calls || ++ticks_since_pickup<120) return;
        const auto hand=mem.read<int>(hero+held_item);
        const auto equipment=mem.read<std::array<int, 3>>(mem.global(0x141660));
        const bool pass=proper_grant && pickup_calls==1 && pickup_owned(mem)
            && hand==expected_hand && equipment[2]==pilot_helmet;
        char trace[260];
        std::snprintf(trace,sizeof trace,
            "GASPAR_HELMET result pass=%d grants=%u playable_ticks=%u hand_before=%d hand_after=%d head=%d owned=%d",
            int(pass),pickup_calls,ticks_since_pickup,expected_hand,hand,equipment[2],
            int(pickup_owned(mem)));
        log(trace);
        finished=true;
        return;
    }
    if (++playable<90 || pickup_owned(mem)) return;
    const auto helmet=placed_helmet(mem);
    if (!helmet) return;
    const auto p=mem.read<Vec3>(helmet+0x10);
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return;

    expected_hand=mem.read<int>(hero+held_item);
    unsigned saved=0;
    const auto scratch=api->stack_push(32,&saved);
    if (!scratch) return;
    // The pickup floats one unit above the floor. Place Ratchet within its
    // native proximity radius; let the actual actor and cutscene grant it.
    const Vec3 position{p.x,p.y,p.z-1.f};
    mem.write(scratch,std::array<float,4>{position.x,position.y,position.z,1.f});
    mem.write(scratch+16,std::array<float,4>{0.f,0.f,0.f,0.f});
    positioned=true;
    placed_actor=helmet;
    teleport(scratch,scratch+16,0,1);
    api->stack_pop(saved);
    const auto old=mem.read<Vec3>(body+0x10);
    auto sphere=mem.read<Vec3>(body);
    sphere.x+=(position.x-old.x)*1024.f;
    sphere.y+=(position.y-old.y)*1024.f;
    sphere.z+=(position.z-old.z)*1024.f;
    mem.write(hero+0x80,position);
    mem.write(body+0x10,position);
    mem.write(body,sphere);
    char trace[200];
    std::snprintf(trace,sizeof trace,
        "GASPAR_HELMET approached placed_actor=%u class=%u hand=%d position=%.2f,%.2f,%.2f",
        helmet,pilot_helmet_class,expected_hand,p.x,p.y,p.z);
    log(trace);
}
}

extern "C" { OPENRAC_MOD_DEFINE("gaspar-helmet-probe","0.1.0") }
extern "C" OPENRAC_MOD_EXPORT int openrac_mod_load(const openrac_api* host,openrac_mod* self) {
    api=host;
    mod=self;
    if (!revolverator::RegionBindings::for_version(host->version)) return 1;
    teleport=reinterpret_cast<Teleport>(Memory(host).find_function("FUN_L00_00216f90"));
    if (!teleport) return 1;
    const int result=host->hook(self,"rac1.item.give","void(i32,i32)",
        reinterpret_cast<void*>(&observe_grant),reinterpret_cast<void**>(&next_give));
    return result ? result : host->on_frame(self,&frame,nullptr);
}
extern "C" OPENRAC_MOD_EXPORT void openrac_mod_unload() {}
