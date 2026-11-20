// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 noire342
#include "native_capture_gag.hpp"
#include "native_gag_camera.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace revolverator;
namespace {
constexpr unsigned hero=0x20000, enemy=0x21000, vars=0x22000, hero_data=0x25000;
constexpr unsigned collision=0x28000;
constexpr unsigned particle_stack=0x2a000;

void check(bool ok, const char* reason) {
    if(ok)return;
    std::fprintf(stderr,"Capture gag: %s\n",reason);
    std::exit(1);
}

struct Fixture;
Fixture* current{};
struct Fixture {
    std::vector<unsigned char> bytes=std::vector<unsigned char>(0x30000);
    openrac_api api{};
    int level=7, deaths=0, sounds=0, shakes=0;
    int camera_sets=0,camera_releases=0;
    unsigned smoke_puffs=0,rising_puffs=0;
    openrac_camera last_camera{};
    bool camera_busy=false;
    Frame frame{1.f/60.f,1,true,true,false,true,true,true,{0,0,36},0};
    Target body{hero,0,0,15}, boss{enemy,vars,1106,24};

    template<class T>void put(unsigned at,T value){std::memcpy(bytes.data()+at,&value,sizeof(T));}
    template<class T>T get(unsigned at)const{T value;std::memcpy(&value,bytes.data()+at,sizeof(T));return value;}

    static void basis(unsigned,unsigned){}
    static int state(int value,int animate){
        check(value==0x80&&animate==1,"grounded death goes through the native state");
        check(current->get<unsigned>(enemy+0x94)==collision,"collision is restored before death");
        check(current->get<unsigned>(hero_data+0x2280)==enemy,"the native death is attributed to the boss");
        ++current->deaths;
        current->put(hero_data+0x22a8,0);
        return 1;
    }
    static void rumble(unsigned actor){check(actor==enemy,"camera shake belongs to the encounter");++current->shakes;}
    static int sound(int id,int flags,unsigned actor,int klass){
        check(!id&&!flags&&actor==enemy&&klass==0x416,"the impact sound is emitted without a shockwave actor");
        return ++current->sounds;
    }
    static void smoke(unsigned point,unsigned velocity,int color,int fade,int lifetime,float size){
        check(current->deaths==1,"dust is emitted only after the successful native crush");
        const auto position=current->get<Vec3>(point);
        const auto drift=current->get<Vec3>(velocity);
        check(point==particle_stack&&velocity==point+16&&lifetime>20&&size>400000.f,
            "impact uses large bounded native smoke puffs");
        check(length_squared(position-Vec3{.55f,0,36})<4.2f&&drift.z>0.f&&
            color>0&&fade>0&&fade<0x1000000,"dust starts at the landing point and fades upward");
        ++current->smoke_puffs;
        if(drift.z>.03f)++current->rising_puffs;
    }
    static void* function(const char* name){
        if(std::strcmp(name,"FUN_001fa030")==0)return reinterpret_cast<void*>(&basis);
        if(std::strcmp(name,"FUN_L00_002223f8")==0)return reinterpret_cast<void*>(&state);
        if(std::strcmp(name,"FUN_L07_003120d8")==0)return reinterpret_cast<void*>(&rumble);
        if(std::strcmp(name,"FUN_L00_0028dc90")==0)return reinterpret_cast<void*>(&sound);
        if(std::strcmp(name,"FUN_L00_0026ced0")==0)return reinterpret_cast<void*>(&smoke);
        return nullptr;
    }
    Fixture(){
        current=this;
        api.memory=bytes.data();api.function=function;
        api.loaded_level=+[]{return current->level;};
        api.setting_bool=+[](openrac_mod*,const char*)->int{return 0;};
        api.log=+[](openrac_mod*,int,const char*){};
        api.stack_push=+[](unsigned size,unsigned* previous)->unsigned{
            check(size==32,"smoke batches reuse the same pair of aligned vectors");
            *previous=particle_stack+32;
            return particle_stack;
        };
        api.stack_pop=+[](unsigned previous){
            check(previous==particle_stack+32,"smoke releases its stack");
        };
        api.size=sizeof api;api.api_minor=12;
        api.camera_view=+[](openrac_camera* out)->int{
            *out={sizeof(openrac_camera),{-8,0,38},{1,0,0},{0,1,0},{0,0,1}};
            return OPENRAC_HOOK_OK;
        };
        api.camera_set=+[](openrac_mod*,const openrac_camera* view)->int{
            if(current->camera_busy)return OPENRAC_HOOK_TAKEN;
            if(view){++current->camera_sets;current->last_camera=*view;}
            else ++current->camera_releases;
            return OPENRAC_HOOK_OK;
        };
        put(hero+0x38,body.stamp);
        put(enemy+0x38,boss.stamp);put(enemy+0x78,vars);put<unsigned short>(enemy+0xa6,1106);
        put<unsigned char>(enemy+0x20,1);put(enemy+0x94,collision);
        put(enemy+0x10,Vec3{4,0,36});put(enemy,Vec3{5,0,37}*1024.f);
        put(vars+0x20,12.f);put(hero_data+0x22a8,4);
    }
};
}

int main(){
    Fixture f;
    NativeCaptureGag gag(&f.api,nullptr,snagglebeast_crush);
    gag.observe(f.boss);
    f.level=1;
    check(!gag.candidate(f.frame),"never intercept any encounter outside Umbris");
    f.level=7;
    f.put<unsigned char>(enemy+0x20,8);
    check(!gag.candidate(f.frame),"ignore the native rope attack while it owns Ratchet");
    f.put<unsigned char>(enemy+0x20,1);
    f.frame.yaw=3.14159265f;
    check(!gag.candidate(f.frame),"the boss must be within the front cone");
    f.frame.yaw=0;

    check(gag.start(f.frame,f.body,f.boss)&&gag.owns(enemy),"the live boss is leased");
    for(int i=0;i<16;++i)gag.tick(f.frame,f.body,hero_data);
    const auto lifted=f.get<Vec3>(enemy+0x10);
    check(lifted.z>36.85f&&f.deaths==0,"the actual actor lifts before the hero dies");
    f.frame.playing=f.frame.equipped=false;f.frame.paused=true;
    for(int i=0;i<60;++i)gag.tick(f.frame,f.body,hero_data);
    check(gag.active()&&f.get<Vec3>(enemy+0x10).z==lifted.z&&f.deaths==0,
        "pause preserves the lease and holds the exact frame");
    f.frame.paused=false;f.frame.playing=true;
    gag.tick(f.frame,f.body,hero_data);
    check(!gag.active()&&f.get<unsigned>(enemy+0x94)==collision&&f.get<Vec3>(enemy+0x10).x==4&&
        f.deaths==0&&f.smoke_puffs==0,
        "unequipping restores the original boss and causes no damage");

    f.frame.equipped=true;
    check(gag.start(f.frame,f.body,f.boss),"a cancelled incident can be attempted again");
    f.frame.primary=false;
    for(int i=0;i<58;++i)gag.tick(f.frame,f.body,hero_data);
    check(f.deaths==1&&f.sounds==1&&f.shakes==1&&!gag.active(),"the completed fall produces exactly one impact");
    check(f.smoke_puffs==32&&f.rising_puffs==12,"impact produces a low dust wave and a rising cloud");
    const auto landed=f.get<Vec3>(enemy+0x10);
    check(std::abs(landed.x-.55f)<.001f&&landed.z==36&&f.get<float>(vars+0x20)==12.f,
        "the living boss lands in front of Ratchet with its original health");
    check(std::abs(f.get<Vec3>(enemy).x/1024.f+.45f)<.001f,
        "the native collision sphere turns with the boss at the landing pose");
    check(gag.take_impact()&&!gag.take_impact(),"the camera consumes one successful impact cue");

    f.frame.yaw=0;
    check(gag.start(f.frame,f.body,f.boss),"the fixture can create a separate incident");
    f.put(enemy+0x38,99u);
    f.put(enemy+0x94,0x29000u);
    gag.tick(f.frame,f.body,hero_data);
    check(f.get<unsigned>(enemy+0x94)==0x29000&&!gag.active()&&f.deaths==1,
        "a reused enemy slot does not inherit this incident or receive writes");

    NativeGagCamera camera(&f.api,nullptr,nullptr,nullptr);
    f.camera_busy=true;
    camera.begin(f.frame.position,{4,0,36},0,8.7f);
    camera.reset();
    check(!f.camera_sets&&!f.camera_releases,"a busy camera is never taken or released");
    f.camera_busy=false;
    camera.begin(f.frame.position,{4,0,36},0,8.7f);
    f.frame.paused=true;
    camera.update(f.frame,false,false);
    check(f.camera_sets==1&&f.camera_releases==1,"a cancelled shot relinquishes the camera in pause");
    f.frame.paused=false;
    camera.begin(f.frame.position,{4,0,36},0,8.7f);
    for(int i=0;i<56;++i)camera.update(f.frame,true,false);
    const auto before_impact=f.last_camera;
    camera.update(f.frame,false,true);
    check(f.last_camera.forward[2]<before_impact.forward[2]-.10f &&
        std::abs(f.last_camera.left[2])>.04f,"the camera kicks downward and rolls on the impact frame");
    f.frame.alive=false;
    for(int i=0;i<30;++i)camera.update(f.frame,false,false);
    check(std::abs(f.last_camera.left[2])>.007f,"the impact is still felt half a second later");
    for(int i=0;i<60;++i)camera.update(f.frame,false,false);
    check(std::abs(f.last_camera.left[2])<.001f,"the roll settles before the return to gameplay");
    check(f.camera_releases==1,"a successful shot keeps its camera through the death fade");
    f.frame.alive=true;
    camera.update(f.frame,false,false);
    check(f.camera_releases==2,"the respawn releases the shot to the native camera");
}
