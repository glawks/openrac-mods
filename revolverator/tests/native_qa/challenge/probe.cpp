// SPDX-License-Identifier: GPL-3.0-or-later
// A private PAL fixture. Never load this probe with a personal memory card.
#include "memory.hpp"
#include "revolverator_api.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdio>

namespace {
using revolverator::Memory;
using Slots = std::array<int,8>;
using Equipment = std::array<int,7>;
using Challenge = void(*)(int);
using Freeze = void(*)(int,int,int);
enum class Step { Load, WaitForLoad, Confirm, Observe, Done };
constexpr int novalis=1, veldin=0, retained_weapon=10, hoverboard=30;
constexpr unsigned hero=0x13f350, card=0x13d290, item_owned=0x13d4c0;
constexpr unsigned favourites=0x141ea0, equipment=0x141660;
constexpr unsigned active_level=0x15ed84, generation=0x15ee20;
constexpr unsigned expected_ammo=7, expected_xp=117;
const openrac_api* api;
openrac_mod* self;
const revolverator_api_v3* revo;
Challenge next;
Step step=Step::Load;
bool candidate;
bool skip_intro;
unsigned frames, playable, settled, calls, checks, failures;
unsigned confirm_frame, call_frame, generation_before;

void log(const char* text) {
    api->log(self,OPENRAC_LOG_INFO,text);
}
void check(const char* name,bool pass) {
    ++checks;
    if(!pass)++failures;
    char text[240];
    std::snprintf(text,sizeof text,"CHALLENGE_QA check=%s pass=%d frame=%u",name,int(pass),frames);
    log(text);
}
void finish() {
    step=Step::Done;
    char text[180];
    std::snprintf(text,sizeof text,
        "CHALLENGE_QA result pass=%d checks=%u failures=%u calls=%u level=%d playable=%u",
        int(failures==0),checks,failures,calls,api->loaded_level(),settled);
    log(text);
}
bool custom_state() {
    revolverator_inventory_v2 item{};
    revolverator_upgrade_v3 upgrade{};
    revolverator_quick_v2 quick{};
    item.size=sizeof item;upgrade.size=sizeof upgrade;quick.size=sizeof quick;
    if(!revo||!revo->get_inventory(REV_REVOLVERATOR_KEY,&item)||
       !revo->get_upgrade(&upgrade)||!revo->get_quick(&quick)) return false;
    char text[190];
    std::snprintf(text,sizeof text,
        "CHALLENGE_QA custom owned=%u ammo=%u xp=%u golden=%u favourite=%u stock_favourites=%u,%u",
        item.owned,item.ammunition,item.xp,upgrade.golden,quick.keys[0],quick.keys[1],quick.keys[2]);
    log(text);
    return item.owned==1&&item.ammunition==expected_ammo&&item.xp==expected_xp&&
        upgrade.golden==1&&quick.count==8&&quick.keys[0]==REV_REVOLVERATOR_KEY;
}
void resolve_revo() {
    if(revo)return;
    const auto library=GetModuleHandleW(L"revolverator.dll");
    const auto query=library?reinterpret_cast<revolverator_query_fn>(
        GetProcAddress(library,"openrac_revolverator_query")):nullptr;
    const auto version=query?query(3):nullptr;
    if(version&&version->version==3&&version->size>=sizeof(revolverator_api_v3))
        revo=reinterpret_cast<const revolverator_api_v3*>(version);
}
struct Stock {
    int level;
    unsigned generation;
    Slots slots;
    Equipment equipped;
    std::array<int,3> pending;
    bool board;
};
Stock stock(const Memory& mem) {
    return {mem.read<int>(mem.global(active_level)),mem.read<unsigned>(mem.global(generation)),
        mem.read<Slots>(mem.global(favourites)),mem.read<Equipment>(mem.global(equipment)),
        mem.read<std::array<int,3>>(mem.global(hero)+0x20b8),
        mem.read<unsigned char>(mem.global(item_owned)+hoverboard)!=0};
}
void describe(const char* event,const Stock& state) {
    char text[390];
    const auto& q=state.slots;
    const auto& e=state.equipped;
    const auto& p=state.pending;
    std::snprintf(text,sizeof text,
        "CHALLENGE_QA %s level=%d generation=%u quick=%d,%d,%d,%d,%d,%d,%d,%d equipment=%d,%d,%d,%d,%d,%d,%d pending=%d,%d,%d hoverboard=%d",
        event,state.level,state.generation,q[0],q[1],q[2],q[3],q[4],q[5],q[6],q[7],
        e[0],e[1],e[2],e[3],e[4],e[5],e[6],p[0],p[1],p[2],int(state.board));
    log(text);
}
void challenge(int slot) {
    if(step!=Step::Confirm) {
        next(slot);
        return;
    }
    ++calls;
    call_frame=frames;
    Memory mem(api);
    const auto before=stock(mem);
    describe("before_native_chain",before);
    next(slot);
    const auto after=stock(mem);
    describe("after_native_chain",after);
    check("native_no_write_branch",slot==-1);
    check("native_advances_campaign_generation",before.level==novalis&&
        before.generation==generation_before&&after.generation==generation_before+1);
    check("native_filters_removed_item_and_keeps_weapon",before.board&&
        before.slots[1]==retained_weapon&&before.slots[2]==hoverboard&&
        !after.board&&after.slots[1]==retained_weapon&&after.slots[2]==0);
    check("native_equipment_slots_reset",after.equipped==Equipment{}&&after.pending==std::array<int,3>{});
    step=Step::Observe;
}
void confirm(void*,int port,std::uint8_t* report,int size) {
    if(port!=0||!report||size<2)return;
    if(step==Step::Confirm) {
        const bool pulse=frames-confirm_frame>=60&&(frames-confirm_frame)%16<4;
        report[1]=std::uint8_t(pulse?report[1]&~0x40u:report[1]|0x40u);
        if(size>12)report[12]=pulse?255:0;
    } else if(step==Step::Observe&&skip_intro&&frames%16<4) {
        // The port's normal Start-to-skip behavior finishes the Veldin intro.
        report[0]=std::uint8_t(report[0]&~0x08u);
    }
}
bool is_playable(const Memory& mem) {
    const auto player=mem.global(hero);
    const auto body=mem.read<unsigned>(player+0x2080);
    return mem.read<int>(mem.level(0x15f5c4))==0&&mem.read<int>(player+0x22a8)>0&&
        Memory::valid(body,256)&&mem.read<unsigned short>(body+0xa6)==0&&
        mem.read<unsigned char>(body+0x20)<0x80;
}
void frame(void*) {
    if(step==Step::Done)return;
    ++frames;
    if(frames>=1900) {
        char text[144];
        std::snprintf(text,sizeof text,"CHALLENGE_QA timeout step=%u level=%d calls=%u",
            unsigned(step),api->loaded_level(),calls);
        log(text);
        check("fixture_completes_before_deadline",false);
        finish();
        return;
    }
    if(step==Step::Confirm)return;
    if(step==Step::Observe) {
        if(api->loaded_level()!=veldin)return;
        Memory mem(api);
        const int mode=mem.read<int>(mem.level(0x15f5c4));
        skip_intro=mode==2;
        if(frames%120==0) {
            char text[220];
            const auto player=mem.global(hero);
            const auto body=mem.read<unsigned>(player+0x2080);
            std::snprintf(text,sizeof text,
                "CHALLENGE_QA veldin_frame=%u mode=%d health=%d body=%x class=%u state=%u held=%d",
                frames,mode,mem.read<int>(player+0x22a8),body,
                unsigned(mem.read<unsigned short>(body+0xa6)),unsigned(mem.read<unsigned char>(body+0x20)),
                mem.read<int>(player+0x10b8));
            log(text);
        }
        if(!is_playable(mem)) {
            settled=0;
            return;
        }
        if(++settled<120)return;
        const auto after=stock(mem);
        describe("settled_veldin",after);
        check("actual_veldin_playable_for_120_frames",calls==1&&frames>call_frame);
        check("stock_equipment_and_quick_slots_on_veldin",after.level==veldin&&
            after.generation==generation_before+1&&after.equipped==Equipment{retained_weapon}&&
            after.pending==std::array<int,3>{}&&mem.read<int>(mem.global(hero)+0x10b8)==8&&
            after.slots[1]==retained_weapon&&after.slots[2]==0&&!after.board);
        if(candidate)check("custom_gold_ammo_xp_and_favourite_retained_on_veldin",custom_state()&&
            after.slots[0]==REV_NATIVE_WALLOPER_KEY);
        else check("stock_baseline_has_no_custom_favourite",after.slots[0]==0);
        finish();
        return;
    }
    if(api->loaded_level()!=novalis)return;
    Memory mem(api);
    if(!is_playable(mem))return;
    resolve_revo();
    const auto mc=mem.global(card);
    const auto& layout=mem.bindings().memcard();
    if(mem.read<int>(mc+layout.operation)>=3||mem.read<int>(mc+layout.pending_operation)>=0)return;
    ++playable;
    if(step==Step::Load) {
        if(playable<60)return;
        check("exact_candidate_presence",candidate==(revo!=nullptr));
        mem.write(mc+layout.active_card,0);
        mem.write(mc+layout.save_index,0);
        mem.write(mc+layout.pending_operation,13);
        mem.write(mc+layout.pending_card,0);
        mem.write(mc+layout.error,0);
        step=Step::WaitForLoad;
        playable=0;
        return;
    }
    if(playable<120)return;
    generation_before=mem.read<unsigned>(mem.global(generation));
    check("prior_generated_native_card_loaded",generation_before==1&&
        mem.read<int>(mem.global(0x15ed98))==10058);
    if(candidate)check("prior_golden_progress_loaded",custom_state());
    auto freeze=reinterpret_cast<Freeze>(api->function("func_001FBC80"));
    if(!freeze)freeze=reinterpret_cast<Freeze>(api->function("mode_freeze_init"));
    if(!freeze)freeze=reinterpret_cast<Freeze>(api->function_at(0x001fbc80));
    check("stock_pal_challenge_confirmation_function",freeze!=nullptr);
    if(!freeze||failures){finish();return;}
    // Keep the saved custom favourite. Exercise the same retained native
    // weapon and storyline item in both the stock and candidate processes.
    for(const auto item:{retained_weapon,hoverboard})
        mem.write<unsigned char>(mem.global(item_owned)+item,1);
    const auto quick=mem.global(favourites);
    mem.write(quick+sizeof(int),retained_weapon);
    mem.write(quick+2*sizeof(int),hoverboard);
    describe("seeded_stock_favourites",stock(mem));
    confirm_frame=frames;
    step=Step::Confirm;
    freeze(7,0,0);
}
}

extern "C" { OPENRAC_MOD_DEFINE("challenge-mode-probe","0.1.0") }
extern "C" OPENRAC_MOD_EXPORT int openrac_mod_load(const openrac_api* host,openrac_mod* mod) {
    api=host;self=mod;
    if(!api->version||std::strcmp(api->version,"pal"))return 1;
    candidate=api->setting_bool(self,"candidate")!=0;
    const int hook=api->hook(self,"rac1.save.new_game_plus","void(i32)",
        reinterpret_cast<void*>(&challenge),reinterpret_cast<void**>(&next));
    if(hook)return hook;
    const int input=api->on_pad(self,&confirm,nullptr);
    return input?input:api->on_frame(self,&frame,nullptr);
}
extern "C" OPENRAC_MOD_EXPORT void openrac_mod_unload() {}
